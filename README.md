# Help2Design Native · 0.4.0

面向 AI 开发反馈的桌面截图批注工具。打开后直接截屏，点击或框选需要修改的位置，输入意见，导出包含原图像素坐标的标准 JSON。

当前开发分支为 `codex/native`，采用 C++20 + Qt 6 Widgets。Windows 与 macOS 分别接入系统截图、快捷键和辅助功能接口；界面和数据处理不依赖浏览器引擎。

| 分支 | 内容 |
| --- | --- |
| `desktop` | 已保留的 Windows WPF 0.2 版本，`cf9c364` |
| `web` | 已保留的 Web 0.3 版本，`66548c5` |
| `codex/native` | 当前 C++ / Qt 桌面版本 |

## 使用

Windows 11 上已完成构建、真实屏幕采集和系统控件识别测试。便携包：
`dist/Help2Design-Native-0.4.0-win-x64.zip`。
压缩包约 21 MB，解压后约 57 MB。解压完整目录，双击 `Help2Design.exe`。已在移除开发环境路径后验证独立启动，所需 DLL 与插件均从便携目录加载。开发机可直接打开对应 dist 目录里的程序。

首次运行直接进入截图。之后使用 **Ctrl + Shift + 2** 或托盘图标再次截图。快捷键被占用时仍可从托盘操作。

- 移动鼠标智能选块，滚轮向上选择更大的包含区域，向下选择更小的区域；直接拖动也可截图。
- 确定选区后，可以移动选区或拖动八个手柄调整大小。Enter 开始批注，Ctrl+C 直接复制截图，右键返回，Esc 取消。
- 截图后使用智能、点、框、调整四种工具。单击或画框输入文字，右侧同步显示编号和描述。
- 调整模式支持移动标记、拖动框的八个手柄、双击编辑文字。Ctrl+Z 撤销，Ctrl+Y 重做，Delete 删除。
- 拖动顶部栏，或按住空格/Alt 拖动画面，可移动整个批注窗口。Ctrl+滚轮缩放；中键切换适应窗口和原尺寸。
- 支持打开、拖入或粘贴 PNG/JPEG/WebP/BMP 图片，以及重新打开项目 JSON。
- Ctrl+S 保存包含原图的项目。关闭窗口后收起到托盘，保留当前内容；退出或替换有修改的项目时提示保存。

Mac 编辑快捷键使用 Command，重做为 Command + Shift + Z。截图快捷键预定为 Command + Shift + 2。

## 导出与坐标

“导出 JSON”可直接复制文本，或保存到一个新目录：原图 PNG、带批注的预览 PNG、`feedback.json`。开启“包含原图数据”后，单个 JSON 即可重新导入恢复。

- 坐标单位始终是**原图像素**，与窗口位置、缩放比例无关；原点在左上角。
- 点使用 `point: {x, y}`。
- 框使用 `rectangle: {x1, y1, x2, y2}`。右下边界不包含在框内，宽度等于 `x2 - x1`。
- `capture.sha256` 标识准确的原始 PNG。未内嵌原图时，重新导入需要同目录的 `capture.imageFile`。
- Windows 的 `screenBounds` 和 `originalScreenBounds` 为系统物理屏幕坐标，可以为负。Mac 的系统坐标采用逻辑点，当前不将它们混写到物理屏幕坐标字段，故这两个字段为空；批注的原图像素坐标完整保留。
- 普通批注继续使用 [v1.0 规范](schema/feedback-v1.schema.json)，兼容旧桌面版和 Web 版。包含 Mac 系统辅助功能来源时使用 [v1.1 规范](schema/feedback-v1.1.schema.json)，新增来源值 `accessibility`。
- 导入还检查唯一 ID、连续编号、图片哈希、实际尺寸、坐标范围和字段类型。JSON Schema 与业务校验共同约束数据。

把 JSON 与原图一起交给 AI。识别得到的是视觉位置或系统辅助功能信息，不包含代码仓库中的 DOM、组件名或源码位置；网页 DOM 标注工具仍是独立产品方向。

## 识别方式与边界

Windows 通过 UI Automation、Mac 通过 Accessibility 获取目标应用公开的元素边界。截图前的元素探测在独立短进程中运行，超时会停止，避免目标应用阻塞截图界面。

