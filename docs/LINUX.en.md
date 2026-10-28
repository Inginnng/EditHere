# Linux preview

[简体中文](LINUX.md) · **English**

The existing release package targets Ubuntu 24.04 x86_64 with Qt 6.8.3. An experimental ARM64 build path is also available for environments such as Kylin V10; it has not passed acceptance testing on Kylin hardware. Real Wayland authorisation on GNOME/KDE and multi-display interaction still need acceptance testing on real hardware.

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

## Experimental ARM64 / Kylin V10 builds

This path targets Linux machines reporting `aarch64` (also called `arm64`) from `uname -m`, including Phytium and Kunpeng devices using this architecture. It does not cover LoongArch or MIPS. It reuses the same `app/` sources, Linux backend, CLI and packaging flow rather than maintaining a separate Kylin application.

Run the [ARM64 workflow](../.github/workflows/native-linux-arm64.yml) manually in GitHub Actions, or inspect a relevant PR build. After a successful build, download the artifact containing `EditHere-linux-aarch64.AppImage` and `edithere-cli`. These experimental artifacts are not included in the regular Release and do not establish compatibility with every Kylin V10 version or device.

The ARM64 workflow builds Qt 6.8.3, the PipeWire 1.0.4 client and EditHere from source inside a Debian 11 ARM64 container on an Ubuntu 24.04 ARM64 host, using glibc 2.31 as its build baseline. [Qt's official ARM64 binaries use Ubuntu 24.04 / glibc 2.39](https://doc.qt.io/qt-6.8/linux.html#supported-configurations) and cannot be used directly on a Kylin environment with glibc 2.31. AppImage bundles Qt and related libraries, but not the system glibc; compatibility still depends on the target machine's actual versions and test results.

ARM64 builds must enable PipeWire to retain the existing Wayland screen stream and manual long-capture path. The video backend retains compressed-format and HTTPS-source support and VAAPI interfaces; actual acceleration depends on the system driver. The desktop still supplies Portal and PipeWire services. Existing x86_64 releases continue using their original SDK, dependencies and build configuration.

Put the experimental AppImage and the CLI from the same build in one directory:

```bash
chmod +x EditHere-linux-aarch64.AppImage edithere-cli
./EditHere-linux-aarch64.AppImage
./edithere-cli --version
./EditHere-linux-aarch64.AppImage --capture
```

Without FUSE, use `APPIMAGE_EXTRACT_AND_RUN=1 ./EditHere-linux-aarch64.AppImage`; for the CLI, use `APPIMAGE_EXTRACT_AND_RUN=1 ./edithere-cli --version`. Keep the AppImage and CLI from the same version and architecture together. OCR still requires Tesseract and the desired language packs from the target system's package repositories.

In [issue #30](https://github.com/Inginnng/EditHere/issues/30), provide the output of these four commands, your device model, the artifact tested and the exact steps that failed:

```bash
uname -m
cat /etc/os-release
getconf GNU_LIBC_VERSION
echo "$XDG_SESSION_TYPE"
```

Acceptance on real hardware should cover screenshots, annotations and layout changes, saving and reopening a project, image export, `./edithere-cli status` after starting the GUI, Chinese OCR, opening and playing video, global shortcuts and the tray menu. Test X11 first. Wayland remains a preview: record Portal authorisation, screenshot and multi-display behaviour. Preserve errors when desktop services, OCR language packs or video backends are missing, so dependency issues can be distinguished from application defects.

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

Depends on a C++20-capable GCC, CMake 3.24+, Ninja, Qt 6.8.3, X11, XTest and the AT-SPI development libraries. Wayland scrolling capture also needs the optional PipeWire 1.0.4 or later development libraries; without them the build keeps ordinary screenshots. An AppImage with PipeWire enabled carries matching versions of the client main library, modules, SPA plugins and configuration, and the packaging environment must provide `libpipewire-0.3-modules`, `libspa-0.2-modules` and `pipewire-bin`. For the full x86_64 dependency list see `.github/workflows/native-linux.yml`; Qt Wayland comes with the base SDK and is not an optional aqt module.

Existing x86_64 build:

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

For an ARM64 build with an older glibc baseline, use the container and dependency setup in `.github/workflows/native-linux-arm64.yml`, or an aarch64 build environment with the same or an older glibc than the target. Build Qt from source first, then pass that SDK to the existing scripts:

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

Override `build-qt-linux.sh`'s install prefix with `EDITHERE_QT_PREFIX` if needed, and point `QT_ROOT` at that prefix afterwards. Skip this step when a suitable Qt SDK built from source is already available. Rebuilding only EditHere while retaining a Qt SDK built against a newer glibc does not lower the package's glibc requirement.

The Qt script also builds shared LGPL FFmpeg 7.1.1 to retain video support; Debian 11's GStreamer is too old for Qt 6.8's multimedia backend. `build-pipewire-linux.sh` installs only a private client SDK and modules, without installing system audio services. Override its prefix with `EDITHERE_PIPEWIRE_PREFIX` and adjust the environment variables above accordingly. Exact dependency sources, licenses and build configuration accompany the package under `usr/share/doc/edithere/build-materials`. Packaging checks every ELF's architecture; `EDITHERE_GLIBC_MAX=2.31` also rejects files requiring a newer glibc.

Packaging defaults to `dist/linux-<version>/` and refuses to overwrite an existing directory; use `EDITHERE_PACKAGE_DIR` to keep each architecture's output separately. x86_64 builds are triggered by release tags and relevant PRs: tagging produces the AppImage as part of the release workflow, attached to the releases page alongside the other platforms' packages. Experimental ARM64 builds run separately and upload artifacts. The dedicated tests simulate the Portal in an isolated DBus session and verify real X11 through Xvfb; they do not replace acceptance in a Kylin or GNOME/KDE session. Launch at login records the original AppImage path, not a temporary mount path; saving the settings after moving refreshes it.

Official interface references: [Qt QScreen](https://doc.qt.io/qt-6.8/qscreen.html), [ScreenCast Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html), [Screenshot Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html), [GlobalShortcuts Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html).
