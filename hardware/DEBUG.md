# hardware —— 当前仍存在的问题

`hardware`（设备选择 `hardware.h` / 音视频抽象 `media.h` / 并行骨架 `parallel/`）在 r677 工作副本下**仍然存在**的缺陷、缺口与取舍台账。用法见同目录 `README.md`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
> 与同类文件的关系：本文件与仓库根 `README.md:125` 指向的根 `DEBUG.md`、各模块自己的 `README.md` / `DEBUG.md` 是同一体系；`fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` 是历史台账，**不搬家不改名**，本文只在 G 组指向它们。别处的历史结论（「dispatch_no.h 根本编译不过」）仍残留在 `test/ffbo/VDTOR_CANDIDATES_VERDICT.md`，见 G1。

本模块规模（实测 `wc -l`）：`hardware.h` 126 行、`media.h` 260 行、`parallel/dispatch_no.h` 164 行、`parallel/dispatch_cpu.h` 91 行；4 个文件，**无 .cpp/.cu/.mm**，无第三方依赖。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `dispatch<type, equip≠no>::compute(std::function)` 直接编译失败
- 位置：`hardware/parallel/dispatch_no.h:149`（另见 `:153`、`:118`、`:109`）
- 现状：`compute()` 内部固定构造 `task_function<type>`（它是 `task<type, equipment::no>` 的派生），再经 `execute<type, equip>::exec` 把 `array<type, equip>` 交给 `task_function::call`，而后者形参是 `array<type, equipment::no>&` ⇒ `equip != no` 时类型不匹配。
- 证据：`:109` `taskobj.call(values);`；`:118` `virtual void call(...array<type, equipment::no>& values) override`；`:151` `task_function<type> taskobj;`。g++ 11.4 `-std=c++17 -fsyntax-only` 实例化 `dispatch<int, equipment::cpu>` 报 `error: cannot convert 'xhqm::parallel::array<int, xhqm::parallel::cpu>' to 'xhqm::parallel::array<int, xhqm::parallel::no>&'`（出口 `dispatch_no.h:109`）。
- 影响：任何按 `equipment::cpu/gpu/net` 使用「便利接口」`compute(std::function)` 的人拿到的是编译错误；也说明 `dispatch_cpu.h` 不是「取消注释就能用」。
- 复现/验证：`cd hardware/parallel && printf '#include "dispatch_no.h"\nint main(){ xhqm::parallel::array<int, xhqm::parallel::equipment::cpu> a(4); xhqm::parallel::dispatch<int, xhqm::parallel::equipment::cpu> d; d.compute([](int&v){v+=1;}, a, true); }\n' | g++ -x c++ -std=c++17 -fsyntax-only -` —— 本节点实测：编译失败（exit 1）。
- 建议：`compute()` 按 `equip` 生成对应任务，或把 `task_function::call` / `exec` 的形参放宽为 `array<type, equip>`。

### A2. `over()` 用 `==` 比较、游标又是 public：游标越界后死循环 + 越界访问
- 位置：`hardware/parallel/dispatch_no.h:85-88`（`array::over`）、`:68-71`（`share::over`）；游标定义 `:44-47`
- 现状：`over()` 返回 `m_index == std::vector<type>::size()`；`_array::m_index` 是 public 成员，`exec` 的循环条件就是 `!values.over()`，循环体自增游标后用 `values[m_index]`（`std::vector::operator[]`，无边界检查）。
- 证据：`:87` `return (m_index == std::vector<type>::size());`；`:104-109` 循环体；`:121` `m_function(values[xhqm::parallel::task<type, xhqm::parallel::equipment::no>::m_index]);`
- 影响：只要有人把 `m_index` 写成大于 `size()` 的值（public 可直接写；或跨实例复用时的残留），`over()` 永远为 false ⇒ 循环不终止且逐次越界读写。属静态推导，未运行。
- 复现/验证：未复现（本节点不落临时文件；需运行期观察 UB）。
- 建议：改为 `m_index >= size()`，并把 `m_index` 收回私有 + 提供 `reset()`。

