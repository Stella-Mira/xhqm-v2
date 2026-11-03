# tensor 模块 —— 当前仍存在的问题

`algebra/tensor`（N 维张量 + 多后端调度）的缺陷台账：算错/崩溃、资源与并发、跨平台、性能、
未实现桩、有意取舍与文档出入，逐条给出代码位置与复现方式。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。

与同类文件的关系：本模块此前没有台账，本文件即其「当前问题」清单；`README.md` 只保留使用说明与
被推翻旧说法的作废标注（含 2026-10-07 复核结论），问题细节一律看这里。历史台账按本仓约定
**不搬家、不改名**——`fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` 各自留在原处，
本文件不复制它们的内容。`test/ffbo/*.md`、`test/nebula_api/PROTOCOL.md` 是别处的设计/协议稿，与本文无关。

统计口径：以下 **35** 条均为 r677 工作副本中**仍存在**的问题（含 F 组「有意为之、不修」的取舍项）：
A 组 6 条、B 组 3 条、C 组 3 条、D 组 6 条、E 组 5 条、F 组 6 条、G 组 6 条。

---

## A. 功能性缺陷（会算错 / 会崩）

### 池化/卷积输出尺寸在 kernel 大于输入时无符号下溢，直接申请天文数字内存

- 位置：`conv.h:16-19`（`conv_out_size`）、`pool.h:14-17`（`pool_out_size`）
- 现状：两个函数都是 `(in + 2 * padding - k) / stride + 1`（conv 还多减 `dilation * (k - 1)`），
  运算数全是 `xhqm::size`，而 `xhqm::size` 就是 `std::size_t`（`head.h:219`）。当
  `k > in + 2 * padding` 时中间结果回绕成极大值，返回值随即被当作输出维度去构造张量。
- 证据：`pool.h:16` `return (in + 2 * padding - k) / stride + 1;`，`conv.h:18` 同构；
  消费点 `pool.h:25-26`（`Lo` → `tensor<3,type> out({N,C,Lo})`）、`conv.h:45-51`、`conv.h:159-160`。
  例：`pool_out_size(1, 3, 1, 0)` 实际算出 `(1-3)/1+1`，无符号下溢后再 `+1`。
- 影响：任何「窗口/核比输入大」的调用（1×1 特征图配 3×3 池化、卷积 padding 不足、
  `k` 传成 0 以外的异常值）会抛 `std::bad_alloc`/`std::length_error` 或直接吃光内存，
  而不是给出「参数非法」这种可诊断错误。
- 复现/验证：未在本轮执行（静态推导自上述表达式）。可跑：

  ```sh
  g++ -std=c++17 -I. -x c++ - -o /tmp/pool_uf <<'EOF' && /tmp/pool_uf
  #include "algebra/tensor/pool.h"
  #include <cstdio>
  int main(){ std::printf("%zu\n", xhqm::tensor_base::pool_out_size(1,3,1,0)); }
  EOF
  ```

- 建议：两个 `*_out_size` 里加一句 `if (k > in + 2 * padding) throw xhqm::error(...)`，并校验 `stride != 0`。

### `tensor::index(flat)` 对含 0 维的张量做整数除零

- 位置：`tensor_base.h:61-71`（关键行 `tensor_base.h:67`）
- 现状：一维下标转坐标的实现逐维取模再整除：`coord[j] = flat % m_counts[j]; flat /= m_counts[j];`。
  只要任一维为 0，就是 `% 0` / `/ 0`。
- 证据：`tensor_base.h:67-68`；而「空张量」在本模块是**常规返回值**——形状不匹配时
  `matmul` 返回默认构造张量（`linalg.h:39-40`，`m_counts` 全 0）、`broadcast_binary` 不可广播时同样
  （`tensor_base.h:91`）、`diff`/`gradient`/`fft` 等也返回空。
- 影响：`tensor<2,f32> t({0, 3}); t.index(0);` 或对上述任何空返回值再调 `index(flat)` 即崩溃
  （整数除零，x86 上是 SIGFPE，属 UB）。
- 复现/验证：未在本轮执行（静态推导）。可跑：

  ```sh
  g++ -std=c++17 -I. -x c++ - -o /tmp/idx0 <<'EOF' && /tmp/idx0
  #include "algebra/tensor/tensor.h"
  int main(){ xhqm::tensor<2, xhqm::f32> t({0, 3}); volatile auto c = t.index(0); return (int)c[0]; }
  EOF
  ```

- 建议：`index(flat)` 开头对 `m_counts[j] == 0` 直接返回零坐标（或抛 `xhqm::error`）。

### COO 稀疏不校验三元组，且 `dense()` 覆盖语义与 SpMV 累加语义不一致

- 位置：`sparse.h:44-46`（三元组构造）、`sparse.h:62-68`（`dense()`）、`sparse.h:71-82`（`operator*`）
- 现状：`sparse_matrix(rows, cols, r, c, v)` 原样接收 `r/c/v`，不检查 `r[i] < rows`、`c[i] < cols`，
  也不处理重复 `(row,col)`。`dense()` 用 `d(row,col) = val`（**赋值**，后者覆盖前者），
  而 SpMV 路径按 `y[row] += val * x[col]`（**累加**）走（后端同理，`backend/tensor_backend_cpu.cpp:133,144`）。
- 证据：`sparse.h:66` `d(m_row[k], m_col[k]) = m_val[k];` vs `sparse.h:80`
  `r[m_row[k]] = xhqm::sum(r[m_row[k]], xhqm::mul(m_val[k], v[m_col[k]]));`；
  后端入口 `sparse.h:76-77` 直接把裸指针交给 `spmv()`，越界坐标在 `tensor_backend_cpu.cpp:133`
  写成 `Y[row[k]]`、`X[col[k]]`。
- 影响：① 由重复三元组构造的矩阵，`dense()` 与 `S * x` 结果**互相矛盾**（数值还随顺序变化）；
  ② 越界坐标会让 SpMV 越界写 `y` / 越界读 `x`（缓冲区溢出，非异常）。
- 复现/验证：未在本轮执行。可跑（预期 `d(0,0) != y[0]`）：

  ```sh
  g++ -std=c++17 -I. -x c++ - -o /tmp/coo <<'EOF' && /tmp/coo
  #include "algebra/tensor/tensor.h"
  using namespace xhqm;
  int main(){
    store<f32> v; v.push_back(2.f); v.push_back(3.f);
    tensor_base::sparse_matrix<f32> S(2,2,{0,0},{0,0},v);   // 两个重复三元组 (0,0)
    matrix<f32> d = S.dense(); vector<f32> x(2,1.f); auto y = S*x;
    return (int)(d(0,0) + y[0]);
  }
  EOF
  ```

- 建议：构造时校验/去重（或明确文档化为「必须无重复、坐标须在界内」并在 `dense()` 里改成累加）。

### `upsample_bilinear2d` 在输入存在 0 维时越界读

