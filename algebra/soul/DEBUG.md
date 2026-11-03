# soul DEBUG — 当前仍存在的问题

`algebra/soul`（header-only 神经网络与学习库：前向组件 + GPT Transformer + reverse-mode AD + 循环网络 + DDPM/UNet）的**现存问题台账**；每条都带 `文件:行号` 证据，均已对照工作副本 r677 的源码复核。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
> 关系：本仓历史台账 `fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` 不搬家、不改名，仍在原处；本文件只覆盖 `algebra/soul`，模块使用说明见同目录 `README.md`。
> 复核手段：静态读码 + `g++ -std=c++17 -fsyntax-only`（5 个 soul 测试 TU 全部通过，g++ 11.4.0 / Linux）。**本轮未构建二进制、未运行测试**（约束：不新建任何文件），凡未实跑的条目标注为「未复现」并给出可直接跑的命令或应补的测试。
> 分组：A 功能性缺陷 · B 资源与并发 · C 跨平台 · D 性能 · E 未实现/占位桩 · F 当前设计取舍（不修） · G 文档与代码的出入。

## A. 功能性缺陷（会算错 / 会崩）

### `attention::init` 不校验 `hidden` 能否被 `heads` 整除，静默截断输出；`heads == 0` 直接整数除零

- 位置：`algebra/soul/attention.h:28`（`head_dim = hidden / heads`）、`attention.h:41-71`（只写 `heads*head_dim` 列）、`attention.h:85-108`；`transformer::init` 同样不校验：`algebra/soul/transformer.h:105-123`
- 现状：`head_dim` 用整数除法取整。`hidden=10, heads=4` → `head_dim=2`，逐头写回的列只覆盖 0..7，输出列 8、9 保持初始化值 0，q/k/v 的 8、9 列永不参与计算，全程无报错。`heads=0` 时 `hidden/0` 是 UB（SIGFPE）。
- 证据：`attention.h:69` `out(r, h * head_dim + c) = oh(r, c);` 的上界是 `heads*head_dim`，注释只写了「要求 hidden 能被 heads 整除」（`attention.h:28`）而没有检查。对照：AD 侧 `ad::multi_head_attention` 会抛错（`ad_ops.h:599`）、`gpt::init` 会抛错（`ad_module.h:252`）。
- 影响：任何 `hidden % heads != 0`（或 `heads=0`）的调用者拿到错误的 logits，且没有异常/断言提示；测试只覆盖 `hidden=8, heads=2`（`test/soul_test.cpp:104`）与 `16, 2`（`test/soul_test.cpp:145`），覆盖不到。
- 复现/验证：未复现。建议补测试：`attention<f32> a; a.init(10, 4, seed);` 后断言抛错。
- 建议：`init` 里加 `if (heads == 0 || hidden % heads != 0) throw xhqm::error(...);`（与前向/AD 两侧保持一致）。

### `kv_cache::append` 对刚 `init` 的空缓存取不到列宽，首 token 被静默丢弃、缓存退化成 (n,0)

- 位置：`algebra/soul/attention.h:119-142`（关键行 `132`：`const xhqm::size seq = K[l].dimension(0), d = K[l].dimension(1);`；`135`：`nk(seq, j) = k_new[l](0, j);`）、`attention.h:125`（`init` 用默认构造张量填充）
- 现状：`init(layers)` 填的是 `tensor<2,type>()`，其 `m_counts{}` 全 0（`algebra/tensor/tensor_base.h:129`），所以首步 `d = dimension(1) = 0`：`nk` 被建成 (1,0)，写 `k_new` 的循环一次都不执行，`k_new[l]` 的值从未入缓存；下一步 `seq=1, d=0` → (2,0)，永远没有数据。
- 证据：同一文件里被真正使用的消费者 `transformer.h:80-81` 走 `concat_rows`，而 `concat_rows` 恰恰对空缓存做了特判（`transformer.h:26-27`：`d = (seq == 0) ? row.dimension(1) : a.dimension(1)`）——即修法已在旁边；`kv_cache::append` 是没人接线的公开 API（`transformer.h::generate_kv` 用 `forward_inc` 返回值回填，`transformer.h:349-353`）。
- 影响：任何照 `attention.h:113-115` 注释搭 KV cache 的人都会得到 (n,0) 缓存；随后 `attend_cached` 会按 (n,0) 取数（`attention.h:97-98` → `tensor::operator()` → `store::operator[]` 无边界检查，`algebra/tensor/store.h:32`）→ 越界读（UB）。
- 复现/验证：未复现（静态确证）。可跑：在 `test/soul_test.cpp`「test_generate_kv_consistency」旁加 `kv_cache<f32> c; c.init(1); c.append({k}, {v});` 后断言 `c.k(0).dimension(1) == hidden`。
- 建议：`append` 用 `k_new[l].dimension(1)` 定宽（对齐 `concat_rows`），或空缓存时直接 `K[l] = k_new[l]`。

### 形状不兼容时张量层返回空张量而非报错，AD 图会静默携带空载荷，并可能在优化器处越界读

- 位置：`algebra/tensor/linalg.h:38-40`（`matmul` 内维不符 → `return xhqm::tensor<2, type>();`）、`algebra/tensor/tensor_base.h:91`（`broadcast_binary` 不可广播 → `return tensor();`）、`tensor_base.h:227-229`（`this_binary` 不可广播 → 静默无操作）
- 现状：soul 的整个前向/反向都建立在这两个运算上（`ad_ops.h:47` `matmul`、`ad_ops.h:67/74-75` 的 `pa->value + pb->value` / `pa->grad + po->grad`、`attention.h:59`、`diffusion.h:78/189`、`unet.h:173`）。一旦某步形状写错，结果是「空张量」而不是异常：空张量继续当正常载荷入图，梯度累加变成空加空，最终 `param -= lr*grad` 静默不动（`this_binary` 的 no-op 分支），或在 `adam_step` 里按 `param.size()` 索引空的 grad → 越界读写（`ad_ops.h:652-660`；`ad_ops.h:183` 是调用点）。
- 证据：`linalg.h:38` 的注释自己写明「否则返回空（防越界读）」——这是 tensor 层的既定约定；soul 侧没有任何一处校验形状（全模块 grep 无 `shape() !=` / `dimension(` 断言式检查，只有 `ad::multi_head_attention` 的整除检查与 `unflatten` 的尺寸检查 `ad_ops.h:960`）。
- 影响：形状错误的训练不会崩，而是表现为「loss 不变 / 输出全 0 / 偶发越界」，极难定位；对首次使用者最典型（`seq`/`hidden` 不一致、`linear` 的 `in_features` 写错）。
- 复现/验证：未复现。可跑：`matmul` 两个内维不一致的 2D 张量 → 断言返回 `size()==0`；再在 `mse` 后接 `adam_step` 观察越界。
- 建议：至少在 `ad::matmul` / `ad::add` / `ad::mul` 入口校验输入形状并抛 `xhqm::error`。

### `generate_kv` 的 prompt 填充段不检查长度，prompt 超过 `max_seq` 会越界读位置编码表

- 位置：`algebra/soul/transformer.h:342-346`（`for (i = 0; i < ids.size(); ++i) ... xin(0, j) += wpe(i, j);`）
- 现状：生成段有防护（`transformer.h:359-360`：`if (pos >= max_seq) break;`），`generate` 也有滑动窗口（`transformer.h:257-259`），唯独 prompt 填充段直接把 `ids` 的长度当成位置索引，而 `wpe` 只有 `(max_seq, hidden)` 行。
- 证据：`transformer.h:112` `wpe = sinusoidal_position<type>(max_seq, hidden);`，消费点 `transformer.h:345-346` 无上界；`tensor::operator()` → `store::operator[]` 无边界检查（`algebra/tensor/store.h:32`）。
- 影响：`generate_kv(ids, ...)` 传长于 `max_seq` 的 prompt → 越界读（UB，可能读到相邻内存或崩溃），无任何提示。
- 复现/验证：未复现。可跑：`gpt.init(16,16,2,2,4,32,seed)`（`max_seq=4`）后调 `generate_kv({1,2,3,4,5}, 1, 0.0f, 1.0f, 0, s)`。
- 建议：填充段加 `const xhqm::size n = min(ids.size(), max_seq)`，或直接抛错。

