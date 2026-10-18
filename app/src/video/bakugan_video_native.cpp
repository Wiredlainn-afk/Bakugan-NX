// Bakugan: the game's movies decoded with FFmpeg's WMV3 decoder instead of the recompiled XDK decoder
// (CPU enhancement; the nfsmw-nx port did the same, see its docs/audio-and-video.md "Videos").
//
// WHAT THE GAME DOES
//   The movies (media/movie/*.pam) are ASF files with a WMV3 video stream (1280x720 at 30 fps, I and P frames). Its
//   decoder thread decodes one frame per call of sub_825413B8(ctx): sub_828F4480, sub_8293F600 and sub_8291F918 do the
//   macroblock work with (ctx, ctx+15952, ctx+17360, 0, 0, h), then the post passes. On the Switch that thread runs at
//   100 % of a core and manages 20-23 frames per second: the picture falls behind and the audio ends first.
//   The picture goes to the planes [ctx+3744] (Y), [ctx+3748] (U), [ctx+3752] (V), whose first visible byte is at
//   +[ctx+220] (Y, 32-row and 32-column borders: stride (off - 32) / 32 = 1344) and +[ctx+224] (U and V, stride 672).
//
// WHAT THIS DOES
//   The recent .pam files the game opened (SDK: BakuganRecentPamOpens) are read by an ASF reader of our own. For each
//   frame the game decodes, the next compressed frame of that file is matched against the bytes the game placed at
//   [ctx+16072]; when they agree, FFmpeg decodes that frame and its planes are copied into the game's planes, and
//   the three macroblock passes return 0 without doing their work. Any mismatch (a movie not opened by path, a
//   skipped frame) leaves that frame to the game's own decoder.
//   bakugan_video_wmv3_shadow: the game decodes as usual and its planes are compared with FFmpeg's (diagnostic).
//
// WHY IT IS EXACT: the same compressed bytes, a conforming decoder; the shadow mode checks it frame by frame.
// RISK: a movie whose ASF layout the reader does not handle (checked by the byte match, then the game decodes).
// KEY: bakugan_video_wmv3_native (with bakugan_cpu_enhancements).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "cpu/bakugan_enhancements.h"
#include "video/bakugan_video_wmv3.h"

namespace rex::kernel::xboxkrnl {
std::vector<std::string> BakuganRecentPamOpens();  // SDK xboxkrnl_io.cpp
}  // namespace rex::kernel::xboxkrnl

BAKUGAN_ENHANCEMENT(g_video_native, bakugan_video_wmv3_native, bakugan::enh::kCpu, true,
                    "The game's WMV3 movies decoded by FFmpeg instead of the recompiled decoder (same bytes, same planes)");
REXCVAR_DEFINE_BOOL(bakugan_video_wmv3_shadow, false, "BAKUGAN enhancements",
                    "Diagnostic: the game decodes and its planes are compared with FFmpeg's for the same bytes");

REXCVAR_DEFINE_INT32(bakugan_video_dump_every, 0, "BAKUGAN enhancements",
                     "Diagnostic: every N-th native movie frame (at most 24 per run) the luma plane that was written to guest "
                     "memory is saved as video_dump/<movie>_<frame>.pgm (0 = off)");

REX_EXTERN(__imp__sub_825413B8);
REX_EXTERN(__imp__sub_828F4480);
REX_EXTERN(__imp__sub_8293F600);
REX_EXTERN(__imp__sub_8291F918);

namespace {

constexpr uint32_t kFrameFunction = 0x825413B8;  // the caller of the three passes
constexpr uint32_t kFrameFunctionBytes = 360;
constexpr uint64_t kFrameFunctionHash = 0x7D88E100B80F900Dull;

uint32_t Rd32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}

struct Planes {
  uint8_t* y = nullptr;
  uint8_t* u = nullptr;
  uint8_t* v = nullptr;
  int stride_y = 0, stride_c = 0;
};

bool GamePlanes(uint8_t* base, uint32_t ctx, int width, Planes& p) {
  const uint32_t off_y = Rd32(base, ctx + 220);
  const uint32_t off_c = Rd32(base, ctx + 224);
  if (off_y < 32 || off_c < 16 || (off_y - 32) % 32 != 0 || (off_c - 16) % 16 != 0) return false;
  p.stride_y = int((off_y - 32) / 32);
  p.stride_c = int((off_c - 16) / 16);
  if (p.stride_y < width || p.stride_c < (width + 1) / 2) return false;
  p.y = base + Rd32(base, ctx + 3744) + off_y;
  p.u = base + Rd32(base, ctx + 3748) + off_c;
  p.v = base + Rd32(base, ctx + 3752) + off_c;
  return true;
}

