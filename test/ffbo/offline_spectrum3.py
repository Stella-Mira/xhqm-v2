# -*- coding: utf-8 -*-
# 排除异常：ρ=1 到底来自主回路，还是来自一个【解耦子块】？
# 前 2 个模能量 100% 落在「其它」超类、最大分量是同一个细胞 #41064 ENS —— 疑似图可约（reducible）。
# 若如此，则「ρ=1 由归一化强制」这个结论要收窄为「某个孤立分量贡献了 λ=1」。
import os, csv, sys, collections
import numpy as np
import scipy.sparse as sp
import scipy.sparse.csgraph as csg

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
OUT = r'D:\WorkSpace\_research\digital-fruit-fly\data\malecns'

rows = list(csv.DictReader(open(os.path.join(OUT, 'neurons.csv'), encoding='utf-8')))
n = len(rows)
sc = np.empty(n, dtype=object); ty = np.empty(n, dtype=object)
for r in rows:
    i = int(r['idx']); sc[i] = r['superclass'] or ''; ty[i] = r['type'] or ''

with open(os.path.join(OUT, 'csr.bin'), 'rb') as f:
    f.read(4); np.frombuffer(f.read(8), dtype=np.uint32)
    nn, nnz = np.frombuffer(f.read(16), dtype=np.uint64); nn, nnz = int(nn), int(nnz)
    rp = np.fromfile(f, dtype=np.uint64, count=nn + 1)
    col = np.fromfile(f, dtype=np.uint32, count=nnz)
    val = np.fromfile(f, dtype=np.float32, count=nnz)

A = sp.csr_matrix((val, col, rp), shape=(nn, nn))

# ── 1) 入度/出度为零的细胞（孤立点）──
indeg = np.diff(rp)
outdeg = np.bincount(col, minlength=nn)
print('入度为 0 的细胞: %d' % int((indeg == 0).sum()))
print('出度为 0 的细胞: %d' % int((outdeg == 0).sum()))
print('入度或出度为 0: %d' % int(((indeg == 0) | (outdeg == 0)).sum()))
for i in [41064]:
    print('  #%d: %s %s 入度 %d 出度 %d' % (i, sc[i], ty[i], indeg[i], outdeg[i]))

# ── 2) 强连通分量（SCC）──
print()
print('计算强连通分量 …')
ncomp, labels = csg.connected_components(A, directed=True, connection='strong', return_labels=True)
sizes = collections.Counter(labels.tolist())
print('SCC 数: %d' % ncomp)
print('最大的 8 个分量大小:', [s for _, s in sizes.most_common(8)])
big = sizes.most_common(1)[0]
print('最大分量: 大小 %d (%.2f%%), 标签 %d' % (big[1], 100.0 * big[1] / nn, big[0]))
# 最大分量里 #41064 在哪个分量
print('#41064 所在分量标签 %d，大小 %d' % (labels[41064], sizes[labels[41064]]))

# ── 3) 最大分量覆盖了哪些超类 ──
main = labels == big[0]
cnt = collections.Counter(sc[i] for i in np.where(main)[0])
tot = collections.Counter(sc.tolist())
print()
print('最大分量内的超类构成（占该超类比例）:')
for s, c in cnt.most_common(8):
    print('    %-22s %7d / %7d = %.1f%%' % (s, c, tot[s], 100.0 * c / tot[s]))

# ── 4) 只保留最大分量，重算谱半径 ──
print()
print('只在最大分量上重算 ρ（这是真正的主回路）…')
import scipy.sparse.linalg as spla
sub = A[main][:, main]
w = spla.eigs(sub, k=6, which='LM', ncv=40, maxiter=3000, tol=1e-5, return_eigenvectors=False)
w = w[np.argsort(-np.abs(w))]
print('主回路 ρ = %.5f' % abs(w[0]))
for i, l in enumerate(w):
    print('    #%d  |λ| = %.5f  相位 %.1f°' % (i + 1, abs(l), np.degrees(np.angle(l))))
