# xhqm 核心层 —— 当前仍存在的问题

`xhqm`（核心门面层：`head.h` / `xhqm.h` / 22 个无扩展名门面头 / `head.cpp`，以及它的两条构建路径 `CMakeLists.txt` 与 `xhqm.vcxproj`）在 r677 工作副本下**仍然存在**的缺陷、缺口与取舍台账。用法与 API 见同目录 `README.md`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。

> **与同类文件的关系**：本仓的台账**不搬家、不改名**。同批新建的根 `DEBUG.md`（`root`，模块路径 `.`）已在 A2/A3、E6、F5、G2 记了与核心层相邻的条目，本文件**不复制**它们，只做交叉引用；`hardware/DEBUG.md`、`lattice/DEBUG.md`、`algebra/geometry/brep/DEBUG.md` 是各模块自己的台账；`fantasy/fantasy_bugs.md`（静态分析 + 历轮修复记录）与 `network/nebula/DEFERRED.md`（讨论过但决定不做）是历史台账。仓库根 `通读报告-2026-10-07.md` 含本模块的旧表述来源。本文件与根 `DEBUG.md` A2/A3 的**分歧**已在下面 E1、A4 写明——那是「同一现象、两种归因」的场合，请以带复现命令的版本为准。

> 核对环境：g++ 11.4（Ubuntu 22.04）/ `-std=c++17` / 工作副本 r677；include 路径 `-I.`、`-I../depend/nlohmann`（fantasy 相关再加 `CMakeLists.txt:82-93` 那组目录）。所有「实测」都是本节点跑过的，命令原样给出；未跑的一律写「未复现」。复现程序一律走 stdin（`printf … | g++ -x c++ -`）或仓库外路径，**不落仓**。

本模块规模（实测 `wc -l`）：`head.h` 279 行、`xhqm.h` 81 行、`head.cpp` 180 行、22 个门面头合计 131 行（113 条 `#include`）、`README.md` 298 行（本次改后）；构建面 `CMakeLists.txt` 721 行、`xhqm.vcxproj`（98 个源文件）、`xhqm.sln`（1 个工程）。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `xhqm/lattice` 门面不自足：`queues.h`/`stacks.h` 用 `xhqm::size` 却不含 `head.h`
- 位置：`lattice/queues.h:19`、`lattice/stacks.h:19`（两处都是 `inline xhqm::size size()`），门面 `xhqm/lattice` 的前两条 include 就是这两个头
- 现状：这两个头只用 `<mutex>/<queue>/<functional>/<utility>`，**没有** `#include "../../head.h"`，而 `xhqm::size` 是 `head.h:219` 定义的别名。`xhqm/lattice` 把 `queues.h`/`stacks.h` 放在最前，于是直接包含该门面时 `xhqm` 命名空间里还没有 `size`。
- 证据：`grep -c "head.h" lattice/queues.h lattice/stacks.h` → 0 / 0；`grep -n "xhqm::size" lattice/queues.h` → `:19`（`stacks.h` 同）。同族对照：`lattice/struct.h` 有 `head.h`（`grep -c` = 1），所以与它同目录的写法并不统一。
- 影响：**直接 `#include "xhqm/lattice"` 编不过（2 条 error）**；经 `xhqm.h`（它先包含 `head.h`，`xhqm.h:33`）或 `#define XHQM_USE_LATTICE` 则没事——也就是说，门面作为「单一包含点」这条契约在 lattice 上不成立，只有走聚合头才成立。
- 复现/验证：`printf '#include "xhqm/lattice"\nint main(){return 0;}\n' | g++ -x c++ -std=c++17 -fsyntax-only -I. -I../depend/nlohmann -` → 实测 2 条 error（`queues.h:19`、`stacks.h:19`）；换成 `#define XHQM_USE_LATTICE` + `#include "xhqm.h"` → 实测 0 error。
- 建议：在这两个头里补 `#include "../../head.h"`（与 `struct.h` 对齐），或把门面里 `struct.h` 提到最前（后者只是掩盖，不推荐）。

### A2. `xhqm/event` 门面不自足：`software/event.h` 用 `xhqm_io` 宏却不含 `head.h`
- 位置：`software/event.h:159`（`struct xhqm_io event`）、级联错误 `:161-165`；门面 `xhqm/event`
- 现状：`software/event.h` 的 include 区只有 guard，没有 `head.h`，而 `xhqm_io` 宏定义在 `head.h:27-31`。宏未定义时 `struct xhqm_io event { … }` 被解析成「声明结构体 `xhqm_io` + 定义该类型的变量 `event` 并带初始化列表」，于是 `:161` 的成员声明 `event_type type = event_type::no;` 变成非法初始化式，一连串错误。
- 证据：`grep -c "head.h" software/event.h` → 0；同目录 `signal.h`/`packet.h` 有（`grep -c` = 1）。**`xhqm/software` 同样包含 `event.h`（其第 3 条 include）却编得过**，因为它的第 1 条 include 是 `software/datum.h` → `algebra/value.h:3` 的 `#include "../head.h"` 把宏先带进来了——缺陷被包含顺序掩盖。
- 影响：**直接 `#include "xhqm/event"` 编不过（8 条 error）**；顺带说明「门面顺序敏感」：往 `xhqm/software` 前面插一个不含 `head.h` 的头就会连带炸掉 event。
- 复现/验证：`printf '#include "xhqm/event"\nint main(){return 0;}\n' | g++ -x c++ -std=c++17 -fsyntax-only -I. -Isoftware/ -Ihardware/ -I../depend/nlohmann -` → 实测 8 条 error（首条 `software/event.h:159:24: error: variable 'xhqm::xhqm_io xhqm::event' has initializer but incomplete type`）。
- 建议：`software/event.h` 补 `#include "../../head.h"`。

### A3. `xhqm_rount` 对负数舍入错（`(int)(x + 0.5f)` 只对非负数成立）
- 位置：`head.h:192`（`#define xhqm_rount(_val) ((int)((_val) + 0.5f))`）
- 现状：经典的「正偏移后截断」写法：对 `-1.5` 得 `(int)(-1.0f) = -1`（就近舍入应为 `-2`），对 `-1.6` 得 `-1`（应为 `-2`），且先用 `float` 加法对 `|x|` 较大时还会先丢精度再截断。同批宏里 `xhqm_equal/xhqm_preci` 是「带精度比较」而非取整，没有同类问题。
- 证据：`grep -rn "xhqm_rount" --include=*.h --include=*.hpp --include=*.cpp .` → **只有定义处 1 条命中**，全仓无调用点；`head.h:188-194` 是这组宏函数的原文。
- 影响：属「会算错」，但当前**无人调用**，所以是潜伏项：任何按「四舍五入」使用它的新代码（尤其是坐标/像素/时间戳这类会出现负值的量）会静默偏 1；README `§三.4` 把它列为公开宏函数，读者会以为它可用。
- 复现/验证：未运行（无调用点）。静态可判：`xhqm_rount(-1.5)` 展开为 `(int)(-1.5 + 0.5f) == -1`。若要用，一行 `printf` 探针即可（`printf '#include "head.h"\nint main(){printf("%d\n", xhqm_rount(-1.5));}\n' | g++ -x c++ -std=c++17 -I. -I../depend/nlohmann -o /tmp/r - && /tmp/r`，预期打印 `-1`）。
- 建议：改用 `std::lround`（或 `(x) < 0 ? (int)((x) - 0.5f) : (int)((x) + 0.5f)`），并在 README 标明语义是「就近、半程向上」。

