# test — xhqm 引擎测试与实验/查看器模块

> 最后同步：2026-10-07，对照工作副本 r677。

xhqm（C++ 数学 / 渲染 / 网络 / 系统引擎）的 **测试与验证层**，位于 `xhqm/test/`。它不是一个库，而是引擎各子模块（algebra / tensor / soul / lattice / software / network / hardware / metaclass / astral / fantasy）的**可执行验证套件**，并附带四个各自带 `project()` 的子工程，外加一棵没有构建入口的接口树：

| 子目录 | 定位 | 构建入口 |
|---|---|---|
| `test/`（根目录） | Catch2 单元测试套件，覆盖引擎核心库的行为与数值正确性 | `test/CMakeLists.txt`，25 个 `add_executable` |
| `sport/` | `soul-exp` 实验库：用 xhqm 的 tensor/soul 能力做 **wav2vec2 / DINOv2 / sport(motion-gen)** 模型推理移植，含 PyTorch 权重导出脚本与逐值对照测试 | `sport/CMakeLists.txt`（`project(soul_exp)`），6 个可执行 |
| `cellsim/` | 单细胞模拟器（电导型突触 / 五类通道 / 分流抑制 / 分级输出），只依赖标准库，被 `ffbo/` 当作可替换细胞模型 include | `cellsim/CMakeLists.txt`（`project(cellsim)`），1 个可执行 `cellsim_bench` |
| `ffbo/` | 数字果蝇：MaleCNS 连接组 → 肌肉 → MuJoCo → fantasy（`mcns_net` 只跑网络、`fly_mcns_view` 全通路） | `ffbo/CMakeLists.txt`（`project(digital_fruit_fly)`），2 个可执行 + `archive/` 归档 |
| `view/` | `RhiViewport` 交互式 3D 渲染查看器（QWindow 对称壳，Vulkan / OpenGL 双后端）+ `ModelLoader`（assimp 模型/骨骼动画加载） | `view/CMakeLists.txt`（`project(RhiViewport)`），1 个可执行 |
| `nebula_api/` | 家庭音视频硬件抽象层（设备 / 流 / MediaMTX）的接口契约与验证源 | **无**（树内没有 `CMakeLists.txt`，见 `DEBUG.md` E1） |

~~三个部分各自有独立的 CMake 工程~~ **已作废（2026-10-07 复核 r677）**：`test/` 根 + `sport/` + `cellsim/` + `ffbo/` + `view/` **各自带 `project()`**（5 个独立工程，互不 `add_subdirectory`），`nebula_api/` 则完全没有构建入口。链接方式分两类：根测试的多数 target 与 `sport/`、`cellsim/` 直接编入 `../algebra`、`../network` 等引擎源文件，`ffbo/`、`view/` 链预编译库（`binary/xhqm.lib`，Linux/mac 为 `binary/libxhqm.a`）。

> 本模块**当前仍存在的问题**（编不过的 target、失效的检查、跨平台残留等）见同目录 `DEBUG.md`；本文档只写「是什么 / 怎么用」。旧文件整理：`test/README.md` 与 `test/DEBUG.md` 都由本次全仓复核建立/更新，历史台账 `fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` 不搬家不改名。

---

## 1. 根目录：Catch2 单元测试套件

### 测试框架

- 使用 **Catch2 v2.13.10 单头版**（`test/catch.hpp`，`CATCH_VERSION_MAJOR/MINOR/PATCH` = 2/13/10），由 `test.h`（17 行）统一定义入口：

```cpp
#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#define CHECK_NEAR(a, b, eps) /* 绝对误差 std::fabs(a-b) <= eps */
```

- `test.h` 提供 `CHECK_NEAR`（绝对误差语义），其余直接使用 Catch2 原生宏（`TEST_CASE` / `CHECK` / `REQUIRE`）。
- 规模（实测）：根目录 **27 个 `.cpp` / 7162 行**（不含 `catch.hpp`、`test.h`），共 **227 个 `TEST_CASE`**。

### 构建要点（`CMakeLists.txt`）

