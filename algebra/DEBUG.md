# algebra DEBUG —— 当前仍存在的问题

`algebra` 是数学地基（`value.h` → `mathf.h` → `itype.h`/`ftype.h` → `ctype.h`）加四个子模块（`tensor` / `analysis` / `geometry` / `soul`）的总览级模块；本文件只收**当前仍存在**的问题，子模块各自的 `README.md` 仍是子模块 API 的权威说明。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。

与同类文件的关系：本仓已有的历史台账 `fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` 按既定约定不搬家、不改名，本文与它们无交集（不同模块）。**`DEBUG.md` 这一命名是 2026-10-07 本次全仓复核才引入的**：根 `DEBUG.md`、`hardware/DEBUG.md`、`lattice/DEBUG.md` 与本模块四个子模块的台账（`tensor/DEBUG.md`、`analysis/DEBUG.md`、`geometry/DEBUG.md`、`geometry/brep/DEBUG.md`、`soul/DEBUG.md`）都在同一批建立，彼此约定「同一件事不重复记」：**子模块范围内的细条目以子模块台账为准**（例如 SIMD 平台守卫见 `tensor/DEBUG.md` C 组、`net` 无实现见其 E 组、`eigs/svds` 见 `analysis/DEBUG.md` E1），本文只保留总览级、基础层与跨模块条目，并在相关条目里指过去；库层面条目（如 `value.h` 常数）根 `DEBUG.md` A1 也记了一份，本文从模块视角保留行号证据。本模块 README 里被推翻的旧说法已就地标成 ~~划掉~~ + **已作废（2026-10-07 复核 r677）**，本文只给出现状与证据，不复述历史。

核对方式：所有条目都是读源码（`read`/`grep`）得到的事实，带实测行号；没有编译运行过任何用例 —— 凡标「未复现」的都是**未在本机运行验证**的条目，只给出可直接跑的最小复现/验证命令。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `isnan(f16)` 恒为 false —— 半精度的 NaN 判定完全失效
- 位置：`algebra/ftype.h:148`（辅：`ftype.h:15-17` 位域、`ftype.h:27-29` 只有 float 构造与 float 转换、`ftype.h:53-58` `nan()` 的位模式）
- 现状：`template<> inline bool isnan(const f16& value) { return (value == (f16()).nan(0) || value == (f16()).nan(1)); }`。`float_16` 没有 `operator==`，两侧只能隐式转成 `float` 再按 IEEE 比较，而 NaN 与任何值（含 NaN）比较都是 false。
- 证据：`ftype.h:116-127` 的全部比较/算术特化都写成 `left > right`、`left == right`，即依赖 `operator float()`；`ftype.h:14-59` 里没有任何位级比较或运算符重载。
- 影响：所有 `xhqm::isnan(f16)` 都返回 false，包括刚被 `nan(h)` 置成 NaN 的值；`isinf(f16)` 反而正确（inf 与自身相等），所以这个错误是单向的、很难被自检发现。用 f16 存 NaN 做缺失值判定的调用方会静默走错分支。
- 复现/验证：未复现。最小验证：`f16 h; xhqm::nan<f16>(h); assert(xhqm::isnan(h));`（当前失败）。现有测试没覆盖：`test/algebra_test.cpp:41-47` 只测了转换与 `max/ismax`。
- 建议：改成按位判断 `return (value.exponent == 0x1F && value.fraction != 0);`。

### A2. `arr`/`tup` 拷贝让嵌套元素共享内层堆块，析构时二次释放
- 位置：`algebra/ctype.h:85-90`（arr 拷贝构造）、`ctype.h:98-103`（arr 拷贝赋值）、`ctype.h:183-192`、`ctype.h:194-203`（tup 同）；存储层 `metaclass/block.h:68-72`（`operator=` = `init` + 裸 `memcpy`）
- 现状：`m_block = _copy.m_block` 是逐字节复制整块内存。当元素类型是 `array`/`tuple` 时，块里放的是 placement new 出来的 `xhqm::arr`/`xhqm::tup`（`ctype.cpp:183`、`ctype.cpp:190`、`ctype.cpp:248`、`ctype.cpp:252`），这些内层对象各自带一个 `xhqm::block`，其 `m_data` 是指向独立堆块的裸指针。
- 证据：`block` 只有 `m_data`/`m_size` 两个标量成员（`block.h:12-13`），拷贝就是 `std::memcpy`（`block.h:18`、`block.h:71`），没有深拷贝也没有引用计数；析构则递归调用内层析构后 `free`（`ctype.cpp:197-220`、`ctype.cpp:260-281`）。
- 影响：`array<...,array<...>>` / `tuple<...,array<...>>` 形态的 arr/tup 只要被拷贝一次（按值传参、放进 `std::vector` 触发扩容、赋值）就会对同一内层块 `free` 两次 —— 崩溃或堆破坏。
- 复现/验证：未复现（只读码，未编译运行）。最小复现：`xhqm::arr a("array<2,array<3,real_32>>"); xhqm::arr b = a;`（两个对象析构即触发）。现有测试只构造平坦数组：`test/algebra_test.cpp:198-203`。
- 建议：拷贝构造/赋值按元素类型逐元素深拷贝（与 `ctype.cpp:197-220` 的析构对称），或给 block 加引用计数。

### A3. `arr`/`tup` 的 `value`/`value_as` 无边界检查，越界即 UB
- 位置：`algebra/ctype.h:119-128`（arr::value，偏移 `index * sizeof(type)`）、`ctype.h:130-153`（arr::value_as）、`ctype.h:205-220`（tup::value，前缀累加偏移）、`ctype.h:222-245`（tup::value_as，`m_tuple[index]` 直接下标）
- 现状：没有 `index < size()` 判断，直接对裸指针解引用；`tup::value` 甚至先对 `m_tuple` 做一次越界读。
- 证据：`ctype.h:122`、`ctype.h:211`、`ctype.h:226` 分别是 `m_block.value_ptr<type>(index * sizeof(type))`、`m_block.value_ptr<type>(offset)`、`m_tuple[index]`；`block::value_ptr` 只做 `(type*)(m_data + offset)`（`block.h:82-91`）。
- 影响：按类型串新建容器后按字段下标取值（序列化/检视路径）只要 index 算错一次就读到堆块外：脏数据或段错误，而且 `good()` 不会变 false，调用方拿不到失败信号。
- 复现/验证：未复现。最小复现：`xhqm::tup t("tuple<real_32,uint_8>"); t.value_as<f64>(7);`
- 建议：两个类型的 `value`/`value_as` 入口加 `if (index >= size()) throw xhqm::error("...")`（`arr`/`tup` 都已提供 `size()`）。