- 位置：`pool.h:251-284`（关键行 `pool.h:268-269`、`pool.h:282-283`）
- 现状：`H`/`W` 为 0 时，`(double)(H - 1)` 是 `size_t` 下溢后的巨大 double，clamp 语句
  `if (sh > (double)(H - 1)) sh = (double)(H - 1);` 因此拦不住任何值，`h0 = (xhqm::size)sh` 得 0，
  随后 `x(n, c, h0, w0)` 在 0 元素张量上取下标。
- 证据：`pool.h:268-269`（clamp）、`pool.h:270-273`（`h0/w0`）、`pool.h:281-283`（四处 `x(n,c,...)` 读取）。
- 影响：形状里空间维为 0（例如占位 batch、或上游算子的空输出）时，读越界内存 → 崩溃或静默脏数据。
- 复现/验证：未在本轮执行。可跑：

  ```sh
  g++ -std=c++17 -I. -x c++ - -o /tmp/up <<'EOF' && /tmp/up
  #include "algebra/tensor/tensor.h"
  using namespace xhqm;
  int main(){
    tensor<4,f32> x({1,1,0,4}, 0.f);                        // H == 0
    auto y = tensor_base::upsample_bilinear2d(x, 4, 4);
    return (int)y.size();
  }
  EOF
  ```

- 建议：函数开头加 `if (H == 0 || W == 0) return 空张量;`（或在 clamp 前先判 `H > 0`）。

### `fft` / `ifft` 对非 2 的幂长度静默返回空张量

- 位置：`signal.h:22`（`fft`）、`signal.h:59`（`ifft`）、判据 `signal.h:15`
- 现状：`if (!is_pow2(n)) return xhqm::tensor<1, xhqm::complex<type>>();` —— 直接返回一个 0 元素的张量，
  不抛异常、不置错误码、不写日志。
- 证据：`signal.h:15` `inline bool is_pow2(xhqm::size n) { return n != 0 && (n & (n - 1)) == 0; }`；
  `signal.h:22`、`signal.h:59` 两处早退。
- 影响：对 1000 点做 `fft` 得到空结果，调用者常在后续 `ifft` / 取模运算才发现，且已无法定位到输入；
  与同模块 `where`/`gather`/`layer_norm` 抛 `xhqm::error` 的风格不一致（`nn.h:20,42,57,71`、`norm.h:64`）。
- 复现/验证：`grep -n "is_pow2" algebra/tensor/signal.h` 即可看到两处静默早退；运行时可用
  `tensor_base::fft(complex<f32> 长度 1000 的向量).size() == 0` 验证。
- 建议：非 2 的幂改为抛 `xhqm::error("fft 长度须为 2 的幂")`，或补零到 2 的幂并回报原始长度。

### 两套后端切换入口语义分叉：`active_backend(kind)` 不会切换已建好的后端实例

- 位置：`backend/backend.h:28-33`、`backend/tensor_backend.cpp:63-75`
- 现状：`xhqm::tensor_base::active_backend(kind)`（`backend.h:31`）写的是 `hardware::active_device_ref()`；
  而张量后端的实例是**懒创建一次**的：`tensor_backend_active()` 只在 `active_ptr()==nullptr` 时按
  `hardware::active_device()` 建一次，之后永不重建（`tensor_backend.cpp:63-68`）。真正换实例的是另一个
  函数 `tensor_backend_select(kind)`（`tensor_backend.cpp:70-75`），它**不**回写 `hardware::active_device()`。
- 证据：`backend.h:31` `inline void active_backend(const device& kind) noexcept { xhqm::hardware::active_device(kind); }`
  vs `tensor_backend.cpp:65-67` `if (!active_ptr()) active_ptr() = create_backend(xhqm::hardware::active_device());`。
- 影响：先调用过一次任意张量算子（实例已建）之后再 `active_backend(device::cpu)`，设备查询会说 cpu、
  实际算子仍跑在原后端上——「切换成功」是假的；反过来 `tensor_backend_select` 之后 `active_device()`
  仍报旧设备，日志/缓存打标（`hardware.h:110-122` 的 `device_name`）与真实执行路径不符。
- 复现/验证：未在本轮执行。可验证：
  `tensor_base::tensor_backend_select(device::gpu)` 后打印 `tensor_base::tensor_backend_active().name()`（变化），
  再 `tensor_base::active_backend(device::cpu)` 后重打印（**不变**）。
- 建议：让 `active_backend(kind)` 转发到 `tensor_backend_select(kind)`（或反之二者共用一份状态），并在头注释里点明。

---

## B. 资源与并发

### SpMV 的并行部分和缓冲是「线程数 × 行数」，阈值却只看 nnz

- 位置：`backend/tensor_backend_cpu.cpp:137`（算术）、`backend/tensor_backend_cpu.cpp:169`（bool）、
  阈值判断 `tensor_backend_cpu.cpp:131,163`
- 现状：并行路径一次分配 `nt × rows` 个元素做每线程部分和（`nt = min(hardware_concurrency(), 32)`），
  是否并行只看 `nnz < XHQM_PARALLEL_THRESHOLD || hw == 1`，完全不看 `rows`。
- 证据：`tensor_backend_cpu.cpp:136-137` `const std::size_t nt = (hw > 32) ? 32 : hw;
  std::vector<std::vector<T>> partial(nt, std::vector<T>(rows, T(0)));`（bool 版见 `:168-169`）。
- 影响：行数大而 nnz 中等的稀疏矩阵最吃亏——`rows=10^8`、`nnz=5000`、`nt=32` 时额外要 32×10^8 个元素
  （f32 约 12.8 GB），极端情况直接 `bad_alloc`；即便不爆，每线程一份稠密向量也会把缓存冲干净。
- 复现/验证：未在本轮执行。可跑：构造 `rows=50_000_000`、`nnz=4096` 的 COO 后调 `S * x`，
  观察 RSS 峰值 ≈ `nt × rows × sizeof(T)`。
- 建议：改成分块（每线程只维护自己片段命中的行的部分和）或把 `rows` 也纳入降级判据。

### CUDA 设备内存池常驻不还，只在上限处停止缓存

- 位置：`backend/tensor_backend_gpu.cu:166-198`（池实现），上限 `tensor_backend_gpu.cu:168`
- 现状：`pool_free` 不 `cudaFree`，而是把缓冲按大小挂回 `g_free_buffers`；`g_pool_bytes + bytes > g_pool_limit`
  才真正释放，`g_pool_limit = 512ull * 1024ull * 1024ull`（512 MiB）。
- 证据：`tensor_backend_gpu.cu:191-196` `if (g_pool_bytes + bytes > g_pool_limit) { cudaFree(p); return; }
  g_free_buffers.emplace(bytes, p); g_pool_bytes += bytes;`；分配侧 `:173-181` 复用 `lower_bound` 命中的缓冲。