### `embedding::forward` 不校验 token id，越界 id 静默越界读（AD 侧同名算子会抛错）

- 位置：`algebra/soul/embedding.h:30-38`（`x(i, j) = weight(ids[i], j);`）
- 现状：前向查表直接按 id 索引 `(vocab, hidden)` 权重，无 `ids[i] < vocab` 检查。
- 证据：对照 AD 侧同一功能 `embedding_lookup` 走 `tensor_base::gather`，那里会抛 —— `algebra/tensor/nn.h:42` `if (ids[i] >= vocab) throw xhqm::error("gather ids 越界");`；`weight(ids[i], j)` 落到无检查的 `store::operator[]`（`algebra/tensor/store.h:32`）。
- 影响：外部传入的 token id（prompt、batch 数据）越界时读到无关内存并继续前向（UB），而不是像 AD 侧那样抛错。
- 复现/验证：未复现。可跑：`embedding<f32> e; e.init(4, 8, seed); e.forward({9});`。
- 建议：前向侧加同样的越界检查（或直接改调 `tensor_base::gather`）。

### `max_pool2d` 反向：窗口完全落在 padding 内时，梯度被灌到元素 0

- 位置：`algebra/soul/ad_ops.h:742-757`（`bool first = true; type acc = 0; xhqm::index ai = 0;`，越界元素 `continue`）、`ad_ops.h:761-766`（反向 `px->grad[idx[i]] = xhqm::sum(px->grad[idx[i]], po->grad[i]);`）
- 现状：若某输出窗口内的 k×k 元素全部越界，`first` 一直为 true，`acc`/`ai` 保持初值 → `idx` 记成 0；反向时该窗口的输出梯度被加到输入的第 0 个元素上（正确语义应是「无输入对应，直接丢弃」）。
- 证据：`ad_ops.h:752-753` 只在 `first` 内更新 `ai`；`avg_pool2d` 的同位反向有防护（`ad_ops.h:800` `if (cnt == 0) continue;`），`max_pool2d` 没有；`tensor_base::max_pool2d` 的前向对同窗口也输出 0（`algebra/tensor/pool.h:95-107`）。
- 影响：`padding > 0` 且窗口全在 padding（例如 k=3、stride=1、padding=2 的角窗口）时梯度错误；`max_pool2d` 在 5 个测试 TU 里 0 命中（见 E7），不会被测试发现。
- 复现/验证：未复现。可跑：对 `x(1,1,2,2)` 做 `max_pool2d(t, x, 3, 1, 2)` + `sum_all` + `backward`，与有限差分对照。
- 建议：`first` 仍为 true 时把该窗口标记为「无梯度」（`idx` 置 -1 并在反向跳过）。

### 二输入损失不校验 target 尺寸，按 pred 元素数读取对方内存

- 位置：`algebra/soul/ad_ops.h:271-294`（`mse`：`n = pred->holder()->size()` 在 `273`，读 `td[i]` 在 `277`）、`ad_ops.h:1149-1180`（`bce`：`1151`/`1155-1159`）、`ad_ops.h:1184-1202`（`nll`：`1187`/`1189`/反向 `1199`）、`ad_ops.h:1458-1489`（`hinge_loss`：`1462-1469`）、`ad_ops.h:1491-1527`（`focal_bce`：`1495-1503`）、`ad_ops.h:1204-1234`（`kl_div_gaussian`：`1208`/`1214`）
- 现状：这些算子只按第一个输入的 `size()` 循环，并用同一个下标去读第二个输入的原始指针，没有 dim/shape/元素数比较。
- 证据：`mse` 里 `const xhqm::size n = pred->holder()->size();` 之后直接 `pd[i] - td[i]`；对照 `softmax_cross_entropy` 的 target 校验其实在 tensor 层（`algebra/tensor/nn.h:94` 长度、`nn.h:98` 越界），说明同仓已有校验先例。
- 影响：target 给短了/形状给错（例如把 `(B,1)` 的标签配 `(B,D)` 的预测）→ 越界读（UB），不会报错。
- 复现/验证：未复现。可跑：`mse(t, pred /*(2,2)*/, target /*(2,1)*/)`。
- 建议：六个损失入口统一加 `holder()->size()`/`shape()` 一致性检查并抛错。

### 归一化的方差用 `E[x²]-E[x]²` 一次归约，数值抵消可能得到负方差 → `sqrt` 出 NaN

- 位置：`algebra/tensor/norm.h:75-77`（`layer_norm` 前向，`soul::layer_norm` 走这条：`algebra/soul/normalize.h:30`）、`algebra/tensor/norm.h:136`（`group_norm` 前向）、`norm.h:167`（`batch_norm` 前向）、`algebra/soul/ad_ops.h:1078`（`group_norm` 反向）、`ad_ops.h:1354`（`batch_norm` 反向）
- 现状：这些位置先累 `sum`/`sum2` 再算 `var = sum2/n - mean²`；输入均值大、方差小（fp32 里 mean≈1e4、var≈1 时 `sum2/n`≈1e8，舍入误差已达 10 量级）时 `var` 可能为负 → `sqrt(var + eps)` = NaN → 整网 NaN。注意 `ad::layer_norm` 的前向/反向是两趟写法（`ad_ops.h:361-367`、`ad_ops.h:389-392`），不受此影响，于是同一算子在前向推理（走 tensor 层）与 AD 训练（手写）之间还有公式差异（与 F4 同源）。
- 证据：`norm.h:76` `type var = xhqm::sub(xhqm::div(sumx2.value(...), type(n)), xhqm::mul(mean, mean));`；`ad_ops.h:1078` `const type var = xhqm::sub(xhqm::div(sum2, type(gsz)), xhqm::mul(mean, mean));`；前后向用同一公式，所以不会「前反向不一致」，而是同时 NaN。
- 影响：训练出现 NaN 时容易被误判为学习率问题；触发需要输入数值大且集中（图像/嵌入量级正常时踩不到）。
- 复现/验证：未复现（测试数值都很小：`test/soul_ad_test.cpp:278` 起的 layer_norm 用例、`test/soul_ad_test.cpp:915` 起的 group_norm 用例）。可跑：对 `x + 1e4` 做 `group_norm` 前向，断言 `isfinite`。
- 建议：`var` 改两趟（先 mean 再 Σ(x-mean)²）或 `var = max(var, 0)`。

### `tape::backward` 不校验 loss 是标量，只给第一个元素播种 1

- 位置：`algebra/soul/ad_node.h:77-81`（`seed_grad_one`：`g[0] = type(1);`）、`ad_node.h:108-116`（`backward` 不检查 `loss->dim()`）
- 现状：把任意节点当 loss 传进去都不会报错；非标量 loss 只有第 0 个元素被当作 `dL/dloss = 1`，其余元素的种子是 0，梯度静默错误。
- 证据：`seed_grad_one` 只写 `g[0]`；`backward` 里 `loss->seed_grad_one();` 前没有任何 `dim()==0` 检查。
- 影响：把「逐元素 loss 向量」误当标量 loss 使用时不报错、梯度错（例如忘记 `sum_all`/`mse` 收口）。
- 复现/验证：未复现。可跑：`t.backward(y)`（`y` 为 (2,2) 的 `relu` 输出）与 `t.backward(sum_all(t, y))` 比较梯度。
- 建议：`backward` 里加 `if (loss->dim() != 0) throw xhqm::error(...)`。

### 优化器动量槽按参数名复用且不校验形状，模型形状变化后越界读写

