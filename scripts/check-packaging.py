#!/usr/bin/env python3
"""Guard the packaging, release and translation artefacts against known regressions.

Every check here exists because the project shipped (or nearly shipped) a broken
build once. The corresponding incident is recorded in docs/REGRESSIONS.md; the
check name is the one used in that page's "回归检查" column.

The checks deliberately avoid Qt, NSIS and any network access so the script can
run on every CI job. When ``makensis`` is available the installer is compiled for
real; otherwise only the source-level assertions run and the skip is reported.

Usage::

    python scripts/check-packaging.py [-v]

Exit code 0 means every check passed.
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Build output, vendored trees, and the two directories that keep historical
# work logs: those quote old versions and old broken snippets on purpose.
SKIP_DIRS = {
    ".git", ".tools", ".workbuddy", ".trae", "build", "build-local",
    "build-macos", "dist", "node_modules", "artifacts", "__pycache__",
}

Problem = str


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def project_version() -> str:
    match = re.search(
        r"project\(\s*EditHere\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", read(ROOT / "CMakeLists.txt")
    )
    if not match:
        raise SystemExit("error: CMakeLists.txt does not declare project(EditHere VERSION x.y.z)")
    return match.group(1)


def as_tuple(version: str) -> tuple[int, ...]:
    return tuple(int(part) for part in version.split("."))


def tracked(pattern: str, root: Path | None = None) -> list[Path]:
    """Match a file-name pattern while pruning build output and vendored trees."""
    base = root or ROOT
    matches: list[Path] = []
    for current, directories, files in os.walk(base):
        directories[:] = [name for name in directories if name not in SKIP_DIRS]
        directory = Path(current)
        matches += [directory / name for name in files if fnmatch.fnmatch(name, pattern)]
    return sorted(matches)


# --------------------------------------------------------------------------- #
# Packaging and installer
# --------------------------------------------------------------------------- #
def check_ps1_utf8_bom() -> list[Problem]:
    """REG-006  PowerShell 5.1 decodes BOM-less UTF-8 as ANSI and mangles Chinese."""
    problems = []
    for path in tracked("*.ps1"):
        data = path.read_bytes()
        if all(byte < 0x80 for byte in data):
            continue  # Pure ASCII parses identically either way.
        if not data.startswith(b"\xef\xbb\xbf"):
            problems.append(
                f"{path.relative_to(ROOT).as_posix()}: contains non-ASCII text but has no "
                f"UTF-8 BOM; Windows PowerShell 5.1 would decode it as ANSI. "
                f"Save it as UTF-8 with BOM."
            )
    return problems


NSI = ROOT / "packaging" / "windows" / "edithere.nsi"
INTEGRATE = ROOT / "packaging" / "windows" / "integrate.ps1"


def check_nsi_source() -> list[Problem]:
    """REG-007  ${IfNotErrors} does not exist in NSIS LogicLib."""
    problems = []
    source = read(NSI)
    if "${IfNotErrors}" in source:
        problems.append(
            "packaging/windows/edithere.nsi: ${IfNotErrors} is not an NSIS LogicLib macro; "
            "use ${IfNot} ${Errors}."
        )
    if "${IfNot} ${Errors}" not in source:
        problems.append(
            "packaging/windows/edithere.nsi: the /UPDATE argument test lost its "
            "${IfNot} ${Errors} guard."
        )
    return problems


def check_installer_wait() -> list[Problem]:
    """REG-004/005  The installer waits instead of aborting, and restores the app."""
    problems = []
    source = read(NSI)
    if "!macro EnsureEditHereClosed" not in source:
        problems.append("edithere.nsi: the EnsureEditHereClosed macro is gone.")
    if "-Mode Close" not in source:
        problems.append(
            "edithere.nsi: nothing asks the running instance to exit (-Mode Close); "
            "the installer would go back to demanding a manual exit."
        )
    # The silent path must wait long enough and must not leave the user with a
    # closed application when the update cannot be applied (REG-004).
    if "StrCpy $2 90" not in source:
        problems.append("edithere.nsi: the silent install no longer waits 90 seconds.")
    if 'Exec \'"$INSTDIR\\EditHere.exe" --autostart\'' not in source:
        problems.append(
            "edithere.nsi: a failed in-app update no longer relaunches the application, "
            "leaving the user with nothing running."
        )
    return problems


def find_makensis() -> str | None:
    from_env = os.environ.get("NSIS_MAKENSIS")
    if from_env and Path(from_env).is_file():
        return from_env
    found = shutil.which("makensis")
    if found:
        return found
    for base in ("ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"):
        directory = os.environ.get(base)
        if not directory:
            continue
        candidate = Path(directory) / "NSIS" / "makensis.exe"
        if candidate.is_file():
            return str(candidate)
    return None


def compile_installer(version: str, verbose: bool) -> tuple[list[Problem], str]:
    """Compile edithere.nsi for real. Returns (problems, note)."""
    makensis = find_makensis()
    if not makensis:
        return [], "makensis not found; the installer source was only linted, not compiled"
    package = Path(tempfile.mkdtemp(prefix="edithere-nsi-package-"))
    for relative in ("EditHere.exe", "edithere-cli.exe", "version.txt"):
        (package / relative).write_bytes(b"")
    (package / "version.txt").write_text(version + "\n", encoding="utf-8")
    shutil.copyfile(INTEGRATE, package / "integrate.ps1")
    (package / "skills" / "edithere").mkdir(parents=True)
    (package / "skills" / "edithere" / "SKILL.md").write_text("# stub\n", encoding="utf-8")
    remove_include = package.parent / "remove-files.nsh"
    remove_include.write_text('Delete "$INSTDIR\\EditHere.exe"\n', encoding="utf-8")
    output = package.parent / "EditHere-installer-check.exe"
    command = [
        makensis, "/V2", "/WX", "/INPUTCHARSET", "UTF8",
        f"/DAPP_VERSION={version}",
        f"/DPROJECT_ROOT={ROOT}",
        f"/DPACKAGE_DIR={package}",
        f"/DOUTPUT_FILE={output}",
        f"/DREMOVE_INCLUDE={remove_include}",
        str(NSI),
    ]
    result = subprocess.run(command, capture_output=True, text=True, errors="replace")
    if verbose:
        print("    " + " ".join(command))
        print("    " + (result.stdout or "").strip())
    shutil.rmtree(package, ignore_errors=True)
    remove_include.unlink(missing_ok=True)
    if result.returncode != 0:
        detail = (result.stdout or result.stderr or "").strip().splitlines()
        tail = " | ".join(detail[-4:])
        return [f"edithere.nsi does not compile with /WX: {tail}"], "makensis compile failed"
    return [], f"installer compiled with {Path(makensis).name}"


def check_portable_zip_flat() -> list[Problem]:
    """REG-001  A wrapped top-level folder breaks the in-app portable update."""
    problems = []
    script = read(ROOT / "scripts" / "package-windows.ps1")
    if "Compress-Archive -Path (Join-Path $outputPath '*')" not in script:
        problems.append(
            "scripts/package-windows.ps1: the portable archive is no longer flattened "
            "(Compress-Archive must use -Path (Join-Path $outputPath '*')). A wrapped "
            "folder makes 'tar -xf' land the new files in a nested subdirectory."
        )
    # The in-app updater extracts straight onto the application directory, so the
    # executable has to sit at the archive root.
    for archive in sorted((ROOT / "dist").glob("*.zip")) if (ROOT / "dist").is_dir() else []:
        with zipfile.ZipFile(archive) as handle:
            names = handle.namelist()
        if "EditHere.exe" not in names:
            problems.append(
                f"{archive.relative_to(ROOT).as_posix()}: EditHere.exe is not at the archive "
                f"root (top level holds {sorted({n.split('/')[0] for n in names})[:4]})."
            )
    return problems


# --------------------------------------------------------------------------- #
# Release pipeline
# --------------------------------------------------------------------------- #
RELEASE = ROOT / ".github" / "workflows" / "release.yml"
MACOS_WORKFLOW = ROOT / ".github" / "workflows" / "native-macos.yml"
LINUX_WORKFLOW = ROOT / ".github" / "workflows" / "native-linux.yml"
LINUX_APPIMAGE = "EditHere-linux-x86_64.AppImage"


def check_release_assets() -> list[Problem]:
    """REG-002/012/122  Version-less asset names, per-file hashes, every platform."""
    problems = []
    workflow = read(RELEASE)
    for pattern, stable in (
        ("EditHere-*-win-x64-setup.exe", "EditHere-win-x64-setup.exe"),
        ("EditHere-*-win-x64.zip", "EditHere-win-x64.zip"),
        ("EditHere-*-macos-universal.dmg", "EditHere-macos-universal.dmg"),
    ):
        if f'mv -f "$f" {stable}' not in workflow or pattern not in workflow:
            problems.append(
                f"release.yml: {pattern} is no longer published as the version-less name "
                f"{stable}; /releases/latest/download links would break again."
            )
    per_file = re.search(r"for f in ([^;]+); do sha256sum", workflow)
    hashed = per_file.group(1).split() if per_file else []
    for needed in ("*.zip", "*.exe", "*.AppImage"):
        if needed not in hashed:
            problems.append(
                f"release.yml: {needed} no longer gets a per-file .sha256; the "
                f"updater and the download page both point at that checksum file."
            )
    problems += check_linux_release_package(workflow)
    checker = read(ROOT / "app" / "updatechecker.cpp")
    for bare in ('EditHere-win-x64-setup.exe', 'EditHere-win-x64.zip'):
        if bare not in checker:
            problems.append(
                f"app/updatechecker.cpp: no fallback for the version-less asset name {bare}; "
                f"clients could not update from a release published with stable names."
            )
    if 'QString("EditHere-%1-win-x64' not in checker:
        problems.append(
            "app/updatechecker.cpp: the versioned asset lookup disappeared; 0.9.4-era "
            "releases only carry versioned names."
        )
    return problems


def check_linux_release_package(release_workflow: str) -> list[Problem]:
    """REG-122  A tag has to publish the Linux package, not just build it on demand.

    0.9.9's release notes promised an AppImage while no job ever built one: the
    Linux workflow had no tag trigger and the release job only waited on the
    Windows and macOS builds. A Linux build that nothing attaches to a release
    is not a package, so the whole chain is pinned here.

    Comparisons are whole-line on purpose: ``EditHere-linux-x86_64.AppImage`` is
    a prefix of its own ``.sha256`` sibling, and a substring test would let a
    broken pipeline pass on the checksum file alone.
    """
    problems = []
    linux_name = LINUX_WORKFLOW.relative_to(ROOT).as_posix()
    linux = read(LINUX_WORKFLOW)
    linux_lines = [line.strip() for line in linux.splitlines()]
    release_lines = [line.strip() for line in release_workflow.splitlines()]
    if "workflow_call:" not in linux_lines:
        problems.append(
            f"{linux_name}: no workflow_call trigger, so release.yml cannot reuse the "
            f"Linux build."
        )
    if f"uses: ./{linux_name}" not in release_lines:
        problems.append(
            "release.yml: the Linux workflow is no longer called, so a tag builds no "
            "AppImage and the release ships without one again."
        )
    for wanted, why in (
        ("name: linux-package", "the package artefact has no name of its own"),
        (f"dist/linux-*/{LINUX_APPIMAGE}", "the AppImage is not collected from the build"),
        ("dist/linux-*/edithere-cli", "the CLI wrapper is not collected from the build"),
    ):
        if wanted not in linux_lines:
            problems.append(f"{linux_name}: {why}.")
    required = [line for line in release_lines if line.startswith("packages=")]
    if not required or LINUX_APPIMAGE not in required[0]:
        problems.append(
            f"release.yml: {LINUX_APPIMAGE} is not one of the packages the release "
            f"insists on, so a tag can still publish without Linux."
        )
    if "artifacts/*.AppImage" not in release_lines:
        problems.append(
            f"release.yml: the AppImage is not attached to the release assets, so the "
            f"release would look complete while Linux is missing."
        )
    return problems

def check_release_notes() -> list[Problem]:
    """REG-011  Generated notes only list merged pull requests, and this repo pushes commits."""
    problems = []
    workflow = read(RELEASE)
    if "body_path: release-notes.md" not in workflow:
        problems.append(
            "release.yml: the release no longer uses body_path from release-notes.md, so the "
            "notes fall back to an empty 'What's Changed' plus a compare link."
        )
    if "CHANGELOG.md" not in workflow:
        problems.append(
            "release.yml: the step that extracts the changelog section for this version is gone."
        )
    # REG-153  A release page is one Markdown body with no language switch, so the
    # English notes have to be folded into the Chinese body rather than replacing
    # or trailing it. Losing that splice silently ships a Chinese-only release.
    if "docs/releases/%s.en.md" not in workflow or "<summary>English</summary>" not in workflow:
        problems.append(
            "release.yml: the release notes are no longer assembled from the Chinese page and "
            "the English page, so a tag would publish a Chinese-only release body."
        )
    return problems


def check_version_consistency(version: str) -> list[Problem]:
    """REG-056/057  One version source, and every restatement agrees with it."""
    problems = []
    changelog = read(ROOT / "CHANGELOG.md")
    headings = [line for line in changelog.splitlines() if line.startswith("## ")]
    if not any(re.match(rf"^##\s+v?{re.escape(version)}(\s|$)", line) for line in headings):
        problems.append(
            f"CHANGELOG.md: no '## {version} ...' section. The release workflow extracts the "
            f"section whose heading starts with the tag version, so the release notes would be "
            f"empty. Headings found: {headings[:6]}"
        )
    # Runtime restatements of the shipped version, e.g. "当前程序 0.9.4".
    for path in tracked("*.md"):
        for match in re.finditer(r"当前程序\s*v?([0-9]+\.[0-9]+\.[0-9]+)", read(path)):
            if match.group(1) != version:
                problems.append(
                    f"{path.relative_to(ROOT).as_posix()}: says 当前程序 {match.group(1)} while "
                    f"CMakeLists.txt declares {version}."
                )
    for path in tracked("*.md"):
        for match in re.finditer(r"v([0-9]+\.[0-9]+\.[0-9]+)\s*(?:Windows 安装器|安装器|版本)", read(path)):
            if match.group(1) != version:
                problems.append(
                    f"{path.relative_to(ROOT).as_posix()}: refers to installer version "
                    f"{match.group(1)} while CMakeLists.txt declares {version}."
                )
    return problems


def check_download_links() -> list[Problem]:
    """REG-012  Documentation must not pin a release that can be deleted."""
    problems = []
    for path in tracked("*.md"):
        text = read(path)
        for match in re.finditer(r"releases/(?:download|tag)/v[0-9]", text):
            line = text[: match.start()].count("\n") + 1
            problems.append(
                f"{path.relative_to(ROOT).as_posix()}:{line}: links to a version-pinned release "
                f"URL; deleted releases turn it into a dead link. Use "
                f"/releases/latest/download/<stable name>."
            )
    for name in ("README.md", "README.en.md"):
        if "releases/latest/download/" not in read(ROOT / name):
            problems.append(f"{name}: no /releases/latest/download/ link left in the download section.")
    return problems


def check_updater_stale_download() -> list[Problem]:
    """REG-003  The download writer appends, so a leftover file corrupts the package."""
    source = read(ROOT / "app" / "updatechecker.cpp")
    body = source[source.find("void UpdateChecker::downloadAndInstall") :]
    body = body[: body.find("\nvoid UpdateChecker::verifyAndInstall")]
    append_at = body.find("QIODevice::Append")
    if append_at < 0:
        return []  # Rewritten to a truncating writer; nothing to guard here.
    if "QFile::remove(downloadPath_)" in body[:append_at]:
        return []
    # The removal may live in the helper that resolves the target instead.
    helper = re.search(
        r"QString UpdateChecker::prepareDownloadTarget\([^)]*\)\s*\{(?P<body>.*?)\n\}", source, re.S
    )
    if helper and "QFile::remove" in helper.group("body") and "prepareDownloadTarget(" in body[:append_at]:
        return []
    return [
        "app/updatechecker.cpp: the leftover download is not removed before the first append; a "
        "second attempt would concatenate two packages and fail the SHA256 check. See "
        "tests/update_test.cpp::staleDownloadIsDroppedBeforeAppending."
    ]


def check_quit_capability_pair(version: str) -> list[Problem]:
    """REG-008  Sending --quit to a build without the server side triggers a capture."""
    problems = []
    main_cpp = read(ROOT / "app" / "main.cpp")
    if '"--quit"' not in main_cpp:
        problems.append("app/main.cpp: the --quit client flag is missing.")
    if 'text == "quit"' not in main_cpp:
        problems.append(
            "app/main.cpp: the socket no longer handles 'quit'; --quit would stop having an "
            "effect on the running instance."
        )
    integrate = read(INTEGRATE)
    match = re.search(r"\$quitRequestSince\s*=\s*\[version\]'([0-9.]+)'", integrate)
    if not match:
        problems.append(
            "packaging/windows/integrate.ps1: $quitRequestSince is gone; every installation "
            "would be asked to quit, including builds where --quit triggers a capture."
        )
    elif as_tuple(match.group(1)) > as_tuple(version):
        problems.append(
            f"packaging/windows/integrate.ps1: $quitRequestSince={match.group(1)} is newer than "
            f"the current version {version}; no installation would ever be asked to exit "
            f"gracefully. Update the constant together with the version bump."
        )
    return problems


def check_ci_toolchain() -> list[Problem]:
    """REG-009/010  aqt tool category rename and a missing NSIS on the runner."""
    problems = []
    release = read(RELEASE)
    macos = read(MACOS_WORKFLOW)
    if "tools_mingw1310" not in release or "qt.tools.win64_mingw1310" not in release:
        problems.append(
            "release.yml: the aqtinstall MinGW tool category/variant pair changed; 3.x uses "
            "tools_mingw1310 / qt.tools.win64_mingw1310."
        )
    if "choco install nsis" not in release:
        problems.append(
            "release.yml: NSIS is no longer installed on the runner, so the installer step fails."
        )
    for name, workflow in (("release.yml", release), ("native-macos.yml", macos)):
        for line in workflow.splitlines():
            if "install-qt" in line and "--archives" in line and "qttranslations" not in line:
                problems.append(
                    f"{name}: the aqt --archives list no longer contains qttranslations; "
                    f"translations/ is a separate archive and Qt's own Chinese strings "
                    f"would be missing from the package."
                )
    return problems


# --------------------------------------------------------------------------- #
# Translations
# --------------------------------------------------------------------------- #
def check_qt_translations() -> list[Problem]:
    """REG-018/019/020  Qt's own catalogue must be deployed, under either name."""
    problems = []
    packager = read(ROOT / "scripts" / "package-windows.ps1")
    if "qtbase_zh_CN.qm" not in packager:
        problems.append(
            "scripts/package-windows.ps1: the Qt Chinese catalogue is no longer copied, so file "
            "dialogs and message box buttons stay English in the Chinese interface."
        )
    if "Simplified Chinese is missing" not in packager:
        problems.append(
            "scripts/package-windows.ps1: the missing-catalogue guard was removed; packaging "
            "would silently produce an English-chrome build."
        )
    i18n = read(ROOT / "app" / "i18n.cpp")
    if '"qtbase_" + installedLocale' not in i18n or '"qt_" + installedLocale' not in i18n:
        problems.append(
            "app/i18n.cpp: the Qt catalogue lookup no longer tries 'qtbase_<locale>' and "
            "'qt_<locale>'; windeployqt renames the file, so one of the two layouts would fail."
        )
    return problems


