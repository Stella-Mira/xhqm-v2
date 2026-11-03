# metaclass 元类模块

xhqm 的编译期元编程 / 类型反射 / 通用容器基础层：模板元工具、类型探测与变换、函数签名解析与封装、原始内存块与多维缓冲。纯 header-only（11 个头文件，实测合计 **1396 行**），是全工程最底层的支撑库，被 `algebra`、`lattice`、`fantasy`、`network`、`software` 等模块复用。

> 最后同步：2026-10-07，对照工作副本 r677。
> **当前问题清单在 [`DEBUG.md`](DEBUG.md)**（本文件只写用法与定位，不重复问题条目）。

## 命名空间

| 命名空间 | 内容 |
|---|---|
| `xhqm::` | 容器（`cache`/`block`/`buffer`/`buffer_dim`/`resource`）、函数反射（`is_function`/`ret_function_args`/`function`/`function_task`/`function_call`）、类型工具别名（`rm_ref_t`/`rm_const_t`/`move`/`copy`） |
| `xhqm::templates::`（inline） | 编译期计算（`compute`）、模板元工具（`select_if`/`select_index`/`docker`/`hyper`）、类型分类（`is_class`/`is_pointer`/`symbols_has`） |

## 目录

```
metaclass/
├── compute.h            编译期数值计算（sum/sub/dot/div/select_index）
├── template.h           模板元工具（去引用/去 const、类型选择、docker/hyper 超容器）
├── cache.h              cache 容器（size=0 用 vector，size>0 用 array，execfun 遍历执行）
├── block.h              原始字节块（malloc/free，value_ptr 类型化偏移访问）
├── buffer.h             buffer 一维动态数组 + buffer_dim 多维数组（行优先展平）
├── resource.h           resource 共享指针封装（shared_ptr + ID 转换）
├── function/            函数反射子模块
│   ├── class_type.h     类型分类与变迁（is_class/is_pointer/is_pseudo、class_type_s）
│   ├── function.h       函数签名解析（is_function_s、ret_function_args）
│   ├── function_type.h  function 函数对象封装 + function<void> 类型擦除 + 推导
│   ├── function_call.h  function_task（缓存参数）/ function_call（参数队列）
│   └── symbols_has.h    运算符存在性探测（symbols_has，宏 xhqm_symbols_has）
└── thread/              空目录（预留，暂无实现）
```

## 1. 编译期计算（`compute.h`）

`compute<type>` 提供一组编译期数值运算（`type` 为值类型，如 `int`/`xhqm::size`）：

- `size_count<type...vals>`：参数个数（`sizeof...(vals)`）
- `select_if<select, val_1, val_2>`：按 `bool` 选值；`select_index<select, val...>`：按下标选值
- `sum<vals...>` / `sum_value`、`sub<select, vals...>` / `sub_value`、`dot<vals...>` / `dot_value`、`div<select, vals...>` / `div_value`
  - ~~`sub`/`div` 的 `select` 控制结合方向：`select=true` 为 `val - sum(rest)`，`select=false` 为左结合链式 `((val - v2) - v3) ...`~~
    **已作废（2026-10-07 复核 r677）**：`select` 不是「结合方向」而是「二选一」，且 `false` 分支由于递归里原样透传 `select`，实际退化成「只减/只除最后一项」——
    `select=true` → `val - sum(vals...)`（`div` 为 `val / dot(vals...)`）；`select=false` → `val - 最后一项`（`div` 为 `val / 最后一项`）。
    实测：`sub_value<true,10,2,3> == 5` ✅、`sub_value<false,10,2,3> == 5` ❌（实得 7）。详见 [`DEBUG.md`](DEBUG.md) 的 A2。

## 2. 模板元工具（`template.h`）

- 类型去除：`rm_ref` / `rm_const` 及别名 `rm_ref_t` / `rm_const_t` / `rm_const_ref_t` / `const_ref_t`
- 转移/拷贝：`move(value)`（转发为右值）、`copy(value)`（转发为 `const&`）
- 常量：`always_true<type>` / `always_false<type>`、`is_same<A, B>`（同型判定）、`size_count<args...>`
- 类型选择：`select_if<select, T1, T2>` / `select_if_type`、`select_index<select, types...>` / `select_index_type`
- 便捷别名：`select_class<select, types...>`（选类型）、`select_function<select, types...>`（选类型并包装为 `std::function<...>`）
- 超容器 `docker`：`docker<type, dim, save_type>` 递归嵌套 `save_type`（默认 `std::vector`），`docker_few`/`docker_more` 减/增一维
- 超数组 `hyper`：`hyper<type, dims...>` 递归嵌套 `std::array`，`hyper_few`/`hyper_more`

