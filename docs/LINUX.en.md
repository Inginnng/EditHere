# Linux preview

[简体中文](LINUX.md) · **English**

The target environment is Ubuntu 24.04 x86_64 with Qt 6.8.3. Linux builds, system interfaces and AppImage packaging are in place; real Wayland authorisation on GNOME/KDE and multi-display interaction still need acceptance testing on real hardware.

The editor, annotations, layout adjustment, project saving, copy and export, and the CLI reuse the existing Qt implementation. Linux updates are downloaded manually from the releases page.

## Running

Put the AppImage and the companion CLI in the same directory:

```bash
chmod +x EditHere-linux-x86_64.AppImage edithere-cli
./EditHere-linux-x86_64.AppImage
./edithere-cli --version
./EditHere-linux-x86_64.AppImage --capture
sudo apt-get install tesseract-ocr tesseract-ocr-chi-sim tesseract-ocr-chi-tra
```

Without FUSE you can use `APPIMAGE_EXTRACT_AND_RUN=1 ./EditHere-linux-x86_64.AppImage`; the CLI supports the same environment variable. The AppImage bundles Qt; OCR uses the system Tesseract and is offline. A working desktop session bus and an xdg-desktop-portal backend matching the desktop are required; installing fonts-noto-cjk is recommended for Chinese fonts. On GNOME, when there is no tray host, an ordinary launch keeps the editor-window entry point and exits once you confirm closing. The CLI can also be invoked through the AppImage with `--cli <arguments>`.

## Platform behaviour

| Capability | Recommended reuse and implementation | Acceptance focus |
| --- | --- | --- |
| Screenshot | X11 uses Qt QScreen; Wayland prefers the ScreenCast Portal/PipeWire | On Wayland, authorise one monitor first, then draw the selection in EditHere; the Portal geometry maps to the selected screen, with no association to native elements. Without PipeWire or ScreenCast monitor support, it falls back to the Screenshot Portal's system region selection and supports ordinary screenshots only |
| Scrolling capture | X11 uses XGetImage/XTest; Wayland reuses the authorised PipeWire screen stream | A shared menu, a fixed-scale preview, and manual vertical/horizontal stitching. Automatic vertical scrolling on X11 needs XTEST and a verifiable original-window process; Wayland currently supports manual scrolling only and refuses to start when the monitor cannot be mapped uniquely |
| Global shortcuts | Wayland uses the GlobalShortcuts portal; X11 uses Xlib and Qt event notification | The desktop manages the actual shortcut authorisation; when unsupported, bind the app's full path plus `--capture` in the system shortcut settings. X11 checks for conflicts and tolerates Caps/Num Lock |
| System element recognition | AT-SPI, reusing the isolated probe subprocess | Depends on the target app exposing accessibility information; traversal count, depth and timeout are bounded; falls back to image detection when unavailable |
| Launch at login | An XDG Autostart `.desktop` file, with Qt QStandardPaths locating the user configuration directory | Path escaping, repair after moving the app, and disabling removing only this app's registration |
| OCR | Tesseract TSV, run asynchronously, with banding and coordinate restoration | Explicitly reports a missing language pack, a failed launch, a timeout and a format error |
| Distribution | linuxdeploy + Qt plugin to produce the AppImage | Official tools are pinned by SHA-256; ships the CLI, a desktop icon, file-association metadata and licences; no deb/Flatpak yet |

## Build and packaging

Depends on GCC, CMake, Ninja, Qt 6.8.3 gcc_64, X11, XTest and the AT-SPI development libraries. Wayland scrolling capture also needs the optional PipeWire 1.0.4 or later development libraries; without them the build keeps ordinary screenshots. An AppImage with PipeWire enabled carries matching versions of the client main library, modules, SPA plugins and configuration, and the packaging environment must provide `libpipewire-0.3-modules`, `libspa-0.2-modules` and `pipewire-bin`. For the full dependency list see `.github/workflows/native-linux.yml`; Qt Wayland comes with the base SDK and is not an optional aqt module.

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

Packaging defaults to `dist/linux-<version>/` and refuses to overwrite an existing directory; use `EDITHERE_PACKAGE_DIR` to specify a new directory. Linux builds are triggered by release tags and relevant PRs: tagging produces the AppImage as part of the release workflow, attached to the releases page alongside the other platforms' packages. The dedicated tests simulate the Portal in an isolated DBus session and verify real X11 through Xvfb; they do not replace acceptance in a GNOME/KDE session. Launch at login records the original AppImage path, not a temporary mount path; saving the settings after moving refreshes it.

Official interface references: [Qt QScreen](https://doc.qt.io/qt-6.8/qscreen.html), [ScreenCast Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html), [Screenshot Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html), [GlobalShortcuts Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html).
