# Bakugan: Battle Brawlers (title 41560857): stage 1-2 notes

Written 2026-10-12. Everything here was produced from the user's own disc image; nothing from the game is in the repo.

## Game
| | |
|---|---|
| Title ID | `41560857` (AV 2135), media ID `221503EA`, disc 1/1, version 0.0.0.1 |
| Executable | `default.xex` (original PE name `bg.exe`), AES + basic compression, imports `xam.xex`, `xboxkrnl.exe` only |
| Image | base `0x82000000`, size `0x00AB0000`, entry `0x823BCB28`, code `0x82120000-0x829780C8` (`.text` + 8 `.embsec_`) |
| Data | `media/d00.arc` (2.5 GB) indexed by `media/d00.bin` ("PAA", 6373 names), 14 `.pam` movies, `.past` sound x7 languages, `media/shaders/*.par` |
| Extra code | none: no `.xex`/`.rel`/`.dll` name in the index, no XEX2 magic in any file other than `default.xex` |

## Toolchain (no admin, no Visual Studio install)
`C:akugan-tools`: `llvm` (clang 20.1.8 portable), `msvc` (CRT + Windows SDK splatted by xwin 0.10.0; its final
compat symlink step fails without privilege and is not needed), `venv` (cmake 4.4.4, ninja 1.13). `tools\win\env.ps1`
sets INCLUDE/LIB from that layout. CMake needs `-DCMAKE_TRY_COMPILE_CONFIGURATION=Release` (no debug CRT libs).
`bakugan-nx\sdk` is a junction to the shared `..\sdk` (git-ignored). The generator is `sdk\out\win-amd64\rexglue.exe`.
Use the Windows Python (`py -3.13 -I`) for the tools: the MSYS2 `python` hands them `/d/...` paths.

## Commands
```
powershell -File tools\win\host_build.ps1      # cmake --build out/host --target rexglue (after configure, see below)
powershell -File tools\codegen.ps1             # rexglue codegen app\bakugan_manifest.toml, image dump to out\default_image.bin
```
Configure (once): `cmake -S ..\sdk -B out/host -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2`.

## Results of stage 2 (codegen of default.xex)
- Codegen succeeds: 301 files, 46,151 generated functions (19,249 `.pdata` entries all among them), 33 s.
- `app/overrides.toml` has 430 `[functions]` entries, 1 `[[switch_tables]]`, setjmp/longjmp; each with its evidence.
  - 110: "target not in any function" from the first codegen: `b` tail calls into MSVC-checked-iterator fragments.
  - 130: `tools/pointer_fixpoint.py` (pointers in data; 131 accepted, 1 pruned below); 2 more by hand from `pointer_audit.py` (vtable-like, rejected by
    the tool because of the `twi 31` bounds trap). Rejected ones are in `out/ptrscan/pass1/rejected.txt`.
  - 186: `tools/gap_fixpoint.py` structural gaps (8 passes, 188 accepted, 2 pruned below; evidence
    `out/gapscan/gapscan-accepted.txt`).
  - 1 jump table (sub_826628C0, 17 of 64 cases generated) + `size = 0x4A0` so its cases land inside the function;
    1 leaf function in a gap (0x826836D8, a byte-table switch that `switch_check` reported as having no generated code).
  - setjmp `0x828B3870` / longjmp `0x828B3550`, found by shape.
- Pruned after the fact (they split a real function): 0x8236BDE0 and 0x823C3804 (inside `.pdata` bodies; the second is
  an SEH `mr r8,r8` scope marker) and 0x822CCE10 (mid-block).
- Final checks on the finished tree: `switch_check` 0 truncated / 0 wrong / 0 missed / 0 unreachable; `pdata_audit`
  0 `.pdata` functions missing; `pointer_audit` 0 vtable-like and 0 built-in-code hits in gaps; codegen log has no
  unresolved branch or jump target.

## Open items (not verified; need the boot)
1. 20 `vpkd3d128 FLOAT16_4` sites, all in function `0x828539F0`, use `mask 3, shift 0`; the SDK validated only mask 2
   (warning kept, now prints the operands: `sdk/src/codegen/builders/vector.cpp`). Shift 0 cannot overflow the host
   vector, but whether mask 3 leaves the other words as the console does is unchecked. Watch skinned/packed geometry.
2. `0x82937508` is a 25 KB guest function that generates 1.36 MB of C++ (> max_file_size_bytes warning): harmless unless
   the build chokes on that translation unit.
3. 11 import-thunk stubs at `0x828EC724-0x828EC8B4` are referenced from the import table (`0x829ADD24-0x829ADD4C`) and
   have no generated code; assumed to be the loader's import thunks (to confirm at boot).
4. `.pdata` comparison: 6 generated functions run past their `.pdata` end (no-return `bl` followed by EH data words
   decoded as code); harmless unless the junk contains an unresolved branch (none reported).
5. Runtime-only classes (unregistered indirect calls, `runtime_gap_resolver.py`) cannot be found statically.
6. Entries whose previous word is `li r11,0` (46) are tail-call fragments after an unconditional trap; they generate
   no warning and are semantically a tail call, but are the first place to look if a checked-iterator path misbehaves.

## Requirement for the Switch build (user, 2026-10-12)
Face buttons are mapped by physical position so the Xbox prompts on screen match the pad: Xbox A = Switch B (bottom),
Xbox B = Switch A (right), Xbox X = Switch Y (left), Xbox Y = Switch X (top), as in nfsmw-nx. This is the SDK
switch input driver's default (`input_xbox_layout = true`); keep it explicit in the Bakugan Switch defaults.
