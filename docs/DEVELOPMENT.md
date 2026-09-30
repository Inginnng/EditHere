# EditHere · 改这里：开发说明

[返回产品介绍](../README.md) · [使用指南](USER-GUIDE.md) · [AI 与命令行](AGENT-CLI.md) · [更新记录](../CHANGELOG.md)

以下命令均在仓库根目录执行，文件路径也相对于仓库根目录。

## 分支与技术栈

当前分支为 `codex/native`，使用 C++20 + Qt 6 Widgets。已保留的 `desktop` 分支为 Windows WPF 0.2（`cf9c364`），`web` 分支为 Web 0.3（`66548c5`）。

## 版本管理

日常修复和小幅优化只增加末位补丁号，例如 `0.8.0 → 0.8.1 → 0.8.2`。中间位只在集中完成较大功能阶段、明确发布时增加；不再为每轮开发递增。第一位保留给明确的大版本发布，已有版本号和历史包保持不变。回退仅针对当次实现，不冻结后续版本；撤回的编号不复用。

产品版本唯一来源为 `CMakeLists.txt` 的 `project(... VERSION ...)`。运行时版本和 Mac 应用信息自动使用该值；成功链接后生成 `build/version.txt`（Mac 为 `build-macos/version.txt`），两平台打包脚本据此命名并随包附带 `version.txt`。修改版本后必须重新构建，避免将旧程序标记为新版本。产品版本与 JSON 格式独立管理：精简反馈的当前格式由 `feedback-minimal.schema.json` 定义（`0.9.0` 起为 `objects` 对象结构，`0.8.21` 的 `annotations` / `changes` 并行数组见 `feedback-v0.7.schema.json`，仍可导入）；`feedback-v1` / `v1.1` / `v2` 描述早期**项目文档**格式，完整项目使用 `project-v3.schema.json`。文件名代表数据格式，不代表应用版本。

## 发布流程

打 `vX.Y.Z` 标签并推送即触发 `.github/workflows/release.yml`：Windows 与 macOS 构建任务分别产出安装包，release 任务汇总后创建 GitHub Release。

- **发布必须带完整安装包**。release 任务在发布前检查 `EditHere-win-x64-setup.exe`、`EditHere-win-x64.zip`、`EditHere-macos-universal.dmg` 三者均存在且非空，任一缺失即令工作流失败；构建任务失败时不会产出只有源码的发布。
- 资产名不含版本号（文档与更新检查使用 `/releases/latest/download/<固定名>`），随包附 `SHA256SUMS.txt` 与逐文件 `.sha256`。
- 发布说明优先取 `docs/releases/<版本>.md`（人工整理的面向用户说明）；该文件缺失时回落到 `CHANGELOG.md` 对应章节。整理说明时先写草稿交用户确认，再打标签。

## 构建与验证

Linux 预览版的系统依赖、构建、AppImage 打包与 X11/Wayland 行为见 [Linux 说明](LINUX.md)。

依赖：Qt **6.8.3**（qtbase、qtimageformats 动态库，外加 qttranslations 提供界面语言用到的 Qt 自带翻译）、CMake 3.24+、Ninja、C++20 编译器。Windows 使用 MinGW GCC 13.1.0；Mac CI 固定 macOS 15 + Xcode 16.4，以匹配 Qt 6.8.3；Xcode 26 SDK 已移除该版本 Qt 链接的 AGL framework。

Windows PowerShell 7，CMake 在 PATH 中：

```powershell
./scripts/build-windows.ps1 -QtRoot C:/Qt/6.8.3/mingw_64 -Compiler C:/Qt/Tools/mingw1310_64/bin/g++.exe -Ninja C:/Tools/ninja.exe
./scripts/package-windows.ps1 -QtRoot C:/Qt/6.8.3/mingw_64 -CompilerBin C:/Qt/Tools/mingw1310_64/bin
```