- 位置：`algebra/soul/ad_module.h:96-102`（`std::map<std::string, slot> slots;`）、`ad_module.h:176-185`（`slot& s = slots[p.name]; if (!s.m) { ...zero_like... }`）、`ad_ops.h:643-661`（`adam_step` 按 `param.size()` 循环读写 `m`/`vv`）
- 现状：槽位只在「空」时按当前参数形状创建一次，此后同一个名字永远复用；`adam_step` 的循环上界取自参数本身，不比较 `m`/`vv` 的尺寸。
- 证据：`ad_module.h:182` 的 `if (!s.m)` 之后没有任何尺寸核对；`ad_module.h:108-120` 的 `zero_like` 只在创建时按 `src->shape()` 建。
- 影响：复用同一个优化器时模型变大（`model.init(...)` 换尺寸、或 `read_json` 读进另一套形状的 state_dict）→ `m[i]`/`vv[i]` 越界读写（UB）。
- 复现/验证：未复现。可跑：`adam_optimizer<f32> opt; gpt<f32> a; a.init(6,8,16,2,seed); /*step*/; a.init(6,32,64,2,seed); /*step*/`。
- 建议：槽位键加入 `(dim, shape)` 指纹，或在 `step` 里尺寸不符时抛错/重建。

### `module::read_json` 不做参数名校验，缺参数时 `node()` 返回 nullptr → forward 解引用空指针

- 位置：`algebra/soul/ad_module.h:69-79`（`read_json`：清空 `params` 后按 json 顺序重建，`param.node` 保持 `nullptr`）、`ad_module.h:45-49`（`node()` 未命中返回 `nullptr`）、消费点例如 `ad_ops.h:45`（`a->holder()`）
- 现状：`read_json` 只按 json 里的名字/载荷重建参数，不比对模型期望的名字集合与形状；`forward` 里 `this->node("w2")` 之类的取用不做空指针检查。
- 证据：`ad_module.h:72-77` 直接 `params.push_back(...)`；`ad_module.h:47` `for (auto& p : params) if (p.name == name) return p.node;` 后 `return nullptr;`；`unet.h:108-109`、`mlp_denoiser::infer`（`diffusion.h:132-134`）同样直接解引用 `payload()`/`as_payload`。
- 影响：把结构不同的 state_dict（参数名不同/更少）读进模型后，下一次 `forward`/`infer` 段错误；形状不同但名字相同时不会崩，而是走 A3 的「空张量静默退化」。
- 复现/验证：未复现。可跑：`gpt<f32> m; m.init(6,8,16,2,seed); m.read_json(nlohmann::json::array()); m.forward(t, in, tgt);`。
- 建议：`read_json` 后按名匹配并校验 dim/shape，缺名或尺寸不符时抛 `xhqm::error`。

### 未初始化（或 `T = 0`）的调度会让 `train_step` 整数除零；`sample` 则静默返回纯噪声

- 位置：`algebra/soul/diffusion.h:149`（`xhqm::size(uniform01(seed) * float(s.T)) % s.T`）、`algebra/soul/unet.h:133`（同款写法）、`diffusion.h:47`（`xhqm::size T = 0;` 默认值）、`diffusion.h:178`（`sample` 的 `for (xhqm::size t = s.T; t-- > 0;)`）
- 现状：`linear_schedule` 默认 `T = 0`；忘记 `init()`（或 `init(0)`）就调 `train_step`/`train_step_image` 时，取模的右操作数是 0 → 整数除零（UB，x86 上是 SIGFPE 崩溃）。`sample` 在同条件下不崩（循环体一次都不执行），直接返回初始高斯噪声。
- 证据：`diffusion.h:149` 与 `unet.h:133` 的 `% s.T` 无条件执行；两处测试都先 `init(100)`（`test/soul_diffusion_test.cpp:65`、`test/soul_diffusion_image_test.cpp:37`），覆盖不到该路径。
- 影响：忘记初始化调度（或想用 T=0 表示「无扩散」）时，训练侧直接崩溃、采样侧静默给出无意义结果，报错点都不指向「调度没 init」。
- 复现/验证：未复现。可跑：`diffusion::linear_schedule<f32> s; diffusion::mlp_denoiser<f32> m; m.init(2, 8, 4, seed); ad::adam_optimizer<f32> opt; diffusion::train_step(m, opt, s, x0, seed);`（此时 `s.T == 0`）。
- 建议：`train_step`/`train_step_image`/`sample` 入口校验 `s.T > 0` 并抛错。

## B. 资源与并发

### `tape` 只增不删，且每次 forward 重新注册参数节点：同一个 tape 复用就无上限增长

- 位置：`algebra/soul/ad_node.h:88-97`（`new_value` 只 `nodes.push_back`，无移除接口）、`ad_module.h:39-43`（`register_params` 每次 forward 为每个参数新建一个叶子节点）、`ad_node.h:110-116`（`backward` 每次对**全表** `all()` 做拓扑排序与 `zero_grad`）
- 现状：`tape` 是节点唯一所有者，没有 `clear()/reset()`；`module::forward` 会重复注册同名参数（共享 payload，所以数值仍正确），节点数与边数随 forward 次数线性增长。
- 证据：`ad_node.h:95` `nodes.push_back(std::move(v));`；`ad_module.h:41-42` 无「已注册则跳过」判断；`ad_node.h:110-112` 每次都 `topological_sort(all())` + 逐节点 `zero_grad()`。
- 影响：把 `tape` 建在训练循环外（常见误用）时内存只增不减，且每次 backward 的排序/清零开销随循环次数上涨，表现为「越训越慢」。
- 复现/验证：未复现。可跑：把 `test/soul_ad_test.cpp:829-837` 的循环改成 `tape` 建在循环外，观察 `t.all().size()` 每轮增长。
- 建议：给 `tape` 加 `clear()`（或在文档/断言里明确「每步新建 tape」）。

### 优化器动量槽只增不减，且同名参数共用同一个槽位

- 位置：`algebra/soul/ad_module.h:96-102`、`ad_module.h:181-183`（`adam_optimizer`）、`ad_module.h:193-194`/`206-208`（`adamw_optimizer`）
- 现状：`slots` 是 `std::map<std::string, slot>`，永不删除；`module::add_param` 也不查重名，因此两个同名参数会共用一份 `m`/`v`。
- 证据：`ad_module.h:181` `slot& s = slots[p.name];`——以名字为唯一键；`ad_module.h:31-36`（`add_param`）没有重名检查。
- 影响：反复 `init()`/换模型时旧槽位残留（内存与 A10 的越界隐患）；同名参数静默共享动量（更新错误）。
- 复现/验证：未复现。可跑：`module<f32> m; m.add_param("w", A); m.add_param("w", B);` 后 `adam_optimizer::step` 两次，比较两个参数的变化。
- 建议：`add_param` 拒绝重名；槽位按 `(name, dim, shape)` 键控或提供 `reset()`。

### `module::param::node` 是裸指针，跨 tape 生命周期悬垂（`payload()` 安全、`node()` 不安全）

- 位置：`algebra/soul/ad_module.h:26`（`value<type>* node = nullptr;`）、`ad_module.h:42`（指向 tape 内节点）、`ad_module.h:45-49`（`node()` 把该指针交出去）；节点生命周期见 `ad_node.h:88-97`
- 现状：参数 payload 是 `shared_ptr`（跨 tape 持久，安全），但它对应的图节点归 `tape` 独有；tape 析构后 `p.node` 变成悬垂指针，`node(name)` 仍会把它返回。
- 证据：`ad_module.h:25-26` 两种所有权紧挨着定义；`ad_module.h:47` 无生命周期校验；模块内 `payload()`（`ad_module.h:50-54`）返回的是 shared payload，因此不危险。
- 影响：在 `forward` 之外（例如想手动接一条边到参数节点）使用 `node()` 时可能 UAF；`train_step` 一类封装（如 `diffusion.h:159-167`）只要保持 tape 存活就没事，属于「不按约定使用才踩」。
- 复现/验证：未复现。可跑：`auto* n = model.node("w1");` 在 `tape` 作用域结束后调用。
- 建议：约定 `node()` 只在 forward 内使用（在注释/文档中写明），或改为返回 weak 句柄并在命中失效时抛错。

### 共享 payload 的 AD 反向不是线程安全的（同一 module 并发 backward 会竞争梯度）