## 3. 容器（cache / block / buffer / resource）

### cache（`cache.h`）

`cache<type, size_v = 0, device = device_no>`：编译期尺寸 `size_v` 决定底层容器。

- `size_v > 0`：继承 `std::array<type, size_v>`（定长）
- `size_v == 0`：继承 `std::vector<type>`（动态）
- `execfun(f, args...)`：对每个元素调用 `f(value, args...)`（遍历执行）
- 推导指引 + 别名 `indexs<size_v>` = `cache<xhqm::index, size_v>`、`counts<size_v>` = `cache<xhqm::count, size_v>`

### block（`block.h`）

原始字节块，`malloc`/`free` 管理，`xhqm_io` 导出。接口：`init(size)` / `free()` / `good()` / `size()`、`value_ptr<type>(offset)`（按字节偏移返回类型化指针）。支持拷贝/移动构造与赋值。

### buffer（`buffer.h`）

- `buffer<type>`：一维动态数组（`new[]`/`delete[]`）。接口 `reset(size)` / `size()` / `value(pos)` / `data()`，支持拷贝/移动。
- `buffer_dim<type, dim>`：`dim` 维数组，以 `std::array<xhqm::size, dim>` 记录维度，底层展平为一维 `buffer`，`value(location)` 做多维下标 → 线性索引映射。

### resource（`resource.h`）

`resource<type, id>`：`std::shared_ptr<type>` 封装，提供 `share_ptr` 成员、构造/赋值（拷贝=共享指针）、`reset(...)` 与 `operator _id()`（以 `_id(*share_ptr)` 构造 ID）。含推导指引（ID 缺省为 `int`）。

## 4. 函数反射（`function/`）

### 类型分类与变迁（`class_type.h`）

- `is_pseudo_s<type>` / `is_pseudo<type>`：是否为「拟似函数」（含 `operator()` 的仿函数）
- `is_class<type>`：`isvalue`（是否非类类型）/ `isclass` / `ispseudo`，基于「成员指针探测」SFINAE
- `is_pointer<type>`：指针偏特化判定
- `class_type_s<type>`：类型变迁元函数，产出 `type/type_ptr/type_ref/const_type/const_type_ptr/const_type_ref/void_ptr` 及 `remove_ptr/remove_ref/remove_const/remove_const_ptr/remove_const_ref/remove_const_ptr_ref` 等别名，含 `void`、指针、引用、const 全组合偏特化；对应 `class_type`/`class_ptr`/`class_ref`/`remove_const_ref` 等别名

### 函数签名解析（`function.h`）

- `is_function_s<type>`：`value`（是否函数）/ `is_const` / `is_normal`（自由函数）/ `is_member`；覆盖自由函数、函数指针、成员函数指针、const 成员函数指针；别名 `is_function<type>`
- `ret_function_args<type>`：解析函数签名，产出 `return_type` / `argvs_type`（`std::tuple`）/ `argvs_copy` / `class_type` / `function_ptr` / `function_std` / `function_type`，并实现 `call(...)` 虚接口；成员函数特化把 `_class_type*` 前置到参数表

### 函数对象封装与类型擦除（`function_type.h`）

- `function<func_type>`：继承 `ret_function_args<func_type>`，可 `operator bool`；构造自函数指针或仿函数（取 `&F::operator()`）；通过 `function_type_s` 推导指引自动判定函数类型
- `function<void>`：无签名类型擦除容器，`up_function<func_type>(f)` / `up_argvs<func_type>(args)` 手动登记，`call<func_type>(args)` 运行时还原调用（手动 new/delete 实现）

### 任务 / 调用队列（`function_call.h`）

- `function_task<func_type>`：绑定函数 + 预缓存参数，`call()` 用 `std::apply` 以缓存参数调用
- `function_call<func_type>`：绑定函数 + 参数队列 `std::queue`，`argvs(...)` 入队、`next()` 出队、`call()` 取队首参数调用

### 运算符存在性探测（`symbols_has.h`）