- 强制 `CMAKE_BUILD_TYPE=Release`（注释说明：MSVC 单配置生成器下 Debug 的 `/MDd` 会与 `xhqm.dll` 的 Release `/MD` ABI 错乱）。
- 定义 `-DXHQM_PROGRAM`，`include_directories("..")` 指向引擎源码根。
- **每个测试是一个独立可执行文件**（`add_executable` 共 **25 个**），多数直接编入所需 `../algebra/*.cpp`、`../network/*.cpp`、`../software/system/*.cpp` 等源文件（避免依赖预编译 DLL 的 ABI 问题）；全文件引用 47 个不同的 `../…` 源文件，逐个实测都存在。
- 25 个 target 里 **23 个是 Catch2 夹具**，另 2 个是手写 `main()` 工具：`gpu_smoke`（GPU 卷积冒烟，靠退出码判定）与 `astral_stress_test`（`gen`/`run` 两段式压测，`TEST_CASE` 数为 0）。**另有 2 个手写程序没有任何 target**（`sparse_mesh_test.cpp`、`thing_demo.cpp`，见 `DEBUG.md` E4）。
- ~~少数测试（`gpu_smoke`、`astral_test`、`astral_behavior_test`、`astral_stress_test`、`cs_test`）链接 `../../binary/xhqm.lib`~~ **已作废（2026-10-07 复核 r677）**：链预编译库的是 **`astral_test` / `astral_behavior_test` / `astral_stress_test` / `cs_test` 四个**，统一走 `XHQM_TEST_LIB`（`CMakeLists.txt:202-206`）= `binary/xhqm.lib`（MSVC）或 `binary/libxhqm.a`（Linux/mac，本机存在）；`gpu_smoke` 是第五个，但它只在 `if(MSVC)` 下建 target（`CMakeLists.txt:60-69`），并直接用 `XHQM_DYNAMIC + /UXHQM_PROGRAM` 走 dllimport 语义。
- **没有 `enable_testing()` / `add_test()`**（`grep -c` = 0）：25 个可执行文件**不会**注册进 `ctest`，必须逐个手跑。这是既定取舍，理由与代价见 `DEBUG.md` F1。

### 测试清单（按目标模块分组）

下表 23 个 Catch2 夹具与 2 个手写工具，全部来自 `CMakeLists.txt` 的 25 个 `add_executable`（括号内为 `TEST_CASE` 数，实测）。**`astral_test` / `astral_behavior_test` / `polydtor_test` 三个 target 当前编译不过**，原因与证据见 `DEBUG.md` A1 / A2。

| 可执行 | 覆盖内容 |
|---|---|
| `algebra_test` (24) | 代数库全量：mathf / complex / dualnum / quaternion / tensor / matrix / vector / figure / linear / geometry / ctype / linalg(eig/svd) / sparse / dual_quaternion |
| `algebra_extended_test` (9) | `analysis` 统计接口 + matrix/vector 基本操作 + sqrtm/logm |
| `tensor_serial_test` (12) | 张量串行算子全量（no 后端 + header-only 回退）：逐元素/广播/视图、linalg 归约/分解/矩阵函数、sparse/signal/stats/poly，含回归用例 |
| `tensor_cpu_test` (24) | CPU 后端（MSVC 下 SIMD + OpenBLAS）：~~验证 `blas→simd→多线程→串行` 调度链真实命中 SIMD~~ **已作废（2026-10-07 复核 r677）**：`/arch:AVX2` 与 `TENSOR_WITH_SIMD/TENSOR_WITH_BLAS` 只在 MSVC 分支给出（`CMakeLists.txt:45-56`），文件里三处能力门在无 SIMD 层时 `WARN` + `return`（`tensor_cpu_test.cpp:164-168,402,452`）；覆盖 spmv 多线程核、整数/f16/bool 内核 |
| `gpu_smoke` (0，手写退出码) | GPU(CUDA) 卷积冒烟：conv1d/2d/3d/4d/full、xcorr、trsv、chol、多精度 matmul/conv/elemwise/spmv，对照 CPU 结果；**只在 MSVC 下建 target**，且不校验实际命中的后端（`DEBUG.md` A3 / F3） |
| `soul_test` (9) | soul 模块：激活/softmax/LayerNorm/Linear/多头因果注意力/嵌入/位置编码/Transformer 前向与生成/采样(topk,topp,beam)/KV cache 一致性 |
| `soul_ad_test` (38) | reverse-mode AD：数值梯度检查、线性回归收敛、XOR 收敛，另含 GPT block 训练、conv1d/2d/3d/转置/分组、BatchNorm/GroupNorm、RNN/LSTM、InfoNCE、adam、module state_dict、tape round-trip |
| `soul_graph_test` (4) | tensor_payload 序列化 round-trip + 有向图节点 + 拓扑排序 |
| `soul_diffusion_test` (3) | DDPM 线性调度 + MLP 去噪器训练收敛 + 反向采样（2D 高斯玩具数据，3000 步，见 `DEBUG.md` D1） |
| `soul_diffusion_image_test` (1) | UNet 2D 图像扩散训练闭环（16×16 高斯光斑玩具分布，1500 步，见 `DEBUG.md` D1） |
| `structure_test` (7) / `structure_extended_test` (10) | lattice 数据结构：链/树/网/方向变体、bind/next/prev/ring_list、child/parent_tree、queues/stacks、图算法、迭代器 traits |
| `pattern_test` (6) / `pattern_extended_test` (10) | 设计模式：notifier/observer、factory/strategy/command/chain/state_machine（单线程与多线程特化） |
| `system_test` (7) | system 库：codesys/timesys/filesys/libmange/execsys/process/threads |
| `network_test` (3) | md5 / base64 / uuid（纯算法部分，标准测试向量） |
| `cs_test` (1) | 通用 C/S 模板 + TCP 传输层（loopback 请求-响应 echo，固定端口 19527，见 `DEBUG.md` B2） |
| `metaclass_test` (4) | block / buffer / cache / buffer_dim |
| `hardware_test` (1) | 设备选择（no/cpu/gpu/auto，header-only） |
| `ecs_test` (9) | `tuple_values` 组件存储（SoA/AoS）+ `model_ecs` 挂载闭环 + 多组件 join + `theory_instancing` 帧图接入 + 10 万节点批量变换 benchmark（none 后端桩，无 GL/VK 依赖） |
| `toon_test` (6) | toon 管线：UBO 布局与偏移、GL/Vulkan/AppleGL 三种 shader 预处理产物、材质默认值、`toon_prefab` 接线（none 后端桩） |
| ~~`astral_test`~~ **当前编译不过** (5) | astral 统一 node + 五类接口（增删改查连）+ json/二进制/mmap 持久化；源文件仍 `#include "../network/astral/kernel_tag.h"`（`:3`），该头在 r677 已不存在 → `DEBUG.md` A1 |
| ~~`astral_behavior_test`~~ **当前编译不过** (31) | evoke（原 kernel_tag）+ kernel 全量行为：级联删除孤儿标签、标签复用、upsert、select、冷热加载；同样卡在已删除的 `kernel_tag.h`（`:4`，待随测试迁移一并改名，见 `DEBUG.md` A1） |
| ~~`polydtor_test`~~ **当前编译不过** (3) | 多态释放回归夹具（经基类指针 delete 派生地址/几何对象 + `parallel` 派发）；停在 A 世代 nebula API（`kind::none`、`addr_t<>`、`to_json`/`clone`/`broadcast`/`send`），见 `DEBUG.md` A2 |
| `astral_stress_test` (0，手写 `gen`/`run`) | 并发读写压测（二进制索引路径）：20 线程 × 10000 次，读写比 7:3 / 3:7（计数与内存读数的两个坑见 `DEBUG.md` B1 / C1） |

