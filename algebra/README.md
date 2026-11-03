# algebra 代数模块接口

> 最后同步：2026-10-07，对照工作副本 r677。

xhqm 的代数库：标量数学、数值类型（复数 / 对偶数 / 四元数 / 对偶四元数）、N 维张量、线性代数、几何与类型系统。数学基础层由 `value.h` / `mathf.h` / `itype.h` / `ftype.h` / `ctype.h` 构成，向上支撑张量、几何、分析与心智模块。

规模（实测，行数按 `awk END{print NR}` 计，末行无换行也算一行）：本模块 76 个源文件 / 21201 行 —— 基础层 8 文件 1118 行、`tensor/` 23 文件 5003 行、`tensor/backend/` 13 文件 6415 行、`geometry/` 3 文件 2863 行、`geometry/brep/` 1 文件 74 行、`analysis/` 11 文件 1731 行、`soul/` 17 文件 3997 行。单文件最大的几个：`tensor/backend/tensor_backend_gpu.cu`（1710）、`soul/ad_ops.h`（1548）、`tensor/backend/tensor_backend_metal.mm`（1525）、`tensor/backend/tensor_backend_simd.h`（1475）、`geometry/geometry.cpp`（1345）、`tensor/linalg.h`（1238）。

对外依赖只有三处：`head.h`、`hardware`（`device` 枚举，仅 `tensor/backend/backend.h` 引用）、`metaclass`（`block` 被 `ctype.h` 用作 arr/tup 的存储；`template.h`/`buffer.h` 被 `geometry/linear.h` 引用）。注意 `value.h` 经 `head.h` **间接**拉入 `network/json.hpp`（除非定义 `XHQM_JSON_EXTERNAL` 换成外置 nlohmann/json），所以「只用基础层」也会把 json 头带进编译。

## 命名空间

| 命名空间 | 内容 |
|---|---|
| `xhqm::` | 数值类型、`tensor`/`tensor_view`/`matrix`/`vector`/`figure`、泛型数学函数、`matx`/`vect`/`poit`、`ctype`/`arr`/`tup` |
| `xhqm::tensor_base::` | 后端抽象、线性代数（matmul/reduce/eig/svd）、`store`、`dtype`、`sparse_matrix` |
| `xhqm::geometry::` | 几何形状（shape/line/face/cube/mesh/sphere/nurbs/…） |
| `xhqm::analysis::` | 分析（MATLAB 风格数值接口，见 `analysis/README.md`） |
| `xhqm::hardware::` | 设备枚举 `device`（no/cpu/gpu/net/auto_），被 tensor_base 复用 |

## 架构

数学基础按「常数 → 泛型运算 → 具体数值类型 → 类型系统」分层：

```
value.h   常数 / 精度 / 数学体系别名 / number<type>
   └─ mathf.h   泛型算术·比较·数学·特殊值·序列化（bool 走 GF(2) 特化）
         ├─ itype.h   整型 u8..u64 / i8..i64 及极值、特化
         └─ ftype.h   浮点 f16(位域)/f32/f64/fld 及极值、特化
               └─ ctype.h   类型枚举 + arr/tup 异构容器 + ctype_v/ctype_s 双向映射
```

各子模块分层：`tensor/`（张量 + 线性代数 + 后端）、`geometry/`（几何 + 齐次线性代数）、`analysis/`（数值分析，接口 header-only；另有 1 行编译单元 `analysis.cpp` 仅用于把聚合头纳入编译）、`soul/`（心智，依赖 tensor + tensor_base，另依赖 `lattice` 与 `network/uuid`）。基础层另有 `itype.cpp`（空编译单元，只 `#include "itype.h"`，CRLF 行尾）。

基础层实测行数：`value.h` 97、`mathf.h` 59、`itype.h` 129、`ftype.h` 181、`ctype.h` 303、`itype.cpp` 6、`ftype.cpp` 60、`ctype.cpp` 283。以上 8 个文件按 `CMakeLists.txt` 的 `XHQM_HEAD_CORE` / `XHQM_BODY_CORE` 恒编译（基础层不受任何模块开关影响）。

影响本模块形态的编译开关（`CMakeLists.txt` 注入）：`XHQM_BUILD_TENSOR`（默认 `OFF`，开 `algebra/tensor` + `algebra/analysis`）、`XHQM_DEVICE`（默认 `CPU`，`NO/CPU/GPU/NET/AUTO` → `XHQM_DEVICE_*`）、`XHQM_TENSOR_BACKEND`（默认 `ON`，`NO/OFF/ON`）、`TENSOR_WITH_CPU`、`TENSOR_WITH_GPU`、`TENSOR_WITH_SIMD`（只在 MSVC 下定义）、`TENSOR_WITH_BLAS`、`XHQM_TENSOR_CUDA`、`XHQM_TENSOR_METAL`；并行阈值 `XHQM_PARALLEL_THRESHOLD` 默认 `4096`（`tensor/backend/tensor_backend.h:16-19`）。开关名与实际文件集对得上：`TENSOR_WITH_CPU`→`tensor_backend_cpu.cpp`、`XHQM_TENSOR_CUDA`→`tensor_backend_gpu.cpp/.cu`、`XHQM_TENSOR_METAL`→`tensor_backend_metal.mm`、`TENSOR_WITH_SIMD`→`tensor_backend_simd.h`（仅头文件）、no 后端恒编入。

