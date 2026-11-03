# tensor 张量库

> 最后同步：2026-10-07，对照工作副本 r677。

xhqm 的 N 维张量模块：以模板化的 `tensor` / `tensor_view` 提供存储、索引、广播与零拷贝视图，
把逐元素、线性代数、卷积、池化等计算经运行时可切换的 `tensor_backend` 调度到
~~none / cpu / gpu / net 四类设备~~ **已作废（2026-10-07 复核 r677）**：设备枚举是
`no / cpu / gpu / net / auto_`（`xhqm::hardware::device`，`hardware/hardware.h:35-42`），但其中
`net` **没有实现类**，被 `create_backend` 直接映射成 cpu（`backend/tensor_backend.cpp:51-56`）；
cpu 后端内部再按 blas → simd → 多线程 → 串行四级降级。

## 模块定位

- **数据层**：`tensor<dim, type>`（拥有存储）、`tensor_view<dim, type>`（基于 stride 的非拥有视图）、
  `matrix<type>` / `vector<type>`（二维/一维浅封装）、`sparse_matrix<type>`（COO 稀疏，唯一稀疏格式）。
- **计算层**：`xhqm::tensor_base` 命名空间下的 `linalg` / `norm` / `nn` / `conv` / `pool` /
  `random` / `shape` / `stats` / `signal` / `poly` / `conv_grad` 等算子。~~全部经后端并行调度。~~
  **已作废（2026-10-07 复核 r677）**：实测调用 `tensor_backend_active()` 的只有 linalg(21 处)、pool(12)、
  tensor_base(8)、norm(7)、nn(4)、signal(3)、conv(3)、random(2)、sparse/matrix/conv_grad(各 1)；
  `stats.h` / `poly.h` / `shape.h` **一次都不调用**（文件头自述「串行桩」），`signal.h` 仅在
  `conv1d_full` / `conv2d_full` / `xcorr` 三处下沉到 `conv3d`（`signal.h:195,217,242`），
  而 `fft` / `ifft` / `interp1` / `trapz` / `filter` 全是纯串行。
- **后端层**：`tensor_backend` 纯虚接口 + ~~none/cpu/gpu/net 四实现~~ **已作废（2026-10-07 复核 r677）**：
  只有 `tensor_backend_no` / `tensor_backend_cpu` / `tensor_backend_gpu`（CUDA 或 Metal 二选一）
  三个实现类，`net` 无实现类；cpu 下含 AVX2+FMA SIMD 内核（MSVC 专属，见「编译配置」），
  gpu 下含 CUDA（cuBLAS/cuSOLVER + 手写 kernel）与 Metal（Apple Silicon）两条真实 GPU 路径。
- **数值层**：`figure<type>` 标量封装（~~f16/f32/f64/bool 特化~~ 实为 `figure<f16>` 与 `figure<bool>` 两个特化，
  见下表）、`complex` / `dualnum` / `quaternion` / `dual_quaternion` 等复合数类型，供张量元素类型直接使用。

## 目录结构

