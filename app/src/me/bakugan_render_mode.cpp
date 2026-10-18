// Sonic Adventure 2: render resolution per console mode and MSAA (see bakugan_render_mode.h).
#include "bakugan_render_mode.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/switch_saltynx.h>

#include "cpu/bakugan_enhancements.h"

#if REX_PLATFORM_SWITCH
#include <switch.h>
#endif

REXCVAR_DEFINE_INT32(bakugan_render_height_handheld, 720, "BAKUGAN",
                     "Render height in handheld mode (the PC's starting mode unless bakugan_render_docked): 720 = the "
                     "game's own 1280x720. Any multiple of 180 from 360 to 2160 (others are rounded to the nearest); "
                     "the width follows at 16:9")
    .range(180, 4320)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(bakugan_render_height_docked, 1080, "BAKUGAN",
                     "Render height in docked mode (the TV): 1080 = 1920x1080. Any multiple of 180 from 360 to "
                     "2160; switching between docked and handheld changes it at the next frame")
    .range(180, 4320)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(bakugan_render_docked, false, "BAKUGAN",
                    "PC only: start in the simulated docked mode (bakugan_render_height_docked). The bind_bakugan_docked key "
                    "(F9) switches between docked and handheld while the game runs, like docking the Switch. The "
                    "Switch itself always follows the console");
REXCVAR_DEFINE_INT32(bakugan_msaa, 0, "BAKUGAN",
                     "Multisample anti-aliasing of the game's own (single-sample) render targets: 0 = off, as the "
                     "game and as before (default); 2 or 4 = that many samples per pixel, resolved when the game "
                     "copies the image. The game's 4x surfaces (Direct3D's fast Clear aliases) stay as they are. Read "
                     "at start")
    .range(0, 4)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(bakugan_render_dynamic, true, "BAKUGAN",
                    "Dynamic render scale: while the GPU is the limit (busy above ~88 % of the time and under 28.5 "
                    "fps) the render height steps down by 180 pixels, to bakugan_render_dynamic_min_height_*, and "
                    "steps back up when there is enough headroom. A step costs one hitch (the targets are remade). "
                    "false = always the configured height");
REXCVAR_DEFINE_DOUBLE(bakugan_render_dynamic_down_fps, 28.2, "BAKUGAN",
                      "bakugan_render_dynamic: the render height only steps down while the frame rate is under this (and the "
                      "GPU is over ~88 % busy). A step is a 150-500 ms hitch (the targets are remade: 63 images at the start "
                      "of a session, 116 after an hour of story), so a window at 28 fps (a 2 fps dip) does not pay for it")
    .range(20.0, 30.0);
REXCVAR_DEFINE_DOUBLE(bakugan_render_dynamic_up_util, 0.80, "BAKUGAN",
                      "bakugan_render_dynamic: a step up needs the p90 GPU busy fraction of the current height times the height "
                      "ratio to stay under this. Higher = bolder (more time at the higher height, a dip to 27-28 fps in the "
                      "heaviest moments); 0.86 is the bold setting (test AP_bolder)")
    .range(0.6, 0.95);
REXCVAR_DEFINE_INT32(bakugan_render_dynamic_up_windows, 8, "BAKUGAN",
                     "bakugan_render_dynamic: windows of 1.5 s that must be lived at a height before the step up, and the "
                     "p90 of their GPU busy time (x the height ratio) must stay under 80 %. 8 = 12 s")
    .range(4, 48);
REXCVAR_DEFINE_INT32(bakugan_render_dynamic_heavy_start, 630, "BAKUGAN",
                     "bakugan_render_dynamic_heavy_cap: the handheld render height a heavy scene (battle) starts at before any "
                     "heavy scene has taught the controller better (a multiple of 90; 720 = no cap)")
    .range(360, 720);
REXCVAR_DEFINE_INT32(bakugan_render_dynamic_heavy_start_docked, 810, "BAKUGAN",
                     "bakugan_render_dynamic_heavy_cap: the same for the docked mode. Console, 2026-10-17: the first battle began at "
                     "1080p and went 990 -> 900 -> 720 in 20 s (89-92 % busy, 24-26 fps); 810p held the 2x2 battle at 27-32 ms")
    .range(360, 2160);