## 子模块索引

| 子模块 | 目录 | 规模（文件/行） | 内容 | 文档 |
|---|---|---|---|---|
| tensor | `tensor/` + `tensor/backend/` | 23/5003、13/6415 | `tensor`/`tensor_view`/`matrix`/`vector`/`figure`/`store`，线性代数与分解，`sparse_matrix`，no/cpu/gpu(cuda)/metal 后端 | `tensor/README.md`、`tensor/DEBUG.md` |
| analysis | `analysis/` | 11/1731 | MATLAB 风格数值接口，薄封装 `tensor_base` 算子 | `analysis/README.md`、`analysis/DEBUG.md` |
| geometry | `geometry/` + `geometry/brep/` | 3/2863、1/74 | `linear.h` 齐次 4×4 线性代数 + `geometry.h` 形状体系 + brep 拓扑层（`brep.h`，仅类型草图） | `geometry/README.md`、`geometry/DEBUG.md`、`geometry/brep/README.md`、`geometry/brep/DEBUG.md` |
| soul | `soul/` | 17/3997 | 前向网络组件 / Transformer（含 KV cache 生成）/ reverse-mode AD / RNN·LSTM·GRU / DDPM·UNet 扩散，header-only，命名空间 `xhqm::soul` | `soul/README.md`、`soul/DEBUG.md` |

层间依赖（实测 include 关系）：`value.h` → `mathf.h` → {`itype.h`, `ftype.h`} → `ctype.h`；`ctype.h` 另外引用 `tensor/complex.hpp`、`tensor/dualnum.hpp`（即基础层的类型系统依赖数值类型），并引用 `metaclass/block.h`。`tensor/` 依赖基础层 + `hardware`；`analysis/`、`soul/` 依赖 `tensor/`；`geometry/` 只依赖基础层 + `metaclass/template.h`、`metaclass/buffer.h`。

## 1. 数学基础

### 常数与精度（`value.h`，命名空间 `xhqm`）

- 常数（`long double` constexpr，`value.h:10-13`）：`en`=2.71828182845904523536l（自然底数）、`pi`=3.14159265358979323846l（圆周率）、`phi`=0.61803398874989484820（注释写「黄金比Ф」，值是 1/φ 而非 1.618…）、`egamma`=0.57721566490153286061l（欧拉-马歇罗尼常数 γ）
- 自然数：`num_0`…`num_11`（`unsigned char`）
- 精度：`precif`(float 1e-5) / `precid`(double 1e-11) / `precil`(long double 1e-19)，模板 `preci<type>`（f32/f64/fld 特化）
- `npos = static_cast<xhqm::count>(-1)`（下标上限）
- 类型别名：`utype=uintmax_t` / `itype=intmax_t` / `ftype=double`，及 `umax`/`imax`/`fmax`
- 数学体系别名：`natural=umax` / `integer=imax` / `measure=fmax` / `symbols=unsigned char` / `duality=bool` / `anycast=void*`
- `number<type>` 模板：把上述常数按 `type` 静态转换（`number<type>::pi` 等），供各类型引用

> 注意：`ln_2` / `ln_10` / `log_e_2` / `log_e_10` / `log_2_e` / `log_10_e` / `sqrt_2` / `sqrt_3` / `sqrt_en` / `sqrt_pi` 这 10 个常数（`value.h:14-23`，实测 10 个）目前仍是占位值 `0.0l`，尚未填充 —— `number<type>` 会把 0 原样透传给所有类型（`value.h:70-79`）。清单与影响见 `DEBUG.md` E1。

### 泛型函数（`mathf.h`，命名空间 `xhqm`）

所有运算经 `xhqm::` 泛型函数，类型特化后支持 f16/f32/f64、整型、bool、复数、对偶数、四元数。

