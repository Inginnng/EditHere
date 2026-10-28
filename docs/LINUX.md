# Linux 预览版

**简体中文** · [English](LINUX.en.md)

现有发布包的目标环境为 Ubuntu 24.04 x86_64、Qt 6.8.3。另提供面向麒麟 V10 等环境的 ARM64 实验构建入口，尚未通过麒麟实机验收。GNOME/KDE 的实际 Wayland 授权与多屏交互仍需实机验收。

编辑器、批注、布局调整、项目保存、复制导出和 CLI 沿用现有 Qt 实现。Linux 自动更新通过发布页手动下载。

## 运行

将 AppImage 与配套 CLI 放在同一目录：

```bash
chmod +x EditHere-linux-x86_64.AppImage edithere-cli
./EditHere-linux-x86_64.AppImage
./edithere-cli --version
./EditHere-linux-x86_64.AppImage --capture
sudo apt-get install tesseract-ocr tesseract-ocr-chi-sim tesseract-ocr-chi-tra
```

没有 FUSE 时可使用 `APPIMAGE_EXTRACT_AND_RUN=1 ./EditHere-linux-x86_64.AppImage`，CLI 同样支持此环境变量。AppImage 自带 Qt；OCR 使用系统 Tesseract，不联网。需要正常的桌面 session bus 和匹配桌面的 xdg-desktop-portal 后端；中文字体建议安装 fonts-noto-cjk。GNOME 没有托盘宿主时，普通启动保留编辑窗口入口，确认关闭后退出。CLI 也可通过 AppImage 的 `--cli <参数>` 调用。

## ARM64 / 麒麟 V10 实验构建

此入口适用于 `uname -m` 为 `aarch64`（也称 `arm64`）的 Linux，例如采用该架构的飞腾、鲲鹏设备；不涵盖 LoongArch 或 MIPS。它复用同一份 `app/`、Linux 平台后端、CLI 和打包流程，不另建麒麟专用程序。

在 GitHub Actions 的 [ARM64 工作流](../.github/workflows/native-linux-arm64.yml)中手动运行或查看相关 PR 构建，成功后下载包含 `EditHere-linux-aarch64.AppImage` 与 `edithere-cli` 的 artifact。该实验产物暂不接入正式 Release，也不代表所有麒麟 V10 版本和设备均可运行。

