# EditHere MCP 连接器

把本地 `edithere-cli` 的 Agent 接口封装为 [Model Context Protocol](https://modelcontextprotocol.io) (stdio) 服务，让 WorkBuddy、Claude Code、Codex 等支持 MCP 的 AI 工具能直接发起 EditHere 标注协作。

零依赖，仅需 Node.js ≥ 18（或 Bundled Node 运行时）。

## 提供的工具

| 工具 | 对应 CLI 命令 | 说明 |
| --- | --- | --- |
| `edithere_status` | `status` | 查询 EditHere 运行与文档状态，不启动 GUI。响应中附带连接器版本与实际使用的 CLI 路径 |
| `edithere_open` | `open <path>` | 打开图片或 `.edithere` 项目（仅请求受理） |
| `edithere_capture` | `capture` | 唤起截图（仅请求受理） |
| `edithere_annotate` | `annotate <path> --output <json> --timeout <s>` | 打开标注会话并**阻塞等待**用户点击"完成并返回 AI"，返回反馈摘要 |
| `edithere_export` | `export <path> --output <json>` | 离线把项目/图片/反馈 JSON 转为反馈 JSON，不等待用户 |

`edithere_annotate` / `edithere_export` 成功后会读取输出 JSON，返回紧凑摘要（批注列表、布局变化、坐标语义提示），反馈原文（含原图 data URL）保存在临时目录 `edithere-feedback/`，可用文件工具按路径读取。传 `includeImage: true` 可在工具结果中附带原图内容块。

## CLI 定位顺序

1. 环境变量 `EDITHERE_CLI`（绝对路径）
2. `PATH` 中的 `edithere-cli(.exe)`
3. Windows：`%LOCALAPPDATA%\Programs\EditHere\edithere-cli.exe`；macOS：`/Applications/EditHere.app/Contents/MacOS/edithere-cli`

`EDITHERE_CLI` 指向的路径不存在或不可执行时，连接器**直接报错**并给出该路径，不再悄然回退到其他位置——避免在用户已明确指定程序时误用另一份 CLI。实际使用的 CLI 路径与来源会出现在 `edithere_status` 响应、启动时的 stderr 以及每条失败信息里。

## 在 WorkBuddy 中启用

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

## 其他 MCP 客户端

命令行直接运行即可：

```bash
node connector/mcp-server.mjs
```

协议：JSON-RPC 2.0，newline-delimited，protocolVersion `2025-06-18`，capabilities 仅 `tools`。

## 注意事项

- `status` / `open` / `capture` / `annotate` 需要连接当前登录用户的桌面；受限沙箱中的 Agent 可能返回 `desktop_access_required`（退出码 8）。离线 `export`、`--help` 不依赖桌面。
- 每次标注/导出都会生成**尚不存在**的唯一输出路径，不覆盖已有文件；取消或超时不产生反馈。
- 同一时刻只支持一个标注会话：会话进行中再次调用 `edithere_annotate` 会返回 `busy`（退出码 4）。等待期间可以正常调用 `status` 等其他工具。
- 必填参数缺失（如未传 `imagePath`）会在调用 CLI 前拦下并说明原因，不把参数问题透成文件或 I/O 错误。
- 反馈遵循 `schema/feedback-v0.7.schema.json`；坐标为图像像素，原点在左上角，不能直接当作屏幕坐标或 CSS 像素。

## 版本

- 0.1.1：`EDITHERE_CLI` 失效时明确报错而非静默回退；必填参数前置校验；`edithere_status` 与失败信息回显实际使用的 CLI 路径与来源；`edithere_annotate` 说明并发限制。
- 0.1.0：首次提供 `status` / `open` / `capture` / `annotate` / `export` 五个工具。

连接器版本独立于 EditHere 程序版本（当前程序 0.8.21）；两者的版本号会同时出现在 `edithere_status` 响应中。