---

## 2. sport — `soul-exp` 实验库（模型推理移植）

详细文档见 `sport/README.md`，此处为摘要。

### 定位

用 xhqm 的 `algebra/tensor`（含 conv）与 `algebra/soul`（transformer / AD）能力，把 HuggingFace / PyTorch 的预训练模型权重导出为 xhqm 可加载的裸格式，再**纯 C++ 复刻前向推理**，并与 PyTorch 参考输出逐值对照锁定正确性。

### 目录

```
test/sport/
├── export/                      # PyTorch 侧权重导出与参考生成（Python）
│   ├── export_wav2vec2.py / export_dinov2.py / export_motion.py
│   ├── make_reference.py / make_reference_motion.py / dump_stages.py
│   └── requirements.txt         # torch, transformers, numpy
├── include/soul_exp/            # C++ 推理头文件（模板，header-only）
│   ├── weight_loader.h          # 读 manifest.json + weights.bin
│   ├── nn.h                     # 共享原语
│   ├── wav2vec2.h / dinov2.h    # 两套视觉/音频编码器前向
│   └── sport.h                  # motion-gen 全链路前向
└── test/                        # 冒烟 + 逐值对照 + 性能基准（6 个可执行）
    ├── loader_smoke.cpp         # 权重加载冒烟
    ├── wav2vec2_smoke.cpp       # wav2vec2 前向冒烟
    ├── loader_test.cpp          # 逐值对照（wav2vec2）
    ├── dinov2_test.cpp          # 逐值对照（DINOv2）
    ├── sport_test.cpp           # motion-gen 全链路对照
    └── bench_speed.cpp          # 性能基准
```

六个可执行由 `sport/CMakeLists.txt` 的 `soul_exp_executable()` 统一建 target（同一函数里带上 `XHQM_CORE_SOURCES` 与 OpenBLAS 库），因此**一个后端开关同时作用于全部六个**。

### 关键类 / API（`namespace soul_exp`）