- 影响：一次大算子把显存顶到几百 MiB 后，即使后续不再用，这 512 MiB 仍被本进程占着——多进程/多显卡
  场景下会被别人当成显存泄漏；也说明「GPU 后端没有显式的显存回收 API」（`upload`/`download` 都是 no-op，
  `tensor_backend_gpu.cpp:248-255`）。
- 复现/验证：跑 `test/gpu_smoke.cpp` 后连续 `nvidia-smi` 观察进程显存不回落。
- 建议：提供 `synchronize()` 之外的显式 `shrink()`/`clear_cache()`（或环境变量调 `g_pool_limit`），并在 README 记明默认 512 MiB。

### `tensor_backend_select` 换实例时不释放旧实例（当前靠单例侥幸不泄漏）

- 位置：`backend/tensor_backend.cpp:70-75`
- 现状：`tensor_backend_select` 直接 `active_ptr() = target;`，旧指针既没 delete 也没归还，
  只靠「所有工厂都返回函数内 `static` 单例」来避免泄漏。
- 证据：`tensor_backend.cpp:72-74`；单例实现：`tensor_backend_no.cpp:33-37`、`tensor_backend_cpu.cpp:858-862`、
  `tensor_backend_gpu.cpp:259-263`、`tensor_backend_metal.mm:1518-1522`。
- 影响：现在不会漏，但任何一次「工厂改成 `new`」（例如未来加 net/多设备实例）就会每次切换泄漏一个后端；
  同时该接口没有线程安全（`active_ptr()` 是裸 `static tensor_backend*`，`tensor_backend.cpp:19-23`），
  多线程同时 `select` + `active` 是数据竞争。
- 复现/验证：`grep -n "active_ptr()" algebra/tensor/backend/tensor_backend.cpp`。
- 建议：工厂返回 `std::unique_ptr` 或让 `select` 记录并释放旧实例；`active_ptr()` 换成 `std::atomic`/加锁。

---

## C. 跨平台

### SIMD 内核没有任何平台守卫，只有 MSVC 能编译；注释声称的「其余平台回退标量桩」不存在

- 位置：`backend/tensor_backend_simd.h:8`（`#include <immintrin.h>`）、`tensor_backend_simd.h:72-76`、
  `tensor_backend_simd.h:483-489`、注释 `tensor_backend_simd.h:343`；构建侧 `CMakeLists.txt:158-162`
- 现状：该文件全文**只有 include 守卫一个条件编译**（无 `_MSC_VER`、无 `__AVX2__`、无 `#if defined`），
  而它调用的 `_mm256_atan2_ps` / `_mm256_pow_ps` / `_mm256_erf_ps` 等是 MSVC SVML 内建，
  GCC/Clang 并不提供。唯一的保护来自构建系统：`TENSOR_WITH_SIMD` 只在 `MSVC` 分支被定义。
- 证据：`CMakeLists.txt:158-161` `if(_tensor_mode STREQUAL "ON" AND MSVC) ... add_definitions(-DTENSOR_WITH_SIMD)`；
  `tensor_backend_simd.h:72-76`（`_mm256_atan2_ps` 等）；`tensor_backend_cpu.cpp:2-6` 的注释也承认
  「仅在 TENSOR_WITH_SIMD 开启时包含，避免在 ARM/macOS 上引入 immintrin.h 而编译失败」——
  即防线在 include 层，而不在文件内部。注释 `simd.h:343` 说「其余平台回退标量桩」，代码里没有这段分支。
- 影响：非 MSVC 平台（GCC/Clang、含 macOS/ARM）一旦手工 `-DTENSOR_WITH_SIMD`（或改用 GCC 编 Windows 目标、
  或有人把该头直接 include 进自己的 TU）立即编译失败，报的是一堆 `_mm256_*` 未声明；
  同时 Linux 上 `tensor_cpu_test` 只能跑「标量/多线程」路径（`test/CMakeLists.txt:52-56`），
  SIMD 分支在 CI 上一直没有验证。
- 复现/验证：可跑（GCC 上必然失败）：

  ```sh
  printf '#include "algebra/tensor/backend/tensor_backend_simd.h"\nint main(){}\n' |
    g++ -std=c++17 -I. -x c++ - -fsyntax-only
  ```

- 建议：在头文件顶部加 `#if !defined(_MSC_VER) || !defined(__AVX2__) #error/#else` 包裹，或把 SVML 依赖
  摘成可替换的 `xhqm::` 泛型回退，让非 MSVC 也能编过（哪怕慢）。

### BLAS 探测与符号写死 scipy-openblas32（Windows 库名 + `scipy_` 前缀 + 手工声明 LAPACKE）

- 位置：`CMakeLists.txt:166-168`、`backend/tensor_backend_cpu.cpp:9-27`
- 现状：CMake 只在 `depend/openblas` 下同时存在 `include/cblas.h` 与 `lib/libscipy_openblas.lib`
  时才定义 `TENSOR_WITH_BLAS`；源码把 `cblas_sgemm/dgemm/strsv/dtrsv` 宏改写成 `scipy_*`，
  并**手工声明**了两个 LAPACKE 原型（`scipy_LAPACKE_spotrf/dpotrf`），刻意不 include `<lapacke.h>`。
- 证据：`tensor_backend_cpu.cpp:13-26`（`OPENBLAS_STANDARD` 开关 + `extern "C"` 手工原型）；
  `CMakeLists.txt:166-168`（`.lib` 是 MSVC 命名）。
- 影响：① 非 Windows / 标准 OpenBLAS / MKL 用户即使装好 BLAS 也命中不了 `TENSOR_WITH_BLAS`（探测路径写死）；
  ② 想加第二个 LAPACK 调用（例如 `potrs`、`geqrf`）必须再手抄一份原型，且带 `scipy_` 前缀；
  ③ `libscipy_openblas.lib` 缺失时只打印 "Tensor CPU BLAS: not found"，构建照过，性能静默降级。
- 复现/验证：`cmake -DXHQM_BUILD_TENSOR=ON -DXHQM_DEVICE=CPU ..` 看 `Tensor CPU BLAS:` 一行；
  Linux 上必然走 "not found (fallback SIMD)"。
- 建议：用 `find_library` 探测通用 BLAS/LAPACK 并把 `scipy_` 前缀做成 CMake 传入的宏（`OPENBLAS_STANDARD` 已有雏形）。

### 4 个 `TENSOR_WITH_*` 宏在源码内没有任何默认值，非 CMake 构建静默退化成「并行桩」

- 位置：`backend/backend.h:7-11`（只把它们写在注释里）、`backend/tensor_backend.cpp:3-11`、
  `backend/tensor_backend_cpu.cpp:4,7`
- 现状：`grep -rn "#define TENSOR_WITH\|#define XHQM_TENSOR_CUDA\|#define XHQM_TENSOR_METAL" algebra/tensor/`
  为空——这些宏完全由 `CMakeLists.txt` 注入，源码不提供默认值（`XHQM_PARALLEL_THRESHOLD` 是唯一例外，
  在 `backend/tensor_backend.h:17-19` 有 `#ifndef` 兜底）。
