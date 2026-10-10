"""Opt-in acceptance of real Qt packages with production NSIS and maintenance.

Set NSIS_MAKENSIS, EDITHERE_ACCEPT_PACKAGE and EDITHERE_ACCEPT_OLD_PACKAGE.
Only randomly named, marked fixtures and an isolated HKCU subtree are touched.
Unlike the stub regressions, the production CLI runtime probe remains enabled.
"""
import hashlib
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
# See installer_transaction_test.py: 8.3 short names and long names are the same
# directory but different strings, and only some Windows APIs expand them.
tempfile.tempdir = os.path.realpath(tempfile.gettempdir())


def main():
    if sys.platform != "win32":
        raise SystemExit("Windows only")
    import winreg
    compiler = os.environ["NSIS_MAKENSIS"]
    package = Path(os.environ["EDITHERE_ACCEPT_PACKAGE"]).resolve(strict=True)
    previous = Path(os.environ["EDITHERE_ACCEPT_OLD_PACKAGE"]).resolve(strict=True)
    work = Path(tempfile.mkdtemp(prefix="ehpa-"))
    (work / ".edithere-maintenance-fixture").write_text("real package acceptance")
    data = work / "Data"
    registry = "Software\\EditHere\\InstallerTests\\package_" + uuid.uuid4().hex
    evidence = {"work": str(work), "registry": registry, "runs": []}
    print("Full package acceptance: " + str(work), flush=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                  ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]

    def worker_finished(receipt):
        lock = str(Path(receipt["journal"]).parent / "lock")
        handle = kernel.CreateFileW(lock, 0x80000000, 3, None, 3, 0x80, None)
        if handle == ctypes.c_void_p(-1).value:
            return False
        kernel.CloseHandle(handle)
        return True

    def clear_registry(path=registry):
        assert path.startswith(registry)
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_64KEY) as key:
                children = [winreg.EnumKey(key, i) for i in range(winreg.QueryInfoKey(key)[0])]
            for child in children:
                clear_registry(path + "\\" + child)
            winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, path, winreg.KEY_WOW64_64KEY)
        except FileNotFoundError:
            pass

    def compile_setup(payload, label):
        version = (payload / "version.txt").read_text(encoding="utf-8-sig").strip()
        exe = work / (label + ".exe")
        result = subprocess.run([compiler, "/V2", "/WX", "/INPUTCHARSET", "UTF8",
                                 "/DAPP_VERSION=" + version, "/DPROJECT_ROOT=" + str(ROOT),
                                 "/DPACKAGE_DIR=" + str(payload), "/DOUTPUT_FILE=" + str(exe),
                                 "/DEDITHERE_TEST_SCOPE_ROOT=" + str(work),
                                 "/DEDITHERE_TEST_DATA_ROOT=" + str(data),
                                 "/DEDITHERE_TEST_REGISTRY_ROOT=" + registry,
                                 str(ROOT / "packaging/windows/edithere.nsi")],
                                capture_output=True, timeout=180)
        (work / (label + "-compile.txt")).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError("NSIS compilation failed: " + str(work))
        return exe

    def last_id():
        result = data / "Results/last.json"
        return json.loads(result.read_text(encoding="utf-8-sig"))["id"] if result.exists() else ""

    def wait_receipt(old_id, operation, target):
        deadline = time.monotonic() + 180
        result = data / "Results/last.json"
        while time.monotonic() < deadline:
            if result.exists():
                receipt = json.loads(result.read_text(encoding="utf-8-sig"))
                if receipt["id"] != old_id:
                    if not worker_finished(receipt):
                        time.sleep(0.1)
                        continue
                    if receipt["status"] != "committed" or receipt["operation"] != operation or Path(receipt["target"]) != target:
                        raise RuntimeError(json.dumps(receipt, ensure_ascii=False))
                    if receipt["cleanupPending"]:
                        raise RuntimeError("Unexpected cleanup pending: " + str(result))
                    evidence["runs"].append(receipt)
                    return receipt
            time.sleep(0.1)
        raise TimeoutError("No final receipt: " + str(work))

    def install(exe, target, args=()):
        before = last_id()
        command = subprocess.list2cmdline([str(exe), "/S", *args]) + " /D=" + str(target)
        process = subprocess.run(command, creationflags=subprocess.CREATE_NO_WINDOW, timeout=180)
        if process.returncode:
            raise RuntimeError("Installer failed")
        return wait_receipt(before, "Install", target)

    def runtime(target, version):
        result = subprocess.run([str(target / "edithere-cli.exe"), "--version"],
                                capture_output=True, timeout=20, cwd=work,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode or result.stdout.decode().strip() != "EditHere " + version:
            raise RuntimeError("Final CLI runtime validation failed")
        # Every install's own log must record the production runtime probe.
        receipt = evidence["runs"][-1]
        if "runtime verified: EditHere " + version not in Path(receipt["log"]).read_text(encoding="utf-8-sig"):
            raise RuntimeError("Installer skipped runtime validation")

    def handoff_update(exe, target, version):
        handoff = Path(tempfile.gettempdir()) / ("EditHere-update-" + uuid.uuid4().hex)
        handoff.mkdir()
        token = uuid.uuid4().hex
        download = handoff / "EditHereSetup.exe"
        shutil.copy2(exe, download)
        request = {"id": handoff.name, "target": str(target), "version": version,
                   "token": token, "package": download.name,
                   "sha256": hashlib.sha256(download.read_bytes()).hexdigest()}
        (handoff / "request.json").write_text(json.dumps(request), encoding="utf-8")
        before = last_id()
        command = subprocess.list2cmdline([str(download), "/S", "/UPDATE",
                                          "/HANDOFF=" + handoff.name]) + " /D=" + str(target)
        process = subprocess.Popen(command, creationflags=subprocess.CREATE_NO_WINDOW)
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError("Installer exited before READY: " + str(process.returncode))
            status_file = handoff / "status.json"
            if status_file.exists():
                status = json.loads(status_file.read_text(encoding="utf-8-sig"))
                if status["status"] != "ready":
                    raise RuntimeError("Unexpected handoff status: " + json.dumps(status))
                if status["token"] != token or status["version"] != version or Path(status["target"]) != target:
                    raise RuntimeError("Handoff identity mismatch")
                break
            time.sleep(0.1)
        else:
            raise TimeoutError("Full installer did not become ready")
        if last_id() != before:
            raise RuntimeError("Installer mutated the transaction before ACK")
        acknowledgement = handoff / "ack.tmp"
        acknowledgement.write_text(json.dumps({"action": "ack", "token": token}), encoding="utf-8")
        os.replace(acknowledgement, handoff / "ack.json")
        receipt = wait_receipt(before, "Install", target)
        if process.wait(timeout=180):
            raise RuntimeError("Acknowledged installer failed")
        if not receipt["acknowledged"] or receipt["token"] != token:
            raise RuntimeError("Installer did not record the acknowledged handoff")
        deadline = time.monotonic() + 20
        while handoff.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
        if handoff.exists():
            raise RuntimeError("Completed update download was not reclaimed: " + str(handoff))
        evidence["handoff"] = {"status": "passed", "id": handoff.name, "cleaned": True}

    def uninstall(target):
        before = last_id()
        subprocess.run([str(target / "Uninstall.exe"), "/S"], check=True, timeout=180,
                       creationflags=subprocess.CREATE_NO_WINDOW)
        wait_receipt(before, "Uninstall", target)
        if (target / "EditHere.exe").exists() or (target / "Uninstall.exe").exists():
            raise RuntimeError("Managed executables survived uninstallation")
        if (target / "user.edithere").read_bytes() != b"unique project":
            raise RuntimeError("User file changed during uninstall")
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry + "\\Installer"):
                raise RuntimeError("Installation registration survived uninstallation")
        except FileNotFoundError:
            pass

    try:
        setup = compile_setup(package, "current")
        version = (package / "version.txt").read_text().strip()
        target = work / "首次安装 & spaces" / "EditHere"
        target.mkdir(parents=True)
        (target / "user.edithere").write_bytes(b"unique project")
        inside = target / "setup.exe"
        shutil.copy2(setup, inside)
        original_setup_hash = hashlib.sha256(inside.read_bytes()).hexdigest()
        install(inside, target, ["/STARTUP=1", "/ADDPATH=1"])
        runtime(target, version)
        if hashlib.sha256(inside.read_bytes()).hexdigest() != original_setup_hash:
            raise RuntimeError("The user's installer was moved or changed")
        install(setup, target, ["/UPDATE"])
        runtime(target, version)
        handoff_update(setup, target, version)
        runtime(target, version)
        uninstall(target)
        if hashlib.sha256(inside.read_bytes()).hexdigest() != original_setup_hash:
            raise RuntimeError("Uninstallation removed the user's installer")
        clear_registry()

        # Keep the real 0.10.3 binaries and dependencies, while using the current
        # maintenance engine to establish an isolated installed baseline.
        old_payload = work / "old-payload"
        shutil.copytree(previous, old_payload)
        for script in ("maintain.ps1", "integrate.ps1"):
            shutil.copy2(ROOT / "packaging/windows" / script, old_payload / script)
        (old_payload / "manifest.json").unlink()
        manifest = [{"path": str(p.relative_to(old_payload)),
                     "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                    for p in sorted(old_payload.rglob("*")) if p.is_file()]
        (old_payload / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        old_setup = compile_setup(old_payload, "previous")
        old_version = (old_payload / "version.txt").read_text().strip()
        target = work / "真实旧版升级" / "EditHere"
        install(old_setup, target, ["/STARTUP=0", "/ADDPATH=0"])
        runtime(target, old_version)
        (target / "user.edithere").write_bytes(b"unique project")
        install(setup, target, ["/UPDATE"])
        runtime(target, version)
        uninstall(target)
        clear_registry()

        target = work / "便携版升级" / "EditHere"
        shutil.copytree(previous, target)
        (target / "user.edithere").write_bytes(b"unique project")
        install(setup, target, ["/UPDATE", "/PORTABLE"])
        runtime(target, version)
        if (target / "Uninstall.exe").exists():
            raise RuntimeError("Portable update created an uninstaller")
        if (target / "user.edithere").read_bytes() != b"unique project":
            raise RuntimeError("Portable update changed the user file")
        print("PASS: fresh install from target, repeated maintenance, READY/ACK update and cleanup, real previous-version upgrade, portable update and uninstallation.", flush=True)
        evidence["status"] = "passed"
    except BaseException as error:
        evidence["status"] = "failed"
        evidence["error"] = str(error)
        raise
    finally:
        clear_registry()
        (work / "acceptance.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
        artifact_directory = Path(os.environ.get("EDITHERE_ACCEPT_ARTIFACT_DIRECTORY",
                                                 ROOT / "artifacts/installer-lifecycle")).resolve()
        if not artifact_directory.is_relative_to(ROOT / "artifacts"):
            raise RuntimeError("Acceptance artifacts must stay inside project artifacts")
        artifact_directory.mkdir(parents=True, exist_ok=True)
        artifact = artifact_directory / "full-package-acceptance.json"
        artifact.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