- 算术：`sum` / `sub` / `mul` / `div` / `dot`（标量 dot=mul）
- 比较：`min` / `max`（二元）、`most`(>) / `less`(<) / `equal` / `equal_most`(>=) / `equal_less`(<=)
- 数学：`abs` / `sin` / `cos` / `tan` / `asin` / `acos` / `atan` / `log` / `sqrt`；`pow(value)`=exp(value)、`log(value, exp)`=以 exp 为底、`pow(value, exp)`
- ~~特殊值（写引用，置为极值/NaN/Inf 并返回）：`min(type&)` / `max(type&)` / `nan(type&)` / `inf(type&)`~~ **已作废（2026-10-07 复核 r677）**：只有 `u8..i64`、`f16/f32/f64/fld` 的特化会写极值/NaN/Inf；泛型模板是桩，把引用置为 `type()`（即 0）后返回，`ismin/ismax` 泛型版直接 `return false`（`mathf.h:38-43`）—— 未特化的类型（复数/对偶数/四元数/自定义）会拿到静默错值，见 `DEBUG.md` A4。
- 判读：`ismin` / `ismax` / `isnan` / `isinf`
- 序列化：`read<T>(string)` / `save<T>(value)`（泛型默认仍是占位：`read` 返回 `type()`、`save` 返回字面量 `"type"`，见 `mathf.h:55-56`；已特化的类型见各节与 `DEBUG.md` E2）

**布尔环 GF(2) 特化**（`{0,1}` 构成特征 2 的域）：

```cpp
sum(true, true) == false   // 加法=减法=XOR
sub(true, true) == false   // 每元素自为负元（-x=x）
mul(true, true) == true    // 乘法=除法=AND
div(true, false) == false  // 除以 0 兜底为 0
```

### 整型（`itype.h`，命名空间 `xhqm`）

- 别名：`u8/u16/u32/u64`（uintN_t）、`i8/i16/i32/i64`（intN_t）
- 常量：`u8min…u64min`、`u8max…u64max`、`u8zero…u64zero`、`u8npos…u64npos`、`i8min…i64min`、`i8max…i64max`
- 每类型特化：`min/max/nan/inf`（写引用；整型的 `nan/inf` 取 max 值）、`ismin/ismax`、`read`（`stoul/stoi/stoll` 等，**解析后直接 `static_cast` 窄化、不做范围检查**，见 `DEBUG.md` A5）、`save`（`to_string`）

### 浮点（`ftype.h`，命名空间 `xhqm`）

- `float_16`：位域结构体（`fraction:10` / `exponent:5` / `sign:1`，共 2 字节），成员 `most/zero/inf/nan(us=正负号)` 直接写位值；与 `float` 互转（`float_16(float)` / `operator float()`）。**没有算术/比较运算符重载**：`sum/sub/mul/div`、`most/less/equal` 等一律靠 `operator float()` 隐式转成 float 再算（`ftype.h:116-127`），代价见 `DEBUG.md` F2
- 别名：`float_32=float` / `float_64=double` / `float_ld=long double`，及 `f16/f32/f64/fld`
- 常量：`f32min/f64min/fldmin`、`f32max/f64max/fldmax`、`f32inf/f64inf/fldinf`、`f32nan/f64nan/fldnan`
- `number<f16>` 特化：`preci=0.001f`、`zero`
- 特化：`f16` 全套数学函数（经 float 转换）；~~`isnan/isinf` 用位模式比较~~ **已作废（2026-10-07 复核 r677）**：实现是转成 float 后用 `==` 比较（`ftype.h:148-149`），于是 `isnan(f16)` 因 NaN≠NaN 恒返回 false（`isinf` 仍正确），见 `DEBUG.md` A1；`f32/f64/fld` 的 `min/max/nan/inf`（写引用）、`read/save`
- `ftype.cpp`：f16⇄f32 转换（sign / fraction>>13 / exponent，inf·nan 直接映射，上溢→inf，下溢→0 忽略 subnormal）

## 2. 数值类型（命名空间 `xhqm`）

### complex（`tensor/complex.hpp`）

`template<typename type> using complex = std::complex<type>`。已特化 `abs/sin/cos/tan/log/pow/sqrt` 及 `sum/sub/mul/div/dot`（dot=实部·实部+虚部·虚部）。序列化格式 `"real+imagi"`。

### dualnum 对偶数（`tensor/dualnum.hpp`）

`dualnum<type> { real, dual }`，`a + bε`，`ε²=0`。**前向自动微分**：四则按对偶数代数，数学函数经链式法则求导。序列化 `"real+duale"`。

```cpp
dualnum<f64> x(2.0, 1.0);   // x=2, dx=1
auto y = sin(x * x);        // y.real=sin(4), y.dual=cos(4)*4
```

### quaternion 四元数（`tensor/quaternion.hpp`）

`quaternion<type> { x, y, z, w }`（Hamilton 乘法，`w` 为实部）。别名 `quat<type>`。

- 代数：`norm2/norm/conjugate/inverse/normalized/identity`，`operator*`（Hamilton 积）、`+/-/×标量/÷标量`
- 旋转：`rotate(vec3)`、`to_matrix()`、`from_matrix/from_axis_angle/from_euler/to_axis_angle/to_euler`
- 插值/李代数：`slerp(a,b,t)`、`exp`（so(3) 指数映射）、`log`（so(3) 对数映射）、`dot`
- 序列化格式 `"x,y,z,w"`

