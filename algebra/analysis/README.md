# analysis 分析模块

> 最后同步：2026-10-07，对照工作副本 r677。

> `xhqm::analysis` —— MATLAB 风格的数值分析接口层。在 `xhqm::tensor` / `matrix` / `vector` 与 `tensor_base` 线性代数后端之上，提供数组构造、数组操作、逐元素数学、线性代数、归约统计、多项式与信号处理等函数，全部为纯模板头文件实现。

---

## 1. 模块定位

- 命名空间：`xhqm::analysis`（与 `xhqm` 标量数学命名空间隔离，避免 `sum` / `abs` / `sin` 等重载二义）。
- 定位：面向 C++ 的 MATLAB 风格数值接口，对 `xhqm::matrix<type>`（2D，`operator*` 为矩阵乘）与 `xhqm::vector<type>`（1D）提供逐元素、归约、分解等函数。
- 后端：f32/f64 逐元素与归约优先走 `tensor_base` 的 unary/elemwise/reduce 等 kernel（SIMD / CUDA / Metal），其余类型回退 `xhqm::` 泛型标量函数；线性代数复用 `tensor_base::svd/eig/lu/qr/chol` 等桩。**注意**：kernel 是否命中取决于活动后端 —— 默认构建 `XHQM_BUILD_TENSOR=OFF`（`CMakeLists.txt:41`）且活动后端是 `no` 桩（`algebra/tensor/backend/tensor_backend.cpp:27-60`），此时全部回退标量。
- 平台差异：本模块 9 个子头（`axis`/`convert`/`construct`/`array`/`math`/`linalg`/`stats`/`poly`/`signal`）里**没有任何 `#if`/`#ifdef` 分支**（只有 include guard），平台差异全部下沉到 `algebra/tensor/backend/`。
- 设计契约：
  - **归约语义**：无参 = 全元素（返回标量）；`f(m, axis)` 沿维，`axis::rows`（0 基，每列归约）/ `axis::cols`（每行归约）。
  - **枚举替代魔法数**：`axis` / `norm_type` / `variance` / `sort_order`，不照搬 MATLAB 的字符串与 1 基 dim。
  - ~~**失败约定**：奇异 / 非方阵 / 空输入 → 返回空容器（`size()==0`）；`det` 返回 0、`cond` 返回 `inf` 是数学正确结果（非失败标记）。~~ **已作废（2026-10-07 复核 r677）**：真实约定分两半 —— ① **奇异 / 非方阵**确实回空容器（实测 `lu`/`qr`/`chol`/`mldivide`/`orth`/`null`），`det` 返回 0、`cond` 返回 `inf` 都是数学结果而非失败标记；② **空输入不遵守**：`cond`/`mean`/`max`/`min` 返回 `0`（`linalg.h:75`、`stats.h:81`、`algebra/tensor/linalg.h:183,191`），`rank` 直接越界读（`linalg.h:61-62`，见 `DEBUG.md` A2）——调用方不能只靠「是否为空」判定。
  - **0 基 + 行优先**：与 C++ 一致，不照搬 MATLAB 的 1 基索引与列优先内存语义。

---

## 2. 架构

- 纯头文件模块：`analysis.h` 为聚合头，按 `axis → construct → array → math → linalg → stats → poly → signal` 顺序聚合；`analysis.cpp` 仅 `#include "analysis.h"`。
- 分层：
  - **薄封装层**（各子模块）：把 `matrix/vector` 零拷贝转成 `tensor`（`values()` 引用），调用 `tensor_base` 桩，再包装回 `matrix/vector`。
  - **共享转换层**：`convert.h` 提供 `detail::as_t1/as_t2`（→tensor，零拷贝）与 `detail::as_vec/as_mat`（tensor→容器，包装构造 memcpy）；`math.h` / `stats.h` 内另有等价的 `detail::map/unary/elemwise_binary/as_tensor` 等内部工具。
  - **算法层**：真正的算法在 `../tensor` 的 `linalg.h` / `stats.h` / `poly.h` / `signal.h` 与 `tensor_base` 中实现，本模块只负责接口适配与命名空间规整。

---

## 3. 关键类 / API

### 3.1 枚举（`axis.h`）

