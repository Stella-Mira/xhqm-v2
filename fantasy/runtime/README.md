# xhqm/fantasy/runtime — 运行时管线模块

> 最后同步：2026-10-07，对照工作副本 r677。

## 模块定位

`runtime` 是 xhqm 引擎「幻想（fantasy）」子系统中的**运行时支撑层**，位于渲染核心（`drawer`/`system`/`backend`）与上层应用之间，负责把「静态数据」高效地变成「可渲染的运行时数据」，并提供渲染帧的组织、调度与度量能力。

它不定义着色器、不直接画图元，而是解决四个工程问题：

- **数据进出**：glTF 2.0 原生加载（`gltf_loader`）+ 引擎原生场景序列化（`scene_io`）。
- **资源管理**：名字 → 资源句柄的注册与复用（`asset_manager`）。
- **帧组织与并行**：声明式帧图（`frame_graph`）、异步加载（`async_loader`）、多线程命令录制（`command_recorder`）。
- **性能观测**：CPU 帧耗时 / draw call 统计与 GPU 时间戳计时（`performance`）。

模块内文件为**扁平结构**（无子目录），按职责划分为 8 个组件；任务描述中的 `scene_io / gltf_loader / frame_graph / performance` 即其中 4 个核心组件。

**成熟度：中。** 8 个组件全部编译进库（`CMakeLists.txt:436-443` 头文件连续 8 条 / `:467` 与 `:483-488` 源文件 6 条齐全），其中 `gltf_loader` / `scene_io` / `bc1` 有实战自检（`test/view/main.cpp`），`frame_graph` 在 `theory_frame_graph` 里生产使用；缺的是**帧图资源声明路径零测试零生产接线**、**glTF 未回填引擎已支持的 TANGENT/COLOR_0**、以及 `async_loader` / `command_recorder` / `asset_manager` / `performance_stats` / `gpu_timer` 五件**在本模块外无任何调用方**。当前问题清单见同目录 [DEBUG.md](DEBUG.md)，本节不再重抄。

## 架构

```
                ┌─────────────────────────────────────────────┐
                │              上层应用 / 系统(system)          │
                └─────────────────────────────────────────────┘
   ┌──────────────────────┐        ┌──────────────────────────┐
   │ 数据进出              │        │ 帧组织与并行              │
   │  gltf_loader (glTF2.0)│        │  frame_graph   (拓扑编排) │
   │  scene_io    (JSON)   │        │  async_loader  (后台加载) │
   │  asset_manager(注册表)│        │  command_recorder(线程池)  │
   └──────────┬───────────┘        └────────────┬─────────────┘
              │                                 │
              ▼                                 ▼
   ┌─────────────────────────────────────────────────────────┐
   │  渲染核心 drawer(prefab/material) · backend(render_backend) │
   └─────────────────────────────────────────────────────────┘
              ▲
   ┌──────────┴───────────┐
   │ 性能观测 performance   │  (帧时间 / draw call / GPU 时间戳)
   │ 纹理压缩 bc1 (DXT1)   │  (CPU 离线压缩)
   └──────────────────────┘
```

生产接线关系（实测）：`frame_graph` 由 `fantasy/theory_pass.h:50` 的 `theory_frame_graph m_graph` 持有，后者是 `property.cpp:28` 的 `theory_frame_graph` 实例（`property.h:30` 前置声明），由 `theory_pass.cpp:83` 每帧调用 `m_graph.execute()`。

## 关键类 / API

### 1. `asset_manager`（asset_manager.h）
资源注册表（M10 资源句柄系统）：按名字登记 / 查找可共享资源，供 glTF 加载与场景序列化使用。

```cpp
xhqm::fantasy::asset_manager am;
am.set_material("floor", mat);                 // 登记材质（shared_ptr 引用计数）
am.set_texture("albedo", tex);                 // 登记纹理
auto m = am.find_material("floor");            // 未命中返回默认 material()
bool has = am.has_texture("albedo");
am.clear();                                    // 清空
```

### 2. `async_loader`（async.h）
异步资源加载：后台线程跑 CPU 加载（文件 IO / 解码 / 解析），主线程每帧 `poll()` 执行 GPU 上传回调，避免阻塞渲染线程。

```cpp
xhqm::fantasy::async_loader loader;
loader.start();
loader.submit(
    [] { /* 后台：读文件、解码、构建 CPU 数据 */ },
    [] { /* 主线程：上传 GPU（VBO/纹理） */ });
// 每帧：
loader.poll();          // 执行已完成的 completion 回调
loader.pending();       // 尚未执行的 CPU 任务数
loader.stop();          // 停止并 join（等待队列清空）
```

