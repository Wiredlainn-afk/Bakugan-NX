#!/usr/bin/env bash
# Assembles the static site (what GitHub Pages serves) from the installer sources and the build artifacts.
#
#   installer/tools/stage_site.sh <out dir> [--wasm <dir>] [--releases <dir>] [--require-complete]
#
#   --wasm <dir>      the WebAssembly tools (hlsl, pack, dxc_web: .mjs + .wasm). Default: installer/wasm. Only used when
#                     config.js has shaderMode 'disc' (0.1.x); with 'embedded' (0.2.0) the shaders are inside the NRO
#   --releases <dir>  the release assets (bakugan.nro, deko3d_shaders.bin, pipeline_list.bin, and bakugan.toml.example when there is one) to publish next to the page;
#                     a manifest.json with their size and SHA-256 is written. Default: none (the page then reports
#                     that no build is published).
#
# The page itself needs no build step: this only copies files and generates releases/manifest.json.
set -eu
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
inst=$(cd "$here/.." && pwd)
root=$(cd "$inst/.." && pwd)
out=${1:?usage: stage_site.sh <out dir> [--wasm <dir>] [--releases <dir>] [--require-complete]}
shift
wasm=$inst/wasm
releases=
strict=0
while [ $# -gt 0 ]; do
  case $1 in
    --require-complete) strict=1; shift;;
    --wasm) wasm=$2; shift 2;;
    --releases) releases=$2; shift 2;;
    *) echo "unknown argument $1" >&2; exit 1;;
  esac
done

rm -rf "$out"
mkdir -p "$out/releases"
embedded=0
grep -q "shaderMode: 'embedded'" "$inst/config.js" && embedded=1
cp "$inst/index.html" "$inst/style.css" "$inst/config.js" "$out/"
cp -R "$inst/js" "$inst/assets" "$out/"
touch "$out/.nojekyll"
cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$out/"

# The translator's shared header, pasted into every generated HLSL by the translator, and the shaders the game
# builds while it runs (not on the disc).
n=0
if [ "$embedded" -eq 0 ]; then
  mkdir -p "$out/wasm"
  cp "$root/shaders/XenosRecomp/shader_common.h" "$out/wasm/shader_common.h"
  cp "$inst/wasm/runtime_containers.json" "$out/wasm/runtime_containers.json"
  for f in hlsl pack dxc_web; do
    for ext in mjs wasm; do
      if [ -f "$wasm/$f.$ext" ]; then cp "$wasm/$f.$ext" "$out/wasm/"; n=$((n + 1)); fi
    done
  done
  echo "staged $n wasm files from $wasm"
  [ "$n" -eq 6 ] || echo "WARNING: expected 6 wasm files (hlsl, pack, dxc_web: .mjs + .wasm); the page will report the missing ones" >&2
else
  echo "shaderMode embedded: no WebAssembly tools staged"
fi

if [ -n "$releases" ]; then
  for asset in "$releases"/*.nro "$releases"/bakugan_native_pipelines.bin "$releases"/deko3d_shaders.bin "$releases"/pipeline_list.bin "$releases"/bakugan.toml.example; do
    [ ! -f "$asset" ] || cp "$asset" "$out/releases/"
  done
fi
if [ "$strict" -eq 1 ]; then
  if [ "$embedded" -eq 0 ]; then
    [ "$n" -eq 6 ] || { echo 'error: incomplete WebAssembly toolchain' >&2; exit 1; }
    [ -s "$out/wasm/runtime_containers.json" ] || { echo 'error: missing runtime_containers.json' >&2; exit 1; }
    [ -s "$out/releases/bakugan_native_pipelines.bin" ] || echo 'WARNING: no pipeline cache in the release: installs without it' >&2
  else
    [ -s "$out/releases/deko3d_shaders.bin" ] || { echo 'error: missing release asset deko3d_shaders.bin' >&2; exit 1; }
    [ -s "$out/releases/pipeline_list.bin" ] || { echo 'error: missing release asset pipeline_list.bin' >&2; exit 1; }
  fi
  [ -s "$out/releases/bakugan.nro" ] || { echo 'error: missing release asset bakugan.nro' >&2; exit 1; }
fi
python3 - "$out" "${RELEASE_TAG:-}" <<'PY'
import hashlib, json, os, sys
out, tag = sys.argv[1], sys.argv[2]
assets = {}
rel = os.path.join(out, "releases")
for name in sorted(os.listdir(rel)):
    path = os.path.join(rel, name)
    if name.endswith(".nro") or name in ("bakugan_native_pipelines.bin", "deko3d_shaders.bin", "pipeline_list.bin", "bakugan.toml.example"):
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        assets[name] = {"size": os.path.getsize(path), "sha256": h.hexdigest()}
json.dump({"tag": tag or None, "assets": assets}, open(os.path.join(rel, "manifest.json"), "w"), indent=1)
print("manifest.json:", ", ".join(assets) or "(no assets)")
PY
echo "site staged in $out"
