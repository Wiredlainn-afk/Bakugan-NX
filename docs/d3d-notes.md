# Bakugan: Battle Brawlers: the game's Direct3D (stage 4 notes, 2026-10-12)

No named reference game exists on this machine (the reference ports' images are the users' own game files, not
shipped), so `tools/d3d_match.py` could not be used. The functions were found from this game alone, by two cheap
methods that agree: `tools/pm4_sites.py` (which functions build Xenos PM4 type-3 headers, `0xC0..`) and a call census
(`tools/d3d_census_build.py`: a counting hook on all 1,829 functions of 0x8263D000-0x82677000 and 0x82826000-0x82828000,
run through boot, intro movie, title, menu and the start of Story; 298 were called).

Device object: `0x40008900` (r3 of every D3D function).

| Function | Address | Evidence |
|---|---|---|
| Draw-time shader/constant load | `sub_82667508` (r3 = device) | the only IM_LOAD/SET_CONSTANT builder called once per draw (162,147 calls vs 154,947 BeginVertices + DrawVertices); reads the bound shaders from device+12872 (VS) and +12868 (PS) |
| D3DDevice_DrawVertices | `sub_8264C4B0` (dev, type, start, count) | called by the game's draw wrapper `sub_8213A370` when mesh mode == 1 with (type 6, start 0, count 4); census |
| D3DDevice_BeginVertices | `sub_8264BA18` (dev, type, count, stride) | same wrapper otherwise: followed by a memcpy `sub_823BA430` into the returned buffer and `sub_8264B9F8` (EndVertices) |
| D3DDevice_DrawIndexedVertices | `sub_8264C8A0` (dev, type, base, start, count) | game wrapper `sub_821A7500` passes base = 0 (`li r5,0`) and shifts its own arguments; DRAW_INDX packets |
| D3DDevice_DrawIndexedVerticesUP | `sub_8264BEF8` | game wrapper `sub_821A7558` forwards a stack argument (180(r1) -> 84(r1)): seven arguments |
| CreateVertexDeclaration | `sub_82650D58` | counts 12-byte elements up to type 0xFF (D3DDECL_END), allocates, copies |
| CreateVertexBuffer / Lock / Unlock | `sub_826483B8` / `sub_82649560` / `sub_82648468` | the game's `sub_82497F60` creates a 12-byte buffer, locks, writes six halfwords (a quad's indices), unlocks |

Shader objects: word 0 low byte 6 = vertex, 7 = pixel; the container copy is at object+0x368 (VS) / +0x28 (PS) with
magic `0x102A11xx` (the renderer's layout check passes: the same XDK layout as Mass Effect's / SA2's); microcode
pointer at object+0x20 (VS) / +0x18 (PS). Container word 0 low bit: 1 = vertex, 0 = pixel.

Not found yet (not needed so far): the flip/swap function (no `XE_SWAP` constant; the renderer follows the ring), the
Create*Shader functions (the renderer notes shader objects at the draw-time load instead), the direct-state
functions SA2's optimizations use.

## Shader library
- The game's own containers sit in `media/shaders/*.par`: 1,858 containers (575 VS, 1,283 PS), extracted by
  `tools/par_extract_shaders.py`; 6 of the 9 containers the menu path uses are byte-identical to them.
- Direct3D's own and rewritten shaders come from the renderer's dumps (`BAKUGAN_SHADER_MISSING`,
  `BAKUGAN_SHADER_DISCOVERY`, `BAKUGAN_VERTEX_VARIANTS`; `tools/win/discover_pass.ps1`) and `tools/wrap_discovered.py`.
- `tools/win/build_shader_library.ps1 <folders>` (XenosRecomp + DXC 1.9.2609 + packer): 1,867 shaders, 74 MB.
- Known gap: 4 vertex shaders of the .par set fail in this DXC ("partial explicit stage input location assignment via
  vk::location(X) unsupported"): vs_0a9e3ed896370809, vs_22d2a3ad3a770551, vs_6d01b49de9526c87, vs_cdad975d25631e2d.

## Status of the native renderer on the PC
Boot, intro movie, title, storage prompt and main menu draw with 0 unidentified loads. Against the emulated GPU with
the same input script: title 2.9% of pixels differ (0.3% by more than 16 levels, all in the blinking "PRESS A" text,
a different fade phase), storage prompt 1.9% (none above 16). Movie and animated-background frames were taken at
different animation instants and are not comparable; a lockstep comparison (`vblank_per_swap`) is not done yet.
Not verified: gameplay (battle) scenes, Story cutscenes, My Room, Options, the remaining movies.