### A4. 泛型 `min/max/nan/inf`（写引用）静默置 0，`ismin/ismax` 恒 false
- 位置：`algebra/mathf.h:38-43`
- 现状：`template<typename type> inline type min(type& value) { value = type(); return value; }`，`max/nan/inf` 同形；泛型 `ismin/ismax` 直接 `return false;`。真正的特化只有 `u8..i64`（`itype.h:57-127`）与 `f16/f32/f64/fld`（`ftype.h:140-145`、`ftype.h:154-179`）。
- 证据：`mathf.h:38-41` 四行都是 `value = type();`；`mathf.h:42-43` 两行 `return false;`。复数另有一套自己的实现（`tensor/complex.hpp:43-48`），对偶数/四元数没有 —— 它们调用二参 `xhqm::min(a,b)` 会因缺 `operator<` 直接编译失败，而一参写引用版会老实写 0。
- 影响：对偶数/四元数/自定义数值类型执行 `xhqm::min(x)` 得到 0（而不是最小值），`ismin(x)` 恒 false；不报错、静默给错值。本模块 README 里「置为极值/NaN/Inf」的说法只对特化类型成立。
- 复现/验证：未复现。验证：`complex<f64> z(1,2); xhqm::min(z); // z 变成 (0,0)`；同理 `dualnum<f64> d(1,2); xhqm::min(d);` 归 0。
- 建议：泛型版改为 `static_assert`/`= delete` 拒绝，别拿 `type()` 当「最小值」兜底。

### A5. `read<u8..i64>` 越界不报错，静默截断
- 位置：`algebra/itype.h:63`（u8）、`:72`（u16）、`:81`（u32）、`:90`（u64）、`:99`（i8）、`:108`（i16）、`:117`（i32）、`:126`（i64）
- 现状：先按宽类型解析（`std::stoul/stoull/stoi/stoll`）再 `static_cast` 窄化，没有任何范围判断。
- 证据：`itype.h:63` = `return static_cast<u8>(std::stoul(value));`；`itype.h:99` = `return static_cast<i8>(std::stoi(value));`。
- 影响：反序列化不可信输入时静默得到错值（`read<u8>("300")` → 44、`read<i8>("200")` → -56），错误直接传播到计算里，没有任何信号。
- 复现/验证：未复现。验证：`assert(xhqm::read<u8>("300") == 44);`（当前成立，即缺陷成立）。
- 建议：解析后与 `uXmax`/`iXmax` 比较，越界抛 `xhqm::error`。

### A6. 类型串非法时用裸 `throw;` → `std::terminate`
- 位置：`algebra/ctype.cpp:157-158`（arr：字段数 < 3、首段是 tuple）、`algebra/ctype.cpp:226-228`（tup：字段数 < 2、首段是 array/tuple）
- 现状：没有活动异常时执行 `throw;`，标准要求调用 `std::terminate()`（abort）。
- 证据：`ctype.cpp:157` = `if (count < 3)	throw; //类型错误`；`ctype.cpp:226` = `if (count < 2)	throw; //类型错误`。
- 影响：`xhqm::arr("array<2>")`、`xhqm::tup("uint_8")` 之类直接终止进程，`try/catch` 拦不住；对读外部字符串的调用方（反序列化）是可被触发的崩溃面。
- 复现/验证：未复现。最小复现：`xhqm::arr a("array<2>");`
- 建议：改成 `throw xhqm::error("arr: 类型串非法")`（`head.h:240-243` 已有 `xhqm::error`）。

### A7. `ftype.cpp` 的 `_float` 构造函数自递归（一旦被调用即栈溢出）
- 位置：`algebra/ftype.cpp:11`
- 现状：`inline _float(const float& value = 0.f) { (*this) = (_float)value; }` —— `(_float)value` 又要调用同一个构造函数，没有终止条件。
- 证据：`grep -rn '_float' algebra/` 只命中 `ftype.cpp:5`（定义）、`ftype.cpp:11`（构造）、`ftype.cpp:17`（`(_float*)&fValue` 重新解释）与 CUDA 的 `__float2half`；构造函数从未被调用，所以今天不会崩。
- 影响：它是 `xhqm::` 下可见的类型，`xhqm::_float f(1.f);` 或任何拷贝构造都会无限递归崩溃；同时误导后来者把它当成可用的 float 位视图（`_float` 里的位域与 f16 转换是配套的）。
- 复现/验证：未复现（会崩，未运行）。最小复现：`xhqm::_float f(1.f);`
- 建议：删掉该构造函数（保留位域 + `operator float()`），真需要构造时用 `memcpy(this, &value, sizeof(value))`。

### A8. 复数 `min/max` 返回的是「模」而不是元素，虚部被丢掉
- 位置：`algebra/tensor/complex.hpp:21-22`
- 现状：`min(a,b)` = `std::min(std::abs(a), std::abs(b))`：`std::abs` 返回实数，再隐式转成 `complex<type>(m, 0)`。
- 证据：`complex.hpp:21-22` 两行正文；比较语义在 `complex.hpp:28-29`（`most/less` 按模比较）。
- 影响：`xhqm::max(complex(2,0), complex(0,3))` 得到 `(3,0)`，既不是 a 也不是 b —— 「挑最大值」的代码会静默错值（相位/虚部全丢）。
- 复现/验证：未复现。验证：`complex<f64> r = xhqm::max(complex<f64>(2,0), complex<f64>(0,3)); // r == (3,0)`
- 建议：返回 `most(a,b) ? a : b`，或把函数改名成 `max_module` 明确语义。

### A9. `tensor_backend_select()` 与 `hardware::active_device()` 状态不同步
- 位置：`algebra/tensor/backend/tensor_backend.cpp:70-76`（select 只写自己的静态指针）、`tensor_backend.cpp:19-22`（`active_ptr()`）、`algebra/tensor/backend/backend.h:29-31`（`active_backend()`/`active_backend(kind)` 读写的是 `hardware::active_device_ref()`）、`hardware/hardware.h:102-108`
- 现状：切后端只改张量侧那份 `active` 指针；`tensor_base::active_backend()` 仍然返回 `hardware::compiled_device()` 推导出的值。
- 证据：`tensor_backend.cpp:72-74`（`create_backend` + `active_ptr() = target`）与 `backend.h:29-31`（`xhqm::hardware::active_device_ref()`）是两处互不相干的状态。
- 影响：任何按 `active_backend()` 判断「现在跑在哪个设备上」的代码（诊断日志、是否走设备内存搬运）都可能与实际执行算子的后端相反；反向调用 `active_backend(kind)` 也不会改变 `tensor_backend_active()` 用的对象。默认 CPU 构建下 `select(device::no)` 之后 `active_backend()` 仍是 `cpu`。
- 复现/验证：未复现。验证：`xhqm::tensor_base::tensor_backend_select(xhqm::hardware::device::no); assert(xhqm::tensor_base::active_backend() == xhqm::hardware::device::no);`（失败即缺陷成立）
- 建议：select 里同时写 `hardware::active_device(kind)`，或让 `active_backend*` 直接读张量侧状态。（同一件事的子模块版本：`tensor/DEBUG.md` A 组「两套后端切换入口语义分叉：`active_backend(kind)` 不会切换已建好的后端实例」，那里更细。）

