# xhqm 引擎 · 根模块当前问题清单

根模块（模块路径 `.`，module id `root`）的**当前仍存在**的问题台账：只记「现在还是坏的 / 还是没写的 / 还是对不上的」，**不记已修项、不做版本历史**。每条都给出可核对的 `文件:行号`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**：本仓的历史台账**不搬家、不改名** —— `fantasy/fantasy_bugs.md`（fantasy 的静态分析清单 + 2026-08-22 起逐轮修复记录，编号 1–43，自陈仅 #21/#22 待定）与 `network/nebula/DEFERRED.md`（nebula 讨论过但**决定不做/没排期**的事项）保持原位，本文件只指过去、不复制内容。各模块的「定位/优势/劣势/缺失/建议」在其自己的 `README.md` 里；本文件是根模块的问题清单，因此也收纳库层面**可复现**的占位桩与文档漂移。模块级 `DEBUG.md` 与本次全仓复核同批建立（同批可见 `hardware/DEBUG.md`、`lattice/DEBUG.md`、`algebra/geometry/brep/DEBUG.md`），彼此不重复记同一件事；历史台账 `fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` 保持原名原位置，只被指引、不被搬动。
>
> 判据纪律：凡写「编译不过 / 某 API 不存在 / 某文件缺失」，必须附复现命令与验证版本。本仓出过「结论被推翻后只在别处订正」的前例（见 `network/README.md:344`、`xhqm/README.md:240`），故本文件每条都标了验证方式。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `algebra/value.h` 的十个数学常数恒为 `0.0l`，用到它们的数值代码会静默算错
- 位置：`algebra/value.h:14-23`
- 现状：`ln_2 / ln_10 / log_e_2 / log_e_10 / log_2_e / log_10_e / sqrt_2 / sqrt_3 / sqrt_en / sqrt_pi` 十个 `static constexpr auto` 全部初始化为 `0.0l`，是同文件 `en / pi / phi / egamma`（`:9-12`，真值）之外的纯占位。它们经 `xhqm::number<T>`（`value.h:64-94`）以 `static_cast<type>` 转发给所有模板使用者。
- 证据：`grep -n "0\.0l" algebra/value.h` → 10 条命中，全部落在 `:14-23`；对照 `value.h:9-12` 的 `en = 2.71828…l` 为真值。`value.h:4` 的 `//#include <numbers>` 也被注释掉，所以没有第二来源兜底。
- 影响：任何按数学含义使用 `xhqm::sqrt_2`、`hqm::number<f32>::ln_2` 之类的代码都拿到 0 而不是真值 —— **不报错、不告警、编译通过**，只在下游结果里体现。属「算错」类，优先级最高。
- 复现/验证：`grep -n "0\.0l" algebra/value.h`（应为 10 条，行号 14–23）。
- 建议：一次填对十个字面量（或直接启用 `<numbers>`），并补 `static_assert` 断言。

### A2. `head.cpp:22` 引用不存在的头文件，该文件当前编译不过（但不在构建里，所以没人发现）
- 位置：`head.cpp:22`
- 现状：`#include "./metaclass/struct/values.h"`，而 `metaclass/` 下只有 `function/` 与 `thread/` 两个子目录，**没有 `struct/`**。同文件 `head.cpp:55` 含 `int main()`，是草稿。
- 证据：`ls metaclass/struct` → `没有那个文件或目录`；`grep -rn "head.cpp" CMakeLists.txt xhqm.vcxproj` → **0 命中**（因此它不参与任何构建，缺陷被隐藏）。`metaclass/README.md:159` 也记了这处悬空引用，但没给行号。
- 影响：直接 `g++ head.cpp` 的人撞墙；接手者按文件名以为它是引擎入口。不影响库本身。
- 复现/验证：`grep -rn "struct/values.h" head.cpp`；`ls metaclass/struct`。
- 建议：把 `head.cpp` 迁进 `test/`（作为示例入口）或删除这一行，别让它继续冒充引擎门面。

