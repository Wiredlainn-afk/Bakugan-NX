# Performance history

How the port got to a steady 30 fps on the Switch's stock clocks, what was measured, and what did not help. Numbers are
from console runs in handheld mode (CPU 1020 MHz, GPU 460.8 MHz as the game asks for it).

## Where it stands

- Menus, cutscenes, story and light battles: 720p, 30 fps.
- Heavy arenas (water, fire, haos, 2x2): 540-630p, 29.6 fps on average, 95 % or more of the time at 28 fps or higher.
- The GPU is the limit in the heavy arenas: about 22 ms busy per frame at 540p, 27-30 ms at 630p, an estimated 29-32 ms
  at 720p against a 33.3 ms frame. About 5 ms of that is EDRAM emulation (transfers between the render targets that
  stand in for the Xbox 360's EDRAM), about 10 ms the scene pass, the rest copies and full-screen passes.
- The CPU is not the limit any more (main thread 50-59 % in battle).

## The dynamic render scale (`app/src/me/bakugan_render_mode.cpp`)

- Windows of 1.5 s measure GPU busy fraction, fps and late frames. A decision is only taken once the previous resize has
  been applied, so steps never stack.
- Step down: GPU over 84 % busy with fps under 28.2 for 3 windows (or 2 under 26.5 fps); or, above 540p only, 3 windows
  with at least 25 % late frames and the GPU over 85 % busy (a cutscene that averaged 29 fps but queued frames behind
  the GPU, so the picture trailed the sound).
- Step up: the p90 of the last 8 windows' busy fraction, scaled by the height ratio, must stay under 80 %. A bolder 86 %
  was tried and was worse: every failed climb costs a resize hitch.
- Heavy scenes (100 or more GPU marks per frame) remember the height they fell to and start there next time.
- Floors: 450p handheld, 720p docked (base 1080p). CAS sharpening applies below native only.
- Resizing is cheap: images are created on 3 threads, old ones are released 8 per frame.

## CPU: the codegen flavour (`app/codegen_flavor.toml`)

Set 1 (condition register, CTR, XER and reserved-address as locals; no MSR; split D-form displacements; inline
`fctiwz`) and set 2 (`non_volatile_as_local`, `elide_lr_stores`, `fp_moves_modeless`) took the main thread in battle from
65-72 % to 50-59 %. Functions that read non-volatile registers their caller set must keep `share_registers` and
`keep_saves` (`app/overrides.toml`; found with `tools/read_before_write*.py`). The WMV3 movie decoder cluster (116
functions) must stay classic: with `non_volatile_as_local` the intro movies came out with pink and green lines on the
console, while the PC build was clean. Hot-function ordering from a profile (`tools/hot_order.py`) gained about 4
points; LTO gained nothing; PGO tooling exists but was not needed.

Other CPU fixes: a frame limiter that sleeps when the game spins on the timebase; the SDK's deferred overlapped I/O delay
cut from 100 ms to 1 ms (save hitches).

## GPU findings

- A few clears and one draw caused 124 of the 156 EDRAM transfers in a heavy frame. The two main refusals are a
  depth-only strip clear (the colour owner has no full word to write) and an unaligned colour+depth strip clear. A masked
  partial clear would save about 1-1.5 ms per frame; not done for the risk to the picture.
- Moving the renderer to deko3d was studied (see the NFSMW notes for that port) and left for later: it fixes CPU cost per
  draw and pipeline stalls, not shader or EDRAM cost, which is what limits this port.

## Guide for porting another game (Xbox 360 -> ReXGlue static recompilation, Switch ARM)
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
  scenario, close with Home+X, never open Home mid-run. Tag every release candidate; keep a dated log with the evidence for every entry.