## B. 资源与并发

### B1. `arr`/`tup` 拷贝构造标了 `noexcept`，分配失败只能崩
- 位置：`algebra/ctype.h:85-90`、`ctype.h:183-188`（`noexcept`）；`metaclass/block.h:15-19`（拷贝构造 = `init` + `memcpy`）、`block.h:36-49`（`init` 不检查 `std::malloc` 的返回值）
- 现状：`init(size)` 在 malloc 失败时 `m_data` 仍是 `nullptr`，紧接着 `std::memcpy(m_data, _copy.m_data, m_size)` 往空指针写。
- 证据：`block.h:44-48` = `if (size > 0) { m_size = size; m_data = (xhqm::byte*)std::malloc(size); }` 无失败分支；`block.h:17-18` 立刻 memcpy。
- 影响：大容器拷贝 / 内存紧张时得到段错误而不是 `std::bad_alloc`；`noexcept` 又保证异常无法外传，失败路径只能终止进程。
- 复现/验证：未复现。验证思路：限制地址空间（`ulimit -v`）后拷贝一个大 `arr`，或直接给 `block::init` 注入失败。
- 建议：`init` 检查 malloc 结果并在失败时抛 `xhqm::error`，同时去掉两个拷贝构造的 `noexcept`。

### B2. `arr`/`tup` 自赋值是 use-after-free
- 位置：`algebra/ctype.h:98-103`（`m_block = _copy.m_block`）、`ctype.h:194-203`（tup 同）；`metaclass/block.h:68-72`
- 现状：`block::operator=` 先 `init(_copy.m_size)` —— 这会 `free` 掉自己的 `m_data`；当 `&_copy == this` 时源指针刚被释放，随后 `memcpy` 读的是已释放内存。
- 证据：`block.h:70` = `this->init(_copy.m_size);`、`block.h:71` = `std::memcpy(m_data, _copy.m_data, m_size);`，中间没有自赋值判断；`ctype.h:98`、`ctype.h:194` 也没有。
- 影响：`a = a;`（或经 `std::swap`、容器内部的自赋值路径）读到脏数据或崩溃；`arr`/`tup` 的赋值运算符看似普通赋值，实际不是安全的。
- 复现/验证：未复现。最小复现：`xhqm::arr a("array<2,2,real_32>"); a = a;`
- 建议：`if (this == &_copy) return;`（两个类型各一处），或在 `block::operator=` 里比较 `m_data == _copy.m_data`。

### B3. 后端激活状态是裸静态变量，运行期切换没有同步
- 位置：`algebra/tensor/backend/tensor_backend.cpp:16-22`（`static tensor_backend* active = nullptr`，非原子）、`:63-67`（`tensor_backend_active()` 首次调用写它）、`:70-76`（select 写它）；`hardware/hardware.h:102-108`（`static device active`，同样非原子）
- 现状：读写都是普通访问，没有原子指针、`std::call_once` 或锁。
- 证据：`tensor_backend.cpp:21` = `static tensor_backend* active = nullptr;`（函数内静态，初始化线程安全、之后的赋值不安全）；`tensor_backend.cpp:65-66` 是「首次调用即写」的路径。
- 影响：一个线程首次调用 `tensor_backend_active()`（CPU 后端的 `parallel_for` 内部也会取后端）与另一个线程 `tensor_backend_select()` 并发时构成数据竞争，按标准是未定义行为；症状大概率只是读到旧后端，但 TSAN 会报。
- 复现/验证：未复现。验证思路：TSAN 下跑「主线程 select + 若干工作线程首次 active」。
- 建议：指针改 `std::atomic<tensor_backend*>`，或把后端选择限定在启动期（之后 select 直接抛错）。

## C. 跨平台

### C1. f16 的位域布局与指针双关只在小端 + 严格别名豁免下成立
- 位置：`algebra/ftype.h:14-17`（`u16 fraction:10; exponent:5; sign:1`）、`algebra/ftype.cpp:5-13`（`_float` 用 u32 位域按同序解释 IEEE）、`ftype.cpp:12`、`ftype.cpp:17`、`ftype.cpp:58`（`*(float*)this`、`(_float*)&fValue`、`*(float*)&value` 三处类型双关；`ftype.cpp:46` 反而是安全的 `memcpy`）
- 现状：位域「声明顺序 ↔ IEEE 位序」的对应依赖小端 + 编译器把首个位域放低位（GCC/Clang/MSVC 在 x86 上都如此）；`*(float*)this` / `*(float*)&value` 违反严格别名规则。
- 证据：`ftype.cpp:12` = `inline operator float() const { return *(float*)this; }`；`ftype.cpp:58` = `return *(float*)&value;`。
- 影响：大端目标（部分 ARM/PPC 配置）上 f16⇄f32 转换结果错位；`-O2 -fstrict-aliasing` 下类型双关有被优化成错值的风险（本机 x86-64 实测行为正确，但标准不保证）。
- 复现/验证：未复现（本机 x86-64，只读码）。验证思路：把 `ftype.cpp:49-59` 的 `*(float*)&value` 改成 `memcpy`，与大端模拟/交叉编译结果比对。
- 建议：f16 两个转换入口统一用 `memcpy` 拼位（`ftype.cpp:46` 已是这个写法），位域只用于读写字段。

