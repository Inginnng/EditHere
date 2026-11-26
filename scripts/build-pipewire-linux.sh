#!/usr/bin/env bash
# Build the existing Wayland capture client's dependencies on the target ABI.
# The desktop continues to supply its PipeWire server and ScreenCast Portal.
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd)"
version=1.0.4
prefix="$(realpath -m "${EDITHERE_PIPEWIRE_PREFIX:-$project_root/.tools/pipewire/$version}")"
downloads="$project_root/.tools/pipewire-downloads"
source_root="$project_root/.tools/pipewire-source"
build_path="$project_root/.tools/pipewire-build/$version"
source_path="$source_root/pipewire-$version"
archive="$downloads/pipewire-$version.tar.gz"
source_url="https://gitlab.freedesktop.org/pipewire/pipewire/-/archive/$version/pipewire-$version.tar.gz"
source_sha256=ce00e0cee3eefaf8e92eecf5d28985f6dab43ccfe7e704d41b0cfda8376a187c
jobs="${BUILD_JOBS:-4}"
[[ "$(uname -s)" == Linux ]] || { echo 'Build PipeWire on the target Linux architecture.' >&2; exit 1; }
for tool in meson ninja curl tar sha256sum pkg-config gcc python3 patchelf; do
  command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done
triplet="$(gcc -dumpmachine)"
libdir="lib/$triplet"
modules="$prefix/$libdir/pipewire-0.3"
spa="$prefix/$libdir/spa-0.2"
materials="$prefix/share/edithere-build-materials/pipewire-$version"
recipe_hash="$(sha256sum "$0" | cut -d' ' -f1)"
if [[ -f "$prefix/.edithere-pipewire-complete" ]] &&
   [[ "$(cat "$prefix/.edithere-pipewire-complete")" == "$recipe_hash" ]]; then
  echo "Using source-built PipeWire: $prefix"
  exit 0
fi
mkdir -p "$downloads" "$source_root" "$materials"
if [[ ! -f "$archive" ]]; then
  curl --fail --location --retry 3 --output "$archive.partial" "$source_url"
  echo "$source_sha256  $archive.partial" | sha256sum --check --status
  mv "$archive.partial" "$archive"
fi
echo "$source_sha256  $archive" | sha256sum --check --status
if [[ ! -d "$source_path" ]]; then
  tar -xf "$archive" -C "$source_root"
fi

# Options and target names are from PipeWire 1.0.4's meson_options.txt and
# src/modules/meson.build. No system service, device backend, audio server,
# PulseAudio replacement or session-manager process is installed.
setup_args=()
if [[ -f "$build_path/meson-private/coredata.dat" ]]; then
  setup_args+=(--reconfigure)
fi
meson setup "${setup_args[@]}" "$build_path" "$source_path" --prefix="$prefix" --libdir="$libdir" \
  --buildtype=release --wrap-mode=nodownload -Dauto_features=disabled \
  -Ddocs=disabled -Dman=disabled -Dexamples=disabled -Dtests=disabled \
  -Dinstalled_tests=disabled -Dsession-managers=[] -Ddbus=disabled \
  -Dflatpak=disabled -Dpipewire-alsa=disabled -Dpipewire-jack=disabled \
  -Dpipewire-v4l2=disabled -Drlimits-install=false -Dlegacy-rtkit=false \
  -Daudiomixer=disabled -Dcontrol=disabled -Daudiotestsrc=disabled \
  -Dvideoconvert=disabled -Dvideotestsrc=disabled \
  -Dspa-plugins=enabled -Dsupport=enabled -Daudioconvert=enabled
client_modules=(protocol-native client-node client-device adapter metadata session-manager)
targets=(pipewire-0.3 spa-support spa-audioconvert)
for module in "${client_modules[@]}"; do
  targets+=("pipewire-module-$module")
done
meson compile -C "$build_path" -j "$jobs" "${targets[@]}"