### dual_quaternion 对偶四元数（`tensor/dual_quaternion.hpp`）

`dual_quaternion<type> { real, dual }`，表示刚体变换（旋转+平移），`real` 为旋转四元数，`dual = 0.5·t·real`。

- `operator*`（组合变换）、`conjugate/normalized`
- `transform(p)`（变换三维点）、`rotation()` / `translation()`（提取旋转/平移）

### figure 标量封装（`tensor/figure.h`）

`figure<type>` 标量 class 封装。四则/比较/数学（`abs/sqrt/sin/cos/tan/log/exp/pow`）。整型/bool 上 `sqrt/sin/...` 用 `static_assert` 拦截（窄化无意义）；`f16` 有特化（float 转换 + 特殊值）。

## 3. 张量模块（`tensor/`）

### tensor（`tensor_base.h`，命名空间 `xhqm`）

`tensor<type, dim>` N 维张量，行优先（row-major）。

- 构造：`tensor(dims, value=0)`、默认空
- 形状：`size()` / `shape()` / `dimension()` / `dimension(pos)` / `reshape(dims)` / `redimension(...)`
- 访问：`value(idx)` / `value(coord)` / `operator[](idx)` / `operator()(i,j,...)` / `data()`
- 逐元素（后端并行）：`this_sum/sub/mul/div`（原地）、`copy_sum/sub/mul/div`（拷贝）、`operator+=/-=/*=//=`、`operator+/-/*//`
- 广播：`broadcast_binary`，形状可广播时自动扩展
- 切片：`slice(begin, end)`（拷贝）
- 遍历：`traverse(func)`（串行，可含跨元素依赖）

### tensor_view（~~`view.h`~~ `tensor_view.h`，命名空间 `xhqm`）

> **已作废（2026-10-07 复核 r677）**：旧文写的文件名 `view.h` 不存在，实际文件是 `algebra/tensor/tensor_view.h`（138 行）。

`tensor_view<type, dim>` 非拥有视图（stride 索引，零拷贝）。

- 构造：`tensor_view(tensor&)` / `tensor_view(data, shape, strides)`
- 访问：`dimension(pos)` / `shape()` / `offset(coord)` / `at(coord)` / `operator()(...)`
- 变换：`transpose(a,b)` / `broadcast(dims)` / `slice<pos>(index)` / `reshape(new_shape)`（均零拷贝；轴越界或形状不可广播时抛 `xhqm::error`）

### matrix / vector（`matrix.h` / `vector.h`，命名空间 `xhqm`）

**matrix<type>**（动态 m×n，运算语义为代数）：

- 构造：`matrix(rows, cols, v)` / `matrix{{...}, {...}}`
- 访问：`rows()/cols()/size()/data()/operator()(i,j)`
- 逐元素：`operator+/-`、`mul`（Hadamard）
- 代数：`operator*(matrix)` 矩阵乘、`operator*(vector)` 矩阵乘向量、`dot(matrix)`（=矩阵乘）、`operator*(type)` 标量缩放
- `transpose()` / `det()`（方阵，高斯消元，静态断言拦截整型）

**vector<type>**（动态 n 维）：

- 构造：`vector(n, v)` / `vector{...}`
- 访问：`size()/data()/operator[]`
- 逐元素：`operator+/-`、`mul`（Hadamard）、`hadamard`（别名）
- 代数：`operator*(vector)` 点积（返回标量）、`dot`、`cross`（3 维叉乘）、`operator*(type)` 标量缩放
- `norm()` / `modulus()` / `normalize()`（静态断言拦截整型）

### 线性代数（`linalg.h`，命名空间 `xhqm::tensor_base`）

| 函数 | 说明 |
|---|---|
| `matmul(a, b)` | 矩阵乘 a[m×k]·b[k×n]，f32/f64 走 BLAS/cuBLAS，其余回退 parallel_for |
| `transpose(t, a, b)` | 转置（拷贝；零拷贝见 tensor_view::transpose） |
| `reduce(t, axis, op, init)` | 沿轴归约（通用 op） |
| `reduce_sum / reduce_max / reduce_min` | 沿轴求和 / 最大 / 最小 |
| `broadcast_to(t, dims)` | 广播视图（零拷贝） |
| `eig_sym(A, max_iter = 100)` | 对称矩阵特征分解（Jacobi），返回 `{values, vectors}` |
| `svd(A)` | A=U·S·Vᵀ（经 AᵀA 的 Jacobi），返回 `{U, S, V}` |
| `lu(A)` / `qr(A)` / `chol(A)` | LU（`{L,U,P}`）/ Householder QR（`{Q,R}`）/ Cholesky（`chol` 仅 f32/f64 走 LAPACK `potrf`，非正定返回空张量） |
| `pinv(A, tol = 0)` / `schur(A, max_iter = 500)` | 伪逆（经 SVD）/ 实 Schur 分解（`{T,Q}`，默认迭代 500 次） |
| `expm(m)` / `sqrtm(m, max_iter = 64, tol = 1e-10)` / `logm(m, max_sqrt = 40, taylor_terms = 20)` | 矩阵指数 / 平方根 / 对数（迭代算法，CPU 上刻意串行，见 `DEBUG.md` F3） |