构建脚本运行检测、核心数据、布局、界面、行内文本、画布交互、设置、多语言、引导、自启、启动流程、更新、文字识别、截图会话和 CLI 等测试；Windows 另含平台测试，以当前 CTest 输出为准。导出样本及窗口渲染截图存于 `artifacts/native-ui/`。安装 `jsonschema==4.26.0` 后，可运行 `python scripts/validate-exports.py` 独立校验导出；macOS 构建（`.github/workflows/native-macos.yml`）会为同一批产物自动运行它，schema 与真实导出不符时构建即失败。改动界面文案后另跑 `python scripts/check-translations.py --qt <Qt 安装目录>`，详见 [多语言与翻译](i18n.md)。打包只写新目录；重复打包请传入新的 `-OutputDirectory`。

除 Qt 测试外，CTest 还注册两条与工具链解耦的检查：`packaging`（`python scripts/check-packaging.py`）校验安装器脚本编码、NSIS 宏与实编译、便携压缩包结构、发布资产命名与校验文件、版本号唯一来源、Qt 翻译部署、文字识别桥接脚本的编码与占位符、`.ts` 与预编译 `.qm` 的逐条对应，以及 AI 与命令行文档、skill 里的反馈结构与导出字段的一致性；`translations`（`scripts/check-translations.py`）在能找到 Qt Linguist 时才注册。这些检查与项目历史问题的对应关系见[问题归档与回归测试](REGRESSIONS.md)，**新增问题修复时要在该页登记并补一条会复发的检查**。

## 截图流程与文字识别

截图不再"松手即批注"。`Overlay`（每屏一个无边框置顶窗口）负责选区与交互，选区定下来后由它内部的 `CaptureToolbar` 提供动作，再由 `Controller` 分别处理；只有「批注」会走 `completeCapture` 进入 `Editor`。

| 文件 | 职责 |
| --- | --- |
| `app/overlay.{h,cpp}` | 选区绘制与调整、候选块、取色、工具条的宿主；只发信号，不做动作 |
| `app/capturetoolbar.{h,cpp}` | 工具条控件与「更多」菜单（固定比例、圆角/边框/阴影、识别语言、历史与选区） |
| `app/capturesession.{h,cpp}` | 比例与尺寸换算、样式合成 `composeCapture()`、跨会话的 `CaptureHistory` |
| `app/pinwindow.{h,cpp}` | 置顶看图窗口：拖动、缩放、透明度、右键菜单 |
| `app/ocr.{h,cpp}` + `app/ocr_mac.mm` | 识别引擎封装：分条带、坐标还原、结果解析；macOS 走 Vision |
| `app/ocrdialog.{h,cpp}` | 识别结果窗口：逐行列表与原图高亮联动 |
| `app/ocrbridge.ps1` | Windows 侧的系统 OCR 桥接脚本，作为 Qt 资源嵌入 |

Windows 的识别选择由工具链决定：**发布包用 MinGW 构建，没有 C++/WinRT**，因此不直接调用 `Windows.Media.Ocr`，而是把系统 OCR 交给一段以 `-EncodedCommand`（Base64 UTF-16LE）传入的 PowerShell 脚本。这条链上有三条硬约束，改动时务必注意：

1. **脚本必须纯 ASCII 且不带 BOM**。带 BOM 时它会被解码成脚本顶部的杂散字符；非 ASCII 文案在编解码链上不可靠。`scripts/check-packaging.py::ocr_bridge` 会拦住这两种情况。
2. **路径必须是反斜杠形式**。Windows Runtime 的文件 API 拒绝 `C:/...`，报 `UNABLE_TO_MASK_PATH`；`prepareOcrBridge()` 用 `QDir::toNativeSeparators` 转换。
3. **不要用管道读子进程输出**。QProcess 在某些受限环境里建不了管道，桥接的结果因此写进临时 JSON 文件，进程输出重定向到文件而不是 `readAllStandardOutput()`。