def check_translations_in_qm() -> list[Problem]:
    """REG-022  The committed .qm is what CI embeds: it must match the .ts."""
    problems = []
    for ts in sorted((ROOT / "app" / "translations").glob("edithere_*.ts")):
        qm = ts.parent / "built" / f"{ts.stem}.qm"
        if not qm.is_file():
            problems.append(
                f"{qm.relative_to(ROOT).as_posix()}: missing; CI has no Linguist tools and falls "
                f"back to this committed file."
            )
            continue
        blob = qm.read_bytes()
        missing_source: list[str] = []
        missing_translation: list[str] = []
        empty: list[str] = []
        unfinished: list[str] = []
        for context in ET.parse(ts).getroot().findall("context"):
            for message in context.findall("message"):
                source = message.findtext("source") or ""
                node = message.find("translation")
                text = "" if node is None else (node.text or "")
                if node is not None and node.get("type") == "unfinished":
                    unfinished.append(source)
                if not text:
                    empty.append(source)
                    continue
                if source.encode("utf-8") not in blob:
                    missing_source.append(source)
                if text.encode("utf-16-be") not in blob and text.encode("utf-8") not in blob:
                    missing_translation.append(source)
        for label, items in (
            ("untranslated", empty),
            ("unfinished", unfinished),
            ("source string absent from the .qm (run lrelease)", missing_source),
            ("translation absent from the .qm (run lrelease)", missing_translation),
        ):
            for item in items[:3]:
                problems.append(f"{ts.name}: {label}: {item!r}")
            if len(items) > 3:
                problems.append(f"{ts.name}: {label}: ... and {len(items) - 3} more")
    return problems