struct Header {
  bool valid = false;
  uint32_t stream = 0, number = 0, size = 0;
};

struct Current {
  uint32_t ctx = 0;
  std::string path;
  std::unique_ptr<bakugan::video_wmv3::WmvMovie> movie;
  uint32_t expect = 0;  // media object number the game should ask for next
  uint64_t frame = 0;
  uint64_t native = 0, fallback = 0, bad_header = 0, not_found = 0;
  int64_t window_start_us = 0;
  uint64_t window_frames = 0;
  double window_ms = 0;
  double worst_mean = 0, worst_max = 0;
};
std::mutex g_mutex;
Current g_cur;
thread_local Header t_entry_header;                // the payload header of the frame being decoded
thread_local bool t_native_frame = false;          // the first pass of this frame was done natively
thread_local bakugan::video_wmv3::Frame t_shadow;  // FFmpeg's picture for the shadow comparison
thread_local bool t_shadow_valid = false;

std::atomic<int> g_state{0};

int64_t NowUs() {
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

bool Enabled(uint8_t* base) {
  int s = g_state.load(std::memory_order_relaxed);
  if (s == 0) {
    s = ((g_video_native.active || REXCVAR_GET(bakugan_video_wmv3_shadow)) &&
         bakugan::enh::GuestCodeMatches(base, kFrameFunction, kFrameFunctionBytes, kFrameFunctionHash,
                                        "bakugan_video_wmv3_native"))
            ? 1
            : -1;
    g_state.store(s, std::memory_order_relaxed);
    REXLOG_INFO("[video] native WMV3: {}", s > 0 ? "engaged (code check passed)" : "off");
  }
  return s > 0;
}

// The ASF payload header the game keeps in front of the frame it decodes (at [ctx+16072] when sub_825413B8 starts):
// +1 stream number, +2 media object number, +3..+6 offset into the object, +7 replicated data length (10), then the
// replicated data: object size (4 bytes) and presentation time (4 bytes).
Header ReadHeader(const uint8_t* base, uint32_t buffer) {
  Header h;
  if (buffer < 0x40000000u || buffer >= 0x80000000u) return h;
  const uint8_t* p = base + buffer;
  if (p[7] != 0x0A) return h;  // not at the payload header: the object size is unknown (0)
  h.stream = p[1] & 0x7F;
  h.number = p[2];
  h.size = uint32_t(p[8]) | (uint32_t(p[9]) << 8) | (uint32_t(p[10]) << 16) | (uint32_t(p[11]) << 24);
  h.valid = h.size > 0 && h.size < (8u << 20);
  if (!h.valid) h.size = 0;
  return h;
}

// Decodes (in order, keeping FFmpeg's reference picture right) up to the object the game is about to decode, and
// returns FFmpeg's picture for it. false when the movie does not have it.
bool DecodeUpTo(bakugan::video_wmv3::WmvMovie& movie, const Header& h, bakugan::video_wmv3::Frame& out,
                int limit = 4096) {
  std::vector<uint8_t> raw;
  bool key = false;
  uint32_t number = 0;
  for (int i = 0; i < limit; ++i) {
    if (!movie.NextRaw(raw, key, number)) return false;
    const bool target = (number & 0xFF) == h.number && (h.size == 0 || raw.size() == h.size);
    if (!movie.DecodeRaw(raw, key, out)) return false;
    if (target) return true;
  }
  return false;
}

// Position `c` on the frame of the header and decode it. True when FFmpeg's picture for it is in `out`.
bool MatchFrame(Current& c, uint8_t* base, uint32_t ctx, const Header& h, bakugan::video_wmv3::Frame& out) {
  if (!h.valid) {
    ++c.bad_header;
    return false;
  }
  // The movie bound before, if the next object is the one asked for.
  if (c.movie && c.ctx == ctx) {
    // (the game does not decode every object through this function: the numbers can jump ahead)
    if (DecodeUpTo(*c.movie, h, out, 160)) {
      c.expect = (h.number + 1) & 0xFF;
      return true;
    }
    c.movie.reset();
  }
  // Another movie, or out of step: try the recent files from their first frame.
  const auto paths = rex::kernel::xboxkrnl::BakuganRecentPamOpens();
  for (const auto& path : paths) {
    auto movie = std::make_unique<bakugan::video_wmv3::WmvMovie>();
    if (!movie->Open(path)) continue;
    if (!DecodeUpTo(*movie, h, out)) continue;
    c.movie = std::move(movie);
    c.path = path;
    c.ctx = ctx;
    c.expect = (h.number + 1) & 0xFF;
    c.frame = 0;
    REXLOG_INFO("[video] native WMV3: '{}' bound to decoder {:08X} at object {} ({} bytes)", path, ctx, h.number,
                h.size);
    return true;
  }
  ++c.not_found;
  return false;
}

void Report(Current& c, int64_t now) {
  if (c.window_start_us == 0) {
    c.window_start_us = now;
    return;
  }
  if (now - c.window_start_us < 5000000) return;
  const double s = double(now - c.window_start_us) / 1e6;
  REXLOG_INFO("[video] native WMV3 '{}': {:.1f} frames/s, FFmpeg {} frames at {:.2f} ms, game decoder {} frames "
              "({} without a usable header, {} not found; worst shadow difference: Y mean {:.3f} max {:.0f})",
              c.path, double(c.window_frames) / s, c.native,
              c.window_frames ? c.window_ms / double(c.window_frames) : 0.0, c.fallback, c.bad_header, c.not_found,
              c.worst_mean, c.worst_max);
  c.window_start_us = now;
  c.window_frames = 0;
  c.window_ms = 0;
}

void CopyPlanes(const bakugan::video_wmv3::Frame& f, const Planes& g) {
  const int wc = (f.width + 1) / 2, hc = (f.height + 1) / 2;
  for (int y = 0; y < f.height; ++y)
    std::memcpy(g.y + int64_t(y) * g.stride_y, f.planes[0] + int64_t(y) * f.strides[0], size_t(f.width));
  for (int y = 0; y < hc; ++y) {
    std::memcpy(g.u + int64_t(y) * g.stride_c, f.planes[1] + int64_t(y) * f.strides[1], size_t(wc));
    std::memcpy(g.v + int64_t(y) * g.stride_c, f.planes[2] + int64_t(y) * f.strides[2], size_t(wc));
  }
}

// Inside the frame function on this thread (its entry hook counts). It used to be the caller test lr in
// [kFrameFunction, +kFrameFunctionBytes): the optimised codegen elides the lr stores.
thread_local int t_in_frame_function = 0;
bool InFrameFunction(const PPCContext&) { return t_in_frame_function > 0; }

}  // namespace

