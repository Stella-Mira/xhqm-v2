# hardware —— 硬件设备选择与并行调度原语

> 最后同步：2026-10-07，对照工作副本 r677。

## 模块定位

`hardware` 是 xhqm 引擎的**通用硬件抽象层**，向上层各模块（`fantasy` 渲染、`algebra` 张量、未来的分布式等）提供统一的「设备类别 + 设备选择」能力，并附带一套 header-only 的并行调度原语。

模块只做三件事：

1. **设备选择（`hardware.h`，126 行）**：定义 `no / cpu / gpu / net / auto_` 五类设备，把「编译期编译进哪些设备」与「运行期激活哪个设备」统一成一套 `constexpr` 接口，供渲染后端、张量后端共享同一份设备语义。
2. **并行原语（`parallel/`）**：提供 `array / share / task / execute / dispatch` 等模板骨架，描述「数据分块 + 任务执行 + 调度」的最小抽象。目前只有串行实现：`equip` 参数是纯标签、不产生任何后端分派，`dispatch_cpu.h` 里 0 行代码。
3. **音视频设备抽象（`media.h`，260 行）**：`kind / dir / media_type / caps / device / hotplug / device_list`，只定义签名不做实现（见文末「音视频设备抽象」）。

成熟度：**中低** —— `hardware.h` 已被两处真实接入（`algebra/tensor/backend/backend.h:25-33` 把设备接口整体别名过去、`fantasy/backend/rhi_no.cpp:167` 按激活设备选后端）；`parallel/` 只有串行骨架（cpu/gpu/net 后端不存在）；`media.h` 只有抽象，库内零实现。

> 注意：真正的线程池、进程、异步结果（`parallel::threads`、`parallel::process`、`parallel::holder`、`parallel::dqueue` 等）定义在 `software/system/threads` 与 `software/system/process` 中，它们与 `hardware/parallel` 同属 `xhqm::parallel` 命名空间，但两边**互不引用**（`threads_pool.h:7-10`、`threads_task.h:130`、`process/process_base.h:12`）。`hardware` 提供的是更底层的设备/调度骨架；要线程池请直接用 `software/system/threads`。

## 目录结构

```
hardware/
├── hardware.h              # 设备选择（核心，header-only，126 行）
├── media.h                 # 音视频设备抽象（接口 + json 往返，260 行）
└── parallel/
    ├── dispatch_no.h       # 并行调度原语（串行实现，模板定义主体，164 行）
    └── dispatch_cpu.h      # CPU 并行版本（91 行里 0 行代码：整段实现被注释，占位）
```

行数为 `wc -l` 口径（`dispatch_no.h` / `dispatch_cpu.h` 末行无换行，编辑器会各多显示 1 行）。模块内 4 个 `.h`，**无 .cpp / .cu / .mm**。

## 关键类 / API

### `xhqm::hardware`（`hardware.h`）

```cpp
namespace xhqm::hardware
{
    // 通用设备类别
    enum class device : std::uint32_t
    {
        no = 0,    // 无（串行 / 桩兜底）
        cpu = 1,   // CPU（hardware.h:9 注释写「自动降级 blas → simd → thread」，
                   //        但该降级链由 algebra/tensor 实现，本层只标设备，见 DEBUG.md G4）
        gpu = 2,   // GPU（cuda；无 CUDA 则桩回退 cpu）
        net = 3,   // 分布式网络（占位，不参与 auto 匹配，需显式指定）
        auto_ = 4, // 自动匹配（运行时 gpu → cpu → no）
    };

    // 编译期默认设备（由 XHQM_DEVICE_* 宏决定）
    constexpr device compiled_device(void) noexcept;

    // auto 解析：gpu → cpu → no（net 不参与自动匹配）
    constexpr device resolve_auto(void) noexcept;

    // 某设备是否已编译进来
    constexpr bool device_compiled(const device& kind) noexcept;

    // 运行时激活设备（函数式 static，全局单例）
    inline device& active_device_ref(void) noexcept;
    inline device  active_device(void) noexcept;                 // 读
    inline void    active_device(const device& kind) noexcept;   // 写

    // 设备名（用于日志 / 缓存打标）
    inline const char* device_name(const device& kind) noexcept;
}
```

要点：

