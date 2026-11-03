"""用真实 MaleCNS 连接组量一件事：把果蝇大脑搬到 PyTorch/GPU 到底买得到什么。

只回答问题，不改运行时。产出：表示规模表 + 每神经步的 SpMV 实测（CPU / GPU / 批量）。
运行：python offline_torch_probe.py
"""
import os, sys, time
import numpy as np

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
OUT = r'D:\WorkSpace\_research\digital-fruit-fly\data\malecns'

# ── 加载 CSR（与 offline_spectrum1.py 同一格式）──
with open(os.path.join(OUT, 'csr.bin'), 'rb') as f:
    f.read(4); np.frombuffer(f.read(8), dtype=np.uint32)
    nn, nnz = np.frombuffer(f.read(16), dtype=np.uint64); nn, nnz = int(nn), int(nnz)
    rp = np.fromfile(f, dtype=np.uint64, count=nn + 1)
    col = np.fromfile(f, dtype=np.uint32, count=nnz)
    val = np.fromfile(f, dtype=np.float32, count=nnz)
print('连接组: h=%d 突触=%d（每神经元入度均 %.1f）' % (nn, nnz, nnz / nn))

# ── 表示规模表（fp32）──
l = nnz
print('\n── 各种「矩阵表示」的规模（fp32）──')
rows = [
    ('解剖学非零（CSR：值+列号+行指针）', nnz * 8 + nn * 4),
    ('h×h 稠密（每个神经元一行权重）', nn * nn * 4),
    ('l×l 稠密（边上一条权重，你说的那种稠密）', l * l * 4),
    ('边空间"每神经元一个矩阵"的块对角全填', np.uint64(3.93e9) * 4),
]
for name, b in rows:
    if b > 1e15: s = '%.2f PB' % (b / 1e15)
    elif b > 1e12: s = '%.2f TB' % (b / 1e12)
    elif b > 1e9: s = '%.1f GB' % (b / 1e9)
    else: s = '%.0f MB' % (b / 1e6)
    print('  %-42s %12s' % (name, s))

# ── CPU：scipy 单线程 SpMV（不含细胞动力学）──
import scipy.sparse as sp
A = sp.csr_matrix((val, col.astype(np.int32), rp.astype(np.int32)), shape=(nn, nn))
x = np.random.randn(nn).astype(np.float64)
A.dot(x); t0 = time.perf_counter()
for _ in range(20): y = A.dot(x)
cpu_ms = (time.perf_counter() - t0) / 20 * 1000
print('\n── 每神经步的 SpMV（不含细胞动力学）──')
print('  scipy CSR 单线程            %8.3f ms' % cpu_ms)

# ── GPU：torch 稀疏 ──
import torch
dev = 'cuda'
idx = torch.from_numpy(col.astype(np.int64))
ptr = torch.from_numpy(rp.astype(np.int64))
v = torch.from_numpy(val)
Ac = torch.sparse_csr_tensor(ptr, idx, v, size=(nn, nn), device=dev, dtype=torch.float32)

def bench(fn, iters=50, warm=10):
    for _ in range(warm): fn()
    torch.cuda.synchronize(); t0 = time.perf_counter()
    for _ in range(iters): fn()
    torch.cuda.synchronize()
    return (time.perf_counter() - t0) / iters * 1000

xg = torch.randn(nn, 1, device=dev)
gpu1 = bench(lambda: torch.sparse.mm(Ac, xg))
print('  torch sparse_csr GPU (1 只) %8.3f ms' % gpu1)

# 批量：一次算 B 只果蝇（参数扫描用的形态）
for B in (16, 64):
    xb = torch.randn(nn, B, device=dev)
    ms = bench(lambda: torch.sparse.mm(Ac, xb), iters=30)
    print('  torch sparse_csr GPU (%2d 只批量) %7.3f ms  ⇒ 每只 %.4f ms' % (B, ms, ms / B))

# ── 细胞动力学那部分（逐神经元 elementwise，5 个通道态）──
# 这个是任何框架都一样的访存量：166700 × 5 个 float
s = torch.rand(nn, device=dev); g = torch.rand(nn, 5, device=dev); cur = torch.rand(nn, device=dev)
dyn = bench(lambda: (g * s[:, None]).sum(1) + cur)
print('\n  参考：细胞动力学 elementwise（GPU） %7.3f ms' % dyn)

# ── 稠密 h×h 直接试一下（预期 OOM）──
try:
    d = torch.zeros(nn, nn, device=dev, dtype=torch.float32)
    print('\n  h×h 稠密 GPU：居然分配成功（%.1f GB）' % (d.numel() * 4 / 1e9))
    del d
except RuntimeError as e:
    print('\n  h×h 稠密 GPU：失败 ⇒ %s' % str(e).split('.')[0][:90])

print('\n显存：已用 %.1f GB / 总 %.1f GB' % (torch.cuda.memory_allocated() / 1e9,
      torch.cuda.get_device_properties(0).total_memory / 1e9))