### 3. `command_recorder`（command_recorder.h）
多线程命令缓冲录制：固定线程池，按 pass 并行执行录制任务，主线程阻塞等待全部完成。

```cpp
xhqm::fantasy::command_recorder rec;
rec.start(0);   // 0 = 取硬件并发数
rec.record({ passA_cmd, passB_cmd, passC_cmd });  // 并行录制，阻塞直到全部完成
rec.stop();
```

### 4. `frame_graph`（frame_graph.h）
声明式渲染帧图：声明 pass 的读写资源，`compile()` 用 Kahn 拓扑排序自动编排执行顺序并计算资源生命周期，`execute()` 按序执行并在首次写入分配 / 最后读取释放离屏资源。

```cpp
xhqm::fantasy::frame_graph fg;
fg.set_backend(rhi);
fg.add_resource("gbuf", w, h, 3);          // 离屏资源（颜色数 3）
fg.add_resource("light", w, h, 1);
fg.add_pass("geometry", {}, {"gbuf"}, []{ /* 写 gbuf */ });
fg.add_pass("lighting", {"gbuf"}, {"light"}, []{ /* 读 gbuf 写 light */ });
std::string err;
if (fg.compile(err)) {
    fg.execute();
    uint h = fg.resource_handle("light");   // execute 期间有效
}
fg.reset();
```

约束：每个资源只能由一个 pass 写入；读到未写入资源、未知资源、自读、成环均会 `compile()` 失败并返回原因。

**这条约束比看上去更严**：`compile()` 把「读自己写的资源」直接判为错误（`frame_graph.cpp:65`），所以「读回写同一资源」这类后处理必须拆成两个 pass。`add_resource` 对重名资源是静默忽略（`:15`），改尺寸要 `reset()` 后重新声明。

**实际生产用法与上面的示例不同**：`theory_render.cpp` 里 12 处 `add_pass` **全部**传空 reads / writes（`:130,211,264,287,379,414,443,470,503,536,567,783`），跨 pass 的纹理传递走 `theory_frame_graph` 自己的 `set_handle`/`handle` 命名表（`theory_pass.h:82-87`）。也就是说依赖推导、生命周期与资源分配这套机制在生产管线里**没有被触发**，当前等价于一个有序回调表。

### 5. `gltf::load`（gltf_loader.h）
glTF 2.0 原生加载：解析 `.glb` / `.gltf`，构建 `prefab` + `material` + 层级展平的世界变换。

```cpp
xhqm::fantasy::gltf::load_result out;
std::string err;
if (xhqm::fantasy::gltf::load("model.glb", out, err)) {
    // out.prefabs[i]   : 每个网格图元一个 prefab（调用方负责 delete）
    // out.transforms[i] : 对应世界变换（层级展平，相对文件根）
}
```

~~v1 范围：静态网格（`POSITION/NORMAL/TEXCOORD_0` + 索引）、PBR metallic-roughness 材质、节点层级变换（TRS / 四元数 / matrix）。**不含**：图像 / 纹理解码、蒙皮、动画、morph target。~~
**已作废（2026-10-07 复核 r677）**：v1 范围已确认（`gltf_loader.cpp` 全文只读 `POSITION`/`NORMAL`/`TEXCOORD_0`+索引，`:238-241`），但「不含」的措辞会误导 —— 加载器对图像 / 蒙皮 / 动画 / morph target **是「不读取、不填充」而不是「拒绝加载」**。`root["images"]` / `["skins"]` / `["animations"]` / `primitive["targets"]` 在全文 **0 命中**（`grep -n "JOINTS\|WEIGHTS\|animations\|morph\|targets\|skin" gltf_loader.cpp` 只命中 `:351` 的 `b.contains("uri")`）。

**已作废（2026-10-07 复核 r677）**：实测漏项比原文多两类。(1) **同 mesh 多 primitive 时材质被各拷一份**：`:298` 的 `p->material(make_material(...))` 对每个 primitive 都新建 `shared_ptr` 克隆同一份 `material_desc`，改其中一个不影响其余 —— 与「同索引材质是同一对象」的直觉相反。(2) **`COLOR_0` / `TANGENT` / `TEXCOORD_1` 未读**，而 `prefab` 侧 `colour()` / `tangent()` / 多套纹理位是齐的（`drawer.h:253-460` 区段），`scene_io` 也会序列化 `colours` / `tangents`（`scene_io.cpp:96-107`）。引擎有能力、glTF 入口不回填，是**当前最大的数据缺口**。

