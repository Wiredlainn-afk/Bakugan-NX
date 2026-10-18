// Bakugan: native AddRef / Release of the game's reference-counted objects (CPU enhancement
// bakugan_cpu_refcount_native).
//
// WHAT THE GAME DOES
//   sub_8249F830 (AddRef) and sub_8249F858 (Release) update the count at [object + 12] inside the Xbox 360's
//   "interrupts off" bracket: mfmsr / mtmsrd r13,1 / lwarx / addi / stwcx. / mtmsrd. The recompiler turns the bracket
//   into the process-wide critical region (REX_CHECK_GLOBAL_LOCK takes and drops it, REX_ENTER_GLOBAL_LOCK takes it
//   again, plus two counter updates and a release), around a compare-and-swap loop that is atomic by itself.
//   The console profile (2026-10-14, intro movie and park cutscene) put these two functions and the mutex calls under
//   them at about 45 % of the game thread: the movie / cutscene code takes and drops references thousands of times a
//   frame.
//
// WHAT THIS DOES
//   AddRef: a lock-free compare-and-swap loop on the same big-endian word, returning the new count as the original.
//   Release: the same for every count except 1; the count-1 case (the object is destroyed through its virtual
//   function) runs the original code unchanged.
//
// WHY IT IS EXACT: the same memory word goes through the same atomic update and the caller gets the same value; the
//   only thing dropped is the global lock, which guards nothing else here: both functions touch only that word and
//   the update is already a compare-and-swap.
// RISK: other code that changes a reference count NON-atomically inside the same global lock would no longer be
//   excluded; the count words are only ever changed by these two functions in the sampled code.
// KEY: bakugan_cpu_refcount_native (with bakugan_cpu_enhancements).
#include <atomic>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ppc.h>

#include "cpu/bakugan_enhancements.h"

BAKUGAN_ENHANCEMENT(g_refcount, bakugan_cpu_refcount_native, bakugan::enh::kCpu, true,
                "Native lock-free AddRef/Release (sub_8249F830/sub_8249F858) instead of the global-lock bracket");

namespace {

constexpr uint32_t kAddRef = 0x8249F830;
constexpr uint32_t kAddRefBytes = 40;
constexpr uint64_t kAddRefHash = 0x88721CBFD69A569Cull;
constexpr uint32_t kRelease = 0x8249F858;
constexpr uint32_t kReleaseBytes = 120;
constexpr uint64_t kReleaseHash = 0xA15EB0A86381AD4Aull;

std::atomic<int> g_state{0};  // 0 unknown, 1 on, -1 off

inline uint32_t* WordAt(uint8_t* base, uint32_t a) {
#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)
  return reinterpret_cast<uint32_t*>(base + a + (a >= 0xE0000000u ? 0x1000u : 0u));
#else
  return reinterpret_cast<uint32_t*>(base + a);
#endif
}

bool Enabled(uint8_t* base) {
  int state = g_state.load(std::memory_order_relaxed);
  if (state == 0) {
    state = (g_refcount.active &&
             bakugan::enh::GuestCodeMatches(base, kAddRef, kAddRefBytes, kAddRefHash, "bakugan_cpu_refcount_native") &&
             bakugan::enh::GuestCodeMatches(base, kRelease, kReleaseBytes, kReleaseHash, "bakugan_cpu_refcount_native"))
                ? 1
                : -1;
    g_state.store(state, std::memory_order_relaxed);
    REXLOG_INFO("[enh] bakugan_cpu_refcount_native: {}", state > 0 ? "engaged (code check passed)" : "off");
  }
  return state > 0;
}

}  // namespace

REX_EXTERN(__imp__sub_8249F830);
REX_HOOK_RAW(sub_8249F830) {
  if (!Enabled(base)) {
    __imp__sub_8249F830(ctx, base);
    return;
  }
  uint32_t* word = WordAt(base, ctx.r3.u32 + 12);
  uint32_t seen = __atomic_load_n(word, __ATOMIC_RELAXED);
  uint32_t next;
  do {
    next = __builtin_bswap32(__builtin_bswap32(seen) + 1u);
  } while (!__atomic_compare_exchange_n(word, &seen, next, true, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));
  ctx.r3.u64 = uint64_t(__builtin_bswap32(seen)) + 1;
}

REX_EXTERN(__imp__sub_8249F858);
REX_HOOK_RAW(sub_8249F858) {
  if (!Enabled(base)) {
    __imp__sub_8249F858(ctx, base);
    return;
  }
  uint32_t* word = WordAt(base, ctx.r3.u32 + 12);
  uint32_t seen = __atomic_load_n(word, __ATOMIC_RELAXED);
  for (;;) {
    const uint32_t value = __builtin_bswap32(seen);
    if (value == 1) {  // last reference: the object destroys itself through its virtual function (original code)
      __imp__sub_8249F858(ctx, base);
      return;
    }
    const uint32_t next = __builtin_bswap32(value - 1u);
    if (__atomic_compare_exchange_n(word, &seen, next, true, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
      ctx.r3.u64 = uint64_t(value) - 1;
      return;
    }
  }
}