- 位置：`algebra/soul/ad_module.h:25`（参数 payload 跨 tape 共享）、`ad_node.h:108-116`（反向就地累加 `grad`）
- 现状：反向闭包全部是「读别人的 grad、就地累加到自己的 grad」；同一 module 的参数被多个 tape 共享，模块内 0 处 `mutex`/`atomic`/`thread`（全模块 grep 无命中）。
- 证据：`ad_node.h:114-115` 逆序调用 `backward_fn`；闭包体内的 `pa->grad = pa->grad + ...`（`ad_ops.h:55-56` 等）全是 `std::vector` 就地写；`ad_module.h:41-42` 把同一个 payload 同时交给多个 tape。
- 影响：多线程各自建 tape 训练同一个 `module` 会产生数据竞争（梯度丢失/撕裂），且没有任何文档提示。
- 复现/验证：未复现（需多线程 + TSan 才能确证）。可跑：两个线程各建 tape 调 `module::forward`/`backward`。
- 建议：在 README/注释里明确「一个 module 同一时刻只允许一个线程做 backward」，或给 payload 加锁/每线程副本。

## C. 跨平台

### 确定性与可复现性依赖 libm 实现，跨编译器/平台不保证逐位一致，且未声明

- 位置：`algebra/soul/random.h:31-37`（`normal01` 用 `std::sqrt/std::log/std::cos`）、`random.h:14-27`（xorshift32 + `[0,1)` 均匀）；同类 `std::pow/std::sin/std::cos` 用法见 `embedding.h:49-54`、`embedding.h:69-74`、`diffusion.h:89-92`、`ad_module.h:216-218`
- 现状：整型 RNG 是可复现的，但正态采样/位置编码/学习率曲线经过 libm 的超越函数；不同 libm（glibc / MSVC UCRT / Apple libm）与不同优化档（`-ffast-math`、SVML）在最后几位会不同。
- 证据：`random.h:36` `return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265358979f * u2);`；同进程同 seed 的一致性有测试（`test/soul_test.cpp:153-157`：重新 init 后逐元素 1e-5），但**没有任何跨平台对照测试**。
- 影响：换平台/换编译器后，同 seed 的权重与生成结果会漂移（`test/soul_test.cpp:161-166` 那种「同 seed 生成结果相同」的断言跨平台可能失败）；与 PyTorch 对照类测试（`test/sport`）必须留容差。
- 复现/验证：同机可复现已验证（`test/soul_test.cpp` 的确定性用例）；跨平台未确认（本机只有 g++ 11.4.0/Linux）。
- 建议：在文档里写明「可复现性限于同一工具链」，或为跨平台逐位一致另提供定点/查表实现。

### `fld`（long double）与多字节类型按原始字节序列化，跨 ABI / 字节序不可移植

- 位置：`algebra/soul/serialize.h:19-29`（`dtype_name`：f16/f32/f64/fld/unknown）、`serialize.h:91-95`（`bytes = value.size() * sizeof(type)` 原始字节入 CBOR）、`serialize.h:118-121`（读回时校验字节数）；类型别名见 `algebra/ftype.h:63`/`ftype.h:68`（`float_ld = long double`）
- 现状：json 只记 dtype 名 + shape + 原始字节，不记 `sizeof(type)`，也没有字节序字段。
- 证据：`serialize.h:91-94` 用 `sizeof(type)`；`serialize.h:119-120` `if (raw.size() != bytes) throw xhqm::error("tensor_payload 字节数不匹配");`。
- 影响：`long double` 在 MSVC 上是 8 字节、Linux x86-64 上是 16 字节，同一份 `fld` 载荷跨平台读回会抛「字节数不匹配」（显式失败，不是静默错值）；大端平台上的 f16/f32/f64 则会静默按错字节序解释。
- 复现/验证：未复现（本机只有一种 ABI）。可跑：在 Linux 存一个 `tensor_payload<1, fld>`，在 MSVC 上读回。
- 建议：元数据里补 `sizeof`（或位宽）+ 字节序标记；或在文档中限定 checkpoint 只在同一 ABI/字节序内交换。

## D. 性能

### 前向注意力/Transformer 是标量三重循环，未走 BLAS 批量化，也没有算子融合

- 位置：`algebra/soul/attention.h:44-71`（逐 head：拆 q/k/v、`matmul`、逐元素乘 scale 与掩码、`softmax`、`matmul`、写回）、`attention.h:87-108`（增量版同样逐 head 拆装）、`transformer.h:56-62`（block 逐层）、`transformer.h:130-132`（逐元素加位置编码）
- 现状：只有 `matmul` 下沉到后端（`tensor_base::matmul`），缩放/掩码/softmax/逐 head 拆分拼装全在标量循环里，每个 head 一次 `tensor` 分配与两次数值拷贝。
- 证据：`attention.h:49-55`（三重复制循环）、`attention.h:58-63`（标量缩放 + 掩码）、`attention.h:67-69`（写回）。
- 影响：序列变长（seq 数百）后前向/生成明显变慢；多头数越多拆分开销越大。
- 复现/验证：未复现（无基准，见 D5）。可跑：对 `seq ∈ {64,256,1024}` 计 `attention::forward` 耗时。
- 建议：把「(seq, heads*head_dim) → (heads, seq, head_dim)」做成一次批量 GEMM + 批量 softmax（或直接上 fused kernel）。

### KV cache 每步整块重分配拷贝，生成 N 步是 O(N²·hidden) 搬运

- 位置：`algebra/soul/transformer.h:24-35`（`concat_rows` 每次都 `tensor<2,type> r(seq+1, d)` 全量复制）、`transformer.h:80-81`（每层每 token 调一次）、`transformer.h:349-353`/`373-377`（生成循环内每 token 每层两次）、`attention.h:128-142`（`kv_cache::append` 同样是整块重建）
- 现状：KV cache 用「每次新建 + 全量拷贝」实现追加，没有预留容量，也没有 `reserve/push` 式的追加。
- 证据：`transformer.h:28-34`（两轮拷贝循环）；`attention.h:133-139`（同样两轮）。
- 影响：`generate_kv` 生成长文本时拷贝量按步数平方增长，抵消了一部分 KV cache 本该省下的重算收益。
- 复现/验证：未复现。可跑：`max_new ∈ {64, 512}` 计 `generate_kv` 耗时并与全量 `forward` 对照。
- 建议：缓存改为「预留 max_seq 行 + 有效长度计数」的视图，追加只写一行。

### AD 每个算子都新建节点与两份张量，backward 每次全图排序 + 全图清零

- 位置：`algebra/soul/ad_ops.h:47`/`67`/`128` 等（每个算子 `make_value` 生成值 + 梯度两份 `tensor`）、`ad_node.h:88-97`（节点分配）、`ad_node.h:110-116`（每次 `topological_sort(all())` + 对所有节点 `zero_grad()`）
- 现状：`tensor_payload` 持有 `value` 与 `grad` 两个张量（`serialize.h:58-59`），反向闭包按节点捕获；`backward` 的排序/清零范围是整张 tape（含与 loss 无关的节点）。
- 证据：`serialize.h:58-62`（`value` + `grad` 双张量，构造时即分配）；`ad_node.h:110-112`（全表排序、全表清零）。
- 影响：小模型（`test/soul_ad_test.cpp` 里那种）没问题；图大时每步的常数开销与内存明显。
- 复现/验证：未复现。可跑：统计 `tape.all().size()` 与单步耗时随 hidden/layers 的增长。
- 建议：给 `tape` 加节点复用/池化，或让 `backward` 只遍历 loss 的可达子图。

### 部分反向是标量循环且重复计算前向已算过的量

- 位置：`algebra/soul/ad_ops.h:1059-1112`（`group_norm` 反向：逐元素三重循环，重新累 `sum/sum2` 与逐通道 dgamma/dbeta）、`ad_ops.h:1337-1385`（`batch_norm` 反向同构）、`ad_ops.h:327-345`（`softmax_cross_entropy` 反向重新算一遍 softmax，而前向已由 `tensor_base::softmax_cross_entropy` 算过）、`ad_ops.h:376-412`（`layer_norm` 反向重算 mean/var）
- 现状：这些闭包都是朴素循环实现，没有缓存前向中间量（如 softmax 概率、归一化统计量），也没有走后端并行 kernel。
- 证据：`ad_ops.h:1066-1077`（两轮统计量重算）、`ad_ops.h:335-343`（`std::exp` 重算 + 逐元素除法）、`ad_ops.h:386-392`。
- 影响：图像扩散（UNet 每步 5 个 group_norm）这类场景反向占大头，且统计量被算两遍。
- 复现/验证：未复现。可跑：对比 `group_norm` 前向与反向的耗时（`test/soul_ad_test.cpp:915` 那套形状太小看不出来）。
- 建议：反向闭包捕获前向统计量（或把归一化反向下沉到 `tensor_base` 复用其前向结果）。