### 稀疏矩阵（`sparse.h`，命名空间 `xhqm::tensor_base`）

`sparse_matrix<type>`（COO 三元组）。构造自稠密 `matrix` 或三元组；`transpose()` / `dense()`（转回稠密）/ `operator*(vector)` / `operator*(matrix)` / `operator*(sparse_matrix)`。

### 后端（`backend/`，命名空间 `xhqm::tensor_base`）

- 设备：`device`（= `hardware::device`：no/cpu/gpu/net/auto_）、`compiled_backend()`、`backend_compiled(kind)`、`active_backend()`、`active_backend(kind)`、`backend_name(kind)`
- 类型标识：~~`dtype { unknown, f16, f32, f64, fld }`~~ **已作废（2026-10-07 复核 r677）**：实测 `dtype` 是 10 个值 —— `unknown / f16 / f32 / f64 / fld / i8 / i16 / i32 / i64 / boolean`（`tensor_backend.h:28-40`），其中 `boolean` 走 GF(2) 语义；`dtype_of<type>::value` 只特化这 10 个类型，未特化类型一律 `unknown`（回退 CPU 泛型路径）
- 接口 `tensor_backend`（纯虚，`tensor_backend.h:239-350`）：`name()/kind()/supports_memory_transfer()`、`parallel_for(begin,end,body)`、`elemwise(dst,a,b,count,dt,op)`、`select(dst,cond,a,b,count,dt)`、`matmul(c,a,b,m,n,k,dt)`、`reduce(dst,src,outer,inner,dt,op)`、`unary(dst,a,count,dt,op)`、`transpose(dst,src,rows,cols,dt)`、`grad_accum(dst,a,b,count,dt,op)`、`spmv(y,x,row,col,val,nnz,rows,cols,dt)`、`pool2d(...)`、`upsample2d(...)`、`dropout(dst,src,count,p,seed,dt)`、`conv3d/conv4d(...)`、`trsv(x,tri,b,n,lower,unit_diag,dt)`、`chol(L,A,n,dt)`、`upload/download/synchronize`。除 `name/kind/supports_memory_transfer/parallel_for/upload/download/synchronize` 外都返回 `bool`，`false` 表示「本后端不做，调用方自己回退」
- 算子编码表：`unary_op`（0..42，含 `unary_isnan/isinf/isfinite/silu`）、`reduce_op`（sum/max/min/prod）、`elemwise_op`（add/sub/mul/div/atan2/hypot/pow/mod/rem）、`grad_op`（relu/leaky_relu/sigmoid）；桩与回退路径共用 `elemwise_eval/unary_eval/grad_eval/reduce_eval`（`tensor_backend.h:126-236`）
- 调度：`tensor_backend_active()`（当前后端）、`tensor_backend_select(kind)`（切换；`auto` 解析 `gpu→cpu→no`，`gpu`/`net` 无实现时回退 cpu→no）。注意 select 只换张量侧的后端对象，不改 `hardware::active_device()`，两者可读到不同答案，见 `DEBUG.md` A9
- 后端实现：`no`（串行桩，算子全部继承基类 `false`，串行语义走上层 `xhqm::` 泛型回退）、`cpu`（`blas→simd→thread→串行` 四级内部降级，`tensor_backend_cpu.cpp`/`tensor_backend_simd.h`）、`gpu`（CUDA：`cuBLAS` + 手写 kernel + cuSOLVER，`tensor_backend_gpu.cpp/.cu`；无 CUDA 时 CMake 仍编一份桩，但不会被创建，见 `DEBUG.md` E3）、`metal`（Apple Silicon，`tensor_backend_metal.mm`，dtype 覆盖窄于 CUDA，见 `DEBUG.md` C4）
- 线程与阈值：CPU 并行走 `std::thread`，线程数 `std::thread::hardware_concurrency()`；元素数 < `XHQM_PARALLEL_THRESHOLD`（默认 4096）时退回串行、避免调度开销

~~聚合头 `tensor.h` 包含以上全部。~~ **已作废（2026-10-07 复核 r677）**：`tensor.h`（21 行）只包含 backend/tensor_backend、dual_quaternion、tensor_base、tensor_view、linalg、norm、nn、conv、pool、random、shape、figure、vector、matrix、sparse；`store.h`、`stats.h`、`signal.h`、`poly.h`、`conv_grad.h`、`complex.hpp`、`dualnum.hpp` 仍需单独 include（`quaternion.hpp` 经 `dual_quaternion.hpp` 间接包含）。

