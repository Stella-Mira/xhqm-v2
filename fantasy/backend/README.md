# xhqm · fantasy/backend 渲染后端（RHI）

> 最后同步：2026-10-07，对照工作副本 r677。

`fantasy/backend` 是 xhqm 渲染层的**底层抽象**：一套双后端（OpenGL / Vulkan）的 RHI（Render Hardware Interface）虚接口，加上统一着色器源、统一 UBO 布局、着色器预编译缓存与 GLSL→SPIR-V 编译链。上层 `fantasy`（drawer / theory / 后处理 / 剔除等）只经 `render_backend` 接口工作，不感知底层是 GL 还是 Vulkan。

> 实际文件清单：18 个条目 = **16 个源文件（合计 7210 行，`wc -l`）+ `README.md` + 本轮新建的 `DEBUG.md`**。其中 `rhi_vk.cpp` 4078 行、`rhi_gl.cpp` 1151 行、`shader_sources.h` 468 行、`shader_preprocess.cpp` 299 行、`rhi_no.cpp` 217 行、`spirv.cpp` 203 行、`rhi_no.h` 190 行、`shader.cpp` 167 行、`ubo.h` 151 行、`backend.h` 120 行，其余 6 个（`shader.h` 57 / `spirv.h` 32 / `shader_preprocess.h` 30 / `rhi_vk_macos.mm` 17 / `rhi_gl.h` 15 / `rhi_vk.h` 15）合计 166 行。
> ~~17 个文件约 6609 行（`rhi_vk.cpp` 3669 行、`rhi_gl.cpp` 1151 行）~~ **已作废（2026-10-07 复核 r677）**：源文件实测合计 **7210 行**，`rhi_vk.cpp` 已是 **4078 行**（本轮唯一还在长的大文件）；只有 `rhi_gl.cpp` 的 1151 行与旧文一致。

## 模块定位

- 向上：为 `fantasy` 提供统一 GPU 操作入口（`render_backend`），供 `drawer`、`theory_pass`、`spatial/gpu_*`、`postfx` 等调用。
- 向下：分别翻译到 OpenGL 3.3+ / Vulkan（实例申请 `VK_API_VERSION_1_3`，`rhi_vk.cpp:749`；实际用到的设备特性只到 1.0 核 + `wideLines`，另有可选的 `VK_EXT_robustness2`、`VK_KHR_portability_subset`，SPIR-V 目标固定 1.0）；macOS 上经 MoltenVK（Metal 表面 + 单独 `.mm`）。无渲染设备时退化为 `none` 桩。
  - ~~Vulkan 1.0~~ **已作废（2026-10-07 复核 r677）**：`init()` 建实例时 `app.apiVersion = VK_API_VERSION_1_3`（`rhi_vk.cpp:749`），并非按 1.0 申请；但代码里没有用任何 1.3 专属特性，SPIR-V 目标也仍是 1.0（`spirv.cpp:182`），所以旧文的「只要 1.0」在能力层面成立，在申请的版本号上不成立。
- 编译期 + 运行期双重归一化：CMake 注入 `BUILD_WITH_VULKAN` / `BUILD_WITH_OPENGL`（是否注入还取决于 `XHQM_DEVICE`，见「依赖」），运行期按设备与探测结果走 `vk → gl → none` 回退链。
- **成熟度（一句话）**：中高 —— GL 后端功能面完整，VK 后端的 RHI 覆盖面已经很大（多窗口/离屏/立方体阴影/SSBO/indirect/compute/时间戳齐全），缺的是外围接线与失效策略（预编译缓存整条链未接线、VK 无 GPU/驱动级缓存打标、VK 查询池 slot 不回收、点光 cube 阴影 far 硬编码）；逐条见同目录 `DEBUG.md`。

## 关键类 / API

### 后端标识（backend.h）

| 符号 | 说明 |
|---|---|
| `backend_kind` | 枚举 `none / opengl / vulkan` |
| `compiled_backend()` | 编译期默认后端：`BUILD_WITH_VULKAN` → vulkan，否则 `BUILD_WITH_OPENGL` → opengl，再否则 none |
| `backend_compiled(kind)` | 某后端是否已编译进来 |
| `active_backend()` / `active_backend(kind)` | 运行时激活后端标识（informational） |
| `backend_name(kind)` | 返回 `"opengl"/"vulkan"/"none"`，用于二进制缓存打标 |
| `device_backend(device)` | 硬件设备 → 后端映射：`no/cpu/net` 恒 none，`gpu/auto` 取编译期默认 |
| `shader_source` | 着色器源输入（`vertex` + `fragment`） |
| `shader_binary` | 预编译产物（`format` / `blob` / `backend` / `key`），可直接加载。**只有后端名 + 内容哈希两类打标，没有 GPU vendor/renderer/驱动版本**（见 `DEBUG.md` A5） |

