# 本机网页入口

仓库技能随 Web Agent 包发布，本文件不保存开发者个人路径。
相对本文件，MCP 服务为 `../../../mcp/server.bundle.cjs`，兼容 CLI 为 `../../../agent.cjs`。

完整解压后运行 `node web/install-skill.cjs`。安装器核对运行文件，并在已安装技能的本引用中写入本机 Node.js、MCP 服务、配置和兼容 CLI 的绝对路径。保留解压目录，移动后重新定位入口。

MCP 需要 Node.js 20+，通过宿主管理的 stdio 连接，无本地 HTTP 监听。技能安装不会自动修改宿主 MCP 配置。
兼容 CLI 使用 Node.js 18+ 和本机回环服务。普通离线 `EditHere.html` 无需 Node.js 或服务器。
