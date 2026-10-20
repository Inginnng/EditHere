# EditHere 反馈 JSON 格式优化方案

> 文档性质：设计方案。本文依据用户提供的页面内容整理，保留优化项、字段示例、迁移规则与实施批次；不代表各项已实现。
>
> 示例说明：代码中的 `…`、省略的哈希及 Schema URL 为示意或占位内容，工具版本与时间为示例值；部分代码块仅用于展示结构。

> **目标**：AI 只拿到一份反馈 JSON，就能判断「改哪个应用、哪个页面、哪个控件、改成什么样」，不依赖额外文档；同时控制文件体积，旧格式仍可导入。

## 实施进度

主要使用场景：导入截图后批注。以下勾选表示已完成实现及相关验证；各阶段单独提交并推送；J01/J04/J05 已通过 [PR #17](https://github.com/Inginnng/EditHere/pull/17) 合入默认分支 `codex/native`，J03 已通过 [PR #18](https://github.com/Inginnng/EditHere/pull/18) 合入默认分支，GitHub 与 Gitee 均已核对为 `533b0e9`。J07 已完成实现、验证及双远端推送，主分支合入状态见 [PR #19](https://github.com/Inginnng/EditHere/pull/19)。

- [x] J01：版本号、工具标识、导出时间与 Schema 标识（本轮第 1 阶段）
- [ ] J02：图片尺寸与缩放比例
- [x] J03：原图模式重构（含三模式、编号预览与附件校验）
- [x] J04：对象 ID 与界面一致的批注编号（本轮第 2 阶段）
- [x] J05：截图区域本机 OCR、编辑与导出（本轮第 3 阶段）
- [ ] J06：有系统控件信息时可选透传；截图导入场景暂缓
- [x] J07：窗口标题、采集上下文与 AI 主动提供页面说明
- [x] J08：显式标注类型（`feedback-minimal-3` 已通过 [PR #20](https://github.com/Inginnng/EditHere/pull/20) 合入 GitHub 默认分支）
- [x] J09：移动与缩放结果（初版 `result` 已通过 PR #20 合入 GitHub 默认分支；本轮精简为单个 `movement`，下文示例保留早期方案）
- [ ] J10：坐标约定（实施前需修正普通批注与移动源的空间区别）
- [ ] J11：视频反馈去重
- [ ] J12：多轮反馈关联
- [ ] J13：完整 Schema、CLI 回执与文档同步；已同步完成 J01/J03/J04/J05/J07/J08/J09 所需部分
- [ ] J14：可选纯文本摘要

### 阶段记录

J01/J04/J05 原始阶段代码与提交：[GitHub 分支](https://github.com/Inginnng/EditHere/tree/codex/0.11.1-test) · [GitHub 提交记录](https://github.com/Inginnng/EditHere/commits/codex/0.11.1-test) · [Gitee 分支](https://gitee.com/InnGing/EditHere/tree/codex/0.11.1-test)。当前主分支：[GitHub](https://github.com/Inginnng/EditHere/tree/codex/native) · [Gitee](https://gitee.com/InnGing/EditHere/tree/codex/native)。

| 阶段 | 实现 | 验证 | 推送记录 |
| --- | --- | --- | --- |
| J01 | 已完成 | 桌面 7 组、连接器 27 例、Web 单元与浏览器流程、真实导出 Schema 和打包检查通过 | `b9011b4`，GitHub / Gitee 已确认 |
| J04 | 已完成 | 桌面 292 项、连接器 31 例、Web 68 例及真实编号/提交流程、Schema 校验通过 | `00739ea`，GitHub / Gitee 已确认 |
| J03 | 已完成 | 桌面 9 组、连接器 42 例（1 项 Windows 符号链接权限跳过）、Web 87 单元及浏览器 22 项、三模式/MCP/CLI 流程、真实 Schema 和正式 Windows 包预览导出通过 | `47ef76d`；已合入主分支（PR #18） |
| J05 | 已完成 | 桌面 9 组（含真实 Windows OCR）、连接器 36 例、Web 65 例及浏览器流程、真实导出 Schema、打包与 863 条翻译检查通过 | `0a4e63f`；三阶段已合入主分支（PR #17） |
| J07 | 已完成 | 桌面 11 组、Windows 独立窗口快照、连接器 47 例（1 项权限跳过）、Web 96 单元及离线/CLI 6 项/MCP 9 项回传、真实 Schema、21 项打包与 897 条翻译检查通过 | `1a13d27`，GitHub / Gitee 已确认；合入状态见 [PR #19](https://github.com/Inginnng/EditHere/pull/19) |

---

## 总览

| 编号 | 优化项 | 优先级 | 批次 |
| --- | --- | --- | --- |
| J01 | 版本号与工具标识 | P0 | 第一批 |
| J02 | 图片尺寸与缩放比例 | P0 | 第一批 |
| J03 | 原图模式重构 | P0 | 第二批 |
| J04 | 编号与 ID | P0 | 第一批 |
| J05 | 框选区域文字识别（OCR） | P0 | 第二批 |
| J06 | 控件语义线索 | P1 | 第二批 |
| J07 | 窗口标题与采集上下文 | P1 | 第二批 |
| J08 | 显式标注类型 | P1 | 第一批 |
| J09 | 移动与缩放的结果值 | P1 | 第一批 |
| J10 | 坐标约定写进文件 | P2 | 第一批 |
| J11 | 视频反馈去重 | P2 | 第三批 |
| J12 | 多轮反馈关联 | P2 | 第三批 |
| J13 | Schema、CLI 回执与文档同步 | P1 | 第一批 |
| J14 | 纯文本摘要（可选） | P3 | 第三批 |

## 命名约定

- 新导出格式命名为 `feedback-minimal-3`，Schema 文件为 `schema/feedback-minimal-v3.schema.json`，`$id` 为 `urn:edithere:feedback-minimal:3`；`feedback-minimal-2`、无版本对象格式和旧并行数组继续可读。
- 不使用 `feedback-2` / `feedback-v2`：仓库中的 `feedback-v2.schema.json` 是早期**项目文档**格式，重名会混淆。
- 当前没有版本号的对象格式追认为 `feedback-minimal-1`；更早的 `annotations` + `changes` 并行数组仍称「旧格式」。

---

## 优化项明细

### J01 版本号与工具标识 · P0

实施说明：已落地。当前导出只保留 `schemaVersion: "feedback-minimal-3"`，省略重复的根级 `$schema`；旧文件里的 `$schema` 仍可读取，本地 Schema 保留稳定标识 `urn:edithere:feedback-minimal:3`。工具版本来自实际构建。视频容器版本保持不变，帧内图片反馈支持新格式。以下方案示例中的旧元数据仅用于说明演进过程；实际导出仅在有位移或缩放时写入单个 `movement`，不再输出 `result` 或 `movements`，读取兼容旧字段。

| 项目 | 现在 | 修改后 |
| --- | --- | --- |
| 格式版本 | 没有，只能按结构猜是哪种格式 | `schemaVersion: "feedback-minimal-2"` |
| 生成工具 | 没有 | `tool: { name, version }` |
| 导出时间 | 没有 | `exportedAt`（带时区的 ISO 时间） |
| Schema 链接 | 没有 | `$schema` 指向 Schema 文件 |

现在：

```json
{ "annotationSpace": "result", "objects": [ … ] }
```

修改后：

```json
{
  "$schema": "https://…/schema/feedback-minimal-v2.schema.json",
  "schemaVersion": "feedback-minimal-2",
  "tool": { "name": "EditHere", "version": "0.12.0" },
  "exportedAt": "2026-10-16T16:30:00+08:00",
  "objects": [ … ]
}
```

- 读取规则：有 `schemaVersion` 按版本解析；没有但有 `objects` 视为 `feedback-minimal-1`；有 `annotations` + `changes` 视为旧格式。
- 版本命名统一为「格式名-主版本」：图片反馈 `feedback-minimal-2`、视频反馈 `video-feedback-2`。项目文件的 `3.0.0` 可以暂不改动。

### J02 图片尺寸与缩放比例 · P0

| 项目 | 现在 | 修改后 |
| --- | --- | --- |
| 图片宽高 | 没有；`--no-image` 时 AI 不知道图多大 | `image.width` / `image.height` |
| 缩放比例 | 没有；分不清 1x 还是 2x 截图 | `image.devicePixelRatio` |
| 坐标换算 | 文档提醒不能直接当 CSS 像素，但文件里没有换算依据 | CSS 像素 ≈ 图像像素 ÷ devicePixelRatio |

- Windows：取截图所在显示器的缩放比例（150% 写 1.5）。
- macOS：取所在屏幕的 backingScaleFactor。
- 来源为文件或剪贴板、无法得知时写 `null`。
- 字段统一放在 J03 的 `image` 对象里。

### J03 原图模式重构 · P0

实施状态：已完成实现与验证。默认 file，支持 embedded / none 及显式编号 JPEG；桌面、CLI、连接器和 Web 对齐，Web 多文件下载使用 ZIP。尺寸与 SHA-256 随三种模式保留，devicePixelRatio 未知时为 null。

现在：

- `image` 是一个字符串，内嵌 PNG / JPEG 的 data URL；`annotate` 和 `export` 默认内嵌。
- 用 `--no-image` 后，只能靠「同名 PNG」这个隐含约定找回原图。
- 视频反馈已经用 `imageFile` 引用帧截图，图片反馈反而没有。

问题：

- base64 会让体积膨胀约三分之一。1080p 截图常见 0.5–2 MB，AI 把 JSON 当文本读入时，这段内容会占掉大量上下文，却毫无用处。
- 没有校验信息，无法确认外部图片就是这份 JSON 对应的原图。

修改后，`image` 改为对象，支持三种模式：

| 模式 | 写法 | 适用场景 |
| --- | --- | --- |
| `file`（新默认） | `file` 指向同目录的图片文件 | AI 工作流、命令行 |
| `embedded` | `dataUrl` 内嵌图片 | 单文件分享、导入 |
| `none` | 只保留尺寸和 `sha256` | 接收方已有原图 |

另加可选 `preview`：画好 ①②③ 编号的标注示意图（JPEG），多模态模型看一张图就能对上每条意见。

现在：

```json
"image": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAA…（约 1 MB）"
```

修改后：

```json
"image": {
  "mode": "file",
  "file": "feedback-0002.png",
  "mimeType": "image/png",
  "width": 1920,
  "height": 1080,
  "devicePixelRatio": 2,
  "sha256": "3a7bd3e2…",
  "preview": "feedback-0002.annotated.jpg"
}
```

- 命令行：`annotate` / `export` 默认 `file` 模式，在输出 JSON 同目录写入图片；新增 `--embed-image`（内嵌）、`--preview`（输出标注示意图）；`--no-image` 保留，对应 `none` 模式。
- 界面：「查看 JSON」里的原图选项改成单选「原图：随文件保存 / 内嵌到 JSON / 不包含」，加复选「附带标注示意图」；JPEG 压缩作为内嵌和示意图的子选项。
- 兼容：读取时遇到字符串 `image`，按 `embedded` 处理。

### J04 编号与 ID · P0

实施说明：已落地。对象 ID 只在当前图片反馈内唯一，视频按帧作用域；导入再导出允许重新生成 ID。编号沿用界面原批注顺序，空移动意见可能造成间隙；导入允许间隙并按编号还原意见顺序，随后按当前界面重新编号。J01 阶段的字符串批注和缺少 ID 的反馈继续可读。

| 项目 | 现在 | 修改后 |
| --- | --- | --- |
| 对象标识 | 没有 | `objects[].id`，例如 `"o1"` |
| 批注编号 | 界面和示意图上有 ①②③，项目文件里也有 `number`，精简反馈没有 | `annotations[].number`，与界面一致 |
| 批注结构 | 纯字符串数组 | `{ "number": 1, "text": "…" }` |

现在：

```json
"annotations": ["按钮往右，与输入框对齐"]
```

修改后：

```json
"id": "o1",
"annotations": [{ "number": 1, "text": "按钮往右，与输入框对齐" }]
```

- 兼容：读取时仍接受字符串批注，按出现顺序补编号。

### J05 框选区域文字识别（OCR）· P0

实施范围：桌面版使用现有本机 OCR 自动识别批注区域；独立 Web 与 MCP 内嵌页支持导入、保留、修改、删除及关闭导出，暂不自动识字。当前 `feedback-minimal-3` 只导出与 `kind` 同级的 `ocrtext` 字符串，省略引擎、语言、置信度和编辑标记；旧 `recognizedText` 封装继续可读。已有结果随内容保留，不因大爆炸调整而重跑。不会凭图片生成 DOM、组件名或源码位置。下文的封装字段及示例记录早期方案，当前输出以 `ocrtext` 为准。

现在：反馈只记录坐标。文档也写明「图像反馈本身不提供 DOM、组件名或源码位置」，AI 只能自己看图猜文字，没看图时完全不知道框住的是什么。

修改后：用户框选、智能选中或移动组件后，在本机识别 `source` 区域内的文字，写入 `recognizedText`。

| 字段 | 含义 |
| --- | --- |
| `text` | 合并后的文字，最多 500 字 |
| `lines` | 原方案可选；本阶段不输出或读取逐行数据 |
| `confidence` | 可选，0–1；仅保留真实值，现有本机引擎不统一返回该值，因此自动识别不填 |
| `engine` | 识别引擎，例如 `windows-ocr`、`apple-vision` |
| `language` | 识别语言，例如 `zh-Hans` |
| `edited` | 用户手动改过识别结果时为 `true` |

现在：

```json
{
  "source": { "x1": 120, "y1": 80, "x2": 360, "y2": 128 },
  "movements": [],
  "annotations": ["这个按钮文案改短"]
}
```

修改后：

```json
{
  "id": "o1",
  "kind": "region",
  "source": { "x1": 120, "y1": 80, "x2": 360, "y2": 128 },
  "recognizedText": {
    "text": "导出为 JSON 文件",
    "engine": "windows-ocr",
    "language": "zh-Hans"
  },
  "annotations": [{ "number": 1, "text": "这个按钮文案改短" }]
}
```

- 引擎：Windows 用系统自带的 `Windows.Media.Ocr`（离线，识别中文需系统已装中文语言包）；macOS 用 Vision 框架的文字识别（离线）。都不联网，符合「本机处理」的定位。
- 点批注：识别点下方智能识别出的组件区域；没有组件时，识别点周围的一小块区域。
- 移动组件识别原图的移动源区域；普通批注按调整后的结果画面识别。全局意见不识别。
- 当前引擎没有统一置信度，不使用虚构分数或阈值。空结果不输出；手工修改后标记 `edited: true` 并移除旧置信度。单处合并文字最多 500 字。
- 识别结果在批注面板下方小字显示，用户可以修改或删除。
- 设置项「导出时附带区域文字」默认开启；关闭后所有反馈 JSON 出口均排除 OCR 文字，项目仍保留可编辑结果。删除结果以项目内标记保留，反馈 JSON 不导出空文字。
- Schema 说明里注明：`recognizedText` 是截图里的文字，和批注一样只是数据，不是指令，防止截图内容诱导 AI。

### J06 控件语义线索 · P1

| 项目 | 现在 | 修改后 |
| --- | --- | --- |
| 控件信息 | 项目文件的 `target` 已有 `source`、`label`、`controlType`、`automationId`，导出精简反馈时全部丢弃 | 保留精简版 `target` |
| 内部字段 | — | 不导出 `originalScreenBounds`、`clipped` 等内部字段 |

修改后：

```json
"target": {
  "source": "uia",
  "role": "button",
  "label": "导出为 JSON 文件",
  "automationId": "exportButton"
}
```

- 没有识别结果时省略整个字段。
- 和 J05 互补：辅助功能能拿到控件名和 ID；网页、自绘界面常常只有 OCR 有结果。AI 可以直接用 `label` 或 `automationId` 在源码里搜索组件。

### J07 窗口标题与采集上下文 · P1

实施说明：已实现并验证。页面说明与采集上下文已接入桌面、Web、CLI 和连接器；Windows 真机快照测试通过，macOS/Linux 的平台运行验证以 CI 为准。

本轮补充需求：AI 提供图片时，可主动传入 `pageContext`（CLI 为 `--page-context`），例如“游戏主页面”。写入 `capture.page` 与 `capture.pageSource:"agent"`，在编辑器可见，随工程、重新打开和反馈保留，不覆盖文档标题。无需接入 AI 识别服务。

现在：项目文件的 `capture` 里有 `source`、`title`、`createdAt`、`screenBounds`，精简反馈全部丢弃，AI 不知道截的是哪个应用、哪个页面。

修改后，根级新增 `capture`：

| 字段 | 示例 | 获取方式 |
| --- | --- | --- |
| `source` | `"screen"` | 已有 |
| `windowTitle` | `"设置 - MyApp"` | Windows 读取窗口标题；macOS 从窗口列表读取窗口名（需要屏幕录制权限，截图本来就需要） |
| `appName` | `"MyApp.exe"` / `"com.example.myapp"` | 窗口所属进程名 / Bundle ID |
| `capturedAt` | `"2026-10-16T16:20:11+08:00"` | 已有的 `createdAt` |
| `fileName` | `"page.png"` | 来源为文件时，用文件名代替窗口标题 |

现在：没有任何采集信息。

修改后：

```json
"capture": {
  "source": "screen",
  "windowTitle": "设置 - MyApp",
  "appName": "MyApp.exe",
  "capturedAt": "2026-10-16T16:20:11+08:00"
}
```

- 选区跨多个窗口时，取抓取时选区中心点所在的前方窗口；滚动截图沿用开始时的快照。Linux X11 同样读取可用窗口信息；Wayland 无法获取时省略，不用当前前台窗口补造。
- 页面说明通过 `pageContext` / `--page-context` 主动传入，去除首尾空白后最多 500 个 UTF-16 单元；写入 `page` 与 `pageSource:"agent"`，随图片工程与视频各帧保留。
- `fileName` 只保存导入文件的原始名称，作为来源数据，不用于附件读取；旧文件没有 capture 仍可读。
- 设置项「导出时附带窗口标题」默认开启，可以关闭（标题里可能有文档名、邮箱等信息）。关闭时所有反馈出口只移除 windowTitle；工程保留原值，page 与 appName 不受影响。
- 浏览器网址暂不采集（隐私敏感、实现复杂），后续再评估。

### J08 显式标注类型 · P1

| 类型 | 现在 | 修改后 |
| --- | --- | --- |
| 区域 | `source` 为矩形 | `kind: "region"` 加 `source` |
| 点 | 零面积矩形 `x1 == x2`、`y1 == y2`，与「右下不包含」约定冲突 | `kind: "point"` 加 `point: { x, y }` |
| 全局 | `source: null` | `kind: "global"`，不写 `source` 和 `point` |

现在：

```json
"source": { "x1": 640, "y1": 300, "x2": 640, "y2": 300 }
```

修改后：

```json
"kind": "point",
"point": { "x": 640, "y": 300 }
```

- 旧格式本来就有 `point` 字段，现在的对象格式反而丢了，等于把它补回来。

### J09 移动与缩放的结果值 · P1

现在：`movements` 只给每一步的 `to`；文档里「按顺序记录」和「`to` 为最终区域」两句并列，有歧义；位移和缩放比要 AI 自己算。

修改后：保留 `movements` 作为绝对目标序列，新增 `result`；它不是完整鼠标事件历史。

现在：

```json
"movements": [{ "to": { "x1": 160, "y1": 80, "x2": 400, "y2": 128 } }]
```

修改后：

```json
"movements": [{ "to": { "x1": 160, "y1": 80, "x2": 400, "y2": 128 } }],
"result": {
  "rect": { "x1": 160, "y1": 80, "x2": 400, "y2": 128 },
  "dx": 40,
  "dy": 0
}
```

- Schema 写明：`result.rect` 等于最后一条 movement 的 `to`。
- 可选：位移小于 2 px 且没有缩放时加 `minor: true`，提示可能是误拖。

### J10 坐标约定写进文件 · P2

| 项目 | 现在 | 修改后 |
| --- | --- | --- |
| 坐标空间 | `annotationSpace` 只能是 `"result"`，不携带信息，还容易让人以为 `source` 也在调整后的坐标里 | 根级 `coordinates` 一次说清 |
| 约定位置 | 只写在文档里 | 写在文件里，AI 不看文档也能理解 |

修改后：

```json
"coordinates": {
  "unit": "image-pixel",
  "origin": "top-left",
  "rectangle": "x2y2-exclusive",
  "source": "original-image",
  "result": "adjusted-canvas"
}
```

- 去掉 `annotationSpace`；读取旧文件时忽略该字段。

### J11 视频反馈去重 · P2

现在：`frames[].feedback.objects` 和根级 `objects` 是同一批内容存了两份，文档要专门提醒「不要按两遍执行」。

修改后：帧内保留完整对象，根级改为只放引用的 `timeline`，版本升为 `video-feedback-2`；帧图片沿用 J03 的 `image` 对象。

现在：

```json
"objects": [
  { "timestampMs": 1200, "frameId": "f1", "source": { … }, "movements": [ … ], "annotations": [ … ] }
]
```

修改后：

```json
"timeline": [
  { "timestampMs": 1200, "frameId": "f1", "objectId": "o1" }
]
```

### J12 多轮反馈关联 · P2

现在：每轮生成一个新文件，文件之间没有关联，AI 难以判断哪些意见是上一轮留下的。

修改后：

```json
"session": {
  "id": "8f3c2a1e-…",
  "round": 2,
  "previous": "feedback-0001.json"
}
```

- 同一张图或同一个项目连续多轮标注时沿用同一个 `id`，`round` 递增，`previous` 指向上一轮的文件名。
- 后续可选：对象级 `carriedOver: true`，标记上一轮没解决、这轮继续提的意见。

### J13 Schema、CLI 回执与文档同步 · P1

- 新增 `schema/feedback-minimal-v3.schema.json`，保留 v2 Schema 与兼容入口。
- 导入兼容旧格式、无版本对象、`feedback-minimal-2` 和 `feedback-minimal-3`；字符串批注和字符串 `image` 自动转换。
- CLI 成功回执统一带 `schemaVersion`、`imageFile`、`previewFile`（现在只有视频导出回执带 `schemaVersion`）。
- 过渡期提供 `--format feedback-minimal-1` 输出旧格式，给已经写死解析逻辑的旧 skill 和脚本使用。
- 同步更新：使用指南「反馈 JSON」、AI 与命令行文档「AI 如何理解反馈」、`skills/edithere/SKILL.md` 的读取说明。
- 测试样例：三种 `kind`、三种原图模式、有无 OCR 结果、旧格式导入，各一份。
- 预留 `extensions` 对象，第三方或实验字段放在这里，避免每加一个字段都要升主版本。

回执现在：

```json
{"ok":true,"command":"annotate","output":"C:/work/feedback-0002.json","annotations":2,"imageIncluded":true}
```

回执修改后：

```json
{"ok":true,"command":"annotate","schemaVersion":"feedback-minimal-2","output":"C:/work/feedback-0002.json","imageFile":"C:/work/feedback-0002.png","previewFile":null,"annotations":2}
```

### J14 纯文本摘要（可选）· P3

- 现在：要复述反馈必须先解析 JSON。
- 修改后：可选 `summary` 字段或同名 `.md` 文件，默认关闭。
- 示例：`1. [按钮「导出为 JSON 文件」] 右移 40 px：按钮往右，与输入框对齐`
- 用途：便于人工核对，也方便不擅长处理坐标的模型。

---

## 完整对比

现在（feedback-minimal-1）：

```json
{
  "annotationSpace": "result",
  "image": "data:image/png;base64,iVBORw0KGgo…（约 1 MB）",
  "objects": [
    {
      "source": { "x1": 120, "y1": 80, "x2": 360, "y2": 128 },
      "movements": [{ "to": { "x1": 160, "y1": 80, "x2": 400, "y2": 128 } }],
      "annotations": ["按钮往右，与输入框对齐"]
    },
    {
      "source": { "x1": 640, "y1": 300, "x2": 640, "y2": 300 },
      "movements": [],
      "annotations": ["这里太挤"]
    },
    {
      "source": null,
      "movements": [],
      "annotations": ["整体配色偏暗"]
    }
  ]
}
```

修改后（feedback-minimal-2）：

```json
{
  "$schema": "https://…/schema/feedback-minimal-v2.schema.json",
  "schemaVersion": "feedback-minimal-2",
  "tool": { "name": "EditHere", "version": "0.12.0" },
  "exportedAt": "2026-10-16T16:30:00+08:00",
  "session": { "id": "8f3c2a1e-…", "round": 2, "previous": "feedback-0001.json" },
  "capture": {
    "source": "screen",
    "windowTitle": "设置 - MyApp",
    "appName": "MyApp.exe",
    "capturedAt": "2026-10-16T16:20:11+08:00"
  },
  "image": {
    "mode": "file",
    "file": "feedback-0002.png",
    "mimeType": "image/png",
    "width": 1920,
    "height": 1080,
    "devicePixelRatio": 2,
    "sha256": "3a7bd3e2…",
    "preview": "feedback-0002.annotated.jpg"
  },
  "coordinates": {
    "unit": "image-pixel",
    "origin": "top-left",
    "rectangle": "x2y2-exclusive",
    "source": "original-image",
    "result": "adjusted-canvas"
  },
  "objects": [
    {
      "id": "o1",
      "kind": "region",
      "source": { "x1": 120, "y1": 80, "x2": 360, "y2": 128 },
      "recognizedText": { "text": "导出为 JSON 文件", "confidence": 0.97, "engine": "windows-ocr", "language": "zh-Hans" },
      "target": { "source": "uia", "role": "button", "label": "导出为 JSON 文件", "automationId": "exportButton" },
      "movements": [{ "to": { "x1": 160, "y1": 80, "x2": 400, "y2": 128 } }],
      "result": { "rect": { "x1": 160, "y1": 80, "x2": 400, "y2": 128 }, "dx": 40, "dy": 0 },
      "annotations": [{ "number": 1, "text": "按钮往右，与输入框对齐" }]
    },
    {
      "id": "o2",
      "kind": "point",
      "point": { "x": 640, "y": 300 },
      "recognizedText": { "text": "高级设置", "confidence": 0.91, "engine": "windows-ocr", "language": "zh-Hans" },
      "movements": [],
      "annotations": [{ "number": 2, "text": "这里太挤" }]
    },
    {
      "id": "o3",
      "kind": "global",
      "movements": [],
      "annotations": [{ "number": 3, "text": "整体配色偏暗" }]
    }
  ]
}
```

## 迁移与兼容

- 写入：只写 `feedback-minimal-3`；不提供新的 CLI 降级开关。
- 读取顺序：先看 `schemaVersion`；`feedback-minimal-2` 按旧对象格式读取；没有时有 `objects` 按旧对象格式处理；有 `annotations` + `changes` 按旧格式处理。
- 自动转换：字符串批注 → `{ number, text }`；字符串 `image` → `embedded` 模式；零面积矩形 → `kind: "point"`；`source: null` → `kind: "global"`；移动区域补充 `result`。

## 实施批次

1. 第一批（格式层，改动小）：J01、J02、J04、J08、J09、J10、J13
2. 第二批（原图与上下文）：J03、J05、J06、J07
3. 第三批（周边与工作流）：J11、J12、J14

## 优化项追踪

原页面列有「反馈 JSON 优化项」追踪入口，但粘贴内容未包含链接或具体追踪记录。
