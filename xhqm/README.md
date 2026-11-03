# xhqm 核心层

> 最后同步：2026-10-07，对照工作副本 r677。

> 引擎公共头与聚合门面。本目录（`xhqm/`）是 xhqm 引擎的「核心层 / 门面层」：只做模块聚合与类型/宏统一，不含具体实现。具体实现分布在上层各子模块（`algebra/`、`lattice/`、`metaclass/`、`network/`、`software/`、`fantasy/`、`hardware/`）。

## 一、模块定位

xhqm 是一个 C++17 的数学 / 渲染 / 网络 / 系统综合引擎。核心层负责三件事：

1. **公共基础头 `head.h`**：导出宏、跨平台宏、基础类型别名、宏函数、`os`/`device` 枚举、`error` 异常、字面量运算符。
2. **聚合头 `xhqm.h` 与 `xhqm/*` 门面**：用 `#define` 开关按需/全量拉入各子模块，形成单一包含入口。
3. **构建根 `CMakeLists.txt`**：模块开关、设备（CPU/GPU/NET/AUTO）、张量后端、渲染后端的选择与降级约定。

## 二、关键文件

| 文件 | 作用 |
|---|---|
| `../head.h` | 公共基础头（279 行）：导出宏 / 类型别名 / 宏函数 / 平台与设备枚举 |
| `../head.cpp` | 草稿文件（180 行，`main()` 在 `:55`）：**不编入库**（CMake 与 vcxproj 都无该文件），且当前 GCC 下编不过（`:63`，见 `DEBUG.md` E1） |
| `../xhqm.h` | 引擎聚合头（81 行）：`USING_XHQM` 全量，或 `XHQM_USE_XXX` 按需 |
| `xhqm/*` | 22 个无扩展名门面头（共 131 行、113 条 `#include`）：只做聚合 include，把各子模块头收拢成单一包含点 |
| `../CMakeLists.txt` | 构建根（721 行）：C++17、模块开关、设备/张量后端/渲染后端选择 |
| `../xhqm.vcxproj` / `../xhqm.sln` | 第二条构建路径（VS 工程，单工程）。**只覆盖 CMake 登记文件的子集**：98 个源文件 vs CMake 263 个，且恒定义 `XHQM_DYNAMIC;XHQM_PROGRAM`（只出 DLL），22 个门面头一个都不在工程里（只有 `xhqm.h`）；详见 `DEBUG.md` E2 |

## 三、关键类 / API

### 1. 导出与编译宏（`head.h`）

- `XHQMEXPORT` / `XHQMIMPORT`：Windows `dllexport/dllimport`，其余平台为空。
- `XHQMCLASS`：由 `XHQM_PROGRAM` 决定导出或导入。
- `xhqm_io`：由 `XHQM_DYNAMIC` 决定是否参与动态库边界。
- `XHQM_JSON_EXTERNAL`：定义则使用环境 `nlohmann/json.hpp`，否则使用引擎内置 `network/json.hpp`。
- 自动定义 `NOMINMAX`，避免与 `algebra/mathf` 的 `min/max` 模板冲突。

### 2. 基础类型别名

```cpp
namespace xhqm_std {
    template<typename type, size_t size> using array  = std::array<type, size>;
    template<typename type, size_t size> using grids  = std::array<std::array<type, size>, size>;
    template<typename type, size_t size> using magic  = std::array<std::array<std::array<type, size>, size>, size>;
    template<typename type>              using list   = std::list<type>;
    template<typename type>              using vector = std::vector<type>;
    template<typename type>              using matrix = std::vector<std::vector<type>>;
    template<typename type>              using tensor = std::vector<std::vector<std::vector<type>>>;
    // docker<type, size>：size=0 退化为 vector，否则为定长 array
}

namespace xhqm {
    using string = std::string;
    using index  = std::size_t;  // 下标
    using count  = std::size_t;  // 数量
    using size   = std::size_t;  // 大小
    using id     = std::size_t;  // ID
    template<size_t dim = 0> using coord = docker_s<index, dim>::value; // 坐标
    template<size_t dim = 0> using topol = docker_s<index, dim>::value; // 拓扑
    template<size_t dim = 0> using shape = docker_s<count, dim>::value; // 形状

    // 内存体系
    using real = float;          // 默认实型
    using writ = signed char;
    using sign = signed char;
    using byte = unsigned char;
    using text = std::string;
    using byte_1 = std::uint8_t;
    using byte_2 = std::uint16_t;
    using byte_4 = std::uint32_t;
    using byte_8 = std::uint64_t;
}
```