| 枚举 | 取值 | 语义 |
|---|---|---|
| `axis` | `rows=0` / `cols=1` | 归约方向（0 基）：rows=每列归约，cols=每行归约 |
| `norm_type` | `fro=0` / `one=1` / `spectral=2` / `inf=3` | 矩阵范数类型（谱范数为默认） |
| `variance` | `sample=0` / `population=1` | 方差归一化：N-1 无偏（默认）/ N 总体 |
| `sort_order` | `ascend=0` / `descend=1` | 排序方向 |

### 3.2 结果结构体

- `lu_result{L,U,P}`、`qr_result{Q,R}`、`schur_result{T,Q}`、`svd_result{U,S,V}`（linalg）
- `deconv_result{q,r}`（poly）
- `meshgrid_result{X,Y}`（construct）
- `minmax_result{value,index}`（stats，`max_index`/`min_index`）

### 3.3 主要函数清单

| 子模块 | 函数 |
|---|---|
| 构造 `construct.h` | `zeros` `ones` `eye` `diag` `linspace` `logspace` `rand` `randn` `blkdiag` `meshgrid` `vander` `compan` `hilb` `toeplitz` `hankel` |
| 数组 `array.h` | `size` `numel` `length` `reshape` `horzcat` `vertcat` `cat` `repmat` `fliplr` `flipud` `flip` `rot90` `tril` `triu` `find` `find_rc` `circshift` `kron` |
| 逐元素 `math.h` | 三角/反三角/双曲 `sin…csch`、指数对数 `exp/log/log10/log2/sqrt/expm1/log1p/exp2`、`power` `atan2` `hypot`、取整 `floor/ceil/round/trunc/fix`、`mod/rem`、`abs/sign`、`erf/erfc`、`gamma/gammaln`、`square/cbrt/rsqrt`、`deg2rad/rad2deg`、`isnan/isinf/isfinite` |
| 线性代数 `linalg.h` | `mtimes` `trace` `inv` `pinv` `det` `norm` `rank` `cond` `rcond` `lu` `qr` `chol` `mldivide` `mrdivide` `rref` `orth` `null` `svd` `eig` `schur` `roots` `expm` `sqrtm` `logm` |
| 统计 `stats.h` | `sum` `prod` `mean` `max` `min` `var` `stddev` `median` `mode` `range` `cumsum` `cumprod` `cummax` `cummin` `diff` `gradient` `cov` `corrcoef` `sort` `sort_index` `sortrows` `unique` `argmax/argmin` `max_index/min_index` `all/any` `nansum/nanmean/nanmax/nanmin` `prctile/quantile/iqr` `ismember/intersect/setunion/setdiff` |
| 多项式 `poly.h` | `polyval` `conv` `deconv` `polyder` `polyint` `poly` `polyfit` |
| 信号 `signal.h` | `interp1` `fft` `ifft` `fftshift` `ifftshift` `trapz` `cumtrapz` `filter` `conv2` `xcorr` |

> 函数清单以源码为准（2026-10-07 逐名核对）。其中 `rcond` **实例化即编译失败**（`linalg.h:193,196` 把 `int 1` 传给 `enum class norm_type`），目前是死接口 —— 见 `DEBUG.md` A1。

---

## 4. 子模块

- `axis.h`：强类型枚举，替代 MATLAB 的 1 基 dim 与字符串魔法数。
- `convert.h`：`detail` 内 vector/matrix ↔ tensor 零拷贝转换，供各文件共享。
- `construct.h`：数组 / 特殊矩阵构造（含 `meshgrid`/`vander`/`compan`/`hilb`/`toeplitz`/`hankel`）。
- `array.h`：尺寸查询、reshape、拼接、翻转/旋转、三角提取、查找、`circshift`、`kron`。
- `math.h`：逐元素数学，`detail::unary/unaryv/elemwise_binary/map` 优先走后端 kernel，回退标量。
- `linalg.h`：线性代数薄封装，复用 `tensor_base` 分解桩与 `matrix::det`。
- `stats.h`：归约 / 统计，含沿维归约、NaN 鲁棒统计、百分位数、集合运算。
- `poly.h`：多项式（系数降幂），基础算子走 `tensor_base::polyval/conv1d_full/deconv` 等。
- `signal.h`：信号处理（interp1/FFT/trapz/filter/conv2/xcorr），薄封装 `tensor_base::signal`。
- `analysis.h` / `analysis.cpp`：聚合头与编译单元。