### A4. `test/polydtor_test.cpp` 仍停在 A 世代 nebula API：20 条编译错误（链接缺口是第二层）
- 位置：`test/polydtor_test.cpp:27`（`addr_b(nebula::kind::none)`）、`:37-46`（6 个 `override`）、`:54`（`nebula::addr_t<nebula::kind::ipv4>`）；目标定义 `test/CMakeLists.txt:156-172`
- 现状：夹具里用到的名字在当前代码里**一个都不存在**——`nebula::kind` 只有 `no/uuid/ipv4/ipv6/serial/bluetooth/nearlink/quantum`（`network/nebula/addr/addr_b.h:14-24`，**没有 `none`**，也没有别名）；当前 `addr_b` 的纯虚接口是 `opened/closed/send_broad/send_frame`（`addr/addr_b.h:55-59`），而夹具 override 的是 `to_json/from_json/equal_other/clone/broadcast/send`；`addr_t` 这个模板全仓只有本文件与 `test/nebula_api` 引用，**没有任何定义**。此外该目标的源列表里 nebula 侧只有 `../network/nebula/addr.cpp`，缺 `addr/` 下 5 个 `.cpp`（`addr_b/addr_uuid/addr_ipv4/addr_serial/addr_ipv6`），所以即使改好调用，链接期也会缺 `addr_b` 等的符号。
- 证据：实测（GCC 11.4）`cd test && g++ -std=c++17 -fsyntax-only -I. -I.. -I../depend/nlohmann polydtor_test.cpp` → **20 条 `error:`，落在 12 个不同行**（`27、37、38、39、40、43、45、46、54、59、67、75`），首条为 `:37:30: error: 'xhqm::nebula::json {anonymous}::tracked_addr::to_json() const' marked 'override', but does not override`；`:54` 报 `'addr_t' is not a member of 'xhqm::nebula'; did you mean 'addr_b'?`。缺源文件：`sed -n '156,172p' test/CMakeLists.txt` 列出的 15 条里 nebula 侧只有 `:158` 的 `../network/nebula/addr.cpp`。
- 影响：该目标**编译期就失败**（不是「链接期才缺符号」——根 `DEBUG.md` A3 记的是链接侧缺口，那一层也真实存在，但先撞的是编译期）。因为仓库**无 ctest / 无 enable_testing / 无 subdirectory 收编**（`grep -n "add_subdirectory\|enable_testing\|add_test" CMakeLists.txt` → 0 命中），没人跑就没人报。
- 复现/验证：见上（`-fsyntax-only` 即可复现 20 条）；链接侧：`cmake -S test -B /tmp/tbuild && cmake --build /tmp/tbuild --target polydtor_test`。
- 建议：把夹具迁到 B 世代（`addr_b` 的 5 个纯虚 + `kind::no`），或先在 `test/CMakeLists.txt` 里显式标禁用并写明原因。

### A5. `test/nebula_api/nebula_api_test.cpp` 也是 A 世代：303 条编译错误落在 100 行，且该目录没有构建入口
- 位置：`test/nebula_api/nebula_api_test.cpp:28`（`data(std::shared_ptr<nebula::link_b>)`）、`:39`（`addr_t<kind::ipv4>`）、`:58/111/243/448/457/466/586/592`（`link_t<kind::ipv4>`）等 100 行
- 现状：该文件头部自述「nebula **B 世代**独立验证（黑盒视角重建）」（`:1`），实际用到的 `link_b`、`link_t<kind>`、`addr_t<kind>` 在当前代码里都不存在：`data` 现在是 `class data : public node`，构造函数是 `explicit data(const text& path = "")`（`network/nebula/data.h:12/45`）；`link` 是不带模板参数的类 `class link : public life`（`link.h:16`）；`addr` 是 `using addr = std::shared_ptr<addr_b>`（`addr.h:14`）。文件里 `degree/discover/region_size/adopt/aggregate/publish/query/policy/id/snapshot/upload/heart/stop` 也都不在 B 世代的 `data/zero` 上。
- 证据：实测（GCC 11.4）`cd test/nebula_api && g++ -std=c++17 -fsyntax-only -I.. -I../.. -I../../depend/nlohmann nebula_api_test.cpp` → **303 条 `error:`，落在 100 个不同行**；首条 `:28:58: error: 'link_b' is not a member of 'xhqm::nebula'; did you mean 'link'?`。另外该目录**没有 CMakeLists.txt**（`find test/nebula_api -name CMakeLists.txt` → 空），`test/CMakeLists.txt` 里也没有它（`grep -n nebula test/CMakeLists.txt` 只有 polydtor 那 3 行），全仓无任何目标引用它。
- 影响：这是一份**不可编译、也不可执行**的 239 行（`:1-639`）自述「B 世代验证」的靶子；接手者若信它的注释，会以为 B 世代 API 长这样。它同时是 README `§八` 那条「仍是 A 世代」结论的另一半证据（那条结论成立，但旧文举的例子不成立，见 G2）。
- 复现/验证：见上；另 `grep -rn "nebula_api" --include=*.txt --include=*.vcxproj .` → 0 命中。
- 建议：明确它的定位——要么把 8 处 `link_t`/1 处 `addr_t` 与 `link_b` 迁到 B 世代并补 `test/CMakeLists.txt` 目标，要么在文件头写「已废弃，仅存档」并禁止它冒充验证靶。

---

## B. 资源与并发

### B1. 「运行时激活设备」是未同步的函数级 `static`，读写之间有数据竞争
- 位置：`hardware/hardware.h:102-108`（`active_device_ref()` 内的 `static device active = compiled_device();`，`active_device()` 读、`active_device(device)` 写）
- 现状：这是一个**可变的进程级全局**（`inline` 函数 → C++17 保证跨 TU 唯一），读写都是裸访问，没有 `std::atomic`、没有锁；C++11 起只保证它**初始化**那一次是线程安全的，之后的读/写不构成同步。而它被真实代码路径读取：张量后端创建时 `algebra/tensor/backend/tensor_backend.cpp:66`、`backend.h:29-31` 的转发、渲染 `fantasy/backend/rhi_no.cpp:167`；写入方是任何调 `active_device(kind)` 的地方。
- 证据：`sed -n '101,108p' hardware/hardware.h`（`inline device& active_device_ref(void) noexcept { static device active = compiled_device(); return active; }`）；`grep -rn "active_device" --include=*.h --include=*.cpp .` → 消费点见上（`test/hardware_test.cpp:20-25` 是唯一的用例，且是单线程语义）。
- 影响：多线程下「一个线程切设备 + 另一个线程在建后端/渲染」时按 C++ 标准是数据竞争（UB）；实践症状是读到切换前的旧值（后端按旧设备创建）。设备枚举是标量，撕裂读不现实，所以症状轻、易被当成「偶发」。
- 复现/验证：未复现（需要运行期并发观察；最省事的验证是 TSan：`-fsanitize=thread` 下两个线程分别 `active_device(x)` 与 `active_device()`，会报 `data race`）。本节点只做了静态判定。
- 建议：把 `static device active` 换成 `static std::atomic<device>`（`device` 是 `enum class : std::uint32_t`，可 `std::atomic_ref` 或直接 `std::atomic<std::uint32_t>`），顺带让 `active_device_ref()` 不再对外暴露可写引用。