### C2. SIMD 内核依赖 MSVC 专有 SVML 内建，头文件内没有编译器保护
- 位置：`algebra/tensor/backend/tensor_backend_simd.h:72`、`:74`、`:285`、`:286`、`:370`、`:374`、`:393`、`:410`、`:414`、`:520`（`_mm256_atan2_ps` / `_mm256_pow_ps` / `_mm256_exp_ps` / `_mm256_log_ps` / `_mm256_sin_ps` / `_mm256_cos_ps` 等）；`CMakeLists.txt:157-162`（只有 MSVC 才加 `/arch:AVX2` 与 `-DTENSOR_WITH_SIMD`）；`test/CMakeLists.txt:53-55`（注释已写明 Linux/mac 上这些内建不可用）
- 现状：这些内建只有 MSVC（SVML）提供；头文件里没有任何 `#if defined(_MSC_VER)` 分支，也没有「无 SVML 时回退标量」的兜底。
- 证据：`tensor_backend_simd.h:74` = `case elem_pow: vr = _mm256_pow_ps(va, vb); break;`；头内 `#if` 只有 include guard（`tensor_backend_simd.h:2-3`）。
- 影响：任何在 GCC/Clang 上手写 `-DTENSOR_WITH_SIMD`（或将来 CMake 放开该开关）都会编译失败，而不是优雅降级到标量；目前仅靠 CMake 只对 MSVC 定义该宏兜住。
- 复现/验证：未复现（未在非 MSVC 环境跑）。验证命令：`g++ -std=c++17 -DTENSOR_WITH_SIMD -mavx2 -fsyntax-only -I<repo_root> -x c++ - <<< '#include "algebra/tensor/backend/tensor_backend_simd.h"'`（预期报 `_mm256_atan2_ps` 未声明）
- 建议：头内加 `#if defined(_MSC_VER)` 守卫，非 MSVC 时走标量或手写近似。（同一件事的子模块版本：`tensor/DEBUG.md` C 组「SIMD 内核没有任何平台守卫，只有 MSVC 能编译；注释声称的『其余平台回退标量桩』不存在」，那里还提到 4 个 `TENSOR_WITH_*` 宏在源码内无默认值。）

### C3. `real_128` 的宽度随平台变：MSVC / Apple arm64 上等于 real_64
- 位置：`algebra/ctype.cpp:143-144`（`real_128` → `sizeof(xhqm::fld)`）、`algebra/ctype.h:264`（`ctype_v<fld> = real_128`）、`ctype.cpp:146-147`（`array`/`tuple` 同样用 `sizeof`）
- 现状：`long double` 在 Linux x86-64 是 16 字节、MSVC x64 是 8 字节、Apple arm64 是 8 字节；`ctype::get_size(real_128)` 直接跟着变。
- 证据：`ctype.cpp:144` = `else if (type == xhqm::ctype::real_128) return sizeof(xhqm::fld);`（第 143 行还留着 `//return 16;` 的旧实现被注释掉）。
- 影响：`get_size(real_128)`、`arr("array<N,real_128>")` 的元素步长、以及按 ctype 名字交换的序列化宽度在平台间不同 —— Windows ↔ Linux 互读 `real_128` 数据会错位；名字里的「128」在 Windows/mac 上是假的。
- 复现/验证：未复现（未在 Windows/mac 跑）。验证：`static_assert(xhqm::ctype::get_size(xhqm::ctype::real_128) == 16);` 在 MSVC / Apple arm64 上失败。
- 建议：把 `real_128` 绑到固定宽度类型（`__float128`/`_Float128` 可用时优先），或改名 `real_ld` 并在文档里写清宽度。

### C4. Metal 后端的 dtype 覆盖远窄于 CUDA，且静默回退 CPU
- 位置：`algebra/tensor/backend/tensor_backend_metal.mm:3-6`（文件头：f64 在 Apple Silicon Metal 上不支持，返回 false 由调用方回退）、`:25-27`（覆盖现状：f16/f32 全算子；f64 仅 elemwise/matmul/reduce；i8/i16/i32 仅 unary(neg/abs/sign)/conv3d/trsv/spmv；bool 无内核）
- 现状：mac 上 `tensor_backend_select(gpu)` 成功后，一部分算子仍按 dtype 返回 false 落到 CPU 泛型路径。
- 证据：`tensor_backend_metal.mm:5-6`、`:25-27` 两处注释与 `.mm` 内 `metal_dtype(dt)` 的 `return false` 分支相对应（该文件共 1525 行，是 mac 唯一 GPU 实现）。
- 影响：按「已经选到 GPU」推断性能会错（f64 卷积/池化、全部 bool 算子实际在 CPU）；交叉比对 CPU 与 CUDA 结果时，容易把 dtype 回退误判成后端 bug。这不属于「未实现」而是平台能力差异，故记在跨平台组。
- 复现/验证：未复现（无 Apple 机器）。验证：mac 上 `tensor_backend_active().conv3d(...)` 传 `dtype::f64`，期望返回 false。
- 建议：把覆盖矩阵写进 `tensor/README.md`，并在首次按 dtype 回退时打一条一次性日志。（单算子缺口已记在 `tensor/DEBUG.md` E 组「Metal 的 `chol` 只有 f32 一条内核，其余类型回退 CPU」。）

## D. 性能

### D1. `tup::value/value_as` 的字段偏移是 O(index) 累加
- 位置：`algebra/ctype.h:208-211`、`ctype.h:216-219`（`for (i < index) offset += ctype::get_size(m_tuple[i]);`）
- 现状：每次随机访问第 k 个字段都要做 k 次 `get_size`（每次又是一串比较，见 D2），没有前缀偏移表。
- 证据：`ctype.h:209-210` = `for (xhqm::index i = 0; i < index; ++i) offset += ctype::get_size(m_tuple[i]);`
- 影响：按字段循环遍历大元组（序列化/检视）退化成 O(n²)：1000 字段的元组走一遍约 50 万次 `get_size`。
- 复现/验证：未复现。验证：构造 1000 字段的 `tup`，循环 `value<f64>(i)` 计时，与「构造期前缀偏移表」实现对比。
- 建议：构造期预算 `std::vector<xhqm::size>` 前缀偏移。

### D2. 类型 ↔ 名称 ↔ 大小全是 if-else 字符串链
- 位置：`algebra/ctype.cpp:65-94`（`get_type`：19 段 `type == "..."`）、`:95-122`（`get_text`）、`:123-150`（`get_size`）
- 现状：每次调用最多做 19 次 `std::string::operator==`；`arr`/`tup` 构造、每个元素的偏移计算（D1）都会反复调用。
- 证据：`ctype.cpp:90-91`（`type.find("array") == 0` 之类的前缀判断）与 `ctype.cpp:25-39` 的枚举一样，全靠线性比较。
- 影响：异构容器的构造与遍历是「字符串比较密集」路径，属于本层唯一的规模相关热点。
- 复现/验证：未复现。验证：`ctype::get_type("real_128")` 循环 1e6 次计时，对比 `static const std::unordered_map`/数组表实现。
- 建议：改成静态查表（枚举 ↔ 名称 ↔ 字节三张表）。

## E. 未实现 / 占位桩

（以下都是「编译器会过、只是没写完/没接上」的条目）

