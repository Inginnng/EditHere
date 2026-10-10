"""Drive real NSIS pages on a private, never activated Windows desktop.

The compiled production installer runs against its guarded file/registry fixture.
Win32 messages operate only on that fixture's own windows. No source callback is
replaced and no input is sent to the user's desktop. Page snapshots and receipts
are retained so failures are inspectable.
"""
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

if sys.platform == "win32":
    import winreg

ROOT = Path(__file__).resolve().parents[1]
# See installer_transaction_test.py: 8.3 short names and long names are the same
# directory but different strings, and only some Windows APIs expand them.
tempfile.tempdir = os.path.realpath(tempfile.gettempdir())


class PrivateDesktop:
    """Own only an isolated installer process and its desktop handles."""
    def __init__(self):
        self.user = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.name = "EditHereInstallerTest_" + uuid.uuid4().hex
        self.user.CreateDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p,
                                            wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
        self.user.CreateDesktopW.restype = wintypes.HANDLE
        self.user.CloseDesktop.argtypes = [wintypes.HANDLE]
        self.desktop = self.user.CreateDesktopW(self.name, None, None, 0, 0x1FF, None)
        if not self.desktop:
            raise ctypes.WinError(ctypes.get_last_error())
        self.callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        self.user.EnumDesktopWindows.argtypes = [wintypes.HANDLE, self.callback_type, wintypes.LPARAM]
        self.user.EnumChildWindows.argtypes = [wintypes.HWND, self.callback_type, wintypes.LPARAM]
        self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        self.user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        self.user.GetDlgCtrlID.argtypes = [wintypes.HWND]
        self.user.IsWindowVisible.argtypes = [wintypes.HWND]
        self.user.IsWindowEnabled.argtypes = [wintypes.HWND]
        self.user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
        self.user.GetDlgItem.restype = wintypes.HWND
        self.user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        self.user.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM,
                                                 wintypes.LPARAM, wintypes.UINT, wintypes.UINT,
                                                 ctypes.POINTER(ctypes.c_size_t)]
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
        self.process = None
        self.pid = None

    def launch(self, executable, arguments):
        class StartupInfo(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("reserved", wintypes.LPWSTR),
                        ("desktop", wintypes.LPWSTR), ("title", wintypes.LPWSTR),
                        ("x", wintypes.DWORD), ("y", wintypes.DWORD), ("width", wintypes.DWORD),
                        ("height", wintypes.DWORD), ("xchars", wintypes.DWORD), ("ychars", wintypes.DWORD),
                        ("fill", wintypes.DWORD), ("flags", wintypes.DWORD), ("show", wintypes.WORD),
                        ("reserved_count", wintypes.WORD), ("reserved_bytes", ctypes.c_void_p),
                        ("stdin", wintypes.HANDLE), ("stdout", wintypes.HANDLE), ("stderr", wintypes.HANDLE)]
        class ProcessInfo(ctypes.Structure):
            _fields_ = [("process", wintypes.HANDLE), ("thread", wintypes.HANDLE),
                        ("pid", wintypes.DWORD), ("tid", wintypes.DWORD)]
        info = StartupInfo()
        info.cb = ctypes.sizeof(info)
        # CreateDesktop belongs to the caller's current window station. A
        # desktop-only name also works for CI runners hosted in service stations.
        info.desktop = self.name
        info.flags = 0x80  # STARTF_FORCEOFFFEEDBACK: do not alter the user's cursor.
        process = ProcessInfo()
        self.kernel.CreateProcessW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p,
                                              ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD,
                                              ctypes.c_void_p, wintypes.LPCWSTR,
                                              ctypes.POINTER(StartupInfo), ctypes.POINTER(ProcessInfo)]
        # NSIS /D= consumes the unquoted remainder of the native command line.
        # CreateProcess receives it directly; no shell interprets these paths.
        ordinary = [argument for argument in arguments if not argument.startswith("/D=")]
        directory = [argument for argument in arguments if argument.startswith("/D=")]
        command = ctypes.create_unicode_buffer(subprocess.list2cmdline([str(executable), *ordinary])
                                               + (" " + directory[-1] if directory else ""))
        if not self.kernel.CreateProcessW(str(executable), command, None, None, False, 0,
                                          None, str(executable.parent), ctypes.byref(info), ctypes.byref(process)):
            raise ctypes.WinError(ctypes.get_last_error())
        self.process, self.pid = process.process, process.pid
        self.kernel.CloseHandle(process.thread)

    def message(self, window, message, wparam=0, lparam=0):
        result = ctypes.c_size_t()
        if not self.user.SendMessageTimeoutW(window, message, wparam, lparam, 2, 3000, ctypes.byref(result)):
            raise RuntimeError(f"Installer window message timed out: {message}")
        return result.value

    def describe(self, window):
        title = ctypes.create_unicode_buffer(8192)
        self.message(window, 0x000D, len(title), ctypes.addressof(title))  # WM_GETTEXT
        classname = ctypes.create_unicode_buffer(256)
        self.user.GetClassNameW(window, classname, len(classname))
        return {"handle": int(window), "class": classname.value, "id": self.user.GetDlgCtrlID(window),
                "text": title.value, "visible": bool(self.user.IsWindowVisible(window)),
                "enabled": bool(self.user.IsWindowEnabled(window))}

    def snapshot(self):
        windows = []
        @self.callback_type
        def top(window, _):
            pid = wintypes.DWORD()
            self.user.GetWindowThreadProcessId(window, ctypes.byref(pid))
            if pid.value != self.pid or not self.user.IsWindowVisible(window):
                return True
            item = self.describe(window)
            item["children"] = []
            @self.callback_type
            def child(control, _):
                if self.user.IsWindowVisible(control):
                    item["children"].append(self.describe(control))
                return True
            self.user.EnumChildWindows(window, child, 0)
            windows.append(item)
            return True
        ctypes.set_last_error(0)
        if not self.user.EnumDesktopWindows(self.desktop, top, 0) and ctypes.get_last_error():
            raise ctypes.WinError(ctypes.get_last_error())
        return windows

    def click(self, dialog, control_id=1):
        control = self.user.GetDlgItem(dialog, control_id)
        if not control or not self.user.IsWindowEnabled(control):
            raise RuntimeError(f"Installer button unavailable: {control_id}")
        if not self.user.PostMessageW(control, 0x00F5, 0, 0):  # BM_CLICK
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.process:
            if self.kernel.WaitForSingleObject(self.process, 0) == 258:
                self.kernel.TerminateProcess(self.process, 1)
                self.kernel.WaitForSingleObject(self.process, 5000)
            self.kernel.CloseHandle(self.process)
            self.process = None
        if self.desktop:
            self.user.CloseDesktop(self.desktop)
            self.desktop = None


