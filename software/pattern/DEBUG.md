# xhqm · software/pattern 设计模式库 · 当前问题清单

`software/pattern`（module id `software/pattern`）的**当前仍存在**的问题台账：只记「现在还是坏的 / 还是没写的 / 还是对不上的」，**不记已修项、不做版本历史**。每条给出可核对的 `文件:行号`。本模块源码合计 **768 行**（`wc -l`：`observer.h` 133、`command.h` 126、`notifier.h` 98、`state_machine.h` 91、`chain.h`/`factory.h`/`strategy.h` 各 77、`instance.cpp` 45、`instance.h` 33、`thread_mode.h` 11），因此本文件逐条覆盖到行级，不做抽样。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**：本模块**没有**自己的历史台账（目录下原先只有 `README.md`，187 行 → 本轮 264 行）。历史上对本模块的公开描述散布在根 `README.md` §5.3（`:291-296`）与 `software/README.md` 的 pattern 小节（`:310-330`、`:396`）；那两份文档**不搬家、不改名、本轮也不改**，其过时说法在本文件 **G 组**逐条订正并指向原行号。根 `DEBUG.md`（模块路径 `.`）已登记的条目本文件**不重复记**：`head.cpp:22` 编译不通过（根 A2 —— 该文件 `:113-118` 是唯一在测试之外提到 `notifier` 的地方，且整段被注释）、`software/signal.h` 与系统 `<signal.h>` 同名（根 C1/F4 —— 与本模块的交集是 `signal.h:13` 对 `xhqm::instance` 的前置声明，见本文件 **F7**）。`network/nebula/DEFERRED.md`、`fantasy/DEBUG.md`、`fantasy/fantasy_bugs.md` 与本模块无内容重叠。
> 判据纪律：凡写「编译不过 / 某实参不能注册 / 某个尺寸」，必须附本轮跑过的命令与验证版本（本机 `g++ (Ubuntu 11.4.0) -std=c++17`，工程标准见 `CMakeLists.txt:19`）；凡写运行期行为而本轮没跑的，一律标「未复现」。
> 本模块**没有条件编译**：`grep -rn "^#if" software/pattern/ | wc -l` → 9，全部是 9 个头的 `#ifndef` 卫哨；去掉 `#ifndef` 后再搜 `#if\b` → 0 命中，即**没有任何平台/开关分支** —— 故 **C 组（跨平台）不适用**，本文件省去该组。本模块也**没有** `TODO/FIXME/XXX/占位/桩/未实现/暂不/尚未` 任何一处标记（`grep -rn "TODO\|FIXME\|XXX\|占位\|桩\|未实现\|暂不\|尚未" software/pattern/` → 0 命中）：下面每一条都是**没有注释兜底**的沉默问题。

---

## 附：旧说法复核对照（本轮 r677 逐条实测）

| 旧说法（出处） | 本轮结论 | 位置 |
|---|---|---|
| 「单例注册表裸指针无析构/释放顺序管理」（根 `README.md:294`） | **成立**；并补两条它没说的：注册表懒创建的锁外竞态（B3）、`get()` 在退出期可能返回已析构对象（F4 的代价） | **F4 / B3** |
| 「notifier 仅支持普通函数指针，无法注册 lambda/成员函数」（根 `README.md:294`） | **部分作废**：无捕获 lambda 可以（隐式转换到 `function_ptr`）；有捕获 lambda / 成员函数指针 / `std::function` 确实不行 | **G2** |
| 「多线程普遍快照拷贝后回调以避免重入」（根 `README.md:293`、模块 `README.md:170`） | **成立，但有一个例外**：`command_history<multi_thread>` 不拷贝栈，走「解锁→回调→上锁」，留下两次取锁之间的窗口 | **G3 / B4** |
| 「缺单测」（根 `README.md:295`、`software/README.md:396`） | **作废**：`test/pattern_test.cpp`（115 行）与 `test/pattern_extended_test.cpp`（144 行）都在，且已在 `test/CMakeLists.txt:178-183` 注册为可执行目标。示例工程则确实没有 | **G5** |
| 「缺模块级聚合头」（根 `README.md:295`） | **成立**：目录下无 `pattern.h`，全仓 0 引用；且 `CMakeLists.txt` 只收录 9 个头中的 3 个 | **E1** |
| 「状态机不支持进入/退出动作与非法转换策略」（根 `README.md:295`） | **成立**，并把「动作是无参 `std::function<void()>`」一并写清 | **F2** |
| 模块 `README.md:7`「每个组件均通过模板参数提供单/多线程双特化」 | **作废**：`instance` 没有 `thread` 参数，恒加锁 | **G1** |
| 模块 `README.md:172`「纯头文件实现易于内联」 | **作废**：`instance.cpp` 参与构建，`instance_manger` 是要链接的符号 | **G4** |
| 模块 `README.md:61`（旧版）「`change()` 以函数指针地址作为变化标识（`void*` 经 `memcpy` 位拷贝）」 | **成立但不完整**：拷的是**固定 8 字节**，不是「函数指针的大小」（本轮已改写为 `README.md:76` 的措辞） | **A1** |
| 模块 `README.md:84`（旧版）「`execute(cmd) / undo() / redo() / can_undo() / can_redo()`」+「`undo` 可为空表示不可撤销」 | **成立**，但两者组合起来有个洞：`undo()` 不查 `undoable()`（本轮已补一句到 `README.md:113-115`） | **A2** |

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `subject::change()` 的 signal 恒拷 8 字节：成员函数指针被截断、小对象实参越界读
- 位置：`observer.h:31-32`（单线程）、`observer.h:87-88`（多线程）
- 现状：`change(func_type function)` 的形参**按值**接收任意类型，signal 用
  `std::memcpy(&signal, &function, sizeof(changed_type))` 取位拷贝，而 `changed_type = void*`（`observer.h:22`、`:45`、`:74`、`:106`）——即**恒拷 8 字节，与实参对象的大小无关**。本轮实测三种实参的尺寸：无捕获 lambda **1** 字节、普通函数指针 **8** 字节、成员函数指针 **16** 字节。
- 证据：`observer.h:27-32` 与 `:84-88` 两处同型；尺寸由编译期探针读出（`show_size<sizeof(l)>` → `show_size<1>`、`show_size<sizeof(void(*)())>` → `<8>`、`show_size<sizeof(void(Button::*)())>` → `<16>`，`g++ -std=c++17 -I. -fsyntax-only`）。模块 `README.md` 示例 3 用的 `change(&Button::press)`（`:209`）正是 16 字节那一类。
- 影响：① 传成员函数指针（模块 README 示例与 `software/README.md:375` 都这么教）→ 只写入低 8 字节，signal 是截断值：`change()` 的消费侧拿它当标识时，两个不同的成员函数若低 8 字节相同就会撞成同一个 signal（观察者无法区分）；② 传无捕获 lambda（`s.change([]{})`，1 字节）→ 从 1 字节对象上读 8 字节**越界**，是 UB，ASAN 会直接报 stack-buffer-overflow。当前测试只传自由函数指针（`test/pattern_test.cpp:83`、`:96`、`:111` 的 `s.change(&add1)`），所以一直没炸。
- 复现/验证：**未复现**（越界读需 ASAN 运行）。静态可断的尺寸证据见上；运行复现：
  `printf '#include "software/pattern/observer.h"\nusing namespace xhqm;\nstruct S : subject<S> {};\nint main(){ S s; s.change([](){}); }\n' | g++ -std=c++17 -I. -fsanitize=address -x c++ - -o /tmp/p && /tmp/p`
