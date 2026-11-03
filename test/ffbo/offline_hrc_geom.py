# -*- coding: utf-8 -*-
# 测量 T4/T5 两条腿（快腿 / 慢腿）之间的【实际空间偏移】Δx，用来标定 HRC 的匹配漂移速度。
#
# 为什么需要它
#   HRC 的原理是「腿 A 在位置 x（快）+ 腿 B 在位置 x+Δx（延迟 Δt）」，且必须 Δt = Δx/v。
#   实测方向选择性一直不出现（xhqm r510），一个可能是两条腿的空间相位对不上。
#   与其盲扫漂移速度（单次 dt=1ms / 20s 要 420 秒，8 个速度就是一小时），
#   不如先离线把 Δx 量出来，再定点验证 —— 这一步不需要跑仿真。
#
# 方法
#   1) 光感受器的实测方位角：来自光学柱 h1/h2（visual_columns.txt），
#      映射与 mcns_net.cpp 完全一致：u=(h1-2)/34，az_deg = 左 ? -135+u*143.5 : 8.5-u*143.5
#   2) 把方位角沿连接组向外做加权平均传播 4 跳（权重 = |归一化突触权重|），
#      得到每个中间神经元（L1-L5 / Mi / Tm ...）的有效方位角。
#   3) 对每个 T4/T5，分别求它「快腿入边」与「慢腿入边」的加权平均方位角，取差。
#
# 内部一致性检查
#   T4a 是前后向、T4d 是后前向 —— 若连接组的视网膜拓扑是真实的，两者的偏移符号应当相反。
import os, csv, sys, math, collections
import numpy as np

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

OUT = r'D:\WorkSpace\_research\digital-fruit-fly\data\malecns'
DT_FAST = 0.005     # cellsim 快兴奋通道 tau
DT_SLOW = 0.050     # cellsim NMDA 通道 tau（--hrc-wire 用的那条）
DELAY = DT_SLOW - DT_FAST

# ── 加载 ──
rows = list(csv.DictReader(open(os.path.join(OUT, 'neurons.csv'), encoding='utf-8')))
n = len(rows)
ty = np.empty(n, dtype=object)
for r in rows:
    ty[int(r['idx'])] = r['type'] or ''

with open(os.path.join(OUT, 'csr.bin'), 'rb') as f:
    f.read(4); np.frombuffer(f.read(8), dtype=np.uint32)
    nn, nnz = np.frombuffer(f.read(16), dtype=np.uint64); nn, nnz = int(nn), int(nnz)
    rp = np.fromfile(f, dtype=np.uint64, count=nn + 1)
    col = np.fromfile(f, dtype=np.uint32, count=nnz)
    val = np.fromfile(f, dtype=np.float32, count=nnz)
print('csr: n=%d nnz=%d' % (nn, nnz))

# ── 光感受器方位角 ──
side = {}; h1 = {}; h2o = {}
with open(os.path.join(OUT, 'visual_columns.txt'), encoding='utf-8') as f:
    for line in f:
        if line.startswith('#'):
            continue
        p = line.split()
        if len(p) < 5:
            continue
        i = int(p[0]); side[i] = p[1]; h1[i] = int(p[2]); h2o[i] = int(p[3])

az = np.full(n, np.nan)
nseed = 0
for i in range(n):
    if i in h1 and (i in h2o):
        u = (h1[i] - 2) / 34.0
        d = (-135.0 + u * 143.5) if side.get(i) == 'L' else (8.5 - u * 143.5)
        az[i] = d
        nseed += 1
print('方位角种子（有光学柱的细胞）：%d 个' % nseed)

# ── 沿连接组传播（加权平均，4 跳）──
post = np.repeat(np.arange(nn, dtype=np.int64), np.diff(rp).astype(np.int64))
w = np.abs(val)
for it in range(4):
    known = ~np.isnan(az[col])
    pre_i = col[known].astype(np.int64)
    ww = w[known]
    po = post[known]
    wsum = np.bincount(po, weights=ww, minlength=nn)
    vsum = np.bincount(po, weights=ww * az[pre_i], minlength=nn)
    add = np.where(wsum > 1e-9, vsum / np.maximum(wsum, 1e-12), np.nan)
    newly = np.isnan(az) & ~np.isnan(add)
    az[newly] = add[newly]
    print('  第 %d 跳后已知方位角的细胞：%d' % (it + 1, int(np.sum(~np.isnan(az)))))

# ── 每个 T4/T5 的两条腿 ──
FAST = {'T4': ('Mi1',), 'T5': ('Tm1', 'Tm2')}
SLOW = ('Tm3', 'Tm9', 'Tm2')
TYPES = ['T4a', 'T4b', 'T4c', 'T4d', 'T5a', 'T5b', 'T5c', 'T5d']

