# EditHere distribution layout

[简体中文](DISTRIBUTION.md) · **English**

[Product introduction](../README.en.md) · [Development](DEVELOPMENT.en.md) · [Licensing and third-party materials](../LICENSING.md)

The Windows, macOS and Linux applications share the C++20 / Qt source in `app/`. Platform backends and packaging scripts produce their different packages. `web/` contains the browser implementation; `connector/`, `skills/` and `trae-plugin/` contain the corresponding AI integrations. Keep one source tree rather than copying it for each platform, and do not distribute build caches as installers.

## Directory responsibilities

| Directory | Contents and purpose |
| --- | --- |
| `app/`, `assets/`, `schema/`, `third_party/` | Shared desktop source, brand assets, data formats and vendored source dependencies |
| `web/` | Browser source, standalone HTML, MCP Apps server and embedded HTML, local compatibility connector and web Skill |
| `scripts/`, `packaging/`, `.github/workflows/` | Existing build, package, installer and CI entry points |
| `connector/`, `skills/`, `trae-plugin/` | Sources for the desktop MCP, desktop Skill and Trae plugin |
| `build/`, `build-macos/`, `build-linux/`, `build-*/` | Development build caches; kept in place and excluded from distribution |
| `artifacts/` | Tests, screenshots and acceptance records; not distributed as a whole |
| `dist/` | Existing packaging output, which may include historical and experimental packages |
| A new directory selected for `prepare-distribution.py` | A local collection arranged by platform and version |

Existing paths remain valid. Build scripts locate the repository through `scripts/..`, and CMake, CI and tests reference the current directories. Preparing a distribution does not move them or replace installed applications or Skills.

## Prepare the collection

Run from the repository root. Python 3.10+ is required. Standalone builds support Node.js 18+; building MCP sources requires Node.js 20+. After changing MCP or shared page sources, run `npm --prefix web/mcp ci` and `node web/mcp/build.mjs` first. Downloaded Web Agent bundles need no npm install.

```powershell
python scripts/prepare-distribution.py --output ../Distribution --include-history
```

The destination must not already exist. Without `--output`, the destination is `dist/distribution-<product-version>-<short-git-hash>/`. The script rebuilds `web/EditHere.html` and checks source/artifact hashes in `web/mcp/build-info.json`, rejecting stale MCP bundles before collecting files. Existing native packages are read and copied, never moved or deleted; an existing destination is not overwritten.

Useful options:

- `--include-history`: also collects existing historical archives, installers and AppImages. It scans defined packaging sources, excluding extracted test trees, build caches and the repository as a whole.
- `--native-dir <directory>`: adds a native packaging source and may be repeated. Use original versioned Windows/macOS build artifacts; Linux requires an adjacent `version.txt` or a `linux-<version>` directory. Final Release assets with unversioned stable names cannot be assigned to the current version directly; the script does not guess or rewrite their versions.
- `--require-all`: requires the current Windows portable ZIP, Windows installer, macOS DMG and Linux AppImage before proceeding. The default allows an incomplete collection and explicitly marks missing packages as pending in their directories and the manifest.
- `--web-only`: collects only the standalone web package and the Web Agent package, without scanning native packages, for local checks and the release workflow; `--channel release` marks the packages and the build stamp as release builds and is only valid together with this option.

The result has this structure:

```text
Distribution/
  README.md
  manifest.json
  SHA256SUMS.txt
  web/<product-version>-<short-git-hash>/
    ...-web.zip
    ...-web-agent.zip
  windows/<product-version>/
  macos/<product-version>/
  linux/<product-version>/
  integrations/
    desktop-mcp/
    desktop-skill/
    trae-plugin/
  archive/<platform>/<original-version>/<source-name+hash>/
```

`web` separates the standalone browser package from the Agent package. The web-agent allowlist includes the MCP server bundle, embedded official App SDK HTML, configuration template, licenses, installer and Skill; it excludes node_modules, tokens, sessions and personal paths. MCP requires Node.js 20+ and a host that supports MCP Apps; the same package retains browser compatibility mode. Packages in `integrations` use their own versions, rather than treating an MCP or plugin version as the desktop product version. Historical packages retain their original version and source, are marked `historical-unverified`, and do not fill gaps in the current release.