| 类 / 函数 | 说明 |
|---|---|
| `weight_table` / `load_weights(dir)` / `load_tensor<dim>(wt, name)` | 权重加载：读 `manifest.json`（层名→shape/offset/nbytes）+ `weights.bin`（行优先 f32 blob），按名重建 `xhqm::tensor` |
| `gelu_exact` / `gelu` / `layer_norm` / `linear` / `attention`（`nn.h`） | 共享原语：精确 erf GELU、LayerNorm、`y = x·Wᵀ + b` 线性、非因果多头注意力（序列统一 `tensor<2>(seq, hidden)`，单 batch 去批） |
| `wav2vec2_base<type>` | wav2vec2-base 前向：7×conv1d 特征抽取 → 投影 512→768 → 12×post-norm encoder；`forward()` 返回 `{ last_hidden_state, extract_features }` |
| `dinov2_base<type>` | DINOv2(ViT-B/14) 前向：patch embed → [CLS]+位置编码 → 12×pre-norm + layer_scale；`forward()` 返回 `{ last_hidden_state, pooler }` |
| `motion_tokenizer<type>` | VQ-VAE 分词器 decode：5 码本 + 部位拼回 + conv1d/ResBlock 解码，输出 (poses, exps) |
| `motion_generator<type>::session` | AR Transformer 生成器流式会话：self/cross 注意力、KV 缓存增量解码、`emo_head` 逐帧情感脉冲 |
| `stream_featurizer<type>` | 流式音频特征器：wav2vec2 滑窗、按 `frame_center` 选帧输出 50Hz/768d 特征 |
| `sport<type>` | 编排：`load()` 载入 w2v+tokenizer+generator，`generate()` 返回 `{ poses, exps, trans, tokens, emo, emo_ids }` |

### 依赖与后端

- 编译依赖 xhqm 核心源文件（`../../algebra/ftype.cpp`、`ctype.cpp`、`tensor/backend/tensor_backend.cpp`、`tensor_backend_no.cpp`、`tensor_backend_cpu.cpp`；`-DSOUL_EXP_CUDA=ON` 时再加 `tensor_backend_gpu.cpp` / `.cu`）。
- 张量后端默认 **CPU**：`-DTENSOR_WITH_CPU`，MSVC 下另加 `/arch:AVX2` 与 `-DTENSOR_WITH_SIMD`（非 MSVC 不加，与引擎本体策略一致，因为 SIMD 内核用了 `_mm256_atan2_ps` 等 MSVC 专属内建）；~~SIMD~~ **（2026-10-07 复核 r677 补注）**：SIMD 只在 MSVC 分支开启，Linux/mac 走标量/多线程路径。
- `-DSOUL_EXP_CUDA=ON` 可启用 CUDA GPU（实测流式小矩阵推理下 GPU 无收益，故默认 CPU）；启用时 `CMAKE_CUDA_ARCHITECTURES` 固定 86（RTX 3090 Ti）。
- OpenBLAS 三级探测（`sport/CMakeLists.txt` 内联判定）：① 仓库外 `<repo 父目录>/depend/openblas`（scipy-openblas32，`scipy_` 前缀，本工作副本里该目录**不存在**）→ ② 系统 OpenBLAS（`cblas_` 前缀，`-DOPENBLAS_STANDARD`）→ ③ 都找不到则回退纯 SIMD/标量（不定义 `TENSOR_WITH_BLAS`）。
- 权重来源：`facebook/wav2vec2-base`（Apache-2.0）、`facebook/dinov2-base`（Meta 许可，商用前需核验）、`body/sport` 的 motion-gen checkpoints。

### 对照精度（`sport/README.md` 记录）

- wav2vec2：`extract_features` ≈ 1.8e-6、`last_hidden_state` ≈ 1.9e-5（峰值归一相对误差）。
- DINOv2：`pooler` ≈ 3.7e-6、`last_hidden_state` ≈ 2.3e-5。
- sport motion-gen：动作 token 0/300 mismatch、emo_ids 精确一致（0/60）、emo_probs rel_to_max ≈ 9.1e-7。

---

## 3. view — RhiViewport 渲染查看器

### 定位

统一 3D 查看视口（**QWindow 对称壳**），运行期由 `--backend vulkan|opengl|auto` 决定渲染后端：
- **Vulkan** → 引擎自建 surface（多窗口每窗口一份 swapchain）；
- **OpenGL** → QOpenGLContext 自管上下文（makeCurrent 帧循环 + swapBuffers）；
- **auto** → 探测 Vulkan 失败回退 GL。

交互 / 选中 / 相机 orbit / 场景构建 / 诊断参数全部共享。**多相机架构**：每个窗口 = ~~独立 `render_camera`~~ **已作废（2026-10-07 复核 r677）**：实际类型是 `xhqm::fantasy::camera`（`fantasy/camera.h:19`，全仓没有 `render_camera` 这个名字；成员声明见 `view/RhiViewport.h:78`，访问器 `camera_ptr()`）+ 独立 `theory_frame_graph` 管线，可 `attach_world()` 共享同一 `world` 查看同一场景。

### 关键类 / API