## 4. 类型系统（`ctype.h`，命名空间 `xhqm`）

- `ctype::type` 枚举：`null/text/bool_1/writ_8/writ_16/uint_8..uint_64/sint_8..sint_64/real_16..real_128/array/tuple`（实测 19 个值，`ctype.h:14-39`）
- `ctype::type_complex(string)`：解析 `array<dims...,type>` / `tuple<t1,t2,...>`（也接受 `shape<...>`）的尖括号与逗号嵌套，返回 `{类型名, 各维/各字段, 末段类型}`；先删空格再按深度切分（`ctype.cpp:5-64`）
- `ctype::get_type(text)` / `get_text(type)` / `get_size(type)`（类型 ↔ 名称 ↔ 字节）；`get_size(real_128)` = `sizeof(long double)`，在 MSVC/Apple arm64 上是 8 而不是 16，见 `DEBUG.md` C3
- `arr`：动态数组（从字符串构造，`name()/type()/dim()/size()/good()/value()/value<type>(i)/value_as<type>(i)`）。~~支持拷贝/移动~~ **已作废（2026-10-07 复核 r677）**：`arr`/`tup` 的拷贝构造与赋值是裸字节 `memcpy`（`ctype.h:85-109`、`194-203` 配 `metaclass/block.h:68-72`），嵌套 `array`/`tuple` 元素会与原对象共享内层堆块并在析构时二次释放；拷贝还不复制 `m_string`（拷贝后 `name()` 为空）。详见 `DEBUG.md` A2、A3、B1、B2。析构确实递归释放嵌套 array/tuple（`ctype.cpp:197-220`）
- `tup`：异构元组（从字符串构造，`name()/get_type()/size()/good()/value<type>(i)/value_as<type>(i)`）；`value/value_as` 无边界检查，越界 index 是未定义行为（`ctype.h:206-245`）
- `arr`/`tup` 构造时的类型串非法（元素数不足、类型名取反）走的是裸 `throw;`，在无活动异常时会 `std::terminate`（`ctype.cpp:157-158`、`226-228`），见 `DEBUG.md` A6
- `text` 元素在 `arr`/`tup` 里没接上：`get_size(text)` 返回 0、`value_as` 也没有 text 分支，见 `DEBUG.md` E7
- 双向映射：`ctype_v<type>::value`（类型→枚举）、`ctype_s<enum>::type`（枚举→类型）

## 5. 几何（`geometry/`）

### linear（`linear.h`，命名空间 `xhqm`）

4×4 齐次坐标线性代数：

- `matx<type>`：矩阵，`operator()(i,j)`、`operator*(matx)`、`det()`、`idemat()`、`tramat()`（转置）、`invmat()`（逆）、`adjmat()`（伴随）、`kidmat()`（子矩阵）
- `vect<type>`：向量（w=0），`x()/y()/z()`、`length()/normal()`、`dot` / `mul`（叉乘）、`operator*(matx)`
- `poit<type>`：点（w=1），`x()/y()/z()`，`operator+/-`（点±向量）
- 变换矩阵：`perspective` / `ortho` / `rotate_x/y/z/v` / `translate` / `scale`、`extract_move/rote/zoom`
- 角度：`radians` / `degrees`

### geometry（`geometry.h`，命名空间 `xhqm::geometry`）

几何形状类：`line` / `face` / `cube` / `grid`（面网格） / `mesh`（体网格） / `circle` / `sphere` / `frustum` / `plane` / `box` / `cylinder` / `cone` / `torus` / `capsule` / `quad` / `curve` / `surface` 派生自 `shape`（`geometry.h:66-520`）；`nurbs` 是独立的 `struct`（**不派生 `shape`**，`geometry.h:461`）；`hyper` 子命名空间（超几何：类型别名 `point/topol/points/topols/bounds/object` + `dimension()` / `hypercube(dim, half=1)` / `simplex(dim)` / `hypersphere(dim, samples, radius=1)`，`geometry.h:31-45`）。~~（均派生 shape）~~ **已作废（2026-10-07 复核 r677）**：旧文漏了 plane/box/cylinder/cone/torus/capsule/quad，并把 `nurbs` 也算进 `shape` 派生类。

## 6. 分析（`analysis/`，命名空间 `xhqm::analysis`）

MATLAB 风格数值接口（header-only，依托 tensor/matrix/vector 与 tensor_base 后端）：