- 建议：把 signal 的类型改成按实参真实类型做的标识（如 `func_type` 的哈希/`const void*` 取址），或至少 `static_assert(sizeof(func_type) >= sizeof(changed_type))` 把越界那一半挡在编译期。

### A2. `command_history::undo()` 不看 `undoable()`：不可撤销的命令照样返回 true，并且会被 `redo()` 二次执行
- 位置：`command.h:23-25`（`command::undo()` 空实现时静默无操作、`undoable()` 存在但从没人用）、`command.h:47-55`（单线程 `undo()`）、`command.h:91-104`（多线程 `undo()`）、`command.h:57-65`/`:106-119`（`redo()` 再执行）
- 现状：`undo()` 只判「撤销栈非空」就 `pop` → `cmd.undo()`（对 `undo == nullptr` 的命令是**空操作**）→ `push` 进重做栈 → `return true`。全模块没有任何一处调用 `undoable()`（`grep -n "undoable" software/pattern/*.h` → 只在 `command.h:25` 定义处命中）。
- 证据：`command.h:49-54`：`if (m_undo_stack.empty()) return false; ... cmd.undo(); m_redo_stack.push_back(std::move(cmd)); return true;` 全程无 `undoable()` 判定；`command.h:20` 的构造函数允许 `undo` 缺省为 `nullptr`。
- 影响：任何「只想记录、不想撤销」的命令（`command([]{...})` 单参构造）进入历史后：① `undo()` 骗调用方说撤销成功（返回值 true、`can_undo()` 减少），实际状态没回退；② 该命令被挪进重做栈，随后 `redo()` 会**再执行一次 `execute()`** —— 对「追加一行 / 发一次网络请求」这类命令就是重复副作用。模块 README（`:116`）只说了「`undo` 可为空表示不可撤销」，没说历史层会怎么处理它们。
- 复现/验证：**未复现**（需运行）。可判定的最小序列：`command_history<> h; int n=0; h.execute(command([&]{ ++n; }));` → `h.undo()` 返回 true 而 `n` 仍为 1；`h.redo()` 后 `n == 2`。
- 建议：`undo()`/`redo()` 里对 `!cmd.undoable()` 的命令直接返回 false（或干脆拒绝入栈），并把返回值语义写进 README。

### A3. `notifier::notify()` 在循环里对同一组实参反复 `std::forward`：右值实参被第一个回调搬空，后续回调拿到 moved-from 值
- 位置：`notifier.h:42-49`（单线程 `notify`）、`notifier.h:81-93`（多线程 `notify`）
- 现状：两个特化都是 `for (auto& hander : 容器) hander.first(std::forward<values_type>(values)...);` —— 转发发生在**循环体内**，同一组 `values` 被第 2..N 个回调重复转发。`m_receivers` 的元素是 `std::pair<receiver, function_ptr>`，`receiver = std::function<func_type>`（`notifier.h:20-23`），按值形参的回调会把右值搬走。
- 证据：`notifier.h:45-48` 与 `:89-92`；`std::function<void(int)>` 尺寸 32 字节（本轮编译期实测），`std::pair<std::function<void(int)>, void(*)(int)>` 为 40 字节 —— 容器里确实是「按值存的 std::function」。
- 影响：注册了 ≥2 个接收者时，`n.notify(std::string("x"))`（或任何只在移动后才正确的类型）会让第 1 个接收者拿走内容，第 2..N 个收到 moved-from（空）值。**静默错值，不崩、不报错**，且单线程/多线程版本一模一样。测试全用 `int`（`test/pattern_test.cpp:40`、`:56`、`:71`），整数没有「被搬空」的状态，所以测不出来。
- 复现/验证：**未复现**（需运行）。最小复现：注册两个 `void(std::string)` 回调，各打印字符串，调 `n.notify(std::string("hello"))` → 第二次打印为空。
- 建议：把 `notify` 的形参在进入循环前做成具名左值（如 `auto&&... safe = values...` 后按左值转发），只允许最后一次转发用 `std::move`；或在文档里明确「`notify` 只支持可平凡复制的实参」。

### A4. `observer.h` 用了 `std::list` 却没有 `#include <list>`：当前靠 `head.h` 的传递包含活着
- 位置：`observer.h:25`（`std::list<observer*> m_observers;`）、`observer.h:78`（多线程版同名字段）；包含清单 `observer.h:3-8`
- 现状：`observer.h` 只含 `../../head.h`、`<functional>`、`<mutex>`、`<vector>`、`<cstring>`、`thread_mode.h`，**没有 `<list>`**；它之所以能单独编译，是因为 `head.h:34` 无条件 `#include <list>`。模块内需要 `std::list` 的另一个头（`notifier.h`）就显式写了自己的 `#include <list>`（`notifier.h:4`）—— 即本模块的既有约定是「自包含」，这一处漏了。
- 证据：`grep -n "include" software/pattern/observer.h` → 无 `<list>`；`grep -n "include <list>" head.h` → `:34`。本轮单独编译探针 `printf '#include "software/pattern/observer.h"\n' | g++ -std=c++17 -I. -fsyntax-only -x c++ -` **通过**（因为 `head.h` 把 `<list>` 带进来了）。
- 影响：今天不炸，但它把「observer 依赖 head.h」从「依赖 `xhqm::id`/`xhqm_io`」升级成了「依赖 head.h 恰好包含哪些标准头」。`head.h` 哪天清一次包含集合（比如把 `<list>` 换成本地用法），或有人把 `observer.h` 抽出来单用，这里立刻编译失败在 `m_observers` 一行。
- 复现/验证：已复现「不失败」这一侧（见上）。反证很容易：临时注释掉 `head.h:34` 的 `#include <list>` 再编译同一探针即报 `'list' is not a member of 'std'`。
- 建议：`observer.h` 补一行 `#include <list>`。

---

## B. 资源与并发