---

## 5. 依赖

- 上游核心：`../../head.h`（`xhqm` 标量数学与基础类型 `size/index/complex`）。
- 张量后端：`../tensor/matrix.h`、`../tensor/vector.h`、`../tensor/complex.hpp`、`../tensor/backend/tensor_backend.h`。
- 算法桩：`../tensor/linalg.h`、`../tensor/stats.h`、`../tensor/poly.h`、`../tensor/signal.h`。
- 标准库：`<cmath>` `<random>` `<array>` `<utility>` `<algorithm>` `<limits>` `<map>` `<vector>` `<type_traits>`。其中 `<vector>`/`<cmath>`/`<type_traits>`/`<limits>` 模块内**没有直接 include**（`array.h:8-9` 用 `std::vector` 却只引 `<array>`/`<utility>`，`construct.h:7` 只引 `<random>`），靠 `../tensor/*.h` 传递包含；GCC 11 实测 9 个头各自单独编译通过，属隐式依赖（见 `DEBUG.md` G4）。
- 构建开关：门面用 `XHQM_USE_ALGEBRA`（`xhqm.h:41-46`，其中 `xhqm/analysis` 在 `:45` → `algebra/analysis/analysis.h`，链路完整），CMake 用 `XHQM_BUILD_TENSOR`（`CMakeLists.txt:41`，**默认 OFF**，`:552` 定义同名宏）。该开关下本模块只登记 `analysis.h`（`CMakeLists.txt:276`）与 `analysis.cpp`（`:311`），其余 9 个头未登记（只影响 IDE 源树，仓库没有 `install()` 规则）；`xhqm.vcxproj` 的 105 条源文件里没有 `algebra/analysis`，也没有 `algebra/tensor` —— Windows 侧只能经 CMake 构建（见 `DEBUG.md` C1）。

---

## 6. 用法示例

```cpp
// 方式一（推荐）：门面头。include 根 = 仓库根目录；xhqm/analysis:2 转发到 ../algebra/analysis/analysis.h
#include "xhqm/analysis"
// 方式二：直接引模块聚合头（include 根 = 仓库根目录）
#include "algebra/analysis/analysis.h"
// 方式三：相对引用（test/algebra_extended_test.cpp:2 就是这么写的）
#include "../algebra/analysis/analysis.h"

using namespace xhqm;
using namespace xhqm::analysis;

// 构造与逐元素数学
matrix<double> A = ones<double>(3) * 2.0;      // 3×3 全 2 矩阵
matrix<double> B = sin(A);                     // 逐元素 sin（后端 unary kernel）

// 归约（无参 = 全元素，返回标量；axis = 沿维）
double s = sum(B);                             // 全元素求和
vector<double> colsum = sum(B, axis::rows);    // 每列求和（0 基 rows）

// 线性代数（奇异返回空容器，det=0 是数学结果）
matrix<double> C = mtimes(A, eye<double>(3));
auto [L, U, P] = lu(C);                        // P·A = L·U
matrix<double> Ai = inv(C);
double d = det(C);

// 统计
double mu = mean(B);
double sd = stddev(B);                         // 样本标准差（N-1）
vector<double> xs = linspace<double>(0, 1, 100);
vector<double> ys = sin(xs);

// 多项式与信号
vector<double> p = polyfit(xs, ys, 5);         // 5 次最小二乘拟合（降幂系数）
vector<double> yp = polyval(p, xs);
vector<complex<double>> sig(1024);             // FFT 长度须为 2 的幂
vector<complex<double>> F = fft(sig);          // radix-2 FFT
vector<complex<double>> back = ifft(F);
```

> ~~`#include "xhqm/algebra/analysis/analysis.h"`~~ **已作废（2026-10-07 复核 r677）**：该路径**不存在**（`fatal error: xhqm/algebra/analysis/analysis.h: 没有那个文件或目录`）；`xhqm/algebra` 是一个门面文件（只引 `value/mathf/itype/ftype/ctype/complex/dualnum`，不含 analysis），真实可用的三条路径见上一段。

