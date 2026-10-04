"""Smoke-test an AppImage without touching the user's settings or desktop session."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

def verify_diagnostics(folder, expected_version, artifact_directory=None):
    sessions = {}
    sources = sorted((Path(folder) / "data").rglob("edithere-*.jsonl"),
                     key=lambda path: (path.name.rsplit(".", 2)[0], int(path.name.rsplit(".", 2)[1])))
    for source in sources:
        records = [json.loads(line) for line in source.read_text(encoding="utf-8").splitlines() if line]
        sessions.setdefault(source.name.rsplit(".", 2)[0], []).extend(records)
    gui_session = None
    for records in sessions.values():
        if any(record.get("component") == "startup.runtime" and
               record.get("fields", {}).get("agentStart") is True for record in records):
            gui_session = records
            break
    if not gui_session:
        raise RuntimeError("Packaged GUI did not create its diagnostic runtime record")
    start = next((record for record in gui_session if record.get("component") == "diagnostics" and
                  record.get("message") == "session.start"), None)
    runtime = next((record for record in gui_session if record.get("component") == "startup.runtime"), None)
    if not start or start.get("fields", {}).get("version") != expected_version:
        raise RuntimeError("Packaged GUI diagnostic startup metadata is missing or has the wrong version")
    for field in ("qt", "os", "architecture"):
        if not start.get("fields", {}).get(field):
            raise RuntimeError("Packaged GUI diagnostic environment metadata is missing: " + field)
    if runtime.get("fields", {}).get("platform") != "xcb" or not runtime.get("fields", {}).get("screens"):
        raise RuntimeError("Packaged GUI diagnostic display dimensions are missing")
    if gui_session[-1].get("component") != "diagnostics" or gui_session[-1].get("message") != "session.end":
        raise RuntimeError("Packaged GUI diagnostic session did not record its normal shutdown")
    if artifact_directory:
        target = Path(artifact_directory).resolve()
        target.mkdir(parents=True, exist_ok=True)
        for source in sources:
            shutil.copy2(source, target / source.name)
    print("Packaged GUI diagnostics: session.start, runtime metadata, session.end OK")

def main():
    package = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="edithere-package-") as folder:
        env = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1")
        for key, suffix in [("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                            ("XDG_CACHE_HOME", "cache"), ("XDG_RUNTIME_DIR", "runtime")]:
            target = Path(folder) / suffix
            target.mkdir(mode=0o700)
            env[key] = str(target)
        env.pop("LD_LIBRARY_PATH", None)
        for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH",
                    "SPA_PLUGIN_DIR", "PIPEWIRE_MODULE_DIR", "PIPEWIRE_CONFIG_DIR",
                    "EDITHERE_PIPEWIRE_LIBRARY"):
            env.pop(key, None)
        version = subprocess.run([str(package), "--cli", "--version"], env=env,
                                 capture_output=True, text=True, timeout=90)
        if version.returncode:
            raise RuntimeError(version.stderr)
        print("Packaged CLI:", version.stdout.strip())
        expected_version = version.stdout.strip().removeprefix("EditHere ")
        # A private Xvfb display and DBus session are provided by the caller.
        env["QT_QPA_PLATFORM"] = "xcb"
        with open(Path(folder) / "app.log", "w+") as log:
            app = subprocess.Popen([str(package), "--agent-start"], env=env, stdout=log, stderr=log)
            try:
                status = None
                for _ in range(30):
                    if app.poll() is not None:
                        log.seek(0)
                        raise RuntimeError("Packaged app exited: " + log.read())
                    result = subprocess.run([str(package), "--cli", "status"], env=env,
                                            capture_output=True, text=True, timeout=90)
                    if result.returncode == 0:
                        candidate = json.loads(result.stdout)
                        if candidate.get("ok") and candidate.get("running"):
                            status = candidate
                            break
                    time.sleep(0.2)
                if not status or not status.get("ok") or not status.get("running"):
                    raise RuntimeError("Packaged app did not expose its CLI endpoint")
                if status.get("version") != expected_version:
                    raise RuntimeError("CLI connected to a different application version")
                executable = Path(status.get("executable", ""))
                if executable.name != "EditHere" or not executable.is_file():
                    raise RuntimeError("CLI did not report a running packaged executable")
                if executable.parents[1].name != "usr" or not (executable.parents[2] / "AppRun").is_file():
                    raise RuntimeError("CLI connected to an executable outside the AppImage")
                if app.poll() is not None:
                    raise RuntimeError("Packaged process exited before endpoint verification")
                print("Packaged GUI and CLI connection: OK")
                print("Packaged GUI:", status["version"], executable)
                quit_result = subprocess.run([str(package), "--quit"], env=env,
                                             capture_output=True, text=True, timeout=90)
                if quit_result.returncode:
                    raise RuntimeError(quit_result.stderr)
                app.wait(timeout=15)
                if app.returncode:
                    raise RuntimeError(f"Packaged app exit: {app.returncode}")
                print("Packaged GUI normal shutdown: OK")
                verify_diagnostics(folder, expected_version, sys.argv[2] if len(sys.argv) > 2 else None)
            finally:
                if app.poll() is None:
                    app.terminate()
                    app.wait(timeout=10)

if __name__ == "__main__":
    main()
