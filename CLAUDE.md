# CLAUDE.md: porting Xbox 360 games with ReXGlue (PC, then Switch)

> **Layout note for this repository.** This is the published copy of the port: the folders of the development tree
> (`bakugan-nx/`, the shared `sdk/`, the reference ports) are flattened to `app/`, `sdk/`, `shaders/`, `tools/`, `docs/`,
> `mesa/`. Where the text below says `bakugan-nx/<x>` read `<x>`, and where it says "the shared `../sdk`" the SDK is
> simply `sdk/`. Test settings files are in `app/tests/` (see `app/tests/README.md`); a RELEASE build is the same source
> with no settings file (`docs/building.md`, "Test and release builds"). Machine paths are those of the author's
> portable toolchain (`C:\bakugan-tools`, `C:\devkitPro`); adjust them. The text is an engineering log written
> while porting: every entry names the evidence (logs, numbers) behind it.


Read this first; it is the distilled process of the Bakugan port (`bakugan-nx/`),
done by reusing the sa2-nx SDK fork and tools. Reference ports (read-only, upstream copies): `sa2-nx-main`,
`nfsmw-nx-main` (docs/platform-notes.md, toolchain.md, measuring.md, mesa.md), `masseffect-nx-main`, `mesa-switch-main`
(26.2.4: NOT the commit the patch needs, see Mesa). Their `docs/porting-guide.md` (sa2) explains the why.

## Rules (from the user)
- No game data or generated code in git (`assets/`, `app/generated/`, `out/`, `*.xex`, `*.iso`, shader dumps are
  ignored). Every analysis entry (overrides.toml etc.) carries its evidence.
- Test runs are always muted + minimized (`tools/win/boot_test.ps1` forces it by env); never take over the desktop.
  A visible run needs the user's OK. Report results faithfully, including what was NOT verified.
- Switch: face buttons BY LETTER (user decision 2026-10-12, as nfsmw-nx README: each Switch button acts as the Xbox button with the same letter, so on-screen prompt letters = pad letters): `input_xbox_layout = false` in app/bakugan_switch_defaults.toml. The earlier by-position mapping was wrong for this user. true = by position.
  it explicit in `app/<game>_switch_defaults.toml`. The user plays the result, so keep the shipped toml windowed+audible.
- One stage at a time, verify each. The user's game is a disc (ISO) they own; the title ID is in the XEX header
  (`tools/xex_header.py`), not necessarily what they typed (Bakugan: 41560857, not ...58).

## Environment (this machine; no admin; paths are load-bearing)
- Python for tools: `py -3.13 -I script.py` (Windows Python). MSYS2/Git `python` hands tools `/d/...` paths and
  breaks them. The venv `C:\bakugan-tools\venv` has cmake, ninja, pillow, xxhash (use it when you need those).
- Portable toolchain `C:\bakugan-tools`: `llvm` (clang 20.1.8), `msvc` (xwin CRT+SDK: no debug CRT libs, so every
  CMake configure needs `-DCMAKE_TRY_COMPILE_CONFIGURATION=Release`), `venv`, `dxc` (1.9.2609), `msys64` (UCRT64;
  junction `C:\msys64` -> it, `<msys64>\opt\devkitpro` -> `C:\devkitPro`), `rustup`+`cargo` (GNU toolchain +
  aarch64-unknown-linux-gnu), `mesa-switch` (clone at 1a8c1a66d6f + nfsmw patch), `dl` (downloads, deletable).
  `bakugan-nx/tools/win/env.ps1` loads the MSVC-less environment (INCLUDE/LIB from `msvc`).
- `bakugan-nx/sdk` is a junction to the shared `../sdk` (git-ignored there). `sdk/thirdparty` is filled by
  `tools/fetch_thirdparty.py --target ../sdk/thirdparty` (git clones; ~10 min; the root `.gitignore` covers it).
- Shell traps: bash tool: long `sleep` is blocked (use background commands + `until` loops). PowerShell: pass paths
  with spaces QUOTED into `-D...=` args (an unquoted "X360 port" silently empties the CMake variable). Heredocs in
  bash mangle backslashes; use the Write tool for files with `\` or `'`.

## Pipeline (commands are relative to `bakugan-nx/`; copy the folder for a new game and rename `bakugan`)
1. **Extract**: `py -3.13 -I tools/extract_iso.py "<iso>"` -> `assets/game_root` (XGD2/3 handled). Scan for extra
   code: index names + `XEX2` magic in every file (random hits inside compressed data are not modules; a real one
   has small header fields). `tools/xex_header.py default.xex` prints title ID, imports, block layout.
2. **Generator**: configure `cmake -S ..\sdk -B out/host -G Ninja -DCMAKE_BUILD_TYPE=Release
   -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
   "-DCMAKE_C_FLAGS=-march=x86-64-v2" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2"`; build target `rexglue`
   (`tools/win/host_build.ps1`, rebuild after SDK edits: `rebuild_host.ps1`). Output `../sdk/out/win-amd64/rexglue.exe`.
3. **Translate**: `app/<game>_manifest.toml` (+ empty `overrides.toml` with a `[functions]` table, `codegen_flavor.toml`);
   `powershell -File tools/codegen.ps1` (sets `REX_MAX_JUMP_TABLE_ENTRIES=1024`, `REXGLUE_DUMP_IMAGE=out/default_image.bin`).
   Codegen is skipped when inputs are unchanged: delete `app/generated/default/codegen.stamp` to force a re-run.
   Fix each "target not in any function" with `tools/log_to_overrides.py out/codegen.log app/overrides.toml "<note>"`.
4. **Sweeps (re-run after every codegen)**, in this order, each adds evidence-carrying entries:
   `tools/pdata_audit.py` (every `.pdata` function must be generated; "longer" = no-return `bl` followed by EH data
   words, harmless), `tools/switch_check.py` (append its `[[switch_tables]]` output), `tools/pointer_fixpoint.py`
   (`REXGLUE` env = rexglue.exe; BACK UP overrides.toml first: a failed re-codegen leaves entries and a rerun
   duplicates them), `tools/pointer_audit.py`, `tools/gap_fixpoint.py out/gapscan`, then `tools/entry_audit.py`
   and prune entries that sit inside a `.pdata` body or follow a non-terminator (the sweeps accept some: SEH
   `mr r8,r8` scope markers, mid-block fall-through targets). setjmp/longjmp: the generated functions with ~64
   vector stores/loads through r3; set `setjmp_address`/`longjmp_address` ROOT keys (before any table).
   **overrides.toml gotcha**: keys appended AFTER a `[[switch_tables]]` block become fields of that table and are
   silently ignored. Per-function keys: `size`, `end`, `parent`, `share_registers`, `name`. Jump-table cases that
   land in other `.pdata` functions: declare the parent function larger (`size`), not fragments.
5. **PC build**: `tools/win/configure_pc.ps1`, `build_pc.ps1` (`build_pc_keepgoing.ps1` lists all errors; use a
   background process for long builds). Exe `out/pc/<game>.exe`; `<game>.toml` next to it. Boot test:
   `tools/win/boot_test.ps1 -Seconds N` (log tail printed). The app is `app/src/main.cpp` + `<game>_app.h`
   (paths, gpu plugin, native-renderer switch, capture) + crash log + debug hooks.
6. **Seeing and driving the game while minimized**: `bakugan_shot_s/_count/_every_s` PNG snapshots from the
   presenter (env `REX_BAKUGAN_SHOT_S=...`), and the SDK cvar `input_script`
   (`REX_INPUT_SCRIPT="start_ms:dur_ms:0x1000,..."`, A=0x1000 B=0x2000 X=0x4000 Y=0x8000 START=0x0010, times from the
   first input poll). PNGs are read with the Read tool. `tools/win/boot_test.ps1` clears nothing: shots overwrite.
7. **Native renderer** (stage 4): `app/src/me` is the sa2/banjo renderer copied with `sa2`->`<game>` renames; only
   `<game>_me_hooks.cpp` is game specific (draw-time shader load + device VS/PS offsets, Draw* hooks).
   Find the D3D functions WITHOUT a reference image: `tools/pm4_sites.py` (functions building PM4 headers),
   `tools/d3d_census_build.py` + `tools/win/build_census.ps1` (counting hooks over the D3D library range, run
   `-Exe out\census_pc\<game>.exe`; reads `census.txt`), `tools/show_func.py <addr>` (listing). Details and the
   Bakugan results: `bakugan-nx/docs/d3d-notes.md`. Shader library loop: `tools/par_extract_shaders.py` (when the
   game ships containers), run native with `tools/win/discover_pass.ps1 -Name pN -Script ...`, then
   `tools/wrap_discovered.py --raw out/disc/pN/discovery --containers <all container folders> --out out/shaders/wrapped`,
   then `tools/win/build_shader_library.ps1 <folders...>` (XenosRecomp + DXC + packer, ~10 min), copy
   `out/shaderlib/*.mesp*` next to the exe. Done = log says "unidentified loads: VS 0, PS 0, immediate 0" in every
   mode. Compare native vs emulated with PNGs (PIL ImageChops); animation phase differs, so compare still screens or
   add lockstep (`vblank_per_swap`) before claiming equality.
8. **Switch**: build NVK once (see Mesa), then `tools/win/build_switch.ps1 -Jobs 8` (forces `DEVKITPRO=C:/devkitPro`;
   needs `libzstd.a` built with devkitA64 into the Mesa SDK lib dir) and `tools/win/package_switch.ps1`.
   Then optimizations (LTO/PGO/hot ordering/direct state: sa2 `docs/porting-guide.md` ch. 9-11), each pixel-identical
   and measured on the console. NOT done for Bakugan.

