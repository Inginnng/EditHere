"""Fetch official linuxdeploy tools, pinned by SHA-256."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / ".tools" / "linuxdeploy"
# The runtime uses an official versioned release, rather than the continuous
# asset that upstream replaces in place. Keep the reviewed SHA-256 as well:
# changing a tag or its asset must still fail closed.
# linuxdeploy's rolling builds also require an explicit digest update whenever
# their assets change.
ASSETS = {"x86_64": [
    ("AppImage/type2-runtime", "20251108", "runtime-x86_64",
     "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"),
    # Reviewed 2026-11-03: official continuous build from 2026-11-01,
    # source 07333c6e942c7d71782b66be924c5d867f9dfdfc, Actions run 41349296615.
    # Keep checking both GitHub's digest and the downloaded bytes.
    ("linuxdeploy/linuxdeploy", "continuous", "linuxdeploy-x86_64.AppImage",
     "df908b19f1ed1cdafa79508d415aa12d6c56ce4d56d4149aa8c09286718706e5"),
    ("linuxdeploy/linuxdeploy-plugin-qt", "continuous", "linuxdeploy-plugin-qt-x86_64.AppImage",
     "cfc1055b2b9dbc08412b579f20990b7b41a17b61beaa5847dc9477c96c9e9617"),
], "aarch64": [
    ("AppImage/type2-runtime", "20251108", "runtime-aarch64",
     "00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444"),
    # Reviewed 2026-11-03 against GitHub's release digest and downloaded bytes;
    # continuous source 07333c6e942c7d71782b66be924c5d867f9dfdfc.
    ("linuxdeploy/linuxdeploy", "continuous", "linuxdeploy-aarch64.AppImage",
     "f38e4d7cebf20d8073ec4e00e0a167ac146cd8585fe2f8fe82df27d74f2bd8e1"),
    ("linuxdeploy/linuxdeploy-plugin-qt", "continuous", "linuxdeploy-plugin-qt-aarch64.AppImage",
     "d5b5a9020065cefda8c7af7c9500f686b23640eac50c88201a81752c6ee2e49c"),
]}


def architecture(value):
    aliases = {"amd64": "x86_64", "arm64": "aarch64"}
    value = aliases.get(value.lower(), value.lower())
    if value not in ASSETS:
        raise argparse.ArgumentTypeError("Unsupported Linux architecture: " + value)
    return value

def fetch(url, token=False):
    headers = {"User-Agent": "EditHere-build"}
    if token and os.environ.get("GH_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GH_TOKEN"]
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=120) as response:
        return response.read()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=architecture, default=platform.machine())
    parser.add_argument("--runtime-sha256", action="store_true",
                        help="Print the pinned runtime hash without downloading")
    args = parser.parse_args()
    if args.runtime_sha256:
        print(ASSETS[args.arch][0][3])
        return
    TOOLS.mkdir(parents=True, exist_ok=True)
    for repo, tag, name, expected in ASSETS[args.arch]:
        target = TOOLS / name
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == expected:
            target.chmod(0o755)
            continue
        release = json.loads(fetch(f"https://api.github.com/repos/{repo}/releases/tags/{tag}", True))
        asset = next(item for item in release["assets"] if item["name"] == name)
        if asset.get("digest") != "sha256:" + expected:
            raise RuntimeError(f"{name} changed upstream; review and update the pinned digest")
        data = fetch(asset["browser_download_url"])
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError(f"Checksum mismatch: {name}")
        temporary = target.with_suffix(".partial")
        temporary.write_bytes(data)
        temporary.replace(target)
        target.chmod(0o755)
        print(f"Verified {name}")

if __name__ == "__main__":
    main()