ARM64 工作流在 Ubuntu 24.04 ARM64 宿主上的 Debian 11 ARM64 容器内，从源码构建 Qt 6.8.3、PipeWire 1.0.4 客户端和 EditHere，以 glibc 2.31 为构建基线。[Qt 官方 ARM64 预编译包使用 Ubuntu 24.04 / glibc 2.39](https://doc.qt.io/qt-6.8/linux.html#supported-configurations)，不能直接用于 glibc 2.31 的麒麟环境。AppImage 携带 Qt 和相关库，但不携带系统 glibc；是否兼容仍以目标机器的实际版本和测试结果为准。

适配复用现有功能路径：ARM64 构建必须启用 PipeWire，保留 Wayland 屏幕流与手动长截图；视频后端保留压缩格式支持和 VAAPI 加速接口。实际硬件加速由系统驱动决定，Portal 与 PipeWire 服务仍由桌面提供。现有 x86_64 发布继续使用原来的 SDK、依赖和构建配置。

把实验包与同次构建的 CLI 放在同一目录后运行：

```bash
chmod +x EditHere-linux-aarch64.AppImage edithere-cli
./EditHere-linux-aarch64.AppImage
./edithere-cli --version
./EditHere-linux-aarch64.AppImage --capture
```

没有 FUSE 时，使用 `APPIMAGE_EXTRACT_AND_RUN=1 ./EditHere-linux-aarch64.AppImage`；CLI 对应为 `APPIMAGE_EXTRACT_AND_RUN=1 ./edithere-cli --version`。不要混用不同版本或不同架构的 AppImage 与 CLI。OCR 仍需从目标系统的软件源安装 Tesseract 和所需语言包。

请在 [issue #30](https://github.com/Inginnng/EditHere/issues/30) 提供以下四条命令的输出，并注明设备型号、所用 artifact 和具体失败步骤：

```bash
uname -m
cat /etc/os-release
getconf GNU_LIBC_VERSION
echo "$XDG_SESSION_TYPE"
```

实机验收至少覆盖截图、批注和布局调整、项目保存后重开、图片导出、GUI 启动后的 `./edithere-cli status`、中文 OCR、视频打开与播放，以及全局快捷键和托盘菜单。先验证 X11；Wayland 继续按预览能力验收，记录 Portal 授权、截图和多屏行为。缺少桌面服务、OCR 语言包或视频后端时，请保留报错信息，区分依赖缺失与程序缺陷。

## 平台行为

| 能力 | 推荐复用与实现 | 验收重点 |
| --- | --- | --- |
| 截图 | X11 使用 Qt QScreen；Wayland 优先使用 ScreenCast Portal/PipeWire | Wayland 先授权一个显示器，再由 EditHere 框选；按 Portal 几何信息对应所选屏幕，不关联原生元素。缺少 PipeWire 或 ScreenCast 显示器能力时回退 Screenshot Portal 的系统选区，仅支持普通截图 |
| 长截图 | X11 使用 XGetImage/XTest；Wayland 复用授权的 PipeWire 屏幕流 | 共用菜单、固定比例预览与纵向/横向手动拼接。X11 自动纵向滚动需要 XTEST 和可验证的原窗口进程；Wayland 当前仅手动滚动，显示器无法唯一对应时拒绝开始 |
| 全局快捷键 | Wayland 使用 GlobalShortcuts portal；X11 使用 Xlib 和 Qt 事件通知 | 桌面管理实际快捷键授权；不支持时在系统快捷键中绑定程序完整路径加 `--capture`。X11 检查冲突并兼容 Caps/Num Lock |
| 系统元素识别 | AT-SPI，沿用隔离探测子进程 | 依赖目标应用提供可访问性信息，限定遍历数量、深度与超时；不可用时回退图片检测 |
| 开机启动 | XDG Autostart `.desktop` 文件，Qt QStandardPaths 定位用户配置目录 | 路径转义、移动程序后修复、禁用仅删除本程序登记 |
| OCR | Tesseract TSV，异步运行、分条带坐标还原 | 明确提示语言包缺失、启动失败、超时和格式错误 |
| 分发 | linuxdeploy + Qt 插件生成 AppImage | 官方工具固定 SHA-256；附 CLI、桌面图标、文件关联元数据与许可；暂不提供 deb/Flatpak |

## 构建与打包

依赖支持 C++20 的 GCC、CMake 3.24+、Ninja、Qt 6.8.3、X11、XTest 和 AT-SPI 开发库。Wayland 长截图还需要可选的 PipeWire 1.0.4 及以上开发库；缺少时构建保留普通截图。启用 PipeWire 的 AppImage 会携带匹配版本的客户端主库、模块、SPA 插件与配置，打包环境须提供 `libpipewire-0.3-modules`、`libspa-0.2-modules` 和 `pipewire-bin`。x86_64 完整依赖清单见 `.github/workflows/native-linux.yml`；Qt Wayland 随基础 SDK 提供，不是 aqt 的可选模块。

现有 x86_64 构建：

```bash
export QT_ROOT=/path/to/Qt/6.8.3/gcc_64
dbus-run-session -- bash scripts/build-linux.sh
dbus-run-session -- xvfb-run -a build-linux/linux_platform_tests -platform xcb
python3 scripts/fetch-linuxdeploy.py
export LINUXDEPLOY="$PWD/.tools/linuxdeploy/linuxdeploy-x86_64.AppImage"
export LINUXDEPLOY_PLUGIN_QT="$PWD/.tools/linuxdeploy/linuxdeploy-plugin-qt-x86_64.AppImage"
bash scripts/package-linux.sh
dbus-run-session -- xvfb-run -a python3 scripts/check-linux-package.py dist/linux-*/EditHere-linux-x86_64.AppImage
```

ARM64 低 glibc 构建请使用 `.github/workflows/native-linux-arm64.yml` 中的容器与依赖配置，或在具有相同/更低目标 glibc 的 aarch64 构建环境中运行。先源码构建 Qt，再将同一 SDK 交给现有脚本：

```bash
bash scripts/build-qt-linux.sh
bash scripts/build-pipewire-linux.sh
export QT_ROOT="$PWD/.tools/qt/6.8.3"
export EDITHERE_PIPEWIRE_RUNTIME_ROOT="$PWD/.tools/pipewire/1.0.4"
export PKG_CONFIG_PATH="$EDITHERE_PIPEWIRE_RUNTIME_ROOT/lib/$(gcc -dumpmachine)/pkgconfig:$QT_ROOT/lib/pkgconfig"
export LD_LIBRARY_PATH="$EDITHERE_PIPEWIRE_RUNTIME_ROOT/lib/$(gcc -dumpmachine):$QT_ROOT/lib:${LD_LIBRARY_PATH:-}"
export EDITHERE_BUILD_DIR="$PWD/build-linux-aarch64"
export EDITHERE_PACKAGE_DIR="$PWD/dist/linux-aarch64-test"
export EDITHERE_GLIBC_MAX=2.31
dbus-run-session -- bash scripts/build-linux.sh
dbus-run-session -- xvfb-run -a "$EDITHERE_BUILD_DIR/linux_platform_tests" -platform xcb
python3 scripts/fetch-linuxdeploy.py --arch aarch64
export LINUXDEPLOY="$PWD/.tools/linuxdeploy/linuxdeploy-aarch64.AppImage"
export LINUXDEPLOY_PLUGIN_QT="$PWD/.tools/linuxdeploy/linuxdeploy-plugin-qt-aarch64.AppImage"
bash scripts/package-linux.sh
dbus-run-session -- xvfb-run -a python3 scripts/check-linux-package.py \
  "$EDITHERE_PACKAGE_DIR/EditHere-linux-aarch64.AppImage"
```

`build-qt-linux.sh` 的安装前缀可用 `EDITHERE_QT_PREFIX` 覆盖；随后须将 `QT_ROOT` 指向该前缀。已有合适的源码构建 Qt SDK 时可跳过此步。仅在较新系统上重新编译 EditHere、继续使用较新 glibc 构建的 Qt，不能降低整包的 glibc 要求。

Qt 构建脚本同时构建共享 LGPL FFmpeg 7.1.1，以保留视频功能；Debian 11 的系统 GStreamer 版本不足以构建 Qt 6.8 的视频后端。`build-pipewire-linux.sh` 只在私有目录准备客户端 SDK 和模块，不安装系统音频服务；前缀可用 `EDITHERE_PIPEWIRE_PREFIX` 覆盖，并同步修改上述环境变量。精确依赖源码、许可与配置随包保存在 `usr/share/doc/edithere/build-materials`。打包时检查所有 ELF 的架构；设置 `EDITHERE_GLIBC_MAX=2.31` 后还拒绝任何依赖更高 glibc 的文件。

默认打包到 `dist/linux-<版本>/`，已有目录时拒绝覆盖；可用 `EDITHERE_PACKAGE_DIR` 指定新目录，分别保留各架构产物。x86_64 构建随发布标签与相关 PR 触发：打标签时由发布工作流一并产出 AppImage，与其他平台的包一同挂到发布页；ARM64 实验构建单独运行并上传 artifact。专项测试在独立 DBus 会话模拟 Portal，真实 X11 通过 Xvfb 验证，不替代麒麟或 GNOME/KDE 会话验收。开机启动登记原始 AppImage 路径，不使用临时挂载路径；移动后保存设置可刷新。

官方接口资料：[Qt QScreen](https://doc.qt.io/qt-6.8/qscreen.html)、[ScreenCast Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)、[Screenshot Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html)、[GlobalShortcuts Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html)。
