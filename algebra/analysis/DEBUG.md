# DEBUG —— `algebra/analysis` 当前仍存在的问题

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
> 与同类文件的关系：本仓的历史台账 `fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` **不搬家、不改名**，本文件只覆盖 `algebra/analysis`（`/home/xhqm/DeepSeek/xhqm-v2/algebra/analysis`）；`analysis/README.md` 的「当前问题清单」一律指到这里，不重抄。
> 口径：每条都带 `文件:行号`。**编译类结论全部实测**（`g++ 11.4.0 -std=c++17 -fsyntax-only`，从 `stdin` 编译，未落任何文件）；**运行期结论凡未实际跑过的都写明「未复现」并给出静态判定链**。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `rcond` 实例化即编译失败（`enum class` 收到 `int`）
- 位置：`linalg.h:193`、`linalg.h:196`（签名 `linalg.h:47`；枚举定义 `axis.h:19-25`）
- 现状：函数体写 `const type n1 = norm(m, 1);` / `const type ni1 = norm(mi, 1);`，而 `norm` 的形参是 `norm_type p`，`norm_type` 是 `enum class`，`int 1` 不能隐式转换。模板只在实例化时报错，所以仓库整体构建（`test/algebra_extended_test.cpp` 没覆盖 `rcond`）不报错 —— **不是「没写完」，是写错了**。
- 证据：实测
  `printf '#include "algebra/analysis/analysis.h"\nint main(){ xhqm::matrix<double> m={{4,7},{2,6}}; return (int)xhqm::analysis::rcond(m); }' | g++ -std=c++17 -I. -x c++ - -fsyntax-only`
  →
  `algebra/analysis/linalg.h:193:45: error: no matching function for call to 'norm(const xhqm::matrix<double>&, int)'`
  `note: cannot convert '1' (type 'int') to type 'xhqm::analysis::norm_type'`（`:196` 同）
- 影响：`rcond` 是死接口；任何调用它的 TU 直接编译不过。README 的函数清单把它列为可用，属文档与代码不符（本轮已在 README 加注）。
- 复现/验证：上面那条命令即复现（本次已跑，报错如上）。
- 建议：改成 `norm(m, norm_type::one)`（`norm_type::one` = 1，最大列和）。

### A2. `rank()` 对空矩阵越界读
- 位置：`linalg.h:61-62`
- 现状：`const xhqm::size k = sv.S.dimension(0);` 之后**无条件**取 `sv.S[0]` 算阈值，没有 `k == 0` 分支。同文件的 `cond` 有这个分支（`linalg.h:75` `if (k == 0) return type(0);`），后端 `norm_spectral` 也有（`algebra/tensor/linalg.h:1047`）—— 三处同构代码只有 `rank` 漏了。
- 证据：`svd` 对 `k = min(m,n) = 0` 会返回尺寸为 0 的 `S`（`algebra/tensor/linalg.h:374` `r.S = tensor<1,type>({k}, type(0))`）；`tensor<1>::operator[]` → `store<type>::operator[]` → `std::vector::operator[]`，**全程无边界检查**（`algebra/tensor/tensor_base.h:334-337`、`algebra/tensor/store.h:32-33`）。
- 影响：`analysis::rank(matrix<double>(0,3))`（或任何 0 行/0 列矩阵）读越界元素 → UB（Debug STL 下断言，Release 下读脏内存）。空矩阵在「先建容器再填充」的代码里很常见。
- 复现/验证：未复现（本次只做静态判定）。触发式：`matrix<double> m(0,3); analysis::rank(m);`
- 建议：`rank` 开头加 `if (k == 0) return 0;`。

### A3. `polyfit` 在正规方程退化时越界读
- 位置：`poly.h:94-96`（守卫只有 `poly.h:84`）
- 现状：`matrix<type> c = mldivide(A, b);` 之后**不判空**，直接 `coeff[i] = c(i, 0);`。而 `mldivide` 在非方阵/奇异/主元近零时 `return matrix<type>();`（`linalg.h:128`、`linalg.h:130`）→ 得到一个 0×0 矩阵，再 `c(i,0)` 即越界（`matrix::operator()` → `matrix.h:53-54` → 无检查的 `tensor::operator()`）。
- 证据：唯一守卫是 `if (y.size() != n || deg + 1 > n) return vector<type>();`（`poly.h:84`）。当 `deg + 1 <= n` 但 Vandermonde 秩亏时（例如 **x 有重复点**：`x = {1,1,2}, y = {1,1,4}, deg = 2`，`V` 前两行全等 → `A = VᵀV` 奇异 → LU 主元 ~1e-16 < `preci<double>`=1e-11 → `lr.U.dimension(0)==0` → `mldivide` 回空矩阵），直接踩越界。
- 影响：崩溃或静默垃圾系数；采样点重复/共线是拟合代码的日常输入。
- 复现/验证：未复现（静态判定）。触发式：`analysis::polyfit(vector<double>{1,1,2}, vector<double>{1,1,4}, 2);`
- 建议：`if (c.rows() == 0) return vector<type>();`。

