# software 软件基础设施模块

> 最后同步：2026-10-07，对照工作副本 r677。

`xhqm` 引擎的软件层通用组件库。提供事件/信号槽、数据序列化（datum）、对象树与数据包（packet）、消息/命令/工程/工具四大运行时容器，以及 `system`（系统工具：线程池、进程、文件、时间、动态库、编码）与 `pattern`（设计模式：单例、观察者、工厂、命令、责任链、策略、通知者、状态机）两大子模块。是渲染、网络、应用框架等上层模块共用的底层胶水。

## 目录结构

```
software/
├── event.h           事件/键码/鼠标码/连接类型枚举
├── signal.h/.cpp     信号槽（对标 Qt，异线程走线程池）
├── datum.h/.cpp      类型化数据序列化（JSON/文件）
├── packet.h/.cpp     对象基类 object + 数据包 packet（对象树 + 工厂注册）
├── message.h/.cpp    消息与消息队列容器
├── command.h/.cpp    命令与命令容器（redo/undo）
├── project.h/.cpp    工程与工程容器
├── utility.h/.cpp    工具（插件）与工具容器
├── system.h/.cpp     顶层系统聚合根 system_root
├── system/           系统工具子模块（见下）
│   ├── process.h     聚合头（转 process/）
│   ├── threads.h     聚合头（锁 / 常驻线程 / 任务 / 线程池）
│   ├── process/      进程封装（对标 QProcess）
│   ├── threads/      线程池 / 任务 / 锁 / 常驻线程
│   ├── filesys.*     文件系统
│   ├── codesys.*     编码转换 / 字符串
│   ├── timesys.*     时间与计时器
│   ├── execsys.*     进程执行简化封装（含批量并发）
│   └── libmange.*    动态库加载（dlopen/LoadLibrary）
└── pattern/          设计模式子模块（见下）
    ├── thread_mode.h 线程策略标签
    ├── instance.*    类型安全单例
    ├── observer.h    观察者
    ├── factory.h     工厂（注册表）
    ├── command.h     命令 + 撤销/重做栈
    ├── chain.h       责任链
    ├── strategy.h    策略
    ├── notifier.h    通知者（函数指针版）
    └── state_machine.h 状态机
```

本模块**没有** `software.h` 聚合头；仓库级聚合走根 `xhqm.h`（`#define XHQM_USE_SOFTWARE`）→ `xhqm/software`（逐文件转发到 `pattern/state_machine.h`）。注意该转发头**不含** `software/system.h` —— 要用 `system_root` 与四类容器必须自己 `#include "software/system.h"`（`xhqm/software` 里没有这一行）。另有单点转发头 `xhqm/signal`、`xhqm/threads`、`xhqm/toolset`、`xhqm/pattern`、`xhqm/datum`、`xhqm/event`。

## 模块定位

- **事件/信号槽**：跨线程解耦通信，`signal` 对标 Qt，异线程槽经 `xhqm::parallel::threads` 线程池派发。
- **数据（datum）**：以「名称 → 类型化指针」的注册表把任意基础类型、`datum`、`std::vector` 统一序列化为 JSON，是对象持久化的基础。
- **对象/数据包（object/packet）**：所有软件对象继承 `derive_tree` 组成对象树；`object` 持有 uuid/name/icon/info/type/attribute 元数据，`packet` 是其软件层包装，并带全局工厂注册表 `regist`。
- **系统工具（system）**：跨平台系统能力封装，`timesys`/`filesys`/`codesys` 偏纯工具，`threads`/`process`/`libmange` 偏底层能力。
- **设计模式（pattern）**：轻量、header-only、带单线程/多线程双特化的通用模式实现。

---

## 信号槽 signal（`signal.h`）

对标 Qt 的信号槽，异线程槽经 `xhqm::parallel::threads` 线程池派发。

```cpp
#include "software/signal.h"
using xhqm::software::signal;
using xhqm::signal_type;

signal<int> sig;

sig.connect([](int v) { std::cout << v; });          // 默认 signal_auto
sig.emit(42);                                        // 同线程直接调用，异线程投递线程池
sig(42);                                             // operator() 等价 emit
```

### 连接类型（对齐 Qt）

| 枚举 | Qt 对应 | 行为 |
|---|---|---|
| `signal_direct` | DirectConnection | 发射线程立即调用 |
| `signal_queued` | QueuedConnection | 提交线程池异步执行 |
| `signal_auto`（默认） | AutoConnection | 同线程 direct，异线程 queued |
| `signal_waited` | BlockingQueuedConnection | 提交线程池并阻塞等待完成（同线程则直接调用） |

### 槽类型

