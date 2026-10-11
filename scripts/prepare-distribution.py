#!/usr/bin/env python3
"""Assemble local distribution files without moving sources or publishing assets."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION = re.compile(r"\d+\.\d+\.\d+")
SKIP = {"AppDir", "licenses", "node_modules", ".git", "tests", "validation", "plugins", "qml"}
LEGAL = ("LICENSE", "NOTICE", "LICENSING.md", "COMMERCIAL-LICENSE.md", "TRADEMARK-POLICY.md", "packaging/THIRD-PARTY-NOTICES.md")
WEB_RUNTIME = ("EditHere.html", "agent.cjs", "agent-session.cjs", "preview.cjs", "preview-server.cjs", "install-skill.cjs", "src/feedback.js")
WEB_GUIDES = ("AI-SETUP.md", "AI-SETUP.en.md")
PACKAGE_FILES = {
    "web-skill": tuple("web/skills/edithere-web/" + name for name in (
        "SKILL.md", "agents/openai.yaml", "references/capture.md", "references/follow-up.md")),
    "desktop-mcp": tuple("connector/" + name for name in (
        "package.json", "mcp-server.mjs", "README.md", "LICENSE", "NOTICE", "icon.svg",
        "connector-meta.json", "mcp.json", "skills/edithere/SKILL.md")),
    "trae-plugin": tuple("trae-plugin/" + name for name in (
        "package.json", "connector.json", ".trae-plugin/plugin.json", "assets/edithere-icon.svg", "skills/edithere/SKILL.md")),
    "desktop-skill": ("skills/edithere/SKILL.md",),
}
GENERIC_WEB_INSTALLATION = """# 本机网页入口