| 类 / 命名空间 | 说明 |
|---|---|
| `class RhiViewport : public QWindow` | 查看视口。`initialize()` 初始化场景/RHI、`start()` 启帧循环、`pick(x,y)` 点选拾取、`set_select()` 选中、`snapshot_to()` 无头导出 PNG、`watch_orange()`/`watch_orange_passive()`/`dump_frame_raw()` 诊断闪帧 |
| `namespace demo`（`ModelLoader.h`） | assimp 模型→`prefab`（纹理 + LBS 蒙皮）+ 骨骼动画 + 程序化几何 |
| `demo::load_model(path, name, out_bone_count)` | 加载模型文件为 `models` 树（支持 glTF/PMX 等，PMX 额外解析贴图表） |
| `demo::load_model_animated(...)` | 加载模型 + 全部骨骼动画 clip |
| `demo::load_model_separate(...)` | 每个 mesh 一个独立 `models` 节点（gizmo 等需单独显隐） |
| `demo::anim_clip` / `demo::sample_palette(clip, time)` | 骨骼动画采样（位置/旋转/缩放关键帧 + slerp + 蒙皮调色板） |
| `demo::make_skinned_bar()` / `demo::bar_palette(angle)` | 程序化双骨棒（验证 LBS 蒙皮） |

### main.cpp 的验证内容

`view/main.cpp`（943 行）不只是一个查看器，还内嵌了引擎 `fantasy` 渲染特性的**就地验证**（打印 `[XXX] ... ok=1`）：

- **当前真正会执行的是 7 段**：BC1 压缩纹理 round-trip（`[BC1] :329`）、glTF 原生加载（`[GLTF] :354`）、场景序列化 scene save/load（`[SCENE] :364`）、blendshape（`[BLEND] :390`）、cubemap depth 离屏（`[CUBE] :414`）、点光阴影 VP（`[PSHADOW] :427`）、浮点纹理（`[FLOAT] :450`）。
- ~~SSAO+TAA+SSR+PCF 后处理、bindless 纹理句柄等（部分 `#if 0` 折叠）~~ **已作废（2026-10-07 复核 r677）**：`#if 0` 折叠的不是「部分」而是**另外 5 段全部**——`view/main.cpp:466-519` 折掉 `[GSKIN]`，`:522-768` 折掉 `[HIZ] :521` / `[HIZCULL] :581` / `[BINDLESS] :618` / `[SSAO+TAA] :637`（判定 printf 在 `:509,:573,:610,:631,:730,:736,:743`）；这 5 段当前没有任何自动化验证，见 `DEBUG.md` E2。
- 命令行开关：`--backend vulkan|opengl|auto`、`--snapshot <path>`、`--snap-series <dir>`（120ms × 15 帧）、`--vmd <path>`（补 VMD 动作）、`--still`、`--watch`（20ms 被动采样 × 400）、`--nobar`、`--windows N`（多窗口）、`--dumpframes <dir>`（20ms × 500 帧）。第 1 个非 `--` 开头的参数是模型路径（`main.cpp:187`）。
- 默认模型/gizmo 与场景临时文件是 Windows 盘符路径（`main.cpp:42/46/365` 的 `D:/WorkSpace/...`），非 Windows 上无参启动会走加载失败分支 —— 见 `DEBUG.md` C2。

### 依赖

- Qt5（Gui 模块）、assimp、GLEW、OpenGL；Vulkan 后端另需 Vulkan SDK + glslang + SPIRV-Tools。
- 链接预编译引擎库：Windows `binary/xhqm.lib`，Linux/macOS `binary/libxhqm.a`。
- 渲染后端判定参数 `XHQM_DEVICE` / `XHQM_RENDER_BACKEND` 与引擎 CMakeLists 语义一致（构建引擎与查看器须传相同 `-D`）；`QT_ROOT` 是 CACHE 变量，默认 `"QT_PATH/"`，需显式覆盖（`view/CMakeLists.txt:23`）。
- 同层的 `ffbo/` 另有一份**已分叉的** `RhiViewport`（687 行 `.cpp` + 144 行 `.h`，多出逐 pass 计时与 `gizmo()` 开关），取舍与代价见 `DEBUG.md` F4。
- `view/build/`（未入库、`svn:ignore` 掩盖的构建残留）见 `DEBUG.md` E3。

---

## 3.5 cellsim / ffbo / nebula_api — 其余三棵子树

这三棵子树**不作为独立文档模块**出 README/DEBUG（在本次全仓复核的文档体系里它们只有本模块这一处入口），所以各写一小节。注意它们**树内**是各自有说明文件的：`cellsim/README.md`（11KB）、`ffbo/README.md`（44KB）+ 5 份设计/评审文档、`nebula_api/README.md`（14KB）+ `PROTOCOL.md`；只有 `view/` 没有树内 README。

### cellsim — 单细胞模拟器

- 独立工程 `project(cellsim)`（`cmake_minimum_required 3.15`），**只依赖标准库**，不引入任何第三方依赖；1 个 target：`cellsim_bench`（`cell_bench.cpp` 314 行 + `cellsim.h` 238 行，`int main()` 无参数）。
- 模型内容：电导型突触、五类通道（快/慢 × 兴奋/抑制 + 一条电压依赖 NMDA 式通道）、分流抑制 `g_inh·(E_inh − v)`、发放适应/不应期/分级输出 —— 目的写在自己的 `cellsim/README.md`（11KB）里。
- 与 `ffbo/` 的关系：`ffbo/CMakeLists.txt:27` 把 `../cellsim` 加进 include 路径，用它替换果蝇的细胞模型。
- `svn:ignore` = `build`。

