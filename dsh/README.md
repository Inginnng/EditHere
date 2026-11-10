# EditHere DSH · 组图批注

面向 DeepSeek Harness（DSH）的可视化页面批注入口。用户把游戏、桌面程序或网页的多个界面截图放进一个本机组图，在 EditHere 页面上直接圈画、写意见；完成提交后，只把**有批注的页面**整理成一份 JSON 和对应的批注图，交给 DSH 中的 AI 继续修改。

它复用 EditHere 网页编辑器的标注、组件拆分、批注编号和 JSON 规则，新增：

- 多页面组图：首页、设置页、HUD 等各自批注，页签切换、重命名、排序、删除；
- 整组意见：不绑定某一页的整体要求；
- 只发送已批注页面：未批注页面只在 `pageIndex` 里留名称和尺寸；
- DSH 回传：本机 loopback 服务 + CLI + 技能，用户点「完成并返回 AI」后 AI 读取 JSON 与批注图；
- 截图入口：拖入/粘贴/多选图片、浏览器内截屏、收件箱目录自动加入、CLI `add`。

## 和现有入口的关系

| 入口 | 场景 | 本轮改动 |
| --- | --- | --- |
| 桌面 EditHere | 系统截图、长截图、OCR、贴图、项目组图 | 支持同一项目清单 |
| `web/EditHere.html` | 离线单图/视频/组图网页编辑器 | 新增组图能力 |
| `web/EditHere.mcp.html` | MCP Apps 宿主内嵌单图与组图 | 支持 projectPath 与草稿恢复 |
| `dsh/` | DSH 本机组图会话 + 技能 | 新增 |

DSH 的 MCP 客户端只桥接工具，不消费 MCP Apps 的 UI 资源，因此这里不复用 MCP Apps，而是提供本机页面 + CLI + 技能。

项目由 AI 准备与维护时，可用 `start --project <组图清单.json>`。更新页面沿用原 ID，下一轮读取更新后的快照，见 [项目组图与结果审批](../docs/PROJECT-REVIEW.md)。

## 快速开始

先构建网页产物（需要 Node.js 20+）：

```powershell
node web/build.cjs
```

发起一个组图会话（示例使用两张截图）：

```powershell
node dsh/cli.mjs start --name "桌面程序页面" `
  --image "C:\shots\home.png" `
  --image "C:\shots\settings.png" `
  --output "C:\project\.edithere\feedback.json" `
  --open
```

命令输出 `sessionId`、`url`、`inbox`、`output`。在浏览器里完成批注后点「完成并返回 AI」，或在 DSH 中让 AI 等待：

```powershell
node dsh/cli.mjs wait --session "<sessionId>" --wait 50
```

返回 `state: "completed"` 后，反馈目录形如：

```text
feedback.json
images/
  page-1-home.png
  page-1-home-annotated.png
  page-2-settings.png
  page-2-settings-annotated.png
```

`feedback.json` 不内嵌 base64，只引用这些图片；`pages[]` 只包含已批注页面，`pageIndex` 记录全部页面。详细字段见 [`skill/edithere-dsh/references/feedback-format.md`](skill/edithere-dsh/references/feedback-format.md)。

## 安装 DSH 技能

```powershell
node dsh/cli.mjs install-skill
```

默认安装到 `%USERPROFILE%\.dsh\skills\edithere-dsh`，并写入 `LOCAL.md` 记录 CLI 绝对路径。只安装到当前项目：

```powershell
node dsh/cli.mjs install-skill --project
```

安装后，在 DSH 对话里说：

> 用 EditHere 让我标注首页和设置页，等我完成后按我的意见修改。

技能会启动会话、把 URL 给用户、等待提交、读取批注图并按页修改。

## 截图怎么进来

1. **AI 传入**：`start --image ...` 或 `add --session ... --image ...`，路径必须是本机绝对路径。
2. **浏览器拖入/粘贴/多选**：一次选择多张图片会自动组成组图；已有组图时继续加页。
3. **页面内截屏**：组图工具条「截屏」调用浏览器 `getDisplayMedia`，选择窗口/屏幕后加入组图。
4. **收件箱**：把任意截图工具保存的 PNG/JPEG/WebP/GIF/BMP 放进会话的 `inbox` 目录，服务端约 1 秒内自动加入；导入后移到 `inbox/imported/`。
5. **CLI 截屏（Windows）**：`node dsh/cli.mjs capture --output "C:\shots\home.png"`，支持 `--all` 多屏和 `--window "标题"`。

## 组图操作