### B1. `observer` 析构不自动注销：`subject` 留着裸指针，`change()` 会回调已销毁对象
- 位置：`observer.h:25` / `:78`（`std::list<observer*> m_observers`）、`observer.h:47-49` / `:108-110`（只删了拷贝/移动）、`observer.h:64` / `:130`（纯虚 `response`，**没有虚析构**）
- 现状：`subject` 存的是观察者的裸指针，`observer` 侧**没有任何析构函数**（`grep -c "~" software/pattern/observer.h` → 0），也没有「析构时自动 `remove(this)`」的通道 —— 注销必须由调用方在观察者死之前手工调 `remove()` 并传对 subject 指针。两条特化都一样。
- 证据：`observer.h:17-65` 与 `:69-131` 两个类定义里没有析构函数；`remove()` 存在但只能显式调用（`observer.h:56-62`、`:121-128`）。
- 影响：局部作用域的观察者（`{ Listener l; l.regist(&btn); }` —— 模块 `README.md` 的示例就是「先建对象再注册」这个形状，`:214-216`）一旦先于 subject 出作用域，subject 的链表里就留下悬垂指针，下一次 `change()` 对野指针调虚函数 → UAF/段错误，且没有任何钩子能发现。对照同一引擎的信号槽（`software/signal.h:18` 注释「receiver 继承 signal_receiver 可在销毁时自动断开」）—— 观察者比自家信号槽弱一档。
- 复现/验证：**未复现**（需运行 ASAN）。最小复现：`Button b; { Listener l; l.regist(&b); } b.press();`（ASAN 应报 heap/stack-use-after-scope）。
- 建议：给 `observer` 加析构函数记住已注册的 subject 并自动 `remove`（或改存 `weak_ptr`/句柄令牌），至少把「必须先 remove 再析构」写进头注释。

### B2. 单线程特化的 `notify`/`change`/`handle` 直接遍历活容器：回调里增删即迭代器失效
- 位置：`notifier.h:45-48`（遍历 `m_receivers`）、`observer.h:33-36`（遍历 `m_observers`）、`chain.h:32-36`（遍历 `m_handlers`）
- 现状：`single_thread` 版为了「零同步开销」不做快照，三个组件的遍历都直接作用在成员容器上；而回调里对同一容器 `regist`/`remove`/`add` 是合法调用（`remove` 自己的实现就是 `remove_if`，`chain::add` 是 `push_back`）。多线程版靠快照隔开了这一点（`notifier.h:84-88`、`observer.h:89-93`、`chain.h:57-61`）。
- 证据：三处 `for (auto& x : 成员容器)` 与各自的 `regist/remove/add` 实现同文件可查；`chain.h:26` 的 `push_back` 会重分配 `std::vector`，`notifier.h:36`/`observer.h:60` 的 `remove_if` 会 erase 当前元素。
- 影响：「收到通知后自己注销」是最自然的用法，在单线程特化下就是 UB：`chain` 里 `add` 触发 vector 重分配 → 正在用的迭代器悬垂；`notifier`/`observer` 里 erase 掉当前元素 → range-for 的迭代器失效。多线程版没这个问题，所以同一份宿主代码「加锁就对、默认就错」。
- 复现/验证：**未复现**（UB，且要看容器实现）。可写一条 `notifier<void()>`：回调里 `n.remove(&self)`，注册两个接收者后 `n.notify()`，在 ASAN/`_GLIBCXX_DEBUG` 下跑。
- 建议：单线程版也先做成局部快照（代价是每次调用一次容器拷贝，见 D1），或明确禁止在回调内改容器并加注释。

### B3. 单例的首次初始化有两处未同步的「双检」：锁外 `new` 注册表 + 无同步读写静态指针
- 位置：`instance.cpp:22-24`（注册表懒创建）、`instance.h:21-23`（`classinstance` 静态缓存）
- 现状：① `instance_regist` 里 `if(!instance_data) instance_data = new _instance_data();` 在**取锁之前**（锁在下一行 `:24` 才拿），且 `instance_data` 是普通全局裸指针（`instance.cpp:20`）；② `instance<T>::get()` 用 `static void* classinstance = nullptr; if (classinstance == nullptr) instance_manger(..., classinstance);` 做缓存，这个 static 的读与写都在锁外，且不是 `std::atomic`。
- 证据：`sed -n '20,24p' software/pattern/instance.cpp`；`sed -n '19,25p' software/pattern/instance.h`；全模块 `grep -n "atomic" software/pattern/` → 0 命中。
- 影响：进程内两个线程**首次**同时调 `get()` 时，可能各 `new` 一份 `_instance_data`（一份立刻泄漏、且各自一把 `m_instance_mutex`），并在 `instance_data` 上形成未同步的写-写/写-读（C++ 内存模型下是数据竞争，形式 UB）。**注意别把影响说大**：单例对象本体是 `instance<T>::static_type()` 里的函数内静态（`instance.h:15`），其初始化由编译器保证线程安全且全程序唯一，所以这两处竞态**不会**导致同一个类型被构造两次 —— 后果是注册表可能被分配两份（一份泄漏）、以及形式上的 UB。触发条件窄（只在首次并发调用时），且已被 `test/` 之外的真实入口用到（`software/packet.h:78`、`fantasy/drawer.cpp:27`），故列此不删。
- 复现/验证：**未复现**（竞态类）。静态可断：`sed -n '22,24p' software/pattern/instance.cpp` 的 `if` 在 `lock_guard` 之前。可在两处插入 `fprintf` 观察并发首次调用时的分配次数。
- 建议：`instance_data` 改成函数内静态（`static _instance_data data;`）或 `std::call_once`，把两处双检都收进同步原语；`classinstance` 用 `std::atomic<void*>` 或干脆每次直接走 `instance_manger`（后者内部已有锁与快路径判断，`instance.cpp:40-44`）。

### B4. `command_history<multi_thread>::undo()/redo()` 的两次取锁之间留窗口：新命令清空的重做栈会被已撤销命令重新填回来
- 位置：`command.h:91-104`（`undo()`）、`command.h:106-119`（`redo()`）、对照 `command.h:83-89`（`execute()` 在锁内 `m_redo_stack.clear()`）
- 现状：`undo()` 分两段取锁 —— 第一段 `pop` 出命令并解锁，然后在**锁外**执行 `cmd.undo()`，第二段再取锁把同一条命令 `push` 进 `m_redo_stack`。`redo()` 同构。「新命令压入撤销栈时清空重做栈」这条不变量只在 `execute()` 的锁内生效，管不到两段锁之间的窗口。
- 证据：`command.h:93-103`：锁 `{...}` → `cmd.undo();`（锁外）→ 新作用域 `std::lock_guard` → `m_redo_stack.push_back(...)`；`command.h:86-88` 是 `execute()` 的唯一清空点。
- 影响：线程 A `undo()` 与线程 B `execute(新命令)` 交错时，最终 `m_redo_stack` 里躺着 A 刚撤销的命令，而它已被 B 的新命令作废 → 之后一次 `redo()` 会重放一个**在新命令之前**的旧操作，撤销/重做语义错乱（配合 A2，还可能重复执行）。宿主是编辑器/命令栈这类多线程场景时很难复现、也难归因。
- 复现/验证：**未复现**（需运行 + 时序）。可写：线程 A 循环 `h.undo()`、线程 B 循环 `h.execute(cmd)`，断言 `can_redo()` 不为真即为命中。
- 建议：把「pop / 回调 / push」用一条「命令正被撤销」的状态（或第二把「历史锁」）串起来，让 `execute()` 在该状态期间不重排栈序，或改成像其它组件那样的「先取快照、再整体替换」。

