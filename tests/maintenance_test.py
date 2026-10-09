"""Exercise the real Windows file transaction without touching the user's shell.

Each test copies the production worker beside a narrow integration fixture. The
fixture implements Snapshot/Restore in a disposable JSON file; the separate NSIS
suite checks the real integration script against an isolated HKCU namespace.
"""
import ctypes
from contextlib import contextmanager
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

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
VERSION = "0.10.4"


@contextmanager
def windows_handle(path, *, directory=False, share=0):
    """Hold a real Windows sharing lock without changing fixture contents."""
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                       ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    create.restype = ctypes.c_void_p
    handle = create(str(path), 0x80000000, share, None, 3,
                    0x02000000 if directory else 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    close = kernel.CloseHandle
    close.argtypes = [ctypes.c_void_p]
    try:
        yield
    finally:
        close(handle)


INTEGRATION_FIXTURE = r"""param([string]$Mode,[string]$InstallDirectory,[string]$SnapshotPath='',
 [string]$ScopeRoot,[string]$RegistryRoot,[string]$Version,[string]$Startup,
 [string]$AddToPath,[int]$TimeoutSeconds=0)
$ErrorActionPreference='Stop'
$state=Join-Path $ScopeRoot 'integration-state.json'
if($Mode -eq 'Check' -or $Mode -eq 'Close'){exit 0}
if($Mode -eq 'Snapshot') {
 @{exists=[IO.File]::Exists($state);content=if([IO.File]::Exists($state)){[IO.File]::ReadAllText($state)}else{''}} |
  ConvertTo-Json | Set-Content -LiteralPath $SnapshotPath -Encoding UTF8
} elseif($Mode -eq 'Restore') {
 $s=Get-Content -LiteralPath $SnapshotPath -Raw | ConvertFrom-Json
 if($s.exists){[IO.File]::WriteAllText($state,$s.content)}elseif([IO.File]::Exists($state)){[IO.File]::Delete($state)}
} elseif($Mode -eq 'Install') {
 @{target=$InstallDirectory;version=$Version;startup=$Startup;path=$AddToPath} |
  ConvertTo-Json | Set-Content -LiteralPath $state -Encoding UTF8
} elseif($Mode -eq 'Uninstall') {if([IO.File]::Exists($state)){[IO.File]::Delete($state)}}
else {exit 1}
exit 0
"""


@unittest.skipUnless(sys.platform == "win32", "Windows file transactions")
class MaintenanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="edithere-maintenance-test-")
        self.work = Path(self.temp.name)
        (self.work / ".edithere-maintenance-fixture").write_text("isolated fixture\n")
        self.worker = self.work / "worker"
        self.worker.mkdir()
        shutil.copy2(ROOT / "packaging/windows/maintain.ps1", self.worker)
        (self.worker / "integrate.ps1").write_text(INTEGRATION_FIXTURE, encoding="utf-8-sig")
        self.target = self.work / "EditHere"
        self.data = self.work / "maintenance"
        self.registry = "Software\\EditHere\\InstallerTests\\" + uuid.uuid4().hex
        self.integration = self.work / "integration-state.json"
        self.integration.write_text('{"version":"0.10.3","unrelated":"preserved"}', encoding="utf-8")
        self.original_integration = self.integration.read_bytes()
        self.make_package(self.target, "0.10.3", {"obsolete.dll": b"old library"})
        (self.target / "user.edithere").write_bytes(b"unique user project")
        (self.target / "custom").mkdir()
        (self.target / "custom/note.txt").write_bytes(b"unique nested note")

    def tearDown(self):
        self.temp.cleanup()

    def make_package(self, directory, version=VERSION, extra=None):
        directory.mkdir()
        files = {"EditHere.exe": b"harmless application fixture " + version.encode(),
                 "edithere-cli.exe": b"harmless CLI fixture " + version.encode(),
                 "version.txt": (version + "\n").encode(),
                 "nested/library.dll": b"packaged dependency " + version.encode()}
        files.update(extra or {})
        manifest = []
        for relative, content in files.items():
            path = directory / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            manifest.append({"path": relative, "sha256": hashlib.sha256(content).hexdigest()})
        (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        (directory / "Uninstall.exe").write_bytes(b"harmless NSIS generated uninstaller")
        return directory

    def stage(self, **kwargs):
        return self.make_package(self.work / ("stage-" + uuid.uuid4().hex), **kwargs)

    def command(self, mode="Install", stage=None, portable=False, validate_launch=False, **extra):
        arguments = [str(POWERSHELL), "-NoLogo", "-NoProfile", "-NonInteractive",
                     "-ExecutionPolicy", "Bypass", "-File", str(self.worker / "maintain.ps1"),
                     "-Mode", mode, "-InstallDirectory", str(self.target), "-Version", VERSION,
                     "-ScopeRoot", str(self.work), "-DataRoot", str(self.data),
                     "-RegistryRoot", self.registry, "-Portable", "1" if portable else "0"]
        if not validate_launch:
            arguments.append("-SkipLaunchValidation")
        if stage:
            arguments += ["-StageDirectory", str(stage)]
        for key, value in extra.items():
            arguments += ["-" + key, str(value)]
        return arguments

    def run_worker(self, mode="Install", stage=None, expected=0, **kwargs):
        result = subprocess.run(self.command(mode, stage, **kwargs), capture_output=True, timeout=45)
        self.assertEqual(result.returncode, expected, result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
        return result

    def receipt(self):
        return json.loads((self.data / "Results/last.json").read_text(encoding="utf-8-sig"))

    def journal(self):
        return json.loads(next((self.data / "Transactions").glob("*/journal.json")).read_text(encoding="utf-8-sig"))

    def assert_old_intact(self):
        self.assertEqual((self.target / "version.txt").read_text().strip(), "0.10.3")
        self.assertTrue((self.target / "obsolete.dll").exists())
        self.assertEqual((self.target / "user.edithere").read_bytes(), b"unique user project")
        self.assertEqual(self.integration.read_bytes(), self.original_integration)

    def test_upgrade_removes_obsolete_files_preserves_unknown_and_commits(self):
        self.run_worker(stage=self.stage(), Desktop=1)
        self.assertEqual((self.target / "version.txt").read_text().strip(), VERSION)
        self.assertFalse((self.target / "obsolete.dll").exists())
        self.assertEqual((self.target / "user.edithere").read_bytes(), b"unique user project")
        self.assertEqual((self.target / "custom/note.txt").read_bytes(), b"unique nested note")
        self.assertEqual(self.receipt()["status"], "committed")
        self.assertFalse(self.receipt()["cleanupPending"])
        self.assertFalse(Path(self.journal()["backup"]).exists())
        self.assertTrue((self.work / "Shell/Desktop/EditHere.lnk").exists())

    def test_first_install_preserves_setup_inside_selected_directory(self):
        shutil.rmtree(self.target)
        self.target.mkdir()
        (self.target / "setup.exe").write_bytes(b"user's installer")
        self.run_worker(stage=self.stage(), InstallerPath=str(self.target / 'setup.exe'))
        self.assertEqual((self.target / "setup.exe").read_bytes(), b"user's installer")
        self.assertFalse(self.receipt()["cleanupPending"])

    def test_manifest_hash_failure_never_changes_old_install(self):
        stage = self.stage()
        (stage / "nested/library.dll").write_bytes(b"corrupted")
        self.run_worker(stage=stage, expected=1)
        self.assert_old_intact()
        self.assertEqual(self.receipt()["status"], "failed")

    def test_unsafe_manifest_path_is_rejected(self):
        stage = self.stage()
        manifest = json.loads((stage / "manifest.json").read_text())
        manifest.append({"path": "../unique.txt", "sha256": "0" * 64})
        (stage / "manifest.json").write_text(json.dumps(manifest))
        self.run_worker(stage=stage, expected=1)
        self.assert_old_intact()

    def test_duplicate_manifest_path_is_rejected(self):
        stage = self.stage()
        manifest = json.loads((stage / "manifest.json").read_text())
        duplicate = dict(manifest[0]); duplicate["path"] = "EDITHERE.EXE"
        manifest.append(duplicate)
        (stage / "manifest.json").write_text(json.dumps(manifest))
        self.run_worker(stage=stage, expected=1)
        self.assert_old_intact()

    def test_unknown_payload_and_user_collision_are_rejected(self):
        stage = self.stage()
        (stage / "unlisted.dll").write_bytes(b"not signed by manifest")
        self.run_worker(stage=stage, expected=1)
        self.assert_old_intact()
        stage = self.stage(extra={"user.edithere": b"new file colliding with user's project"})
        self.run_worker(stage=stage, expected=1)
        self.assert_old_intact()

    def test_integration_failure_restores_old_files_and_registration(self):
        self.run_worker(stage=self.stage(), TestFailure="IntegrationFailure", expected=1)
        self.assert_old_intact()
        self.assertEqual(self.journal()["state"], "RolledBack")
        self.assertTrue(Path(self.journal()["displaced"]).exists())

    def test_crash_after_old_file_move_recovers_previous_install(self):
        result = subprocess.run(self.command(stage=self.stage(), TestFailure="AfterOldFileMove"), capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        journal = self.journal()
        moved = journal['plan'][journal['cursor']]['path']
        self.assertTrue(self.target.exists())
        self.assertFalse((self.target / moved).exists())
        self.assertTrue((Path(journal['backup']) / moved).exists())
        self.run_worker(mode="Recover")
        self.assert_old_intact()
        self.assertEqual(self.journal()["state"], "RolledBack")

    def test_crash_after_integration_recovers_files_registry_and_shortcuts(self):
        result = subprocess.run(self.command(stage=self.stage(), Desktop=1, TestFailure="AfterIntegration"), capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(self.integration.read_text(encoding="utf-8-sig"))["version"], VERSION)
        self.run_worker(mode="Recover")
        self.assert_old_intact()
        self.assertFalse((self.work / "Shell/Desktop/EditHere.lnk").exists())

    def test_first_install_crash_after_new_file_move_recovers_empty_original_state(self):
        shutil.rmtree(self.target)
        result = subprocess.run(self.command(stage=self.stage(), TestFailure="AfterNewFileMove"), capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        self.run_worker(mode="Recover")
        self.assertFalse(self.target.exists())
        self.assertEqual(self.integration.read_bytes(), self.original_integration)

    def test_concurrent_worker_cannot_mutate_target_or_take_lock(self):
        first = subprocess.Popen(self.command(stage=self.stage(), TestPauseSeconds=5), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                files = list((self.data / "Transactions").glob("*/journal.json")) if (self.data / "Transactions").exists() else []
                if files and json.loads(files[0].read_text())["state"] == "Prepared":
                    break
                time.sleep(0.1)
            else:
                self.fail("first installer did not reach fixture pause")
            self.run_worker(stage=self.stage(), expected=2)
            output, errors = first.communicate(timeout=35)
            self.assertEqual(first.returncode, 0, output.decode(errors="replace") + errors.decode(errors="replace"))
            self.assertEqual(self.receipt()["status"], "committed")
        finally:
            if first.poll() is None:
                first.kill(); first.wait()

    def test_uninstall_failure_keeps_program_and_registration_retryable(self):
        self.run_worker(mode="Uninstall", TestFailure="UninstallFailure", expected=1)
        self.assert_old_intact()
        self.run_worker(mode="Uninstall")
        self.assertFalse((self.target / "EditHere.exe").exists())
        self.assertFalse((self.target / "manifest.json").exists())
        self.assertEqual((self.target / "user.edithere").read_bytes(), b"unique user project")
        self.assertFalse(self.integration.exists())
        self.assertEqual(self.receipt()["status"], "committed")

    def test_cleanup_failure_is_recorded_and_retry_uses_persisted_manifest(self):
        self.run_worker(stage=self.stage(), TestFailure="CleanupFailure")
        self.assertTrue(self.receipt()["cleanupPending"])
        backup = Path(self.journal()["backup"])
        # Model partial cleanup before a crash: the original on-disk manifest
        # no longer exists, yet the persisted journal still has ownership.
        (backup / "manifest.json").unlink()
        self.run_worker(mode="Recover")
        self.assertFalse(backup.exists())
        self.assertFalse(self.receipt()["cleanupPending"])
        self.assertEqual((self.target / "version.txt").read_text().strip(), VERSION)

    def test_portable_install_does_not_change_registration_or_shortcuts(self):
        self.run_worker(stage=self.stage(), portable=True, Desktop=1, Startup=1, AddToPath=1)
        self.assertEqual(self.integration.read_bytes(), self.original_integration)
        self.assertFalse((self.work / "Shell").exists())

    def test_existing_program_without_manifest_is_preserved(self):
        (self.target / "manifest.json").unlink()
        self.run_worker(stage=self.stage(), expected=1)
        self.assertEqual((self.target / "version.txt").read_text().strip(), "0.10.3")
        self.assertEqual(self.integration.read_bytes(), self.original_integration)

    def test_fixture_scope_cannot_escape_to_other_directory(self):
        self.run_worker(stage=self.stage(), ScopeRoot=str(self.work / "missing-marker"), expected=1)
        self.assert_old_intact()
        self.assertFalse(self.data.exists())

    def test_runtime_validation_failure_rolls_back_both_layers(self):
        self.run_worker(stage=self.stage(), validate_launch=True, expected=1)
        self.assert_old_intact()
        self.assertEqual(self.journal()["state"], "RolledBack")

    def test_retained_unique_backup_does_not_block_future_update(self):
        self.run_worker(stage=self.stage(), TestFailure="CleanupFailure")
        old_backup = Path(self.journal()["backup"])
        (old_backup / "obsolete.dll").write_bytes(b"unique modification after commit")
        self.run_worker(stage=self.stage())
        self.assertEqual((old_backup / "obsolete.dll").read_bytes(), b"unique modification after commit")
        histories = list((self.data / "Transactions").glob("*/history-*.json"))
        self.assertTrue(histories)
        history = json.loads(histories[-1].read_text())
        self.assertTrue(history["cleanupPending"])
        self.assertEqual(Path(history["backup"]), old_backup)
        self.assertEqual(self.receipt()["status"], "committed")

    def test_different_targets_share_integration_lock(self):
        first = subprocess.Popen(self.command(stage=self.stage(), TestPauseSeconds=5), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                files = list((self.data / "Transactions").glob("*/journal.json")) if (self.data / "Transactions").exists() else []
                if files and json.loads(files[0].read_text())["state"] == "Prepared":
                    break
                time.sleep(0.1)
            else:
                self.fail("first installer did not reach fixture pause")
            original_target = self.target
            self.target = self.work / "AnotherTarget"
            self.make_package(self.target, "0.10.3")
            self.run_worker(stage=self.stage(), expected=2)
            self.assertEqual((self.target / "version.txt").read_text().strip(), "0.10.3")
            self.target = original_target
            output, errors = first.communicate(timeout=35)
            self.assertEqual(first.returncode, 0, output.decode(errors="replace") + errors.decode(errors="replace"))
            self.assertEqual(self.receipt()["status"], "committed")
        finally:
            if first.poll() is None:
                first.kill()
            first.communicate()

    def test_handoff_cancel_before_ack_leaves_original_intact(self):
        handoff = Path(tempfile.gettempdir()) / ("EditHere-update-" + uuid.uuid4().hex)
        handoff.mkdir()
        token = uuid.uuid4().hex
        (handoff / "request.json").write_text(json.dumps({"target": str(self.target), "version": VERSION, "token": token}))
        process = subprocess.Popen(self.command(stage=self.stage(), HandoffId=handoff.name), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 15
            status = handoff / "status.json"
            while time.monotonic() < deadline:
                if status.exists() and json.loads(status.read_text(encoding="utf-8"))["status"] == "ready":
                    break
                time.sleep(0.1)
            else:
                self.fail("installer did not send READY")
            self.assert_old_intact()
            (handoff / "ack.json").write_text(json.dumps({"action": "cancel", "token": token}))
            output, errors = process.communicate(timeout=20)
            self.assertEqual(process.returncode, 1, output.decode(errors="replace") + errors.decode(errors="replace"))
            self.assert_old_intact()
            self.assertEqual(self.receipt()["status"], "cancelled")
            self.assertFalse(self.receipt()["acknowledged"])
            deadline = time.monotonic() + 10
            while handoff.exists() and time.monotonic() < deadline:
                time.sleep(0.1)
            self.assertFalse(handoff.exists(), "worker did not reclaim its cancelled handoff")
        finally:
            if process.poll() is None:
                process.kill()
            process.communicate()
            if handoff.exists():
                shutil.rmtree(handoff)

    def test_crashed_other_target_blocks_new_registration_until_recovered(self):
        result = subprocess.run(self.command(stage=self.stage(), TestFailure="AfterOldRename"), capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        first_target = self.target
        self.target = self.work / "AnotherTarget"
        self.make_package(self.target, "0.10.3")
        self.run_worker(stage=self.stage(), expected=1)
        self.assertEqual(self.integration.read_bytes(), self.original_integration)
        self.assertEqual((self.target / "version.txt").read_text().strip(), "0.10.3")
        self.target = first_target
        self.run_worker(mode="Recover")
        self.assert_old_intact()

    def test_portable_update_retries_after_partial_file_move(self):
        result = subprocess.run(self.command(stage=self.stage(), portable=True, TestFailure="AfterOldFileMove"), capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.target.exists())
        self.run_worker(stage=self.stage(), portable=True)
        self.assertEqual((self.target / "version.txt").read_text().strip(), VERSION)
        self.assertEqual((self.target / "user.edithere").read_bytes(), b"unique user project")
        self.assertEqual(self.integration.read_bytes(), self.original_integration)

    def test_portable_new_directory_is_rejected(self):
        shutil.rmtree(self.target)
        self.run_worker(stage=self.stage(), portable=True, expected=1)
        self.assertFalse(self.target.exists())
        self.assertEqual(self.integration.read_bytes(), self.original_integration)

    def test_committed_receipt_survives_crash_before_cleanup(self):
        result = subprocess.run(self.command(stage=self.stage(), TestFailure="AfterCommit"), capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.receipt()["status"], "committed")
        self.assertEqual(self.journal()["state"], "Committed")
        self.run_worker(mode="Recover")
        self.assertEqual((self.target / "version.txt").read_text().strip(), VERSION)
        self.assertFalse(Path(self.journal()["backup"]).exists())

    def test_modified_managed_file_is_retained_as_unique_backup(self):
        (self.target / "obsolete.dll").write_bytes(b"user's unique change before update")
        self.run_worker(stage=self.stage())
        backup = Path(self.journal()["backup"])
        self.assertEqual((backup / "obsolete.dll").read_bytes(), b"user's unique change before update")
        self.assertTrue(self.receipt()["cleanupPending"])

    def test_unacknowledged_retry_does_not_recover_pending_old_transaction(self):
        result = subprocess.run(self.command(stage=self.stage(), TestFailure="AfterOldRename"), capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        old_journal = self.journal()
        handoff = Path(tempfile.gettempdir()) / ("EditHere-update-" + uuid.uuid4().hex)
        handoff.mkdir()
        (handoff / "request.json").write_text(json.dumps({"target": str(self.target), "version": VERSION, "token": uuid.uuid4().hex}))
        try:
            self.run_worker(stage=self.stage(), HandoffId=handoff.name, expected=1)
            moved = old_journal['plan'][old_journal['cursor']]['path']
            self.assertFalse((self.target / moved).exists(), "unacknowledged handoff restored old files")
            self.assertTrue(Path(old_journal["backup"]).exists())
            self.assertEqual(self.journal()["state"], old_journal["state"])
            self.assertFalse(self.receipt()["acknowledged"])
        finally:
            if handoff.exists():
                shutil.rmtree(handoff)

    def test_locked_unknown_files_are_never_read_or_moved(self):
        setup = self.target / 'setup.exe'
        setup.write_bytes(b'original downloaded setup')
        with windows_handle(self.target / 'user.edithere'), windows_handle(setup):
            self.run_worker(stage=self.stage(), InstallerPath=str(setup))
            self.assertEqual(self.journal()['schema'], 2)
            self.assertEqual(self.journal()['layout'], 'files')
        self.assertEqual(setup.read_bytes(), b'original downloaded setup')
        self.assertEqual((self.target / 'user.edithere').read_bytes(), b'unique user project')
        with windows_handle(self.target / 'custom/note.txt'):
            self.run_worker(mode='Uninstall')
        self.assertEqual((self.target / 'custom/note.txt').read_bytes(), b'unique nested note')

    def test_directory_rename_lock_does_not_block_file_install_or_uninstall(self):
        with windows_handle(self.target, directory=True, share=3):
            # A zero-access metadata handle does not participate in Windows
            # sharing checks. Prove this is a real rename-denying read handle.
            with self.assertRaises(PermissionError):
                self.target.rename(self.work / 'target-rename-must-be-blocked')
            self.run_worker(stage=self.stage())
            self.run_worker(mode='Uninstall')
        self.assertTrue(self.target.exists())
        self.assertEqual((self.target / 'user.edithere').read_bytes(), b'unique user project')

    def test_managed_file_sharing_lock_fails_and_restores_partial_application(self):
        # version.txt sorts after the binaries and dependencies, before the
        # manifest that is deliberately published last. Its lock forces a
        # failure after several real replacement moves have already occurred.
        with windows_handle(self.target / 'version.txt', share=3):
            self.run_worker(stage=self.stage(), expected=1)
        self.assert_old_intact()
        self.assertEqual(self.journal()['state'], 'RolledBack')
        self.assertTrue(Path(self.journal()['displaced']).exists())

    def test_crash_after_new_file_move_preserves_modified_new_file_on_recovery(self):
        result = subprocess.run(self.command(stage=self.stage(), TestFailure='AfterNewFileMove'),
                                capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        journal = self.journal()
        relative = journal['plan'][journal['cursor']]['path']
        (self.target / relative).write_bytes(b'unique new-version modification')
        self.run_worker(mode='Recover')
        self.assert_old_intact()
        self.assertEqual((Path(journal['displaced']) / relative).read_bytes(),
                         b'unique new-version modification')

    def test_recovery_does_not_overwrite_user_file_created_after_old_move(self):
        result = subprocess.run(self.command(stage=self.stage(), TestFailure='AfterOldFileMove'),
                                capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        journal = self.journal()
        relative = journal['plan'][journal['cursor']]['path']
        (self.target / relative).write_bytes(b'user created this during interruption')
        self.run_worker(mode='Recover', expected=4)
        self.assertEqual((self.target / relative).read_bytes(), b'user created this during interruption')
        self.assertTrue((Path(journal['backup']) / relative).exists())

    def test_pending_new_only_user_file_is_not_touched_during_recovery(self):
        result = subprocess.run(self.command(stage=self.stage(extra={'new-only.dll': b'new dependency'}),
                                             TestFailure='AfterPrepare'), capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        (self.target / 'new-only.dll').write_bytes(b'user-created file before application')
        self.run_worker(mode='Recover')
        self.assert_old_intact()
        self.assertEqual((self.target / 'new-only.dll').read_bytes(), b'user-created file before application')

    def test_unknown_junction_is_preserved_without_traversal(self):
        outside = self.work / 'outside-user-directory'
        outside.mkdir()
        (outside / 'unique.txt').write_bytes(b'outside contents')
        junction = self.target / 'unknown-junction'
        result = subprocess.run(['cmd.exe', '/c', 'mklink', '/J', str(junction), str(outside)],
                                capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        try:
            with windows_handle(outside / 'unique.txt'):
                self.run_worker(stage=self.stage())
                self.run_worker(mode='Uninstall')
            self.assertTrue(junction.is_dir())
            self.assertEqual((outside / 'unique.txt').read_bytes(), b'outside contents')
        finally:
            junction.rmdir()

    def test_setup_managed_filename_collision_fails_before_ready(self):
        handoff = Path(tempfile.gettempdir()) / ('EditHere-update-' + uuid.uuid4().hex)
        handoff.mkdir()
        (handoff / 'request.json').write_text(json.dumps({'target': str(self.target), 'version': VERSION,
                                                        'token': uuid.uuid4().hex}))
        try:
            self.run_worker(stage=self.stage(), InstallerPath=str(self.target / 'EditHere.exe'),
                            HandoffId=handoff.name, expected=1)
            self.assert_old_intact()
            self.assertFalse(self.receipt()['acknowledged'])
            self.assertNotEqual(json.loads((handoff / 'status.json').read_text(encoding='utf-8'))['status'], 'ready')
        finally:
            if handoff.exists():
                shutil.rmtree(handoff)

    def test_invalid_file_journal_path_is_rejected_without_file_changes(self):
        result = subprocess.run(self.command(stage=self.stage(), TestFailure='AfterPrepare'),
                                capture_output=True, timeout=45)
        self.assertNotEqual(result.returncode, 0)
        journal_path = next((self.data / 'Transactions').glob('*/journal.json'))
        journal = self.journal()
        journal['plan'][0]['path'] = '../outside.exe'
        journal_path.write_text(json.dumps(journal))
        self.run_worker(mode='Recover', expected=1)
        self.assert_old_intact()

    def test_historical_schema1_directory_transaction_recovers_with_new_worker(self):
        legacy = ROOT / 'dist/installer-lifecycle-verified/EditHere-0.10.4-win-x64/maintain.ps1'
        if not legacy.exists():
            self.skipTest('Historical schema1 worker snapshot is unavailable')
        current = (self.worker / 'maintain.ps1').read_bytes()
        shutil.copyfile(legacy, self.worker / 'maintain.ps1')
        try:
            result = subprocess.run(self.command(stage=self.stage(), TestFailure='AfterOldRename'),
                                    capture_output=True, timeout=45)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(self.target.exists())
            self.assertEqual(self.journal()['schema'], 1)
        finally:
            (self.worker / 'maintain.ps1').write_bytes(current)
        self.run_worker(mode='Recover')
        self.assert_old_intact()
        self.assertEqual(self.journal()['state'], 'RolledBack')

    def prepare_historical_installed_target_transaction(self):
        legacy = ROOT / 'dist/installer-lifecycle-verified/EditHere-0.10.4-win-x64/maintain.ps1'
        if not legacy.exists():
            self.skipTest('Historical schema1 worker snapshot is unavailable')
        current = (self.worker / 'maintain.ps1').read_bytes()
        shutil.copyfile(legacy, self.worker / 'maintain.ps1')
        try:
            result = subprocess.run(self.command(stage=self.stage(), TestFailure='AfterNewRename'),
                                    capture_output=True, timeout=45)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(self.target.exists())
            self.assertEqual(self.journal()['schema'], 1)
            self.assertEqual((self.target / 'version.txt').read_text().strip(), VERSION)
        finally:
            (self.worker / 'maintain.ps1').write_bytes(current)

    def test_historical_schema1_recovery_preserves_running_setup_in_locked_target(self):
        self.prepare_historical_installed_target_transaction()
        setup = self.target / 'new-setup.exe'
        setup.write_bytes(b'new downloaded setup remains here')
        old_backup = Path(self.journal()['backup'])
        with windows_handle(self.target, directory=True, share=3), windows_handle(setup):
            self.run_worker(mode='Recover', InstallerPath=str(setup))
        self.assert_old_intact()
        self.assertEqual(setup.read_bytes(), b'new downloaded setup remains here')
        self.assertEqual((old_backup / 'user.edithere').read_bytes(), b'unique user project')
        self.assertEqual(self.journal()['state'], 'RolledBack')
        self.assertIn('legacyFileRecovery', self.journal())
        self.assertTrue(self.receipt()['cleanupPending'])
        self.assertEqual(Path(self.receipt()['recovery']), old_backup)

    def test_historical_file_recovery_can_resume_after_a_restored_file_move(self):
        self.prepare_historical_installed_target_transaction()
        setup = self.target / 'new-setup.exe'
        setup.write_bytes(b'new downloaded setup remains here')
        with windows_handle(self.target, directory=True, share=3), windows_handle(setup):
            result = subprocess.run(self.command(mode='Recover', TestFailure='AfterOldFileMove'),
                                    capture_output=True, timeout=45)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('legacyFileRecovery', self.journal())
            self.run_worker(mode='Recover')
        self.assert_old_intact()
        self.assertEqual(setup.read_bytes(), b'new downloaded setup remains here')
        self.assertEqual(self.journal()['state'], 'RolledBack')

    def test_historical_first_install_recovery_preserves_new_setup_in_locked_target(self):
        shutil.rmtree(self.target)
        self.prepare_historical_installed_target_transaction()
        self.assertFalse(self.journal()['hadTarget'])
        setup = self.target / 'new-setup.exe'
        setup.write_bytes(b'new downloaded setup remains here')
        with windows_handle(self.target, directory=True, share=3), windows_handle(setup):
            self.run_worker(mode='Recover', InstallerPath=str(setup))
        self.assertTrue(self.target.exists())
        self.assertFalse((self.target / 'EditHere.exe').exists())
        self.assertFalse((self.target / 'manifest.json').exists())
        self.assertEqual(setup.read_bytes(), b'new downloaded setup remains here')
        self.assertEqual(self.integration.read_bytes(), self.original_integration)
        self.assertIn('legacyFileRecovery', self.journal())
        self.assertEqual(self.journal()['state'], 'RolledBack')
        self.assertEqual(Path(self.receipt()['recovery']), Path(self.journal()['displaced']))


if __name__ == "__main__":
    unittest.main(verbosity=2)
