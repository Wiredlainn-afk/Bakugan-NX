#!/usr/bin/env python3
"""Debug: makes every guest function record its name (REX_FUNC_PROLOGUE) for -DBAKUGAN_TRACE_FUNCS builds. Run after codegen."""
import os
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'app', 'generated', 'default', 'bakugan_pch.h')
s = open(p, encoding='utf-8').read()
if 'BAKUGAN_TRACE_FUNCS' not in s:
    s += '''
#ifdef BAKUGAN_TRACE_FUNCS
extern thread_local const char* g_bakugan_trace[256];
extern thread_local unsigned g_bakugan_trace_index;
#undef REX_FUNC_PROLOGUE
#define REX_FUNC_PROLOGUE() do { g_bakugan_trace[g_bakugan_trace_index++ & 255] = __func__; } while (0)
#endif
'''
    open(p, 'w', encoding='utf-8').write(s)
print('trace patch ok')
