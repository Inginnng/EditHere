# 项目组图与结果审批

EditHere 是开发结果的组图评审台。用户启用组图工作流后，由 AI 运行项目、收集页面并维护清单；用户逐页评审或写整组意见后一次提交；AI 根据代码影响面分层改码、按需重截受影响页面并更新同一清单，形成「收集 → 评审 → 改码 → 精准换图 → 复审」闭环。

桌面（Windows / macOS / Linux）、桌面 MCP 连接器、Trae 插件、Web Agent / MCP Apps、离线 Web 与 DSH 共用同一套页面 ID 与 `image-set-feedback-1` 容器。实际可用能力仍以所用程序版本、`--help` 与宿主实际连接的工具为准。

## 1. 首次建立完整页面覆盖

项目级组图评审默认覆盖全部已实现且可访问的页面。先查看路由、导航、页面/场景目录与运行入口，列出全部可访问页面与关键状态，再逐项进入真实运行状态截图：

- **包含范围**：主页面、设置子页、与本轮任务有关的弹窗/抽屉、关键交互结果，以及有明确验收要求时的明暗主题或窄屏/移动端视口。用户明确指定只评某些页面时沿用该范围。
- **真实运行截图**：已有设计图只作参考，旧图、示例图或固定测试渲染不能冒充当前运行画面。因登录态、外部服务或工具受限导致个别页面无法访问时，保留已完成页面并在说明中列出缺口与最小补充条件，不用少量代表图宣称全部覆盖。
- **画质标准**：优先保存真实设备像素的无损 PNG，区分逻辑尺寸与设备像素尺寸，不用缩小后的 JPEG 预览充当原图，也不靠改扩展名伪装无损图。详细采集流程与工具避坑见 `references/capture.md`。

## 2. 维护稳定项目清单

每个项目维护一份清单（默认 `.edithere/results/project.json`）：

```json
{
  "schemaVersion": "edithere-image-set-1",
  "name": "桌面程序页面",
  "pages": [
    {"id": "home", "name": "首页", "path": "shots/home.png"},
    {"id": "settings", "name": "设置", "path": "shots/settings.png"}
  ]
}
```

- `id` 是页面跨轮次的稳定身份（例如 `home`、`settings.ai`、`inventory`），重命名、改尺寸或换图时保持不变；新页面才分配新 ID。`name` 应面向不看源码的用户可读。
- 已有清单先读取再增量维护，保留既有页序与 ID。建议按轮次归档截图与反馈（如 `shots/round-001/`、`reviews/round-001/feedback.json`），**不要覆盖用户当前正在评审的截图或旧轮次反馈**。
- 使用技能自带的 `scripts/image-set.mjs`（传该脚本的绝对路径，无需依赖源码仓库）：

```powershell
node "<skill-dir>/scripts/image-set.mjs" create .edithere/results/project.json --name "桌面程序页面"
node "<skill-dir>/scripts/image-set.mjs" upsert .edithere/results/project.json --id home --name "首页" --image shots/home.png
node "<skill-dir>/scripts/image-set.mjs" list .edithere/results/project.json
node "<skill-dir>/scripts/image-set.mjs" remove .edithere/results/project.json --id obsolete-page
```

- `upsert` 自动计算并写入原图 SHA-256；截图更新后必须重新 `upsert`，避免把旧图提交给用户。`remove` 仅从清单移除引用，保留原图文件。

## 3. 交给用户审批

每轮审批必须使用**新的唯一输出文件或输出目录**。桌面或服务端在打开组图时会读取当轮截图快照，后续更新清单不会悄悄改写用户正在审阅的画面。

