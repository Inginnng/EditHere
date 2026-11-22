# 退出码、版本基线与排障

本文件汇总桌面 CLI、连接器、受限沙箱与 Web/DSH 服务排障规则，主流程无需记忆这些细节，遇到异常时按表排查即可。

## 1. CLI 退出码对照表

所有 CLI 响应（`--help`、`--version` 除外）为单行 UTF-8 JSON：

| 退出码 | 错误码 / 含义 | 处理方式 |
| --- | --- | --- |
| `0` | 成功 | 验证 stdout JSON `ok === true` 且本次唯一 `--output` 文件存在并可解析后实施 |
| `2` | 参数或协议错误 | 核对输入文件/清单绝对路径是否存在、参数是否合法 |
| `3` | 程序或接口不可用 | 先跑 `status`；若为 `agent_endpoint_unavailable`，保留当前工作并由用户正常重启程序，**不要强杀进程** |
| `4` | `busy`（正忙） | 存在未保存文档、未完成截图或进行中的会话；先让用户处理或等待当前会话，不重复发起 |
| `5` | 文件或 I/O 错误 | 确认输入存在、输出路径**尚不存在**且父目录可写 |
| `6` | 用户取消 | 不沿用上一轮结果，不自动再开一轮 |
| `7` | 等待超时 | 同 `6`；确认用户确实未提交后再询问是否重新发起（`--timeout` 支持 `1`–`86400` 秒） |
| `8` | `desktop_access_required` | 当前运行环境无权访问登录桌面会话，按第 3 节切换受控桌面入口 |

## 2. 程序版本基线

先执行 `edithere-cli --version`（不连接桌面、不受沙箱限制），并与 `status` 返回的 `version` 核对；若两者不一致，说明本机存在多份安装，按**较低版本**判定能力并提醒用户核对路径（不悄悄改用其他位置同名程序）：

| EditHere 版本 | 可用能力与注意事项 |
| --- | --- |
| **0.10.0 及以上** | 完整支持图片、`.edithere` 项目、项目组图与视频标注（`video-feedback-1`） |
| **0.9.8 – 0.9.9** | 仅支持图片与 `.edithere` 项目；输入视频以 `io_error` 失败 |
| **0.9.6 – 0.9.7** | 历史端点命名与桌面不一致，`status` 误报 `running:false` 且触发 `startup_timeout`；提示升级到新版 |
| **0.9.0 及以上** | 导出采用 `objects` 结构（更早版本为并行 `annotations` + `changes`） |
| **0.8.21 及以上** | 仅当桌面接口明确不存在时 `status` 才返回 `running:false`；权限受限或连接异常时 `running:null` 表示未知 |

版本不足时引导用户从发布页（`https://github.com/Inginnng/EditHere/releases/latest`）升级，不擅自覆盖系统环境。

## 3. 桌面连接与受限沙箱

- Windows / macOS 的 `status`、`open`、`capture`、`annotate` 都需要连接当前登录用户的桌面会话；`--help`、`--version` 与离线 `export` 不需要。
- 若 Agent 处于受限沙箱环境，从第一次调用桌面命令起就应请求工具自带的受控桌面执行方式（例如 Codex `exec_command` 的 `require_escalated` 并说明用途），避免在不可见会话里启动窗口；切勿修改全局沙箱设置或让应用提权。
- 工具创建进程前直接报沙箱错误时，尚未取得 EditHere 状态，不可断定程序未运行。

## 4. 断连、中断与会话恢复

- **连接中断 / 回执丢失**：仅检查**本轮唯一输出路径**是否存在且为有效 JSON，核实用户已点击「完成并返回 AI」后再读取继续；未确认前不盲目重试，也不沿用旧轮结果。
- **`startup_failed` / `startup_timeout`**：结合 `connection` 字段诊断，不要循环重启桌面程序。
- **Web 兼容服务（端口 7330）与 DSH 服务（端口 4782）**：
  - 7330 端口被其他工作区占用时，为 `web/preview.cjs` 和 `web/agent.cjs` 指定同一个 `EDITHERE_WEB_PORT`（`1`–`65535`），禁止直接杀掉未知进程。
  - 兼容网页提交后已持久化到磁盘（`.edithere/sessions/<sessionId>.json` 或会话目录），即使后台 Node 进程随宿主回合停止而退出，恢复或再查 `poll` 仍可核验结果。
  - DSH 服务启动失败时检查 `%USERPROFILE%\.edithere\dsh\logs\server-error.log`，或提示用户手动运行 `node <dsh>/cli.mjs serve`。