本包的网页 CLI 相对本文件为 `../../../agent.cjs`，需要 Node.js 18+。
解压到长期保留的可写目录后，运行 `node web/install-skill.cjs` 安装技能。
安装器生成本机入口绝对路径；包中不保存开发者的个人路径。
更换解压目录后需重新配置。普通离线 HTML 不需要本地会话服务。
"""


def sha256(file: Path) -> str:
    value = hashlib.sha256()
    with file.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def text(file: Path) -> str:
    return file.read_text(encoding="utf-8-sig").strip()


def version(root: Path) -> str:
    match = re.search(r"project\(EditHere VERSION (\d+\.\d+\.\d+)\b", text(root / "CMakeLists.txt"))
    if not match:
        raise ValueError("Cannot read the product source version from CMakeLists.txt")
    return match[1]


def revision(root: Path) -> tuple[str, bool]:
    try:
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True, stderr=subprocess.DEVNULL).strip()
        dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=normal"], cwd=root, text=True).strip())
        return commit, dirty
    except (OSError, subprocess.CalledProcessError):
        return "source-tree", True


def package_info(file: Path) -> tuple[str, str, str] | None:
    """Filename/adjacent metadata identifies a package, never its release approval."""
    name = file.name
    if any(marker in name for marker in ("-web", "-mcp", "-skill", "-trae")):
        return None
    match = re.fullmatch(r"EditHere-(\d+\.\d+\.\d+)(?:-[\w.-]+)?-win-x64(-setup\.exe|\.zip)", name)
    if match:
        return "windows", match[1], "installer" if match[2].endswith("exe") else "portable"
    # Earlier previews did not all include the win-x64 suffix.
    match = re.fullmatch(r"EditHere-(\d+\.\d+\.\d+)-[\w.-]+\.zip", name)
    if match and "macos" not in name:
        return "windows", match[1], "preview"
    match = re.fullmatch(r"EditHere-(\d+\.\d+\.\d+)-macos-universal\.dmg", name)
    if match:
        return "macos", match[1], "dmg"
    if name == "EditHere-linux-x86_64.AppImage":
        for candidate in (file.parent / "version.txt", file.parent / "AppDir/usr/share/doc/edithere/version.txt"):
            if candidate.is_file() and VERSION.fullmatch(text(candidate)):
                return "linux", text(candidate), "appimage"
        match = re.search(r"(?:^|/)linux-(\d+\.\d+\.\d+)(?:/|-|$)", file.parent.as_posix())
        if match:
            return "linux", match[1], "appimage"
        return "linux", "unknown", "appimage"
    if re.fullmatch(r"EditHere-[\w.-]+-win-x64\.zip", name):
        return "windows", "unknown", "preview"
    return None


def candidates(directory: Path) -> list[Path]:
    """Bounded scan skips unpacked dependency trees and private runtime data."""
    found: list[Path] = []
    if not directory.is_dir():
        return found
    for current, dirs, files in os.walk(directory, followlinks=False):
        here = Path(current)
        depth = len(here.relative_to(directory).parts)
        dirs[:] = sorted(name for name in dirs if name not in SKIP and not name.startswith("distribution-") and not (here / name).is_symlink()) if depth < 5 else []
        if "EditHere.exe" in files or "Uninstall.exe" in files:
            # An installed/test payload may contain nested old installers; retain
            # its root packages in the historical index, never ship the payload.
            dirs[:] = []
        for name in sorted(files):
            file = here / name
            if not file.is_symlink() and package_info(file):
                found.append(file)
    return found


def internal_zip_version(file: Path) -> str | None:
    with zipfile.ZipFile(file) as archive:
        names = [name for name in archive.namelist() if name.rstrip("/").split("/")[-1] == "version.txt" and len(name.split("/")) <= 2]
        if len(names) != 1:
            return None
        value = archive.read(names[0]).decode("utf-8-sig").strip()
        return value if VERSION.fullmatch(value) else None


def current_slot(file: Path, info: tuple[str, str, str], source_version: str, root: Path, explicit_dirs: list[Path]) -> str | None:
    platform, built, kind = info
    if built != source_version:
        return None
    standard = {
        "portable": f"EditHere-{source_version}-win-x64.zip",
        "installer": f"EditHere-{source_version}-win-x64-setup.exe",
        "dmg": f"EditHere-{source_version}-macos-universal.dmg",
        "appimage": "EditHere-linux-x86_64.AppImage",
    }
    if kind not in standard or file.name != standard[kind]:
        return None
    allowed = [root / "dist", root / "dist" / f"linux-{source_version}", *explicit_dirs,
               *(directory / f"linux-{source_version}" for directory in explicit_dirs)]
    if file.parent not in allowed:
        return None
    if file.stat().st_size == 0:
        raise ValueError(f"Empty current package: {file}")
    if kind == "portable" and internal_zip_version(file) != source_version:
        raise ValueError(f"ZIP build version differs from filename: {file}")
    return platform + "/" + kind


def archive_entries(base: Path, product: str) -> dict[str, Path]:
    """Explicit public files only; local keys, paths and captures never enter ZIPs."""
    entries = {}
    for name in PACKAGE_FILES[product]:
        file = base / name
        if not file.is_file() or file.is_symlink() or not file.resolve().is_relative_to(base.resolve()):
            raise ValueError(f"Missing or unsafe package source: {file}")
        entries[name] = file
    return entries


def write_zip(file: Path, entries: dict[str, Path | str]) -> None:
    file.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(file, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, value in sorted(entries.items()):
            if name.startswith("/") or ".." in Path(name).parts:
                raise ValueError(f"Unsafe package entry: {name}")
            data = value.read_bytes() if isinstance(value, Path) else value.encode("utf-8")
            item = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            item.compress_type = zipfile.ZIP_DEFLATED
            item.external_attr = (0o100644 << 16)
            archive.writestr(item, data)


def assemble(root: Path, output: Path, *, include_history: bool = False, native_dirs: list[Path] | None = None,
             require_all: bool = False, node: str = "node") -> dict:
    root, output = root.resolve(), output.resolve()
    native_dirs = [folder.resolve() for folder in native_dirs or []]
    # Never copy an output directory into itself, overwrite a previous catalog,
    # or mistake an active source/worktree location for a distribution target.
    if output.exists():
        raise ValueError(f"Output already exists; choose a fresh directory: {output}")
    if output == root or root.is_relative_to(output):
        raise ValueError("Output cannot contain the source repository")
    if output.is_relative_to(root) and output.parts[len(root.parts)] != "dist":
        raise ValueError("Inside the repository, output must be under dist/")
    for folder in native_dirs:
        if not folder.is_dir():
            raise ValueError(f"Native input directory does not exist: {folder}")
        if folder == output or folder.is_relative_to(output) or output.is_relative_to(folder):
            raise ValueError("Native input and output directories must not overlap")
    source_version = version(root)
    roots = [root / "dist", *native_dirs]
    artifacts = root / "artifacts"
    if artifacts.is_dir():
        roots += [folder / "source/dist" for folder in sorted(artifacts.glob("release-*")) if folder.is_dir()]
    files = sorted({file.resolve() for directory in roots for file in candidates(directory)})
    records, selected = [], {}
    for file in files:
        info = package_info(file)
        slot = current_slot(file, info, source_version, root, native_dirs)
        digest = sha256(file)
        if slot and slot in selected and selected[slot][1] != digest:
            raise ValueError(f"Different current packages occupy {slot}; choose one input directory explicitly")
        if slot:
            selected[slot] = (file, digest)
        try:
            origin = file.relative_to(root).as_posix()
        except ValueError:
            origin = "native-input-" + str(next(index for index, folder in enumerate(native_dirs, 1) if file.is_relative_to(folder))) + "/" + file.name
        check = "absent"
        sidecar = file.with_name(file.name + ".sha256")
        if sidecar.is_file():
            expected = text(sidecar).split()[0].lower()
            check = "matched" if expected == digest else "mismatch"
            if slot and check == "mismatch":
                raise ValueError(f"Current package checksum mismatch: {file}")
        records.append({"source": origin, "platform": info[0], "version": info[1], "kind": info[2], "bytes": file.stat().st_size,
                        "sha256": digest, "sidecar": check, "currentSlot": slot, "file": file})
    required = {"windows/portable", "windows/installer", "macos/dmg", "linux/appimage"}
    missing = sorted(required - selected.keys())
    if require_all and missing:
        raise ValueError("Current native packages missing: " + ", ".join(missing))
    # Build before writing output, so a build failure leaves no misleading catalog.
    subprocess.run([node, str(root / "web/build.cjs")], cwd=root, check=True, stdout=subprocess.PIPE, text=True)
    for name in (*WEB_RUNTIME, *WEB_GUIDES):
        if not (root / "web" / name).is_file():
            raise ValueError(f"Web runtime file missing: {name}")
    commit, dirty = revision(root)
    short = commit[:8]
    output.mkdir(parents=True)
    created = []

    def record(file: Path, product: str, package_version: str, state: str, **extra) -> None:
        created.append({"path": file.relative_to(output).as_posix(), "product": product, "version": package_version,
                        "state": state, "bytes": file.stat().st_size, "sha256": sha256(file), **extra})

    common: dict[str, Path | str] = {name: root / name for name in LEGAL}
    stamp = {"productSourceVersion": source_version, "sourceCommit": commit, "sourceDirty": dirty,
             "webRevision": short, "channel": "local-preview", "published": False}
    web_version = source_version + "-" + short
    common["build-info.json"] = json.dumps(stamp, ensure_ascii=False, indent=2) + "\n"
    offline = output / "web" / web_version / f"EditHere-{source_version}-web-{short}.zip"
    write_zip(offline, {**common, "EditHere.html": root / "web/EditHere.html",
                       "README.md": "# EditHere Web\n\n双击 EditHere.html，或部署到静态网站。无需 Node.js。图片、视频和反馈在浏览器内处理。\n\n此包不含 Agent 自动返回连接器；需要右侧批注和反馈返回时使用同目录的 web-agent 包。\n\n这是按当前源码生成的本地预览包，版本与来源见 build-info.json。许可见 LICENSE / LICENSING.md。\n"})
    record(offline, "web", web_version, "assembled-local-preview")
    agent_entries = {**common, **{"web/" + name: root / "web" / name for name in WEB_RUNTIME},
                     **{name: root / "web" / name for name in WEB_GUIDES},
                     **archive_entries(root, "web-skill")}
    agent_entries["web/skills/edithere-web/references/local-installation.md"] = GENERIC_WEB_INSTALLATION
    agent_entries["README.md"] = """# EditHere Web Agent