### A3. `caps` 的 `medias` json 往返把 `media_type::none` 变成 `video`
- 位置：`hardware/media.h:169`（写）、`hardware/media.h:190`（读）
- 现状：写侧 `(m == media_type::audio) ? "audio" : "video"`，非 audio 一律写 `"video"`；读侧 `(m == "audio") ? audio : video`，非 `"audio"` 一律读回 `video`。`media_type::none = 0`（`:57-62`）是合法枚举值，往返后被改成 1。
- 证据：`:169` 与 `:190` 两行互为逆运算但都不是双射；`media.h:57-62` 定义 `none=0, audio=1, video=2`。
- 影响：任何把 `media_type::none` 放进 `medias` 的发布方/订阅方在 json 往返后拿到错误的媒体类型。当前仓库内**没有**这样的调用点（`test/nebula_api/nebula_api_test.cpp:281`、`test/nebula_api/client/device_impl.h:171` 只写 video/audio），属潜伏缺陷。
- 复现/验证：`xhqm::hardware::media::caps c; c.medias = { media_type::none }; caps c2; c2.from_json(c.to_json()); // c2.medias[0] == media_type::video`（静态可判；本节点未运行）。
- 建议：写侧对 `none` 输出 `"none"` 或跳过，读侧未知串保持 `none`。

### A4. `caps::from_json` 不把 `none`/未知 `kind`、`dir` 写回
- 位置：`hardware/media.h:182-187`
- 现状：`kt`/`dt` 只在 `i = 1..7`（kind）/ `1..3`（dir）范围内匹配，`"none"` 与任何未知串都不命中，于是保留调用者对象里的**旧值**，函数仍返回 `true`。
- 证据：`:184-187` 两个 for 循环；`kind::none = 0`（`:39`）与 `dir::none = 0`（`:51`）不在循环范围内。
- 影响：`from_json({"kind":"none"})` 之后 `c.k` 仍是上一次的值（不是 `none`），使用方静默拿到与 json 不一致的能力描述；老对端不发键、或收到新类别串时最容易踩。
- 复现/验证：`media::caps c; c.k = media::kind::cam; c.from_json(media::json{{"kind","none"}}); // c.k 仍为 cam`（静态可判；本节点未运行）。
- 建议：显式处理 `"none"`；无法识别时把 `k`/`d` 置 `none` 或返回 `false`。

---

## B. 资源与并发

### B1. `dispatch` 异步路径只持有 `std::ref(values)`：`array` 先析构即悬垂
- 位置：`hardware/parallel/dispatch_no.h:146`（`std::ref(values)`）、`:133`（析构 join）、`:156-159`（`wite()`）
- 现状：`wait=false` 时线程捕获 `std::ref(m_execute)` 与 `std::ref(values)`；`dispatch` 只在自身析构、下一次 `compute_task`、或显式 `wite()` 时 join。线程存活期间 `values` 的生命周期没有任何保证（`array` 的拷贝/移动已删除，也没有 shared_ptr 兜底）。
- 证据：`:146` `m_thread = std::thread(&dispatch::static_execute<task_type>, std::ref(m_execute), taskobj, std::ref(values));`；`:133` `virtual ~dispatch() { if (m_thread.joinable()) m_thread.join(); }`
- 影响：只要 `dispatch` 比 `array` 先声明（`dispatch<int> d; array<int> a(n); d.compute(f, a, false);`）——作用域收尾时 `a` 先销毁、`d` 的析构随后才 join —— 工作线程就是在已销毁的 `array` 上跑，未定义行为（数据竞争/崩溃）。
- 复现/验证：未复现（需运行期竞态；可在夹具里把 `array` 放进内层作用域制造）。
- 建议：头里写明「`array` 必须先于 `dispatch` 定义」，或让 `dispatch` 持有所需数据的所有权。

