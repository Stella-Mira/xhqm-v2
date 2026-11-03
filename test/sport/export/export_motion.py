#!/usr/bin/env python3
"""导出 motion（sport）权重：tokenizer/generator 的 .ckpt → manifest.json + weights.bin，stats.npz → stats.json。

用法:
    python export_motion.py [--root D:\\WorkSpace\\stream\\motion\\checkpoints] [--out out/motion]
"""
import argparse
import json
import os

import numpy as np
import torch


def dump_state_dict(state_dict, out_dir, model_name):
    os.makedirs(out_dir, exist_ok=True)
    manifest = {"model": model_name, "dtype": "f32", "params": []}
    offset = 0
    with open(os.path.join(out_dir, "weights.bin"), "wb") as f:
        for name, param in state_dict.items():
            t = param.detach().cpu().float().numpy()
            t = np.ascontiguousarray(t, dtype=np.float32)
            raw = t.tobytes()
            manifest["params"].append(
                {"name": name, "shape": list(t.shape), "offset": offset, "nbytes": len(raw)})
            f.write(raw)
            offset += len(raw)
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    return len(manifest["params"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=r"D:\WorkSpace\stream\motion\checkpoints")
    ap.add_argument("--out", default="out/motion")
    ap.add_argument("--gen", default="",
                    help="生成器 ckpt 覆盖（如情感头 generator-emo.ckpt）；空则用 --root/generator/best.ckpt")
    args = ap.parse_args()

    tok = torch.load(os.path.join(args.root, "tokenizer", "best.ckpt"),
                     map_location="cpu", weights_only=False)
    n1 = dump_state_dict(tok["model"], os.path.join(args.out, "tokenizer"), "motion-tokenizer")
    print(f"tokenizer: {n1} params -> {args.out}/tokenizer")

    gen_path = args.gen or os.path.join(args.root, "generator", "best.ckpt")
    gen = torch.load(gen_path, map_location="cpu", weights_only=False)
    n2 = dump_state_dict(gen["model"], os.path.join(args.out, "generator"), "motion-generator")
    with open(os.path.join(args.out, "generator", "meta.json"), "w", encoding="utf-8") as f:
        json.dump({"emotions": gen.get("emotions"), "T": gen.get("T")}, f, ensure_ascii=False)
    print(f"generator: {n2} params -> {args.out}/generator")

    s = np.load(os.path.join(args.root, "tokenizer", "stats.npz"))
    stats = {k: s[k].astype(np.float32).tolist() for k in s.files}
    with open(os.path.join(args.out, "stats.json"), "w", encoding="utf-8") as f:
        json.dump(stats, f, ensure_ascii=False)
    print(f"stats: {list(stats.keys())} -> {args.out}/stats.json")


if __name__ == "__main__":
    main()
