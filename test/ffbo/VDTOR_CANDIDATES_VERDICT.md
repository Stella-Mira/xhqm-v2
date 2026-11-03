# 虚析构候选 7 例的判定（第三方，macOS / Apple clang）

> 工具：`check_vdtor.py`（纯文本扫描，只出**候选**）
> 判定手段：编译器与标准库特征做**权威判定**，不靠肉眼读继承链
> 判定者：蝶清梦（Sylvia Muse），macOS / arm64 / Apple clang
> 结论：**7 个候选里 3 个是真缺陷**（其中 1 个能稳定复现 SIGTRAP），
> 2 个是永远不参与编译的死候选，1 个是扫描器假阳性，1 个其实已有虚析构。
> 本文只记录判定结论，**不改任何源码**。

## 0.5 处置状态（后续更新）

| 项 | 处置 | 版本 |
|---|---|---|
| `nebula::addr_b` 虚析构 | **已修**（`addr.h` +1 行 `virtual ~addr_b() = default;`） | r550 |
| `geometry::shape` 虚析构 | **已修**（`geometry.h` +1 行） | r550 |
| 回归夹具 | **已补** `test/polydtor_test.cpp`（三条释放路径 + 编译期特征断言） | r550 |
| `dispatch_no.h` 坏代码 + `task` 虚析构 | **已清理**（构造函数名 `array`→`share`、`= delete` 指向自身、去掉虚函数尾逗号、补 `virtual ~task()`） | r553 |
| `dispatch_no.h` 拉进编译面 | **已补** 夹具用例 `test_parallel_dispatch_no` | r553 |
| `check_vdtor.py` 假阳性/假阴性 | **已修**（剥注释 + 解析继承链；7 候选 → 1） | — |
| `function.h:63` `_ret_function_args` | **未修**（判定为死候选：`_ret_function_args_ptr` 只有 typedef 自身一处，无多态持有） | — |

两个已修项的夹具都验证过**有效性**：把 fix 临时撤掉后夹具确实失败
（`addr_b`：断言 FAILED + SIGTRAP exit 133；`task`：3 处断言 FAILED），恢复后全绿。


## 0. 判定方法（为什么不是"看代码"）

「有纯虚函数、无虚析构」只是候选。真判据是**会不会通过基类指针 delete**。
肉眼读代码在两种情形下会判错：

- 虚析构可能**继承自基类**（候选类自己的 body 里看不到）；
- 纯虚函数可能写在**注释里**（扫描器按文本匹配，照样命中）。

所以本文的判定用两条编译期证据，逐类给出：

```cpp
std::has_virtual_destructor_v<T>          // 含继承链，权威
```

```bash
clang++ -Wall -Wdelete-abstract-non-virtual-dtor \
        -Wdelete-non-abstract-non-virtual-dtor ...
```

后者的警告文本本身就是判据：
`delete called on '<T>' that is abstract but has non-virtual destructor`。
补充证据：clang 会在 **libc++ 的 `unique_ptr.h:77`** 上报出该警告
（编译器自己承认 `unique_ptr` 会 delete 该类型），这比手写一遍 `delete` 更强。

判据统一，判定表如下。

## 1. 判定表

| # | 候选 | 位置 | `has_virtual_destructor` | 真缺陷？ | 依据 |
|---|---|---|---|---|---|
| 1 | `geometry::shape` | `algebra/geometry/geometry.h:66` | **false** | **是（潜在）** | 编译器警告命中；但 `shape*` 全仓库零使用 |
| 2 | `parallel::task` | `hardware/parallel/dispatch_cpu.h:34` | — | **否（死代码）** | 该行在 `//` 注释内，扫描器假阳性 |
| 3 | `parallel::task` | `hardware/parallel/dispatch_no.h:91` | — | **否（死代码）** | 该文件无人 include、库内 0 符号，且自身编译不过 |
| 4 | `_ret_function_args` | `metaclass/function/function.h:63` | — | **否（死候选）** | `_ret_function_args_ptr` 只有 typedef 自身一处，无多态持有 | 
| 5 | `nebula::addr_b` | `network/nebula/addr.h:74` | **false** | **是（现行）** | 编译器警告 + 运行时 SIGTRAP，见 §2 |
| 6 | `nebula::addr_t` | `network/nebula/addr.h:93` | **false** | **是（现行）** | 同上，`unique_ptr<addr_b>::clone()` 会 delete 各特化 |
| 7 | `nebula::data` | `network/nebula/data.h:64` | **true** | **否（已正确）** | 虚析构继承自 `find`：`network/nebula/find.h:79` `virtual ~find()` |