### E1. `value.h` 的 10 个常数仍是 `0.0l`
- 位置：`algebra/value.h:14-23`（`ln_2` / `ln_10` / `log_e_2` / `log_e_10` / `log_2_e` / `log_10_e` / `sqrt_2` / `sqrt_3` / `sqrt_en` / `sqrt_pi`），透传点 `value.h:70-79`
- 现状：10 个常量全部等于 `0.0l`；`number<type>::ln_2` 之类因此在所有类型上都是 0，编译器不会告警。
- 证据：`value.h:14` = `static constexpr auto ln_2 = 0.0l;`（同类共 10 行）；`value.h:70-79` 把它们逐个 `static_cast<type>`。
- 影响：用这些常数做换算的公式静默算错（信息论/数值分析最常见），也是根 README「基础层缺常量」一说的来源。
- 复现/验证：未复现（读码即可判定）。验证：`static_assert(xhqm::number<f64>::sqrt_2 > 1.0);` 失败。
- 建议：填入真实值（如 `0.69314718055994530942l`），纯常量替换、零风险。（库层面版本记在根 `DEBUG.md` A1，本文从模块视角保留行号证据。）

### E2. 泛型 `read/save` 是占位：返回 `type()` 与字面量 `"type"`
- 位置：`algebra/mathf.h:55-56`
- 现状：已特化的只有整型 `u8..i64`（`itype.h:63-127`）、浮点 `f16/f32/f64/fld`（`ftype.h:150-179`）、`complex<f32/f64/fld>`（`complex.hpp:52-86`）、`dualnum`（`dualnum.hpp:74-99`）、`quaternion<f32/f64/fld>`（`quaternion.hpp:237-257`）；`bool`/`char`/`wchar_t`/自定义类型落到泛型占位。
- 证据：`mathf.h:55` = `template<typename type> inline type read(const std::string& value) { return type(); }`；`mathf.h:56` = `... save(const type& value) { return "type"; }`。
- 影响：`save(true)` 返回字符串 `"type"`、`read<bool>("true")` 返回 false —— 序列化往返静默产出垃圾（`tup` 里的 bool 字段、`arr<bool_1>` 的检视路径都会踩）。
- 复现/验证：未复现。验证：`assert(xhqm::save(true) == "type");`（当前成立，即占位成立）
- 建议：对无特化类型改 `static_assert` 拒绝，而不是用占位值假装成功。

### E3. 非 CUDA 构建里的 GPU 桩是死代码
- 位置：`algebra/tensor/backend/tensor_backend_gpu.h:10-12`（注释声称「当前为桩：声明内存搬运能力，但算子静默回退主机串行执行」）、`tensor_backend_gpu.cpp:58-245`（每个算子 `#else` 分支 `return false`）、`algebra/tensor/backend/tensor_backend.cpp:38-44`（只有 `XHQM_TENSOR_CUDA` 才会 `return create_gpu_backend();`，否则回退 cpu/no）、`CMakeLists.txt:657-661`（CUDA 未找到时仍然编译 `tensor_backend_gpu.cpp`）
- 现状：CUDA 未找到的分支会编出一份 gpu 桩，但没有任何路径会创建它（Metal 走 `create_metal_backend()`，其余走 cpu/no）。
- 证据：`tensor_backend.cpp:39` = `return create_gpu_backend();` 被 `#if defined(XHQM_TENSOR_CUDA)`（`:38`）包住；`CMakeLists.txt:661` = `message(STATUS "Tensor GPU backend: CUDA not found, stub fallback")`。
- 影响：读注释/看构建日志会以为「无 CUDA 也有 GPU 后端可回退」，实际回退逻辑在 `create_backend` 里，桩是死代码，还容易让人误判 `gpu` 的运行时行为与 G3 的注释。
- 复现/验证：未复现。验证：`grep -rn 'create_gpu_backend' algebra/` —— 唯一调用点在 `#if defined(XHQM_TENSOR_CUDA)` 内。
- 建议：要么删桩并让 CMake 直接报错，要么让 `create_backend` 在无 CUDA 时也构造它并在 `name()` 里标 `"gpu(stub)"`。（同一件事的子模块版本：`tensor/DEBUG.md` E 组「未编 CUDA 时 `tensor_backend_gpu.cpp` 仍被编译成『全 false 桩』」。）

### E4. GPU 后端恒报「支持内存搬运」，但 upload/download 是空实现
- 位置：`algebra/tensor/backend/tensor_backend_gpu.cpp:41`（`supports_memory_transfer()` 恒 `true`）、`:248-249`（`upload`/`download` 空体）、`:250-255`（只有 `synchronize` 在 CUDA 下真的同步）
- 现状：即使编了 CUDA，`upload/download` 也不搬数据；真实搬运在 kernel 内部自己做（`tensor_backend_gpu.cu:164-193` 设备内存池 + 各算子的 `cudaMemcpyAsync`）。
- 证据：`tensor_backend_gpu.cpp:41` = `bool supports_memory_transfer() const noexcept override { return true; }`；对照 `tensor_backend_no.cpp:18` 与 `tensor_backend_cpu.cpp:479` 都返回 `false` —— 只有 gpu 报 true。
- 影响：按能力标志判断「数据已在设备上」的上层代码会拿到错误前提（把主机指针当设备指针用）；与 `.cu` 里「每次调用自行 H2D/D2H」的模型自相矛盾。
- 复现/验证：未复现。验证：CUDA 构建下 `tensor_backend_active().supports_memory_transfer()` 为 true，但 `upload` 之后数据仍在主机内存。
- 建议：没有真实搬运时返回 `false`，让调用方只用算子级 API（`.cu` 现状正是如此）。

### E5. `net` 设备/后端没有实现，只做占位回退
- 位置：`algebra/tensor/backend/backend.h:11`（注释「net 占位回退 CPU/no」）、`algebra/tensor/backend/tensor_backend.cpp:51-56`、`hardware/hardware.h:35-42`（`device::net = 3` 存在）
- 现状：`XHQM_DEVICE=NET` 或 `tensor_backend_select(device::net)` 都能编译通过，实际拿到名为 `"cpu"`（有 CPU 后端时）或 `"no"` 的后端。
- 证据：`tensor_backend.cpp:53` = `return create_cpu_backend();	// 分布式占位，回退 CPU`。
- 影响：上层以为在分布式后端上跑，实际是本地 CPU；`XHQM_DEVICE=NET` 构建会静默给出与「分布式」预期不符的行为，且没有任何告警。
- 复现/验证：未复现。验证：`tensor_base::tensor_backend_select(xhqm::hardware::device::net); std::string(tensor_base::tensor_backend_active().name()) == "cpu"`（CPU 构建下成立）。
- 建议：`net` 走显式 `xhqm::error`，或至少在 `name()` 里标出 `"net(stub→cpu)"`。（同一件事的子模块版本：`tensor/DEBUG.md` E 组「`net` 设备没有实现类，被直接映射成 cpu / no」。）

