# EditHere MCP 连接器

把本地 `edithere-cli` 的 Agent 接口封装为 [Model Context Protocol](https://modelcontextprotocol.io) (stdio) 服务，让 WorkBuddy、Claude Code、Codex 等支持 MCP 的 AI 工具能直接发起 EditHere 标注协作。

零依赖，仅需 Node.js ≥ 18（或 WorkBuddy 托管的 Node 运行时）。

本目录同时是 WorkBuddy 开放平台的**连接器提交包**，结构遵循 [连接器规范](https://open.workbuddy.cn/docs/connector)。

## 目录结构

```
connector/
├── connector-meta.json      # WorkBuddy 连接器元信息（上架用）
├── mcp.json                 # WorkBuddy 连接器 MCP 配置（上架用）
├── icon.svg                 # 市场图标（上架用）
├── skills/edithere/SKILL.md # 指导 AI 使用本连接器（上架用）
├── mcp-server.mjs           # MCP 服务实现
├── package.json             # npm 发布用
├── LICENSE / NOTICE         # 许可与声明
└── README.md
```

`connector-meta.json`、`mcp.json`、`icon.svg`、`skills/` 四项只在上架 WorkBuddy 时被读取；本地直接运行 MCP 服务只需要 `mcp-server.mjs`。

## 提供的工具

| 工具 | 对应 CLI 命令 | 说明 |
| --- | --- | --- |
| `edithere_status` | `status` | 查询 EditHere 运行与文档状态，不启动 GUI。响应中附带连接器版本与实际使用的 CLI 路径 |
| `edithere_open` | `open <path>` | 打开图片、视频或 `.edithere` 项目（含视频项目，仅请求受理） |
| `edithere_capture` | `capture` | 唤起截图（仅请求受理） |
| `edithere_annotate_start` | `annotate` | 发起标注会话并**立即返回会话 ID**，不阻塞 |
| `edithere_annotate_poll` | — | 查询会话；用户提交后返回反馈摘要 |
| `edithere_annotate` | `annotate` | 发起标注会话并**阻塞等待**用户点击"完成并返回 AI" |
| `edithere_export` | `export <path> --output <json>` | 离线把项目（含视频项目）/图片/反馈 JSON 转为反馈 JSON，不等待用户 |

### 为什么有两种发起方式

`edithere_annotate_start` + `edithere_annotate_poll` 是**推荐路径**：两次调用都在毫秒级返回，满足 WorkBuddy 连接器规范"单次请求建议 30 秒内响应"。标注本身依赖用户手动点击完成，天然是长任务；`edithere_annotate` 保留给支持长工具调用的客户端（Claude Code、Codex 等），语义与 0.1.x 完全一致，未做破坏性改动。

`edithere_annotate` / `edithere_export` 成功后会读取输出 JSON，返回紧凑摘要（批注列表、布局变化、坐标语义提示），反馈原文（含原图 data URL）保存在临时目录 `edithere-feedback/`，可用文件工具按路径读取。传 `includeImage: true` 可在工具结果中附带原图内容块。

### 视频标注反馈

`edithere_open`、`edithere_annotate_start` 和 `edithere_annotate` 接受视频路径及保存的视频项目。为兼容已有客户端，路径参数仍叫 `imagePath`。用户在 EditHere 中播放或拖动时间轴，点击“暂停”后查看当前画面；首次实际点、框或全局批注才保存截图并进入批注画布。选择模式、缩放、平移、保存项目或导出 JSON 不会为仅查看的画面新建截图；可以继续定位到其他时间点，最后一次提交全部已标注画面。

视频导出使用 `schemaVersion: "video-feedback-1"`。`video.source` 引用源视频地址；`frames[n]` 保存帧 ID、相对视频起点的 `timestampMs` / `timestampUs` 和一份现有图片反馈结构 `feedback`。`frames[n].feedback.image` 是该帧的 Base64 截图，`frames[n].imageFile` 如存在则指向 JSON 同目录的外部截图。根 `objects` 是各帧对象的展开索引，每项附 `frameId`、`timestampMs`；这些对象已经出现在对应帧中，AI 应按一份要求执行，避免重复。

连接器以 `frames` 为批注来源，按帧输出时间戳、截图入口、矩形位置及修改意见。相同矩形在不同时间点的批注独立保留。`includeImage: true` 时每张图像内容块前都有对应的帧 ID 和时间戳。默认最多附 8 张视频截图，Base64 总量最多 24 MiB 字符；超出的帧会明确列出 `frames[n].feedback.image` / `frames[n].imageFile` 提取索引，截图仍保留在完整 JSON 或同目录文件中。`includeImage` 只控制工具回复是否附图，不改变磁盘上的 JSON。

传 `noImage: true` 时 JSON 不内嵌截图，但仍保留时间戳、批注和外部截图索引（如有）。如同目录截图文件可用，连接器可以据此返回图片内容；否则会明确提示缺少视觉证据。连接器只读取 JSON 同目录内的 PNG/JPEG 文件名，不跟随通向其他目录的符号链接。源视频地址失效不影响读取已保存的批注截图。

对于大量标注，摘要最多展开 128 帧、256 个对象、100000 字符，并明确提示未展开的内容及完整帧索引范围；AI 必须继续按索引读取反馈文件。连接器读取文件上限为 512 MiB。帧关联、时间戳或展开索引不一致时返回解析错误，避免 AI 把意见套用到错误时间点。

完整交互与 JSON 示例见 [视频标注说明](../docs/VIDEO-ANNOTATION.md)。

## 前置条件：本机需已安装 EditHere

连接器只是适配层，**不包含 EditHere 程序本体**。程序缺失时所有需要桌面的工具都会返回 `edithere-cli 不可用`，并附带安装指引。安装入口：<https://github.com/Inginnng/EditHere/releases/latest>。

## CLI 定位顺序

1. 环境变量 `EDITHERE_CLI`（绝对路径）
2. `PATH` 中的 `edithere-cli(.exe)`
3. Windows：`%LOCALAPPDATA%\Programs\EditHere\edithere-cli.exe`；macOS：`/Applications/EditHere.app/Contents/MacOS/edithere-cli`

`EDITHERE_CLI` 指向的路径不存在或不可执行时，连接器**直接报错**并给出该路径，不再悄然回退到其他位置——避免在用户已明确指定程序时误用另一份 CLI。实际使用的 CLI 路径与来源会出现在 `edithere_status` 响应、启动时的 stderr 以及每条失败信息里。

## 使用方式

### 在 WorkBuddy 中（开发者本地）

在 `~/.workbuddy/mcp.json` 中加入：

```json
{
  "mcpServers": {
    "edithere": {
      "command": "node",
      "args": ["<仓库路径>/connector/mcp-server.mjs"]
    }
  }
}
```

然后在 WorkBuddy 的连接器管理页，通过自定义连接器入口"信任"并启用该 server，重新打开会话即可使用。免安装版或非默认位置可在配置中追加 `"env": {"EDITHERE_CLI": "D:\\Tools\\EditHere\\edithere-cli.exe"}`。

### 经 npm 安装

```bash
npx -y edithere-mcp
```

这也是 `mcp.json` 里给 WorkBuddy 市场的配置方式（`npx` 加 `-y` 以避免非交互环境下卡在确认提示）。

### 其他 MCP 客户端

命令行直接运行即可：

```bash
node connector/mcp-server.mjs
```

协议：JSON-RPC 2.0，newline-delimited，protocolVersion `2025-06-18`，capabilities 仅 `tools`。

## 上架 WorkBuddy 连接器市场

[连接器规范](https://open.workbuddy.cn/docs/connector) 要求提交一个目录并打包后交审核，审核通过后进入连接器市场，更新通常在 10～15 分钟内同步。本目录已按规范组织，提交前需确认：

- [ ] `edithere-mcp@0.2.0` 已发布到 npm（或修改 `mcp.json` 的 `args`）。**市场只读取 `mcp.json`，不会随包分发服务端代码**，所以 stdio 方案必须先把包发到 npm
- [ ] `connector-meta.json` 的 `source` 全局唯一（当前为 `edithere`）
- [ ] `icon.svg` 在 64×64 小尺寸下可辨识
- [ ] `mcp.json` 只配置一个 MCP Server，`npx` 参数带 `-y`
- [ ] 凭证未硬编码（本连接器无鉴权，`auth_mode` 留空）
- [ ] 异常路径已覆盖：CLI 缺失、桌面不可达（退出码 8）、会话忙（退出码 4）、用户取消（6）、超时（7）

规范中"单次请求建议 30 秒内响应"由 `edithere_annotate_start` / `edithere_annotate_poll` 满足；阻塞式的 `edithere_annotate` 在说明中已标注为长任务。

## 注意事项

- `status` / `open` / `capture` / `annotate*` 需要连接当前登录用户的桌面；受限沙箱中的 Agent 可能返回 `desktop_access_required`（退出码 8）。离线 `export`、`--help` 不依赖桌面。
- 每次标注/导出都会生成**尚不存在**的唯一输出路径，不覆盖已有文件；取消或超时不产生反馈。
- 同一时刻只支持一个标注会话：会话进行中再次发起会返回 `busy`（退出码 4）。等待期间可以正常调用 `status` 等其他工具。
- 非阻塞会话只存在于连接器进程内；连接器重启后旧 `sessionId` 失效并会明确报错，需重新发起。
- 必填参数缺失（如未传 `imagePath`）会在调用 CLI 前拦下并说明原因，不把参数问题透成文件或 I/O 错误。
- 图片反馈遵循 `schema/feedback-minimal.schema.json`，视频反馈采用 `video-feedback-1` 并在每帧内复用现有 `objects` 结构（每项含 `source`、`movements`、`annotations`）。早期图片版本的 `annotations` / `changes` 并行数组仍可导入。坐标为对应截图的图像像素，原点在左上角，不能直接当作屏幕坐标或 CSS 像素。

## 许可

本连接器与 EditHere 本体一同采用 **MIT License**（见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)）：在保留版权声明与许可文本的前提下，可免费使用、修改、分发、再许可和出售，包括用于商业目的。EditHere 本体同样以 MIT 发布。EditHere 的名称与标识不随 MIT 许可授予。

## 版本

- 0.2.0：新增 `edithere_annotate_start` / `edithere_annotate_poll` 非阻塞会话；CLI 缺失时附安装指引；补齐 WorkBuddy 连接器提交包（`connector-meta.json` / `mcp.json` / `icon.svg` / `skills`）。
- 0.1.1：`EDITHERE_CLI` 失效时明确报错而非静默回退；必填参数前置校验；`edithere_status` 与失败信息回显实际使用的 CLI 路径与来源；`edithere_annotate` 说明并发限制。
- 0.1.0：首次提供 `status` / `open` / `capture` / `annotate` / `export` 五个工具。

连接器版本独立于 EditHere 程序版本（当前程序 0.10.0，本地测试版，未公开发布）；两者的版本号会同时出现在 `edithere_status` 响应中。
