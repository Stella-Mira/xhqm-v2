# -*- coding: utf-8 -*-
# 谱分析第二半：视觉输入在【近 1 本征模】上的投影有多大。
#
# 为什么这一问是关键：线性情形下 s = Σ_i [⟨u_i, b⟩ / (1 − λ_i)] v_i
#   —— 响应不是由 W 的整体大小决定，而是由【输入 b 在慢模上的投影】决定。
# 如果近 1 的那些模上投影很小，那么「视觉占 6% 撬不动 85% 的递归」就是【对齐问题】，
# 该改的是输入编码（让视觉信息落到慢模上），而不是继续加增益。
#
# 方法：对 A 求前 k 个最大模的右本征向量 v_i 与左本征向量 u_i（u_i 是 Aᵀ 的右本征向量），
# 计算输入向量 b 在每对上的系数 c_i = |⟨u_i, b⟩|，与 ‖b‖‖u_i‖ 归一化后比较。
# 同时看每个模在各 superclass 上的能量分布（判断慢模是全局的还是局部的）。
import os, csv, sys
import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

OUT = r'D:\WorkSpace\_research\digital-fruit-fly\data\malecns'
K = 16

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

A = sp.csr_matrix((val.astype(np.float64), col.astype(np.int32), rp.astype(np.int32)), shape=(nn, nn))
AT = A.T.tocsr()

print('求右本征向量 …')
w_r, V = spla.eigs(A, k=K, which='LM', ncv=60, maxiter=5000, tol=1e-5)
print('求左本征向量（= Aᵀ 的右本征向量）…')
w_l, U = spla.eigs(AT, k=K, which='LM', ncv=60, maxiter=5000, tol=1e-5)
idx = np.argsort(-np.abs(w_r)); w_r = w_r[idx]; V = V[:, idx]
idx2 = np.argsort(-np.abs(w_l)); w_l = w_l[idx2]; U = U[:, idx2]

# ── 输入向量 ──
# 1) 均匀：注入组（光感受器 R1-6）全为 1
inj = np.zeros(n); inj[0] = 0
# 从 groups.txt 读注入组
for line in open(os.path.join(OUT, 'groups.txt'), encoding='utf-8'):
    p = line.split()
    if len(p) >= 2 and p[0] == 'visual_R1_6':
        ids = [int(x) for x in p[1:]]
        inj[ids] = 1.0
        break
print('注入组 visual_R1_6: %d 个细胞' % int(inj.sum()))

# 2) 结构化：用光学柱方位角造一条条纹（与 mcns_net 的 stripe 图案同一映射）
side = {}; h1 = {}; h2 = {}
for line in open(os.path.join(OUT, 'visual_columns.txt'), encoding='utf-8'):
    if line.startswith('#'):
        continue
    p = line.split()
    if len(p) < 5:
        continue
    i = int(p[0]); side[i] = p[1]; h1[i] = int(p[2]); h2[i] = int(p[3])
grat = np.zeros(n)
for i in range(n):
    if inj[i] <= 0 or i not in h1:
        continue
    u = (h1[i] - 2) / 34.0
    az = (-135.0 + u * 143.5) if side.get(i) == 'L' else (8.5 - u * 143.5)
    grat[i] = 0.5 + 0.5 * np.sin(az * (2.0 * np.pi / 0.8))     # 空间周期 0.8 rad，与程序默认一致
grat_mean = grat[inj > 0].mean()
grat[inj > 0] -= grat_mean                                       # 去掉直流（modulation 才是有信息的部分）
print('条纹输入: 调制幅度（去掉直流后）‖b‖=%.1f' % np.linalg.norm(grat))

print()
print('=== 视觉输入在各本征模上的归一化投影 |⟨u_i,b⟩|/(‖u_i‖‖b‖) ===')
print('%-4s %-12s %-10s %-12s %-12s' % ('#', '|λ|', '相位(°)', '均匀投影', '条纹投影'))
for i in range(min(8, K)):
    ur = U[:, i]; vr = V[:, i]
    nu = np.linalg.norm(ur)
    c_u = abs(np.vdot(ur, inj)) / max(nu * np.linalg.norm(inj), 1e-12)
    c_g = abs(np.vdot(ur, grat)) / max(nu * np.linalg.norm(grat), 1e-12)
    print('%-4d %-12.5f %-10.1f %-12.4f %-12.4f' % (i + 1, abs(w_r[i]), np.degrees(np.angle(w_r[i])), c_u, c_g))
print()
print('参照：随机向量对任意模的典型投影量级 ≈ 1/√h = %.5f' % (1.0 / np.sqrt(n)))
print()

# ── 慢模的空间分布：在哪些 superclass 上有能量 ──
print('=== 前 3 个模在各 superclass 上的能量占比（右本征向量）===')
scs = ['ol_sensory', 'ol_intrinsic', 'visual_projection', 'cb_intrinsic', 'descending_neuron', 'vnc_intrinsic', 'vnc_motor']
for i in range(3):
    v = V[:, i]; e = np.abs(v) ** 2; e = e / e.sum()
    print('模 %d (|λ|=%.4f):' % (i + 1, abs(w_r[i])))
    tot_other = 1.0
    for s in scs:
        m = np.array([sc[j] == s for j in range(n)])
        val_ = e[m].sum()
        tot_other -= val_
        print('    %-22s %6.2f%%' % (s, 100.0 * val_))
    print('    %-22s %6.2f%%' % ('其它', 100.0 * tot_other))
    # 最大的单个分量在哪个类型
    j = int(np.argmax(np.abs(v)))
    print('    最大分量: #%d %s %s' % (j, sc[j], ty[j]))
