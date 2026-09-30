# Linux 预览版

目标环境为 Ubuntu 24.04 x86_64、Qt 6.8.3。已接入 Linux 构建、系统接口和 AppImage 打包；GNOME/KDE 的实际 Wayland 授权与多屏交互仍需实机验收。

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

## 平台行为

| 能力 | 推荐复用与实现 | 验收重点 |
| --- | --- | --- |
| 截图 | X11 使用 Qt QScreen；Wayland 使用 Screenshot portal | Wayland 先由系统授权/选区，再在应用中审阅；Portal 不返回桌面坐标，不关联原生元素。X11 混合缩放时禁用原生坐标探测，保留图片检测 |
| 全局快捷键 | Wayland 使用 GlobalShortcuts portal；X11 使用 Xlib 和 Qt 事件通知 | 桌面管理实际快捷键授权；不支持时在系统快捷键中绑定程序完整路径加 `--capture`。X11 检查冲突并兼容 Caps/Num Lock |
| 系统元素识别 | AT-SPI，沿用隔离探测子进程 | 依赖目标应用提供可访问性信息，限定遍历数量、深度与超时；不可用时回退图片检测 |
| 开机启动 | XDG Autostart `.desktop` 文件，Qt QStandardPaths 定位用户配置目录 | 路径转义、移动程序后修复、禁用仅删除本程序登记 |
| OCR | Tesseract TSV，异步运行、分条带坐标还原 | 明确提示语言包缺失、启动失败、超时和格式错误 |
| 分发 | linuxdeploy + Qt 插件生成 AppImage | 官方工具固定 SHA-256；附 CLI、桌面图标、文件关联元数据与许可；暂不提供 deb/Flatpak |

## 构建与打包

依赖 GCC、CMake、Ninja、Qt 6.8.3 gcc_64、X11 和 AT-SPI 开发库。完整依赖清单见 `.github/workflows/native-linux.yml`；Qt Wayland 随基础 SDK 提供，不是 aqt 的可选模块。

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

默认打包到 `dist/linux-<版本>/`，已有目录时拒绝覆盖；可用 `EDITHERE_PACKAGE_DIR` 指定新目录。Linux 构建随发布标签与相关 PR 触发：打标签时由发布工作流一并产出 AppImage，与其他平台的包一同挂到发布页。专项测试在独立 DBus 会话模拟 Portal，真实 X11 通过 Xvfb 验证，不替代 GNOME/KDE 会话验收。开机启动登记原始 AppImage 路径，不使用临时挂载路径；移动后保存设置可刷新。

官方接口资料：[Qt QScreen](https://doc.qt.io/qt-6.8/qscreen.html)、[Screenshot portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html)、[GlobalShortcuts portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html)。
