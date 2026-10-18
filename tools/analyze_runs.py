"""Per-run summary of numbered test logs (one arena or one configuration per launch).

    python tools/analyze_runs.py <logs folder> <label>[,<label>...] [<first log number>]

The labels name bakugan_<first>.log, bakugan_<first+1>.log, ... in order (default first = 1). Prints, for each log: duration,
battle fps (windows with the GPU >= 40 % busy), the share of windows >= 28 fps, the time at each render height, the
resolution steps, the hitches by type, the ring thread's cost and the GPU's busy time, and the deferred-recording worker's
drain wait (the time the ring thread waits for the worker, per 10 s).
"""
import collections
import re
import statistics as st
import sys

BASE = sys.argv[1] if len(sys.argv) > 1 else 'logs'
LABELS = (sys.argv[2] if len(sys.argv) > 2 else 'water,fire,haos,earth,wind,neutral').split(',')
FIRST = int(sys.argv[3]) if len(sys.argv) > 3 else 1

for k, label in enumerate(LABELS):
    n = FIRST + k
    path = '%s/bakugan_%03d.log' % (BASE, n)
    text = open(path, encoding='utf-8', errors='replace').read().split('\n')
    stamps = [re.match(r'\[[\d-]+ ([\d:]+)', l) for l in text]
    stamps = [s.group(1) for s in stamps if s]
    wins, steps = [], []
    for l in text:
        m = re.search(r'\[res\] dynamic window: GPU busy (\d+) % at ([\d.]+) fps(?:, [\d.]+ % of the frames late)?, render height (\d+)', l)
        if m:
            wins.append((int(m.group(1)), float(m.group(2)), int(m.group(3))))
        m = re.search(r'\[res\] dynamic: GPU (\d+) % busy at ([\d.]+) fps -> render height (\d+) -> (\d+)', l)
        if m:
            steps.append((int(m.group(3)), int(m.group(4))))
    battle = [w for w in wins if w[0] >= 40]
    hitches = []
    for l in text:
        m = re.search(r'\[hitch\] frame of ([\d.]+) ms \(GPU ([\d.]+) real, record ([\d.]+)\): (\d+) copies.* (\d+) GPU waits', l)
        if m:
            hitches.append((float(m.group(1)), float(m.group(2)), float(m.group(3)), int(m.group(5))))
    h100 = [h for h in hitches if h[0] >= 100]
    kinds = collections.Counter()
    for ms, gpu, rec, waits in h100:
        if waits >= 2 and gpu >= 50:
            kinds['rescale/GPU'] += 1
        elif gpu >= 50 or rec >= 30:
            kinds['gpu/record'] += 1
        else:
            kinds['game-thread'] += 1
    ring = []
    for l in text:
        m = re.search(r'ring thread: ([\d.]+) ms of work per Swap \(([\d.]+) us per draw, (\d+) draws', l)
        if m:
            ring.append((float(m.group(1)), float(m.group(2)), int(m.group(3))))
    gpu = []
    for l in text:
        m = re.search(r'GPU per Swap \((\d+) Swaps\): total ([\d.]+) ms;.* gap ([\d.]+) ms', l)
        if m:
            gpu.append((int(m.group(1)), float(m.group(2)), float(m.group(3))))
    drains = []
    for l in text:
        m = re.search(r'deferred recording \(10 s\): (\d+) calls queued.*\((\d+) waiting for the worker, ([\d.]+) ms in total', l)
        if m:
            drains.append((int(m.group(1)), int(m.group(2)), float(m.group(3))))
    print('=' * 100)
    print('%03d %-18s %s -> %s  windows %d (battle %d)' % (n, label, stamps[0] if stamps else '?', stamps[-1] if stamps else '?',
                                                          len(wins), len(battle)))
    if battle:
        fps = [w[1] for w in battle]
        share = collections.Counter(w[2] for w in battle)
        ok = sum(1 for f in fps if f >= 28)
        print('   battle fps: mean %.1f  min %.1f  windows >=28: %d/%d (%.0f%%) | heights: %s' % (
            st.mean(fps), min(fps), ok, len(fps), 100.0 * ok / len(fps),
            ', '.join('%dp %d%%' % (h, 100 * c / len(battle)) for h, c in sorted(share.items()))))
    print('   resolution steps: %d  %s' % (len(steps), ' '.join('%d>%d' % s for s in steps)))
    print('   hitches >=100ms: %d (%s) | >=300 ms: %d, worst %.0f ms' % (
        len(h100), ', '.join('%s %d' % kv for kv in kinds.items()), sum(1 for h in h100 if h[0] >= 300),
        max([h[0] for h in hitches] or [0])))
    heavy = [r for r in ring if r[2] >= 200]
    if heavy:
        print('   ring thread (>=200 draws/Swap): %.1f ms/Swap mean, max %.1f; %.0f us/draw; %.0f draws/Swap' % (
            st.mean(r[0] for r in heavy), max(r[0] for r in heavy), st.mean(r[1] for r in heavy),
            st.mean(r[2] for r in heavy)))
    busy = [(tot - gap, tot) for sw, tot, gap in gpu if tot - gap > 8]
    if busy:
        print('   GPU busy ms/Swap in heavy windows: mean %.1f max %.1f (frame %.1f)' % (
            st.mean(b[0] for b in busy), max(b[0] for b in busy), st.mean(b[1] for b in busy)))
    heavy_d = [d for d in drains if d[0] > 400000]
    if heavy_d:
        print('   deferred worker drain wait (windows with >400k calls): mean %.0f ms/10 s, max %.0f' % (
            st.mean(d[2] for d in heavy_d), max(d[2] for d in heavy_d)))