### B5. 命令历史无上限、也没有 `clear()`：长跑宿主的撤销栈只增不减
- 位置：`command.h:36-37`（`std::vector<command> m_undo_stack / m_redo_stack`）、`command.h:40-45`（`push_back` 无淘汰）、`command.h:76-77`（多线程版同）
- 现状：`m_undo_stack` 只增不减（`undo()` 只是把它搬到 `m_redo_stack`），两条栈都是 `std::vector<command>`，而 `command` 持两个 `std::function`；全模块没有任何容量上限、没有 `clear()`、没有 `size()`（`grep -n "clear\|max_size\|limit\|size()" software/pattern/command.h` → 只有 `:44`、`:88` 两处 `m_redo_stack.clear()`，清的是重做栈，不是历史）。
- 证据：`command.h:36-37`、`:76-77` 的成员定义；`:43` 与 `:87` 的 `m_undo_stack.push_back(std::move(cmd));` 没有配套的淘汰逻辑。
- 影响：编辑器/工具类宿主连续操作 N 次就有 N 个 `std::function` 常驻（捕获越多越大），内存单调上涨且**无法主动释放**；`undo()` 的代价也随之线性增长（每次 move 一条）。这是典型的「无上限缓冲」，触发条件只是「跑得够久」。
- 复现/验证：未复现。静态可断：`grep -n "clear()" software/pattern/command.h` → 2 命中，都是 `m_redo_stack.clear()`（`command.h:44`、`:88`），没有清历史的成员；`grep -n "size()\|empty()" software/pattern/command.h` → 只有 `empty()` 判定（`:49`、`:59`、`:67-68`、`:96`、`:111`、`:121-122`），没有 `size()`、没有接口能问「历史有多长」。
- 建议：加 `clear()` 与可配置的历史深度（超限丢最旧的一条），或至少在注释里写明「历史由调用方负责裁剪」。

---

## D. 性能

### D1. `multi_thread` 版每次调用都整容器快照拷贝：6 个组件的热路径各带一次堆分配
- 位置：`notifier.h:84-88`、`observer.h:89-93`、`chain.h:57-61`、`strategy.h:67-71`、`factory.h:59-65`、`state_machine.h:77-84`
- 现状：多线程特化一律「锁内拷一份、解锁后遍历/调用」。拷的规模按容器而定：`notifier` 拷整个 `std::list<std::pair<std::function<void(int)>, void(*)(int)>>`（本轮实测元素 **40 字节**，其中 `std::function<void(int)>` 本身 **32 字节**），`chain` 拷 `std::vector<std::function<bool(int&)>>`（元素 **32 字节**），`observer` 拷 `std::vector<observer*>`（元素 8 字节），`strategy`/`factory`/`state_machine` 各拷一个 `std::function`。
- 证据：上述 6 处的局部快照变量（`notifier.h:84`/`observer.h:89`/`chain.h:57` 叫 `snapshot`，`strategy.h:67` 也叫 `snapshot`，`factory.h:59` 叫 `creator`，`state_machine.h:77` 叫 `transition`）；尺寸来自本轮编译期探针（`show_size<sizeof(std::function<void(int)>)>` → 32、`show_size<sizeof(std::pair<std::function<void(int)>, void(*)(int)>)>` → 40）。
- 影响：这是**有意为之**的取舍（源码注释写明「避免重入死锁与长持锁」，也是 B2 在多线程侧不成立的唯一原因），但它把「无锁组件的零开销」换成了「每次调用至少一次容器拷贝 + 一次堆分配」。每帧 notify 一次、接收者几十个的场景（渲染/事件系统）会把 `std::list` 逐节点拷贝+分配的成本放进帧预算：`notifier` 的快照是逐节点拷贝，每个节点还含一份 `std::function`（32 字节）的拷贝。
- 复现/验证：未复现（未做基准）。可断：对照 `single_thread` 与 `multi_thread` 两个 `notify` 的循环体（`notifier.h:45-48` vs `:84-92`），差异就是一次 `snapshot.assign`。
- 建议：容器只在写入时换新缓冲（copy-on-write + `shared_ptr<const 容器>`，读侧只拷指针），把 O(接收者) 的拷贝降成 O(1)。

### D2. 除测试外没有生产调用点：8 个组件里只有单例真的在引擎里跑
- 位置：真实调用点 3 处 —— `software/packet.h:78`（`xhqm::instance<regist_map>::get()`）、`fantasy/drawer.cpp:27`（`xhqm::instance<fantasy::_render_info_>::get()`）、`software/signal.cpp:37`（`xhqm::getInstance<_pool_life_>()`）；只包含头不调用 3 处 —— `software/signal.h:11`+`:13`（为 friend 提供模板声明）、`fantasy/fantasy.h:7`、`software/system/libmange.cpp:2`（后者通篇只出现这一行 `instance`，是冗余包含）。其余 7 个组件仅 `test/pattern_test.cpp`、`test/pattern_extended_test.cpp`
- 现状：按组件在全仓（排除本模块）搜调用点：单例是**真实生产路径**（跨模块注册表 `packet.h:78`、渲染 id 分配 `drawer.cpp:27`、线程池安装 `signal.cpp:37`）；`subject</observer<` → 只有 `test/pattern_test.cpp`；`factory<`（`xhqm::factory`）→ 只有 `test/pattern_extended_test.cpp`（`network/json.hpp` 里那几处 `..._factory<` 是 nlohmann 的 `input_adapter_factory`，与本模块无关）；`strategy<` / `command_history` / `chain<` / `state_machine<` → 只有 `test/pattern_extended_test.cpp`；`notifier<` → `test/pattern_test.cpp` 与 `head.cpp:113-118`，而后者整段是注释（该文件本身编译不过，见根 `DEBUG.md` A2）。
- 证据：`grep -rn "<组件名>" --include=*.h --include=*.cpp . | grep -v "^./software/pattern/"` 去重后的文件清单（见上）；`software/README.md:396` 也承认 `command_root` 与 `pattern::command_history` 是两套并存的命令机制、未统一。
- 影响：这些组件的**性能特征（D1 的快照拷贝）、并发行为（B2/B4）从未在真实负载下暴露过** —— 只有单线程语义的单元测试在跑。任何「引擎已经在用这套模式库」的预期都是高估：真正在用的是单例（+ 引擎自己的信号槽/命令体系），其余组件属于「已实现、可复用、但没有生产入口」。
- 复现/验证：`grep -rn "state_machine<\|chain<\|strategy<\|command_history" --include=*.h --include=*.cpp . | grep -v "^./software/pattern/"` → 只命中 `test/pattern_extended_test.cpp`。
- 建议：保持现状即可，但别把「引擎的观察者/状态机」说成线上组件；接线时应先补并发/性能用例。