```cpp
signal<int> sig;

// 1. lambda / std::function
sig.connect([](int v) { ... }, signal_type::signal_auto);

// 2. 普通函数
void free_slot(int v);
sig.connect(&free_slot, signal_type::signal_direct);

// 3. 成员函数（含 const 重载）
struct receiver : xhqm::software::signal_receiver {
    void on_value(int v) { ... }
    void on_value2(int v) const { ... }
};
receiver r;
sig.connect(&r, &receiver::on_value, signal_type::signal_auto);
sig.connect(&r, &receiver::on_value2, signal_type::signal_queued);
```

### 自动断开

槽绑定的对象继承 `xhqm::software::signal_receiver` 后，对象析构即自动断开，`emit` 会跳过已销毁的接收者，不会悬垂：

```cpp
{
    receiver r;
    sig.connect(&r, &receiver::on_value);
    sig.emit(1);  // 有效
}                 // r 析构，自动断开
sig.emit(2);      // 槽被跳过，安全
```

未继承 `signal_receiver` 的对象不自动断开，调用方自行保证生命周期。

### 断开 / 线程池

```cpp
sig.disconnect(&r);      // 断开某接收者的全部槽
sig.disconnect_all();    // 断开全部

// 异线程槽要先装好进程级槽池：池本体是 new threads(2, 4)，随进程回收
xhqm::signals::pool_life();          // main 首行调用一次；未安装时 pool() 直接抛 xhqm::error
xhqm::parallel::threads pool(2, 8);
signal<int> sig2(&pool);             // 也可给 signal 显式指定自己的线程池，绕过全局池
```

> 注意：`emit` 先对槽列表做快照再派发，槽回调内可安全地 connect/disconnect；`signal` 不可拷贝、不可移动。
>
> **`pool_life()` 是硬约束**：`signal_queued` 与「跨线程 `signal_auto`」两条路径会调 `xhqm::signals::pool()`，而它在未安装时抛 `xhqm::error("signals::pool 未安装…")`（`signal.cpp:14-20`）。装池入口只有 `xhqm::signals::pool_life()`（`signal.cpp:35-38`，内部 `getInstance<_pool_life_>()`）；**别照抄异常文本里那句 `xhqm::getInstance<signals::pool_life>()`** —— `pool_life` 是函数不是类型，照抄编译不过（本机实测）。仓内目前没有调用方调它（详见 `DEBUG.md` A15 与根 `DEBUG.md` B1）。
>
> **自动断开的边界**：`signal_receiver` 的存活标志只在 `emit` 派发前查一次（`signal.h:146`）。同线程 `direct` 槽因此安全；但 `queued`/跨线程 `auto` 槽把 `object` 原始指针捕获进了投递任务（`signal.h:107-110,160-163`），**接收者在任务真正执行前析构就会悬垂**（见 `DEBUG.md` B1）。需要严格生命周期时用 `signal_direct`，或自己在槽里判活。

> ~~重要实现细节：本文件位于 `-I` 搜索路径且名为 `signal.h`，会遮蔽系统 `<signal.h>`；非 Windows 上通过 `#include_next <signal.h>` 先引入真正的系统头（SIGTERM/SIGKILL/kill 等）再定义信号槽。使用该模块的 TU 引用系统信号相关头时需注意此遮蔽行为（`process_base.cpp` 内已显式处理）。~~ **已作废（2026-10-07 复核 r677）**：遮蔽与 `include_next` 本身都属实，但「注意事项」把重点写反了 —— **直接 include 系统信号头是安全的**（`include_next` 已生效），真正的坑是**包含顺序**，见下。

> **遮蔽系统的 `<signal.h>`（`signal.h:9-13`，行号与结论以本工作副本为准）**：本文件位于 `-I software/` 路径内且与系统头同名，本工程 TU 的 `#include <signal.h>`/`<csignal>` 命中的都是本文件；它在非 Windows 编译器上先 `#include_next <signal.h>` 把真正的系统头拉进来，所以**直接 include 系统信号头是安全的**（本机 g++ 11.4 + `-std=c++17`：单独 include `<signal.h>`、`<csignal>` 均 0 error，`SIGTERM/SIGKILL/kill` 都可用）。
>
> **真正的隐患是「包含顺序」**：glibc 的 `<sys/wait.h>` 在自己的 `__BEGIN_DECLS`（即 `extern "C"`）里 `#include <signal.h>`（`/usr/include/x86_64-linux-gnu/sys/wait.h:27,36`）。若某 TU **先** include `<sys/wait.h>`，本文件就落进 C linkage，`signal.h:11` 的 `pattern/instance.h` → `head.h` → `<map>` 一路连锁报错。本机实测：
>
> ```sh
> printf '#include <sys/wait.h>\nint main(){return 0;}\n' \
>   | g++ -std=c++17 -fsyntax-only -x c++ -I. -Isoftware -     # 5091 个 error
> # 包含链：sys/wait.h:36 → signal.h:11 → pattern/instance.h:3 → head.h:33 → <map>
> # 首条：bits/functexcept.h:101 conflicting declaration of C function
> # 随后：ext/numeric_traits.h:50 template with C linkage
>
> printf '#include <signal.h>\n#include <sys/wait.h>\nint main(){return 0;}\n' \
>   | g++ -std=c++17 -fsyntax-only -x c++ -I. -Isoftware -     # 0 error（guard 已置位）
> ```
>
> `process_base.cpp:22,24` 正是靠「先 `<signal.h>`、后 `<sys/wait.h>`」这个顺序才编得过，同文件还补了 `kill`/`SIGTERM`/`SIGKILL` 的手工声明兜底。MSVC 由 CMake 把 UCRT 头目录前置规避（`CMakeLists.txt:51-75`）；MinGW（`__GNUC__` 且 `_WIN32`）两条路都不走，见 `DEBUG.md` C2。