- **编译宏**由 CMake 选项 `XHQM_DEVICE`（`NO / CPU / GPU / NET / AUTO`，缓存默认值 `CPU`，`CMakeLists.txt:99-100`）注入为 `XHQM_DEVICE_NO / XHQM_DEVICE_CPU / XHQM_DEVICE_GPU / XHQM_DEVICE_NET / XHQM_DEVICE_AUTO`（`CMakeLists.txt:101-116`）。
- **`AUTO` 归一化**：`XHQM_DEVICE_AUTO` 会同时打开 `XHQM_DEVICE_CPU` 与 `XHQM_DEVICE_GPU`（`hardware.h:19-26`），实现「全编译、运行时回退」——`NET` 不被 AUTO 顺带打开。
- **编译期优先级**：`compiled_device()` 按 `AUTO > GPU > NET > CPU > no` 判定（`hardware.h:45-58`）——同时定义了 `NET` 与 `CPU` 时返回的是 `net`。
- **`resolve_auto()` 不探测硬件**：它只看宏，按 `GPU > CPU > no` 返回（`hardware.h:61-70`）；真正的运行期探测在消费方（张量后端 / 渲染后端）。
- `device::no` 始终视为已编译（兜底），`device::auto_` 恒为 true（`hardware.h:73-99`）。
- `active_device` 是函数内 `static` 变量，默认值取 `compiled_device()`（`hardware.h:102-108`）。懒初始化线程安全，但**读写这个变量本身没有同步**（见 `DEBUG.md` B2）。

### `xhqm::parallel`（`parallel/dispatch_no.h`）

```cpp
namespace xhqm::parallel
{
    enum equipment { no = 0, cpu, gpu, net };  // 并行设备：不并行 / cpu / gpu / 分布式  // :18-21
    enum state     { init = 0, exec, over };   // 并行状态：初始化 / 运行 / 结束（当前无人使用）// :23-26

    // 基类骨架（数据成员）
    struct _dispatch { std::thread m_thread; std::atomic<bool> m_wait = true; };  // :28-32
    struct _execute  { xhqm::id m_id = 0; };                                     // :34-37
    struct _share    { };                                                        // :39-42 空结构，无人继承
    struct _array    { xhqm::index m_index = 0; };                               // :44-47
    struct _task     { xhqm::id m_id = 0; xhqm::index m_index = 0; };            // :49-53

    // 独占数据：继承 std::vector<type> + 游标 m_index
    template<typename type, equipment equip = equipment::no>
    struct array : public _array, public std::vector<type>
    { /* 4 个构造 + over()：m_index 是否到末尾；拷贝/移动已删除 */ };              // :74-89

    // 共享数据（share，与 array 结构逐行相同；「共享」语义未实现）
    template<typename type, equipment equip = equipment::no>
    struct share : public _array, public std::vector<type> { /* ... */ };        // :57-72

    // 任务基类：纯虚 call，对 array 元素逐一处理（有虚析构，可经 task* 释放）
    template<typename type, equipment equip = equipment::no>
    struct task : public _task
    { virtual ~task() = default; virtual void call(array<type, equip>&) = 0; };  // :91-96

    // 执行器：把 task 顺序套用到 array 的每个元素（游标自增）
    template<typename type, equipment equip = equipment::no>
    struct execute : public _execute
    { template<typename task_type> void exec(task_type, array<type, equip>&); }; // :98-112

    // 调度器：阻塞/异步运行一个 task；异步时开 std::thread，析构 join
    template<typename type, equipment equip = equipment::no>
    class dispatch : public _dispatch
    {
        template<typename task_type> void compute_task(const task_type&, array<type, equip>&, bool wait = true);
        void compute(std::function<void(type&)>, array<type, equip>&, bool wait = true);
        void wite();  // 等待（join）——注意拼写，没有 wait() 别名
    };                                                                           // :125-160

    // std::function 适配任务（写死 no 设备，所以 equip≠no 时 compute() 编译失败）
    template<typename type>
    struct task_function : public task<type, equipment::no>
    { std::function<void(type&)> m_function; /* call 时对元素执行 */ };          // :114-123
}
```

要点：

