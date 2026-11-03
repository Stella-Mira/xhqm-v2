# metaclass —— 当前仍存在的问题

`metaclass`（编译期元编程 / 类型反射 / 通用容器基础层，纯 header-only）在 r677 工作副本下**仍然存在**的缺陷、缺口与取舍台账。用法与 API 见同目录 `README.md`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
> **与同类文件的关系**：本仓台账**不搬家不改名**。本文件与根 `DEBUG.md`、`xhqm/DEBUG.md`、`algebra/DEBUG.md` 是同一体系；`fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` 是历史台账。仓库根 `通读报告-2026-10-07.md:109` 那组旧结论中，本模块涉及的「`thread/` 空目录」成立、「`head.cpp` 引用不存在的 `values.h`」不成立（见 A5）；「`buffer_dim` 多维下标方向与行优先相反待验证」本轮已实测确认（见 A3）。

> 核对环境：g++ 11.4（Ubuntu 22.04）/ `-std=c++17` / 工作副本 r677；探针一律走 `/tmp`，**不落仓**；include 路径 `-I<repo 根>`。所有「实测」都是本节点跑过的，命令原样给出；没跑的一律写「未复现」。

本模块规模（实测 `wc -l`）：`compute.h` 58、`template.h` 224、`cache.h` 78、`block.h` 56、`buffer.h` 199、`resource.h` 62、`tuple_values.h` 246、`function/` 5 个头合计 473、`thread/` **空目录**；合计 11 个头 **1396 行**，无 `.cpp`。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `buffer_dim::value()` 的两个重载都编不过：形参是 `const` 却在函数体里被赋值
- 位置：`metaclass/buffer.h:178`（非 const 重载 `value(const std::array<xhqm::size, dim>&)`，声明在 `:172`）、同文件 `:166`（const 重载 `value(std::array<xhqm::size, dim>) const`，声明在 `:160`）
- 现状：两处都执行 `_location[index] = std::accumulate(...)`，把**入参**当暂存数组原地改写；非 const 重载的形参是 `const std::array&` ⇒ 赋值给 const 元素，属硬编译错误。
- 证据：`sed -n '172,178p' metaclass/buffer.h`；实测（本节点）：`xhqm::buffer_dim<int,2> m(...); m.value(p)` → `buffer.h:178:50: error: assignment of read-only location '(& _location)->std::array<long unsigned int, 2>::operator[](index)'`。
- 影响：**`buffer_dim::value()` 在任何调用形式下都无法实例化** —— 传 `std::array{1,2}` 这类临时量会命中 `:160` 的 const 重载（同样在 `:166` 报错），传具名变量会命中 `:172`（在 `:178` 报错）。也就是说本模块「多维数组取值」这一核心能力当前**完全不可用**，README 的用法示例 `mat.value({1,2}) = 3.14f` 编不过。
- 复现/验证：`cd <repo> && printf '#include "metaclass/buffer.h"\nint main(){ xhqm::buffer_dim<int,2> m(std::array<xhqm::size,2>{2,3}); std::array<xhqm::size,2> p{1,2}; return m.value(p); }\n' | g++ -x c++ -std=c++17 -fsyntax-only -I. -` → 1 条 error（`buffer.h:178`）。
- 建议：把非 const 重载的形参改成按值 `std::array<xhqm::size, dim> _location`（或另用局部数组做暂存），两处一起改；const 重载目前是被死路径掩盖的同一处 bug，应一并修。

### A2. `compute::sub` / `compute::div` 的语义与直觉相反（`select` 名义是「方向」，实际是「二选一」）
- 位置：`metaclass/compute.h:35`（`sub`）、`:49`（`div`）
- 现状：`sub<select, val, vals...>::value = val - select_if<select, sum<vals...>, sub<select, vals...>>::value`；由于 `select` 在递归里被**原样透传**（不是取反、也不是终止条件），整条链的每一层都会先递归算到 `sub<select>` 的终止形态再回填，实际结果：
  - `select=true`：`val - sum(vals...)`（正确）；
  - `select=false`：`val - 最后一项`（因为终止形态 `sub<select, val>::value = val` 只保留末位）。
  `div` 同构，`select=false` 时得 `val / 最后一项`。