---

## 事件 event（`event.h`）

事件/键码/鼠标码/连接类型等枚举，供输入与信号槽共用。

| 枚举 | 含义 |
|---|---|
| `event_type` | `no` / `key` / `mouse` / `timer` |
| `key_type` | 键码（0-9、A-Z、小键盘、F1-F12、功能键、浏览器/媒体键、标点键等） |
| `mouse_type` | 鼠标键与按键 |
| `signal_type` | 信号槽连接类型（见上） |
| `timer_type` | `signal_pot` / `signal_now` / `signal_end` |

基类 `xhqm::event` 仅含 `event_type type`，供派发用。

> **包含要求**：`event.h` 用了导出宏 `xhqm_io` 却没有 include `../head.h` —— 单独 `#include "software/event.h"` 编不过（本机实测 8 个 error，`xhqm_io` 未定义；仓库自带的转发头 `xhqm/event` 同样 8 个）。先包含 `software/signal.h`、`software/datum.h`、`software/packet.h` 或根 `xhqm.h` 任一即可。见 `DEBUG.md` A14。

---

## 数据序列化 datum（`datum.h` / `datum.cpp`）

以「名称 → 类型化指针 + 读写函数」的注册表组织数据，支持嵌套 `datum`、`std::vector` 与文件/JSON 互转。底层是手写的 JSON 切分器（`_split_json_object_` / `_split_json_array_` / `_split_json_value_`）。

```cpp
xhqm::datum d;
int  a = 1;
bool b = true;
std::vector<double> v = {1.0, 2.0, 3.0};

d.resigt("a", &a);          // 基础类型：默认走 save_data/read_data
d.resigt("b", &b);
d.resigt("v", &v);          // vector：默认走 save_list/read_list

std::string json;
const xhqm::datum* ptr = &d;
xhqm::datum::save_json(ptr, json);   // {"a":1,"b":true,"v":[1.000000,2.000000,3.000000]}
xhqm::datum::save_file("/tmp/d.json", d);
xhqm::datum::read_file("/tmp/d.json", d);
```

数值走 `std::to_string`（`datum.h:135`），所以浮点是**固定 6 位小数**、整数无小数（上面注释是实测格式，不是「1.0」）。`save_json`/`read_json` 取的是 `const xhqm::datum*&`（指针的引用）、`save_file`/`read_file` 取 `xhqm::datum&`，按签名传左值即可。

`datum` 本体不可拷贝、不可移动（`datum.h:269-270`），只能就地注册。

类型通过 `datum_data_v<type>` 特化映射到 `uint_08`…`real_ld`/`text_t`/`bool_t`/`datum_t` 等标签。自定义类型可传自定义 `sptr`/`rptr` 回调覆盖默认序列化。未特化的类型落到主模板 `file_t`（`datum.h:69`），保存时报 `"save file error"` 并返回 false。

> ~~注意：`read_list` 要求目标 `std::vector` 已预分配与 JSON 数组等长的元素（不做扩容）；`save_list` 对空列表会产生 `[]`。~~ **已作废（2026-10-07 复核 r677）**：前半句成立（`datum.h:251` 要求 `elems.size() == data_ptr->size()`，不等即返回 false，不扩容）；后半句不成立 —— 空 vector 写出的是**一个 `]`**（`datum.h:225,233`：`values = "["` 之后 `values.back() = ']'` 把那个 `[` 覆盖掉了），既不是 `[]` 也不合法，而且函数仍返回 true。空数组往返必然失败（读回时 `_split_json_array_("]")` 得 1 个元素 ≠ 0）。见 `DEBUG.md` A2。

