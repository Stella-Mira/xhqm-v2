# xhqm 引擎 · 模块全景评估

> 最后同步：2026-10-07，对照工作副本 r677。
>
> 本文件是对 xhqm 库全模块的一次系统性梳理：每个模块的**定位**、**优势**、**劣势**、**缺失**与**未来建议**。各模块细读与接口文档见各子目录的 `README.md`。
>
> 生成时间：2026-09-10　覆盖模块数：23　参考：各模块源码 + 各子模块 README
>
> 更新（2026-10-02）：`network/nebula` 已按 B 世代重整（`node/addr/data/life/link/find/post/zero/star`）并纳入 CMake 与 VS 构建（12 个 TU，两条构建路径实测通过）；`network/astral` 的标签层已由 `kernel_tag` 改名为 `evoke`。第 2.1、4.3 节与 astral 相关表述已同步修正。
>
> 更新（2026-10-03，r661）：nebula 文件集补齐为 **13 个 TU**（新增 `addr/addr_ipv6`、`addr/addr_serial`、`addr/addr_uuid`，原 `addr_ptr` 已更名 `addr_uuid`）；控制台 `zero` 与工具层 `tool` 合并（`zero::kit`，API 更名 `broad_claim` / `create_message`，收消息走 `data_signal` 信号而非虚函数）；`data` 层新增 `frame_record` 统一解包内核条目；r660 一次性修掉四处真缺陷（`zero(config)` 池参数 UB、`exec_proxy` 竞态/自死锁/`argv` 载荷被吞、`addr` 的 `is_open()` 恒假）。第 4.1、4.3 节已同步修正。
>
> 更新（2026-10-07，r677）：同步一次全模块复核。**nebula 文件集确认为 13 个 TU**（文件树 `find network/nebula -name '*.cpp' | wc -l` = 13，与 `CMakeLists.txt:378-390` 的 `XHQM_BODY_NETWORK`、`xhqm.vcxproj:99-111` 的 13 条 `ClCompile` 三方一致）。**`xhqm/` 门面头 22 份、113 条 `#include` 全部命中、零悬空**（逐条解析 include 路径实测）；`xhqm/nebula` 已改为只引 B 世代的 9 个头，`xhqm/astral` 已删 `kernel_tag.h` 的引用。**主要文档漂移点**（本轮一并订正，详见本文件内 `已作废（2026-10-07 复核 r677）` 标注）：① 2.7 节曾写 lattice 的 `directed_mesh/matrix_mesh/graph_algorithms` 原文件树缺失——三个文件都在；② 3.2 节据 `rhi_vk.h:10` 的旧注释把 Vulkan 后端称为「占位」——实现是完整的 4078 行；③ 3.1 节 bug 清单记作「53+ 项已修」——台账 `fantasy/fantasy_bugs.md` 实际编号 1–43；④ 2.1 节称 `XHQM_USE_*` 与 `XHQM_BUILD_*`「一一对应」——实际是 7 ↔ 4 的不对称；⑤ 若干行数（tensor `linalg.h`、`rhi_vk.cpp`）已随文件增长而失准。**本文件的「当前问题清单」一律不在此展开：见同目录 `DEBUG.md`，以及各模块自己的 `README.md` / `DEBUG.md`（`fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` 为历史台账，不搬家不改名）。**

---

## 目录

0. 怎么用（最小可运行）
1. 全局总览
2. 数学与核心（xhqm / algebra / lattice / metaclass / hardware）
3. 渲染引擎（fantasy 全子系统）
4. 网络与分布式（network / astral / nebula）
5. 软件层（software / system / pattern）
6. 测试层（test）

---

## 0. 怎么用（最小可运行）

C++17 工程；默认 `Release`（`CMakeLists.txt:30-33`），MSVC 强制 `/utf-8`（`CMakeLists.txt:25-28`），产物输出到**仓库外**的上溯一级 `../binary/`（`XHQM_OUTPUT_DIR`，`CMakeLists.txt:577`）。四个模块开关**默认全 OFF**（`CMakeLists.txt:41-44`）。

**构建**（命令行取自 `CMakeLists.txt:2-15` 的编译建议注释，仅保留可选项名的实测拼写）：

```bash
cmake -S . -B build-shared \
  -DXHQM_DYNAMIC=1 \
  -DXHQM_DEVICE=CPU \
  -DXHQM_RENDER_BACKEND=OPENGL \
  -DXHQM_BUILD_TENSOR=ON -DXHQM_BUILD_NETWORK=ON \
  -DXHQM_BUILD_SYSTEM=ON -DXHQM_BUILD_FANTASY=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-shared
```

`XHQM_DEVICE` 取 `NO / CPU / GPU / NET / AUTO`（`CMakeLists.txt:99-100`，默认 `CPU`），它注入 `hardware/hardware.h:14-15` 的 `XHQM_DEVICE_*` 宏；`XHQM_RENDER_BACKEND` 取 `AUTO / VULKAN / OPENGL`（`CMakeLists.txt:183`，仅在 device=gpu 时生效）。

**最小可运行程序**（两处写法都真实存在：聚合头与直接包含；API 取自 `hardware/hardware.h:35-122`，`test/hardware_test.cpp:12-24` 逐条调用过同名函数）：

```cpp
// 写法 A：走聚合头（xhqm.h:21-30 全量，或 :36-38 的单模块开关）
#define XHQM_USE_HARDWARE
#include "xhqm.h"

// 写法 B：直接包含（测试就是这么用的，见 test/hardware_test.cpp:2）
// #include "hardware/hardware.h"

#include <cstdio>

int main()
{
    using xhqm::hardware::device;

    // 编译期默认设备（没有 XHQM_DEVICE_* 宏时为 device::no）
    device compiled = xhqm::hardware::compiled_device();
    // 运行时激活设备，初值等于 compiled_device()
    device active = xhqm::hardware::active_device();

    std::printf("compiled=%s active=%s cpu_compiled=%d\n",
        xhqm::hardware::device_name(compiled),          // 返回 const char*：cpu/gpu/net/auto/no
        xhqm::hardware::device_name(active),
        (int)xhqm::hardware::device_compiled(device::cpu));

    xhqm::hardware::active_device(device::cpu);         // 运行期切换；device::net 不参与 auto 匹配
    return 0;
}
```

