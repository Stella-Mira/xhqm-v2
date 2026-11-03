# pattern 模式库

> 最后同步：2026-10-07，对照工作副本 r677。

## 模块定位

`xhqm` 引擎的基础设计模式库，提供一组 **header-only（除单例注册表外）** 的轻量级 C++ 设计模式组件，供引擎的数学、渲染、网络、系统等各层复用。~~每个组件均通过模板参数 `<..., class thread = xhqm::single_thread>` 提供 **单线程（无锁）** 与 **多线程（加锁）** 两套特化实现，默认走无锁路径，兼顾性能与线程安全。~~ **已作废（2026-10-07 复核 r677）**：`thread` 模板参数只存在于 7 个组件上（`notifier` / `observer` / `command_history` / `chain` / `strategy` / `state_machine` / `factory`，默认值都是 `xhqm::single_thread`）；**单例 `instance` 没有 `thread` 参数，也不 `#include "thread_mode.h"`，它恒走加锁路径**（`instance.cpp:42` 每个类型一把 `std::mutex`）——「每个组件都有单/多线程双特化」不成立，`command`（命令值类）同样是普通类。

实测规模（`wc -l`）：`observer.h` 133、`command.h` 126、`notifier.h` 98、`state_machine.h` 91、`chain.h` 77、`factory.h` 77、`strategy.h` 77、`instance.cpp` 45、`instance.h` 33、`thread_mode.h` 11 行 —— 源码合计 768 行；含本文件（264 行）共 1032 行。注意 `instance.h` / `instance.cpp` 的末行不带换行符，编辑器会显示为 34 / 46 行。

成熟度：**中高** —— 8 个模式组件齐、7 个有双特化、`test/` 下有两个测试文件在案；缺模块级聚合头、单例没有测试覆盖、状态机只到「转换表 + 无参动作」这一层。当前仍存在的问题见 [DEBUG.md](DEBUG.md)。

## 目录结构

```
pattern/
├── instance.h (33 行) / instance.cpp (45 行)  单例管理器（type_index 注册表，唯一含 .cpp 的组件）
├── observer.h (133 行)         观察者模式（subject / observer）
├── notifier.h (98 行)          通知者（裸函数指针回调注册）
├── command.h (126 行)          命令模式（command + command_history 撤销/重做）
├── chain.h (77 行)             责任链模式
├── strategy.h (77 行)          策略模式
├── state_machine.h (91 行)     状态机（状态 × 事件 → 转换表）
├── factory.h (77 行)           工厂模式（id 注册表创建派生类）
└── thread_mode.h (11 行)       线程模型标签（single_thread / multi_thread）
```

- **没有模块级聚合头**：目录下无 `pattern.h`，全仓也没有任何文件包含它（`grep -rn "pattern/pattern.h\|software/pattern.h"` → 0 命中）。
- `CMakeLists.txt:516-518` 的 `XHQM_HEAD_SYSTEM` 只把 `notifier.h` / `observer.h` / `instance.h` 三个头列进了工程文件清单，其余 6 个头（含 `thread_mode.h`）不在清单里；`CMakeLists.txt:537` 把 `software/pattern/instance.cpp` 编进 `XHQM_BODY_SYSTEM`（即 `xhqm` 库）。

## 关键类 / API

### 线程模型标签 `thread_mode.h`

```cpp
namespace xhqm {
    struct single_thread {};   // 单线程：无锁顺序执行（默认）
    struct multi_thread {};    // 多线程：加锁线程安全
}
```

两个空标签，不含任何成员；下面 7 个组件拿它当第二个/第三个模板参数（`thread_mode.h:7-8`）。

### 单例管理器 `instance.h`

基于 `std::type_index` 的类型安全全局单例注册表，`get()` 首次调用时惰性构造。

```cpp
template<typename class_type> class instance {
public:
    static class_type& get();   // 线程安全惰性单例
};
template<typename class_type> inline class_type& getInstance();
```

- 底层 `instance_manger(const std::type_index&, void* (*static_type)(), void*&)` 维护
  `unordered_map<type_index, _instance>` 与每类型的独立 `mutex`，避免全局锁竞争。