---

## E. 未实现 / 占位桩

> 本组是「现在编译器会过、只是没写或没接上」的东西。本模块**没有**一行 `TODO/占位/未实现` 注释（见文首判据），所以这里记的是**结构性缺口**。

### E1. 没有模块级聚合头 `pattern.h`，且 9 个头里只有 3 个进了工程文件清单
- 位置：`software/pattern/`（目录清单）；`CMakeLists.txt:516-518`（`XHQM_HEAD_SYSTEM` 里的 pattern 段）
- 现状：目录下 11 个文件（`instance.h`/`instance.cpp` + 7 个模式头 + `thread_mode.h` + `README.md`；本轮新增 `DEBUG.md` 后为 12 个），**没有** `pattern.h` 之类的聚合头；全仓 `grep -rn "pattern/pattern.h\|software/pattern.h"` → 0 命中，即没人期待过它。同时 `XHQM_HEAD_SYSTEM` 只列了 `notifier.h`、`observer.h`、`instance.h`，`chain.h`/`strategy.h`/`command.h`/`state_machine.h`/`factory.h`/`thread_mode.h` 6 个文件不在任何 CMake 源清单里（只被 `test/` 与 `README` 引用）。
- 证据：`ls software/pattern`（11 条，不含本轮新增的 `DEBUG.md`）；`sed -n '516,518p' CMakeLists.txt`。
- 影响：① 用 `#include "software/pattern/pattern.h"` 一把全引的写法不存在（必须逐个 include —— 本文件与模块 README 的示例也都是逐个列的）；② 后 6 个头在 IDE/工程视图里不可见，改上限、找符号时容易漏掉，也容易被误认为「不是本工程的一部分」。
- 复现/验证：`ls software/pattern | grep -c "^pattern.h$"` → 0；`grep -n "pattern/" CMakeLists.txt` → 7 命中，其中 `:39`/`:47`/`:492` 是描述注释、`:516-518` 三个头、`:537` 一个源文件。
- 建议：加一个只含 8 个 `#include` 的 `pattern.h`，并把 6 个头补进 `XHQM_HEAD_SYSTEM`。

### E2. 单例（`instance.h` / `instance.cpp`）没有任何测试覆盖
- 位置：`test/pattern_test.cpp`（只 include `notifier.h`、`observer.h`）、`test/pattern_extended_test.cpp`（只 include `factory.h`、`strategy.h`、`command.h`、`chain.h`、`state_machine.h`）；注册点 `test/CMakeLists.txt:178-183`
- 现状：两个测试目标覆盖 7 个组件，**`instance` 一次都没被 include 或调用**（`grep -rn "instance" test/pattern_test.cpp test/pattern_extended_test.cpp test/test.h` → 0 命中），而它恰好是唯一有 `.cpp`、唯一带 `xhqm_io` 导出宏、唯一被生产代码使用的组件（D2）。B3/F4 这两条最需要运行期验证的问题因此没有入口。
- 证据：两个测试文件的 include 清单（`test/pattern_test.cpp:2-3`、`test/pattern_extended_test.cpp:2-6`）；`grep -rn "pattern/instance\|instance<" test/` → 0（`test/` 下其它文件里的 `instance` 字样属于别的模块，如 `test/ecs_test.cpp`、`test/toon_test.cpp`）。
- 影响：单例的「同类型只构造一次」「退出期行为」这两个核心契约没有回归手段；换编译器/换库类型（`XHQM_DYNAMIC` 的 SHARED 构建，`CMakeLists.txt:584-588`）时只能靠人肉。
- 复现/验证：`grep -rn "instance" test/pattern_test.cpp test/pattern_extended_test.cpp test/test.h` → 0 命中（本轮实测）。
- 建议：补一个 `instance_test.cpp`：同类型两次 `get()` 取址相等、不同 TU（两个 .cpp）取址相等、析构函数清理 `g_pool` 一类的钩子。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 单/多线程是「编译期双份实现」而不是运行时可换策略
- 位置：`thread_mode.h:7-8`（两个空标签）；成对出现 8 对类特化（7 个组件；`observer.h` 的 `subject` 与 `observer` 是两个类模板，各一对）：`notifier.h:18`/`:54`、`observer.h:18`/`:70`（`subject`）与 `:41`/`:102`（`observer`）、`command.h:34`/`:73`、`chain.h:19`/`:44`、`strategy.h:18`/`:38`、`state_machine.h:20`/`:47`、`factory.h:21`/`:45`
- 现状：每个组件手写两遍（`class X<..., xhqm::single_thread>` + `class X<..., xhqm::multi_thread>`），靠标签在编译期选；`single_thread` 版不含任何同步原语，`multi_thread` 版不含无锁分支。本轮数出 **16 个类特化定义**（`grep -n "^	*class [a-z_]*<" software/pattern/*.h | wc -l` → 16）。两份实现已经开始分叉：`notifier` 单线程版 `= default` 允许拷贝/移动、多线程版 `= delete`（`notifier.h:25-27` vs `:62-64`）；`command_history<multi_thread>` 的结构（两次取锁、锁外回调）与单线程版根本不同（`command.h:83-119` vs `:40-65`）。
- 理由（为什么现在这样是对的）：热路径零同步开销是这套组件存在的意义（引擎侧真正用到的地方是高频路径）；运行期多态（虚函数表/`std::function` 策略）会给每次 `notify` 加一层间接跳转，而线程模型是**宿主在编译期就知道**的信息（渲染线程 vs 工作线程），没有必要留到运行期。
- 代价：① 16 个类特化两两重复，改一处语义要改两处 —— B2/B4 这两个缺陷**只存在于其中一个特化**里，正是重复成本的直接体现；② 同一个类型不能运行期切换（比如启动参数决定单/多线程时，两套代码都得实例化）；③ 两个特化的可拷贝性、返回值语义并不一致，使用者要分别读。
- 验证：`grep -n "^	*class [a-z_]*<" software/pattern/*.h` → 16 行（逐条见「位置」）；`sed -n '83,119p' software/pattern/command.h`。
- 建议：保持双特化，但把两者共享的逻辑抽成基类/策略类（只让「锁与快照」成为差异点），避免继续分叉。