---

## C. 跨平台

### C1. `head.h` 的平台分支嵌套错一层：非 Windows 非 macOS 平台上 `os::this_os` 不存在、`XHQM_LINUX/UNIX/POSIX/ANDROID/OSNO` 永不定义
- 位置：`head.h:101-138`（`#ifdef _WIN32` … `#elif __APPLE__` … 内层 `#if TARGET_IPHONE_SIMULATOR`（`:113`）把 `__ANDROID__`（`:122`）、`__linux__`（`:125`）、`__unix__`（`:128`）、`_POSIX_VERSION`（`:131`）、`#else`（`:134`）**全部套在 `__APPLE__` 分支里**；`:137` 收内层、`:138` 收外层，外层**没有 `#else`**）
- 现状：在 Linux 上 `_WIN32` 与 `__APPLE__` 都为假 → 整个 `struct os` 里**一行都不进**，`constexpr static type this_os` 根本没被声明，`XHQM_LINUX`/`XHQM_UNIX`/`XHQM_POSIX`/`XHQM_ANDROID`/`XHQM_OSNO` 也一个都不定义。Windows（`:104 XHQM_WINDOWS`）与 macOS（`:121 XHQM_MAC`）正常。
- 证据：实测两条探针（GCC 11.4，r677）——① `#ifndef XHQM_LINUX #error "XHQM_LINUX IS NOT DEFINED" #endif` → 触发报错；② `int main(){ return (int)xhqm::os::this_os; }` → `error: 'this_os' is not a member of 'xhqm::os'`。
- 影响：**当前无人被坑，纯潜伏**——`grep -rn "this_os"` 只在 `head.h` 自身命中；平台宏里只有 `XHQM_WINDOWS` 有真实消费（44 处），`XHQM_LINUX`/`XHQM_MAC` 唯一「命中」是 `network/network.h:43` 的注释。但只要有人按 `#ifdef XHQM_LINUX`（或读 `os::this_os` 做运行期判断）写新代码，在 Linux 上就会静默走错分支或编不过——而 Linux 恰是本仓实际构建/部署的平台。
- 复现/验证：`printf '#include "head.h"\n#ifndef XHQM_LINUX\n#error "XHQM_LINUX IS NOT DEFINED"\n#endif\n' | g++ -x c++ -std=c++17 -fsyntax-only -I. -I../depend/nlohmann -`（实测触发）；以及 `printf '#include "head.h"\nint main(){ return (int)xhqm::os::this_os; }\n' | g++ -x c++ -std=c++17 -fsyntax-only -I. -I../depend/nlohmann -`（实测报 `this_os` 不是成员）。
- 建议：在 `head.h:113` 之前把 `#elif __ANDROID__`/`__linux__`/`__unix__`/`_POSIX_VERSION`/`#else` 这一串从 `__APPLE__` 分支里**提到外层**（即把 `:113` 的内层 `#if TARGET_IPHONE_SIMULATOR` 改成外层被 `__APPLE__` 守卫的独立块）。

### C2. `metaclass/function/symbols_has.h` 用了 9 行 MSVC-only 语法，GCC/Clang 下 `XHQM_USE_METACLASS` 与 `xhqm/function` 直接编不过
- 位置：`metaclass/function/symbols_has.h:22、24、26、28、31、33、35、37、39`
- 现状：这 9 行是「成员变量模板的部分特化」写法（`template <typename typein> static constexpr bool _has_sum<typein, std::void_t<…>> = true;`，声明在 `:21`）。GCC 明确拒绝（`error: explicit template argument list not allowed`），MSVC 接受。于是 `xhqm/function` 门面（它第 5 条 include 就是 `symbols_has.h`）以及 `#define XHQM_USE_METACLASS`（`xhqm.h:54-60` 会引 `xhqm/function`）在 GCC/Clang 上都是硬失败。
- 证据：实测两路都复现——① `printf '#include "xhqm/function"\nint main(){return 0;}\n' | g++ -x c++ -std=c++17 -fsyntax-only -I. -Isoftware/ -Ihardware/ -I../depend/nlohmann -` → 9 条 error；② 换 `#define XHQM_USE_METACLASS` + `#include "xhqm.h"` → 同样 9 条 error。对照：另外 6 个 `XHQM_USE_*`（ALGEBRA/LATTICE/NETWORK/SOFTWARE/FANTASY/HARDWARE）在同环境下 0 error。
- 影响：**Linux/macOS 上无法使用 `function` 反射层**（README `§三.6`/`§五` 把它算作 metaclass 的公开能力）——而且因为 `CMakeLists.txt:209-248` 的 `XHQM_HEAD_CORE` **不收** `metaclass/function/*`（不在任何 CMake 文件集里，见 E3），库自身构建时这些文件根本不参与编译，所以这个失败从不会在 CI/本地构建里暴露；它只在**消费者**开启该模块时炸。
- 复现/验证：见上两条命令（任一即可）。
- 建议：用 C++17 常规写法替代（如在类内用 `template <typename typein, typename = void> struct _has_sum_impl` 的偏特化，或改用 `if constexpr` + 检测惯用法），改完两路探针都应为 0 error。

### C3. `test/nebula_api/` 硬编码了另一棵源码树的绝对路径
- 位置：`test/nebula_api/nebula_api_test.cpp:454`、`:455`、`:522`、`:591`（`client::mtx_server mtx("/home/xhqm/Space/xhqm-pro/depend/mediamtx/mediamtx", 28561)` 等）
- 现状：4 处把 `mediamtx` 可执行文件的绝对路径写死到 `/home/xhqm/Space/xhqm-pro/...`——`xhqm-pro` 是**另一棵树**，不在本工作副本内（`/home/xhqm/DeepSeek/xhqm-v2`）。
- 证据：`grep -rn "xhqm-pro" test/nebula_api/` → 上述 4 行；`ls /home/xhqm/Space/xhqm-pro` → 本节点不存在。
- 影响：换机器、换检出目录、或清理旧树后这些用例必失败；再叠加 A5（整个文件编不过），这份「API 验证」目前是**双重不可运行**状态。对库本身无影响（不在任何构建目标里）。
- 复现/验证：`grep -n "xhqm-pro" test/nebula_api/nebula_api_test.cpp`（4 条命中）。
- 建议：改成环境变量/相对路径（或标记为需外部 mediamtx 的可选用例）。