Windows、macOS、Linux 使用同一份包。需要 Node.js 18 或更新版本。
解压到当前用户可写且长期保留的目录，不能直接在 ZIP 中运行。
首次安装：把本包与 AI-SETUP.md 交给 Agent；无需预先安装 Skill。该指南同时说明下载校验、依赖检查、已有安装处理和健康检查。

安装网页版技能：
```text
node web/install-skill.cjs
```
默认安装到 CODEX_HOME/skills/edithere-web 或 ~/.codex/skills/edithere-web。
其他宿主使用 `--destination <技能目录的绝对路径>`。已存在的技能不会被覆盖。
安装时生成本机路径记录；更换解压目录后需重新配置。不会改写桌面技能。

预览：`node web/preview.cjs`，再打开 http://127.0.0.1:7330/EditHere.html。
发起批注：`node web/agent.cjs start --image <图片绝对路径> --output <尚不存在的JSON绝对路径> --timeout 86400`。
Agent 使用返回的 URL 传图，通过同一 sessionId 的 poll/wait 读取用户明确提交的反馈。
截图和后续唤醒依赖宿主提供的能力；不能从连接器自身推断已支持自动截图或后台唤醒。

服务和私有会话数据在运行后写入 web/tests/output；含图片与访问令牌，不要再次整目录压缩分发。
若 7330 已有另一份 EditHere 服务，应按其 PID 和路径核实后处理，不能直接杀掉其他程序。
本包为本地预览整理，不表示已发布或通过三平台实机验收。来源见 build-info.json。
"""
    agent_package = output / "web" / web_version / f"EditHere-{source_version}-web-agent-{short}.zip"
    write_zip(agent_package, agent_entries)
    record(agent_package, "web-agent", web_version, "assembled-local-preview")
    integration_specs = (("desktop-mcp", "connector", "connector/package.json"),
                         ("trae-plugin", "trae-plugin", "trae-plugin/package.json"),
                         ("desktop-skill", "skills/edithere", None))
    for product, folder, metadata in integration_specs:
        package_version = json.loads(text(root / metadata))["version"] if metadata else source_version
        entries = archive_entries(root, product)
        entries.update(common)
        entries["docs/VIDEO-ANNOTATION.md"] = root / "docs/VIDEO-ANNOTATION.md"
        guide = {"desktop-skill": "skills/edithere/SKILL.md", "desktop-mcp": "connector/README.md", "trae-plugin": "trae-plugin/.trae-plugin/plugin.json"}[product]
        entries["README-distribution.md"] = f"# {product}\n\n组件版本 {package_version}，来自源码 {commit}。\n此包使用桌面 edithere-cli，需另行安装相应平台的 EditHere。它与 web-agent 是两个入口。\n\n入口说明或配置见 {guide}。本地整理包，未执行发布。\n"
        file = output / "integrations" / product / str(package_version) / f"EditHere-{product}-{package_version}-{short}.zip"
        write_zip(file, entries)
        record(file, product, str(package_version), "assembled-unverified")
    for platform in ("windows", "macos", "linux"):
        directory = output / platform / source_version
        directory.mkdir(parents=True)
        platform_files = [(slot, file, digest) for slot, (file, digest) in selected.items() if slot.startswith(platform + "/")]
        for slot, file, digest in platform_files:
            destination = directory / file.name
            shutil.copy2(file, destination)
            if sha256(destination) != digest:
                raise ValueError(f"Copy verification failed: {destination}")
            record(destination, platform, source_version, "collected-unverified", source=next(item["source"] for item in records if item["file"] == file))
            if platform == "linux" and (file.parent / "edithere-cli").is_file():
                shutil.copy2(file.parent / "edithere-cli", directory / "edithere-cli")
                record(directory / "edithere-cli", platform + "-cli", source_version, "collected-unverified")
        required_here = sorted(slot for slot in missing if slot.startswith(platform + "/"))
        (directory / "README.md").write_text(f"# {platform} · 源码目标 {source_version}\n\n" +
            ("尚缺当前版本包：" + ", ".join(required_here) + "。需要对应平台构建或取回 CI 产物。\n" if required_here else "已汇集同版本命名产物；仍需原生打包检查、签名/公证要求及平台运行验收。\n") +
            "旧版本保留在 ../../archive/，不能改名充当当前版本。现有构建入口见主仓 docs/DISTRIBUTION.md。\n", encoding="utf-8")
    for item in records:
        file = item.pop("file")
        item["classification"] = "current-candidate" if item["currentSlot"] else "historical-unverified"
        if include_history and not item["currentSlot"]:
            slug = re.sub(r"[^A-Za-z0-9._-]+", "_", file.parent.name).strip("._") or "legacy"
            directory = output / "archive" / item["platform"] / item["version"] / (slug + "-" + item["sha256"][:10])
            directory.mkdir(parents=True, exist_ok=True)
            destination = directory / file.name
            already_archived = destination.exists()
            if not already_archived:
                shutil.copy2(file, destination)
            if sha256(destination) != item["sha256"]:
                raise ValueError(f"Archive copy verification failed: {destination}")
            if not already_archived:
                record(destination, item["platform"], item["version"], "historical-unverified", source=item["source"])
            item["archivedPath"] = destination.relative_to(output).as_posix()
    report = {"schemaVersion": 1, "generatedAt": datetime.now(timezone.utc).isoformat(), "productSourceVersion": source_version,
              "sourceCommit": commit, "sourceDirty": dirty, "published": False, "completeNativeSet": not missing,
              "missingCurrentNative": missing, "packages": created, "existingArtifacts": records}
    (output / "manifest.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    checksum_files = sorted(file for file in output.rglob("*") if file.is_file())
    (output / "SHA256SUMS.txt").write_text("".join(sha256(file) + "  " + file.relative_to(output).as_posix() + "\n" for file in checksum_files), encoding="utf-8")
    (output / "README.md").write_text(f"""# EditHere 分发目录

