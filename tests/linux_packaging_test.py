"""Exercise architecture selection, ABI rejection and the packaged CLI wrapper.

All files are disposable fixtures. No build tools are downloaded and the CLI
tests launch a small argument-reporting shell script instead of EditHere.
"""
import argparse
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


def load_script(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), ROOT / "scripts" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


fetch_tools = load_script("fetch-linuxdeploy")
abi = load_script("check-linux-abi")


class ToolArchitectureTest(unittest.TestCase):
    def test_architecture_aliases_select_the_matching_tools(self):
        for value, expected in (("x86_64", "x86_64"), ("amd64", "x86_64"),
                                ("aarch64", "aarch64"), ("arm64", "aarch64")):
            with self.subTest(value=value):
                self.assertEqual(fetch_tools.architecture(value), expected)
                names = [item[2] for item in fetch_tools.ASSETS[expected]]
                self.assertEqual(names, ["runtime-" + expected,
                                         "linuxdeploy-" + expected + ".AppImage",
                                         "linuxdeploy-plugin-qt-" + expected + ".AppImage"])

    def test_runtime_hash_uses_the_host_or_explicit_architecture_without_network(self):
        cases = [("arm64", [], "aarch64"), ("amd64", [], "x86_64"),
                 ("x86_64", ["--arch", "aarch64"], "aarch64"),
                 ("aarch64", ["--arch", "x86_64"], "x86_64")]
        for host, arguments, expected in cases:
            with self.subTest(host=host, arguments=arguments):
                output = io.StringIO()
                with patch.object(fetch_tools.platform, "machine", return_value=host), \
                        patch.object(sys, "argv", ["fetch-linuxdeploy.py", *arguments, "--runtime-sha256"]), \
                        patch.object(fetch_tools, "fetch", side_effect=AssertionError("unexpected network access")), \
                        redirect_stdout(output):
                    fetch_tools.main()
                self.assertEqual(output.getvalue().strip(), fetch_tools.ASSETS[expected][0][3])

    def test_unknown_host_and_explicit_architecture_are_rejected(self):
        with self.assertRaises(argparse.ArgumentTypeError):
            fetch_tools.architecture("loongarch64")
        for arguments in ([], ["--arch", "mips64"]):
            with self.subTest(arguments=arguments):
                error = io.StringIO()
                with patch.object(fetch_tools.platform, "machine", return_value="loongarch64"), \
                        patch.object(sys, "argv", ["fetch-linuxdeploy.py", *arguments, "--runtime-sha256"]), \
                        patch.object(fetch_tools, "fetch", side_effect=AssertionError("unexpected network access")), \
                        redirect_stderr(error), self.assertRaises(SystemExit) as result:
                    fetch_tools.main()
                self.assertEqual(result.exception.code, 2)
                self.assertIn("Unsupported Linux architecture", error.getvalue())


class LinuxAbiTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="edithere-abi-test-")
        self.addCleanup(self.temp.cleanup)
        self.appdir = Path(self.temp.name) / "AppDir"
        self.appdir.mkdir()

    def elf(self, relative, machine=183):
        # inspect_elf checks the ELF identity and e_machine before invoking
        # readelf. Version-info output is provided independently below.
        header = bytearray(20)
        header[:6] = b"\x7fELF\x02\x01"
        header[18:20] = machine.to_bytes(2, "little")
        target = self.appdir / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(header)
        return target

    def executables(self):
        return [self.elf("usr/bin/" + name) for name in ("EditHere", "edithere-cli")]

    def version_info(self, output):
        result = subprocess.CompletedProcess(["readelf"], 0, stdout=output, stderr="")
        return patch.object(abi.subprocess, "run", return_value=result)

    def test_mixed_architecture_library_is_rejected(self):
        self.executables()
        self.elf("usr/lib/libwrong.so", machine=62)
        with self.assertRaisesRegex(ValueError, "ELF machine 62 does not match aarch64"):
            abi.check_appdir(self.appdir, "aarch64")

    def test_glibc_requirement_above_baseline_is_rejected(self):
        executable = self.executables()[0]
        output = "Version needs section '.gnu.version_r' contains 1 entry:\n  Name: GLIBC_2.34  Flags: none\n"
        with self.version_info(output), self.assertRaisesRegex(ValueError, "requires GLIBC_2.34, above glibc 2.31"):
            abi.inspect_elf(executable, "aarch64", (2, 31))

    def test_version_definitions_are_not_host_requirements(self):
        self.executables()
        self.elf("usr/lib/libprovided.so")
        output = ("Version definition section '.gnu.version_d' contains 2 entries:\n"
                  "  Name: GLIBC_2.39\n  Name: GLIBC_PRIVATE\n"
                  "Version needs section '.gnu.version_r' contains 2 entries:\n"
                  "  Name: GLIBC_2.17\n  Name: GLIBC_2.31\n")
        with self.version_info(output) as readelf:
            count, highest = abi.check_appdir(self.appdir, "aarch64", (2, 31))
        self.assertEqual((count, highest), (3, (2, 31)))
        self.assertEqual(readelf.call_count, 3)

    def test_definition_only_library_has_no_host_glibc_requirement(self):
        library = self.elf("usr/lib/libprovided.so")
        with self.version_info("Version definition section '.gnu.version_d':\n  Name: GLIBC_2.39\n"):
            self.assertEqual(abi.inspect_elf(library, "aarch64", (2, 31)), ())

    def test_private_host_requirement_is_rejected(self):
        executable = self.executables()[0]
        with self.version_info("Version needs section '.gnu.version_r':\n  Name: GLIBC_PRIVATE\n"), \
                self.assertRaisesRegex(ValueError, "unsupported host requirement GLIBC_PRIVATE"):
            abi.inspect_elf(executable, "aarch64", (2, 31))

    def test_non_elf_metadata_is_ignored(self):
        self.executables()
        (self.appdir / "AppRun").write_text("#!/bin/bash\n", encoding="utf-8")
        self.assertEqual(abi.check_appdir(self.appdir, "aarch64"), (2, ()))

    def test_empty_appdir_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Missing packaged executable"):
            abi.check_appdir(self.appdir, "aarch64")

    def test_non_elf_executables_are_rejected_even_with_a_valid_library(self):
        for name in ("EditHere", "edithere-cli"):
            target = self.appdir / "usr/bin" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("not an ELF executable\n", encoding="utf-8")
        self.elf("usr/lib/libvalid.so")
        with self.assertRaisesRegex(ValueError, "ELF"):
            abi.check_appdir(self.appdir, "aarch64")

    def test_invalid_elf_identity_is_rejected(self):
        executable = self.elf("usr/bin/EditHere")
        executable.write_bytes(b"\x7fELF\x01\x01" + b"\0" * 14)
        with self.assertRaisesRegex(ValueError, "64-bit little-endian ELF"):
            abi.inspect_elf(executable, "aarch64")