## Mesa/NVK recipe (one time)
`git clone danfromtico/mesa-switch`, `git checkout 1a8c1a66d6f`, `patch -p1 < nfsmw-nx-main/mesa/mesa-switch-nfsmw.patch`
(the repo's `mesa-switch-main` is 26.2.4 and does not take it). MSYS2 UCRT64 packages: gcc clang llvm
spirv-llvm-translator pkgconf cmake python(+packaging setuptools yaml mako) meson ninja rust-bindgen cbindgen. In the
checkout replace `clang64/clangarm64/mingw64` with `ucrt64` in `windows_x64_host_cross_file_msys2.txt`,
`switch_native_tools_msys2.txt`, `host-tool-msys2-wrapper.sh`, `rustc-*-wrapper.sh`. Run `build-unified.sh` from
`C:\msys64\usr\bin\bash.exe -lc` with MSYSTEM=UCRT64, HOME/USERPROFILE set (meson cmake lookup dies with "Could not
determine home directory"), `ALLOW_DIRTY=1`, `SOURCE_DATE_EPOCH`, `RUSTUP_HOME/CARGO_HOME`,
`RUSTUP_TOOLCHAIN=stable-x86_64-pc-windows-gnu`, `MESA_SWITCH_RUSTC=<cargo>/bin/rustc.exe`, `DEVKITPRO=/opt/devkitpro`.
~25 min. Script used: `C:\bakugan-tools\dl\mesa_build.sh`. "rustfmt not installed" errors are harmless.

## Bakugan: Battle Brawlers facts (title 41560857, `bakugan-nx/`)
- XEX: bg.exe, base 0x82000000, entry 0x823BCB28, code 0x82120000-0x829780C8, imports xam+xboxkrnl only, no extra
  modules. Data: `media/d00.arc` + `d00.bin` (PAA index), `.pam` movies, `media/shaders/*.par` (1,858 shader containers).
- Codegen: 46k functions, ~430 override entries, 1 switch table, setjmp 0x828B3870 / longjmp 0x828B3550.
  Details/open items: `docs/stage2-notes.md`.
- D3D: device 0x40008900; shader load `sub_82667508`, bound VS +12872 / PS +12868; DrawVertices `8264C4B0`,
  DrawIndexedVertices `8264C8A0`, Begin/EndVertices `8264BA18`/`8264B9F8`, DrawIndexedVerticesUP `8264BEF8`.
- Status (2026-10-13 evening): **visually complete 1:1 port on PC and on the Switch**. Verified by the user on the
  console: menus, all arenas, cut-ins, gate cards, story intro (portal fixed on PC, console confirmation of that last
  fix pending), park scene, name entry via the Switch keyboard. Remaining: **performance** (menus ~25 fps, battles
  5-15 fps on the console; target 30 fps) and two minor items below.

## Milestone: visually complete baseline (2026-10-13) and the performance goal
- **Baseline (backup before any optimization)**: git tags `bakugan-nx-visual-baseline` (before the portal fix) and
  `bakugan-nx-visual-baseline-v2` (current), branch `bakugan-nx-baseline` (+ backups in `D:\Claude\_backups`, see
  "Repository state"). Known minor items: the **neutral arena floor sometimes renders black instead of shiny**
  (intermittent, ~2 of 11 PC runs, cause unknown, see the notes below) and the **Darkus arena is untested** (locked;
  probably fine, shares materials with Haos). Do optimizations on branch `bakugan-nx` (or a new `optimization` branch)
  and compare images against the baseline tag; never merge a change that alters pixels without saying so.
- **Performance goal**: the stock Xbox 360 rate. The game runs at **30 fps** (PC logs: 300 Swaps per 10 s in menus and in
  matches; the console is vsync-locked at 30 by a swap interval of 2). Console today: menus 25 fps, battles 5-15 fps.
  Measured cost in battle (GPU per frame): depth conversions (`edram_import`) 105-130 ms, colour conversions ~25 ms,
  everything else small. CPU is not the limit. Read nfsmw-nx `docs/performance-history.md`, `measuring.md`,
  `native-renderer.md` before touching it. First ideas: the speed tests in `out/nx/toml_tests` (EDRAM modes 3 / 1,
  540p) and making the depth import cheaper.

## Repository state and backups (2026-10-13)
- Branches `bakugan-nx` (working), `bakugan-nx-baseline` (the 1:1 port, kept equal to the last verified visual state;
  fast-forward it only when a visual fix is verified) and `main` all point to the same commit after the baseline
  update; tags `bakugan-nx-visual-baseline` (before the portal fix) and `bakugan-nx-visual-baseline-v2` (with it).
  The push needs the user (GitHub sign-in): `git push origin bakugan-nx bakugan-nx-baseline main --tags`.
- Backups outside git: `D:\Claude\_backups\X360-port_2026-10-13.bundle` (all branches, restore with
  `git clone <bundle>`) and `D:\Claude\_backupsakugan-nx_2026-10-13_assets.zip` (shader containers + library, the
  Switch SD staging files, toml tests, PC play toml, the PC save).
- Cleanup done: diagnostic folders in `bakugan-nx/out` (floor_run_*, arena_*, green_*, texdump, emu_shots, montage
  PNGs, `out/pc/captures`, `out/pc/native_shots`, old PC logs) and the staged 3.2 GB game copy in
  `out/nx/sdcard/.../game_root` (the SD card has its own; the source is `assets/game_root`). `bakugan-nx/sdk` is a
  JUNCTION to `../sdk`: never delete through it. Regenerable but kept: `out/host` (codegen tool), `out/pc`, `out/nx`
  (build trees), `out/shaders` + `out/shaderlib`, `out/glslang`.

## Switch hardware debugging log (what was learned; read before changing anything)
- **Run layout on the SD card** `sdmc:/switch/bakugan-nx/`: `bakugan.nro`, `bakugan.toml` (test settings, overrides the
  NRO's built-in defaults), `bakugan_shaders.mesp` + `.idx`, `game_root/` (default.xex + media/, 3.2 GB; `$SystemUpdate`
  and `nxeart` are skipped on purpose), logs go to `logs/` and `logs/rex/rex_profile.log`, shader dumps to
  `shader_dump/{discovery,missing,variants}` when `bakugan_shader_misses = "shader_dump"`. Staged copy of all of it:
  `bakugan-nx/out/nx/sdcard/switch/bakugan-nx/`; test tomls in `out/nx/toml_tests/`. The user copies logs/dumps back
  to `<repo>\logs` and `<repo>\shader_dump`.
- **First hang (black screen, console unresponsive)** = the SDK built a 3.2 GB asset pack at first boot (default on
  Switch). Fixed by `asset_pack = false`, `asset_pack_build = false` in the toml (the shipped defaults should keep that
  for this game; the loose files are read directly; first boot scans 8,085 files ~18 s, then a VFS index is cached).
- **Second hang (game never loaded)** = a truncated `d00.arc` on the SD copy (698 MB instead of 2,510,774,272). Not a
  port bug; found with `sa2_io_trace` + a probe in `sdk/src/core/filesystem_switch_read.cpp`. Verify copies with
  `tools/win/verify_sd_copy.ps1 -Sd E:` (names + sizes against `assets/game_root`). Reads past 2 GiB of a loose file now
  go through `fsFileRead` with a 64-bit offset (kept: harmless and correct).
- **Diagnostics that exist and work**: `sa2_io_trace = N` (first N opens/reads + every guest thread start, info level),
  `switch_profile_stacks = true` (stack samples in `logs/rex/rex_profile.log`; resolve host addresses with
  `aarch64-none-elf-addr2line -f -e out/nx/bakugan 0x<image offset>`: names are `sub_<guest addr>`),
  `bakugan_gpu_marks_categories = true` + `bakugan_gpu_marks_each_pass = true` (log line "GPU time by pass" and
  "GPU per Swap": per-category GPU ms; multiply by 1.627 for real time), `bakugan_shader_misses`. Guest thread starts
  are also logged on PC. A PC reference log for the boot I/O sequence: `out/pc_trace_ref.log`.
- **Controls**: user decided by LETTER (`input_xbox_layout = false`): Switch A = Xbox A so on-screen letters are the
  pad's letters (as nfsmw-nx). Earlier by-position mapping was rejected after the first hardware test.
- **Name entry**: `XamShowKeyboardUI` now opens the Switch keyboard applet (`sdk/src/kernel/xam/xam_keyboard_switch.cpp`,
  called from xam_ui.cpp under `__SWITCH__`). User confirmed it works.
- **Shader library loop** (done 3 times): user plays with `bakugan_shader_misses` set -> `shader_dump/discovery/*.ucode`
  -> `py -3.13 -I tools/wrap_discovered.py --raw <dump>/discovery --containers out/shaders/par out/shaders/missing*
  out/shaders/wrapped* --out out/shaders/wrappedN` (+ copy `<dump>/missing/*.bin` to `out/shaders/missingN`) ->
  `tools/win/build_shader_library.ps1 <all those folders>` (~12 min, run in background) -> copy
  `out/shaderlib/bakugan_shaders.mesp*` to `out/pc` and the SD staging folder. Library now 1,880 shaders. The log line
  "unresolved draw shader pairs ... PS=0" lists VS hashes with no shader (=> draws dropped => black UI/text/models).
  A new screen the user had not reached before shows new hashes: that is normal, not a regression.
- **Performance state (battle)**: GPU-bound, ~185-200 ms GPU per frame: `edram_import` (depth conversions between
  EDRAM representations, "depth-repr-switch", ~10 imports/frame of ~1000 tiles) 115-130 ms, `edram_alias` ~25 ms (45
  before `bakugan_native_conversion_frag = true`, now default), 640-wide passes ~22 ms, real scene drawing small.
  CPU is not the limit (ring thread ~5 ms/frame). Ideas not yet measured on console, test tomls ready in
  `out/nx/toml_tests`: `bakugan_native_edram_alias_mode` 3 / 1 (less exact), `bakugan_render_height_handheld = 540`.
  If those do not help: make the depth import itself cheaper (stencil/copy-engine path, avoid repr switches, ZCULL
  planes; see `bakugan_native_edram4_*` cvars in `app/src/me/native/bakugan_native_targets.cpp`) and read
  nfsmw-nx `docs/measuring.md`, `performance-history.md`, `mesa.md`.
- **Arena grain/noise: SOLVED on PC (2026-10-13), console confirmation pending.** Cause: the lit materials draw opaque
  with the alpha test `RB_COLORCONTROL 0x8700000E` = enabled, function `>=`, reference 1.000. The Xbox 360 render
  backend rounds the alpha of an 8-bit target to 8 bits before the test (0.998 passes); a float compare fails and
  punches salt-and-pepper holes (grain, "semi-transparent" assets, cards in hands, arena floors). Fix:
  `bakugan_native_alpha_test_8bit` (default true) in `bakugan_native_draws.cpp` compares round(alpha*255): `>=` ->
  threshold (ref8-0.5)/255, `>` -> `>=` (ref8+0.5)/255, `<=` -> `<` (ref8+0.5)/255, `<` (ref8-0.5)/255; `==`/`!=`
  unchanged. Found by: the emulated GPU showed the same noise (so not our renderer), bisecting by skipping vertex
  shaders (`REX_BANJO_NATIVE_SKIP_VS="121,793,1738"` cleared it), then logging the draws' state (`BAKUGAN_DIAG_VS=<n>`
  prints RB_COLORCONTROL, alpha ref, blend, depth state, vertex streams and VS constants of that VS's first draws;
  `BAKUGAN_DIAG_PS=<n>` checks PS constants for NaN). Ruled out on the way: mipmaps, PCF, pre-pass skip, ZCULL,
  depth samples x, aniso, vertex dedupe, texture binding thread / cross-frame cache, I/O caches
  (`sa2_io_verify=true` re-reads every cached read), texture decode (155 DXT textures + cubes dumped with
  `BAKUGAN_DUMP_TEXTURES=<dir>` and viewed), alpha-to-mask (not used), vertex data and constants (clean). PC recipe to
  reach the green arena: Battle Arena -> A (character) -> third battlefield icon (Left x2 from Random) -> A, with
  `REX_INPUT_SCRIPT='15000:300:0x0010,23000:300:0x0010,31000:300:0x0010,55000:300:0x0010,65000:300:0x1000,75000:300:0x1000,100000:300:0x0002,105000:300:0x1000,120000:300:0x1000,126000:300:0x0004,128000:300:0x0004,132000:300:0x1000,142000:300:0x1000,152000:300:0x1000,162000:300:0x1000,172000:300:0x1000'`
  (the battlefield menu is at ~122 s; UP 1, DOWN 2, LEFT 4, RIGHT 8). `tools/win/window_shots.ps1` (needs the user's
  OK) captures a visible window of the emulated GPU for ground truth. PIL: `C:\bakugan-toolsenv\Scripts\python.exe`.
- **Shader loop on PC (faster than console trips), 2026-10-13**: `out/pc/bakugan.toml` has `bakugan_shader_misses =
  "shader_dump"`, `bakugan_renderer_native = true`, `mnk_mode = true` (keyboard: Space/; = A, Backspace/' = B, L = X,
  P = Y, Return/X = Start, WASD = left stick, Shift+arrows = D-pad, 1/3 = LB/RB, Q/E = LT/RT), so any PC play session
  or scripted run writes `out/pc/shader_dump/{discovery,missing,variants}`. The log's "unresolved draw shader pairs ...
  PS=0" lists VS hashes whose draws are DROPPED (floors, symbols, props missing). Fix: `missing/*.bin` into a new
  `out/shaders/missingN`, `wrap_discovered.py --raw out/pc/shader_dump/discovery --containers <all> --out wrappedN`, then
  `build_shader_library.ps1` over every folder (par missing..missing5 wrapped..wrapped5, ~12 min), copy
  `out/shaderlib/bakugan_shaders.mesp*` to `out/pc` and the SD staging folder. A hash that wrap_discovered "skips
  (its microcode is in the real container X)" is covered by a freshly dumped missing/ container: rebuild and re-run.
  Done = no "unresolved draw shader pairs" line and "(0 without shaders)". Library now 1,894 shaders: arenas (wind
  floor, neutral element symbols), story intro and park verified on PC. Darkus arena (locked) untested.
  Story mode on PC: save exists in `Documentsakugan\B13EBABEBABEBABEč60857 00001akugan-title\savegame.txt`;
  script `...,75000:A,105000:A,125000:A,145000:A,165000:A` (Story is the first menu item) reaches the intro.
  Battle arena battlefield order (row 1): neutral(black floor, purple sky), earth?, wind(green), water(blue),
  Random; row 2: fire(lava), locked. Practice question: DOWN then A = "No" starts the match directly.
- **Portal fixed (2026-10-13)**: Xenos blend op 4 (REV_SUBTRACT = dst*d - src*s) was recorded as Vulkan SUBTRACT; now
  `VK_BLEND_OP_REVERSE_SUBTRACT` (cvar `bakugan_native_blend_rev_as_sub=true` restores the old mapping for A/B). The
  story opening's gate (pipeline VS n1334 / PS n1505, blend 01010186) now shows its swirling vortex.
- **Neutral arena floor shine is INTERMITTENT (not a regression)**: black floor with the symbols vs shiny floor with
  sky reflections, same build, same script; seen black 2 times out of ~11 PC runs (flaky_2, neutral_new). It is NOT the
  blend change (old mapping gave a black floor in one run and a shiny one in another). Log diffs between black and shiny
  runs show nothing obvious (only a few `EDRAM mode4 pair` lines). Ideas: texture caches
  (`bakugan_native_cache_textures_between_frames=false` over ~15 runs), resolved-texture timing; a texture dump of a
  black run is still missing (run `BAKUGAN_DUMP_TEXTURES=<dir>` in a loop until the floor brightness < 12 AND the frame
  is not completely black; run 7 of the last loop was a fully black frame, not a black floor). Low priority.
- **Console freeze on the green (wind) arena (2026-10-13 morning)**: happened once with the old library; the user later
  played the wind arena on the console without a freeze after the 8-bit alpha-test fix and the shader libraries.
  If it returns: check `process 3281/3285 MB` in rex_profile.log (memory headroom), then lower
  `bakugan_native_textures_mb_max` / texture pool.
- **Performance stage, round 1 (2026-10-13 afternoon, commits 7c7ed1ad, d02172b2)**:
  * All 12 EDRAM-transfer fragment shaders (`app/src/me/native/shaders/me_edram_*.frag`) use exact float address math
    (`FDiv/FMod/FMad`, operands < 2^20) instead of integer divide/modulo (Maxwell has no fast integer divide, slow
    32-bit multiply). Pixel-identical on PC (8 scripted battle frames, baseline exe `out/pc/bakugan_baseline.exe` =
    state before the change vs new exe). Console result: depth import 107 -> ~14 ms/frame, colour alias 56 -> ~6 ms;
    menus, cut-ins, loading, neutral arena smooth/playable (user report).
  * Remaining cost, busy arenas (water worst ~76-83 ms GPU/frame = 12 fps, ground ~45-50 ms = 20 fps): scene passes
    ("640" cat 3 22-31 ms, "320" cat 4 ~11 ms) and ~180 tiny EDRAM transfer ops per frame (~0.18 ms fixed cost each,
    ~32 ms: 6-tile ops between a 4xMSAA 280x720 layout and 1x 560x720 at the same EDRAM tiles, ~32 draws/frame, plus
    1-tile depth<->colour ops at tile 0 with the 9-pass stencil path). Cost is serialization points (render pass +
    barriers on NVK), not pixels. Per-draw stats (`bakugan_native_stats_per_draw_s = 10`, log "per-draw GPU time"):
    ~540 draws/frame, only ~16 Mfrag; some pixel shaders 15-60 ns/frag (PS n1678, n502, n807); cat 4 PS n1508 94
    draws 9.7 ms (vertex/fixed bound). Ring thread ~75 ms of work/Swap (CPU also near the frame time: "record 42 ms").
    Ideas, not done: merge colour+depth(+stencil) conversions of one sync into one pass, fragment version of the
    depth->colour export (compute now, engine switch), skip provably dead transfers, cheaper tiny passes, shader
    cost of the few heavy PS. nfsmw-nx lesson: 1 ms GPU saved = ~0.5 ms frame when CPU also limits.
  * Diagnostics now available on console: `bakugan_native_edram4_pair_report` (transfer pairs + draws),
    `bakugan_native_stats_per_draw_s` (per pixel shader GPU time/fragments), `bakugan_diag_dump_marks_s = N`
    (full GPU timeline of 6 submissions, repeats every N s; log line "GPU marks of one slot").
    `bakugan_native_stats_pipeline` gives per-pass Mfrag only when per_draw is 0 (they are exclusive).
    Test toml with all of them: `out/nx/toml_tests/G_stats.toml` (also the SD staging `bakugan.toml`).
- **Round 2/3 (2026-10-14)**: console test of round 1+stairs fix: stairs and fountain finish fixed (user confirmed).
  Water arena 720p H test: 67-79 ms/frame (13-15 fps); 540p I test: 50-59 ms (17-20 fps; conversions scale with
  pixels, still fill-bound). Round 2 (52a4f7f1): float address math in the 14 EDRAM compute shaders (depth export
  6.7 -> 1.9 ms). Round 3 (not yet on console): CPU profile of the 540p run (`tools/switch_profile_report.py
  logs/rex/rex_profile.log --elf out/nx/bakugan --max-fps 22`): game thread ~100 % busy spinning in D3D's GPU wait
  (sub_8263E868 -> sub_8265A180, db16cyc loop), SoundThread (sub_82127BA8 loop: voices update + Sleep(0)) ~90 % of
  a core, ring thread ~54 %: 3 cores shared. Fixes (pixel-identical on PC, 8 frames): `cpu/bakugan_gpu_wait.cpp`
  retargeted (was SA2 addresses and not even in CMake) = game thread sleeps until ring progress; new
  `cpu/bakugan_sound_sleep.cpp` = sound loop sleeps `bakugan_sound_sleep_us` (1000) per iteration. Both log
  "[enh] ...: engaged (code check passed)". Listen for audio glitches; PC CPU time fell 327 s -> 68 s per 245 s run.
  Staged toml = H_stacks (720p + stack sampling + timeline dump). Next ideas: merge the ~180 tiny transfer ops,
  fragment depth export, heavy PS (n1678 water surface 15 ns/frag), render-scale option for busy arenas (user decides).
- **Park stairs (missing yellow frames/corners), console only**: not a missing shader (no unresolved pairs). Log:
  "resolved logical crop rejected 19AF3000 64x256: unproven logical fetch layout" + "rejection causes 109 x 1269":
  a 64x256 mipped detail texture was fetched at an address with a stale resolve (640x360) -> the draw was dropped.
  Fix (d02172b2, NOT yet confirmed on console; PC never hits it): `FetchIsOrdinaryTexture()` /
  `ResolvedTextureForFetch` returns nullptr + flag when the failure is UnprovenLayout with scale_1x and the fetch
  has mips or another base: PrepareTexture then takes the ordinary guest-memory path. If stairs are still wrong:
  check "rejection causes" count in the next log.
- **Shader library loop gotcha (fixed)**: `tools/win/build_shader_library.ps1` now puts the msys `usrin` FIRST on
  PATH (env.ps1 removes it; Windows `timeout.exe` otherwise wins and every container "fails"). Library now 1,901
  shaders (+ `out/shaders/missing6`, `wrapped6`); 4 DXC failures (vs_0a9e3ed8..., vs_22d2a3ad..., vs_6d01b49d...,
  vs_cdad975d...) are not referenced by any dropped draw so far.
- **Baseline policy**: `bakugan-nx-baseline` / tag v2 stay at the visually verified state (388c2f5d). The perf shaders
  are pixel-identical (verified) but the baseline is only fast-forwarded after the user confirms the console.
  PC A/B recipe: copy exe to `out/pc/bakugan_baseline.exe` before building; scripted green-arena run (`REX_INPUT_SCRIPT`
  from the shader-loop section, shots at 175 s every 8 s) for both exes; compare PNGs with PIL ImageChops (identical
  = pass). PC runs at the handheld 720p by default (docked 1080p: `bakugan_render_height_docked`, test it too before
  merging render changes; user reminder 2026-10-13: handheld 720p, docked pseudo 1080p).
- **Round 4-6 (2026-10-14 afternoon)**: (a) sleeps measured: the game's frame limiter wakes on time (0.01 ms late), so
  it is NOT the A/V desync cause; (b) the audio is ahead in movies and cutscenes because the game steps its
  animation per frame and runs <30 fps (intro movie 20-23 fps: two threads at 100 %, decode kernels sub_825413B8,
  sub_8291F918 on the decode thread and sub_824AA460 polling loop on the game thread; park/tutorial 21-26 fps: GPU
  bound by the EDRAM transfers and the 1120x1120 scene pass); (c) `sub_8249F830/F858` AddRef/Release took the global
  lock per call: native lock-free versions in `cpu/bakugan_refcount.cpp`; (d) `ProveFullOverwrite` refused every
  Direct3D depth Clear (VS619/PS1626, mode 5) because of the stale RB_COLORCONTROL alpha test although mode 5 runs no
  pixel shader: rule added behind `bakugan_native_edram4_alpha_const` (now ON): console test P vs Q in the same
  scenes: water arena depth import 15 -> 8 ms, total 71-80 -> 65-71 ms; cutscene import 12.8 -> 7.3 ms, 22-25 ->
  ~26 fps, park audio drift ~1-3 s (was worse). PC pixel check identical. (e) Diagnostics: `bakugan_diag_transfer_log_from`
  / `_frames` (in-order transfer + clear decisions per frame), "overwrite proof refused" and "rect-list draw" lines,
  "colour 0 clear plan refused at (source line: count)" (me/native/bakugan_native_targets.cpp PlanClearColor0EDRAM4:
  line of the refusal = rect not tile aligned / owner not a plain RGBA8 colour view). (f) A promotion of repeated small
  imports exists (`bakugan_native_edram4_promote_imports`) but never engages (the repeats are runs of ONE sync call).
  Remaining cost in a cutscene frame (~38 ms): 1120x1120 scene pass 16 ms (222 draws), character mini-pass 560x720
  6 ms (102 draws), conversions ~14 ms, copies 4-5 ms. Next options: main-scene colour+depth clear redirect
  (2 x 1022 tiles), merge the 32 per-frame mini-render transfers, per-draw cost of the scene pass, render-scale
  option (540p = water 17-20 fps) which the user must approve; movie decode kernels (native versions) for the intro.
- **Open items, in order**: (1) console results of the build staged in `out/nx/sdcard/switch/bakugan-nx` (stairs fix +
  G_stats timeline dump: water arena ~1 min with a throw, park scene; send logs + shader_dump); (2) performance
  round 2 from the timeline dump (merge transfer ops, heavy shaders, CPU "record" time); target stable 30 fps handheld
  stock clocks (GPU 460.8 MHz profile already requested by `bakugan_gpu_boost`); (3) neutral arena floor shine
  intermittency; (4) Darkus arena check when unlocked; (5) language files/dubs discussion once performance is done;
  (6) the push (GitHub sign-in; the remote already holds the user's commit f65bad87 with `logs/`, `shader_dump/` and a
  photo: ask whether to rewrite that history before pushing more).
- **Process lessons**: always check what the user actually copied (stale toml/NRO happened twice); state exactly
  which files to copy; keep one change-set per hardware run when possible; tell the user plainly when a result is
  not verified.

- Known risks: a real C++ `throw` hits `__debugbreak` (SDK RtlRaiseException does not unwind); `twi` bounds traps
  only log; `vpkd3d128 FLOAT16_4` mask 3 audited as consistent (pairs) but numerically untested.

## Updating a game after a bug report
1. Reproduce with `boot_test.ps1` + `input_script` + snapshots; read `out/pc/logs/<game>_NNN.log`.
2. "Call to invalid or unregistered function at 0x..." -> add `[functions]` entry (evidence = the log line + caller),
   re-run codegen, rebuild. Illegal instruction / "Switch case out of range" -> `switch_check.py`; symbolize host
   crashes with `llvm-symbolizer` on `<game>.exe`; temporary caller-logging hooks go in `<game>_debug_hooks.cpp`
   (remove when fixed). Black/missing draws natively -> check "unidentified" log lines, run a discovery pass.
3. Record the fix and its evidence in `app/overrides.toml` / `docs/`, commit on a branch.

## Starting another game: checklist
Copy `bakugan-nx` -> `<game>-nx` (without assets/out/generated); global rename `bakugan`->`<game>` incl. cvars
(`<game>_renderer_native`...), file names under `app/src/me` and the CMake lists; new manifest/overrides; keep
`tools/`. Re-derive every game-specific address (D3D functions, device offsets, setjmp, EH quirks); do not reuse
Bakugan's. Check the XDK layout assumptions first (shader object type bytes 6/7, container magic `0x102A11xx` at
object+0x368/+0x28 held for Bakugan; the renderer logs a layout warning when not).