```
algebra/tensor/
├── tensor.h                聚合头（仿 fantasy/fantasy.h）；**不含** stats/signal/poly/conv_grad，用它们须单独 include
├── tensor_base.h           存储 + 索引 + 逐元素基础（header-only，模板约束）
├── tensor_view.h           tensor_view 视图（切片 / 转置 / 广播 / reshape，header-only）
├── linalg.h                线性代数（matmul / transpose / reduce / broadcast / eig / svd / lu / qr / ...）
├── norm.h                  归一化与 softmax（softmax / layer_norm / rms_norm / group_norm / batch_norm(_eval) / update_running_stats，header-only）
├── nn.h                    神经网络基础算子（where / gather / scatter_add / dropout / softmax_cross_entropy）
├── conv.h                  卷积（conv1d/2d/3d/4d，groups/bias）
├── conv_grad.h             卷积反向（conv1d/2d/3d 的 grad_weight / grad_input / grad_bias + im2col2d / col2im2d）
├── pool.h                  池化与上采样（max/avg pool1d/2d/3d + upsample_nearest2d / bilinear2d）
├── random.h                随机张量（rand / randn，并行可复现）
├── shape.h                 形状变换（squeeze / unsqueeze / reshape_view）
├── figure.h                标量封装
├── vector.h                vector（动态 n 维）
├── matrix.h                matrix（动态 m×n）
├── sparse.h                稀疏矩阵（COO）
├── complex.hpp             复数
├── dualnum.hpp             对偶数（前向自动微分）
├── quaternion.hpp          四元数
├── dual_quaternion.hpp     对偶四元数
├── store.h                 元素存储（bool 用 figure<bool> 连续内存）
├── poly.h                  多项式
├── signal.h                信号（fft / ifft / interp1 / 卷积 / 滤波）
├── stats.h                 统计
├── backend/
│   ├── backend.h           设备枚举 + 编译宏归一化 + 运行时选择
│   ├── tensor_backend.h    虚接口 tensor_backend
│   ├── tensor_backend.cpp  调度：tensor_backend_active() / tensor_backend_select()
│   ├── tensor_backend_no.h/.cpp     串行桩
│   ├── tensor_backend_cpu.h/.cpp    多线程 CPU 后端（blas → simd → thread 降级）
│   ├── tensor_backend_simd.h        AVX2+FMA SIMD 内核（MSVC SVML 专属，1475 行）
│   ├── tensor_backend_gpu.h/.cpp    GPU host 侧（CUDA/Metal 分派；无 CUDA 时为全 false 桩）
│   ├── tensor_backend_gpu.cu        CUDA 内核（cuBLAS/cuSOLVER + 手写 kernel，1710 行）
│   └── tensor_backend_metal.h/.mm   Metal 后端（macOS/Apple Silicon，1525 行）
└── README.md               本文件（使用说明）
└── DEBUG.md                当前仍存在的问题台账（35 条，2026-10-07 复核 r677）
```

规模（r677 工作副本实测 `wc -l`，36 个源码文件合计 11417 行；不含 README.md / DEBUG.md）：

| 文件 | 行 | 文件 | 行 |
|---|---|---|---|
| `backend/tensor_backend_gpu.cu` | 1710 | `backend/tensor_backend_metal.mm` | 1525 |
| `backend/tensor_backend_simd.h` | 1475 | `linalg.h` | 1238 |
| `backend/tensor_backend_cpu.cpp` | 864 | `tensor_base.h` | 431 |
| `conv_grad.h` | 409 | `backend/tensor_backend.h` | 358 |

`tensor` / `tensor_view` / `linalg` / `norm` / `nn` / `pool` / `shape` 等是模板，
实现留在头文件内；只有非模板的 backend 层做 `.h/.cpp`（或 `.cu` / `.mm`）分离
（对齐 fantasy 的 backend，但 fantasy 能用固定 `value` 而 tensor 泛型）。

## 关键类 / API

### 数据类（命名空间 `xhqm`）

> 命名空间订正（2026-10-07 复核 r677）：`tensor` / `tensor_view` / `matrix` / `vector` / `figure` 在
> `xhqm`（`tensor_base.h:11`、`tensor_view.h:10`、`matrix.h:14`、`vector.h:14`、`figure.h:10`）；
> `sparse_matrix` 与 `store` 在 **`xhqm::tensor_base`**（`sparse.h:13`、`store.h:14`）。