- 证据：`sed -n '33,53p' metaclass/compute.h`；实测三条 `static_assert`（本节点）：`sub_value<true,10,2,3> == 5` ✅、`div_value<true,100,2,5> == 10` ✅、`sub_value<false,10,2,3> == 5` ❌（实际得 `10 - 3 = 7`）。README 旧文对 `select=false` 的描述是错误的（已在该 README 里订正）。
- 影响：按「`false` = 左结合链式 `((val-v2)-v3)`」理解并使用的人**每条都会算错**，且是编译期常量错误、不会给运行时提示。当前仓内无调用点（`grep -rn "sub_value\|div_value" --include=*.h --include=*.hpp --include=*.cpp .` 只命中 `compute.h` 自身），属潜伏项。
- 复现/验证：`printf '#include "metaclass/compute.h"\nint main(){ static_assert(xhqm::templates::compute<int>::sub_value<false,10,2,3> == 5, "boom"); }\n' | g++ -x c++ -std=c++17 -fsyntax-only -I. -` → 1 条 `static assertion failed`。
- 建议：要么把 `select=false` 的语义实现成真正的左结合链（递归里改为逐层折叠），要么删掉 `select` 参数、只保留 `val - sum(vals...)`，并在注释里写清口径。

### A3. `buffer_dim` 的下标方向是**列优先**（维 0 变化最快），与文档/直觉的「行优先」相反
- 位置：`metaclass/buffer.h:163-168`（const 重载）与 `:175-181`（非 const 重载）的索引折叠逻辑
- 现状：折叠用 `index = Σ location[i] × Π d[0..i-1]`，即**第 0 维步长最小**（`d[0]`=最快变化维）。所以 `buffer_dim<int,2>({2,3})` 的线性布局对应 `(r,c)`：`0→(0,0), 1→(1,0), 2→(0,1), 3→(1,1), 4→(0,2), 5→(1,2)`（列优先 / Fortran 序），而 README 与 `buffer.h:20` 的注释都把它说成「行优先展平」。
- 证据：把 `value()` 按 A1 的修法**在仓库外的副本里**改成按值形参后实跑（本节点）：`(r0,c0)->0, (r0,c1)->2, (r0,c2)->4, (r1,c0)->1, (r1,c1)->3, (r1,c2)->5`。（仓库内原件因 A1 编不过，无法直接实跑；这是「修掉 A1 后就能观察到的语义」。）
- 影响：照「行优先」理解去填/读多维数组（尤其接图像、矩阵、张量这类约定 row-major 的数据）会**整片错位**，且不报错。`algebra/geometry/linear.h` 与 `fantasy` 都 include 了 `buffer.h`，但它们的 `matx/vect` 用的是自己的 `linear.h` 存储，所以目前没有已知的错误消费点——属潜伏项。
- 复现/验证：见 A1 的复现命令（先修 A1 才能跑）；判定依据是 `buffer.h:166/:178` 的 `std::accumulate(m_dimensions.begin(), m_dimensions.begin() + index, _location[index], …)`。
- 建议：先定口径（要行优先就把步长改成 `Π d[i+1..dim-1]`），再同步 `buffer.h:20` 的注释与 README 示例；两处不要各说一套。