### A4. `logspace(a,b,1)` 除零 → NaN
- 位置：`construct.h:84-91`（对照 `linspace` 的守卫 `construct.h:75`）
- 现状：`const type step = xhqm::div(xhqm::sub(b, a), type(n - 1));` 在 `n == 1` 时除以 0 → `±inf`；随后 `xhqm::mul(step, 0)` = `inf * 0` = NaN → `pow(10, NaN)` = NaN。`linspace` 同样位置有 `if (n == 1) { r[0] = b; return r; }`，`logspace` 没有。
- 证据：两函数并排读即见不对称（`construct.h:72-80` vs `construct.h:84-91`）；MATLAB 语义 `logspace(a,b,1)` = `10^b`。
- 影响：单点对数等距调用得到 NaN（而不是 `10^b`），且与 `linspace` 行为不一致 —— 静默算错。
- 复现/验证：未复现（静态判定）。触发式：`analysis::logspace<double>(0.0, 2.0, 1)` 期望 `{100}`。
- 建议：补 `if (n == 1) { r[0] = xhqm::pow(type(10), b); return r; }`。

### A5. `tril/triu` 的 `k` 是无符号，负 `k`（MATLAB 语义）静默回绕
- 位置：`array.h:122`（`tril`）、`array.h:133`（`triu`）；判据 `array.h:127`（`j > i + k`）、`array.h:138`（`j < i + k`）；根因 `head.h:219` `using size = std::size_t`
- 现状：`k` 声明为 `const xhqm::size& k = 0`，是**无符号**。`tril(A, -1)` 把 -1 变成 `SIZE_MAX`，`i + k` 按 2^64 回绕，判据被改写成 `j > i-1`，结果整片错。GCC `-Wall -Wextra` 对此**零告警**（实测）。
- 证据：实测无告警：`printf '...tril(m,-1); circshift(m,-1,axis::rows); triu(m,-1);...' | g++ -std=c++17 -I. -Wall -Wextra -x c++ - -fsyntax-only`（输出里没有一行来自 `algebra/analysis/*.h`）。回绕推导：`k = SIZE_MAX`，行 i 的判据 `j > i + SIZE_MAX`，模 2^64 后即 `j > i - 1`。
- 影响：MATLAB 迁移代码里的 `tril(A,-1)`（保留次对角线）/ `triu(A,1)`（k 为正时正确，负时错）静默给出错误矩阵。测试只用默认 `k = 0`，未覆盖。
- 复现/验证：未复现（静态判定）。触发式：`analysis::tril(matrix<double>(2,2,1.0), -1)` 期望 `[[1,0],[1,1]]`，实际 `[[1,1],[1,0]]`。
- 建议：`k` 改带符号类型（或 `xhqm::index` + 显式负值分支）。

### A6. `circshift` 的负 `k` 只在「维度是 2 的幂」时恰好正确
- 位置：`array.h:176`、`array.h:182`、`array.h:190`；根因 `head.h:217` `using index = std::size_t`
- 现状：`k` 是 `const xhqm::index&`（无符号），归一化写成 `((k % rows) + rows) % rows`。当 `k = -1`（回绕成 `2^64-1`）时，`(2^64-1) % rows` 只有在 `rows` 整除 `2^64`（即 rows 是 2 的幂）时才等于 `-1 mod rows`。
- 证据：`rows = 3` → `(SIZE_MAX % 3) = 0` → 位移 0（应为 2）；`rows = 4` → 3（恰好等于 `-1 mod 4`）。
- 影响：`circshift(m, -1, axis::rows)` 在 3 行矩阵上「不动」，在 4 行矩阵上却是对的 —— 随尺寸变化的隐蔽错误。测试只覆盖 `k = +1`（`test/algebra_extended_test.cpp:198`）。
- 复现/验证：未复现（静态判定）。触发式：3 行矩阵 `circshift(m, -1, axis::rows)` 期望每列循环下移 1 行。
- 建议：`k` 改带符号，或调用点显式传 `rows - 1`。