### 6. `scene_save` / `scene_load`（scene_io.h）
引擎运行时场景的原生 JSON 序列化（nlohmann）：保存 / 加载 prefab 几何 + 材质 + 变换，与 glTF 互补（原生格式覆盖 skin / blend 之外的静态几何与材质）。

```cpp
std::vector<xhqm::fantasy::prefab*> meshes;
std::vector<xhqm::fantasy::matrix> transforms;
xhqm::fantasy::scene_save("scene.json", meshes, transforms, err);
xhqm::fantasy::scene_load("scene.json", meshes, transforms, err);
```

~~与 glTF 互补（原生格式覆盖 skin / blend 之外的静态几何与材质）。~~

**已作废（2026-10-07 复核 r677）**：「覆盖 skin / blend 之外的静态几何与材质」不成立，实际覆盖面比这窄：

| 维度 | 实际行为 | 位置 |
|------|----------|------|
| `positions` | 必写；`triangles` 数量不符则整条跳过（`continue`） | `:131` |
| `normals`/`uvs`/`colours`/`tangents` | 仅当尺寸 **恰好** 等于 `n*3` 才写，否则**静默丢弃**且 `scene_save` 仍返回 `true` | `:84,90,96,102` |
| `material` | 只存 `material_desc` 的 7 个标量字段 | `:33-47` |
| 材质里的 `normal_map` / `ao_map`（`fantasy::texture`） | **不保存**，往返后必然丢失 | `drawer.h:156-157` |
| `instances`（instancing 变换表） | **不保存** | — |
| `skin_index` / `skin_weight` / `bone_palette` / `blend_*` | **不保存** | — |

其中「静默丢弃属性数组」已确认为**功能性缺陷**（写进 [DEBUG.md](DEBUG.md) A1）。材质 7 个标量字段的 `rgba ↔ uint`、`value ↔ double` 往返是无损的（`(uint)` / `(double)` 转换两侧位宽足够），不算缺陷。

### 7. `performance_stats` / `gpu_timer`（performance.h）
- `performance_stats`：帧开始 / 结束统计 draw call 数与帧耗时（毫秒）。
- `gpu_timer`：GPU 时间戳计时器，`begin()` 打起始点，`end_ms()` 打结束点并返回 GPU 耗时。

```cpp
xhqm::fantasy::performance_stats ps;
ps.begin_frame();
// ... 渲染 ...
ps.end_frame(dt_ms);
uint dc = ps.draw_calls();   // 最近一帧 draw calls

xhqm::fantasy::gpu_timer t;
if (t.begin(err)) { /* ...渲染... */ double ms = t.end_ms(); }
```

实测的两条精确语义（原文没说，容易被误用）：`performance_stats` 的 `draw_calls()` / `frame_ms()` 都只反映**最近一帧**，`begin_frame()` 的作用是调 `render_backend_active().reset_draw_call_count()`（`performance.cpp:10`）；`gpu_timer::end_ms()` **每次调用都会重新 `timestamp_record(m_end)` 并同步读回**（`:41-43`），不能当纯读取用。`end_ms()` 返回 0 有两种情况：`begin()` 未成功，或 `e < b`（`:44`）。

### 8. `bc1_encode` / `bc1_decode`（bc1.h）
BC1 (DXT1) 纹理压缩：CPU 离线压缩，4×4 块每块 8 字节（4bpp）。v1 只编码 RGB（忽略 alpha，输出始终不透明）；宽高非 4 的倍数时按边缘重复补齐。

```cpp
std::vector<std::uint8_t> packed = xhqm::fantasy::bc1_encode(rgba8, w, h);  // 长度 = ceil(w/4)*ceil(h/4)*8
std::vector<xhqm::fantasy::rgba> rgba8 = xhqm::fantasy::bc1_decode(packed, w, h);
```

尺寸与失败语义（实测）：`bc1_encode` 在 `w==0 || h==0 || flat.size() < w*h` 时返回**空 vector**（`bc1.cpp:23`）；`bc1_decode` 在 `w==0 || h==0 || bc1.size() < ceil(w/4)*ceil(h/4)*8` 时返回**空 vector**（`:106,108`）。调用方必须用空判断区分「失败」与「全黑图」。

## 子模块（按职责）