- 证据：同上 grep 为空；`backend.h:7-11` 的注释暗示「宏已由 CMake 注入」。
- 影响：把 `algebra/tensor` 的头文件拿去手工编译/嵌进别的构建系统（例如只用 `cl.exe`/`g++` 命令行、
  或某天引入 Bazel）时，`tensor_backend.cpp:3-11` 的三个 `#if defined` 全部落空 → 只剩 no 桩，
  `tensor_backend_active().name()` 返回 "no"，而用户以为自己在跑 CPU/GPU 后端。
- 复现/验证：下面这条不带任何 `-D` 也能编过（说明宏不是编译必需，只是行为全变）：

  ```sh
  printf '#include "algebra/tensor/tensor.h"\nint main(){ return (int)xhqm::tensor_base::backend_compiled(xhqm::tensor_base::device::gpu); }\n' |
    g++ -std=c++17 -I. -x c++ - -fsyntax-only
  ```

- 建议：在 `backend/backend.h` 里给 `TENSOR_WITH_*` 加一组 `#ifndef` 的显式默认（或 `#error` 要求显式选择），
  让「没配宏」这件事看得见。

---

## D. 性能

### `parallel_for` 没有线程池，每次调用都创建并 join `hardware_concurrency()` 个线程

- 位置：`backend/tensor_backend_cpu.cpp:481-517`
- 现状：`unsigned threads = std::thread::hardware_concurrency();` 后当场 `pool.emplace_back(...)`，
  最后逐个 `join()`；函数返回即全部销毁。整个模块没有线程池/任务队列——同样的「现场起线程再 join」
  写法在 spmv（`cpu.cpp:139-147`、`171-179`）与 im2col GEMM（`cpu.cpp:260-265`、`445-450`）里各抄了一遍。
- 证据：`tensor_backend_cpu.cpp:488-516`（`std::vector<std::thread> pool; pool.reserve(threads);` → 循环创建 → join）。
- 影响：一个「逐元素 + 归约 + 逐元素」的算子链里每个步骤都付一次线程创建/销毁成本；
  在 8~16 核机器上，元素数量刚过阈值（4096）时线程创建开销完全可以吃掉并行收益——
  而阈值本身（4096）正是为了规避这件事，两条机制互相抵消。
- 复现/验证：跑 `test/tensor_cpu_test.cpp`（`TEST_CASE("test_cpu_backend_f32_kernels")` 等）时用
  `perf stat -e sched:sched_process_fork`/事件计数观察 fork/创建次数随算子调用次数线性增长。
- 建议：加一个模块级线程池（`hardware_concurrency()` 个常驻 worker + 条件变量），`parallel_for` 改为投递+等待。

### 全元素归约与 argmax/argmin 族仍是纯串行，未接后端 kernel

- 位置：`linalg.h:164-273`
- 现状：`sum_all`/`prod_all`/`max_all`/`min_all`（`linalg.h:166-195`）与
  `argmax_all`/`argmin_all`/`argmax`/`argmin`（`linalg.h:199-273`）都是单线程 for 循环，
  不调用 `tensor_backend_active()`。
- 证据：`linalg.h:164-165` 注释「全元素归约（串行桩，返回标量）／注：CPU 并行 = CPU 串行（可并行归约，暂未回补）」；
  `linalg.h:197-198` 同样的注；对照 `reduce_last`（`linalg.h:117-134`）是真的走 `backend.reduce`。
- 影响：`norm_fro`（`linalg.h:1013`）、`stats.h` 的 `var/stddev`（`stats.h:21,31,45`）、`vector::norm/normalize`
  （`vector.h:84,90`）都建立在 `sum_all` 上 → 整个「统计 + 范数」路径在 CPU 上是单线程，
  在 GPU 上更明显（显存里的张量被拉回主机逐元素扫）。
- 复现/验证：`grep -n "sum_all\|argmax_all" algebra/tensor/*.h` 可见调用方；跑大张量（1e8 元素）
  对比 `reduce_sum(t, 0)`（走 kernel）与 `sum_all(t)` 的耗时差。
- 建议：`sum_all` 等改为 `reduce_last` + 取标量，或在后端加一个 `reduce_all` 槽位。

### 标量就地运算绕过后端 kernel（`a *= 2.0f` 永远命不中 SIMD/GPU 的 elemwise）

- 位置：`tensor_base.h:244-279`
- 现状：`this_sum/this_sub/this_mul/this_div(const type&)` 四个标量版本都只调 `parallel_for` 逐元素算；
  只有「形状相同的 tensor ⊗ tensor」才会先试 `tensor_backend_active().elemwise(...)`（`tensor_base.h:215-220`）。
- 证据：`tensor_base.h:246-251`（标量 `this_sum` 直接 `parallel_for`）；对比 `tensor_base.h:218`。
- 影响：模型里大量出现的 `x * scale`、`x /= norm` 这类写法在 CPU 上无法命中 AVX2 内核、
  在 GPU 上完全留在主机（GPU 的 `parallel_for` 是主机串行，见 F 组）；同一件事用
  `x = x * tensor(...)` 才走加速路径，行为不一致且难发现。
- 复现/验证：`test/tensor_cpu_test.cpp` 里没有任何「标量就地运算命中后端」的用例
  （`grep -n "this_mul\|operator\*=" test/tensor_cpu_test.cpp` 为空），可与 `elemwise` 用例对比。
- 建议：给 `elemwise` 增加「标量广播」入口（或让标量版本走一个 1 元素张量的 elemwise 路径）。

### 排序型统计算子每次调用都整张量拷贝 + `std::sort`，且无沿轴版本

- 位置：`stats.h:145-149`（median）、`stats.h:156-158`（mode）、`stats.h:174-176`（prctile）、
  `stats.h:228-230`（unique）、`stats.h:268-272`（intersect）、`stats.h:289-292`（setunion）、
  `stats.h:304-308`（setdiff）
- 现状：每个函数都 `std::vector<type> s(n);` 拷贝全部元素再排序（`std::sort`，O(n log n)），
  `prctile/quantile/iqr` 各自独立排一次（`iqr` 排两遍，`stats.h:189`）。
- 证据：`stats.h:145-147` 与 `stats.h:188-189` `quantile → prctile`、`iqr → prctile(75) + prctile(25)`。
- 影响：训练循环里按 batch 求中位数/IQR 时，每步都在主机上做全量拷贝+两次全排序；
  `tensor(10^7)` 量级下会成为热点，而 GPU 上的张量还得先同步回主机。
- 复现/验证：`grep -n "std::sort" algebra/tensor/stats.h`（10 处）。
- 建议：加 `nth_element` 版本；`iqr` 复用一次排序结果；补沿轴的 `median(axis)`。

### CPU 上 f16 卷积没有 BLAS 路径，直接退化为朴素直接卷积

