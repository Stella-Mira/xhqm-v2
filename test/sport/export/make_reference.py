#!/usr/bin/env python3
"""生成 wav2vec2-base 的 PyTorch 参考输出 + 探测 pos_conv_embed 的 weight_norm 重建公式。

用法:
    python make_reference.py [out_dir]

输出（out_dir 内，全部 raw f32 行优先）:
    input.bin           测试输入 (1, 16000)
    last_hidden.bin     encoder 输出 (1, T, 768)
    extract.bin         feature_extractor 输出 (1, 512, T')
    pos_conv_weight.bin pos_conv_embed 的有效卷积权重 (768, 48, 128)
"""
import os
import sys

import numpy as np
import torch
from transformers import Wav2Vec2Model


def main() -> None:
    model = Wav2Vec2Model.from_pretrained("facebook/wav2vec2-base")
    model.eval()

    # —— 探测 pos_conv_embed 的 weight_norm 参数与有效权重 ——
    conv = model.encoder.pos_conv_embed.conv
    w_eff = conv.weight.detach().cpu().float()          # 参数化后的有效权重
    sd = model.state_dict()
    o0 = sd["encoder.pos_conv_embed.conv.parametrizations.weight.original0"].cpu().float()
    o1 = sd["encoder.pos_conv_embed.conv.parametrizations.weight.original1"].cpu().float()
    print("effective weight shape:", tuple(w_eff.shape))
    print("original0 shape:", tuple(o0.shape), " original1 shape:", tuple(o1.shape))
    recon = torch._weight_norm(o1, o0, 2)
    print("recon == effective:", bool(torch.allclose(recon, w_eff, atol=1e-6)))

    # —— 测试输入（确定性随机） ——
    torch.manual_seed(0)
    seq = 16000
    x = torch.randn(1, seq)

    with torch.no_grad():
        out = model(x)
        last_hidden = out.last_hidden_state.detach().cpu().float().numpy()
        extract = model.feature_extractor(x).detach().cpu().float().numpy()

    out_dir = sys.argv[1] if len(sys.argv) > 1 else "ref"
    os.makedirs(out_dir, exist_ok=True)
    x.numpy().astype(np.float32).tofile(os.path.join(out_dir, "input.bin"))
    last_hidden.astype(np.float32).tofile(os.path.join(out_dir, "last_hidden.bin"))
    extract.astype(np.float32).tofile(os.path.join(out_dir, "extract.bin"))
    w_eff.numpy().astype(np.float32).tofile(os.path.join(out_dir, "pos_conv_weight.bin"))
    print("saved ->", out_dir)
    print("last_hidden shape:", last_hidden.shape, " extract shape:", extract.shape)


if __name__ == "__main__":
    main()