| 类 | 说明 | 关键成员 |
|---|---|---|
| `tensor<dim, type>` | N 维张量（拥有存储，行优先） | `shape()/size()/dimension()`、`reshape/redimension`、`index(coord)↔coord`、`data()`、`operator[]/()`、逐元素 `+ - * /`（Hadamard）、`+= -= *= /=`、`slice/broadcast_binary` |
| `tensor_view<dim, type>` | 基于 stride 的非拥有视图 | `transpose(a,b)`、`broadcast(dims)`、`slice<pos>(i)`、`reshape(dims)`、`is_contiguous()`、`offset(coord)` |
| `matrix<type>` | 二维矩阵浅封装 | `rows()/cols()`、`operator*`（矩阵乘）、`transpose()`、`det()`、`data()`、逐元素 `+ -` 与 `mul()` |
| `vector<type>` | 一维向量浅封装 | `operator*`（点积）、`cross()`（3 维）、`norm()/normalize()`、`mul()/hadamard()` |
| `sparse_matrix<type>` | COO 稀疏矩阵 | `rows()/cols()/nnz()`、`transpose()`、`dense()`、`operator*`（×向量/×矩阵/×稀疏） |
| `figure<type>` | 标量数值封装 | ~~f16/f32/f64/bool 特化~~ 实际只有 `figure<f16>`（`figure.h:64`）与 `figure<bool>`（GF(2)：`+/-`=XOR、`*//`=AND，`figure.h:116`）是特化，f32/f64/fld 走主模板（`figure.h:16`）；四则/比较/数学函数 |
| `store<type>`（`xhqm::tensor_base`） | 元素存储容器 | `bool` 特化用 `figure<bool>` 连续内存，规避 `std::vector<bool>` 位压缩（`store.h:39-63`） |

### 后端接口（命名空间 `xhqm::tensor_base`）

- `enum dtype`：`unknown/f16/f32/f64/fld/i8/i16/i32/i64/boolean`，经 `dtype_of<type>` 编译期映射，非精确标量（复数/对偶数/四元数）一律 `unknown` 回退 CPU。
- `enum unary_op`（43 项）：relu/leaky_relu/sigmoid/gelu/silu 等激活 + exp/log/三角/双曲/rounding/sqrt/erf/abs/sign/isnan 等。
- `enum elemwise_op`：`+ - * / atan2 hypot pow mod rem`；`enum reduce_op`：sum/max/min/prod；`enum grad_op`：relu/leaky_relu/sigmoid 反向。
- `class tensor_backend`（`xhqm_io` 纯虚接口）：`parallel_for`（并行调度）＋ `elemwise/select/matmul/reduce/unary/transpose/grad_accum/spmv/pool2d/upsample2d/dropout/conv3d/conv4d/trsv/chol`（各算子不支持时返回 `false`，调用方回退 CPU 通用路径）＋ `upload/download/synchronize`。
- 运行时调度：`tensor_backend_active()` 取当前后端；`tensor_backend_select(device)` 切换。

### 算子（命名空间 `xhqm::tensor_base`，均为模板）

| 文件 | 主要 API |
|---|---|
| `linalg.h` | `matmul`、`transpose`、`reduce/reduce_sum/max/min/prod/last`、`sum_all/prod_all/max_all/min_all`、`argmax/argmin(_last/_all)`、`broadcast_to`、`eig_sym/svd/pinv/lu/qr/schur/eig`、`trsv_lower/upper`、`chol`、`inv/identity/rref/expm/sqrtm/logm`、`norm_fro/one/inf/spectral`、`trace`、`cumsum/cumprod(_last)/cummax_last/cummin_last`、`sort/sort_index`、`all/any` |
| `norm.h` | `softmax(x, axis)`、`layer_norm(x, gamma, beta, eps, axis)`、`rms_norm`、`group_norm`、`batch_norm`(训练态)、`batch_norm_eval`、`update_running_stats` |
| `nn.h` | `where`、`gather`、`scatter_add`、`dropout`、`softmax_cross_entropy` |
| `conv.h` | `conv_nd<S>`（S=1..4）、`conv1d/2d/3d/4d`、`conv_out_size`，channels-first + groups/bias |
| `conv_grad.h` | `im2col2d/col2im2d`、`conv1d/2d/3d` 的 `grad_weight/grad_input/grad_bias`（conv2d 走 im2col+GEMM，1d/3d 直连循环） |
| `pool.h` | `max_pool1d/2d/3d`、`avg_pool1d/2d/3d`、`upsample_nearest2d`、`upsample_bilinear2d`（align_corners=false） |
| `random.h` | `rand/randn`、`rand_uniform/rand_normal`、`rand_seed_at`（splitmix64 派生 + xorshift32 + Box-Muller，并行可复现） |
| `shape.h` | `squeeze/unsqueeze/reshape_view` |
| `stats.h` | `var/stddev/cov/diff/gradient/median/mode/prctile/quantile/iqr/nansum/nanmean/nanmax/nanmin/ismember/unique/sortrows/intersect/setunion/setdiff` |
| `signal.h` | `fft/ifft/fftshift/ifftshift/interp1/trapz/cumtrapz/filter/conv1d_full/conv2d_full/xcorr`（fft 仅 2 的幂长度） |
| `poly.h` | `polyval/deconv/polyder/polyint`（多项式） |