### F2. 状态机只做「转换表 + 无参动作」：没有进入/退出动作，也没有非法转换策略
- 位置：`state_machine.h:22`（转换表定义）、`:29-31`（`add_transition(from, event, to, action)`）、`:35-42`（`handle` 对未注册的 (状态, 事件) 直接 `return false`）
- 现状：每个 (from, event) 只挂**一个** `std::function<void()>`，它拿不到 from/to（无参）；没有 per-state 的 on_enter/on_exit；`handle()` 对非法转换连日志都不打（返回值是唯一信号）；`start()` 之前 `current()` 返回 `state_type{}`（枚举即 0，未必是合法枚举值）。旧说法「不支持进入/退出动作与非法转换策略」**成立**。
- 理由（为什么现在这样是对的）：需要它的两个场景（进/出动作、非法转换处理）都能在调用方一行解决 —— `handle()` 的 bool 返回值就是非法转换的判定口，进出动作可以写在转换动作里（因为动作已经知道自己是哪条转换：注册时就绑定了）；把 on_enter/on_exit 加进表结构会引入「状态是否可见」「转换失败时动作是否已执行」这类必须文档化的语义，而当前全仓**没有任何生产调用点**（D2），不值得先把语义做复杂。最小接口也让 `state_machine` 只需要 `state_type/event_type` 可比较这一个概念要求。
- 代价：① 一条转换的动作无法复用（要给 A→B 和 B→A 写两个动作，或自己捕获状态）；② 「进入某状态要做什么」这类在状态上的逻辑只能散落在每条入边（N 条入边 = N 处复制）；③ 非法转换静默失败，排障时只能靠 `current()` 反推。
- 验证：`sed -n '22,42p' software/pattern/state_machine.h`；`grep -n "enter\|exit\|illegal\|on_" software/pattern/state_machine.h` → 0 命中。
- 建议：保持现状；若要扩展，优先加「动作接收 from/to」（`std::function<void(state_type, state_type)>`），它比进出动作更小、更能解开动作复用的死结。

### F3. `notifier` 用裸函数指针当唯一标识，且 `remove` 一次删光同函数的全部注册
- 位置：`notifier.h:20-23`（`function_ptr` 是裸指针，容器里同时存 `std::function`）、`:29-32`（`regist` 不去重）、`:34-40`（`remove` 用 `remove_if` 删**所有**匹配项）
- 现状：注册标识就是函数地址；同一函数注册两次会排两项、`notify` 调两次，而**一次 `remove` 会把两项都删掉**（`remove_if` 不是「删最早的一项」）；`regist`/`remove` 都不返回任何东西，调用方无法知道删了几项。
- 理由（为什么现在这样是对的）：裸函数指针是这里唯一「无分配、可比较」的标识 —— 换成 `std::function` 就无法比较相等（`std::function` 没有 `operator==`），而用一个自增 token 做标识会把 API 从「传函数」变成「传返回值」，调用点全要改。`remove_if` 的语义也是「把这个函数的注册全部撤销」，对「一个函数只该收一次通知」的常见心智而言反而更安全。
- 代价：① 不能注册有捕获的 lambda / 成员函数 / `std::function`（见 G2），宿主得自己写 `bind` 的替代品（静态函数 + 全局状态，`test/pattern_test.cpp:12-16` 就是这么做的）；② `regist` 两次再用 `remove` 一次，会连带清掉自己没打算清的那一项；③ 回调参数在 `notify` 里被重复转发（A3）。
- 验证：`sed -n '29,40p' software/pattern/notifier.h`；本轮实测的「可注册/不可注册」表见模块 `README.md:95-101`。
- 建议：保持裸函数指针；补一个 `size()`/`count(fn)`，让「删了几项」可见。

### F4. 单例注册表是进程期不释放的裸对象，且不做销毁顺序管理
- 位置：`instance.cpp:20`（`_instance_data* instance_data = nullptr;`）、`:22-23`（`new`）、`:9-18`（注册表内部：`unordered_map<type_index, _instance>` + 每类型 `shared_ptr<mutex>`）；对象本体 `instance.h:13-17`（`static_type()` 的函数内静态）
- 现状：注册表 `new` 出来后再没有任何 `delete`，`m_instances_regist` 里的 `void* m_object` 永久持有对象地址；单例对象本体由函数内静态持有，退出期按「构造逆序」析构，而注册表不与之联动 —— 退出期静态析构函数里再调 `getInstance<T>()` 会得到**已析构对象的地址**（缓存指针非空 → 直接 `reinterpret_cast` 返回，不重建、不报错）。旧说法「单例注册表裸指针无析构/释放顺序管理」**成立**，这里把它记成取舍而非缺陷：它是有意的。
- 理由（为什么现在这样是对的）：单例本体是进程级资源，进程退出时 OS 回收；而**主动去销毁注册表反而更危险** —— `unordered_map` 的析构顺序与各类型静态对象的析构顺序无关，一旦有静态析构函数回调 `get()`，就会出现「注册表已释放 + 对象仍在被引用」的双重悬垂。引擎自己的消费者也是按这个前提写的：`software/signal.cpp:14-18` 的 `pool()` 只存裸指针、`_pool_life_` 的析构只做 `g_pool = nullptr;`（`:29-32`），注释明说「池子本体 new 出来、随进程回收」。
- 代价：① 泄漏检查工具（Valgrind/ASAN/CRT）会把注册表与各单例报成泄漏，噪音得自己滤；② 退出期 `getInstance<T>()` 无保护（见上），`B3` 的两处竞态也因此不会被任何析构逻辑修正；③ 类型一旦注册进表就再也没有「忘掉它」的手段（没有 `erase`/`reset` API），热重载类宿主需要换进程。
- 验证：`sed -n '20,24p' software/pattern/instance.cpp`；`grep -n "delete\|reset()" software/pattern/instance.cpp` → 0 命中。
- 建议：保持不释放；把「退出期不要调 `getInstance`」写进头注释，并在泄漏基线里预置这条。

### F5. 多线程版的用户回调一律在锁外执行（顺序因此不由接口保证）
- 位置：`notifier.h:89-92`、`observer.h:94-97`、`chain.h:62-66`、`strategy.h:72`、`factory.h:66`、`state_machine.h:85`、`command.h:85`/`:100`/`:115`
- 现状：所有多线程特化都把回调/用户代码（含 `std::make_shared<Derived>()` 这种用户构造函数）放在锁外，锁只保护容器的读写。`command_history<multi_thread>::execute()` 更是**先执行回调、再取锁入栈**（`command.h:85`）。
- 理由（为什么现在这样是对的）：回调里重新进本对象是最常见的需求（通知里注销自己、命令里再压一条命令、状态机动作里再 `handle`）；回调持锁必然死锁（`std::mutex` 不可重入），而把回调移到锁外是唯一既保重入又不用递归锁的方案 —— 递归锁会掩盖真正的设计问题，代价也更大。
- 代价：① 并发的 `execute()` 之间，「入栈顺序」与「实际执行顺序」可以相反（谁先抢到锁谁先进栈）→ `undo()` 的顺序不一定与动作发生的顺序相反；② `undo()/redo()` 的两段锁因此不是原子的（B4）；③ 回调期间容器可能已被别的线程改过（快照保护的是快照那一刻，不是回调期间的语义）。
- 验证：`sed -n '83,104p' software/pattern/command.h`；`grep -n "lock_guard" software/pattern/strategy.h` → `:48`/`:54`/`:69`（三处都在回调之外）。
- 建议：保持锁外回调；在 `command_history` 的注释里写明「并发 execute 不保证栈序」，需要严格顺序的宿主自己串行化。