### C4. `head.h` 的 MSVC 自动链接用相对路径，只对「恰好在 `binary/` 下一层」的工程成立
- 位置：`head.h:11-13`（`#if !defined(XHQM_PROGRAM) && !defined(XHQM_DYNAMIC)` → `#pragma comment(lib,"../binary/xhqm.lib")`）
- 现状：MSVC 消费者只要不定义 `XHQM_PROGRAM`/`XHQM_DYNAMIC`，就会被自动追加这条相对库路径。相对路径由链接器的工作目录解析（VS 下即工程目录），所以只有「工程目录的上一级正好有 `binary/xhqm.lib`」时才命中——仓内消费者（如 `test/`，输出目录默认 `${CMAKE_CURRENT_SOURCE_DIR}/../binary/`，`CMakeLists.txt:577`）符合，仓外消费者（如 `stella`）不符合。
- 证据：`sed -n '9,13p' head.h`；产物目录规则见 `CMakeLists.txt:576-581`（非 Windows 下 `XHQM_OUTPUT_DIR` 默认 `../binary/`）。库路径不存在时 MSVC 的典型症状是 `LNK1104: 无法打开文件`。
- 影响：仓外 MSVC 工程要么被这条 pragma 带到一个错误的/不存在的路径（`LNK1104`），要么被迫定义 `XHQM_PROGRAM` 来关掉它（而定义它会连带把 `XHQMCLASS` 变成 `dllexport`，见 F2）。属「隐式构建耦合」。
- 复现/验证：未复现（本节点无 MSVC）。静态可读：`head.h:12` 的字符串是相对路径。
- 建议：把自动链接改成可关闭的显式开关（如 `#ifdef XHQM_AUTOLINK` 才 `#pragma comment(lib, …)`），或至少在 README 的依赖章节写明这条相对路径假设。

---

## D. 性能

### D1. `head.h` 无条件吸收 909,621 字节的 `network/json.hpp`，每个 TU 付约 0.9 s
- 位置：`head.h:43-49`（`#ifdef XHQM_JSON_EXTERNAL` → `<nlohmann/json.hpp>`，否则 `#include "./network/json.hpp"`）
- 现状：`head.h` 是几乎所有 TU 的公共头（`xhqm.h:33` 第一件事就是包含它，各模块头又以 `../../head.h` 为底），因此**即使完全不碰 JSON 的 TU 也要解析一份 909 KB 的 nlohmann 吸收版**。换 `-DXHQM_JSON_EXTERNAL` 只是换成环境版 nlohmann（同样的解析量），不是「按需不含」。
- 证据：`wc -c network/json.hpp` → 909621（对比 `network/httplib.h` 353360）；语法检查计时（GCC 11.4，3 次取范围）：空 TU `int main(){}` ≈ 0.00 s，`#include "head.h"` + `int main(){}` ≈ 0.85–0.92 s。即**单个 TU 约 0.9 s 花在这条包含链上**，与是否使用 JSON 无关。
- 影响：全开关构建（CMake 登记的 260 个仓内源文件）里，这是所有 TU 共同的固定开销；小改动触发的增量重编（header 一变、依赖它的 TU 全重编）感受最明显。对运行期无影响。
- 复现/验证：`wc -c network/json.hpp`；计时 `for i in 1 2 3; do /usr/bin/time -f "%e s" bash -c 'printf "#include \"head.h\"\nint main(){return 0;}\n" | g++ -x c++ -std=c++17 -fsyntax-only -I. -I../depend/nlohmann -'; done`。
- 建议：至少别让 `head.h` 顶层包含 json（把 JSON 依赖下放到真正用它的头），或提供「不自动包含 json」的开关给不碰 JSON 的模块用；短期无需改语义，仅记录。

---

## E. 未实现 / 占位桩

### E1. `head.cpp` 是编不过的草稿：`main()` 在 `:55`，唯一错误在 `:63` 的 MSVC 扩展后缀（根 `DEBUG.md` A2 的归因有误）
- 位置：`head.cpp:63`（`xhqm::float_16 f16u = 0b0000001111111111ui16;`）；同文件 `:55` 的 `int main()`、`:12-52` 的整段注释块、`:22` 的 `#include "./metaclass/struct/values.h"`
- 现状：**该文件当前 GCC 下编不过，且只有 1 条错误，出在 `:63`**：`ui16` 是 MSVC 的整数后缀扩展（`unsigned __int16`），GCC 报 `unable to find numeric literal operator 'operator""ui16'`（还提示 `-fext-numeric-literals`，那是给虚数后缀用的，不适用）。`head.cpp:12` 的 `/*` 与 `:52` 的 `*/` 之间整段是注释，所以 `:22` 那条指向不存在目录的 include（`metaclass/struct/` 确实不存在）**不参与编译**，不是编译失败的原因——根 `DEBUG.md` A2 把它记为原因，与实测不符；`metaclass/README.md` 记录的也是同一处，同样不构成编译错误。
- 证据：实测 `g++ -std=c++17 -fsyntax-only -I. -I../depend/nlohmann head.cpp` → **1 条 error**，`head.cpp:63:27`；`ls metaclass/struct` → 不存在（`exit 2`）；`grep -n "^/\*\|^\*/" head.cpp` → `12:/*`、`52:*/`。另该文件 `:12-52` 里还有 `xhqm/tensot`（`:15`，拼写错）、`./geometry/point_measure.h`（`:14`）等一批早期路径，全是死注释。
- 影响：接手者按文件名会以为它是引擎入口；按根 `DEBUG.md` A2 去「修 `:22` 的悬空 include」则**修完仍编不过**（改注释里的 include 对编译零影响），这正是本仓记录过的「把某次现场当成稳定事实」的复发。文件不在任何构建里（`grep -rn "head.cpp" CMakeLists.txt xhqm.vcxproj` → 0 命中），所以对库无影响。
- 复现/验证：`cd /home/xhqm/DeepSeek/xhqm-v2 && g++ -std=c++17 -fsyntax-only -I. -I../depend/nlohmann head.cpp`（实测 1 条 error，行号 63）。
- 建议：要么删掉这一行/整个草稿，要么把 `ui16` 写成标准形式（如 `xhqm::float_16 f16u = 0x03FF;`）让它重新成为可用冒烟；若要保留 `:22` 的历史记录，请注明它在注释块内。