### A3. `test/polydtor_test` 目标漏列 `addr/` 下 5 个源文件，链接必然缺符号
- 位置：`test/CMakeLists.txt:156-172`（目标定义），`test/polydtor_test.cpp:54`
- 现状：`add_executable(polydtor_test …)` 的源列表里只有 `../network/nebula/addr.cpp`，**没有** `addr/addr_b.cpp`、`addr/addr_uuid.cpp`、`addr/addr_ipv4.cpp`、`addr/addr_serial.cpp`、`addr/addr_ipv6.cpp`。而该测试用到的正是 B 世代的 `nebula::addr_t<nebula::kind::ipv4>`（`:54`）、`clone()`（`:40`、`:76`）、`broadcast()`（`:45`），这些符号在 `addr/` 下的 TU 里。测试文件本身还停留在 A 世代 API。
- 证据：`sed -n '156,172p' test/CMakeLists.txt` 列出的 15 个源文件中 nebula 侧只有 `addr.cpp`；`grep -n "addr_t\|clone\|broadcast" test/polydtor_test.cpp` → `:40,:45,:54,:76`。
- 影响：该目标链不过；因为**无 CI、无 ctest**（见 C 组与 §1），没人跑就没人报。
- 复现/验证：`cmake -S test -B test/build && cmake --build test/build --target polydtor_test`（预期链接期缺 `addr_b`/`addr_ipv4` 等符号）。
- 建议：要么把它迁到 B 世代 API 并补齐源列表，要么直接在 `test/CMakeLists.txt` 里标为禁用并写明原因。

### A4. `dispatch_no.h` 的 `share` 继承了 `_array` 而不是为它准备的 `_share`
- 位置：`hardware/parallel/dispatch_no.h:58`（`share`）、`:39-42`（`_share`）、`:44-47`（`_array`）
- 现状：`struct share : public _array, public std::vector<type>` —— 「共享数据」却带上了「独占数据」的 `m_index` 成员；空结构体 `_share` 从此**全无人用**。作为对照，`:75` 的 `array` 也是 `: public _array, public std::vector<type>`，于是两个类型的布局与语义实际相同，「share / array」的区分形同虚设。
- 证据：`sed -n '39,47p;58p;75p' hardware/parallel/dispatch_no.h`；`grep -rn "_share" hardware/` 只有定义处 `:39-42`，无任何继承或使用。
- 影响：当前无实例化（CPU 并行整块未实现，见 E1），所以**不崩**；但一旦按 `dispatch_cpu.h` 里那份被注释的设计落地并行，`share` 会被当成可变状态的容器用，是个错误的地基。
- 复现/验证：`grep -rn "_share\b" hardware/`（应只有 `:39-42` 一处）。
- 建议：`share` 改继承 `_share`，或在落地并行前先删掉这对未定型的类型。

---

## B. 资源与并发

### B1. 「进程级槽池必须由宿主在 main 首行安装」是隐式契约，违反只在运行期抛异常
- 位置：`software/signal.h:49`（声明 `xhqm_io void pool_life();`）、`software/signal.cpp:18`（抛错处）、`software/signal.cpp:35-37`（安装实现）
- 现状：异线程触发的 `signal_auto` 走 `xhqm::signals::pool()`；池未安装时直接 `throw xhqm::error("signals::pool 未安装：main 首行请调用 xhqm::getInstance<signals::pool_life>();")`。安装动作没有任何编译期约束，也没有启动期自检，全靠每个宿主记得写。
- 证据：`grep -rn "pool_life" software/signal.h software/signal.cpp` → 声明 `signal.h:49`、抛错 `signal.cpp:18`、实现 `signal.cpp:35-37`；`software/signal.h:13` 为 `pool_life` 的 friend 依据做了前置声明。`network/nebula/link.h`/`link.cpp` 侧只用显式 `signal_direct`，所以**同一进程里是否踩雷取决于哪条链路被触发**。
- 影响：跨模块、跨宿主。命中条件＝宿主忘了安装 **且** 有异线程 `signal_auto`（如进程级 `uuid_broad`）；症状是运行期抛异常而非链接/编译错误。上一轮在实配里发现 `star_device` 全链路没调 `pool_life()`（其余节点入口都调了），后果未在本机复现。
- 复现/验证：`grep -rn "pool_life" software/` 看契约；在未安装池的宿主里从异线程触发一次 `signal_auto` 即可复现抛错。**本机未做该触发复现**。
- 建议：给 `pool()` 加一次性自检（首次调用即安装 + 打日志），把契约从「文档要求」变成「代码保证」。

### B2. astral 内核的 `datas` 只增不减：每个写入方都在给一个不裁剪的历史追加
- 位置：`network/astral/kernel.cpp:271`（`node.datas.push_back(rec)`）、`:500-508` 与 `:669-671`（保存路径的原子 rename）
- 现状：写节点即 `push_back` 进内核条目列表，**没有任何裁剪/合并**；持久化侧是「临时文件 + 原子 rename」的整体重写，也没有版本压缩。内存窗口的 `capacity`/`duration` 只影响窗口视图，不影响内核里的历史长度。消费方（如 nebula 的 `data` 层）每帧写内核且从不调 `remove_time`。
- 证据：`grep -n "push_back" network/astral/kernel.cpp` → `:271` 为写路径；全文件无 `erase`/`resize`/`shrink` 式的历史裁剪；`grep -rn "remove_time" network/nebula/` → nebula 侧无调用者。
- 影响：长跑进程 RSS 单调上升。上一轮在消费方 `stella` 侧观测到 `star_qqbot` 26.8 GB OOM 与此同源（消费方用 `size:4096` + mem_watch 做了缓解，内核裁剪仍未做）。触发条件＝任何长驻节点持续写同一节点通道。
- 复现/验证：`grep -n "push_back" network/astral/kernel.cpp`；内存侧未在本仓复现（本仓无长驻节点），**未复现**。
- 建议：给内核加显式上限开关或快照合并（属产品口径，见 F3）。

