# EditHere · Development notes

[简体中文](DEVELOPMENT.md) · **English**

[Back to the product overview](../README.en.md) · [User guide](USER-GUIDE.en.md) · [AI integration and CLI](AGENT-CLI.en.md) · [Changelog (Chinese)](../CHANGELOG.md)

Run all the commands below from the repository root; file paths are also relative to the repository root.

## Branches and technology stack

The current branch is `codex/native`, built with C++20 + Qt 6 Widgets. The retained `desktop` branch is Windows WPF 0.2 (`cf9c364`), and the `web` branch is Web 0.3 (`66548c5`).

## Version management

Routine fixes and small improvements only increase the last patch number, for example `0.8.0 → 0.8.1 → 0.8.2`. The middle number increases only when a larger feature stage is completed in one go and the release is explicit; it is no longer incremented for every round of development. The first number is reserved for explicit major releases, and existing version numbers and historical packages stay unchanged. A rollback applies only to the current implementation and does not freeze later versions; withdrawn numbers are not reused.

The single source of truth for the product version is `project(... VERSION ...)` in `CMakeLists.txt`, currently **0.10.5**. The runtime version and the Mac app information use that value automatically; after a successful link, `build/version.txt` is generated (on Mac, `build-macos/version.txt`), and the packaging scripts for both platforms name packages from it and ship `version.txt` with the package. After changing the version you must rebuild, so that an old binary is not labelled as a new version. The product version and the JSON formats are managed independently: the current format for minimal image feedback is defined by `feedback-minimal.schema.json` (an `objects` object structure since `0.9.0`; the parallel `annotations` / `changes` arrays of `0.8.21` are in `feedback-v0.7.schema.json` and can still be imported); `feedback-v1` / `v1.1` / `v2` describe the early **project document** formats, while a full image project uses `project-v3.schema.json`. Video feedback and video projects use `video-feedback-v1.schema.json` (`video-feedback-1`) and `video-project-v1.schema.json` (`video-project-1`) respectively. The file name represents the data format, not the app version.

## Release process

Pushing a `vX.Y.Z` tag triggers `.github/workflows/release.yml`: the Windows, macOS and Linux build jobs each produce a package, and the release job aggregates them and creates a GitHub Release. Updating the source version or building a local package does not mean the public latest release has been updated; GitHub's `/releases/latest` is decided by its stable Release metadata. In-app updates check stable releases on both GitHub `Inginnng/EditHere` and [Gitee `InnGing/EditHere`](https://gitee.com/InnGing/EditHere) and select the highest version. For the same version, a source with a compatible package and its SHA-256 file is preferred. If one source fails, the other source's result is kept and the app reports an incomplete check; if both fail, it reports both errors. Packages and checksums stay with the same source; the existing GitHub release workflow does not publish to Gitee automatically.

- **A release must ship complete packages.** Before publishing, the release job checks that `EditHere-win-x64-setup.exe`, `EditHere-win-x64.zip`, `EditHere-macos-universal.dmg` and `EditHere-linux-x86_64.AppImage` all exist and are non-empty; if any is missing, the workflow fails. A failed build job never produces a source-only release.
- Asset names contain no version number (the docs and the update check use `/releases/latest/download/<stable name>`), and each package ships with `SHA256SUMS.txt` and a per-file `.sha256`.
- Where the packages land after the release job downloads them is decided by `upload-artifact`, so do not predict it: only the path after the fixed prefix preceding the first wildcard survives, which puts `dist/linux-*/<file>` under `linux-<version>/` rather than `dist/linux-<version>/`. The release job moves those four kinds of file to the archive root by name, at any depth; do not go back to moving them by directory (REG-157).
- Release notes are taken first from `docs/releases/<version>.md` (a hand-written, user-facing description); when that file is missing, it falls back to the matching section of `CHANGELOG.md`. When preparing the notes, write a draft for the user to confirm before tagging.
- The notes are maintained in both languages, `docs/releases/<version>.md` and `docs/releases/<version>.en.md`, and release.yml assembles them into one body: a language switcher at the top, the Chinese notes, then the English notes under `## English`. A release page is a single field with no language switch, and the English half is a real heading rather than a collapsed block, because an anchor pointing into a closed block lands on nothing. `check-packaging.py` fails when only the Chinese page exists.

