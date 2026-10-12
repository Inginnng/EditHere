# 让 AI 安装 EditHere 网页版

**简体中文** · [English](AI-SETUP.en.md)

Web Agent 包包含真实 MCP Apps 入口和普通浏览器兼容入口。MCP 使用宿主管理的 stdio 与内嵌 HTML，不监听 localhost；只安装 Skill 不会建立 MCP 连接。普通离线 `EditHere.html` 无需服务器。

## 给 AI 的提示词

> 请按这份指南安装 EditHere Web Agent。我会提供 web-agent ZIP 地址或本地文件及 SHA-256。先检查已有安装，完整解压到保留的用户目录。优先配置当前宿主的 EditHere Web MCP，验证工具发现和内嵌界面；安装 edithere-web Skill。宿主不支持 MCP Apps 时说明实际限制，使用普通浏览器兼容入口。保留现有配置，不覆盖其他 MCP 或技能。告诉我如何批注当前页面。

## 取得并检查包

选择文件名含 **`-web-agent-`** 的 ZIP，应包含 `web/mcp/server.bundle.cjs`、`web/EditHere.mcp.html`、`web/install-skill.cjs` 和第三方许可。只有 `EditHere.html` 的 `-web-` 包用于直接编辑。

使用作者实际提供的地址或本地包，不推测 Release 资产地址。核对作者提供的 SHA-256，再完整解压到新的、当前用户可写且长期保留的目录。Windows 可用 `Get-FileHash -Algorithm SHA256`，macOS 用 `shasum -a 256`，Linux 用 `sha256sum`。不要在安装后删除解压运行时。

MCP 需要 **Node.js 20+**；普通浏览器兼容 CLI 支持 Node.js 18+。下载包内的服务和 App SDK 已打包，无需 npm install、Qt、Python 或桌面 EditHere。截图依赖 Agent 宿主的实际浏览器或窗口工具。

## 连接 MCP 与安装 Skill

1. 检查 `node --version` 和当前宿主的 MCP 配置、已有 EditHere 工具。使用实际可执行的 Node.js 绝对路径，不覆盖已有效的安装。
2. 从解压目录安装 Skill：

   ```text
   node web/install-skill.cjs
   ```

   默认目录为 `$CODEX_HOME/skills/edithere-web`，未设置时为 `~/.codex/skills/edithere-web`。其他宿主先定位其技能目录，再使用：

   ```text
   node web/install-skill.cjs --destination <宿主技能目录内edithere-web的绝对路径>
   ```

   安装器返回 `state:"installed"` 和本机入口，在 `references/local-installation.md` 写入绝对路径及 MCP 配置。它不改宿主配置；目的地已存在就拒绝，先检查并复用原安装或使用用户指定的新目录，不删除原技能绕过冲突。
3. 读取 `web/mcp/mcp.json` 模板，或安装回执的 `mcpConfig`。按当前宿主文档将 stdio 条目合并到现有配置：`command` 是 Node.js 绝对路径，唯一 `args` 是本次解压目录 `web/mcp/server.bundle.cjs` 的绝对路径。保留其他 MCP 条目；无需 HTTP URL。宿主可能使用 TOML 或独立设置界面，应转换为它实际支持的格式。
4. 按宿主方式重新载入 MCP 与 Skill。确认真实工具列表中出现 `edithere_web_open` 和 `edithere_web_read`，而不是仅根据文件存在就宣称连接成功。
5. 用一张实际测试图片调用 `edithere_web_open`，检查 `uiSupport/state` 并核对图片已出现在该宿主的内嵌界面。MCP 工具可用不代表宿主支持 MCP Apps；未声明 UI 能力的宿主会收到 `unsupported` 与兼容命令，不创建空会话。**当前 WorkBuddy 版本是否支持须实测**；CodeBuddy Code Web UI / IDE 的支持说明不能代替 WorkBuddy 验证。

## 使用与验收

用户说“帮我用 EditHere 批注游戏主页”后，Agent 截取并检查本轮实际画面，然后调用已列出的工具：

```json
{"imagePath":"<本次图片绝对路径>","name":"游戏主页"}
```

这是 `edithere_web_open` 的参数；可另传尚不存在的绝对 `outputPath`，未指定时程序在原图目录的 `.edithere` 子目录生成唯一反馈文件。保存返回的 sessionId 与输出路径。宿主通过 MCP UI 资源显示界面，无需 7330 页面服务。

“反馈”先保存完整桌面兼容 JSON，再请求宿主发送对话消息。页面分别报告保存和消息状态；宿主拒绝、仅填入输入框或没有消息能力时，用户回到对话说“继续”。Agent 对原 sessionId 调用 `edithere_web_read`，核对 `completed/sha256` 后处理；读取回执省略图片 base64，完整图片仍在输出 JSON。宿主接受消息不等于 AI 已经实施修改。

MCP 会话和草稿默认保存在 `~/.edithere/web-sessions`，可在 MCP 配置中设置 `EDITHERE_WEB_DATA_DIR` 为持久绝对目录。停止宿主不会抹掉已经保存的数据；重新连接后用 `edithere_web_open({"sessionId":"<原ID>"})` 重开，或 read 已提交结果。取消与停止须尊重，不能自行恢复已被用户停止的任务。

验收至少包含：实际截图传入、批注及位移提交、磁盘输出与 read 一致、消息失败时仍可读取、MCP 重启后重开原会话。只能准确报告已经验证的宿主；不能将参考宿主测试称为 WorkBuddy 实测。

## 普通浏览器兼容入口

宿主不支持 MCP Apps 时，仍可使用：

```text
node web/agent.cjs start --image <实际图片绝对路径> --output <新反馈JSON绝对路径> --timeout 86400
node web/agent.cjs poll --session <start返回的sessionId>
```

打开 start 回执的会话 URL。普通预览可用 `node web/preview.cjs --json` 验证 7330 服务；端口属于其他进程时核实 PID 与路径，不自动结束其他服务。此模式默认提交后手动“继续”，没有直接宿主消息桥。终端后台服务可能随 Agent 停止而终止，不能承诺它独立常驻。已完成的 poll 可读取持久结果，不因服务结束而丢失。

兼容日志和旧版数据可能位于 `web/tests/output/`；MCP/当前会话数据目录及输出均含私有图片，勿打包分发。技能的 [跟进参考](skills/edithere-web/references/follow-up.md) 说明本次明确选择定时跟进时的约束。