### 3. 平台 / 设备枚举

```cpp
// 平台探测：os::type（os_no/ios/mac/unix/linux/posix/window/android/harmony）
struct os { enum type { ... }; constexpr static type this_os = ...; };
// 编译期平台宏：XHQM_WINDOWS / XHQM_IOS / XHQM_MAC / XHQM_LINUX / ...

// 设备结构 + 大小端探测
struct device {
    enum type { device_no, device_cpu, device_gpu, device_net,
                device_memory, device_cpu_memory, device_gpu_memory, device_net_memory };
    static bool isle();  // 小端
    static bool isbe();  // 大端
};

// 硬件设备选择（hardware/hardware.h，各模块共享）
namespace xhqm::hardware {
    enum class device : std::uint32_t { no = 0, cpu = 1, gpu = 2, net = 3, auto_ = 4 };
    constexpr device compiled_device();       // 编译期默认设备
    constexpr device resolve_auto();          // auto 解析：gpu → cpu → no
    constexpr bool   device_compiled(device); // 某设备是否已编译
    device active_device();                    // 运行时激活设备
    void   active_device(device);              // 切换
    const char* device_name(device);           // 设备名
}
```

### 4. 宏函数与工具（`head.h`）

```cpp
xhqm_max(a, b)  xhqm_min(a, b)  xhqm_abs(a, b)          // 极值/差值
xhqm_rount(v)   xhqm_equal(a, b, p)  xhqm_preci(a, b)   // 取整/带精度比较
xhqm_create(ptr, C)  xhqm_delete(ptr)                   // new/delete 守卫
xhqm_byte(data, i)  xhqm_uchar / xhqm_schar / xhqm_f32 / xhqm_f64  // 字节/窄化转换
XHQMCONNECTSTR(x, y)  XHQMTOSTR(x)                      // 宏拼接 / 字符串化
```

### 5. 异常与字面量

```cpp
class error : public std::runtime_error { ... };  // 引擎统一异常

inline namespace literals {
    bool operator""_false(const char*, size);  // 打印并返回 false
    bool operator""_true(const char*, size);   // 打印并返回 true
    void operator""_cout(const char*, size);   // 打印
    void operator""_cout(unsigned long long);
    void operator""_cout(long double);
}
```

### 6. 聚合头开关（`xhqm.h`）

```cpp
#define USING_XHQM   // 一次性开启全部模块
// 或按需：
#define XHQM_USE_ALGEBRA      // 代数（基础类型/张量/几何/分析）
#define XHQM_USE_LATTICE      // 图结构（链/树/网）
#define XHQM_USE_METACLASS    // 元类（block/buffer/cache/函数反射）
#define XHQM_USE_NETWORK      // 网络（socket/加密/astral 记忆/nebula 拓扑网络）
#define XHQM_USE_SOFTWARE     // 软件（信号槽/事件/数据/消息/命令/系统/设计模式）
#define XHQM_USE_FANTASY      // 渲染引擎
#define XHQM_USE_HARDWARE     // 硬件（设备选择）
#include "xhqm.h"
```

> 宏名带 `USE_` 前缀，避免与各头文件的 include guard（如 `XHQM_FANTASY` / `XHQM_HARDWARE`）冲突。