## Documentation languages

Interface text goes through Qt translations (see [Internationalisation and translations](i18n.md)); **the repository documentation does not**. It keeps one file per language, with `.en` on the English copy:

- Seven user-facing documents have an English version: `USER-GUIDE`, `LONG-CAPTURE`, `VIDEO-ANNOTATION`, `AGENT-CLI`, `AI-SETUP`, `LINUX` and `DEVELOPMENT`. Each pair carries a language switcher under its title pointing at the other half, and `README.en.md` links every English page. All three rules are checked by `check-packaging.py`'s `translated_docs`, so a new page has to be registered in `TRANSLATED_DOCS`.
- `CHANGELOG.md` stays Chinese-only: it is the single source of history, and keeping one history twice guarantees drift. What users see for a given version is the release page instead — the bilingual release notes above.
- `docs/releases/*.md` are release-page body fragments with no title and no switcher line: a relative link inside them resolves against the release URL, so the two languages are joined by release.yml, and `translated_docs` only guarantees that both halves exist.
- In the English pages, JSON field names, commands, file paths and platform identifiers stay as they are. Chinese text that is genuinely on screen — the subtitles in the demo animation, for instance — stays Chinese too and is not translated.
- Chinese only: `i18n.md`, `REGRESSIONS.md`, `PRERELEASE-0.9.9.md` and other maintainer records, plus historical release pages such as `docs/releases/0.9.x.md`.
- The Windows portable package still ships only the Chinese `AGENT-CLI.md` and `VIDEO-ANNOTATION.md`; `package-windows.ps1` rewrites their relative links to repository URLs, the switcher line included.
## Build and verification

For the Linux preview's system dependencies, build, AppImage packaging and X11/Wayland behaviour, see the [Linux guide](LINUX.en.md).

Dependencies: Qt **6.8.3** (the qtbase, qtimageformats and qtmultimedia shared libraries, plus qttranslations for the Qt-supplied translations the interface languages use), CMake 3.24+, Ninja and a C++20 compiler. Windows uses MinGW GCC 13.1.0; Mac CI is pinned to macOS 15 + Xcode 16.4 to match Qt 6.8.3; the Xcode 26 SDK removed the AGL framework that this Qt version links against. Video playback needs the Multimedia plugin and the matching decoder dependencies deployed with the package; users are not asked to install Qt separately.

Windows PowerShell 7, with CMake on PATH:

```powershell
./scripts/build-windows.ps1 -QtRoot C:/Qt/6.8.3/mingw_64 -Compiler C:/Qt/Tools/mingw1310_64/bin/g++.exe -Ninja C:/Tools/ninja.exe
./scripts/package-windows.ps1 -QtRoot C:/Qt/6.8.3/mingw_64 -CompilerBin C:/Qt/Tools/mingw1310_64/bin
```

The build script runs tests covering detection, core data, layout, interface, inline text, canvas interaction, settings, localisation, the guide, autostart, the startup flow, updates, text recognition, capture sessions and the CLI; Windows additionally includes platform tests, and the current CTest output is authoritative. Export samples and window rendering screenshots are stored in `artifacts/native-ui/`. After installing `jsonschema==4.26.0`, run `python scripts/validate-exports.py` to validate exports independently; the macOS build (`.github/workflows/native-macos.yml`) runs it automatically on the same artifacts, and the build fails when a schema does not match a real export. After changing interface text, also run `python scripts/check-translations.py --qt <Qt installation directory>`; see [internationalisation and translation (Chinese)](i18n.md) for details. Packaging only writes to a new directory; to package again, pass a new `-OutputDirectory`.