`std::has_virtual_destructor` 实测输出（编译期，含继承链）：

```
xhqm::geometry::shape                          ★无虚析构★
xhqm::geometry::line                           ★无虚析构★
xhqm::nebula::addr_b                           ★无虚析构★
xhqm::nebula::addr_t<xhqm::nebula::kind::ipv4> ★无虚析构★
xhqm::nebula::addr_t<xhqm::nebula::kind::ipv6> ★无虚析构★
xhqm::nebula::data                             有虚析构 ✅
xhqm::nebula::post                             有虚析构 ✅
xhqm::nebula::find                             有虚析构 ✅
xhqm::nebula::link_b                           有虚析构 ✅
```

`nebula::data` 是关键反例：扫描器把它报成候选，但它**已经正确**
（`find.h:79` 有 `virtual ~find()`，`data` 自己还有 `~data() override;`）。
**只读 body 的扫描器对"虚析构来自基类"是盲的**，这里出的是**假阴性**。

## 2. `addr_b` / `addr_t`：现行缺陷，可稳定复现零输出崩溃

证据一（编译器自己炸在标准库头里，`-Wall`）：

```
p5_addrb.cpp:17:5: warning: delete called on 'xhqm::nebula::addr_b' that is abstract
                            but has non-virtual destructor [-Wdelete-abstract-non-virtual-dtor]
libc++/__memory/unique_ptr.h:77:5: warning: delete called on non-final
        'xhqm::nebula::addr_t<xhqm::nebula::kind::ipv4>' that has virtual functions
        but non-virtual destructor [-Wdelete-non-abstract-non-virtual-dtor]
libc++/__memory/unique_ptr.h:77:5: ... addr_t<kind::ipv6> / addr_t<kind::serial> ...
```

证据二（运行时，`delete` 一个派生自 `addr_b` 的具体类）：

```
== 通过基类指针 delete（addr_b*）==
Process stopped, stop reason = EXC_BREAKPOINT (code=1, subcode=0x1000007ec)
frame #0: xhqm::nebula::addr_b::~addr_b() + 16
->  0x1000007ec <+16>: brk    #0x1        ← SIGTRAP，exit 133
```

**这正是 `ecs_test` 当初那个 SIGTRAP 的同一签名**（r546 修的 `fantasy::drawer`）：
arm64 + 优化下「基类指针 delete 派生类、基类析构非虚、派生类多出一块内存」会直接 `brk #0x1`，
症状是**零输出崩溃**，极难定位。

危害面（比 `shape` 严重）：

- `using addr = std::shared_ptr<addr_b>`（`addr.h:91`）——
  `nebula::addr` 遍布 `m_answer_addr` / `m_listen_addr` / `broadcast()` / `dial()` 等，
  引用计数归零时**必然经 `addr_b*` 释放**；
- `virtual std::unique_ptr<addr_b> clone() const = 0`（`addr.h:84`）返回基类 unique_ptr，
  三个特化各有实现（`addr.h:161 / 255 / 348`）——**克隆一次就是一次多态删除**；
- 派生类是**非平凡**的：`using text = std::string`（`addr.h:48`），
  特化里有 `text m_host` 等成员 ⇒ 跳过派生析构会**漏 `std::string` 的堆缓冲**；
- 间接证明：编译探针时 clang 连 `addr_t<serial>` 之类**没被探针引用的特化**都报了警告，
  说明 `unique_ptr` 的删除路径覆盖了全部特化。

修法（供参考，未实施）：`struct addr_b { ... virtual ~addr_b() = default; };`
参照工程内既有正确写法：`query.h:15` / `store.h:15` / `zero.h:22` 都是
`virtual ~xxx() = default;`，`find.h:79` / `link_b`（`addr.h:487`）也是虚析构。

## 3. `shape`：真缺陷，但是"潜在"（现无触发路径）

- `algebra/geometry/geometry.h:66` `struct xhqm_io shape { virtual xhqm::string type() const = 0; };`
- 派生类**有 20+ 个**（`line / face / cube / grid / mesh / circle / sphere / frustum /
  plane / box / cylinder / cone / torus / capsule / quad / curve / surface` …）；
