# 让 AI 帮你配置 EditHere

[返回产品介绍](../README.md) · [命令行说明](AGENT-CLI.md) · [配套 skill](../skills/edithere/SKILL.md)

**免安装版也可以配合 skill 使用，不必运行安装器或加入 PATH。** 程序负责截图和标注，skill 负责指导 AI 找到程序、等待你提交，再读取反馈；两者需要分别准备。

把下面这段话交给能在你电脑上执行命令、读写文件的 AI Agent：

```text
请按 https://github.com/Inginnng/EditHere/blob/codex/native/docs/AI-SETUP.md 帮我配置 EditHere 和当前 AI 工具的 edithere skill。先复用本机已有的程序；没有时再下载安装。不要打断我正在编辑的内容。完成后验证命令行与 skill，告诉我怎么开始使用。
```

希望免安装时，用这一段：

```text
请按 https://github.com/Inginnng/EditHere/blob/codex/native/docs/AI-SETUP.md 为我配置 EditHere 免安装版和当前 AI 工具的 edithere skill。先找已有的解压目录；没有时下载官方完整免安装版压缩包。不要运行安装器、修改 PATH 或设置开机启动。保存 CLI 的完整路径，验证后告诉我怎么使用。
```

网页聊天或远程云端 Agent 如果不能访问你的电脑，就只能提供指导，不能仅凭这段话完成本机安装。**仓库当前保持私有**：配置所需的仓库文件与 Releases 需要账号已获访问权限；公开后，匿名访问才可用。

以下步骤供执行配置的 Agent 使用。按用户当前语言沟通，只为当前工具配置，不默认给其他 AI 工具也安装一份。

## 1. 检查环境并复用现有程序

- 确认执行环境是用户的 Windows 或 macOS 本机，且具有本次操作所需的工具权限。Windows 发布包与 macOS 14+ 预览版可用；目前没有 Linux 发布包。
- 先检查用户本轮提供的程序目录，再检查已有 skill 中的 `references/local-installation.md`、当前会话 `EDITHERE_CLI`（若有）、PATH 与常见安装目录；不默认全盘扫描。
- Windows 默认安装目录为 `%LOCALAPPDATA%\Programs\EditHere`，CLI 为 `edithere-cli.exe`。免安装版的 CLI 位于完整解压目录内，与 `EditHere.exe` 及依赖文件共同保留。
- macOS 的 CLI 位于 `EditHere.app/Contents/MacOS/edithere-cli`，常见应用目录为 `/Applications` 或 `~/Applications`。
- 找到现有程序后先用完整路径执行 `--version` 和 `--help`，确认存在本任务所需命令。能使用时直接复用；旧版本确需升级时，先说明版本差异并安排在用户保存、正常退出后进行。不要结束 GUI 进程、覆盖正在运行的文件或清空未保存文档。

`EDITHERE_CLI` 是 skill 约定的可选定位方式，不是应用命令行开关，也不要求修改全局环境变量。免安装使用通过下文的本机路径记录即可跨会话定位。

## 2. 确定官方版本、下载并校验

仅在本机没有可用程序或确需升级时下载。