A shipped package can be checked on its own, away from the repository: `node scripts/web-package-check.cjs <extracted directory or ZIP path> [test image]` reads only files inside the package and detects the package type itself. The standalone package is checked for a root-level single-file HTML, its licenses and the absence of any MCP runtime; the web-agent package additionally checks the three artifact hashes recorded in `web/mcp/build-info.json`, the channel wording of `README.md`, the Skill installation (a repeated install must be refused) and the MCP stdio flow (hosts with and without the UI capability, plus reading the embedded HTML back). When no test image is found the MCP section is reported as skipped rather than passed. The result only proves the package is self-consistent and works locally; it does not replace acceptance on the target host.

Use the generated `manifest.json` to check the collected versions, sources, SHA-256 hashes, availability and missing packages. A manifest and checksums identify the collected bytes; they do not replace runtime, signing, notarization or platform acceptance checks. The source product version comes from `CMakeLists.txt`. Updating that version does not rename an older binary package into a new release. For example, source version `0.10.5` still leaves a platform pending when its matching package is absent.

## Original platform artifacts

`<version>` means the version recorded by a successful build, not a manually chosen filename.

| Platform | Existing build / package entry points | Default original artifacts | Stable public download names |
| --- | --- | --- | --- |
| Windows x64 | `scripts/build-windows.ps1` → `scripts/package-windows.ps1` → `scripts/package-installer.ps1` | `dist/EditHere-<version>-win-x64.zip`, `dist/EditHere-<version>-win-x64-setup.exe` | `EditHere-win-x64.zip`, `EditHere-win-x64-setup.exe` |
| macOS Universal | `scripts/build-macos.sh`, including tests and DMG packaging | `dist/EditHere-<version>-macos-universal.dmg` | `EditHere-macos-universal.dmg` |
| Linux x86_64 | `scripts/build-linux.sh` → `scripts/package-linux.sh` | `dist/linux-<version>/EditHere-linux-x86_64.AppImage`, its `.sha256`, and `edithere-cli` | `EditHere-linux-x86_64.AppImage`, `edithere-cli` |
| Web | `node web/build.cjs`; MCP uses `node web/mcp/build.mjs`; the collection script prepares both packages (`--web-only` builds just them) | `web/EditHere.html`, `web/EditHere.mcp.html`, `web/mcp/server.bundle.cjs` and distribution ZIPs | `EditHere-web.zip` (standalone web edition), `EditHere-web-agent.zip` (Agent package) |

The Windows ZIP keeps the application, DLLs and plugin directories at its root. Do not add an enclosing platform directory: the desktop updater extracts the ZIP directly into the installation directory. The macOS CLI lives at `EditHere.app/Contents/MacOS/edithere-cli`. The separate Linux `edithere-cli` is a wrapper for its accompanying AppImage and must not be mixed with a different version.

Desktop packaging reads `version.txt`, generated after a successful link. See [Development](DEVELOPMENT.en.md) for the version source, commands and tests. The default toolchain requires Qt 6.8.3, CMake 3.24+, Ninja and a C++20 compiler. Windows releases use MinGW GCC 13.1.0. macOS targets 14+ and `arm64;x86_64`, with CI pinned to Xcode 16.4. Linux targets Ubuntu 24.04 x86_64 and additionally needs platform dependencies and linuxdeploy; see the [Linux guide](LINUX.en.md). Preparing a collection on Windows does not build macOS or Linux installers automatically.

## Local preparation and public releases

The collection script only prepares local files. It does not create or push tags, publish GitHub / Gitee Releases, register startup tasks or update installed applications.

The existing `.github/workflows/release.yml` builds and collects the four native package types and the two web ZIPs after a version tag triggers it, then publishes them under the stable names in the table (the web ZIPs come from the `build-web` job running `--web-only --channel release`; the line endings of embedded assets and of the shipped legal texts are pinned in `.gitattributes` so the same source does not produce different bytes per platform). Documentation and the Windows updater depend on these names; this layout does not change them. Before publishing, also verify the tag against the built version, all platform artifacts, tests, licensing and corresponding source materials. A historical failed release, experimental package or matching filename does not replace those checks.

Signing, macOS notarization and physical-device acceptance, and Linux preview status remain as described in the current [product documentation](../README.en.md) and build records. See [Third-party notices](../packaging/THIRD-PARTY-NOTICES.md) for Qt, FFmpeg and corresponding source requirements. Preparing a collection does not claim public distribution acceptance is complete.
