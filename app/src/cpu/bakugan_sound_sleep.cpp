// Bakugan: the game's sound thread sleeps between its mixer updates instead of spinning (CPU enhancement
// bakugan_cpu_sound_sleep).
//
// WHAT THE GAME DOES
//   The sound thread's main loop (sub_82127BA8) is: update every voice (sub_8212D9C8: critical sections, volumes),
//   then Sleep(0) (sub_823BC230 -> sub_823BE670 -> KeDelayExecutionThread with a zero interval, a yield), and again.
//   On the Xbox 360 that thread had a hardware thread to itself. On the Switch the application has 3 cores; the console
//   profile (2026-10-14, water arena) showed this thread running ~90 % of a core, next to the game thread spinning in
//   Direct3D's GPU wait, so the render thread that feeds the GPU had to share cores.
//
// WHAT THIS DOES
//   When sub_823BC230 is called from that loop with a zero interval, the thread sleeps bakugan_sound_sleep_us first
//   (default 1000 us). The mixer still runs ~1000 times a second (it is paced by a timer difference it computes
//   itself, sub_82127538), far more often than the 5.33 ms audio frames.
//
// WHY IT IS EXACT: no game state changes; only when the next iteration starts.
// RISK: an audio underrun if the thread is starved for longer than the buffers cover (it was never starved before).
// KEY: bakugan_cpu_sound_sleep (with bakugan_cpu_enhancements); bakugan_sound_sleep_us.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "cpu/bakugan_enhancements.h"

BAKUGAN_ENHANCEMENT(g_sound_sleep, bakugan_cpu_sound_sleep, bakugan::enh::kCpu, true,
                "The game's sound thread sleeps between mixer updates (its loop yields with Sleep(0) and spun a core)");