> **读侧的坑（一句话版，细节见 `DEBUG.md`）**：① `text` 反序列化是「砍掉首尾各一个字符」而不是剥引号（`datum.h:160-166`），非字符串值会被剪成残缺串，单字符值还会踩 `erase(end()-1)` 的未定义行为（A3）；② `read_json` 要求字段**个数相等且顺序一致**，名字不匹配就**静默跳过**、最后仍返回 true；空 `datum` 读一个不存在的文件同样返回 true（A4）；③ `save_data<text>` 对值不做任何转义，字段名也不转义（`datum.h:120`、`datum.cpp:268-270`）——含引号/反斜杠/逗号的值写出去就回不来（A1）。

---

## 对象树与数据包 packet（`packet.h` / `packet.cpp`）

```cpp
// object：对象基类，继承 derive_tree 组成对象树，持有元数据
class object : public xhqm::derive_tree<xhqm::object> {
    // type/uuid/name/icon/info（text），attribute（datum 注册表）
    template<typename type> type& attribute(const xhqm::text& name);  // 取元数据字段
    template<typename type> type* type_ptr();                          // dynamic_cast 包装
};

// packet：软件层数据包，object 的子类包装
class packet : protected xhqm::object {
    // select/active 标志，save/read 序列化 attribute 到 /packet/<uuid>
    software::packet* root_ptr();      // 返回所在对象树根
    template<typename type> type* dynamic_ptr();  // 类型安全下行转换
};
```

### 工厂注册 regist

```cpp
// 派生类型通过静态注册进入全局工厂表（按类型名创建 object*）
template<typename _object_type> class regist;
struct object_type { template<typename _type> static xhqm::text name(); };

// 宏：在派生类所在命名空间注册
#define xhqm_regist(type, space, _regist) ...
// 宏：动态库导出工厂函数（配合 libmange）
#define xhqm_regist_create(object_type) xhqm::object* create_object() { ... }
```

`regist<T>` 构造时把 `T` 的创建器写入全局单例 `instance<std::map<text, function>>`，配合 `object_type::name<T>()` 实现按类型名的运行时反射式创建（插件/动态库场景常用）。键是 `xhqm_regist` 拼出的**全限定名字符串**（`#space "::" #type`，如 `"xhqm::software::packet"`），`space` 只能填命名空间名、必须与类所在命名空间一致；`regist` 写入时 `type` 为空则抛 `std::string("unknow object_type")`（`packet.h:68-71`），而 `object_type::name<T>()` 对**从没注册过**的 `T` 只会静默返回空串。

> `xhqm_regist(type, space, _regist)` 的第三个参数 `_regist` 在宏体里**从未被引用**（`packet.h:127`，调用处一律留空），是历史残留。见 `DEBUG.md` G3。
>
> 对象树只存裸指针、不负责释放（`lattice/tree.hpp:798-809` 的 `_add_child_ptr`/`_sub_child_ptr` 只改父指针），所以 `packet::root_ptr()` 拿到的根是否真是 `software::packet` 取决于树根类型，`dynamic_cast` 失败返回 nullptr 而不是报错。

---

## 运行时容器（message / command / project / utility）

四类 `packet` 派生容器，共同挂在 `system_root` 下：

```cpp
class system_root : public packet {
    message_root* get_message_root();   // 子 0：消息队列
    command_root* get_command_root();   // 子 1：命令容器
    project_root* get_project_root();   // 子 2：工程容器
    utility_root* get_utility_root();   // 子 3：工具（插件）容器
};
```

- **message**：`message(text, level)` 记录文本 + 级别（`msg_txt`/`msg_log`/`msg_info`/`msg_warn`/`msg_error`）+ 时间戳；`message_root` 用 `std::queue` 缓存并批量写日志。`save()` 是**排空式**的：把队列里的消息全部写出并 `pop` 掉（`message.cpp:42-50`），文件名是 `message/<epoch 秒>.log`、目录要先存在。
- **command**：`command(name)` 抽象基类，派生实现 `_redo()/_undo()`；`redo()/undo()` 带 `m_active/m_sucess` 状态机；`command_root` 提供命令容器接口。`read_command("cmd a b", cmd, values)` 按空格切分（`codesys::split_string`，**跳过空项、不处理引号**）。
- **project**：`project` 作为 `packet` 的容器（`push_packet`/`move_packet` 就是 `add_child_ptr`/`sub_child_ptr`，只改父子指针、不接管所有权），`project_root::projects()` 列出所有子工程，`project_ptr(index)` 的 `index` **不做边界检查**。
- **utility**：`utility` 抽象基类（`enter()/leave()/rece()`），定位为可动态加载的工具/插件；`utility_root::save` 把子项 uuid 逐行写进 `utility/utility.list`，`read` 用 `file >> uuid` 只取**第一条**。