源码目标版本：**{source_version}**；源码提交：`{commit}`；工作树有改动：`{str(dirty).lower()}`。
这是本地整理，**未发布**。实际文件与 SHA-256 见 manifest.json / SHA256SUMS.txt。

| 目录 | 用途 |
| --- | --- |
| web/{web_version}/ | 独立网页包与网页 Agent 包，按 Git 修订区分预览 |
| windows/{source_version}/ | 当前桌面版本的 Windows x64 安装包与便携 ZIP |
| macos/{source_version}/ | 当前桌面版本的 macOS Universal DMG |
| linux/{source_version}/ | 当前桌面版本的 Linux x86_64 AppImage |
| integrations/ | 桌面 MCP、桌面 Skill 与 Trae 插件，各自保留版本 |
| archive/ | 按平台、原始版本和来源/哈希保留的历史包，仅供追溯 |

当前缺包：{', '.join(missing) if missing else '无；仍需发布前验收'}。
Web 文件名中的 {source_version} 表示对应源码基线，不表示它与桌面版功能完全相同。
历史包与测试包不会晋升为当前包，原始文件、开发目录和安全修复 worktree 均未移动或删除。
历史校验文件是否匹配见 existingArtifacts.sidecar；校验匹配只代表文件完整，不能证明运行或发布状态。
先选择对应平台、版本和入口，再分发其中具体 ZIP/安装包，不要把整个目录或 archive 一起发给用户。