### B2. `hardware::active_device` 的读写没有同步
- 位置：`hardware.h:102-108`
- 现状：`active_device_ref()` 内是函数级 `static device active = compiled_device();`，读（`:107`）与写（`:108`）都直接落在这个普通静态变量上；README 里「天然线程安全」只对**初始化**成立。
- 证据：`:104` `static device active = compiled_device();`；`:107-108` `inline device active_device(void)` / `inline void active_device(const device&)`。
- 影响：一个线程切换设备、另一个线程同时创建后端（`algebra/tensor/backend/tensor_backend.cpp:66`、`fantasy/backend/rhi_no.cpp:167` 都读它）则是数据竞争；即便不撕裂，也存在「读到旧值还是新值」的不确定时序。
- 复现/验证：未复现（需并发切换 + 建后端）。
- 建议：改成 `std::atomic<device>`（底层就是 `std::uint32_t`，开销可忽略）。

### B3. 守卫宏 `XHQM_PARALLEL` 过泛：撞名即整文件静默失效
- 位置：`hardware/parallel/dispatch_no.h:1-2`
- 现状：整个并行骨架用 `#ifndef XHQM_PARALLEL / #define XHQM_PARALLEL` 做守卫；同模块 `dispatch_cpu.h:2` 用 `#pragma once`，`hardware.h:1` / `media.h:1` 用文件级专属宏（`XHQM_HARDWARE` / `XHQM_HARDWARE_MEDIA`），只有这一处用了命名空间级的泛宏。
- 证据：`grep -rn "XHQM_PARALLEL" --include=*.h --include=*.cpp .` 目前只命中 `dispatch_no.h:1-2`（`XHQM_PARALLEL_THRESHOLD` 是另一个名字），**当前未触发**；但同一命名空间下还有 `software/system/threads/threads_pool.h`、`threads_task.h`、`process/process_base.h` 三份并存的头。
- 影响：日后任何一个并行层头用了同一个 `XHQM_PARALLEL`，先被包含的一方会**静默丢掉**另一方的全部定义，报错点会跑到很远的使用处（这类「静默丢弃」在本仓没有构建期校验）。
- 复现/验证：`grep -rn "#define XHQM_PARALLEL\b" .`（仅一处）。
- 建议：改名 `XHQM_PARALLEL_DISPATCH_NO`，并统一为 include guard 或 `#pragma once` 一种。

### B4. `exec` 先推进游标再执行任务：任务抛异常时该元素被静默跳过
- 位置：`hardware/parallel/dispatch_no.h:106-109`
- 现状：循环体顺序是「写 id → 写 index → `values.m_index += 1` → `taskobj.call(values)`」，自增在调用**之前**且异常时不回滚；异常向上传播后，调用者若复用同一个 `array`，该下标的元素已被视为处理过。
- 证据：`:108` `values.m_index += 1;` 在 `:109` `taskobj.call(values);` 之前。
- 影响：任何让任务抛异常（或中途放弃）的调用方，重试时会**静默少算一个元素**——不报错，只是结果不对。
- 复现/验证：未复现（可在夹具里用一个必定 `throw` 的 `task` 复现）。
- 建议：`call` 之后再自增游标，或异常时回退 `m_index`。

---

## C. 跨平台

