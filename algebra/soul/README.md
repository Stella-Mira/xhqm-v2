# soul

> 最后同步：2026-10-07，对照工作副本 r677。

心智模块。定位：神经网络与学习相关能力（前向组件、自回归生成、reverse-mode 自动微分、扩散模型、循环网络），依赖 `xhqm::tensor`（张量）与 `xhqm::tensor_base`（线性代数 / 后端），AD 引擎额外依赖 `lattice`（图模板）与 `network/uuid`。

规模实测：17 个 header、合计 3997 行 —— `ad_ops.h` 1548 / `ad_io.h` 448 / `transformer.h` 387 / `ad_module.h` 298 / `diffusion.h` 198 / `unet.h` 182 / `serialize.h` 152 / `attention.h` 151 / `recurrent.h` 144 / `ad_node.h` 123 / `activation.h` 86 / `embedding.h` 82 / `random.h` 68 / `normalize.h` 56 / `linear.h` 46 / `soul.h` 18 / `ad.h` 10。

全部 header-only（对齐 tensor 模块的模板约定），命名空间 `xhqm::soul`（AD 在 `xhqm::soul::ad`，扩散在 `xhqm::soul::diffusion`）。两点使用前提：

- **不在聚合头里**：`xhqm.h` / `head.h` 均不包含本模块，必须显式 `#include "algebra/soul/soul.h"`（或只包含所需子头）。全仓唯一包含 `algebra/soul/soul.h` 的是 `test/soul_test.cpp:3`；`test/sport` 只包含 `activation.h`。
- **无开关宏**：模块内 0 处 `#if/#ifdef`（除头文件保护），根 `CMakeLists.txt` 的 `XHQM_BUILD_*` 也不含 soul（只列 tensor/analysis/network/fantasy/system）；但 AD 的 uuid 序列化在**链接期**需要 `network/uuid.cpp` + `network/md5.cpp`，tensor 后端需要 `algebra/tensor/backend/tensor_backend*.cpp`（见 `test/CMakeLists.txt:96-133`）。

## 已实现：前向神经网络组件 + Transformer（前向 + 生成）

| 阶段 | 文件 | 内容 |
|---|---|---|
| 1 | `random.h` | 确定性 RNG（xorshift32 + Box-Muller）与权重初始化（Xavier 均匀 / 正态填充），填充串行以保证可复现（`xorshift32` / `uniform01` / `normal01` / `fill_uniform` / `fill_normal` / `xavier_uniform`） |
| 2 | `activation.h` | `relu` / `gelu`（tanh 近似，c=0.7978845608028654、a=0.044715）/ `sigmoid` / `tanh_act` / `silu`，`softmax`（2D 沿最后一维、1D）；逐元素优先走后端 unary kernel（SIMD/GPU），失败回退 `parallel_for`；另有通用 `apply` |
| 3 | `normalize.h` | `layer_norm` / `rms_norm`（沿特征维归一化，作用于 (rows, features) 的 2D 张量，下沉到 `tensor_base`；eps 默认 1e-5） |
| 4 | `linear.h` | `linear`：`y = x·W^T + b`，W 存 (out, in)，缓存转置权重 `weight_t`，bias 初始化为 0 |
| 5 | `attention.h` | `attention`：多头因果自注意力（Q/K/V 投影 + 缩放点积 + 因果掩码 + 输出投影）；另含 `kv_cache` 与增量注意力（见下） |
| 6 | `embedding.h` | `embedding` 词嵌入查表（weight (vocab, hidden)，N(0, 0.02) 初始化）+ `sinusoidal_position` 正弦绝对位置编码 + `rope` 旋转位置编码（支持 `start_pos`，但全仓未接线，见 `DEBUG.md` E4） |
| 7 | `transformer.h` | `transformer_block`（pre-norm + 注意力 + MLP 残差）与 `transformer`（GPT 风格 decoder：`forward` / `sample` / `sample_nucleus` / `generate` / `generate_beam` / `generate_kv`） |
| 8 | `recurrent.h` | `rnn` / `lstm` / `gru`（scan 展开 + 权重节点共享；仅 `rnn` 有纯张量 `infer`） |
| 9 | `diffusion.h` | DDPM 扩散模型（线性调度 + MLP 去噪器 + 训练/采样闭环） |
| 10 | `unet.h` | UNet 去噪器 + 2D 图像扩散训练/采样闭环 |
| - | `soul.h` | 聚合头（`ad.h` 只聚合 AD：`ad_node.h` + `ad_ops.h` + `ad_io.h` + `ad_module.h`） |

