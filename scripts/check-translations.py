#!/usr/bin/env python3
"""Verify the interface translation files stay in sync with the C++ sources.

Checks performed:

1. ``lupdate`` finds exactly the same source strings as the committed ``.ts``
   file, so nothing is left untranslated and nothing is left behind as obsolete.
2. Every translation context is one the runtime can actually look up: ``h2d``,
   ``EditHere``, or ``h2d::<Class>`` for a class that declares ``Q_OBJECT``.
   A plain class or a named namespace inside a ``.cpp`` makes ``lupdate`` derive
   ``h2d::Scope`` while the runtime resolves the file-local ``tr()`` to ``h2d``;
   the string then silently stays untranslated. See docs/i18n.md.
3. No message is translated to an empty string.
4. ``app/translations/built/*.qm`` exists. Its content is compared message by message
   against the ``.ts`` by ``scripts/check-packaging.py``, which needs no Linguist
   tools and therefore also runs in CI; this script does not repeat that check, and
   it deliberately does not compare file modification times, because a fresh
   ``git clone`` writes files in index order and would report a false mismatch.

Usage::

    python scripts/check-translations.py [--qt D:/Qt/6.8.0/msvc2022_64]
    python scripts/check-translations.py --lupdate /path/to/lupdate

Exit code 0 means the translation files are consistent.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "app"
TRANSLATIONS = APP / "translations"
SOURCE_SUFFIXES = (".cpp", ".h", ".mm")
EXPECTED_PLAIN_CONTEXTS = {"h2d", "EditHere"}


def find_tool(qt: str | None, name: str, explicit: str | None = None) -> str:
    """Locate lupdate/lrelease via --lupdate, --qt, QTDIR, or PATH."""
    if explicit:
        if Path(explicit).is_file():
            return explicit
        sys.exit(f"error: {name} does not exist at {explicit}")
    candidates = []
    if qt:
        candidates.append(Path(qt) / "bin" / f"{name}.exe")
        candidates.append(Path(qt) / "bin" / name)
    for env in ("QTDIR", "Qt6_DIR"):
        value = os.environ.get(env)
        if value:
            base = Path(value)
            candidates += [base / "bin" / f"{name}.exe", base / "bin" / name]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    found = shutil.which(name)
    if not found:
        sys.exit(f"error: {name} not found; pass --qt <Qt install directory>")
    return found


def source_files() -> list[str]:
    # lupdate does not pick up Objective-C++ files when scanning a directory, so
    # every file is passed explicitly; the macOS strings must be translated too.
    files = sorted(str(p) for p in APP.iterdir() if p.suffix in SOURCE_SUFFIXES)
    if not files:
        sys.exit(f"error: no C++ sources found under {APP}")
    return files


def q_object_contexts() -> set[str]:
    """Contexts moc can provide: h2d::<Class> for every class with Q_OBJECT."""
    contexts: set[str] = set()
    pattern = re.compile(
        r"class\s+(\w+)\s*(?:final\s*)?(?::[^{]*)?\{(?P<body>[^}]*?Q_OBJECT)", re.S
    )
    for path in sorted(APP.glob("*.h")):
        for match in pattern.finditer(path.read_text(encoding="utf-8")):
            contexts.add(f"h2d::{match.group(1)}")
    return contexts


def read_messages(path: Path) -> list[tuple[str, str, bool]]:
    """Return (context, source, translated) for every message in a .ts file."""
    tree = ET.parse(path)
    messages = []
    for context in tree.getroot().findall("context"):
        name = context.findtext("name") or ""
        for message in context.findall("message"):
            source = message.findtext("source") or ""
            translation = message.find("translation")
            text = "" if translation is None else (translation.text or "")
            unfinished = translation is not None and translation.get("type") == "unfinished"
            messages.append((name, source, bool(text) and not unfinished))
    return messages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt", help="Qt installation directory that holds bin/lupdate")
    parser.add_argument("--lupdate", help="Explicit path to the lupdate executable")
    args = parser.parse_args()

    lupdate = find_tool(args.qt, "lupdate", args.lupdate)
    problems: list[str] = []

    ts_files = sorted(TRANSLATIONS.glob("edithere_*.ts"))
    if not ts_files:
        sys.exit(f"error: no .ts files under {TRANSLATIONS}")

    known = q_object_contexts()
    allowed = EXPECTED_PLAIN_CONTEXTS | known

    with tempfile.TemporaryDirectory() as tmp:
        for ts in ts_files:
            fresh_path = Path(tmp) / ts.name
            shutil.copy(ts, fresh_path)
            result = subprocess.run(
                [lupdate, *source_files(), "-ts", str(fresh_path), "-no-obsolete"],
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                problems.append(f"{ts.name}: lupdate failed\n{result.stdout}{result.stderr}")
                continue

            fresh = read_messages(fresh_path)
            committed = read_messages(ts)

            fresh_keys = [(c, s) for c, s, _ in fresh]
            committed_keys = [(c, s) for c, s, _ in committed]
            missing = [k for k in fresh_keys if k not in committed_keys]
            stale = [k for k in committed_keys if k not in fresh_keys]
            for context, source in missing:
                problems.append(f"{ts.name}: not in the committed file: [{context}] {source!r}")
            for context, source in stale:
                problems.append(f"{ts.name}: stale entry: [{context}] {source!r}")
            if len(fresh_keys) != len(committed_keys):
                problems.append(
                    f"{ts.name}: {len(fresh_keys)} strings in the sources, "
                    f"{len(committed_keys)} in the file"
                )

            for context, source, translated in fresh:
                if context not in allowed:
                    problems.append(
                        f"{ts.name}: context {context!r} is not reachable at runtime "
                        f"(source {source!r}); spell out the context with "
                        f'QCoreApplication::translate("<context>", ...) in the nested scope'
                    )
                if not translated:
                    problems.append(f"{ts.name}: untranslated: [{context}] {source!r}")

    for ts in ts_files:
        qm = TRANSLATIONS / "built" / f"{ts.stem}.qm"
        if not qm.is_file():
            problems.append(f"missing prebuilt translation {qm.relative_to(ROOT)}")

    if problems:
        print(f"{len(problems)} problem(s):")
        for problem in problems:
            print(f"  - {problem}")
        return 1

    total = sum(len(read_messages(ts)) for ts in ts_files)
    print(f"translations OK: {len(ts_files)} file(s), {total} message(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