### 模块内没有性能基准

- 位置：`test/CMakeLists.txt:80-133`（soul 的 5 个目标全是功能/数值测试）、`algebra/soul/*.h`（无 bench 入口）
- 现状：模块没有 benchmark 目标；仓库里的 `test/sport/bench_speed.cpp` 测的是 `soul_exp` 自己的实现（`test/sport/include/soul_exp/nn.h:6-7` 只复用 `soul` 的 `activation.h`），`test/cellsim/cell_bench.cpp` 与本模块无关。
- 证据：`grep -rniE "bench|benchmark" test/CMakeLists.txt` 无 soul 相关命中；`find . -iname "*bench*"` 只有上述两个文件。
- 影响：改动注意力/KV cache/反向实现后没有回归基线，D1-D4 的性能问题只能靠感觉判断。
- 复现/验证：`find . -iname "*bench*" -not -path "./.svn/*"`（本轮已跑）。
- 建议：给 `transformer::forward` / `generate_kv` / `gpt` 训练一步加最小基准（参照 `test/sport/test/bench_speed.cpp` 的计时写法）。

## E. 未实现 / 占位桩

### 整图 checkpoint 只覆盖 dim 0-2 与 18 个算子名，卷积/UNet 图回读会抛异常

- 位置：`algebra/soul/ad_io.h:400-409`（`make_payload` 的 switch 只有 case 0/1/2，`default` 抛「load_tape 不支持该维度」）、`ad_io.h:111-372`（`reinstall_backward` 认识的算子名）、`ad_io.h:372`（`throw xhqm::error("reinstall_backward 未知算子: " + op);`）
- 现状：`ad_ops.h` 向前向图写入 42 个算子名（`grep -oE '"[a-z_0-9]+"\)' algebra/soul/ad_ops.h | sort -u`），`reinstall_backward` 只处理 18 个（含 `leaf`）；以下 25 个名字回读必抛：`avg_pool2d`、`batch_norm`、`bce`、`conv1d`、`conv2d`、`conv2d_grouped`、`conv3d`、`conv_transpose2d`、`dropout`、`flatten`、`focal_bce`、`gelu`、`group_norm`、`hinge`、`kl_div_gaussian`、`max_pool2d`、`nll`、`silu`、`squeeze`、`tanh`、`unflatten`、`unsqueeze`、`upsample_bilinear2d`、`upsample_nearest2d`、`where`；任何 3D/4D/5D 载荷（含全部卷积）在 `make_payload` 就抛。
- 证据：`ad_io.h:402` `switch (j.at("dim").get<xhqm::size>())` 后只有 0/1/2；`ad_io.h:115-372` 的算子名清单（`grep -oE 'op == "[a-z_0-9]+"' algebra/soul/ad_io.h`）与 `ad_ops.h` 的名单做过 diff。**编译器能过、只是没写完**：异常是显式失败，不会静默给出错权重。
- 影响：只有「2D + 白名单算子」的图能 checkpoint（`test_graph_roundtrip`/`test_multi_head_attention_roundtrip` 正是这类，`test/soul_ad_test.cpp:650`/`760`）；`unet_denoiser` 训练出的图像扩散模型（4D + conv2d/silu/group_norm）无法保存回读；`ad::gpt` 恰好落在白名单内（`relu` + `layer_norm` + `matmul` + `embedding` + `softmax_ce`）所以能通关。
- 复现/验证：未复现。可跑：对 `unet_denoiser::forward` 得到的 loss 调 `save_tape` 再 `load_tape`，观察抛错；或直接调 `load_tape` 于含 `silu` 节点的 2D 图。
- 建议：`make_payload` 补 3/4/5 维，`reinstall_backward` 补齐 25 个算子（或改成「按算子注册表」的可扩展形式）。

### `conv3d` 只能前向/反向，5D 参数无法被任何优化器更新（也不能回读）

- 位置：`algebra/soul/ad_ops.h:1302-1325`（`conv3d` 用 `as_payload<5, type>`，权重是 5D）、`ad_module.h:107-120`（`zero_like` 只到 case 4）、`ad_module.h:122-133`（`sgd_apply` 只到 4）、`ad_module.h:135-151`（`adam_apply` 只到 4）、`ad_module.h:154-165`（`decay_apply` 只到 4）
- 现状：`module::add_param` 是模板，5D 参数能加进来，但任何 `step()` 都会在 `default:` 分支抛「不支持该维度」；序列化也不支持 5D（E1）。
- 证据：`ad_module.h:118/131/149/163` 四个 `default: throw`；`test/soul_ad_test.cpp:1086-1103` 的 `test_grad_conv3d` 只做数值梯度，没有优化器/往返路径。**编译器能过、只是没接上**。
- 影响：`conv3d` 实际只能用于「自己拿着 grad 手写更新」的场景；用 `module` + 优化器的流程走不通。
- 复现/验证：未复现。可跑：`module<f32> m; m.add_param("w", tensor<5,f32>(...));` 后 `sgd_optimizer::step(m)`。
- 建议：把四个 `*_apply`/`zero_like` 扩到 5（或按 shape 扁平化处理），并在文档里注明 conv3d 的当前边界。

### 反向重建的 dim 分派用 `default` 兜底为 0 维，一旦放开高维载荷就是类型混淆

- 位置：`algebra/soul/ad_io.h:132-138`（`add`）、`ad_io.h:149-158`（`mul`）、`ad_io.h:159-168`（`relu`）、`ad_io.h:169-179`（`leaky_relu`）、`ad_io.h:180-189`（`sigmoid`）
- 现状：这些 `switch (node->dim())` 只列 `case 2/1`，`default` 一律 `detail::install_*<0, type>(...)`；由于 `make_payload` 目前只放行 0-2 维，`default` 可达值只有 0，因此**当前等价**。
- 证据：`ad_io.h:136` `default: detail::install_add<0, type>(a, b, out); return;`；配合 `ad_io.h:402-408` 的维度限制，3/4 维节点在更早处就抛错。
- 影响：将来按 E1 放开 3D/4D 载荷后，4D 的 `add` 节点会按 `as_payload<0, type>` 强转（`serialize.h:139-142` 是 `static_cast`，无运行期校验）→ 读到完全无关的内存（UB），而不是报错。
- 复现/验证：未复现（当前不可达）。可跑：放开 `make_payload` 的 3D 分支后回读一个 3D `add` 图。
- 建议：与 E1 一并改（显式列出 0-4 维，`default` 抛错而不是兜底 0 维）。

### `rope` 已实现但全仓没有调用点（transformer 用的是正弦绝对位置编码）

- 位置：`algebra/soul/embedding.h:62-77`（`rope` 定义，含 `start_pos`）
- 现状：`grep -rn "rope(" --include=*.h --include=*.hpp --include=*.cpp .` 只命中定义处这一行；`transformer` 用的是 `wpe = sinusoidal_position(...)`（`transformer.h:112`，消费点 `transformer.h:130-132`、`345-346`、`369-370`），AD 侧 `gpt` 干脆没有位置编码（F6）。
- 证据：全仓 grep（本轮已跑）唯一命中 `algebra/soul/embedding.h:62`。
- 影响：README/根 README 里「RoPE」容易被读成已接线能力（G3）；想让 `transformer` 用 RoPE 需要自己改造 `attention`（当前 `attention::forward` 只吃 x，没有位置参数）。
- 复现/验证：`grep -rn "rope(" --include=*.h --include=*.hpp --include=*.cpp . | grep -v "^./.svn"`（本轮已跑）。
- 建议：要么把它接进 `attention/transformer`，要么在文档里明确标注「可用但未接线」。

