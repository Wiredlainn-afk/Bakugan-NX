// Diagnostic (wmv3-ffmpeg branch): what the game's recompiled WMV decoder looks like in Bakugan.
// sub_825413B8 decodes one frame of a movie: it calls three row-decoding passes and the loop filter / border
// extension with the decoder context in r3. This logs, for the first calls of each movie, the context fields the
// nfsmw port needed to replace the decoder (the picture planes and their origin, strides, sizes).
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_BOOL(bakugan_video_diag, false, "BAKUGAN",
                    "Diagnostic: log the WMV decoder context of the game's movie frames");

REX_EXTERN(__imp__sub_825413B8);

namespace {
uint32_t Rd32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
}  // namespace

REX_HOOK_RAW(sub_825413B8) {
  static uint32_t calls = 0;
  const uint32_t n = calls++;
  const bool log = REXCVAR_GET(bakugan_video_diag) && (n < 6 || n % 200 == 0);
  const uint32_t c = ctx.r3.u32;
  if (log) {
    REXLOG_INFO("[video] frame {} ctx {:08X}: planes Y {:08X} U {:08X} V {:08X} origin +{} +{} | "
                "[3744..3760] {:08X} {:08X} {:08X} {:08X} | [16004] {} | [216..232] {} {} {} {} | "
                "[3916] {} [15504] {} [14856] {} [15228] {}", n, c, Rd32(base, c + 3744), Rd32(base, c + 3748),
                Rd32(base, c + 3752), Rd32(base, c + 220), Rd32(base, c + 224), Rd32(base, c + 3744),
                Rd32(base, c + 3748), Rd32(base, c + 3752), Rd32(base, c + 3756),
                Rd32(base, c + 16004) >> 16, Rd32(base, c + 216), Rd32(base, c + 220), Rd32(base, c + 224),
                Rd32(base, c + 228), Rd32(base, c + 3916), Rd32(base, c + 15504), Rd32(base, c + 14856),
                Rd32(base, c + 15228));
    // The first 128 words of the context, to find the sizes.
    if (n < 2) {
      for (uint32_t row = 0; row < 16; ++row) {
        REXLOG_INFO("[video] ctx[{:5}..] {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", row * 32,
                    Rd32(base, c + row * 32), Rd32(base, c + row * 32 + 4), Rd32(base, c + row * 32 + 8),
                    Rd32(base, c + row * 32 + 12), Rd32(base, c + row * 32 + 16), Rd32(base, c + row * 32 + 20),
                    Rd32(base, c + row * 32 + 24), Rd32(base, c + row * 32 + 28));
      }
    }
  }
  if (REXCVAR_GET(bakugan_video_diag) && n < 6) {
    for (uint32_t off = 15940; off < 16130; off += 32)
      REXLOG_INFO("[video] call {} ctx[{}..] {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", n, off,
                  Rd32(base, c + off), Rd32(base, c + off + 4), Rd32(base, c + off + 8), Rd32(base, c + off + 12),
                  Rd32(base, c + off + 16), Rd32(base, c + off + 20), Rd32(base, c + off + 24), Rd32(base, c + off + 28));
    const uint32_t d0 = Rd32(base, c + 16072);
    if (d0 >= 0x40000000u && d0 < 0x48000000u) {
      char hex[200];
      for (uint32_t i = 0; i < 48; ++i) std::snprintf(hex + i * 2, 3, "%02X", base[d0 + i]);
      REXLOG_INFO("[video] call {} data at {:08X}: {}", n, d0, hex);
    }
  }
  if (REXCVAR_GET(bakugan_video_diag) && n < 0) {
    // Every context word that points into guest heap memory, with the first 24 bytes it points at: the compressed
    // frame is searched among them offline (the bytes are compared with the movie file).
    for (uint32_t off = 15000; off < 24000; off += 4) {
      const uint32_t v = Rd32(base, c + off);
      if (v < 0x40000000u || v >= 0x48000000u) continue;
      char hex[80];
      for (uint32_t i = 0; i < 24; ++i) std::snprintf(hex + i * 2, 3, "%02X", base[v + i]);
      REXLOG_INFO("[video] call {} ctx[{}] -> {:08X}: {}", n, off, v, hex);
    }
  }
  __imp__sub_825413B8(ctx, base);
}