- 位置：`backend/tensor_backend_cpu.cpp:736-742`（BLAS 只认 f32/f64）、`conv.h:66-101`（回退实现）
- 现状：`conv3d` 仅 f32/f64 走 im2col + GEMM，其余类型返回 false，`conv_backend` 随即回退到
  `conv_nd` 的直接卷积（每个输出元素都要重解核坐标：`conv.h:83-98` 的双层解码）。
- 证据：`tensor_backend_cpu.cpp:737-742`；`conv.h:189` `return conv_nd<S, type>(...)`。
- 影响：f16 训练/推理在 CPU 上做卷积时性能断崖（既无 SIMD 整型 GEMM 路径，也无 f16 BLAS），
  而 CUDA/Metal 侧的 f16 是支持的（`tensor_backend_gpu.cu:1547` 有 `__half` 分支）；
  同一份代码在 CPU 与 GPU 上的耗时比会被这类类型差异拉得很大。
- 复现/验证：`grep -n "conv3d_im2col" algebra/tensor/backend/tensor_backend_cpu.cpp`（只有 `float`/`double` 实例化）。
- 建议：给 f16 走「转 f32 → BLAS → 转回 f16」的桥接（`simd.h:1041` 已有同思路的先例）。

### im2col 的 col 矩阵超过 16M 元素即静默回退直接卷积

- 位置：`backend/tensor_backend_cpu.cpp:64`
- 现状：`if (K * M > (std::size_t(1) << 24)) return false;  // >16M 元素回退`（阈值写死 2^24 = 16777216）。
- 证据：`tensor_backend_cpu.cpp:60-64`（`M = n*od*oh*ow`、`K = cin_g*kd*kh*kw`）；
  注释同处说明「col 过大回退直接卷积（conv_nd 桩）」。
- 影响：大 batch / 大特征图（例如 N=64、C=256、H=W=56 的 3×3 卷积）会静默从 GEMM 掉到
  逐元素直接卷积，且没有任何日志/计数器表明发生了回退——性能分析时容易误判为「BLAS 不生效」。
- 复现/验证：`grep -n "1) << 24" algebra/tensor/backend/tensor_backend_cpu.cpp`；
  用超过阈值的 shape 跑 `conv2d`，对比 `K*M` 刚好低于阈值时的耗时。
- 建议：把阈值提成常量并允许外部覆盖，回退时至少记一次计数（或暴露给调用方）。

---

## E. 未实现 / 占位桩

### `net` 设备没有实现类，被直接映射成 cpu / no

- 位置：`backend/tensor_backend.cpp:51-56`
- 现状：`case device::net:` 分支直接 `return create_cpu_backend();`（无 CPU 后端时 `create_no_backend()`），
  注释自述「分布式占位，回退 CPU」。`backend/` 目录下不存在任何 net 相关文件。
- 证据：`tensor_backend.cpp:51-56`；`ls algebra/tensor/backend/` 只有 no/cpu/gpu/metal/simd 五组文件；
  设备枚举本身有 `net = 3`（`hardware/hardware.h:40`）。
- 影响：`tensor_backend_select(device::net)`「成功」且 `name()` 返回 "cpu"，调用方无法从返回值/名称判断
  net 未生效；跨机分布式算子的任何假设都不成立。
- 复现/验证：`tensor_backend_select(device::net); tensor_backend_active().name()` → `"cpu"`。
- 建议：保持回退但让 `name()` 报告 "cpu(net-fallback)"，或让 `tensor_backend_select(net)` 明确失败。

### `no` 后端只实现 `parallel_for` + 内存 no-op，15 个算子全部继承基类 `false`

- 位置：`backend/tensor_backend_no.cpp:13-30`
- 现状：`no_backend` 里只有 `name/kind/supports_memory_transfer/parallel_for/upload/download/synchronize`；
  `elemwise`/`select`/`matmul`/`reduce`/`unary`/`transpose`/`grad_accum`/`spmv`/`pool2d`/`upsample2d`/
  `dropout`/`conv3d`/`conv4d`/`trsv`/`chol` 全部走 `tensor_backend.h` 里 `return false` 的默认实现。
- 证据：`tensor_backend_no.cpp:9-12` 注释「全部算子一律继承基类默认 false……只有一套实现，避免与 no 后端重复」；
  默认实现见 `tensor_backend.h:257,262,267,272,277,282,287,293,300,306,311,323,334,340,344`。
- 影响：这是**有意**的极简桩（见 F 组），但它意味着「no 后端」这条路径上没有任何独立实现被编译进来——
  `DEVICE=NO` 构建里所有算子语义都由模板层单独保证，等于少了一套交叉验证。
- 复现/验证：`test/tensor_serial_test.cpp`（`XHQM_DEVICE_CPU TENSOR_WITH_CPU` 时才有 CPU 后端；
  纯 no 路径见 `test/CMakeLists.txt:27-33`）。
- 建议：不改（有意取舍）；若要验证，可在 `tensor_serial_test.cpp` 里加一个「强制 no 后端」的用例。

### 未编 CUDA 时 `tensor_backend_gpu.cpp` 仍被编译成「全 false 桩」

- 位置：`backend/tensor_backend_gpu.cpp:52-247`（每个算子都有 `#else ... return false;`）；
  构建 `CMakeLists.txt:660-663`
- 现状：探测不到 CUDAToolkit 时 CMake 仍把 `tensor_backend_gpu.cpp` 加进目标（只是不定义 `XHQM_TENSOR_CUDA`），
  于是 `gpu_backend` 里 15 个算子都返回 false，`parallel_for` 走主机串行，`upload/download` 空实现，
  只有 `synchronize()` 里那句 `cudaDeviceSynchronize()` 被 `#ifdef` 掉。
- 证据：`tensor_backend_gpu.cpp:57-60`（`#else ... return false; // 未编 CUDA：回退 CPU`）等 15 处；
  `CMakeLists.txt:659-662` `else() target_sources(xhqm PRIVATE "algebra/tensor/backend/tensor_backend_gpu.cpp")
  message(STATUS "Tensor GPU backend: CUDA not found, stub fallback")`。
- 影响：装了「GPU 版」但没有 CUDA 工具的机器上，`device::gpu` 能选中，`name()` 返回 "gpu"，
  但每个算子都静默回退 CPU——profile 里表现为「GPU 没加速」，而不是「GPU 不可用」。
- 复现/验证：`tensor_backend_select(device::gpu); tensor_backend_active().name()` 与
  `tensor_backend_active().supports_memory_transfer()`（返回 true，但 `upload/download` 是 no-op）对照看。
- 建议：桩后端把 `name()` 改成 "gpu(stub)" 或让 `create_gpu_backend()` 在无 CUDA 时返回 nullptr，
  让 `create_backend` 的 `#elif defined(TENSOR_WITH_CPU)` 兜底逻辑（`tensor_backend.cpp:38-44`）真正生效。

### Metal 的 `chol` 只有 f32 一条内核，其余类型回退 CPU