### 后端抽象接口（rhi_no.h）

`class render_backend` 是核心虚接口（87 个纯虚函数），按功能分组：

- **上下文**：`init` / `value_type` / `value_size` / `finish` / `clear` / `viewport` / `winding` / `read_pixels`
- **帧与表面**（Vulkan 需要，GL 为 no-op）：`create_surface` / `destroy_surface` / `begin_frame` / `end_frame`；支持多窗口（每窗口一份 swapchain 状态）
- **着色器**：`shader_create`（源码 / 二进制两种重载）/ `shader_delete` / `shader_get_binary`
- **顶点/索引**：`vertex_create` / `vertex_update` / `index_create` 等
- **纹理**：`texture_create` / `texture_create_compressed`（BC1）/ `texture_create_float`（RGBA32F）/ `texture_update` / `texture_create_cubemap` / `texture_bindless_handle`
- **渲染状态**：`blend` / `depth_test` / `color_write` / `stencil_write|test|disable` / `set_msaa`（`set_msaa` 仓内**无调用点**，见 `DEBUG.md` C3）
- **离屏（MRT）**：`create_offscreen` / `begin_offscreen` / `end_offscreen` / `offscreen_texture` / `offscreen_depth_texture` / 立方体深度与颜色离屏 / `read_offscreen`
- **uniform 与绘制**：`use_program` / `bind_texture` / `uniform_*` / `draw` / `draw_indexed` / `draw_instanced` / `draw_indexed_instanced`
- **indirect / compute / SSBO**：`indirect_create` / `draw_indexed_indirect` / `draw_indexed_indirect_storage` / `shader_compute_create` / `compute_dispatch` / `storage_*` / `compute_storage_bind`（VK 的 compute 描述符集里声明了 binding 0 的 UBO，但没有任何写入路径，见 `DEBUG.md` E4）
- **GPU 查询与计时**：`query_*`（遮挡查询样本数）/ `timestamp_*`（per-pass 纳秒时间戳）
- **统计**：`draw_call_count` / `reset_draw_call_count`

辅助类型：

- `rhi_primitive`：`points / lines / triangles / quads / triangle_fan`
- `sampler_desc`：采样器描述（min/mag 滤波、mipmap、clamp、深度比较、`max_lod`、各向异性，共 7 个字段，默认 `linear_* = true / mipmap = true / clamp = true / compare = false / max_lod = 0 / anisotropy = 1`），替代散落的硬编码采样参数

运行时调度函数：

| 函数 | 说明 |
|---|---|
| `render_backend_active()` | 单例，**首次调用**按设备与编译宏探测创建后端（`vk → gl → none`），并调用该后端的 `init()` |
| `render_backend_select(kind)` | 显式切换 `active_ptr`：**不调用 `init()`、也不释放/退役原后端**，调用方须自己 `render_backend_active().init(err)`（仓内两个调用点都补了这一句，见 `DEBUG.md` A6） |
| `render_backend_retire(self)` | 后端析构时退役：active 指针切 none，防静态资源悬空 |
| `render_backend_shutdown()` | 显式关闭后端，active 切 none（在 GL 上下文销毁前调用） |

### 着色器抽象与缓存（shader.h）

| 符号 | 说明 |
|---|---|
| `shader_hash(text)` | FNV-1a 64 稳定哈希（offset basis `1469598103934665603`、prime `1099511628211`） |
| `shader_key(source)` | 对 vertex+fragment 的稳定内容哈希生成缓存键（16 个十六进制字符） |
| `shader_factory` | 预编译抽象：`compile` / `load` / `extract` / `supports_binary` |
| `default_shader_factory` | 默认工厂，直通当前后端的 `_shader` 实现；`supports_binary()` 只认 `opengl`（`shader.cpp:85-89`） |
| `shader_cache` | 预编译缓存：`acquire`（首次编译落盘，之后直接加载）/ `save_file` / `load_file`。文件格式：magic `XHQMSHDR`（8 字节）+ version `1` + backend + key + format + blob（`shader.cpp:10-11`、`:91-134`） |