### E6. `eigs` / `svds` 仍然缺失（`roots`/`schur` 已经有了）
- 位置：`algebra/analysis/README.md` 的「7. 已知取舍与后续」（2026-10-07 复核时在 `:148`，自认暂缓 eigs/svds）；`algebra/analysis/linalg.h:242`（`schur`）、`:253`（`roots`）已实现并转发 `tensor_base`；全模块源码（`*.h/*.hpp/*.cpp/*.cu/*.mm`）无 `eigs`/`svds` 定义
- 现状：Arnoldi/Lanczos 的迭代特征值/奇异值没有实现，只有稠密 `eig`（`analysis/linalg.h:222`）/`svd`（`:204`）。
- 证据：`grep -rn 'eigs\|svds' algebra/ --include='*.h'` → 0 命中；文档侧命中只有 `analysis/README.md:148`、`analysis/DEBUG.md` E1 与本文/README 的引用。
- 影响：需要稀疏或部分特征值的大矩阵只能退化成稠密分解（内存与时间都吃紧）。
- 复现/验证：未复现。验证：`grep -rn 'eigs\|svds' algebra/ --include='*.h'`
- 建议：保持 `analysis/README.md` 的「暂缓」措辞不变（本模块 README 已订正为「只缺这两项」）；同一条的子模块版本见 `analysis/DEBUG.md` E1。

### E7. `text` 元素在 `arr`/`tup` 里没接上
- 位置：`algebra/ctype.cpp:123-150`（`get_size` 没有 `text` 分支，落到末尾 `return 0;`）、`algebra/ctype.h:130-153`（arr::value_as 的 18 个分支里没有 `text`）、`ctype.h:222-245`（tup::value_as 同）
- 现状：`ctype::text` 在枚举（`ctype.h:17`）、`get_type/get_text`（`ctype.cpp:68`、`:98`）、`ctype_v<text>`（`ctype.h:249`）里都存在，但容器侧把它当 0 字节元素处理，取值也没有对应分支。
- 证据：`ctype.cpp:126-149` 的分支从 `null` 直接跳到 `bool_1`，没有 `text`；`ctype.h:134-151` 的分支同样从 `null` 到 `bool_1`。
- 影响：`xhqm::arr A("array<2,3,text>");` 的块大小算成 0 → `good()` 为 false，构造「成功」但完全不可用；`value_as<std::string>(i)` 返回默认构造的空串（不会报错），静默错值。
- 复现/验证：未复现。最小复现：`xhqm::arr A("array<2,3,text>"); assert(A.good()); // 失败`
- 建议：要么给 `text` 定义固定宽度/偏移语义，要么在构造时直接拒绝 `text` 元素类型。

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. `arr`/`tup` 用「字符串类型描述 + 裸块 + placement new + 手写析构」
- 位置：`algebra/ctype.cpp:183`、`:190`、`:248`、`:252`（placement new）、`ctype.cpp:208`、`:215`、`:271`、`:275`（手动析构）、`ctype.h:50-154`、`ctype.h:156-246`
- 现状：类型由字符串表达（如 `"array<2,3,real_32>"`），元素按 `ctype` 枚举紧凑排在 `xhqm::block` 的裸缓冲里；`array`/`tuple` 元素用 placement new 构造，析构时手写逐元素 `~arr()`/`~tup()`。
- 证据：`ctype.cpp:183` = `new(m_block.value_ptr(i * size)) xhqm::arr(type_list.back());//构造`；`ctype.cpp:208` = `m_block.value_ptr<xhqm::arr>(i * size)->~arr();//析构`。
- 影响（取舍）：**为什么现在这样是对的** —— 字符串类型描述才能与 ctype 枚举、序列化、反射式检视互转，不必为「每种 dtype × 每种维度组合」造模板实例，块 + 元素类型也天然支持任意 `ctype`。**代价**：对象生命周期、拷贝、析构全靠手写代码负责，当前拷贝路径是裸 `memcpy`，直接换来 A2/B1/B2；元素步长还必须与 `get_size` 手工保持同步（`real_128` 的宽度问题见 C3）。
- 复现/验证：不适用（这是设计决定，不是缺陷）。验证这条取舍确实存在：`grep -n 'new(m_block.value_ptr' algebra/ctype.cpp`
- 建议：不修；要修的是它的实现缺口（按元素类型逐元素深拷贝），而不是回退成模板。

### F2. `f16` 不重载算术/比较运算符，一切经 `operator float()`
- 位置：`algebra/ftype.h:27-29`（只声明 `float_16(const float&)` 与 `operator float()`）、`ftype.h:116-151`（所有泛型特化都写成 float 表达式）
- 现状：`sum/sub/mul/div`、`most/less/equal` 等对 f16 的实现都是先把两侧转成 float 再算，`float_16` 自身不提供任何运算符。
- 证据：`ftype.h:118` = `template<> inline f16 sum(const f16& left, const f16& right) { return left + right; }` —— 这里的 `+` 是 float 的加法，结果再隐式转回 f16。
- 影响（取舍）：**为什么现在这样是对的** —— 半精度只当存储/传输格式，运算统一在 float 域完成，避免为 10 位尾数手写一整套位级算子，也让 `float_16` 保持成可 `memcpy` 的 POD 位域。**代价**：每次运算两次转换；没有位级算子（subnormal 在 `ftype.cpp:33-38` 被丢成 0）；比较落到 float 比较，于是 `isnan(f16)` 失效（A1）；不能 constexpr 运算。
- 复现/验证：不适用（设计决定）。验证：`grep -n 'operator' algebra/ftype.h` 只命中 `operator float()`。
- 建议：不修；要动就只补位级 `isnan/isinf`（A1 的修法），不要改成「补全运算符」。