REXCVAR_DEFINE_INT32(bakugan_limiter_margin_ms, 4, "BAKUGAN enhancements",
                     "bakugan_cpu_sound_sleep: the game's frame limiter (sub_823B4F30) sleeps Sleep(remaining - 1 ms) and "
                     "spins the rest; on the Switch a woken thread can run several ms late, which misses a vsync and "
                     "doubles the frame (66.7 ms). The sleep is shortened by this many ms and the game's own spin loop "
                     "finishes the wait (0 = as the game asks)")
    .range(0, 15)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(bakugan_sound_priority, false, "BAKUGAN enhancements",
                    "Switch, with bakugan_cpu_sound_sleep: the game's sound thread (the mixer that feeds the audio output) runs "
                    "at the audio priority (0x2B) instead of the guest one (0x3B). Console log 2026-10-16, water arena: with "
                    "the main thread at 90 %, the ring at 55 % and a game worker at 48 % the three cores were full, the sound "
                    "thread had to wait its turn among them and 12-37 % of the audio frames of the last 20 s were never "
                    "delivered (the robotic sound). It sleeps 1 ms every iteration, so it cannot hog a core.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(bakugan_limiter_yield_us, 150, "BAKUGAN enhancements",
                     "bakugan_cpu_sound_sleep: the frame limiter's final wait (sub_823B4F30 loop at 0x823B52C0: it reads the "
                     "timebase over and over until the frame time is up) sleeps in steps while more than this many "
                     "microseconds remain, and spins only the rest. On the console (water arena, 2026-10-16) 55 % of the "
                     "main thread's CPU samples were in that spin, a third of a core, while the three cores were full and the "
                     "audio starved. 0 = spin as the game does")
    .range(0, 2000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(bakugan_sound_sleep_us, 1000, "BAKUGAN enhancements",
                     "bakugan_cpu_sound_sleep: sleep per iteration of the sound thread's loop, in microseconds")
    .range(100, 20000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
constexpr uint32_t kSleepWrapper = 0x823BC230;  // Sleep(0): li r4,0; b sub_823BE670
constexpr uint32_t kSleepBytes = 8;
constexpr uint64_t kSleepHash = 0x5B34F6BA7B04A675ull;
constexpr uint32_t kLoop = 0x82127BA8;  // the sound thread's main loop
constexpr uint32_t kLoopBytes = 568;
constexpr uint64_t kLoopHash = 0x4B702404E8A542BEull;
constexpr uint32_t kFrame = 0x823B4F30;  // the main frame loop with the software frame limiter (29.97 fps)
constexpr uint32_t kFrameBytes = 1016;
constexpr uint64_t kFrameHash = 0xF1288263FF643F96ull;
// Limiter sleep statistics, reported every 10 s from the hook itself (single thread: the game thread).
struct LimiterStats {
  uint64_t calls = 0, over_2ms = 0, over_5ms = 0;
  double requested_ms = 0, over_ms = 0, max_over_ms = 0;
  std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
} g_limiter;
std::atomic<int> g_state{0};  // 0 unknown, 1 on, -1 off
std::atomic<uint64_t> g_limiter_yields{0};  // sleeps taken instead of spinning in the limiter's final wait
std::atomic<uint64_t> g_limiter_sleeps{0};  // frames that still had time left and slept (see LimiterSleeps)
}  // namespace

namespace bakugan::enh {
// The resolution controller reads this: frames - sleeps = frames that overran their 33.3 ms (the limiter had nothing to
// wait for). 0 while the hook is off.
uint64_t LimiterSleeps() { return g_limiter_sleeps.load(std::memory_order_relaxed); }
bool LimiterActive() { return g_state.load(std::memory_order_relaxed) > 0; }
}  // namespace bakugan::enh

// Which loop the calling thread is in. The sleep wrapper used to look at ctx.lr for that; the optimised codegen elides
// the lr stores, so the two loops announce themselves (thread-local depth counters set by hooks on their entries).
namespace {
thread_local int t_in_sound_loop = 0;
thread_local int t_in_frame_loop = 0;
}  // namespace

#ifdef __SWITCH__
extern "C" void RexSwitchSetCurrentThreadPriority(int priority);
#endif

REX_EXTERN(__imp__sub_82127BA8);
REX_HOOK_RAW(sub_82127BA8) {
  ++t_in_sound_loop;
#ifdef __SWITCH__
  // Audio priority while the loop sleeps between iterations (the wrapper below puts it back if that is not engaged).
  if (g_sound_sleep.active && REXCVAR_GET(bakugan_sound_priority)) {
    RexSwitchSetCurrentThreadPriority(0x2B);
    REXLOG_INFO("[enh] bakugan_sound_priority: the sound thread runs at priority 0x2B");
  }
#endif
  __imp__sub_82127BA8(ctx, base);
  --t_in_sound_loop;
}

REX_EXTERN(__imp__sub_823B4F30);
REX_HOOK_RAW(sub_823B4F30) {
  ++t_in_frame_loop;
  __imp__sub_823B4F30(ctx, base);
  --t_in_frame_loop;
}

// The limiter's final wait (sub_823B4F30, loop at 0x823B52C0) reads the timebase (mftb through the host clock: UpdateGuestClock, a
// mutex) over and over until the frame time is up. Detected by its rhythm, not by the register state (the optimised
// codegen keeps lr and the non-volatile registers out of the context): inside the frame loop, calls of this function
// that follow each other within 30 microseconds. After eight of them in a row the wait sleeps in short steps instead.
REX_EXTERN(__imp__sub_823BC1D8);
REX_HOOK_RAW(sub_823BC1D8) {
  if (t_in_frame_loop > 0 && g_state.load(std::memory_order_relaxed) > 0) {
    static const int32_t yield_us = REXCVAR_GET(bakugan_limiter_yield_us);
    if (yield_us > 0) {
      thread_local std::chrono::steady_clock::time_point last{};
      thread_local uint32_t tight = 0;
      const auto now = std::chrono::steady_clock::now();
      if (now - last < std::chrono::microseconds(30)) {
        if (++tight >= 8) {
          std::this_thread::sleep_for(std::chrono::microseconds(yield_us));
          g_limiter_yields.fetch_add(1, std::memory_order_relaxed);
          last = std::chrono::steady_clock::now();
          __imp__sub_823BC1D8(ctx, base);
          return;
        }
      } else {
        tight = 0;
      }
      last = now;
    }
  }
  __imp__sub_823BC1D8(ctx, base);
}

REX_EXTERN(__imp__sub_823BC230);
REX_HOOK_RAW(sub_823BC230) {
  int state = g_state.load(std::memory_order_relaxed);
  if (state == 0) {
    state = (g_sound_sleep.active &&
             bakugan::enh::GuestCodeMatches(base, kSleepWrapper, kSleepBytes, kSleepHash, "bakugan_cpu_sound_sleep") &&
             bakugan::enh::GuestCodeMatches(base, kLoop, kLoopBytes, kLoopHash, "bakugan_cpu_sound_sleep") &&
             bakugan::enh::GuestCodeMatches(base, kFrame, kFrameBytes, kFrameHash, "bakugan_cpu_sound_sleep"))
                ? 1
                : -1;
    g_state.store(state, std::memory_order_relaxed);
    REXLOG_INFO("[enh] bakugan_cpu_sound_sleep: {}", state > 0 ? "engaged (code check passed)" : "off");
#ifdef __SWITCH__
    // Not sleeping between iterations: a spinning loop at the audio priority would starve everything below it.
    if (state <= 0 && t_in_sound_loop > 0) RexSwitchSetCurrentThreadPriority(0x3B);
#endif
  }
  if (state > 0 && ctx.r3.u32 == 0 && t_in_sound_loop > 0) {
    static const uint32_t us = uint32_t(REXCVAR_GET(bakugan_sound_sleep_us));
    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::microseconds(us));
    g_sound_sleep.Use();
    g_sound_sleep.Add(uint64_t(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count()));
  }
  if (state > 0 && t_in_frame_loop > 0 && ctx.r3.u32 != 0) {
    // The frame limiter's Sleep(remaining - 1 ms): shorten it and measure how late the wake-up is.
    static const uint32_t margin = uint32_t(REXCVAR_GET(bakugan_limiter_margin_ms));
    const uint32_t asked = ctx.r3.u32;
    const uint32_t shortened = asked > margin ? asked - margin : 0;
    ctx.r3.u64 = shortened;
    const auto t0 = std::chrono::steady_clock::now();
    __imp__sub_823BC230(ctx, base);
    const auto t1 = std::chrono::steady_clock::now();
    const double took = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double over = took - double(shortened);
    ++g_limiter.calls;
    g_limiter_sleeps.fetch_add(1, std::memory_order_relaxed);
    g_limiter.requested_ms += asked;
    g_limiter.over_ms += over;
    if (over > g_limiter.max_over_ms) g_limiter.max_over_ms = over;
    if (over > 2.0) ++g_limiter.over_2ms;
    if (over > 5.0) ++g_limiter.over_5ms;
    if (t1 - g_limiter.last > std::chrono::seconds(10) && g_limiter.calls) {
      REXLOG_INFO("[enh] frame limiter spin replaced by {} short sleeps (10 s)", g_limiter_yields.exchange(0));
      REXLOG_INFO("[enh] frame limiter sleeps (10 s): {} calls, asked {:.1f} ms on average (shortened by {} ms), "
                  "woke {:.2f} ms late on average, worst {:.1f} ms, {} over 2 ms, {} over 5 ms",
                  g_limiter.calls, g_limiter.requested_ms / double(g_limiter.calls), margin,
                  g_limiter.over_ms / double(g_limiter.calls), g_limiter.max_over_ms, g_limiter.over_2ms,
                  g_limiter.over_5ms);
      g_limiter = LimiterStats{};
    }
    return;
  }
  __imp__sub_823BC230(ctx, base);
}

namespace {
const bool g_unit = [] {
  g_sound_sleep.amount_unit = "us asleep";
  return true;
}();
}  // namespace
