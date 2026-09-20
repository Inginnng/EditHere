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
if [[ ! -f "$project_root/schema/feedback-v0.7.schema.json" ]]; then printf '%s\n' "The current feedback schema is missing." >&2; exit 1; fi
build_version="$(cat "$build_path/version.txt")"
if [[ ! "$build_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then printf '%s\n' "Invalid build version." >&2; exit 1; fi
output="$project_root/dist/EditHere-$build_version-macos-universal"
if [[ -e "$output" || -e "$output.dmg" ]]; then printf '%s\n' "Output folder or DMG already exists: $output" >&2; exit 1; fi
mkdir -p "$output"
cp -R "$build_path/EditHere.app" "$output/"
cp "$build_path/version.txt" "$output/"
"$QT_ROOT/bin/macdeployqt" "$output/EditHere.app" -always-overwrite
cp -R "$project_root/packaging/licenses" "$output/"
cp -R "$project_root/schema" "$output/"
cp "$project_root/packaging/使用说明.txt" "$project_root/packaging/THIRD-PARTY-NOTICES.md" "$output/"
cp "$project_root/LICENSE" "$project_root/LICENSING.md" "$project_root/COMMERCIAL-LICENSE.md" "$project_root/NOTICE" "$output/"
sed 's|packaging/THIRD-PARTY-NOTICES.md|THIRD-PARTY-NOTICES.md|g' "$project_root/LICENSING.md" > "$output/LICENSING.md"
# Local test signing only. Distribution signing/notarization needs the product owner's Apple identity.
codesign --force --deep --sign - "$output/EditHere.app"
hdiutil create -volname EditHere -srcfolder "$output" -format UDZO "$output.dmg"
printf '%s\n' "Local test build: $output.dmg"