后续汇总：在主仓运行 `python scripts/prepare-distribution.py --output <新的输出目录> --include-history`。
从 CI 取回构建阶段的原始包可通过重复 `--native-dir <目录>` 加入：Windows/macOS 文件名须带版本，Linux 须保留相邻版本记录或 linux-<版本> 目录。
最终 Release 的无版本稳定文件名不能直接用作当前版本输入。`--require-all` 会拒绝缺少当前三平台成品的汇总。
不覆盖已有分发目录，不从此命令上传 GitHub/Gitee，也不会修改稳定下载资产名。
""", encoding="utf-8")
    # The root guide is included in checksums after its content is finalized.
    with (output / "SHA256SUMS.txt").open("a", encoding="utf-8") as stream:
        stream.write(sha256(output / "README.md") + "  README.md\n")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--include-history", action="store_true")
    parser.add_argument("--native-dir", action="append", default=[], type=Path,
                        help="Original build artifacts: versioned Windows/macOS names; Linux with adjacent version metadata")
    parser.add_argument("--require-all", action="store_true")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    destination = args.output or ROOT / "dist" / f"distribution-{version(ROOT)}-{revision(ROOT)[0][:8]}"
    try:
        report = assemble(ROOT, destination, include_history=args.include_history, native_dirs=args.native_dir, require_all=args.require_all, node=args.node)
    except (ValueError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Distribution error: {error}\n")
    print(json.dumps({"output": str(destination.resolve()), "packages": len(report["packages"]), "missingCurrentNative": report["missingCurrentNative"], "published": False}, ensure_ascii=False))


if __name__ == "__main__":
    main()