### ffbo — 数字果蝇

- 独立工程 `project(digital_fruit_fly)`，2 个 target：
  - `mcns_net`（`mcns_net.cpp` 1753 行）：只跑网络、不含身体与渲染，用法 `mcns_net <malecns目录> [选项]`，选项含 `--sec` / `--cell-model` / `--bio-gain` / `--graded-base` / `--graded-amp` / `--norm-gain` / `--stim-off-after`（`mcns_net.cpp:423-433`）。
  - `fly_mcns_view`（`fly_mcns_view.cpp` 2069 行 + 本目录自己那份 `RhiViewport.cpp/.h`）：网络 + MuJoCo 身体 + fantasy 渲染 + 复眼 raycast 全通路，定义 `XHQM_PROGRAM XHQM_VIEW_HAS_GL=1`。
- `archive/fly_brain_manc_view.cpp` 是**归档**的旧方案（FAFB 大脑 + MANC 桥接），**不在构建里**；另有 5 个一次性调查工具（`actuator_dump` / `fly_drive_diag` / `force_probe` / `import_connections` / `mujoco_diag`）按注释「故意不进构建」，需手工编译。
- 文档 6 份：`README.md`（44KB，含 2026-09-16 的当前状态节）、`DESIGN_NOTES.md`（68KB）、`DEND_RANK_K.md`、`RENDER_FIX_PLAN.md`、`RENDER_FIX_REVIEW.md`、`VDTOR_CANDIDATES_VERDICT.md`；脚本侧有 `align_ref.py` / `offline_*.py` / `torch_bio_brain.py` / `sweep_*.ps1` / `run_mcns_net.bat`。
- **当前无法在非 Windows 上配置**（`CMakeLists.txt:12-13` 写死 `C:/Qt/5.15.2/msvc2019_64` 与仓库外的 `D:/WorkSpace/_research/digital-fruit-fly/depend/mujoco`）→ `DEBUG.md` C3。

### nebula_api — 家庭音视频硬件抽象层

- **没有构建入口**：树内没有 `CMakeLists.txt`，`test/CMakeLists.txt` 里也搜不到该目录（`DEBUG.md` E1）。
- 内容：`README.md`（14KB，项目总纲）+ `PROTOCOL.md`（6.7KB）+ 6 个接口头（`common/protocol.h` 138 行、`client/device_client.h` 236 行、`client/device_impl.h` 324 行、`client/linux_hotplug.h` 131 行、`client/mtx_server.h` 185 行、`server/media_server.h` 221 行）+ `nebula_api_test.cpp`（639 行）。
- 测试源停在已被删除的 API 世代（`link_b` / `link_t` / `addr_t`）并硬编码了另一棵树的可执行文件路径，因此这棵子树目前只是「接口契约 + 未接构建的验证源码」。
- `svn:ignore` = `nebula_api_test`（忽略编译产物同名文件）。

---

## 4. 总体架构

```
xhqm/（引擎源码，只读依赖）
 ├─ algebra / soul / tensor / lattice / software / network / hardware / metaclass / fantasy
 └─ test/（本模块）
     ├─ *.cpp + test.h + catch.hpp   ──▶ 引擎库单元测试（Catch2，23 个夹具 / 227 个 TEST_CASE）
     ├─ sport/     ──▶ soul-exp 模型推理实验库 + PyTorch 权重导出 + 对照测试
     ├─ cellsim/   ──▶ 单细胞模拟器（仅标准库，作为 ffbo 的可替换细胞模型）
     ├─ ffbo/      ──▶ 数字果蝇：MaleCNS 连接组 → MuJoCo 身体 → fantasy 渲染
     ├─ view/      ──▶ RhiViewport 渲染查看器 + assimp 模型/动画加载
     └─ nebula_api/──▶ 家庭音视频硬件抽象层接口契约（无构建入口）
```

- **根测试**：把引擎各库的公开接口当作被测对象，用最小必要源文件编译，保证单元级、可离线、确定性验证。
- **sport**：把 xhqm 的 tensor/soul 当作“推理运行时”，验证其算力足以承载真实模型前向（并作为 motion 流式语音驱动的前置实验）。
- **cellsim / ffbo**：把 xhqm 的 system/hardware/fantasy 当作“仿真运行时”，先验证脉冲网络的连接组与权重符号，再往上接身体与渲染。
- **view**：把 xhqm 的 fantasy 渲染后端当作“RHI 运行时”，做交互验证与图形特性就地自检。
- **nebula_api**：接口契约与验证源码，尚未接进任何构建。

---

## 5. 构建与使用示例

