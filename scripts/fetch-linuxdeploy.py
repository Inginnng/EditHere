"""Fetch official linuxdeploy tools, pinned by SHA-256."""
import hashlib
import json
import os
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / ".tools" / "linuxdeploy"
# The runtime uses an official versioned release, rather than the continuous
# asset that upstream replaces in place. Keep the reviewed SHA-256 as well:
# changing a tag or its asset must still fail closed.
# linuxdeploy's rolling builds also require an explicit digest update whenever
# their assets change.
ASSETS = [
    ("AppImage/type2-runtime", "20251108", "runtime-x86_64",
     "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"),
    # Bumped 2026-10-04: linuxdeploy rebuilt its continuous asset on
    # 2026-08-01 and the previous pin no longer matched.
    ("linuxdeploy/linuxdeploy", "continuous", "linuxdeploy-x86_64.AppImage",
     "8aea8da0f7f7039d2a2cecb14657d752a222a5e1d3825caeef186c82f751cdd1"),
    ("linuxdeploy/linuxdeploy-plugin-qt", "continuous", "linuxdeploy-plugin-qt-x86_64.AppImage",
     "cfc1055b2b9dbc08412b579f20990b7b41a17b61beaa5847dc9477c96c9e9617"),
]

def fetch(url, token=False):
    headers = {"User-Agent": "EditHere-build"}
    if token and os.environ.get("GH_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GH_TOKEN"]
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=120) as response:
        return response.read()

def main():
    TOOLS.mkdir(parents=True, exist_ok=True)
    for repo, tag, name, expected in ASSETS:
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