---

## C. 跨平台

### C1. `software/signal.h` 与系统 `<signal.h>` 同名，靠 `include_next` + MSVC 头序绕过，对包含顺序敏感
- 位置：`software/signal.h:9-13`、`CMakeLists.txt:51-72`
- 现状：`software/` 在 `-I` 搜索路径内（见 `CMakeLists.txt:247` 把 `software/signal.h` 收进 `XHQM_HEAD_CORE`），于是任何 TU 引 `<signal.h>`/`<csignal>` 都先命中它。非 Windows 侧靠 `signal.h:10` 的 `#include_next <signal.h>` 把真头拉回来，由 `:9` 的 `#if !defined(_WIN32) && !defined(_MSC_VER) && (defined(__GNUC__) || defined(__clang__))` 守住；MSVC 没有 `include_next`，改由 `CMakeLists.txt:51-72` 把 UCRT 头目录 `include_directories(BEFORE …)` 排到最前。
- 证据：`sed -n '9,13p' software/signal.h`；`grep -n "XHQM_UCRT\|UCRT include dir prepended" CMakeLists.txt` → `:58-72`（含 `:71` 的 `include_directories(BEFORE "${XHQM_UCRT}")` 与 `:72` 的 STATUS 提示）。
- 影响：GCC/Clang 下，若某 TU **先** `#include <sys/wait.h>`，glibc 会在自己的 `extern "C"` 里拉入这个同名头，使 `signal.h:13` 的前置声明落进 C linkage → `conflicting declaration of C function`。**本轮未复现**；上一轮已在 `network/README.md:344` 记录独立探针复现过，并给出两个方向的改法。另一个后果是**包含顺序成了隐式接口**，接手者改 include 顺序就可能踩雷。
- 复现/验证：写一个 TU 先 `#include <sys/wait.h>` 再 `#include "software/signal.h"`，用 GCC 编译。**本轮未跑**。
- 建议：把 `signal.h:13` 的前置声明移到 `#include_next <signal.h>` 之前（或改 friend 方案），让本头在 `extern "C"` 里也安全。

### C2. Windows 盘符路径硬编码：库源码已干净，残留集中在测试与 MSVC 链接选项
- 位置：`CMakeLists.txt:674`（`/PDB:D:/WorkSpace/xhqm_v2/binary/xhqm.pdb`）、`test/view/main.cpp:42,46,365`、`test/ffbo/CMakeLists.txt:13`、`test/nebula_api/` 与 `test/ffbo/*.bat`
- 现状：**库源码 0 处**盘符路径（唯一正则命中 `hardware/media.h:226` 是注释里的 `rtsp://…`，属误匹配）。真正写死的是 MSVC 的 PDB 输出路径与若干 demo/测试的资源路径。
- 证据：`grep -rn "[A-Za-z]:[/\\\\]" --include=*.h --include=*.hpp --include=*.cpp --include=*.mm --include=*.cu .` 排除 `.svn`/`build*`/`catch.hpp`/`json.hpp`/`httplib.h` 后，只剩 `hardware/media.h:226` 一条且为误匹配；含 `test/` 与 `*.txt`/`*.vcxproj` 的统计为 56 条命中，`test/catch.hpp` 占 27 条（第三方）。
- 影响：Linux/macOS 构建不受影响；在别人的 Windows 机器上，`:674` 会让 `/DEBUG` 的 PDB 被丢向一个不存在的 `D:/WorkSpace/...`；`test/view/main.cpp` 的三处路径让该查看器在非原作者机器上直接载不到模型。
- 复现/验证：`grep -rn "D:/WorkSpace" --include=*.cpp --include=*.txt --include=*.vcxproj .` → `CMakeLists.txt:674`、`test/ffbo/CMakeLists.txt:13`、`test/view/main.cpp:42,46,365`。
- 建议：PDB 路径改用 `${PROJECT_BINARY_DIR}`；demo 资源路径改为命令行参数或环境变量。

