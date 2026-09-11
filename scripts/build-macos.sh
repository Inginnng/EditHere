#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd)"
: "${QT_ROOT:?Set QT_ROOT to the Qt 6.8 macos SDK directory}"
build_path="$project_root/build-macos"
cmake -S "$project_root" -B "$build_path" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_ROOT" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build "$build_path" --parallel
export H2D_TEST_ARTIFACTS="$project_root/artifacts/native-ui"
ctest --test-dir "$build_path" --output-on-failure
output="$project_root/dist/Help2Design-Native-0.5.0-macos-universal"
if [[ -e "$output" ]]; then printf '%s\n' "Output already exists: $output" >&2; exit 1; fi
mkdir -p "$output"
cp -R "$build_path/Help2Design.app" "$output/"
"$QT_ROOT/bin/macdeployqt" "$output/Help2Design.app" -always-overwrite
cp -R "$project_root/packaging/licenses" "$output/"
cp -R "$project_root/schema" "$output/"
cp "$project_root/packaging/使用说明.txt" "$project_root/packaging/THIRD-PARTY-NOTICES.md" "$output/"
# Local test signing only. Distribution signing/notarization needs the product owner's Apple identity.
codesign --force --deep --sign - "$output/Help2Design.app"
hdiutil create -volname Help2Design -srcfolder "$output" -ov -format UDZO "$output.dmg"
printf '%s\n' "Local test build: $output.dmg"