REXCVAR_DEFINE_BOOL(bakugan_render_dynamic_heavy_cap, true, "BAKUGAN",
                    "bakugan_render_dynamic: the height a heavy scene (battle: >= 100 GPU marks a frame) fell to is remembered "
                    "and applied when the next heavy scene starts, instead of beginning at the height the calm menus climbed "
                    "to and overloading for ~10 s. The ceiling rises again after 30 s without overload and is forgotten after "
                    "10 min without a heavy scene")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_DOUBLE(bakugan_render_dynamic_late_frac, 0.25, "BAKUGAN",
                      "bakugan_render_dynamic: also step down when this fraction of the frames of a 1.5 s window overran "
                      "their 33.3 ms (the game's frame limiter had no time left to sleep) while the GPU is over "
                      "bakugan_render_dynamic_late_util busy, even if the average rate is still 30 fps. Such frames show as "
                      "stutter (a 50 ms frame followed by a short one): the story cutscene of 2026-10-16 ran 29.9 fps at "
                      "83 % GPU busy with 25-60 % of the frames late and looked laggy. 0 = off")
    .range(0.0, 1.0);
REXCVAR_DEFINE_INT32(bakugan_render_dynamic_late_floor, 6, "BAKUGAN",
                     "bakugan_render_dynamic_late_frac only steps down while the height is above this many eighths of the base "
                     "(6 = never below 540p of 720p); 0 = no floor besides the minimum height")
    .range(0, 8);
REXCVAR_DEFINE_DOUBLE(bakugan_render_dynamic_late_util, 0.85, "BAKUGAN",
                      "bakugan_render_dynamic_late_frac: the GPU busy fraction above which late frames count")
    .range(0.3, 1.0);
REXCVAR_DEFINE_INT32(bakugan_render_dynamic_min_height_handheld, 450, "BAKUGAN",
                     "bakugan_render_dynamic: lowest render height in handheld mode (a multiple of 180)")
    .range(360, 2160);
REXCVAR_DEFINE_INT32(bakugan_render_dynamic_min_height_docked, 720, "BAKUGAN",
                     "bakugan_render_dynamic: lowest render height in docked mode (a multiple of 180)")
    .range(360, 2160);
REXCVAR_DEFINE_INT32(bakugan_render_test_toggle_at, 0, "BAKUGAN",
                     "Test (PC): toggle the simulated docked mode after this presented swap (0 = never)")
    .range(0, 2000000000);
REXCVAR_DEFINE_INT32(bakugan_render_test_toggle_every, 0, "BAKUGAN",
                     "Test (PC): after bakugan_render_test_toggle_at, toggle again every this many presented swaps "
                     "(0 = once)")
    .range(0, 2000000000);

// Non-CPU group: the new behaviours have their own keys like every other change (docs/enhancements.md).
BAKUGAN_ENHANCEMENT(g_render_scale, bakugan_gpu_render_scale, bakugan::enh::kNonCpu, true,
                "GPU: render at bakugan_render_height_handheld / bakugan_render_height_docked (720 / 1080 by default), "
                "following docked / handheld live; off = always the game's 1280x720");
BAKUGAN_ENHANCEMENT(g_msaa, bakugan_gpu_msaa, bakugan::enh::kNonCpu, true,
                "GPU: honour bakugan_msaa (2x / 4x host multisampling of the game's single-sample render targets, "
                "resolved at the game's copies); with bakugan_msaa = 0 (default) nothing changes");