### C1. `dispatch_no.h` 靠 `head.h` 的传递包含拿 `std::vector` / `std::forward`
- 位置：`hardware/parallel/dispatch_no.h:3-11`（包含表）、`:58`/`:75`（`std::vector` 基类）、`:63`/`:80`（`std::forward`）
- 现状：本文件使用 `std::vector` 与 `std::forward`，但只包含 `../../head.h` 与 `<atomic> <queue> <mutex> <thread> <optional> <functional> <shared_mutex>`；`<vector>` 来自 `head.h:36`，`<utility>` 同样靠传递。顺带：`<queue>`/`<optional>`/`<shared_mutex>` 本文件并未使用。
- 证据：`grep -n "#include" hardware/parallel/dispatch_no.h` 无 `<vector>`/`<utility>`；本仓已有同类事故——`svn log hardware/parallel/dispatch_no.h` 的 r557 记录：「dispatch_no.h（r553 修可编可实例化时漏）：`std::atomic<bool>` 无头，gcc 下 incomplete type 编译失败；补 `#include <atomic>`」。`media.h:22` 也是同类教训的产物（显式包含 `../algebra/itype.h` 并注明「独立编译必需」）。
- 影响：MSVC 的传递包含比 libstdc++ 宽（r557 注释明说），换编译器、或 `head.h` 的包含表一改就碎。
- 复现/验证：`grep -n "#include" hardware/parallel/dispatch_no.h`；把 `head.h:36` 的 `<vector>` 注释掉即可复现失败（本节点未做该破坏性实验）。
- 建议：按「用了什么就包含什么」补 `<vector>`、`<utility>`，并清掉未使用的包含。

### C2. `media::hotplug` 只有 Linux 实现，且实现体在 test/ 下被无条件包含
- 位置：`hardware/media.h:239-255`（抽象与平台矩阵注释）；实现 `test/nebula_api/client/linux_hotplug.h:19`（`#include <sys/inotify.h>`）、`test/nebula_api/client/device_client.h:17`（无条件 `#include "linux_hotplug.h"`）
- 现状：`media.h:240` 写明平台实现「Linux=inotify/libudev、Windows=WM_DEVICECHANGE、macOS=IOKit」，但仓库里只有 inotify 一份；`test/nebula_api/client/` 下没有任何 `_WIN32` / `__APPLE__` 分支。
- 证据：`linux_hotplug.h:19` 的 POSIX 头；`device_client.h:17` 无守卫；`grep -rn "_WIN32\|__APPLE__" test/nebula_api/client/*.h` 无结果。
- 影响：`nebula_api` 客户端目前在 Windows/macOS 编译不过（问题不在抽象层，而在这份唯一的平台实现）。本模块承诺的平台矩阵只落地了 Linux。
- 复现/验证：在非 Linux 上编译 `test/nebula_api/nebula_api_test.cpp`；注意该测试**没有 CMake 目标**（`grep -rn "nebula_api" test/CMakeLists.txt` 无结果），需手工编译。
- 建议：给该实现的包含与用法加平台守卫，或按平台补 stub。

---

## D. 性能

### D1. 异步任务 = 每次新建一个 `std::thread`，无池化、无复用、无并发度
- 位置：`hardware/parallel/dispatch_no.h:146`（起线程）、`:144`（下一次调用前先 join）、`:128`（每实例只有一份 `execute`）
- 现状：`compute_task(..., wait=false)` 每次都新建线程；`dispatch` 只有 `m_thread` 一个槽位，第二次调用先 join 前一个再新建 ⇒ 既不能并发多个任务，也不复用线程。
- 证据：`:144` `if (m_thread.joinable()) m_thread.join();`、`:146` 的 `std::thread(...)`；对照 `software/system/threads/threads_pool.h:100-146`（`parallel::threads` 有 cored/allow 线程数、channel、`holder` 异步结果），`hardware/parallel` 完全不引用它（`dispatch_no.h:3` 只包含 `../../head.h`）。
- 影响：每个异步任务付一次 OS 线程创建/销毁开销（未实测数字），任务粒度越小占比越高；想做「按块并行」得自己再搭一层。
- 复现/验证：未实测（本节点不落临时文件、未跑基准）。
- 建议：把并行执行委托给同命名空间的 `parallel::threads`。

---

## E. 未实现 / 占位桩