**开关不对称（实测口径，务必按这个理解）**：`xhqm.h:21-31` 一共 7 个 `XHQM_USE_*`，而 `CMakeLists.txt:41-44` 只有 4 个 `XHQM_BUILD_*`（`TENSOR` / `NETWORK` / `FANTASY` / `SYSTEM`，**默认全 OFF**），且两族名字并不一一对应（`XHQM_USE_ALGEBRA` ↔ `XHQM_BUILD_TENSOR`、`XHQM_USE_SOFTWARE` ↔ `XHQM_BUILD_SYSTEM`）；`LATTICE` / `METACLASS` / `HARDWARE` **没有任何 CMake 开关**，它们的头被恒收进 `XHQM_HEAD_CORE`（`CMakeLists.txt:209-248`）恒参与编译。两族宏在源码里都**没有** `#ifdef` 消费点（`grep -rn "XHQM_BUILD_" --include=*.h --include=*.cpp .` 只命中 `xhqm.h:9` 的一句注释；`XHQM_USE_*` 只被 `xhqm.h` 自己使用）——它们都是**文件集选择器**，不是编译期特性门。

## 四、架构

```
xhqm/                       ← 核心层（本目录，纯门面）
├── head.h                  公共基础头（类型/宏/枚举/异常/字面量）
├── xhqm.h                  引擎聚合头（模块开关）
├── CMakeLists.txt          构建根
└── xhqm/*                  22 个无扩展名门面头
    ├── algebra / tensor / geometry / analysis    → algebra/
    ├── lattice                                    → lattice/
    ├── block / buffer / cache / function / tuple_values → metaclass/
    ├── network / cipher / astral / nebula         → network/
    ├── software / signal / threads / toolset / pattern → software/
    ├── fantasy                                    → fantasy/
    └── datum / event（单头映射）                  → software/
```

> 上表 22 个门面已全数列出（旧版漏了 `tuple_values`，已补）。`xhqm.h` 并不逐个引用这 22 个门面：它引用了其中 19 个（`datum` / `event` 由 `xhqm/software` 直接引底层头覆盖，`pattern` 只被 `xhqm/software` 的 9 条同类 include 覆盖），因此单独 `#include "xhqm/pattern"` 是合法但无人引用的入口。

**分层**：`head.h`（基础）→ `xhqm/*`（门面）→ 各子模块头/实现。

**构建**：CMake 用 `XHQM_BUILD_*` 控制模块编入；核心（`XHQM_HEAD_CORE` / `XHQM_BODY_CORE`）恒编译，包括 hardware、algebra 基础类型、geometry、metaclass 基础、lattice 基础、system 基础进程/线程/时间/文件/代码/执行，以及 `software/signal`（信号槽池，nebula 的 `frame_signal`/`broad_signal` 依赖它）、`network/uuid` 与 `md5`（多模块公共依赖，`CMakeLists.txt:546-549` 无条件追加）、张量 `no` 串行桩（保证关掉 TENSOR 模块时 `tensor_backend_active` 仍可链接运行）。

**两条构建路径的规模差（实测）**：`CMakeLists.txt` 共登记 263 条源文件路径（去掉 3 条变量/注释造成的假象后，仓内文件 260 个；其中 10 个 `set(XHQM_HEAD_*/XHQM_BODY_*)` 块 252 条 + 无条件追加的 uuid/md5 + 按条件 `target_sources` 的渲染/张量后端）；`xhqm.vcxproj` 只登记 98 个，**差集实测 165 个仓内文件**（fantasy 90、algebra 42、software 19、lattice 6、metaclass 6、hardware 2）——即 fantasy/algebra 的 tensor·analysis·soul/metaclass/lattice/hardware/geometry 都不在 VS 工程里。所以 `xhqm.sln` 是**子集路径**，不是 CMake 的等价替代；差异清单见 `DEBUG.md` E2。

## 五、子模块一览

