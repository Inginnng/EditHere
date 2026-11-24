# EditHere Unified Project Format (`edithere-project-2`)

为了解决目前桌面端 `.edithere`（`project-v3`）与网页端 `.edithere-web.json`（`edithere-web-project-1`）互不兼容的问题，0.14 架构定义了跨端统一的便携项目容器格式 `edithere-project-2`，供未来 B2 桌面 WebView 工作台、离线 Web、Web Agent 与 CLI 共同读写。

## 顶层结构

```json
{
  "schemaVersion": "edithere-project-2",
  "tool": { "name": "EditHere", "version": "0.14.1" },
  "kind": "set",
  "name": "Project Review Gallery",
  "createdAt": "2026-11-22T07:00:00.000Z",
  "pages": [
    {
      "id": "home",
      "name": "Home",
      "mediaType": "image",
      "image": {
        "mode": "embedded",
        "mimeType": "image/png",
        "width": 1440,
        "height": 900,
        "devicePixelRatio": null,
        "sha256": "...",
        "dataUrl": "data:image/png;base64,..."
      },
      "objects": []
    }
  ],
  "groupNotes": []
}
```

## 设计约定

1. **统一覆盖单图、组图与视频帧**：
   - `kind`: `"single"` | `"set"` | `"video"`。
   - `pages[]` 使用稳定 `id`（匹配 `^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$`），单图项目即为长度为 `1` 的 `pages[]`。
   - 视频项目页面携带 `mediaType: "video"` 与按时间戳索引的标注帧 `frames[]`。
2. **页内批注复用 `feedback-minimal-4` 对象模型**：
   - `pages[].objects[]` 直接采用 `feedback-minimal-4` 的 `region` / `point` / `global`、`movement`、`children` 与可选 `ocrtext`，编辑器状态与导出结果零损耗映射。
3. **图像资产校验**：
   - 支持 `image.mode: "embedded"`（便携自包含 `dataUrl`）与 `"file"`（同目录相对路径）；均要求携带 `mimeType`、像素 `width/height` 与字节 `sha256`。
4. **互操作转换**：
   - 使用 `node cli/edithere.mjs convert to-unified <manifest.json> --output <project.json>` 将 `edithere-image-set-1` 清单封装为便携 `edithere-project-2`；
   - 使用 `node cli/edithere.mjs convert to-manifest <project.json> --out-dir <dir>` 将 `edithere-project-2`（或旧版 `edithere-web-project-1`）安全解包为 `project.json` 与无损原图目录。
