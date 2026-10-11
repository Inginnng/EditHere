# 让 AI 安装 EditHere 网页版

**简体中文** · [English](AI-SETUP.en.md)

这是首次安装指引，不要求先装好 Skill。需要能访问本机文件、运行终端和打开本机网页的 Agent。Windows、macOS、Linux 使用同一份 Web Agent 包；普通网页聊天能否安装，取决于其宿主是否提供这些能力。

## 给 AI 的提示词

> 请按这份指南帮我安装 EditHere 网页 Agent 版。我会提供 web-agent ZIP 的下载地址或本地文件，以及对应的 SHA-256 校验和。先检查已有运行时和技能，使用 Node.js 18+，完整解压到长期保留的可写用户目录，安装 edithere-web Skill 并验证页面服务。完成后告诉我如何用 EditHere 批注当前页面。已有技能或其他服务需要处理时保留它们，说明具体冲突。

## 取得正确的包

选择文件名含 **`-web-agent-`** 的 ZIP，里面应有 `web/install-skill.cjs`。只有 `EditHere.html` 的 `-web-` 包用于直接浏览器编辑，不包含 Agent 会话服务。

当前整理首先生成本地文件；**生成 ZIP 不等于已有公开下载链接**。下载时使用作者实际提供或正式发布页实际列出的地址，不能假设桌面版的 `releases/latest` 已包含 Web 包。本地收到 ZIP 时可以直接进入以下流程。分发方需同时提供相应 `SHA256SUMS.txt` 或明确的包哈希。

## 安装步骤

1. 检查 `node --version`，需要 18 或更新版本；可使用宿主已提供的 Node.js 绝对路径。Web Agent 包不含 Node.js，不需要 Qt、Python、npm 依赖或桌面 EditHere。缺少 Node.js 时说明缺失依赖，按用户环境配置。
2. 核对 ZIP 的 SHA-256，再完整解压到新的、当前用户可写且长期保留的目录。可用 Windows PowerShell `Get-FileHash -Algorithm SHA256`、macOS `shasum -a 256` 或 Linux `sha256sum`；使用平台的 ZIP 解压工具保留目录结构。不要在临时下载缓存中安装后删掉运行时。
3. 从解压目录运行：

   ```text
   node web/install-skill.cjs
   ```

   默认目的地为 `$CODEX_HOME/skills/edithere-web`，未设置时为 `~/.codex/skills/edithere-web`。其他宿主先定位其实际技能目录，再指定绝对路径：

   ```text
   node web/install-skill.cjs --destination <宿主技能目录内edithere-web的绝对路径>
   ```

   安装成功返回 JSON，其中 `state` 为 `installed`，并给出技能目录、CLI 和 Node.js 路径。安装器会核对运行文件，并把本机入口写入技能 `references/local-installation.md`。目的地已存在就拒绝；先读取原技能和本机引用，复用仍有效的入口，或按用户选定的新目录安装。不要删除已有技能来绕过冲突。

4. 从同一解压目录启动并检查服务：

   ```text
   node web/preview.cjs --json
   ```

   成功结果应包含 `service:"edithere-web-preview"`、`agentBridge:true`、`apiVersion:1`，以及指向本次解压目录的 HTML 路径、PID 和 SHA-256。打开返回的 `url`。7330 端口若属于另一份安装或其他服务，先报告其 PID/路径冲突，不自动终止它。此检查证明页面服务可访问，不能代替用户提交批注的验证。
5. 确认技能的 `SKILL.md` 与三个 `references` 文件已安装，本机引用指向保留的运行时。按宿主提供的方式重新载入技能；若当前对话尚不能识别新技能，可先直接读取已安装的 `SKILL.md` 执行本轮。告诉用户安装位置、运行时位置与实际验证结果。

## 首次使用

用户可以说“帮我用 EditHere 批注游戏主页”，或指定 `edithere-web`。Agent 按已安装的 Skill 截取本轮实际画面，准备唯一图片与尚不存在的反馈 JSON 路径：

```text
node web/agent.cjs start --image <图片绝对路径> --output <新的反馈JSON绝对路径> --timeout 86400
node web/agent.cjs poll --session <start返回的sessionId>
```

打开 `start` 返回的会话 URL，才能自动传入图片；普通预览地址不对应本次会话。用户点击顶部“反馈”后，Agent 读取同一个 sessionId，仅在 `completed` 且反馈有效时继续实施修改。截图来自宿主的实际截图能力，右侧打开方式由宿主决定。

点击反馈会把结果交给本机会话服务。能否在 Agent 已结束本轮后自动继续，取决于宿主的调度能力及用户授权；注册成功才承诺定时跟进。没有调度时，告诉用户提交后回到对话说“继续”。`wait` 单次最多 55 秒，仅在用户要求当场等待时使用。

会话图片、令牌与日志在运行时写入 `web/tests/output/`，不应随运行过的目录再次分发。移动运行时后需要重新定位技能入口。详细交互能力和限制见仓库的 `web/README.md`；安装后的批注流程见 Skill。