def check_english_names() -> list[Problem]:
    """REG-023  One feature keeps one English name in the interface and the README."""
    problems = []
    stale = "Exploded view"
    if stale in read(ROOT / "README.en.md"):
        problems.append(
            f"README.en.md: still calls the feature {stale!r}; the interface, the code "
            f"identifiers and README.en.md all use 'Explode'."
        )
    for ts in sorted((ROOT / "app" / "translations").glob("edithere_*.ts")):
        if stale in read(ts):
            problems.append(
                f"{ts.relative_to(ROOT).as_posix()}: still translates 大爆炸 as {stale!r}."
            )
    if "Explode" not in read(ROOT / "README.en.md"):
        problems.append("README.en.md: the public English name 'Explode' disappeared.")
    # REG-154  长截图 reads "Scrolling capture" on the toolbar button, in the tray
    # menu, on the settings page and in the progress window title. Three strings
    # said "long capture" instead, so the same feature was named two ways
    # depending on which message you happened to hit. 超长截图 is a different
    # feature and keeps "Ultra-long capture".
    for ts in sorted((ROOT / "app" / "translations").glob("edithere_*.ts")):
        for context in ET.parse(ts).getroot().findall("context"):
            for message in context.findall("message"):
                source = message.findtext("source") or ""
                if "长截图" not in source or "超长" in source:
                    continue
                node = message.find("translation")
                translation = "" if node is None else (node.text or "")
                lowered = translation.lower()
                if "long capture" in lowered and "scrolling capture" not in lowered:
                    problems.append(
                        f"{ts.name}: {source!r} is translated as {translation!r}; 长截图 is "
                        f"'Scrolling capture' everywhere else. Give the feature one English name."
                    )
    if "scrolling capture" not in read(ROOT / "README.en.md").lower():
        problems.append("README.en.md: the public English name 'Scrolling capture' disappeared.")
    # REG-155  README.en.md quotes this button as the reader will look for it on
    # screen, but the catalogue shipped "Done, back to AI" while the README
    # advertised "Finish and return to AI": one button under two English names.
    wanted = "Finish and return to AI"
    for ts in sorted((ROOT / "app" / "translations").glob("edithere_*.ts")):
        for context in ET.parse(ts).getroot().findall("context"):
            for message in context.findall("message"):
                if (message.findtext("source") or "") != "完成并返回 AI":
                    continue
                node = message.find("translation")
                translation = "" if node is None else (node.text or "")
                if translation != wanted:
                    problems.append(
                        f"{ts.name}: 完成并返回 AI is translated as {translation!r}, but "
                        f"README.en.md quotes this button as {wanted!r}."
                    )
    if wanted not in read(ROOT / "README.en.md"):
        problems.append(f"README.en.md: no longer quotes the interface button {wanted!r}.")
    return problems


