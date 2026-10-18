// Debug aid (PC, -DBAKUGAN_TRACE_FUNCS): the last 256 guest functions entered by each thread, printed when the thread
// takes an access violation. Used to find which function misbehaves after a codegen option change (2026-10-17).
#ifdef BAKUGAN_TRACE_FUNCS
#include <cstdint>
#include <cstdio>
#include <windows.h>

thread_local const char* g_bakugan_trace[256];
thread_local unsigned g_bakugan_trace_index = 0;

static LONG CALLBACK TraceHandler(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  const ULONG_PTR address = info->ExceptionRecord->ExceptionInformation[1];
  if ((address >> 32) != 2) return EXCEPTION_CONTINUE_SEARCH;  // guest memory is mapped at 0x2_0000_0000
  static volatile LONG once = 0;
  if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
  FILE* f = std::fopen("bakugan_trace.txt", "w");
  if (!f) return EXCEPTION_CONTINUE_SEARCH;
  std::fprintf(f, "access violation at guest %llx, last functions entered (oldest first):\n", (unsigned long long)(address & 0xFFFFFFFF));
  for (unsigned i = 0; i < 256; ++i) {
    const char* name = g_bakugan_trace[(g_bakugan_trace_index + i) & 255];
    if (name) std::fprintf(f, "  %s\n", name);
  }
  std::fclose(f);
  return EXCEPTION_CONTINUE_SEARCH;
}

static const bool g_installed = [] { AddVectoredExceptionHandler(1, TraceHandler); return true; }();
#endif
