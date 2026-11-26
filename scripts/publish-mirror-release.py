#!/usr/bin/env python3
"""发布并回查 EditHere 在 AtomGit 和 Gitee 上的发行版。"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

ATOMGIT_API = "https://api.atomgit.com/api/v5"
ATOMGIT_REPO = "Inginnng/EditHere"
GITEE_API = "https://gitee.com/api/v5"
GITEE_REPO = "InnGing/EditHere"
GITHUB_API = "https://api.github.com"
GITHUB_REPO = "Inginnng/EditHere"
ATOM_DOWNLOAD = f"https://gitcode.com/{ATOMGIT_REPO}/releases/download"
FALLBACK_HEADING = "## 补充下载 / Additional downloads"

#: 单个附件超过这个体积时 Gitee 会拒绝（其上限为 100 MB）。
GITEE_MAX_BYTES = 100 * 1024 * 1024

USER_AGENT = "EditHere-release-mirror"


class HttpError(Exception):
    def __init__(self, status: int, body: bytes, url: str) -> None:
        super().__init__(f"HTTP {status} from {urllib.parse.urlsplit(url).hostname}")
        self.status = status
        self.body = body
        self.url = url


def request(
    method: str,
    url: str,
    *,
    data: bytes | None = None,
    headers: dict[str, str] | None = None,
    retries: int = 3,
    timeout: int = 120,
    allow_404: bool = False,
) -> tuple[int, bytes]:
    """发一个请求，对 5xx 和连接错误做有限重试；404 由调用方决定如何处理。"""
    merged = {"User-Agent": USER_AGENT}
    if headers:
        merged.update(headers)

    last_error: Exception | None = None
    for attempt in range(1, retries + 1):
        req = urllib.request.Request(url, data=data, method=method, headers=merged)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as error:
            body = error.read()
            if error.code == 404 and allow_404:
                return 404, body
            if error.code < 500 or attempt == retries:
                raise HttpError(error.code, body, url) from error
            last_error = error
        except (urllib.error.URLError, TimeoutError) as error:
            last_error = error
        if attempt < retries:
            time.sleep(2 * attempt)
    raise HttpError(0, str(last_error).encode(), url)


def request_json(method: str, url: str, **kwargs) -> tuple[int, dict | None]:
    status, body = request(method, url, **kwargs)
    if not body:
        return status, {}
    return status, json.loads(body.decode("utf-8"))


def multipart(field: str, filename: str, content: bytes) -> tuple[bytes, str]:
    """构造一个单字段的 multipart/form-data 请求体。"""
    boundary = uuid.uuid4().hex
    content_type = mimetypes.guess_type(filename)[0] or "application/octet-stream"
    head = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="{field}"; filename="{filename}"\r\n'
        f"Content-Type: {content_type}\r\n\r\n"
    ).encode("utf-8")
    tail = f"\r\n--{boundary}--\r\n".encode("utf-8")
    return head + content + tail, f"multipart/form-data; boundary={boundary}"


def load_meta(path: Path) -> dict:
    meta = json.loads(path.read_text(encoding="utf-8"))
    for key in ("tagName", "name", "body"):
        if key not in meta:
            raise SystemExit(f"{path} 缺少字段 {key}")
    meta["isPrerelease"] = bool(meta.get("isPrerelease", False))
    return meta


def collect_assets(directory: Path) -> list[Path]:
    files = sorted(p for p in directory.iterdir() if p.is_file())
    if not files:
        raise SystemExit(f"{directory} 里没有可上传的文件")
    return files


def tag_commit(remote: str, tag: str) -> str | None:
    ref = f"refs/tags/{tag}"
    try:
        result = subprocess.run(
            ["git", "ls-remote", remote, ref, f"{ref}^{{}}"],
            check=True, capture_output=True, text=True, timeout=30,
            env={**os.environ, "GIT_TERMINAL_PROMPT": "0"},
        )
    except (subprocess.SubprocessError, OSError) as error:
        raise ValueError(f"无法读取 {remote} 上的标签 {tag}") from error
    refs = {name: sha for sha, name in (line.split() for line in result.stdout.splitlines())}
    return refs.get(f"{ref}^{{}}", refs.get(ref))


def wait_for_mirror_tag(remote: str, tag: str, commit: str) -> None:
    for delay in (0, 15, 30, 60, 60, 60, 75):
        if delay:
            time.sleep(delay)
        actual = tag_commit(remote, tag)
        if actual == commit:
            return
        if actual:
            raise ValueError(f"{remote} 的 {tag} 指向 {actual}，GitHub 指向 {commit}")
        if delay == 0:
            print(f"等待 {remote} 从 GitHub 同步标签 {tag}", flush=True)
    raise ValueError(f"{remote} 等待镜像标签 {tag} 超时，请更新仓库镜像后重新运行 mirror_only")


def asset_map(release: dict) -> dict[str, dict]:
    return {item["name"]: item for item in release.get("assets") or [] if item.get("name")}


def check_release(release: dict | None, meta: dict, *, exact_notes: bool) -> None:
    if not release or release.get("tag_name") != meta["tagName"] or release.get("name") != meta["name"]:
        raise ValueError("公开发行版的标签或标题与 GitHub 不一致")
    notes = (release.get("body") or "").replace("\r\n", "\n").rstrip()
    source = (meta["body"] or "").replace("\r\n", "\n").rstrip()
    if notes != source and (exact_notes or not notes.startswith(source + "\n\n")):
        raise ValueError("公开发行版的说明与 GitHub 不一致")


def publish_atomgit(meta: dict, files: list[Path], token: str, commit: str, mark_latest: bool) -> None:
    base = f"{ATOMGIT_API}/repos/{ATOMGIT_REPO}/releases"
    tag = meta["tagName"]
    detail = f"{base}/{tag}"
    query = urllib.parse.urlencode({"access_token": token})
    headers = {"Content-Type": "application/json"}
    status, existing = request_json("GET", detail, allow_404=True)
    payload = {
        "tag_name": tag, "target_commitish": commit, "name": meta["name"], "body": meta["body"],
    }
    if meta["isPrerelease"]:
        payload["release_status"] = "pre"
    if status == 404:
        if not token:
            raise ValueError("缺少 ATOMGIT_TOKEN，使用网页发布后可重新验证")
        try:
            _, existing = request_json("POST", f"{base}?{query}", data=json.dumps(payload).encode(),
                                       headers=headers, retries=1)
        except HttpError as error:
            if b"image repository" in error.body:
                raise ValueError("AtomGit 拒绝通过 API 写入拉取镜像。用已同步的标签在网页新建发行版、上传全部附件，再运行 mirror_only") from error
            raise
        if not existing:
            _, existing = request_json("GET", f"{detail}?{query}")

    for path in files:
        previous = asset_map(existing or {}).get(path.name)
        if previous:
            if previous.get("size") != path.stat().st_size:
                raise ValueError(f"AtomGit 同名附件大小不一致：{path.name}")
            continue
        if not token:
            raise ValueError(f"AtomGit 缺少附件 {path.name}，请在网页补传或配置 ATOMGIT_TOKEN")
        upload_query = urllib.parse.urlencode({"access_token": token, "file_name": path.name})
        _, ticket = request_json("GET", f"{detail}/upload_url?{upload_query}", retries=1)
        if not ticket or not ticket.get("url"):
            raise ValueError(f"AtomGit 没有返回 {path.name} 的上传地址")
        request("PUT", ticket["url"], data=path.read_bytes(), headers=ticket.get("headers") or {}, timeout=600)
        print(f"AtomGit：已上传 {path.name}")

    _, published = request_json("GET", detail)
    check_release(published, meta, exact_notes=True)
    if published.get("target_commitish") != commit or published.get("prerelease") != meta["isPrerelease"]:
        raise ValueError("AtomGit 公开发行版的提交或发布状态与 GitHub 不一致")
    assets = asset_map(published)
    missing = [p.name for p in files if not assets.get(p.name, {}).get("browser_download_url")
               or assets[p.name].get("size") != p.stat().st_size]
    if missing:
        raise ValueError(f"AtomGit 公共发行版附件缺失或大小不符：{', '.join(missing)}")
    if mark_latest and published.get("release_status") != "latest":
        if not token:
            raise ValueError("请在 AtomGit 网页将发行版设为最新版本")
        payload["release_status"] = "latest"
        request("PATCH", f"{detail}?{query}", data=json.dumps(payload).encode(), headers=headers, retries=1)
        _, published = request_json("GET", detail)
        if (published or {}).get("release_status") != "latest":
            raise ValueError("AtomGit 尚未将发行版公开标记为最新")
    print(f"AtomGit：已回查 {len(files)} 个附件  https://gitcode.com/{ATOMGIT_REPO}/releases/{tag}")


def publish_gitee(meta: dict, files: list[Path], token: str, commit: str, atomgit_verified: bool) -> None:
    base = f"{GITEE_API}/repos/{GITEE_REPO}/releases"
    tag = meta["tagName"]
    detail = f"{base}/tags/{tag}"
    headers = {"Authorization": f"Bearer {token}", "Content-Type": "application/json"}
    payload = {"tag_name": tag, "target_commitish": commit, "name": meta["name"],
               "body": meta["body"], "prerelease": meta["isPrerelease"]}
    _, existing = request_json("GET", detail, allow_404=True)
    if not existing:
        if not token:
            raise ValueError("缺少 GITEE_TOKEN，使用网页发布后可重新验证")
        _, existing = request_json("POST", base, data=json.dumps(payload).encode(), headers=headers, retries=1)
        if not existing or not existing.get("id"):
            _, existing = request_json("GET", detail)
    if not existing or not existing.get("id"):
        raise ValueError("Gitee 未返回发行版 ID")
    release_id = existing["id"]
    if existing.get("target_commitish") != commit:
        raise ValueError("Gitee 发行版关联的提交与 GitHub 不一致")

    hosted = asset_map(existing)
    notes = existing.get("body") or ""
    quota_exceeded = False
    quota_blocked = set()
    links = {p.name: f"{ATOM_DOWNLOAD}/{tag}/{urllib.parse.quote(p.name)}" for p in files}
    for path in sorted(files, key=lambda p: (p.stat().st_size, p.name)):
        if path.name in hosted or (atomgit_verified and f"]({links[path.name]})" in notes):
            continue
        if quota_exceeded or path.stat().st_size > GITEE_MAX_BYTES:
            quota_blocked.add(path.name)
            continue
        if not token:
            raise ValueError(f"Gitee 缺少附件 {path.name}，请配置 GITEE_TOKEN 或在网页补传")
        body, content_type = multipart("file", path.name, path.read_bytes())
        try:
            request("POST", f"{base}/{release_id}/attach_files", data=body,
                    headers={**headers, "Content-Type": content_type}, timeout=600, retries=1)
        except HttpError as error:
            if error.status == 400 and "配额" in error.body.decode("utf-8", errors="replace"):
                quota_exceeded = True
                quota_blocked.add(path.name)
                continue
            raise
        print(f"Gitee：已上传 {path.name}")

    _, published = request_json("GET", detail)
    check_release(published, meta, exact_notes=False)
    if published.get("target_commitish") != commit or published.get("prerelease") != meta["isPrerelease"]:
        raise ValueError("Gitee 公开发行版的提交或发布状态与 GitHub 不一致")
    hosted = asset_map(published)
    missing = [p.name for p in files if not hosted.get(p.name, {}).get("browser_download_url")]
    if missing:
        notes = published.get("body") or ""
        if not atomgit_verified:
            raise ValueError(f"Gitee 缺少 {', '.join(missing)}，需先完成 AtomGit 验收才能使用直链")
        new_links = [name for name in missing if f"]({links[name]})" not in notes]
        if set(new_links) - quota_blocked:
            raise ValueError("Gitee 未回查到本次上传的附件，请重跑验收")
        if new_links:
            if not token:
                raise ValueError("请配置 GITEE_TOKEN 或在网页补上缺少附件的 AtomGit 直链")
            items = "\n".join(f"- [{name}]({links[name]})" for name in missing)
            payload["body"] = f"{(meta['body'] or '').rstrip()}\n\n{FALLBACK_HEADING}\n\n{items}\n"
            request("PATCH", f"{base}/{release_id}", data=json.dumps(payload).encode(), headers=headers, retries=1)
            _, published = request_json("GET", detail)
            check_release(published, meta, exact_notes=False)
        if any(f"]({links[name]})" not in (published.get("body") or "") for name in missing):
            raise ValueError("Gitee 公开发行版缺少经核对的附件直链")
        print(f"Gitee：附件配额替代下载（AtomGit）：{', '.join(missing)}")
    print(f"Gitee：已回查 {len(files) - len(missing)} 个原生附件、{len(missing)} 个 AtomGit 直链"
          f"  https://gitee.com/{GITEE_REPO}/releases/tag/{tag}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--meta", type=Path, required=True)
    parser.add_argument("--targets", default="atomgit,gitee", choices=("atomgit,gitee", "atomgit", "gitee"))
    parser.add_argument("--no-latest", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    meta = load_meta(args.meta)
    files = collect_assets(args.assets)
    tag = meta["tagName"]
    if not re.fullmatch(r"v(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)", tag):
        parser.error(f"无效的稳定版本标签：{tag}")
    print(f"发行版 {tag}（{meta['name']}），共 {len(files)} 个文件")
    if args.dry_run:
        print(f"[dry-run] {args.targets}：{', '.join(p.name for p in files)}")
        return 0

    try:
        _, source = request_json("GET", f"{GITHUB_API}/repos/{GITHUB_REPO}/commits/{tag}", timeout=20)
    except (HttpError, ValueError) as error:
        raise SystemExit(f"无法核对 GitHub 标签 {tag}：{error}") from None
    commit = (source or {}).get("sha", "")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise SystemExit(f"GitHub 未返回标签 {tag} 对应的提交")
    mark_latest = not args.no_latest and meta.get("isLatest", False) and not meta["isPrerelease"]
    failures = []
    atomgit_verified = False
    targets = args.targets.split(",")
    if "atomgit" in targets:
        try:
            wait_for_mirror_tag(f"https://atomgit.com/{ATOMGIT_REPO}.git", tag, commit)
            publish_atomgit(meta, files, os.environ.get("ATOMGIT_TOKEN", "").strip(), commit, mark_latest)
            atomgit_verified = True
        except (HttpError, ValueError) as error:
            print(f"AtomGit 发布未完成：{error}", file=sys.stderr)
            failures.append("AtomGit")
    if "gitee" in targets:
        try:
            token = os.environ.get("GITEE_TOKEN", "").strip()
            wait_for_mirror_tag(f"https://gitee.com/{GITEE_REPO}.git", tag, commit)
            publish_gitee(meta, files, token, commit, atomgit_verified)
        except (HttpError, ValueError) as error:
            print(f"Gitee 发布未完成：{error}", file=sys.stderr)
            failures.append("Gitee")
    if failures:
        print(f"未通过发行版验收：{', '.join(failures)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