### E2. 第二条构建路径 `xhqm.vcxproj` 只覆盖 CMake 文件集的约三分之一，且恒按 DLL 配置
- 位置：`xhqm.vcxproj`（98 个源文件）、`xhqm.sln`（1 个工程）
- 现状：VS 工程登记的源文件是 98 个；`CMakeLists.txt` 登记 263 条（去掉 3 条解析假象——`:70` 的 `${XHQM_UCRT}/signal.h`、`:166` 的 `${OPENBLAS_ROOT}/include/cblas.h`、`:80` 注释里的 `"system.h"`——实际仓内 260 个），**差集 165 个仓内文件不在 VS 工程里**：fantasy 90、algebra 42（analysis/soul/tensor 全族）、software 19（`datum/packet/message/command/project/utility/system.cpp/system.h` 与 `pattern/*`）、lattice 6、metaclass 6、hardware 2。VS 工程同时**不定义任何 `XHQM_BUILD_*`**（对比 CMake `CMakeLists.txt:551-574` 按开关 `add_definitions`），只固定 `XHQM_DYNAMIC;XHQM_PROGRAM`（即只编 DLL），另有 2 处 `ExcludedFromBuild`。22 个门面头**一个都不在工程里**（工程里唯一的 `xhqm` 前缀条目是 `xhqm.h`）。
- 证据：`grep -o 'Include="[^"]*"' xhqm.vcxproj | wc -l` → 105（含 `Debug|Win32` 等 4 条配置名，源文件 98）；`grep -o "<PreprocessorDefinitions>[^<]*" xhqm.vcxproj | sort -u` → 只有 `XHQM_DYNAMIC;XHQM_PROGRAM;WIN32_LEAN_AND_MEAN` 一行带引擎宏；`grep -c "fantasy\|lattice\|metaclass\|hardware" xhqm.vcxproj` → 6（全是无关命中，未见这些模块的任何文件条目）。
- 影响：按 README 旧说法把 `xhqm.sln` 当「第二条构建路径」的人，在 Windows 上**编不出含 fantasy/tensor/lattice/metaclass 的库**，也拿不到与 CMake 一致的配置（CMake 默认静态、VS 恒动态）。这不是「两条等价的路径」，而是「一个长期只维护网络+软件子集的 VS 工程」。
- 复现/验证：`grep -o 'Include="[^"]*"' xhqm.vcxproj | sed 's/Include="//;s/"//' | tr '\\' '/' | sort -u > /tmp/vs.txt; wc -l /tmp/vs.txt` 与 `grep -o '"[^"]*\.\(h\|hpp\|cpp\|mm\|cu\)"' CMakeLists.txt | sort -u | wc -l` 对比。（本节点实测：98 vs 263，差集 168/实际 165。）
- 建议：在 README 里明确它只是子集路径（本次已写），并把「要么补齐 fantasy/tensor/lattice 条目、要么在 `xhqm.sln` 里加注释说明它只出网络+软件子集」记进后续计划。

### E3. 65 个仓内源文件不在任何 CMake 文件集里（全是头，含 `head.cpp`）
- 位置：`CMakeLists.txt` 的 10 个 `set(XHQM_HEAD_*/XHQM_BODY_*)` 块（`:209-538`）
- 现状：逐文件比对后，65 个仓内源文件（不含 `test/`）不出现在任何文件集里，例如 lattice 头 14 个（`next_list/prev_list/bind_list/ring_list/parent_tree/child_tree/bind_tree/arrow_mesh/bind_mesh/level_tree/matrix_mesh/sparse_mesh/graph_algorithms.hpp` + `struct.h`）、`metaclass/function/*` 5 个 + `metaclass/resource.h`、`algebra/analysis/*.h` 9 个、`algebra/tensor/*` 11 个（含 `backend/tensor_backend_{cpu,gpu,metal,no,simd}.h`）、`algebra/soul/*` 5 个、fantasy 7 个、`software/pattern/*` 6 个、`hardware/parallel/dispatch_{no,cpu}.h`、`network/{json.hpp,httplib.h}`、`head.cpp`、`xhqm.h`。**其中没有任何 `.cpp`**（唯一的 `.cpp` 是 `head.cpp`，见 E1），所以不影响编译与链接，只影响 IDE 可见性与头文件依赖跟踪。
- 证据：脚本化比对（CMake 集合并 vs `os.walk` 源文件集合）→ `65`；按目录分布见上。`lattice/DEBUG.md` G4 已就 lattice 子集单独记过（6 条登记 vs 20 个文件），本条是同一现象的全仓口径。
- 影响：在 VS/IDE 里看不到 `metaclass/function/*`（正是 C2 那个编译不过的头）与 lattice 的多数实现头，容易误判「文件缺失」；改这些头不会触发 CMake 重新生成（header-only 下影响有限）。
- 复现/验证：在仓库根跑下面这段（本节点实测输出 `65 | .cpp: 1 | head.cpp in list: True`）：
  ```python
  import os, re
  listed = set(re.findall(r'"([^"\\]+\.(?:h|hpp|cpp|mm|cu))"', open('CMakeLists.txt', encoding='utf-8').read()))
  files = set()
  for root, dirs, names in os.walk('.'):
      dirs[:] = [d for d in dirs if d not in ('.svn', 'build', 'build-shared')]
      if root.startswith('./test'):
          continue
      for n in names:
          if n.endswith(('.h', '.hpp', '.cpp', '.mm', '.cu')):
              files.add(os.path.relpath(os.path.join(root, n), '.').replace('\\', '/'))
  missing = sorted(files - listed)
  print(len(missing), '| .cpp:', sum(m.endswith('.cpp') for m in missing), '| head.cpp in list:', 'head.cpp' in missing)
  ```
  反向抽查：`for f in lattice/next_list.hpp metaclass/function/function.h hardware/parallel/dispatch_no.h software/pattern/factory.h; do printf "%-42s %s\n" "$f" "$(grep -c "$f" CMakeLists.txt)"; done` → 全 0。**注意**：这些文件没有对应的可执行目标（C2 的失败也正因此不会在库构建中暴露）。
- 建议：把 `metaclass/function/*` 与 `hardware/parallel/*` 补进 `XHQM_HEAD_CORE`（至少让 IDE 与依赖跟踪看得见），C2 修好前不要再往里加新头。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 模块开关「7 个 `XHQM_USE_*` 对 4 个 `XHQM_BUILD_*`」的不对称，且两族宏在源码里都没有 `#ifdef` 消费点
- 位置：`xhqm.h:21-31`（`USING_XHQM` 一次性定义 7 个 `XHQM_USE_*`）、`CMakeLists.txt:41-44`（4 个 `option`：`XHQM_BUILD_TENSOR/NETWORK/FANTASY/SYSTEM`，默认全 OFF）、`CMakeLists.txt:209-248`（`XHQM_HEAD_CORE` 恒收 hardware/algebra 基础/geometry/metaclass 基础/lattice 基础/system 基础/`software/signal`）
- 取舍理由：`LATTICE`/`METACLASS`/`HARDWARE` 是**底座**——`signal`（槽池）、`geometry`、`metaclass` 基础、lattice 的基础容器是其它模块的公共依赖（`CMakeLists.txt:250-270` 的注释就解释了为什么 `software/signal.cpp` 必须恒编：只开 NETWORK 时 nebula 的 `addr_b` 需要 `signals::pool()`，否则 `LNK2019`）。给它们开关只会制造「开关关着但下游还需要」的伪选择，所以开关只留给可整块摘掉的重量级模块（张量/网络/渲染/软件栈）。两族宏名字对不上（`USE_ALGEBRA` ↔ `BUILD_TENSOR`、`USE_SOFTWARE` ↔ `BUILD_SYSTEM`）是因为跨侧命名视角不同：头侧按「我要哪块知识」，构建侧按「我要编哪堆 TU」。
- 代价：① 名字不成对，读者会以为它们一一对应（本仓旧 README 正是这么写的）；② 两族宏**都不是编译期特性门**——`grep -rn "XHQM_BUILD_"` 在源码里只命中 `xhqm.h:9` 的一句注释，`XHQM_USE_*` 只被 `xhqm.h` 自己消费，所以「关掉某模块」= 不编那堆文件，不存在 `#ifdef` 保护，写错开关的结果是**未定义符号**而不是编译期报错；③ `LATTICE/METACLASS/HARDWARE` 无法从构建里摘掉。
- 结论：**不修**（不对称本身是对的）。已在 `README.md` 的 `§三.6` 与 `§四` 写明口径与数字。