## Round 7 (performance-fidelity branch): movies decoded by FFmpeg (2026-10-14)
- WMV3 movies (.pam, ASF) are decoded by FFmpeg instead of the recompiled XDK decoder (app/src/video/bakugan_video_native.cpp,
  bakugan_video_wmv3.*; keys bakugan_video_wmv3_native (default ON, keep), bakugan_video_wmv3_shadow, bakugan_video_wmv3_simd,
  bakugan_video_dump_every). Evidence: PC shadow mode bit-identical (Y/U/V max diff 0, ~930 frames); console audio/video
  sync fixed (user, 2026-10-14). FFmpeg on the Switch: 6-10 ms/frame logos, 14-21 ms opening.
- Grid ("censorship"-like) artifacts in the logos: NOT the decoder. Console luma/U/V dumps of 16 frames are byte-identical
  to the PC ones, NEON off changed nothing, and the user says it also shows on PC. Cosmetic; parked by the user's decision.
- Tags: perf-fidelity-before-wmv3 (state before), perf-fidelity-with-wmv3 (this state). performance-fidelity == wmv3-ffmpeg.
- Optimisation next: GPU fixed cost in the water arena (transfers), then the optimised codegen flavour + LTO + PGO
  (tools/post_codegen.py, direct_calls.py exist; the build is still "classic codegen", no LTO in app/CMakeLists.txt).