### E1. `dispatch_cpu.h` 0 行代码，且全仓库无人 include
- 位置：`hardware/parallel/dispatch_cpu.h:19-90`（被注释的全部实现）、`:1-12`（头部）
- 现状：`wc -l` 91 行里 17 行非注释——1 个 `#pragma once` + 10 个 `#include` + 4 行 namespace 开括号 + 2 行收尾括号，**没有一行代码**；全仓库没有任何文件 include 它（唯一相关引用是它自己 `:3` 的 `#include "dispatch_no.h"`），也不在任何 CMake 源列表里 ⇒ 其内容从不参与编译。
- 证据：`sed -n '19,90p' hardware/parallel/dispatch_cpu.h | grep -vc '^\s*//'` = 0；`grep -rn "dispatch_cpu" --include=*.h --include=*.cpp --include=*.txt .` 只有它自己。
- 影响：改它的内容不会被任何构建发现（不会报错也不会生效）；接手者容易以为「取消注释就有 CPU 并行」，而注释体其实只是串行骨架换了个 `equip=cpu` 默认参数（见 E3）。
- 复现/验证：`wc -l hardware/parallel/dispatch_cpu.h`（91）；非注释非空行 16 行、非注释行 17 行（本节点实测）。
- 建议：要么删文件，要么把它接进 `test/polydtor_test.cpp` 的编译面并真正实现。

### E2. `parallel/` 里只有 `no` 一份实现：`equipment::cpu/gpu/net` 没有后端
- 位置：`hardware/parallel/dispatch_no.h:18-21`（枚举定义）
- 现状：`parallel/` 下只有 `dispatch_no.h` 与全注释的 `dispatch_cpu.h`，没有 `dispatch_gpu.h`/`dispatch_net.h`；`equipment::gpu`/`equipment::net` 全仓库只出现在枚举定义处，`equipment::cpu` 只出现在 `dispatch_cpu.h` 的注释里（`:20/:33/:39/:55`）。
- 证据：`find . -name "dispatch_*"` 只有两个文件；`grep -rn "equipment::cpu\|equipment::gpu\|equipment::net"` 只命中注释。设备标签的消费方确实另有后端：张量侧 GPU 走 `algebra/tensor/backend/tensor_backend_gpu.cu`（存在）、NET 是「分布式占位，回退 CPU」（`algebra/tensor/backend/tensor_backend.cpp:51-56`）；渲染侧 no/cpu/net 一律映射为 none 桩（`fantasy/backend/rhi_no.cpp:167-172`）。缺的是**并行层自己**的 gpu/net 分发。
- 影响：`hardware::device` 的 gpu/net 在并行层没有对应实现；`device::net` 目前只是「回退到本地后端」的标签。
- 复现/验证：`find . -name "dispatch_*"; grep -rn "equipment::" --include=*.h .`
- 建议：无（等真的有 GPU/NET 并行需求再做；README 保持「未落地」口径即可）。

### E3. `equip` 模板参数不产生任何分派
- 位置：`hardware/parallel/dispatch_no.h:57`、`:74`、`:91`、`:98`、`:125`
- 现状：`array`/`share`/`task`/`execute`/`dispatch` 各只有这一份通用定义，没有针对 `equipment::cpu/gpu/net` 的显式特化，也没有 `if constexpr` 分支 ⇒ `array<double, equipment::gpu>` 与 `array<double>` 的行为逐字相同，`equip` 纯粹是类型标签。
- 证据：`grep -n "^\s*template" hardware/parallel/dispatch_no.h` 5 处全是主模板、0 处特化；`grep -rn "equipment::" hardware/` 除枚举外只命中注释。
- 影响：传 `equipment::gpu` 的调用方会以为自己拿到了 GPU/并行路径，实际是串行执行（再叠加 A1：`compute` 那个重载对 `equip != no` 还编译不过）。
- 复现/验证：`grep -n "equipment::" hardware/parallel/dispatch_no.h`；A1 的编译探针。
- 建议：至少在注释里写明「`equip` 当前不改变行为」，避免语义误导。