- 数组构造：`zeros/ones/eye/diag/linspace/logspace/rand/randn/blkdiag`
- 数组操作：`size/numel/reshape/horzcat/vertcat/cat/repmat/fliplr/flipud/tril/triu/rot90/find`
- 逐元素数学：`sin/cos/tan/…/exp/log/log10/log2/sqrt/power/floor/ceil/round/mod/rem/abs/sign` 及 `atan2/hypot/expm1/log1p`
- 线性代数：`mtimes/inv/pinv/det/trace/norm/rank/cond/rcond/lu/qr/chol/mldivide/mrdivide/rref/orth/null/svd/eig/schur/roots/expm/sqrtm/logm`
- 归约统计：`sum/prod/mean/max/min/var/stddev/cumsum/cumprod/diff/median/mode/cov/corrcoef/sort/unique`（另有沿维归约、NaN 鲁棒统计、百分位数、集合运算、`sortrows`）
- 多项式：`polyval/conv/deconv/polyder/polyint/poly/polyfit`
- 信号：`interp1/fft/ifft`（radix-2）

~~暂缓：`roots/schur/eigs/svds`（需迭代算法）。详见 `analysis/README.md`。~~ **已作废（2026-10-07 复核 r677）**：`roots` 与 `schur` 已经可用（`analysis/linalg.h:242`、`:253` 转发 `tensor_base::schur/roots`，后者经一般特征值分解）；仍然缺的只有 `eigs`/`svds`（Arnoldi/Lanczos 迭代），与 `analysis/README.md`「7. 已知取舍与后续」节的记载一致（该节 2026-10-07 复核后位于 `:148`），见 `DEBUG.md` E6。

## 7. 心智模块（`soul/`）

见 `soul/README.md`（含各文件清单与用法；该文档与本节同批更新）。~~依赖 tensor + tensor_base，规划神经网络与学习能力。~~ **已作废（2026-10-07 复核 r677）**：这些能力已经实现，不再是「规划」——前向组件（`activation/normalize/linear/attention/embedding`）、Transformer（`transformer.h`，含 KV cache 增量推理与 nucleus/beam 采样）、reverse-mode 自动微分（`ad_node/ad_ops/ad_module/ad_io`）、RNN·LSTM·GRU（`recurrent.h`）、DDPM 与 UNet 图像扩散（`diffusion.h`/`unet.h`）、整图 checkpoint 与参数快照（`serialize.h`）都在；命名空间 `xhqm::soul`（AD 在 `xhqm::soul::ad`，扩散在 `xhqm::soul::diffusion`），除 tensor/tensor_base 外还依赖 `lattice`（AD 图模板）与 `network/uuid`，全部 header-only；测试在 `test/soul_ad_test.cpp`、`test/soul_diffusion_test.cpp` 等。

## 依赖

- `xhqm/head.h`：类型别名 `text/byte/size/count/index`、`xhqm_io`、`xhqm::error`、宏工具；并（默认）拉入 `network/json.hpp`（定义 `XHQM_JSON_EXTERNAL` 时改为外置 `<nlohmann/json.hpp>`，`head.h:43-49`）
- `hardware`：设备枚举 `xhqm::hardware::device`（`no/cpu/gpu/net/auto_`）+ `compiled_device/active_device/device_name`，仅由 `tensor/backend/backend.h:14-33` 复用
- `metaclass`：`block`（`xhqm/block` → `metaclass/block.h`）是 `ctype.h` 的 `arr/tup` 存储；`template.h`/`buffer.h` 被 `geometry/linear.h:4-5` 使用（~~`tensor_base` 复用~~ **已作废（2026-10-07 复核 r677）**：`tensor_base.h` 只 include `head.h`、`backend/tensor_backend.h`、`store.h`，不引用 metaclass）
- `tensor/complex.hpp`、`tensor/dualnum.hpp`（`ctype.h:7-8` 引用）、`tensor/quaternion.hpp`（经 `dual_quaternion.hpp`）
- 标准库：`<cmath>` / `<cstdint>` / `<limits>` / `<numeric>` / `<string>` / `<cstring>` / `<vector>` / `<array>` / `<algorithm>` / `<functional>` / `<type_traits>` / `<immintrin.h>`（AVX2，仅 MSVC SIMD 路径）
- 可选外部依赖：BLAS（`libscipy_openblas`，CPU matmul/conv）、CUDA Toolkit（cuBLAS/cuSOLVER）、Metal/MPS（macOS）；工程标准为 **C++17**（`CMakeLists.txt:19`），故 `<numbers>`（C++20）未启用，见 `DEBUG.md` F7

## 简要用法示例

以下片段里的类型名、函数名、参数与断言都取自仓库内真实用法（`test/algebra_test.cpp`、`head.cpp`），include 行给出三种在仓库里真实可用的写法：