> 注意（逐条核过）：`command_root::redo/undo/save/read`、`command::save/read`、`project_root::save/read`、`message_root::read`、`utility::save/read` 都是**空函数体**；`utility_root::read` 加载动态库后只有 `int a = 0;`（`utility.cpp:56-65`）。`system_root` 的 4 个子容器是 `new` 出来的、对象树不管释放，也没有配套释放入口（`system.cpp:10-13`）。完整清单与行号见 `DEBUG.md`（E 组 / B2）。
>
> ~~`command_root` 的 redo/undo、`project_root` 的 save/read、`utility_root::read` 目前多为空实现或占位（见「已知缺口」）。~~ **已作废（2026-10-07 复核 r677）**：结论方向对但清单不全 —— 除了这三处，`command_root` 的 save/read、`command::save/read`、`message_root::read`、`utility::save/read` 同样是空的（`command.cpp:36-42,65-85`、`message.cpp:54-57`、`utility.cpp:17-23`、`project.cpp:24-30`）。

---

## system 系统工具子模块（`system/`）

### 线程池 threads（`system/threads/`）

```cpp
#include "software/system/threads.h"

xhqm::parallel::threads pool(2, 4);   // 2 核心，最多 4 线程，空闲 1s 回收

// 提交任务，返回 holder（内含 promise/future）
auto h = pool.submit<int>(0, [](int a, int b){ return a + b; }, 1, 2);
h.future_ptr()->get();                 // 3

// 带超时的提交：超时则置取消
pool.submit_task(0, h, std::chrono::milliseconds(100));
```

- `target`：任务单元（`create/cancel/runing/failures/successes` 状态机，CAS 保证只完成一次）。
- `holder<return_type>`：封装 `promise/future` + `target`，支持取消、取结果（可拷贝/可移动，拷贝共享同一个 promise）。
- `threads`：核心线程 + 允许线程 + 任务上限 + keep-alive，按通道（channel）分队列；生命周期 `life()/stop()/join()/dath()`。默认构造是 `threads(1, 1, xhqm::npos, 1000ms)`（`threads_pool.h:105-106`）——**排队上限默认无限**；核心线程数必须 ≥1 且 ≤ 允许线程数，否则构造抛 `xhqm::error`（`threads_pool.cpp:23-34`）。`join()`/`dath()` 在池内任务里调用会抛异常。
- `submit_task(channel, holder, timeout)`：`timeout` 为 `0` 等同只提交；到期只做 `set_cancel()`，而取消靠 CAS `create→cancel`，**已经开跑的任务取消不掉**（`threads_pool.cpp:415-435`、`threads_task.h:257-260`）。
- `dqueue`：通道队列（`std::deque<target::ptr>`）。
- `lock_mutex`/`lock_get`/`lock_set`：`std::shared_mutex` 的读/写 RAII 包装。
- `exist`：常驻线程基类（`redoexcept` 一次 / `runexcept` 循环 / `undoexcept` 收尾）；`exist_this<T>` 用回调绑定。

### 进程 process（`system/process/process_base.h`）

对标 QProcess 的跨平台进程封装：

```cpp
xhqm::parallel::process proc;
proc.command("ls -la");            // command 模式：shell 执行（/bin/sh -c 或 cmd.exe /c）
// proc.program("/bin/ls", {"-la"});  // program 模式：直接执行
// proc.command_line("a | b");        // 原始命令行：POSIX 仍套 sh -c，Windows 整串直用
proc.capture_stdout(true);
proc.stderr_stdout(true);          // 合并 stderr 到 stdout
proc.exec();
proc.wait();
auto r = proc.get_result();        // result{exit_code, exit_stat, exec_error, exec_msg, exec_time,
                                   //        terminated, term_signal, exec_out, exec_err}
if (r.success()) { ... }
```

`result::success()` = `exit_code == 0 && exec_error == none`（`process_base.h:48`）。支持 `timeout`、环境变量、工作目录、输入/输出回调（`function_i`/`function_o`/`function_b` 原始字节）、`throw_on_error`，以及长驻进程的 `pid()`/`wait_for(ms)`/`terminate()`/`terminate(grace_ms)`。

> 两个语义要点：① `wait()` 会先关 stdin 写端再等（`process_base.cpp:611`），带 timeout 时超时会 `SIGTERM` 进程组→等 1s→`SIGKILL`（`process_base.cpp:650-676`），`term_signal` 记录的是首次发出的信号号；② **析构是 `wait(10)`**（`process_base.h:70`）—— 对象析构会把超时强制改成 10ms，仍在跑的子进程（含整棵进程树）会被温和终止再强杀。要求子进程活过作用域就得自己持有/转移。`exec_out`/`function_o` 是**按行**语义且**空行会被丢掉**（`process_base.cpp:131`），二进制流走 `function_b`。见 `DEBUG.md` B4/B6。

### 简化进程执行 execsys（`system/execsys.h`）