Besides the Qt tests, CTest registers two toolchain-independent checks: `packaging` (`python scripts/check-packaging.py`) verifies the installer script encoding, the NSIS macros and an actual compile, the portable archive structure, the release asset names and checksum files, the single version source, the Qt translation deployment, the encoding and placeholders of the text recognition bridge script, the one-to-one correspondence between `.ts` files and the precompiled `.qm` files, and the consistency of the feedback structure and export fields in the AI and command-line documentation and in the skill; `translations` (`scripts/check-translations.py`) is registered only when Qt Linguist can be found. For how these checks map to the project's past issues, see the [issue archive and regression tests (Chinese)](REGRESSIONS.md); **when you fix a new problem, record it on that page and add a check that will catch a recurrence**.

Contributor consent is verified by `.github/workflows/cla.yml`, implemented in `scripts/cla-check.cjs` with configuration in `.github/cla/config.json`: the workflow checks out only the trusted base and never runs code from the pull request, requires the confirming account to be the contributor's own, and records the agreement version and the SHA-256 digest of `cla/v2.md` on a separate `cla-signatures` branch. After changing any CLA-related file, run `node --test tests/cla_check_test.cjs` locally; the read-only `.github/workflows/cla-tests.yml` runs the same test on pull requests. See the [contributor licence agreement (Chinese)](../CLA.md) for the agreement and the enablement steps.

## Capture flow and text recognition

A screenshot is no longer “release to annotate”. `Overlay` (one borderless, always-on-top window per screen) handles selection and interaction; once the selection is settled, the `CaptureToolbar` inside it offers the actions and `Controller` handles each one separately. Only “Annotate” goes through `completeCapture` into `Editor`.

| File | Responsibility |
| --- | --- |
| `app/overlay.{h,cpp}` | Selection drawing and adjustment, candidate blocks, colour picking, and the host of the toolbar; it only emits signals and performs no actions |
| `app/capturetoolbar.{h,cpp}` | The toolbar controls and the “More” menu (fixed ratio, corners/border/shadow, recognition language, history and selection) |
| `app/capturesession.{h,cpp}` | Ratio and size conversion, style compositing in `composeCapture()`, and the cross-session `CaptureHistory` |
| `app/pinwindow.{h,cpp}` | The pinned image window: dragging, zooming, transparency and the context menu |
| `app/ocr.{h,cpp}` + `app/ocr_mac.mm` | The recognition engine wrapper: banding, coordinate restoration and result parsing; macOS uses Vision |
| `app/ocrdialog.{h,cpp}` | The recognition result window: a line-by-line list linked to highlighting on the source image |
| `app/ocrbridge.ps1` | The Windows-side system OCR bridge script, embedded as a Qt resource |

On Windows the choice of recognition depends on the toolchain: **release packages are built with MinGW, which has no C++/WinRT**, so the app does not call `Windows.Media.Ocr` directly; instead it hands system OCR to a PowerShell script passed in with `-EncodedCommand` (Base64 UTF-16LE). This chain has three hard constraints; keep them in mind when making changes:

1. **The script must be pure ASCII with no BOM.** With a BOM it decodes into stray characters at the top of the script; non-ASCII text is unreliable through the encode/decode chain. `scripts/check-packaging.py::ocr_bridge` blocks both cases.
2. **Paths must use backslashes.** The Windows Runtime file APIs reject `C:/...` and report `UNABLE_TO_MASK_PATH`; `prepareOcrBridge()` converts them with `QDir::toNativeSeparators`.
3. **Do not read the child process output through a pipe.** QProcess cannot create pipes in some restricted environments, so the bridge writes its result to a temporary JSON file and redirects the process output to a file instead of `readAllStandardOutput()`.

The macOS side uses Vision directly (`app/ocr_mac.mm`, linked with `-framework Vision`); both sides are **offline, upload nothing and add no third-party dependencies**.

The Windows installer additionally depends on NSIS 3.x. You can extract the official NSIS ZIP and point to `makensis.exe` directly, with no global installation. Run this after the portable archive has been produced:

```powershell
./scripts/package-installer.ps1 -NsisCompiler C:/Tools/nsis-3.x/makensis.exe
```

Replace the NSIS path in the example with the real location. The 0.8.21 installer output is `dist/EditHere-0.8.21-win-x64-setup.exe`. The installer installs for the current user into `%LOCALAPPDATA%\Programs\EditHere`, using the current user's Start menu, uninstall registration and file associations, without requesting administrator privileges. Launch at login and PATH are selected by default on a new installation, and the desktop shortcut is optional; upgrades preserve the startup option based on the existing current-user Run entry. Uninstalling removes the packaged files according to the installed-file list and keeps the user's settings and projects.

