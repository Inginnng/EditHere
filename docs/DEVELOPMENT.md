# EditHere · 改这里：开发说明

[返回产品介绍](../README.md) · [使用指南](USER-GUIDE.md) · [AI 与命令行](AGENT-CLI.md) · [更新记录](../CHANGELOG.md)

以下命令均在仓库根目录执行，文件路径也相对于仓库根目录。

## 分支与技术栈

当前分支为 `codex/native`，使用 C++20 + Qt 6 Widgets。已保留的 `desktop` 分支为 Windows WPF 0.2（`cf9c364`），`web` 分支为 Web 0.3（`66548c5`）。

## 版本管理

日常修复和小幅优化只增加末位补丁号，例如 `0.8.0 → 0.8.1 → 0.8.2`。中间位只在集中完成较大功能阶段、明确发布时增加；不再为每轮开发递增。第一位保留给明确的大版本发布，已有版本号和历史包保持不变。回退仅针对当次实现，不冻结后续版本；撤回的编号不复用。

产品版本唯一来源为 `CMakeLists.txt` 的 `project(... VERSION ...)`。运行时版本和 Mac 应用信息自动使用该值；成功链接后生成 `build/version.txt`（Mac 为 `build-macos/version.txt`），两平台打包脚本据此命名并随包附带 `version.txt`。修改版本后必须重新构建，避免将旧程序标记为新版本。产品版本与 JSON 格式独立管理：`0.8.21` 的精简反馈继续由 `feedback-v0.7.schema.json` 定义，完整项目使用 `project-v3.schema.json`。

## 构建与验证

依赖：Qt **6.8.3**（qtbase、qtimageformats 动态库）、CMake 3.24+、Ninja、C++20 编译器。Windows 使用 MinGW GCC 13.1.0；Mac CI 固定 macOS 15 + Xcode 16.4，以匹配 Qt 6.8.3；Xcode 26 SDK 已移除该版本 Qt 链接的 AGL framework。

Windows PowerShell 7，CMake 在 PATH 中：

```powershell
./scripts/build-windows.ps1 -QtRoot C:/Qt/6.8.3/mingw_64 -Compiler C:/Qt/Tools/mingw1310_64/bin/g++.exe -Ninja C:/Tools/ninja.exe
./scripts/package-windows.ps1 -QtRoot C:/Qt/6.8.3/mingw_64 -CompilerBin C:/Qt/Tools/mingw1310_64/bin
```

构建脚本运行检测、核心数据、布局、界面、行内文本、画布交互、设置、引导、自启、启动流程、更新和 CLI 等测试；Windows 另含平台测试，以当前 CTest 输出为准。导出样本及窗口渲染截图存于 `artifacts/native-ui/`。安装 `jsonschema==4.26.0` 后，可运行 `python scripts/validate-exports.py` 独立校验导出。打包只写新目录；重复打包请传入新的 `-OutputDirectory`。

Windows 安装器另依赖 NSIS 3.x。可解压 NSIS 官方 ZIP 后直接指定 `makensis.exe`，无需全局安装。在免安装版压缩包生成后运行：

```powershell
./scripts/package-installer.ps1 -NsisCompiler C:/Tools/nsis-3.x/makensis.exe
```

请将示例中的 NSIS 路径换为实际位置。0.8.21 的安装器输出为 `dist/EditHere-0.8.21-win-x64-setup.exe`。安装器按当前用户安装到 `%LOCALAPPDATA%\Programs\EditHere`，使用当前用户的开始菜单、卸载登记与文件关联，不请求管理员权限。登录启动和 PATH 在新安装时默认选中，桌面快捷方式可选；升级依据既有当前用户 Run 登记保留启动选项。卸载按安装文件清单移除包内文件，保留用户设置与项目。

Mac 构建（尚未实机验收）：

```bash
export QT_ROOT="$HOME/Qt/6.8.3/macos"
bash scripts/build-macos.sh
```

脚本生成通用 `.app` 和本地测试用 `.dmg`，使用临时签名。CLI 一同部署到 `EditHere.app/Contents/MacOS/edithere-cli`；DMG 提供 Applications 拖拽入口。构建前会检查所选 SDK 是否包含 Qt 6.8.3 需要的 AGL，并将同一 SDK 显式传给 CMake。CI 附件中的 `build-environment.txt` 记录实际 Xcode、SDK、Qt 和临时目录；`test-results.xml` 与 `LastTest.log` 提供具体失败测试。macOS 的 Unix socket 按完整路径的字节数计限长，隔离测试统一使用短名称，并校验当前临时目录下的端点长度。私有仓库的 [macOS 构建记录](https://github.com/Inginnng/EditHere/actions/workflows/native-macos.yml) 保留对应构建的测试结果和 DMG；屏幕录制授权、辅助功能授权及多屏截图仍需实机验收。

CLI 的参数、JSON 响应和用户完成协议见 [AI 与命令行](AGENT-CLI.md)。配套 skill 位于 `skills/edithere`，可通过 `skill-creator` 的 `quick_validate.py` 校验；该校验不替代 CLI 行为测试或用户交互验收。

Qt 与 MinGW 的许可、版权声明位于 `packaging/licenses/`，随运行包交付；对应 Qt 源码存于 `dist/native-sources/`。公开分发时需同时提供相应源码归档，见[第三方说明](../packaging/THIRD-PARTY-NOTICES.md)。