### 只有 `rnn` 有纯张量推理路径，`lstm`/`gru` 以及三者的「全序列输出」都缺

- 位置：`algebra/soul/recurrent.h:47-62`（`rnn::infer`）、`recurrent.h:82-100`（`lstm::forward` 只有建图版）、`recurrent.h:119-138`（`gru::forward` 只有建图版）、`recurrent.h:31-44`/`82-100`/`119-138`（都只返回最后一步的 h / {h,c}）
- 现状：`lstm`/`gru` 没有 `infer`，想推理也必须建 `tape` 并注册参数；三个结构的 `forward` 都只交出最后时刻状态，没有逐时间步输出。
- 证据：`recurrent.h:47` 是模块内唯一的 `infer`；`recurrent.h:43` `return h;`、`recurrent.h:99` `return { h, c };`。
- 影响：序列标注/teacher-forcing 类用法需要自己再展开一遍；推理侧没有「不建图」的快路径（性能与内存都吃亏）。
- 复现/验证：未复现。可跑：`lstm<f32> m; m.infer(...)`（不存在该成员）。
- 建议：补 `lstm/gru::infer` 与可选的「返回全序列」重载。

### 没有 batch>1 的因果注意力，也没有 FlashAttention / 量化 / 混合精度设施

- 位置：`algebra/soul/attention.h:5`/`attention.h:35-71`（前向注意力固定 (seq, hidden)）、`ad_ops.h:589-612`（`multi_head_attention` 的 mask 是共享 (S,S)，x 是 2D → 单序列语义）、`ad_ops.h:559-569`（`sdpa`）
- 现状：AD 侧的多头注意力虽然接受任意 2D 行数，但掩码是 (S,S) 一张共享表，把 batch 折进行维就会让不同样本互看，因此不具备真正的 batch>1 因果注意力；全模块 grep `flash`/`quant`/`int8`/`autocast` 0 命中（`f16` 只作为序列化 dtype 名出现：`serialize.h:23`）。
- 证据：`ad_ops.h:590-593` 签名里的 `const tensor<2, type>& mask`；`attention.h:37` `const xhqm::size seq = x.dimension(0);`。**编译器能过、只是没实现**。
- 影响：批训练只能走「卷积/归一化/损失的批量维 + 逐样本循环做注意力」；显存/混合精度优化无路可走。
- 复现/验证：`grep -rniE "flash|quant|int8|autocast" algebra/soul/*.h`（本轮已跑，0 命中）。
- 建议：如要做批量注意力，需要 (B,H,S,D) 的 4D 路径与块状掩码；量化/混合精度是独立课题，建议在 README 里保持「未实现」表述。

### 验证缺口：8 个算子 0 测试覆盖，且库内没有梯度检查工具

- 位置：`test/soul_ad_test.cpp`（38 个 `TEST_CASE`）、`test/soul_test.cpp`（9）、`test/soul_graph_test.cpp`（4）、`test/soul_diffusion_test.cpp`（3）、`test/soul_diffusion_image_test.cpp`（1）
- 现状：`max_pool2d`、`upsample_bilinear2d`、`where`、`dropout`、`unsqueeze`、`squeeze`、`flatten`、`unflatten` 在 5 个 TU 里 0 命中（`avg_pool2d`/`upsample_nearest2d` 仅被 UNet 冒烟测试间接走到）；有限差分检查是在测试文件里逐个算子手写的（如 `test/soul_ad_test.cpp:1100-1102`），`algebra/soul` 里没有 `grad_check` 之类的工具函数。
- 证据：`grep -n "max_pool2d\|avg_pool2d\|upsample\|dropout\|unsqueeze\|squeeze(\|flatten\|unflatten\|where(" test/soul_ad_test.cpp` 无命中；`grep -rn "grad_check" algebra/soul` 无命中。
- 影响：A6（max_pool2d 反向）这类缺陷不会被测试拦下；新增算子要复制粘贴一整套有限差分样板，容易漏测。
- 复现/验证：`grep -c 'TEST_CASE(' test/soul_*.cpp`（本轮已跑：9/38/4/3/1）；上面两条 grep 本轮已跑。
- 建议：补这 8 个算子的数值梯度用例，并把有限差分抽成 `ad::grad_check` 工具。

## F. 当前设计取舍（**不修**，写明理由与代价）

### 前向注意力/Transformer 只支持单序列（batch = 1）

- 位置：`algebra/soul/transformer.h:5`、`transformer.h:126-137`、`attention.h:5`、`attention.h:35-71`
- 现状：接口统一成 2D `(seq, hidden)`，KV cache、采样、位置编码都只按一个序列写，代码最短、行为最可预测；训练侧真正的批量维交给 AD（`conv2d`/`group_norm`/损失都按 `dimension(0)=N` 批处理，`test/soul_diffusion_image_test.cpp:34-45` 用 batch=16）。
- 证据：`transformer.h:5`「单序列（batch=1）前向与自回归生成」、`attention.h:5`「输入输出均为 (seq, hidden) 的 2D 张量（单序列，batch=1）」；训练侧批量确实存在：`test/soul_diffusion_image_test.cpp:34-45` 用 batch=16。
- 影响：前向侧批量推理要自己循环；AD 的注意力同样只有单序列（E6），批内样本多了只能逐样本算注意力；与主流 (B,H,S,D) 接口不兼容（迁移代码要改写）。
- 复现/验证：不适用（设计取舍，F 组**不修**）；代价与理由见上。
- 建议：这是接口层面的既定选择，改它等于重写 `attention`/`transformer` 与 KV cache，不建议在 r677 上动。

### 逐元素算子按 `dim` 手工 `switch` 分派，且各算子的维度覆盖集合不一致

- 位置：`ad_ops.h:101-106`（`add_scalar` 只 1/2）、`ad_ops.h:538-543`（`mul_scalar` 只 1/2）、`ad_ops.h:175-181`（`relu` 0/1/2）、`ad_ops.h:716-722`（`dropout` 0/1/2）、`ad_ops.h:901-907`（`unsqueeze` 0/1/2）、`ad_ops.h:928-933`（`squeeze` 只 1/2）、`ad_ops.h:998-1006`/`1036-1044`/`1254-1262`（`silu`/`gelu`/`tanh` 0-4）
- 现状：类型擦除后 `dim` 是运行期值，必须有一个显式分派点；手写展开（`add_impl<2>` / `<1>` / `<0>`）比模板递归/`std::variant` 更直接，编译期就能展开全部循环，且新人一眼能看懂。
- 证据：`ad_ops.h:5-6` 的设计声明「维度运行时擦除，逐元素算子按 dim 分派」，以及各算子 switch 的 case 集合差异（`ad_ops.h:101-106` 的 1/2 维 vs `ad_ops.h:998-1006` 的 0-4 维）。
- 影响：加一个受支持的维度要同时改三处 —— `ad_ops.h`（算子）、`ad_io.h`（回读重建的 switch）、`ad_module.h`（优化器的 `*_apply`/`zero_like`）；各处 case 集合不统一（同一份 `tanh` 支持 0-4，而同为逐元素的 `add_scalar` 只到 2），调用者只能靠试或读码，5D 更是只在 `conv3d` 单开一个洞（E2）。
- 复现/验证：不适用（设计取舍，F 组**不修**）；各算子 case 集合可 `grep -n "switch (x->dim())" algebra/soul/ad_ops.h` 复核。
- 建议：分派点本身保留；如果要动手，方向是「一处定义维度集合，其余由它生成」，而不是逐个算子补 case。

### 反向闭包有两份实现（前向安装 + 回读重建），靠人工保持同步