### A7. `preci<type>` 对未特化类型失效：整型下守卫静默失效、`f16` 下直接编译失败
- 位置：根因 `algebra/value.h:42-45`（`template <typename type = float> constexpr type preci = 0;` 只有 `float`/`double`/`long double` 三个特化）；消费点 `linalg.h:62`（`rank` 阈值）、`linalg.h:132`（`mldivide` 奇异判据）、`algebra/tensor/linalg.h:382`（`svd` 判零奇异值）、`:418`（`lu` 主元判据）
- 现状（两副面孔，均已实测）：
  - **整型**：`preci<int>` = 0 → `abs(U(i,i)) < 0`、`maxv < 0` 恒假，判据全部失效；`rank` 的 `tol = S[0] × 0 × dim = 0`。实测 `matrix<int>` 下 `rank`/`mldivide`/`sum` **编译通过**（即静默）。
  - **`f16`**：`f16 = xhqm::float_16`（`algebra/ftype.h:65`）是类，其 `float` 构造不是 constexpr（`algebra/ftype.h:27` `float_16(const float& fValue);`），所以 `preci<float_16>` 实例化直接报错。`float_16` 自己带了 `preci = 0.001f`（`algebra/ftype.h:112`），但那是**类静态成员**，变量模板取不到。
- 证据：实测
  `printf '#include "algebra/analysis/analysis.h"\nint main(){ xhqm::matrix<f16> m(2,2); auto r = xhqm::analysis::rank(m); (void)r; }' | g++ -std=c++17 -I. -x c++ - -fsyntax-only`
  → `algebra/analysis/../tensor/../value.h:42:57: error: call to non-'constexpr' function 'xhqm::float_16::float_16(const float&)'`（`mldivide` 同）；而 `matrix<int>` 版本编译通过（`rank`/`mldivide`/`sum` 三项均 OK）。
- 影响：整型矩阵上「奇异 → 空容器」的约定失效、转而产生 inf/NaN；`rank` 把数值零奇异值计入秩。`f16` 走不到运行期，是硬编译错误。后端 `dtype_of` 明确承认 `f16` 与各整型（`algebra/tensor/backend/tensor_backend.h:44,48-51`）。
- 复现/验证：编译部分已实测（上面两条命令）；整型的运行期后果未复现（静态判定：判据为 `< 0`，恒假）。
- 建议：给 `float_16` 补 `preci<>` 特化，或在 `rank`/`mldivide` 等消费点对整型/无特化类型 `static_assert` 拦掉。

---

## B. 资源与并发

### B1. `rand`/`randn` 的静态 `mt19937` 无同步 —— 并发调用是数据竞争
- 位置：`construct.h:97`、`construct.h:110`
- 现状：`static std::mt19937 gen(5489u);` 是**函数内静态对象**（C++11 保证初始化线程安全，但对象本身无锁）。分布对象每次新建，引擎跨调用持续复用。
- 证据：`construct.h:94-102`、`construct.h:107-115` 两处都是同一个模式；全模块没有任何 `mutex`/`thread_local`/`atomic`（grep `seed|srand|mt19937` 仅命中这两行 + README）。
- 影响：两个线程同时调用 `analysis::rand<double>`（或 `randn`）会并发修改同一引擎状态 → 数据竞争，UB；此外同一模板实例的序列与调用顺序耦合（`rand<f32>`/`rand<f64>` 是各自独立的两条流）。
- 复现/验证：未复现（需 TSan + 多线程，本次未运行）。
- 建议：改 `thread_local`，或提供显式 seed/引擎参数。

### B2. `unary`/`unaryv` 在回退路径上白分配一整块结果
- 位置：`math.h:45-52`（`unary`）、`math.h:54-61`（`unaryv`）
- 现状：函数先 `matrix<type> r(m.rows(), m.cols());`（默认构造 + 填初值），再问后端 `backend.unary(r.data(), ...)`；后端说「不支持」时**不回收这块 `r`**，转而 `return map(m, ...)` 又分配一块同样大小的矩阵。默认构建（`no` 后端，`create_backend` 全部返回 `no`）以及所有非 f32/f64 类型都必然走这条回退路径。
- 证据：`if (...unary(r.data(), ...)) return r; return map(m, std::forward<F>(scalar));` —— `r` 在回退分支被丢弃；`math.h` 里每个逐元素接口（`sin/cos/exp/log/sqrt/abs/floor/...`）都经这条路径。
- 影响：每次逐元素调用多一次「分配 + 写满」整块矩阵的开销（n×n 即 n² 元素），在默认后端下是纯浪费。
- 复现/验证：未复现（未计时）。
- 建议：先问后端能力再分配，或把 `r` 作为输出缓冲传给 `map`。

---

## C. 跨平台

