// bakugan-nx - flight recorder (see bakugan_flight.h)
#include "cpu/bakugan_flight.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/platform.h>

#if REX_PLATFORM_SWITCH
#include <pthread.h>
#include <switch.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace rex::system {
int BakuganLiveXFiles();  // SDK xfile.cpp
}

namespace rex::kernel::xboxkrnl {
std::string BakuganLastOpenedPath();  // SDK xboxkrnl_io.cpp
std::string BakuganReadsInFlight();   // SDK xboxkrnl_io.cpp
}

REXCVAR_DECLARE(bool, log_single_file);  // SDK logging.cpp
REXCVAR_DEFINE_BOOL(bakugan_flight_recorder, true, "BAKUGAN enhancements",
                    "Write logs/flight.txt, one synced line per second (frames, GPU wait site, last file opened, memory)");

namespace bakugan::flight {
namespace {

std::atomic<uint64_t> g_presented{0};
std::atomic<const char*> g_wait_site{nullptr};
std::atomic<int64_t> g_wait_since_ms{0};
std::once_flag g_started;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void SyncFile(FILE* f) {
  std::fflush(f);
#if defined(_WIN32)
  _commit(_fileno(f));
#else
  fsync(fileno(f));
#endif
}

void Run() {
  // One run per file: the previous run's file is kept once (flight.prev.txt, for a hang or crash investigation) and
  // the one before it goes, so the file does not grow with use.
  // With log_single_file off (the closed beta, numbered logs) the file keeps growing across runs as before.
  const bool one_run = REXCVAR_GET(log_single_file);
  if (one_run) {
    std::remove("logs/flight.prev.txt");
    std::rename("logs/flight.txt", "logs/flight.prev.txt");
  }
  std::FILE* f = std::fopen("logs/flight.txt", one_run ? "w" : "a");
  if (!f) {
    if (one_run) {
      std::remove("flight.prev.txt");
      std::rename("flight.txt", "flight.prev.txt");
    }
    f = std::fopen("flight.txt", one_run ? "w" : "a");
  }
  if (!f) return;
  std::fprintf(f, "---- start (one line per second: presented frames, wait site, last file, memory) ----\n");
  SyncFile(f);
  const int64_t start = NowMs();
  uint64_t last_presented = 0;
  int still = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const int64_t now = NowMs();
    const uint64_t presented = g_presented.load(std::memory_order_relaxed);
    const uint64_t delta = presented - last_presented;
    last_presented = presented;
    still = delta == 0 ? still + 1 : 0;
    const char* site = g_wait_site.load(std::memory_order_relaxed);
    const int64_t since = g_wait_since_ms.load(std::memory_order_relaxed);
    std::string memory;
#if REX_PLATFORM_SWITCH
    {
      u64 used = 0, total = 0, app_used = 0, app_total = 0, sys_used = 0, sys_total = 0;
      svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
      svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
      svcGetSystemInfo(&app_total, SystemInfoType_TotalPhysicalMemorySize, INVALID_HANDLE, PhysicalMemorySystemInfo_Application);
      svcGetSystemInfo(&app_used, SystemInfoType_UsedPhysicalMemorySize, INVALID_HANDLE, PhysicalMemorySystemInfo_Application);
      svcGetSystemInfo(&sys_total, SystemInfoType_TotalPhysicalMemorySize, INVALID_HANDLE, PhysicalMemorySystemInfo_System);
      svcGetSystemInfo(&sys_used, SystemInfoType_UsedPhysicalMemorySize, INVALID_HANDLE, PhysicalMemorySystemInfo_System);
      // Kernel resources of the process (sessions, threads, events, transfer memories): current / limit.
      u64 rl64 = 0;
      s64 cur[5] = {0, 0, 0, 0, 0}, lim[5] = {0, 0, 0, 0, 0};
      if (R_SUCCEEDED(svcGetInfo(&rl64, InfoType_ResourceLimit, CUR_PROCESS_HANDLE, 0))) {
        const Handle rl = Handle(rl64);
        const LimitableResource kinds[5] = {LimitableResource_Memory, LimitableResource_Threads,
                                            LimitableResource_Events, LimitableResource_TransferMemories,
                                            LimitableResource_Sessions};
        for (int k = 0; k < 5; ++k) {
          svcGetResourceLimitCurrentValue(&cur[k], rl, kinds[k]);
          svcGetResourceLimitLimitValue(&lim[k], rl, kinds[k]);
        }
      }
      char buffer[320];
      std::snprintf(buffer, sizeof(buffer),
                    "proc %llu/%llu MB | app pool %llu/%llu MB | system pool %llu/%llu MB | threads %llu/%llu "
                    "events %llu/%llu transfer %llu/%llu sessions %llu/%llu | live files %d",
                    (unsigned long long)(used >> 20), (unsigned long long)(total >> 20),
                    (unsigned long long)(app_used >> 20), (unsigned long long)(app_total >> 20),
                    (unsigned long long)(sys_used >> 20), (unsigned long long)(sys_total >> 20),
                    (unsigned long long)cur[1], (unsigned long long)lim[1], (unsigned long long)cur[2],
                    (unsigned long long)lim[2], (unsigned long long)cur[3], (unsigned long long)lim[3],
                    (unsigned long long)cur[4], (unsigned long long)lim[4], rex::system::BakuganLiveXFiles());
      memory = buffer;
    }
#else
    memory = "-";
#endif
    std::string wait = "-";
    if (site) {
      char buffer[96];
      std::snprintf(buffer, sizeof(buffer), "%s for %.1f s", site, double(now - since) / 1000.0);
      wait = buffer;
    }
    const std::string file = rex::kernel::xboxkrnl::BakuganLastOpenedPath();
    const std::string reads = rex::kernel::xboxkrnl::BakuganReadsInFlight();
    std::fprintf(f, "%s t=%.0f presented +%llu (total %llu) | in GPU wait: %s | file reads: %s | last open: %s | %s\n",
                 still >= 3 ? "STALL" : "ok   ", double(now - start) / 1000.0, (unsigned long long)delta,
                 (unsigned long long)presented, wait.c_str(), reads.c_str(), file.c_str(), memory.c_str());
    SyncFile(f);
  }
}

void StartOnce() {
  std::call_once(g_started, [] {
    if (!REXCVAR_GET(bakugan_flight_recorder)) return;
#if REX_PLATFORM_SWITCH
    // A small stack, and a failure is not an error (std::thread threw on the console and aborted the app).
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    pthread_create(
        &thread, &attr, [](void*) -> void* { Run(); return nullptr; }, nullptr);
    pthread_attr_destroy(&attr);
#else
    try {
      std::thread(Run).detach();
    } catch (...) {
    }
#endif
  });
}

}  // namespace

void NotePresented() {
  StartOnce();
  g_presented.fetch_add(1, std::memory_order_relaxed);
}

WaitScope::WaitScope(const char* site) {
  StartOnce();
  previous_ = g_wait_site.load(std::memory_order_relaxed);
  previous_since_ = g_wait_since_ms.load(std::memory_order_relaxed);
  g_wait_since_ms.store(NowMs(), std::memory_order_relaxed);
  g_wait_site.store(site, std::memory_order_relaxed);
}

WaitScope::~WaitScope() {
  g_wait_site.store(previous_, std::memory_order_relaxed);
  g_wait_since_ms.store(previous_since_, std::memory_order_relaxed);
}

}  // namespace bakugan::flight