`symbols_has<type>`：SFINAE 探测类型是否支持 `+ - * / == < > <= >=`，产出 `has_sum/has_sub/has_dot/has_div/has_equal/has_less_than/has_greater_than/has_less_equal/has_greater_equal`。另提供宏 `xhqm_symbol_return(symbol)`（取成员返回值类型）与 `xhqm_symbols_has(symbol)`（探测成员存在）。

## 架构

模块按「编译期计算 → 类型工具 → 容器 → 函数反射」自底向上分层：

```
compute.h / template.h         编译期数值与类型选择（最底层）
      └─ class_type.h          类型分类与变迁（依赖 template.h）
             ├─ function.h     函数签名解析（依赖 class_type.h）
             │     └─ function_type.h / function_call.h   函数封装与调用
             └─ symbols_has.h  运算符探测（依赖 class_type.h）
cache.h / block.h / buffer.h / resource.h   通用容器（独立，依赖 head.h）
```

`function/` 子目录实现「函数反射」：先由 `class_type.h` 完成类型分类，`function.h` 解析签名，`function_type.h` 提供可调用的 `function` 对象与无签名擦除，`function_call.h` 在其上提供参数缓存/队列的调用语义。容器组独立，仅依赖 `head.h`（与 `template.h`）。

## 依赖

- 内部：`xhqm/head.h`（`size`/`count`/`index`/`byte`/`byte_1`/`byte_2` 别名、`device::type` 枚举、`xhqm_io` 宏）；`template.h`（buffer/function 引用）
- 标准库：`<list>` / `<array>` / `<vector>` / `<functional>` / `<tuple>` / `<memory>` / `<type_traits>` / `<queue>` / `<stdexcept>` / `<cstring>` / `<string>` / `<numeric>` / `<algorithm>`
- 反向依赖（使用本模块）：`algebra/geometry/linear.h`、`fantasy/fantasy.h`、`network/socket.h`、`lattice/struct.h`、`software/system/libmange.h`、`software/pattern/notifier.h`、`test/metaclass_test.cpp`

## 简要用法示例

```cpp
// 真实路径（xhqm/ 下没有 metaclass 目录，只有无扩展名的门面文件；见 DEBUG.md 的 G1）
#include "metaclass/template.h"                 // 或伞头 #include "xhqm/template"
#include "metaclass/compute.h"                  //             #include "xhqm/compute"
#include "metaclass/buffer.h"                   //             #include "xhqm/buffer"
#include "metaclass/cache.h"                    //             #include "xhqm/cache"
#include "metaclass/function/function_type.h"   //             #include "xhqm/function"
using namespace xhqm;

// 编译期计算（注意 sub/div 的 select 语义，见上文）
static_assert(templates::compute<int>::sum_value<1, 2, 3, 4> == 10);
static_assert(templates::compute<int>::select_index<2, 10, 20, 30>::value == 30);

// 类型选择与去修饰
select_if_type<true, int, float> a;      // int
rm_const_ref_t<const int&> b;            // int

// 多维容器（注意：buffer_dim::value() 当前编不过，且下标是「第 0 维最快变化」，见 DEBUG A1/A3）
buffer_dim<float, 2> mat(std::array<xhqm::size, 2>{3, 4});   // 3×4
// mat.value(loc) = 3.14f;               // 现版本无法编译

// 函数反射与封装
int add(int x, int y) { return x + y; }
function<int(int, int)> f = &add;
int r = f(1, 2);                         // 3
function_task task(&add, 3, 4);
int r2 = task();                         // 7（用缓存参数）

// 运算符探测
static_assert(symbols_has<int>::has_sum); // int 支持 +
```

## 已知问题 / 待补

**完整清单见 [`DEBUG.md`](DEBUG.md)。** 这里只留三条最容易踩到的：

- `buffer_dim::value()` 的两个重载都编不过（形参是 `const` 却在函数体里被赋值，`buffer.h:178`/`:166`），本模块的「多维取值」当前完全不可用。
- `compute` 的 `sub`/`div` 在 `select=false` 时退化为「只减/只除最后一项」，与直觉相反（`compute.h:35`/`:49`）。
- `function<void>` 的析构函数里两行 `std::runtime_error` 是死代码（不 `throw`），手动 `new` 出来的函数对象与参数元组**永不释放**（`function_type.h:44-48`）。
