"""Reject mixed-architecture ELF files and dependencies newer than a glibc baseline."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

MACHINES = {"x86_64": 62, "aarch64": 183}


def version(value):
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)+", value):
        raise argparse.ArgumentTypeError("Expected a version such as 2.31")
    return tuple(map(int, value.split(".")))


def inspect_elf(path, architecture, glibc_max=None):
    with path.open("rb") as source:
        header = source.read(20)
    if not header.startswith(b"\x7fELF"):
        return None
    if len(header) != 20 or header[4:6] != b"\x02\x01":
        raise ValueError(f"{path}: expected a 64-bit little-endian ELF")
    machine = int.from_bytes(header[18:20], "little")
    if machine != MACHINES[architecture]:
        raise ValueError(f"{path}: ELF machine {machine} does not match {architecture}")
    requirements = []
    if glibc_max is not None:
        result = subprocess.run(["readelf", "--version-info", "--wide", str(path)],
                                env=dict(os.environ, LC_ALL="C"),
                                capture_output=True, text=True, check=True)
        # Definitions in a bundled library describe what it provides, rather
        # than what it needs from the host. Only inspect the needs section.
        needs = result.stdout.partition("Version needs section")[2]
        for name in re.findall(r"Name:\s+(GLIBC_\S+)", needs):
            suffix = name.removeprefix("GLIBC_")
            if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)+", suffix):
                raise ValueError(f"{path}: unsupported host requirement {name}")
            required = version(suffix)
            if required > glibc_max:
                ceiling = ".".join(map(str, glibc_max))
                raise ValueError(f"{path}: requires {name}, above glibc {ceiling}")
            requirements.append(required)
    return max(requirements, default=())


def check_appdir(folder, architecture, glibc_max=None):
    folder = Path(folder).resolve()
    for executable in ("EditHere", "edithere-cli"):
        path = folder / "usr/bin" / executable
        if not path.is_file():
            raise ValueError(f"Missing packaged executable: {executable}")
        with path.open("rb") as source:
            if source.read(4) != b"\x7fELF":
                raise ValueError(f"Packaged executable is not ELF: {executable}")
    seen = set()
    highest = ()
    count = 0
    for path in sorted(folder.rglob("*")):
        if not path.is_file() or path.resolve() in seen:
            continue
        seen.add(path.resolve())
        needed = inspect_elf(path, architecture, glibc_max)
        if needed is None:
            continue
        if folder not in path.resolve().parents:
            raise ValueError(f"Packaged ELF points outside the AppDir: {path}")
        count += 1
        highest = max(highest, needed)
    if not count:
        raise ValueError("No ELF files found in the AppDir")
    return count, highest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appdir", type=Path)
    parser.add_argument("--architecture", required=True, choices=MACHINES)
    parser.add_argument("--glibc-max", type=version)
    args = parser.parse_args()
    try:
        count, highest = check_appdir(args.appdir, args.architecture, args.glibc_max)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(f"Packaged ELF architecture: {args.architecture}, {count} files checked")
    if args.glibc_max:
        print("Highest required glibc:", ".".join(map(str, highest)) or "none")
    return 0


if __name__ == "__main__":
    sys.exit(main())