- 底部页面条切换页面；`[` / `]` 上一页/下一页；页面缩略图右上角是批注数。
- 「加页」「截屏」在组图工具条；「管理」可重命名、上移、下移、删除。
- 侧栏标题旁的「整组意见」图标添加不绑定页面的意见；每条显示为「整 1」。
- 导出时只打包有批注或真实位移的页面；整组意见单独放在 `groupNotes`。
- 页面尺寸可以不同；坐标始终使用各页原图的像素坐标。

## DSH 原生客户端插件（分支）

除了本目录的「本机页面 + skill」入口，DSH 仓库里另有一个原生客户端插件分支：

- DSH 仓库分支：`feat/edithere-native-client`
- 提交：`32441b6 feat(client): add native plugin installer and e2e test`（前序 `7bc6350`、`a77baa0`）
- 包：`packages/client/ui-edithere`

它在 DSH 输入框工具行增加「批注」按钮，点击后在 DSH 页面内打开全屏 EditHere；用户完成批注后，插件自动把反馈路径和「只改 pages[] 中已批注页面」的提示写入当前会话并尝试发送。宿主侧在进程内启动 EditHere loopback 服务，不依赖 MCP Apps、不重打 `apps/web`。

安装后包会复制到 `%USERPROFILE%\.dsh\profiles\node_modules\@deepseek-ai\dsh-client-ui-edithere`，并在目标 profile 的 `cordis.patch.yml` 里加入 `edithere-native` 配置。**重启 DSH 后生效**。安装/更新在 DSH 仓库的 `packages/client/ui-edithere` 目录执行：

```powershell
node install.mjs --server "<EditHere 仓库>/dsh/server.mjs" --profile desktop --build
```

如果不需要，删除 `cordis.patch.yml` 里的 `edithere-native` insert 条目即可。

验证时建议使用独立 profile（`%DSH_HOME%\profiles\edithere-test`），已在真实 DSH 前端中确认按钮、iframe、单图/组图提交和自动回填消息均正常。该分支的 README 说明了配置、构建和边界。

## 本机协议

除健康检查外，全部接口都在 `/__edithere_dsh` 下：

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| GET | `/session?token=` | 页面读取会话、页面数据、草稿和整组意见 |
| GET | `/events?token=` | SSE：新截图、完成/取消状态 |
| POST | `/draft?token=` | 保存组图草稿（同源） |
| POST | `/feedback?token=` | 提交反馈（同源，独占写输出） |
| POST | `/cancel?token=` | 取消会话（同源） |
| POST | `/sessions` | Agent 创建会话（Bearer key） |
| GET | `/sessions/:id` | Agent 查询回执（Bearer key） |
| POST | `/sessions/:id/pages` | Agent 追加截图（Bearer key） |
| POST | `/sessions/:id/cancel` | Agent 取消（Bearer key） |

Agent 接口的 key 在 `~/.edithere/dsh/server-key`；浏览器页面只拿到一次性会话 token。

## 安全边界

- 服务只监听 `127.0.0.1`，校验 Host、Origin 和 `Sec-Fetch-Site`；没有 CORS。
- 浏览器页面用一次性 256 位 token；Agent/CLI 接口需要 `~/.edithere/dsh/server-key` 的本机 Bearer key。
- 页面 CSP 从 `connect-src 'none'` 改为 `'self'`，只允许同源回传。
- 截图、批注、导出都在本机完成；不上传、不调用模型。
- 输出文件使用独占写入，已存在的路径不会被覆盖。

## 数据目录

默认 `%USERPROFILE%\.edithere\dsh`，可用 `EDITHERE_DSH_DATA_DIR` 覆盖；端口默认 `4782`，可用 `EDITHERE_DSH_PORT` 或 `--port` 覆盖。

```text
~/.edithere/dsh/
  server-key
  sessions/<id>.json
  sessions/<id>/pages/*.png
  sessions/<id>/inbox/
  logs/
```

## 验证

```powershell
node web/build.cjs
node dsh/tests/server.test.mjs
node dsh/cli.mjs --help
```

浏览器端到端脚本需要本机可用的 Playwright/Chromium，见 `web/tests/`。当前环境若无法启动浏览器，可用 Node 测试覆盖服务端与反馈包结构。

## 已知边界

- 组图草稿会在编辑时自动保存到本机会话目录，刷新后恢复；草稿保存失败时仍可用「保存项目」手动保留。
- DSH 反馈 JSON 为只读交付格式，移除了 base64 图片，不能直接重新导入编辑器。
- `capture.ps1` 仅 Windows；其他平台请用系统截图工具保存文件后 `--image` 或放入收件箱。
- 组图上限 64 页；单页图片上限 48 MiB；建议先缩放到接近最终显示尺寸。
- 视频仍使用现有视频批注流程，DSH 组图当前面向静态页面。