- 位置：`backend/tensor_backend_metal.mm:1364-1368`（`if (idx != 1) return false;   // 仅 f32`）、
  内核 `tensor_backend_metal.mm:742`（`xhqm_chol_f32`）、注册 `tensor_backend_metal.mm:821`
- 现状：15 个算子里 14 个有 `SUFFIX` 宏展开的多类型内核，`chol` 只有 `f32` 一个变体，
  且 `run_chol` 显式拒绝其它 dtype。
- 证据：`metal.mm:742` 起的内核定义与 `:821` 的 `kChol = "xhqm_chol_f32"`；对比
  `metal.mm:155,181,219,...` 那批 `##SUFFIX` 模板化内核。
- 影响：Metal 路径上 f16 模型遇 `chol`（例如高斯过程、某些归一化层）会掉回 CPU，
  与 README 旧文「全部 15 个算子（f16/f32）」的印象不符——f16 的 chol 从未实现。
- 复现/验证：`grep -n "xhqm_chol" algebra/tensor/backend/tensor_backend_metal.mm`（只有 f32 一处）。
- 建议：补 `xhqm_chol_f16`（或把 f16 桥接成 f32 再转回，同 `simd.h:1041` 的做法）。

### `conv1d` / `conv3d` 的反向传播没有 im2col，只有直连多重循环

- 位置：`conv_grad.h:128-190`（conv1d/conv3d 的 grad_weight / grad_input）；对照
  `conv_grad.h:65-111`（conv2d 反向，已用 `im2col2d` + 后端 `matmul`）
- 现状：文件头注释自述「串行直连，正确性优先（groups=1）……后续可下沉 im2col/col2im 加速」
  （`conv_grad.h:4-5`）；只有 conv2d 的 `grad_weight`/`grad_input` 走 `matmul`（`conv_grad.h:78,108`）。
- 证据：`conv_grad.h:128-145`（1d 权重梯度是五重循环）、`conv_grad.h:180-190+`（3d 同构）；
  `conv_grad.h:78` `tensor<2, type> gwf = matmul(transpose(col, 0, 1), dyf);`。
- 影响：1D/3D 卷积网络的反向是串行朴素实现，训练时前向走 GEMM、反向掉两三个数量级，
  且没有任何「未加速」提示；`groups` 也不支持（只能 1）。
- 复现/验证：`grep -n "im2col\|matmul" algebra/tensor/conv_grad.h` —— conv2d 之外无命中。
- 建议：把 `im2col2d` 泛化到 1d/3d（或至少按 conv2d 的写法补 3d），并给反向加 groups 参数。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### 形状/维度不匹配一律静默返回空或零，不抛异常

- 位置：`linalg.h:39-40`（matmul 内维不符 → 空张量）、`tensor_base.h:91`（不可广播 → 空张量）、
  `matrix.h:79`（矩阵乘向量维度不符 → 空 vector）、`vector.h:64`（点积长度不符 → `type(0)`）、
  `sparse.h:73,87`（稀疏乘维度不符 → 空）、`pool.h:70,142,218`（avg 池化窗口全越界 → 0）
- 现状：数值算子层不抛异常，用「空/零」表示非法形状；只有 `nn.h`（where/gather/scatter_add/dropout）、
  `norm.h`（layer_norm/rms_norm/group_norm/batch_norm）、`tensor_view`（transpose/slice/broadcast/reshape）、
  `conv.h:35-36` 抛 `xhqm::error`。
- 理由：模板层要兼容 `f16/整型/bool/复数/对偶数` 等无异常语义的值类型（`figure<bool>` 就是 GF(2) 环），
  且 `tensor` 的算子被设计成「纯函数、可并行、无副作用」，返回值即错误信号，避免在数值内核里插入
  可能打断 `parallel_for` 的抛出路径（`cpu_backend::parallel_for` 里线程体里抛出会 `std::terminate`，
  见 `tensor_backend_cpu.cpp:509-513`）；MATLAB 风格的空返回也是本仓一贯约定。
- 代价：错误被推迟到下游，`auto C = matmul(A,B)` 拿到空张量后往往在几十行之后才以「结果全 0/崩」暴露，
  且 `matmul` 与 `where` 两套风格并存，接手者必须先记住哪些函数抛、哪些不抛；调试成本高于异常成本。
- 结论：保留（与 GPU/并行语义一致性 > 早失败）。

### 稀疏只做 COO，且「稀疏 × 稀疏」用 `std::map` 归并

- 位置：`sparse.h:4-9`（格式声明）、`sparse.h:96-114`（`operator*(sparse_matrix)`）
- 现状：`sparse_matrix` 只有 COO 三元组（`m_row/m_col/m_val`，`sparse.h:20-22`），没有 CSR/CSC/ELL；
  稀疏乘稀疏用 `std::map<pair<index,index>, type>` 累加后过滤 `|v| > preci<type>`。
- 理由：COO 是唯一能同时覆盖「从稠密提取」「与稠密向量/矩阵相乘」「转置」「转回稠密」四种用途的最小格式，
  且能被后端 `spmv` 直接消费（`tensor_backend.h:289-293` 的接口就是按 COO 三元组定义的）；
  `std::map` 版本天然处理重复索引的合并，正确性优先。
- 代价：没有行压缩格式 → 无法做 CSR 级 SpMV（`cpu.cpp:133` 用的是 `Y[row[k]]` 随机写，
  并行时必须每线程一份稠密部分和，见 B 组第 1 条）；矩阵链式乘法 O(nnz²·log) 且依赖 RNG 顺序；
  大图/大稀疏矩阵（rows 亿级）内存与时间都不可接受。
- 结论：保留（接口面已按 COO 定死，换格式要动后端虚接口）。

### `no` 后端不做任何算子实现，语义只在模板层保证

- 位置：`backend/tensor_backend_no.cpp:9-12`
- 现状：见 E 组同名条目。
- 理由：注释写明「串行标量语义统一走上层模板的 `xhqm::` 泛型回退……只有一套实现，避免与 no 后端重复」——
  若 no 后端也实现一遍 15 个算子，同一语义就有两份代码，模板改动会漏改后端（这正是 Metal/CUDA
  各自实现一份带来的维护面，参见「后端虚接口」相关讨论）。
- 代价：no 路径没有任何「独立的第二实现」可做交叉验证；`DEVICE=NO` 构建的测试覆盖面 = 模板层覆盖率，
  一旦模板层的某个 `xhqm::` 泛型函数写错，no 与 cpu 会同时错（没有互检）。
- 结论：保留。

### GPU/Metal 的 `parallel_for` 回退主机串行 —— 没下沉到原语的算子拿不到 GPU

- 位置：`backend/tensor_backend_gpu.cpp:43-50`、`backend/tensor_backend_metal.mm:1395-1400`
- 现状：两个 GPU 后端的 `parallel_for` 都是 `for (i = begin; i < end; ++i) body(i);` 的裸循环，
  注释说明「GPU 无法执行 `std::function` 闭包，并行算力走 elemwise/matmul 原语」。
