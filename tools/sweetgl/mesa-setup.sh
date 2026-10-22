#!/bin/bash
# Native shader tools, then static Panfrost/GLES for iOS. Requires Xcode.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
H="$ROOT/tools/sweetgl"
M="$ROOT/research/mesa"
N="$ROOT/out/mesa-host"
B="$ROOT/out/mesa-ios"
PREFIX="$ROOT/out/mesa-host-tools"
EXPECTED=$(cat "$H/mesa-revision")
test "$(git -C "$M" rev-parse HEAD)" = "$EXPECTED"
if ! git -C "$M" apply --reverse --check "$H/mesa.patch" 2>/dev/null; then
  git -C "$M" apply --check "$H/mesa.patch"
  git -C "$M" apply "$H/mesa.patch"
fi

LLVM=$(brew --prefix llvm)
TRANSLATOR=$(brew --prefix spirv-llvm-translator)
SPIRV=$(brew --prefix spirv-tools)
export PATH="$LLVM/bin:$(brew --prefix bison)/bin:$PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$ROOT/out/mesa-libclc:$TRANSLATOR/lib/pkgconfig:$SPIRV/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

# These architecture-independent files and version match Mesa's pinned CI.
CLC="$ROOT/out/mesa-libclc"
mkdir -p "$CLC"
for file in spirv64-mesa3d-.spv spirv-mesa3d-.spv mesa-libclc.pc.in; do
  if [ ! -s "$CLC/$file" ]; then
    curl --fail --location --retry 3 \
      "https://gitlab.freedesktop.org/api/v4/projects/27606/packages/generic/mesa-libclc/22.1.8.3/$file" \
      --output "$CLC/$file"
  fi
done
python3 - "$CLC" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
lines = p.joinpath('mesa-libclc.pc.in').read_text().splitlines()
p.joinpath('mesa-libclc.pc').write_text('\n'.join(
    'libexecdir=' + str(p) if line.startswith('libexecdir=') else line
    for line in lines) + '\n')
PY

COMMON=(--buildtype=release -Db_ndebug=true -Ddefault_library=static
  -Dplatforms= -Degl=disabled -Dglx=disabled -Dgbm=disabled -Dvulkan-drivers=
  -Dzstd=disabled -Dzlib=disabled -Dexpat=disabled -Dxmlconfig=disabled
  -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled
  -Dbuild-tests=false -Dshader-cache=disabled -Dperfetto=false
  -Dgallium-va=disabled -Dgallium-rusticl=false -Dhtml-docs=disabled
  -Ddisplay-info=disabled -Dteflon=false -Dpanfrost-rust=false)
CC="$LLVM/bin/clang" CXX="$LLVM/bin/clang++" \
  CFLAGS="-I$H/include" CXXFLAGS="-I$H/include" meson setup "$N" "$M" \
  "${COMMON[@]}" --prefix "$PREFIX" -Dgallium-drivers= -Dopengl=false \
  -Dtools=panfrost -Dllvm=enabled -Dmesa-clc=enabled \
  -Dinstall-mesa-clc=true -Dprecomp-compiler=enabled \
  -Dinstall-precomp-compiler=true -Dstatic-libclc=all
meson compile -C "$N" mesa_clc vtn_bindgen2 panfrost_compile
mkdir -p "$PREFIX/bin"
cp "$N/src/compiler/clc/mesa_clc" "$PREFIX/bin/mesa_clc"
cp "$N/src/compiler/spirv/vtn_bindgen2" "$PREFIX/bin/vtn_bindgen2"
cp "$N/src/panfrost/clc/panfrost_compile" "$PREFIX/bin/panfrost_compile"

# Resolve the selected SDK rather than hard-coding a developer's Xcode path.
python3 - "$H" "$ROOT/out/ios-cross.txt" <<'PY'
import pathlib, subprocess, sys
def xcrun(*args):
    return subprocess.check_output(['xcrun', '--sdk', 'iphoneos', *args], text=True).strip()
sdk = xcrun('--show-sdk-path')
flags = ['-arch', 'arm64', '-isysroot', sdk, '-miphoneos-version-min=16.0']
lines = ['[binaries]']
for key, tool in [('c', 'clang'), ('cpp', 'clang++'), ('objc', 'clang')]:
    lines.append(key + ' = ' + repr([xcrun('--find', tool)] + flags))
for key, tool in [('ar', 'ar'), ('strip', 'strip')]:
    lines.append(key + ' = ' + repr(xcrun('--find', tool)))
lines += ["pkg-config = 'false'", '', '[built-in options]',
          'c_args = ' + repr(['-I' + sys.argv[1] + '/include']),
          'cpp_args = ' + repr(['-I' + sys.argv[1] + '/include']), '',
          '[host_machine]', "system = 'darwin'", "subsystem = 'ios'",
          "cpu_family = 'aarch64'", "cpu = 'arm64'", "endian = 'little'"]
pathlib.Path(sys.argv[2]).write_text('\n'.join(lines) + '\n')
PY
meson setup "$B" "$M" --cross-file "$ROOT/out/ios-cross.txt" "${COMMON[@]}" \
  -Dgallium-drivers=panfrost -Dgles1=disabled -Dgles2=enabled -Dopengl=true \
  -Dtools= -Dllvm=disabled -Dmesa-clc=system -Dprecomp-compiler=system