> 注意：`shader_factory` / `default_shader_factory` / `shader_cache` / `shader_key` / `shader_hash` 在本仓**没有任何调用点**——缓存链已实现但没接上（`DEBUG.md` E1）；`supports_binary()` 在 VK 后端恒为 `false`（`rhi_vk.cpp:739`），所以 VK 侧不会落盘二进制。

### 着色器预处理（shader_preprocess.h / shader_sources.h）

`shader_sources.h` 内 18 个着色器入口（`select_* / prefab_* / toon_* / sprite_* / point_line_ploy_* / curve_* / nurbs_* / occlusion_* / depth_*`，每个 `inline const char*`，其中 `toon_vert` 直接复用 `prefab_vert`、`curve_frag`/`nurbs_frag` 复用 `point_line_ploy_frag`）用 `XHQM_*` 宏写成**单一事实源**；`shader_preprocess(src, vulkan, apple_gl)` 用**字符串替换**（`replace_all`，共 47 次替换调用）把宏展开为 GLSL 330 core（GL）或 GLSL 450（Vulkan）。

- `apple_gl` 由调用方算出来：`#ifdef __APPLE__` 且非 Vulkan 后端（`fantasy/drawer.cpp:420-424`）；为 true 时只保留极简漫反射（`shader_preprocess.cpp:26-35`），见 `DEBUG.md` F4。
- 替换顺序有讲究：必须先替换 `XHQM_FLAG_BLEND_WEIGHT` 再替换其前缀 `XHQM_FLAG_BLEND`（`shader_preprocess.cpp:243`）。
- `shader_sources.h` 里用到的宏全部都有对应替换（`toon_test` 断言预处理产物无 `XHQM_` 残留）；`shader_preprocess.h:7-17` 的「宏清单」只列了 10 组，不含 `XHQM_TOON_LIGHTING`、`XHQM_IRRADIANCE`、`XHQM_NORMAL_MAP`、`XHQM_AO_MAP`、`XHQM_CUBE_SHADOW`、`XHQM_PREFILTER`、`XHQM_INSTANCE_ID` 与 15 个 `XHQM_FLAG_*`（`DEBUG.md` G3）。

GL 与 VK 的描述符/采样器映射（`shader_preprocess.cpp:271-295` 生成声明，`rhi_vk.cpp:1416-1429` 生成布局）：binding 0 = 动态 UBO，1/2/3 = 颜色纹理，**4 = `shadowMap[4]`（一个 4 元素数组，不是 binding 4..7）**，5 = `cubeShadow`，6 = `irradianceMap`，7 = `prefilteredMap`，8 = `normalMap`，9 = `aoMap`；GL 侧这些采样器是普通 `uniform`，采样单元靠 `uniform_int(program, "prefabTexture_0", i)` 下发（`fantasy/drawer.cpp:754-760`）→ `rhi_gl.cpp:619-634` 回落到 `glUniform1i`。

### UBO 单一事实源（ubo.h）

- 常量：`UBO_MAX_LIGHTS=8` / `UBO_MAX_CASCADES=4` / `UBO_MAX_FLAGS=16` / `UBO_MAX_BONES=512` / `UBO_MAX_INSTANCES=64`
- 字节偏移：`UBO_TRANSFORM`(0) … `UBO_INSTANCES`(34144)、`UBO_SIZE=38240`，均由常量派生（`UBO_INSTANCES + 64*64`）
- `flags[i]` 的字节偏移 = `UBO_FLAGS + i*16`（`ubo_flag(i)`），16 个语义下标 `FLAG_USE_LIGHTING`(0) … `FLAG_HAS_INSTANCES`(15)
- `ubo_offset(name)`：上层 uniform 名 → UBO 字节偏移（GL/Vulkan 共用同一映射），未知名返回 `-1`；`"tranMatrix"` 与 `"params"` 都指向偏移 0（别名，见 `DEBUG.md` F3）
- `ubo_glsl(vulkan)`：生成与偏移严格一致的 GLSL std140 块声明

### SPIR-V 编译链（spirv.h）