### C3. 渲染与 GPU 侧没有无显示的验证入口，导致该子系统在无 GPU 节点上无法回归
- 位置：`test/view/main.cpp`（943 行，唯一查看器入口）、`test/view/`（无 `README.md`）、`fantasy/fantasy_bugs.md:3`
- 现状：渲染验证依赖 `RhiViewport` 这种需要图形上下文的入口；`test/view/` 没有离屏/headless 的目标，也没有自己的 README。`fantasy/fantasy_bugs.md:3` 自陈「仅静态分析，未运行（fantasy 依赖视觉/图形上下文，本节点无渲染能力）」。
- 证据：`ls test/view/` → `build/ CMakeLists.txt main.cpp ModelLoader.h RhiViewport.cpp RhiViewport.h`（无 README）；`test/CMakeLists.txt` 的 25 个 target 里没有 view（view 是独立工程）。
- 影响：CI 或多平台矩阵一旦引入，渲染侧无法出一致性/黄金图结论；`fantasy` 的缺陷只能靠读代码发现（这正是 `fantasy_bugs.md` 的成因）。
- 复现/验证：`ls test/view/`；`grep -c "headless\|offscreen" test/view/main.cpp`。
- 建议：把 view 的就地验证抽成独立 headless 目标（离屏帧 + 校验和），让渲染至少有一层可自动跑的回归。

---

## D. 性能

本轮**未取得可复现的性能测量**，故不设 D 组 —— 按本文件纪律，宁缺不编。已知与性能相关的两个事实都属「有意取舍」，记在 F2（关停延迟以 poll 超时为上限）与 F3（写路径不做内核裁剪换历史可追溯）。

---

## E. 未实现 / 占位桩

> 本组全是「现在编译会过、只是没写完或没接上」的东西。

### E1. `dispatch_cpu.h` 整块被注释，CPU 并行实际不存在
- 位置：`hardware/parallel/dispatch_cpu.h:19-88`
- 现状：91 行的文件里，`#pragma once` + `#include "dispatch_no.h"` 之后，`array` / `task` / `execute` / `dispatch` 四段实现**全部是注释**，只留下空的 `namespace xhqm { namespace parallel { } }`。模块自己的 README 已如实记过这一点（`hardware/README.md:12` 「`dispatch_cpu.h` 里 0 行代码」、`:27` 「91 行里 0 行代码：整段实现被注释，占位」），此处登记是为了让根级读者看到「设备抽象只有选择、没有执行」。
- 证据：`cat -n hardware/parallel/dispatch_cpu.h` → `:19-88` 每行以 `//` 开头；实际生效代码只有 11 行。
- 影响：`hardware` 模块的设备抽象只有「选择」没有「执行」；谁按名字找 CPU 并行分块都会落空。
- 复现/验证：`grep -c "^\s*//" hardware/parallel/dispatch_cpu.h` 对比 `wc -l hardware/parallel/dispatch_cpu.h`。
- 建议：要么落地多线程分块，要么整文件删掉并让 `hardware/README.md` 停止宣称有并行骨架。

### E2. `socket::ipv6path()` 恒返回 false
- 位置：`network/socket.cpp:90-94`
- 现状：函数体只有 `return false;`，参数一个都没用。声明的语义是「IPv6 地址串 → host/port 解析」，实际是桩。同文件 `ipv4path()`（`:74-88`）是真实现。
- 证据：`sed -n '90,94p' network/socket.cpp`。
- 影响：任何走 IPv6 文本地址解析的路径静默失败（返回 false），调用方若只判返回值就当「地址不合法」处理。
- 复现/验证：`grep -n "ipv6path" -A 4 network/socket.cpp`。
- 建议：补实现或把声明从 `socket.h:52` 删掉，别留同名的真假混用接口。

### E3. AES 声明了 5 种模式，只有 ECB/CBC 能设、能跑
- 位置：`network/aes.h:26`（`enum class mode { ECB, CBC, CFB, OFB, CTR }`）、`network/aes.h:32-33`（仅 `set_mode_ecb` / `set_mode_cbc`）
- 现状：枚举里有 5 项，setter 只有 2 个；`network/aes.cpp` 里的 `switch` 只按密钥长度 `aes_128/aes_192/aes_256` 分派（`:65-78`），没有按模式分派的分支。CFB/OFB/CTR 既无 setter 也无实现。
- 证据：`grep -n "ECB\|CBC\|CFB\|OFB\|CTR\|GCM" network/aes.h`；`grep -n "switch\|case" network/aes.cpp` → 只有 `m_type` 分支。
- 影响：调用方看到枚举以为能选流模式，实际只能落在 ECB/CBC；`set_mode_cfb()` 之类根本不存在，是编译期就暴露的坑（比静默更安全，但仍属桩）。
- 复现/验证：`grep -n "set_mode_" network/aes.h` → 只有两个。
- 建议：补 CFB/OFB/CTR 与 GCM，或把枚举裁到已实现集合并写明。