def check_ocr_bridge() -> list[Problem]:
    """REG-059..061  The PowerShell OCR bridge survives the way it has to be shipped."""
    problems = []
    bridge = ROOT / "app" / "ocrbridge.ps1"
    if not bridge.is_file():
        return [
            "app/ocrbridge.ps1: missing; the Windows reader is a PowerShell bridge because the "
            "release builds with MinGW, where C++/WinRT is not available."
        ]
    raw = bridge.read_bytes()
    if raw[:3] == b"\xef\xbb\xbf":
        problems.append(
            "app/ocrbridge.ps1: starts with a UTF-8 BOM. It is passed to powershell.exe as a "
            "Base64 UTF-16LE string, so a BOM is decoded as a stray character at the top of the "
            "script and the call fails."
        )
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as error:
        problems.append(
            f"app/ocrbridge.ps1: is not pure ASCII ({error}); non-ASCII text cannot survive the "
            f"Base64 command line intact and turns into mangled output."
        )
        return problems

    placeholders = ("__EDITHERE_IMAGE__", "__EDITHERE_OUTPUT__", "__EDITHERE_LANGUAGE__")
    for placeholder in placeholders:
        if placeholder not in text:
            problems.append(
                f"app/ocrbridge.ps1: the placeholder {placeholder} disappeared; "
                f"app/ocr.cpp substitutes it before running the bridge and would hand the reader "
                f"an empty path."
            )
    # The script is embedded as a Qt resource, which is what the tests and the application
    # both read; if that line goes, the bridge ships as nothing.
    if "app/ocrbridge.ps1" not in read(ROOT / "CMakeLists.txt"):
        problems.append(
            "CMakeLists.txt: app/ocrbridge.ps1 is no longer added as a Qt resource, so the "
            "reader would find no script at runtime."
        )
    ocr_cpp = read(ROOT / "app" / "ocr.cpp")
    for placeholder in placeholders:
        if placeholder not in ocr_cpp:
            problems.append(
                f"app/ocr.cpp: no longer substitutes {placeholder}; the bridge would run against "
                f"the placeholder text instead of the real path."
            )
    # Windows Runtime file APIs reject the forward slashes Qt hands out, so the path has to
    # be converted before it reaches the command line. See REG-060.
    if "QDir::toNativeSeparators" not in ocr_cpp:
        problems.append(
            "app/ocr.cpp: no longer converts paths with QDir::toNativeSeparators; the Windows "
            "Runtime file API rejects 'C:/...' with UNABLE_TO_MASK_PATH."
        )
    return problems