### F2. `xhqm_io` 只在 `XHQM_DYNAMIC` 下非空，`XHQMCLASS` 由 `XHQM_PROGRAM` 决定
- 位置：`head.h:21-31`（`XHQM_PROGRAM` → `XHQMCLASS = XHQMEXPORT`，否则 `XHQMIMPORT`；`XHQM_DYNAMIC` → `xhqm_io = XHQMCLASS`，否则 `xhqm_io` 为空）；注入点 `CMakeLists.txt:584-595`（静态与动态**都**定义 `XHQM_PROGRAM`，只有动态额外定义 `XHQM_DYNAMIC`）
- 取舍理由：同一份头要同时服务静态库、动态库与「消费者」三种角色，只能把差异压到两个宏里：静态库不需要 `dllexport`，消费者需要 `dllimport`（`head.h:11-13` 还会自动链接导入库）。让 `xhqm_io` 在非动态下**完全为空**是最省事的选择——静态构建时头文件里那些导出标注就相当于不存在。
- 代价：① `xhqm_io` 的语义依赖「谁定义了什么」，`grep xhqm_io` 读代码时无法判断一个类是否跨 DLL 边界；② 静态构建里 `XHQM_PROGRAM` 也被定义（`CMakeLists.txt:592`），MSVC 下 `XHQMCLASS` 仍是 `dllexport`（对静态库是无害的噪声，但会让人误判这是动态构建）；③ 消费方不定义任何宏时，`xhqm_io` 为空 + 自动链接 `../binary/xhqm.lib`（C4）——即「什么都不知道」的默认组合是静态消费。
- 结论：**不修**（三态共用一份头是既定契约）。代价已在本条与 C4 记录。

### F3. `head.h` 顶部无条件关掉 MSVC C4251（导出类含 STL 成员）
- 位置：`head.h:9-10`（`#pragma warning (disable:4251)`）
- 取舍理由：引擎的公共类大量直接把 `std::map/std::vector/std::string/stl` 类型放在成员里并标 `xhqm_io` 导出（例如 `nebula::addr_b::m_neighbors`、`data::m_windows`），MSVC 会为每个这样的成员发 C4251（「需要有 dll 接口」）。不关掉，动态构建下头文件里会有成百上千条噪声，淹掉真信号。
- 代价：**真正的跨 DLL ABI 风险被一起静音**。这个风险不是空谈：`test/CMakeLists.txt:2-5` 的注释就是一次现场记录——单配置生成器（Ninja）不指定 `CMAKE_BUILD_TYPE` 会退回 Debug(`/MDd`)，与 `xhqm.dll` 的 Release(`/MD`) 不匹配，`std::string/map/vector` 跨 DLL 边界会 **ABI 错乱（崩溃/网络失败）**，所以必须在 `project()` 之前固定 Release。同理，Debug 库被 Release 消费者链接也会静默错位。
- 结论：**不修**（关警告是动态库可用的前提），但把代价写在这里：跨 DLL 边界的类是 ABI 敏感的，Debug/Release（或不同 VS 工具集）不可混链。

### F4. 22 个门面头故意不带扩展名
- 位置：`xhqm/`（`algebra`、`astral`、`nebula` … 共 22 个无扩展名文件，实测 131 行、113 条 `#include`，且**全部只含 `#include` 与注释**、0 行代码）
- 取舍理由：门面表达的是「我要这个模块」，写成 `#include "xhqm/algebra"` 读起来像模块名而不是某个具体头，避免被当成普通头单独引用/单独编译。（同一条也从根 `DEBUG.md` F5 记录，本文件不重复其论证。）
- 代价：这里的补充代价是**构建系统与工具整批看不见它们**——`xhqm.vcxproj` 里 0 个门面头（只有 `xhqm.h`，实测 98 个源文件里无一条 `xhqm/<name>`）；`glob "**/*.h"`、`find -name '*.h'` 也整批漏掉（本轮任务提醒原文即此）；门面的正确性没有构建期校验（A1/A2/C2 三个门面编不过就是这么漏出去的）。
- 结论：**不修**（命名是有意的）。代价是「必须有人手工核 22 份门面」，已在本文件 A1/A2/C2 与「已验证事实」里给出复核命令。

### F5. 根 CMake 不收编 `test/`，每个测试目标各自维护「最小源闭包」
- 位置：`CMakeLists.txt`（全文 0 处 `add_subdirectory` / `enable_testing` / `add_test`）、`test/CMakeLists.txt:6`（自己的 `project(xhqm_algebra_test CXX)`）、`test/CMakeLists.txt:152-155`（注释明说「主库靶子 xhqm 在此不可见…故按本文件既有惯例显式列源文件」）
- 取舍理由：`test/` 要能在没有主库产物（`../../binary/xhqm.lib` 只在 Windows 存在）的机器上单独配置，所以它自带 `project()`、自带库类型判定（`:201-206`），并把「这 10 行 CMake 能编」当作设计目标，代价是每个目标重复列源文件。
- 代价：源列表是**手工反推的最小闭包**，随手漏一项就是一个「没人跑到」的死目标——`test/polydtor_test.cpp` 漏 `addr/` 下 5 个 `.cpp`（A4）与 `test/sparse_mesh_test.cpp` 干脆没有目标（根 `DEBUG.md` E7）都是这条代价的产物；再加上无 ctest，失败不会有人报。
- 结论：**不修**（独立工程是有意的），但代价已经实际发生两次，建议把「目标能配置+能编」变成显式清单（本次只记录）。

---

## G. 文档与代码的出入

### G1. `README.md` 的用法示例把张量模板参数写反了（`tensor<double, 2>`）
- 位置：`xhqm/README.md`（旧 `§七.2` 示例）；真值声明 `algebra/tensor/tensor_base.h:18`（`template<xhqm::size dim, class type = xhqm::measure> class tensor`）
- 现状：旧示例 `xhqm::tensor<double, 2> t({3, 4}, 0.0);` 把**类型写在维数前面**。实测该写法 3 条错误（`type/value mismatch at argument 1/2 in template parameter list`）；改成 `tensor<2, double> t({3, 4}, 0.0)` 通过（构造函数见 `tensor_base.h:131`：`tensor(const std::array<xhqm::size, dim>& dims, const type& value = 0)`）。
- 证据：`grep -rn "xhqm::tensor<" algebra/ | head` → 全是 `tensor<2, type>` / `tensor<1, type>` 形式（如 `algebra/analysis/linalg.h:262`）；探针 `printf '#define XHQM_USE_ALGEBRA\n#include "xhqm.h"\nint main(){ xhqm::tensor<double,2> t({3,4}, 0.0); (void)t; }\n' | g++ -x c++ -std=c++17 -fsyntax-only -I. -I../depend/nlohmann -` → 实测 3 条 error；把 `double,2` 换成 `2,double` → 0 error。
- 影响：照抄示例的人直接编不过；`xhqm/README.md` 是核心层唯一的用法入口，错了会波及所有新接入者。
- 复现/验证：见上两条探针命令。
- 建议：README 已改（现为 `tensor<2, double>` 并标注了模板参数顺序的来源行）。**这是本次唯一改动 README 用法示例的地方。**

