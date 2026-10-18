// Bakugan: Battle Brawlers hooks for the native renderer (src/me, from masseffect-nx through banjo-nx), with the
// game's addresses (docs/d3d-notes.md: found by call census + PM4 header scan, 2026-10-12).
//
// The renderer reads the game's PM4 ring by itself; these hooks only feed it what the ring cannot say:
//
//   - Shader objects. NoteShaderObject reads the container copy inside a D3D shader object and its microcode pointer.
//     The draw-time load is sub_82667508 (r3 = device; the single caller of IM_LOAD per draw: 160k calls in the
//     menu/movie census): it reads the bound VS from device+12872 and the PS from device+12868 (this XDK; SA2's 2012
//     XDK has +13072 / +13068), so the objects are noted there.
//   - Draw records, a hint for pairing (NoteDrawCall: primitive type, then r5..r7 as D3D received them):
//       D3DDevice_DrawVertices         sub_8264C4B0 (dev, type, start, count)        census: type 6, start 0, count 4
//       D3DDevice_DrawIndexedVertices  sub_8264C8A0 (dev, type, base, start, count)  (the game's wrapper sub_821A7500
//                                       passes base = 0)
//       D3DDevice_BeginVertices        sub_8264BA18 (dev, type, count, stride) + EndVertices sub_8264B9F8: the game
//                                       copies the vertices into the buffer Begin returns (DrawVerticesUP's form)
//       D3DDevice_DrawIndexedVerticesUP sub_8264BEF8 (seven arguments, the last on the stack; wrapper sub_821A7558)
//
// Every hook calls the original; none changes a PPC register itself.

#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "me/me_native_system.h"

namespace {

inline uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

constexpr uint32_t kDeviceVertexShader = 12872;  // 0x3248
constexpr uint32_t kDevicePixelShader = 12868;   // 0x3244

// The draw records (NoteDrawCall) read the bound shaders from the same device fields.
const bool g_device_layout_set = [] {
  me::native::SetDeviceShaderOffsets(kDeviceVertexShader, kDevicePixelShader);
  return true;
}();

}  // namespace

// Draw-time shader load (FlushState's shader part, called from every Draw*).
REX_EXTERN(__imp__sub_82667508);
REX_HOOK_RAW(sub_82667508) {
  const uint32_t device = ctx.r3.u32;
  if (device >= 0x40000000 && device < 0x7F000000) {
    const uint32_t vs = Load32(base, device + kDeviceVertexShader), ps = Load32(base, device + kDevicePixelShader);
    if (!me::native::ShaderObjectsFinished(vs, ps)) {
      me::native::NoteShaderObject(base, vs, true);
      me::native::NoteShaderObject(base, ps, false);
    }
  }
  __imp__sub_82667508(ctx, base);
}

REX_EXTERN(__imp__sub_8264C4B0);
REX_HOOK_RAW(sub_8264C4B0) {  // D3DDevice_DrawVertices
  me::native::NoteDrawCall(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  __imp__sub_8264C4B0(ctx, base);
}

REX_EXTERN(__imp__sub_8264C8A0);
REX_HOOK_RAW(sub_8264C8A0) {  // D3DDevice_DrawIndexedVertices
  me::native::NoteDrawCall(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  __imp__sub_8264C8A0(ctx, base);
}

REX_EXTERN(__imp__sub_8264BA18);
REX_HOOK_RAW(sub_8264BA18) {  // D3DDevice_BeginVertices (DrawVerticesUP's first half)
  me::native::NoteDrawCall(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  __imp__sub_8264BA18(ctx, base);
}

REX_EXTERN(__imp__sub_8264BEF8);
REX_HOOK_RAW(sub_8264BEF8) {  // D3DDevice_DrawIndexedVerticesUP
  me::native::NoteDrawCall(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  __imp__sub_8264BEF8(ctx, base);
}
