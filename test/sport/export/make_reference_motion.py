#!/usr/bin/env python3
"""生成 motion 参考输出（temperature=0 贪心 → 确定性 token + 逐帧情绪）。

用 body/sport 的管线跑同一段音频，导出：输入音频、5 路 token、反归一化 poses/exps/trans，
以及逐帧情绪（emo_probs (T,5) softmax 概率 + emo_ids (T,) argmax 标签）。

用法:
    python make_reference_motion.py [--out out/motion/ref] [--sec 2.0] [--seed 0]
                                    [--gen D:\\...\\generator-emo.ckpt]
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, r"D:\WorkSpace\body")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="out/motion/ref")
    ap.add_argument("--sec", type=float, default=2.0)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--gen", default=r"D:\WorkSpace\body\sport\checkpoints\generator-emo.ckpt",
                    help="生成器 checkpoint（含 emo_head）")
    args = ap.parse_args()

    import torch
    from sport import Sport
    from sport.generator import MotionGenerator
    from sport.emotion import EMOTIONS

    os.makedirs(args.out, exist_ok=True)

    # 确定性音频（16k 单声道）
    rng = np.random.RandomState(args.seed)
    n = int(args.sec * 16000)
    audio = (rng.randn(n) * 0.1).astype(np.float32)
    audio.tofile(os.path.join(args.out, "input.bin"))

    sport = Sport(device="cpu", temperature=0.0, top_k=0)
    sport.load("")   # tokenizer + stats（stream 权重，不变）

    # 覆盖生成器：加载情感头 checkpoint
    ckg = torch.load(args.gen, map_location="cpu", weights_only=False)
    sd = ckg["model"]
    n_emo = len(ckg.get("emotions", EMOTIONS))
    pos_rows = int(sd["pos_emb.weight"].shape[0])
    cond_dim = int(sd["cond_proj.weight"].shape[1]) if "cond_proj.weight" in sd else 0
    gen = MotionGenerator(n_emotion=n_emo, max_pos=pos_rows, cond_dim=cond_dim)
    missing, unexpected = gen.load_state_dict(sd, strict=False)
    gen.eval()
    print(f"[gen] loaded {args.gen} (n_emo={n_emo} cond_dim={cond_dim} "
          f"missing={len(missing)} unexpected={len(unexpected)})", flush=True)
    emo_id = 0  # neutral 种子

    feat = sport.featurizer()
    feat.reset()
    sess = gen.new_session(emo_id, temperature=0.0, top_k=0, cond=None)

    chunk = int(0.5 * 16000)
    received = 0.0
    n_frames = max(1, int(round(len(audio) * 30 / 16000)))
    new_list = []

    def drain():
        nonlocal received
        while len(sess.hist) < n_frames and len(sess.hist) / 30.0 + 0.2 <= received + 1e-6:
            sess.step()

    for i in range(0, len(audio), chunk):
        new = feat.push(audio[i:i + chunk])
        if new.shape[0]:
            new_list.append(new)
            c0 = feat.emitted_upto - (new.shape[0] - 1) * 0.02
            sess.push_audio(new, c0)
        received = min(len(audio), i + chunk) / 16000.0
        drain()

    received = len(audio) / 16000.0
    while len(sess.hist) < n_frames:
        sess.step()

    idxs = [np.array([h[k] for h in sess.hist], np.int64) for k in range(5)]
    poses, exps, trans = sport.motion_from_tokens(idxs)

    tok = np.stack(idxs, axis=1).astype(np.int32)   # (T, 5)
    tok.tofile(os.path.join(args.out, "tokens.bin"))
    poses.astype(np.float32).tofile(os.path.join(args.out, "poses.bin"))
    exps.astype(np.float32).tofile(os.path.join(args.out, "exps.bin"))
    trans.astype(np.float32).tofile(os.path.join(args.out, "trans.bin"))

    # 逐帧情绪（"情感脉冲"）：softmax 概率 + argmax 标签
    emo_probs = np.asarray(sess.emo_hist, np.float32)      # (T, n_emo)
    emo_probs.tofile(os.path.join(args.out, "emo_probs.bin"))
    emo_probs.argmax(axis=1).astype(np.int32).tofile(os.path.join(args.out, "emo_ids.bin"))

    # 供分阶段对照：喂给生成器的音频特征 + 中心时刻
    feats_all = np.concatenate(new_list, axis=0) if new_list else np.zeros((0, 768), np.float32)
    feats_all.astype(np.float32).tofile(os.path.join(args.out, "audio_feats.bin"))
    centers = sess.centers[:sess.audio_count].numpy().astype(np.float32)
    centers.tofile(os.path.join(args.out, "audio_centers.bin"))

    print(f"ref saved -> {args.out}")
    print(f"  frames={tok.shape[0]}  poses={poses.shape}  exps={exps.shape}  trans={trans.shape}  "
          f"emo_probs={emo_probs.shape}")
    print(f"  emo_ids[:16]={emo_probs.argmax(1)[:16].tolist()}")


if __name__ == "__main__":
    main()