- `static_type()` 使用函数内 `static` 保证对象只构造一次。
- **没有 `thread` 参数**（恒加锁）。引擎里的真实用法两种都有人用：
  `xhqm::instance<regist_map>::get()[regist_type] = create;`（`software/packet.h:78`，把单例当跨模块注册表）、
  `xhqm::getInstance<signals::pool_life>();`（`software/signal.cpp:37`，借单例的构造/析构钩子安装线程池）。
- 被单例构造的类型可以把构造函数留成私有，只要声明 `template <typename T> friend class xhqm::instance;`（`software/signal.cpp:24`）。

### 观察者模式 `observer.h`

```cpp
template<class subject_type, class thread = single_thread> class subject;
template<class subject_type, class thread = single_thread> class observer;

subject<MyType>::change(func);            // 触发所有已注册 observer 的 response()
observer<MyType>::regist(subject*);       // 注册（持有 this 指针，禁止拷贝/移动）
observer<MyType>::remove(subject*);       // 注销
virtual void response(subject*, void* signal);  // 纯虚回调
```

- `change()` 是成员函数模板，`func` 是按值形参且**不约束类型**；`signal` 取该形参对象**前 `sizeof(void*)` 字节**的位拷贝（`observer.h:31-32`、`:87-88`）。
- 多线程版 `change()` 先拷贝观察者快照再回调，避免重入死锁与长持锁。
- `subject` 只持裸 `observer*`，`observer` 没有析构函数；`(subject, observer)` 的生死配对由调用方负责（见 DEBUG.md B1）。
- 单线程版 `subject` 未删除拷贝/移动，多线程版删了（`observer.h:80-82`）—— 拷贝单线程 `subject` 会把观察者列表一起复制。

### 通知者 `notifier.h`

依赖 `metaclass/function/function_type.h`，以 `xhqm::function<func_type>::function_ptr` 注册**裸函数指针**（`void(int)` 对应 `void(*)(int)`，见 `metaclass/function/function.h:85`），`std::forward` 转发参数；容器里同时存一份 `std::function` 供调用（`notifier.h:23`）。

```cpp
template<class func_type, class thread = single_thread> class notifier;
notifier<void(int)> n;
n.regist(&fn);
n.remove(&fn);
n.notify(42);              // 转发可变参数
```

实测能注册什么（`g++ -std=c++17 -fsyntax-only`，工作副本 r677）：

| 传给 `regist(...)` 的实参 | 结果 |
| --- | --- |
| 普通函数指针 `&fn` | ✅（测试与本文档用法） |
| 无捕获 lambda `[](int){}` | ✅（隐式转换成函数指针） |
| 有捕获 lambda `[x](int){}` | ❌ `cannot convert ... to function_ptr` |
| 成员函数指针 `&T::method` | ❌ 同上 |
| `std::function<void(int)>` | ❌ 同上 |

> 旧说法「notifier 仅支持普通函数指针，无法注册 lambda」只在**有捕获**的 lambda 与成员函数上成立；无捕获 lambda 可以直接注册（见 DEBUG.md G2）。

### 命令模式 `command.h`

```cpp
class command {
    command(std::function<void()> execute, std::function<void()> undo = nullptr);
    void execute(); void undo(); bool undoable();
};
template<class thread = single_thread> class command_history;
// execute(cmd) / undo() / redo() / can_undo() / can_redo()
```

- `undo` 可为空表示不可撤销；新命令压入撤销栈时清空重做栈。
- `command_history<>` 默认 `thread = xhqm::single_thread`；`undo()` 的返回值是「撤销栈非空」而不是「真的撤销了」，不可撤销的命令照样进重做栈（见 DEBUG.md A2）。

### 责任链 `chain.h`

```cpp
template<class request_type, class thread = single_thread> class chain;
chain<Req> c;
c.add([](Req& r){ return /*true=已处理*/ false; });
bool handled = c.handle(req);   // 处理器返回 true 停止传递
```

`add` 收 `std::function<bool(request_type&)>`，所以捕获 lambda 可以直接用；另有 `empty()`。

### 策略模式 `strategy.h`

```cpp
template<class func_type, class thread = single_thread> class strategy;
strategy<int(int,int)> s{&add};
s.set(&mul); s.clear();
int r = s.execute(3, 4);        // 转发可变参数
explicit operator bool();       // 是否已设置策略
```