### F3. CPU 上的迭代矩阵函数（`schur`/`sqrtm`/`logm`/`expm`）刻意串行
- 位置：`algebra/tensor/linalg.h:1122`、`:1162`、`:1190`（注释「CPU 并行 = CPU 串行（迭代逐次依赖…）」）
- 现状：这三个迭代实现都是串行循环，只有内部调用的 `matmul`/`inv` 会走并行/后端加速。
- 证据：`linalg.h:1122` = `// 注：CPU 并行 = CPU 串行（迭代逐次依赖，确实不适合 CPU 并行；内部 matmul 已并行）。`
- 影响（取舍）：**为什么现在这样是对的** —— 逐次依赖的迭代无法安全并行，强行分块只增加调度与同步开销；并行度已经由内部 `matmul`/`inv` 提供。**代价**：大矩阵的 `expm/sqrtm/logm/schur` 在 CPU 上明显慢于 BLAS 化路径；这种「串行」没有通过 backend 的 `parallel_for` 表达，从后端侧看不出来。
- 复现/验证：不适用（设计决定）。验证：`grep -n 'CPU 并行 = CPU 串行' algebra/tensor/linalg.h`（3 处）
- 建议：不修；若要提速，应从算法入手（分块 Padé、缩放-平方的 BLAS 化），而不是把循环并行化。

### F4. 加速后端只对「精确匹配的 dtype」开 kernel
- 位置：`algebra/tensor/backend/tensor_backend.h:25-27`（注释：`complex<float>`/`dualnum<float>` 也是 8 字节，用 `sizeof` 判类型会把它们当成 double）、`tensor_backend.h:42-52`（`dtype_of` 泛型在 `:43`，9 个显式特化在 `:44-52`）
- 现状：只有 `f16/f32/f64/fld/i8/i16/i32/i64/bool` 在 `dtype_of` 里有映射，其余类型一律 `dtype::unknown`，后端算子返回 false 后由上层走 `xhqm::` 泛型串行路径。
- 证据：`tensor_backend.h:26` = `// 其余（复数/对偶数/四元数/自定义）一律回退 CPU，避免按 sizeof 误判`；`tensor_backend.h:43` = `template<class type> struct dtype_of { static constexpr dtype value = dtype::unknown; };`。
- 影响（取舍）：**为什么现在这样是对的** —— 宁可慢也不能按 `sizeof` 猜类型，猜错的后果是静默错值（把复数当 double 算）。**代价**：复数/对偶数/四元数/自定义类型完全没有 SIMD/BLAS/GPU 加速，而且从调用方看不出「这段为什么慢」。
- 复现/验证：不适用（设计决定）。验证：`grep -n 'dtype_of' algebra/tensor/backend/tensor_backend.h`
- 建议：不修；需要加速时应显式提供 dtype 适配层，而不是放宽匹配规则。

### F5. 关闭 tensor 模块时仍然恒编入 no 后端与调度单元
- 位置：`CMakeLists.txt:266-269`（`XHQM_BODY_CORE` 里的注释与 `tensor_backend.cpp` / `tensor_backend_no.cpp`）
- 现状：`XHQM_BUILD_TENSOR=OFF`（默认）时，`tensor_backend.cpp` 与 `tensor_backend_no.cpp` 仍然编入核心库，`matrix`/`tensor` 等模板头因此总能链接运行。
- 证据：`CMakeLists.txt:266-267` = `# 张量 no 后端恒编入：关闭 TENSOR 模块时 tensor_backend_active 仍可用` / `# （等价于「TENSOR 开 + DEVICE=NO」的串行桩），matrix/tensor 运算可链接运行`。
- 影响（取舍）：**为什么现在这样是对的** —— 保证矩阵/张量模板的最小可用性（不依赖模块开关就能跑串行）。**代价**：核心库恒带一份后端实现与进程级全局状态（`tensor_backend_active()` 的静态指针，见 A9/B3）；「没开 tensor 也能跑」在文档里不显眼，容易让调用方以为拿到的是并行实现。
- 复现/验证：不适用（设计决定）。验证：`sed -n '266,270p' CMakeLists.txt`
- 建议：不修；在 README/CMake 注释里写明即可（本模块 README 已写）。

### F6. `XHQM_PARALLEL_THRESHOLD` 默认 4096（低于阈值退回串行）
- 位置：`algebra/tensor/backend/tensor_backend.h:16-19`（默认值）、`tensor_backend_cpu.cpp:131`、`:163`、`:493`（实际判断点）
- 现状：元素数（或非零数）小于 4096 时 CPU 后端走串行，超过才 `std::thread` 分块并行。
- 证据：`tensor_backend.h:18` = `#define XHQM_PARALLEL_THRESHOLD 4096`；`tensor_backend_cpu.cpp:493` = `if (count < XHQM_PARALLEL_THRESHOLD || threads == 1)`。
- 影响（取舍）：**为什么现在这样是对的** —— 小规模数据上线程创建/唤醒的开销大于并行收益。**代价**：4096 附近吞吐有个台阶；这是单一经验值，逐元素/卷积/池化的规模曲线并不一致，没有按算子分别标定。
- 复现/验证：不适用（设计决定）。验证：`grep -rn 'XHQM_PARALLEL_THRESHOLD' algebra/`
- 建议：不修默认值；调优时按算子分别压测再覆盖该宏。

### F7. 常数手写、不引入 `<numbers>`（工程锁 C++17）
- 位置：`algebra/value.h:5`、`algebra/mathf.h:9`（`//#include <numbers>` 被注释）、`CMakeLists.txt:19`（`set(CMAKE_CXX_STANDARD 17)`）
- 现状：`en/pi/phi/egamma` 等常数在 `value.h:10-23` 手写为 `long double` 字面量，`<numbers>` 的包含被注释掉。
- 证据：`value.h:5` = `//#include <numbers>`；`CMakeLists.txt:19` = `set(CMAKE_CXX_STANDARD 17)`（`<numbers>` 是 C++20 头）。
- 影响（取舍）：**为什么现在这样是对的** —— 工程标准锁在 C++17，用不了 `std::numbers`，手写是唯一选择。**代价**：常量要人工维护（当前其中 10 个还是 0，见 E1），也用不上 `std::numbers` 的模板常量；根 README 把它记成「未启用 `<numbers>`」，容易被读成偷懒而不是语言版本约束。
- 复现/验证：不适用（设计决定）。验证：`grep -n 'CMAKE_CXX_STANDARD' CMakeLists.txt`
- 建议：不修；`CMAKE_CXX_STANDARD` 升到 20 时再统一切换到 `std::numbers`。

## G. 文档与代码的出入