`spirv_initialize` / `spirv_finalize` / `compile_glsl_to_spv(source, stage, spv, error)`：仅 Vulkan 后端使用，依赖 glslang，自提供 `TBuiltInResource` 资源上限（`spirv.cpp:29-148` 手写 111 行赋值 = 102 个字段 + 9 个 `limits.*` 开关；EXT mesh/task 字段按 `GLSLANG_VERSION_MAJOR > 11` 条件编译，`:124-135`），以避免依赖 `glslang-default-resource-limits.lib` 的 STL 符号问题——不过 CMake 在 Linux/macOS 仍然链了这个库（`DEBUG.md` G4）。

## 架构

```
fantasy/backend
├── backend.h               后端种类 + 编译宏归一化 + shader_source/shader_binary
├── rhi_no.h                render_backend 虚接口 + rhi_primitive + sampler_desc + 调度函数
├── rhi_no.cpp              none 桩 + 后端探测/选择/退役/关闭
├── rhi_gl.h / rhi_gl.cpp   OpenGL 后端（gl_backend，GLEW，1151 行，单类）
├── rhi_vk.h / rhi_vk.cpp   Vulkan 后端（vk_backend，4078 行，单类 + 内存池/表面表/程序表）
├── rhi_vk_macos.mm         macOS NSView → CAMetalLayer（MoltenVK）
├── shader.h / shader.cpp   着色器哈希 / key / 工厂 / 二进制缓存（magic "XHQMSHDR" v1）
├── shader_preprocess.h/.cpp  XHQM_* 宏 → GLSL 330/450 预处理（字符串替换，47 处）
├── shader_sources.h        统一着色器源（18 个入口：select/prefab/toon/sprite/point_line/curve/nurbs/occlusion/depth）
├── spirv.h / spirv.cpp     GLSL → SPIR-V（glslang，自维护 TBuiltInResource）
└── ubo.h                   统一 UBO 布局（偏移 + GLSL 块，单一事实源，UBO_SIZE=38240）
```

### 运行时调度

`render_backend_active()` 首次调用时（`rhi_no.cpp:163-192`）：

1. `no/cpu/net` 设备 → `none` 桩
2. `gpu` 设备 → 单后端探测（`BACKEND=VULKAN`→vk→none，`BACKEND=OPENGL`→gl→none）；双后端编译（`AUTO`）走完整回退链
3. `auto` 设备 → 完整回退链 `vk → gl → none`

每个后端 `init()` 成功才算可用，失败释放避免泄漏（这条「失败释放」对返回函数内静态单例的工厂是**未定义行为**，见 `DEBUG.md` A1）。

### Vulkan 后端的工程化要点（rhi_vk.cpp）

- **内存池子分配**：host-visible 大块（**64 MiB**，单次需求更大时按需放大，`rhi_vk.cpp:154`）子分配，减少 `vkAllocateMemory` 次数；池内存**只增不减**（无回收路径，见 `DEBUG.md` B1）
- **环形 UBO slot**：`UBO_SLOTS = 1024`（1023 环形，slot0 保留不参与分配）；uniform 先写 CPU 侧暂存 `program_t::ubo_stage`，draw 时只把「曾经被写过」的 64 B 块（`UBO_BLK=64`、`UBO_BLKS=598`、`UBO_USED_WORDS=10` 位图）固化进本 draw 独占的 slot，避免后画覆盖先画；单帧同 program 超过 511 次 draw 会触发环形过冲，退回 `vkDeviceWaitIdle`（`rhi_vk.cpp:560-579`）
  - ~~每个 draw 固化一份 uniform 数据（slot0 为 CPU 暂存区）~~ **已作废（2026-10-07 复核 r677）**：CPU 暂存**已不在 slot0**，而是每个 program 独占的 `std::vector<char> ubo_stage`（`rhi_vk.cpp:312-317`、`:2435-2467`）；slot0 现在只是一个**保留不分配的槽**（`:2564-2566` 分配 `slot = 1 + (p.ubo_slot++ % (UBO_SLOTS - 1))`）。旧文描述的是修复前（`fantasy_bugs.md` #52）的实现。
- **描述符集轮转**：每个 program 持有固定的 `MAX_DESC_SETS = 1024` 份描述符集并**逐 draw 轮转一份**（帧起点 `desc_rot` 也推进一格），回卷判据是「距上次回卷已过去 ≥ 在飞帧数」；单帧把整环吃满才退回 `vkDeviceWaitIdle`（`rhi_vk.cpp:85`、`:525-579`、`:1315-1323`）
  - ~~每帧在飞一份独立描述符集，避免跨帧 `vkUpdateDescriptorSets` 改写未定义行为~~ **已作废（2026-10-07 复核 r677）**：现在是**每 program 1024 份、每绘制一份**（`:85` 注释「每绘制轮转一份」、`:560-579` `advance_desc_set`），不是每帧一份；「每帧在飞」只是描述符集**份数**当初的设计动机。