@unittest.skipUnless(sys.platform == "win32" and os.environ.get("NSIS_MAKENSIS")
                     and os.environ.get("EDITHERE_UPDATE_STUB"), "needs NSIS and harmless Windows stub")
class InstallerUIFlowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = Path(tempfile.mkdtemp(prefix="ehui-"))
        (cls.work / ".edithere-maintenance-fixture").write_text("private desktop UI acceptance")
        cls.data = cls.work / "Data"
        cls.registry_root = "Software\\EditHere\\InstallerTests\\ui_" + uuid.uuid4().hex
        cls.evidence = []
        package = cls.work / "package"
        package.mkdir()
        for name in ("EditHere.exe", "edithere-cli.exe"):
            shutil.copy2(os.environ["EDITHERE_UPDATE_STUB"], package / name)
        for script in ("maintain.ps1", "integrate.ps1", "file_maintenance.ps1"):
            source = ROOT / "packaging/windows" / script
            if source.is_file():
                shutil.copy2(source, package / script)
        (package / "version.txt").write_text("0.9.9\n")
        (package / "payload.txt").write_text("new installed payload")
        entries = [{"path": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                   for path in sorted(package.iterdir())]
        (package / "manifest.json").write_text(json.dumps(entries), encoding="utf-8")
        cls.installer = cls.work / "EditHere-test-setup.exe"
        compile_result = subprocess.run([os.environ["NSIS_MAKENSIS"], "/V2", "/WX", "/INPUTCHARSET", "UTF8",
                                        "/DAPP_VERSION=0.9.9", f"/DPROJECT_ROOT={ROOT}", f"/DPACKAGE_DIR={package}",
                                        f"/DOUTPUT_FILE={cls.installer}", f"/DEDITHERE_TEST_SCOPE_ROOT={cls.work}",
                                        f"/DEDITHERE_TEST_DATA_ROOT={cls.data}", f"/DEDITHERE_TEST_REGISTRY_ROOT={cls.registry_root}",
                                        "/DEDITHERE_TEST_SKIP_LAUNCH_VALIDATION", str(ROOT / "packaging/windows/edithere.nsi")],
                                       capture_output=True, timeout=60)
        if compile_result.returncode:
            raise RuntimeError(compile_result.stdout.decode(errors="replace") + compile_result.stderr.decode(errors="replace"))
        print(f"Real NSIS page evidence on private desktop: {cls.work}", flush=True)

    @classmethod
    def clear_registry(cls):
        assert cls.registry_root.startswith("Software\\EditHere\\InstallerTests\\ui_")
        def remove(path):
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0,
                                    winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_64KEY) as key:
                    children = [winreg.EnumKey(key, index) for index in range(winreg.QueryInfoKey(key)[0])]
                for child in children:
                    remove(path + "\\" + child)
                winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, path, winreg.KEY_WOW64_64KEY)
            except FileNotFoundError:
                pass
        remove(cls.registry_root)

    @classmethod
    def tearDownClass(cls):
        (cls.work / "ui-pages.json").write_text(json.dumps(cls.evidence, ensure_ascii=False, indent=2), encoding="utf-8")
        cls.clear_registry()

    def setUp(self):
        self.clear_registry()
        shell = self.work / "Shell"
        assert shell.is_relative_to(self.work) and (self.work / ".edithere-maintenance-fixture").is_file()
        if shell.exists():
            shutil.rmtree(shell)
        self.receipt_file = self.data / "Results/last.json"
        self.receipt_file.unlink(missing_ok=True)
        self.desktop = PrivateDesktop()
        self.addCleanup(self.desktop.close)

    @staticmethod
    def page_text(snapshot):
        return "\n".join(item["text"] for window in snapshot for item in [window, *window["children"]])

    def wait_page(self, label, predicate, seconds=45):
        deadline = time.monotonic() + seconds
        snapshot = []
        while time.monotonic() < deadline:
            snapshot = self.desktop.snapshot()
            text = self.page_text(snapshot)
            if predicate(snapshot, text):
                self.evidence.append({"test": self.id(), "page": label, "windows": snapshot})
                self.assertNotIn("升级或修复", text)
                self.assertNotIn("迁移", text)
                self.assertNotIn("若要改", text)
                return snapshot
            time.sleep(0.1)
        self.evidence.append({"test": self.id(), "page": "timeout-" + label, "windows": snapshot})
        self.fail(f"Page {label} not reached. Actual visible installer text:\n{self.page_text(snapshot)}")

    def next(self, snapshot):
        dialog = next(window["handle"] for window in snapshot
                      if any(child["id"] == 1 and child["class"] == "Button" for child in window["children"]))
        self.desktop.click(dialog)

    def finish(self, snapshot):
        for window in snapshot:
            for child in window["children"]:
                if child["class"] == "Button" and "启动 EditHere" in child["text"]:
                    self.desktop.message(child["handle"], 0x00F1, 0)  # BM_SETCHECK / BST_UNCHECKED
        self.next(snapshot)
        self.assertEqual(self.desktop.kernel.WaitForSingleObject(self.desktop.process, 10000), 0)
        receipt = json.loads(self.receipt_file.read_text(encoding="utf-8-sig"))
        self.assertEqual(receipt["status"], "committed")
        return receipt

    def test_first_install_shows_location_options_then_real_progress_and_finish(self):
        target = self.work / "first" / "中文 & spaces"
        self.desktop.launch(self.installer, ["/D=" + str(target)])
        page = self.wait_page("welcome", lambda _, text: "欢迎" in text)
        self.next(page)
        page = self.wait_page("license", lambda _, text: "许可" in text or "协议" in text)
        self.next(page)
        page = self.wait_page("options", lambda _, text: "组件" in text)
        self.assertTrue(any(child["class"] == "SysTreeView32" for window in page for child in window["children"]))
        self.next(page)
        page = self.wait_page("directory", lambda _, text: str(target) in text)
        self.assertTrue(any(child["class"] == "Edit" and child["text"] == str(target)
                            for window in page for child in window["children"]))
        self.next(page)
        self.wait_page("install-progress", lambda _, text: "正在安装" in text)
        page = self.wait_page("install-finish", lambda _, text: "安装完成" in text)
        receipt = self.finish(page)
        self.assertEqual(Path(receipt["target"]), target)
        self.assertTrue((target / "EditHere.exe").is_file())
        self.assertEqual((target / "payload.txt").read_text(), "new installed payload")

    def test_repeat_install_uses_registered_location_and_only_update_confirmation_progress_finish(self):
        target = self.work / "existing" / "中文 & spaces"
        different = self.work / "unused-location"
        baseline = subprocess.run(subprocess.list2cmdline([str(self.installer), "/S", "/STARTUP=0", "/ADDPATH=0"])
                                  + " /D=" + str(target), timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(baseline.returncode, 0)
        self.assertTrue((target / "EditHere.exe").is_file())
        self.receipt_file.unlink()
        user_file = target / "notes.txt"
        user_file.write_text("user file must survive repeat install")
        self.desktop.launch(self.installer, ["/D=" + str(different)])
        page = self.wait_page("update-confirmation", lambda _, text: "更新现有版本" in text)
        text = self.page_text(page)
        self.assertIn(str(target), text)
        self.assertNotIn(str(different), text)
        self.assertIn("当前版本", text)
        self.assertIn("安装包版本", text)
        self.assertFalse(any(child["class"] in ("Edit", "SysTreeView32")
                             for window in page for child in window["children"]))
        self.next(page)
        self.wait_page("update-progress", lambda _, text: "正在更新 EditHere" in text)
        page = self.wait_page("update-finish", lambda _, text: "EditHere 更新完成" in text)
        receipt = self.finish(page)
        self.assertEqual(Path(receipt["target"]), target)
        self.assertFalse(different.exists())
        self.assertEqual(user_file.read_text(), "user file must survive repeat install")
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.registry_root + "\\Uninstall\\EditHere", 0,
                            winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            self.assertEqual(winreg.QueryValueEx(key, "DisplayVersion")[0], "0.9.9")

    def test_automatic_update_goes_directly_to_progress_then_exits_after_commit(self):
        target = self.work / "automatic" / "中文 & spaces"
        baseline = subprocess.run(subprocess.list2cmdline([str(self.installer), "/S", "/STARTUP=0", "/ADDPATH=0"])
                                  + " /D=" + str(target), timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(baseline.returncode, 0)
        self.receipt_file.unlink()
        self.desktop.launch(self.installer, ["/UPDATE", "/D=" + str(target)])
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            page = self.desktop.snapshot()
            text = self.page_text(page)
            if not text.strip():
                time.sleep(0.1)
                continue
            self.evidence.append({"test": self.id(), "page": "automatic-first-visible-page", "windows": page})
            self.assertIn("正在更新 EditHere", text)
            self.assertNotIn("更新现有版本", text)
            self.assertNotIn("欢迎", text)
            self.assertNotIn("选择安装位置", text)
            self.assertFalse(any(child["class"] in ("Edit", "SysTreeView32")
                                 for window in page for child in window["children"]))
            break
        else:
            self.fail("Automatic update never displayed its real progress page")
        self.assertEqual(self.desktop.kernel.WaitForSingleObject(self.desktop.process, 45000), 0)
        receipt = json.loads(self.receipt_file.read_text(encoding="utf-8-sig"))
        self.assertEqual(receipt["status"], "committed")
        self.assertEqual(Path(receipt["target"]), target)
        self.assertEqual(receipt["version"], "0.9.9")


if __name__ == "__main__":
    unittest.main(verbosity=2)