### E4. nebula 的 `kind` 词表 7 项，`addr_from` 只会造 4 种
- 位置：`network/nebula/addr/addr_b.h:17-23`（`uuid, ipv4, ipv6, serial, bluetooth, nearlink, quantum`）、`network/nebula/addr.cpp:12-20`（`addr_from` 的 `switch`）
- 现状：`addr_from` 只处理 `uuid / ipv4 / ipv6 / serial`，其余落 `default: a = nullptr;`（`:20`），即**静默返回空句柄**，不抛不报。
- 证据：`grep -n "case nebula::kind::" network/nebula/addr.cpp` → 4 条；`sed -n '17,23p' network/nebula/addr/addr_b.h` → 7 个枚举项。
- 影响：配置里写 `"kind":"bluetooth"` 的节点会拿到 `nullptr` 地址，**看起来装配成功、实际入不了网**（与 F1 同一种静默失败模式）。
- 复现/验证：`grep -n "case nebula::kind" network/nebula/addr.cpp`。
- 建议：`default` 分支至少写一条日志/计数，别把「不支持」和「解析失败」做成同一个返回值。

### E5. tensor 宣称有 net 后端，实际没有对应实现文件
- 位置：`algebra/tensor/backend/backend.h:6`、`algebra/tensor/backend/`（无 `tensor_backend_net.*`）
- 现状：`backend.h:6` 的注释写「none / cpu / gpu / net 各为独立实现，运行时经 `tensor_backend_active()` 调度」，但目录里只有 `tensor_backend_no.* / _cpu.* / _gpu.* / _metal.*`，**没有 net 那一份**。
- 证据：`ls algebra/tensor/backend/`（13 个文件，无 net）；`grep -rn "tensor_backend_net" algebra/` → 0 命中。
- 影响：按注释理解调度链的人会以为 net 是可用后端；实际 `device::net` 只能落到串行/CPU 兜底。
- 复现/验证：`ls algebra/tensor/backend/ | grep -c net` → 0。
- 建议：把 `backend.h:6` 的注释改成「net 为预留」，或落地分布式后端。

### E6. `metaclass/thread/` 是空目录
- 位置：`metaclass/thread/`（`ls -la` 只有 `.` 与 `..`）
- 现状：目录存在但无任何文件，`metaclass/README.md` 的「已知问题」节记为「预留」。因为 SVN 不跟踪空目录，它连版本控制的状态都没有。
- 证据：`ls -la metaclass/thread/`。
- 影响：`metaclass/` 的目录结构与实际内容不一致，接手者会找不存在的实现。
- 复现/验证：`ls -la metaclass/thread/`。
- 建议：删除该目录，或填入实现并加进 `XHQM_HEAD_CORE`。

### E7. `test/sparse_mesh_test.cpp` 没有任何构建目标，编不到也跑不到
- 位置：`test/sparse_mesh_test.cpp`；`test/CMakeLists.txt`（全文件无该目标）
- 现状：该测试文件在树内存在，但 `test/CMakeLists.txt` 的 25 个 `add_executable` 里没有它；全仓搜索文件名 0 命中，说明也不在其它 CMake 工程里。
- 证据：`grep -n "sparse_mesh" test/CMakeLists.txt` → 0 命中；`grep -rn "sparse_mesh_test" --include=*.txt --include=*.vcxproj --include=*.md .` → 0 命中。
- 影响：`lattice/sparse_mesh.hpp` 的唯一专门测试从未被执行过；这条与 §2.7 的「可达性不齐」是同一个盲区（`sparse_mesh.hpp` 既不在 CMake 头集、也不在门面里）。
- 复现/验证：`ls test/*_test.cpp | wc -l`（25）对比 `grep -c add_executable test/CMakeLists.txt`（25）—— 数目相等但集合不等，差集就是它。
- 建议：补 target，或把文件移出 `test/` 以免冒充已覆盖。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 地址装配是宿主的责任，模块内不自动注入（`insert_addr` 无调用者）
- 位置：`network/nebula/link.h:54`（声明）、`network/nebula/link.cpp:71`（定义）、`network/nebula/README.md:387`
- 现状：全仓 `insert_addr` 只有这两处，**没有任何调用者**；`config` 里的 `answer` / `listen` 键也没有代码读取。
- 理由（为什么现在这样是对的）：传输与装配解耦 —— 同一逻辑节点可以同时挂 `uuid` 回环、`ipv4` 定向长连、`ipv4` 广播三种句柄，谁挂、挂几件由业务决定，库不该替它猜。设计意图已写在 `network/nebula/README.md:387`「地址装配是外部责任」。
- 代价：`zero(config)` 造出的节点**本地能力齐全但入不了网**，不抛不报，`degree()` 恒 0 —— 与 E4 同一种静默失败模式；排查时表现为「看得见、推不到、两侧无日志」。
- 验证：`grep -rn "insert_addr" --include=*.h --include=*.cpp .`（排除 `.svn`）→ 仅声明与定义。
- 建议：保持解耦，但给「无地址」状态加一条启动期警告日志。