### 注意力与 KV cache（`attention.h`）

- `attention::forward`：全量多头因果自注意力，单序列 (seq, hidden)，scale = 1/√head_dim，未来位掩码取 `-1e9`。
- `attention::project_kv` / `attend_cached`：只投影 K/V、对缓存 K/V 做增量缩放点积（q 看全部缓存，无因果掩码）。
- `transformer_block::forward_inc`：KV cache 增量前向，返回新隐藏 + 追加后的完整 K/V（= `block_inc{h,k,v}`）。
- `kv_cache`：按层缓存 K/V（`init(layers)` / `k(l)` / `v(l)` / `reset`），供自回归生成避免每步重算历史 token。
  - ⚠ `kv_cache::append` 当前**不可用**（空缓存首步无宽度 → 缓存退化为 (n,0)、token 静默丢弃），实际可用路径是 `forward_inc` 返回值回填 `cache.K[l] = r.k`。见 `DEBUG.md` A2 / G1。

### 生成策略（`transformer.h`）

- `sample`：temperature ≤ 0 贪心，否则 softmax 采样。
- `sample_nucleus`：top-k + top-p（nucleus）采样（top_k 为 0 或 ≥ n、top_p ≥ 1 表示不截断）。
- `generate`：滑动窗口自回归续写（窗口超过 `max_seq` 时保留末尾 `max_seq` 个）。
- `generate_beam`：束搜索（按累计对数似然保留全局最优 beam）。
- `generate_kv`：KV cache 增量自回归生成（与全量 forward 结果一致，`test/soul_test.cpp:223` 逐行对照 logits）；它**不做滑动窗口**，`pos >= max_seq` 时直接停止；prompt 填充段不检查长度（见 `DEBUG.md` A4）。

## 已实现：reverse-mode 自动微分（AD 引擎）

依赖 `algebra/tensor` 与 `lattice` 图模板，命名空间 `xhqm::soul::ad`：

| 文件 | 内容 |
|---|---|
| `serialize.h` | `serializable`（save_json/read_json 纯虚）→ `tensor_holder`（类型擦除：data/gradient/shape/dim）→ `tensor_payload<dim,type>`（持有 value + grad，json 存 dtype + shape + 原始字节序，CBOR byte string）；另有 `dtype_name` / `json_to_cbor` / `cbor_to_json` / `as_payload` |
| `ad_node.h` | `value<type>` 值节点（继承 `lattice::directed_node`，来/去连接 + uuid + 算子名 + 反向闭包）与 `tape`（拓扑排序 + 反向传播） |
| `ad_ops.h` | 算子与优化器（见下） |
| `ad_io.h` | 整图序列化：`save_tape` / `load_tape`（节点 + 连接 + 权重 + 算子超参），反向闭包按 op 名 + 超参重建（`reinstall_backward`） |
| `ad_module.h` | 可训练模块：`module` / `sgd_optimizer` / `adam_optimizer` / `adamw_optimizer` / `gpt` + `clip_grad_norm` / `cosine_warmup_lr` |
| `ad.h` | 聚合头 |
| `lattice/directed_mesh.hpp` | 扩写的图节点：区分「来连接」（谁指向我）与「去连接」（我指向谁），`adjacent_list()` 兼容 `topological_sort` |

### 算子清单（`ad_ops.h`）