### E4. `share` / `_share`：共享语义未实现，`share` 与 `array` 逐行相同
- 位置：`hardware/parallel/dispatch_no.h:39-42`（`_share` 空结构）、`:57-72`（`share`）、`:74-89`（`array`）
- 现状：`share` 与 `array` 的基类、四个构造函数、`over()` 完全一致（唯一差别是 `array:83` 多一个 `;;`）；`share` 继承的是 `_array` 而非 `_share`，而 `_share` 是空结构、无成员、无人继承——没有引用计数、没有锁、没有并发访问协议。
- 证据：`:58` `struct share : public _array, public std::vector<type>`；`:65-66` 与 `:82-83` 一样删除拷贝/移动；`grep -rn "_share" hardware/` 只有定义本身；唯一使用点是 `test/polydtor_test.cpp:107`（`share<int> shared_values(3);`）。
- 影响：「共享数据」这个名字会让人以为能跨线程共享——实际它和独占版 `array` 一样删除了拷贝/移动、也没有任何同步；把 `share` 传给两个 `dispatch` 会直接踩 B1/A2。
- 复现/验证：逐行对比 `dispatch_no.h:57-72` 与 `:74-89`；`grep -rn "struct share\|_share" hardware/`
- 建议：要么实现共享语义（引用计数/读锁），要么删掉 `share`/`_share` 以免误导。

### E5. `parallel::state`、`_execute::m_id` / `_task::m_id` 是死符号
- 位置：`hardware/parallel/dispatch_no.h:23-26`（`state`）、`:34-37`（`_execute::m_id`）、`:49-53`（`_task::m_id`）
- 现状：`state`（`init/exec/over`）全仓无使用；`_execute::m_id` 只有默认值 0、从不被赋值，而 `exec` 每轮把它抄进 `taskobj.m_id` ⇒ 任务拿到的 id 恒为 0。
- 证据：`grep -rn "parallel::state" .` 除定义外无命中；`:106` `taskobj.m_id = this->m_id;` 与 `:128` `xhqm::parallel::execute<type, equip> m_execute;`（全仓无处设置 `m_execute.m_id`）。
- 影响：想用 `task::m_id` 区分任务/分块的调用方永远拿到 0；`state` 会让人以为这里真有生命周期状态机。
- 复现/验证：`grep -rn "m_execute" hardware/`；`grep -rn "\.m_id" hardware/`
- 建议：删掉，或在 `dispatch` 里给 `m_execute.m_id` 一个真实来源（计数器/UUID）。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. `hardware::device` 与 `parallel::equipment` 是两套枚举，只靠数值对齐
- 位置：`hardware/hardware.h:35-42`（`no=0,cpu=1,gpu=2,net=3,auto_=4`）与 `hardware/parallel/dispatch_no.h:18-21`（`no=0,cpu,gpu,net`）
- 现状：两套枚举独立声明、取值顺序一一对应，但没有互相转换的函数。
- 为什么现在这样是对的：`parallel/` 因此可以完全不依赖 `hardware.h`（`dispatch_no.h:3` 只包含 `head.h`），设备层与并行层各自可单独编译、单独取用——这就是 README:12 说的「骨架层 vs 设备层」分工。
- 代价：新增/调整设备类别要改两处且没有编译器保护；`auto_` 在并行层没有对应项。
- 复现/验证：`grep -n "enum" hardware/hardware.h hardware/parallel/dispatch_no.h`

### F2. 异步完成靠析构 / `wite()`，没有回调、没有 future
- 位置：`hardware/parallel/dispatch_no.h:133`（析构 join）、`:156-159`（`wite()`）
- 现状：`wait=false` 只起线程，完成时刻只能靠再调 `compute_task`、显式 `wite()` 或析构 `dispatch` 来等；没有任何完成通知。
- 为什么现在这样是对的：骨架层刻意保持最小（不引入 future/回调依赖，也不要求调用方持有 `dispatch` 的生命周期协议以外的约定）。
- 代价：调用方无从得知完成时刻，也无法在一个 dispatcher 上并发跑多个任务（`m_thread` 只有一个槽位）；配合 B1 的生命周期契约，异步用法很容易写错。
- 复现/验证：读 `dispatch_no.h:126-160`