### F2. 关停/唤醒以 reactor 的 poll 超时兜底，不做立即返回
- 位置：`network/nebula/addr/addr_ipv4.h:87`（`s_react_ms = 200`）、`addr/addr_ipv6.h:61`（`s_react_ms = 200`）、`addr/addr_serial.h:165`（`s_poll_ms = 100`）
- 现状：跨线程关闭不直接打断正在 `poll` 的 reactor 线程，而是等超时返回；发送侧另外做了「入队即唤醒 + 每轮 drain-until-empty」，因此**突发帧不丢**是设计目标之一（`addr_ipv4.h:19-20` 记了「无帧时实测 join 在 200ms 内返回」）。
- 理由：从任意线程强行 join/唤醒一个带锁的 reactor 容易引入锁序与自死锁（该模块 r660 刚修过 `exec_proxy` 的竞态/自死锁）。用有界超时换实现简单与可预测，是划算的。
- 代价：最坏关停延迟 ≈ `s_react_ms` + 本轮 handler 耗时（ipv4/ipv6 200 ms、串口 100 ms）；上层若在「关停必须立刻」的路径上用，会看到 200ms 级的尾巴。
- 验证：`grep -rn "s_react_ms\|s_poll_ms" network/nebula/addr/`。
- 建议：保持现状；把这三个常数提成构造参数，供对延迟敏感的上层覆盖。

### F3. astral 内核「只追加、不裁剪」，历史与内存一起留下
- 位置：`network/astral/kernel.cpp:271`；`network/astral/kernel.cpp:500-508`、`:669-671`
- 现状：写节点即追加，持久化用「临时文件 + 原子 rename」整体重写；内存窗口的 `capacity`/`duration` 只裁视图，不动内核历史。
- 理由：历史可追溯、写路径无需回读旧数据、也避免了在写路径上做压缩带来的锁与 IO 放大；`data` 层的 `remove` 也只删内存窗口而保留内核历史（这是有意语义，见 `network/nebula/README.md` 的 `data` 条目）。
- 代价：RSS 与磁盘随运行时长单调上升；消费方已被咬过（`star_qqbot` 26.8 GB，见 B2）。要不要裁剪是**产品口径**，不是纯技术选择。
- 验证：`grep -n "push_back" network/astral/kernel.cpp`。
- 建议：不改成默认裁剪，但补一个显式上限开关（默认关 = 与今天等价）。

### F4. `software/signal.h` 坚持与系统 `<signal.h>` 同名
- 位置：`software/signal.h:1-13`
- 现状：`signal` 是引擎的公共词汇（`signal_direct` / `signal_queued` / `signal_auto` / `signal_waited`，对标 Qt），改名会波及全仓调用点；代价是必须靠 `include_next` 与 MSVC 头序绕过（见 C1）。
- 理由：API 词表的一致性价值高于一个头文件的重名风险，且绕过手段已经封装在头文件内部与 CMake 里，普通使用者无感。
- 代价：包含顺序成为隐式接口（C1），且 `-I` 里出现 `software/` 就会全局影响 `<csignal>`。
- 验证：`sed -n '1,13p' software/signal.h`。
- 建议：保持命名，把 C1 的 `extern "C"` 安全性补上，让代价消失。

### F5. `xhqm/` 的 22 个门面头故意不带扩展名
- 位置：`xhqm/`（`algebra`、`astral`、`nebula`… 共 22 个无扩展名文件）
- 现状：写法是 `#include "xhqm/algebra"` —— 读起来像模块名而不是文件路径，这是有意为之。
- 理由：门面头表达的是「我要这个模块」，不带扩展名让人不会把它当普通头去单独引用。
- 代价：工具与脚本默认不认它们是源码 —— `find -name '*.h'`、`glob "**/*.h"`、编辑器的 C++ 索引都会**整批漏掉这 22 个文件**（本轮实测正是如此：`.h/.hpp/.cpp/.mm/.cu` 计数在 `xhqm/` 下为 0）。
- 验证：`ls xhqm/ | grep -v README.md | wc -l` → 22；`find xhqm -name '*.h*'` → 0。
- 建议：保持命名，在 `xhqm/README.md` 顶部把「无扩展名」这条写明（现已写明，见 2.1 节引用）。