未设置策略（`clear()` 之后）就调 `execute()` 会抛 `std::bad_function_call` —— 调用前先用 `(bool)s` 判一次。

### 状态机 `state_machine.h`

```cpp
template<class state_type, class event_type, class thread = single_thread> class state_machine;
sm.start(initial);
sm.add_transition(from, event, to, action = nullptr);  // 可选动作回调
bool moved = sm.handle(event);  // 返回是否发生转换
sm.current();
```

- 转换表是 `std::map<std::pair<state_type, event_type>, std::pair<state_type, std::function<void()>>>`（`state_machine.h:22`），要求 `state_type`/`event_type` 可比较（`enum class` 可以）。
- 动作是无参 `std::function<void()>`，看不到 from/to；`handle()` 对未注册的 (当前状态, 事件) 只 `return false`，不抛异常、不打日志（见 DEBUG.md F2）。
- `start()` 之前 `current()` 返回 `state_type{}`（枚举即 0，未必是合法枚举值）。

### 工厂模式 `factory.h`

```cpp
template<class base_type, class id_type = xhqm::id, class thread = single_thread> class factory;
factory<Base> f;
f.regist<Derived>(id);          // 类型安全注册派生类
std::shared_ptr<Base> p = f.create(id);  // 未注册返回 nullptr
bool ok = f.has(id);
```

`id_type` 默认 `xhqm::id` = `std::size_t`（`head.h:220`），转换表是 `std::map`；`regist<Derived>` 内部是 `std::make_shared<Derived>()`，**派生类必须可默认构造**，且同 id 重复 `regist` 是静默覆盖（见 DEBUG.md F6）。

## 架构

1. **统一线程模型抽象**：7 个组件以 `thread` 模板参数选择单/多线程特化，`single_thread` 版零同步开销，`multi_thread` 版通过 `std::mutex` + **快照拷贝** 策略保护容器，把回调/用户代码移到锁外执行，规避重入死锁与长持锁。~~所有 multi_thread 组件都是「快照后回调」~~ **已作废（2026-10-07 复核 r677）**：`command_history<multi_thread>` 例外 —— 它不拷贝栈，而是「解锁 → 调用户回调 → 再上锁」，`undo()`/`redo()` 因此有两次取锁之间的窗口（`command.h:91-119`，见 DEBUG.md B5）。
2. **注册表 + 惰性初始化**：`instance`（类型单例）与 `factory`（对象工厂）都采用「注册表 + 首次使用时创建」的模型。
3. **最小依赖**：仅依赖 `head.h`（`xhqm_io`、`xhqm::id`）、`metaclass/function/function_type.h`（`notifier` 需要）及 C++ 标准库。~~纯头文件实现易于内联。~~ **已作废（2026-10-07 复核 r677）**：模块本身不是纯头文件 —— `instance.cpp` 编进 `xhqm` 库（`CMakeLists.txt:537`），`instance.h` 里的 `instance_manger` 是唯一需要链接的符号（带 `xhqm_io` 导出宏，`instance.h:8`）。逐个文件的包含面：`instance.h`/`observer.h`/`factory.h` 引 `../../head.h`；只有 `notifier.h` 引 `metaclass/function/function_type.h`；`chain.h`/`strategy.h`/`command.h`/`state_machine.h`/`thread_mode.h` 只引标准库与 `thread_mode.h`。
4. **类型安全**：`factory.regist<Derived>`、`instance<Class>::get()` 均保留编译期类型信息，避免 `void*` 强转散落各处。

## 依赖

| 依赖 | 用途 |
| --- | --- |
| `../../head.h` | `xhqm_io` 导出宏（`head.h:28/30`）、`xhqm::id = std::size_t`（`head.h:220`）等基础头；`instance.h:3`、`observer.h:3`、`factory.h:11` 引用 |
| `../../metaclass/function/function_type.h` | `notifier.h:8` 的函数指针/签名提取（`xhqm::function<func_type>::function_ptr` / `function_std`） |
| `thread_mode.h` | 本模块内的单/多线程标签，7 个头引用（`instance.h` 不用） |
| C++ 标准库 | `<mutex>` `<functional>` `<memory>` `<typeindex>` `<map>` `<list>` `<vector>` `<cstring>` |