### C1. 构建与平台集成：两套开关、两个工程，本模块都没登记全
- 位置：`CMakeLists.txt:41`（`option(XHQM_BUILD_TENSOR "Build tensor/analysis module" OFF)`，**默认 OFF**）、`:552`（`-DXHQM_BUILD_TENSOR`）、`:275-312`（`XHQM_HEAD_TENSOR` 只有 `algebra/analysis/analysis.h`，`XHQM_BODY_TENSOR` 只有 `algebra/analysis/analysis.cpp`）、`xhqm.h:41-46`（门面 `XHQM_USE_ALGEBRA` → `xhqm/analysis`）、`analysis.h:1-2`
- 现状：
  - 门面链路完整：`XHQM_USE_ALGEBRA`（`xhqm.h:41`）→ `#include "xhqm/analysis"`（`xhqm.h:45`）→ `#include "../algebra/analysis/analysis.h"`（`xhqm/analysis:2`），实测 `#include "xhqm/analysis"` 编译通过。
  - CMake 开关名是 `XHQM_BUILD_TENSOR`，与门面宏 `XHQM_USE_*` 不是同一族（同名 `-D` 宏在 `CMakeLists.txt:552` 定义，但**本模块源码里没有任何一处引用它**，只有 include guard）。该开关下本模块 11 个文件只登记 2 个（`analysis.h`、`analysis.cpp`），另 9 个头（`axis/construct/array/math/linalg/stats/poly/signal/convert`）未登记 —— 仅影响 IDE 源树，仓库**没有 `install()` 规则**，不影响编译与安装。
  - `xhqm.vcxproj` 的 105 条 `Include=...` 里**没有** `algebra/analysis`，也**没有** `algebra/tensor`（该工程实际只含 `algebra/{ctype,ftype,itype,mathf,value}` + `network/*` + `head.h`）→ Windows/MSVC 侧根本编不到本模块，只能经 CMake。
  - 头文件宏名不一致：聚合头是 `XHQM_ALGOR_ANALYSIS`（`analysis.h:1-2`），其余 9 个是 `XHQM_ANALYSIS_*`。
- 证据：上述 grep/实测命令均已在 2026-10-07 跑过（`grep -o 'Include="[^"]*"' xhqm.vcxproj | wc -l` = 105；`grep -n "analysis" xhqm.vcxproj` 0 命中）。
- 影响：MSVC 用户拿不到本模块；把 CMake 开关名与门面宏名当同一件事的人会白折腾（两族名字对不上不是本模块独有的问题，见根 `README.md:134`）。
- 复现/验证：`grep -rn "XHQM_BUILD_TENSOR" algebra/analysis/` → 0 命中；`grep -o 'Include="[^"]*"' xhqm.vcxproj | wc -l` → 105。
- 建议：CMake 头集补全 9 个头（或明确只作 IDE 用途的说明）；聚合头 guard 拼写与其余 9 个统一。

---

## D. 性能

### D1. `axis::rows` 归约不走后端 kernel（逐列归约走通用路径 + 跨步访问）
- 位置：`stats.h:62`（`sum(m, axis)`）、`stats.h:266,273,283,289,366,371`（`prod/mean/max/min/var/stddev` 同构）；分派点 `algebra/tensor/linalg.h:137-141`、`:114-134`
- 现状：`reduce_sum(t, axis)` 只有 `axis == dim - 1` 才走 `reduce_last` → `backend.reduce(...)`（SIMD/GPU kernel）；2-D 张量里 `axis::cols`(=1) 命中，`axis::rows`(=0) 落到通用 `reduce`（`algebra/tensor/linalg.h:87-112`）：按输出元素 `parallel_for`、内层对 `t.value(t.index(coord))` 串行累加且按行跨步取数。
- 证据：`if (axis == dim - 1) return reduce_last(t, reduce_op_sum);` 与 `return reduce(t, axis, ...)` 两条分支（`algebra/tensor/linalg.h:139-140`）；调用侧把枚举直接转维度号 `(xhqm::size)a`（`stats.h:62`，`axis::rows = 0`）。
- 影响：MATLAB 默认方向的归约（每列）在大矩阵上拿不到 SIMD/GPU 归约，且访存不连续。
- 复现/验证：未复现（未计时/未看反汇编）。
- 建议：给 axis=0 也下沉 kernel（或在上层转置后走末维）。