macOS 侧直接用 Vision（`app/ocr_mac.mm`，链 `-framework Vision`），两边都**不联网、不上传、不新增第三方依赖**。

Windows 安装器另依赖 NSIS 3.x。可解压 NSIS 官方 ZIP 后直接指定 `makensis.exe`，无需全局安装。在免安装版压缩包生成后运行：

```powershell
./scripts/package-installer.ps1 -NsisCompiler C:/Tools/nsis-3.x/makensis.exe
```

请将示例中的 NSIS 路径换为实际位置。0.8.21 的安装器输出为 `dist/EditHere-0.8.21-win-x64-setup.exe`。安装器按当前用户安装到 `%LOCALAPPDATA%\Programs\EditHere`，使用当前用户的开始菜单、卸载登记与文件关联，不请求管理员权限。登录启动和 PATH 在新安装时默认选中，桌面快捷方式可选；升级依据既有当前用户 Run 登记保留启动选项。卸载按安装文件清单移除包内文件，保留用户设置与项目。

应用内 Windows 更新复用 NSIS 的 Unicode 文件操作，安装版和便携版共用安装包。便携更新参数为 `/S /UPDATE /PORTABLE /D=<原目录>`，`/D=` 必须最后且不加引号；该协议从 0.9.9 起支持，旧发行版仅打开发布页。先准备同级暂存目录，再移动旧目录、切换新目录，切换失败时恢复旧目录；成功后保留备份及 `update-backup.txt`。便携模式跳过系统集成。下载使用 Qt 的独立临时目录和原子写入，并强制校验同名 SHA-256 文件。macOS 当前仅提供手动下载入口。

选择沿用现有开源 NSIS，避免额外维护 cmd/tar 更新脚本；WinSparkle 需要新增 appcast 与签名发布链，Qt Installer Framework 需要迁移安装器及更新仓库，不适合作为本次修复的必要依赖。`tests/installer_transaction_test.py` 使用真实 NSIS 和无副作用的测试程序，在隔离目录验证成功替换、复制失败、目录锁定和切换失败回滚；需要设置 `NSIS_MAKENSIS` 与 `EDITHERE_UPDATE_STUB`，缺失时跳过。

Mac 构建（尚未实机验收）：

```bash
export QT_ROOT="$HOME/Qt/6.8.3/macos"
bash scripts/build-macos.sh
```

脚本生成通用 `.app` 和本地测试用 `.dmg`，使用临时签名。CLI 一同部署到 `EditHere.app/Contents/MacOS/edithere-cli`；DMG 提供 Applications 拖拽入口。构建前会检查所选 SDK 是否包含 Qt 6.8.3 需要的 AGL，并将同一 SDK 显式传给 CMake。CI 附件中的 `build-environment.txt` 记录实际 Xcode、SDK、Qt 和临时目录；`test-results.xml` 与 `LastTest.log` 提供具体失败测试。macOS 的 Unix socket 按完整路径的字节数计限长，隔离测试统一使用短名称，并校验当前临时目录下的端点长度。公开仓库的 [macOS 构建记录](https://github.com/Inginnng/EditHere/actions/workflows/native-macos.yml) 保留对应构建的测试结果和 DMG；屏幕录制授权、辅助功能授权及多屏截图仍需实机验收。

CLI 的参数、JSON 响应和用户完成协议见 [AI 与命令行](AGENT-CLI.md)。配套 skill 位于 `skills/edithere`，可通过 `skill-creator` 的 `quick_validate.py` 校验；该校验不替代 CLI 行为测试或用户交互验收。

Qt 与 MinGW 的许可、版权声明位于 `packaging/licenses/`，随运行包交付；对应 Qt 源码存于 `dist/native-sources/`。公开分发时需同时提供相应源码归档，见[第三方说明](../packaging/THIRD-PARTY-NOTICES.md)。