- 理由：`parallel_for` 的签名接受 `std::function<void(size_t)>`（`tensor_backend.h:250-251`），
  主机闭包里可以捕获任意 C++ 对象、调用 `xhqm::` 泛型（含复数/对偶数），无法翻译到设备；
  强行支持只能靠内核编译/序列化，成本远超收益。所以 15 个原语槽位是 GPU 加速的**唯一入口**。
- 代价：凡没下沉到原语的算子（`stats.h` 全部、`signal.h` 除三个卷积外全部、`poly.h`、`shape.h`、
  `linalg.h` 的全元素归约族、`conv.h` 的回退路径）在 GPU 上仍是主机单线程跑，并且**数据要来回搬**；
  用户看到 `device::gpu` 生效就以为整条链都加速了。
- 结论：保留（语义正确性优先），但 README 已注明哪些路径不下沉。

### `store<bool>` 用 `figure<bool>` 存连续内存，并 `reinterpret_cast<bool*>` 暴露 `data()`

- 位置：`store.h:39-63`（关键行 `store.h:61-62`）、`figure.h:116-118`
- 现状：`std::vector<figure<bool>>` 内部存储，`data()` 返回 `reinterpret_cast<bool*>(m_values.data())`，
  后端按 `dtype::boolean` 经 `unsigned char` 访问（`backend/tensor_backend_cpu.cpp:160,583`）。
- 理由：`std::vector<bool>` 是位压缩代理（无 `data()`、不能给后端裸指针），`std::deque<bool>` 内存不连续，
  两者都不能满足 `elemwise`/`transpose` 按元素字节拷贝（`tensor_backend_cpu.cpp:667-676`）的要求；
  `figure<bool>` 只含一个 `bool` 成员（`figure.h:116-118`，`sizeof == 1`、无填充），
  且后端只用字符类型（`unsigned char`）别名访问，规避了 `bool*` 别名的严格别名争议。
- 代价：正确性依赖「`figure<bool>` 布局 == `bool`」这一未由语言保证的事实（若将来给 `figure<bool>`
  加成员/虚函数即静默失效）；`reinterpret_cast<bool*>` 本身仍是灰色写法，换编译器/开 UBSan 时需要复核。
- 结论：保留（有注释说明前提），但任何人改 `figure<bool>` 的布局必须先看 `store.h:59-62`。

### `net` 用 cpu 顶替，保留枚举但实际语义不同

- 位置：`backend/tensor_backend.cpp:51-56`
- 现状：见 E 组首条；`device::net` 合法、可选中，落到 cpu 后端。
- 理由：设备枚举与运行时选择在 `hardware/hardware.h` 里是跨模块共享的（渲染/张量/未来分布式都读它），
  删掉 `net` 会破坏 `XHQM_DEVICE=NET` 的构建分支（`CMakeLists.txt:110-112` 定义 `XHQM_DEVICE_NET`）
  与 `device_compiled(net)` 的既有语义；分布式后端未实现时「显式指定 net 就退回 cpu」比「拒绝切换」更容易
  在既有调用点落地。
- 代价：`tensor_backend_active().name()` 无法区分「真的在 cpu」和「想要 net 但被顶替」，
  自动化脚本/日志据此判断会得到错误结论（profile、性能回归对比尤其容易误判）。
- 结论：保留，但建议至少让名称带上回退痕迹（见 E 组首条的「建议」）。

---

## G. 文档与代码的出入

### `tensor_backend_metal.h` 说「仅 f32 走 Metal kernel」，实际是 f16/f32/i8/i16/i32

- 位置：`backend/tensor_backend_metal.h:13`、`backend/tensor_backend_metal.mm:885-899`
- 现状：头注释写 `// 仅 f32 走 Metal kernel；f64 等其余类型返回 false，由调用方回退 CPU。`；
  实际 `metal_dtype()` 把 f16/f32/i8/i16/i32 依次映射为 0..4，只有 f64/bool/unknown 落 `default: return -1`。
- 证据：`tensor_backend_metal.h:13` vs `tensor_backend_metal.mm:886-898`。
- 影响：读头文件的人会以为 Metal 只能跑 f32 而放弃 f16 模型；反向也危险——以为「只有 f32」所以在
  f16 路径上跳过 GPU 检查。README 的旧表述（f16/f32）已按实测订正，但源码注释未改（本轮不许改源码）。
- 复现/验证：`grep -n "metal_dtype" -A14 algebra/tensor/backend/tensor_backend_metal.mm | head -20`。
- 建议：把该注释改为「支持 f16/f32/i8/i16/i32；f64/bool 返回 false 回退 CPU；chol 仅 f32」。

### `tensor_backend_no.h` 注释里的设备名 `device::none` 不存在

- 位置：`backend/tensor_backend_no.h:10`
- 现状：注释「工厂：创建串行桩后端（单例；`device::none`，兜底与显式选择均可用）」，
  枚举实际成员是 `no`（`hardware/hardware.h:37`：`no = 0`），`none` 这个名字在仓里不存在。
- 证据：`tensor_backend_no.h:10` vs `hardware/hardware.h:35-42`；实现里用的是 `device::no`
  （`tensor_backend_no.cpp:17`）。
- 影响：低（纯注释），但这是**后端抽象层**的头文件，接手者按 `device::none` 搜索会零命中。
- 复现/验证：`grep -rn "device::none" algebra/tensor/`（只有这一处注释）。
- 建议：改成 `device::no`。

### `tensor_backend.h` 的 `elemwise` 注释只列了 4 个 op，枚举其实有 9 个（且 SIMD 真实现了 9 个）

- 位置：`backend/tensor_backend.h:253`（注释）、`backend/tensor_backend.h:112-123`（`elemwise_op`）
- 现状：接口注释写「`dst[i] = a[i] OP b[i]`（op: 0=+ 1=- 2=* 3=/）」，而枚举是
  `elem_add/sub/mul/div/atan2/hypot/pow/mod/rem` 共 9 项；SIMD 内核确实为 4..8 写了实现。
- 证据：`tensor_backend.h:253` vs `:112-123`；`backend/tensor_backend_simd.h:72-77`
  （`elem_atan2`/`elem_hypot`/`elem_pow`/`elem_mod`/`elem_rem` 分支）。
- 影响：想用 `pow`/`hypot` 的调用方会以为后端不支持而绕开 kernel；也只从注释看的话，
  会以为补 op 只需改 0..3 的分派。
- 复现/验证：`grep -n "elem_atan2\|elem_pow" algebra/tensor/backend/tensor_backend_simd.h`。
- 建议：注释改为「见 `elemwise_op`（9 项）；CPU SIMD 覆盖全部 9 项」并同步 `elemwise_eval` 说明。

### `tensor_backend_simd.h` 声称「其余平台回退标量桩」，文件里没有这段平台分支

