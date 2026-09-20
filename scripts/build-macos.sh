#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd)"
: "${QT_ROOT:?Set QT_ROOT to the Qt 6.8 macos SDK directory}"
build_path="$project_root/build-macos"
# Resolve the same SDK for preflight and CMake; a cached/default SDK must not
# silently override the Qt-compatible Xcode selected by CI or a local build.
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
if [[ ! -d "$sdk_path/System/Library/Frameworks/AGL.framework" ]]; then
  printf '%s\n' "Qt 6.8.3 requires AGL.framework, which is absent from the selected SDK: $sdk_path" \
    "Select Xcode 16.4 with DEVELOPER_DIR before building; do not use the Xcode 26 SDK for this Qt release." >&2
  exit 1
fi
mkdir -p "$build_path"
{
  xcodebuild -version
  printf 'SDK: %s\nQt: %s\nTMPDIR: %s\n' "$sdk_path" "$QT_ROOT" "${TMPDIR:-/tmp}"
  "$QT_ROOT/bin/qmake" -query QT_VERSION
  cmake --version
} | tee "$build_path/build-environment.txt"
cmake -S "$project_root" -B "$build_path" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_ROOT" -DCMAKE_OSX_SYSROOT="$sdk_path" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build "$build_path" --parallel
export H2D_TEST_ARTIFACTS="$project_root/artifacts/native-ui"
ctest --test-dir "$build_path" --output-on-failure --output-junit "$build_path/test-results.xml"
if [[ ! -f "$project_root/schema/feedback-v0.7.schema.json" ]]; then printf '%s\n' "The current feedback schema is missing." >&2; exit 1; fi
build_version="$(cat "$build_path/version.txt")"
if [[ ! "$build_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then printf '%s\n' "Invalid build version." >&2; exit 1; fi
output="$project_root/dist/EditHere-$build_version-macos-universal"
if [[ -e "$output" || -e "$output.dmg" ]]; then printf '%s\n' "Output folder or DMG already exists: $output" >&2; exit 1; fi
mkdir -p "$output"
cp -R "$build_path/EditHere.app" "$output/"
cp "$build_path/version.txt" "$output/"
cp "$build_path/edithere-cli" "$output/EditHere.app/Contents/MacOS/edithere-cli"
"$QT_ROOT/bin/macdeployqt" "$output/EditHere.app" -always-overwrite -executable="$output/EditHere.app/Contents/MacOS/edithere-cli"
ln -s /Applications "$output/Applications"
cp -R "$project_root/skills" "$output/"
sed -e 's|../skills/|skills/|g' -e 's|../schema/|schema/|g' -e 's|../README.md|https://github.com/Inginnng/EditHere|g' -e 's|USER-GUIDE.md|https://github.com/Inginnng/EditHere/blob/codex/native/docs/USER-GUIDE.md|g' "$project_root/docs/AGENT-CLI.md" > "$output/AGENT-CLI.md"
cp -R "$project_root/packaging/licenses" "$output/"
cp -R "$project_root/schema" "$output/"
cp "$project_root/packaging/使用说明.txt" "$project_root/packaging/THIRD-PARTY-NOTICES.md" "$output/"
cp "$project_root/LICENSE" "$project_root/LICENSING.md" "$project_root/COMMERCIAL-LICENSE.md" "$project_root/NOTICE" "$output/"
sed 's|packaging/THIRD-PARTY-NOTICES.md|THIRD-PARTY-NOTICES.md|g' "$project_root/LICENSING.md" > "$output/LICENSING.md"
# Local test signing only. Distribution signing/notarization needs the product owner's Apple identity.
codesign --force --deep --sign - "$output/EditHere.app"
hdiutil create -volname EditHere -srcfolder "$output" -format UDZO "$output.dmg"
printf '%s\n' "Local test build: $output.dmg"
