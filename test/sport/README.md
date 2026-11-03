# soul-exp — xhqm soul/tensor 实验库

xhqm 渲染引擎内的实验库（位于 `test/sport`，随 xhqm 一并入库），用 xhqm 的
`algebra/tensor`（含 conv）与 `algebra/soul`（transformer / AD）能力做具体模型实验。

第一个实验：**wav2vec2 权重移植**——把 HuggingFace/PyTorch 的 `wav2vec2-base`（Apache-2.0）
权重导出为 xhqm 可加载的裸格式，再用 xhqm 的 conv1d + transformer 复刻其前向推理
（作为 motion 流式语音驱动的冻结音频特征器）。

## 目录

```
test/sport/
├── README.md                     # 本文件
├── export/
│   ├── export_wav2vec2.py        # HF/PyTorch → manifest.json + weights.bin
│   └── requirements.txt          # torch, transformers, numpy
├── include/soul_exp/
│   ├── weight_loader.h           # 读 manifest.json + weights.bin，按层名取 shape/原始 f32
│   ├── nn.h                      # 共享原语：精确 GELU / LayerNorm / Linear / 非因果多头注意力
│   ├── wav2vec2.h                # wav2vec2-base 前向（feature_extractor + projection + encoder）
│   ├── dinov2.h                  # DINOv2(ViT-B/14) 前向（patch embed + transformer + [CLS]）
│   └── sport.h                   # motion-gen 前向（wav2vec2 流式特征 + AR Transformer + VQ-VAE decode + 逐帧情感脉冲）
└── test/
    ├── wav2vec2_smoke.cpp        # 算子级数值自检（不依赖权重）
    ├── loader_test.cpp           # wav2vec2 前向对照 PyTorch 参考输出
    ├── dinov2_test.cpp           # DINOv2 前向对照 PyTorch 参考输出
    └── sport_test.cpp            # motion-gen 全链路对照 PyTorch（token 精确 + 动作/情感 1e-5）
```

## 权重导出格式

`export/export_wav2vec2.py` 把 PyTorch state_dict 的每个参数（统一转 f32、行优先，
即 PyTorch 原生内存序）按 `name` 顺序拼接写入 `weights.bin`，并生成 `manifest.json`：

```json
{
  "model": "facebook/wav2vec2-base",
  "dtype": "f32",
  "params": [
    { "name": "feature_extractor.conv_layers.0.conv.weight",
      "shape": [512, 1, 10], "offset": 0, "nbytes": 20480 },
    ...
  ]
}
```

- `shape` 与 PyTorch 完全一致；conv1d 权重即 `(Cout, Cin/groups, K)`，与 xhqm `tensor_base::conv1d` 的权重布局**天然对齐**。
- Linear 权重为 `(out, in)`，xhqm `matmul(x, W)` 用 `(in, out)`，架构侧转置即可（wav2vec2.h 处理）。
- 导出仅做 `detach().cpu().float()`，不做任何量化/重排，保证逐字节可复现。

## 路线

1. ✅ `weight_loader.h`：读 manifest + bin，`shape(name)` / `data(name)` / `load_tensor<dim>(name)`。
2. ✅ `wav2vec2.h`：复刻 wav2vec2-base 前向（feature_extractor 7×conv1d+GroupNorm+GELU →
   feature_projection LayerNorm+Linear 512→768 → encoder pos_conv_embed grouped conv1d + 12×非因果注意力+FFN）。
   冻结推理，不需要 quantizer。冒烟自检见 `test/wav2vec2_smoke.cpp`（算子级数值自检）。
3. ✅ `test/`：`loader_test.cpp` 加载导出权重跑前向，与 PyTorch `Wav2Vec2Model(...)` 输出对照。
   峰值归一相对误差 `max|diff|/max|ref|`：`extract_features` ≈ 1.8e-6、`last_hidden_state` ≈ 1.9e-5
   （12 层 float32 累加噪声量级），锁定移植正确性。

## 移植要点（对齐 HF 的实现细节）

- **特征抽取归一化只在前 7 层的第 0 层**：`conv_layers[0]` = conv+GroupNorm+GELU，其余层 = conv+GELU。
- **GroupNorm(num_groups=num_channels)**：每通道独立归一化（InstanceNorm），biased var。
- **pos_conv_embed 的 conv 是 weight_norm 重参数化**：state_dict 里是 `parametrizations.weight.original0(g)` + `original1(v)`，
  有效权重 = `v · (g / norm_except_dim(v, 2, dim=2))`（对除 kernel 维外的所有维求 L2 范数）；另加 `Wav2Vec2SamePadLayer`
  去掉偶数核 + 对称 pad 产生的多出 1 个时间步。
- **GELU 为精确 erf 版**（`F.gelu`，非 tanh 近似）；encoder 为 post-norm 结构（attention 残差→LN，FFN 残差→final LN）。

