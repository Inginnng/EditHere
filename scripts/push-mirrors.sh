#!/usr/bin/env bash
# 将本地分支与标签同步到 Gitee / AtomGit 镜像仓库
# 用法：
#   scripts/push-mirrors.sh                  # 推送当前分支 + 标签到 Gitee
#   ALL_BRANCHES=1 scripts/push-mirrors.sh   # 推送全部分支 + 标签
#   DRY_RUN=1 scripts/push-mirrors.sh        # 只演练，不真正写入
#   FORCE_TAGS=1 scripts/push-mirrors.sh     # 覆盖远端已存在但指向不同的标签
#   scripts/push-mirrors.sh gitee atomgit    # 指定远程；AtomGit 是只读镜像，会失败
#
# AtomGit 上的仓库是从 GitHub 拉取的只读镜像仓库，不接受推送（服务端返回
# "the repository is an image repository"）。它的内容由 AtomGit 自己跟随 GitHub，
# 不需要也不能由本地推送，因此默认只推 Gitee。
set -uo pipefail

root="$(git rev-parse --show-toplevel 2>/dev/null)" || {
  echo "请在 EditHere 仓库内运行本脚本。" >&2
  exit 1
}
cd "$root"

targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then
  targets=(gitee)
fi

branch="${BRANCH:-$(git rev-parse --abbrev-ref HEAD)}"
push_args=()
if [ "${DRY_RUN:-0}" != "0" ]; then
  push_args+=(--dry-run)
fi

# 标签逐个比对：缺失的批量推送，指向冲突的默认跳过
push_tags() {
  local target="$1"
  local -a missing=() conflicts=() missing_names=()
  local tag sha ref name
  local -A remote_sha=()

  while read -r sha ref; do
    [ -z "${ref:-}" ] && continue
    case "$ref" in *'^{}'*) continue ;; esac
    name="${ref#refs/tags/}"
    remote_sha["$name"]="$sha"
  done < <(git ls-remote --tags "$target" 2>/dev/null)

  while read -r tag; do
    [ -z "$tag" ] && continue
    sha="$(git rev-parse "refs/tags/$tag")"
    if [ -z "${remote_sha[$tag]:-}" ]; then
      missing+=("refs/tags/$tag")
      missing_names+=("$tag")
    elif [ "${remote_sha[$tag]}" != "$sha" ]; then
      conflicts+=("$tag")
    fi
  done < <(git tag)

  if [ ${#missing[@]} -gt 0 ]; then
    echo "推送标签：${missing_names[*]}"
    git push "${push_args[@]}" "$target" "${missing[@]}" || return 1
  else
    echo "标签已是最新。"
  fi

  if [ ${#conflicts[@]} -gt 0 ]; then
    if [ "${FORCE_TAGS:-0}" != "0" ]; then
      echo "覆盖指向不同的标签：${conflicts[*]}"
      git push "${push_args[@]}" --force "$target" "${conflicts[@]/#/refs/tags/}" || return 1
    else
      echo "跳过已存在且指向不同的标签：${conflicts[*]}（确认后可用 FORCE_TAGS=1 覆盖）"
    fi
  fi
  return 0
}

failed=()
for target in "${targets[@]}"; do
  if ! git remote get-url "$target" >/dev/null 2>&1; then
    echo "跳过 $target：本地没有配置同名远程。"
    continue
  fi
  echo "==> 推送到 $target"
  if [ "${ALL_BRANCHES:-0}" != "0" ]; then
    git push "${push_args[@]}" "$target" --all || { failed+=("$target"); continue; }
  else
    git push "${push_args[@]}" "$target" "refs/heads/${branch}:refs/heads/${branch}" \
      || { failed+=("$target"); continue; }
  fi
  if [ "${SKIP_TAGS:-0}" = "0" ]; then
    push_tags "$target" || { failed+=("$target"); continue; }
  fi
  echo "$target：完成。"
done

if [ ${#failed[@]} -gt 0 ]; then
  echo "失败目标：${failed[*]}" >&2
  echo "认证问题见 docs/DEVELOPMENT.md 的「镜像推送」一节。" >&2
  exit 1
fi

echo "全部镜像同步完成。"