### D2. `orth`/`null` 各算两次 SVD
- 位置：`linalg.h:164-165`（`orth`）、`linalg.h:178-179`（`null`）、`linalg.h:60`（`rank` 内部的 `svd`）
- 现状：两个函数都先 `auto sv = xhqm::tensor_base::svd(to_tensor(m));` 取 U/V，紧接着 `const xhqm::size r = rank(m);` —— 而 `rank` 自己又跑一次完整 SVD。
- 证据：`linalg.h:164` + `:165`；`:178` + `:179`；`rank` 的 SVD 在 `:60`。SVD 经 Jacobi 特征分解（`algebra/tensor/linalg.h:355-394`），是这两个函数的主体开销。
- 影响：成本约 2×，且 `orth`/`null` 常被放在循环里（子空间迭代）。
- 复现/验证：未复现（未计时）；静态判定明确。
- 建议：用已算好的 `sv.S` 现算秩（或给 `rank` 加一个接收奇异值的重载）。

### D3. `polyfit` 走正规方程，条件数被平方
- 位置：`poly.h:89-94`
- 现状：`Vt = V.transpose(); A = Vt * V; b = Vt * ymat; c = mldivide(A, b);` —— 经典正规方程（normal equations）。`κ(VᵀV) = κ(V)²`，`deg` 稍大或 x 跨度大时有效位直接掉一半以上；这也是 A3 那条越界路径的同一个成因面。
- 证据：`poly.h:89-94`；对照 MATLAB `polyfit` 用 QR。模块内已有 QR：`xhqm::tensor_base::qr`（`linalg.h:103`）。
- 影响：拟合系数精度（尤其高次/病态），且比 QR 更容易撞上奇异判据。
- 复现/验证：未复现（未做精度对比）。
- 建议：改 QR 路线（`A = Q·R` 后用 `R` 回代）。

### D4. 全元素归约与分位数是串行桩，且每次调用都整段拷贝
- 位置：`algebra/tensor/linalg.h:164-195`（`sum_all`/`prod_all`/`max_all`/`min_all` 自带注释「CPU 并行 = CPU 串行（可并行归约，暂未回补）」）、`algebra/tensor/stats.h:141-150, 152-168, 170-185`（`median`/`mode`/`prctile`）
- 现状：`analysis::sum(m)`/`prod`/`max`/`min`（全元素版）走串行循环；`median`/`mode`/`prctile`/`quantile`/`iqr` 每次调用都 `std::vector<type> s(n)` 整段拷贝 + `std::sort`（O(n log n)），`iqr` 更是调两次 `prctile` → 两次拷贝两次排序。
- 证据：`sum_all`（`algebra/tensor/linalg.h:166-172`）`for` 累加无并行；`median`（`algebra/tensor/stats.h:143-149`）拷贝+排序；`iqr`（`algebra/tensor/stats.h:189`）`prctile(75) - prctile(25)`。
- 影响：最常用的 `sum(A)` 拿不到并行；`iqr`/`median` 在统计循环里开销约 2×~n log n。
- 复现/验证：未复现（未计时）。
- 建议：归约按块并行；`iqr` 复用一次排序结果。

---

## E. 未实现 / 占位桩

### E1. `eigs`/`svds` 确实不存在（稠密单数形式已实现）
- 位置：`linalg.h:204`（`svd`）、`:222`（`eig`）、`:242`（`schur`）、`:253`（`roots`）
- 现状：2026-10-07 全仓源码 grep `eigs|svds` **0 命中**（只有 `README.md`、`algebra/README.md` 等文档提到）→ 旧说法「eigs/svds 未实现」**为真**；但「roots/schur 暂缓」已过时，两者都实现并被 `test/algebra_extended_test.cpp:106,115` 覆盖。
- 证据：`grep -rn "eigs\|svds" --include=*.h --include=*.cpp algebra/` → 0；`algebra/analysis/README.md:148` 已写明。
- 影响：大稀疏矩阵只能做全量稠密分解（Krylov 迭代类接口缺失）。
- 复现/验证：上面的 grep 即复现。
- 建议：维持暂缓（README §7 已声明）。

### E2. 复数支路未覆盖：6 个 API 对 `matrix<complex<T>>` 直接编译失败
- 位置（失败报错点全在 tensor 后端）：`linalg.h:42`(`inv`)→`algebra/tensor/linalg.h:1078`；`linalg.h:206`(`svd`)→`:380`；`linalg.h:47-53`(`norm` 四种全挂)→`:1026`（`norm_fro` 在 `:1013-1018`）；`linalg.h:113`(`chol`)→`:769`；`stats.h:194`(`median`)→`<algorithm>` 比较器；`stats.h:254`(`sort`)→`:991`
- 现状：实测（每个表达式一个 TU，`-fsyntax-only`）：
  - **可用**：`sum`、`abs`、`max`、`var`、`cumsum`、`mtimes`、`det`、`trace`、`fft`
  - **编译失败**：`inv`、`svd`、`norm`（`fro`/`one`/`inf`/`spectral` 四种都失败）、`chol`、`median`、`sort`
  - 典型报错：`error: no match for 'operator>' (operand types are 'const std::complex<double>' and 'std::complex<double>')`（`:380`，复数无实序关系）、`error: no matching function for call to 'sum(std::complex<double>&, double)'`（`:1026`）、`error: no match for 'operator<=' ...`（`:769`）
