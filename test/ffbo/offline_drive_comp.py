# -*- coding: utf-8 -*-
# 测量视觉链每一级的【驱动构成】：一个下游群体的输入里，来自上一级视觉通路的占多少，
# 来自自身/其它中央脑递归的占多少。
#
# 为什么必须测这个
#   上一轮实测：给 ol_intrinsic→visual_projection 加 5 倍通路增益，信号成功跨过
#   （visual_projection 从 6/0.0 变成 162/70.1）；但再给 visual_projection→cb_intrinsic
#   加 5 倍增益，cb_intrinsic 依旧 14~41 个显著、群体 d′ 恒为 0.0（低于假阳性水平）。
#   输入调制放大 5 倍而输出完全不变，只可能是：该级驱动里视觉那一份本来就极小，
#   放大 5 倍后仍然是零头；或者该级被自身递归动态主导。
#   这个数决定了「加结构（子单元/归一化）」有没有靶子 —— 没靶子就不该动手。
import os, csv, sys, collections
import numpy as np

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

OUT = r'D:\WorkSpace\_research\digital-fruit-fly\data\malecns'

rows = list(csv.DictReader(open(os.path.join(OUT, 'neurons.csv'), encoding='utf-8')))
n = len(rows)
sc = np.empty(n, dtype=object)
ty = np.empty(n, dtype=object)
for r in rows:
    i = int(r['idx']); sc[i] = r['superclass'] or ''; ty[i] = r['type'] or ''

with open(os.path.join(OUT, 'csr.bin'), 'rb') as f:
    f.read(4); np.frombuffer(f.read(8), dtype=np.uint32)
    nn, nnz = np.frombuffer(f.read(16), dtype=np.uint64); nn, nnz = int(nn), int(nnz)
    rp = np.fromfile(f, dtype=np.uint64, count=nn + 1)
    col = np.fromfile(f, dtype=np.uint32, count=nnz)
    val = np.fromfile(f, dtype=np.float32, count=nnz)

post = np.repeat(np.arange(nn, dtype=np.int64), np.diff(rp).astype(np.int64))
pre_sc = sc[col]
wabs = np.abs(val)
print('csr: n=%d nnz=%d' % (nn, nnz))
print()

# 视觉链的顺序（按信号传递方向）
CHAIN = [
    ('visual_projection',  'ol_intrinsic'),
    ('cb_intrinsic',       'visual_projection'),
    ('descending_neuron',  'cb_intrinsic'),
    ('vnc_motor',          'descending_neuron'),
]
print('%-20s %10s %12s %14s %12s' % ('目标群体', '入边数', '|权重|合计', '来自上一级占比', '自身递归占比'))
for target, upstream in CHAIN:
    m = (post < nn) & (sc[post] == target)
    if not m.any():
        print('%-20s %10s' % (target, '无')) ; continue
    tot = wabs[m].sum()
    up = wabs[m & (pre_sc == upstream)].sum()
    self_ = wabs[m & (pre_sc == target)].sum()
    print('%-20s %10d %12.0f %13.2f%% %11.2f%%' % (
        target, int(m.sum()), tot, 100.0 * up / max(tot, 1e-9), 100.0 * self_ / max(tot, 1e-9)))

print()
print('── 各目标群体驱动构成的 top 5 来源（按 |权重|）──')
for target, upstream in CHAIN:
    m = (post < nn) & (sc[post] == target)
    if not m.any():
        continue
    agg = collections.Counter()
    for s2, w2 in zip(pre_sc[m], wabs[m]):
        agg[s2] += w2
    tot = sum(agg.values())
    print('%s（|权重|合计 %.0f）:' % (target, tot))
    for s2, w2 in agg.most_common(5):
        print('    %-24s %10.0f  %6.2f%%' % (str(s2), w2, 100.0 * w2 / max(tot, 1e-9)))
