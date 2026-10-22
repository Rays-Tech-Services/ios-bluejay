#!/bin/bash
# Link static Mesa and the EAGL/DRM bridge into the injectable iOS library.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
H="$ROOT/tools/sweetgl"
M="$ROOT/research/mesa"
B="$ROOT/out/mesa-ios"
O="$ROOT/out/sweetgl"
mkdir -p "$O"

meson introspect "$B" --targets > "$O/targets.json"
python3 - "$B" "$O" <<'PY'
import json, pathlib, shlex, subprocess, sys
b, out = map(pathlib.Path, sys.argv[1:])
renderer_roots = ('src/mesa/', 'src/mapi/', 'src/gallium/auxiliary/',
                  'src/gallium/drivers/panfrost/', 'src/gallium/winsys/panfrost/',
                  'src/panfrost/', 'src/compiler/', 'src/util/', 'src/c11/')
archives = sorted({str(pathlib.Path(f).relative_to(b))
    for target in json.loads(out.joinpath('targets.json').read_text())
    if target['type'] == 'static library'
    for f in target['filename'] if f.endswith('.a')
    and str(pathlib.Path(f).relative_to(b)).startswith(renderer_roots)})
if not archives:
    raise SystemExit('No Mesa static archives configured')
subprocess.run(['ninja', '-C', str(b), *archives], check=True)
out.joinpath('mesa-libs.rsp').write_text('\n'.join(shlex.quote(str(b / p)) for p in archives) + '\n')
commands = json.loads(b.joinpath('compile_commands.json').read_text())
entry = next(x for x in commands if x['file'].endswith('state_tracker/st_manager.c'))
args = entry.get('arguments') or shlex.split(entry['command'])
out.joinpath('mesa-defines.rsp').write_text('\n'.join(shlex.quote(a) for a in args if a.startswith('-D')) + '\n')
PY

BRIDGE="$B/src/mesa/glapi/glapi/libglapi_bridge.a"
nm -g "$BRIDGE" > "$O/bridge.nm"
python3 "$H/gen-dispatch.py" "$H/ios-dispatch.txt" "$M/include" "$O/bridge.nm" "$O/dispatch.c"
CC=(xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0)
INC=(-I"$H/include" -I"$H" -I"$ROOT/kernel/include" -I"$M/include" -I"$M/src"
  -I"$M/src/mesa" -I"$M/src/mapi" -I"$M/src/gallium/include"
  -I"$M/src/gallium/auxiliary" -I"$M/src/gallium/drivers"
  -I"$M/src/gallium/winsys" -I"$B/src" -I"$B/src/mesa" -I"$B/src/mapi")
for name in sweetgl sweet_drm eagl layerdump dispatch; do
  src="$H/$name.c"
  if [ "$name" = dispatch ]; then src="$O/dispatch.c"; fi
  "${CC[@]}" -O2 -Wall -fvisibility=hidden @"$O/mesa-defines.rsp" "${INC[@]}" \
    -c "$src" -o "$O/$name.o"
done
cat > "$O/exports.txt" <<'EOF'
_sweetgl_init
_sweetgl_create
_sweetgl_make_current
_sweetgl_destroy
_sweetgl_selftest
EOF
xcrun --sdk iphoneos clang++ -arch arm64 -miphoneos-version-min=16.0 \
  -dynamiclib -dead_strip -install_name /usr/lib/sweetgl.dylib \
  -exported_symbols_list "$O/exports.txt" -o "$O/sweetgl.dylib" \
  "$O/sweetgl.o" "$O/sweet_drm.o" "$O/eagl.o" "$O/layerdump.o" "$O/dispatch.o" \
  @"$O/mesa-libs.rsp" -framework IOKit -framework CoreFoundation \
  -framework IOSurface -framework OpenGLES -framework CoreGraphics -lobjc -lc++ -lz
codesign --force --sign - "$O/sweetgl.dylib"
codesign --verify --strict "$O/sweetgl.dylib"
file "$O/sweetgl.dylib"
shasum -a 256 "$O/sweetgl.dylib" > "$O/SHA256SUMS"