## 架构

```
tensor / tensor_view / matrix / vector / sparse      （数据层，模板 header-only）
        │  调用
        ▼
tensor_backend_active()  ──►  tensor_backend（虚接口）
        │
        ├─ no    ：串行桩，算子全部回退模板标量语义
        ├─ cpu   ：blas(matmul/conv) → simd(elemwise/reduce/unary/...) → 多线程 → 串行 四级降级
        ├─ gpu   ：CUDA（cuBLAS/cuSOLVER + 手写 kernel）或 Metal（Apple Silicon compute）
        └─ net   ：分布式占位，当前回退 cpu
```

- 后端只负责「并行调度 + 裸指针 kernel + 内存搬运」，元素语义统一由上层模板经 `xhqm::` 泛型函数实现，
  保证 no/cpu/gpu 的数值结果一致。
- `dtype_of<type>` 编译期判定元素类型，加速后端只对精确标量开 kernel，其余回退 CPU，
  避免按 `sizeof` 误判（如 `complex<float>`、`dualnum<float>` 均为 8 字节会被当成 double）。
- 设备枚举 `device { no = 0, cpu = 1, gpu = 2, net = 3, auto_ = 4 }` 沿用 `hardware::device`
  （`hardware/hardware.h:35-42`，tensor 侧只有 `using device = xhqm::hardware::device;`，`backend/backend.h:22`）。
  ~~与 `parallel::equipment`、`device::type` 语义一致、独立声明、按数值映射。~~
  **已作废（2026-10-07 复核 r677）**：`xhqm::parallel::equipment` 只存在于 `hardware/README.md` 的文档里，
  代码中无此枚举（`xhqm::parallel` 命名空间在 `software/system/threads/*`，但无 `equipment`）；
  `xhqm::device::type` 是另一个 8 值枚举（`head.h:145-154`），前 4 项 no/cpu/gpu/net 与 `hardware::device`
  数值一致，但第 5 项是 `device_memory = 4`，与 `auto_ = 4` 数值撞车，不能一一映射。
- 并行阈值 `XHQM_PARALLEL_THRESHOLD = 4096`（`backend/tensor_backend.h:17-19`）：~~数据量小于该值时并行路径退回串行~~
  **订正（2026-10-07 复核 r677）**：实际只有 5 处消费该阈值——`cpu_backend::parallel_for`
  （`backend/tensor_backend_cpu.cpp:493`）、`spmv_arith`/`spmv_bool`（`tensor_backend_cpu.cpp:131,163`）、
  `all/any`（`linalg.h:837,850`）；SIMD 内核、BLAS `matmul`、GPU kernel 都不看它。

## 契约与编译配置

- 命名空间：张量类 `xhqm::tensor`（对齐 `xhqm::matrix`），视图 `xhqm::tensor_view`；
  后端 / 线性代数 / 存储 / 设备枚举 / 归一化 / 神经网络算子 / 池化 / 随机 / 形状变换在 `xhqm::tensor_base`。