## 使用示例

真实 include 路径（四种都在本仓源码里出现过，任选一种）：`"software/pattern/xxx.h"`（仓库根在 `-I` 里，`CMakeLists.txt:79`）、`"pattern/xxx.h"`（`software/` 在 `-I` 里，`CMakeLists.txt:95`，见 `software/signal.h:11`）、`"../pattern/xxx.h"`（见 `software/system/libmange.cpp:2`）、`"../software/pattern/xxx.h"`（见 `software/packet.h:5`、`fantasy/fantasy.h:7`、`test/*.cpp`）。

```cpp
#include "software/pattern/instance.h"
#include "software/pattern/factory.h"
#include "software/pattern/observer.h"
#include "software/pattern/state_machine.h"

using namespace xhqm;

// 1. 单例
class Logger { /* ... */ };
Logger& log = getInstance<Logger>();

// 2. 工厂
struct Shape { virtual ~Shape() = default; };
struct Circle : Shape {};
factory<Shape> shapes;
shapes.regist<Circle>(1);
std::shared_ptr<Shape> s = shapes.create(1);

// 3. 观察者
struct Button : subject<Button> {
    void press() { change(&Button::press); }  // 以函数指针为信号
};
struct Listener : observer<Button> {
    void response(Button*, void*) override { /* 收到 press 变化 */ }
};
Button btn; Listener l;
l.regist(&btn);
btn.press();

// 4. 状态机
enum class S { Idle, Run };
enum class E { Start, Stop };
state_machine<S, E> sm;
sm.start(S::Idle);
sm.add_transition(S::Idle, E::Start, S::Run);
sm.add_transition(S::Run,  E::Stop,  S::Idle);
sm.handle(E::Start);   // Idle -> Run
sm.handle(E::Stop);    // Run  -> Idle
```

上面 4 段都能编译；另外三段是测试里的真实用法（可直接抄）：

```cpp
// 5. 通知者：注册的是裸函数指针（无捕获 lambda 也行）
static int g_sum = 0;
static void add1(int v) { g_sum += v; }
notifier<void(int)> n;
n.regist(&add1);  n.notify(5);  n.remove(&add1);

// 6. 责任链 + 策略
chain<int> c;
c.add([](int& r) { if (r < 10) { r += 1; return true; } return false; });
c.add([](int& r) { r += 100; return true; });
int r = 5;  c.handle(r);            // r == 6（第一个处理器返回 true 即停止）

strategy<int(int, int)> st;
st.set([](int a, int b) { return a + b; });
int sum = st.execute(2, 3);          // 5

// 7. 命令 + 撤销/重做
int value = 0;
command_history<> h;
h.execute(command([&value] { value = 10; }, [&value] { value = 0; }));
h.undo();                            // value == 0
h.redo();                            // value == 10
```

> 多线程用法：把 `thread` 模板参数换成 `xhqm::multi_thread`，例如
> `notifier<void(int), xhqm::multi_thread>`、`factory<Base, id, xhqm::multi_thread>`、
> `state_machine<S, E, xhqm::multi_thread>`。`instance` 没有这个参数。

现成的可运行测试（本仓自带，不是示例工程）：`test/pattern_test.cpp`（115 行，notifier / observer 的单线程、多线程与并发用例）、`test/pattern_extended_test.cpp`（144 行，factory / strategy / command / chain / state_machine 的单/多线程用例），在 `test/CMakeLists.txt:178-183` 注册为 `pattern_test` 与 `pattern_extended_test`。注意 `test/CMakeLists.txt` 是**独立 CMake 工程**（自带 `cmake_minimum_required`/`project`，`include_directories("..")`），不被根 `CMakeLists.txt` 的 `add_subdirectory` 收录（根工程里没有 `add_subdirectory`）。

## 当前问题清单

本模块**当前仍存在**的问题（不再包含已修项与历史讨论）统一记在 [DEBUG.md](DEBUG.md)：A 功能性缺陷、B 资源与并发、D 性能、E 未实现、F 设计取舍、G 文档与代码的出入。历史上对本模块的公开描述在根 `README.md` §5.3 与 `software/README.md` 的 pattern 小节，其中已过时的说法在 DEBUG.md 的 G 组逐条订正。