各模块的「最小可用代码」节在自己的 README 里，接口名与参数含义以那些节为准（例如 `lattice/README.md`、`xhqm/README.md`、`network/nebula/README.md`）。

**跑测试**：`test/` 是**独立 CMake 工程**，`include_directories("..")` 指回引擎源码根、强制 `Release`（`test/CMakeLists.txt:1-16`）；25 个 Catch2 目标各自是可执行文件，**没有 `ctest`，必须逐个手跑**（清单与每个目标的覆盖范围见 `test/README.md`）：

```bash
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Release
cmake --build test/build --target structure_test
./test/build/structure_test
```

---

## 1. 全局总览

| 模块 | 定位 | 成熟度 |
|---|---|---|
| xhqm（核心门面） | 统一类型/宏/设备抽象/模块开关 | 高 |
| algebra 基础 | 数学地基（常数/泛型/数值/类型） | 中 |
| algebra/tensor | N 维张量 + 多后端调度 | 高 |
| algebra/analysis | MATLAB 风格数值分析 | 中 |
| algebra/geometry | 3D 计算几何 / NURBS / hyper | 中 |
| algebra/soul | 神经网络 / Transformer / AD / 扩散 | 中 |
| fantasy（渲染） | 双后端 RHI + 实时渲染 | 高 |
| fantasy/backend | OpenGL/Vulkan RHI 后端 | 中高 |
| fantasy/runtime | 场景 IO / 帧图 / 并行 / 性能 | 中 |
| fantasy/spatial | BVH / 剔除 / 遮挡 / LOD | 中高 |
| fantasy/lighting | 光照 / CSM / 点光阴影 | 中 |
| fantasy/postfx | bloom/tonemap/ssao/taa/ssr 等 | 中 |
| fantasy/animation | 骨骼 / 混合 / IK / GPU 蒙皮 | 中 |
| hardware | 设备抽象 + 并行骨架 | 低 |
| lattice | 图 / 树 / 网 数据模型 + 图算法 | 中高 |
| metaclass | 元编程 / 反射 / 容器 | 中 |
| network | 传输 / 加密 / 记忆 / 分布式 | 中高 |
| network/astral | 持久化记忆内核 | 高 |
| network/nebula | 无中心分布式组网推送 | 中 |
| software | 信号槽 / 序列化 / 运行时容器 | 中 |
| software/system | 跨平台系统原语 / 线程池 | 中高 |
| software/pattern | 设计模式库 | 中 |
| test | 单元测试 + 推理实验 + 渲染查看器 | 中 |

**跨模块共性观察**

- **优点一致**：header-only、模板化、跨平台封装（POSIX/WINDOWS 双后端）、后端抽象（CPU/GPU/NET/AUTO）贯穿数学与渲染。
- **短板一致**：单文件偏大（`fantasy/backend/rhi_vk.cpp` 4078 行、`network/json.hpp` 为引擎内第三方副本）；**测试有但无编排**——`test/` 下 25 个 Catch2 目标全是独立可执行文件，仓库内**无 `ctest`、无 `enable_testing()`、无任何 CI 配置**（`.github`/`.gitlab-ci.yml` 均不存在）；Windows 硬编码路径残留但**不在库源码里**（库源码 0 处盘符路径；命中集中在 `test/view/main.cpp:42,46`、`test/ffbo/CMakeLists.txt:13`、`CMakeLists.txt:674` 的 PDB 输出路径）；部分接口为占位/桩实现。
- **最大增量空间**：自动化测试/回归（把 25 个目标收进统一 runner）、文档与源码一致性、占位实现落地、分布式(net/nebula)真正打通。

> 本节只给结论。**当前仍存在的问题逐条带 `文件:行号` 证据的清单见同目录 `DEBUG.md`**，各模块细节见其自己的 `README.md`。

---

## 2. 数学与核心

### 2.1 xhqm（核心门面）
- **定位**：统一类型、宏、设备抽象与模块开关，作为各子模块的单一包含入口。
- **优势**：模块化开关清晰（`USING_XHQM` 全量 / `XHQM_USE_*` 按需）；跨平台设备抽象完善（CPU/GPU/NET/AUTO 分级、检测不到自动降级）；公共头统一（NOMINMAX、导出宏、os/device 枚举、error 异常集中）；门面聚合清晰；张量 no 串行桩恒编入，关张量也能链接。**门面头与文件集的一致性本轮实测通过**：`xhqm/` 下 22 个无扩展名门面头共 113 条 `#include`，逐条解析目标路径**零悬空**。
- **劣势**：~~模块化开关与 CMake `XHQM_BUILD_*` 一一对应~~ **已作废（2026-10-07 复核 r677）**：实际是 **7 ↔ 4 的不对称** —— `xhqm.h:21-29` 给出 7 个 `XHQM_USE_*`，`CMakeLists.txt:41-44` 只有 4 个 `XHQM_BUILD_*`（TENSOR / NETWORK / FANTASY / SYSTEM），且名字对不上（`XHQM_USE_ALGEBRA` 对 `XHQM_BUILD_TENSOR`、`XHQM_USE_SOFTWARE` 对 `XHQM_BUILD_SYSTEM`）；`LATTICE / METACLASS / HARDWARE` **没有任何 CMake 开关**，它们的头被恒收进 `XHQM_HEAD_CORE`（`CMakeLists.txt:209-248`）。`head.cpp` 是含 `main()` 的草稿（`head.cpp:55`）且**确实不在构建里**（`CMakeLists.txt`、`xhqm.vcxproj` 都搜不到 `head.cpp`），但它同时含一处悬空引用（见 `DEBUG.md`）。宏函数多带来命名污染与类型安全风险。**历史（已解决，保留备查）**：门面头曾长期滞后于文件集，`xhqm/nebula`、`xhqm/astral` 一度引用已被删除的头文件而直接编译失败；本轮复核两处都已收敛（`xhqm/nebula` 现只引 B 世代 9 个头并附 A 世代移除说明，`xhqm/astral` 已删 `kernel_tag.h` 引用），故 113 条 include 零悬空。
- **缺失**：~~network/metaclass/hardware 缺 README（本次已补）~~ **已作废（2026-10-07 复核 r677）**：三者都已存在——`network/README.md`（346 行）、`metaclass/README.md`（162 行）、`hardware/README.md`（247 行）。~~构建产物提交进仓库污染源码树~~ **已作废（2026-10-07 复核 r677）**：实测 `svn ls -R` 共 527 个受控文件、**不含任何 `.so/.dll/.lib/.a/.exe/.obj/.pdb`**；`build/`、`build_lite` 在 `svn:ignore` 里，`build-shared/` 是未跟踪的本地构建目录。无 CI；门面头与文件集的一致性没有构建期校验。
- **建议**：头 main 迁入 test；收敛宏为 constexpr 模板；引入多平台多后端矩阵 CI；把 `xhqm/*` 门面头指向的文件纳入构建期存在性检查。