~~基础：`make_value` / `add`(+标量广播) / `mul` / `mul_scalar` / `sum_all` / `mse` / `where`；激活：`relu` / `leaky_relu` / `sigmoid` / `silu` / `gelu` / `tanh` / `softmax` / `softmax_cross_entropy`；矩阵/形状：`matmul` / `transpose` / `slice_columns` / `concat_columns` / `unsqueeze` / `squeeze` / `flatten` / `unflatten`；注意力：`sdpa` / `attention`(单头) / `multi_head_attention`(多头) / `causal_mask` / `non_causal_mask`；卷积/归一化/池化（NCHW 4D）：`conv2d` / `conv2d_grouped` / `group_norm` / `layer_norm` / `max_pool2d` / `avg_pool2d` / `upsample_nearest2d` / `upsample_bilinear2d`；嵌入/正则：`embedding_lookup` / `dropout`；优化器：`sgd_step` / `momentum_step` / `adam_step`~~ **已作废（2026-10-07 复核 r677）**：旧清单把 `layer_norm` 划进 NCHW 4D 组（它是 (rows, cols) 沿最后一维的 2D 算子），且漏记 11 个公开算子 —— `conv1d` / `conv3d` / `conv_transpose2d` / `batch_norm` / `cross_attention` / `bce` / `nll` / `kl_div_gaussian` / `hinge_loss` / `focal_bce` / `info_nce`；实测 `ad_ops.h` 共 67 个函数定义（含 `add_impl` 等 13 个内部 `*_impl` 与 2 个 `make_value` 重载），其中向计算图写入 **42 个不同的算子名**（`grep -oE '"[a-z_0-9]+"\)' algebra/soul/ad_ops.h | sort -u | wc -l`），而 `load_tape` 的重建白名单只认 18 个（含 `leaf`）。补全后分组如下：

- 基础：`make_value`（含共享 payload 重载）/ `add`(+标量广播) / `add_scalar` / `mul` / `mul_scalar` / `sum_all` / `where` / `transpose` / `slice_columns` / `concat_columns` / `unsqueeze` / `squeeze` / `flatten` / `unflatten`
- 激活：`relu` / `leaky_relu`(slope 默认 0.01) / `sigmoid` / `silu` / `gelu` / `tanh` / `softmax`（2D）/ `dropout`（inverted）
- 损失：`mse` / `softmax_cross_entropy` / `bce` / `nll` / `hinge_loss` / `focal_bce` / `kl_div_gaussian` / `info_nce`(组合算子：matmul+transpose+mul_scalar+softmax_ce)
- 注意力：`sdpa` / `attention`(单头) / `multi_head_attention`(多头) / `cross_attention` / `causal_mask` / `non_causal_mask`
- 卷积（NCHW / NCDHW，含 bias）：`conv1d`(3D) / `conv2d`(4D) / `conv2d_grouped` / `conv3d`(5D) / `conv_transpose2d`（w 布局 (Cin,Cout,KH,KW)）
- 归一化 / 池化 / 上采样：`layer_norm`(2D) / `group_norm`(NCHW) / `batch_norm`(NCHW, 训练态统计量) / `max_pool2d` / `avg_pool2d` / `upsample_nearest2d` / `upsample_bilinear2d`
- 嵌入：`embedding_lookup`（前向 `tensor_base::gather` / 反向 `scatter_add`，ids 越界会抛错）
- 优化器（张量级，非 module 版）：`sgd_step` / `momentum_step`(mu 默认 0.9) / `adam_step`(b1 0.9 / b2 0.999 / eps 1e-8，含偏置修正)

注意：整图回读只认得其中 18 个算子名（见 `DEBUG.md` E1），`conv1d/2d/3d/conv_transpose2d/conv2d_grouped/silu/gelu/tanh/group_norm/batch_norm/pool/upsample/where/dropout/unsqueeze/squeeze/flatten/unflatten/bce/nll/hinge/focal_bce/kl_div_gaussian` 等不在重建白名单里。

### 训练闭环（`ad_module.h`）