```cpp
auto r = xhqm::execsys::execute_cmd("ls -la");
auto r = xhqm::execsys::execute_program("/bin/echo", {"hi"});
auto rs = xhqm::execsys::execute_bat(cmds, 4);  // 批量并发执行（原子游标 + N 线程）
```

`execute_bat(commands, max_concurrent)` 按 `max_concurrent` 起工作线程、用 `std::atomic` 游标分发下标（`execsys.cpp:29-60`）；结果与 `commands` 等长。注意 `max_concurrent == 0` 时**不会起任何线程**，但返回的结果是默认构造的 `result`，`success()` 全为 true（`execsys.cpp:33`）—— 见 `DEBUG.md` A9。

### 文件系统 filesys（`system/filesys.h`）

全静态类，基于 `std::filesystem`：`is_dir/is_file/is_exists`、`dir_list/file_list/path_list`、`rename/move_dir/move_file/copy_dir/copy_file/create_dir/create_file/delete_dir/delete_file`，以及 `env_value/data_directory/temp_directory/path_directory/work_directory/current_path`。

> **返回值口径不一致**：`dir_list` 返回的是**完整路径**（`ite.path().string()`，`filesys.cpp:49`），`file_list` 只返回**文件名**（`ite.path().filename().string()`，`filesys.cpp:61`），而 `path_list` 是两者的拼接（`filesys.cpp:65-74`）—— 拿 `dir + "/" + item` 拼路径时目录项会错。另外 `create_dir` 用的是 `create_directory`（父目录不存在即失败，已存在也返回 false，`filesys.cpp:119-127`），不会「确保目录存在」。见 `DEBUG.md` A11/B5。
>
> `path_directory()` 与 `work_directory()` 的实现**逐行相同**（都取可执行文件所在目录并各自缓存，`filesys.cpp:238-274` / `276-312`），`work_directory()` 并不是进程工作目录（那要 `current_path()`）。见 `DEBUG.md` A16。

### 时间 timesys（`system/timesys.h`）

```cpp
xhqm::timesys::mtime();       // UTC 毫秒时间戳（还有 ntime/utime/stime）
xhqm::timesys::msleep(100);   // 休眠（nsleep/usleep/msleep/ssleep）
xhqm::timesys::localtime();   // 实际格式 "YYYY/M/D H:M:S:mill"，如 "2026/10/7 1:33:0:123"
xhqm::nowtime nt = xhqm::timesys::systemtime();  // 年月日时分秒毫秒分解
```

`localtime()` 用 `/` 分隔日期、字段**不补零**（`timesys.cpp:46-52`，逐字段 `std::to_string` 拼 `"/"` 与 `":"`）；`timesys::day/week/year/month` 四个时长别名在 MSVC 的 `_HAS_CXX20` 下才有（见 `DEBUG.md` C1）。

> ~~`xhqm::timesys::localtime();   // "YYYY-MM-DD HH:MM:SS"`~~ **已作废（2026-10-07 复核 r677）**：实际格式是 `"YYYY/M/D H:M:S:mill"`（斜杠分隔、时分秒与毫秒都不补零），与子文档 `software/system/README.md:122` 的 `"2025/9/9 1:33:0:123"` 一致。

```cpp
xhqm::timer t;                // 计时器
```

~~计时器：`uptimer/runtime/utimes/mtimes/stimes/run_time`~~ **已作废（2026-10-07 复核 r677）**：六个数值接口可用（`timesys.h:97-102`，全部是纳秒差值换算：`runtime()` 纳、`utimes()` 微、`mtimes()` 毫、`stimes()` 秒）；文本三件套里只有 `old_time()` 正确（起点的本地时间，`timesys.cpp:62-65`），`now_time()` 把「已耗时毫秒」当 epoch 传入（得到的是 1970 年附近那一瞬的时刻），`run_time()` 又多减了一次 `m_starns`、在无符号域里下溢（本机按 2026-10-07 的 `m_starns` 算，落在约公元 2498 年）——两者都算不出「耗时」，见 `DEBUG.md` A12。

### 编码 codesys（`system/codesys.h`）

UTF-8 字符串相关：`string2hexa/hexa2string`、`FILE_BINtoBXT/FILE_BXTtoBIN`（二进制文本互转）、`unicode_to_string/string_to_unicode`、`split_string`、`utf8_to_system/system_to_utf8`（Windows 走 CP_ACP，Linux/macOS 原样返回）。

`split_string(str, char)` **跳过空项**（`"a,,b"` → 3 项里的 2 项，`codesys.cpp:104-112`，`test/system_test.cpp` 就是这个断言）；`split_string(str, std::string tag, ...)` 走 `std::regex` 正则切分。`unicode_*` 用 `std::wstring_convert<std::codecvt_utf8<wchar_t>>`（`codesys.cpp:94-102`）：C++17 起标准弃用，本机 GCC 11.4 实测无告警（`-Wdeprecated-declarations` 也是 0 条），换新工具链时需要留意。