- 编译宏（~~由 CMake 注入~~ **订正（2026-10-07 复核 r677）**：只在 CMake 里注入，源码内没有任何 `#define` 兜底，
  手工编译（非 CMake）不定义它们会静默退化成「并行桩」）：
  - `TENSOR_WITH_CPU`：编译 CPU 后端（OFF=仅并行桩；ON=含 SIMD/BLAS）
  - `TENSOR_WITH_GPU`：编译 GPU 后端（`XHQM_TENSOR_CUDA` / `XHQM_TENSOR_METAL` 为实际后端）
  - `TENSOR_WITH_SIMD` / `TENSOR_WITH_BLAS`：CPU 加速层，注入条件见下
- 由 `CMakeLists.txt` 实测的注入条件（r677）：
  - `TENSOR_WITH_CPU`：`XHQM_BUILD_TENSOR=ON` 且模式 OFF/ON（`CMakeLists.txt:148-150`）；模式 NO 时连
    `tensor_backend_cpu.cpp` 都不编译（`CMakeLists.txt:630-632`），只剩 no 桩。
  - `TENSOR_WITH_SIMD`：**仅 MSVC**（`CMakeLists.txt:158-162`，并附 `/arch:AVX2`）——因为内核用的是
    MSVC SVML 内建（`_mm256_atan2_ps` / `_mm256_pow_ps` / `_mm256_erf_ps`，`backend/tensor_backend_simd.h:72-76,483-489`），
    GCC/Clang 无这些内建。
  - `TENSOR_WITH_BLAS`：需要 `depend/openblas` 下存在 `include/cblas.h` 与 `lib/libscipy_openblas.lib`
    （`CMakeLists.txt:166-168`），符号带 `scipy_` 前缀，源码用宏改写调用名（`backend/tensor_backend_cpu.cpp:9-26`）。
  - GPU：`APPLE` → `XHQM_TENSOR_METAL` + `.mm` + Metal/MPS 框架；否则探测 CUDAToolkit → `XHQM_TENSOR_CUDA` +
    `.cu`；探测不到也仍编译 `tensor_backend_gpu.cpp` 的桩（`CMakeLists.txt:633-663`）。
- CMake 缓存变量：
  - `XHQM_DEVICE`：NO / CPU / GPU / NET / AUTO（默认 CPU）
  - `XHQM_TENSOR_BACKEND`：NO / OFF / ON（自动检测 SIMD/BLAS/CUDA/Metal），默认 ON
  - `XHQM_RENDER_BACKEND`：AUTO / VULKAN / OPENGL（渲染后端，仅 device=GPU 时生效；AUTO 下该变量也被忽略）
- 后端加速实现状态：
  - **CPU**：blas → simd → 多线程 → 串行四级降级。`matmul`/`conv`(f32/f64)/`trsv`/`chol` 走 BLAS，
    逐元素/归约/一元/转置/`select` 走 SIMD（AVX2+FMA，仅 MSVC），整型/bool 有专属 SIMD 内核；
    **f16 的 conv 无 BLAS 路径**（`tensor_backend_cpu.cpp:736-742` 只认 f32/f64），会退回 `conv_nd` 直接卷积。
  - **CUDA**：`tensor_backend_gpu.cu`（1710 行）实现真实 GPU kernel——`matmul` 走 cuBLAS
    （f16 用 `cublasGemmEx`，f32/f64 用 `cublasSgemm/Dgemm`，`tensor_backend_gpu.cu:445,482,522`）；
    `chol` 走 cuSOLVER `potrf`（f32/f64，`:1312-1325`）；`trsv` 的 f32/f64 也走 cuBLAS，
    仅 i8/i16/i32 用手写前代/回代 kernel（`:1281-1283` vs `:1496` `launch_trsv_int`）；
    其余 12 个算子为手写 kernel（`elemwise`/`select`/`pool2d`/`upsample2d`/`dropout`/`reduce`/`unary`/
    `grad_accum`/`transpose`/`spmv`/`conv3d`/`conv4d`，均可核对到 `xhqm_cuda_*` 入口）；
    `dropout` 的 device 端 rng 与 CPU `rand_seed_at` 同为 splitmix64 → xorshift32 → `>>8 / 2^24`
    （`tensor_backend_gpu.cu:129-147,151-163` vs `random.h:15-35`），同 seed 可复现。
    编译带 `nvcc -Xcompiler /utf-8`（`CMakeLists.txt:27`），架构默认 `XHQM_CUDA_ARCH=86`（`CMakeLists.txt:652-654`）。
    `upload`/`download` 是空实现（数据每次调用现拷，`tensor_backend_gpu.cpp:248-249`），
    设备缓冲由 512 MiB 上限的池复用（`tensor_backend_gpu.cu:166-198`，详见 `DEBUG.md` B 组）。
  - **Metal**：`tensor_backend_metal.mm`（1525 行）在 macOS/Apple Silicon 上提供全部 15 个算子的 Metal compute kernel，
    支持类型比旧文更宽：~~f16/f32~~ **订正（2026-10-07 复核 r677）**：`metal_dtype` 接受
    f16/f32/i8/i16/i32 五种（`tensor_backend_metal.mm:885-899`），f64/bool 返回 -1 后回退 CPU。
  - 不反向依赖 `fantasy` 的 rhi，避免底层张量库依赖渲染引擎的层级倒挂。

