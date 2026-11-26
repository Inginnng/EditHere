# EditHere Schema Lifecycle & Archival Policy

`schema/` 目录目前保留了 0.7 到 0.13 演进过程中积累的 13 份 JSON Schema。为避免 1.0 正式版之前继续堆叠历史协议，本清单将所有 Schema 分为 **活跃契约（Active）** 与 **只读归档候选（Legacy Read-Only）** 两类。

## 1. 活跃契约（Active — 长期维护与强校验）

| 文件 | 标识 | 用途 |
| --- | --- | --- |
| `schema/feedback-minimal-v4.schema.json` | `feedback-minimal-4` | 单图与页内反馈现行主格式 |
| `schema/image-set-feedback-v1.schema.json` | `image-set-feedback-1` | 项目组图（最多 64 页）反馈容器 |
| `schema/image-set-project-v1.schema.json` | `edithere-image-set-1` | AI 维护的项目组图清单 |
| `schema/video-feedback-v1.schema.json` | `video-feedback-1` | 视频按时间点反馈容器 |
| `schema/video-project-v1.schema.json` | `video-project-1` | 桌面视频标注工程 |
| `schema/project-v3.schema.json` | `project-v3` | 0.12–0.13 桌面工程格式（向统一格式过渡） |
| `edithere-project-2`（见 `docs/UNIFIED-PROJECT-FORMAT.md`） | `edithere-project-2` | 0.14+ 桌面 WebView 与 Web/MCP 共用的统一便携项目格式 |

## 2. 历史只读归档候选（Legacy Read-Only — 仅做导入迁移兼容）

以下文件在 **新导出路径中已不再生成**，1.0 前迁移至 `schema/legacy/`（或独立迁移模块），不在主文档和 Skill 主链中展开：

- 图片早期反馈：`feedback-v0.7.schema.json`、`feedback-v1.schema.json`、`feedback-v1.1.schema.json`、`feedback-v2.schema.json`
- 精简版早期反馈：`feedback-minimal.schema.json`、`feedback-minimal-v2.schema.json`、`feedback-minimal-v3.schema.json`