- 证据：上面每条的编译命令 + 报错文本（2026-10-07 实测，命令见「复现/验证」）。
- 影响：`analysis` 是零拷贝薄封装，后端缺口原样暴露成分析层**编译期**失败（不是运行期错值，但也意味着「MATLAB 里能跑」的复数线代在这里不能写）。
- 复现/验证：
  `printf '#include "algebra/analysis/analysis.h"\nusing namespace xhqm; using namespace xhqm::analysis;\nint main(){ matrix<complex<double>> A(2,2), C(2,2); auto r = norm(A); (void)r; return 0; }' | g++ -std=c++17 -I. -x c++ - -fsyntax-only`
- 建议：README 明写「复数仅逐元素与全元素归约可用」（本轮已写进 §7 成熟度）；要真支持则得在 tensor 后端加复数排序/比较。

### E3. MATLAB 对应重载缺失（grep 确认不存在）
- 位置：`stats.h:408-414`（`nansum`/`nanmean`/`nanmax`/`nanmin` **只有 vector 版**，无 matrix 版）、`stats.h:222-226`（`gradient` 只有 1-D）、`stats.h:192`（无 `median(m, axis)`）、`stats.h:252`（无 `sort(m, axis)`）、`stats.h:389-404`（`all`/`any` 只有 `matrix<bool>`/`vector<bool>`，数值型不能用）、`signal.h:57`（`cumtrapz` 只有 `(y, x)` 两参，缺 `cumtrapz(y)`）、`construct.h:137`（`meshgrid` 只有 `(x, y)` 两参，缺单参）、`construct.h:51,61`（`diag` 无 `k` 偏移）；`fft2`/`ifft2` 全模块 0 命中。
- 现状：这些重载没有实现（不是桩写了一半）→ 调用即编译失败。
- 证据：上面每处的签名行；`grep -rn "fft2\|nanmedian\|cumtrapz" algebra/analysis/` 只命中 `cumtrapz`。
- 影响：MATLAB 迁移代码在编译期报错（相对安全），但覆盖面比 README 的函数清单暗示的窄（README 只列名、不列重载）。
- 复现/验证：`grep -n "cumtrapz\|fft2" algebra/analysis/*.h`。
- 建议：按需补 vector/matrix 对称重载；README 的函数表加「重载范围」说明。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. `rand`/`randn` 固定种子 5489，不提供种子注入
- 位置：`construct.h:97`、`construct.h:110`（`static std::mt19937 gen(5489u);`）
- 为什么现在这样是对的：可复现优先 —— 同一段代码在任何机器、任何进程都给出同一串随机数，回归用例和 bug 复现不必先固定种子；接口面只留 `rand(m,n)`/`rand(n)` 两个形状重载，没有 engine/seed 参数要传。
- 代价：不可按次注入种子（没法「同分布两种采样」）；并发不安全（见 **B1**）；`rand<f32>` 与 `rand<f64>` 是两条独立流，交叉调用时序列不再是同一条；用 `uniform_real_distribution<double>` 再窄化到 `type`（`construct.h:98,100`）。

### F2. 失败一律用空容器，不用异常/告警
- 位置：实现如 `array.h:43,54,68`、`linalg.h:89`（`lu`）、`:128,130`（`mldivide`）、`:181`（`null`）、`:195`（`rcond` 的 `inv` 判空）、`algebra/tensor/linalg.h:405`（`lu` 非方阵/空）、`:752`（`chol` 非方阵）、`:769`（`chol` 非正定）
- 为什么现在这样是对的：header-only + 无异常约定（跨 CUDA/Metal 后端也没有统一 error 通道）；调用方统一用 `size()==0` / `rows()==0` 判定，比每个函数带 out-param 或抛异常更省事，也不给头文件加 `try/catch` 负担。
- 代价：与 MATLAB「报错/警告」风格不同；而且**不彻底** —— `cond`/`mean`/`max`/`min` 的空输入返回 `0`、`rank` 直接越界（**A2**），调用方不能只靠「是否为空」判定（README §1 已加注）。