### F6. 工厂只支持「无参默认构造的派生类」，且同 id 重复注册是静默覆盖
- 位置：`factory.h:26-30`（`regist<derived_type>(id)` 里写死 `std::make_shared<derived_type>()`）、`:33-38`（`create`）、`:69-73`（`has`）
- 现状：注册接口只有模板形式，没有「注册一个自定义创建函数」的重载，所以派生类必须可默认构造、不能带构造参数，也不能用带依赖注入的创建器；`regist` 对已存在的 id 直接 `m_creators[id] = ...` 覆盖，不返回旧值、不报错（只有 `has()` 能让调用方事先查）。
- 理由（为什么现在这样是对的）：`factory<Base, id>` 的价值是「id → 具名派生类型」的编译期类型安全映射（对照 `software/packet.h:78` 那套手工 `regist_map` 注册表的写法），保持单一模板接口能让 `regist<Derived>(id)` 一句话完成注册、也不给出「传进来的创建器返回了不匹配的类型」的空间。需要参数的场景在引擎里由调用方在创建后自行初始化满足。
- 代价：① 带参构造的类上不了这个工厂（要在派生类里加默认构造 + 初始化接口）；② 重复注册 id（尤其多 TU 里各自 `regist`）会静默吃掉前一份，排查时看不到任何痕迹；③ `create()` 未注册返回 `nullptr` 而不是抛错，两处都得判。
- 验证：`sed -n '26,40p' software/pattern/factory.h`；`grep -n "regist" software/pattern/factory.h` → 只有 `:27` 一个重载。
- 建议：保持现状；给 `regist` 加一个 `bool` 返回值（是否覆盖了已有 id）即可让 ② 可见。

### F7. `instance` 模板同时充当「私有无参构造类型的构造入口」，跨模块契约只有前置声明支撑
- 位置：`instance.h:10-26`（`instance<T>::static_type()` 是唯一构造点）、`software/signal.cpp:24`（`template <typename T> friend class xhqm::instance;`）、`software/signal.h:13`（`namespace xhqm { template <typename T> class instance; }   // 前置声明：pool_life 的 friend 依据`）
- 现状：引擎里唯一「私有构造 + 私有析构」的单例是 `signals::_pool_life_`（`software/signal.cpp:22-33`：构造函数里 `new` 出线程池、析构里摘除安装），它能被构造**只因为**它把 `xhqm::instance` 声明成 friend；为了让这个 friend 有效，`software/signal.h:13` 必须在头里前置声明本模块的模板。也就是说：本模块的 `instance`（含它的模板形参个数、名字、命名空间）成了 `software/signal.h` 的编译期依赖，而且这条依赖在两侧都只是注释/声明，没有任何 `static_assert` 或测试守着。
- 理由（为什么现在这样是对的）：把构造/析构钩子放在私有里、由单例模板做唯一入口，是「安装一个进程级资源」最省事的写法：调用方只需 `xhqm::getInstance<signals::pool_life>();`（`signal.cpp:37`），不必自己维护一个全局标志；同时 `_pool_life_` 的析构（`:29-32`）能附着在单例本体的静态析构上，天然获得「进程退出即卸载」。friend 授信给了模板整体而不是某个具体类型，也正是为了让它能被 `instance<T>::static_type()` 访问。
- 代价：① 改 `instance` 的模板签名（加参数、改名、换命名空间）会直接打断 `software/signal.h:13` 的前置声明与 `signal.cpp:24` 的 friend —— 报错位置在 `signal` 侧，不在本模块；② 谁都可以对任意类型用 `getInstance<T>()`，因此「T 的构造是私有的」这件事没有任何集中检查；③ 与根 `DEBUG.md` C1 耦合：C1 建议挪动的正是 `signal.h:13` 这行前置声明（它落在 `extern "C"` 里会有 C/C++ 链接问题），改动时会连带本模块。
- 验证：`sed -n '22,33p' software/signal.cpp`；`sed -n '11,14p' software/signal.h`；`grep -rn "friend class xhqm::instance" --include="*.cpp" --include="*.h" software/ fantasy/` → 1 命中（`software/signal.cpp:24`）。
- 建议：保持 friend 授信；把 `signal.h:13` 的前置声明改成 `#include "pattern/instance.h"`（该头已在 `:11` 被包含，前置声明其实是冗余的），顺带解掉 C1 的那一半风险。

---

## G. 文档与代码的出入

### G1. 「每个组件都有单/多线程双特化」不成立：`instance` 没有 `thread` 参数
- 位置：模块 `README.md:7`（本轮已加 ~~划线~~ 与「已作废」标注）、`:256` 的多线程用法注（本轮保留，并显式补了「`instance` 没有这个参数」）；事实侧 `instance.h:10-26`（类模板只有一个形参，且不 `#include "thread_mode.h"`）、`instance.cpp:42`（每类型一把 `mutex`）。注：`software/README.md:310` 的措辞是「均支持 `single_thread`（默认，无锁）/ `multi_thread`（加锁）双特化」，同样对 `instance` 不成立。
- 现状：8 个组件里 `command` 是普通值类、`instance` 无 `thread` 参数且恒加锁，只有 7 个（`notifier` / `observer` / `command_history` / `chain` / `strategy` / `state_machine` / `factory`）有双特化。原文的「每个组件均」「默认走无锁路径」对单例是错的。
- 证据：`grep -n "thread" software/pattern/instance.h` → 0 命中；`grep -n "include" software/pattern/instance.h` → 只有 `../../head.h` 与 `<typeindex>`。
- 影响：按「换个模板参数就线程安全」的预期去用 `instance` 的人会找不到参数；反过来，以为单例可换无锁实现的人会读错它的成本模型（它每次类型注册都过锁）。
- 复现/验证：`grep -c "thread" software/pattern/instance.h` → 0。
- 建议：README 已订正；若真要统一，给 `instance` 补 `thread` 参数属于接口改动（F7 的跨模块契约要一起看）。