### 2.2 algebra 基础
- **定位**：数学地基（value/mathf/itype/ftype/ctype 五层），向上支撑张量/几何/分析/心智。
- **优势**：分层清晰、依赖单向；泛型函数 + 模板特化统一 f16/f32/f64/整型/bool/complex/dualnum/quaternion；手写 IEEE754 half；arr/tup 以字符串描述异构容器并做编译期类型映射。
- **劣势**：`algebra/value.h` 有 **10 个**常数是占位 `0.0l`（`ln_2 / ln_10 / log_e_2 / log_e_10 / log_2_e / log_10_e / sqrt_2 / sqrt_3 / sqrt_en / sqrt_pi`，`value.h:14-23`，实测计数）；整型 nan/inf 取 max、f16 下溢归零忽略 subnormal，精度语义粗糙；ctype.cpp 的 arr/tup 用 placement new + 手动析构，易出错、含 throw/注释残留。
- **缺失**：基础层无单测；泛型 read/save 默认占位；float_16 缺算术运算符重载；`<numbers>` 未启用（`value.h:4` 被注释掉）。
- **建议**：补齐占位常数；给 float_16 增加位级运算与 subnormal；补单测与 constexpr 断言；统一序列化格式文档。

### 2.3 algebra/tensor
- **定位**：N 维张量计算，模板数据层 + 运行期调度到 none/cpu/gpu/net，cpu 内按 blas→simd→线程→串行降级。
- **优势**：后端抽象干净、算子不支持即回退 CPU 保语义一致；设备覆盖广（CPU BLAS+AVX2/CUDA cuBLAS+cuSOLVER+手写 kernel/Metal 15 算子/串行桩）；类型系统严谨（dtype_of 映射、复数/对偶/四元数回退）；模板 header-only 兼容全元素类型；零拷贝视图 + matrix/vector/sparse 代数封装。
- **劣势**：单头文件偏大（~~linalg 1226 行~~ **已作废（2026-10-07 复核 r677）**：实测 `algebra/tensor/linalg.h` 已是 **1238 行**；`backend/tensor_backend_simd.h` 1475 行、`backend/tensor_backend_gpu.cu` 1710 行两项与原文一致）；部分算子为串行桩；后端接口虚函数签名碎片化、新增算子四处同步。
- **缺失**：net 分布式后端仅占位（`backend/backend.h:6` 声称 `none / cpu / gpu / net` 各为独立实现，但 `algebra/tensor/backend/` 下**没有** `tensor_backend_net.*` 文件）；缺单测/基准目录；稀疏仅 COO（`algebra/tensor/sparse.h:4` 明写「只存非零三元组（row, col, value）」），缺 CSR/CSC 与高性能 kernel。
- **建议**：补全 net 分布式后端并接入澪网络；为每算子建测试与基准；扩充稀疏格式与 CUDA/Metal kernel；拆分大文件 + 插件化注册。

### 2.4 algebra/analysis
- **定位**：MATLAB 风格数值分析接口（构造/数组/逐元素/线代/统计/多项式/信号）。
- **优势**：接口全面七大子域；后端抽象清晰（f32/f64 走 unary/elemwise/reduce kernel，其余回退）；强类型枚举替代魔法数；matrix/vector 与 tensor 零拷贝。
- **劣势**：rand/randn 固定种子不可按次设；axis::rows 归约回退通用路径性能损失；纯头文件缺显式实例化/ABI 边界。
- **缺失**：~~eigs/svds 未实现~~ **已作废（2026-10-07 复核 r677）**：单数形式**已经实现**——`algebra/analysis/linalg.h:204` 有 `svd_result<type> svd(const matrix<type>&)`、`:222` 有 `eig(...)`（返回复特征值向量），`schur`、`roots` 亦已解锁；真正「暂缓」的只是 MATLAB 风格的迭代复数形式 `eigs`/`svds`（Arnoldi/Lanczos），见 `algebra/analysis/README.md:128`；缺多维张量归约/统计；缺复矩阵完整覆盖（除 fft）。
- **建议**：补 eigs/svds；axis::rows 下沉 kernel 加基准；rand 加可注入种子；扩展 complex 覆盖。

### 2.5 algebra/geometry
- **定位**：3D 计算几何/网格生成 + N 维高维泛化，服务渲染与拾取/碰撞，含 BRep 拓扑层。
- **优势**：行主序齐次线性代数内核完整（matx/vect/poit + 投影/旋转/平移 + 行列式/逆）；形状体系广（box/cylinder/cone/torus/capsule + NURBS 求值）；高维探索成体系（hyper 第 0 层超立方/单纯形/超球）；求交/度量齐备 + 防再犯坑清单。
- **劣势**：缺单测基线，NURBS 导数/求交回归风险高；geometry.cpp 1345 行、linear.h 937 行偏大；类型命名 4/6 字母（poit/vect/tramat）学习成本高。
- **缺失**：hyper 仅第 0 层，凸几何/凸包/Voronoi–Delaunay/组合拓扑未实现；BRep 仅定义结构无造型/布尔运算；缺网格拓扑操作与序列化统一接口。
- **建议**：补求交/度量/NURBS 单测与数值基线；推进高维凸几何；实现 BRep 造型与布尔；拆分大文件。