### F3. 强类型枚举替代 MATLAB 的 1 基 dim 与字符串参数
- 位置：`axis.h:12-39`；默认值 `linalg.h:47`（`norm_type::spectral`）、`stats.h:364/369`（`variance::sample`）、`stats.h:383`（`sort_order::ascend`）
- 为什么现在这样是对的：`sum(A, 1)` 这种「1 到底是行还是列」的历史歧义在类型系统里消失；`variance`/`sort_order` 也是具名而非布尔/魔数。
- 代价：不能照抄 MATLAB 的 `dim=1/2`（`axis::rows`=0 表示「每列归约」，语义与字面直觉相反，必须读 `axis.h:14-15` 的注释）；`enum class` 也让 `norm(m, 1)` 这类写法直接编译失败（**A1** 就是踩了这个）。

### F4. 薄封装 + `values()` 零拷贝，不在分析层重写算法
- 位置：`convert.h:17-19`（`as_t1`/`as_t2`）、`stats.h:22-24`（`as_tensor`/`as_tensor1`）、`linalg.h:18-22`（`to_tensor`/`from_tensor`）
- 为什么现在这样是对的：算法只有一份真源在 `tensor_base`，no/cpu/simd/gpu/metal 五个后端一处受益；分析层只做命名与语义适配，加一个 API 不新增算法分支。
- 代价：后端缺口原样暴露（**E2** 复数、**D1** axis::rows）；同义转换工具存在**三套名字**（`convert.h` 的 `as_t1/as_t2/as_vec/as_mat`、`stats.h:19-44` 的 `as_tensor/as_matrix/as_vector/as_t1_flat`、`linalg.h:18-22` 的 `to_tensor/from_tensor`），改一处要同步三处；返回值一律要 `matrix(t)` 包装构造一次 memcpy（`convert.h:22-24`）。

### F5. `chol` 返回下三角 `L`（`A = L·Lᵀ`），与 MATLAB 的 `R'R` 约定相反
- 位置：`linalg.h:109-114`；后端 `algebra/tensor/linalg.h:749-754`（`backend.chol` = LAPACK/cuSOLVER `potrf`，输出下三角）
- 为什么现在这样是对的：与 `potrf` 的输出方向一致，省一次转置；`A = L·Lᵀ` 在解方程时与 LU 的前代/回代一致。
- 代价：MATLAB 迁移代码要转置（MATLAB `chol` 默认取上三角给 `R`，`A = R'R`）；且不校验输入对称性 —— 只读下三角（`algebra/tensor/linalg.h:756-758`），传入非对称矩阵会静默拿到「下三角部分的 Cholesky」。

### F6. `cumsum`/`cumprod(matrix)` 是展平前缀（返回 `vector`），不是 MATLAB 的沿维累积
- 位置：`stats.h:136-139`、`stats.h:149-152`；沿维版另有重载 `stats.h:163-171`
- 为什么现在这样是对的：1-D 桩（`cumsum_last`/`cumprod_last`）只处理末维，展平后语义与「返回 `vector`」的类型自洽（matrix 入参、vector 出参，一眼知道形状变了）。
- 代价：与 MATLAB 不一致（MATLAB 对矩阵默认沿首个非单一维累积）；从 MATLAB 迁过来的人会拿到按行优先展平的 1-D 结果而不是逐列累积 —— 需要沿维必须显式写 `cumsum(m, axis::rows)`。

### F7. 后端可插拔，默认 `no` 后端全部回退标量
- 位置：`algebra/tensor/backend/tensor_backend.cpp:27-60`（`create_backend`：auto→gpu→cpu→no；`gpu` 无 CUDA、`net` 占位都回退 `cpu`/`no`）、`CMakeLists.txt:268-269`（`tensor_backend.cpp` + `tensor_backend_no.cpp` **恒编入**）、`CMakeLists.txt:41`
- 为什么现在这样是对的：不依赖 BLAS/CUDA/Metal 也能编译运行（`no` 后端每个算子返回 `false`，上层自动标量回退）；后端缺失是软失败而非链接错误。
- 代价：默认构建（`XHQM_BUILD_TENSOR=OFF`、后端 `no`）下 README §1 提到的 SIMD/CUDA/Metal kernel **一个都不会命中**，所有逐元素/归约走标量回退 —— 任何性能结论都必须标注「哪个后端、哪个开关」。

---

## G. 文档与代码的出入

