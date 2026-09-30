"""Smoke-test an AppImage without touching the user's settings or desktop session."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

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
        version = subprocess.run([str(package), "--cli", "--version"], env=env,
                                 capture_output=True, text=True, timeout=90)
        if version.returncode:
            raise RuntimeError(version.stderr)
        print("Packaged CLI:", version.stdout.strip())
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
                        status = json.loads(result.stdout)
                        break
                    time.sleep(0.2)
                if not status or not status.get("ok"):
                    raise RuntimeError("Packaged app did not expose its CLI endpoint")
                print("Packaged GUI and CLI connection: OK")
                quit_result = subprocess.run([str(package), "--quit"], env=env,
                                             capture_output=True, text=True, timeout=90)
                if quit_result.returncode:
                    raise RuntimeError(quit_result.stderr)
                app.wait(timeout=15)
                if app.returncode:
                    raise RuntimeError(f"Packaged app exit: {app.returncode}")
            finally:
                if app.poll() is None:
                    app.terminate()
                    app.wait(timeout=10)

if __name__ == "__main__":
    main()