- 位置：`ad_ops.h:50-57` ↔ `ad_io.h:120-127`（`matmul`）、`ad_ops.h:70-76` ↔ `ad_io.h:22-28`（`add`）、`ad_ops.h:303-314` ↔ `ad_io.h:223-235`（`softmax`）、`ad_ops.h:376-412` ↔ `ad_io.h:266-301`（`layer_norm`，逐字重复）等
- 现状：`std::function` 闭包不可序列化，回读时必须按「算子名 + 超参」重建（`ad_io.h:6-7` 的注释写明了这一点），所以第二份实现无法避免。
- 证据：`ad_io.h:6-7`「反向闭包 std::function 不可序列化，故回读后按 op 名 + 超参重新安装（reinstall_backward）」、`ad_io.h:15-16`「下述 install_* 与 ad_ops.h 中对应 backward 闭包逐字等价」——后者是人工承诺，没有机制保证。
- 影响：同一段数学有两处副本，改一处忘一处时**只有 `load_tape` 之后的反向会错**，而现有测试只覆盖白名单里的图（`test/soul_ad_test.cpp:650`/`760`），漂移很难被发现；`ad_io.h:16` 那句「与 ad_ops.h 中对应 backward 闭包逐字等价」是人工承诺，没有机制保证。
- 复现/验证：不适用（设计取舍，F 组**不修**）；两处实现的差集可用 E1 的名单对比复核。
- 建议：保留双份，但改 `ad_ops.h` 反向时必须在同一提交里同步 `ad_io.h`（可加注释交叉引用）。

### 前向模块与 AD 算子是两套数学实现

- 位置：`normalize.h:28-31`（`layer_norm::forward` 下沉 `tensor_base::layer_norm`）↔ `ad_ops.h:355-371`（AD 里手写了一遍前向）↔ `ad_io.h:266-301`（回读再写一遍反向）；`attention.h:35-72` ↔ `ad_ops.h:589-612`（多头注意力两套）
- 现状：前向侧要「纯张量、不建图」的推理快路径（`generate`/`infer`），AD 侧要在图里插节点，两者共用同一份实现反而要引入「是否建图」的分支；`normalize.h:5-6` 的注释也说明它只保留薄封装。
- 证据：`normalize.h:5-6`「计算已下沉到 tensor_base::layer_norm / rms_norm，本层仅保留参数与薄封装」；同款双份还见于 `diffusion.h:117-127`（AD `forward`）与 `diffusion.h:130-139`（纯张量 `infer`）、`unet.h:86-103` 与 `unet.h:106-124`。
- 影响：同一算子存在两处数值（例如 `layer_norm` 的方差：tensor 层用 `E[x²]-E[x]²` 一趟 `norm.h:75-77`，AD 用手写两趟 `ad_ops.h:361-367`）→ 极端数值下前向推理与 AD 训练的归一化结果不完全一致（A8）；改公式要记得两边都改。
- 复现/验证：不适用（设计取舍，F 组**不修**）；`layer_norm` 两处公式的差异见 A8。
- 建议：双实现是性能/清晰度的取舍，保留；但要意识到「同一个算子的两处数值不等价」。

### `tape::backward` 每次清零全图梯度并播种 1，跨 tape 的梯度累积不可行

- 位置：`ad_node.h:108-116`（`for (auto* v : order) v->zero_grad();` 然后 `loss->seed_grad_one();`）、`ad_module.h:25`（参数 payload 跨 tape 共享）
- 现状：单次 `backward` 语义干净 —— 用户不需要自己 `zero_grad`，也不会被上一次的残留梯度污染；这也让「前向 → 反向 → step」的循环最好写（`test/soul_ad_test.cpp:829-837`）。
- 证据：`ad_module.h:5-6` 注释写明「参数为 shared payload：value 跨 epoch 持久，grad 由 backward 填充」；`test/soul_ad_test.cpp:829-837` 是每步新建 tape 的标准写法。
- 影响：`module` 参数是 **shared payload**，第二个 tape 的 `backward` 会把第一个 tape 辛苦累积的 grad 清零，因此 micro-batch 梯度累积（多个小 batch 累加后 step 一次）在当前语义下做不到，只能「backward 后立刻 step」。
- 复现/验证：不适用（设计取舍，F 组**不修**）；想确认累积不可行可对同一 module 连做两次 `backward`（第二次会把第一次的 grad 清零）。
- 建议：语义保留；若要累积，需要显式引入「每参数一份外部 grad 累加器」的接口，属于新功能而非修 bug。

### `ad::gpt` 是最小可训练模型：无位置编码、FFN 用 `relu`、无 dropout

- 位置：`ad_module.h:268-291`（`embedding_lookup` → `multi_head_attention` + `causal_mask` → `add` → `layer_norm` → `matmul(relu(...))` → `matmul(wlm)` → `softmax_cross_entropy`）
- 现状：它是 AD 通路的端到端冒烟/收敛体（`test_gpt_train`、`test_gpt_module` 用 300 步把 loss 压到 0.3 以下），刻意只留「可训练参数 + 交叉熵」这条最短链路，避免位置编码/正则的引入把 AD 的数值问题盖住；位置编码能力已由前向侧 `transformer` 提供（`transformer.h:112`）。
- 证据：`ad_module.h:242-243` 的类注释只承诺「embedding + 多头注意力 + layer_norm + FFN」；前向实现（`ad_module.h:282-291`）里没有任何位置编码调用。
- 影响：不能当语言模型用 —— 位置信息只能靠因果掩码间接表达，长序列上等价于 bag-of-tokens；要做真 transformer 训练，得自己往 `ad::gpt::forward` 里加位置嵌入（或改用 `embedding.h` 的 `rope`，但它也没接线，E4）。
- 复现/验证：不适用（设计取舍，F 组**不修**）；缺位置编码的后果已由 `gpt` 无 wpe 调用（`ad_module.h:282-291`）直接可见。
- 建议：保持最小模型定位，但在文档里写清「这是 AD 冒烟模型，不是 `transformer` 的等价实现」。

### UNet 去噪器固定 4 组归一化、无 skip 连接、时间条件靠调用方拼通道、上采样用 nearest

- 位置：`algebra/soul/unet.h:60`/`65`（`gn_groups = 4` 硬编码）、`unet.h:86-103`（前向：d1 → avg_pool → d2 → avg_pool → md → upsample → u1 → upsample → u2 → oc，无跨层拼接）、`unet.h:20-29`（`broadcast_time4d`）、`unet.h:140-141`（调用方自己 concat 时间通道）
- 现状：玩具/小图扩散（`test/soul_diffusion_image_test.cpp:34` 用 1×16×16、base_ch=16）只需要一个能跑通训练闭环的去噪器；时间条件固定在纯张量层做，UNet 内部不感知时间，接口保持「纯 (N,C,H,W) → (N,C,H,W)」，AD 与 `infer` 两条路径都好写。
- 证据：`unet.h:6`「时间条件：把正弦时间嵌入广播成额外通道…（由调用方在纯张量层完成），UNet 内部不感知时间」；`unet.h:60` 与 `unet.h:65` 两处把组数写死为 4。
- 影响：`base_ch`（以及 `2*base_ch`）必须是 4 的倍数，否则 `tensor_base::group_norm` 会抛「group_norm groups 须整除通道数」（`algebra/tensor/norm.h:120`）—— 报错来得晚且指向 tensor 层，容易让人困惑；它也不是严格意义的 U-Net（没有编码器→解码器的跳连，细节重建能力弱）；上采样用 nearest（`upsample_nearest2d`）而非转置卷积，`conv_transpose2d` 虽已实现但在这里没被用上（`ad_ops.h:1406`）。
- 复现/验证：不适用（设计取舍，F 组**不修**）；`base_ch` 不被 4 整除时的报错来自 `algebra/tensor/norm.h:120`，可用 `unet_denoiser::init(1, 3, 8, 1, seed)` + 一次前向复现。
- 建议：定位「可训练的小图扩散范例」即可；若要当生产去噪器，需要参数化 `gn_groups`、加 skip 连接并换掉上采样。

### RNG 填充串行执行（可复现优先）