| 入口 | 发起方式 |
| --- | --- |
| 桌面 CLI | `edithere-cli annotate <清单绝对路径> --output <新的反馈绝对路径>`（保持同一次进程等待提交） |
| 桌面 MCP 连接器 / 插件 | `edithere_annotate_start` 的 `imagePath` 传清单绝对路径，可用 `outputDir` 指定本轮目录，再用 `edithere_annotate_poll` 跟进 |
| MCP Apps | `edithere_web_open({projectPath:"<清单绝对路径>",outputPath:"<新的反馈绝对路径>"})`；恢复本轮只传 `sessionId` |
| Web Agent 兼容入口 | `node <web>/agent.cjs start --project <清单绝对路径> --output <新的反馈绝对路径>`，打开回执 URL |
| DSH | `node <dsh>/cli.mjs start --project <清单路径> --output <新的反馈路径> --open` |
| 离线 Web | `node "<skill-dir>/scripts/image-set.mjs" bundle <清单路径> --output <新的网页项目.json>`，交由用户导入并在导出后交回 |

- 桌面 `open` / `edithere_open` 仅用于浏览，不能代替带 **「完成并返回 AI」** 回传的 `annotate`。
- 只有用户明确点击完成并提交后才进入实施；未提交草稿、取消或超时均不作为修改依据。无批注时用户仍可提交空反馈表示本轮无修改意见。

## 4. 读取反馈并分层改码

- `pageIndex` 是整组上下文索引（仅含 ID、名称、尺寸与是否批注）；`pages[]` 只携带有批注或发生布局移动的页面；`groupNotes` 是不绑定单页的整组意见。
- 按 `pages[].id` 对回本轮截图与源码页面（**不能用组图页码直接当作 `pages[]` 下标**），逐页结合批注图编号、原图、文字、`movement` 与 `children` 理解意图。
- 修改真实组件、路由、样式或场景源码，禁止只修改截图或清单应付。动手前先按 `references/layout-translation.md` 区分 **「局部微调」** 与 **「设计系统级调整」**，确保既不误改公共组件，也不靠重复打补丁制造样式漂移。

## 5. 代码影响面驱动的精准换图与下一轮复审

改完代码并完成验证后，**以实际源码改动影响面决定换图范围**，不盲目对整组页面重截或做全量像素比对：

1. **列出「受影响页面清单」**：
   - **必换页**：本轮收到了批注（或移动反馈）且已实际修改源码的页面（例如用户批注了「物品栏」，改完就必重截并替换「物品栏」）。
   - **波及页**：本轮修改了共享组件、公共布局、主题/Design Token 或全局样式，且经代码引用链确认会产生可见变化的页面，一并列入重截清单。
   - **跳过页**：本次代码改动完全未触及的页面（以及仅重构内部非视觉逻辑且无视觉影响的页面），直接跳过重截，保留原截图、原 ID 与页序。
2. **定向重截与更新清单**：仅对清单中的「必换页 + 波及页」进入真实运行状态重截无损 PNG，并用原 `--id` 执行 `image-set.mjs upsert` 更新路径与 SHA-256；如有新增页面则追加，下线页面则 `remove`。
3. **AI 先自查再交回复审**：对照本轮每条意见与源码改动先视检更新后的截图（确认问题已落地且未破坏邻近元素），然后打开同一清单 + 新输出路径展示下一轮结果，明确汇报：已落实哪些意见、替换了哪些页面（以及因共享改动连带更新了哪些页）、还有哪些待核对或歧义项。
4. **收束条件**：若用户已授权连续多轮组图评审，AI 改完换图后直接展示下一轮组图，不重复询问是否继续打开；当用户明确认可、要求停止/暂停或提交空反馈时收束，取消或超时不自动重开新轮。

## 6. 与桌面「AI 解析」的关系及容量上限

- 默认由当前 Agent 直接阅读截图与反馈 JSON 落地修改；桌面「AI 解析（预览功能）」只是可选的独立文字整理入口，不是组图闭环的前置条件，技能也不复制程序内固定系统提示词以免漂移。
- 当前容量上限：**最多 64 页**、单图最大 **48 MiB**、组图总像素 **1.28 亿**；Web Agent / MCP 快照另受 **96 MiB** 会话消息上限约束。超限时按功能模块拆成多份清单并说明覆盖范围，不静默裁图降清晰度。macOS 与 Linux 实机效果需在对应平台核验，不以 Windows 运行替代跨平台结论。
