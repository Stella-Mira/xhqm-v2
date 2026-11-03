#!/usr/bin/env python3
"""导出 encoder 各阶段参考（与 C++ 逐段 diff 用）：proj / pos / enc_in / enc_l0。"""
import math
import os

import numpy as np
import torch
from transformers import Wav2Vec2Model


def save(t, path):
    t.detach().cpu().float().numpy().astype(np.float32).tofile(path)


def main():
    model = Wav2Vec2Model.from_pretrained("facebook/wav2vec2-base")
    model.eval()
    out = sys_argv_out()

    # 用与 C++ loader_test 相同的输入（ref/input.bin，seed 0）
    inp_path = sys_argv_inp()
    inp = np.fromfile(inp_path, dtype=np.float32)
    x = torch.from_numpy(inp).reshape(1, -1)

    feat = model.feature_extractor(x)                    # (1,512,49)
    h = feat.transpose(1, 2)                              # (1,49,512)
    save(h, os.path.join(out, "proj_in.bin"))

    proj = model.feature_projection
    h_ln = proj.layer_norm(h)                             # (1,49,512)
    save(h_ln, os.path.join(out, "proj_ln.bin"))
    h_proj = proj.projection(h_ln)                        # (1,49,768)
    save(h_proj, os.path.join(out, "proj.bin"))

    pe = model.encoder.pos_conv_embed(h_proj)             # (1,49,768)
    save(pe, os.path.join(out, "pos.bin"))

    enc_in = model.encoder.layer_norm(h_proj + pe)        # (1,49,768)
    save(enc_in, os.path.join(out, "enc_in.bin"))

    enc_l0 = model.encoder.layers[0](enc_in)              # (1,49,768)
    save(enc_l0, os.path.join(out, "enc_l0.bin"))

    print("saved ->", out)


def sys_argv_out():
    import sys
    return sys.argv[1] if len(sys.argv) > 1 else "dbg"


def sys_argv_inp():
    import sys
    return sys.argv[2] if len(sys.argv) > 2 else "out/ref/input.bin"


if __name__ == "__main__":
    main()
