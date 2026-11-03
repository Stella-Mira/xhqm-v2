# -*- coding: utf-8 -*-
# 递归矩阵的谱分析。
#
# 为什么要做（零成本、且决定后面往哪走）：
#   实测 cb_intrinsic 有 85.34% 的驱动来自自身递归，而视觉只占 5.95%；给视觉通路加 5 倍增益
#   只能跨过一级突触然后停住。线性情形下答案取决于 s=(I−W)⁻¹x —— 放大不取决于 W 的整体大小，
#   而取决于输入 x 在 W 的哪几个本征模上有投影，以及这些模的本征值离 1 有多近。
#
# 本脚本回答三件事：
#   1) 谱半径 ρ(W) 是多少？在什么增益下越过 1（= 网络进入自激活的临界点）？
#   2) 有多少个 |λ| 接近 1？（少数 ⇒ 可选择性自持；大量 ⇒ 饱和。这直接对应「吸引子景观 vs
#      单个饱和不动点」的判据）
#   3) 视觉输入模式在近 1 本征模上的投影有多大？（决定 6% 能不能撬动 85%）
#
# 注意：这里算的是【解剖矩阵】的谱，不是工作点的有效矩阵 J = D·W（D 是各神经元的局部增益）。
# 对脉冲阈值单元，只有处于线性区的单元 D 才非零。所以本谱是「结构倾向」的上界，
# 不是工作谱。第一遍先看结构倾向。
import os, csv, sys, collections
import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

OUT = r'D:\WorkSpace\_research\digital-fruit-fly\data\malecns'

# ── 加载 ──
rows = list(csv.DictReader(open(os.path.join(OUT, 'neurons.csv'), encoding='utf-8')))
n = len(rows)
sc = np.empty(n, dtype=object)
for r in rows:
    sc[int(r['idx'])] = r['superclass'] or ''

with open(os.path.join(OUT, 'csr.bin'), 'rb') as f:
    f.read(4); np.frombuffer(f.read(8), dtype=np.uint32)
    nn, nnz = np.frombuffer(f.read(16), dtype=np.uint64); nn, nnz = int(nn), int(nnz)
    rp = np.fromfile(f, dtype=np.uint64, count=nn + 1)
    col = np.fromfile(f, dtype=np.uint32, count=nnz)
    val = np.fromfile(f, dtype=np.float32, count=nnz)

A = sp.csr_matrix((val.astype(np.float64), col.astype(np.int32), rp.astype(np.int32)),
                  shape=(nn, nn))
print('矩阵: %d×%d, 非零 %d, 稠密度 %.4f%%' % (nn, nn, nnz, 100.0 * nnz / (nn * nn)))

# 行和统计：程序里每行 |Σ| = syn_gain（存盘版本是每行 |Σ| = 1）
rs_sum = np.asarray(A.sum(axis=1)).ravel()          # 带符号行和
rs_abs = np.asarray(np.abs(A).sum(axis=1)).ravel()  # 绝对值行和
print('存盘行和: |Σ| 中位 %.4f（应≈1）| 带符号 Σ 中位 %+.4f（偏离 0 说明兴奋/抑制不平衡）'
      % (np.median(rs_abs), np.median(rs_sum)))
print('  带符号行和分布：p05 %+.3f  p50 %+.3f  p95 %+.3f' %
      (np.percentile(rs_sum, 5), np.percentile(rs_sum, 50), np.percentile(rs_sum, 95)))

# ── 谱：先看存盘矩阵（行 |Σ|=1），再按 syn_gain 缩放 ──
print()
print('=== 求前 20 个最大模本征值（非对称 ⇒ 复数）===')
t0 = __import__('time').time()
ev = spla.eigs(A, k=20, which='LM', ncv=60, maxiter=3000, tol=1e-4, return_eigenvectors=False)
ev = ev[np.argsort(-np.abs(ev))]
print('耗时 %.1fs' % (__import__('time').time() - t0))
print('%-4s %-22s %-10s %-10s' % ('#', 'λ（存盘尺度）', '|λ|', '相位(°)'))
for i, l in enumerate(ev):
    print('%-4d %-22s %-10.5f %-10.1f' % (i + 1, '%+.5f%+.5fj' % (l.real, l.imag),
                                          abs(l), np.degrees(np.angle(l))))
rho = abs(ev[0])
print()
print('谱半径 ρ(A_存盘) = %.5f' % rho)
print('⇒ 程序里 W = syn_gain × A，所以 ρ(W) = syn_gain × %.5f' % rho)
print('   临界增益 syn_gain* = 1/ρ = %.2f（超过它线性区就不稳定）' % (1.0 / rho))
print()
print('各 syn_gain 下 ρ(W) 与 |λ|≥1 的模数：')
print('%-10s %-12s %-12s %s' % ('syn_gain', 'ρ(W)', '|λ|≥1 的模数', '判定'))
for g in [2, 4, 8, 15, 30, 60, 120]:
    rr = g * rho
    ncrit = int(np.sum(np.abs(ev) * g >= 1.0))
    verdict = '稳定' if rr < 1 else ('临界附近' if rr < 2 else '强不稳定/饱和')
    print('%-10d %-12.3f %-12d %s' % (g, rr, ncrit, verdict))
print()
print('注：只有前 20 个模，所以「|λ|≥1 的模数」是下界。')
