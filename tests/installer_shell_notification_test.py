"""Windows PowerShell 5 notification regression in an NSIS plugin directory.

No registry values or shortcuts are written. The production Notify-Shell
function is extracted verbatim and run with the real native NSIS System.dll
in the process working directory, reproducing the installed execution context.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
# See installer_transaction_test.py: 8.3 short names and long names are the same
# directory but different strings, and only some Windows APIs expand them.
tempfile.tempdir = os.path.realpath(tempfile.gettempdir())
POWERSHELL = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
LEGACY_NOTIFY = r'''Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class EditHereShell { [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, UIntPtr w, string l, uint f, uint t, out UIntPtr r); [DllImport("shell32.dll")] public static extern void SHChangeNotify(int e, uint f, IntPtr a, IntPtr b); }'
Write-Output 'LEGACY_UNEXPECTED_SUCCESS'
'''


def find_native_plugin():
    candidates = [ROOT / ".tools/nsis/nsis-3.10/Plugins/x86-unicode/System.dll"]
    if os.environ.get("NSIS_MAKENSIS"):
        compiler = Path(os.environ["NSIS_MAKENSIS"])
        candidates += [compiler.parent / "Plugins/x86-unicode/System.dll",
                       compiler.parent.parent / "Plugins/x86-unicode/System.dll"]
    if os.environ.get("ProgramFiles(x86)"):
        candidates += [Path(os.environ["ProgramFiles(x86)"]) / "NSIS/Plugins/x86-unicode/System.dll"]
    return next((candidate for candidate in candidates if candidate.is_file()), None)


@unittest.skipUnless(sys.platform == "win32", "Windows PowerShell 5 / native shell notifications")
class ShellNotificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plugin = find_native_plugin()
        if cls.plugin is None:
            raise unittest.SkipTest("requires the native NSIS System.dll plugin")
        source = (ROOT / "packaging/windows/integrate.ps1").read_text(encoding="utf-8-sig")
        cls.function = "function Notify-Shell {" + source.split("function Notify-Shell {", 1)[1].split("\ntry {", 1)[0]

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="edithere-shell-notification-")
        self.work = Path(self.temp.name)
        shutil.copy2(self.plugin, self.work / "System.dll")

    def tearDown(self):
        self.temp.cleanup()

    def run_powershell(self, commands):
        script = self.work / "notification-test.ps1"
        script.write_text("$ErrorActionPreference='Stop'\n$RegistryRoot=''\n" + commands, encoding="utf-8-sig")
        return subprocess.run([str(POWERSHELL), "-NoLogo", "-NoProfile", "-NonInteractive",
                               "-ExecutionPolicy", "Bypass", "-File", str(script)],
                              cwd=self.work, capture_output=True, timeout=30)

    def test_legacy_implicit_reference_resolves_native_plugin_and_fails(self):
        result = self.run_powershell(LEGACY_NOTIFY)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"System.dll", result.stderr)
        self.assertIn(self.work.name.encode("ascii"), result.stderr)
        self.assertNotIn(b"LEGACY_UNEXPECTED_SUCCESS", result.stdout)

    def test_production_notification_uses_framework_reference_from_dirty_cwd(self):
        result = self.run_powershell(self.function + "\nNotify-Shell\nif(!('EditHereShell' -as [type])) { throw 'Interop type was not created.' }\nWrite-Output 'NOTIFY_COMPLETE'\n")
        self.assertEqual(result.returncode, 0, result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
        self.assertIn(b"NOTIFY_COMPLETE", result.stdout)
        self.assertNotIn(b"WARNING", result.stdout)
        self.assertFalse(result.stderr)

    def test_notification_compiler_failure_is_only_a_warning(self):
        result = self.run_powershell("function Add-Type { throw 'fixture compiler unavailable' }\n" + self.function + "\nNotify-Shell\nWrite-Output 'CONTINUE_AFTER_NOTIFY'\n")
        self.assertEqual(result.returncode, 0, result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
        self.assertIn(b"fixture compiler unavailable", result.stdout)
        self.assertIn(b"CONTINUE_AFTER_NOTIFY", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