- 编译器警告命中（探针全文见 §5）；运行时同样静默跳过派生析构；
- **但 `geometry::shape*` 全仓库零使用**（`grep 'shape\*'` 无结果），
  没有任何地方经基类指针持有/删除它 ⇒ **当下无触发路径**；
- 派生类目前只有值类型成员，暂无「多出一块内存」的布局差异。

⇒ 结论：**不是现行缺陷，是埋雷**。谁日后写 `shape*`（或 `unique_ptr<shape>`）就会踩。
建议与 `addr_b` 一起修（一行 `virtual ~shape() = default;`）。

## 4. 两个死候选 & 一个假阳性（附带挖出的既有问题）

### 4.1 `hardware/parallel/dispatch_cpu.h:34` —— 扫描器假阳性

该文件里 `struct task` **只出现在注释行**内：

```
34:		//struct task : public _task
```

扫描器的正则按**文本**匹配，注释里的 `= 0;` 照样命中。
复现（直接调用扫描器的匹配逻辑）：

```
匹配到 class task，行 34
body 里面含纯虚声明: True  ← 来自注释文本
body 前 60 字: '\n\t\t//\tvirtual void call(xhqm::parallel::array<type, equip>& '
```

### 4.2 `hardware/parallel/dispatch_no.h:91` —— 死代码，且该文件**根本编译不过**

- 全仓库只有 `dispatch_cpu.h:3` `#include "dispatch_no.h"`，而 `dispatch_cpu.h` 自己无人 include；
- 伞头 `hardware/hardware.h` 不引用 `hardware/parallel/`；
- `hardware/parallel/` 下**只有这两个 .h，没有任何 .cpp**；
- `nm` 查已构建的 `libxhqm.a`：**0 个 `dispatch` / `parallel` 符号**；
- 该文件自己就编不过：`dispatch_no.h:59:4: error: a type specifier is required`
  —— 类名是 `share`（`:57 struct share : public _array, public std::vector<type>`），
  构造函数却写成 `array(...)`（`:59-66`）。同文件 `:74` 另有正确定义的 `struct array`。
  **这是一处复制粘贴残留，因为从不参与编译所以从未暴露。**

⇒ 这 2 个候选不影响编译产物，改与不改都无收益；但 `dispatch_no.h` 的
「类名 `share` / 构造函数 `array`」本身就是待清理的坏代码，建议单独记为清理项。

### 4.3 `metaclass/function/function.h:63` —— 死候选

`typedef struct _ret_function_args_base { }*_ret_function_args_ptr;`（`:60`）
**全仓库只有这一行自己**，没有任何地方用该指针类型持有或删除对象；
6 个派生（`:79 / 103 / 127 / 151` 等）全是栈上的模板实例，无多态删除。

## 5. 复现方式（可重跑）

前置：只需一个 include 路径 `-I<xhqm>`；链接已构建的 `binary/libxhqm.a` 可跑运行时探针。

> **踩坑记录**：不要额外加 `-I<xhqm>/algebra`。
> 那会让系统头 `<cctype>` 被 `algebra/ctype.h` 抢先命中，
> libc++ 立刻在 `__string/char_traits.h` / `__algorithm/find.h` 报一堆
> `wint_t` / `__constexpr_wmemchr` 未定义。CMake 的真实配置只有 `-I./`（见 `CMakeLists.txt:79`）。

```bash
XH=~/Space/WorkSpace/xhqm
# 判据证据（编译器警告）
clang++ -std=c++17 -I$XH -Wall -Wdelete-abstract-non-virtual-dtor \
        -Wdelete-non-abstract-non-virtual-dtor probe.cpp
# 运行时证据（SIGTRAP）
clang++ -std=c++17 -I$XH probe.cpp ~/Space/WorkSpace/binary/libxhqm.a -o probe && ./probe
```

## 6. 给 `check_vdtor.py` 的两条改进建议（工具属清梦侧，供参考）

1. **扫描前剥注释**（至少剥行注释）：可消除 4.1 的假阳性，`dispatch_cpu.h:34` 会消失。
2. **body 里看不到虚析构时，必须报"待查"而不是"缺陷"**：
   `nebula::data` 的虚析构来自 `find`，扫描器判不出来（假阴性）。
   建议在输出里加一句「虚析构可能继承自基类，请用 `std::has_virtual_destructor` 复核」，
   与本文 §0 的方法一致——**权威判定交给编译器，扫描器只负责把范围缩小**。