In-app Windows updates and manual installation share the NSIS payload and the `maintain.ps1` maintenance transaction. Portable updates use `/S /UPDATE /PORTABLE /D=<original directory>`; `/D=` must come last and remain unquoted. Older callers remain compatible. The original installer runs directly, stages the payload, then backs up and publishes manifest-owned files individually. The target root and unknown files stay in place. One maintenance worker handles the target lock, manifest and hash checks, space preflight, integration snapshots, runtime version validation and interrupted-operation recovery. Portable mode skips system integration. In 0.10.4, in-app updates use a token-validated READY/ACK handoff and keep the application running until the worker is ready. Schema2 file transactions retain schema1 recovery for historical operations. Final results and logs live outside the target; old files are reclaimed only when safe. The maintainer protocol is described in [Windows installation and maintenance (Chinese)](INSTALLATION.md). Downloads still use Qt's temporary directory, atomic writes and the same-named SHA-256 checksum. macOS currently offers manual downloads only.

Qt and NSIS remain in place, with file and registration changes coordinated by one maintenance worker. `tests/installer_transaction_test.py` validates real NSIS in-place execution, destination, argument handling and isolated HKCU integration, using `NSIS_MAKENSIS` and `EDITHERE_UPDATE_STUB`. `tests/maintenance_test.py` uses real file transactions and a JSON integration fixture to exercise recovery, concurrency and uninstallation. `tests/installer_ui_flow_test.py` drives real first-install and repeat-install pages on a private hidden Windows desktop and checks page order and titles. `tests/installer_shell_notification_test.py` runs production notification code from a directory containing native NSIS System.dll, checking managed references and nonfatal warnings. Harmless-stub fixtures explicitly skip the runtime version probe. Test scopes require an isolation marker and cannot alter the developer's real PATH, associations or shortcuts. These checks run in the Windows release workflow. `tests/installer_package_acceptance.py` accepts complete new and old Windows packages through `EDITHERE_ACCEPT_PACKAGE` and `EDITHERE_ACCEPT_OLD_PACKAGE`, using real NSIS, isolated HKCU and the actual CLI version probe. Isolated acceptance and final-package acceptance on a clean system are recorded separately.

Mac build (not yet accepted on real hardware):

```bash
export QT_ROOT="$HOME/Qt/6.8.3/macos"
bash scripts/build-macos.sh
```

The script produces a universal `.app` and a local-test `.dmg`, using an ad-hoc signature. The CLI is deployed alongside it at `EditHere.app/Contents/MacOS/edithere-cli`, and the DMG provides a drag-to-Applications entry point. Before building it checks whether the selected SDK contains the AGL that Qt 6.8.3 needs, and passes that same SDK explicitly to CMake. The `build-environment.txt` in the CI artifacts records the actual Xcode, SDK, Qt and temporary directory; `test-results.xml` and `LastTest.log` show the specific failing tests. macOS limits Unix socket path lengths by the byte count of the full path, so the isolation tests use short names throughout and validate the endpoint length under the current temporary directory. The public repository's [macOS build runs](https://github.com/Inginnng/EditHere/actions/workflows/native-macos.yml) keep the test results and DMG for the corresponding build; Screen Recording permission, Accessibility permission and multi-display capture still need acceptance testing on real hardware.

For the CLI's arguments, JSON responses and user-completion protocol, see [AI integration and CLI](AGENT-CLI.en.md). The companion skill lives in `skills/edithere` and can be validated with `skill-creator`'s `quick_validate.py`; that validation does not replace CLI behaviour tests or user-interaction acceptance.

The licences and copyright notices for Qt and MinGW live in `packaging/licenses/` and ship with the runtime package; the corresponding Qt source is stored in `dist/native-sources/`. Public distribution must also provide the corresponding source archives; see the [third-party notices (Chinese)](../packaging/THIRD-PARTY-NOTICES.md).
