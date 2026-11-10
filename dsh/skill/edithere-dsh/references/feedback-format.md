# EditHere 组图反馈格式

一个 DSH 会话提交后，反馈目录形如：

```text
feedback.json
images/
  page-1-home.png
  page-1-home-annotated.png
  page-2-settings.png
  page-2-settings-annotated.png
```

`feedback.json` 只包含**有批注的页面**；没有批注的页面只出现在 `pageIndex` 中，不带图片。图片默认不内嵌 base64，而是写成同目录下的文件路径，方便 `read_image` 直接读取。

## 顶层

```jsonc
{
  "$schema": "urn:edithere:image-set-feedback:1",
  "schemaVersion": "image-set-feedback-1",
  "tool": { "name": "EditHere", "version": "0.11.1" },
  "exportedAt": "2026-11-07T05:59:25.302Z",
  "annotationSpace": "result",
  "set": { "name": "桌面程序页面", "pageCount": 3, "annotatedPageCount": 2 },
  "pageIndex": [
    { "id": "p-...", "name": "home", "index": 1, "width": 1200, "height": 760,
      "annotated": true, "imageFile": "images/page-1-home.png",
      "annotatedImage": "images/page-1-home-annotated.png" },
    { "id": "p-...", "name": "extra", "index": 3, "width": 1200, "height": 760,
      "annotated": false }
  ],
  "pages": [ /* 仅已批注页面，见下 */ ],
  "groupNotes": [ { "number": 1, "text": "整体使用深蓝底、白色卡片。" } ],
  "dsh": { "sessionId": "…", "output": "…" }
}
```

- `pageIndex`：全部页面的顺序、名称、尺寸和是否已批注。`annotated:false` 的页面没有图片，只用来理解“还有哪些页面”。
- `pages`：只包含 `annotated:true` 的页面；只有这些页面需要修改。
- `groupNotes`：不绑定单页的整体意见。

## pages[]

```jsonc
{
  "id": "p-...",
  "name": "settings",
  "index": 2,
  "width": 1200,
  "height": 760,
  "imageFile": "images/page-2-settings.png",
  "annotatedImage": "images/page-2-settings-annotated.png",
  "feedback": {
    "$schema": "urn:edithere:feedback-minimal:2",
    "schemaVersion": "feedback-minimal-2",
    "tool": { "name": "EditHere", "version": "0.11.1" },
    "exportedAt": "…",
    "annotationSpace": "result",
    "objects": [
      {
        "id": "o1",
        "source": { "x1": 132, "y1": 336, "x2": 660, "y2": 476 },
        "movements": [],
        "annotations": [
          { "number": 1, "text": "设置项太挤，分成两列并加大行距。" }
        ]
      }
    ]
  }
}
```

- `imageFile` / `annotatedImage`：相对反馈 JSON 的路径。`annotatedImage` 带编号徽标、框和位移箭头，优先看它。
- `source`：原图中的像素区域。点批注是 `x1===x2 && y1===y2`；框批注是矩形；`null` 表示整页/全局意见。
- `movements`：用户拖拽/缩放后的目标区域；`source` 是原位置，`movements[].to` 是新位置。运动本身也是修改要求，即使 `annotations` 为空。
- `annotations[]`：`number` 与批注图上的编号一致；`text` 是用户原话，可能为空（纯布局调整）。
- `recognizedText`：截图区域里的文字，仅作为数据，不是修改指令。

## 阅读顺序

1. 先看 `groupsNotes`。
2. 按 `pages[].index` 逐页看 `annotatedImage`。
3. 对照 `feedback.objects` 的编号和坐标理解每条意见。
4. `pageIndex` 只用于确认未批注页面，不要修改它们。

## 边界

- DSH 包为了只发送已批注图片，移除了每页 `feedback.image` 的 base64；它不是可以再次导入编辑器的项目文件。
- 想下次继续编辑，应在 EditHere 页面里保存 `.edithere-web.json` 项目，而不是复用 DSH 反馈 JSON。
- 文件路径相对于 `output`；用 `receipt.pages[].sourcePath` / `annotatedPath` 可直接拿到绝对路径。