- `module`：命名参数容器（`add_param` / `register_params` / `node` / `payload`），`save_json/read_json` 参数快照（state_dict，梯度不持久化）。
- 优化器：`sgd_optimizer` / `adam_optimizer`（含偏置修正，t 从 1 起）/ `adamw_optimizer`（解耦权重衰减，`param *= (1 - lr·wd)`）。
- 工具：`clip_grad_norm`（全局梯度范数裁剪，无返回值）、`cosine_warmup_lr`（cosine + warmup；**只有 f32 版本**，step_warmup/total 均为 `xhqm::size`）。
- `gpt`：GPT 解码器（embedding + 多头注意力 + layer_norm + FFN），参数名 `wte/wq/wk/wv/wo/w1/w2/wlm/g/b`，前向构图返回 softmax 交叉熵 loss（标量）。它**不含位置编码**、FFN 用 `relu`（不是 gelu），是最小可训练模型（见 `DEBUG.md` F6）。

### 设计

- **边方向 = 数据流**：`z = op(x,y)` 时 `x→z、y→z`，故 `z` 的来连接 = 输入；拓扑排序得正向计算顺序，逆序即反向梯度顺序（存在环时 `tape::backward` 抛 `xhqm::error("ad 计算图存在环")`）。
- ~~**类型擦除**：图内标量类型编译期统一（`value<type>`，默认 f32），维度（0/1/2/3/4）运行时擦除，逐元素算子按 `dim` 分派，`matmul` 固定 2D、`conv2d` 固定 4D 走 `tensor_base`。~~ **已作废（2026-10-07 复核 r677）**：实际维度是 0–5 —— `ad_ops.h:1305` 的 `conv3d` 用 `as_payload<5, type>`（tensor 侧 `conv3d` 声明为 `tensor<5, type>`），`silu/gelu/tanh` 的 dim 分派覆盖到 4；但 `matmul` 固定 2D、`conv2d` 固定 4D，且序列化载荷只到 0–2、优化器只到 0–4（`DEBUG.md` E1/E2）。逐元素算子按 `dim` 手工 switch。
- **循环 = 时间步展开**：AD 引擎是 DAG，RNN/LSTM/GRU 通过「时间步新建 hidden 节点 + 复用同一组权重节点」实现，权重梯度跨时间步自动累加。
- **序列化**：元数据（算子名/shape/dtype/uuid）与张量原始字节都进 CBOR byte string，不经过 float→double 转换；梯度不持久化。
- **tape 每步新建**：参数以 shared payload 跨 tape 持久，图节点随 tape 释放；同一 tape 反复 forward 会持续堆节点（`DEBUG.md` B1）。

### 用法

最小 include 与编译（`test/` 是独立 CMake 工程，`include_directories("..")`）：

```cpp
#include "algebra/soul/soul.h"        // 聚合头；AD 也可以只包含 "algebra/soul/ad.h"
using namespace xhqm;
using namespace xhqm::soul;
```

```bash
# 语法自检（本模块 5 个测试 TU 在 g++ 11.4.0 / -std=c++17 下全部通过）
g++ -std=c++17 -fsyntax-only -I. -Itest test/soul_ad_test.cpp
```

**前向 Transformer（单序列；参数序按声明）**

```cpp
uint32_t seed = 123;
transformer<f32> m;
m.init(16, 16, 2, 2, 32, 32, seed);            // vocab, hidden, layers, heads, max_seq, ffn, seed
auto logits = m.forward({ 1, 2, 3, 4 });         // (seq, vocab) = (4, 16)

tensor<1, f32> l1(std::array<xhqm::size, 1>{ 16 }, 0.0f);   // 单步 logits 供采样
uint32_t s = 99;
xhqm::size tok = m.sample(l1, 0.8f, s);                       // (logits, temperature, seed)：<=0 贪心
tok = m.sample_nucleus(l1, 1.0f, 3, 0.9f, s);                // (logits, temperature, top_k, top_p, seed)

uint32_t g1 = 1, g2 = 1, g3 = 1;
auto a = m.generate({ 1, 2 }, 8, 0.0f, g1);                        // (ids, max_new, temperature, seed)
auto b = m.generate_kv({ 1, 2 }, 6, 0.0f, 1.0f, 0, g2);           // (ids, max_new, temperature, top_p, top_k, seed)
auto c = m.generate_beam({ 1, 2 }, 6, 3, 0.5f, g3);               // (ids, max_new, beam_width, temperature, seed)
```

