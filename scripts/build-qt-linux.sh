#!/usr/bin/env bash
# Build the existing Qt desktop stack against the host's glibc. The official
# Qt ARM64 SDK uses glibc 2.39 and cannot target Kylin V10's glibc 2.31.
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd)"
qt_version=6.8.3
ffmpeg_version=7.1.1
qt_prefix="$(realpath -m "${EDITHERE_QT_PREFIX:-$project_root/.tools/qt/$qt_version}")"
source_root="$project_root/.tools/qt-source/$qt_version"
build_root="$project_root/.tools/qt-build/$qt_version"
downloads="$project_root/.tools/qt-downloads"
materials="$qt_prefix/share/edithere-build-materials"
jobs="${BUILD_JOBS:-4}"
[[ "$(uname -s)" == Linux ]] || { echo 'Build Qt on the target Linux architecture.' >&2; exit 1; }
for tool in cmake ninja curl tar sha256sum pkg-config gcc g++ make; do
  command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done
# Qt 6.8 supports GCC 10. Debian 11 supplies GCC 10 and GLib 2.66; do not
# replace those packages with binaries from a newer distribution.
[[ "$(g++ -dumpversion | cut -d. -f1)" -ge 10 ]] || { echo 'Qt 6.8 requires GCC 10 or newer.' >&2; exit 1; }
recipe_hash="$(sha256sum "$0" | cut -d' ' -f1)"
if [[ -f "$qt_prefix/.edithere-qt-complete" ]] &&
   [[ "$(cat "$qt_prefix/.edithere-qt-complete")" == "$recipe_hash" ]]; then
  echo "Using source-built Qt: $qt_prefix"
  exit 0
fi
mkdir -p "$downloads" "$source_root" "$build_root" "$materials"

fetch_source() {
  local url="$1" digest="$2" archive="$downloads/${1##*/}"
  if [[ ! -f "$archive" ]]; then
    curl --fail --location --retry 3 --output "$archive.partial" "$url"
    echo "$digest  $archive.partial" | sha256sum --check --status
    mv "$archive.partial" "$archive"
  fi
  echo "$digest  $archive" | sha256sum --check --status
  # Keep the exact sources with the binary artifact, including upstream
  # copyright/licensing and the FFmpeg configure options below.
  cp "$archive" "$materials/"
  echo "$digest  ${archive##*/}" >> "$materials/SHA256SUMS"
  echo "$url" >> "$materials/SOURCES.txt"
}

: > "$materials/SHA256SUMS"
cat > "$materials/SOURCES.txt" <<'EOF'
EditHere Linux ARM64 preview build materials

Qt 6.8.3 and FFmpeg 7.1.1 are built from the unmodified source archives in
this directory. Qt remains dynamically linked; FFmpeg is shared LGPL with
GPL, nonfree and version3 components disabled. The matching source archives,
upstream licenses and configuration summaries accompany the application.
The recipe is scripts/build-qt-linux.sh in the corresponding EditHere source.

Source URLs:
EOF
# SHA-256 values published beside each tar.xz in the official Qt archive.
while read -r module digest; do
  archive="$module-everywhere-src-$qt_version.tar.xz"
  fetch_source "https://download.qt.io/archive/qt/6.8/$qt_version/submodules/$archive" "$digest"
  if [[ ! -d "$source_root/$module-everywhere-src-$qt_version" ]]; then
    tar -xf "$downloads/$archive" -C "$source_root"
  fi
done <<'EOF'
qtbase 56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80
qtsvg 35eb516460f00f264eb504baa253432384351cf23fb9980a5857190e8deef438
qtimageformats 049bfb99845e4801672aca07c3c4fc4c140f932a3a33faa899419579e33ef1c8
qtshadertools f6ec88bf42deba84d8f6b5d0914636ceed4749ccb51d1945b2f79b322b7ecf47
qtwayland 20fe385887d21190165a3180c17dcfc8b9a0e1da4ec76865b6334bdc709994b0
qtmultimedia 32e82307d783172a3b984cc3c47c5e4e8b819cee3cbfc702c7012c47f15f6b01
EOF

