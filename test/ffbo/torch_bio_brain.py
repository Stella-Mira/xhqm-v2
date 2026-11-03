"""按生物分层结构写的果蝇大脑参考实现（PyTorch）。

回应「生物里没有权重、只有各类激活函数」：

  · 连接组是**固定给定的**，不参与学习 —— 生物学里"谁连谁"是先天布线，可调的是每个神经元
    自己的**传递函数**。所以本文件里权重是 buffer，不是 parameter。
  · 每个神经元是一个**多输入非线性函数** Φᵢ，不是一个权重向量：
        rank-1（点神经元，等价于数字网络那种"线性投影 + 共享非线性"）：
            sᵢ = φ_soma( Σ_j wᵢⱼ xⱼ − θᵢ )
        rank-K（K 条树突分支，生物的真实形态）：
            hᵢᵦ = Σ_{j∈分支b} wᵢⱼ xⱼ
            zᵢ  = Σ_b gᵢᵦ · relu( hᵢᵦ − ϑᵢᵦ )     ← 输入到胞体【之前】就先非线性组合
            sᵢ  = φ_soma( zᵢ − θᵢ )
    ⇒ 秩 K 是"每个神经元一个矩阵"的具体化；K=1 退化为 rank-1，可直接当对照。
  · 分支划分用【突触前类型】：不同突触前伙伴落在不同树突区，是生物学的合理代理，
    数据里现成有 superclass。
  · 阈值 θ、时间常数 τ、分支增益/阈值、分级或脉冲 —— 这些"激活函数式"旋钮单独调**不改变
    函数类**，所以本文件把它们也做成参数，好让实验把"秩"和"调参"分开。

只当离线参考/实验台，不代替 xhqm 运行时。运行：python torch_bio_brain.py
"""
import os, sys, csv
import numpy as np
import torch

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
DATA = r'D:\WorkSpace\_research\digital-fruit-fly\data\malecns'

LAYERS = ['ol_sensory', 'ol_intrinsic', 'visual_projection', 'cb_intrinsic',
          'descending_neuron', 'vnc_motor']
SENSOR = 'ol_sensory'          # 刺激入口：光感受器所在类（6098 个）


def load_connectome():
    with open(os.path.join(DATA, 'csr.bin'), 'rb') as f:
        f.read(4); np.frombuffer(f.read(8), dtype=np.uint32)
        n, nnz = np.frombuffer(f.read(16), dtype=np.uint64); n, nnz = int(n), int(nnz)
        rp = np.fromfile(f, dtype=np.uint64, count=n + 1)
        col = np.fromfile(f, dtype=np.uint32, count=nnz)
        val = np.fromfile(f, dtype=np.float32, count=nnz)
    sc = np.empty(n, dtype=object); sc[:] = ''
    for r in csv.DictReader(open(os.path.join(DATA, 'neurons.csv'), encoding='utf-8')):
        i = int(r['idx'])
        if i < n: sc[i] = r['superclass'] or ''
    return n, nnz, rp, col, val, sc


def branch_of_edge(n, nnz, rp, col, sc, K):
    """把每条边按【突触前 superclass】分到 K 个分支（向量化）。
    每个神经元入边里最多的 K-1 种突触前类型各占一个分支，其余全部归分支 0。"""
    branch = np.zeros(nnz, dtype=np.int8)
    if K <= 1: return branch
    _, tcode = np.unique(sc, return_inverse=True)        # 每个神经元一个类型编号
    T = int(tcode.max()) + 1
    pre = tcode[col].astype(np.int64)                    # 每条边的突触前类型
    row = np.repeat(np.arange(n, dtype=np.int64), np.diff(rp).astype(np.int64))
    counts = np.bincount(row * T + pre, minlength=n * T).reshape(n, T)
    top = np.argsort(-counts, axis=1)[:, :K - 1]         # 每个神经元的前 K-1 种
    for r in range(K - 1):
        branch[pre == top[row, r]] = r + 1
    del row
    return branch