### G1. 根 README 的模块总览说「基础层无单测」，实际有
- 位置：根 `README.md` 模块总览表里 algebra 一行（模块外文档，本次未改；2026-10-07 复核时该行在 `:142`，同批其它任务也在改这份文件，行号会漂）
- 现状：基础层有 Catch2 覆盖 —— `test/algebra_test.cpp`（766 行）从 `test_mathf`（`:13`，含 f16 转换、`max<f16>`/`ismax`、`read/save` 往返、`number<f64>::pi`）到 `test_ctype`（`:193`，构造 `arr`/`tup` 并查 `size()`）；`test/algebra_extended_test.cpp`（378 行）覆盖 matrix/vector/stats/linalg/logm-sqrtm；`test/CMakeLists.txt:16-25` 的 `algebra_test` 目标把 `../algebra/ftype.cpp`、`ctype.cpp` 编了进去。
- 证据：`grep -n 'TEST_CASE' test/algebra_test.cpp` → 24 个用例，首个是 `test_mathf`（`:13`）、`test_ctype` 在 `:193`。
- 影响：外部读者（以及交接文档）会据此低估基础层；真实缺口不是「没测」，而是没覆盖 A1/A2/A3/A4 这些边角（NaN 判定、嵌套容器拷贝、越界、泛型兜底）。
- 复现/验证：`grep -n 'TEST_CASE' test/algebra_test.cpp test/algebra_extended_test.cpp`
- 建议：该行改成「有 Catch2 覆盖，缺 NaN/嵌套容器/越界等边角用例」；同一行的「未启用 `<numbers>`」应改成「C++17 约束」（见 F7）。

### G2. 兄弟台账与本模块的交叉引用已经漂移
- 位置：`algebra/analysis/DEBUG.md:222-223`（G1「兄弟文档仍写『roots/schur 暂缓』」把上游行号指到 `algebra/README.md:216`）、同文件 `:226-232`（G2 记「跨文档行号引用已漂移」）；`algebra/README.md`（本轮已改，那句旧话现在 `:246` 且已标「已作废」）
- 现状：2026-10-07 同批建立了 6 份模块级 DEBUG.md（根、`hardware`、`lattice` + 本模块的 `tensor`/`analysis`/`geometry`/`geometry/brep`/`soul`）。`analysis/DEBUG.md` 的 G1 按「编辑前」的行号把说法指向本模块 README，而本模块 README 同轮又改了行号并已就地作废那句 —— 引用与被引用方各自都对，但行号对不上了。`tensor/DEBUG.md` 与本文在 A9/C2/C4/E3/E5 五处记的是同一批事实（已在各条目注明子模块版本的位置）。
- 证据：`grep -n 'algebra/README.md:216' algebra/analysis/DEBUG.md` → 命中 `:223`、`:228`；`grep -n '已作废' algebra/README.md` → 旧句现在 `:246`。
- 影响：接手的人按 `analysis/DEBUG.md` 的指引去 `algebra/README.md:216` 找「暂缓 roots/schur/eigs/svds」会找到别的段落；两条台账对同一件事的措辞也可能再次分叉。
- 复现/验证：`grep -rn 'algebra/README.md:' algebra/*/DEBUG.md`
- 建议：台账之间只引用**节名/条目 id**（如「§6 已作废段」「`algebra/DEBUG.md` E6」），不引用行号；同批编辑后由其中一方做一次交叉引用回扫。

### G3. `tensor_backend_gpu.h` 的注释与调用现实不符
- 位置：`algebra/tensor/backend/tensor_backend_gpu.h:10-12`
- 现状：注释写「当前为桩：声明内存搬运能力，但算子静默回退主机串行执行」；实际无 CUDA 时根本不会构造这个后端（`tensor_backend.cpp:38-44` 只在 `XHQM_TENSOR_CUDA` 下创建它），有 CUDA 时它是真内核（`tensor_backend_gpu.cu`，1710 行，cuBLAS/cuSOLVER + 手写 kernel）。
- 证据：`tensor_backend_gpu.h:11-12` 两行注释 vs `tensor_backend_gpu.cpp:41`（`supports_memory_transfer()` 恒 true）与 `tensor_backend.cpp:39`（CUDA 下才 `return create_gpu_backend();`）。
- 影响：读注释会同时低估（以为永远是桩）和高估（以为无 CUDA 也有 gpu 桩可用）这个后端，并误导 E3/E4 的判断。
- 复现/验证：`grep -rn 'create_gpu_backend' algebra/`
- 建议：注释改为「有 CUDA 时为真内核；无 CUDA 时本文件编成死代码，回退发生在 `tensor_backend.cpp`」。

### G4. `tensor/README.md` 的目录结构把 GPU host 层记成「CUDA/Metal 分派」
- 位置：`algebra/tensor/README.md` 的「目录结构」一节里 `tensor_backend_gpu.h/.cpp` 那一行（2026-10-07 复核时在 `:64`；子模块同批也在改这份文件，行号会漂）
- 现状：该行写「GPU host 侧（CUDA/Metal 分派；无 CUDA 时为全 false 桩）」；Metal 分派其实在 `algebra/tensor/backend/tensor_backend.cpp:34-37`（`XHQM_TENSOR_METAL` → `create_metal_backend()`），`tensor_backend_gpu.cpp` 里只有 CUDA host 包装与 CUDA 侧的 `#ifdef` 分支。
- 证据：`grep -in 'metal' algebra/tensor/backend/tensor_backend_gpu.cpp` 无命中；Metal 相关命中集中在 `tensor_backend.cpp:34-37` 与 `tensor_backend_metal.h/.mm`。
- 影响：按目录结构找 Metal 实现的人会翻错文件（也会以为切 Metal 要改 gpu.cpp）；「无 CUDA 时为全 false 桩」的说法另见 E3（桩不可达）。
- 复现/验证：`grep -n 'metal' algebra/tensor/backend/tensor_backend.cpp algebra/tensor/backend/tensor_backend_gpu.cpp`
- 建议：该行改成「GPU host 侧（CUDA 包装；Metal 分派在 tensor_backend.cpp）」。属子模块文档，本次未改。

---

## 统计

- A 功能性缺陷：9 条（A1–A9）
- B 资源与并发：3 条（B1–B3）
- C 跨平台：4 条（C1–C4）
- D 性能：2 条（D1–D2）
- E 未实现 / 占位桩：7 条（E1–E7）
- F 设计取舍（不修）：7 条（F1–F7）
- G 文档与代码出入：4 条（G1–G4）
- 合计：36 条

未确认项（不在上面计数内，留给下一轮）：本文件所有条目都没有编译/运行验证 —— 标「未复现」的 25 条（A1–A9、B1–B3、C1–C4、D1–D2、E1–E7）只给出可直接跑的最小复现/验证命令；F 组 7 条是设计取舍、G 组 4 条是文档核对，验证方式为 grep/sed 命令。此外 `tensor_backend_metal.mm`（1525 行）与 `tensor_backend_gpu.cu`（1710 行）只做了注释级 + grep 级核对，未逐行通读，其内核正确性未评估。
