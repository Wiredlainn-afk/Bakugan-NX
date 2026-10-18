# Installer page

A static web page (plain HTML, CSS and ES modules, no build step, no CDN) that builds the SD card package of the port
from the player's own Bakugan: Battle Brawlers game. It is meant for GitHub Pages. **Everything runs in the browser: the
game files are never uploaded.** The only downloads are the page itself and the build (`bakugan.nro` and, when the release
has one, `bakugan_native_pipelines.bin`), from the same site.

It is based on the installer of [masseffect-nx](https://github.com/nebadasSwifty/masseffect-nx) by NebadasSwifty, without
its NSP, DLC and Unreal-package parts, and uses the WebAssembly DXC build pinned by StevensND's nfsmw-nx installer.

The player picks the **disc image (.iso)** or the **extracted disc folder**, the page checks `default.xex` against the
known edition (`config.js`), and creates:

* `bakugan-nx.zip` (full install): `bakugan-nx/{bakugan.nro, cache/..., bakugan_shaders.mesp, bakugan_shaders.mesp.idx,
  game_root/...}`, to be extracted into `sdmc:/switch/`;
* `bakugan-nx-update.zip`: the same without `game_root/`, to extract over an existing install (the disc is still needed,
  because the shaders are made from it).

## Languages

The game has voices (`media/sound/<language>/`) and an intro movie (`media/movie/intro_4_3_<code>.pam`) in seven languages,
about 80 MB each. The page has a "Languages to include" step (all ticked by default): the unticked ones are left out of
the zip (keeping only English saves about 480 MB of the 3.3 GB). The NRO (build 0.1.1 or newer) has a file-system fallback
(`vfs_interchangeable`, `app/bakugan_switch_defaults.toml`, `sdk/src/filesystem/virtual_file_system.cpp`): when the game
asks for a file of a language that is not on the card, the same file of the first installed language is served instead.
The menus and text come from `d00.arc` and are not affected. With a published build older than `config.languages.minBuild`
the page shows no choice and includes every language.

## How it works

```
 source (ISO or folder) --> file list [{path, size, lazy Blob}]        js/xdvdfs.js, js/source.js
        default.xex --> SHA-256 --> edition table (config.js)           js/plan.js
 1 download   releases/bakugan.nro (+ pipeline cache) checked against manifest.json
 2 scan       media/shaders/*.par -> Xenos shader containers            js/par_scan.js
              + the shaders the game builds while it runs              wasm/runtime_containers.json
 3 translate  container -> HLSL (hlsl.wasm) -> SPIR-V (dxc_web.wasm)   workers (js/workers/handlers.js)
 4 pack       bakugan_shaders.mesp + .idx                              pack.wasm
 5 zip        streamed to the disk the user picks                      js/zip.js, js/sink.js
```

The disc has 1,858 of the game's shaders. The other 48 (46 vertex, 2 pixel) are written by the game's own code into the
command stream while it runs, so they are not on the disc; `wasm/runtime_containers.json` (75 KB, made by
`tools/make_runtime_containers.py`) carries them, as the masseffect-nx installer does for its own run-time shaders. Four
vertex shaders fail in DXC ("partial explicit stage input location assignment via vk::location(X) unsupported") in the
native toolchain too; they are listed in `config.js` (`knownShaderFailures`) and left out. Result: 1,902 shaders, a
package of about 88 MB plus a 2.9 MB index.

## Building the site

The workflow `.github/workflows/installer.yml` builds `hlsl` and `pack` with Emscripten
(`shaders/wasm/build_wasm_tools.sh`), fetches the pinned DXC (`installer/tools/fetch_dxc.sh`), takes `bakugan.nro` (and
`bakugan_native_pipelines.bin`) from the latest published release, runs `tools/wasm_smoke.mjs` and deploys to GitHub
Pages (Settings > Pages > Source: "GitHub Actions"). Locally:

```
shaders/wasm/build_wasm_tools.sh                       # needs Emscripten; writes shaders/wasm/out
installer/tools/fetch_dxc.sh installer/wasm            # the pinned DXC (dxc_web.mjs + .wasm)
installer/tools/stage_site.sh _site --wasm shaders/wasm/out --releases <folder with bakugan.nro>
```

## Testing the pipeline in Node

`test/run_disc.mjs` runs the real pipeline (the real WebAssembly tools, DXC included) over an extracted disc:

```
node test/run_disc.mjs <extracted disc folder> out.zip [--full]
```

It takes about 3 minutes and writes the shader package; with the package placed next to `bakugan.exe`, the PC build runs
a battle with 0 unidentified shader loads and 0 failed draws.