- **多窗口表面**：每窗口独立 surface/swapchain/附件/命令池/信号量/fence（`m_surfaces` 以 native window 指针为键，同窗口同尺寸 = 纯激活，尺寸变化 = 销毁重建）
- **管线缓存键**：顶点布局哈希 + 渲染状态位段（bit40..45/46..48）+ 拓扑（bit52+）+ `render_pass` 散列；每个 program 一份 `unordered_map<size_t, VkPipeline>`（重建离屏/立方体会产生新 render pass → 新管线，旧管线只在 `shader_delete`/析构时销毁，见 `DEBUG.md` D2）
- **平台 surface**：Windows Win32 / Linux XCB（每窗口独立 xcb 连接）/ macOS Metal（MoltenVK，`rhi_vk_macos.mm` 17 行）

## 依赖

- **OpenGL**：GLEW + 系统 `OpenGL/gl.h`（macOS）或 `GL/gl.h`（`rhi_gl.cpp:10-17`）
- **Vulkan**：Vulkan SDK（`vulkan.h`）；Linux 用 XCB，Windows 用 Win32，macOS 用 Metal（MoltenVK）
- **glslang**：GLSL→SPIR-V（`ShaderLang.h` / `ResourceLimits.h` / `GlslangToSpv.h`），兼容新版与发行版（Ubuntu 11.8）头文件布局差异（`spirv.cpp:8-19`）
- 依赖路径统一在上一级 `depend/`（glew / vulkan / glslang；头目录见 `CMakeLists.txt:84-93`）；Linux/macOS 用系统库（`find_package`，`CMakeLists.txt:699-721`：`libglew-dev libvulkan-dev glslang-dev`；macOS 额外链 `glslang-default-resource-limits SPIRV-Tools-opt SPIRV-Tools SPIRV glslang`（`:712-714`）+ Cocoa/QuartzCore 框架）；Windows 走 `depend/vulkan/glslang/lib/*.lib`（`:687-696`，**不含** `glslang-default-resource-limits`）

CMake 选项：`XHQM_RENDER_BACKEND=OPENGL|VULKAN|AUTO`（默认 AUTO，同时编译两后端运行时调度），注入 `BUILD_WITH_VULKAN` / `BUILD_WITH_OPENGL`（`CMakeLists.txt:176-201`）。**实际编译哪些后端文件还受 `XHQM_DEVICE` 控制**（`CMakeLists.txt:602-629`）：`NO/CPU/NET` 一个都不编译，`AUTO` 编译两个，`GPU` 才看 `XHQM_RENDER_BACKEND`；`rhi_no.cpp` / `shader.cpp` / `shader_preprocess.cpp` 恒在源文件表里。

## 简要用法示例

真实用法取自 `test/toon_test.cpp:36-40`（预处理）、`fantasy/postfx/tonemap.cpp:73-84`（顶点/索引/离屏）、`fantasy/element.cpp:47`（`tranMatrix`）、`test/view/RhiViewport.cpp:26-53`（后端选择与窗口）：