### A4. `function<void>` 的类型擦除：析构函数里的两行 `std::runtime_error` 是死代码 ⇒ 手动 `new` 出来的两块内存**永不释放**
- 位置：`metaclass/function/function_type.h:44-48`（析构）、`:53-54`/`:67-68`（两次 `new`）
- 现状：`~function()` 里写的是 `if (m_argvs_ptr) std::runtime_error error("…");` —— 这是**构造一个临时异常对象**，不是 `throw`。对象立即析构，什么都没发生；紧接着没有 `delete`。两个指针因此泄漏（`up_function` / `up_argvs` 每次覆盖还会再多漏一块旧内存）。
- 证据：`cat -n metaclass/function/function_type.h | sed -n '44,48p'`；`grep -n "new " metaclass/function/function_type.h` → `:54`、`:68`；`grep -n "delete" metaclass/function/function_type.h` → `:53`、`:59`、`:67`、`:75`（**都只在覆盖旧值时删，析构里没有**）。
- 影响：`function<void>` 是「无签名擦除容器」，凡用它登记回调的代码每登记一次泄漏两块堆内存（函数对象 + 参数元组），析构时不回收；同时**手动转型 `static_cast<...>` 没有运行时类型标记**，用错误的 `func_type` 去 `up_argvs` / `call` 是静默 UB（不崩则读到别的类型）。
- 复现/验证：静态可判（`sed` 证据已足够）。运行期泄漏可这样看：`printf '#include "metaclass/function/function_type.h"\nint main(){ for(int i=0;i<100000;++i){ xhqm::function<void> f; f.up_function<int(int)>([](int x){return x;}); f.up_argvs<int(int)>(std::make_tuple(1)); } }\n' | g++ -x c++ -std=c++17 -I. -o /tmp/leakprobe - && valgrind --leak-check=summary /tmp/leakprobe` —— 未复现（本节点未装 valgrind 未跑）。
- 建议：析构里补 `delete static_cast<...>`（或直接把两个指针换成 `std::unique_ptr<void, 擦除器>`）；再加一个 `typeid`/标签字段让转型可校验。

### A5. ~~`head.cpp` 引用不存在的 `./metaclass/struct/values.h`（悬空引用）~~ —— 该 include 在注释块内，与「编不过」无因果
- 位置：`head.cpp:22`（在 `:12` 的 `/*` 与 `:52` 的 `*/` 之间）
- 现状：本模块 README 旧「已知问题」把它记成一条悬空引用，根 `通读报告-2026-10-07.md` 也照抄了。实测：`metaclass/struct/` 目录**确实不存在**，但 `head.cpp:22` 位于注释块内、不参与编译。`head.cpp` 真正编不过的原因是 `:63` 的 `0b…ui16`（MSVC 扩展后缀，GCC 不认），与 `values.h` 无关。
- 证据：`sed -n '10,25p' head.cpp`（先 `/*` 后若干 include）；`ls metaclass/struct` → 无此目录；`cmake --build build` 与 `g++ -fsyntax-only head.cpp` 的首条错误都在 `:63`。
- 影响：照旧文「补上 `values.h`」去修，对编译结果**零影响**；反而会让人以为注释块里的伪代码是活代码。
- 复现/验证：`cd <repo> && g++ -std=c++17 -fsyntax-only -I. -I../depend/nlohmann head.cpp 2>&1 | grep error:` → 只有 `:63` 那条。
- 建议：README 的表述改为「注释块内的历史伪代码（`head.cpp:12-52`）」；`head.cpp` 的 `ui16` 问题属核心层，记在 `xhqm/DEBUG.md`。

### A6. `resource<type, id>` 的推导指引对**单参写法**是坏的：`resource r{5}` 编不过
- 位置：`metaclass/resource.h:12-16`（指引）、`:19-20`（模板头：第二个参数 `_id` **没有默认值**）、`:25`/`:29-40`（两个构造函数）
- 现状：指引把 `_id` 缺省成 `int`，但类模板声明里 `_id` 无默认值 ⇒ 想用「显式两参」必须写 `resource<int,int>`；而想用「单参推导」时，候选构造函数 `template<typename... _argv_type> resource(_argv_type&&...)` 与拷贝/移动构造竞争，实测推导失败。
- 证据：实测（本节点）三组探针：`resource<int,int> r;` → **0 error**；`resource r{5};` → **4 error**（`tuple index must be in range`、`class template argument deduction failed`、`no matching function for call to 'resource(int)'`）；`int id = resource<int,int>{};` → **0 error**（`operator _id()` 对 `_id = int` 可用）。
- 影响：README「构造自动推导（ID 缺省为 int）」这句只对**类型可推导**的情形成立（如 `resource<T> r{...}` 经别名/显式类型），对 `resource r{5}` 这类写法直接编不过；使用者会误以为是编译器问题。
- 复现/验证：`printf '#include "metaclass/resource.h"\nint main(){ xhqm::resource r{5}; }\n' | g++ -x c++ -std=c++17 -fsyntax-only -I. -` → 4 条 error。
- 建议：给类模板第二个参数加默认值（`template<class type, class id = int>`），或把指引改成不依赖 `tuple_element` 的专门写法。

