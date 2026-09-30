"""Run the real NSIS portable update against disposable copies, never an installed app.

NSIS_MAKENSIS and EDITHERE_UPDATE_STUB point to a compiler and harmless test exe.
All package, destination and backup directories are inside one temporary root.
"""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "win32" and os.environ.get("NSIS_MAKENSIS")
                     and os.environ.get("EDITHERE_UPDATE_STUB"), "needs NSIS and the harmless Windows update stub")
class InstallerTransactionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Kept on failure for inspection; never remove a directory supplied by the caller.
        cls.work = Path(tempfile.mkdtemp(prefix="edithere-installer-test-"))
        package = cls.work / "package"
        package.mkdir()
        cls.stub = Path(os.environ["EDITHERE_UPDATE_STUB"])
        shutil.copy2(cls.stub, package / "EditHere.exe")
        shutil.copy2(cls.stub, package / "edithere-cli.exe")
        shutil.copy2(ROOT / "packaging/windows/integrate.ps1", package / "integrate.ps1")
        (package / "version.txt").write_text("0.9.9\n")
        (package / "payload.txt").write_text("new payload")
        (package / "nested").mkdir()
        (package / "nested/new.txt").write_text("new nested payload")
        remove = cls.work / "remove.nsh"
        remove.write_text('Delete "$INSTDIR\\EditHere.exe"\n')
        cls.installer = cls.work / "update-test.exe"
        command = [os.environ["NSIS_MAKENSIS"], "/V2", "/WX", "/INPUTCHARSET", "UTF8",
                   "/DAPP_VERSION=0.9.9", f"/DPROJECT_ROOT={ROOT}", f"/DPACKAGE_DIR={package}",
                   f"/DOUTPUT_FILE={cls.installer}", f"/DREMOVE_INCLUDE={remove}",
                   str(ROOT / "packaging/windows/edithere.nsi")]
        result = subprocess.run(command, capture_output=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
        cls.rollback_installer = cls.work / "rollback-test.exe"
        rollback_command = [arg if not arg.startswith("/DOUTPUT_FILE=") else f"/DOUTPUT_FILE={cls.rollback_installer}"
                            for arg in command]
        rollback_command.insert(1, "/DEDITHERE_TEST_FAIL_COMMIT")
        result = subprocess.run(rollback_command, capture_output=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
        print(f"Installer transaction evidence: {cls.work}", flush=True)

    def target(self, label):
        directory = self.work / label / "中文 & spaces (portable)"
        directory.mkdir(parents=True)
        shutil.copy2(self.stub, directory / "EditHere.exe")
        (directory / "version.txt").write_text("0.9.8\n")
        (directory / "payload.txt").write_text("old payload")
        (directory / "nested").mkdir()
        (directory / "nested/user.txt").write_text("unique user data")
        return directory

    def run_update(self, directory, installer=None):
        # NSIS explicitly requires an unquoted, final /D= argument. No cmd shell.
        command = subprocess.list2cmdline([str(installer or self.installer), "/S", "/UPDATE", "/PORTABLE"])
        result = subprocess.run(command + " /D=" + str(directory), timeout=40,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        return result.returncode

    def lock(self, path, directory=False):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                      ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        # Directory: permit copying, deny rename. File: deny reading the source.
        handle = kernel.CreateFileW(str(path), 0x80000000, 3 if directory else 0, None, 3,
                                    0x02000000 if directory else 0x80, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value, ctypes.get_last_error())
        self.addCleanup(kernel.CloseHandle, handle)

    def test_success_preserves_user_files_and_recoverable_backup(self):
        directory = self.target("success")
        self.assertEqual(self.run_update(directory), 0)
        self.assertEqual((directory / "payload.txt").read_text(), "new payload")
        self.assertEqual((directory / "nested/user.txt").read_text(), "unique user data")
        self.assertEqual((directory / "nested/new.txt").read_text(), "new nested payload")
        backup = Path((directory / "update-backup.txt").read_text(encoding="utf-16-le").strip().lstrip("\ufeff"))
        self.assertTrue(backup.is_relative_to(self.work))
        self.assertEqual((backup / "payload.txt").read_text(), "old payload")
        self.assertEqual((backup / "nested/user.txt").read_text(), "unique user data")
        self.assertFalse((directory / "Uninstall.exe").exists())

    def test_failed_copy_preserves_working_version(self):
        directory = self.target("copy-failure")
        self.lock(directory / "payload.txt")
        self.assertNotEqual(self.run_update(directory), 0)
        self.assertEqual((directory / "version.txt").read_text(), "0.9.8\n")
        self.assertFalse((directory / "nested/new.txt").exists())

    def test_locked_directory_never_gets_partially_updated(self):
        directory = self.target("rename-failure")
        self.lock(directory, directory=True)
        self.assertNotEqual(self.run_update(directory), 0)
        self.assertEqual((directory / "payload.txt").read_text(), "old payload")
        self.assertEqual((directory / "version.txt").read_text(), "0.9.8\n")
        self.assertEqual((directory / "nested/user.txt").read_text(), "unique user data")

    def test_failed_commit_restores_original_directory(self):
        directory = self.target("rollback")
        self.assertNotEqual(self.run_update(directory, self.rollback_installer), 0)
        self.assertEqual((directory / "payload.txt").read_text(), "old payload")
        self.assertEqual((directory / "version.txt").read_text(), "0.9.8\n")
        self.assertEqual((directory / "nested/user.txt").read_text(), "unique user data")
        self.assertFalse((directory / "nested/new.txt").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
