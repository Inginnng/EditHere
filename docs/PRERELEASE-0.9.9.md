# EditHere 0.9.9 预发说明

日期：2026-09-30（香港时间）。本版用于提前试用与收集反馈，尚未公开发布；程序内显示版本号为 `0.9.9`。

## 本次改动

- **批注图片比例统一**：复制和导出的批注图片采用统一图片展示宽度，编号、文字和侧栏不再随原图分辨率变小或变大。相同宽高比、相同相对位置的批注布局基本一致；不同宽高比仍保留各自比例。项目原图与坐标不变。
- **Windows 更新可靠性**：安装版和免安装版共用 NSIS 更新流程。更新前确认未保存的编辑；取消或安装器启动失败不会退出当前程序。下载强制校验 SHA-256，安装时先准备新目录，再切换，失败恢复旧目录并保留备份。
- **设置保留**：保存设置不再重置截图圆角、阴影和边框；升级保留开机启动与 PATH 选择。安装前的进程检查限定在目标目录。
- **Linux 预览支持**：新增 X11/Wayland 截图、全局快捷键、XDG 自启动、AT-SPI 元素识别、Tesseract OCR、CLI 与 AppImage 打包。复用 Qt、桌面 Portal、AT-SPI、Tesseract 和 linuxdeploy。

## 平台与包状态

### Windows

本次在 Windows 11 x64、Qt 6.8.0 / MSVC 2022 本地构建上完成全量测试，包括原生截图、快捷键和焦点恢复专项。

本地应用为 `build-local/EditHere.exe`，CLI 为同目录的 `edithere-cli.exe`。这是开发验证构建，**不是已完成最终验收的 Windows 发行包**；不能只复制单个 EXE 给其他电脑使用。

正式分发仍需通过现有 Qt 6.8.3 / MinGW 打包流程生成免安装 ZIP 与 NSIS 安装器，再对最终包进行干净系统安装和从旧版本升级的验收。安装器的临时目录事务测试已通过，但不等于生产安装目录的端到端升级已验收。

### Linux

本次预览目标为 **Ubuntu 24.04 x86_64**，不代表所有发行版或 ARM64 均已兼容。

已验证的本地包位于 `dist/linux-0.9.9-tested/`：

- `EditHere-linux-x86_64.AppImage`：GUI，也支持 `--cli`。
- `edithere-cli`：配套 CLI 启动脚本，应与 AppImage 放在同一目录。
- `EditHere-linux-x86_64.AppImage.sha256`：完整性校验文件。

AppImage SHA-256：

```text
225e65f8b337bad8905fcb68af3ef75619f8342638bae575b6940830206cfd1f
```

在包所在目录运行：

```bash
sha256sum -c EditHere-linux-x86_64.AppImage.sha256
chmod +x EditHere-linux-x86_64.AppImage edithere-cli
./EditHere-linux-x86_64.AppImage
./edithere-cli --version
./EditHere-linux-x86_64.AppImage --capture
```

没有 FUSE 时可用 `APPIMAGE_EXTRACT_AND_RUN=1 ./EditHere-linux-x86_64.AppImage`；调用配套 CLI 时也可设置此环境变量。

OCR 使用本地系统 Tesseract，不上传图片。需要 OCR 时安装引擎与语言包：

```bash
sudo apt-get install tesseract-ocr tesseract-ocr-chi-sim tesseract-ocr-chi-tra
```

中文字体建议安装 `fonts-noto-cjk`。完整平台说明见 [Linux 使用说明](LINUX.md)。

## 已知限制与待验收项