```cpp
// —— include：三条真实路径，挑一条 ——
#include "xhqm/algebra"                 // 聚合转发头（注意 xhqm/algebra 是文件、不是目录；head.cpp:2 的用法）
#include "algebra/tensor/tensor.h"      // 仓库根在 include 路径上（CMakeLists.txt:79 include_directories("./")）
// #include "../algebra/ctype.h"       // 模块内/测试里的相对写法（test/algebra_test.cpp:6）

using namespace xhqm;

// —— 基础层：常数 / 精度 / f16 / 布尔环 / 类型系统 ——
f64 r  = 2.0 * number<f64>::pi;         // number<type>::pi 由 value.h 的 long double 常数静态转换而来
f32 tol = preci<f32>;                   // 1e-5f（f64 → 1e-11，fld → 1e-19）
f16 h(0.5f);                            // 半精度：位域 struct，靠 operator float() 参与运算
float hf = h;                           // 0.5f
f16 hmax; xhqm::max<f16>(hmax);         // 写引用式取特殊值：hmax 被置为 +max，ismax(hmax) == true
// 注意：number<f64>::ln_2 / sqrt_2 等 10 个常数当前是 0.0l（DEBUG.md E1）

bool x = sum(true, true);               // false —— bool 走 GF(2) 特化：加法=减法=XOR
bool y = div(true, false);              // false —— 除以 0 兜底为 0

arr A("array<2,2,real_32>");            // 2×2 float（字符串描述类型，元素按 ctype 枚举存放）
if (!A.good()) { /* 类型串非法或分配失败：此时 value() 不可用 */ }
A.value<f32>(3) = 1.5f;                 // 一维下标按行优先，范围 0..size()-1
tup T("tuple<uint_32,real_64>");        // 异构元组：字段按类型串顺序紧凑排布
T.value<u32>(0) = 7u;                   // value<type>(i) 返回 type&，不做边界检查（DEBUG.md A3）
auto name  = ctype::get_text(ctype_v<f64>::value);  // "real_64"
auto bytes = ctype::get_size(ctype::real_64);       // 8

// —— 张量与线性代数（用法同 test/algebra_test.cpp:102-131、206-245）——
tensor<2, f64> t(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);  // 构造：维度数组 + 初值，行优先
t(0, 0) = 1; t(0, 1) = 2; t(1, 0) = 3; t(1, 1) = 4;
tensor<2, f64> had = t * t;                      // 逐元素（Hadamard），矩阵乘要用 matmul
tensor<2, f64> mm  = tensor_base::matmul(t, t);  // f32/f64 走 BLAS/cuBLAS，其余回退 parallel_for
tensor<1, f64> col = tensor_base::reduce_sum(t, 1);
auto e = tensor_base::eig_sym(t);                // {values, vectors}（Jacobi，max_iter 默认 100）
auto u = tensor_base::svd(t);                    // {U, S, V}

matrix<f64> m({ { 1, 2 }, { 3, 4 } });           // 也可以 matrix(rows, cols, 初值)
double d = m.det();                              // -2（整型被 static_assert 拦下，bool 放行）
vector<f64> v({ 1.0, 2.0, 3.0 });
double dotv  = v * v;                            // 两向量 operator* 是点积（返回标量）
vector<f64> cr = v.cross(vector<f64>({ 0.0, 0.0, 1.0 }));  // 仅 3 维有效

figure<f64> fg(4.0);
double sq = fg.sqrt().value();                   // 2.0（整型/bool 上 sqrt/sin 由 static_assert 拦下）

// —— 几何：齐次 4×4 与形状 ——
vect<f64> va(1.0, 0.0, 0.0), vb(0.0, 1.0, 0.0);
double zerod  = vect<f64>::dot(va, vb);          // 静态调用
vect<f64> crs = vect<f64>::mul(va, vb);          // vect::mul 是叉乘
matx<f64> R   = rotate_x(0.0);                   // 变换矩阵工厂（还有 ortho/perspective/translate/scale…）
matx<f64> idm = R.idemat();
double rad    = radians(180.0);                  // == pi
geometry::circle c(2.0);
c.poit(geometry::poit(0, 0, 0));                 // 圆心
c.pose(geometry::vect(0, 0, 1));                 // 法向
double px = c.circle_point(0.0).x();             // 2.0
```

跑法：这些断言都在 `test/algebra_test.cpp`（766 行）与 `test/algebra_extended_test.cpp`（378 行）里，Catch2 目标 `algebra_test` 由 `test/CMakeLists.txt:16` 定义（编入 `../algebra/ftype.cpp`、`ctype.cpp`、`geometry.cpp`、`tensor/backend/tensor_backend{,_no}.cpp`）。

## 已知问题

当前仍存在的问题（功能性缺陷、资源与并发、跨平台、性能、占位桩、设计取舍、文档出入）统一记在 **`algebra/DEBUG.md`**（总览级 + 基础层 + 跨模块条目），每条带 `文件:行号` 证据；本 README 只在相关条目处留一行指针，不重抄。子模块台账见上表「子模块索引」的 `tensor/DEBUG.md`、`analysis/DEBUG.md`、`geometry/DEBUG.md`、`geometry/brep/DEBUG.md`、`soul/DEBUG.md`（与 `hardware/DEBUG.md`、`lattice/DEBUG.md` 同为 2026-10-07 全仓复核同批建立），子模块范围内的条目不在这里重复。