## Round 8 (performance-fidelity branch, 2026-10-14/15): best-fps state before the native 2x MSAA work
- Tag `perf-fidelity-best-fps` (+ bundle and NRO/shader copies in `D:\Claude\_backups\best_fps_20261015`). Water arena 540p:
  22.8 -> ~24.4 fps mean (heavy windows), best stretches 29-30 fps; transfer import 3.5 -> 0.4 ms/frame.
- Done: dynamic scale steps wait for a natural hitch (<=6 s down / 20 s up), render targets not blitted on rescale;
  EDRAM clears: dead-depth colour clears + shadow-map clear through the 1x alias by base/format/pitch (28,800 shadow
  import ops/300 frames gone); WMV3 movies by FFmpeg (keep ON); cube-map shader variables (no gain, harmless).
- Diagnostics (all default off except flight): `bakugan_flight_recorder` (logs/flight.txt, fsync'd line/s with GPU wait
  site, file reads in flight, last open, memory, live files), `bakugan_ablate_vs_groups` (+`_phase_s`, `_arm_ms`),
  `bakugan_render_force_after_s/_height_handheld` (start at 720, hold 540), `bakugan_video_dump_every`.
- Ablation result (GPU ms/frame saved): VS121 group (PS1736/1091 lit materials) 5-17, VS779 ~5, characters (744,1082)
  ~5, water VS800 1-5, post 1135/1813 1-2 each: the cost is per-pixel ALU of the lit materials plus heavy overdraw
  (about 6-11 Mfrag per frame at 540p = 12-20 screens). CORRECTION (2026-10-15): the first idea, that the scene is
  supersampled 2x because the 2x-MSAA guest target is emulated as sample rows, is WRONG. Host rows are pixels (tile
  height 16>>msaa_y is used for pixel rows; a clear rect 640x360 shaded exactly 230,400 fragments), i.e. the scene is
  already single-sample 1120x584 at host. Branch `native-msaa2` (made from this state) therefore has no valid premise
  yet; its real use is the next experiment (overdraw: depth pre-pass / draw order / ZCULL coverage, see below).
  ZCULL (stderr "[nvk] ZCULL: plano ..."): planes exist for 1120x1120 (840x840 at 540p), 640x720, 960x540, 600x600,
  280x720 ...; NO plane for 1280x1280, 1600x1600, 560x720/640x720 (second), 960x960, 1200x1200, 420x540, 480x540 (the
  TRANSFER_DST usage). Main scene depth does have one. Ideas not done: ZCULL planes for more depth images; fewer
  transparent layers; cheaper PS ALU (FP16 is not usable on NAK/GM20B).
- Console hangs: flight file showed a file read (vcm0098.past) never returning inside fs, then an Atmosphere kernel panic
  (errors/fatal_errors). Reads handed fs pointers into guest memory (page protections change under it); all Switch
  reads now go through a host bounce buffer (sdk/src/core/filesystem_posix.cpp, filesystem_switch_read.cpp). One clean
  console run since; keep watching. Hekate SD benchmark clean. Startup-at-540p was NOT the hang cause (a duplicate toml
  key made the run use defaults).
- Test tomls: tools/win/stage_toml.ps1 <name> copies out/nx/toml_tests/<name>.toml into the SD folder. Never put the
  same key twice in a toml (the whole file is rejected, defaults apply, no log file).

- **Branches/tags (2026-10-15)**: `performance-fidelity` = best-fps state (tag `perf-fidelity-best-fps`, backups in
  `D:\Claude\_backupsest_fps_20261015`: bundle, NRO, shader library, toml, CLAUDE.md). `native-msaa2` = new
  experiments branch from the same commit. `bakugan-nx-baseline` is the old pushed state.

