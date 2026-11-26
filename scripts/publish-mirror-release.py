#!/usr/bin/env python3
"""把 GitHub 的正式发行版同步到 Gitee 与 AtomGit。

GitHub 是唯一上游：``release.yml`` 在 GitHub 建好发行版之后，由本脚本把同一批
文件、同一份说明发到两个国内镜像。镜像的 Release 页面不是 git 对象，仓库镜像
（AtomGit 的镜像仓库）不会同步它，所以必须单独发布。

用法::

    python3 scripts/publish-mirror-release.py \\
        --assets mirror-assets --meta mirror-release.json

``--assets`` 是待上传的文件目录，``--meta`` 是 ``gh release view --json`` 的输出
（读到 tagName、name、body、isPrerelease）。令牌从环境变量读：
``ATOMGIT_TOKEN``、``GITEE_TOKEN``；缺失的那个站点会被跳过，不影响另一个。

两个平台的差异（踩过的坑）：

* AtomGit 的 release 对象没有数字 id，只能按 tag 定位；也没有 ``attach_files``
  接口，附件要先 ``GET .../upload_url`` 拿签名地址和请求头，再 ``PUT`` 文件。
* AtomGit 新建之后还要 ``PATCH`` 一次把 ``release_status`` 标成 ``latest``，
  否则发行版页面上不会显示为最新版本。
* Gitee 用数字 id 传附件，走 ``POST .../releases/{id}/attach_files`` 的
  multipart 表单。
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
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

#: 单个附件超过这个体积时 Gitee 会拒绝（其上限为 100 MB）。
GITEE_MAX_BYTES = 100 * 1024 * 1024

USER_AGENT = "EditHere-release-mirror"


class HttpError(Exception):
    def __init__(self, status: int, body: bytes, url: str) -> None:
        super().__init__(f"HTTP {status} for {url}: {body[:400]!r}")
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


def request_json(method: str, url: str, **kwargs) -> tuple[int, dict]:
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


def publish_atomgit(meta: dict, files: list[Path], token: str, mark_latest: bool) -> None:
    base = f"{ATOMGIT_API}/repos/{ATOMGIT_REPO}/releases"
    query = urllib.parse.urlencode({"access_token": token})
    tag = meta["tagName"]

    status, existing = request_json("GET", f"{base}/{tag}?{query}", allow_404=True)
    if status == 404:
        payload = json.dumps(
            {
                "tag_name": tag,
                "name": meta["name"],
                "body": meta["body"],
                "release_status": "pre" if meta["isPrerelease"] else "latest",
            }
        ).encode("utf-8")
        status, existing = request_json(
            "POST",
            f"{base}?{query}",
            data=payload,
            headers={"Content-Type": "application/json"},
        )
        print(f"AtomGit：已创建发行版 {tag}")
    else:
        print(f"AtomGit：发行版 {tag} 已存在，补齐缺失附件")

    if mark_latest and not meta["isPrerelease"]:
        # 新建之后必须再 PATCH 一次，发行版页面才会把它标成最新版本。
        payload = json.dumps(
            {
                "tag_name": tag,
                "name": meta["name"],
                "body": meta["body"],
                "release_status": "latest",
            }
        ).encode("utf-8")
        request(
            "PATCH",
            f"{base}/{tag}?{query}",
            data=payload,
            headers={"Content-Type": "application/json"},
        )
        print("AtomGit：已标记为最新版本")

    remote_names = {item.get("name") for item in existing.get("assets") or []}
    for path in files:
        if path.name in remote_names:
            print(f"AtomGit：跳过已存在的 {path.name}")
            continue
        # 没有 attach_files 接口：先要签名上传地址，再把文件 PUT 上去。
        upload_query = urllib.parse.urlencode({"access_token": token, "file_name": path.name})
        _, ticket = request_json("GET", f"{base}/{tag}/upload_url?{upload_query}")
        url = ticket.get("url")
        headers = ticket.get("headers") or {}
        if not url:
            raise SystemExit(f"AtomGit 没有返回 {path.name} 的上传地址")
        request("PUT", url, data=path.read_bytes(), headers=headers, timeout=600)
        print(f"AtomGit：已上传 {path.name} ({path.stat().st_size} 字节)")


def publish_gitee(meta: dict, files: list[Path], token: str) -> None:
    base = f"{GITEE_API}/repos/{GITEE_REPO}/releases"
    tag = meta["tagName"]

    status, existing = request_json(
        "GET", f"{base}/tags/{tag}?{urllib.parse.urlencode({'access_token': token})}", allow_404=True
    )
    # Gitee also returns HTTP 200 with a JSON null body for a missing release.
    if status == 404 or existing is None:
        payload = urllib.parse.urlencode(
            {
                "access_token": token,
                "tag_name": tag,
                # Gitee 现在把 target_commitish 当必填，缺了直接 400。
                "target_commitish": tag,
                "name": meta["name"],
                "body": meta["body"],
                "prerelease": "true" if meta["isPrerelease"] else "false",
            }
        ).encode("utf-8")
        status, existing = request_json(
            "POST",
            base,
            data=payload,
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )
        print(f"Gitee：已创建发行版 {tag}")

    # POST 有时返回空体，拿不到 id 就回查一次。
    if not existing or not existing.get("id"):
        status, existing = request_json(
            "GET", f"{base}/tags/{tag}?{urllib.parse.urlencode({'access_token': token})}"
        )
        if existing:
            print(f"Gitee：发行版 {tag} 已存在，补齐缺失附件")

    release_id = (existing or {}).get("id")
    if not release_id:
        raise SystemExit("Gitee 没有返回发行版 id，无法上传附件")

    remote_names = {item.get("name") for item in existing.get("assets") or []}
    for path in files:
        if path.name in remote_names:
            print(f"Gitee：跳过已存在的 {path.name}")
            continue
        size = path.stat().st_size
        if size > GITEE_MAX_BYTES:
            raise SystemExit(f"Gitee 单附件上限 100 MB，{path.name} 有 {size} 字节")
        body, content_type = multipart("file", path.name, path.read_bytes())
        upload_url = f"{base}/{release_id}/attach_files?{urllib.parse.urlencode({'access_token': token})}"
        request(
            "POST",
            upload_url,
            data=body,
            headers={"Content-Type": content_type},
            timeout=600,
        )
        print(f"Gitee：已上传 {path.name} ({size} 字节)")


def main() -> int:
    parser = argparse.ArgumentParser(description="把 GitHub 发行版同步到 Gitee 与 AtomGit")
    parser.add_argument("--assets", type=Path, required=True, help="待上传的文件目录")
    parser.add_argument("--meta", type=Path, required=True, help="gh release view --json 的输出")
    parser.add_argument(
        "--targets",
        default="atomgit,gitee",
        help="要发布的站点，逗号分隔（默认 atomgit,gitee）",
    )
    parser.add_argument("--no-latest", action="store_true", help="不把 AtomGit 发行版标为最新")
    parser.add_argument("--dry-run", action="store_true", help="只打印将要执行的动作")
    args = parser.parse_args()

    meta = load_meta(args.meta)
    files = collect_assets(args.assets)
    targets = [item.strip() for item in args.targets.split(",") if item.strip()]

    print(f"发行版 {meta['tagName']}（{meta['name']}），共 {len(files)} 个文件")

    if args.dry_run:
        for target in targets:
            print(f"[dry-run] 将发布到 {target}：{', '.join(p.name for p in files)}")
        return 0

    failures: list[str] = []

    if "atomgit" in targets:
        token = os.environ.get("ATOMGIT_TOKEN", "").strip()
        if token:
            try:
                publish_atomgit(meta, files, token, mark_latest=not args.no_latest)
            except HttpError as error:
                print(f"AtomGit 发布失败：{error}", file=sys.stderr)
                failures.append("atomgit")
        else:
            print("跳过 AtomGit：没有配置 ATOMGIT_TOKEN")

    if "gitee" in targets:
        token = os.environ.get("GITEE_TOKEN", "").strip()
        if token:
            try:
                publish_gitee(meta, files, token)
            except HttpError as error:
                print(f"Gitee 发布失败：{error}", file=sys.stderr)
                failures.append("gitee")
        else:
            print("跳过 Gitee：没有配置 GITEE_TOKEN")

    if failures:
        print(f"失败站点：{', '.join(failures)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