// Entry of the frame function: remember which ASF payload (media object) the game is about to decode.
REX_HOOK_RAW(sub_825413B8) {
  // [ctx+3604] counts the decoded frames from the movie's first object: the ASF media object number.
  t_entry_header = ReadHeader(base, Rd32(base, ctx.r3.u32 + 16072));
  t_entry_header.number = Rd32(base, ctx.r3.u32 + 3604) & 0xFF;
  t_entry_header.valid = true;
  ++t_in_frame_function;
  __imp__sub_825413B8(ctx, base);
  --t_in_frame_function;
}

// First pass: decodes the whole frame with FFmpeg (or, in shadow mode, only prepares FFmpeg's picture).
REX_HOOK_RAW(sub_828F4480) {
  t_native_frame = false;
  t_shadow_valid = false;
  if (!InFrameFunction(ctx) || !Enabled(base)) {
    __imp__sub_828F4480(ctx, base);
    return;
  }
  const bool shadow = REXCVAR_GET(bakugan_video_wmv3_shadow);
  const uint32_t game_ctx = ctx.r3.u32;
  std::lock_guard<std::mutex> lock(g_mutex);
  Current& c = g_cur;
  const int64_t t0 = NowUs();
  bakugan::video_wmv3::Frame frame;
  const bool ok = MatchFrame(c, base, game_ctx, t_entry_header, frame);
  Planes planes;
  if (ok && GamePlanes(base, game_ctx, frame.width, planes)) {
    if (shadow) {
      t_shadow = frame;
      t_shadow_valid = true;
      __imp__sub_828F4480(ctx, base);
      return;
    }
    CopyPlanes(frame, planes);
    if (const int every = REXCVAR_GET(bakugan_video_dump_every); every > 0 && c.frame % uint64_t(every) == 0) {
      static int dumps = 0;
      if (dumps < 24) {
        ++dumps;
        std::string name = c.path.substr(c.path.find_last_of("\\/:") + 1);
        std::error_code ec;
        std::filesystem::create_directories("video_dump", ec);
        const std::string file = fmt::format("video_dump/{}_{}.pgm", name, c.frame);
        if (FILE* f = std::fopen(file.c_str(), "wb")) {
          std::fprintf(f, "P5\n%d %d\n255\n", frame.width, frame.height);
          for (int y = 0; y < frame.height; ++y)
            std::fwrite(planes.y + int64_t(y) * planes.stride_y, 1, size_t(frame.width), f);
          std::fclose(f);
        }
        // Chroma planes too (half size): the mosaic could be in U/V.
        const int cw = (frame.width + 1) / 2, ch = (frame.height + 1) / 2;
        const uint8_t* cp[2] = {planes.u, planes.v};
        for (int i = 0; i < 2; ++i) {
          const std::string cf = fmt::format("video_dump/{}_{}_{}.pgm", name, c.frame, i ? 'v' : 'u');
          if (FILE* f = std::fopen(cf.c_str(), "wb")) {
            std::fprintf(f, "P5\n%d %d\n255\n", cw, ch);
            for (int y = 0; y < ch; ++y) std::fwrite(cp[i] + int64_t(y) * planes.stride_c, 1, size_t(cw), f);
            std::fclose(f);
          }
        }
      }
    }
    t_native_frame = true;
    ++c.native;
    ++c.frame;
    ++c.window_frames;
    c.window_ms += double(NowUs() - t0) / 1000.0;
    Report(c, NowUs());
    ctx.r3.u64 = 0;
    return;
  }
  ++c.fallback;
  Report(c, NowUs());
  __imp__sub_828F4480(ctx, base);
}