**KV cache 增量前向（`test/soul_test.cpp:233-249` 的真实写法）**

```cpp
kv_cache<f32> cache;
cache.init(m.layers);
std::vector<xhqm::size> ids = { 1, 2, 3, 4, 5 };
tensor<2, f32> h;                                  // 最后一个 token 的隐藏态
for (xhqm::size pos = 0; pos < ids.size(); ++pos)
{
    tensor<2, f32> xin = m.wte.forward(std::vector<xhqm::size>{ ids[pos] });
    for (xhqm::size j = 0; j < m.hidden; ++j) xin(0, j) += m.wpe(pos, j);
    for (xhqm::size l = 0; l < m.layers; ++l)
    {
        auto r = m.blocks[l].forward_inc(xin, cache.K[l], cache.V[l]);   // -> {h, k, v}
        xin = r.h; cache.K[l] = r.k; cache.V[l] = r.v;
    }
    h = xin;
}
tensor<2, f32> logits = m.lm_head.forward(m.ln_f.forward(h));          // (1, vocab)
// 不要用 kv_cache::append（当前损坏，见 DEBUG.md A2）
```

**AD：手写小图 + 反向 + 参数更新（`test/soul_ad_test.cpp` 的写法）**

```cpp
#include "algebra/soul/ad.h"
using namespace xhqm::soul::ad;

tape<f32> t;
auto* x = make_value<2, f32>(t, X);                     // 输入（叶子，无反向）
auto* y = make_value<2, f32>(t, Y);                     // 目标
auto* w = make_value<2, f32>(t, W, "leaf", "w");        // 命名叶子（回读后可按 name() 取回）
auto* pred = matmul(t, x, w);                           // 构图即算前向
auto* loss = mse(t, pred, y);                           // 标量 loss（全元素平均，ad_ops.h:273-278）
t.backward(loss);                                       // 从 loss 反向，梯度累加在各 payload 的 grad
auto* gw = as_payload<2, f32>(w->holder());
sgd_step(w, 0.05f);                                     // 或 momentum_step / adam_step / module + 优化器
```

**训练闭环（module + 优化器，`test/soul_ad_test.cpp:817-840`）**

```cpp
gpt<f32> model;
model.init(8, 16, 32, 2, seed);                         // vocab, hidden, ffn, heads, seed
adam_optimizer<f32> opt(0.01f);                          // lr, b1=0.9, b2=0.999, eps=1e-8
for (int e = 0; e < 300; ++e)
{
    tape<f32> t;                                        // 每步新建 tape（见 DEBUG.md B1/F）
    auto* loss = model.forward(t, input_ids, target_ids);   // 构图 + 注册参数，返回 loss 节点
    t.backward(loss);
    opt.step(model);                                    // 按参数名更新 payload 的 value
}
auto checkpoint = model.save_json();                    // 参数快照（state_dict，梯度不持久化）
gpt<f32> model2; model2.init(8, 16, 32, 2, seed); model2.read_json(checkpoint);
```

**循环网络（`test/soul_ad_test.cpp:1197-1232`）**

```cpp
rnn<f32> r; r.init(2, 2, seed);
tape<f32> tt;
auto* x0 = make_value<2, f32>(tt, X0);
auto* x1 = make_value<2, f32>(tt, X1);
auto* h0 = make_value<2, f32>(tt, H0);
auto* hf = r.forward(tt, { x0, x1 }, h0);              // 权重节点共享：梯度跨时间步累加
tt.backward(sum_all(tt, hf));
auto hid = r.infer(xs /*(seq,batch,input)*/, H0);       // 纯张量推理；lstm/gru 没有 infer（DEBUG.md E5）
```

### 整图序列化（checkpoint）

`save_tape` 把整张计算图（节点 uuid/算子名/参数名/算子超参/权重原始字节 + 输入连接）存成 json，`load_tape` 回读并重建反向闭包，梯度不持久化（回读后 `backward` 重新计算）。

