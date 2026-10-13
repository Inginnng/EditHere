# EditHere 分发目录

**简体中文** · [English](DISTRIBUTION.en.md)

[返回产品介绍](../README.md) · [开发说明](DEVELOPMENT.md) · [许可与第三方材料](../LICENSING.md)

EditHere 的 Windows、macOS、Linux 版本共用 `app/` 中的 C++20 / Qt 源码，通过平台后端和打包脚本生成不同程序包。`web/` 是网页实现；`connector/`、`skills/`、`trae-plugin/` 是相应的 AI 接入材料。不要为每个平台复制一套源码，也不要将构建缓存当作安装包交付。

## 目录职责

| 目录 | 内容与用途 |
| --- | --- |
| `app/`、`assets/`、`schema/`、`third_party/` | 桌面共用源码、品牌资源、数据格式和随源码维护的依赖 |
| `web/` | 网页源码、独立 HTML、MCP Apps 服务与内嵌 HTML、兼容本地连接器及网页 Skill |
| `scripts/`、`packaging/`、`.github/workflows/` | 原有构建、打包、安装器和 CI 入口 |
| `connector/`、`skills/`、`trae-plugin/` | 桌面 MCP、桌面 Skill、Trae 插件的来源 |
| `build/`、`build-macos/`、`build-linux/`、`build-*/` | 开发构建缓存；保留原位置，不纳入分发 |
| `artifacts/` | 测试、截图和验收记录；不整体分发 |
| `dist/` | 原有打包脚本的输出；可能混有历史包与实验包 |
| `prepare-distribution.py` 指定的新目录 | 按平台和版本整理的本地分发汇总 |

原有路径保持有效。构建脚本按 `scripts/..` 定位仓库，CMake、CI 和测试也引用现有目录；整理分发不移动这些目录，不替换已经安装的程序或 Skill。

## 生成统一汇总

在仓库根目录执行，依赖 Python 3.10+；离线网页构建支持 Node.js 18+，MCP 源码构建需要 Node.js 20+。修改 MCP 或其共享页面源码后先运行 `npm --prefix web/mcp ci`、`node web/mcp/build.mjs`；下载后使用已经打包的 Web Agent 不需要 npm。

```powershell
python scripts/prepare-distribution.py --output ../分发 --include-history
```

目标必须是尚不存在的新目录。省略 `--output` 时，生成 `dist/distribution-<产品版本>-<Git短哈希>/`。脚本先重新生成 `web/EditHere.html`，核对 `web/mcp/build-info.json` 中的源码与产物哈希，拒绝旧 MCP bundle 与新源码混装，再汇总分发文件。现有原生程序包只被读取和复制，不移动、不删除，不覆盖已有分发目录。

常用参数：

- `--include-history`：额外整理已存在的历史压缩包、安装器和 AppImage；仅扫描规定的打包来源，不复制测试解压目录、开发缓存或整个仓库。
- `--native-dir <目录>`：可重复指定其他原生打包来源。Windows/macOS 应使用构建阶段带版本名的原始产物；Linux 应保留相邻 `version.txt` 或 `linux-<版本>` 目录。最终 Release 的无版本稳定文件名不能直接归入当前版本，不从文件名猜测或补写版本。
- `--require-all`：要求当前版本的 Windows 便携包、Windows 安装器、macOS DMG、Linux AppImage 全部齐备；任一缺失时预先报错。默认允许生成不完整汇总，并在对应目录和清单中明确标为待构建。
- `--web-only`：只整理离线网页包与 Web Agent 包，不扫描原生程序包，供本地核对与发布工作流使用；`--channel release` 把包内说明与构建标记标为发布渠道，只能与该参数同用。

生成结果的结构为：

```text
分发/
  README.md
  manifest.json
  SHA256SUMS.txt
  web/<产品版本>-<Git短哈希>/
    ...-web.zip
    ...-web-agent.zip
  windows/<产品版本>/
  macos/<产品版本>/
  linux/<产品版本>/
  integrations/
    desktop-mcp/
    desktop-skill/
    trae-plugin/
  archive/<平台>/<原版本>/<来源名+哈希>/
```

`web` 将直接打开的网页包与供 Agent 使用的连接器包分开。web-agent 白名单包含 MCP 服务 bundle、官方 App SDK 的内嵌 HTML、配置模板、许可、安装器与 Skill；不包含 node_modules、令牌、会话或个人路径。MCP 需要支持 MCP Apps 的宿主和 Node.js 20+；同包保留普通浏览器兼容入口。`integrations` 中的接入包按自身版本整理，不将 MCP 或插件版本当作桌面应用版本。历史包保留原版本及来源，标为 `historical-unverified`，不会补进当前版本的缺项。