### 2.6 algebra/soul
- **定位**：header-only 神经网络与学习库（前向组件、GPT Transformer、reverse-mode AD、循环网络、DDPM/UNet）。
- **优势**：全 header-only 模板、命名空间分层（soul/ad/diffusion）；AD 完整（类型擦除 tape、拓扑反向、checkpoint 序列化、反向闭包重建）；训练闭环（module + sgd/momentum/adam/adamw + gpt/mlp_denoiser/unet_denoiser）；生成策略丰富（贪心/nucleus/束搜索/KV-cache + RoPE）。
- **劣势**：前向 Transformer 仅 batch=1；AD 算子按 dim 手工 switch 分派难扩展；前向/反向实现重复。
- **缺失**：缺 batch>1 注意力；暂无数值量化/混合精度/算子融合；缺梯度检查/单测覆盖说明与 benchmark。
- **建议**：推广注意力到 batch 维；统一 dim 分派/模板元编程；补层归一化缓存/FlashAttention/算子融合。

### 2.7 lattice
- **定位**：图与层次数据模型基础层，统一「边类型系统」抽象链/树/网三族 + 线程安全容器 + 经典图算法。
- **优势**：统一边类型系统一套抽象覆盖三族；模板元编程成熟（select_if 编译期存储计算、CRTP 静态多态、static_assert）；指针族/连续族分层选型；图算法层齐全（拓扑/SCC/WCC/Dijkstra/Floyd/Kruskal/Prim/最大流/匹配/A*/欧拉，共 11 个）。
- **劣势**：level_tree 中间插入 O(N) 前缀重建、matrix_mesh O(N²) 增删；本地容器裸 new/delete 手动管理、reset 不释放；directed_node 与 mesh_dir 体系割裂。
- **缺失**：~~directed_mesh/matrix_mesh/graph_algorithms 原文件树缺失~~ **已作废（2026-10-07 复核 r677）**：**三个文件都在 `lattice/` 下且都在被使用** —— `directed_mesh.hpp`（4534 B）、`matrix_mesh.hpp`（10204 B）、`graph_algorithms.hpp`（16617 B）。真实的口径是**可达性不齐**，而不是文件缺失：`graph_algorithms.hpp` 在门面头 `xhqm/lattice` 里，且被生产代码 `network/nebula/find.cpp`（dijkstra 适配）与 `algebra/soul/ad_node.h` 引用；`directed_mesh.hpp` 进了 `XHQM_HEAD_CORE`（`CMakeLists.txt:237`）却**不在 `xhqm/lattice` 门面里**，只能靠直接 include；`matrix_mesh.hpp` / `sparse_mesh.hpp` / `level_tree.hpp` **既不在 CMake 头集、也不在门面里**，目前只被 `test/` 引用（分别为 `structure_test.cpp`、`sparse_mesh_test.cpp`+`ffbo/*`、`structure_test.cpp`）。即 `#define XHQM_USE_LATTICE` + `#include "xhqm.h"` 拿不到这 4 个头。`directed_node` 无文档（本次已补）；level_tree 无中间删除/换父；directed_node 无边数据；缺统一序列化 I/O。
- **建议**：统一 directed_node 与 mesh_dir 并补边数据；为连续族补增量重建；补统一序列化协议；加属性测试与基准。

### 2.8 metaclass
- **定位**：编译期元编程 / 类型反射 / 通用容器基础层（纯 header-only）。
- **优势**：纯 header-only 零编译依赖；编译期计算与类型选择体系完整（compute/template、select_if、docker/hyper）；函数反射完整（自由/指针/成员/const 成员，派生 function/function_task/function_call）；容器组职责单一。
- **劣势**：function<void> 用手动 new/delete + 静态转型、无虚析构与运行时类型标记；resource reset 语义偏差、operator _id 受限；buffer_dim 多维下标方向与行优先相反待验证。
- **缺失**：thread/ 子目录为空（实测 `metaclass/thread/` 只有 `.`/`..`，无任何文件）；head.cpp 引用不存在的 struct/values.h（**实测成立且本轮定位到行**：`head.cpp:22` 的 `#include "./metaclass/struct/values.h"`，而 `metaclass/struct/` 目录根本不存在 —— 不过 `head.cpp` 不在构建里，所以不会因此编不过；它本身含 `main()`，是草稿）；无统一聚合头、function/ 反射无单测。
- **建议**：用虚基类+unique_ptr/std::function 重写类型擦除；实现/移除 thread/ 并修复悬空引用；修正 resource 语义、补聚合头与 static_assert 测试。

### 2.9 hardware
- **定位**：通用硬件抽象层，统一 no/cpu/gpu/net/auto 设备选择语义 + header-only 并行调度骨架。
- **优势**：设备语义统一（渲染/张量共享）；编译期/运行期分离、检测不到降级；header-only 零依赖、懒初始化线程安全；降级链清晰（gpu→cpu→no）。
- **劣势**：CPU 并行未实现（`hardware/parallel/dispatch_cpu.h` 全文 91 行，第 19–88 行整块被注释掉，`xhqm::parallel` 命名空间实际是空的，仅 `#pragma once` + `#include "dispatch_no.h"`）；~~dispatch_no.h 有未完工笔误（share 构造名 array、task::call 尾逗号、delete 双分号）~~ **已作废（2026-10-07 复核 r677）**：前两处已修 —— `share` 的四个构造函数现在都叫 `share`（`dispatch_no.h:60-63`），`task::call` 声明 `virtual void call(...) = 0;` 无尾逗号（`:95`）且已补虚析构（`:94`）；**只有第三处仍在**：`array(array&&) = delete;;` 双分号（`dispatch_no.h:83`）。另外发现一处原文没记的：`share` 继承的是 `_array` 而不是为它准备的 `_share`（`:58` 对 `:39-42`），导致 `_share` 全无人用。
- **缺失**：GPU/NET 后端本体缺失（只有选择逻辑）；并行原语与 software 线程池割裂；无 share 同步/任务依赖/归约抽象。
- **建议**：落地 dispatch_cpu 多线程分块；打通 hardware 与 software 线程池；补 GPU/NET 分发骨架；修复 dispatch_no.h 笔误并补 reduce/依赖图抽象。

---

## 3. 渲染引擎（fantasy）

