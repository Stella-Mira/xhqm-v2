#!/usr/bin/env python3
"""导出 DINOv2-base 权重（manifest.json + weights.bin）+ 生成 PyTorch 参考输出。

用法:
    python export_dinov2.py --model facebook/dinov2-base --out out/dinov2-base --ref out/ref_dinov2
"""
import argparse
import json
import os

import numpy as np


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="facebook/dinov2-base")
    ap.add_argument("--out", default="out/dinov2-base")
    ap.add_argument("--ref", default="out/ref_dinov2")
    args = ap.parse_args()

    import torch
    from transformers import Dinov2Model

    print(f"loading {args.model} ...")
    model = Dinov2Model.from_pretrained(args.model)
    model.eval()

    # —— 导出 state_dict → manifest.json + weights.bin ——
    os.makedirs(args.out, exist_ok=True)
    manifest = {"model": args.model, "dtype": "f32", "params": []}
    offset = 0
    with open(os.path.join(args.out, "weights.bin"), "wb") as f:
        for name, param in model.state_dict().items():
            t = param.detach().cpu().float().numpy()
            t = np.ascontiguousarray(t, dtype=np.float32)
            raw = t.tobytes()
            manifest["params"].append(
                {"name": name, "shape": list(t.shape), "offset": offset, "nbytes": len(raw)}
            )
            f.write(raw)
            offset += len(raw)
    with open(os.path.join(args.out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    print(f"exported {len(manifest['params'])} params -> {args.out}")

    # —— 参考输出（518×518 随机图，seed 0）——
    torch.manual_seed(0)
    img = torch.randn(1, 3, 518, 518)
    with torch.no_grad():
        out = model(img)
    os.makedirs(args.ref, exist_ok=True)
    img.numpy().astype(np.float32).tofile(os.path.join(args.ref, "input.bin"))
    out.last_hidden_state.detach().cpu().float().numpy().astype(np.float32).tofile(
        os.path.join(args.ref, "last_hidden.bin"))
    out.pooler_output.detach().cpu().float().numpy().astype(np.float32).tofile(
        os.path.join(args.ref, "pooler.bin"))
    print(f"ref saved -> {args.ref}  last_hidden={tuple(out.last_hidden_state.shape)}  "
          f"pooler={tuple(out.pooler_output.shape)}")


if __name__ == "__main__":
    main()
