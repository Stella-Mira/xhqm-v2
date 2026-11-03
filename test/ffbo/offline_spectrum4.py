# -*- coding: utf-8 -*-
# 干净版对齐测量：直接用格林函数（resolvent），避开左右本征向量配对问题。
#
# 为什么重做：上一版用 |⟨u_i,b⟩| 量投影，但 ①前两个模来自孤立 2-环伪影
# ②非对称矩阵的左右本征向量按排序配对在退化本征值上不可靠。
#
# 现在只取主强连通分量（99.17%），解 s = (γI − A)⁻¹ b，扫 γ 逼近 ρ=0.915：
#   若 ‖s‖/‖b‖ 随 γ→ρ 发散 ⇒ 输入确实激励了主导慢模（递归会放大它）
#   若基本不变        ⇒ 输入与慢模近正交（放大不了）⇒ 该改输入编码而不是加增益
import os, csv, sys
import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla
import scipy.sparse.csgraph as csg

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
OUT = r'D:\WorkSpace\_research\digital-fruit-fly\data\malecns'

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
A = sp.csr_matrix((val, col, rp), shape=(nn, nn))

# 只取最大强连通分量
ncomp, lab = csg.connected_components(A, directed=True, connection='strong', return_labels=True)
big = np.argmax(np.bincount(lab))
main = lab == big
idx = np.where(main)[0]
Am = A[idx][:, idx].tocsr()
m = Am.shape[0]
print('主分量: %d 个细胞（%.2f%%）' % (m, 100.0 * m / nn))

# 注入组（光感受器 R1-6），只保留在主分量里的
inj = np.zeros(n)
for line in open(os.path.join(OUT, 'groups.txt'), encoding='utf-8'):
    p = line.split()
    if len(p) >= 2 and p[0] == 'visual_R1_6':
        inj[[int(x) for x in p[1:]]] = 1.0
        break
bm = inj[idx]
print('输入向量: 主分量内 %d 个被注入细胞' % int(bm.sum()))
b = bm / np.linalg.norm(bm)

# 另外造一个【全脑均匀】输入做对照：看是不是视觉这一路特别偏
uni = np.ones(m) / np.sqrt(m)

print()
print('ρ(主回路) = 0.91503 ⇒ 临界 syn_gain* = %.3f' % (1.0 / 0.91503))
print()
print('用 GMRES 迭代解（非对称稀疏矩阵直接 LU 分解填充会爆炸）')
print()
print('%-10s %-16s %-10s %-16s %-10s' % ('γ', '视觉输入 ‖s‖/‖b‖', '迭代', '全脑均匀 ‖s‖/‖b‖', '迭代'))
I = sp.identity(m, format='csr')
def solve(g, rhs):
    M = (g * I - Am).tocsr()
    x, info = spla.gmres(M, rhs, rtol=1e-8, atol=0.0, maxiter=2000, restart=200)
    return x, info
for g in [2.0, 1.5, 1.2, 1.05, 1.00, 0.97, 0.95, 0.93, 0.920, 0.916]:
    try:
        s_vis, i1 = solve(g, b)
        s_uni, i2 = solve(g, uni)
        print('%-10.3f %-16.3f %-10d %-16.3f %-10d' % (
            g, np.linalg.norm(s_vis), i1, np.linalg.norm(s_uni), i2))
    except Exception as e:
        print('%-10.3f 求解失败: %s' % (g, e))
print()
print('判读：γ → ρ⁺(0.915) 时若放大急剧上升 ⇒ 该输入激励了主导慢模；')
print('      若基本平 ⇒ 该输入与慢模近正交，加增益无用，要改输入编码。')
print('      最后一列对照「全脑均匀输入」：若均匀输入放大很多而视觉输入不放大，')
print('      说明慢模存在、但视觉这一路没接上去。')