### 3.1 fantasy（渲染引擎总览）
- **定位**：双后端 RHI + 场景图/光照阴影/动画/后处理/空间剔除/可插拔帧图管线。
- **优势**：双后端 RHI + 单一着色器源/统一 UBO；GPU 驱动剔除链零回读；可插拔 theory_pass 帧图；渲染特性集完整（CSM/点光/IBL/PBR/SSAO/SSR/TAA/Bloom/FXAA）；配套静态分析 bug 清单（~~53+ 项已修~~ **已作废（2026-10-07 复核 r677）**：`fantasy/fantasy_bugs.md` 实际是**编号 1–43 的 43 条**（逐号核对，无跳号、无 44 及以上），其「说明」节自陈**当前剩余未修仅 #21、#22**，两项均判定为「潜在耦合、当前无实际缺陷，改需描述符/UBO 布局重构」；台账另有 2026-08-24 起的多轮修复记录，编号连续到 #43）。
- **劣势**：~~Vulkan 后端标注占位~~ **已作废（2026-10-07 复核 r677）**：那句「占位」是 `fantasy/backend/rhi_vk.h:10` 的一行**过时注释**（`// 工厂：创建 Vulkan 后端（当前为占位实现）`），与实现不符 —— `rhi_vk.cpp` 是完整的 4078 行实现，含 `vkCreateInstance`/`vkCreateDevice`/`vkCreateSwapchainKHR`/`vkCreateGraphicsPipelines`/`vkCreateDescriptorPool`/`vkCmdDrawIndexed`/`vkQueueSubmit` 等共 264 处 `vk*` 调用，`create_vk_backend()` 返回真实的静态后端单例（`rhi_vk.cpp:4072-4076`）；macOS 单独 .mm 未收敛；新旧相机并存、旧 light 节点类名复用（迁移双轨）；阴影/UBO 历史问题缺自动化回归；glTF 仅静态网格+PBR。
- **缺失**：无自动化渲染测试与黄金图；后处理缺降噪与运动矢量；描述符/UBO offset 别名耦合（bug #21/#22）；缺跨后端一致性测试。
- **建议**：重构描述符/UBO 绑定语义解决 #21/#22；建自动化回归 + CI 黄金图 + 跨后端一致性；完善 Vulkan/macOS；扩展 glTF 纹理/蒙皮/动画与后处理降噪/运动矢量。

### 3.2 fantasy/backend
- **定位**：OpenGL/Vulkan 双后端 RHI，统一着色器源、UBO 布局与着色器预编译缓存。
- **优势**：单一 render_backend 虚接口屏蔽差异；UBO 布局/着色器源双单一事实源；编译期归一化+运行期回退链；Vulkan 实现工程化（内存池子分配、环形 UBO、描述符集轮转、多窗口 surface、管线缓存键）；着色器二进制缓存 + FNV-1a 稳定哈希跨后端打标。
- **劣势**：Vulkan 后端单体类职责混杂（~~3669 行~~ **已作废（2026-10-07 复核 r677）**：实测 `fantasy/backend/rhi_vk.cpp` 已达 **4078 行**，本轮唯一仍在增长的大文件；同目录 `rhi_gl.cpp` 1151 行）；着色器预处理用字符串替换易误替换（`shader_preprocess.cpp` 299 行）；spirv.cpp 自维护 TBuiltInResource 需随 glslang 手动同步。
- **缺失**：无 D3D/Metal 原生后端；点光 cube 阴影 far 硬编码 10；二进制缓存缺 GPU vendor/renderer 级失效打标；无自动化测试/校验层。
- **建议**：拆分 Vulkan 单体类为子模块；用真正预处理器替代字符串替换；着色器缓存加 vendor 打标；补后端回归测试。

### 3.3 fantasy/runtime
- **定位**：资源注册、glTF/场景 IO、帧图编排与并行调度、性能观测与纹理压缩。
- **优势**：帧图 Kahn 拓扑自动编排并算离屏资源生命周期；glTF 原生加载与引擎 JSON 序列化互补；async_loader/command_recorder 职责分离；接口简洁、注释标注版本与限制。
- **劣势**：glTF 不含图像/蒙皮/动画/morph；错误处理靠传引用分散；frame_graph::execute 每 pass 线性遍历全部资源。
- **缺失**：缺 glTF 贴图解码与 PBR 纹理通道绑定；缺蒙皮/动画/morph 导入；缺帧图资源别名/复用；缺自动化单测。
- **建议**：扩展 gltf_loader 贴图解码并接通 material_desc 纹理通道；加蒙皮/动画导入；帧图加资源别名/子图；补单测与基准。

### 3.4 fantasy/spatial
- **定位**：渲染可见性决策层（BVH、CPU/GPU 视锥剔除、遮挡剔除、Hi-Z、LOD、渲染队列排序）。
- **优势**：职责清晰、接口小而稳定；GPU 剔除零读回（compute 直接写 DrawIndexedIndirectCommand）；遮挡剔除跨帧复用避免 stall；LOD cross-fade 混合；渲染队列排序方向正确。
- **劣势**：spatial_bvh 裸 new/delete 管理、栈压力；遮挡剔除一帧延迟易漏绘/过度绘制；GPU 着色器内联且 params SSBO 布局硬编码手打包易错；slot_of 线性查找 O(n²)。
- **缺失**：BVH 无 SAH/增量更新；extract_planes 两份重复实现；gpu_hiz_culler 缺深度缓冲读取/端到端集成；缺单测/性能基准。
- **建议**：统一 extract_planes；引入 SAH 构建与增量更新；GPU 剔除加 compact pass；补单测与剔除率统计。

### 3.5 fantasy/lighting
- **定位**：光照与阴影（light 数据环境 + 方向光/CSM/点光 cube 三类阴影图）。
- **优势**：职责分离（light 与 pass 解耦）；三类阴影图共享统一离屏目标生命周期；light_data 脏标记缓存；CSM 对数/线性混合 + point_shadow 6 面 90° 覆盖，数学正确。
- **劣势**：cutoff/outer_cutoff 存余弦但命名未体现易误填；阴影图均为深度专用、无 PCF/软阴影/CSM texel-snapping 稳定化。
- **缺失**：缺聚光灯阴影；无阴影采样/PCF/bias 管理；未实现点光逐面裁剪/分层与多光源阴影管理。
- **建议**：补 spot shadow；CSM 加 texel-snapping/紧致投影与 cascade blend；抽象统一 shadow 基类支持 PCF/ESM/VSM；点光多层渲染与阴影池化。