```cpp
auto cbor = json_to_cbor(save_tape(t));             // std::vector<uint8_t>，落盘为二进制
tape<f32> t2 = load_tape<f32>(cbor_to_json(cbor));  // 权重 + 图结构 + 反向就绪
// 限制：载荷只支持 dim 0–2，算子名只支持 18 个（见 DEBUG.md E1）
```

约定：叶子参数用 `make_value(..., "leaf", "参数名")` 命名；`leaky_relu` 的 slope、`layer_norm` 的 eps、`softmax_cross_entropy` 的 target、`embedding_lookup` 的 ids、`slice_columns` 的 c0/c1 会自动存入节点 `params` 供重建反向。

### 训练闭环（module + optimizer）

`module` 持有命名参数（shared payload，value 跨 epoch 持久），`gpt` 前向把参数注册进 tape 并构图；优化器按名更新参数（按运行时 dim 分派，只支持 0–4 维）。

```cpp
gpt<f32> model; model.init(vocab, hidden, ffn, heads, seed);
adam_optimizer<f32> opt(0.01f);
for (int epoch = 0; epoch < N; ++epoch) {
    tape<f32> t;
    auto* loss = model.forward(t, input_ids, target_ids);   // 构图 + 注册参数
    t.backward(loss);                                        // 反向
    opt.step(model);                                         // 按名更新参数
}
auto checkpoint = model.save_json();                         // 参数快照（state_dict）
gpt<f32> model2; model2.init(vocab, hidden, ffn, heads, seed); model2.read_json(checkpoint);
```

## 已实现：扩散模型（DDPM + UNet，`diffusion.h` / `unet.h`）

命名空间 `xhqm::soul::diffusion`，复用时序/去噪思路：

- `linear_schedule<type>`：线性噪声调度（β_t / α_t / ᾱ_t），`init(T, beta_start = 1e-4, beta_end = 0.02)`；β 按下标线性插值（`frac = t/(T-1)`，`T == 1` 时 frac=0 → 全为 beta_start，`T == 0` 时数组为空且训练入口会整数除零，见 `DEBUG.md` A12）。
- `q_sample`：前向加噪 `x_t = √ᾱ_t·x0 + √(1-ᾱ_t)·ε`（纯张量，不进 AD，支持任意 dim）。
- `time_embedding`：~~正弦时间嵌入（`ω_k = 10000^(-2k/D)`）~~ **已作废（2026-10-07 复核 r677）**：实际相位是 `phase = (t/T)·100·ω_k`（`diffusion.h:86-90`，多一个 ×100 放大），且奇数维时末位直接填 `t/T`（`diffusion.h:94`）。
- `mlp_denoiser<type>`（玩具 2D 数据去噪器）：`concat(x, t_emb)` → 3 层 MLP（relu）→ ε 预测；`forward`（AD 构图）/ `infer`（纯张量）。`init(in_dim, hidden, time_dim, seed)`。
- `train_step`：采样 t/ε → 加噪 → 构图 → MSE → `backward` → `adam_optimizer.step`；**整批共享一个 t**（`diffusion.h:148-149`），返回 loss。
- `sample`：DDPM 反向采样，逐步去噪到 x_0（t=0 那一步不加噪 z）。
- `unet_denoiser<type>`：编码(conv2d+group_norm+silu → avg_pool 2×)×2 → 瓶颈 → 解码(upsample → conv2d+gn+silu)×2 → 输出 conv；组数固定 4、无 skip 连接，时间条件由调用方把时间嵌入广播成额外通道 concat 进输入（`broadcast_time4d` / `concat_channels4d`）。`init(in_ch, base_ch, time_dim, out_ch, seed)`。
- `train_step_image` / `sample_image`：2D 图像（NCHW）扩散训练/采样闭环；`sample_image(model, s, n, ch, H, W, seed)`。
- 纯张量工具：`tile_rows` / `concat_features` / `broadcast_time4d` / `concat_channels4d` / `xavier_conv4d`（fan_in = Cin·KH·KW）。

## 依赖