# --------------------------------------------------------------------------- #
# Feedback format documentation
# --------------------------------------------------------------------------- #
FEEDBACK_DOCS = (
    ROOT / "docs" / "AGENT-CLI.md",
    ROOT / "docs" / "AGENT-CLI.en.md",
    ROOT / "skills" / "edithere" / "SKILL.md",
    ROOT / "connector" / "skills" / "edithere" / "SKILL.md",
    ROOT / "trae-plugin" / "skills" / "edithere" / "SKILL.md",
)
FEEDBACK_PRODUCER_KEYS = ("annotationSpace", "objects", "movements", "annotations")


def check_feedback_docs() -> list[Problem]:
    """REG-111  Every page explaining the feedback must name the fields the exporter writes."""
    problems = []
    source = read(ROOT / "app" / "model.cpp")
    start = source.find("QJsonObject exportFeedback")
    end = source.find("QByteArray serializeFeedback")
    if start < 0 or end < start:
        return [
            "app/model.cpp: exportFeedback is no longer recognisable, so the shipped pages cannot be "
            "checked against it."
        ]
    exporter = source[start:end]
    for key in FEEDBACK_PRODUCER_KEYS:
        if f'"{key}"' not in exporter:
            problems.append(
                f"app/model.cpp: exportFeedback no longer writes {key!r}. The feedback structure "
                f"changed, so docs/AGENT-CLI.md and the three skills/edithere copies have to be "
                f"updated in the same commit."
            )
    for path in FEEDBACK_DOCS:
        name = path.relative_to(ROOT).as_posix()
        if "objects" not in read(path):
            problems.append(
                f"{name}: explains the feedback with the legacy annotations/changes pair only. The "
                f"exporter writes objects, so a reader following this page finds no notes at all."
            )
    guide = read(ROOT / "docs" / "AGENT-CLI.md")
    for token in ("objects[].source", "objects[].movements", "objects[].annotations"):
        if token not in guide:
            problems.append(
                f"docs/AGENT-CLI.md: the field table no longer documents {token}; it is the page AI "
                f"readers follow to interpret a feedback file."
            )
    # Both packages rewrite the ../schema/ links and ship the directory, so a linked schema that
    # does not exist becomes a dead link inside the installed package (REG-012).
    for match in re.finditer(r"\.\./schema/([\w.\-]+\.schema\.json)", guide):
        if not (ROOT / "schema" / match.group(1)).is_file():
            problems.append(f"docs/AGENT-CLI.md: links to schema/{match.group(1)}, which does not exist.")
    return problems