### 3.6 fantasy/postfx
- **定位**：全屏后处理（bloom/tonemap/fxaa/ssao/taa/ssr/compose 七个独立特效）。
- **优势**：统一 setup/apply/reset/present 生命周期；双后端着色器（GL330/VK450 内嵌按 rhi.name 自动选择）；reset 幂等回收安全；覆盖完整后处理链；细节到位（tonemap 保 alpha、TAA Halton jitter + 3x3 钳制）。
- **劣势**：七文件各自复制 quad 顶点/索引/vert 源码，无公共基类；算法简化（Bloom 单次 3x3、FXAA 5-tap、SSR 16 步）。
- **缺失**：缺历史帧持久化（TAA history 由调用方管理）；无统一编排器；apply 未 ready 静默返回无日志；参数硬编码在 shader。
- **建议**：抽取 postfx_pass 基类；引入 postfx_pipeline 编排器管理中间纹理与 TAA ping-pong；增强算法（多级降采样 Bloom、SSR 二分细化、SSAO 去噪）；外露 shader 参数为可调 member。

### 3.7 fantasy/animation
- **定位**：骨骼动画（骨骼层级树、关键帧采样、混合、CCD IK、GPU compute 蒙皮，统一产出蒙皮调色板 palette=world*inv_bind）。
- **优势**：职责划分清晰（skeleton/clip+blend/ik/gpu_skinning）；CPU/GPU 蒙皮数学约定一致并有 1e-4 容差自动验证；GPU 蒙皮走 compute+SSBO 零读回；slerp 混合去缩放/再缩放避免体积塌缩（M9 修正）；骨骼世界姿态 DFS 计算不依赖索引顺序。
- **劣势**：IK solve_ccd 每关节重算整树世界姿态 O(iter×链长×骨骼数) 冗余；clip 采样欧拉角线性插值易旋转走样且线性搜索低效；gpu_skinner 每帧全量重传、read_palette 依赖 GPU→CPU 读回。
- **缺失**：无 clip 播放控制器（循环/变速/事件/交叉淡化）；无资产导入（glTF/FBX 骨骼动画解析）；palette SSBO 未接入网格渲染；IK 仅 CCD，缺 FABRIK/关节限位/pole 约束。
- **建议**：加 clip 播放器/状态机；实现 glTF 动画蒙皮导入；palette SSBO 接入顶点着色器闭环 GPU 蒙皮；clip 通道改四元数插值；扩展 IK（FABRIK/解析式/限位/pole）并优化 CCD 增量更新。

---

## 4. 网络与分布式

### 4.1 network（网络总览）
- **定位**：自底向上跨平台传输（TCP/UDP/socket/帧协议）、加密编码（MD/SHA/AES/DES/Base64/UUID/授权）、astral 记忆内核、nebula 分布式拓扑。
- **优势**：分层清晰协议栈完整（传输→加密→记忆→分布式四层）；跨平台封装干净（平台差异单点收敛到 network.h，其余无 #ifdef，poll 规避 Windows FD_SETSIZE）；分布式设计成熟（L1 有界度邻居图+L2 事件驱动发现+L3 订阅推送，维护顺带完成）；astral 持久化海量（JSON/二进制/mmap 二级索引 10 亿级，惰性加载+分片锁）；加密自研覆盖广（SHA-1/2/3、MD2/4/5、AES/DES、UUID v1-v4）。
- **劣势**：部分接口为桩（**均本轮实测复核，原文判断成立**：AES 在 `network/aes.h:26` 声明 `enum class mode { ECB, CBC, CFB, OFB, CTR }`，但只有 `set_mode_ecb()` / `set_mode_cbc()`（`aes.h:32-33`），CFB/OFB/CTR 无 setter 也无实现；`network/socket.cpp:90-94` 的 `socket::ipv6path()` 函数体只有 `return false;`；nebula 的 `kind` 词表 `addr/addr_b.h:17-23` 有 7 项，而 `addr.cpp:12-20` 的 `addr_from` 只能造 `uuid / ipv4 / ipv6 / serial` 四种，`bluetooth/nearlink/quantum` 仍是词表预留）；注释偏实现细节而非契约文档。
- **缺失**：缺 TLS/安全传输层（明文 TCP/UDP）；缺单测与基准；nebula 缺鉴权/加密入网与信任模型（uuid 即凭证）。
- **建议**：补全 AES CFB/OFB/CTR 与 GCM 认证加密并接入事件通道；为 cipher 建官方测试向量基准；完善 nebula 入网鉴权与节点签名；补 `bluetooth/nearlink/quantum` 真实通道与封装直连/回退。

### 4.2 network/astral
- **定位**：「一切皆节点」的持久化记忆存储内核（内存主存、双向关联、冷热分离惰性加载、CBOR/JSON/mmap 持久化、只读二分索引）。
- **优势**：冷热分离惰性加载；shared_mutex 读写锁+内部 _impl 重入避免死锁；三种持久化统一、保存原子 rename 并刷新 m_offset；损坏数据段防护完善；upsert/rebuild_names 提供标签去重。
- **劣势**：info/标签查重依赖精确匹配，模糊查询全表扫描；select 对冷节点逐条加热 IO 放大；create/upsert 标签复用逻辑重复。
- **缺失**：无磁盘压缩/增量写入，append-only datas 持续膨胀；无事务/多键原子操作与崩溃恢复日志；无单测/基准与二进制版本兼容策略文档。
- **建议**：抽取标签复用公共函数；datas 加压缩/快照合并；补 WAL/事务日志；查询路径加基准与批量预取。