图片识别完全在本机运行，使用颜色连通区域、边缘聚合和矩形包含关系。可识别卡片、图片区域和较明确的轮廓，不执行 OCR，也不提供物体语义识别。系统元素信息不可用时，图片识别和手动框选仍可使用。无需网络服务、账号或模型下载。

当前支持从多块屏幕中选择其中一块内的区域，不支持跨显示器拼接选区。混合缩放、多屏布局、受保护画面和不同浏览器的系统元素暴露情况仍需更多设备测试。单图最多 3200 万像素、单边不超过 32767，最多 1000 条批注，每条 10000 字。

## 平台验证状态

| 项目 | 状态 |
| --- | --- |
| Windows x64 | 已在 Windows 11 构建和测试；最低构建目标 Windows 10 1809+，未逐一测试旧系统 |
| Windows 截图 / UI Automation | 已针对本程序的真实测试窗口验证像素颜色、物理尺寸、按钮名称与边界 |
| 原图坐标 / 高 DPI 裁剪 | 已自动验证，包括 2 倍缩放的裁剪映射 |
| 中文界面 / 弹窗 / 导出窗口 | 已通过原生窗口测试和渲染截图检查 |
| Mac | 已实现平台代码和通用构建脚本；**未编译、未实机验证，也未生成可交付 Mac 包** |
| Mac ARM / Intel | 构建配置指定 arm64 与 x86_64；需由 Mac 构建结果确认 |
| 外部桌面自动化 | 本次工具受环境初始化错误影响，未能完成鼠标级全流程人工替代验收 |

Mac 目标为 macOS 14+，使用 ScreenCaptureKit。首次截图需要用户在系统设置中允许屏幕录制；系统元素识别需要单独的辅助功能授权，托盘菜单提供入口。没有这些权限时会提示，普通导入图片批注不依赖这些权限。

GitHub Actions 的 Mac 工作流已经写入仓库，**尚未推送、未运行**。有 Mac 构建环境后可先获得本地测试包，再完成权限、Retina、多屏与快捷键实测。公开发布时还需要产品所有者的 Apple 签名、公证流程。

## 构建

依赖：Qt **6.8.3**（qtbase、qtimageformats，动态库）、CMake 3.24+、Ninja、C++20 编译器。
Windows 本次使用 MinGW GCC 13.1.0；Mac 使用 Xcode Command Line Tools 和匹配的 Qt SDK。

Windows PowerShell 7 示例，CMake 在 PATH 中：

```powershell
./scripts/build-windows.ps1 -QtRoot C:/Qt/6.8.3/mingw_64 -Compiler C:/Qt/Tools/mingw1310_64/bin/g++.exe -Ninja C:/Tools/ninja.exe
./scripts/package-windows.ps1 -QtRoot C:/Qt/6.8.3/mingw_64 -CompilerBin C:/Qt/Tools/mingw1310_64/bin
```

打包脚本使用新的输出目录，避免覆盖已有成果；重复打包可传入新的 `-OutputDirectory`。发布包动态附带 Qt 和 MinGW 运行库，不需要安装 Qt、Python、Node 或 .NET。

Mac（当前脚本未经本机执行）：

```bash
export QT_ROOT="$HOME/Qt/6.8.3/macos"
bash scripts/build-macos.sh
```

输出通用 .app 和供本地测试的 .dmg，使用临时签名；这不是已经签名公证的公开发行版。

## 验证与许可

构建脚本默认运行三个 Windows 测试组：核心数据与算法、窗口标注交互、实际平台截图与系统元素。测试包含旧 WPF 项目的重新导入。界面与导出样本在 `artifacts/native-ui/`。

独立验证实际 JSON 导出（Python 环境安装 `jsonschema==4.26.0`）：

```text
python scripts/validate-exports.py
```

Qt 与 MinGW 的原始许可、版权声明存于 `packaging/licenses/`，随运行包交付；对应 Qt 源码保存在 `dist/native-sources/`。重新收集许可可运行 `scripts/collect-licenses.py`。对外发布应同时提供这些对应源码归档，详见 [第三方说明](packaging/THIRD-PARTY-NOTICES.md)。

平台接口参考：[Qt macOS 支持与通用构建](https://doc.qt.io/qt-6.8/macos.html)、[Apple 截图接口](https://developer.apple.com/documentation/screencapturekit/scscreenshotmanager)、[Qt 开源许可说明](https://www.qt.io/development/open-source-lgpl-obligations)。