### A7. `resource::reset(...)` 转调被管理对象自己的 `reset(...)`，语义与「重建资源」不符
- 位置：`metaclass/resource.h:43-48`
- 现状：`void reset(_argv_type&&...) { share_ptr->reset(...); }` —— 请求的是 `_type::reset`，不是 `std::shared_ptr::reset`。于是它**不换对象、不重新构造**，而是把参数转发给被管理对象；如果 `_type` 没有名为 `reset` 的成员，这个成员模板一旦实例化就编译失败（当前 `resource<int,int>` 即如此）。
- 证据：`sed -n '43,48p' metaclass/resource.h`；`grep -rn "\.reset(" --include=*.h software/ fantasy/ | grep -i resource` → 0 命中（无调用点）。
- 影响：名字暗示「释放并重建」，实际是「给被管理对象发一条 reset 消息」；同名不同义，容易被当成 shared_ptr 语义误用。
- 复现/验证：未复现（无调用点）；`printf '#include "metaclass/resource.h"\nint main(){ xhqm::resource<int,int> r; r.reset(1); }\n' | g++ -x c++ -std=c++17 -fsyntax-only -I. -` 会报 `int` 无成员 `reset`。
- 建议：改名（如 `invoke_reset`）或改成 `share_ptr = std::make_shared<_type>(...)` 的真重建语义。

---

## B. 资源与并发

### B1. `block` / `buffer` 用手动 `malloc`/`new[]` + 裸指针，且拷贝语义是「浅拷贝后再各自释放」
- 位置：`metaclass/block.h`（`init/free` + `value_ptr`）、`metaclass/buffer.h:24-60`（`reset(size)` / `data()`）
- 现状：两个容器都自己管原始内存，公开 `data()` / `value_ptr()` 裸指针；`buffer` 的拷贝构造函数按值复制指针（见 `:117` 附近的 `m_buffer(_copy.m_buffer)` 走的是 `buffer` 自己的拷贝）——本节点**只做了阅读，未构造双释放探针**，故本条标「未复现」，仅记录风险面。
- 证据：`grep -n "new\[\]\|delete\[\]\|malloc\|free" metaclass/block.h metaclass/buffer.h`。
- 影响：接 `block`/`buffer` 的模块一旦复制容器并越过生命周期，就可能双释放；`geometry/linear.h` 与 `fantasy` 都在用（见 README 依赖节）。
- 复现/验证：未复现。
- 建议：换成 `std::unique_ptr<type[]>` / `std::vector<type>`（`cache` 已经是这么做的），或显式 `= delete` 拷贝。

---

## C. 跨平台

（本模块纯头文件、无平台分支，**无 C 组条目**。平台相关的问题在 `xhqm/DEBUG.md` 的 C 组。）

---

## D. 性能

### D1. `compute` / `template.h` 的类型工具在嵌套实例化下编译期开销随参数个数线性增长，无快速路径
- 位置：`metaclass/compute.h:27-32`（`sum` 递归）、`:41-46`（`dot` 递归）、`metaclass/template.h` 的 `docker` / `hyper` 递归嵌套
- 现状：全部用「一次剥一个元素」的递归模板实现；`docker<type, dim, save_type>` / `hyper<type, dims...>` 每加一维就多一层嵌套类型。当前仓内最大的实例化规模很小（`test/metaclass_test.cpp` 与 `fantasy` 用量有限），所以**没有实测到编译变慢**。
- 证据：`sed -n '27,46p' metaclass/compute.h`；本节点未做编译耗时基准。
- 影响：潜在项 —— 一旦有人拿它算几十个元素的 `sum`/`dot`（编译期常量折叠场景），模板实例化深度与编译时间会明显上升，且没有 `constexpr` 折叠的快速路径。
- 复现/验证：未复现（无基准）。
- 建议：若出现编译期瓶颈，改用折叠表达式（C++17 `(... + vals)`）替代递归模板。