# Upstream defines other server modules unconditionally. Select only the client
# outputs, while letting Meson's install manifest select its public SDK headers
# and generated pkg-config files (rather than maintaining another header list).
mkdir -p "$modules" "$spa/support" "$spa/audioconvert" "$prefix/share/pipewire"
for library in "$build_path/src/pipewire/"libpipewire-0.3.so*; do
  [[ -d "$library" ]] || cp -a "$library" "$prefix/$libdir/"
done
for module in "${client_modules[@]}"; do
  install -m 755 "$build_path/src/modules/libpipewire-module-$module.so" "$modules/"
  patchelf --set-rpath '$ORIGIN:$ORIGIN/..' "$modules/libpipewire-module-$module.so"
done
patchelf --set-rpath '$ORIGIN' "$(readlink -f "$prefix/$libdir/libpipewire-0.3.so.0")"
install -m 755 "$build_path/spa/plugins/support/libspa-support.so" "$spa/support/"
install -m 755 "$build_path/spa/plugins/audioconvert/libspa-audioconvert.so" "$spa/audioconvert/"
install -m 644 "$build_path/src/daemon/client.conf" "$prefix/share/pipewire/"
meson introspect --installed "$build_path" > "$build_path/client-install-manifest.json"
python3 - "$build_path/client-install-manifest.json" "$prefix" "$libdir" <<'PY'
import json
from pathlib import Path
import shutil
import sys

manifest, prefix, libdir = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
for source, destinations in json.loads(manifest.read_text()).items():
    for destination in destinations if isinstance(destinations, list) else [destinations]:
        target = Path(destination)
        if target.is_relative_to(prefix / 'include') or target.is_relative_to(prefix / libdir / 'pkgconfig'):
            target.parent.mkdir(parents=True, exist_ok=True)
            if Path(source).is_dir():
                shutil.copytree(source, target, dirs_exist_ok=True)
            else:
                shutil.copy2(source, target)
PY

cp "$archive" "$materials/"
cp "$source_path/COPYING" "$source_path/LICENSE" "$materials/"
cp "$build_path/meson-info/intro-buildoptions.json" "$materials/"
cp "$build_path/meson-logs/meson-log.txt" "$materials/"
cp "$0" "$materials/build-pipewire-linux.sh"
echo "$source_sha256  ${archive##*/}" > "$materials/SHA256SUMS"
printf '%s\n' "$source_url" > "$materials/SOURCES.txt"
{
  uname -m
  getconf GNU_LIBC_VERSION
  gcc --version
  meson --version
} > "$materials/build-environment.txt"

export PKG_CONFIG_PATH="$prefix/$libdir/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$prefix/$libdir:${LD_LIBRARY_PATH:-}"
[[ "$(pkg-config --modversion libpipewire-0.3)" == "$version" ]]
cat > "$build_path/client-smoke.c" <<'EOF'
#include <pipewire/pipewire.h>
int main(void) {
    pw_init(NULL, NULL);
    struct pw_main_loop *loop = pw_main_loop_new(NULL);
    if (!loop) return 1;
    struct pw_context *context = pw_context_new(pw_main_loop_get_loop(loop), NULL, 0);
    if (!context) return 2;
    pw_context_destroy(context);
    pw_main_loop_destroy(loop);
    pw_deinit();
    return 0;
}
EOF
# Keep SDK paths quoted: shell expansion of pkg-config output does not unescape
# its backslashes for spaces/non-ASCII directory names.
gcc "$build_path/client-smoke.c" -o "$build_path/client-smoke" -D_REENTRANT -pthread \
  -I"$prefix/include/pipewire-0.3" -I"$prefix/include/spa-0.2" \
  -L"$prefix/$libdir" -lpipewire-0.3
PIPEWIRE_MODULE_DIR="$modules" SPA_PLUGIN_DIR="$spa" \
  PIPEWIRE_CONFIG_DIR="$prefix/share/pipewire" "$build_path/client-smoke"
echo "$recipe_hash" > "$prefix/.edithere-pipewire-complete"
echo "Source-built PipeWire client: $prefix"