1. 从官方仓库 `Inginnng/EditHere` 的 [Releases](https://github.com/Inginnng/EditHere/releases) 元数据选择适合当前平台的发布版本，读取该版本说明、实际资产名称及 `SHA256SUMS.txt`，不要根据旧文档猜文件名或下载地址。
2. 优先使用本机已有的 GitHub CLI 登录状态访问私有仓库。例如 `gh release view --repo Inginnng/EditHere --json tagName,assets,body` 可读取最新发布信息，再按得到的标签和准确资产名下载。已有仓库权限也可由当前工具支持的 GitHub 连接提供。
3. 如果返回 401/404 或无权下载，核实仓库权限与登录状态；私有仓库的 404 不等于项目不存在。报告具体阻塞，不索要访问令牌、不猜镜像，也不把网页错误内容保存成安装包后执行。没有权限时，可继续复用本机已有完整程序；缺失的仓库文件需由已有访问权限的方式取得。
4. 下载程序和同一版本的 `SHA256SUMS.txt`，用 SHA-256 比对对应条目。Windows 可用 `Get-FileHash -Algorithm SHA256 -LiteralPath <文件路径>`；macOS 可用 `shasum -a 256 <文件路径>`。缺失条目或不一致时停止使用该文件并检查原因。

应用包使用 `SHA256SUMS.txt` 校验；`SHA256SUMS-A1.txt` 对应宣传视频和品牌素材，不能代替应用包校验。校验和用于核对文件完整性，不等于发布者数字签名。

当前 v0.8.21 Windows 安装器未签名；macOS 预览版尚未公证，也未完成实体 Mac 验收。下载新版本时以对应发布说明为准。若系统拦截，说明实际提示并保留用户决策，不关闭 Gatekeeper、SIP 或系统安全防护来完成配置。

## 3. 按用户选择准备程序

### Windows：常规安装

新安装使用官方当前用户安装器，不要求以管理员身份安装。用户未要求开机启动或加入 PATH 时，本次 AI 配置采用 `/STARTUP=0 /ADDPATH=0`，后续使用完整 CLI 路径。

在确认已完成上述下载与校验后，可通过 PowerShell 执行：

```powershell
# $InstallerPath 为已校验的安装器绝对路径。
$InstallProcess = Start-Process -FilePath $InstallerPath -ArgumentList '/S', '/STARTUP=0', '/ADDPATH=0' -WindowStyle Hidden -Wait -PassThru
if ($InstallProcess.ExitCode -ne 0) {
    throw "EditHere 安装失败，退出码：$($InstallProcess.ExitCode)"
}
$EditHereCli = Join-Path $env:LOCALAPPDATA 'Programs\EditHere\edithere-cli.exe'
```

`/S` 必须大写。沿用已有安装时不重新执行该示例、不改变用户的启动或 PATH 偏好；需要自定义目录时遵循该版本安装器实际支持的参数。

### Windows：免安装使用

下载完整 Windows 免安装版 ZIP，解压到用户有写入权限、准备长期保留的目录，例如用户选定的 `Tools\EditHere`。保留全部依赖，不只复制 `edithere-cli.exe`。记录解压后的实际路径，不运行安装器，不修改 PATH 或开机启动设置。

### macOS：应用包

下载匹配平台的官方 DMG，挂载后把完整 `EditHere.app` 复制到稳定目录，优先复用用户选定的应用目录。不要只提取 CLI，也不要从临时挂载卷长期运行。使用最终 `.app` 内的 CLI 完整路径；CLI 验证不需要主动发起截图或申请屏幕录制权限。DMG 中的 `skills/edithere` 位于挂载卷根目录，复制 `.app` 不会同时安装 skill，仍需执行下一步。

## 4. 安装或更新当前工具的 skill

应用包内的 skill 可能早于路径定位修复。优先读取官方仓库 `codex/native` 分支当前版本：先将它解析为一次明确的 commit，再从该 commit 获取**整个 `skills/edithere` 目录**，包含 `SKILL.md`、引用文件及其他配套文件。不要把不同 commit 的文件混装。已有可信本地仓库时，也可使用已核对的同一版本文件。

先识别当前工具实际发现的技能目录和现有 `edithere`，再选择位置：

| 当前工具 | 配置位置 |
| --- | --- |
| Codex | 新安装按当前官方约定使用 `~/.agents/skills/edithere`。如果当前工具已经从 `~/.codex/skills/edithere` 或自定义 `CODEX_HOME/skills/edithere` 加载本技能，在原位置比较并更新，避免重名安装。 |
| Claude Code | 默认 `~/.claude/skills/edithere`；尊重该工具实际配置的自定义配置根目录。 |
| 其他 Agent | 仅使用该工具明确支持的 skill 目录或配置入口；若不支持技能自动发现，说明可参照此指南和命令行文档手动调用，不宣称已安装 skill。 |

参考：[Codex 技能说明](https://learn.chatgpt.com/docs/build-skills) · [Claude Code 技能说明](https://code.claude.com/docs/en/skills)。

如果目标目录已有文件，先比较并保留用户的本机修改；不盲目递归覆盖，不建立第二个同名 skill。完整技能目录是可读取的操作说明，安装前应检查其内容，执行仍须遵守当前工具与用户的权限边界。

在**已安装的 skill 目录**中生成或更新 `references/local-installation.md`，使下次会话能找到免安装版或自定义目录中的程序。以下值必须换成实际检查结果：

```markdown
# 本机 EditHere 配置

- CLI 完整路径：C:\Users\example\Tools\EditHere\edithere-cli.exe
- 程序目录：C:\Users\example\Tools\EditHere
- CLI 版本：实际 --version 输出
- 使用方式：免安装版 / 当前用户安装 / macOS 应用包
- skill 来源：Inginnng/EditHere，实际 commit
```

macOS 同样记录实际完整路径。该文件只记录本机路径与版本，不写入令牌等凭据，**不提交回项目仓库**；更新 skill 时保留并复核它。

## 5. 验证与交付

通过完整路径分别执行 `--version` 和 `--help`，检查退出码及输出。Windows 示例：

```powershell
& $EditHereCli --version
if ($LASTEXITCODE -ne 0) { throw 'EditHere CLI 版本检查失败。' }
& $EditHereCli --help
if ($LASTEXITCODE -ne 0) { throw 'EditHere CLI 帮助检查失败。' }
```

有必要进一步验证时，可在独立临时目录用测试图片进行离线 `export`，输出使用不存在的新路径。安装验收不自动调用 `annotate`、`capture` 或 `open`，不打开或替换用户正在编辑的文档。桌面调用权限与错误处理见[命令行说明](AGENT-CLI.md#windows-agent-的桌面访问)。

核查当前工具是否已发现 `edithere`。如果它必须重载或开启新会话才会发现，明确说“文件已配置，尚需重新加载”，不要仅因复制成功就声称技能已加载。CLI 可执行、技能文件已配置、当前会话可触发，是三个需分别确认的状态。

完成后向用户说明：程序完整路径、版本、安装或免安装方式、skill 路径及其加载状态，以及仍存在的真实阻塞。提示用户在后续需要标注时输入：

> 用 EditHere 让我标注这张界面，完成后按反馈修改。

只有用户随后请求标注时，才按 skill 发起对应会话并等待用户点击“完成并返回 AI”。