- **Round 9 (2026-10-15, branch `native-msaa2`; the best-fps state stays at tag `perf-fidelity-best-fps`)**:
  * Resolution ladder in EIGHTHS (me_resolution_scale.h: q 8 = 720p, 7 = 630, 6 = 540, 5 = 450; ScaleEdge =
    floor((g*q + 3)/8), tile 10q x 2q): dynamic scale steps 720 -> ... -> 450 (floor `bakugan_render_dynamic_min_height_
    handheld` 450, two steps at once when fps < 26 and util > 0.9). Water arena fight at 450p: mean 27.2 fps in heavy
    windows (24.4 at the 540p floor), long stretches locked at 29.9; hardest moments still 21-26 fps.
  * The limiter at 450p is the CPU, not the GPU: GPU "gap between jobs" ~26 ms of a 48 ms frame; ring thread 30 ms of
    work per Swap (79-90 us/draw, 250-580 draws) and the main thread 35 ms. Profile (`AB_hang_stacks` toml +
    tools/switch_profile_report.py with the matching ELF, e.g. out/nx/profile_stepA.elf): ring thread self time
    DrawsVulkanImpl::Draw 19 %, CopyVertices 18 % (all on one load instruction after the prefetch: cold source memory
    while the GPU hammers the bus), Type3 5 %, armDCacheClean 4 %, SampleFingerprint 3 %.
  * Optimised codegen set 1 (`app/codegen_flavor.toml`: cr/ctr/xer/reservation locals, skip_msr, dform_disp_split,
    fctiwz_inline) + tools/post_codegen.py (pch no volatile, 29,277 loop barriers, 90,414 direct calls). Audit
    (tools/read_before_write.py): only the guest setjmp sub_828B3870. NO measurable fps change (the ring thread is our
    native code, not recompiled game code). Classic generated tree backup: D:\Claude\_backups\generated_classic_20261015.
    Hooks that read ctx.lr were rewritten with thread-local flags (sound sleep, movie decoder) so set 2 (elide_lr_stores,
    non_volatile_as_local, fp_moves_modeless) is possible; not done yet.
  * Ring thread micro-fixes: NEON conversion of 16-bit strip/restart indices (Draw self 36 -> 23 samples/interval),
    vertex copy prefetch 2 KB (no effect), vertex zone (`bakugan_native_vertex_zone_mb` 12, verify cvar): static vertex
    ranges (79 % of an arena frame's vertex bytes are identical to the previous frame) are kept in the first 12 MB of
    each upload slot buffer and bound in place; 3.4 GB not copied in one fight, 0 mismatches over 20,000+ checked hits,
    no visual errors reported. Its fps effect was NOT clearly visible (that fight had 420-580 draws/frame, 80-90
    us/draw): a stack-profile run of the zone build is staged to see where the ring thread time went. If a visual
    glitch ever shows up (flickering/stretched geometry), suspect the zone first: `bakugan_native_vertex_zone_mb = 0`.
  * Diagnostics added: `bakugan_diag_draw_log` ([drawlog]/[drawfrag] per draw of the diagnostic frame, with
    `bakugan_native_stats_per_draw_s`), `bakugan_native_diag_vertices_repeated`, ablation (`bakugan_ablate_vs_groups`).
  * Findings: scene overdraw after early-Z is only ~1.6x; the largest fragment counts are the cheap 1280-pitch post/UI
    passes; FP16 is not usable (NAK/GM20B); the cube-map local-array rewrite gave nothing. Never put one key twice in
    a toml. The SD staging folder holds only nro + shader library + the toml of the current test (I swap tomls).

## Round 10 (CAS presenter effect)
- `present_effect = "cas"` (+ `present_cas_additional_sharpness`) sharpens the upscaled image. It now applies only while the
  renderer runs below native scale: `rex::ui::SetPresentSharpenWanted(scale_q < 8)` (SDK presenter.cpp, called from
  bakugan_native_targets.cpp at init and on every rescale); at 720p the effect is plain bilinear.
- Test toml AG_cas_strong (sharpness 0.7, up from 0.0 default in AF_cas450). Untested on console at the time of writing.
- Open finding: ring thread waits on the slot fence 3-6 s per 10 s at 450p (record and GPU serialized); not yet fixed.

## Round 11 (2026-10-15, GPU: where the heavy-frame time goes)
- Heavy water-arena frame at 450p (AC_profile_450 / AH_profile_draws, bakugan_029/030): 46 ms GPU = main scene pass
  ("640", pitch 640-1279) ~25 ms + character pass ("320") ~10.5 ms + EDRAM import/export/alias ~5.5 + copies 1.5. GPU is
  the limiter (91-93 % busy, no gap) - the earlier "CPU-limited, GPU gap 26 ms" reading came from light scenes under the
  30 fps cap. Ring-thread slot-fence waits are the CPU waiting for the GPU, not a serialization bug.
- New report: the 12 most expensive individual draws of the diagnostic frames ("top draw N: ... ns/vertex"). Result: only
  VS n966 (main pass) and VS n1628 (character pass), lit skinned meshes of ~5,000 vertices, cost 2.5-2.9 ms per draw
  (~500 ns/vertex, even with 0 fragments); everything else is normal.
- Tried and NO gain (kept, default off): `bakugan_gpu_indexed_pointer` (SPEC_CONSTANT_INDEXED_POINTER, bit 2): skinning
  palettes (arrays >= 32 registers) read through the constants pointer instead of the dynamic UBO. Active in 157,000 PC
  draws, GPU time unchanged -> divergent UBO indexing was not the cause.
- Cause found in the HLSL: those VS have a backward conditional jump (the compiler's do { } while (p0) light loops) so they
  were translated to the `while (true) switch (pc)` dispatcher (201 VS), which the source comments already record as ~5 us
  per vertex on the Switch (forward-only shaders were fixed by structuring in round 2). shader_recompiler.cpp now also
  structures properly nested backward conditional jumps as do { } while under SPEC_CONSTANT_STRUCTURED_CF (hardware aL
  loops and unconditional backward jumps keep the dispatcher; the dispatcher stays as the else branch). All 201 got the
  structured form; library 76 -> 87 MB. Pre-change library backups: D:\Claude\_backups\shaderlib_20261015(_ptr).
- A new shader library invalidates the pipeline cache: the first battle after it shows a 1-1.6 s stall (black screen,
  audio continues) while pipelines compile; the next run is clean.
- Scratch tool for one container: scratchpad try_one.sh pattern (build translator with env.ps1, run on in/vs_<hash>.bin, DXC).
- **Result of the structured light loops (bakugan_032, water arena, AG_cas_strong, 2026-10-15)**: heavy windows mean 29.4 fps
  (min 24.2 at a step transition, 2 of 36 windows below 28; bakugan_028 before: mean 24.5, 15 of 21 below 28), the ladder
  sits at 540/630p (450p only 6 windows, 720p 2). Backup of this state: `D:\Claude\_backups\best_fps_loops_20261015`
  (NRO, library, toml, CLAUDE.md), tag `perf-loops-structured`. User: sharpness (CAS 0.7, only below 720p) is
  "perfect, barely noticeable"; keep as is.
- The ~1 s black screen before a battle (+ audio crackling) was the first-sight compile of pipelines: list entries from the
  previous run carried spec bit 2 (indexed pointer) that is off now, so they were not prewarmed (42 compiled at 273 ms
  each at startup, more at the battle). The 241 "requests without data" in one audio interval = the same stall. Should be
  gone on the next run with the same library/keys; for a release ship a warmed `cache/bakugan_native_pipelines.bin`.
- Exit crash seen (rex_crash.log: std::terminate std::system_error at shutdown, after the last log lines): not investigated.
- **Profile of the new baseline at 450p (bakugan_034, AH_profile_draws, full water-arena battle)**: GPU busy only
  15-20 ms of the 33 ms frame (640 pass 5-10 ms, 320 pass 2-3 ms, EDRAM alias 2.8 + copies 1.7 + no-depth scene 1.5 +
  export 1.2 + import 0.5 = ~7.7 ms of emulation overhead). VS n966 is 68 ns/vertex now (was 520). Serialized per-draw
  ~12 ms/frame: PS n1736 (lit material through VS n121) 2.4 ms, PS n1508 character pass 1.6, PS n1430 0.8, post/UI
  full-screen passes (cat 8) ~1.6. Ring thread 21-27 ms of work per Swap (60-75 us/draw, 300-460 draws): the next
  limiter if the GPU gets faster. User: black screen now behaves, crackling gone, "resolution looks worse" only because
  the profile toml pins 450p. Estimate for the heavy moments: 630p ~33 ms GPU (matches 86 % busy, 29 fps), 720p ~40 ms:
  720p in heavy moments needs the scene pass ~40 % cheaper (depth pre-pass / more ZCULL planes / culling), not micro-opts.

## Release state (2026-10-15, branch native-msaa2, baseline = tag perf-loops-structured + defaults)
- The NRO's built-in defaults (app/bakugan_switch_defaults.toml) now also carry `present_effect = "cas"` and
  `present_cas_additional_sharpness = 0.7` (dynamic render scale was already on, floor 450p handheld / 720p docked).
  Z_release_check.toml (logging only) verifies that the defaults alone reproduce the AG_cas_strong behaviour.
- Pipeline cache for the first battle: after a few battles on a console, copy `<SD>\switch\bakugan-nx\cache\
  bakugan_native_pipelines.bin` to `bakugan-nx\release\cache\`; tools\win\package_switch.ps1 then packages it.
  (A stale rex_crash.log in logs/rex is NOT from the latest runs: the thread ids and the NAK compile frames are from an
  older session; the "exit crash" suspicion in this file above was a misreading of copied-file times.)
- Docked mode on PC (REX_BAKUGAN_RENDER_DOCKED=true): 1920x1080 at 12/8, runs 90 s without errors.
- Next experiments (separate branches, baseline stays): B depth pre-pass / overdraw, C cheaper EDRAM transfers.
- **Logs policy (2026-10-15)**: release = built-in `log_single_file = true`, `log_max_files = 1` (SDK cvar log_single_file:
  logs/<app>.log replaced at every start, rotated copies removed) and logging off unless the player enables it; the
  flight recorder now writes one file per run (logs/flight.txt, previous run as flight.prev.txt). Closed beta / test
  tomls (out/nx/toml_tests/*.toml, except Z_release_check) set `log_single_file = false` = numbered bakugan_NNN.log as
  before. Shader dump (`bakugan_shader_misses`) is off in release: it only exists to collect shaders the library
  lacks; the 6 known misses are already packed.

## Round 12 (2026-10-15 evening): release candidate 1 and the fire-arena fixes
- **Fire arena throw/steer lag (edram_export 12-29 ms, import9 up to 20 ms per Swap)**: cause was the "promoted import"
  (`bakugan_native_edram4_promote_imports`, added for the shadow map): after a few small imports in a frame it widened the
  area to the WHOLE target image. The colour image is 1280 rows tall but the game uses 720, so 560 tiles of the main scene
  depth (+ stencil) were converted into colour ~11x per frame (~2.5 ms each), and the 16x1536 strips into the 1600x1600
  depth target became whole-image 9-pass imports. Fix: promotion only when it brings in <= 200 tiles
  (`bakugan_native_edram4_promote_max_tiles`; the shadow map's are 80-91, so the water arena is unchanged). Found with the
  `[xfer-big]` diagnostic (first 40 depth->color transfers >= 200 tiles with their draw). Lag gone on console.
- **Black arena (HUD only) in the water arena after the fire arena**: `SearchResolvedPartial` accepted a resolve whose base
  merely landed on a tile of an older 1280x720 texture at x = 576 (the log showed "partial resolve ... local destination
  576,0" + "the 1280x720 scene is downscaled to 704x720"). Candidates always have the segment's width, so only x = 0 can hold
  the segment: now `x + dx + segment_width <= candidate width` is required. Works on console.
- **Release configuration** (app/bakugan_switch_defaults.toml, built in): logging on and ONE replaced file
  (`log_single_file = true`, `log_max_files = 1`; SDK logging.cpp; flight recorder per run + flight.prev.txt), CAS 0.7
  below native, dynamic scale on, `asset_pack = false` / `asset_pack_build = false` (the SDK default on Switch builds a
  3.2 GB pack at the first start = the first black-screen hang; the release defaults had lost them), profiler report/stacks
  off. Test tomls set `log_single_file = false` (numbered logs). Start-up check of the game files (default.xex 11,055,104,
  d00.arc 2,510,774,272, d00.bin 387,296 bytes; skipped with `<root>.nxpack`) and a text screen with the reason when the
  start fails (sdk switch_fatal_screen.cpp). NRO: name Bakugan Battle Brawlers, author bakugan-nx, version 0.1.0, icon from
  app/icon.jpg (256x256). Folders: `out/nx/sdcard` (tests, stage_toml.ps1) and `out/nx/sdcard_release`
  (tools/win/package_release.ps1; `release/cache/bakugan_native_pipelines.bin` is packaged when present).
- **Cold pipeline cache is NOT the cause of battle lag spikes** (first-run log: 3 ring-record stalls in 13 min). The spikes
  were rescale steps (15 steps, 13 hitches of 150-280 ms) and game-thread hitches (100-250 ms, ~2-3/min, cause unknown).
  Controller change: a height the GPU could not hold is blocked for 90 s doubling (`blocked_until`, `failures`).
- Floor stays 450p handheld / 720p docked (360p was tried in the controller and reverted at the user's request; it works
  on PC: 4/8 scale applies cleanly).
- **Ring thread profile (stack sampler, 23 fps arena window)**: Draw 14 %, BeginRecording (command pool reset,
  vk_meta_object_list_reset) ~13 % of running samples, allocator + locks ~19 %, EDRAM bookkeeping of the thin-strip ops (~30
  tiny imports + ~30 exports per Swap) ~7 %, ProveVertexFetchReorder 3 %. In the 30 fps run (bakugan_040) the meta reset
  is only 2 % and the arena holds 30 fps with the ring at 28-33 ms/Swap (profile overhead included). Counters added:
  `[native] meta-object commands` (copy_image ~32/Swap, buffer_to_image ~1/Swap, no blit/resolve/clear_color).
- Open: the ~3 s mid-battle freeze in the fire arena (flight recorder itself starved; not IO/GPU) seen once; long load
  stalls (14 s / 6 s into a battle); menus at ~23 fps (game thread); exit-time crash suspicion was a misreading of file
  times (stale rex_crash.log).

## Standard test (from 2026-10-15 20:40) - use this for every performance comparison
- **Build**: the NRO under test + shader library; **toml `AK_profile_xfer`** (release behaviour: dynamic scale, CAS 0.7, native
  renderer; plus the CPU stack sampler, GPU marks per category, and one serialized diagnostic frame every 10 s: per-draw GPU
  time, top draws, GPU time of every EDRAM transfer pair). Numbered logs (`log_single_file = false`). Matching unstripped
  ELF kept as `out/nx/profile_<tag>.elf`.
- **Procedure**: ONE ARENA PER LAUNCH, in this order: 001 water, 002 fire (include throws/steering), 003 haos, 004 earth,
  005 wind, 006 neutral; 3-5 minutes of fighting each, quit from the game menu, copy `logs/` (numbered log, flight.txt,
  `logs/rex/rex_profile.log` - it is overwritten every launch, so copy it after each arena if stacks matter).
- **Analysis**: `python tools/analyze_runs.py logs water,fire,haos,earth,wind,neutral` (per arena: battle fps, share of
  windows >= 28 fps, time at each height, steps, hitches by type, ring thread ms/Swap, GPU busy) and
  `python tools/analyze_categories.py logs ...` (GPU ms per category by render height).
- **Baseline (build faf43aed + diag, AK run 2026-10-15 20:12-20:35, handheld, floor 450p)**:

  | arena | battle fps mean | windows >= 28 | time at 450/540/630/720p | steps | ring ms/Swap (us/draw) | GPU busy ms |
  |---|---|---|---|---|---|---|
  | 001 water | 29.2 | 88 % | 81 / 6 / 0 / 12 % | 2 | 24.0 (63) | 19.6 (max 36) |
  | 002 fire | 24.6 | 22 % | 55 / 0 / 0 / 44 % | 2 | 38.0 (123) | 20.5 |
  | 003 haos | 28.7 | 75 % | 0 / 25 / 65 / 10 % | 2 | 31.6 (113) | 25.5 |
  | 004 earth | 29.8 | 100 % | 0 / 0 / 66 / 33 % | 1 | 19.1 (72) | 23.0 |
  | 005 wind | 29.9 | 100 % | 720p 100 % | 0 | 19.7 (78) | 24.2 |
  | 006 neutral | 29.9 | 100 % | 720p 100 % | 0 | - | 20.7 |

- **Findings from that run**: (1) GPU busy time grows ~pixels^0.36, not with the pixel count: 18.6 ms at 450p vs 19-24 ms at
  720p; the controller's pixel-ratio prediction (47 ms) kept the water arena at 450p for 81 % of its fights. Changed to the
  height ratio (pixels^0.5, still conservative) in `DynamicScale`. (2) The fire arena is CPU-bound: ring thread 38 ms/Swap,
  123 us/draw (water 63) with the GPU at 20 ms busy: resolution does not help there; the ring thread needs its own profile
  (the stack log of that run was overwritten: re-run the fire arena alone and copy `rex_profile.log`). (3) Per-draw stage
  timing: `uploads` is the largest single stage (7-15 us/draw), then targets/bias/indices/vertices 4-5, framing 3.5,
  record 3. (4) Serialized transfer costs (water, 450p): D000 640x720 -> C000 640x720 depth export ~1.0 ms (1.6 at 630p),
  C000 1120x1120 my1 -> 1280x1280 0.8-1.3 ms, C000 560x720 mx1my1 -> 1120x1120 0.6-1.0 ms, 25 thin-strip ops ~0.95 ms.
  Cost per big conversion ~0.4 ms fixed + 1.45 ns/pixel (serialized, includes the diagnostic marks).

## Round 13 (2026-10-15 night / 2026-10-16): the deferred worker was the fire arena's bottleneck
- **Cause (stack profile of the deferred-recording worker, `tools`-less scripts in the scratchpad; method: thread "?" at
  priority 0x2C pinned to core 0, stacks of intervals < 28 fps resolved with the matching ELF)**: ~80 % of its time was
  `vkCmdCopyImage` -> `nvk_cmd_copy_image_meta` -> `vk_meta_copy_image` -> `copy_draw` (this NVK build records image copies, depth
  and colour alike, as meta draws: image views created per call, dynamic state saved/restored, pipeline bind, draw, views
  destroyed at the command-pool reset). ~32 one-tile depth copies per Swap (D000 1600x1600 -> D0E0 560x720 and the like). The
  ring thread waited 3.7-5.7 s per 10 s at `vkEndCommandBuffer` for the worker (the new "drains by call site" log line).
- **Fix**: `CopyTilesEDRAM4`: depth tile runs go image -> buffer (slot.buffer_stencil_copy, now also TRANSFER_DST) -> image through
  the copy engine (`bakugan_native_depth_copy_via_buffer`), colour runs through the colour conversion fragment pass
  (`bakugan_native_copy_tiles_frag`, plain-copy mode of `ConvertAliasColorFragEDRAM4`). Result on console (build 21:47):
  fire arena 26.3 -> 29.6 fps mean, windows >= 28 fps 36 % -> 92 %, ring 35 -> 17 ms/Swap (128 -> 59 us/draw), drain wait
  3.8 s -> 0.03 s per 10 s. Water 28.1 fps (57 % >= 28, GPU bound at 630p: scene pass 12.6 ms at 630p), story 29.9 fps (99 %).
  AL/AM (worker affinity any core / core 2) did not help and are NOT adopted (AM: audio robotic, 24.6 fps).
- **Story run (50 min)**: 13 resolution steps; each step remakes ALL images (63 at the start of a session, 116 after an
  hour: 190 ms -> 510 ms, "create" 120 -> 340 ms) = the "big lag spikes everywhere". Steps were triggered by single 3 s windows
  at 28 fps. `bakugan_render_dynamic_down_fps` (default 27.5, was 28.5 hard-coded) now has to be undercut to step down.
  Open: skip or lazily rescale idle images (resolved textures not used for minutes) to make a step cheap.
- Other hitches of the story run (68 of >= 100 ms in 50 min, game thread): during them the CPU is idle and the game thread
  sleeps in its own KeDelayExecutionThread (sub_823BE670 <- sub_823BC230) or waits for the ring; no I/O or GPU cause found.
- Tools: tools/analyze_runs.py <logs> <labels> [first number]; tools/analyze_categories.py.
- **Story cutscene stutter (log 015, 23:26-23:27, vcm0006, ~70 s)**: average 29.9 fps and 83 % GPU busy at 630p looked laggy.
  The frame limiter made 123 / 224 / 193 sleeps in three 10 s windows of ~296 frames (a frame that overran has nothing to
  sleep for) and the ring waited 7-15 ms per frame on the slot fence (GPU behind): 33/50 ms judder invisible in the mean
  fps. The controller only looked at mean fps. New: `bakugan_render_dynamic_late_frac` (0.25) + `_late_util` (0.75): a 1.5 s
  window with >= 25 % late frames and GPU > 75 % busy counts as a hot window (2 in a row = one step down). Window log line
  now prints the late-frame percentage. Also: every >= 150 ms hitch outside rescales/scene loads sits on a save
  create/open/close (savegame.txt 55 KB + thumbnail, on the game thread; 15 of them in 50 min); not changed yet.
- **Save hitches found (2026-10-16)**: sdk/src/system/kernel_state.cpp `kDeferredOverlappedDelayMillis` was 100: every deferred
  overlapped call (the game's XamContentCreate/Open of its save, 2-3 per save) slept 100 ms on the dispatch thread before
  completing, matching every 166-250 ms hitch with idle GPU in log 015 (game thread sleeping in the frame limiter while it
  waited). Now 1 ms. Check: no hitch >= 150 ms next to "[save] XamContentCreate" lines.
- **Cheaper resolution steps (2026-10-16, untested on console)**: RescaleAll made its ~110 images one by one (create 190-340 ms of a
  330-510 ms step, release 50-100 ms). Now the Vulkan objects are created by `bakugan_render_rescale_threads` (3) threads at
  once, and the replaced images are destroyed 8 per frame afterwards (`olds_pending_`, `ReleaseOldSlice`). Expect the "render
  scale" log line to show create ~100-150 ms and release ~0. PC check: forced 720p->450p rescale (REX_BAKUGAN_RENDER_FORCE_AFTER_S) ok.

## Round 14 (2026-10-16 evening): the stutter rule made water/fire/haos sit at 450p
- Test 001 water / 002 fire / 003 haos / 004 story (build 11:28: stutter rule + save delay + parallel rescale). fps up, resolution
  down: water 29.8 fps (98 % of windows >= 28) but 450p 87 % of the time (before: 28.1 fps, 57 %, 540-630p); fire 29.5 (90 %),
  450p 72 %; haos 29.5 (91 %), 450p 71 %; story 29.9 (100 %), 630/720p 55/44 %. GPU busy at 450p: mean 51-54 %, max 65-69 %.
- **Cause (controller bug, old but exposed)**: a decision is only a request; the targets are remade at a calm frame up to 6 s (down)
  / 20 s (up) later. Windows in between still show the OLD height, so decisions stacked: 720->630->540->450 in 6-10 s (all
  applied as one jump), and the climb 450->540->630->720 in 18 s on calm windows (31-50 % busy) applied as ONE jump 450->720,
  straight into the heavy moment (86-93 % busy, 22-26 fps) -> overload, cascade down, block 90-180 s. The yo-yo = overloads +
  3-5 rescale hitches per excursion.
- **Fix**: `Dynamic.pending`: no new decision until the last one is applied (40 s timeout); a height's history (util of its
  1.5 s windows) starts when a rescale is applied; a step up needs `bakugan_render_dynamic_up_windows` (20 = 30 s) windows lived
  at the height and p90(util) x height ratio < 0.80; down wait 6 -> 3 s. Expected: 540-630p instead of 450p in the arenas.
- **Robotic audio (story, laser vs opponent's throw)**: rex_profile "requests without data" = frames the game's audio client did not
  deliver: 0 for 112 intervals, then 43 and 233 (of ~1900) in the last two (+88 at the first battle start, 11 at 18:05); XMA
  load in the last window was the maximum of the run (15.7k contexts, 824 ms worker; before: 12.7k). No saturated thread
  (SoundThread 15 %, XMA decoder 10 %, Audio Worker 2 %), fps 30. Cause not found yet.
- **Run 005 (water, 2 min, laser sabotage; robotic audio worse) - 2026-10-16 19:05**: ran the OLD controller (down wait "6.0 s (wait limit)",
  two decisions 4.5 s apart before the first was applied). fps 30.0, 100 % of windows >= 28, 450p 77 % / 720p 22 %. Audio: requests
  without data 0 x13 intervals, then 94 and 708 (37 %). CPU total 213 -> 240 % of the 300 % the 3 cores give: Main 86-90 % (was
  41-50 %), ring 52-58 %, a game worker `XThread...` (entry sub_8243D098, hot sub_8240E460) 11 -> 30 -> 48 %, worker 26 %.
  The sound thread (guest priority 0x3B) shares the band with Main and that worker; Horizon only time-slices at 0x3B.
  Fix: `bakugan_sound_priority` (true): the sound loop's thread runs at 0x2B (it sleeps 1 ms per iteration). The log now
  prints the NRO file's size and mtime at start (`[enh] NRO file ...`): the "build Oct 16 10:16:54" string is one file's
  __TIME__ and does NOT identify the build.
- **Run 006 (water, 9 min, build 19:09: sound priority 0x2B + pending gate)**: NRO line in the log confirmed the build. Controller: steps
  applied after 3.0 s each, no stacking, but still 720->630->540->450 in 18 s (each justified: 89/88/82 % busy, 60-100 % late
  frames) and two climbs 450->540 (p90 66 % x 1.2 = 79 %) both failed (86 %/82 %, 60-87 % late): 540p cannot hold the water
  arena's heavy moments. fps 29.6, 97 % windows >= 28, 450p 76 % / 540p 15 %. Audio: still 128 missing requests in the last
  interval (0 in 49 before; sound thread now 0x2B) -> priority was not the cause.
- **Real finding (stack profile of Main in run 006)**: 55 % of the main thread's running samples are the frame limiter's final wait
  (sub_823B4F30 loop at 0x823B52C0, `bl sub_823BC1D8` = mftb via UpdateGuestClock with a mutex, call site lr 0x823B52D8):
  a third of a core burnt spinning, 86-91 % "CPU" of Main of which the game logic is ~40 %. With the cores full (213-250 % of
  300 %) that spin steals from the rest. `bakugan_limiter_yield_us` (300): the spin sleeps in steps while more than 300 us
  remain (ctx.f13 - ctx.f31 seconds). Log: "frame limiter spin replaced by N short sleeps". PC check: ~3.7 sleeps/frame.
- **Run 007 (water, ~3 min, build 20:56 with the limiter yield)**: NRO line confirmed. Main thread CPU 86-91 % -> 71-80 % at the same
  load. Audio requests without data: 95 (interval 10, right after a 41 s suspend: the user opened the Home menu) and 74 (interval 13,
  the laser: where the user quit). Stack of the Audio Worker in interval 13: 32 % (clean interval: 14 %) inside
  `KeWaitForMultiple` called from the game's render-driver callback (sub_824401C8): the callback waits for a guest object that the
  game's sound thread signals, and the sound thread spends 87 % of its time in my 1 ms `bakugan_sound_sleep_us` sleep. Under
  load that sleep makes the callback miss its 5.33 ms frame. Now 250 us (`AN_sound_spin.toml` = original spinning, A/B).

## rc4 (2026-10-16 21:20): back to the best resolution/fps behaviour, plus the fixes; audio experiments parked
- User decision: stop the robotic-sound investigation for now; restore the best fps+resolution build (21:47 rc3-copy-engine
  behaviour: no stutter rule) + the fixes since. Done as defaults, no code removed:
  OFF: `bakugan_render_dynamic_late_frac` 0 (stutter rule), `bakugan_sound_priority` false, `bakugan_sound_sleep_us` back to 1000.
  KEPT: save-delay fix (sdk kernel_state 100 -> 1 ms), parallel rescale + deferred release, controller pending gate (a decision is
  not repeated before it is applied), history-based step up (`bakugan_render_dynamic_up_windows` 8 = 12 s, p90 x ratio < 0.8),
  down wait 3 s, `bakugan_render_dynamic_down_fps` 27.5, limiter spin -> short sleeps (`bakugan_limiter_yield_us` 300: Main thread
  86-91 % -> 71-80 % CPU), cumulative rex_profile.log, NRO stamp in the log.
- Goal now: full 720p at 30 fps. Known walls (water arena): GPU busy 82-89 % at 540-630p in heavy moments; ring thread 17-19 ms/Swap;
  Main thread 71-80 % CPU (game logic ~40 % + limiter).

## Round 15 (2026-10-16 night): rc4 results and the conversion shader
- rc4 (21:15 build, 8-9 min per run): water 29.6 fps / 95 % windows >= 28 / 540p 95 %; fire 29.3 / 83 % / 540p 30 %, 630p 58 %,
  720p 11 %; haos 29.6 / 92 % / 540p 92 %; story 29.9 / 99 % / 720p 73 %; cutscene 30.0 / 100 % / 720p 73 %, 540p 26 %.
  vs the stutter build: same fps, 450p gone (540-630p instead). vs 21:47: water 28.1 -> 29.6 fps, resolution similar.
  Steps: 1-2 per run (the pending gate works). User saw a horizontal line at the top of the HUD portrait of the player
  character in the second to last run (story), gone midway: not investigated (an edge of a resolved texture after a rescale?).
- GPU per frame at 720p (story, 20 ms busy): 640 5.3, **alias 6.3** (EDRAM colour conversions), noZ 4.0, copies 2.1, import/export
  2.0. The serialized diagnostic frame: three full-frame colour conversions = 4.4 of 4.8 ms of EDRAM transfers
  (C000 my1 1120x1120 -> 1x 1280x1280 1285 us/op x2; 560x720 mx1my1 -> 1120x1120 591 us x2; 1x -> my1 670 us). ~1.4 ns per
  processed pixel = ~165 lane-cycles a pixel in me_edram_color_to_color.frag.
- Change (exact): the shader computes each quotient once (the remainders came from a second division of the same operands)
  and, when both host views are RGBA8 (bit 16 of both formats), outputs the fetched texel without the byte pack/unpack.
  PC (REX_BAKUGAN_NATIVE_CONVERSION_FRAG=true, scripted green arena, 215 s): 10.7k conversions, 0 declined, picture correct.
- **Results of the conversion shader (22:18 build, logs 014-017, 2026-10-16 night)**: water 29.7 fps / 92 % >= 28 / 540p 72 %, 630p 20 %,
  720p 8 %; fire 29.5 / 93 % / 630p 88 %, 720p 11 %; haos 28.9 / 76 % (4.5 min, one 1.3 s hitch) / 540p 88 %; story (tournament
  battle) 29.5 / 89 % / 540p 80 %. "alias" GPU ms at equal heights: water 540p 4.1 -> 3.4, fire 630p 6.6 -> 5.5, 720p 5.4 -> 4.5,
  haos 540p 4.2 -> 3.7 (-12..17 %): less than hoped; the cost is not the divisions (texel fetch / write bound).
- Serialized diag frame, fire 630p (17.4 ms draws + 7.9 ms EDRAM transfers): 32 one-tile ops per frame in three kinds (depth ->
  colour "late stencil fetch" 48 us/op = 1.55 ms; 280x720 mx1my1 -> 560x720 18 us = 0.6 ms; depth -> depth 17 us = 0.54 ms) ~2.7 ms of
  per-op overhead; one depth -> colour op of 184 tiles = 2.05 ms (11 us/tile, 10x the conversions' per-tile cost).
  Candidates: batch/cheapen the one-tile ops, speed up the depth -> colour path.
- Not a bug: 20 "pread failed errno 5" per run (the log's cap) are reads past the end of the movie files (client_4_3.pam is
  7,557,535 bytes, the reads start at 7,602,176): the movie reader reads ahead, the SDK returns EIO instead of a short read.
- Pipeline compiles still cost 295-1192 ms the first time a pipeline is seen (story 1192, water 504): the console's cache keeps
  growing; copy `SD:\switch\bakugan-nx\cache\bakugan_native_pipelines.bin` into `release\cache` when it stops growing.
- **Missing bakugan portrait (story, log 017 23:32:29, user closed the game)**: `resolved logical crop rejected 1934E000 228x300:
  unproven logical fetch layout` (cap of 32 logged; every frame) with `resolved crop contract failure 2` = IncompatibleFormat: guest
  formats 6/20: the portrait is a DXT4/5 (20) texture fetched at an address where an old 1120x584 RGBA8 resolve entry still
  lived. The CPU-data fallback (`fetch_ordinary_texture_`) only covered UnprovenLayout with scale_1x. Fix: a block-compressed
  fetch (18-20, 49, 51-53, 58-61) can never sample a resolve -> ordinary texture. Log line "[native] resolved crop: fetch format..."
  (first 3). Only log 017 had the failure; the earlier horizontal line on the HUD portrait may be a related stale entry (unproven).

## Round 16 (2026-10-17): haos fps drops
- Haos windows < 28 fps (logs 016, 011, 003): (a) the first ~20-30 s (arena load: 25-27 fps, many 100-480 ms game-thread hitches with GPU 5 ms,
  record 0: the game's own loading, not changed); (b) the first battle minutes: the menus climbed to 720p, the battle started there
  (640 pass 12-14.7 ms, alias 5.3 ms, 24-28 fps for ~10 s before the step down to 540p); (c) isolated 26 fps windows at 540p with the
  GPU 70 % busy (frames with 17-24 copies and GPU 25-37 ms: heavy single frames, not seen by the average).
- GPU marks per frame separate scenes: battles/arenas 130-157, story walking 19-54, menus 7-19 (cutscene/tournament battle 129-131).
  `NoteGpuBusy(ns, marks)` now feeds the controller. New `bakugan_render_dynamic_heavy_cap` (true): the height a heavy scene
  (>= 100 marks a frame) fell to is remembered and applied when the next heavy scene starts (log "dynamic: heavy scene ... (the height
  the last heavy scene fell to)"); a climb inside a heavy scene raises the ceiling, 30 s without overload raises it, 10 min without a
  heavy scene forgets it. Only helps the 2nd+ battle of a session (the 1st still starts at 720p).
- Heavy scenes now also have a starting height before anything is learned: `bakugan_render_dynamic_heavy_start` = 630 (handheld only):
  the 1st battle of a session starts at 630p instead of the menus' 720p (every battle of 2026-10-16 began at 720p and overloaded:
  water 720>630>540, fire 720>630, haos 720>540, story battle 720>540); it climbs again after 12 s if the GPU has room.

## Round 17 (2026-10-17): first docked run on the console (log 018, water, 2 battles, 19 min)
- Docked works: 1920x1080 at start, handheld/docked switches mid-run applied in ~90 ms ("console mode changed"). Display 1920x1080.
- GPU docked is ~1.8x faster: water 720p docked busy 21.9 ms (heavy battle windows) vs handheld water 540p 23 ms; 810p docked 28.4 ms.
- Battle 1 began at 1080p (no docked heavy cap): 25.9 fps, 89 % busy -> 990 -> 900 -> 720 in 20 s (3 steps, each applied after 3 s,
  not stacked); 720p then held 29.9 fps for 8 min. Battle 2 (2x2, 545-595 draws/Swap): climbed 720 -> 810 in the menu, heavy cap
  back to 720, climbed to 810 again and stayed there 7 min at 27-28.4 fps, GPU 27-32 ms (82-97 % busy): the down rule (fps < 27.5
  and busy > 88 %, two in a row) never fired.
- Changes (build 14:06): hot windows counted with decay (3 hot windows or 2 under 26.5 fps), thresholds fps < 28.2 and busy > 84 %
  (`bakugan_render_dynamic_down_fps` 28.2); docked heavy start `bakugan_render_dynamic_heavy_start_docked` 810.

## Round 18 (2026-10-17 evening): handheld arenas with the 14:06 build (logs 019 water x fire, 020 haos, 021 story double battle)
- fps / windows >= 28 / time at heights: water+fire 29.9 / 98 % / 540p 84 %, 630p 11 %, 720p 4 %; haos 29.8 / 96 % / 540p 88 %, 720p 11 %;
  story double battle 29.3 / 89 % / 540p 88 %, 720p 11 %. (haos was 28.9 / 76 %, story battle 29.5 / 89 % before.)
- BUG: the heavy start (630p) never applied in the first battle of each launch: the 30 s "raise the ceiling" rule used the time since
  the last height change (minutes in the menus) instead of the time in the heavy scene, raised the cap to 720 in the first heavy
  window and the battle started at 720p again (720 -> 540 after 24 fps). Fixed (heavy_since): build 17:5x. The 2nd battle (fire)
  did get "630 -> 540 (the height the last heavy scene fell to)": the up step 540 -> 630 decided in the menu was applied at the
  scene-load hitch and undone 4 s later (two pointless rescales; not changed).
- Fire at 630p: busy 88 %, 26.6 fps at 16:01 (earlier build: 630p held in fire for 88 % of the run).
- Pink/colour checkered bakugan (story, user closed at once): the last log lines are two new textures (DXT1 1024x1024 11 levels at
  1459E000 and DXT4/5 512x512 at 1453E000, first use at 16:14:32.4; no error). Stable textures are rechecked with doubling
  intervals 1,2,4,8,16,32 frames: streaming that lands later than ~30 frames shows old/garbage content for up to 32+7 frames
  (documented popping). Unknown whether it persisted; not changed.
- Griffon checkered (user: "same monster, same attribute loaded fine before"): intermittent, so a timing / state problem, not a decoder or a
  shader fault. No "stable texture changed" line for the two new textures (only changes found at interval >= 4 were logged). Added a
  diagnostic: every re-upload of a texture already on the GPU is logged ("C3 texture re-uploaded: addr WxH format, N frames after its
  previous upload (check interval I)", first 300), so the next occurrence shows whether the texture was fixed a few frames later.

## Round 19 (2026-10-17 evening): CPU diet (codegen set 2) - the main thread is the limit in heavy battles
- Why: in the story 2x2 battle the 27.8 fps windows had the main thread at 94-95 % CPU (ring 66 %, 230 % of 300 % total, GPU 70 %): CPU
  bound, resolution does not help. After the limiter spin fix the profile is flat (biggest guest function 6 %); 10 % of the top-150
  sampled PCs sit right after an `msr fpcr`, 25 % after a `str` into the context struct. The sibling ports (sa2-nx, masseffect-nx) gain
  10-15 % from: set 2 of the optimised codegen (non_volatile_as_local, elide_lr_stores, fp_moves_modeless), LTO, PGO and hot function
  ordering; bakugan was still on set 1 only (docs: sa2-nx-main/docs/porting-guide.md ch. 9, enhancements.md "Build-time").
- Done: `app/codegen_flavor.toml` set 2. `tools/win/try_flavor.ps1` = codegen + post + PC build + 215 s scripted battle;
  `tools/win/trace_patch.py` + `-DBAKUGAN_TRACE_FUNCS=ON` (src/bakugan_trace.cpp) = the last 256 guest functions of a thread on a
  guest access violation (out/pc/bakugan_trace.txt). The PC build re-runs codegen on its own: patch the pch AFTER it.
- non_volatile_as_local crashed the battle load (vector::operator[] bounds trap, null element). tools/read_before_write.py lists 44
  functions that read non-volatile registers their caller set; they are NOT only EH funclets: the whole-program compiler passes extra
  inputs in r14-r31/f26-f31/v14-v31 to ordinary `bl` callees that keep the usual prologue (sub_82147138 reads r19; callers sub_82205CD8).
  share_registers alone breaks them (their __savegprlr/__restgprlr are elided, they return with clobbered registers and the
  caller copies those back). New generator option `keep_saves` (sdk/src/codegen: FunctionConfig/FunctionNode/context.cpp): with
  share_registers the function keeps REAL save/restore helper calls (the helpers' bodies now use ctx), so it hands the caller its
  registers back. app/overrides.toml marks the 44 (false alarms not marked: sub_823BD500, sub_823BD890, sub_828C7270, setjmp sub_828B3870).
- PC: scripted battle 215 s runs (116k draws / 10 s in the battle), no trap, no violation. NRO 59.6 -> 54.1 MB (lite 58.3).
  Backups: D:\Claude\_backups\generated_set1_20261017 (generated tree before set 2), nro_set2lite_20261017 (ELF of the lite NRO + the rc4h NRO).
- **Diet build, first console test (log 022, 44 direct marks, NRO 54.1 MB)**: every intro movie broken (pink/green lines, grid): the
  register-passing helper sub_8255D790 reads v16-v31 defined TWO frames up, the frame in between never touches them (pass-through),
  its caller's locals never reached ctx. `tools/read_before_write_chain.py` (fixpoint over the call graph on code generated WITHOUT
  marks) finds 237 functions (44 direct + 193 pass-through): all marked share_registers + keep_saves in overrides.toml (build 19:23,
  NRO 57.6 MB). Water battles (1v1 and 2x2) in that run: 29.5 fps, 96 % of windows >= 28, 540p 87 % (unchanged: GPU-bound, busy
  23-28 ms at 540-630p); Main thread CPU in battle intervals mean 50 % / max 82 % (before: 65-72 % / 86-95 %).
- The 720p goal is a GPU problem in the heavy arenas: the diet frees CPU but not GPU. 720p heavy-battle busy ~ 30+ ms (>= 92 %).
- Movies still broken in the 19:23 diet2 build (pulsating pink/green lines every couple of seconds; PC is clean at the same frames:
  Activision, NOWPRO, Premium Agency, Nelvana and the opening all fine on the PC build) -> ARM/GCC specific. The native WMV3 path
  copies FFmpeg's planes into the game's and the three macroblock passes return 0, but the game's POST passes (guest VMX code, incl.
  the register-passing helper sub_8255D790) still run on them. Bisect on the console with `tools/win/switch_variant.ps1 <name>`
  (regenerate + Switch build, out/nx/variants/<name>.nro/.elf): v1_lite = elide_lr_stores + fp_moves_modeless only (no register
  locals); v2_nv_only = non_volatile_as_local only (237 marks).

## Round 20 (2026-10-17 night): CPU follow-ups after the diet
- v3 (diet, movie decoder cluster classic): 29.7 fps / 97 % of windows >= 28 in a 15 min water run (1v1 + 2x2), Main CPU in battle mean 59 % /
  max 86 % (before the diet 65-72 % / 85-95 %), hitches >= 100 ms 6.3 per minute (before 7.2-8.8), no new kind of hitch. RAM: no OOM
  anywhere, proc 3281/3285 MB constant (the pool is taken at start), GPU images 150-350 MB.
- The movies: culprit = non_volatile_as_local on ARM (v1_lite = elide_lr_stores + fp_moves_modeless only was clean). v3 keeps the 116 functions
  reachable from the movie frame function sub_825413B8 classic (share_registers + keep_saves); the cause inside is not found (PC clean).
- Hot function ordering ported: `app/hot_functions.ld` (tools/hot_order.py over launch 025's stacks, 1500 functions, 711 game functions) +
  CMake option BAKUGAN_SWITCH_HOT_ORDER (ON); NRO 59.3 MB, _start still at 0. LTO / -fipa-pta ported as BAKUGAN_SWITCH_LTO (OFF by
  default; `tools/win/build_switch_lto.ps1` builds out/nx_lto). `tools/win/switch_variant.ps1 <name>` = regenerate + build + keep the NRO/ELF.

## Round 21 (2026-10-17 night): hot order / LTO results and the ordered transfer log (log 026, frame 6000, water 1v1)
- Main thread CPU in battle intervals (water 1v1 + 2x2 story): v3 diet 59 % (max 86), v4 hot order 55 % (max 80), v5 LTO 56 % (max 76):
  hot order ~-4 points, LTO nothing measurable (+3 MB NRO, link 7 min, cannot be checked on PC): not worth it. fps/resolution unchanged
  (GPU bound): 29.7 / 98 % windows >= 28 / 540p 95 % (hot) and 29.6 / 96 % / 540p 90 % (LTO). PGO tooling ported (BAKUGAN_PGO,
  tools/win/build_switch_pgo.ps1, pgo_merge.ps1, src/bakugan_pgo.cpp; instrumented NRO built, 77 MB) but not used: the CPU is no longer the limit.
- EDRAM transfers of one heavy frame (156 ops/frame, ordered log): 5 families caused by 3 declined clears and 1 draw:
  (1) clear rect 1520,0-1536,1536 depth-only (16 px strip down the last tile column of the 1600-wide depth surface): "declined partial tile:
  owner unsupported" -> 36 colour->depth imports (owner C000 1280x1280) + 24 depth->depth copies (owner D3FE 1120x1120 my1);
  (2) clear rect 480,0-512,512 depth+colour0 (32 px strip of the 560-wide surface): "color 0 not redirectable" (PlanClearColor0EDRAM4
  refuses unaligned rects, line 5552: 24,771 refusals per run) -> 32 depth->depth copies (D000->D0E0) + 32 depth->colour exports (D000->C000 560x720,
  the 42 us compute family); (3) draw VS n744 PS n1508 prim 6: 32 x (C000 280x720 mx1my1 -> 560x720, 6 tiles);
  tail of the frame: 5 big conversions (560x720 -> 1120x1120 my1 -> 640x720 depth -> 1280x1280).
  The strip clears are the cause of ~124 of the 156 ops (~2-3 ms in heavy frames). Fix options: redirect partial depth-only clears into colour
  owners with a masked write (stencil byte = R is kept), accept unaligned rects in the colour-0 plan.
- PartialEDRAM4 now counts its refusals per source line ("partial depth clear refused at ..." in the 10 s report) and takes unowned partial tiles
  (`bakugan_native_edram4_partial_unowned`); on PC the refusal is line 5274 (`color_for(owner)` null: a depth-only clear has no full word to write).

## Round 22 (2026-10-17 night): partial-clear refusals (log 028) and the bolder climb (log 029)
- Log 028 (water 1v1+2x2, partial diagnostic build): 29.7 fps, 96 % windows >= 28, 540p 88 % / 630p 8 % / 720p 4 %, 23 hitches >= 100 ms.
  Refusals counted per source line: depth-only strip clear refused at line 5274 (`color_for` null: no stencil = no full colour word) 14,786;
  colour-0 plan refused at 5707 (unaligned rect) 9,964; unowned tiles taken 0. Both fixes are worth only ~1-1.5 ms/frame: NOT done (risk of
  picture errors > gain). If done later: masked fragment write (G,B,A) into the RGBA8 colour owner keeping R = stencil.
- Test AP_bolder (`bakugan_render_dynamic_up_util` 0.86 instead of 0.80; log 029, ~12 min, same two battles): NOT better. 29.5 fps, 95 %
  windows >= 28 (min 21.8), 540p 82 % / 630p 16 % / 720p 1 %, 33 hitches >= 100 ms (6 rescale, worst 700 ms). Three climbs 540>630, every one
  fell back after 3 s-60 s (630p heavy busy 25-27 ms = 88 %, fps 27-28); each climb/fall costs a rescale hitch. Default stays 0.80.
- Heavy-arena GPU busy (mean, ms): 540p 22-23, 630p 27-30, 720p heavy estimated 29-32. Frame budget 33.3 -> 720p in heavy 2x2 needs ~4-5 ms
  more than the transfer fixes give. Resolution in heavy arenas stays 540-630p; light scenes/menus/cutscenes sit at 720p.

## Guide for future ports (Xbox 360 -> ReXGlue static recompilation, Switch ARM)
### Method (what paid off, in order)
1. Measure first, per 10 s window: fps, main-thread CPU %, GPU busy (total minus gap), GPU categories/marks, hitches >= 100 ms with their cause
   (game thread / rescale / GPU-record). Log everything to one file per launch, keep a standard test and compare builds on the SAME scripted
   path. Verify the NRO that ran (size/mtime logged at start): one run was an old build.
2. Find which side limits: CPU main thread > ~70 % or ring thread > frame time = CPU bound; GPU busy > 85 % at 30 fps = GPU bound.
### CPU (recompiled code)
- Codegen flavour (`codegen_flavor.toml`): set 1 (cr/ctr/xer/reserved locals, skip_msr, dform_disp_split, fctiwz_inline) and set 2
  (`non_volatile_as_local`, `elide_lr_stores`, `fp_moves_modeless`) took the main thread from 65-72 % to 50-59 % in battle. Always run
  `tools/read_before_write.py` and `read_before_write_chain.py` on classic output: functions that READ non-volatile registers their caller set
  (register-passing helpers, incl. pass-through frames) must keep `share_registers` + `keep_saves` in overrides.toml, else corrupt output.
  Hand-written/SIMD-heavy decoders (video: 116 functions under the movie decoder) must stay classic: non_volatile_as_local broke the intros on ARM
  only (PC was clean) -> always test movies/cutscenes ON THE CONSOLE after a flavour change; bisect with `switch_variant.ps1`.
- Hot function ordering from a profile (`tools/hot_order.py` -> linker script): ~-4 points main thread. LTO: nothing (7 min link): skip.
  PGO tooling exists, unused because the CPU stopped being the limit.
- Frame limiter: yield when the guest spins on timebase reads (rhythm detection), do not add sleeps elsewhere. Kernel delay of deferred
  overlapped I/O 100 -> 1 ms removes save hitches. Worker threads: find the real bottleneck thread by profile before optimising (it was the
  deferred worker in the fire arena). Audio thread priority/sleep experiments gave nothing and caused regressions: parked.
- Check RAM once (not a limit here). Keep a best-fps backup before every codegen change.
### GPU (EDRAM emulation, Vulkan/NVK on Switch)
- Costs in a heavy frame (540p, ms): scene pass ~10, copies ~2, no-Z passes ~2.3, EDRAM alias/import/export ~5. NVK: vkCmdCopyImage colour is a
  meta draw (use a fragment pass), depth copies go through the copy engine (buffer copy). Batch conversions; avoid per-tile ops.
- Dump the ORDERED transfer log of one frame (`bakugan_diag_transfer_log_from/_frames`) and group by family: a few clears/draws caused 124 of 156
  ops. Count refusals per source line (`PartialNo(__LINE__)`) before writing a fix; estimate gain from measured serialized cost first.
- Dynamic resolution: 1.5 s windows, pending gate (no decision before the last rescale is applied), history-based step up (p90 of 8 windows),
  heavy-scene cap learned from falls, floors (handheld 450p / docked 720p). Being bolder did NOT help: every failed climb costs a hitch.
  Create rescaled images on several threads and release old ones a few per frame to cut rescale hitches.
- Realistic target on Switch handheld for a game this heavy: 720p in menus/cutscenes/light battles, 540-630p in heavy arenas at stable 30.
### Process
- Test on PC first (scripted battle, muted+minimized), then console with one NRO staged (hbmenu cannot tell same-named NROs apart), one log per
  scenario, close with Home+X, never open Home mid-run. Tag every release candidate; keep CLAUDE.md rounds with evidence per entry.

## Round 23 (2026-10-18): standard build (AK) re-run: water (log 030) and story with a cutscene + save (log 031)
- Log 030 (water 1v1+2x2, 22 min): 29.6 fps, 96 % windows >= 28 (min 25.3), 540p 96 % / 630p 3 %, 2 steps down, 39 hitches >= 100 ms
  (28 game thread, 6 rescale, 5 gpu/record), worst 317 ms. Same as logs 028/029: heavy arena GPU busy 22.7 ms mean.
- Log 031 (story, 17 min, handheld): 720p 100 %, 29.7 fps, 100 % windows >= 28, 15 hitches >= 100 ms (worst 1305 ms = the save at 23:11:18-25,
  and 833 ms record = pipeline compile at 23:06:42). The cutscene at the end (23:09:50-23:11:10): 29.5-30.0 fps, 0-16 % late frames, GPU busy
  71-92 % in two windows, main thread 30 %, ring 30 %, CPU total 106-120 %; frames over 45 ms: 0-4 per 10 s. No pacing problem in the numbers
  -> the user's "delayed video" is an audio/video offset (video behind audio), which the counters do not measure. Not fixed; needs the
  scene name/time and whether it is a pre-rendered movie or an engine cutscene.

## Round 24 (2026-10-18): late-frame rule above 540p (test AQ_late, log 032, water 1v1+2x2)
- Cause of the story cutscene lag (log 031, 23:09:52-23:10:55, in-engine, 720p): GPU 92 % busy, up to 84 % late frames, slot-fence waits
  3.6-4 s per 10 s (~12 ms/frame) while the mean stayed 28.6-29.8 fps; the controller only reacts to fps < 28.2, so it never stepped down. Queued
  frames = video behind audio.
- New cvar `bakugan_render_dynamic_late_floor` (eighths; 6 = the late-frame rule never steps below 540p). AQ_late = AK + late_frac 0.25,
  late_util 0.85, late_floor 6. NRO 12:24 build (59,332,059 bytes).
- Log 032: 29.6 fps, 95 % windows >= 28 (min 27.1), 540p 84 % / 630p 15 %, 0 logged steps in the analyzer (4 controller steps by hand, two of them
  "(stutter)" 630->540 at 100 % late frames), hitches >= 100 ms 32 (worst 300 ms) vs log 030: 29.6 / 96 % / 540p 96 % / 39 hitches. Arenas not
  harmed; the cutscene itself could not be reproduced, so the fix is unverified there.

## Release rc5h (2026-10-18)
- Defaults changed in code (release has no toml): `bakugan_render_dynamic_late_frac` 0.25, `_late_util` 0.85, `_late_floor` 6 (the AQ_late test
  values; the late-frame rule never steps below 540p). Release folder: `out\nx\sdcard_release\switch\bakugan-nx` (tools\win\package_release.ps1).