def bash_executable():
    if sys.platform == "win32":
        git_bash = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe"
        if git_bash.is_file():
            return str(git_bash)
    return shutil.which("bash")


BASH = bash_executable()


@unittest.skipUnless(BASH, "Bash is required to exercise the Linux CLI wrapper")
class LinuxCliWrapperTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="edithere-cli-test-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name) / "package with spaces & symbols"
        self.directory.mkdir()
        self.wrapper = self.directory / "edithere-cli"
        shutil.copyfile(ROOT / "packaging/linux/edithere-cli", self.wrapper)
        self.wrapper.chmod(0o755)
        self.bin = Path(self.temp.name) / "stubs"
        self.bin.mkdir()
        self.write_script(self.bin / "uname", '#!/bin/bash\nprintf "%s\\n" "$EDITHERE_TEST_UNAME"\n')
        self.env = dict(os.environ)
        search = [str(self.bin)]
        if sys.platform == "win32":
            search.extend([str(Path(BASH).parent), str(Path(BASH).parent.parent / "usr/bin")])
        self.env["PATH"] = os.pathsep.join(search + [self.env.get("PATH", "")])

    def write_script(self, path, content):
        with path.open("w", encoding="utf-8", newline="\n") as output:
            output.write(content)
        path.chmod(0o755)

    def package(self, architecture):
        self.write_script(self.directory / ("EditHere-linux-" + architecture + ".AppImage"),
                          '#!/bin/bash\nprintf "%s\\0" "' + architecture + '" "$@"\n'
                          'exit "${EDITHERE_TEST_EXIT:-0}"\n')

    def run_wrapper(self, architecture, arguments=()):
        env = dict(self.env, EDITHERE_TEST_UNAME=architecture)
        # A UTF-8 driver avoids Windows-to-MSYS argv conversion changing quotes
        # or Unicode. Prepend the stub inside Bash: Git's Windows launcher may
        # put its own usr/bin before the PATH supplied by subprocess.
        driver = Path(self.temp.name) / "invoke.sh"
        command = " ".join(shlex.quote(value) for value in [self.wrapper.as_posix(), *arguments])
        self.write_script(driver, '#!/bin/bash\nexport PATH="$(cd ' + shlex.quote(self.bin.as_posix()) +
                          ' && pwd):$PATH"\nexec ' + command + "\n")
        return subprocess.run([BASH, "--noprofile", "--norc", str(driver)],
                              env=env, capture_output=True, timeout=10)

    def test_architecture_selection_and_arguments_are_preserved(self):
        self.package("x86_64")
        self.package("aarch64")
        arguments = ["annotate", "image with spaces.png", "", "$(touch injected)",
                     "a; b & c | d", "*.png", "'quoted'", '"double"', "back\\slash", "line\nbreak", "中文"]
        for architecture, expected in (("x86_64", "x86_64"), ("amd64", "x86_64"),
                                       ("aarch64", "aarch64"), ("arm64", "aarch64")):
            with self.subTest(architecture=architecture):
                result = self.run_wrapper(architecture, arguments)
                self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", errors="replace"))
                self.assertEqual(result.stdout.decode("utf-8").split("\0"), [expected, "--cli", *arguments, ""])

    def test_missing_package_is_rejected(self):
        result = self.run_wrapper("aarch64", ["--version"])
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"Missing executable AppImage", result.stderr)
        self.assertEqual(result.stdout, b"")

    def test_unknown_architecture_is_rejected(self):
        self.package("x86_64")
        self.package("aarch64")
        result = self.run_wrapper("loongarch64", ["--version"])
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"Supported Linux package architectures", result.stderr)
        self.assertEqual(result.stdout, b"")

    def test_package_exit_status_is_preserved(self):
        self.package("aarch64")
        self.env["EDITHERE_TEST_EXIT"] = "23"
        self.assertEqual(self.run_wrapper("aarch64", ["--version"]).returncode, 23)


if __name__ == "__main__":
    unittest.main()