namespace bakugan::render {
namespace {

std::atomic<bool> g_docked_pc{false};
std::atomic<bool> g_docked_pc_set{false};
std::atomic<uint32_t> g_last_logged_q{0};

// q = height / 90 (eighths of 720p), from 4 (360p) to 24 (2160p); other heights go to the nearest multiple of 90.
uint32_t QuartersOf(int32_t height) {
  const int32_t q = (std::clamp(height, 360, 2160) + 45) / 90;
  return uint32_t(std::clamp(q, 2, 12));
}

bool DockedPc() {
  if (!g_docked_pc_set.load(std::memory_order_acquire)) {
    bool expected = false;
    if (g_docked_pc_set.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
      g_docked_pc.store(REXCVAR_GET(bakugan_render_docked), std::memory_order_release);
  }
  return g_docked_pc.load(std::memory_order_acquire);
}

}  // namespace

bool Docked() {
#if REX_PLATFORM_SWITCH
  // libnx keeps the operation mode cached; the SDK's applet loop (windowed_app_context_switch.cpp) processes the
  // OperationModeChanged message that updates it. Reverse-NX's "fake" mode is obeyed, as the window does.
  return rex::ui::switch_saltynx::BaseMode(appletGetOperationMode() == AppletOperationMode_Console);
#else
  return DockedPc();
#endif
}

namespace {
// Dynamic scale controller state (ring thread only, except the busy counter).
std::atomic<uint64_t> g_gpu_busy_ns{0};
std::atomic<uint64_t> g_gpu_marks{0};  // timestamp marks of all frames so far: ~130-160 a frame in a battle, 7-50 elsewhere
std::atomic<uint64_t> g_rescales_applied{0};  // rescales actually done (a decision can wait up to 20 s for a calm frame)
struct Dynamic {
  bool docked = false;
  bool valid = false;
  uint32_t q = 0;  // current dynamic scale
  std::chrono::steady_clock::time_point window_start;
  std::chrono::steady_clock::time_point last_change;
  uint64_t busy_start = 0;
  uint32_t swaps = 0;
  uint64_t sleeps_start = 0;  // limiter sleeps at the start of the window
  uint32_t hot_windows = 0, cool_windows = 0;
  // GPU busy of the 1.5 s windows lived through at the current height (reset when a rescale is applied): the step up
  // is judged by the busy time of the HEAVY windows (p90), not by the calm one it happens to be in.
  std::array<float, 48> util_history{};
  uint32_t util_count = 0;
  uint64_t rescales_seen = 0;
  // Heavy scenes (a battle: >= 100 marks a frame) have their own ceiling. Console, 2026-10-16: every battle began at the
  // height the calm menus had climbed to (720p), overloaded for ~10 s (24-27 fps) and dropped; the next battle did the
  // same. The height a heavy scene fell to is remembered and applied when the next one starts.
  uint64_t marks_start = 0;
  uint32_t heavy_cap = 0;  // set when the state is made: bakugan_render_dynamic_heavy_start, then what the last heavy scene fell to
  std::chrono::steady_clock::time_point last_heavy;
  bool in_heavy = false;  // the previous window was heavy
  std::chrono::steady_clock::time_point heavy_since;  // start of the current run of heavy windows
  // A decision is only a request: the targets are remade at a calm frame, up to bakugan_render_rescale_wait_{down,up}_s
  // later. Until it is applied the windows still show the OLD height, and deciding on them stacked steps (console,
  // 2026-10-16: 720 -> 630 -> 540 -> 450 decided in 6 s while the game was still at 720; 450 -> 540 -> 630 -> 720 in 18 s,
  // applied as one jump 450 -> 720). No new decision until the last one is applied (or 40 s have passed).
  bool pending = false;
  uint64_t applied_at_decision = 0;
  std::chrono::steady_clock::time_point pending_since;
  uint32_t penalty = 0;  // each down step soon after an up step doubles the wait before the next up step
  std::chrono::steady_clock::time_point last_up;
  // A height the GPU could not hold is not tried again for a while: 90 s after the first failure, doubling with each
  // further one (at most ~12 min). Every try costs two steps of ~200 ms hitches (up, and back down), and the heavy
  // moments of a fight come back every 30-60 s. Indexed by the scale in eighths (q <= 31).
  std::array<std::chrono::steady_clock::time_point, 32> blocked_until{};
  std::array<uint8_t, 32> failures{};
} g_dyn;
}  // namespace

void NoteGpuBusy(uint64_t ns, uint32_t marks) {
  g_gpu_marks.fetch_add(marks, std::memory_order_relaxed);
#if REX_PLATFORM_SWITCH
  ns = uint64_t(double(ns) * 1.627);  // raw NVK timestamps run slow by this factor (the same one the 10 s report uses)
#endif
  g_gpu_busy_ns.fetch_add(ns, std::memory_order_relaxed);
}

// ---- ablation (diagnostic): skip groups of vertex shaders in turn and log what the frame time does -----------------
// bakugan_ablate_vs_groups = "744,1082;779;800" (VS numbers, groups separated by ';') with bakugan_ablate_phase_s:
// phases alternate baseline / group 0 / baseline / group 1 ...; at the end of each phase one line says the fps and the
// GPU ms per frame. Meant for a fixed render height (bakugan_render_dynamic = false).
REXCVAR_DEFINE_INT32(bakugan_render_force_after_s, 0, "BAKUGAN",
                     "Test: this many seconds after the first frame the handheld render height is held at "
                     "bakugan_render_force_height_handheld (0 = off). Starting the game at 540 itself hung the console.")
    .range(0, 3600);
REXCVAR_DEFINE_INT32(bakugan_render_force_height_handheld, 540, "BAKUGAN",
                     "Test (bakugan_render_force_after_s): the handheld render height to hold").range(180, 1080);

REXCVAR_DEFINE_STRING(bakugan_ablate_vs_groups, "", "BAKUGAN",
                      "Diagnostic: vertex shader numbers to skip in turn, groups separated by ';' (see bakugan_ablate_phase_s)");
REXCVAR_DEFINE_INT32(bakugan_ablate_arm_ms, 20, "BAKUGAN", "Diagnostic: GPU ms per frame (3 s in a row) that starts the ablation").range(1, 100);
REXCVAR_DEFINE_INT32(bakugan_ablate_phase_s, 12, "BAKUGAN", "Diagnostic: seconds per ablation phase").range(3, 120);

namespace {
std::vector<std::unordered_set<uint32_t>> g_ablate_groups;
std::vector<std::string> g_ablate_names;
std::atomic<int> g_ablate_group{-1};
struct AblateState {
  bool init = false;
  int64_t start_ns = 0;
  int sequence = -1;
  std::chrono::steady_clock::time_point phase_start;
  uint64_t phase_busy_start = 0;
  uint64_t frames = 0;
};
AblateState g_ablate;

void AblationInit() {
  g_ablate.init = true;
  const std::string list = REXCVAR_GET(bakugan_ablate_vs_groups);
  std::stringstream groups(list);
  std::string group;
  while (std::getline(groups, group, ';')) {
    std::unordered_set<uint32_t> set;
    std::stringstream items(group);
    std::string item;
    while (std::getline(items, item, ',')) {
      if (!item.empty()) set.insert(uint32_t(std::strtoul(item.c_str(), nullptr, 10)));
    }
    if (!set.empty()) {
      g_ablate_groups.push_back(std::move(set));
      g_ablate_names.push_back(group);
    }
  }
}

void AblationTick() {
  if (!g_ablate.init) AblationInit();
  if (g_ablate_groups.empty()) return;
  const auto now = std::chrono::steady_clock::now();
  // Armed by load: three 1 s windows in a row with at least bakugan_ablate_arm_ms of GPU time per frame (a battle
  // arena; menus and cutscenes are far below), so the phases measure the scene the shaders belong to.
  static bool armed = false;
  static int busy_windows = 0;
  static auto window_start = now;
  static uint64_t window_busy_start = g_gpu_busy_ns.load(std::memory_order_relaxed), window_frames = 0;
  if (!armed) {
    ++window_frames;
    if (std::chrono::duration<double>(now - window_start).count() >= 1.0) {
      const uint64_t b = g_gpu_busy_ns.load(std::memory_order_relaxed);
      const double ms = window_frames ? double(b - window_busy_start) / 1e6 / double(window_frames) : 0.0;
      busy_windows = ms >= double(REXCVAR_GET(bakugan_ablate_arm_ms)) ? busy_windows + 1 : 0;
      window_start = now;
      window_busy_start = b;
      window_frames = 0;
      if (busy_windows >= 3) {
        armed = true;
        REXLOG_INFO("[ablate] armed: the scene is GPU heavy ({:.1f} ms/frame); phases start now", ms);
      }
    }
    if (!armed) return;
  }
  const uint64_t busy = g_gpu_busy_ns.load(std::memory_order_relaxed);
  if (g_ablate.sequence < 0) {
    g_ablate.sequence = 0;
    g_ablate.phase_start = now;
    g_ablate.phase_busy_start = busy;
    g_ablate_group.store(-1, std::memory_order_relaxed);
    return;
  }
  ++g_ablate.frames;
  const double secs = std::chrono::duration<double>(now - g_ablate.phase_start).count();
  if (secs < double(REXCVAR_GET(bakugan_ablate_phase_s))) return;
  const int groups = int(g_ablate_groups.size());
  const int current = g_ablate_group.load(std::memory_order_relaxed);
  REXLOG_INFO("[ablate] phase {} ({}): {} frames in {:.1f} s = {:.1f} fps, GPU busy {:.2f} ms/frame", g_ablate.sequence,
              current < 0 ? std::string("baseline") : fmt::format("skipping VS {}", g_ablate_names[size_t(current)]),
              g_ablate.frames, secs, double(g_ablate.frames) / secs,
              g_ablate.frames ? double(busy - g_ablate.phase_busy_start) / 1e6 / double(g_ablate.frames) : 0.0);
  ++g_ablate.sequence;
  const int s = g_ablate.sequence % (2 * groups);
  g_ablate_group.store((s & 1) ? s / 2 : -1, std::memory_order_relaxed);
  g_ablate.phase_start = now;
  g_ablate.phase_busy_start = busy;
  g_ablate.frames = 0;
}
}  // namespace

bool AblationSkipsVs(uint32_t number) {
  const int g = g_ablate_group.load(std::memory_order_relaxed);
  return g >= 0 && g_ablate_groups[size_t(g)].count(number) != 0;
}

namespace {
uint32_t DynamicScale(bool docked, uint32_t base_q) {
  const uint32_t min_q = std::min(base_q, QuartersOf(docked ? REXCVAR_GET(bakugan_render_dynamic_min_height_docked)
                                                           : REXCVAR_GET(bakugan_render_dynamic_min_height_handheld)));
  const auto now = std::chrono::steady_clock::now();
  Dynamic& d = g_dyn;
  if (!d.valid || d.docked != docked) {
    d = Dynamic{};
    d.valid = true;
    d.docked = docked;
    d.q = base_q;
    d.window_start = d.last_change = now;
    d.busy_start = g_gpu_busy_ns.load(std::memory_order_relaxed);
    d.marks_start = g_gpu_marks.load(std::memory_order_relaxed);
    d.sleeps_start = bakugan::enh::LimiterSleeps();
    // No heavy scene seen yet: every battle of the console's tests began at the menus' 720p and overloaded (24-28 fps for
    // ~10 s), so heavy scenes start at bakugan_render_dynamic_heavy_start (handheld only) until one teaches otherwise.
    d.heavy_cap = std::min(base_q, QuartersOf(docked ? REXCVAR_GET(bakugan_render_dynamic_heavy_start_docked)
                                                       : REXCVAR_GET(bakugan_render_dynamic_heavy_start)));
    return d.q;
  }
  d.q = std::clamp(d.q, min_q, base_q);
  ++d.swaps;
  const double wall = std::chrono::duration<double>(now - d.window_start).count();
  if (wall < 1.5) return d.q;
  const uint64_t busy_now = g_gpu_busy_ns.load(std::memory_order_relaxed);
  const double util = double(busy_now - d.busy_start) * 1e-9 / wall;
  const double fps = double(d.swaps) / wall;
  if (const uint64_t applied = g_rescales_applied.load(std::memory_order_relaxed); applied != d.rescales_seen) {
    d.rescales_seen = applied;
    d.util_count = 0;  // a new height: its history starts here (this window holds the rescale's own hitch)
  } else {
    d.util_history[d.util_count++ % d.util_history.size()] = float(util);
  }
  // Frames that overran: the limiter slept in fewer calls than there were frames.
  const uint64_t sleeps_now = bakugan::enh::LimiterSleeps();
  const double late = bakugan::enh::LimiterActive() && d.swaps >= 20
                          ? std::clamp(1.0 - double(sleeps_now - d.sleeps_start) / double(d.swaps), 0.0, 1.0)
                          : 0.0;
  const uint64_t marks_now = g_gpu_marks.load(std::memory_order_relaxed);
  const double marks_per_swap = d.swaps ? double(marks_now - d.marks_start) / double(d.swaps) : 0.0;
  d.marks_start = marks_now;
  const bool heavy = marks_per_swap >= 100.0;
  if (heavy && !d.in_heavy) d.heavy_since = now;
  d.in_heavy = heavy;
  d.sleeps_start = sleeps_now;
  d.window_start = now;
  d.busy_start = busy_now;
  d.swaps = 0;
  static uint32_t window_log = 0;
  if (++window_log % 7 == 0)
    REXLOG_INFO("[res] dynamic window: GPU busy {:.0f} % at {:.1f} fps, {:.0f} % of the frames late, render height {} "
                "(min {}, base {})", util * 100.0, fps, late * 100.0, 90 * d.q, 90 * min_q, 90 * base_q);
  if (d.pending) {
    if (g_rescales_applied.load(std::memory_order_relaxed) > d.applied_at_decision) {
      d.pending = false;
      d.last_change = now;  // the dwell at the new height starts when it exists
    } else if (std::chrono::duration<double>(now - d.pending_since).count() > 40.0) {
      d.pending = false;
    } else {
      d.hot_windows = d.cool_windows = 0;
      return d.q;
    }
  }
  const double since_change = std::chrono::duration<double>(now - d.last_change).count();
  static const bool heavy_cap_on = REXCVAR_GET(bakugan_render_dynamic_heavy_cap);
  if (heavy_cap_on && heavy) {
    if (std::chrono::duration<double>(now - d.last_heavy).count() > 600.0 && d.last_heavy != std::chrono::steady_clock::time_point{})
      d.heavy_cap = std::min(base_q, QuartersOf(docked ? REXCVAR_GET(bakugan_render_dynamic_heavy_start_docked)
                                                       : REXCVAR_GET(bakugan_render_dynamic_heavy_start)));
    d.last_heavy = now;
    // A heavy scene that has held a height for 30 s raises its ceiling to it (30 s of THIS heavy scene: the first version
    // counted the time since the last change, which at the start of every battle is minutes, and raised the ceiling to
    // the menus' 720p in the first heavy window: console 2026-10-17, the cap never applied).
    if (d.q > d.heavy_cap && since_change > 30.0 && util < 0.85 &&
        std::chrono::duration<double>(now - d.heavy_since).count() > 30.0)
      d.heavy_cap = d.q;
    if (d.q > d.heavy_cap && d.q > min_q) {
      const uint32_t from = d.q;
      d.q = std::max(min_q, d.heavy_cap);
      d.hot_windows = 0;
      d.last_change = now;
      d.pending = true;
      d.pending_since = now;
      d.applied_at_decision = g_rescales_applied.load(std::memory_order_relaxed);
      REXLOG_INFO("[res] dynamic: heavy scene ({:.0f} marks a frame, GPU {:.0f} % busy at {:.1f} fps): render height {} -> {} "
                  "(the height the last heavy scene fell to)", marks_per_swap, util * 100.0, fps, 90 * from, 90 * d.q);
      return d.q;
    }
  }
  // GPU-bound and under the target rate: step down (two windows in a row).
  static const double down_fps = REXCVAR_GET(bakugan_render_dynamic_down_fps);
  static const double late_frac = REXCVAR_GET(bakugan_render_dynamic_late_frac);
  static const double late_util = REXCVAR_GET(bakugan_render_dynamic_late_util);
  static const uint32_t late_floor = uint32_t(REXCVAR_GET(bakugan_render_dynamic_late_floor));
  const bool stutter = late_frac > 0.0 && late >= late_frac && util > late_util && d.q > late_floor;
  // Hot windows are counted with a slow decay (a calm window takes one off instead of zeroing the count): console, docked 2x2
  // battle 2026-10-17: 27.4-28.4 fps at 810p with the GPU 82-97 % busy for minutes, windows alternating just under and over
  // the old 27.5 fps / 88 % thresholds, so "two in a row" never came. Three hot windows, or two under 26.5 fps.
  if (((util > 0.84 && fps < down_fps) || stutter) && d.q > min_q) {
    d.cool_windows = 0;
    ++d.hot_windows;
    if ((d.hot_windows >= 3 || (d.hot_windows >= 2 && fps < 26.5)) && since_change > 3.0) {
      const uint32_t from = d.q;
      // One step is 90 px (an eighth); well under the target rate it takes two, so the ladder is climbed in fewer
      // hitches (each step is a ~200 ms frame).
      d.q = std::max(min_q, from - ((fps < 26.0 && util > 0.90) ? 2u : 1u));
      d.hot_windows = 0;
      d.last_change = now;
      d.pending = true;
      d.pending_since = now;
      d.applied_at_decision = g_rescales_applied.load(std::memory_order_relaxed);
      if (heavy) d.heavy_cap = std::min<uint32_t>(d.heavy_cap, d.q);  // the next heavy scene starts here
      // The heights above the new one failed: no step back up into them for a while (see blocked_until).
      for (uint32_t h = d.q + 1; h <= from && h < d.blocked_until.size(); ++h) {
        if (now > d.blocked_until[h] + std::chrono::minutes(10)) d.failures[h] = 0;  // an old failure is forgotten
        const uint32_t n = std::min<uint32_t>(d.failures[h], 3u);
        d.blocked_until[h] = now + std::chrono::seconds(90u << n);
        if (d.failures[h] < 255) ++d.failures[h];
      }
      if (std::chrono::duration<double>(now - d.last_up).count() < 60.0) d.penalty = std::min(d.penalty + 1, 4u);
      else d.penalty = 0;
      REXLOG_INFO("[res] dynamic: GPU {:.0f} % busy at {:.1f} fps{} -> render height {} -> {}", util * 100.0, fps,
                  stutter ? fmt::format(", {:.0f} % of the frames late (stutter)", late * 100.0) : std::string(),
                  90 * from, 90 * d.q);
    }
    return d.q;
  }
  if (d.hot_windows) --d.hot_windows;
  // Headroom: the cost of the next step up is about the pixel ratio; go back when it would still fit with room.
  if (d.q < base_q && fps > 29.0) {
    // The GPU time of a frame grows far slower than its pixels: measured on the console (6 arenas, 2026-10-15) the
    // busy time went 18.6 ms at 450p -> 19-24 ms at 720p (2.56x the pixels, ~1.3x the time; ~pixels^0.36) because a
    // large part of a frame is fixed cost (EDRAM transfers, copies, vertex work). The pixel ratio predicted 47 ms and
    // kept the water arena at 450p for 81 % of its fights. The height ratio (pixels^0.5) is still on the safe side.
    const double ratio = double(d.q + 1) / double(d.q);
    const bool blocked = d.q + 1 < d.blocked_until.size() && now < d.blocked_until[d.q + 1];
    // Seen on the console (2026-10-16, water/fire/haos): the climb 450p -> 720p took 18 s on three calm windows (31-50 %
    // busy), the heavy moment of the fight came back (86-93 % busy at 720p, 22-26 fps) and the descent was three steps in
    // 10 s; the controller then sat at 450p for 90-180 s with the GPU 55 % busy. A step up now needs bakugan_render_
    // dynamic_up_windows windows (1.5 s each) lived at this height and the p90 of their busy time must still fit after it.
    static const uint32_t up_windows = uint32_t(REXCVAR_GET(bakugan_render_dynamic_up_windows));
    double p90 = util;
    if (d.util_count >= up_windows) {
      std::array<float, 48> sorted = d.util_history;
      const size_t n = std::min<size_t>(d.util_count, sorted.size());
      std::sort(sorted.begin(), sorted.begin() + n);
      p90 = double(sorted[std::min(n - 1, n * 9 / 10)]);
    }
    static const double up_util = REXCVAR_GET(bakugan_render_dynamic_up_util);
    if (p90 * ratio < up_util && d.util_count >= up_windows && !blocked) {
      if (since_change > 8.0 * double(1u << d.penalty)) {
        const uint32_t from = d.q;
        d.q = from + 1;
        d.cool_windows = 0;
        if (heavy) d.heavy_cap = std::max(d.heavy_cap, d.q);  // a climb inside a heavy scene moves its ceiling
        d.last_change = now;
        d.last_up = now;
        d.pending = true;
        d.pending_since = now;
        d.applied_at_decision = g_rescales_applied.load(std::memory_order_relaxed);
        REXLOG_INFO("[res] dynamic: GPU {:.0f} % busy (p90 {:.0f} % over {} windows) at {:.1f} fps -> render height {} -> {}",
                    util * 100.0, p90 * 100.0, d.util_count, fps, 90 * from, 90 * d.q);
      }
    } else {
      d.cool_windows = 0;
    }
  } else {
    d.cool_windows = 0;
  }
  return d.q;
}
}  // namespace

uint32_t WantedScaleQuarters() {
  AblationTick();
  if (!g_render_scale.active) return 8;
  const bool docked = Docked();
  const int32_t height = docked ? REXCVAR_GET(bakugan_render_height_docked) : REXCVAR_GET(bakugan_render_height_handheld);
  uint32_t q = QuartersOf(height);
  static const auto first_call = std::chrono::steady_clock::now();
  const int32_t force_after = REXCVAR_GET(bakugan_render_force_after_s);
  if (force_after > 0 && !docked &&
      std::chrono::duration<double>(std::chrono::steady_clock::now() - first_call).count() >= double(force_after)) {
    // Test mode: the game starts at the normal height (starting at 540 hung the console twice) and is then held at
    // bakugan_render_force_height_handheld, with no dynamic stepping.
    q = std::min(q, QuartersOf(REXCVAR_GET(bakugan_render_force_height_handheld)));
  } else if (REXCVAR_GET(bakugan_render_dynamic)) {
    q = DynamicScale(docked, q);
  }
  if (g_last_logged_q.exchange(q | (docked ? 0x100u : 0u), std::memory_order_relaxed) != (q | (docked ? 0x100u : 0u))) {
    REXLOG_INFO("[res] {} mode: render {}x{} (bakugan_render_height_{} = {}{})", docked ? "docked" : "handheld",
                160 * q, 90 * q, docked ? "docked" : "handheld", height,
                int32_t(90 * q) != height ? fmt::format(", rounded to a multiple of 90") : std::string());
  }
  return q;
}

uint32_t ScaleQuartersOf(bool docked) {
  if (!g_render_scale.active) return 8;
  return QuartersOf(docked ? REXCVAR_GET(bakugan_render_height_docked) : REXCVAR_GET(bakugan_render_height_handheld));
}

void ToggleDocked() {
#if !REX_PLATFORM_SWITCH
  const bool docked = !DockedPc();
  g_docked_pc.store(docked, std::memory_order_release);
  REXLOG_INFO("[res] simulated {} (PC): render height {}", docked ? "docking" : "undocking",
              docked ? REXCVAR_GET(bakugan_render_height_docked) : REXCVAR_GET(bakugan_render_height_handheld));
#endif
}

void OnPresented(uint64_t presented) {
#if !REX_PLATFORM_SWITCH
  const int32_t at = REXCVAR_GET(bakugan_render_test_toggle_at);
  if (at <= 0 || presented < uint64_t(at)) return;
  const int32_t every = REXCVAR_GET(bakugan_render_test_toggle_every);
  if (presented == uint64_t(at) || (every > 0 && (presented - uint64_t(at)) % uint64_t(every) == 0)) {
    REXLOG_INFO("[res] test toggle at presented swap {}", presented);
    ToggleDocked();
  }
#else
  (void)presented;
#endif
}

uint32_t MsaaSamples() {
  if (!g_msaa.active) return 1;
  const int32_t samples = REXCVAR_GET(bakugan_msaa);
  return samples >= 4 ? 4u : samples >= 2 ? 2u : 1u;
}

void NoteRescale() {
  g_render_scale.Use();
  g_rescales_applied.fetch_add(1, std::memory_order_relaxed);
}
void NoteMsaaUse(uint64_t n) { g_msaa.UseSampled(); (void)n; }

void RegisterKeys() {
#if !REX_PLATFORM_SWITCH
  rex::ui::RegisterBind("bind_bakugan_docked", "F9",
                        "Switch between the simulated docked and handheld modes (render resolution)",
                        [] { ToggleDocked(); });
#endif
}

}  // namespace bakugan::render