LAYOUT_SCHEMAS = ("project-v3.schema.json", "feedback-v2.schema.json")


def check_layout_schema() -> list[Problem]:
    """REG-112  The published layout schema must accept the groups exportLayout writes."""
    problems = []
    layout_cpp = read(ROOT / "app" / "layout.cpp")
    if '"manual", sourceBounds' not in layout_cpp:
        # A region made by hand no longer records its source bounds, so nothing to guard here.
        return problems
    for name in LAYOUT_SCHEMAS:
        path = ROOT / "schema" / name
        try:
            group = json.loads(read(path))["$defs"]["layoutGroup"]
        except (json.JSONDecodeError, KeyError) as error:
            problems.append(f"schema/{name}: the layoutGroup definition is unreadable ({error}).")
            continue
        for rule in group.get("allOf", []):
            origin = rule.get("if", {}).get("properties", {}).get("origin", {})
            if origin.get("const") != "manual":
                continue
            branch = json.dumps(rule.get("then", {}), ensure_ascii=False)
            if '"type": "null"' in branch and "layoutRectangle" not in branch:
                problems.append(
                    f"schema/{name}: the manual-group branch forces originalRectangle to null, but "
                    f"app/layout.cpp::addLayoutRegion records the members' source bounds so that a "
                    f"later piece removal cannot move the region. Every exported project would be "
                    f"rejected by scripts/validate-exports.py, which the macOS build runs."
                )
    return problems