# Debian 11's GStreamer 1.18 is below Qt 6.8's required 1.20. Build the same
# FFmpeg major used by the existing Qt 6.8 desktop packages on the old ABI.
fetch_source "https://ffmpeg.org/releases/ffmpeg-$ffmpeg_version.tar.xz" \
  733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1
if [[ ! -d "$source_root/ffmpeg-$ffmpeg_version" ]]; then
  tar -xf "$downloads/ffmpeg-$ffmpeg_version.tar.xz" -C "$source_root"
fi
mkdir -p "$build_root/ffmpeg"
(
  cd "$build_root/ffmpeg"
  "$source_root/ffmpeg-$ffmpeg_version/configure" \
    --prefix="$qt_prefix" --libdir="$qt_prefix/lib" \
    --enable-shared --disable-static --enable-pic \
    --disable-gpl --disable-nonfree --disable-version3 --disable-autodetect \
    --disable-doc --disable-programs --disable-debug --disable-network \
    --disable-avdevice --disable-avfilter --disable-postproc
  make -j"$jobs"
  make install
)
mkdir -p "$materials/ffmpeg"
cp "$source_root/ffmpeg-$ffmpeg_version/"COPYING* "$materials/ffmpeg/"
cp "$source_root/ffmpeg-$ffmpeg_version/LICENSE.md" "$materials/ffmpeg/"
cp "$build_root/ffmpeg/ffbuild/config.log" "$build_root/ffmpeg/ffbuild/config.mak" "$materials/ffmpeg/"
export PKG_CONFIG_PATH="$qt_prefix/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$qt_prefix/lib:${LD_LIBRARY_PATH:-}"

mkdir -p "$build_root/qtbase"
(
  cd "$build_root/qtbase"
  "$source_root/qtbase-everywhere-src-$qt_version/configure" \
    -prefix "$qt_prefix" -release -opensource -confirm-license \
    -nomake tests -nomake examples -- -G Ninja \
    -DQT_INSTALL_LIBDIR=lib -DQT_INSTALL_LIBEXECDIR=libexec \
    -DFEATURE_xcb=ON -DFEATURE_dbus=ON -DFEATURE_glib=ON \
    -DFEATURE_opengl=ON -DFEATURE_vulkan=OFF
  cmake --build . --parallel "$jobs"
  cmake --install .
)
for module in qtsvg qtimageformats qtshadertools qtwayland qtmultimedia; do
  mkdir -p "$build_root/$module"
  options=()
  if [[ "$module" == qtwayland ]]; then
    # Only the desktop client plugins are needed; EditHere is not a compositor.
    options+=(-DFEATURE_wayland_client=ON -DFEATURE_wayland_server=OFF)
  elif [[ "$module" == qtmultimedia ]]; then
    options+=(-DFFMPEG_DIR="$qt_prefix" -DFEATURE_ffmpeg=ON \
      -DFEATURE_gstreamer=OFF -DFEATURE_pulseaudio=ON)
  fi
  (
    cd "$build_root/$module"
    "$qt_prefix/bin/qt-configure-module" "$source_root/$module-everywhere-src-$qt_version" \
      -- -G Ninja -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF "${options[@]}"
    cmake --build . --parallel "$jobs"
    cmake --install .
  )
done
for module in qtbase qtsvg qtimageformats qtshadertools qtwayland qtmultimedia; do
  mkdir -p "$materials/$module"
  cp -R "$source_root/$module-everywhere-src-$qt_version/LICENSES" "$materials/$module/"
  cp "$build_root/$module/config.summary" "$materials/$module/"
done
cp "$0" "$materials/build-qt-linux.sh"
{
  uname -m
  getconf GNU_LIBC_VERSION
  g++ --version
  pkg-config --modversion glib-2.0
} > "$materials/build-environment.txt"
echo "$recipe_hash" > "$qt_prefix/.edithere-qt-complete"
echo "Source-built Qt: $qt_prefix"