### F6. `build/`、`build_lite` 走 `svn:ignore`，构建产物不入库 —— 但唯一的回归资产正放在里面
- 位置：`svn propget svn:ignore .` → `build` / `build_lite`；`build/nebula_b_check.cpp`（7787 B）、`build/star_contract_check.cpp`（4376 B）
- 现状：`svn ls -R` 共 527 个受控文件、**不含任何二进制产物**（`.so/.dll/.lib/.a/.exe/.obj/.pdb` 0 命中），源码树是干净的；但 nebula 的 30 项自检靶与 r677 的组件契约自检靶都只存在于这个**被忽略**的目录里。
- 理由：产物不入库是对的（避免污染源码树与仓库膨胀），这一点本文件不主张改。
- 代价：这两份自检是 nebula / star 契约目前**唯一的可执行回归**，换机器或 `rm -rf build` 即永久丢失；`test/` 里也没有对应目标（`nebula_api` 无构建入口，见 A3 与 §4.3）。
- 验证：`svn status build` → `I build`；`svn ls -R | grep -icE "\.(so|dll|lib|exe|pdb)$"` → 0。
- 建议：把 `nebula_b_check.cpp` / `star_contract_check.cpp` 移进 `test/` 并登记目标（保留 `build/` 忽略规则不变）。

---

## G. 文档与代码的出入

### G1. `rhi_vk.h` 的注释仍自称「占位实现」，与 4078 行的真实实现不符
- 位置：`fantasy/backend/rhi_vk.h:10`
- 现状：注释为 `// 工厂：创建 Vulkan 后端（当前为占位实现）`，而 `fantasy/backend/rhi_vk.cpp` 是 4078 行的完整实现，含 `vkCreateInstance`(2) / `vkCreateDevice`(2) / `vkCreateSwapchainKHR`(2) / `vkCreateGraphicsPipelines`(1) / `vkCreateDescriptorPool`(4) / `vkCmdDrawIndexed`(3) / `vkQueueSubmit`(3) 等共 264 处 `vk*` 调用，`create_vk_backend()`（`:4072-4076`）返回真实的静态后端单例。
- 证据：`grep -c "vkCreate\|vkCmd\|vkDestroy\|vkAllocate" fantasy/backend/rhi_vk.cpp` → 264；`wc -l fantasy/backend/rhi_vk.cpp` → 4078。
- 影响：这条注释**已经害过一次** —— 顶层 `README.md` 曾据此把整个 Vulkan 后端记为「占位」并写进模块评估（本轮已订正，见 G3）。凡沿用旧结论的文档都会继续错。
- 复现/验证：`sed -n '10p' fantasy/backend/rhi_vk.h`。
- 建议：删掉那半句注释。

### G2. `CMakeLists.txt:356` 的「待同步」注释已过时
- 位置：`CMakeLists.txt:355-356`
- 现状：注释写「A 世代的 notify/sse_server/… 已从 network/nebula/ 移除；**门面头 xhqm/nebula 与 network/README.md 仍引用旧名，待同步**」。实测门面头侧已同步完毕：`xhqm/nebula` 只引 B 世代 9 个头并附一行 A 世代移除说明，`xhqm/` 全 22 个门面头的 113 条 `#include` **零悬空**。
- 证据：`cat xhqm/nebula`（9 条 include + 1 行说明）；逐条解析 22 个门面头的 include 目标 → `total includes=113 missing=0`。
- 影响：接手者会去找一个不存在的同步任务，或据此怀疑门面头是坏的（上一轮 `xhqm/README.md:240` 刚订正过一条同类误判）。
- 复现/验证：`for f in xhqm/*; do grep -oP '(?<=#include ")[^"]+' "$f"; done` 逐条 `test -e "xhqm/$inc"`。
- 建议：把该行改成「门面头已同步（r677 实测 113/113 命中）」或直接删除。