# 为每条边标注：突触前类型属于快腿 / 慢腿 / 都不是（只对 T4/T5 的入边关心）
# ★ 必须覆盖全部 nnz 条边。第一版把循环上界写成 nn（细胞数），只有前 166700 条边有类型，
#   其余全是 None ⇒ 所有类型都报「无法测」。用 fancy index 一次向量化拿全。
pre_ty = ty[col]

print()
print('%-6s %7s %9s %10s %10s %10s %12s' % ('类型', '细胞数', '有效腿', 'Δaz中位(°)', '均值(°)', '标准差(°)', '匹配速度(°/s)'))

# 诊断：T4a / T5a 的突触前类型构成，以及其中有多少条边有已知方位角。
# 「无法测」时必须先看这个 —— 是类型名对不上，还是方位角没传播到那类细胞。
for dt in ('T4a', 'T5a'):
    ids = [i for i in range(nn) if ty[i] == dt]
    cnt = collections.Counter(); known = collections.Counter()
    for i in ids[:200]:
        for k in range(int(rp[i]), int(rp[i + 1])):
            t2 = pre_ty[k]
            cnt[t2] += 1
            if not np.isnan(az[col[k]]):
                known[t2] += 1
    print('\n[诊断] %s 前 200 个细胞的突触前类型（边数 / 其中方位角已知）:' % dt)
    for t2, c in cnt.most_common(10):
        print('    %-10s %8d / %8d' % (str(t2), c, known[t2]))

summary = {}
for t in TYPES:
    fam = t[:2]
    ids = [i for i in range(nn) if ty[i] == t]
    if not ids:
        continue
    diffs = []
    nvalid = 0
    for i in ids:
        a, b = rp[i], rp[i + 1]
        if b <= a:
            continue
        seg_pre = pre_ty[a:b]
        seg_az = az[col[a:b]]
        seg_w = w[a:b]
        mf = np.isin(seg_pre, FAST[fam]) & ~np.isnan(seg_az)
        ms = np.isin(seg_pre, SLOW) & ~np.isnan(seg_az)
        if not mf.any() or not ms.any():
            continue
        af = np.sum(seg_w[mf] * seg_az[mf]) / max(np.sum(seg_w[mf]), 1e-9)
        as_ = np.sum(seg_w[ms] * seg_az[ms]) / max(np.sum(seg_w[ms]), 1e-9)
        diffs.append(as_ - af)
        nvalid += 1
    if not diffs:
        print('%-6s %7d %9s' % (t, len(ids), '无法测'))
        continue
    dd = np.array(diffs)
    med = float(np.median(dd)); mu = float(np.mean(dd)); sd = float(np.std(dd))
    vmatch = abs(math.radians(med)) / DELAY if med == med else float('nan')
    summary[t] = (med, mu, sd, vmatch, nvalid)
    print('%-6s %7d %9d %10.3f %10.3f %10.3f %12.2f' % (
        t, len(ids), nvalid, med, mu, sd, math.degrees(vmatch)))

print()
print('说明：匹配速度 = |Δaz| / (τ_slow − τ_fast) = |Δaz| / %.0fms —— 这是让 HRC 的两条腿对齐' % (DELAY * 1000))
print('      所要求的条纹漂移速度 v = stripe_period × stripe_hz。当前默认 0.8×4 = 3.2 rad/s = 183°/s。')
print('      一致性检查：T4a 与 T4d（前后向 vs 后前向）的 Δaz 符号应当相反。')

# ── 方法自检：传播之后方位角还保留空间结构吗？──
# 如果每个类型的 az 都退化成同一个值（std≈0），那上面测出的「偏移≈0」是我的方法坏了，
# 而不是连接组没有偏移。必须先把这条排除掉，否则「没有偏移」这个结论不成立。
print()
print('[方法自检] 各类型传播后的方位角分布（单位：度）:')
print('%-8s %8s %9s %9s %9s %9s' % ('类型', '细胞数', '均值', '标准差', '最小', '最大'))
for t in ['L1', 'L2', 'L3', 'Mi1', 'Mi4', 'Mi9', 'Tm1', 'Tm2', 'Tm3', 'Tm9', 'T4a', 'T4d', 'T5a', 'T5c']:
    ids = [i for i in range(nn) if ty[i] == t]
    if not ids:
        continue
    a = az[ids]
    a = a[~np.isnan(a)]
    if a.size == 0:
        print('%-8s %8d %9s' % (t, len(ids), '无'))
        continue
    print('%-8s %8d %9.2f %9.2f %9.2f %9.2f' % (t, len(ids), a.mean(), a.std(), a.min(), a.max()))

# ── 导出给 C++ 用 ──
# mcns_net 的 --hrc-shift 需要每个细胞的方位角来把 T4/T5 慢腿的突触前细胞换成
# 「方位角偏移 Δc 的同类细胞」，从而显式构造 HRC 缺失的空间基线。
az_out = az.astype(np.float32)
az_out.tofile(os.path.join(OUT, 'cell_az.bin'))
print()
print('写出 cell_az.bin：%d 个 float32（NaN = 方位角未知），供 mcns_net --hrc-shift 使用' % n)