- 位置：`backend/tensor_backend_simd.h:343`（注释）、`backend/tensor_backend_simd.h:8`（唯一的条件包含）
- 现状：注释说「一元逐元素（SIMD；超越函数经 SVML，仅在 MSVC 下编译，其余平台回退标量桩）」，
  但该文件全文只有 include 守卫一个 `#if`，没有任何 `_MSC_VER` 判断；「回退标量桩」实际发生在
  **调用方**（`backend/tensor_backend_cpu.cpp:612-634` 的 `switch(dt)` 标量实现）和 include 层
  （`tensor_backend_cpu.cpp:4-6`）。
- 证据：`tensor_backend_simd.h:343`；`grep -c "MSC_VER" backend/tensor_backend_simd.h` → 0。
- 影响：与 C 组第 1 条同源（非 MSVC 直接编不过）；注释把「调用方回退」写成了「本文件回退」，
  排查编译错误时会先去改这个头。
- 复现/验证：`grep -n "MSC_VER\|__AVX2__\|#if" algebra/tensor/backend/tensor_backend_simd.h`。
- 建议：注释改为「仅在 MSVC(MSVC+AVX2) 下被 `TENSOR_WITH_SIMD` 引入；非 MSVC 由 cpu.cpp 走标量路径」，
  或直接加真正的 `#if` 守卫。

### `stats.h` 头注释只列 3 个算子，文件里实际有 20 多个

- 位置：`stats.h:4`；实际实现 `stats.h:15-320`
- 现状：头注释「统计算子（串行桩）：var（方差）/ stddev（标准差）/ cov（协方差矩阵）。」
  之外还有 `diff`/`gradient`/`median`/`mode`/`prctile`/`quantile`/`iqr`/`nansum`/`nanmean`/`nanmax`/`nanmin`/
  `ismember`/`unique`/`sortrows`/`intersect`/`setunion`/`setdiff`（共 17 个未列）。
- 证据：`stats.h:4` vs `stats.h:95,126,140,151,169,186,188,192,199,206,214,258,224,240,264,285,300`。
- 影响：低，但会让「这模块只有方差/协方差」的判断流传（README 的旧算子表也漏了 `ismember`，本轮已补）。
- 复现/验证：`grep -n "^		\(template\|type\|xhqm::tensor\)" algebra/tensor/stats.h`.
- 建议：头注释改为「统计与集合算子（串行桩）：var/stddev/cov + 差分/梯度/分位数/NaN 归约/集合运算」。

### README 的旧说法（2026-10-07 已订正，作废标注保留在 README 内）

- 位置：`README.md`（本轮修改处均已用 ~~删除线~~ + **已作废（2026-10-07 复核 r677）** 或 **订正** 标注）
- 现状与证据（逐条，供接手者对照）：
  1. `#include "xhqm/algebra/tensor/tensor.h"` —— 该路径不存在（`xhqm/` 下是无扩展名伞头文件，
     `xhqm/algebra` 是文件不是目录）；真实写法 `#include "algebra/tensor/tensor.h"`，
     测试与 soul 都是这个相对路径（`test/tensor_cpu_test.cpp:4`、`algebra/soul/linear.h:5`）。
  2. 用法示例 `matrix<f32> M({2, 2}, 1.0f)` —— 无匹配构造函数：`matrix` 只有
     `(rows, cols, v=0)`、initializer_list、`explicit matrix(const tensor<2>&)` 三种（`matrix.h:24,26,43`）。
     已实测编译失败（`g++ -std=c++17 -I. -fsyntax-only`），改为 `matrix<f32> M(2, 2, 1.0f)`。
  3. 「`tensor_backend` 纯虚接口 + none/cpu/gpu/net 四实现」—— 只有 3 个实现类，
     net 无实现（`backend/` 目录清单 + `tensor_backend.cpp:51-56`）。
  4. 「计算层……全部经后端并行调度」—— `stats.h`/`poly.h`/`shape.h` 零调用，
     `signal.h` 仅 3 处（`signal.h:195,217,242`）。
  5. 「`figure<type>` f16/f32/f64/bool 特化」—— 只有 `figure<f16>`、`figure<bool>` 是特化（`figure.h:64,116`）。
  6. 「Metal 提供全部 15 个算子的 kernel（f16/f32）」—— 15 个算子确实都有，但类型是 f16/f32/i8/i16/i32
     （`tensor_backend_metal.mm:885-899`），且 `chol` 只有 f32（`metal.mm:1364-1368`，见 E 组）。
  7. 「设备枚举……与 `parallel::equipment`、`device::type` 语义一致、按数值映射」——
     `equipment` 只在 `hardware/README.md` 里有；`xhqm::device::type` 是 8 值枚举、第 5 项
     `device_memory = 4` 与 `auto_ = 4` 撞车（`head.h:145-154`）。
  8. 「稀疏只有 COO」—— **属实**（保留，未标注作废）：`sparse.h:4-22` 只有三元组存储。
  9. 「缺单测/基准目录」—— **一半作废**：单测**有**（`test/tensor_serial_test.cpp` 568 行 12 个 `TEST_CASE`、
     `test/tensor_cpu_test.cpp` 711 行 24 个、另有 `algebra_test.cpp`/`gpu_smoke.cpp`）；
     基准目录**没有**（全仓 `find -iname "*bench*"` 为空）。README 已按此分别写明。
  10. 「单文件偏大」—— **属实**：实测 `wc -l`：`tensor_backend_gpu.cu` 1710、
      `tensor_backend_metal.mm` 1525、`tensor_backend_simd.h` 1475、`linalg.h` 1238、
      `tensor_backend_cpu.cpp` 864（36 个源码文件合计 11417 行，不含 README.md/DEBUG.md）。README 已补实测表格。
  11. 「后端虚接口签名碎片化（新增算子要四处同步）」—— **属实且更严重**：新增一个算子要同步
      `backend/tensor_backend.h`（接口+文档默认 false）、`tensor_backend_cpu.cpp`（可选实现）、
      `tensor_backend_gpu.cpp`（`extern "C"` 声明 + 15 处 wrapper 之一）、`tensor_backend_gpu.cu`
      （launcher + kernel）、`tensor_backend_metal.mm`（MSL 内核 + `run_*` + `override`），
      外加 README/DEBUG 与 `CMakeLists.txt` 的源文件清单，即实际是 5~7 处；
      证据：`dropout` 一个算子就在 `tensor_backend.h:310`、`tensor_backend_gpu.cpp:9,99`、
      `tensor_backend_gpu.cu:151,387,1585`、`tensor_backend_metal.mm:1466` 各出现一次。
- 影响：这 11 条正是接手者最容易复读的旧结论；README 现已带作废标注，源码注释侧的 G1~G4 四条仍待改（本轮铁律禁止改源码）。
- 复现/验证：对照 `README.md` 中的 `已作废`/`订正` 标注逐条 `grep`。
- 建议：源码侧只需改 G1/G2/G3/G4 这四条注释，其余保持现状。