REX_HOOK_RAW(sub_8293F600) {
  if (t_native_frame && InFrameFunction(ctx)) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8293F600(ctx, base);
}

REX_HOOK_RAW(sub_8291F918) {
  if (t_native_frame && InFrameFunction(ctx)) {
    ctx.r3.u64 = 0;
    t_native_frame = false;
    return;
  }
  const uint32_t game_ctx = ctx.r3.u32;
  const bool was_shadow = t_shadow_valid && InFrameFunction(ctx);
  __imp__sub_8291F918(ctx, base);
  if (was_shadow) {
    t_shadow_valid = false;
    Planes planes;
    const auto& f = t_shadow;
    if (GamePlanes(base, game_ctx, f.width, planes)) {
      uint64_t sum = 0;
      int mx = 0;
      for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x) {
          const int d = std::abs(int(planes.y[int64_t(y) * planes.stride_y + x]) -
                                 int(f.planes[0][int64_t(y) * f.strides[0] + x]));
          sum += uint64_t(d);
          mx = std::max(mx, d);
        }
      // The chroma planes too (their differences count into the same maximum).
      const int wc = (f.width + 1) / 2, hc = (f.height + 1) / 2;
      for (int y = 0; y < hc; ++y)
        for (int x = 0; x < wc; ++x) {
          mx = std::max(mx, std::abs(int(planes.u[int64_t(y) * planes.stride_c + x]) -
                                     int(f.planes[1][int64_t(y) * f.strides[1] + x])));
          mx = std::max(mx, std::abs(int(planes.v[int64_t(y) * planes.stride_c + x]) -
                                     int(f.planes[2][int64_t(y) * f.strides[2] + x])));
        }
      const double mean = double(sum) / double(f.width * f.height);
      std::lock_guard<std::mutex> lock(g_mutex);
      g_cur.worst_mean = std::max(g_cur.worst_mean, mean);
      g_cur.worst_max = std::max(g_cur.worst_max, double(mx));
      ++g_cur.window_frames;
      ++g_cur.native;
      Report(g_cur, NowUs());
    }
  }
}

namespace {
const bool g_unit = [] {
  g_video_native.amount_unit = "frames";
  return true;
}();
}  // namespace