| 子模块 | 开关 | 内容 |
|---|---|---|
| `hardware` | **无开关（恒编入 `XHQM_HEAD_CORE`）** | 设备枚举 `no/cpu/gpu/net/auto` 与激活/解析，供张量后端、渲染复用 |
| `algebra` | `XHQM_USE_ALGEBRA` / `XHQM_BUILD_TENSOR` | 数值类型（`f16/f32/f64`、complex/dualnum/quaternion/dual_quaternion）、泛型数学函数、`tensor/matrix/vector/tensor_view`、线性代数/稀疏矩阵/后端（no/cpu SIMD/BLAS/gpu CUDA/Metal）、`ctype` 类型系统、`geometry` 几何、`analysis`（MATLAB 风格数值接口）、`soul`（Transformer 前向 + reverse-mode 自动微分） |
| `lattice` | **无 CMake 开关**（基础恒编入 `XHQM_HEAD_CORE`）；聚合头用 `XHQM_USE_LATTICE` | 链/树/网三族图结构（next/prev/bind/ring、child/parent/bind、arrow/bind mesh）+ 线程安全队列/栈 |
| `metaclass` | **无 CMake 开关**（基础恒编入 `XHQM_HEAD_CORE`）；聚合头用 `XHQM_USE_METACLASS` | `block`（字节块）、`buffer`/`buffer_dim`（定长/动态数组）、`cache`（定长/动态封装）、`compute`/`template`（模板元）、`function`（函数类型反射/封装，当前 GCC 下编不过，见 `DEBUG.md` C2） |
| `network` | `XHQM_USE_NETWORK` / `XHQM_BUILD_NETWORK` | 加密（md2/4/5/sha/aes/des/uuid/base64/license）、socket（TCP/UDP 双栈、帧协议）、mmap、cs、`astral`（一切皆节点的记忆库）、`nebula`（B 世代拓扑网络：addr/data/life/link/find/post/zero，见 `network/nebula/README.md`） |
| `software` | `XHQM_USE_SOFTWARE` / `XHQM_BUILD_SYSTEM` | `datum`（异构数据注册/JSON 序列化）、`event`、`packet/message/command/project/utility`、`signal`（对标 Qt 信号槽）、`system`（`timesys/filesys/codesys/execsys/process/threads/libmange`）、`pattern`（observer/notifier/factory/strategy/command/chain/state_machine/instance/thread_mode） |
| `fantasy` | `XHQM_USE_FANTASY` / `XHQM_BUILD_FANTASY` | 实时渲染：双后端 RHI（OpenGL/Vulkan）、场景图、绘制、光照/阴影、后处理、动画、GPU 剔除、glTF 加载、可插拔渲染管线 |

各子模块详情见各自 `README.md`（`algebra/`、`algebra/analysis/`、`algebra/geometry/`、`algebra/tensor/`、`algebra/soul/`、`lattice/`、`software/`、`fantasy/`）。

## 六、依赖

- **标准**：C++17（`CMAKE_CXX_STANDARD 17`）。
- **JSON**：内置 `network/json.hpp`（nlohmann 吸收版），或 `-DXHQM_JSON_EXTERNAL` 用环境版。
- **可选（编译期自动检测，缺失回退）**：
  - 张量 CPU：OpenBLAS（`../depend/openblas`）→ SIMD（MSVC AVX2）→ 多线程 → 串行；
  - 张量 GPU：CUDA（cuBLAS/cuSOLVER）或 Apple Metal/MPS；
  - 渲染：GLEW + OpenGL、Vulkan + glslang/SPIRV-Tools（Linux/macOS 系统包，Windows `../depend/vulkan`）。
- 第三方依赖统一放上层 `../depend/`。

## 七、简要用法示例

示例里的类型名、参数与宏全部取自代码本身的真实用法；标注的「实测」= 本节点用 g++ 11.4（`-std=c++17 -fsyntax-only`，`-I.` + `-I../depend/nlohmann`，按需再加下面 §2 那组 `-I`）跑过。

### 1. 最小可运行（不带任何模块开关）

`#include "xhqm.h"` 在没有任何 `#define` 时只拉入 `head.h`（类型别名 / 宏函数 / `os`·`device` 枚举 / `error` / 字面量运算符）：

```cpp
#include "xhqm.h"          // 编译时把仓库根放进 -I（-I.）
using namespace xhqm;

int main() {
    byte b = 0;                      // 内存体系别名（head.h:233）
    xhqm::error e("boom");           // 引擎统一异常（head.h:240）
    std::cout << (device::isle() ? "little" : "big") << " endian" << std::endl;
    return (int)b;
}
```