| 组件 | 头文件 | 职责 |
|------|--------|------|
| 资源注册表 | `asset_manager.h` | 名字 → material / texture 登记与复用 |
| 异步加载 | `async.h` / `async.cpp` | 后台 CPU 加载 + 主线程 poll 上传 |
| 命令录制 | `command_recorder.h` / `.cpp` | 线程池并行录制，阻塞聚合 |
| 帧图 | `frame_graph.h` / `.cpp` | 拓扑排序编排 pass + 资源生命周期 |
| glTF 加载 | `gltf_loader.h` / `.cpp` | glTF 2.0 → prefab / material / 变换 |
| 场景序列化 | `scene_io.h` / `.cpp` | prefab 几何 + 材质 + 变换 ↔ JSON |
| 性能统计 | `performance.h` / `.cpp` | 帧时间 / draw call / GPU 时间戳 |
| 纹理压缩 | `bc1.h` / `bc1.cpp` | BC1 (DXT1) CPU 编解码 |

> 构建清单里的位置（`CMakeLists.txt`）：头文件连续 8 条在 `:436-443`，源文件 6 条**被其它模块隔开** —— `performance.cpp` 在 `:467`，其余 5 个在 `:483-488`。改这里时别按「连续块」删改（见 [DEBUG.md](DEBUG.md) G4）。

实测行数（`wc -l *.h *.cpp`，模块共 1439 行）：

| 文件 | 行数 | 文件 | 行数 |
|------|------|------|------|
| `gltf_loader.cpp` | 381 | `async.cpp` | 88 |
| `scene_io.cpp` | 192 | `command_recorder.cpp` | 75 |
| `frame_graph.cpp` | 162 | `performance.cpp` | 48 |
| `bc1.cpp` | 151 | `frame_graph.h` | 67 |
| `async.h` | 53 | `performance.h` | 49 |
| `command_recorder.h` | 47 | `asset_manager.h` | 46 |
| `gltf_loader.h` | 31 | `scene_io.h` | 26 |
| `bc1.h` | 23 | | |

调用方实测（`grep` 全仓，排除本模块自身）：`frame_graph` 的资源声明 API 生产零调用；`theory_frame_graph` 被 `test/ecs_test.cpp:269` 与 `fantasy/property.cpp:28` 使用。`gltf::load`、`scene_save`/`scene_load`、`bc1_encode`/`bc1_decode` 只在 `test/view/main.cpp:329-385` 有调用。`asset_manager`、`async_loader`、`command_recorder`、`performance_stats`、`gpu_timer` **在本模块外零引用**。

## 依赖

- **引擎内部**
  - `fantasy.h`：命名空间 `xhqm::fantasy`、基础类型、`xhqm_io` 导出宏。
  - `drawer.h`：`prefab`（几何网格）、`material`（`std::shared_ptr<material_desc>`）、`texture`。
  - `rhi_no.h`：`render_backend` 抽象、`render_backend_active()`、`create_offscreen`、`timestamp_query_*`（frame_graph / performance 使用）。实际路径为 `fantasy/backend/rhi_no.h`，经 `include_directories("./fantasy/backend/")`（`CMakeLists.txt:84`）解析到裸名。
  - `colors.h`：`xhqm_rgba` 及 `xhqm_rgba_r/g/b` 分量宏（bc1 / scene_io 使用）。
  - `metaclass/resource.h`：`texture` 的实现底座（`resource<_type,_id>` 内部持 `std::shared_ptr<_type>`）。
- **第三方**
  - `nlohmann/json` 3.11.2。**两条路径并存**：`scene_io.cpp:5` 显式 `#include "../network/json.hpp"`（仓内自带 24639 行）；`gltf_loader.cpp:14` 只用 `nlohmann::json` **而不 include**，靠 `gltf_loader.h` → `fantasy.h` → `algebra/value.h` → `head.h:43-49` 的条件包含间接拿到（默认走仓内那份，定义 `XHQM_JSON_EXTERNAL` 时走 `<nlohmann/json.hpp>`，全仓无人定义该宏）。`gltf_loader.cpp` 的依赖是隐式的，见 [DEBUG.md](DEBUG.md) G2。
- **标准库**：`<thread> <mutex> <condition_variable> <atomic> <deque> <functional> <queue> <unordered_map> <fstream> <cstring> <cstdint> <vector> <string> <array> <memory>`。

## 最小可运行用法

以下三段的每一行都取自仓库内真实调用点（`test/view/main.cpp:329-385`、`test/ecs_test.cpp:269-296`），include 路径按 `CMakeLists.txt:79` 的 `include_directories("./")` 写全仓相对路径。