### 动态库 libmange（`system/libmange.h`）

```cpp
xhqm::libid id = xhqm::libmange::loadlib("/path/lib.so");
xhqm::libmange::has_function(id, "create_object");
auto fn = xhqm::libmange::function_ptr<int, int>(id, "add");  // 查一次符号，强类型
int r = xhqm::libmange::function<int>(id, "add", 1, 2);       // 查 + 调一步到位
xhqm::libmange::freelib(id);
```

`_libary` 为 RAII 句柄，`libary = resource<_libary, libid>` 引用计数包装；`self()` 返回主程序句柄（不可释放，供主程序导出符号给插件回调）；`depend(dirpath)` 批量加载目录依赖。

---

## pattern 设计模式子模块（`pattern/`）

通用设计模式模板，均支持 `single_thread`（默认，无锁）/ `multi_thread`（加锁）双特化，由 `thread_mode.h` 的标签选择：

```cpp
// 单例（线程安全）
xhqm::instance<Foo>::get();

// 观察者：change(函数指针) 触发 response(subject, signal)
class view : public xhqm::observer<model, xhqm::single_thread> { void response(...) override; };

// 工厂（注册表）
xhqm::factory<Base, int> f;  f.regist<Derived>(1);  auto p = f.create(1);

// 命令 + 撤销/重做
xhqm::command_history<> hist;
hist.execute(xhqm::command([]{ ... }, []{ ... }));  hist.undo();  hist.redo();

// 责任链：handler 返回 true 停止
xhqm::chain<Req> ch;  ch.add([](Req& r){ return true; });  ch.handle(req);

// 策略
xhqm::strategy<void()> s([](){ ... });  s.execute();

// 通知者（按函数指针注册，非对象成员）
xhqm::notifier<void(int)> n;  n.regist(&fn);  n.notify(1);

// 状态机：from --event--> to，可选动作
xhqm::state_machine<State, Event> sm;
sm.start(State::A);  sm.add_transition(State::A, Event::Go, State::B, []{ ... });
sm.handle(Event::Go);
```

- `observer`/`notifier`/`chain`/`strategy`/`state_machine` 多线程版 `change/notify/handle/execute` 均在锁内拷贝快照后调用，避免重入死锁与长持锁。
- `instance` 单例实现经全局 `instance_manger` 保证跨编译单元唯一（按 `std::type_index` 注册）。
- 上面这段 API 都有可跑的用例：`test/pattern_test.cpp`（notifier / observer 的单线程与多线程）与 `test/pattern_extended_test.cpp`（factory / strategy / command / chain / state_machine 的单线程与多线程），对应 CMake 目标 `pattern_test`、`pattern_extended_test`（`test/CMakeLists.txt:178-185`）。`instance.cpp` 是 pattern 里唯一的 .cpp，目标里也要链上它（`test/CMakeLists.txt:260`）。

---

## 依赖

- 本模块头：`../head.h`（基础类型 `u8…fld/real/integer/natural/byte/text`、`xhqm_io`、`xhqm::error`、常量与宏）。
- `../algebra/value.h` `itype.h` `ctype.h`：数值类型别名（`datum` 与 `timesys`/`process` 使用）。
- `../lattice/tree.hpp`：`derive_tree` 对象树（`packet.h` 的 `object` 继承）。
- `../network/uuid.h`：`cipher::uuid`（对象自动生成 v4 UUID）。
- `../metaclass/resource.h`：`resource` 引用计数（`libary`）。
- `../metaclass/function/function_type.h`：函数 traits（`notifier`）。
- 系统库：`std::filesystem`、`dlfcn.h`（POSIX）/ `Windows.h`（Win）、`<thread>/<mutex>/<future>` 等。

---

## 用法示例（快速集成）

```cpp
#include "software/system.h"       // system_root + 四类容器
#include "software/signal.h"       // 信号槽
#include "software/datum.h"        // 序列化

xhqm::signals::pool_life();        // 用到异线程槽（queued / 跨线程 auto）才需要：main 首行装池

// 1. 信号槽跨线程通信
xhqm::software::signal<int> sig;
sig.connect([](int v){ /* 在线程池执行 */ }, xhqm::signal_type::signal_queued);
sig.emit(42);

// 2. 数据持久化
xhqm::datum cfg;
int width = 800;
cfg.resigt("width", &width);
xhqm::datum::save_file("config.json", cfg);

// 3. 系统工具
auto files = xhqm::filesys::file_list(".");
auto res   = xhqm::execsys::execute_cmd("uname -a");

// 4. 线程池
xhqm::parallel::threads pool(2, 4);
auto h = pool.submit<int>(0, []{ return 42; });
h.future_ptr()->wait();
```

