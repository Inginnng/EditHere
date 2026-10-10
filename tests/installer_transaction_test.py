"""Exercise the production NSIS process against an isolated maintenance scope.

NSIS_MAKENSIS and EDITHERE_UPDATE_STUB select a compiler and harmless test exe.
The production worker and integration script run unchanged. Guarded test registry
keys and shortcuts live under one unique fixture; no real user integration is used.
Worker receipts, NSIS completion markers and diagnostic files are retained.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
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

# A Windows 8.3 short name (C:\Users\RUNNER~1\...) and its long form
# (C:\Users\runneradmin\...) name the same directory but never compare equal as
# strings. Only some APIs expand the short form: [IO.Path]::GetFullPath, used by
# maintain.ps1, does, while NSIS passes /D= through verbatim. Canonicalizing the
# temp root once keeps every derived path, receipt and registry value spelled the
# same way, so the run does not depend on the length of the runner's user name.
tempfile.tempdir = os.path.realpath(tempfile.gettempdir())


def canonical_path(path):
    """Compare paths by directory identity rather than by 8.3 spelling."""
    return os.path.normcase(os.path.realpath(str(path)))


def read_receipt(path, attempts=20):
    """Parse a receipt the maintenance worker publishes with [IO.File]::Replace.

    File.Replace briefly denies readers the destination, and an NSIS uninstaller
    keeps working after it returns, so a poll loop must treat a sharing violation
    as "not published yet" instead of as a failure.
    """
    for attempt in range(attempts):
        try:
            return json.loads(path.read_text(encoding="utf-8-sig"))
        except (FileNotFoundError, PermissionError):
            if attempt + 1 == attempts:
                raise
            time.sleep(0.05)


@unittest.skipUnless(sys.platform == "win32" and os.environ.get("NSIS_MAKENSIS")
                     and os.environ.get("EDITHERE_UPDATE_STUB"), "needs NSIS and the harmless Windows update stub")
class InstallerTransactionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = Path(tempfile.mkdtemp(prefix="ehit-"))
        (cls.work / ".edithere-maintenance-fixture").write_text("isolated integration tests")
        cls.data = cls.work / "Data"
        cls.result_file = cls.work / "nsis-result.txt"
        cls.registry_root = "Software\\EditHere\\InstallerTests\\nsis_" + uuid.uuid4().hex
        cls.evidence = []
        package = cls.work / "package"
        package.mkdir()
        cls.stub = Path(os.environ["EDITHERE_UPDATE_STUB"])
        shutil.copy2(cls.stub, package / "EditHere.exe")
        shutil.copy2(cls.stub, package / "edithere-cli.exe")
        for script in ("integrate.ps1", "maintain.ps1"):
            shutil.copy2(ROOT / "packaging/windows" / script, package / script)
        (package / "version.txt").write_text("0.9.9\n")
        (package / "payload.txt").write_text("new payload")
        (package / "nested").mkdir()
        (package / "nested/new.txt").write_text("new nested payload")
        cls.write_manifest(package)
        source = (ROOT / "packaging/windows/edithere.nsi").read_text(encoding="utf-8-sig")
        # These hooks observe completion only. Production paths, file operations,
        # integration and the worker are never replaced or disabled by rewriting.
        marker = r'''
Var TestResultWritten
!macro MarkTestResult RESULT
    StrCpy $TestResultWritten 1
    FileOpen $9 "${TEST_RESULT_FILE}.details" w
    ${If} $FailureSummary != ""
        FileWriteUTF16LE $9 "$FailureSummary"
    ${Else}
        FileWriteUTF16LE $9 "$FailureStep"
    ${EndIf}
    FileClose $9
    FileOpen $9 "${TEST_RESULT_FILE}" w
    FileWriteUTF16LE $9 "${RESULT}$\r$\n$EXEPATH$\r$\n$\r$\n$INSTDIR$\r$\n$StartupChoice$\r$\n$PathChoice$\r$\n$UpdateMode$\r$\n$PortableMode$\r$\n$WorkerReturn$\r$\n"
    FileClose $9
!macroend
'''
        source = source.replace('Var DesktopChoice\n', 'Var DesktopChoice\n' + marker)
        source = source.replace("        SetErrorLevel 0\n        Quit",
                                "        !insertmacro MarkTestResult 0\n        SetErrorLevel 0\n        Quit")
        source += '''
Function .onInstSuccess
    !insertmacro MarkTestResult 0
FunctionEnd
Function .onInstFailed
    !insertmacro MarkTestResult 1
FunctionEnd
'''
        source = re.sub(r'(?m)^([ \t]*)SetErrorLevel 1\n\1Abort',
                        lambda match: match[1] + '!insertmacro MarkTestResult 1\n' + match[0], source)
        fixture = cls.work / "test.nsi"
        fixture.write_text(source, encoding="utf-8")
        cls.installer = cls.work / "transaction-test.exe"
        command = [os.environ["NSIS_MAKENSIS"], "/V2", "/WX", "/INPUTCHARSET", "UTF8",
                   "/DAPP_VERSION=0.9.9", f"/DPROJECT_ROOT={ROOT}", f"/DPACKAGE_DIR={package}",
                   f"/DOUTPUT_FILE={cls.installer}", f"/DTEST_RESULT_FILE={cls.result_file}",
                   f"/DEDITHERE_TEST_SCOPE_ROOT={cls.work}", f"/DEDITHERE_TEST_DATA_ROOT={cls.data}",
                   f"/DEDITHERE_TEST_REGISTRY_ROOT={cls.registry_root}",
                   "/DEDITHERE_TEST_SKIP_LAUNCH_VALIDATION", str(fixture)]
        cls.compile(command)
        cls.rollback_installer = cls.work / "rollback.exe"
        rollback_command = [arg if not arg.startswith("/DOUTPUT_FILE=") else f"/DOUTPUT_FILE={cls.rollback_installer}"
                            for arg in command]
        rollback_command.insert(1, "/DEDITHERE_TEST_FAIL_COMMIT")
        cls.compile(rollback_command)
        print(f"Installer maintenance evidence: {cls.work}", flush=True)

    @classmethod
    def compile(cls, command):
        result = subprocess.run(command, capture_output=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))

    @staticmethod
    def write_manifest(directory, names=None):
        files = sorted(directory.rglob("*")) if names is None else [directory / name for name in names]
        entries = [{"path": str(file.relative_to(directory)).replace("/", "\\"),
                    "sha256": hashlib.sha256(file.read_bytes()).hexdigest()}
                   for file in files if file.is_file() and file.name not in ("manifest.json", "Uninstall.exe")]
        (directory / "manifest.json").write_text(json.dumps(entries), encoding="utf-8")

    @classmethod
    def clear_registry(cls):
        # The guarded fixture creates only this fresh randomly named subtree.
        assert cls.registry_root.startswith("Software\\EditHere\\InstallerTests\\nsis_")
        def remove(path):
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_64KEY) as key:
                    children = [winreg.EnumKey(key, i) for i in range(winreg.QueryInfoKey(key)[0])]
                for child in children:
                    remove(path + "\\" + child)
                winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, path, winreg.KEY_WOW64_64KEY)
            except FileNotFoundError:
                pass
        remove(cls.registry_root)

    def setUp(self):
        self.clear_registry()
        shell = self.work / "Shell"
        assert shell.is_relative_to(self.work) and (self.work / ".edithere-maintenance-fixture").is_file()
        if shell.exists():
            shutil.rmtree(shell)

    @classmethod
    def tearDownClass(cls):
        (cls.work / "results.json").write_text(json.dumps(cls.evidence, indent=2, ensure_ascii=False), encoding="utf-8")
        cls.clear_registry()

    def target(self, label):
        directory = self.work / label / "中文 & spaces (portable)"
        directory.mkdir(parents=True)
        shutil.copy2(self.stub, directory / "EditHere.exe")
        shutil.copy2(self.stub, directory / "edithere-cli.exe")
        (directory / "version.txt").write_text("0.9.8\n")
        (directory / "payload.txt").write_text("old payload")
        (directory / "obsolete.dll").write_text("old managed file")
        self.write_manifest(directory)
        (directory / "nested").mkdir()
        (directory / "nested/user.txt").write_text("unique user data")
        return directory

    def run_installer(self, directory, arguments, installer=None, setup_subdirectory=None, directory_argument=None):
        executable = installer or self.installer
        if setup_subdirectory is not None:
            setup_root = directory / setup_subdirectory
            setup_root.mkdir(parents=True, exist_ok=True)
            executable = setup_root / "EditHere-setup.exe"
            shutil.copy2(installer or self.installer, executable)
        self.result_file.unlink(missing_ok=True)
        Path(str(self.result_file) + ".details").unlink(missing_ok=True)
        receipt_file = self.data / "Results/last.json"
        receipt_file.unlink(missing_ok=True)
        command = subprocess.list2cmdline([str(executable), "/S", *arguments])
        result = subprocess.run(command + " /D=" + (directory_argument or str(directory)), timeout=45,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            if self.result_file.is_file():
                content = self.result_file.read_text(encoding="utf-16-le").splitlines()
                if len(content) >= 9:
                    receipt = read_receipt(receipt_file) if receipt_file.is_file() else None
                    self.last_result = {"exit_code": int(content[0]), "installer_image": content[1],
                                        "target": content[3],
                                        "startup": content[4], "add_to_path": content[5],
                                        "update_mode": content[6], "portable_mode": content[7],
                                        "worker_exit": content[8], "receipt": receipt}
                    details_file = Path(str(self.result_file) + ".details")
                    self.last_result["failure_message"] = details_file.read_text(encoding="utf-16-le") if details_file.exists() else ""
                    self.evidence.append(self.last_result)
                    if content[8] == "0":
                        self.assertIsNotNone(receipt, "NSIS zero must have a durable worker receipt")
                        self.assertEqual(receipt["status"], "committed")
                        self.assertEqual(canonical_path(receipt["target"]),
                                         canonical_path(self.last_result["target"]))
                    self.assertEqual(result.returncode, self.last_result["exit_code"],
                                     "the original setup process must report the actual outcome")
                    return self.last_result["exit_code"]
            time.sleep(0.05)
        self.fail(f"installer did not report final completion: {self.work}")

    def run_update(self, directory, installer=None, setup_subdirectory=None):
        return self.run_installer(directory, ["/UPDATE", "/PORTABLE"], installer, setup_subdirectory)

    def registry_value(self, suffix, name):
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.registry_root + "\\" + suffix,
                            0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            return winreg.QueryValueEx(key, name)

    def set_registry_value(self, suffix, name, value, kind):
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, self.registry_root + "\\" + suffix,
                                0, winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_64KEY) as key:
            winreg.SetValueEx(key, name, 0, kind, value)

    def run_integration(self, mode, directory, snapshot):
        powershell = Path(os.environ["WINDIR"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        return subprocess.run([str(powershell), "-NoLogo", "-NoProfile", "-NonInteractive",
                               "-ExecutionPolicy", "Bypass", "-File", str(ROOT / "packaging/windows/integrate.ps1"),
                               "-Mode", mode, "-InstallDirectory", str(directory), "-Version", "0.9.9",
                               "-Startup", "1", "-AddToPath", "1", "-SnapshotPath", str(snapshot),
                               "-ScopeRoot", str(self.work), "-RegistryRoot", self.registry_root],
                              capture_output=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)

    def lock(self, path, directory=False):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                      ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.CreateFileW(str(path), 0x80000000, 3 if directory else 0, None, 3,
                                    0x02000000 if directory else 0x80, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value, ctypes.get_last_error())
        self.addCleanup(kernel.CloseHandle, handle)

    def test_success_preserves_users_and_removes_obsolete_owned_files(self):
        directory = self.target("success")
        self.assertEqual(self.run_update(directory), 0)
        self.assertEqual((directory / "payload.txt").read_text(), "new payload")
        self.assertEqual((directory / "nested/user.txt").read_text(), "unique user data")
        self.assertEqual((directory / "nested/new.txt").read_text(), "new nested payload")
        self.assertFalse((directory / "obsolete.dll").exists())
        self.assertFalse((directory / "Uninstall.exe").exists())
        self.assertFalse(self.last_result["receipt"]["cleanupPending"])

    def test_fresh_install_to_missing_directory_registers_real_fixture(self):
        directory = self.work / "fresh-missing" / "中文 & spaces (installed)"
        self.assertEqual(self.run_installer(directory, ["/STARTUP=0", "/ADDPATH=0"]), 0)
        self.assertEqual(self.last_result["startup"], "0")
        self.assertEqual(self.last_result["add_to_path"], "0")
        self.assertTrue((directory / "Uninstall.exe").is_file())
        self.assertEqual(self.registry_value("Installer", "InstallDir")[0], str(directory))
        self.assertEqual(self.registry_value("Uninstall\\EditHere", "DisplayVersion")[0], "0.9.9")
        self.assertTrue((self.work / "Shell/Programs/EditHere/EditHere.lnk").is_file())

    def test_first_install_from_setup_inside_destination(self):
        directory = self.work / "first-in-root" / "中文 & spaces (installed)"
        self.assertEqual(self.run_installer(directory, ["/STARTUP=1", "/ADDPATH=1"], setup_subdirectory=""), 0)
        self.assertTrue((directory / "EditHere-setup.exe").is_file())
        self.assertTrue((directory / "Uninstall.exe").is_file())
        self.assertEqual((directory / "payload.txt").read_text(), "new payload")
        self.assertEqual(Path(self.last_result["installer_image"]), directory / "EditHere-setup.exe")
        self.assertIn(str(directory), self.registry_value("Environment", "Path")[0])
        self.assertIn(str(directory), self.registry_value("Run", "EditHere")[0])

    def test_first_install_from_setup_in_nested_destination_preserves_files(self):
        directory = self.work / "first-in-subdir" / "中文 & spaces (installed)"
        downloads = directory / "downloads"
        downloads.mkdir(parents=True)
        (downloads / "user.txt").write_text("unique user file")
        self.assertEqual(self.run_installer(directory, ["/STARTUP=0", "/ADDPATH=0"], setup_subdirectory="downloads"), 0)
        self.assertEqual((downloads / "user.txt").read_text(), "unique user file")
        self.assertTrue((directory / "EditHere.exe").is_file())

    def test_update_from_setup_inside_destination(self):
        directory = self.target("update-in-root")
        self.assertEqual(self.run_update(directory, setup_subdirectory=""), 0)
        self.assertEqual(self.last_result["portable_mode"], "1")
        self.assertEqual((directory / "payload.txt").read_text(), "new payload")
        self.assertEqual((directory / "nested/user.txt").read_text(), "unique user data")
        self.assertTrue((directory / "EditHere-setup.exe").is_file())

    def test_original_setup_stays_in_target_and_reports_actual_outcome(self):
        directory = self.target("original-setup")
        self.assertEqual(self.run_update(directory, setup_subdirectory=""), 0)
        setup = directory / "EditHere-setup.exe"
        self.assertEqual(Path(self.last_result["installer_image"]), setup)
        self.assertEqual(setup.read_bytes(), self.installer.read_bytes())

    def test_locked_user_file_does_not_block_update(self):
        directory = self.target("copy-failure")
        self.lock(directory / "nested/user.txt")
        self.assertEqual(self.run_update(directory), 0)
        self.assertEqual((directory / "version.txt").read_text(), "0.9.9\n")
        self.assertTrue((directory / "nested/user.txt").is_file())

    def test_target_directory_need_not_be_renamed(self):
        directory = self.target("directory-stays")
        self.lock(directory, directory=True)
        self.assertEqual(self.run_update(directory), 0)
        self.assertEqual((directory / "payload.txt").read_text(), "new payload")
        self.assertEqual((directory / "version.txt").read_text(), "0.9.9\n")
        self.assertEqual((directory / "nested/user.txt").read_text(), "unique user data")

    def test_locked_managed_file_fails_without_partial_changes(self):
        directory = self.target("managed-file-locked")
        self.lock(directory / "payload.txt")
        self.assertNotEqual(self.run_update(directory), 0)
        self.assertEqual(self.last_result["receipt"]["status"], "failed")
        self.assertEqual((directory / "version.txt").read_text(), "0.9.8\n")
        self.assertFalse((directory / "nested/new.txt").exists())

    def test_integration_failure_restores_directory_and_registration(self):
        directory = self.work / "integration-rollback"
        self.assertEqual(self.run_installer(directory, ["/STARTUP=1", "/ADDPATH=1"]), 0)
        (directory / "user.txt").write_text("unique user data")
        self.assertNotEqual(self.run_installer(directory, ["/UPDATE", "/STARTUP=0", "/ADDPATH=0"], self.rollback_installer), 0)
        self.assertEqual((directory / "user.txt").read_text(), "unique user data")
        self.assertTrue((directory / "Uninstall.exe").is_file())
        self.assertIn(str(directory), self.registry_value("Run", "EditHere")[0])
        self.assertIn(str(directory), self.registry_value("Environment", "Path")[0])
        self.assertEqual(self.last_result["receipt"]["status"], "failed")
        self.assertTrue(self.last_result["receipt"]["restored"])

    def test_same_directory_reinstall_overwrites_snapshot_and_commits(self):
        directory = self.work / "repeat-install"
        self.assertEqual(self.run_installer(directory, ["/STARTUP=1", "/ADDPATH=1"]), 0)
        first_id = self.last_result["receipt"]["id"]
        (directory / "user.txt").write_text("unique user data")
        self.assertEqual(self.run_installer(directory, ["/STARTUP=0", "/ADDPATH=0"]), 0)
        self.assertNotEqual(self.last_result["receipt"]["id"], first_id)
        self.assertEqual((directory / "user.txt").read_text(), "unique user data")
        self.assertEqual(self.registry_value("Installer", "AddedToPath")[0], 0)
        with self.assertRaises(FileNotFoundError):
            self.registry_value("Run", "EditHere")

    def assert_existing_install_uses_registered_target(self, label, arguments, setup_inside=False):
        existing = self.work / (label + "-existing")
        self.assertEqual(self.run_installer(existing, ["/STARTUP=1", "/ADDPATH=1", "/DESKTOP=1"]), 0)
        (existing / "user.txt").write_text("existing user file")
        selected = self.work / (label + "-selected")
        selected.mkdir()
        (selected / "user.txt").write_text("unique user data")
        selected_files = {"user.txt": (selected / "user.txt").read_bytes()}
        if setup_inside:
            selected_files["EditHere-setup.exe"] = self.installer.read_bytes()
        self.assertEqual(self.run_installer(selected, arguments, setup_subdirectory="" if setup_inside else None), 0)
        self.assertEqual(Path(self.last_result["target"]), existing)
        self.assertEqual(self.last_result["update_mode"], "1")
        self.assertEqual(self.last_result["startup"], "1")
        self.assertEqual(self.last_result["add_to_path"], "1")
        self.assertEqual({str(file.relative_to(selected)): file.read_bytes()
                          for file in selected.rglob("*") if file.is_file()}, selected_files)
        self.assertEqual((existing / "user.txt").read_text(), "existing user file")
        self.assertTrue((self.work / "Shell/Desktop/EditHere.lnk").is_file())
        self.assertEqual(self.registry_value("Installer", "InstallDir")[0], str(existing))

    def test_existing_manual_install_automatically_updates_registered_target(self):
        self.assert_existing_install_uses_registered_target("manual-existing", [], setup_inside=True)

    def test_update_uses_registered_target_and_preserves_existing_options(self):
        self.assert_existing_install_uses_registered_target("update-existing", ["/UPDATE"])

    @staticmethod
    def cleanup_handoff(directory):
        assert directory.parent == Path(tempfile.gettempdir()) and directory.name.startswith("EditHere-update-")
        for name in ("request.json", "status.json", "ack.json"):
            (directory / name).unlink(missing_ok=True)
        if directory.exists():
            directory.rmdir()

    def test_handoff_directory_conflict_reports_failed_without_ack_or_changes(self):
        existing = self.work / "handoff-existing"
        self.assertEqual(self.run_installer(existing, ["/STARTUP=0", "/ADDPATH=0"]), 0)
        original = {str(file.relative_to(existing)): file.read_bytes()
                    for file in existing.rglob("*") if file.is_file()}
        selected = self.work / "handoff-requested"
        selected.mkdir()
        (selected / "user.txt").write_text("unique user data")
        handoff_root = Path(tempfile.gettempdir()) / ("EditHere-update-" + uuid.uuid4().hex)
        handoff_root.mkdir()
        token = uuid.uuid4().hex
        (handoff_root / "request.json").write_text(json.dumps({"target": str(selected), "version": "0.9.9", "token": token}),
                                                  encoding="utf-8")
        self.addCleanup(self.cleanup_handoff, handoff_root)
        self.assertNotEqual(self.run_installer(selected, ["/UPDATE", "/HANDOFF=" + handoff_root.name]), 0)
        self.assertEqual(Path(self.last_result["target"]), selected,
                         "an acknowledged updater target must never be redirected")
        self.assertEqual(self.last_result["worker_exit"], "")
        status = json.loads((handoff_root / "status.json").read_text(encoding="utf-8"))
        self.assertEqual(status["status"], "failed")
        self.assertEqual(status["token"], token)
        self.assertEqual(Path(status["target"]), selected)
        self.assertEqual(status["version"], "0.9.9")
        self.assertEqual(self.last_result["receipt"]["status"], "failed")
        self.assertFalse(self.last_result["receipt"]["acknowledged"])
        self.assertEqual({str(file.relative_to(existing)): file.read_bytes()
                          for file in existing.rglob("*") if file.is_file()}, original)
        self.assertEqual([file.name for file in selected.iterdir()], ["user.txt"])
        self.assertNotIn("暂存目录", self.last_result["failure_message"])

    def test_older_setup_stops_before_staging_or_modifying_existing_install(self):
        directory = self.work / "newer-installed"
        self.assertEqual(self.run_installer(directory, ["/STARTUP=0", "/ADDPATH=0"]), 0)
        (directory / "version.txt").write_text("0.10.0\n")
        original = {str(file.relative_to(directory)): file.read_bytes()
                    for file in directory.rglob("*") if file.is_file()}
        self.assertNotEqual(self.run_installer(directory, []), 0)
        self.assertEqual(self.last_result["worker_exit"], "")
        self.assertIsNone(self.last_result["receipt"])
        self.assertIn("已安装更新版本", self.last_result["failure_message"])
        self.assertEqual({str(file.relative_to(directory)): file.read_bytes()
                          for file in directory.rglob("*") if file.is_file()}, original)

    def test_equivalent_registered_directory_with_case_and_separator_can_update(self):
        directory = self.work / "equivalent-registered"
        self.assertEqual(self.run_installer(directory, ["/STARTUP=0", "/ADDPATH=0"]), 0)
        self.assertEqual(self.run_installer(directory, ["/UPDATE"], directory_argument=str(directory).upper() + "\\"), 0)
        self.assertEqual((directory / "payload.txt").read_text(), "new payload")

    def test_equivalent_registered_directory_cannot_be_updated_as_portable(self):
        directory = self.work / "registered-as-portable"
        self.assertEqual(self.run_installer(directory, ["/STARTUP=0", "/ADDPATH=0"]), 0)
        old_files = {str(file.relative_to(directory)): file.read_bytes()
                     for file in directory.rglob("*") if file.is_file()}
        self.assertNotEqual(self.run_installer(directory, ["/UPDATE", "/PORTABLE"],
                                               directory_argument=str(directory).upper() + "\\"), 0)
        self.assertEqual(self.last_result["worker_exit"], "")
        self.assertIsNone(self.last_result["receipt"])
        self.assertEqual({str(file.relative_to(directory)): file.read_bytes()
                          for file in directory.rglob("*") if file.is_file()}, old_files)

    def test_update_preserves_desktop_choice_and_explicit_override(self):
        directory = self.work / "desktop-choice"
        shortcut = self.work / "Shell/Desktop/EditHere.lnk"
        self.assertEqual(self.run_installer(directory, ["/STARTUP=0", "/ADDPATH=0", "/DESKTOP=1"]), 0)
        self.assertTrue(shortcut.is_file())
        self.assertEqual(self.run_installer(directory, ["/UPDATE"]), 0)
        self.assertTrue(shortcut.is_file())
        self.assertEqual(self.run_installer(directory, ["/UPDATE", "/DESKTOP=0"]), 0)
        self.assertFalse(shortcut.exists())

    def test_portable_update_rejects_missing_prior_application(self):
        directory = self.work / "missing-portable"
        self.assertNotEqual(self.run_update(directory), 0)
        self.assertFalse((directory / "EditHere.exe").exists())
        self.assertEqual(self.last_result["receipt"]["status"], "failed")

    def test_registry_snapshot_preserves_types_and_concurrent_foreign_path(self):
        directory = self.target("typed-snapshot")
        snapshot = self.work / "typed-snapshot.json"
        self.set_registry_value("Installer", "InstallDir", str(directory), winreg.REG_SZ)
        self.set_registry_value("Installer", "AddedToPath", 0, winreg.REG_DWORD)
        saved = {"binary": (bytes(range(8)), winreg.REG_BINARY),
                 "none": (b"\x01\x00", winreg.REG_NONE),
                 "expand": ("%TEMP%\\raw", winreg.REG_EXPAND_SZ),
                 "multi": (["a", "b"], winreg.REG_MULTI_SZ),
                 "dword": (0xffffffff, winreg.REG_DWORD),
                 "qword": (0xffffffffffffffff, winreg.REG_QWORD)}
        for name, (value, kind) in saved.items():
            self.set_registry_value("Installer", name, value, kind)
        self.set_registry_value("Environment", "Path", "%TEMP%\\baseline", winreg.REG_EXPAND_SZ)
        self.set_registry_value("Run", "EditHere", "%APPDATA%\\custom", winreg.REG_EXPAND_SZ)
        for _ in range(2):
            result = self.run_integration("Snapshot", directory, snapshot)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        result = self.run_integration("Install", directory, snapshot)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        raw_path, kind = self.registry_value("Environment", "Path")
        self.set_registry_value("Environment", "Path", raw_path + ";C:\\concurrent", kind)
        for name in saved:
            self.set_registry_value("Installer", name, "changed", winreg.REG_SZ)
        result = self.run_integration("Restore", directory, snapshot)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        for name, expected in saved.items():
            self.assertEqual(self.registry_value("Installer", name), expected)
        self.assertEqual(self.registry_value("Run", "EditHere"), ("%APPDATA%\\custom", winreg.REG_EXPAND_SZ))
        self.assertEqual(self.registry_value("Environment", "Path"),
                         ("%TEMP%\\baseline;C:\\concurrent", winreg.REG_EXPAND_SZ))

    def test_snapshot_rejects_another_registered_target_before_writing(self):
        directory = self.target("snapshot-target-check")
        snapshot = self.work / "rejected-snapshot.json"
        other = str(self.work / "other-target")
        self.set_registry_value("Uninstall\\EditHere", "InstallLocation", other, winreg.REG_SZ)
        result = self.run_integration("Snapshot", directory, snapshot)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(snapshot.exists())
        self.assertEqual(self.registry_value("Uninstall\\EditHere", "InstallLocation")[0], other)

    def test_uninstaller_removes_only_managed_files_and_own_registration(self):
        directory = self.work / "uninstall-owned-only"
        self.assertEqual(self.run_installer(directory, ["/STARTUP=1", "/ADDPATH=1"]), 0)
        (directory / "user.txt").write_text("unique user data")
        old_id = self.last_result["receipt"]["id"]
        subprocess.run([str(directory / "Uninstall.exe"), "/S"], timeout=40,
                       creationflags=subprocess.CREATE_NO_WINDOW, check=True)
        receipt_file = self.data / "Results/last.json"
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            receipt = read_receipt(receipt_file)
            if receipt["id"] != old_id:
                self.assertEqual(receipt["operation"], "Uninstall")
                self.assertEqual(receipt["status"], "committed")
                self.evidence.append({"uninstall": receipt})
                break
            time.sleep(0.05)
        else:
            self.fail("uninstaller did not produce a final receipt")
        self.assertEqual((directory / "user.txt").read_text(), "unique user data")
        self.assertFalse((directory / "EditHere.exe").exists())
        self.assertFalse((directory / "Uninstall.exe").exists())
        with self.assertRaises(FileNotFoundError):
            self.registry_value("Installer", "InstallDir")
        self.assertNotIn(str(directory), self.registry_value("Environment", "Path")[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