## 依赖

- **xhqm 内部**：`head.h`（`xhqm::index/count/size`）、`algebra/ftype.h`（`f16/f32/f64/fld` 及泛型数学函数）、
  `algebra/mathf.h` / `itype.h`（`number<type>` 常量、整型别名）、`hardware/hardware.h`（`device` 枚举与运行时设备选择）。
- **标准库**：`<array>/<vector>/<numeric>/<algorithm>/<functional>/<cmath>/<atomic>/<execution>/<cstdint>` 等。
- **可选加速**：BLAS（CPU matmul/conv）、`<immintrin.h>`（AVX2+FMA SIMD）、CUDA Toolkit（cuBLAS/cuSOLVER）、
  Metal（macOS/Apple Silicon，`Metal Performance Shaders`）。
- 层间关系：`tensor` 不反向依赖 `fantasy` 渲染引擎；`matrix/vector/sparse` 在 tensor 之上，是代数语义的浅封装。

## 定义与约定

（后端四层分级；编译期与运行期一致，先检测、检测不到回退）

| 层级 | 含义 |
|---|---|
| CPU 串行 | 单线程串行实现 |
| CPU 并行 | 多线程并行实现，仅限 `std::thread` 与 `parallel_for` |
| CPU 扩展 | SIMD 与 BLAS（统一视作 CPU 的扩展实现） |
| GPU 扩展 | CUDA 与 Metal |

- CPU 串行与并行应当使用模板实现，自然兼容所有类型。
- CPU 并行线程数为 `std::thread::hardware_concurrency()`。
- 数据量 < `XHQM_PARALLEL_THRESHOLD`（4096）时回退单线程——只对 `parallel_for` / `spmv` / `all,any`
  生效，SIMD/BLAS/GPU 路径不受此阈值控制（见上文「架构」的订正）。
- 逐元素 `operator*` 是 Hadamard 积，不是矩阵乘；矩阵乘在 `linalg.h` 的 `matmul`（`matrix::operator*`）。
- 高维坐标 ↔ 一维下标为行优先（row-major）展开。

## 简要用法示例

包含路径订正（2026-10-07 复核 r677）：~~`#include "xhqm/algebra/tensor/tensor.h"`~~
**该路径不存在**——`xhqm/` 目录里放的是无扩展名的模块聚合头（`xhqm/algebra`、`xhqm/fantasy` …），
没有 `xhqm/algebra/tensor/` 这层。真实写法是以仓库根为包含根：