实测：通过。要按需开一个模块就加一个开关，例如只开硬件层：

```cpp
#define XHQM_USE_HARDWARE
#include "xhqm.h"
#include <iostream>

int main() {
    // 编译期默认设备（hardware/hardware.h:45）；resolve_auto() 是 gpu→cpu→no 的运行时解析
    xhqm::hardware::device dev = xhqm::hardware::compiled_device();
    std::cout << xhqm::hardware::device_name(dev) << std::endl;   // "cpu"/"gpu"/"net"/"auto"/"no"
    return 0;
}
```

实测：通过。7 个 `XHQM_USE_*` 里，`ALGEBRA` / `LATTICE` / `NETWORK` / `SOFTWARE` / `FANTASY` / `HARDWARE` 六个实测 0 错误，只有 `METACLASS` 编不过（见下 §2 与 `DEBUG.md` C2）。

> 直接包含单个门面头（`#include "xhqm/event"`、`#include "xhqm/lattice"`、`#include "xhqm/function"`）目前有 3 个不自足而编不过，先看 `DEBUG.md` A1/A2/C2。

### 2. 全量包含（`USING_XHQM`）

```cpp
#define USING_XHQM
#include "xhqm.h"
using namespace xhqm;

int main() {
    float_32 f = number<f32>::pi;           // 数值类型：algebra/ftype.h:61、algebra/value.h:67
    std::cout << f << std::endl;

    hardware::device dev = hardware::compiled_device();
    std::cout << hardware::device_name(dev) << std::endl;  // 当前设备
    return 0;
}
```

实测（GCC 11.4）**两条注意**：

- 这条包含链会走到 fantasy，必须补上 CMake 那组额外 `-I`（`CMakeLists.txt:82-93`：`fantasy/`、`fantasy/backend/`、`fantasy/spatial/`、`fantasy/lighting/`、`fantasy/postfx/`、`fantasy/animation/`、`fantasy/runtime/`、`hardware/`、`software/`），否则第一跳就死在 `fantasy/drawer.h:7: fatal error: backend.h: 没有那个文件或目录`。
- 补上 `-I` 之后仍然编不过：`XHQM_USE_METACLASS` 会拉入 `metaclass/function/symbols_has.h` 的 9 行 MSVC-only 语法（9 errors，`DEBUG.md` C2）。**「全量包含」当前只在 MSVC 下成立。**

### 3. 按需包含

```cpp
#define XHQM_USE_ALGEBRA
#define XHQM_USE_NETWORK
#include "xhqm.h"
using namespace xhqm;

int main() {
    // 张量：模板参数顺序是 <维数, 元素类型>（algebra/tensor/tensor_base.h:18）
    tensor<2, double> t({3, 4}, 0.0);   // 构造 = ({各维长度}, 初值)（tensor_base.h:131）
    // 网络帧收发：connect_tcp_auto_timeout(host, port, timeout_ms) → hander（network/network.h:350）
    auto sock = network::connect_tcp_auto_timeout("127.0.0.1", 8080, 1000);
    return 0;
}
```

实测：`xhqm::tensor<2,double> t({3,4}, 0.0);` 通过。~~`xhqm::tensor<double, 2>`~~ **已作废（2026-10-07 复核 r677）**：旧示例把类型写在维数前面，实测 3 个错误（`type/value mismatch`）；真值声明见 `algebra/tensor/tensor_base.h:18`。

### 4. CMake 构建

