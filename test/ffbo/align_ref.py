"""rank-K 对齐实验的 PyTorch 侧驱动脚本（离线，不进运行时）。

目的：把 torch_bio_brain.py 的 BioBrain 当作【参考动力学】，只跑我们关心的配置并打印机器可读的表，
好与 C++ 的 --cell-model cur（同构模式）并排比较。

不改 torch_bio_brain.py（那是 r566 的提交物），只是 import 它并按需调用。
用法：python align_ref.py <K> <syn_gain> [--seed 0]
输出：一行 JSON，含每个 superclass 的 (显著数, 假阳性期望, 对照显著数, 稳态平均输出)
"""
import sys, os, json, io, contextlib
import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import torch_bio_brain as ref

LAYERS = ['ol_sensory', 'ol_intrinsic', 'visual_projection', 'cb_intrinsic',
          'descending_neuron', 'vnc_motor']


def main():
    K = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    sg = float(sys.argv[2]) if len(sys.argv) > 2 else 16.0
    seed = 0
    amp = 1.5
    T = 300
    t_on = list(range(100, 200))
    dev = 'cuda' if torch.cuda.is_available() else 'cpu'

    with contextlib.redirect_stdout(io.StringIO()):
        n, nnz, rp, col, val, sc = ref.load_connectome()
        res = {}
        for rnd in (False, True):
            brain = ref.BioBrain(n, nnz, rp, col, val, sc, K=K, syn_gain=sg,
                                 dev=dev, seed=seed, branch_mode='relu')
            rec = brain.run(t_on, amp, T)
            for L in LAYERS:
                x = rec[L]
                on, off = x[100:200], torch.cat([x[0:100], x[200:]], 0)
                c, tot = ref.sig_count(on, off, False)
                cc, _ = ref.sig_count(on, off, rnd)
                if not rnd:
                    res[L] = dict(sig=c, fp=tot * 0.0027, steady=float(x[200:].mean()),
                                  on_mean=float(x[100:200].mean()), off_mean=float(x[0:100].mean()),
                                  n_out=int((x[100:200].abs().sum(0) > 0).sum()))
                else:
                    res[L]['ctrl'] = cc
    res['_config'] = dict(K=K, syn_gain=sg, seed=seed, amp=amp, T=T, dev=dev)
    print(json.dumps(res, ensure_ascii=False))


if __name__ == '__main__':
    main()