### F3. `media.h` 只定义抽象、不做实现（D13）
- 位置：`hardware/media.h:5-16`（自述范围与「不定义」清单）、`:209-255`（纯虚接口 + `std::function` 签名）
- 现状：`device` 接口全为纯虚（仅 `stream_url()`/`status()` 有默认实现）、`device_list` 只是 `std::function<std::vector<caps>()>` 签名、`hotplug` 全纯虚；字节搬运（read/write、MediaMTX 桥接）明确划归实现层。
- 为什么现在这样是对的：平台实现（ALSA/WASAPI/AVFoundation、V4L2/DShow）与内嵌 MediaMTX 起流都随平台与部署变化，抽象层保持纯净、低功耗设备可以只实现子集。
- 代价：库内没有任何可用设备实现，每个平台/每个调用方都要各写一遍（目前唯一参考实现在 `test/nebula_api/client/`：`device_impl.h`、`linux_hotplug.h`）；C2 的平台缺口由此而来。
- 复现/验证：`grep -n "virtual" hardware/media.h`；本模块无 .cpp，`grep -rn "hardware/media.h" --include=*.cpp` 只有 test/nebula_api 下两处

### F4. 与 `software/system` 的并行设施并存、互不引用
- 位置：`hardware/parallel/dispatch_no.h:3`（只包含 `head.h`）↔ `software/system/threads/threads_pool.h:7-10`、`threads_task.h:130`、`process/process_base.h:12`
- 现状：两边同属 `xhqm::parallel` 命名空间，但 `hardware/parallel` 是调度骨架、`software/system/threads` 是线程池/进程/异步结果实现；核心构建只收后者（`CMakeLists.txt:240` 的 `XHQM_HEAD_CORE` 收 `threads_pool.h`；`hardware/parallel/*` 不在任何 CMake 列表里，只在 `test/polydtor_test.cpp:10` 的编译单元里被引入）。
- 为什么现在这样是对的：照 README:12 的分工——`hardware` 提供「更底层的设备/调度骨架」，真正的线程池/进程归 `software/system`；骨架不进核心构建也就不会给主库引入额外线程语义。
- 代价：同一命名空间里并存两套概念（`parallel::state` vs 各自的 `state` 枚举、`dispatch` vs `threads`）、两套线程策略（每任务一线程 vs 线程池），后来者容易选错；骨架事实上无人使用（唯一使用者是回归夹具）。
- 复现/验证：`grep -rn "hardware" software/`（0 命中）、`grep -rn "software" hardware/*.h hardware/parallel/*.h`（0 命中）、`grep -rn "dispatch_no" --include=*.txt --include=*.cpp .`

---

## G. 文档与代码的出入

### G1. `test/ffbo/VDTOR_CANDIDATES_VERDICT.md` 正文仍写「dispatch_no.h 根本编译不过」
- 位置：`test/ffbo/VDTOR_CANDIDATES_VERDICT.md:156-166`（§4.2 正文）、`:58`（候选表）；同一文件 `:17-18` 的处置状态表已写「r553 已清理」
- 现状：该文件自相矛盾——状态表承认 r553 已修，§4.2 正文与候选表仍留着修前的结论（「该文件无人 include、库内 0 符号，且自身编译不过」，并引 `dispatch_no.h:59:4: error: a type specifier is required`）。
- 证据：本节点实测 `cd hardware/parallel && printf '#include "dispatch_no.h"\nint main(){}\n' | g++ -x c++ -std=c++17 -fsyntax-only -Wall -Wextra -` 返回 0（无诊断）；`dispatch_no.h:60-63` 的构造函数名已是 `share`，`:95` 已无尾逗号。
- 影响：这是目前还在散播「dispatch_no.h 编译不过」的两处之一（另一处见 G2）；接手的人会重复 r553 的工作，或误以为 `test/polydtor_test.cpp:90` 的夹具是假的。
- 复现/验证：`sed -n '156,166p' test/ffbo/VDTOR_CANDIDATES_VERDICT.md`；上面的编译探针。
- 建议：本模块不越权改别的档；请该文件负责人把 §4.2 与 `:58` 更新为「r553 已修，夹具在 `test/polydtor_test.cpp`」。

