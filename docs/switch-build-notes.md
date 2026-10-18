# Switch build notes (2026-10-12)

Result: `out\nx\bakugan.nro` (61.6 MB; links Mesa 26.2.1 `git-1a8c1a66d6`, the nfsmw-nx-patched mesa-switch; `_start` at
0). **Not run on a console** (no hardware here): everything below is build-level evidence only.

## Toolchain actually used
- devkitA64 GCC 16.1.0 + libnx 4.12.0 (already installed in `C:\devkitPro`), through `tools/switch/cmake/switch-devkitA64.cmake`.
- NVK driver: `C:akugan-tools\mesa-switch` = danfromtico/mesa-switch at commit `1a8c1a66d6f` + `nfsmw-nx-main/mesa/
  mesa-switch-nfsmw.patch` (applies with 0 failures at that commit; the 26.2.4 copy in the repo does not take it).
  Built with `build-unified.sh` (script `C:akugan-tools\dl\mesa_build.sh`), result in
  `mesa-switch/mesa-unified-install/opt/devkitpro/portlibs/switch` (libvulkan.a 93 MB, libnvk.a, libnak_rs.a, ...).
- MSYS2 (portable, `C:akugan-tools\msys64`, with a junction `C:\msys64` -> it, and `<msys64>\opt\devkitpro` ->
  `C:\devkitPro`, because the build files hardcode `C:/msys64/...`), **UCRT64** environment (the MINGW64 one fails in
  Mesa's SPIR-V `vtn_setjmp` workaround under clang); packages: gcc clang llvm spirv-llvm-translator pkgconf cmake
  python(+packaging setuptools yaml mako) meson ninja rust-bindgen cbindgen.
- Rust: rustup GNU toolchain `stable-x86_64-pc-windows-gnu` + target `aarch64-unknown-linux-gnu` under
  `C:akugan-tools\{rustup,cargo}` (`MESA_SWITCH_RUSTC`, `RUSTUP_TOOLCHAIN`); `rustfmt` is not installed (non-fatal).
- The build files' `clang64`/`clangarm64` references were changed to `ucrt64` in the working copy
  (`windows_x64_host_cross_file_msys2.txt`, `switch_native_tools_msys2.txt`, the three rustc/host wrappers).
- `libzstd.a` for the Switch: devkitPro has no switch-zstd package, so zstd 1.5.7 `lib/` was compiled with devkitA64
  (`-DZSTD_MULTITHREAD=0`) and placed in the Mesa SDK `lib/` (not in `C:\devkitPro`).

## Commands
```
powershell -File tools\win\build_switch.ps1 -Jobs 8        # configures out\nx the first time, builds the NRO
powershell -File tools\win\package_switch.ps1 [-IncludeGame] # out\nx\sdcard\switch\bakugan-nx\
```
Copy `assets\game_root` (the extracted disc) to `sdmc:/switch/bakugan-nx/game_root`; the shader package sits next to
the NRO. Settings: `app/bakugan_switch_defaults.toml` (compiled in) and `app/bakugan_switch.toml` (the player's
`bakugan.toml`, written next to the NRO when missing).

## Requirement implemented
Face buttons by position (`input_xbox_layout = true`, explicit in both toml files): Xbox A = Switch B, B = A, X = Y,
Y = X, so the on-screen Xbox prompts sit under the same physical buttons (user, 2026-10-12; as nfsmw-nx).

## Not verified / not done
- Booting on a console, performance, memory limits (the reference ports' `2001-0103` mapping limit, one libnx
  exception stack, ...), GPU time. No LTO/PGO/ordering (a plain Release build); no asset pack.
- The shader package covers boot, intro movie, title and menus only; gameplay shaders are not discovered yet.
- `tools/check_switch_build.py` is the SA2 script (its halt list and flavour check are SA2's): only `_start` at 0 and
  the driver string were checked by hand.