---

## E. 未实现 / 占位桩

### E1. `thread/` 是空目录（预留，零实现）
- 位置：`metaclass/thread/`
- 现状：目录存在但**没有任何文件**（`ls -la metaclass/thread/` 只有 `.` 与 `..`）。目录本身未入库（`svn ls metaclass/` 里没有 `thread`）。
- 证据：`find metaclass/thread -type f` → 0；`svn ls metaclass/` → `block.h buffer.h cache.h compute.h function/ resource.h template.h tuple_values.h`。
- 影响：想找「metaclass 的线程工具」的人会扑空；实际线程池在 `software/system/threads/`。
- 复现/验证：`ls -la metaclass/thread`。
- 建议：要么删掉空目录，要么在 README 里把它明确标成「预留、未入库」。

### E2. `compute` / `template.h` / `resource` / `function/`（反射全套）**无任何单元测试**
- 位置：`test/metaclass_test.cpp`（本模块唯一的测试文件）
- 现状：该文件只有 4 个 `TEST_CASE`：`test_block` / `test_buffer` / `test_cache` / `test_buffer_dim`（`grep -n TEST_CASE test/metaclass_test.cpp` → 4 条）。`compute.h`、`template.h`、`resource.h`、`function/`（`class_type.h` / `function.h` / `function_type.h` / `function_call.h` / `symbols_has.h`）**0 覆盖**。
- 证据：`grep -c "function_type\|symbols_has\|class_type\|ret_function_args\|select_if\|docker\|hyper\|compute<" test/metaclass_test.cpp` → 0。
- 影响：本模块是全工程最底层（被 `algebra`/`lattice`/`fantasy`/`network`/`software` 复用），却只有 4 个容器的冒烟测试；A1/A2/A6 这类缺陷能长期留下正是因为没有测试面。
- 复现/验证：`grep -n TEST_CASE test/metaclass_test.cpp`。
- 建议：按「一个头一个 TEST_CASE」补 `compute` / `template` / `resource` / `function/` 的冒烟断言（注意 A1、A6 现在就会失败，正好当回归靶）。

### E3. 无统一聚合头，使用方必须逐个 include
- 位置：`metaclass/`（无 `metaclass.h`）
- 现状：模块内没有聚合头；门面 `xhqm/metaclass` **不存在**（`svn ls -r 677 xhqm/ | grep -c metaclass` → 0），只有 `xhqm/block`、`xhqm/buffer`、`xhqm/cache`、`xhqm/compute`、`xhqm/template`、`xhqm/tuple_values` 等**单个头**的门面，且 `xhqm/function` 对应的是 `metaclass/function/` 子目录里若干个头拼起来的聚合。
- 证据：`ls xhqm/ | grep -v '\.md$'`（22 条里无 `metaclass`）；`grep -rl "metaclass/" --include=*.h . | head`。
- 影响：新用的人要先搞清楚「哪个头里有我要的东西」；README 的目录树是当前唯一索引。
- 复现/验证：`ls xhqm/ | grep metaclass` → 空。
- 建议：补 `metaclass/metaclass.h`（或 `xhqm/metaclass`）与 `xhqm/metaclass` 门面，风格对齐 `xhqm/lattice`。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 本模块不提供序列化：容器只给连续指针视图，格式交给上层
- 位置：`block::value_ptr` / `buffer::data` / `buffer_dim::data()`
- 现状与理由：本模块的定位是「编译期元编程 + 通用容器基础层」，序列化格式在 xhqm 里由各消费模块自己定（`astral` 的 JSON/二进制/mmap、`fantasy` 的 `scene_io`、`nebula` 的 `data` 通道），容器若自带格式反而会与它们冲突。只给指针视图能让 `memcpy`/GPU 上传零拷贝接入。
- 代价：调用方必须自己保证尺寸与字节序；`buffer_dim` 的维度信息（`dimensions()`）不与数据一起落盘，反序列化时得自己记。
- 状态：**不修**（属有意设计）。

