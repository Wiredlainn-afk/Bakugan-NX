# Building

Everything below is for Windows (PowerShell) and was done with a portable toolchain that needs no administrator rights.
Nothing from the game is in this repository: you make the code, the shaders and the NRO from your own copy of the game.

## What you need

| Tool | Used for |
|---|---|
| LLVM/clang 20, the MSVC CRT and Windows SDK (for example splatted with `xwin`), CMake, Ninja | The PC build and the code generator (`tools/win/env.ps1` loads them; the default root is `C:\bakugan-tools`, override it with `BAKUGAN_TOOLS`) |
| devkitPro: devkitA64 (GCC 16.1) and libnx 4.12 | The Switch build (`tools/switch/cmake/switch-devkitA64.cmake`) |
| The NVK driver | Linked into the NRO. See [../mesa/README.md](../mesa/README.md); point `NVK_SDK` at the SDK folder it makes |
| DirectXShaderCompiler, glslang | The shader library |
| Python 3 | The tools in `tools/` |

[switch-build-notes.md](switch-build-notes.md) lists everything the first Switch build needed (MSYS2 UCRT64, Rust,
packages) and the problems found.

## Steps

1. **Third-party sources.** `python tools/fetch_thirdparty.py` fetches the SDK's libraries into `sdk/thirdparty`.
2. **The game.** `python tools/extract_iso.py "<your game>.iso"` extracts the disc to `assets/game_root` (or copy the
   extracted disc there yourself; it needs `default.xex` and `media/`).
3. **Code generator and recompiled code.** `tools/win/host_build.ps1` builds the generator, then
   `tools/codegen.ps1` translates `default.xex` into `app/generated/` using `app/bakugan_manifest.toml`,
   `app/overrides.toml` and `app/codegen_flavor.toml`. `tools/post_codegen.py` applies the fixes the generator does not.
4. **PC build** (the reference and the test bed): `tools/win/configure_pc.ps1`, then `tools/win/build_pc.ps1`. The result
   is `out/pc/bakugan.exe`; `tools/win/boot_test.ps1` runs it muted and minimised for tests.
5. **Shaders.** The game's shaders are found by running the PC build (`tools/win/discover_pass.ps1`, then
   `tools/wrap_discovered.py`) and packed with `tools/win/build_shader_library.ps1 <folders>` into
   `out/shaderlib/bakugan_shaders.mesp` and `.idx`. See [../shaders/README.md](../shaders/README.md).
6. **Switch build.** `tools/win/build_switch.ps1 -Jobs 8` makes `out/nx/bakugan.nro`.
7. **Release files.** `tools/win/package_release.ps1 [-Zip]` makes, without the game's shaders (the installer page makes
   them in the player's browser from their own disc):
   * `out/nx/sdcard_release/switch/bakugan-nx/`: the SD card folder (`bakugan.nro`, `cache/bakugan_native_pipelines.bin`,
     `game_root/` with an instruction file). `-Zip` zips it as `out/nx/bakugan-nx-<version>.zip`.
   * `out/nx/release_assets/`: `bakugan.nro`, the optional pipeline cache (from `release/cache/`) and `SHA256SUMS.txt`:
     the files to attach to the GitHub release (tag `v0.1.0`), which the installer page
     ([../installer/README.md](../installer/README.md)) downloads.

   `-WithShaders` also puts the shader package in the SD card folder: the self-contained version, with the game's
   shaders translated; use it for your own installs. `tools/win/package_switch.ps1` and `tools/win/stage_toml.ps1`
   make test folders with a chosen settings file.

## Test and release builds

Both are the same source and the same NRO. What differs is the settings file and the folder:

| | Test build | Release build |
|---|---|---|
| Script | `tools/win/package_switch.ps1`, then `tools/win/stage_toml.ps1 <name>` | `tools/win/package_release.ps1` |
| Folder | `out/nx/sdcard/switch/bakugan-nx` | `out/nx/sdcard_release/switch/bakugan-nx` and `out/nx/release_assets` |
| `bakugan.toml` | One of the files in [`app/tests`](../app/tests/README.md) (numbered logs, profile report, GPU categories, diagnostic frames) | None: the NRO writes the player's default one; everything else is compiled in (`app/bakugan_switch_defaults.toml`), logging replaced at every start |
| Pipeline cache | Not copied (the console keeps growing its own while testing; `-WithCache` adds the packaged one) | Included when `release/cache/bakugan_native_pipelines.bin` exists |
| Logs | Numbered (`bakugan_NNN.log`), one per start, with the profile report | One `logs/bakugan.log`, replaced at every start (`log_single_file = true`, `log_max_files = 1`), plus `logs/flight.txt` and `flight.prev.txt` |

The standard comparison test is `app/tests/AK_profile_xfer.toml`; `Z_release_check.toml` checks that the empty file
gives the release behaviour. The dynamic render scale, the late-frame rule and CAS are on by default in both.
[../CLAUDE.md](../CLAUDE.md) is the engineering log and process guide of the port, written for people porting another
game with the same tools.

## Checks and tools

- `tools/check_switch_build.py` checks the ELF of a Switch build.
- `tools/win/try_flavor.ps1` and `tools/win/switch_variant.ps1` build and test codegen variants (see
  [performance-history.md](performance-history.md)).
- `tools/read_before_write.py` and `tools/read_before_write_chain.py` find the recompiled functions that must keep
  `share_registers` and `keep_saves` in `app/overrides.toml`.
- `tools/analyze_runs.py`, `tools/analyze_categories.py` and `tools/hot_order.py` read the console logs and profiles.
- PGO: `tools/win/build_switch_pgo.ps1`, `tools/win/pgo_merge.ps1`, `tools/pgo_summary.py`.