### 5.1 根目录单元测试

```bash
cd xhqm/test
cmake -B build -G Ninja          # 或 -G "Visual Studio 17 2022"；build/ 已在 svn:ignore 里
cmake --build build --target hardware_test   # 也可以不带 --target 全建
./build/hardware_test            # 逐个手跑：没有 ctest，没有 add_test 注册
./build/algebra_test -s          # Catch2 支持 -s（显示成功断言）/ 用例名过滤
./build/soul_ad_test "test_xor"  # 只跑匹配的 TEST_CASE
./build/gpu_smoke; echo $?       # 非 Catch2 工具（MSVC 专属 target）：靠退出码判定（0=全过）
```

- 想写一个新夹具：源文件放 `test/`，`#include "test.h"`（它自己 `#define CATCH_CONFIG_MAIN`，一个可执行只放一份入口），再 `TEST_CASE("…") { CHECK(...); CHECK_NEAR(实际, 期望, 绝对误差eps); }`；最后在 `test/CMakeLists.txt` 里 `add_executable` 并**显式列出所需引擎源文件闭包**（不能只写夹具自己 —— 例：`hardware_test` 只要夹具本身，`algebra_test` 要 7 个源文件，`system_test` 要 7 个）。
- 真实最小例子（取自 `hardware_test.cpp:1-26`，路径与类型都照抄可编译）：

```cpp
#include "../hardware/hardware.h"   // include 根 = 引擎源码根（CMakeLists 的 include_directories("..")）
#include "test.h"                   // Catch2 v2.13.10 + CHECK_NEAR
#include <cstring>
using namespace xhqm;               // 工程全局定义 -DXHQM_PROGRAM

TEST_CASE("test_device")
{
    CHECK(std::strcmp(hardware::device_name(hardware::device::cpu), "cpu") == 0);
    CHECK(hardware::device_compiled(hardware::device::no));      // no 设备恒可用
    hardware::active_device(hardware::device::cpu);              // 参数是 hardware::device 枚举
    CHECK(hardware::active_device() == hardware::device::cpu);
}
```

- 两个后端相关注意点：① `tensor_cpu_test` / `soul_diffusion_image_test` 另加了 `XHQM_DEVICE_CPU + TENSOR_WITH_CPU`（MSVC 下还有 `TENSOR_WITH_SIMD/TENSOR_WITH_BLAS` 与 `/arch:AVX2`），其余张量夹具跑的是默认 no 后端；② `-DXHQM_PROGRAM` 是全局的，链 `binary/xhqm.lib` 的 `gpu_smoke` 反而要 `/UXHQM_PROGRAM` 去掉它。

> 前置：四个链接主库的测试（`astral_test` / `astral_behavior_test` / `astral_stress_test` / `cs_test`）与 MSVC 下的 `gpu_smoke` 需先构建主库产出 `binary/xhqm.lib`（`XHQM_BUILD_TENSOR=ON + XHQM_DEVICE=AUTO`）；Linux/mac 对应 `binary/libxhqm.a`（本工作副本已存在）。当前编不过的 3 个 target 见 `DEBUG.md` A1 / A2。

### 5.2 sport 模型推理

```bash
# 1) 导出权重（Python）
cd xhqm/test/sport/export
pip install -r requirements.txt
python export_wav2vec2.py --out ../out/wav2vec2-base   # 生成 manifest.json + weights.bin

# 2) 编译（默认 CPU 后端；-DSOUL_EXP_CUDA=ON 可切 GPU）
cd xhqm/test/sport
cmake -B build && cmake --build build

# 3) 逐值对照 / 冒烟 / 性能（6 个可执行，参数都有默认值，见各自 main）
./build/loader_smoke
./build/wav2vec2_smoke
./build/loader_test   [out/wav2vec2-base] [out/ref]        # 默认取这两个目录
./build/dinov2_test   [out/dinov2-base]   [out/ref_dinov2]
./build/sport_test    [out/motion] [out/wav2vec2-base] [out/motion/ref]
./build/bench_speed   [out/motion] [out/wav2vec2-base] [秒数=2.0]
```

### 5.3 view 渲染查看器

```bash
cd xhqm/test/view
cmake -B build -DQT_ROOT=/path/to/Qt -DXHQM_DEVICE=GPU -DXHQM_RENDER_BACKEND=VULKAN
cmake --build build
# 注意：产物不在 build/ 下 —— 两个平台分支都把 CMAKE_RUNTIME_OUTPUT_DIRECTORY 设为 ../../../binary
#      （view/CMakeLists.txt:28 与 :33），所以可执行文件是 <repo>/binary/RhiViewport
../../../binary/RhiViewport --backend auto [模型路径.glb]         # 默认模型是 Windows 盘符路径，非 Windows 需显式传模型
../../../binary/RhiViewport 模型.glb --snapshot out.png --windows 2   # 多窗口 + 无头截图（每窗口一张 out_winN.png）
../../../binary/RhiViewport 模型.glb --vmd 动作.vmd --watch          # 追加 VMD + 闪帧诊断采样
```