### 4.3 network/nebula
- **定位**：无中心、有界度、事件驱动的分布式节点组网 + 通道化数据推送（B 世代：`node → addr / data / life → link → find → post → zero/star`，详见 `network/nebula/README.md`）。
- **优势**：分层清晰且只有单一世代（L1 `link` 有界度邻居图、L2 `find` 区域图 + dijkstra 路由、L3 `post` 订阅推送、`data` 通道/窗口 + astral 记忆内核、`zero` 工具表 + 词表组装、`star` 组件组合模板）；无中心自组织（UDP 广播入网 + 心跳判死 + 自动补连接）；事件驱动且幂等（事件 id 去重 + deep 递减防环，区域图 60 s 软状态老化）；地址多态与传输解耦（`addr_uuid` 进程内回环 / `addr_ipv4` 双 reactor / `addr_ipv6` 组播 / `addr_serial` 每缆双线程，地址由句柄 `insert_addr` 注入）；文件集已与 CMake 和 VS 工程对齐（**13 个 TU** 两路构建实测链接通过）。
- **劣势**：跨线程关闭依赖 reactor 的 poll 超时唤醒（`addr/addr_ipv4.h:87` `s_react_ms = 200`、`addr/addr_ipv6.h:61` `s_react_ms = 200`、`addr/addr_serial.h:165` `s_poll_ms = 100`，三处实测），不是立即返回；r658 起**槽池必须由宿主显式安装**（`software/signal.h:49` 声明 `pool_life()`，未安装时 `software/signal.cpp:18` 直接 `throw xhqm::error("signals::pool 未安装：main 首行请调用 ...")`）；`kind` 词表实际落地 `uuid/ipv4/ipv6/serial` 四种。
- **补充（2026-10-07 复核）**：**「地址由句柄 `insert_addr` 注入」是 API 事实、但不是自动化行为** —— 全仓搜索 `insert_addr` 只有两处命中（声明 `link.h:54`、定义 `link.cpp:71`），**没有任何调用者**，`config` 里的 `answer`/`listen` 也无人读取；只 `zero(config)` 造出的节点默认没有地址、`degree()` 恒 0，装配方必须自己 `insert_addr(...)` 再调 `life()`。这是有意设计（`network/nebula/README.md:387` 明写「地址装配是外部责任」），代价是「造得出节点但入不了网」的静默失败。
- **缺失**：缺 nebula 单测/集成测试与示例节点工程（当前仅本地 `build/nebula_b_check.cpp`——实测存在、7787 B，而同目录 `build/` 在 `svn:ignore` 里，故**未入库**；另有 2026-10-07 新增的 `build/star_contract_check.cpp` 同样未入库）；缺节点间鉴权/加密（uuid 即凭证）；**A 世代残留只在测试侧，且两处都编不起来**：`test/polydtor_test.cpp` 仍用 `nebula::addr_t<nebula::kind::ipv4>`（`:54`）、`clone()`（`:40`、`:76`）、`broadcast()`（`:45`），而 `test/CMakeLists.txt:156-172` 的 `polydtor_test` 目标只列了 `../network/nebula/addr.cpp`、**缺 `addr/` 下的 5 个源文件**（B1 级的 `addr_ipv4.cpp` 等），按现状链接会缺符号；`test/nebula_api/` 同样用 `link_b` / `addr_t` / `link_t<kind>`（`nebula_api_test.cpp:28,39,58`…）且**在 `test/CMakeLists.txt` 里 0 命中、树内无构建入口**。
- **建议**：把 `nebula_b_check` 这类自检靶移入仓库并扩成回归集；补节点间鉴权/加密与长连接复用；实现 `bluetooth/nearlink/quantum` 真实通道；补 B 世代集成测试与示例；把测试夹具迁移到 B 世代 API。

---

## 5. 软件层

### 5.1 software（软件层总览）
- **定位**：信号槽/事件、datum 序列化、packet 对象树与工厂、四类运行时容器，及 system（系统原语）与 pattern（设计模式）两子模块。
- **优势**：信号槽对标 Qt（direct/queued/auto/waited，接收者析构自动断开）；system 跨平台封装完善（线程池按通道分队列+任务取消/超时、进程对标 QProcess）；pattern header-only 单/多线程双特化、多线程快照防重入死锁；datum 类型化注册表实现嵌套 JSON。
- **劣势**：手写 JSON 对 text 不转义、含引号/逗号会损坏（实测成立：`software/datum.h:120` 是 `data_str = ("\"" + std::string(*data_ptr) + "\"");`，只加引号不做转义）；signal.h 遮蔽系统 `<signal.h>` 依赖 include_next（实测成立：`software/signal.h:10` 的 `#include_next <signal.h>`，由 `signal.h:9` 的 `#if !defined(_WIN32) && !defined(_MSC_VER) && (defined(__GNUC__) || defined(__clang__))` 守住；MSVC 侧由 `CMakeLists.txt:51-72` 把 UCRT 头目录排到最前解决）；command/project/utility_root 的 redo/save/read 多空实现（`software/command.cpp` 仅 87 行、`project.cpp` 31 行、`utility.cpp` 70 行；空函数体实测：`command::save` `command.cpp:36-38`、`command::read` `:40-42`、`command_root::redo(std::string)` `:77-79`、`command_root::save` `:80-82`、`command_root::read` `:83-85`）。
- **缺失**：datum read_list 不自动扩容、packet::save 未确保目录存在；无单测/示例；两套命令机制未统一；无顶层聚合头。
- **建议**：完善 datum 字符串转义与扩容；补齐 command/project/utility_root 持久化；统一命令机制并补测试；新增顶层 software.h 聚合头。

### 5.2 software/system
- **定位**：操作系统抽象层（编码/文件/时间/进程/动态库/线程池）。
- **优势**：跨平台双后端封装（XHQM_WINDOWS/POSIX）接口统一；线程池多通道轮转、弹性扩缩、CAS 状态机、异常不外泄；进程执行对标 QProcess（后台读管道避免死锁）；动态库 RAII+强类型函数指针模板零开销、支持中文路径与主程序符号导出。
- **劣势**：codesys 依赖已弃用的 std::codecvt_utf8（实测成立：`software/system/codesys.cpp:4` 含 `<codecvt>`，`:96`、`:101` 用 `std::wstring_convert<std::codecvt_utf8<wchar_t>>`）；filesys 多数仅返回 bool 不捕获 std::filesystem 抛错（实测成立：`filesys.h` 全文 `catch` 命中数为 **0**）；大量静态方法+全局/thread_local 状态可测试性与并发语义弱。
- **缺失**：filesys 缺递归目录复制/删除自定义实现；timesys 缺 steady_clock 单调时钟（实测成立：`timesys.h`/`timesys.cpp` 中 `steady_clock` 命中数均为 0）；线程池无任务优先级/per-channel 容量；进程无跨平台信号/信号量/进程组管理。
- **建议**：以 std::filesystem::recursive 或平台 API 补递归复制删除并统一异常包装；新增 steady_clock 计时；线程池加优先级与背压；迁移 codecvt 到现代 UTF 方案。