> 上面每一段都在本机过了编译探针（`g++ 11.4 -std=c++17 -fsyntax-only -I. -Isoftware`，含 `pool_life()`、`connect`/`disconnect`、`resigt` + `save_json/save_file/read_file`、`system_root` 取四个子容器 —— 全部 0 error）。更完整的真实用法看 `test/system_test.cpp`（codesys/timesys/filesys/libmange/execsys/process/threads 七个 TEST_CASE）与 `test/pattern_test.cpp`、`test/pattern_extended_test.cpp`。

---

## 已知缺口 / 注意事项

本模块的**当前问题台账在 `software/DEBUG.md`**（A 功能性缺陷 / B 资源与并发 / C 跨平台 / D 性能 / E 未实现占位 / F 设计取舍 / G 文档漂移，每条带 `文件:行号` 与复现方式）。这里只留结论级摘要，逐条细节不在此重抄：

- **未实现/占位（E 组）**：`command_root` 的 redo/undo/save/read、`command::save/read`、`project_root::save/read`、`message_root::read`、`utility::save/read` 是空函数体；`utility_root::read` 加载动态库后只有 `int a = 0;`（`utility.cpp:64`），插件加载链路未接上。`threads_task.h` 里那套 `#ifndef XHQM_HEAD` 兜底定义是**永远不生效的死代码**（同文件 `:4` 已无条件 include `head.h`）。
- **datum（A 组，手写 JSON）**：~~`save_data` 对 `text` 不做转义（引号/反斜杠/冒号/逗号/控制符会破坏往返），`read_list` 不自动扩容。~~ **已作废（2026-10-07 复核 r677）**：两句都成立，但只是问题的一半 —— 值不转义之外**字段名也不转义**，空 vector 写出的是一个 `]` 而不是 `[]`，读侧的 `text` 会盲删首尾字符（单字符值踩 UB）、`read_json` 对字段乱序/缺字段静默返回成功、`value_ptr(name)` 查不到名字会越界、vector 字段的类型标签与取用侧对不上。逐条见 `DEBUG.md` A1–A7。
- **`packet::save` 写 `packet/<uuid>` 前不建目录、也不检查 `save_file` 的返回值**（`packet.cpp:36-40`）—— 同一问题在 `message_root::save`、`utility_root::save` 里各有一份（`filesys::create_dir` 用的 `create_directory` 不建父目录）。
- ~~`signal.h` 遮蔽系统 `<signal.h>`，引用系统信号宏/函数的 TU 需像 `process_base.cpp` 一样显式处理。~~ **已作废（2026-10-07 复核 r677）**：重点不在「引用系统信号宏的 TU」而在于**包含顺序** —— 先 include `<sys/wait.h>` 会让本头落进 `extern "C"`，实测 5091 个 error（详见上面的信号槽章节与 `DEBUG.md` A13/C2）。
- ~~无单元测试/示例工程~~ **已作废（2026-10-07 复核 r677）**：模块的 `pattern/*` 与 `system/*` 有真实测试与 CMake 目标 —— `test/system_test.cpp`（7 个 TEST_CASE：codesys/timesys/filesys/libmange/execsys/process/threads）、`test/pattern_test.cpp`、`test/pattern_extended_test.cpp`，目标定义在 `test/CMakeLists.txt:178-198`；但全仓**没有 `add_test`/ctest、没有 CI**（这条归根 `DEBUG.md`），所以没人跑就没人报。
- ~~（缺）无顶层聚合头~~ **已作废（2026-10-07 复核 r677）**：模块目录里确实没有 `software.h`，但仓库级聚合头是根 `xhqm.h`（`#define XHQM_USE_SOFTWARE`）→ `xhqm/software`，它覆盖了本模块的 23 个头（缺 `software/system.h`，见 `DEBUG.md` G4）。
- **`command_root`（`software::command`）与 `pattern::command_history` 是两套独立机制**，未统一：前者以 packet/对象树 + `_redo/_undo` 虚函数为核心且容器接口是空壳，后者是 `std::function` + 双栈（单/多线程特化），两者没有共同基类或桥接。
- **价值判断**：signal / `system/*` / `pattern/*` 成熟度中高（有调用方：`network/nebula`、`network/license.cpp`、`fantasy/fantasy.h`、`test/`）；datum、packet 与四类运行期容器**仓内零调用方**（只有模块自包含），成熟度低 —— 未接线，别按「能持久化」去依赖它。
- 仓内使用面（本机实测的 grep 结果）：模块外只有 `network/nebula/nebula.h`（timesys/threads/process/filesys）、`network/nebula/addr/addr_b.h`、`network/license.cpp`、`fantasy/fantasy.h`、`test/{system_test,pattern_test,pattern_extended_test,ffbo/import_connections}.cpp` 引用了本模块的头。