### G3. 顶层 `README.md` 本轮订正的五处漂移（保留备查，防复发）
- 位置：`README.md`（本文件同目录）第 2.1 / 2.3 / 2.7 / 3.1 / 3.2 节
- 现状：原文与本轮实测的差异共五处，全部已在 `README.md` 内以 `~~旧文~~ **已作废（2026-10-07 复核 r677）**` 标注，此处只做索引：
  1. §2.7 称 lattice 的 `directed_mesh/matrix_mesh/graph_algorithms` **原文件树缺失** → 三个文件都在且都在被使用；真问题是**门面可达性不齐**。
  2. §3.2/§3.1 称 Vulkan 后端**标注占位** → 是实现完整的 4078 行（源头即 G1 的过时注释）。
  3. §3.1 称 fantasy bug 清单「**53+ 项已修**」 → 台账实际为**编号 1–43**，仅 #21/#22 待定。
  4. §2.1 称 `XHQM_USE_*` 与 `XHQM_BUILD_*`「**一一对应**」 → 实际 **7 ↔ 4**，且名字对不上。
  5. §2.3 的 `linalg` 行数 1226 → 实为 **1238**；§3.2 的 `rhi_vk.cpp` 行数 3669 → 实为 **4078**。
- 证据：`grep -n "已作废（2026-10-07 复核 r677）" README.md`。
- 影响：这五条的共同成因是本仓的系统性模式 —— **结论被推翻后只改了最先发现的那处，别的文档不回填**（`network/nebula/README.md`、`DEFERRED.md` 早已订正，顶层 README 直到本轮才跟上）。不记下来的话，下一次换人复核还会复读旧结论。
- 复现/验证：`grep -n "已作废" README.md`。
- 建议：作为仓库级约定，凡写「某某导致编译失败 / 某 API 不存在 / 某文件缺失」，必须同时写复现命令与验证版本。

### G4. 问题台账的命名与位置不统一：`DEBUG.md` / `*_bugs.md` / `DEFERRED.md` / 各类计划文档并存
- 位置：全仓 `README.md`（**29 份**）与各类台账（`DEBUG.md`、`fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md`、`test/nebula_api/PROTOCOL.md`、`test/ffbo/RENDER_FIX_PLAN.md` 等）；`*.md` 合计 42 份
- 现状：29 份 README 都在讲「定位/优势/劣势/缺失/建议」，本身不是问题台账。台账这一层历史上有三种形态，且都**保留原名原名位置**（按本仓约定不搬家、不改名）：`fantasy/fantasy_bugs.md`（静态分析清单 + 2026-08-22 起逐轮修复记录）、`network/nebula/DEFERRED.md`（「决定不做」的事项）、以及 `test/ffbo/` 与 `test/nebula_api/` 下的一批计划/评审文档（`DESIGN_NOTES.md`、`RENDER_FIX_REVIEW.md`、`VDTOR_CANDIDATES_VERDICT.md`、`DEND_RANK_K.md`…）。**`DEBUG.md` 这一命名是 2026-10-07 本次全仓复核才引入的**：根（本文件）与若干模块在同一轮里各自建立（同批可见 `hardware/DEBUG.md`、`lattice/DEBUG.md`、`algebra/geometry/brep/DEBUG.md`），所以「每模块一份 `DEBUG.md`、历史台账保持原样并互相指引」是**正在形成中的约定，尚未覆盖全部模块**。
- 证据：`find . -path ./.svn -prune -o -name "DEBUG.md" -print`；`find . -path ./.svn -prune -o -name "README.md" -print | wc -l` → 29；`find . -path ./.svn -prune -o -name "*.md" -print` 共 42 份。
- 影响：接手者要判断「这条缺失现在还在不在」，得先在 README 的「缺失」节、`DEBUG.md`、以及两种旧命名台账之间找对地方 —— 本轮花在核实旧说法上的时间，大部分来自这个不统一。反过来，只要约定落地，这类问题会随覆盖度提高而收敛。
- 复现/验证：`ls */DEBUG.md */*/DEBUG.md */*/*/DEBUG.md 2>/dev/null`；`grep -rln "当前仍存在\|仍存在" --include=*.md .`
- 建议：未建 `DEBUG.md` 的模块补齐（照本文件的分组与条目格式），旧命名台账保持不动、只在各自 README 顶部注明「历史台账在此」。

---

> 本轮**未核实**、因而未写入以上任何一组的待办：`fantasy/spatial` 的 `extract_planes` 重复实现、`slot_of` 的 O(n²) 复杂度、`fantasy/postfx` 的算法简化数值（Bloom 单次 3x3 / FXAA 5-tap / SSR 16 步）、`fantasy/animation` 的「clip 采样欧拉角线性插值」、3.2 节「点光 cube 阴影 far 硬编码 10」、5.1 节「`packet::save` 未确保目录存在」，以及 nebula「13 个 TU 两路构建实测链接通过」这一构建结论（本轮只核对了文件集与工程条目的一致性，未重跑构建）。这些需要各自的复现命令，留给下一轮。