- `equipment` 与 `hardware::device` 语义对应（`no/cpu/gpu/net`），但独立声明、按数值映射，两者之间没有转换函数（`dispatch_no.h:18-21` ↔ `hardware.h:35-42`）。
- `array` / `share` 是「`std::vector` + 游标」的组合，拷贝/移动（含赋值）都被删除，数据独占——实测 `a = b`、`share<int> x = y` 均编不过。
- `equip` 是**纯标签**：`array`/`task`/`execute`/`dispatch` 只有一份通用定义、没有 `equipment::cpu/gpu/net` 特化，`array<double, equipment::gpu>` 的行为与 `array<double>` 逐字相同。`compute(std::function)` 这个便利重载把任务类型写死为 `task_function<type>`（= `equipment::no`），对 `equip != no` 的 `dispatch` 调用它**直接编译失败**。
- `dispatch` 采用「先 join 上一任务，再决定同步/异步执行」的模型：`wait=true` 同步执行，`wait=false` 起线程异步执行（每次都新建 `std::thread`，无线程池），析构时自动 `join`；异步期间 `array` 必须活着（`dispatch` 只持有 `std::ref`）。
- `dispatch_cpu.h` 里被注释掉的整段内容，只是把 `equipment::cpu` 写成模板默认参数的一份**串行骨架副本**（`exec` 仍是 `while(!values.over())` 循环，不是线程级分发），而且全仓库无人 include 这个头——它现在 0 行代码、从不参与编译。CPU 并行分发实际未实现。

上述每一条的现状、证据与影响见 `DEBUG.md`（A/B/E 组）。

## 架构

```
                 ┌─────────────────────────────────────┐
                 │            XHQM_DEVICE 宏           │
                 │  (NO/CPU/GPU/NET/AUTO，CMake 注入)  │
                 └──────────────────┬──────────────────┘
                                    │ 归一化
        ┌───────────────────────────┴───────────────────────────┐
        │              xhqm::hardware（设备选择层）              │
        │  compiled_device / resolve_auto / device_compiled      │
        │  active_device（运行期） / device_name（打标）         │
        └───────────────────┬───────────────────────┬───────────┘
                            │                       │
             ┌──────────────▼──────────┐  ┌─────────▼──────────────┐
             │  fantasy 渲染后端        │  │  algebra 张量后端      │
             │  fantasy::device_backend │  │  tensor_backend 复用   │
             │  （backend.h:85）        │  │  compiled_backend 等   │
             │  rhi_no 按 active_device │  │  别名到 hardware       │
             │  选 no/cpu/net vs gpu    │  │  （backend.h:25-33）   │
             └─────────────────────────┘  └────────────────────────┘

                 ┌─────────────────────────────────────┐
                 │        xhqm::parallel（并行层）      │
                 │  equipment / state / _dispatch       │
                 │  array·share / task / execute        │
                 │  dispatch / task_function            │
                 └─────────────────────────────────────┘
```

设计思路：**编译期定能力、运行期选设备、检测不到就回退**。上层（渲染/张量）不直接关心后端细节，只读 `hardware` 的激活设备再映射到具体后端。`media.h` 不在这张图里——它不消费设备选择，只定义音视频设备的接口与 json 结构。

## 依赖

- `head.h`：提供 `xhqm::id / index / count / size / text` 等基础类型（`dispatch_no.h:3` 以 `../../head.h` 引入，`media.h:21` 同）：`xhqm::index = std::size_t`、`xhqm::count = std::size_t`、`xhqm::size = std::size_t`、`xhqm::id = std::size_t`（`head.h:217-220`）、`xhqm::text = std::string`（`head.h:234`）。
- `dispatch_no.h` 直接包含（`:5-11`）：`<atomic> <queue> <mutex> <thread> <optional> <functional> <shared_mutex>`。注意 `<vector>` / `<utility>` **没有**直接包含，靠 `head.h:36` 传递进来（`<queue>/<optional>/<shared_mutex>` 本文件并未使用）。
- `dispatch_cpu.h` 直接包含（`:3-12`）：`<vector> <queue> <memory> <thread> <mutex> <condition_variable> <future> <functional> <stdexcept>`——该文件整段被注释，这些包含目前没有实际作用。
- `media.h` 直接包含（`:21-24`）：`../head.h`、`../algebra/itype.h`（`xhqm::u32`，独立编译必需）、`<functional>`；json 用 `using json = nlohmann::json`（`media.h:33`），来源是 `head.h:42-49` 的 `network/json.hpp`（或外部 nlohmann）。
- 编译期宏：`XHQM_DEVICE_NO / CPU / GPU / NET / AUTO`（由根 `CMakeLists.txt:99-116` 的 `XHQM_DEVICE` 选项注入）。
- 除标准库外无第三方依赖；`media.h` 不含任何平台头（平台实现归实现层）。

## 简要用法示例