## DINOv2（图像向量）

`dinov2.h`：复刻 `facebook/dinov2-base`（ViT-B/14，518×518 → 37×37=1369 patch）。
- patch embed = `Conv2d(3→768, k=14, s=14)`；[CLS] + 可学习位置编码；12× pre-norm transformer。
- 关键差异 vs wav2vec2：**layer_norm_eps=1e-6**（不是 1e-5）；每层有 **layer_scale**（`layer_scale1/2.lambda1`，逐特征维缩放）；**pre-norm**（不是 post-norm）。
- 输出 `pooler`（[CLS] 向量）与 `last_hidden_state`（patch 特征）。
- 对照 PyTorch `Dinov2Model`：`pooler` ≈ 3.7e-6、`last_hidden_state` ≈ 2.3e-5（峰值归一相对误差）。
- 下载：直连 huggingface.co 需走本机代理 `http://127.0.0.1:7890`（或 `HF_ENDPOINT=https://hf-mirror.com`）。

## sport（motion-gen，音频 → SMPL-X 动作 + 逐帧情感脉冲）

`sport.h`：复刻 `body/sport` 的 motion-gen 推理管线（权重导出自 `stream/motion/checkpoints`）。
- 链路：音频(16k) → wav2vec2 滑窗流式特征(50Hz,768d) → AR Transformer 生成器（6 层, D=512/8 头, 帧级 5 token 并行, KV 缓存增量采样）→ VQ-VAE 分词器 decode（5 码本 + conv1d 解码器）→ 反归一化 → poses(165)/exps(100)/trans(3)。
- **情感脉冲（新增）**：生成器 `n_out(x)` 后接 `emo_head`（Linear(d,5)），逐帧输出 5 类情绪 softmax 概率（argmax 即离散标签 neutral/happy/sad/angry/surprised）；预测分布对 `emo_emb` 做**软调制**，回馈到下一帧输入（自回归），全局 `emo_id` 退化为第 0 帧「情绪种子」。每帧输出 `(token, emo_probs(5), emo_id)`。
- 情感对照 PyTorch（temperature=0）：**emo_ids 精确一致（0/60 mismatch）**，emo_probs `rel_to_max` ≈ 9.1e-7；动作 token 仍 0/300 mismatch。
- 训练：`body/sport/scripts/train_generator_emo.py` 两阶段（先冻结骨干训 `emo_head` 线性探针，再联合微调），监督来自 `stream/motion/data/emotion_labels.json`（逐时间段 5 类标注 → 逐帧标签）。
- 关键细节（已对齐）：生成器第 0 帧 = BOS + 种子情绪嵌入；部位拼回 `merge_parts` 的 mouth/face 表情偏移分别是 3/6；VQ-VAE 的 ResBlock 用 `GroupNorm(8, 512)` + 扩张卷积(16,8,4,2,1)；`n_out` 是完整 LayerNorm。
- 权重导出：`export/export_motion.py`（.ckpt → manifest+weights.bin+stats.json，`--gen` 指向含 emo_head 的 ckpt），参考生成：`make_reference_motion.py`（额外输出 `emo_probs.bin`/`emo_ids.bin`）。

### 推理性能（本机 RTX 3090Ti / 2s 音频 → 60 帧，CPU 后端 SIMD+OpenBLAS）

| 阶段 | no 标量后端 | CPU 后端 + matmul 化 |
|---|---|---|
| featurizer(wav2vec2) | 109.7s | 1.84s |
| push_audio | 1.7s | 12ms |
| generator | 5.1s（85ms/帧）| 1.25s（**21ms/帧**，< 33ms 实时预算）|
| decode | 1.26s | 11ms |
| **RTF** | 58.9x | **1.56x**（串行；流式 featurizer/generator 流水线后 ≈ 0.9x 实时）|

- 后端：`CMakeLists.txt` 默认 CPU（`XHQM_DEVICE_CPU` + `TENSOR_WITH_CPU/SIMD/BLAS`），走 `tensor_backend_cpu`（多线程 + AVX2 SIMD + OpenBLAS）；`-DSOUL_EXP_CUDA=ON` 可编 GPU 后端（需 `-Xcompiler=/utf-8`，因 xhqm 的 `.cu` 含中文注释，否则 GBK 误解析吞行）。
- **GPU 结论**：流式小矩阵增量推理下 GPU 传输开销无收益——wav2vec2 forward gpu≈cpu（277 vs 270ms）、生成器小矩阵 GPU 慢 2x（46 vs 21ms/帧），故默认 CPU。

## 许可

- 权重：`facebook/wav2vec2-base` 为 Apache-2.0（商用前再核验 HF 卡片）；DINOv2 权重亦受 Meta 许可约束（商用前核验）。
- 代码：本库自研；xhqm 依赖见其仓库。