### G2. `README.md §八` 给 `test/nebula_api/` 举的 A 世代例子不成立（据代码是 `hardware::media` 的方法与本目录自有命名空间）
- 位置：旧 `xhqm/README.md:238`；对照 `test/nebula_api/nebula_api_test.cpp:286/309`（`c.to_json()` / `a2.from_json(...)` 是 `hardware::media::caps` 与 `audio_format` 的方法）、`test/nebula_api/server/media_server.h:24`（`namespace server` 是该目录自带的）
- 现状：旧文写「`test/polydtor_test.cpp` 与 `test/nebula_api/` 仍是 A 世代 nebula API（`addr_t<kind>`、`to_json/clone/equal/broadcast/send`、`server`/`sensor`）」。**「仍是 A 世代」这个结论成立**（实测 100 行 / 303 条错误，见 A5），但其中 `to_json/clone/equal/broadcast/send` 只在 `polydtor_test.cpp` 成立，`server`/`sensor` 在该目录里指的只是自带的 `namespace server` 与本地类名 `counter_sensor`；`nebula_api_test.cpp` 真正的 A 世代残留是 `addr_t<kind>`（`:39`）与 `link_t<kind>`（8 处）。
- 证据：`grep -n "to_json\|from_json" test/nebula_api/nebula_api_test.cpp` → 只有 `:286`、`:309`，两处都在 `hardware::media` 的 `TEST_CASE` 内；`grep -rn "addr_t\|link_t\|link_b" test/nebula_api/nebula_api_test.cpp` → `:39`、`:28`、`:58/111/243/448/457/466/586/592`。
- 影响：按旧文的例子去 grep 会以为「把这个目录里的 `to_json/broadcast` 换掉就好」，实际要换的是 `link_b`/`link_t`/`addr_t` 与一整套 `data/link` 构造函数（A5）。
- 复现/验证：`grep -n "to_json\|from_json\|addr_t\|link_t\|link_b" test/nebula_api/nebula_api_test.cpp`。
- 建议：README 已订正（保留旧文划线 + `已作废（2026-10-07 复核 r677）`，并把真证据列出）。

### G3. `README.md §八` 把 `polydtor_test` 的失败阶段记成「缺源文件导致编不过」
- 位置：旧 `xhqm/README.md:238` 尾句；对照 A4 的实测（20 条**编译期** error，12 行）
- 现状：旧文把原因归到 `test/CMakeLists.txt` 的目标漏列 `addr/` 下 5 个源文件（`CMakeLists.txt`/`test/CMakeLists.txt` 均已核实确实漏列）。但**先撞的是编译期**：夹具本体仍是 A 世代（`kind::none`/`addr_t`/6 个不存在的 `override`），`-fsyntax-only` 就足以复现 20 条 error，根本走不到链接。
- 证据：见 A4 的实测命令与行号（`27、37-40、43、45、46、54、59、67、75`）。
- 影响：只补源文件（根 `DEBUG.md` A3 的建议）而不改夹具，目标依旧编不过——修错地方。两件事都要做。
- 复现/验证：`cd test && g++ -std=c++17 -fsyntax-only -I. -I.. -I../depend/nlohmann polydtor_test.cpp 2>&1 | grep -c "error:"` → 20。
- 建议：README 已按实测订正（把「编不过」的实测口径与链接缺口并列写着）；根 `DEBUG.md` A3 的「链接必然缺符号」为真但需补一句「编译期先失败」。

### G4. `README.md §八` 称 `head.cpp`「仅作语法冒烟」——它当前编不过
- 位置：旧 `xhqm/README.md:241`；对照 `head.cpp:63`
- 现状：旧文「`head.cpp` 是含 `main()` 的草稿文件，未纳入构建，仅作语法冒烟」。「未纳入构建」为真（`grep -rn "head.cpp" CMakeLists.txt xhqm.vcxproj` → 0 命中），「可作语法冒烟」为假：GCC 下 1 条 error（`:63` 的 `ui16` 后缀，详见 E1）。
- 证据：`g++ -std=c++17 -fsyntax-only -I. -I../depend/nlohmann head.cpp` → 1 条 error（`head.cpp:63:27`）。
- 影响：接手者会把一个「编不过的草稿」当成可用的语法哨兵，甚至把它当作改动 `head.h` 后的自检入口。
- 复现/验证：见上命令。
- 建议：README 已订正（保留划线 + 作废标注，并指向本文件 E1）。

### G5. `README.md §四` 的架构树只列了 21 个门面，漏 `tuple_values`
- 位置：旧 `xhqm/README.md:141-153`（架构块）
- 现状：架构块按去向分组列出 `xhqm/*` 的 22 个门面，但 `tuple_values`（→ `metaclass/tuple_values.h`，被 `xhqm.h:59` 在 `XHQM_USE_METACLASS` 下引用）没有出现在任何一行，读者会以为门面只有 21 个。
- 证据：`ls xhqm/ | grep -v README.md | wc -l` → 22；对照旧树块的清单 → 21。
- 影响：新接入者按架构树找「元类有哪些门面」会漏掉组件存储入口（`metaclass/tuple_values.h` 是 `xhqm.h` 里唯一直接引用的 tuple 相关头）。
- 复现/验证：`ls -A xhqm/ | grep -v README.md | wc -l`（22）与文档块的条目数对比。
- 建议：README 已补（现分组为 `block / buffer / cache / function / tuple_values → metaclass/`，并附一句「xhqm.h 只引用其中 19 个门面」的说明）。

### G6. `CMakeLists.txt:338-340` 的 nebula 分层注释仍写着已不存在的 `tool` 层
- 位置：`CMakeLists.txt:338-340`（`# nebula：B 世代分层（下→上）node 基类 → addr（地址/邻居表）→ data（通道/记忆）→ life（事件池）→ link（L1 邻居图）→ find（L2 搜索）→ post（L3 订阅推送）→ tool（工具封装）→ zero/star（组装）`）
- 现状：`network/nebula/tool.h` / `tool.cpp` **在工作副本里已不存在**（`find network/nebula -name "tool.*"` → 0 命中；r653 的 svn log 里它们还是 `M`，之后被删除），而 `network/nebula/zero.h:5` 明确写着「zero：零号协议的顶层节点基类（**原 tool 层与原来的 zero 组装层合并为一个名字**）」。`network/nebula/README.md:345` 的「A 世代已移除」清单里也没有单独列 tool（它不算 A 世代，是被合并掉了）。
- 证据：`ls network/nebula/` 无 `tool.*`；`sed -n '5p' network/nebula/zero.h`；`CMakeLists.txt:338-340` 原文如上。对照 `xhqm/nebula` 门面：9 条 include 里也没有 tool（`nebula.h/addr.h/data.h/life.h/link.h/find.h/post.h/zero.h/star.h`）。
- 影响：按注释找 `tool` 层的人会找不到文件，进而怀疑文件集缺了东西（`CMakeLists.txt:355` 那句「待同步」注释会加强这个误会，但那句已被根 `DEBUG.md` G2 单独订正，本文件不重复）。
- 复现/验证：`ls network/nebula/ | grep -c tool` → 0；`grep -rn "tool" network/nebula/zero.h | head`。
- 建议：把注释里的「→ tool（工具封装）→ zero/star（组装）」改成「→ zero（原 tool 层并入）/star（组装）」（需改 CMakeLists，本次不动）。