### 5.4 cellsim / ffbo

```bash
cd xhqm/test/cellsim && cmake -B build && cmake --build build && ./build/cellsim_bench

cd xhqm/test/ffbo    # 仅 Windows（Qt5 + MuJoCo + GLEW 路径写死，见 DEBUG.md C3）
cmake -B build && cmake --build build
./build/mcns_net <malecns目录> [--sec 10 --cell-model cellsim …]   # 只跑网络
./build/fly_mcns_view <malecns目录>                                # 网络 + 身体 + 渲染
```

> `nebula_api/` 没有构建入口，无法按上述方式构建（`DEBUG.md` E1）。

---

## 6. 依赖汇总

| 依赖 | 用途 | 备注 |
|---|---|---|
| Catch2 v2.13.10（`catch.hpp`，657 KB 单头） | 根目录单元测试框架 | 单头内置，无需安装 |
| xhqm 引擎头文件 + 部分源文件 | 全部 | 根测试 / sport / cellsim 直接编入（根工程引用 47 个 `../…` 源文件），ffbo / view 链预编译库 |
| OpenBLAS | sport / tensor_cpu / soul_diffusion_image | 仓库外 `depend/openblas`（scipy-openblas32）或系统 OpenBLAS；都没有则回退 SIMD/标量 |
| CUDA（可选） | gpu_smoke / sport GPU 后端 | 需 nvcc + `/utf-8`（`.cu` 含中文注释）；`gpu_smoke` target 仅 MSVC 建 |
| Python（torch/transformers/numpy） | sport 权重导出与参考生成 | 仅导出阶段，推理纯 C++ |
| Qt5 Gui | view 查看器 / ffbo | QWindow / QImage |
| assimp / GLEW / OpenGL | view 模型加载与 GL 后端 | |
| Vulkan SDK + glslang + SPIRV-Tools | view Vulkan 后端 | macOS 需额外 Cocoa/QuartzCore/objc |
| MuJoCo（`mujoco.lib`，可选） | ffbo `fly_mcns_view` 的物理身体 | 路径写死在仓库外的 `digital-fruit-fly/depend/mujoco`（`DEBUG.md` C3） |
| 无（仅标准库 + STL） | cellsim | 有意不引入第三方依赖 |

---

## 7. 设计特点与注意事项

- **多后端一致性验证**：~~`gpu_smoke`、`tensor_cpu_test` 把同一算子分别在 no/cpu/gpu 后端运行并对照~~ **已作废（2026-10-07 复核 r677）**：`gpu_smoke` 的 `no ↔ gpu` 对照会因为后端静默回退而退化成 CPU 自比，且没有断言实际命中的后端（`DEBUG.md` A3）；`tensor_cpu_test` 只在 MSVC 下才走到 SIMD 层，Linux/mac 上相关用例被能力门跳过（`DEBUG.md` G1）。
- **数值精度锁定**：sport 用“峰值归一相对误差”对照 PyTorch，规避近零元素放大相对误差的问题（数值见 §2「对照精度」，出处 `sport/README.md:59,77,85`）。
- **平台差异显式处理**：SIMD 仅 MSVC 开启（`_mm256_*` intrinsics），Linux 走标量/多线程；BLAS 前缀、Qt/Vulkan/assimp 路径按平台分支 —— 代价是 `ffbo` 整体只在 Windows 可配、`view` 的默认模型路径也是 Windows 盘符（`DEBUG.md` C2 / C3）。
- **诊断工具内建**：view 提供闪帧诊断（`watch_orange*` / `dump_frame_raw` / `--snap-series`），把“合成侧 vs 引擎侧”闪烁问题可复现化。
- **防御性编码**：`ModelLoader::sample_palette` 对 NaN/Inf/过大（`|v| > 1e6`）骨骼矩阵钳位回退单位阵，避免蒙皮顶点被拉飞（`view/ModelLoader.h:614-628`）。
- **成熟度**：中高 —— 覆盖广（23 个 Catch2 夹具 / 227 个 TEST_CASE）、数值断言细（多数 f64 用例用 1e-12）；缺的是**编排与门禁**：没有 ctest/CI、3 个 target 编不过、SIMD/GPU 覆盖只在 Windows 真正生效（分别为 `DEBUG.md` F1 / A1–A2 / F2–F3）。
- **当前问题清单**：本文档只写「是什么 / 怎么用」；**仍存在的问题**（会算错 / 会挂 / 编不过 / 没接上 / 文档与代码不符）全部记在同目录 `DEBUG.md`，此处不重抄。

> 许可提示：sport 权重分别受 Apache-2.0（wav2vec2）与 Meta 许可（DINOv2）约束，商用前需自行核验。