```bash
cmake -S . -B build \
  -DXHQM_DEVICE=CPU \
  -DXHQM_TENSOR_BACKEND=ON \
  -DXHQM_BUILD_TENSOR=ON \
  -DXHQM_BUILD_NETWORK=ON \
  -DXHQM_BUILD_SYSTEM=ON \
  -DXHQM_BUILD_FANTASY=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

- `XHQM_DEVICE`：`NO / CPU / GPU / NET / AUTO`（默认 CPU）。
- `XHQM_DYNAMIC`：定义则编动态库（`XHQM_PROGRAM` + `XHQM_DYNAMIC` 自动注入），否则静态库。
- 产物输出到 `../binary/`（可用 `XHQM_OUTPUT_DIR` 覆盖）。

## 八、已知问题

**当前仍存在的问题（每条带 `文件:行号` 证据）见同目录 `DEBUG.md`**——本模块的清单按 A 功能性缺陷 / B 资源与并发 / C 跨平台 / D 性能 / E 未实现与占位桩 / F 不修的设计取舍 / G 文档与代码出入 分组。此处只留历史订正记录与三条要点：

- `test/polydtor_test.cpp` 与 `test/nebula_api/nebula_api_test.cpp` 仍是 A 世代 nebula API，实测 GCC 11.4 下分别 **20 处**与 **100+ 处**编译错误（`kind::none`、`addr_t<kind>`、`link_t<kind>`、`link_b`、`data(link)`、`clone/equal_other/broadcast/send/to_json/from_json` 这些名字在当前代码里都不存在）。`test/CMakeLists.txt:156-172` 的 `polydtor_test` 目标列了 15 个源文件，其中 nebula 侧只有 `../network/nebula/addr.cpp`，缺 `addr/` 下 5 个（`addr_b/addr_uuid/addr_ipv4/addr_ipv6/addr_serial`），所以即便改好 A 世代调用，链接期仍会缺符号。
  ~~旧文把 `test/nebula_api/` 的例子写成 `to_json/clone/equal/broadcast/send`、`server`/`sensor`~~ **已作废（2026-10-07 复核 r677）**：该目录 `nebula_api_test.cpp:286/309` 的 `to_json/from_json` 是 `hardware::media::caps` / `audio_format` 的方法，`server::media_server` 是该目录自带的命名空间——都不是 A 世代 nebula API 的证据；它真正的 A 世代残留是 `addr_t<kind>`（`:39`）与 `link_t<kind>`（`:58`/`:111`/`:243`/`:448`/`:457`/`:466`/`:586`/`:592`）。另外该目录在树内**没有构建入口**（无 CMakeLists）。
- ~~`network/nebula/` 的 `kind` 词表里 `ipv6 / serial / bluetooth / nearlink / quantum` 只有文本互转，没有实现类（`addr_from` 目前只造 `ptr` 回环与 `ipv4`）。~~
  **已作废（2026-10-07 复核 r677）**：`ipv6` 与 `serial` 的实现类都已落地，并在文件树与 CMake BODY 里（`CMakeLists.txt:383-384` 的 `addr/addr_serial.cpp`、`addr/addr_ipv6.cpp`；`addr/addr_uuid.cpp` 是原 `addr_ptr` 的改名为 uuid 后的实现——SVN r653 同一次提交里 `D addr_ptr.*` + `A addr_uuid.*`，可查证）。实际口径：`addr_from` 造 `kind` 为 `uuid / ipv4 / ipv6 / serial` 四种（`network/nebula/addr.cpp:14-19`）；仍只有词与文本互转、无实现类的是 **`bluetooth / nearlink / quantum`** 三个（枚举在 `addr/addr_b.h:21-23`，文本互转在 `addr/addr_b.cpp:20-22/33-35`）。细节见 `network/nebula/README.md`。
- `head.cpp` 是含 `main()`（`:55`）的草稿文件，未纳入任何构建（`CMakeLists.txt`、`xhqm.vcxproj` 全无该文件名）；~~仅作语法冒烟~~ **已作废（2026-10-07 复核 r677）**：GCC 下**编不过**——唯一错误在 `head.cpp:63` 的 `0b0000001111111111ui16`（无对应字面量运算符；`ui16` 是 MSVC 扩展后缀，GCC 提示的 `-fext-numeric-literals` 也不适用）。注意 `head.cpp:12-52` 整段是 `/* */` 注释块，其中 `:22` 的 `#include "./metaclass/struct/values.h"` 并不参与编译（详见 `DEBUG.md` E1，该条目同时订正了根 `DEBUG.md` A2 的归因）。