# --------------------------------------------------------------------------- #
# Build recipe
# --------------------------------------------------------------------------- #
def check_build_recipe() -> list[Problem]:
    """REG-058  The Visual Studio generator crashes on this project's environment."""
    problems = []
    windows_script = read(ROOT / "scripts" / "build-windows.ps1")
    if '"-G", "Ninja"' not in windows_script:
        problems.append(
            "scripts/build-windows.ps1: no longer configured with -G Ninja; the Visual Studio "
            "generator fails with MSB6001 on machines that carry both Path and PATH."
        )
    candidates = tracked("*.md") + tracked("*.yml") + tracked("*.ps1") + tracked("*.sh")
    for path in candidates:
        text = read(path)
        for match in re.finditer(r'-G\s+"?Visual Studio', text):
            line = text[: match.start()].count("\n") + 1
            problems.append(
                f"{path.relative_to(ROOT).as_posix()}:{line}: documents or uses the Visual "
                f"Studio generator, which cannot build this project here. Use Ninja."
            )
    return problems


# --------------------------------------------------------------------------- #
# Translated documentation
# --------------------------------------------------------------------------- #
# Every one of these pages is published in English beside its Chinese original.
# docs/releases/<version>.md is handled separately: it is a fragment assembled by
# release.yml, not a standalone page with a switcher.
TRANSLATED_DOCS = (
    "docs/USER-GUIDE.md",
    "docs/LONG-CAPTURE.md",
    "docs/VIDEO-ANNOTATION.md",
    "docs/AGENT-CLI.md",
    "docs/AI-SETUP.md",
    "docs/LINUX.md",
    "docs/DEVELOPMENT.md",
)


def check_translated_docs(version: str) -> list[Problem]:
    """REG-151/152  The English documentation set stays complete and reachable.

    Nothing in the build consumes these pages, so without a check they rot
    silently in two directions: a new Chinese page ships with no translation, or
    an English page exists but nothing links to it, which for a reader is the
    same as not having been translated. The switchover itself is guarded because
    a page pair with no visible link between the two halves is undiscoverable.
    """
    problems: list[Problem] = []
    for chinese in TRANSLATED_DOCS:
        english = chinese[:-3] + ".en.md"
        if not (ROOT / english).is_file():
            problems.append(
                f"{english}: missing. {chinese} is published in English as well; without the "
                f"translation an English reader reaches a dead end."
            )
            continue
        if Path(chinese).name not in read(ROOT / english):
            problems.append(
                f"{english}: no link back to {Path(chinese).name}. The two "
                f"language variants are only discoverable through that switcher."
            )
        if Path(english).name not in read(ROOT / chinese):
            problems.append(
                f"{chinese}: no link to {Path(english).name}; a reader of the "
                f"Chinese page cannot find the English one."
            )
    # The release notes for the shipped version are bilingual too.
    for name in ("docs/releases/%s.md" % version, "docs/releases/%s.en.md" % version):
        if not (ROOT / name).is_file():
            problems.append(
                f"{name}: missing. Every release publishes its notes in both languages, and "
                f"release.yml folds the English page into the Chinese body."
            )
    # An English page that no English page links to is a file, not a translation.
    readme = read(ROOT / "README.en.md")
    for chinese in TRANSLATED_DOCS:
        english = Path(chinese).name[:-3] + ".en.md"
        if english not in readme:
            problems.append(
                f"README.en.md: never links {english}, so that English page cannot be reached "
                f"from the English entry point."
            )
    return problems