- Linux GNOME/KDE 的真实 Wayland 授权、快捷键和多屏混合缩放尚未实机验收。现有 Portal 测试使用独立 DBus 模拟服务；X11 截图和快捷键已在 Xvfb 中验证。
- Wayland 截图先由系统授权/选区，再在应用中审阅。Portal 不提供原始桌面坐标，因此不关联桌面原生元素；不是与 Windows 完全一致的截图交互。
- 桌面不支持 GlobalShortcuts Portal 时，需要在系统快捷键中绑定 AppImage 完整路径加 `--capture`。没有托盘宿主时，普通启动保留编辑窗口入口，关闭后退出。
- Linux OCR 依赖已安装的 Tesseract 语言包；AT-SPI 元素识别依赖目标应用的可访问性信息。不可用时会提示或回退，不承诺所有应用都能识别原生元素。
- Linux 与 macOS 更新采用发布页手动下载。Linux 尚未接入正式 Release 资产流程；当前不会通过公开 Latest 自动分发本预览包。
- Windows 最终发行包的干净系统安装、真实旧版本升级，以及生产环境的取消/失败恢复仍需验收。当前 Windows Release 构建使用 `-SkipTests`，发布前还需落实测试门禁。
- 本轮未运行 macOS 构建或原生测试，不能将 Windows/Linux 的通过结果等同于 macOS 已通过。

## 试用重点与反馈方式

试用前备份重要项目和设置，不要删除旧版备份目录。

1. 对同一内容的低分辨率、高分辨率图片添加相同位置与文字的批注，比较复制导出的字号、编号和侧栏布局。
2. 测试区域、点和移动批注，保存项目后重新打开，检查原图和坐标是否保持正确。
3. 检查快捷键触发、冲突提示、重新绑定，以及截图取消后能否继续正常操作。
4. 检查保存设置后的圆角、阴影、边框，以及开机启动选项是否保留。
5. Linux 重点反馈桌面环境、X11/Wayland 类型、屏幕数量和缩放比例；分别测试截图授权允许与取消、OCR 缺少语言包和 CLI 连接。

反馈请包含：系统与桌面版本、包来源及程序版本、复现步骤、预期/实际结果、截图或报错；截图中的敏感信息请先遮盖。不要发送账号令牌、完整个人配置或未经脱敏的私密图片。

## 本轮测试记录（维护者）

测试基于当前本地未提交工作区，不代表已发布或已通过远端 CI。

| 检查 | 结果 | 本地证据 |
| --- | --- | --- |
| 快速检查 | 全量构建成功，`git diff --check` 无错误 | 本轮构建与检查输出 |
| Windows 常规全量 CTest | 18/18 套件通过，包含检测、批注、布局、CLI、设置、翻译、OCR、启动、更新、截图与安装事务 | `build-local/prerelease-results.xml` |
| Windows 原生桌面专项 | 1/1 套件通过 | `build-local/prerelease-platform-results.xml` |
| NSIS 安装事务 | 4/4 用例通过：成功与备份、复制失败、目录锁定、提交失败回滚；仅操作临时目录 | Windows 全量结果中的 `installer-transaction` |
| Linux 全量 CTest | 18/18 套件通过 | `build-linux/test-results.xml` |
| Linux X11 专项补测 | 8 项通过、0 失败、0 跳过，包含 QtTest 初始化/清理 | `build-linux/prerelease-platform.txt` |
| Windows/Linux 真实 GitHub 检查 | 两端均通过，只读查询，不安装更新 | 两端构建目录的 `prerelease-live-update.txt` |
| Linux 最终 AppImage | 版本 0.9.9；GUI 启动、CLI 连接与退出通过；在临时配置及隔离虚拟桌面运行 | `scripts/check-linux-package.py` 本轮输出 |

默认全量测试中的联网更新用例通过单独联网运行补测；Linux offscreen 中跳过的两个 X11 用例通过 Xvfb 补测。全量套件通过不代表没有内部跳过项，以上补测覆盖了这些项目。

Windows 首次沙箱内 CTest 在启动子进程时停滞，直接运行同一检测测试通过；停止已确认属于本轮的测试进程后，在正常权限环境完整复跑，18/18 通过。记录最终成功的 JUnit 文件，不能用历史 `LastTestsFailed.log` 判断本轮结果。

结论：可以进行受控预发试用；Linux 保持预览标识，Windows 最终发行包验收和真实 Wayland 验收完成前，不宣称跨平台正式发布已全部完成。