### F2. `function<void>` 走「手动类型擦除」而不是 `std::function` / 虚基类
- 位置：`metaclass/function/function_type.h`
- 现状与理由：这个容器要支持**运行时才知道签名**的场景（`up_function<func_type>` / `call<func_type>`），比 `std::function` 多一层「签名可延迟到调用点」的能力；引擎里没有异常与 RTTI 依赖（`head.h` 的 `xhqm_io` 导出宏 + 跨 DLL 边界也回避了 `std::function` 的 ABI 不确定性）。
- 代价：`new`/`delete` 手管（见 A4 的泄漏与无类型标记）、错误签名不报错。
- 状态：**结构不修**（有意），但 A4 的「析构不释放」是明确缺陷，应修。

### F3. `resource` 的默认 ID 只有推导路径，不写进模板参数
- 位置：`metaclass/resource.h:12-16`
- 现状与理由：想让 `resource(a, b)` 这种写法直接推出 `{type, id}`，所以走推导指引而不是给模板参数默认值（给默认值就变成「两参时可省」而不是「单参可推」）。
- 代价：单参写法推出不来（A6）。
- 状态：**口径不修**，但 A6 的可用性问题应修（补 `id = int` 的默认值不会破坏两参推导）。

---

## G. 文档与代码的出入

### G1. README 用法示例的 include 路径不存在
- 位置：`metaclass/README.md`（本次改前，`## 简要用法示例` 段）
- 现状：旧示例自 `#include "xhqm/metaclass/template.h"` 起，而 `xhqm/` 下放的是**无扩展名的门面文件**，没有 `xhqm/metaclass/` 这个目录（`metaclass` 这一项在 `xhqm/` 里**根本不存在**）。照抄会直接 `No such file or directory`。
- 证据：`svn ls -r 677 xhqm/ | grep metaclass` → 0；`ls xhqm/ | grep -v '\.md$'` → 22 条，无 `metaclass`。
- 影响：新接手的人第一个示例就编不过。
- 复现/验证：`printf '#include "xhqm/metaclass/template.h"\nint main(){}\n' | g++ -x c++ -std=c++17 -I. -fsyntax-only -` → fatal error。
- 建议：本次已在 README 里订正为真实路径（`metaclass/*.h`）；门面缺失本身见 E3。

### G2. README 旧「已知问题」两条与代码不符（本次已订正）
- 位置：`metaclass/README.md`（本次改前的 `## 已知问题 / 待补`）
- 现状：① 「`head.cpp` 引用 `./metaclass/struct/values.h`，该文件不存在（悬空引用）」—— 引用在注释块内，与编译无关（A5）；② 「`function/` 反射子模块尚无单元测试」的字面成立，但同段没写「`compute`/`template`/`resource` 同样零覆盖」，读者会以为只有反射缺测试（E2）。
- 证据：见 A5 与 E2 的复现命令。
- 影响：旧文把「注释里的伪代码」当成活代码，会误导修复方向。
- 复现/验证：同上。
- 建议：已在本次 README 订正，并把问题清单指向本文件。

### G3. `lattice/struct.h` 声明依赖本模块 `compute`，而 `xhqm/lattice` 门面不含 `metaclass` 的任何头 ⇒ 该依赖当前不可达
- 位置：`lattice/struct.h`（`#include "../metaclass/compute.h"`）与门面 `xhqm/lattice`（16 条 include，均为 `lattice/*`）
- 现状：`struct.h` 确实引用 `compute.h`（`grep -c compute.h lattice/struct.h` → 1），但没有任何门面把两者串起来，也没有 `.cpp` 走这条路径；本节点未构造「先 include `xhqm/lattice` 再编出 error」的最小用例，故此条标「未确认」。
- 证据：`grep -rn "metaclass/compute.h" --include=*.h .` → 只命中 `lattice/struct.h`。
- 影响：暂无可观察后果。
- 复现/验证：未确认。
- 建议：无需处理，留档。