```cpp
#include "fantasy/backend/backend.h"            // 编译期 + 运行期后端标识
#include "fantasy/backend/rhi_no.h"             // render_backend 接口 + 调度函数
#include "fantasy/backend/shader_preprocess.h"  // 库里自己用 "shader_preprocess.h"（CMake 已加 -I./fantasy/backend/）
#include "fantasy/backend/shader_sources.h"
// 编译命令里带 -I<仓库根>（CMakeLists.txt:79 的 include_directories("./")）即可命中以上路径

using namespace xhqm::fantasy;

// 1. 拿后端（首次调用触发 vk→gl→none 探测，并对选中的后端调 init()）
render_backend& rhi = render_backend_active();
printf("active backend: %s\n", rhi.name());         // "vulkan" / "opengl" / "none"
const bool vk = (rhi.name()[0] == 'v');             // 引擎里判断后端的惯用写法（drawer.cpp:418）

// 2. 表面：GL 是 no-op；Vulkan 必须给真实原生窗口（Windows HWND / Linux X11 Window id，macOS NSView）
std::string err;
rhi.create_surface(reinterpret_cast<void*>(window_handle), 800, 600, err);

// 3. 统一着色器源 → 预处理 → 编译（Vulkan 侧在 shader_create 内部编译 GLSL→SPIR-V）
std::string vs = shader_preprocess(shader_sources::prefab_vert(), vk);
std::string fs = shader_preprocess(shader_sources::prefab_frag(), vk);
uint prog = rhi.shader_create(vs, fs, err);         // 失败返回 0，原因在 err

// 4. 顶点/索引：locations = 每个属性的分量数，stride 是它们之和（rhi_vk.cpp:1616-1626 / rhi_gl.cpp:294-304）
std::vector<value> quad   = { -1,-1, 0,0,  1,-1, 1,0,  1,1, 1,1,  -1,1, 0,1 };
std::vector<uint>  layout = { 2, 2 };
uint vb = rhi.vertex_create(quad, layout, err);
uint ib = rhi.index_create({ 0, 1, 2, 0, 2, 3 }, err);

// 5. 每帧（Vulkan 必须成对调用 begin_frame/end_frame；GL 两者是空实现）
xhqm::matrix transform(4, 4);                       // 引擎矩阵：data() 返回 value*，按列主序写入
rhi.begin_frame(err);
rhi.clear(0xff28303a);                              // rgba：GL 立即清，VK 记下来在 render pass 的 clearValue 清
rhi.use_program(prog);
rhi.uniform_matrix(prog, "tranMatrix", transform.data(), 1);   // 名字要能命中 ubo_offset()（ubo.h:71-113）；真实写法见 element.cpp:47
rhi.uniform_lights(prog, light_count, light_data);             // 每盏灯 16 个 value（light.h LIGHT_BYTES）
rhi.bind_texture(0, tex);                                      // unit 0..11 → binding 1..9（rhi_vk.cpp:2251-2264）
rhi.draw_indexed(vb, ib, rhi_primitive::triangles, 0, 6);
rhi.end_frame();

// 6. 收尾：GL 上下文销毁前退役后端（否则静态 shader 的 shader_delete 会在无上下文时崩）
render_backend_shutdown();
```

`test/toon_test.cpp` 的 6 个用例（UBO 偏移 ↔ GLSL 布局一致、GL/Vulkan/AppleGL 预处理产物无残留宏、材质默认值、`toon_prefab` 接线）是这套 API 的现成最小自检，构建目标见 `test/CMakeLists.txt:273-305`（只链 `rhi_no.cpp`/`shader.cpp`/`shader_preprocess.cpp`，跑在 `none` 后端的桩上）。

> ~~`rhi.uniform_matrix(prog, "transform", mat, 1)`~~ **已作废（2026-10-07 复核 r677）**：`ubo_offset()` 不认识 `"transform"`（只认 `"tranMatrix"` / `"params"`，`ubo.h:75`），传错名字会返回 -1，GL 侧再回落到 `glGetUniformLocation(program, "transform")`（UBO 块内成员取不到 location）→ **整条矩阵设置静默无效**；仓内真实写法是 `"tranMatrix"`（`fantasy/element.cpp:47`）。
> ~~`rhi.create_surface(nullptr, 800, 600, err)`~~ **已作废（2026-10-07 复核 r677）**：Vulkan 侧 `nullptr` 会直接 `vkCreateWin32SurfaceKHR`/`vkCreateXcbSurfaceKHR` 失败（`rhi_vk.cpp:955-1001` 的平台分支都要真实 HWND / X11 Window id，macOS 要 NSView 的 CAMetalLayer），旧文那样写只有 GL 后端能跑。

## 当前问题清单

本模块**当前仍存在**的缺陷 / 缺口 / 设计取舍，一律记在同目录 `DEBUG.md`（A–G 组，每条带 `文件:行号` 证据与复现方式），本文件不重抄。历史台账 `fantasy/fantasy_bugs.md`（静态分析 + 历轮修复记录）与 `network/nebula/DEFERRED.md`（讨论过但决定不做）按本仓约定**不搬家、不改名**；仓库根 `DEBUG.md` 与 `README.md` 里与本模块相关的条目（如 `rhi_vk.h:10` 的旧「占位」注释）只做指针，见 `DEBUG.md` G2。