### G1. 兄弟文档仍写「roots/schur 暂缓」，与本模块代码矛盾
- 位置：`algebra/README.md:216`（「暂缓：`roots/schur/eigs/svds`（需迭代算法）」）
- 现状：`roots`（`linalg.h:253`）与 `schur`（`linalg.h:242`）**已实现**且被测试覆盖（`test/algebra_extended_test.cpp:106,115`）；根 `README.md:156` 已订正过这一点，`algebra/README.md` 没跟着改。
- 证据：上面三处行号 + 本模块 `README.md:148`。
- 影响：接手者按 `algebra/README.md` 判断会以为要自己实现 `roots`/`schur`。
- 复现/验证：`grep -n "暂缓" algebra/README.md`。
- 建议：上游把 `algebra/README.md:216` 同步成「仅 `eigs/svds` 暂缓」。**本文件所属任务只允许改 `algebra/analysis/` 下的两份文档，故此处只记录不动手。**

### G2. 跨文档行号引用已漂移（根 `README.md:156` → 本模块 README）
- 位置：`README.md:156`（根，原文引「见 `algebra/analysis/README.md:128`」）、本模块 `README.md:148`
- 现状：那条 `eigs/svds` 暂缓项本轮编辑后从 `:128` 移到了 **`:148`**（本轮在它前面加了日期行、`rcond` 注、用法示例说明）；引用方是根 README，不在本次可改范围内。
- 证据：`grep -n "暂缓" algebra/analysis/README.md` → `148:`；根 `README.md:156` 仍写 `analysis/README.md:128`。
- 影响：接手者按根 README 的行号找到的是 §6 用法示例里的说明行，不是那句话。
- 复现/验证：`sed -n '148p' algebra/analysis/README.md`。
- 建议：上游把根 `README.md:156` 的行号引用改成 `:148`（或改成不带行号的指法）。

### G3. 根 README 对本模块复数能力的概括不准

- 位置：`README.md:87`（根；「缺复矩阵完整覆盖（除 fft）」）
- 现状：复数并非「除了 fft 全缺」，也并非「只有 fft 可用」—— 实测 `sum`/`abs`/`max`/`var`/`cumsum`/`mtimes`/`det`/`trace`/`fft` **都可用**，`inv`/`svd`/`norm`/`chol`/`median`/`sort` 编译失败（详见 **E2**）。另外「缺多维张量归约/统计」应表述为「本模块公开面只有 `matrix`/`vector` 入口，N 维归约在 `algebra/tensor/linalg.h:87` 已有实现但未从分析层暴露」。
- 证据：E2 的实测清单；`analysis/` 下所有公开签名只接 `matrix<type>`/`vector<type>`。
- 影响：按根 README 判断会低估（也误解）本模块的复数覆盖范围。
- 复现/验证：见 E2 的编译命令。
- 建议：上游订正根 `README.md:87`。**同上，本次不动本模块目录外的文件。**

### G4. 标准库依赖靠传递包含（隐式依赖，本模块没有直接 include）
- 位置：`array.h:8-9`（只引 `<array>`/`<utility>`，却用 `std::vector<xhqm::index>`）、`construct.h:7`（只引 `<random>`，却用 `std::is_integral_v` 与 `::std::pow`）、`linalg.h:10`（只引 `<array>`，却用 `std::numeric_limits`）、`poly.h:7-12`（用 `::std::pow` 却没引 `<cmath>`）
- 现状：README §5 把 `<cmath> <random> <array> <utility> <algorithm> <limits> <map> <vector> <type_traits>` 列为依赖，但其中 `<vector>`/`<cmath>`/`<type_traits>`/`<limits>` 在模块内的直接 include 数**为 0** —— 全靠 `../tensor/*.h` 把它们带进来。今天能编过纯粹是传递包含恰好成立。
- 证据：`grep -n "#include <" algebra/analysis/*.h` → 只出现 `<array> <utility> <random> <cmath> <type_traits> <algorithm> <limits> <map>`，没有 `<vector>`；GCC 11 实测 9 个子头各自单独 `-fsyntax-only` 全部通过（即当前不是故障）。
- 影响：上游 tensor 头一旦调整 include，本模块会以「莫名其妙的 `std::vector` 未声明」形式炸掉；换标准库实现（libc++/MSVC STL）时更容易暴露 —— 本模块在 MSVC 侧根本没有构建登记（见 **C1**），一旦补登记就会先撞上这里。
- 复现/验证：`for h in axis construct array math linalg stats poly signal convert; do printf '#include "algebra/analysis/%s.h"\nint main(){return 0;}\n' $h | g++ -std=c++17 -I. -x c++ - -fsyntax-only || echo FAIL $h; done`（本次实跑：9 个全过）。
- 建议：哪个头用哪套标准库工具就显式 include（加 `<vector>`、`<cmath>`、`<type_traits>`、`<limits>`，成本为零）。