```cpp
#include "algebra/tensor/tensor.h"   // 聚合头；测试与 soul 都是这种相对写法
// 或是仓库自带的伞头：#include "xhqm/algebra"

// 聚合头不含 stats/signal/poly/conv_grad，用到它们要单独 include（否则调用点报「不是成员」）
#include "algebra/tensor/stats.h"

using namespace xhqm;

// 1) 构造张量（2×3，全 0），逐元素赋值
tensor<2, f32> a({2, 3}, 0.0f);
a(0, 1) = 2.0f;              // 或 a[{0,1}] / a[1]
tensor<2, f32> b({2, 3}, 1.0f);

// 2) 逐元素四则（Hadamard）与广播
auto c = a + b;              // 逐元素相加
auto d = a * 2.0f;           // 标量缩放
a += b;                      // 就地累加（形状相同走后端 elemwise）

// 3) 矩阵乘（linalg / matrix）
tensor<2, f32> A({2, 4}, 1.0f), B({4, 3}, 2.0f);
auto C = tensor_base::matmul(A, B);            // 2×3，走后端 BLAS/SIMD
xhqm::matrix<f32> M(2, 2, 1.0f), N(2, 2, 2.0f);  // 构造是 (rows, cols, 初值)
auto P = M * N;                                // matrix::operator* 即矩阵乘

// 4) 归约 / 归一化
auto s = tensor_base::reduce_sum(A, 1);        // 沿轴 1 求和
auto soft = tensor_base::softmax(C, 1);        // 数值稳定 softmax

// 5) 神经网络算子
tensor<4, f32> img({1, 3, 32, 32}, 0.0f);      // NCHW
auto pooled = tensor_base::max_pool2d(img, 2, 2, 0);   // (kernel, stride, padding)

// 6) 随机张量（并行可复现，seed 默认 5489）
auto r = tensor_base::rand<f32, 2>({3, 3});   // [0,1) 均匀
auto g = tensor_base::randn<f32, 2>({3, 3});  // N(0,1)

// 7) 视图（零拷贝转置/切片/广播）
tensor_view<2, f32> v(A);
auto vt = v.transpose(0, 1);
auto vs = v.slice<0>(0);                       // 取第 0 行，降为 1 维

// 8) 稀疏矩阵 × 向量（COO；先由稠密矩阵构造，默认构造是 0×0 空矩阵）
xhqm::matrix<f32> dense(3, 3, 0.0f);
dense(0, 0) = 2.0f; dense(1, 2) = 3.0f;
tensor_base::sparse_matrix<f32> S(dense);      // 提取 |v| > eps 的三元组
xhqm::vector<f32> x(3, 1.0f);
auto y = S * x;                                // SpMV，走后端 kernel（失败回退串行）

// 9) 运行时后端切换（no/cpu/gpu/auto_；net 会落到 cpu）
tensor_base::tensor_backend_select(xhqm::tensor_base::device::cpu);
```

> 该示例已用 `g++ -std=c++17 -I. -x c++ - -fsyntax-only` 逐条核对编译通过（2026-10-07，r677）。
> 旧版示例里的 `matrix<f32> M({2, 2}, 1.0f)` 与 `#include "xhqm/algebra/tensor/tensor.h"`
> 均无法编译（前者无匹配构造函数，见 `matrix.h:24,26,43`），已按实测改写。

验证入口（本模块**有单测**）：`test/tensor_serial_test.cpp`（568 行，12 个 `TEST_CASE`，no 后端）、
`test/tensor_cpu_test.cpp`（711 行，24 个 `TEST_CASE`，cpu 后端 + SIMD/BLAS；MSVC 侧附加
`XHQM_DEVICE_CPU TENSOR_WITH_CPU TENSOR_WITH_SIMD TENSOR_WITH_BLAS` 与 `/arch:AVX2`）、
`test/algebra_test.cpp`、`test/gpu_smoke.cpp`（链接预编译 xhqm.dll 的 GPU 卷积冒烟）。
基准目录：**没有**（全仓无 bench/perf 目录，`find -iname "*bench*"` 为空）。

## 当前问题清单

成熟度：中高——CPU/CUDA/Metal 三条路径都是真货且带单测，缺的是基准测试、非 MSVC 的 SIMD/BLAS 路径，
以及 `net` 后端的实现。

**当前仍存在的问题统一记在 `DEBUG.md`（同目录）**，本文件不重抄。历史台账（`fantasy/fantasy_bugs.md`、
`network/nebula/DEFERRED.md`）按本仓约定不搬家、不改名，各自保留。