DOC_LINK = re.compile(r"\]\((?!https?:|mailto:)(?P<target>[^)\s#]*)(?P<anchor>#[^)\s]*)?\)")


def github_slug(heading: str) -> set[str]:
    """Every slug GitHub may produce for a heading.

    Two forms are accepted because reproducing GitHub's rule for CJK punctuation
    exactly is not worth it: one keeps every non-ASCII character, the other keeps
    only letters and digits. An anchor written from the real heading matches one
    of the two, while a genuinely different word ("limits" for "boundaries")
    matches neither.
    """
    text = re.sub(r"`([^`]*)`", r"\1", heading.strip().lower())
    forms: set[str] = set()
    for keep_punctuation in (True, False):
        characters: list[str] = []
        for character in text:
            if character.isalnum() or character in "-_":
                characters.append(character)
            elif ord(character) > 0x2FFF and keep_punctuation:
                characters.append(character)
            elif character.isspace():
                characters.append("-")
        forms.add("".join(characters))
    return forms


def check_doc_links() -> list[Problem]:
    """REG-156  A documentation link has to land on something.

    The English pages and the Chinese originals point at each other, and several
    pages link straight into a heading of another page. Nothing consumes Markdown
    at build time, so a reworded heading or a mistyped file name cuts a reader's
    only route to the other language while every test stays green. Targets under
    build output or regenerated evidence are exempt: those files exist only after
    a run, so not finding them here is not a broken link.
    """
    problems: list[Problem] = []
    pages = tracked("*.md")
    headings: dict[Path, set[str]] = {}
    for page in pages:
        found: set[str] = set()
        for line in read(page).splitlines():
            heading = re.match(r"^#{1,6}\s+(.*?)\s*$", line)
            if heading:
                found |= github_slug(heading.group(1))
        headings[page.resolve()] = found
    for page in pages:
        name = page.relative_to(ROOT).as_posix()
        for match in DOC_LINK.finditer(read(page)):
            target, anchor = match.group("target"), match.group("anchor")
            if target and not target.endswith(".md"):
                continue
            if not target and not anchor:
                continue
            destination = (page.parent / target).resolve() if target else page.resolve()
            relative = destination.relative_to(ROOT) if destination.is_relative_to(ROOT) else None
            regenerated = relative is not None and any(part in SKIP_DIRS for part in relative.parts)
            if regenerated:
                continue
            if not destination.is_file():
                problems.append(f"{name}: links to {target}, which does not exist.")
                continue
            if not anchor:
                continue
            if anchor[1:].lower() not in headings.get(destination, set()):
                problems.append(
                    f"{name}: {anchor} is not a heading in {target}; a reader following the "
                    f"link lands at the top of the page instead."
                )
    return problems


CHECKS = [
    ("ps1_utf8_bom", check_ps1_utf8_bom),
    ("nsi_source", check_nsi_source),
    ("installer_wait", check_installer_wait),
    ("portable_zip_flat", check_portable_zip_flat),
    ("release_assets", check_release_assets),
    ("release_notes", check_release_notes),
    ("download_links", check_download_links),
    ("stale_download", check_updater_stale_download),
    ("qt_translations", check_qt_translations),
    ("translations_in_qm", check_translations_in_qm),
    ("english_names", check_english_names),
    ("ci_toolchain", check_ci_toolchain),
    ("ocr_bridge", check_ocr_bridge),
    ("feedback_docs", check_feedback_docs),
    ("layout_schema", check_layout_schema),
    ("build_recipe", check_build_recipe),
    ("doc_links", check_doc_links),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-v", "--verbose", action="store_true", help="print every command that runs")
    parser.add_argument("--no-compile", action="store_true", help="skip the makensis compile")
    args = parser.parse_args()

    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

    version = project_version()
    problems: list[Problem] = []
    notes: list[str] = []

    for name, check in CHECKS:
        found = check()
        problems += [f"[{name}] {problem}" for problem in found]
        if args.verbose:
            print(f"  {name}: {'FAIL' if found else 'ok'}")

    # Version comparison and the installer compile need the version, so they are
    # driven here instead of through the uniform signature above.
    for name, found in (
        ("version_consistency", check_version_consistency(version)),
        ("quit_capability_pair", check_quit_capability_pair(version)),
        ("translated_docs", check_translated_docs(version)),
    ):
        problems += [f"[{name}] {problem}" for problem in found]
        if args.verbose:
            print(f"  {name}: {'FAIL' if found else 'ok'}")

    if not args.no_compile:
        found, note = compile_installer(version, args.verbose)
        problems += [f"[nsi_compile] {problem}" for problem in found]
        notes.append(note)
        if args.verbose:
            print(f"  nsi_compile: {'FAIL' if found else 'ok'} ({note})")

    for note in notes:
        print(f"note: {note}")
    if problems:
        print(f"{len(problems)} problem(s):")
        for problem in problems:
            print(f"  - {problem}")
        return 1
    print(f"packaging OK: {len(CHECKS) + 3} check(s) passed for version {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