- `../tensor/tensor.h`（`xhqm::tensor` + `xhqm::tensor_base` 后端）；模块内 13 个头直接包含它
- `../../lattice/directed_mesh.hpp`、`../../lattice/graph_algorithms.hpp`（AD 图节点 / 拓扑排序）
- `../../network/uuid.h`（节点 uuid）
- nlohmann/json + CBOR（序列化，经 tensor 模块引入）
- 链接期额外 TU（见 `test/CMakeLists.txt:80-133`）：`algebra/ftype.cpp`、`algebra/ctype.cpp`、`algebra/tensor/backend/tensor_backend.cpp`、`tensor_backend_no.cpp`（图像扩散测试再加 `tensor_backend_cpu.cpp`）、`network/uuid.cpp`、`network/md5.cpp`

## 判断

成熟度：**中高** —— 前向组件、reverse-mode AD、循环网络、DDPM/UNet 都有可跑的闭环与数值对照测试；缺的是 AD checkpoint 覆盖面（只到 2D + 18 算子）、conv3d 的可训练性、批量注意力，以及 FlashAttention / 量化 / 混合精度 / 性能基准。当前问题清单见 `DEBUG.md`（44 条）。

- 前向 Transformer（含 KV cache 增量推理、nucleus/beam 采样）、reverse-mode AD、循环网络（RNN/LSTM/GRU）、扩散模型（DDPM/UNet）均已可用，训练闭环封装为 `module` + `optimizer` + `gpt` / `mlp_denoiser` / `unet_denoiser`；但 `kv_cache::append` 这个公开入口是坏的（`DEBUG.md` A2），`ad::gpt` 无位置编码（F6）。
- ~~整图 checkpoint（`save_tape` / `load_tape`）与参数快照（`module::save_json/read_json`）均可落盘/回读。~~ **已作废（2026-10-07 复核 r677）**：参数快照（state_dict）不受限；整图 checkpoint 只在「dim ≤ 2 且算子名在 18 个白名单内」（GPT/线性/序列化往返测试那种图）成立，UNet 图像图（4D + conv/silu/group_norm）回读会抛异常（`DEBUG.md` E1）。
- 优化器已齐 `sgd` / `momentum` / `adam` / `adamw`（含偏置修正与解耦权重衰减），配 `clip_grad_norm` / `cosine_warmup_lr`；张量级更新只支持 0–4 维（`DEBUG.md` E2），学习率调度只有 f32 版。
- 注意力已齐单头 `attention` 与多头 `multi_head_attention`（`slice_columns`/`concat_columns` 按头切拼），但都是单序列语义（共享 (S,S) 掩码），没有 batch>1 因果注意力（F1/E6）。
- 4D 卷积类算子（`conv2d`/`group_norm`/`pool`/`upsample`）前向下沉 `tensor_base`，反向在 AD 层直连或按窗口散射，支撑 UNet 图像扩散；其中 `max_pool2d` 反向在「窗口全落在 padding 内」时把梯度灌到元素 0（`DEBUG.md` A6）。
- ~~缺梯度检查~~（出自仓库根 `README.md:101`）**已作废（2026-10-07 复核 r677）**：梯度检查**是有的** —— `test/soul_ad_test.cpp` 38 个 `TEST_CASE` 里有 20 余处有限差分对照（matmul/relu/sigmoid/softmax/softmax_ce/layer_norm/embedding/attention/silu/gelu/tanh/group_norm/batch_norm/conv1d/conv2d/conv2d_grouped/conv3d/conv_transpose2d/rnn/lstm/hinge/focal）；库内确实没有 `grad_check` 工具函数，池化/上采样/where/dropout/unsqueeze/squeeze/flatten/unflatten 也没有梯度对照，只被 UNet 冒烟测试间接走到（`DEBUG.md` E7）。

测试：`test/` 下 5 个 TU、55 个 `TEST_CASE`（`soul_test` 9 / `soul_ad_test` 38 / `soul_graph_test` 4 / `soul_diffusion_test` 3 / `soul_diffusion_image_test` 1），覆盖前向组件与采样、AD 数值梯度与收敛、整图序列化往返、module 快照、扩散训练/采样；未覆盖的算子见 `DEBUG.md` E7。
