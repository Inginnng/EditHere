#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd)"
: "${QT_ROOT:?Set QT_ROOT to the Qt 6.8 gcc_64 SDK directory}"
build_path="${EDITHERE_BUILD_DIR:-$project_root/build-linux}"
cmake -S "$project_root" -B "$build_path" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_ROOT" -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$build_path" --parallel "${BUILD_JOBS:-4}"
export QT_QPA_PLATFORM=offscreen
export H2D_TEST_ARTIFACTS="$project_root/artifacts/linux-ui"
ctest --test-dir "$build_path" --output-on-failure --output-junit "$build_path/test-results.xml"