### G2. 「notifier 仅支持普通函数指针，无法注册 lambda」——无捕获 lambda 可以注册
- 位置：根 `README.md:294`（原文，本轮不改那份文档）；事实侧 `notifier.h:20`、`:29`、`metaclass/function/function.h:85`。注：`software/README.md:398` 的示例措辞是「通知者（按函数指针注册，非对象成员）」，**这个说法是准确的**，问题只在根 README 那句「无法注册 lambda」。
- 现状：`function_ptr = _return_type(*)(_args_type...)`（对 `void(int)` 就是 `void(*)(int)`），`regist` 收这个类型；无捕获 lambda 可隐式转换成函数指针，因此**能**注册。根 `README.md:294` 引用的 `metaclass/function/function.h:133`（成员函数特化）在 `notifier<void(int)>` 这个实例化里根本不参与 —— 模板实参是函数**签名**，命中的是 `function.h:77-78` 的自由函数特化（`function_ptr` 在 `:85`）。
- 证据：本轮逐个实测（`g++ -std=c++17 -I. -fsyntax-only`）：`n.regist(&plain)` ✅、`n.regist([](int){})` ✅、捕获 lambda ❌ `cannot convert ... to function_ptr`、`&Holder::method` ❌、`std::function<void(int)>` ❌。结果表已写进模块 `README.md:95-101`。
- 影响：「不能注册 lambda」这句话会把本来可用的写法（无捕获 lambda 当回调）说死，也会让人以为必须像 `test/pattern_test.cpp:12-16` 那样为每个回调准备一个静态函数。反过来，「成员函数/`std::function` 不能注册」这条是硬限制，文档不该省略。
- 复现/验证：见上面 5 条单行探针；模块 README 的表格即为其结论。
- 建议：根 `README.md:294` 与 `software/README.md` 相应句子后续订正为「裸函数指针（无捕获 lambda 可隐式转换）」。

### G3. 「多线程一律快照后回调」有例外：`command_history<multi_thread>` 走的是「解锁→回调→上锁」
- 位置：模块 `README.md:170`（本轮已加 ~~划线~~ 与「已作废」标注）；根 `README.md:293` 的「多线程普遍快照拷贝后回调」措辞尚可（「普遍」成立），`software/README.md:406` 只把 `observer`/`notifier`/`chain`/`strategy`/`state_machine` 列进快照名单、**没有**列 `command_history`，这一句是准确的；事实侧 `command.h:83-119`
- 现状：`execute()` 先执行回调再取锁入栈（`:85`）；`undo()`/`redo()` 分两段取锁、回调在中间（`:93-103`、`:108-118`）。它保护的容器是两条 `std::vector`，从不拷贝快照。
- 证据：`sed -n '83,104p' software/pattern/command.h`。
- 影响：按「多线程版都是快照语义」推断的人会以为 `undo()` 是原子的，进而写出 B4 那样依赖「清空的重做栈不会被复填」的代码。
- 复现/验证：`grep -n "snapshot" software/pattern/*.h` → 命中 4 个文件 12 处（`notifier`/`observer`/`chain`/`strategy`），**没有 command.h**；`factory.h:59` 与 `state_machine.h:77` 做的是同样的拷贝，只是局部变量叫 `creator`/`transition`（`grep -n "snapshot" software/pattern/factory.h software/pattern/state_machine.h` → 0 命中，别把这两处漏掉）。
- 建议：模块 README 已订正；`command_history` 的两段锁若按 B4 修掉，这条差异可以再收窄。

### G4. 「纯头文件实现易于内联」——模块有唯一的 `.cpp`，且它进了构建
- 位置：模块 `README.md:172`（本轮已加 ~~划线~~ 与「已作废」标注）、`instance.h:8`（`xhqm_io` 导出宏声明）、`instance.cpp:38-45`（定义）、`CMakeLists.txt:537`（`XHQM_BODY_SYSTEM` 收录）
- 现状：`instance_manger` 是唯一必须链接的符号；`instance.h` 里 `instance<T>::get()` 那层是头内的薄封装，真正的注册逻辑在 `.cpp` 里。`CMakeLists.txt:516-518` 也把它当「头 + 源」一对来收（头进 `XHQM_HEAD_SYSTEM`、源进 `XHQM_BODY_SYSTEM`）。
- 证据：`grep -n "pattern/instance" CMakeLists.txt` → `:518`（头）、`:537`（源）。
- 影响：把 pattern 当「header-only 库」直接拷进别的工程时会漏掉 `instance.cpp` → 链接期 `undefined reference to xhqm::instance_manger(...)`，而报错点在 `instance.h` 的调用处，容易误判为头文件问题。注意 `XHQM_DYNAMIC`（`CMakeLists.txt:584-588`）下这个符号是 dllimport/export 的，跨 DLL 使用还要带对宏。
- 复现/验证：`grep -n "instance_manger" software/pattern/instance.h software/pattern/instance.cpp` → 声明与定义各一处。
- 建议：模块 README 已订正；在聚合头（E1）里也把「需要链接 instance.cpp」写进注释。

### G5. 「缺单测」作废：两个测试文件在案并已注册目标；「无示例工程」「缺编译覆盖」另说
- 位置：根 `README.md:295`（「缺单测与编译覆盖」）、`software/README.md:396`（「无单元测试/示例工程」）；事实侧 `test/pattern_test.cpp`（115 行）、`test/pattern_extended_test.cpp`（144 行）、`test/CMakeLists.txt:178-183`
- 现状：两个测试覆盖 7 个组件（notifier/observer 含并发用例；factory/strategy/command/chain/state_machine 含单/多线程用例），并注册为 `pattern_test` 与 `pattern_extended_test` 两个可执行目标。test/ 目录里确实**没有** pattern 的示例工程，`instance` 也确实没有测试（E2）。
- 证据：`test/CMakeLists.txt:178-183`；本轮以 `g++ -std=c++17 -I. -fsyntax-only test/pattern_test.cpp test/pattern_extended_test.cpp` 语法编译通过（本机 `g++ 11.4.0`，工程标准 `CMAKE_CXX_STANDARD 17`）。**未做**完整构建与运行：`test/CMakeLists.txt` 是独立 CMake 工程（`:1-14`，自带 `project(xhqm_algebra_test)` 与 `include_directories("..")`），根 `CMakeLists.txt` 里没有 `add_subdirectory`，所以它不是主线构建的一部分。
- 顺带两处同源的行号/事实偏差（都在 `software/README.md`，本轮不改那份文档）：`:408` 把目标行号写成 `test/CMakeLists.txt:178-185`（实际 pattern 两个目标在 `:178-180` 与 `:182-183`，`:186` 起的那个是 `hardware_test`）；同句接着说「`instance.cpp` 是 pattern 里唯一的 .cpp，目标里也要链上它（`test/CMakeLists.txt:260`）」—— `:260` 那一份 `instance.cpp` 属于 **`ecs_test`** 目标（其源列表在 `:229-260`，紧接着 `:262` 是 `target_include_directories(ecs_test ...)`），`pattern_test`/`pattern_extended_test` 的源列表里**没有**它（它们不需要单例）。
- 影响：这句「缺单测」会让后来者重复造测试（或直接跳过回归）；真实情况是「有单测但没有默认构建入口，也没有 CI 记录」。
- 复现/验证：`ls -l test/pattern_test.cpp test/pattern_extended_test.cpp`；`grep -n "pattern_test\|pattern_extended_test" test/CMakeLists.txt`。**未复现**：没有跑 `cmake` + 链接 + 执行。
- 建议：根 `README.md` 与 `software/README.md` 后续把「缺单测」改成「单测在 `test/`（7/8 组件），无默认构建入口、单例无覆盖」。