class BioBrain(torch.nn.Module):
    """固定布线 + 每神经元自己的传递函数（含 K 条树突分支）。"""

    def __init__(self, n, nnz, rp, col, val, sc, K=1, dt=20e-3, syn_gain=16.0,
                 graded=True, dev='cuda', seed=0, branch_mode='relu'):
        super().__init__()
        g = torch.Generator(device=dev).manual_seed(seed)
        self.n, self.K, self.dt, self.graded, self.dev = n, K, dt, graded, dev
        self.branch_mode = branch_mode   # 'relu'（分支非线性）| 'identity'（只分组、不非线性）

        crow = torch.from_numpy(rp.astype(np.int64))
        self.register_buffer('crow', crow)
        br = branch_of_edge(n, nnz, rp, col, sc, K)
        self.A = []
        for k in range(K):
            e = np.nonzero(br == k)[0]
            ci = torch.from_numpy(col[e].astype(np.int64))
            vv = torch.from_numpy(val[e].astype(np.float32) * syn_gain)
            self.register_buffer('ci%d' % k, ci)
            self.register_buffer('vv%d' % k, vv)
            self.A.append(torch.sparse_csr_tensor(crow, ci, vv, size=(n, n), device=dev))

        u = lambda a, b, *shape: (torch.rand(*shape, generator=g, device=dev) * (b - a) + a)
        self.theta = torch.nn.Parameter(u(0.85, 1.15, n))               # 阈值异质性
        self.tau = torch.nn.Parameter(u(0.08, 0.12, n))                 # 膜时间常数
        self.g_br = torch.nn.Parameter(torch.ones(n, K, device=dev))    # 分支增益
        self.br_th = torch.nn.Parameter(u(-0.1, 0.1, n, K))             # 分支阈值
        self.v = torch.zeros(n, device=dev)
        self.s = torch.zeros(n, device=dev)

        self.masks = {}
        for L in LAYERS:
            idx = np.nonzero(sc == L)[0]
            if idx.size:
                self.masks[L] = torch.from_numpy(idx.astype(np.int64)).to(dev)
        self.masks['_all'] = torch.arange(n, device=dev)

    def _drive(self, cur):
        if self.K == 1:
            I = cur + torch.sparse.mm(self.A[0], self.s[:, None])[:, 0]
        else:
            H = torch.stack([torch.sparse.mm(A, self.s[:, None])[:, 0] for A in self.A], 1)
            if self.branch_mode == 'identity':
                # 只按突触前类型分组、不加非线性：Σ_b H_b = Σ_j w_j s_j，与 K=1 严格等价。
                # 这个模式是对照：若它与 K=1 给出同样的结果，就证明"分组"本身没作用，
                # 起作用的是【非线性落在分支（输入侧）而不是只落在胞体（输出侧）】。
                Z = H.sum(1)
            else:
                Z = (self.g_br * torch.relu(H - self.br_th)).sum(1)
            I = cur + Z
        nv = self.v + (-self.v / self.tau) * self.dt + I * self.dt
        if self.graded:
            s = torch.clamp(nv, 0.0, 1.0)
        else:
            fire = nv >= self.theta
            s = fire.to(nv.dtype)
            nv = torch.where(fire, torch.zeros_like(nv), nv)
        self.v, self.s = nv, s
        return s

    @torch.no_grad()
    def run(self, stim_steps, amp, steps):
        rec = {L: [] for L in LAYERS if L in self.masks}
        sm = self.masks[SENSOR]
        for t in range(steps):
            cur = torch.zeros(self.n, device=self.dev)
            if t in stim_steps: cur[sm] = amp
            s = self._drive(cur)
            for L in rec: rec[L].append(s[self.masks[L]].clone())
        return {L: torch.stack(v) for L, v in rec.items()}


def sig_count(on, off, rnd, seed=12345):
    """逐细胞 |Δ|>3σ 计数。σ = 经验方差 + AR(1) 折算有效样本数（与 C++ 侧同一套）。
    rnd=True：把两相样本混起来重分（破坏与刺激的对应，保留自相关结构）—— 对照。"""
    if rnd:
        g = torch.Generator(device=on.device).manual_seed(seed)
        allx = torch.cat([on, off], 0)[torch.randperm(on.shape[0] + off.shape[0],
                                                      generator=g, device=on.device)]
        h = on.shape[0]
        on, off = allx[:h], allx[h:2 * h]
    m_on, m_off = on.mean(0), off.mean(0)
    v_on, v_off = on.var(0, unbiased=True), off.var(0, unbiased=True)

    def neff(x, v):
        cov = ((x[:-1] - x[:-1].mean(0)) * (x[1:] - x[1:].mean(0))).mean(0)
        rho = torch.clamp(cov / torch.clamp(v, min=1e-12), 0.0, 0.98)
        return x.shape[0] * (1 - rho) / (1 + rho)
    se = torch.sqrt(torch.clamp(v_on / neff(on, v_on) + v_off / neff(off, v_off), min=1e-12))
    return int(((m_on - m_off).abs() > 3 * se).sum()), int(se.numel())


def main():
    dev = 'cuda' if torch.cuda.is_available() else 'cpu'
    n, nnz, rp, col, val, sc = load_connectome()
    print('连接组 h=%d 突触=%d  设备=%s' % (n, nnz, dev))
    print('分区: ' + '  '.join('%s=%d' % (L, int((sc == L).sum())) for L in LAYERS))

    T = 300
    t_on = list(range(100, 200))
    print('\n刺激：向 %s（%d 个传感器）注入阶跃电流；判据 |Δ|>3σ（经验方差 + AR(1)），对照=标签随机化'
          % (SENSOR, int((sc == SENSOR).sum())))

    def one(tag, **kw):
        brain = BioBrain(n, nnz, rp, col, val, sc, dev=dev, **kw)
        rec = brain.run(t_on, 1.5, T)
        out = ['%-30s' % tag]
        for L in ('ol_sensory', 'ol_intrinsic', 'visual_projection', 'cb_intrinsic',
                  'descending_neuron', 'vnc_motor'):
            x = rec[L]
            on, off = x[100:200], torch.cat([x[0:100], x[200:]], 0)
            c, tot = sig_count(on, off, False)
            ctrl, _ = sig_count(on, off, True)
            act = float(x[200:].mean())          # 稳态平均输出（判断是否只是工作点被推高）
            out.append('%5d/%-5.0f(对%3d,活%.2f)' % (c, tot * 0.0027, ctrl, act))
        print(''.join(out))

    hdr = '%-30s' + '%22s' * 6
    print('\n' + hdr % ('配置', 'ol_sensory', 'ol_intrinsic', 'visual_proj', 'cb_intrinsic',
                        'descending', 'vnc_motor'))
    print('（每格 = 显著数 / 假阳性期望(对照数, 稳态平均活动)）')
    one('K=1  点神经元 sg=16', K=1, syn_gain=16)
    one('K=1  点神经元 sg=128（增益×8）', K=1, syn_gain=128)
    one('K=1  点神经元 sg=1024（增益×64）', K=1, syn_gain=1024)
    one('K=8  仅分组·无分支非线性', K=8, syn_gain=16, branch_mode='identity')
    one('K=8  分支整流（秩8）', K=8, syn_gain=16)
    one('K=4  分支整流（秩4）', K=4, syn_gain=16)
    one('K=8  分支整流 sg=4（压增益）', K=8, syn_gain=4)


if __name__ == '__main__':
    main()
