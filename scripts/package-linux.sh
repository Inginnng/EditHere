#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd)"
: "${QT_ROOT:?Set QT_ROOT to the Qt 6.8 gcc_64 SDK directory}"
: "${LINUXDEPLOY:?Set LINUXDEPLOY to the official linuxdeploy AppImage}"
: "${LINUXDEPLOY_PLUGIN_QT:?Set LINUXDEPLOY_PLUGIN_QT to the official Qt plugin AppImage}"
build_path="${EDITHERE_BUILD_DIR:-$project_root/build-linux}"
[[ "$(uname -m)" == x86_64 ]] || { echo 'This package targets x86_64' >&2; exit 1; }
version="$(cat "$build_path/version.txt")"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid built version' >&2; exit 1; }
output="${EDITHERE_PACKAGE_DIR:-$project_root/dist/linux-$version}"
[[ ! -e "$output" ]] || { echo "Output already exists: $output" >&2; exit 1; }
mkdir -p "$output"
appdir="$output/AppDir"
DESTDIR="$appdir" cmake --install "$build_path"
cp "$project_root/packaging/linux/AppRun" "$appdir/AppRun"
chmod +x "$appdir/AppRun"
cp -R "$project_root/skills" "$project_root/schema" "$appdir/usr/share/doc/edithere/"
cp -R "$project_root/packaging/licenses" "$appdir/usr/share/doc/edithere/"
cp "$build_path/version.txt" "$appdir/usr/share/doc/edithere/"
cp "$project_root/NOTICE" "$project_root/LICENSING.md" "$appdir/usr/share/doc/edithere/"
mkdir -p "$appdir/usr/share/doc/edithere/licenses/linux"
# PipeWire loads client modules at runtime, so ldd/linuxdeploy cannot discover
# them from the executable. Keep the client modules/configuration with the
# matching library; the user's desktop supplies the server and Portal.
pipewire_root="${EDITHERE_PIPEWIRE_RUNTIME_ROOT:-/usr}"
if grep -q '^PIPEWIRE_FOUND:INTERNAL=1$' "$build_path/CMakeCache.txt"; then
  triplet="$(gcc -dumpmachine)"
  modules="$pipewire_root/lib/$triplet/pipewire-0.3"
  spa="$pipewire_root/lib/$triplet/spa-0.2"
  [[ -d "$modules" && -d "$spa" && -f "$pipewire_root/share/pipewire/client.conf" ]] || {
    echo 'PipeWire client runtime is missing; install matching libpipewire modules, SPA modules and pipewire-bin.' >&2
    exit 1
  }
  mkdir -p "$appdir/usr/lib/pipewire-0.3" "$appdir/usr/lib/spa-0.2/support" \
    "$appdir/usr/lib/spa-0.2/audioconvert" "$appdir/usr/share/pipewire"
  for module in protocol-native client-node client-device adapter metadata session-manager; do
    cp "$modules/libpipewire-module-$module.so" "$appdir/usr/lib/pipewire-0.3/"
  done
  cp "$spa/support/libspa-support.so" "$appdir/usr/lib/spa-0.2/support/"
  cp "$spa/audioconvert/libspa-audioconvert.so" "$appdir/usr/lib/spa-0.2/audioconvert/"
  cp "$pipewire_root/share/pipewire/client.conf" "$appdir/usr/share/pipewire/"
  for component in libpipewire-0.3-modules libspa-0.2-modules pipewire-bin; do
    cp "$pipewire_root/share/doc/$component/copyright" "$appdir/usr/share/doc/edithere/licenses/linux/$component.txt"
  done
  pipewire_sdk="$(pkg-config --variable=prefix libpipewire-0.3)"
  for component in libpipewire-0.3-0t64 libpipewire-0.3-0 libpipewire-0.3-common; do
    if [[ -f "$pipewire_sdk/share/doc/$component/copyright" ]]; then
      cp "$pipewire_sdk/share/doc/$component/copyright" "$appdir/usr/share/doc/edithere/licenses/linux/$component.txt"
    fi
  done
fi
for copyright in /usr/share/doc/libatspi2.0-0/copyright /usr/share/doc/libglib2.0-0t64/copyright \
  /usr/share/doc/libdbus-1-3/copyright /usr/share/doc/libpcre2-8-0/copyright /usr/share/doc/libffi8/copyright \
  /usr/share/doc/libgcc-s1/copyright /usr/share/doc/libstdc++6/copyright \
  /usr/share/doc/libxtst6/copyright /usr/share/doc/libpipewire-0.3-0t64/copyright \
  /usr/share/doc/libpipewire-0.3-0/copyright /usr/share/doc/libspa-0.2-modules/copyright; do
  if [[ -f "$copyright" ]]; then
    package="$(basename "$(dirname "$copyright")")"
    cp "$copyright" "$appdir/usr/share/doc/edithere/licenses/linux/$package.txt"
  fi
done
for component in libpulse0 libasyncns0 libsndfile1 libflac12t64 libogg0 libopus0 \
  libvorbis0a libvorbisenc2 libmp3lame0 libmpg123-0t64; do
  copyright="$pipewire_root/share/doc/$component/copyright"
  if [[ -f "$copyright" ]]; then
    cp "$copyright" "$appdir/usr/share/doc/edithere/licenses/linux/$component.txt"
  fi
done
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="$QT_ROOT/bin/qmake"
export LD_LIBRARY_PATH="$QT_ROOT/lib:${LD_LIBRARY_PATH:-}"
if [[ -d "$appdir/usr/lib/pipewire-0.3" ]]; then
  export LD_LIBRARY_PATH="$appdir/usr/lib/pipewire-0.3:$LD_LIBRARY_PATH"
fi
export EXTRA_QT_PLUGINS="wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration;multimedia"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export VERSION="$version" ARCH=x86_64 OUTPUT="EditHere-linux-x86_64.AppImage"
export LDAI_RUNTIME_FILE="$project_root/.tools/linuxdeploy/runtime-x86_64"
[[ -f "$LDAI_RUNTIME_FILE" ]] || { echo 'Run scripts/fetch-linuxdeploy.py first' >&2; exit 1; }
echo '156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074 '"$LDAI_RUNTIME_FILE" | sha256sum --check --status
plugin_directory="$(dirname "$LINUXDEPLOY_PLUGIN_QT")"
export PATH="$plugin_directory:$QT_ROOT/bin:$PATH"
cd "$output"
deployment_args=()
if grep -q '^PIPEWIRE_FOUND:INTERNAL=1$' "$build_path/CMakeCache.txt"; then
  # linuxdeploy blacklists PipeWire by default. This client library must match
  # the client modules above, so explicitly request its deployment.
  deployment_args+=(--library "$(pkg-config --variable=libdir libpipewire-0.3)/libpipewire-0.3.so.0")
fi
"$LINUXDEPLOY" --appdir "$appdir" "${deployment_args[@]}" --plugin qt --custom-apprun "$project_root/packaging/linux/AppRun" --output appimage
[[ -s "$OUTPUT" ]] || { echo 'AppImage was not produced' >&2; exit 1; }
cp "$project_root/packaging/linux/edithere-cli" "$output/edithere-cli"
chmod +x "$output/edithere-cli"
sha256sum "$OUTPUT" > "$OUTPUT.sha256"
echo "Linux package: $output/$OUTPUT"
