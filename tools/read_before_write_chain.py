#!/usr/bin/env python3
"""read_before_write.py plus the registers that merely PASS THROUGH a function.

With non_volatile_as_local a function that calls a "register-passing" callee H (one that reads r14-r31/f14-f31/v14-v31 its caller
set, see read_before_write.py) must itself hold those registers in ctx, and so must every function between the one that defines
the value and H: a pass-through frame never touches the register, so the plain audit does not list it, but its own caller's
local copy never reaches ctx and H reads a stale value (console 2026-10-17: every intro movie broken, pink/green lines: the
register-passing helper sub_8255D790 reads v16-v31 defined two frames up).

Run on code generated WITHOUT share_registers marks (like read_before_write.py). Fixpoint over the direct call graph: at a call
site to G, G's live-in registers count as READ by the caller at that point; whatever is not written on every path to the call
is live-in for the caller too.

  python tools/read_before_write_chain.py app/generated/default [--toml]
"""
import glob
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('rbw', os.path.join(HERE, 'read_before_write.py'))
rbw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rbw)


def analyze_with_calls(cfg, noreturn, summary):
    lines, succ0, calls = cfg
    n = len(lines)
    succ = [([] if (i in calls and calls[i] in noreturn) else succ0[i]) for i in range(n)]
    ud = []
    for i, s in enumerate(lines):
        u, d = rbw.uses_defs(s)
        callee = calls.get(i)
        if callee and callee in summary:
            for nm in summary[callee]:
                u |= 1 << rbw.idx[nm]
        ud.append((u, d))
    preds = [[] for _ in range(n)]
    for i in range(n):
        for j in succ[i]:
            preds[j].append(i)
    ALL = rbw.ALL
    IN = [ALL] * n
    if n:
        IN[0] = 0
    OUT = [ALL] * n
    work = list(range(n))
    inwork = [True] * n
    while work:
        i = work.pop()
        inwork[i] = False
        new_in = 0 if i == 0 else ALL
        if i == 0:
            for p in preds[i]:
                new_in &= OUT[p]
        else:
            for p in preds[i]:
                new_in &= OUT[p]
            if not preds[i]:
                new_in = ALL
        IN[i] = new_in
        o = new_in | ud[i][1]
        if o != OUT[i]:
            OUT[i] = o
            for j in succ[i]:
                if not inwork[j]:
                    inwork[j] = True
                    work.append(j)
    bad = {}
    for i in range(n):
        u = ud[i][0] & ~IN[i]
        if not u:
            continue
        ln = lines[i]
        head = ln.split(',', 1)[0]
        is_store = ln.startswith('REX_STORE_') or (
            ln.startswith('simde_mm_store') and ('REX_RAW_ADDR(' in head or 'base +' in head))
        if is_store:
            continue
        for k, nm in enumerate(rbw.names):
            if u >> k & 1 and nm not in bad:
                bad[nm] = lines[i][:100]
    return bad


def main():
    gen = sys.argv[1]
    want_toml = '--toml' in sys.argv
    cfgs = {}
    for f in sorted(glob.glob(gen + '/*_recomp.*.cpp')):
        cur = None
        body = []
        for ln in open(f, encoding='utf-8', errors='ignore'):
            m = rbw.fn_re.match(ln)
            if m:
                cur = m.group(1) or m.group(2)
                body = []
                continue
            if cur is None:
                continue
            if ln.startswith('}'):
                cfgs[cur] = rbw.build_cfg(body)
                cur = None
                continue
            body.append(ln)
    noreturn = set()
    changed = True
    while changed:
        changed = False
        for nm, cfg in cfgs.items():
            if nm not in noreturn and not rbw.returns(cfg, noreturn):
                noreturn.add(nm)
                changed = True
    # the false alarms of read_before_write.py (xor/subfe idioms and the guest setjmp) never propagate
    ignore = {'sub_823BD500', 'sub_823BD890', 'sub_828C7270', 'sub_828B3870'}
    summary = {}
    rounds = 0
    while True:
        rounds += 1
        grew = False
        for nm, cfg in cfgs.items():
            if nm in ignore:
                continue
            bad = analyze_with_calls(cfg, noreturn, summary)
            old = summary.get(nm, {})
            if set(bad) != set(old):
                summary[nm] = bad
                grew = True
        print('# round', rounds, len(summary), 'functions with live-in registers', file=sys.stderr)
        if not grew or rounds > 30:
            break
    for nm in sorted(summary):
        regs = sorted(summary[nm])
        if not regs:
            continue
        if want_toml:
            print(f'"0x{nm[4:]}" = {{ share_registers = true, keep_saves = true }}  # {" ".join(regs)}')
        else:
            print(nm, ' '.join(regs))


if __name__ == '__main__':
    main()