```cpp
#include "hardware/hardware.h"
#include <cstdio>

using namespace xhqm;

int main()
{
    // 编译期能力查询
    hardware::device dev = hardware::compiled_device();   // 默认设备（由 XHQM_DEVICE 决定）
    std::printf("compiled: %s\n", hardware::device_name(dev));

    // 是否编译了 gpu
    bool has_gpu = hardware::device_compiled(hardware::device::gpu);
    std::printf("has_gpu: %d\n", has_gpu ? 1 : 0);

    // auto 解析（编译期按宏回退 gpu → cpu → no；不做硬件探测）
    hardware::device resolved = hardware::resolve_auto();
    std::printf("resolved: %s\n", hardware::device_name(resolved));

    // 运行时切换激活设备
    hardware::active_device(hardware::device::cpu);
    std::printf("active: %s\n", hardware::device_name(hardware::active_device()));
    hardware::active_device(hardware::compiled_device()); // 恢复默认

    return 0;
}
```

并行原语（串行实现，当前 `dispatch_no.h`；与回归夹具 `test/polydtor_test.cpp:98-104` 同款）：

```cpp
#include "hardware/parallel/dispatch_no.h"

using namespace xhqm;

int main()
{
    // 独占数据块（std::vector + 游标 m_index）
    xhqm::parallel::array<int> values(4);              // 4 个元素，游标从 0 起
    for (xhqm::index i = 0; i < 4; ++i) values[i] = (int)i * 10;

    // 调度器：对每个元素加 1（同步执行），跑完和为 64
    xhqm::parallel::dispatch<int> dispatcher;
    dispatcher.compute([](int& v) { v += 1; }, values, /*wait*/ true);
    // dispatcher.compute(..., values, /*wait*/ false); // 异步：每次新建线程，析构/wite() 时 join，
    //                                                  // 且 values 必须活得比 dispatcher 久

    return 0;
}
```

音视频抽象（示例取自 `test/nebula_api/nebula_api_test.cpp:271-326` 与 `test/nebula_api/client/device_impl.h:40-73`）：

```cpp
#include "hardware/media.h"

using namespace xhqm;

// 实现层才写设备本体：抽象层的 device 全是纯虚
struct probe_device : hardware::media::device
{
    hardware::media::caps capability() const override { return {}; }
    bool open(const hardware::media::json&) override { return true; }   // config 省略即默认格式
    void close() override {}
    bool good() const override { return true; }
    xhqm::text stream_url() const override { return "rtsp://test:8554/cam"; }
};

int main()
{
    hardware::media::caps c;
    c.id = "cam.3090.0";        // 全局名 kind.host.index（ASCII slug，可读名放 c.name）
    c.host = "3090";
    c.k = hardware::media::kind::cam;
    c.d = hardware::media::dir::input;
    c.medias = { hardware::media::media_type::video };
    c.videos = { hardware::media::video_format{} };   // 默认 1920x1080 @30 h264

    hardware::media::json j = c.to_json();            // 发布进 nebula 目录的元数据
    hardware::media::caps c2;
    c2.from_json(j);                                  // c2.k == kind::cam、c2.is_video() == true

    probe_device pd;
    return pd.good() ? 0 : 1;
}
```

## 说明与验证