> 参数含义（均为强类型枚举，`axis.h:12-39`，全部 0 基）：`axis::rows`=0 沿行方向归约（每列出 1 个结果，≈MATLAB `dim=1`）/ `axis::cols`=1 每行出 1 个结果；`norm_type::spectral`=2 是 `norm` 默认，另有 `fro`=0 / `one`=1（最大列和）/ `inf`=3（最大行和）；`variance::sample`=0 默认 N-1 / `population`=1；`sort_order::ascend`=0 默认。`linspace(a,b,n)` 默认 `n=100`（`construct.h:72`），`logspace(a,b,n)` 默认 `n=50`（`construct.h:84`）；`polyval/polyfit/conv/deconv` 的系数一律**降幂**。

> 失败检查（务必自己判）：分解与解方程类在奇异/非方阵时返回空容器，取用前先判空，例如 `auto lr = lu(A); if (lr.L.rows() == 0) return;`；`mldivide` 只接受方阵且要求 `|U(i,i)| >= preci<type>`（`linalg.h:128-132`，double 阈值 `1e-11`），`mrdivide(a,b)` 只支持方阵并走 `inv`（`linalg.h:148-151`）——非方阵一律回空容器，不做最小二乘。

> 实测：上面这段示例（含 `lu` 结构化绑定与 1024 点 `fft`）已逐字提取（`#include` 行换成本模块的真实路径）并在 g++ 11.4 `-std=c++17 -fsyntax-only` 下编译通过，0 error（2026-10-07）。

> 注意：~~`using namespace xhqm` 与 `using namespace xhqm::analysis` 同时使用会二义，需用 `xhqm::analysis::` 限定或传 const 实参~~ **已作废（2026-10-07 复核 r677）**：实测只有 `size(m)` 一处二义 —— `xhqm::size` 是类型别名（`head.h:219` `using size = std::size_t`），与 `analysis::size` 同名，报 `reference to 'size' is ambiguous`；其余 24 个无限定调用（`sum/mean/max/min/abs/sqrt/sin/exp/norm/var/sort/diff/trace/inv/det/rank/cond/unique/median/numel/ones/eye/zeros/diag`；探针共 25 个）全部通过。标准差接口命名为 `stddev`（MATLAB 名 `std` 与 `::std` 冲突）。

---

## 7. 已知取舍与后续

- 剔除（无必要）：`ndims`（matrix/vector 维度固定）、`sub2ind/ind2sub`（2D 行优先索引一行表达式）、`nthroot/pow2/nextpow2`（std 一行表达式）。2026-10-07 在本模块 grep 核对：以上符号 0 命中（只在本文件里被提到）。
- 暂缓：`eigs/svds`（Arnoldi/Lanczos 迭代算法）—— 2026-10-07 全仓源码 grep 0 命中，确认**尚未实现**；稠密单数形式已实现：`svd`（`linalg.h:204`）、`eig`（`linalg.h:222`）、`schur`（`linalg.h:242`）、`roots`（`linalg.h:253`），且被 `test/algebra_extended_test.cpp:78,89,106,115` 覆盖。本行原为 `README.md:128`，本轮编辑后移到 `README.md:148`。
- 可选优化：沿维归约可改走 `tensor_base::reduce_*` kernel（实测只有 `axis::cols` 命中最后一维 kernel —— `algebra/tensor/linalg.h:139` 的 `axis == dim-1` 分支；`axis::rows` 落到通用 `reduce`（`algebra/tensor/linalg.h:87-112`）跨行跨步累加），大矩阵有收益。~~需 `to_tensor` 转换权衡~~ **已作废（2026-10-07 复核 r677）**：不存在转换成本 —— 调用点传的是 `detail::as_tensor(m)`（`stats.h:22`），即 `matrix::values()` 的零拷贝引用。
- `rand/randn` 使用固定种子（5489）的静态 `mt19937`（`construct.h:97,110`），序列可复现但不支持按次设定种子（取舍与并发风险见 `DEBUG.md` F1 / B1）。
- 成熟度：**中高** —— 七域（构造/数组/逐元素/线代/统计/多项式/信号）接口齐备、主流程与 MATLAB 对齐且有集成测试；缺 `eigs`/`svds`、缺复矩阵完整覆盖（6 个 API 对 `matrix<complex<T>>` 编译失败）、`axis::rows` 归约未下沉 kernel。
- **当前仍存在的问题清单**（缺陷 / 资源并发 / 跨平台 / 性能 / 未实现 / 取舍 / 文档出入，逐条带 `文件:行号` 证据）见同目录 `DEBUG.md`；本文件不重抄。
