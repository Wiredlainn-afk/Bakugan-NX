# GPU time per category by render height, per arena of a standard test:
#   python tools/analyze_categories.py <logs folder> water,fire,haos,earth,wind,neutral
import re,sys,statistics as st,collections
BASE=sys.argv[1] if len(sys.argv)>1 else 'logs'
ARENAS=(sys.argv[2] if len(sys.argv)>2 else 'water,fire,haos,earth,wind,neutral').split(',')
def t2s(h):
    a=h.split(':'); return int(a[0])*3600+int(a[1])*60+float(a[2])
for n,name in enumerate(ARENAS,1):
    L=open(f'{BASE}/bakugan_{n:03d}.log',encoding='utf-8',errors='replace').read().split('\n')
    wins=[]
    for l in L:
        m=re.match(r'\[[\d-]+ ([\d:.]+)\].*\[res\] dynamic window: GPU busy (\d+) % at ([\d.]+) fps(?:, [\d.]+ % of the frames late)?, render height (\d+)',l)
        if m: wins.append((t2s(m.group(1)),int(m.group(4))))
    rows=collections.defaultdict(list)
    for l in L:
        m=re.match(r'\[[\d-]+ ([\d:.]+)\].*GPU per Swap \((\d+) Swaps\): total ([\d.]+) ms; (.*)',l)
        if not m: continue
        t=t2s(m.group(1)); sw=int(m.group(2)); tot=float(m.group(3)); rest=re.sub(r' \(0.00 Mfrag\)','',m.group(4))
        d={k:float(v) for k,v in re.findall(r'(\w+) ([\d.]+) ms',rest)}
        busy=tot-d.get('gap',0)
        if busy<10: continue
        h=min(wins,key=lambda w:abs(w[0]-t))[1] if wins else 0
        fps=sw/10.0
        rows[h].append((fps,busy,d.get('640',0),d.get('320',0),d.get('copies',0),d.get('scene_no_z',0),d.get('edram_alias',0),d.get('edram_import',0),d.get('edram_export',0),d.get('edram_import9',0),d.get('other',0)+d.get('shadows',0)+d.get('scene',0)))
    print('==',name)
    for h in sorted(rows):
        r=rows[h]; m=lambda i: st.mean(x[i] for x in r)
        print('  %dp n=%2d fps %.1f  GPU busy %.1f ms: 640 %.1f | 320 %.1f | copies %.1f | noZ %.1f | alias %.1f | imp %.1f exp %.1f imp9 %.1f | other %.1f'%(h,len(r),m(0),m(1),m(2),m(3),m(4),m(5),m(6),m(7),m(8),m(9),m(10)))