- `hardware.h` 是稳定的 header-only 接口，`test/hardware_test.cpp`（26 行）覆盖设备名、`device_compiled`、编译期/运行期一致性与切换；CMake 目标 `hardware_test`（`test/CMakeLists.txt:186`）。
- `parallel/dispatch_no.h` 的回归夹具是 `test/polydtor_test.cpp`（129 行）里的 `TEST_CASE("test_parallel_dispatch_no")`（`:90-129`）：`task` 虚析构断言、`array(4)` + 同步 `compute`（和为 64）、`share(3)` 实例化、经 `task*` 多态释放；CMake 目标 `polydtor_test`（`test/CMakeLists.txt:156`）。该头单独编译也过（`g++ -std=c++17 -fsyntax-only -Wall -Wextra`，0 诊断）。
- `media.h` 的行为覆盖在 `test/nebula_api/nebula_api_test.cpp`：caps json 往返与接口可继承性 `:271-326`，热插拔端到端 `:576-639`。**该测试没有 CMake 目标**（`test/CMakeLists.txt` 里搜不到 `nebula_api`），需自行编译。
- `hardware.h` / `media.h` 都在核心构建里（`CMakeLists.txt:211-212` 的 `XHQM_HEAD_CORE`），随主库一起被编译；`parallel/` 两个头**不在任何 CMake 列表里**，只在 `test/polydtor_test.cpp` 的编译单元参与编译。
- ~~`parallel/dispatch_no.h` 存在若干历史遗留的未完工/笔误（不影响 `hardware.h`）：`share` 模板的构造函数名仍写为 `array(...)`（复制粘贴残留）；`task::call` 声明带尾部逗号 `(array<type, equip>&, )`，直接使用会编译失败；`array` / `share` 的删除构造/移动声明有 `delete;;` 双分号笔误。~~ **已作废（2026-10-07 复核 r677）**：前两条笔误由 r553 修掉（`dispatch_no.h:60-63` 构造函数名已是 `share`，`:95` 已无尾逗号，并有夹具防止回归），`delete;;` 只剩 `array` 移动构造一处（`:83`，类作用域的空声明，无编译/行为影响，`share` 是干净的）；原说法把两处混为一谈。
- `parallel/dispatch_cpu.h` 整体被注释（91 行里 0 行代码）、且无人 include，CPU 并行分发实际未实现；真正的线程池/进程在 `software/system/threads` 与 `software/system/process`（见「模块定位」的注意）。
- **当前仍存在的问题清单（带 `文件:行号` 证据）见同目录 `DEBUG.md`**，本 README 不重复。

---

## 音视频设备抽象（media.h，M3）

`hardware/media.h` 定义**音视频设备抽象层**（D13 落地：只定义，不做真实现）：

```cpp
namespace xhqm::hardware::media
{
    using json = nlohmann::json;   // 本层 json 别名（media.h:33）

    enum class kind : xhqm::u32;     // none=0/mic=1/cam=2/spk=3/scr=4/mix=5/compose=6/vloop=7
    enum class dir : xhqm::u32;      // none=0/input=1/output=2/duplex=3
    enum class media_type : xhqm::u32;  // none=0/audio=1/video=2
    // kind_text() / dir_text()：写 json 用的短名（"cam" / "input" …）

    struct audio_format;     // rate=16000 / channels=1 / codec="pcm_s16le"（json 往返）
    struct video_format;     // width=1920 / height=1080 / fps=30 / codec="h264"（json 往返）
    struct caps;             // 设备能力：id/host/name/kind/dir/medias/audios/videos（json 往返）

    class device;            // 接口：capability()/open(config)/close()/good()/stream_url()/status()
    using device_list = std::function<std::vector<caps>()>;   // 枚举签名（实现层提供）
    class hotplug;           // 热插拔：watch(on_add, on_remove)/stop()（实现层提供）
}
```

- **设备名约定**：`mic.<host>.*` / `cam.<host>.*` / `spk.<host>.*` / `scr.<host>.*` /
  虚拟设备 `v.mix.<name>` / `v.compose.<name>` / `v.vloop.<name>`（`media.h:17-18`；`scr` 是 r638
  从 `screen` 改成的四字名，`kind::scr` 同时覆盖「采集」与「显示」两种方向）。
- **`caps.name` 是可读名**（r665 新增）：`id` 一律 ASCII slug（全局名 + 主机 + 序号，如
  `cam.3090.0`），给人看的名字放 `name`，随设备目录公告一起传播。
- **`device::status()`** 返回运行时状态快照（约定字段 `open` / `url` / `good` / `caps`，
  实现层可追加），供 status 指令与状态查询用；默认返回空 object。
- **不定义字节搬运**：read/write 与 MediaMTX 桥接在实现层；平台实现（ALSA/WASAPI/
  AVFoundation、V4L2/DShow）后续逐平台落地，低功耗设备可只实现子集。
- **流地址归属**：`device::stream_url()` 返回实现层起流（内嵌 MediaMTX）后的 rtsp 地址
  （未起流/不支持流式返回空串），经 nebula streams 通道发布——xhqm 不搬音视频字节（D14/D15）。
- **参考实现**（不在本模块，属 `test/`）：设备骨架 `test/nebula_api/client/device_impl.h`、
  MediaMTX 起流 `test/nebula_api/client/mtx_server.h`、热插拔 `test/nebula_api/client/linux_hotplug.h`
  （Linux inotify；Windows/macOS 尚无实现，见 `DEBUG.md` C2）。
- **json 往返的两处不对称**（`medias` 里的 `media_type::none`、`kind`/`dir` 的 `"none"`）见 `DEBUG.md` A3/A4。