### 5.3 software/pattern
- **定位**：基础设计模式库（单例/观察者/通知者/命令/责任链/策略/状态机/工厂），每组件支持单线程无锁/多线程加锁双特化。
- **优势**：统一线程模型抽象（thread 模板切换单/多线程，默认无锁零开销）；多线程普遍快照拷贝后回调避免重入；纯头文件依赖极小；类型安全（保留编译期类型信息）。
- **劣势**：单例注册表裸指针无析构/释放顺序管理；notifier 仅支持普通函数指针，无法注册 lambda/成员函数（实测成立：`software/pattern/notifier.h:20` 与 `:56` 都取 `xhqm::function<func_type>::function_ptr`，而该别名在 `metaclass/function/function.h:85`（自由函数）与 `:133`（成员函数）定义为**裸函数指针**类型 `_return_type(*)(_args_type...)` / `_return_type(_class_type::*)(_args_type...)`，不是 `std::function`）。
- **缺失**：缺单测与编译覆盖；缺模块级聚合头；状态机不支持进入/退出动作与非法转换策略。
- **建议**：为 observer/notifier 补重入防护；instance 注册表补显式销毁有序析构；补单测示例与聚合头。

---

## 6. 测试层

### 6.1 test
- **定位**：可执行验证层：Catch2 单元测试 + soul-exp 模型推理实验库（sport）+ RhiViewport 渲染查看器（view），三套独立 CMake 工程。
- **优势**：覆盖完整（algebra/tensor/soul/lattice/software/network/hardware/metaclass/astral/cs 全核心，含纯算法/并发压测/持久化往返）；多后端一致性验证（gpu_smoke/tensor_cpu_test 跨 no/cpu/gpu 对照 locked 调度链）；sport 数值精度锁定（峰值归一相对误差对照 PyTorch，wav2vec2/DINOv2/motion-gen 1e-5~1e-7，权重逐字节可复现）；view 是生产级查看器兼诊断工具（双后端、多相机、内建闪帧诊断）。
- **劣势**：多数测试编入引擎源文件、少部分链 DLL，两套链接方式 ABI/宏差异易踩坑；view/main.cpp 内嵌大量就地验证代码 + `#if 0` 折叠职责混杂（实测 `test/view/main.cpp` 943 行、内含 2 处 `#if 0`）；缺统一 runner/CI 清单需逐个手跑（`test/CMakeLists.txt` 有 **25 个 `add_executable`**，全仓无 `enable_testing()`/`add_test()`/`CTestTestfile.cmake`，也无 `.github`/`.gitlab-ci.yml`）。
- **缺失**：~~无根 README/测试索引（本次新建）~~ **已作废（2026-10-07 复核 r677）**：`test/README.md` 已存在（237 行）；**但有一个测试文件没接上构建**：`test/sparse_mesh_test.cpp` 在 `test/CMakeLists.txt` 里**没有任何 target**（全仓搜索该文件名 0 命中），当前编不到、跑不到；sport CUDA 后端仅标「无收益」缺跨平台基准与自动后端选择（`test/sport/README.md:100-101` 给出实测：wav2vec2 forward gpu≈cpu 277 vs 270 ms、生成器小矩阵 GPU 慢 2× 46 vs 21 ms/帧，故默认 CPU）；`test/view/` **没有 README**（同层 `sport`/`cellsim`/`ffbo`/`nebula_api` 都有）；view 无离屏/无窗口的 headless 渲染测试入口。
- **建议**：引入统一测试编排（ctest/脚本）汇总所有 target 输出单份通过率；将 view 内嵌验证抽离为独立 headless 测试；为 sport 增加 CI 基准脚本与 CPU/GPU 自动判定，沉淀为可复用推理 runtime。

---

## 结语

xhqm 是一个架构成熟、多后端抽象完整、覆盖数学/渲染/网络/系统/测试的高质量引擎，尤其在**多后端调度一致性**、**GPU 驱动渲染管线**、**无中心分布式设计**与**海量持久化**上展现了较强的工程功底。当前最主要的提升空间集中在：

1. **自动化与回归**：补单元测试基线、CI 多平台多后端矩阵、渲染黄金图对比。
2. **占位桩落地**：tensor 的 net 分布式后端、hardware 的 CPU/GPU/NET 分发（`dispatch_cpu.h` 整块注释）、`bluetooth/nearlink/quantum` 通道（~~serial/quantum~~ **已作废（2026-10-07 复核 r677）**：`serial` 已落地，实现类 `network/nebula/addr/addr_serial.{h,cpp}` 在文件树与 CMake BODY 里、每缆读写双线程、`s_poll_ms = 100`）、MATLAB 迭代式 `eigs/svds`（单数 `svd`/`eig` 已实现）、BRep 造型等尚未实现的能力。
3. **文档一致性**：消除 Windows 硬编码路径遗留（**只余 `test/` 与 CMake 的 PDB 路径，库源码 0 处**），统一接口契约注释，收敛拆分库命名。
4. **分布式闭环**：打通 nebula 模板世代迁移、鉴权加密与 astral 的查询/持久化。

---

> **下一步该看什么**：本文件的第 2–6 节是模块全景（定位/优势/劣势/缺失/建议），**不是缺陷台账**。要接手修问题，直接看 `DEBUG.md`（根模块的当前问题清单，每条带 `文件:行号`）；模块级细节与历史台账见各子目录：`fantasy/fantasy_bugs.md`（static-analysis 清单 + 逐轮修复记录）、`network/nebula/DEFERRED.md`（讨论过但暂不做的事项）、以及各模块自己的 `README.md`。


---

## 许可

Apache License 2.0，见 `LICENSE`。

```
Copyright 2026 Stella Mira (星辉澪)
```