### G2. 根 `README.md` §2.9 hardware 的劣势/建议仍是 r553 之前的口径
- 位置：`README.md:190`（劣势）、`README.md:192`（建议）；另见 `README.md:108` 成熟度表
- 现状：`:190` 仍把「share 构造名 array、task::call 尾逗号、delete 双分号」三项并列为未完工笔误。前两项已被 r553 修掉（`dispatch_no.h:60-63`、`:95`，实测编译通过）；只有 `array` 移动构造的 `;;` 仍在（`:83`，类作用域的空声明，无编译/行为影响，属笔误而非缺陷）。`:192` 的「修复 dispatch_no.h 笔误」已无需再做。
- 证据：`sed -n '186,192p' README.md`；本节点对 `dispatch_no.h` 的实测编译与逐行核对（`grep -rn ";;" hardware/parallel/` 只有 `:83`）。
- 影响：上游规划会把已完成的清理项重复计入。
- 复现/验证：`grep -n "dispatch_no" README.md`
- 建议：由根 README 的负责人同步（本次只改 `hardware/README.md`）。

### G3. `test/README.md` 的硬件行只记了 `hardware_test`
- 位置：`test/README.md:56`
- 现状：测试清单里 hardware 只有一行 `hardware_test | 设备选择（no/cpu/gpu/auto，header-only）`；实际还有 `polydtor_test`（`test/polydtor_test.cpp:90-129` 的 `test_parallel_dispatch_no` 覆盖 `dispatch_no.h`：虚析构断言、`array` + 同步 `compute`、`share` 实例化、经 `task*` 多态释放）与 `nebula_api_test`（`:271` caps 往返、`:576` 热插拔，覆盖 `media.h`），后者还没有 CMake 目标。
- 证据：`grep -n "hardware\|polydtor" test/README.md`；`grep -n "add_executable(polydtor_test\|add_executable(hardware_test" test/CMakeLists.txt` = `:156` / `:186`；`grep -rn "nebula_api" test/CMakeLists.txt`（无结果）。
- 影响：看覆盖面表的人会以为 `media.h` 与 `parallel/` 两半都没有夹具。
- 复现/验证：见上两条 grep。
- 建议：由 test/ 的负责人补两行（本模块只改 `hardware/README.md`）。

### G4. `hardware.h:9` 的「CPU 内部自动降级 blas → simd → thread」在本模块没有对应实现
- 位置：`hardware/hardware.h:9`（源码注释；`hardware/README.md` 里同一句已在本次复核中补了说明，这里是源码侧残余）
- 现状：`hardware.h` 只产出设备标签，降级链实际在别处——张量侧由 `XHQM_TENSOR_BACKEND` 与 `TENSOR_WITH_CPU/BLAS/SIMD` 决定（`CMakeLists.txt:118-150`、`algebra/tensor/backend/tensor_backend.cpp:45-50`），渲染侧则把 `no/cpu/net` 一律映射成 none 桩（`fantasy/backend/rhi_no.cpp:167-172`）。
- 证据：`grep -rn "blas\|simd" hardware/` 只命中注释，模块内无任何降级代码；本模块无 .cpp。
- 影响：只读 hardware 的人会去找不存在的降级实现，或误以为 `device::cpu` 自带 BLAS/SIMD。
- 复现/验证：`grep -rn "blas\|simd" hardware/*.h hardware/parallel/*.h`
- 建议：源码注释属别处，本模块不改源码；`hardware/README.md` 已补「该链由 algebra/tensor 实现，本层只标设备」的说明。
