# EditHere 发版规范

每个正式版本必须在 **GitHub、AtomGit 和 Gitee** 都有公开 Release，才算发版完成。GitHub 是发行说明、签名、校验文件和安装包的唯一来源。三个站点的同名标签必须指向同一个提交。

## 正常发版

1. 通过 GitHub Actions 的 **Release** 工作流构建并发布正式版。推送正式标签 `vX.Y.Z` 会触发工作流。工作流先构建和验证附件，再公开 GitHub Release。
2. `mirror-release` 下载 GitHub 已发布的全部附件，校验文件名、大小和 SHA-256。Gitee 和 AtomGit 均从 GitHub 单向拉取分支和标签。工作流为每站等待镜像标签最多约 5 分钟，确认同一提交后发布该站的 Release。
3. 等 `mirror-release` 通过，再通知用户、更新下载入口或把任务标为完成。任务会从公开接口回查两个发行版。AtomGit 必须有全部附件，Gitee 的附件必须原生上传或满足下述配额替代条件。

## 失败后补发

- **标签缺失或不一致**。等待超时后，在 Gitee 的「仓库镜像管理」里点「更新」，或在 AtomGit 的「仓库镜像」里点「立即同步」。若镜像设为手动，每次发版先更新镜像。标签可见后运行 `mirror_only`。同名标签指向不同提交时先查明原因。
- **AtomGit API 拒绝写入镜像仓库**。在 AtomGit 的「新建发行版」中选择已同步的标签，复制 GitHub 发行说明，上传从 GitHub 下载并校验过的全部附件。若 GitHub 将该版本标为最新，AtomGit 也选「设置为最新版本」。
- **Gitee 附件配额不足**。先上传能容纳的附件。只对配额挡住的文件，在同一版 Gitee 发行说明里添加经过 AtomGit 验收的同版本文件直链。工作流会记录原生附件和直链数量。其他上传错误需要修复并重试。
- **只补发或重新验收**。在 GitHub Actions 的 **Release → Run workflow** 选择默认分支，输入已公开的 GitHub 标签，勾选 `mirror_only` 并取消 `validate_only`。该模式复用原包，补齐缺项并重新回查两站。工作流需要仓库的 `GITEE_TOKEN` 和 `ATOMGIT_TOKEN` 执行 API 写入，已齐全的公开发行版可直接验收。

## 仓库镜像设置

Gitee 和 AtomGit 均使用 GitHub → 国内仓库的 Pull 镜像。镜像负责分支、标签和提交。`mirror-release` 负责两站的发行说明、附件和公开验收。
