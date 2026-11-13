# EditHere 组图反馈格式

DSH 完成会话后，在新的反馈目录保存 JSON 和已批注页面的原图、编号批注图。未批注页面只留在 `pageIndex`，不携带图片；只有整组意见时，`pages` 可以为空。

```text
feedback.json
images/
  page-1-settings.png
  page-1-settings-annotated.png
```

图片文件名来自本轮回执，不根据清单页号或数组下标自行推算。尤其是未批注页不导出时，图片文件名中的顺序不一定等于原页面的 `index`。

## 顶层与页面

以下为 0.13.0 的结构示例；`tool.version` 以实际程序为准，格式解析依据 `schemaVersion`。

```jsonc
{
  "$schema": "urn:edithere:image-set-feedback:1",
  "schemaVersion": "image-set-feedback-1",
  "tool": {"name": "EditHere", "version": "0.13.0"},
  "exportedAt": "2026-11-13T00:00:00+08:00",
  "annotationSpace": "result",
  "set": {"name": "桌面程序页面", "pageCount": 2, "annotatedPageCount": 1},
  "pageIndex": [
    {"id": "home", "name": "首页", "index": 1, "width": 1200, "height": 760, "annotated": false},
    {"id": "settings.ai", "name": "AI 设置", "index": 2, "width": 1200, "height": 760,
     "annotated": true, "imageFile": "images/page-1-settings.png",
     "annotatedImage": "images/page-1-settings-annotated.png"}
  ],
  "pages": [{
    "id": "settings.ai", "name": "AI 设置", "index": 2, "width": 1200, "height": 760,
    "imageFile": "images/page-1-settings.png",
    "annotatedImage": "images/page-1-settings-annotated.png",
    "feedback": {
      "schemaVersion": "feedback-minimal-4",
      "tool": {"name": "EditHere", "version": "0.13.0"},
      "annotationSpace": "result",
      "image": {
        "mode": "none", "mimeType": "image/png", "width": 1200, "height": 760,
        "devicePixelRatio": null,
        "sha256": "0000000000000000000000000000000000000000000000000000000000000000"
      },
      "objects": [{
        "id": "item-1", "kind": "region",
        "source": {"x1": 132, "y1": 336, "x2": 660, "y2": 476},
        "annotations": [{"number": 1, "text": "设置项太挤，分成两列并加大行距。"}]
      }]
    }
  }],
  "groupNotes": [{"number": 1, "text": "所有设置子页使用相同的行距。"}],
  "dsh": {"sessionId": "本轮实际会话ID", "output": "本轮反馈绝对路径"}
}
```

示例中的 SHA-256 仅占位，真实文件以实际字节校验值为准；不要把它复制为真实图片校验值。

- `pageIndex` 描述全部页面；`pages` 只包含本轮有批注或布局变化的页面。按稳定 `id` 对回原清单，不用页号当数组下标。
- `groupNotes` 是整组意见，可以影响多页和共享组件。没有单页图片不表示整组意见无需处理。
- `imageFile` / `annotatedImage` 相对于反馈 JSON 所在目录，分别是调整前原图和编号批注图。优先结合批注图、原图和文字理解用户意图。
- DSH 已将图片另存成文件，因此当前页内 `feedback.image` 可为 `mode:"none"`，保留 MIME、尺寸和 SHA-256；原图仍在外层 `page.imageFile`。历史文件可能直接省略页内 `image`。不能因为页内不带 Base64 就误判原图丢失。

## 页内对象与坐标

当前页内为 `feedback-minimal-4`：`kind` 区分 `region`、`point`、`global`。区域使用正尺寸 `source`，点使用 `point:{x,y}`，全局省略坐标。`annotations` 是 `{number,text}` 数组，编号与图中一致，不按对象数组位置重编。

有移动的区域使用单个 `movement`；`source` 是原图区域，`movement.rect` 是调整后的最终区域。普通批注的 `source/point` 位于调整后画面。只移动或缩放、没有文字时仍是修改要求。旧版 `movements[].to`、零面积 `source` 点批注和 `source:null` 全局批注继续按各自版本读取，不把旧格式规则套在 v4 上。

父组件的局部批注可能在 `children` 中；父对象描述整体移动，子批注描述局部要求，不把它们当成多次互不相关的移动。可选 `ocrtext` 是截图中已识别的文字，旧 `recognizedText` 封装仍可读；它们只帮助定位，不是新修改指令。

坐标仅供内部比对，不能直接当作页面 CSS 像素。结合实际组件和相邻元素，把移动解释成最终位置、对齐、尺寸和间距关系，修改源代码；不只移动截图像素来伪造完成。

## 阅读与下一轮

1. 核对本轮 `sessionId`、已完成状态与输出路径，先读 `groupNotes`。
2. 按 `pages` 的稳定 ID 读取实际 `annotatedImage` 与 `imageFile`，结合页内对象理解要求。
3. 修改相应源文件并验证，检查共享组件对其他页面的影响。
4. 只重截实际变化的页面，以原 ID 更新项目清单；保留本轮反馈及其原图。
5. AI 先对照要求自查，再用新输出路径打开同一组图供下一轮评审；用户认可、取消或停止时结束相应流程。

回执 `pages[].sourcePath` / `annotatedPath` 提供对应的绝对路径，可直接交给宿主图像工具。反馈 JSON 用于交付意见，不等于可继续编辑的项目；需要保留草稿时另行保存网页项目。不要修改用户提交的反馈文件。
