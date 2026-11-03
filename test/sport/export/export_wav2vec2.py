#!/usr/bin/env python3
"""从 HuggingFace/PyTorch 导出 wav2vec2-base 权重到 manifest.json + weights.bin。

用法:
    python export_wav2vec2.py --model facebook/wav2vec2-base --out out/wav2vec2-base

输出:
    out/wav2vec2-base/manifest.json   参数清单（name/shape/offset/nbytes）
    out/wav2vec2-base/weights.bin     全部参数按 state_dict 顺序拼接的 raw f32（行优先）

说明:
    - 只做 detach().cpu().float()，不改布局、不量化，保证逐字节可复现。
    - conv1d 权重 (Cout, Cin/groups, K) 与 xhqm tensor_base::conv1d 布局天然一致。
"""
import argparse
import json
import os

import numpy as np


def export(model_name: str, out_dir: str) -> None:
    import torch
    from transformers import Wav2Vec2Model

    print(f"loading {model_name} ...")
    model = Wav2Vec2Model.from_pretrained(model_name)
    os.makedirs(out_dir, exist_ok=True)

    manifest = {"model": model_name, "dtype": "f32", "params": []}
    offset = 0
    with open(os.path.join(out_dir, "weights.bin"), "wb") as f:
        for name, param in model.state_dict().items():
            t = param.detach().cpu().float().numpy()
            t = np.ascontiguousarray(t, dtype=np.float32)  # 行优先连续
            raw = t.tobytes()
            manifest["params"].append(
                {
                    "name": name,
                    "shape": list(t.shape),
                    "offset": offset,
                    "nbytes": len(raw),
                }
            )
            f.write(raw)
            offset += len(raw)

    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    n_params = len(manifest["params"])
    print(f"exported {n_params} params ({offset} bytes) -> {out_dir}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="facebook/wav2vec2-base")
    ap.add_argument("--out", default="out/wav2vec2-base")
    args = ap.parse_args()
    export(args.model, args.out)