下发给用户的包可以脱离仓库单独复核：`node scripts/web-package-check.cjs <解压目录或 ZIP 路径> [测试图片]` 只读包内文件，自行识别包型。离线包核对根目录单文件 HTML、许可与「不含 MCP 运行时」；web-agent 包额外核对 `web/mcp/build-info.json` 里三个产物的哈希、`README.md` 的渠道口径、技能安装（含重复安装必须被拒绝）以及 MCP stdio 链路（未声明与声明 UI 能力的两种宿主、内嵌 HTML 可读回）。找不到测试图片时 MCP 段标记为跳过而不是通过。该结果只证明包自洽并在本机可用，不代替宿主实机验收。

以生成的 `manifest.json` 为本次汇总结果：核对版本、来源、SHA-256、可用状态和缺项。清单与校验和证明收集了哪些字节，不替代程序运行、签名、公证和平台验收。当前产品源码版本来自 `CMakeLists.txt`；已有旧程序包不会因为源码升级而被改名为新版。整理时即使源码是 `0.10.5`，没有对应打包产物的平台也必须继续显示待构建。

## 各平台原始产物

`<版本>` 指成功构建写出的版本，不是手动填写的文件名。

| 平台 | 原有构建 / 打包入口 | 默认原始产物 | 对外稳定下载名 |
| --- | --- | --- | --- |
| Windows x64 | `scripts/build-windows.ps1` → `scripts/package-windows.ps1` → `scripts/package-installer.ps1` | `dist/EditHere-<版本>-win-x64.zip`、`dist/EditHere-<版本>-win-x64-setup.exe` | `EditHere-win-x64.zip`、`EditHere-win-x64-setup.exe` |
| macOS Universal | `scripts/build-macos.sh`，含测试和 DMG 打包 | `dist/EditHere-<版本>-macos-universal.dmg` | `EditHere-macos-universal.dmg` |
| Linux x86_64 | `scripts/build-linux.sh` → `scripts/package-linux.sh` | `dist/linux-<版本>/EditHere-linux-x86_64.AppImage`、同名 `.sha256`、`edithere-cli` | `EditHere-linux-x86_64.AppImage`、`edithere-cli` |
| Web | `node web/build.cjs`；MCP 用 `node web/mcp/build.mjs`；统一脚本整理两种包（`--web-only` 可单独整理） | `web/EditHere.html`、`web/EditHere.mcp.html`、`web/mcp/server.bundle.cjs` 及分发 ZIP | `EditHere-web.zip`（离线网页版）、`EditHere-web-agent.zip`（Agent 包） |

Windows ZIP 内直接放程序、DLL 和插件目录，不能再套一层平台目录：桌面更新器会将 ZIP 直接解压到安装目录。macOS 的 CLI 位于 `EditHere.app/Contents/MacOS/edithere-cli`；Linux 的独立 `edithere-cli` 是与 AppImage 配套使用的包装脚本，不应混搭另一版本。

桌面打包从成功链接后生成的 `version.txt` 读取版本。版本唯一来源、命令示例及测试入口见[开发说明](DEVELOPMENT.md)。默认构建依赖 Qt 6.8.3、CMake 3.24+、Ninja 和 C++20 编译器：Windows 发行流程使用 MinGW GCC 13.1.0；macOS 构建目标为 14+、`arm64;x86_64`，CI 固定 Xcode 16.4；Linux 包目标为 Ubuntu 24.04 x86_64，还需要平台依赖和 linuxdeploy，见[Linux 说明](LINUX.md)。本地 Windows 汇总不会自动构建 macOS 或 Linux 安装包。

## 本地汇总与公开发布

统一脚本只准备本地文件。它不会打标签、推送仓库、创建 GitHub / Gitee Release、注册开机启动，或更新当前机器上的程序。

现有 `.github/workflows/release.yml` 在版本标签触发后构建并汇总四类原生程序包与两种 Web 包，再使用表中的稳定下载名发布（Web 包由 `build-web` 作业调用 `--web-only --channel release` 生成；内嵌素材与随包法律文本的行尾由 `.gitattributes` 固定，避免同一源码在不同平台产出不同字节）。文档和 Windows 更新器依赖这些名字，本次目录整理不更改它们。正式发布还应核对标签与构建版本、全部平台产物、测试结果、许可及相应源码材料；历史失败的发布记录、试验包或同版本名文件都不能代替这些检查。

Windows 与 macOS 的签名状态、macOS 公证及实机验收、Linux 预览状态仍以当前[产品说明](../README.md)和构建记录为准。Qt、FFmpeg 等许可和对应源码要求见[第三方说明](../packaging/THIRD-PARTY-NOTICES.md)；汇总脚本不代表已完成公开分发验收。
