"""Collect verbatim license material from matching, unmodified dependency source trees."""
import argparse
import json
import shutil
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--qt-source", type=Path, required=True, help="Matching Qt source directory containing module subdirectories")
parser.add_argument("--qt-modules", nargs="+", default=["qtbase", "qtimageformats", "qtmultimedia"], help="Qt module source trees to collect")
parser.add_argument("--ffmpeg-source", type=Path, help="Matching FFmpeg source directory")
parser.add_argument("--mingw-licenses", type=Path)
args = parser.parse_args()
destination = Path(__file__).resolve().parents[1] / "packaging" / "licenses"
for module in args.qt_modules:
    root = args.qt_source / module
    if not (root / "LICENSES").is_dir():
        raise SystemExit(f"Missing source license directory: {root}")
    out = destination / module
    shutil.copytree(root / "LICENSES", out, dirs_exist_ok=True)
    for attribution in (root / "src").rglob("qt_attribution.json"):
        relative = attribution.relative_to(root)
        target = out / "third-party" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(attribution, target)
        data = json.loads(attribution.read_text(encoding="utf-8-sig"), strict=False)
        for item in data if isinstance(data, list) else [data]:
            names = item.get("LicenseFile", [])
            if isinstance(names, str):
                names = [names]
            for name in names:
                options = [attribution.parent / name, attribution.parent / item.get("Path", "") / name]
                source = next((p.resolve() for p in options if p.is_file()), None)
                if source is None or not source.is_relative_to(root.resolve()):
                    raise SystemExit(f"Missing license file: {attribution}: {name}")
                target = out / "third-party" / source.relative_to(root.resolve())
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
if args.mingw_licenses:
    shutil.copytree(args.mingw_licenses, destination / "mingw", dirs_exist_ok=True)
if args.ffmpeg_source:
    out = destination / "ffmpeg"
    out.mkdir(parents=True, exist_ok=True)
    for name in ("COPYING.LGPLv2.1", "COPYING.LGPLv3", "LICENSE.md", "README.md"):
        source = args.ffmpeg_source / name
        if not source.is_file():
            raise SystemExit(f"Missing FFmpeg license material: {source}")
        shutil.copy2(source, out / name)
    for source in args.ffmpeg_source.glob("COPYING.*"):
        shutil.copy2(source, out / source.name)
print(f"License material: {destination}")