### G7. `CMakeLists.txt:673-674` 的 Release PDB 路径写死 D 盘，不跟随 `XHQM_OUTPUT_DIR`
- 位置：`CMakeLists.txt:672-675`（`if(WIN32) target_link_options(xhqm PRIVATE "/DEBUG" "/PDB:D:/WorkSpace/xhqm_v2/binary/xhqm.pdb")`）
- 现状：同一份 CMakeLists 里产物目录是可配置的（`CMakeLists.txt:577`：`set(XHQM_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../binary/" CACHE STRING …)`，注释还写着「上溯一级到 D:\WorkSpace\xhqm_v2\binary」），但 PDB 路径是**硬编码的绝对 Windows 路径**，与 `XHQM_OUTPUT_DIR` 无关。文本在 Linux 构建机上同样存在（`:672` 的 `WIN32` 守卫使它只在 Windows 生效）。
- 证据：`sed -n '671,676p' CMakeLists.txt`；`grep -n "XHQM_OUTPUT_DIR" CMakeLists.txt` → `:577`（可覆盖）与 `:674`（不跟随）。
- 影响：把仓库换到别的盘符/目录、或用 `-DXHQM_OUTPUT_DIR` 指定独立输出目录时，PDB 仍往 `D:/WorkSpace/xhqm_v2/binary/` 写（该目录不存在时链接告警/失败，取决于 VS 版本）。根 `DEBUG.md` C2 已把「Windows 盘符路径残留」记在根模块，本条只补「它不跟随 `XHQM_OUTPUT_DIR`」这一点。
- 复现/验证：`grep -n "PDB:" CMakeLists.txt` → `:674`。
- 建议：改成 `"/PDB:${XHQM_OUTPUT_DIR}xhqm.pdb"`。

---

## 已验证事实（供复核，非问题）

以下数字/契约在本轮（g++ 11.4 / C++17 / r677）实测确认**正确**，因此不进上面的问题清单；留作后续复核的锚点：

- **门面规模与自洽**：`xhqm/` 下 22 个无扩展名门面头，共 **131 行 / 113 条 `#include`**，逐条解析目标路径 **0 条悬空**；22 份文件里**没有一行代码**（除 `#include` 与注释外 0 行）。命令：`ls -A xhqm/ | grep -v README.md | wc -l`（22）、`grep -h "#include" xhqm/<22 个文件> | wc -l`（113）、对每个 include 做 `[ -f ]` 判定（0 缺失）。
- **门面直接包含的编译矩阵（GCC 11.4）**：22 个里 19 个 0 error；失败 3 个 = `xhqm/lattice`（2 error，A1）、`xhqm/event`（8 error，A2）、`xhqm/function`（9 error，C2）。
- **开关矩阵（GCC 11.4）**：`XHQM_USE_ALGEBRA / LATTICE / NETWORK / SOFTWARE / FANTASY / HARDWARE` 六个各为 **0 error**；`XHQM_USE_METACLASS` 为 **9 error**（C2）。`USING_XHQM` 在补足 fantasy 的 `-I` 后仍失败（同 C2）；只带 `-I.` 时失败于 `fantasy/drawer.h:7` 找不到 `backend.h`。
- **开关不对称的数字口径**：`xhqm.h` 7 个 `XHQM_USE_*`（`:23-29`）；`CMakeLists.txt:41-44` 4 个 `option`，默认全 OFF；`XHQM_HEAD_CORE`（`:209-248`，31 条）恒含 `hardware/hardware.h`、`metaclass/cache|block|buffer|compute|template|tuple_values.h`、`lattice/stacks|queues|list|tree|mesh|directed_mesh`、`software/signal.h` 与 system 基础；`XHQM_BODY_CORE`（`:250-270`，13 条）恒含 `software/signal.cpp` 与张量 no 后端两个 TU；`network/uuid|md5` 在 `:546-549` 无条件追加。
- **两条构建路径的规模**：CMakeLists 引号内源文件路径 263 条（3 条为变量/注释假象）→ 仓内 260 个；`xhqm.vcxproj` 98 个（`Include=` 105 条含 4 条配置名）；差集实测 165 个仓内文件（E2 的分布）。nebula 的 13 个 TU 三方一致（`CMakeLists.txt:378-390` 的 BODY 13 个 `.cpp`、`xhqm.vcxproj` 的 13 条 nebula `.cpp`、文件树 13 个）。
- **A 世代残留的可验证口径**：`nebula::addr_t` 在全仓**只有** `test/polydtor_test.cpp:54` 与 `test/nebula_api/nebula_api_test.cpp:39` 两处引用（无定义）；`nebula::link_t` 只在 `nebula_api_test.cpp` 的 8 处 + 该目录 `README.md:116`、`client/device_client.h:35` 的注释里；`kind::none` 在 nebula 里不存在（枚举是 `no`，`addr_b.h:16`）。
- **`addr_from` 的实际口径**：造 `uuid / ipv4 / ipv6 / serial` 四种（`network/nebula/addr.cpp:14-19`，serial 在缺 `port` 时认 `baud`，默认 `addr_serial::s_baud`）；`kind` 词表 7 项（`addr_b.h:17-23`；枚举本体含 `no` 在 `:14-24`），文本互转 7 对（`addr_b.cpp:12-37`），`bluetooth/nearlink/quantum` 只有词与互转、无实现类。
- **`addr_uuid` 是 `addr_ptr` 改名**：SVN r653 同一次提交里 `D network/nebula/addr/addr_ptr.cpp|.h` + `A network/nebula/addr/addr_uuid.cpp|.h`（`svn log -v -r 653 network/nebula/addr/`），可查证。
- **README 示例的可编译性**：`#include "xhqm.h"`（裸，只带 `head.h`）0 error；`#define XHQM_USE_HARDWARE` + `hardware::compiled_device()/device_name()` 0 error；`tensor<2,double> t({3,4}, 0.0)` 0 error；`network::connect_tcp_auto_timeout("127.0.0.1", 8080, 1000)` 与声明一致（`network/network.h:350`，返回 `hander`）；`float_32`（`algebra/ftype.h:61`）与 `number<f32>::pi`（`algebra/value.h:67`）都存在。
- **head.h 的公开面行号锚点**：`NOMINMAX` `:5-7`、导出宏 `:9-19`、`XHQMCLASS` `:21-25`、`xhqm_io` `:27-31`、json 包含 `:43-49`、`os` 枚举 `:87-99`、`device` 结构 `:144-175`、宏函数 `:188-209`、类型别名 `:216-238`、`error` `:240-243`、字面量 `:256-275`。
- **编译成本基线**：空 TU ≈ 0.00 s；`#include "head.h"` ≈ 0.85–0.92 s（两者都加 `-I. -I../depend/nlohmann`）——即 D1 那条包含链的固定开销。
- **`metaclass/thread/` 是空目录**（`ls -la` 只有 `.`/`..`）——已由根 `DEBUG.md` E6 记录，本文件不重复。
- **不属本模块但会连带踩到的锚点**：`test/hardware_test.cpp:20-25`（`active_device` 的单线程语义用例，B1 的验证入口）；`test/CMakeLists.txt:2-5`（Debug/Release ABI 现场，F3 的证据）；`test/CMakeLists.txt:201-206`（测试侧库类型判定）。