- 位置：`algebra/soul/random.h:4-5`（注释即理由）、`random.h:41-53`（`fill_uniform`/`fill_normal` 单线程循环）、`random.h:14-22`（有状态 xorshift32）
- 现状：`xorshift32` 是有状态递推，并行填充要么产生数据竞争、要么破坏「同 seed 同结果」的可复现性；串行填充让 `test/soul_test.cpp:153-157` 这类确定性断言得以成立。
- 证据：`random.h:4-5` 的注释即理由「填充一律串行（RNG 有状态，并行会数据竞争且破坏可复现性）」；`test/soul_test.cpp:153-157` 依赖该性质做「同 seed 重新 init 输出一致」的断言。
- 影响：大权重初始化（例如 vocab 很大的 embedding）是纯单线程，初始化耗时随参数量线性增长；且可复现性只保证同一工具链（C1）。tensor 侧其实有并行版每元素独立 RNG（`algebra/tensor/random.h:47-63` 的 `rand_uniform/rand_normal`），soul 没有采用。
- 复现/验证：不适用（设计取舍，F 组**不修**）；串行填充的位置即 `random.h:41-63` 的普通 for 循环。
- 建议：保留串行语义；如要提速，可改用 tensor 侧的「每元素从 (seed, index) 派生状态」方案（但会改变同 seed 的数值，属于破坏兼容的改动）。

### 序列化只存 dtype/shape/原始字节，梯度不持久化

- 位置：`serialize.h:5-6`/`83-96`（原始字节进 CBOR byte string，不走 float→double）、`serialize.h:123`（读回时 `grad` 重建为零）、`ad_io.h:6-7`（闭包不可序列化，回读后重建）
- 现状：raw bytes 保真且紧凑（浮点经 json number 会掉精度/膨胀），`std::function` 本来就没法序列化，所以图结构用「算子名 + 超参」重建。
- 证据：`serialize.h:5-6`「data 走 CBOR byte string，不经过 float→double 转换，紧凑且保真」；`serialize.h:123` 读回时显式把 `grad` 重建为零。
- 影响：跨 ABI/字节序不可移植（C2）；回读后的图必须先跑一次 `backward` 才拿到梯度（不能「存档含梯度」）；算子白名单必须跟着 `ad_ops.h` 维护（E1/E3）。
- 复现/验证：不适用（设计取舍，F 组**不修**）；跨 ABI 的失败形态见 C2（抛「字节数不匹配」）。
- 建议：设计正确，代价需要写进文档（已在 README 的「整图序列化」小节标注限制）。

## G. 文档与代码的出入

### 代码注释教用户用 `kv_cache::append`，而 `append` 当前是坏的

- 位置：`algebra/soul/attention.h:113-115`（注释：「用法：每生成一个新 token，把各层的 K/V（(1, hidden)）append 进来；取 k(layer)/v(layer) 得完整序列 K/V 供 sdpa 用」）
- 现状：注释描述的用法在上述前提下会得到 (n,0) 的空缓存（根因见 A2）；仓库内真正被使用的路径是 `forward_inc` + `cache.K[l] = r.k`（`transformer.h:349-353`、`test/soul_test.cpp:242-245`）。
- 证据：`attention.h:113-115` 的注释 vs `attention.h:130-141` 的实现。
- 影响：照注释写的新代码会静默拿到空 K/V，随后在 `attend_cached`/`sdpa` 处越界读。
- 复现/验证：见 A2 的复现步骤（同一处）。
- 建议：修 A2，或先把注释改成「用 `forward_inc` 的回填」并标注 `append` 待修。

### 根 `README.md:101`「缺梯度检查」与测试现状不符（本模块 README 无权改根档）

- 位置：`README.md:101`（仓库根，原文：「**缺失**：缺 batch>1 注意力；暂无数值量化/混合精度/算子融合；缺梯度检查/单测覆盖说明与 benchmark。」）
- 现状：梯度检查**存在**——`test/soul_ad_test.cpp` 38 个 `TEST_CASE` 中有 20 余处有限差分对照（`test/soul_ad_test.cpp:46`/`84`/`118`/`237`/`273`/`313`/`1100-1102` 等）；「单测覆盖说明」也存在（`test/README.md:45-49` 逐目标列出覆盖点）。库内确实没有 `grad_check` 工具函数（E7），`benchmark` 也确实缺（D5）。
- 证据：`grep -c 'TEST_CASE(' test/soul_ad_test.cpp` = 38；`grep -n "CHECK_NEAR(.*grad\|数值梯度" test/soul_ad_test.cpp` 多处命中；`test/README.md:45-49` 的表格。
- 影响：接手的人会以为 AD 没有数值验证，重复造轮子或低估测试覆盖。
- 复现/验证：`grep -n "数值梯度" test/soul_ad_test.cpp | wc -l`（本轮已跑）。
- 建议：根 README 订正为「缺库内 grad_check 工具与性能基准；数值梯度检查在 test/soul_ad_test.cpp」，或标注为已作废（本模块 README 不能改根档，故记在此）。

### 根 `README.md:99` 的「KV-cache + RoPE」把未接线的 `rope` 算成已有能力

- 位置：`README.md:99`（仓库根，原文：「生成策略丰富（贪心/nucleus/束搜索/KV-cache + RoPE）」）
- 现状：`rope` 只有定义（`algebra/soul/embedding.h:62-77`），全仓没有调用点；`transformer` 用的是 `sinusoidal_position`（`transformer.h:112`）。
- 证据：`grep -rn "rope(" --include=*.h --include=*.hpp --include=*.cpp .` 仅命中定义处（本轮已跑）。
- 影响：读者会以为 RoPE 已接进生成路径，实际生成用的是正弦绝对位置编码。
- 复现/验证：同上的 grep（本轮已跑）。
- 建议：根 README 改为「KV-cache + （未接线的）RoPE」，或先接线（E4）。

### `ad_ops.h` 的头注释把维度说成 0/1/2，与文件内的 3/4/5 维算子矛盾

- 位置：`algebra/soul/ad_ops.h:5`（「标量类型编译期确定（type），维度运行时擦除（0/1/2，逐元素算子按 dim 分派）」）
- 现状：同文件里 `conv1d` 走 3D（`ad_ops.h:1274-1298`）、`conv2d` 走 4D（`ad_ops.h:1119-1145`）、`conv3d` 走 5D（`ad_ops.h:1302-1325`）、`silu`/`gelu`/`tanh` 的 dim 分派覆盖到 4（`ad_ops.h:998-1006`/`1036-1044`/`1254-1262`）。
- 证据：注释行与上述 `as_payload<3/4/5, type>` 调用点；同族注释 `ad_io.h:398` 写的是「dim 运行时 0/1/2 分派」，那句与 `ad_io.h:402-408` 的实现**一致**，只有 `ad_ops.h:5` 过时。
- 影响：只读头注释会低估算子维度，或误以为 `conv2d` 不可用。
- 复现/验证：`grep -n "as_payload<5\|as_payload<4\|as_payload<3" algebra/soul/ad_ops.h`（本轮已跑）。
- 建议：注释改成「维度运行时擦除（0-5，序列化载荷只到 2，优化器只到 4）」。

### 根 `CMakeLists.txt` 的 soul 头清单缺 5 个文件，`xhqm.vcxproj` 里本模块 0 命中

- 位置：`CMakeLists.txt:296-307`（`XHQM_HEAD_TENSOR` 中的 soul 段，共 12 个）、`xhqm.vcxproj`（grep `soul` 0 命中）
- 现状：清单列了 random/activation/normalize/linear/attention/embedding/transformer/serialize/ad_node/ad_ops/ad/soul，缺 `ad_io.h`、`ad_module.h`、`diffusion.h`、`unet.h`、`recurrent.h`（这 5 个都是被 `soul.h`/`ad.h` 包含的头，所以编译不受影响，`xhqm.h` 也不含本模块）。
- 证据：`grep -c soul xhqm.vcxproj` = 0；`CMakeLists.txt` 里 soul 段只有 12 条；仓库无 `install()` 规则，所以目前只是工程视图/清单缺文件。
- 影响：IDE/工程视图里看不到 AD 序列化、module、扩散、UNet、循环网络的 5 个头文件；将来若按该清单做安装/打包，会漏发这 5 个头。
- 复现/验证：`grep -n "algebra/soul/" CMakeLists.txt`、`grep -c soul xhqm.vcxproj`（本轮已跑）。
- 建议：清单补齐这 5 个；MSVC 工程若要显示头文件，也在 `xhqm.vcxproj` 里补上。