```cpp
#include "fantasy/runtime/gltf_loader.h"
#include "fantasy/runtime/scene_io.h"
#include "fantasy/runtime/bc1.h"
#include "fantasy/runtime/async.h"        // 异步加载器：头名 async.h（不是 async_loader.h）
#include "fantasy/colors.h"               // xhqm_rgba / xhqm_rgba_r/g/b

// 1. glTF → prefab + 世界变换；调用方负责 delete 每个 prefab
std::string err;
xhqm::fantasy::gltf::load_result gr;
if (xhqm::fantasy::gltf::load("assets/model.glb", gr, err))
{
    size_t tris = 0;
    for (auto* p : gr.prefabs) tris += (size_t)p->size();   // size() 返回三角形个数
    // gr.prefabs.size() == gr.transforms.size()（成功路径恒成立）
}

// 2. 场景 JSON 往返（gr.prefabs 与 gr.transforms 平行传入）
const std::string tmp = "scene.json";
if (xhqm::fantasy::scene_save(tmp, gr.prefabs, gr.transforms, err))
{
    std::vector<xhqm::fantasy::prefab*> back;
    std::vector<xhqm::fantasy::matrix> btrans;
    if (xhqm::fantasy::scene_load(tmp, back, btrans, err))
        for (auto* p : back) delete p;      // 加载出来的也要调用方 delete
}
for (auto* p : gr.prefabs) delete p;

// 3. BC1 往返：长度 = ceil(w/4)*ceil(h/4)*8；宽高非 4 倍数按边缘重复补齐
const xhqm::uint W = 64, H = 64;
std::vector<xhqm::rgba> src(W * H);
for (xhqm::uint y = 0; y < H; ++y)
    for (xhqm::uint x = 0; x < W; ++x)
        src[y * W + x] = xhqm_rgba((x * 4) % 256, (y * 4) % 256, ((x + y) * 2) % 256, 255);
std::vector<std::uint8_t> enc = xhqm::fantasy::bc1_encode(src, W, H);
if (enc.empty()) { /* 失败：w/h 为 0 或 flat.size() < w*h */ }
std::vector<xhqm::rgba> dec = xhqm::fantasy::bc1_decode(enc, W, H);
// 解码结果经 BC1 有损：对比时用均方误差，不要判等
```

```cpp
#include "fantasy/runtime/frame_graph.h"
#include "fantasy/backend/rhi_no.h"

xhqm::fantasy::frame_graph fg;
xhqm::fantasy::render_backend& rhi = xhqm::fantasy::render_backend_active();
fg.set_backend(rhi);
fg.add_resource("scene", 1280, 720, 1);                   // color_count = RHI 离屏颜色附件数
fg.add_resource("hdr",   1280, 720, 1);
fg.add_pass("scene_pass", {},      {"scene"}, [&] {
    xhqm::uint id = fg.resource_handle("scene");
    rhi.begin_offscreen(id);   // 切离屏；帧图自身不切，必须 pass 自己做
    /* 画场景 */
    rhi.end_offscreen();       // 回 swapchain
});
fg.add_pass("bloom", {"scene"}, {"hdr"}, [&] { /* 读 scene 写 hdr */ });

std::string err;
if (fg.compile(err))          // 失败时 err 给出原因（未知资源 / 多写者 / 自读 / 成环）
{
    for (bool frame = true; frame; frame = next_frame())   // next_frame() = 宿主提供的取帧/退出判据
    {
        fg.execute();         // 按拓扑序跑 callback，首次触碰建离屏、最后触碰释放
        break;                // 占位：真实宿主在此推进帧
    }
}
// fg.reset();                // 只在「要改声明」时调：释放全部离屏并清空资源 + pass + 顺序
                              // （析构函数也会调它；不要在每帧循环里调，否则下一帧图是空的）
```

> 说明：示例只展示 API 调用方式与真实参数含义，不构造完整渲染上下文。`frame_graph` 的 callback 是 `std::function<void()>`，**不会**把资源句柄传进来 —— 在 callback 内用 `fg.resource_handle(name)` 取（`frame_graph.h:58`，`execute` 期间有效）。
>
> **接进生产前先读 [DEBUG.md](DEBUG.md) D2**：当前 `execute()` 是「每帧建、每帧删」全部离屏（`frame_graph.cpp:124,135`），而生产侧 `theory_pass.cpp:83` 每帧都调 `execute()`。照上面示例接线会让整条后处理链的 render target 每帧重建 —— 这也正是现有 12 个 pass 全部传空 reads/writes 的原因。
