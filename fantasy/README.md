# xhqm · fantasy 渲染引擎

> 最后同步：2026-10-07，对照工作副本 r677。

fantasy 是 xhqm 的实时渲染层：一套**双后端（OpenGL / Vulkan）RHI 抽象**，叠加场景图、绘制、光照、阴影、动画、后处理、空间剔除、GPU 驱动管线、拾取与工程化模块。本文档面向使用者与后续维护者，说明模块定位、架构、关键类/API、子模块、依赖与最小可用代码。

> 本文只讲**是什么、怎么用**；**当前仍存在**的问题清单在 [`DEBUG.md`](DEBUG.md)（含与 `fantasy_bugs.md` 的编号对照），本文不复述。
> 规模：本模块 97 个源文件（`.h/.hpp/.cpp/.mm`，不含 `.svn`）共 18258 行；最大的四个是 `backend/rhi_vk.cpp` 4078 行、`backend/rhi_gl.cpp` 1151 行、`drawer.cpp` 1004 行、`theory_render.cpp` 835 行。

## 模块定位

- **一句话**：xhqm 引擎中负责「把场景图 → GPU 命令 → 像素」的实时渲染子系统，屏蔽 GL/Vulkan 差异，提供可插拔的帧图管线。
- **边界**：只做渲染与渲染资源管理；数学（matrix/vect/poit）、设备探测、元编程容器、UUID/JSON 等由兄弟模块（`algebra`/`hardware`/`metaclass`/`network`/`lattice`/`software`）提供。
- **坐标系**：z-up，列向量约定（M·v），引擎矩阵行主序存储、上传 GLSL std140 前 `tramat()`。
- **演进**：默认管线已从「单体 `theory_camera_render` 手写串联」重构为「可插拔 `theory_pass` 编排」（见下文）。

## 架构总览

分层（自下而上）：

```
┌─────────────────────────────────────────────────────────────┐
│ 9 工程化  runtime/  frame_graph · async · command_recorder    │
│                    performance · gltf_loader · scene_io · bc1│
├─────────────────────────────────────────────────────────────┤
│ 8 管线编排 theory_pass / theory_frame_graph / theory_render   │
│                    （shadow→[instancing]→scene→[depth]→[ssao]→…→tonemap）│
├─────────────────────────────────────────────────────────────┤
│ 7 动画     animation/  skeleton · clip · ik · gpu_skinning    │
├─────────────────────────────────────────────────────────────┤
│ 6 后处理   postfx/  bloom · fxaa · tonemap · ssao · taa · ssr │
├─────────────────────────────────────────────────────────────┤
│ 5 光照     lighting/  light · shadow_map · csm · point_shadow │
├─────────────────────────────────────────────────────────────┤
│ 4 空间     spatial/  bvh · culler · occlusion · gpu_culler    │
│                    gpu_hiz · gpu_hiz_culler · lod · queue     │
│            注：spatial/ 与 lighting/ 下的剔除器/LOD/排序/CSM/  │
│            点光阴影都是「已实现、未接入默认管线」的可选组件    │
├─────────────────────────────────────────────────────────────┤
│ 3 场景图    object/system/models/camera/scenes │
│ 2 绘制      drawer/prefab/sprite · element · colors           │
│ 1 资源      vertex/shader/texture/material (resource<> 句柄)   │
├─────────────────────────────────────────────────────────────┤
│ 0 RHI      render_backend (rhi_no.h) + rhi_gl / rhi_vk / none │
│            ubo.h · shader_sources.h · shader_preprocess · spirv│
└─────────────────────────────────────────────────────────────┘
```

**每帧数据流**：`object::alive(world->theory())` → 各 `theory_pass::life()` 标记本帧 use → `theory_frame_graph::life()`（殿后）做 diff/compile/execute → 各 pass 按拓扑序执行（**离屏资源由各 pass 自管**；`frame_graph::add_resource` 那条声明式资源生命周期路径在生产管线里没有调用者，见 `DEBUG.md` F2）→ 后处理链「读 `"scene"` → 写回 `"scene"`」式传递 → `tonemap` present 到 swapchain。

**GPU 驱动路径**（能力完备，**未接进默认管线**）：`gpu_culler`（compute 视锥剔除）/ `gpu_hiz` + `gpu_hiz_culler`（Hi-Z 遮挡剔除）/ `gpu_skinner`（compute 蒙皮）把结果写入 SSBO，再经 `draw_indexed_indirect_storage` 零 CPU 读回消费 —— 这条链目前只有 `test/view/main.cpp` 的自检块在跑，`theory_scene` 仍是逐 drawer 的 `draw_indexed`（见 `DEBUG.md` E3/D2）。

## 关键类 / API

| 层 | 类/接口 | 说明 |
|---|---|---|
| 场景图 | `object`（blurry.h） | 一切场景节点基类；唯一 `object_id()`，纯虚 `life()/emit()`，`derive_tree` 父子树 |
| | `system` | 携带坐标 `coord()`；静态 `transform()/traverse()` 做世界矩阵传播 |
| | `models` | 挂 `drawer` 的节点：`insert_drawer/remove_drawer/handle_drawers` |
| | `camera` | 相机（默认透视）：视图内聚 `view()/window()`、`add_ui()`、orbit 单一状态、键盘移动输入 |
| | `scenes`/`world` | 聚合 `theory/target/camera` 三子图；`add_theory/add_target/add_camera` |
| 绘制 | `drawer` | 可绘制体基类：顶点缓冲、光源、材质、阴影态 |
| | `prefab` | 三角网格：顶点/法线/UV/颜色、LBS 蒙皮、blendshape、切线、instancing、3 层纹理 |
| | `toon_prefab` | 卡通渲染网格（prefab 着色变体）：ramp 分档光照 + 硬边高光 + 边缘光，几何/蒙皮/形变/实例化与 prefab 完全一致 |
| | `sprite` | 四边形贴图 |
| | `poits/lines/ploys/curve/nurbs/arrow` | 点/线/多边形/NURBS 等图元（element.h） |
| 资源 | `vertex`/`shader`/`texture` | `resource<>` 包装的移动句柄（`_vertex/_shader/_texture`） |
| | `material`/`material_desc` | PBR 材质（base_color/metallic/roughness/ao/emissive/normal_map/ao_map）+ 卡通参数（ramp_threshold/ramp_softness/rim_power/spec_threshold/rim_color） |
| RHI | `backend_kind` | `none/opengl/vulkan` |
| | `render_backend`（rhi_no.h） | RHI 虚接口：shader/vertex/index/texture/离屏/计算/SSBO/查询/时间戳 |
| | `render_backend_active()/select()/shutdown()` | 运行时后端调度 |
| | `ubo.h` | 统一 UBO std140 布局（GL/VK 单一事实源），`ubo_glsl()` 生成着色器块 |
| | `shader_sources.h` + `shader_preprocess` | 统一着色器源 + `XHQM_*` 宏展开为 GLSL 330/450 |
| | `spirv` | GLSL → SPIR-V（glslang，仅 Vulkan） |
| | `shader_factory`/`shader_cache` | 预编译二进制缓存 |
| 空间 | `spatial_bvh`/`scene_culler` | BVH + 视锥剔除 |
| | `occlusion_culler` | GPU 遮挡查询（跨帧复用） |
| | `gpu_culler` | compute 视锥剔除，写 indirect 命令 |
| | `gpu_hiz`/`gpu_hiz_culler` | Hi-Z 金字塔 + 两阶段遮挡剔除 |
| | `lod_group` | 距离 LOD + cross-fade |
| | `render_queue` | 不透明近→远 / 透明远→近排序 |
| 光照 | `light_type`/`light_desc` | 方向/点/聚光描述 |
| | `light` | 场景级光照环境：光源列表 + IBL 环境光 |
| | `shadow_map`/`cascaded_shadow_map`/`point_shadow_map` | 方向光阴影 / CSM / 点光 cube 阴影 |
| 后处理 | `bloom_effect`/`fxaa_effect`/`tonemap_effect`/`ssao_effect`/`taa_effect`/`ssr_effect`/`compose_effect` | 各自 `setup(rhi,w,h)` + `apply(...)` |
| 动画 | `skeleton`/`animation_clip`/`animation_blender`/`quat`/`ik_solver`/`gpu_skinner` | 骨骼树 / 关键帧采样混合 / CCD IK / GPU 蒙皮 |
| 管线 | `theory_pass`/`theory_frame_graph` | 可编排 pass 基类 + 编排器 |
| | `theory_shadow/scene/depth/ssao/ssr/bloom/fxaa/taa/tonemap/ibl/gizmo_sync` | 具体 pass |
| | `setup_pipeline_for()`/`setup_default_pipeline()` | 便捷组装 |
| 工程化 | `frame_graph`/`async_loader`/`command_recorder`/`performance_stats`/`gpu_timer`/`gltf::load`/`scene_save|load`/`bc1_encode|decode`/`asset_manager` | 见 runtime 子模块 |
| 颜色 | `color`（colors.h） | RGBA ↔ HLSA/HVSA/CMYK 转换；`xhqm_rgba()` 宏 |

## 目录结构与子模块

```
fantasy/
├── fantasy.h          核心类型别名（matrix/vector/point/value/rgba/resource…）
├── colors.h/.cpp      rgba 颜色体系与转换
├── blurry.h/.cpp      object 基类（场景图节点，唯一 object_id）
├── system.h/.cpp      system / models / scenes（world）
├── camera.h/.cpp      camera 相机（默认透视、视图内聚、add_ui、orbit、键盘移动）
├── drawer.h/.cpp      drawer 基类 + prefab / sprite（可绘制体）
├── element.h/.cpp     poits / lines / ploys / curve / nurbs / arrow
├── property.h/.cpp    light 模型类 + coord + theory_camera_render(遗留) / theory_system_pose
├── theory_pass.h/.cpp 可插拔 pass 基类 + theory_frame_graph 编排器
├── theory_render.h/.cpp 具体 pass（shadow/scene/depth/ssao/ssr/bloom/fxaa/taa/tonemap/ibl/gizmo_sync）
├── fantasy_bugs.md    历史台账（编号 1–53，含已修项与 P0/P1 排障全过程；保留原名原位置）
├── DEBUG.md           当前仍存在的问题（A–G 分组，每条带 文件:行号；含与 fantasy_bugs.md 的编号对照）
├── backend/           RHI 抽象与双后端
│   ├── backend.h      后端种类 + 编译宏归一化 + 预编译 shader 数据
│   ├── rhi_no.h/.cpp  render_backend 虚接口 + 运行时调度（active/select/shutdown）
│   ├── rhi_gl.h/.cpp  OpenGL 后端
│   ├── rhi_vk.h/.cpp  Vulkan 后端（rhi_vk.cpp 4078 行，完整实现；注意 rhi_vk.h:10 的注释仍自称「占位实现」，是过时注释）
│   ├── rhi_vk_macos.mm 17 行：macOS 平台辅助，把 Qt 的 NSView 指针取/建 CAMetalLayer 供 MoltenVK 用（非 Vulkan 后端本体；仅 Apple 平台由 CMakeLists.txt:709-711 收录）
│   ├── ubo.h          统一 UBO 布局（151 行；字节偏移 + GLSL 块，GL/Vulkan 共用单一事实源）
│   ├── shader_sources.h 统一着色器源（GL/Vulkan 单一事实源）
│   ├── shader_preprocess.* 着色器宏预处理（299 行；出 GLSL 330 core / 450）
│   ├── spirv.*        GLSL → SPIR-V 编译链（glslang）
│   └── shader.*       shader 工厂 / 二进制缓存
├── spatial/           空间分区与剔除
│   ├── spatial        BVH（射线拾取 + 视锥）
│   ├── culler         视锥剔除（BVH）
│   ├── occlusion      GPU 遮挡查询（包围盒 + 查询，跨帧复用）
│   ├── gpu_culler     GPU compute 视锥剔除（SSBO 写 indirect 命令）
│   ├── gpu_hiz        Hi-Z 深度金字塔（compute 下采样 max）
│   ├── gpu_hiz_culler GPU 两阶段遮挡剔除（视锥 + Hi-Z）
│   ├── lod            距离分级 + cross-fade
│   └── render_queue   不透明/透明排序
├── lighting/          光照
│   ├── light.h        光源描述（方向/点/聚光）+ 场景级光照环境（IBL）
│   ├── shadow         方向光阴影映射
│   ├── csm            级联阴影
│   └── point_shadow   点光 cube 阴影（六面深度）
├── postfx/            后处理
│   ├── bloom          Bloom（亮度提取→高斯模糊→合成）
│   ├── fxaa           FXAA 抗锯齿
│   ├── tonemap        HDR 色调映射（Reinhard）+ gamma
│   ├── ssao           屏幕空间环境光遮蔽
│   ├── taa            时间抗锯齿（Halton jitter + 邻域钳制）
│   ├── ssr            屏幕空间反射
│   └── compose        通用合成（mode 0=multiply / 1=add，供 AO/SSR 用）
├── animation/         动画
│   ├── skeleton       骨骼层级树 + 蒙皮调色板
│   ├── animation      clip 采样 / 四元数 slerp 混合
│   ├── ik             CCD 逆向运动学
│   └── gpu_skinning   compute GPU 蒙皮（调色板写 SSBO）
└── runtime/           工程化
    ├── frame_graph    声明式 pass 编排（拓扑排序 + 资源生命周期）
    ├── async          异步资源加载（后台 IO + 主线程 GPU 上传）
    ├── command_recorder 多线程录制
    ├── performance    性能统计 + GPU 时间戳计时
    ├── gltf_loader    glTF 2.0 原生加载（.glb/.gltf + PBR 材质）
    ├── bc1            BC1 (DXT1) 纹理压缩（CPU 离线）
    ├── scene_io       场景 JSON 序列化
    └── asset_manager  资源注册表（名字 → material/texture）
```

## 依赖

**外部（编译/链接）**：GLEW（OpenGL 加载）、Vulkan SDK（`vulkan-1.lib` + 头文件）、glslang + SPIRV-Tools（GLSL→SPIR-V，仅 Vulkan）、nlohmann/json（scene_io；引擎内副本 `network/json.hpp`）、assimp（demo 模型加载，非引擎核心）；可选 BLAS（`depend/openblas`，张量后端 CPU 扩展，非渲染必需）。第三方统一放在上一级 `depend/`（CMake 中 `../depend/...` 引用）。

**内部（xhqm 兄弟模块）**：`algebra`（matrix/vect/poit/matx）、`hardware`（设备探测）、`metaclass`（resource/buffer/template）、`network`（uuid/json）、`lattice`（tree）、`software`（pattern/instance）。

**构建选项**：
- `XHQM_RENDER_BACKEND`：`OPENGL` / `VULKAN` / `AUTO`（默认 AUTO，同时编译两后端，运行时调度）。
- `XHQM_DYNAMIC`：定义则编成动态库（DLL + 导入库），否则静态库。
- `BUILD_WITH_OPENGL` / `BUILD_WITH_VULKAN`：由 CMake 按 BACKEND 注入的编译宏。

## 快速开始

### 1. 构建引擎库

```bat
cmake -G Ninja -DXHQM_BUILD_FANTASY=ON -DXHQM_DEVICE=GPU -DXHQM_RENDER_BACKEND=AUTO ^
      -DXHQM_BUILD_SYSTEM=ON -DXHQM_BUILD_NETWORK=ON -DXHQM_DYNAMIC=1 -DCMAKE_BUILD_TYPE=Release ^
      -S D:\WorkSpace\xhqm_v2\xhqm -B D:\WorkSpace\xhqm_v2\xhqm\build
ninja -C D:\WorkSpace\xhqm_v2\xhqm\build
```

产物输出到 `D:\WorkSpace\xhqm_v2\binary\xhqm.lib`（+ `xhqm.dll`）。demo 与引擎须同为 Debug 或同为 Release（MSVC 调试/发布 STL 容器大小不同，跨 DLL 传容器会读错尺寸）。

**三个容易踩的构建开关**（`CMakeLists.txt`）：

- `XHQM_BUILD_FANTASY` **默认 OFF**（`:43`）—— 不开这个，`fantasy/` 的头一个都不进 `xhqm` 库，且 `include_directories("./fantasy/%") × 6` 全部失效（`:82-91`）。它同时依赖 `XHQM_BUILD_NETWORK`（uuid）与 `XHQM_BUILD_SYSTEM`（pattern/threads），`:46-47` 会为此发 WARNING。
- `XHQM_DEVICE` **默认 `CPU`**（`:99`），而 `rhi_gl.cpp`/`rhi_vk.cpp`/`spirv.cpp` 只在 `DEVICE=AUTO` 或 `DEVICE=GPU` 时才进构建（`:606-629`）—— 想跑渲染必须显式给 `GPU` 或 `AUTO`。`DEVICE=NO/CPU/NET` 时后端只剩 `none` 桩（`backend/backend.h:8-16` 的映射表）。
- `XHQM_RENDER_BACKEND`（`:183`，取值 `AUTO`/`VULKAN`/`OPENGL`，**默认 `AUTO`**）：只在 `DEVICE=GPU` 时生效并据此注入 `-DBUILD_WITH_VULKAN` / `-DBUILD_WITH_OPENGL`（`:190-201`）；`DEVICE=AUTO` 时两后端都编（`:610-613`），`DEVICE=NO/CPU/NET` 时一个都不编。

### 2. 搭建最小场景

```cpp
#include <xhqm/fantasy>
using namespace xhqm::fantasy;

// 世界（world = scenes）
world* w = new world();
w->camera()->resize(800, 600, 100);          // 透视投影（默认 fov 45°）

// 一个三角形模型
prefab* tri = new prefab();
tri->size(1);                                 // 1 个三角形
tri->vertex({{ {point(0,0,0), point(1,0,0), point(0,1,0)} }});
tri->normal({{ {0,0,1},{0,0,1},{0,0,1} }});
tri->open_face(true);
tri->color_face(xhqm_rgba(255, 160, 40, 255));

models* m = new models();
m->insert_drawer(tri);                        // insert 时触发 updata()（建顶点缓冲）
w->add_target(m);                             // 加入场景
```

### 3. 渲染循环

```cpp
// 每帧一次（内部走 clear -> traverse -> models::drawer -> drawer::render）
object::alive(w->theory());
```

注意：这条「最小场景」路径**用它自己的 theory 子树**（`scenes` 构造时就建好三棵子图）；一旦改用下面的 `setup_default_pipeline`，绘制就交给那些 pass，不要再混用两条路径。

**可插拔管线（推荐）**：

```cpp
#include "theory_render.h"    // 或 "../fantasy/theory_render.h"；CMake 已把 fantasy/ 放进包含路径
using namespace xhqm::fantasy;

world* w = new world();
w->camera()->resize(800, 600, 100);
// ... 搭好模型后：w->add_target(m); ...

setup_default_pipeline(w, PIPE_SSAO | PIPE_SSR | PIPE_BLOOM | PIPE_FXAA | PIPE_TAA);
// 之后每帧 object::alive(w->theory()) 驱动整条链
```

### 4. 一个自足的「地面 + 立方体」例子（照 `test/view/main.cpp` 的真实用法拼）

下面每一条调用都在仓库里能找到出处（`make_box` 见 `test/view/main.cpp:88`）。窗口/交换链由宿主负责：GL 后端 `create_surface()` 直接返回 true（不需要窗口句柄），Vulkan 后端要传平台原生窗口（Win32 `HWND` / XCB `xcb_window_t` / macOS `NSView*`），然后每帧 `begin_frame(err) … end_frame()`。

```cpp
#include "../fantasy/fantasy.h"          // 核心类型与门面（也可用无扩展名的 ./xhqm/fantasy）
#include "../fantasy/theory_render.h"    // setup_default_pipeline / PIPE_* / 各 pass
#include "../fantasy/camera.h"           // camera
#include "../fantasy/backend/rhi_no.h"   // render_backend_active()
#include <cmath>
using namespace xhqm::fantasy;

// 两块三角形拼一个朝 +z 的四边形（法线 +z；z 是「上」，见「约定与注意事项」）
prefab* make_quad(value half, value z, rgba color)
{
    prefab* p = new prefab();
    p->size(2);                                               // 2 个三角形
    p->vertex({
        { { point(-half, -half, z), point(half, -half, z), point(half, half, z) } },
        { { point(-half, -half, z), point(half, half, z), point(-half, half, z) } },
    });
    p->normal({                                               // 每顶点一条 → 尺寸 = 三角形数 × 3
        {0,0,1}, {0,0,1}, {0,0,1},
        {0,0,1}, {0,0,1}, {0,0,1},
    });
    p->open_face(true); p->open_line(false); p->open_poit(false);
    p->open_light(true);                                      // 参与光照（不吃光照的面看不到方向光阴影）
    p->color_face(color);
    return p;
}

int main()
{
    // ① 后端：AUTO 编译时按 vk → gl → none 回退；显式指定用 render_backend_select(backend_kind::vulkan)
    render_backend& rhi = render_backend_active();
    std::string err;
    if (!rhi.create_surface(native_window, 1280, 720, err)) { /* 打印 err 并退出 */ }

    // ② 世界：scene 的 theory/target/camera 三棵子图在构造时就建好了
    world* w = new world();
    w->camera()->resize(1280, 720, 100);     // (宽, 高, 深度)；默认透视，fov 45°(= xhqm::radians(45))、near 0.01、far 10000
    w->camera()->orbit(true);                // 开启 orbit；life() 每帧把 focus/yaw/pitch/dist 应用到 coord
    w->camera()->dolly(25); w->camera()->focus(point(0, 0, 0));

    // ③ 场景：models 节点挂 drawer；insert_drawer 会触发 updata() 建顶点缓冲
    models* ground = new models();
    ground->insert_drawer(make_quad(30.0f, -95.0f, xhqm_rgba(120, 170, 95, 255)));
    w->add_target(ground);

    models* box_node = new models();
    box_node->insert_drawer(make_box(3.0f, 3.0f, 3.0f, xhqm_rgba(145, 145, 155, 255)));  // make_box 见 test/view/main.cpp:88
    box_node->system::coord(xhqm::translate(0.0f, 0.0f, -94.5f));   // 注意：模型要放 z 为负值（见下）
    w->add_target(box_node);

    // ④ 光照环境（可选）：不建的话 theory_shadow 会退回一个默认方向光 (0,-0.6,-0.8)
    //    light 是纯数据层（不是 object，缺省构造就可用），给 drawer 用 drawer::light_env 挂上（优先于 drawer::lights）
    light env;                                   // 默认 ambient = 0x505050、reflection = 1
    light_desc sun;
    sun.type = light_type::directional;
    sun.color = xhqm_rgba(255, 255, 255, 255);
    sun.direction = point(0.0f, -0.6f, -0.8f);   // 光线行进方向（光 → 场景）
    env.add_light(sun);
    for (auto* d : box_node->handle_drawers()) d->light_env(&env);   // 每个参与的 drawer 各自挂（drawer 优先读 light_env）
    // 注意：setup_default_pipeline 内部 new 的 theory_scene/theory_shadow 无法从外部取用
    //       （theory_frame_graph 只暴露 passes()，拿不到具体 pass*），所以「场景级」光照环境只能逐 drawer 挂。

    // ⑤ 管线：一条 setup_default_pipeline 挂到 world 的 theory 子树
    setup_default_pipeline(w, PIPE_INSTANCING | PIPE_SSAO | PIPE_BLOOM);

    // ⑥ 每帧
    while (true)
    {
        rhi.begin_frame(err);                       // GL 后端是空操作；Vulkan 在这里 acquire/begin render pass
        rhi.clear(xhqm_rgba(20, 24, 32, 255));      // 背景色（BGR(A) 字节序，见下）
        object::alive(w->theory());                 // 驱动 theory 子树：各 pass life() → fg 编译并执行
        rhi.end_frame();                            // GL 空操作；Vulkan 在这里 submit + present
        rhi.finish();                               // 演示用；实时循环一般不要每帧等空闲
    }
}
```

**几个真实存在的细节**（照抄自代码，不是约定俗成）：

- `prefab::vertex` 的长度是**三角形数**（`value.size() == m_size`），而 `normal/colour/flipuv/values/skin_*/tangent` 的长度是**三角形数 × 3**（每顶点一条）—— 尺寸不对时 setter 返回 `false` 且**不写入**（`drawer.h:359-399`），静默失败。
- `open_poit/open_line/open_face` 默认都是 `true`；只要面不想要点和线框，就得像上面那样显式关掉。
- `camera::fov()` 的入参是**弧度**（`m_fov = xhqm::radians(value(45))`，`camera.h:23`），`near/far/fov` 的 setter 都会内部重做一次 `resize()`（沿用当前 `m_view_w/h/d`）。
- `camera::resize(w, h, d)` 的第三个参数是**深度尺度**（正交可见范围 `±d`、透视用于 `world_per_pixel`），不是别的尺寸。
- 多相机/多窗口：`w->cameras()` 拿到附加相机子树，`w->add_camera(cam)` 挂上去；每窗口一条 `setup_pipeline_for(cam, flags)` 的管线，然后**手动** `fg->run()` 驱动（不挂 world 树）。


## 核心概念

### 场景图：object → system/models/camera

- `object`（blurry.h）是所有场景节点的基类，构造时分配唯一 `object_id()`，用 `derive_tree` 构成父子树。
- `system` 携带坐标变换 `coord()`；`models` 是「挂 drawer 的节点」；`camera` 提供 `proj()/view()` 与拾取。
- `world = scenes` 聚合 theory / target / camera 三个子图。

```cpp
m->system::coord(xhqm::translate(-60,0,40) * xhqm::scale(70));  // 设置模型变换
w->add_target(m);
```

### 绘制：drawer → prefab/sprite

`drawer` 是「可绘制体」基类，关键流程：

- 填数据：`size(n)` 声明三角形数，再 `vertex/normal/flipuv/colour` 传每顶点数据（各 *3 展平）。
- 开通道：`open_poit/line/face(bool)`。
- 蒙皮：`open_skin(true)` + `skin_index/skin_weight/bone_palette`（LBS）。
- 形变：`open_blend(true)` + `blend_delta/blend_weight`（blendshape，含多目标 `blend_targets`）。
- 实例化：`instances(vector<matrix>)`。
- 静态批处理：`prefab::batch_static(vector<prefab*>)` 合并同布局网格，draw call 由 N 降为 1。

`prefab` 是最常用的三角网格；`sprite` 是四边形贴图；`element.h` 里是点/线/多边形等。

### RHI：render_backend

> ~~Vulkan 后端标注占位~~ **已作废（2026-10-07 复核 r677）**：`backend/rhi_vk.h:10` 的 `// 工厂：创建 Vulkan 后端（当前为占位实现）` 是一行**过时注释**，与实现不符 —— `backend/rhi_vk.cpp` 是 **4078 行**的完整实现（`vkCreateInstance`/`vkCreateDevice`/`vkCreateSwapchainKHR`/`vkCreateGraphicsPipelines`/`vkCreateDescriptorPool`/`vkCmdDrawIndexed`/`vkQueueSubmit` 等），`create_vk_backend()`（`rhi_vk.cpp:4072-4076`）返回真实的静态后端单例。这句注释只在 `rhi_vk.h` 里活着，代码侧不成立（同一事实的根级登记见 `DEBUG.md` G1）。
>
运行时通过 `render_backend_active()` 拿到当前后端；`render_backend_select(backend_kind::opengl)` 可切换（AUTO 模式）。所有 GPU 操作（shader/顶点/索引/纹理/绘制/状态/离屏/查询/计算/SSBO）都经此接口，上层不用关心 GL 还是 Vulkan。Vulkan 后端额外提供 `create_surface/destroy_surface`（多窗口 swapchain）与 `texture_bindless_handle`（bindless 句柄，GL 返回 ARB_bindless 64 位句柄，Vulkan 返回纹理 id 代理）。

### 光照

`light_desc` 定义光源（方向/点/聚光），`light` 收拢「光源列表 + IBL 环境光」作为跨帧持久的数据层，被 `theory_scene/theory_shadow/theory_ibl` 消费：

```cpp
light env;
light_desc sun;
sun.type = light_type::directional;
sun.color = 0xffffffff;
sun.direction = point(0, 0, -1);      // 光线行进方向（光→场景）
env.add_light(sun);
```

支持：多光源（最多 `UBO_MAX_LIGHTS=8`；超出被着色器循环的 `i < XHQM_MAX_LIGHTS` 静默截断）、方向光阴影（`shadow_map`）、级联阴影（`cascaded_shadow_map`，`setup` 拒绝 `count==0` 或 `count>4`）、点光 cube 阴影（`point_shadow_map`）、IBL 环境光（irradiance/prefiltered cubemap）、PBR metallic-roughness 材质（`drawer::material`）。旧用法 `drawer->lights(...)` 仍保留，`drawer->light_env(&env)` 优先。

**接线现状**：默认管线只挂 `theory_shadow`（用的就是单级 `shadow_map`，`theory_render.cpp:811`），并把 `cascade_count` 写死为 1（`theory_render.cpp:79`）；`cascaded_shadow_map` 与 `point_shadow_map` 是**已实现但未接入**的可选组件（原因与代价见 `DEBUG.md` F3）。`light` 是纯数据层（不是 `object`）；`light::ambient()` 默认 `0x505050`、`reflection()` 默认 `1.0`。

### 动画

- `skeleton` 描述骨骼层级；`animation_clip` 做关键帧 + 四元数 slerp 采样；`animation_blender` 做过渡混合（保留缩放分量）；`ik_solver` 做 CCD。
- `gpu_skinner` 把调色板计算搬到 compute 着色器（`gpu_skinning.h`），结果写 SSBO，避免逐帧 CPU 累乘。**未接进 `drawer` 的渲染路径**（`drawer.cpp` 里没有 `gpu_skinning` 的任何引用），要自己接；当前只有 `test/view/main.cpp:492` 的自检在用。
- 最简用法：每帧更新 `bone_palette` 即可驱动 LBS 蒙皮。

### 拾取

```cpp
object* hit = camera->slected(x, y);   // 左下角坐标
```

内部是离屏 id 渲染 + 读回像素反查 `object_id`，不污染主画面。

### 离屏渲染 / 后处理

`create_offscreen` / `begin_offscreen` / `end_offscreen` / `offscreen_texture` 组成通用离屏（支持 MRT、HDR、深度专用、cubemap 颜色/深度）；`bloom_effect`、`fxaa_effect`、`tonemap_effect`、`ssao_effect`、`taa_effect`、`ssr_effect`、`compose_effect` 是对其封装，`apply(scene_tex)` 返回结果纹理；HDR 离屏（`create_offscreen(..., hdr=true)`）支持浮点色彩。

### 工程化

- `frame_graph`：声明资源 + pass，自动拓扑排序并管理资源生命周期（`add_resource` 这条路径在 `fantasy/` 内**无调用者**，默认管线的 pass 都自管离屏，见 `DEBUG.md` F2/B3）。
- `async_loader`：CPU 加载放后台线程，`poll()` 在主线程执行 GPU 上传回调。`stop()` 会 **join 后台线程（先跑完已入队的 task）**，然后丢弃未 `poll()` 的 completion（`runtime/async.cpp:42-54`；其 `m_running`/`m_stop` 是裸 `bool`，见 `DEBUG.md` B1）。
- `command_recorder`：固定线程池并行录制。
- `performance_stats`：draw call / 帧时间统计 + `gpu_timer` 逐 pass GPU 计时。
- `gltf_loader`：glTF 2.0 原生加载（v1 范围：静态网格 + PBR 材质 + 节点层级）。**不含**纹理/图像/采样器、蒙皮、动画、morph（源码里连 `"textures"`/`"skins"`/`"animations"` 的字样都没有），也**不读** `mesh.primitives[].mode`（非 TRIANGLES 会被按三角形读，见 `DEBUG.md` A4）；稀疏/常量 accessor 按全零处理（`gltf_loader.cpp:107-111`）。`scene_io` 场景 JSON 序列化；`bc1` BC1 纹理压缩（`bc1_encode`/`bc1_decode`，v1 只编码 RGB、解码时 alpha 恒为 255，`runtime/bc1.h:6`）；`asset_manager` 资源注册表。
- GPU 驱动：`gpu_culler`（compute 视锥剔除）、`gpu_hiz` + `gpu_hiz_culler`（Hi-Z 遮挡剔除）把剔除结果写 SSBO，经 `draw_indexed_indirect_storage` 零 CPU 读回消费 —— **未接进默认管线**（见 `DEBUG.md` E3）。

## theory_pass 可插拔渲染管线

默认渲染管线已从「单一体 `theory_camera_render` 手写串联」重构为「可插拔 `theory_pass` 编排」。每个渲染功能（阴影 / 主渲染 / 深度 / 后处理）是一个独立 theory，挂到 world 的 theory 子树，由 `theory_frame_graph` 统一注册、diff 增删、重编译、执行。

### 核心类（theory_pass.h）

- `theory_pass`：可编排 pass 的抽象基类（继承 `theory`）。子类只需实现两个纯虚：
  - `declare(frame_graph&)`：向 frame_graph 声明 pass（add_pass + 塞 callback）；
  - `render()`：渲染代码（作为 pass callback，每帧 execute 时调用）。
  基类统一骨架：`used()` 停用不参与；首次注册进 fg（触发 dirty）；每帧标 use。
- `theory_frame_graph`：管线编排器。持有 `frame_graph` + theory_pass 注册表 + 跨 pass 命名纹理句柄表（`set_handle/handle`）。`life()` 每帧：diff 出 use=false 的 pass（对应已移除/停用的 theory）→ 若 dirty 或有 unused 则「reset + 重新 declare + compile」（同一帧完成，无空帧）→ `execute()` 跑所有 callback → 全部 use 清零。

### 具体 pass（theory_render.h/.cpp）

| pass | 作用 | 句柄约定 |
|---|---|---|
| `theory_shadow` | 方向光阴影（前置，单级 shadow_map） | — |
| `theory_scene` | 主渲染到 HDR 离屏 | 写 `"scene"` |
| `theory_instancing` | ECS 批量提交（树遍历收集 `model_ecs` 节点→`sync_instances()`），须在 scene 前执行；未挂时 `model_ecs::render` 自同步兜底 | — |
| `theory_depth` | 线性视图深度到浮点离屏（`clipPos.w`） | 写 `"depth"` |
| `theory_ssao` | 屏幕空间 AO（读 depth，经 compose 乘 scene） | 写 `"ao"` |
| `theory_ssr` | 屏幕空间反射（读 scene+depth，经 compose 加 scene —— **同时改写 `"scene"`**） | 写 `"scene"`（加反射）与 `"ssr"` |
| `theory_bloom` | Bloom（读 scene 写回 scene） | — |
| `theory_fxaa` | FXAA（读 scene 写回 scene） | — |
| `theory_taa` | TAA（读 scene + 历史写回 scene） | — |
| `theory_tonemap` | Reinhard tonemap + present（读 scene） | — |
| `theory_ibl` | IBL：程序化/equirect 环境贴图 → **CPU** 辐照度卷积 + CPU 单粗糙度（0.5）预过滤 | 写 `irradiance`/`prefiltered`（`brdf_lut` 见 `DEBUG.md` E2：声明了但从不创建） |
| `theory_gizmo_sync` | 每帧同步 gizmo 姿态为相机逆旋转 | — |

后处理链以「读 `"scene"` → 写回 `"scene"`」方式链式传递（注意 `theory_ssao`/`theory_ssr` 都会覆盖 `"scene"`）；SSAO/SSR 经 `postfx/compose.h`（`mode 0`=multiply、`mode 1`=add）合成进场景。`theory_ibl` 与 `theory_gizmo_sync` **不在** `setup_pipeline_for` 的组装序里，要手工 `new` + `fg->register_pass`。

### 组装

```cpp
setup_default_pipeline(world*, uint flags = 0)
```

组装序（`theory_render.cpp:800-833`）：shadow → [instancing] → scene → [depth] → [ssao] → [ssr] → [bloom] → [fxaa] → [taa] → tonemap → fg（殿后）。flags 位（`theory_render.h:248-253`）：`PIPE_BLOOM = 1<<0`、`PIPE_FXAA = 1<<1`、`PIPE_SSAO = 1<<2`、`PIPE_SSR = 1<<3`、`PIPE_TAA = 1<<4`、`PIPE_INSTANCING = 1<<5`（ECS 批量提交）；含 SSAO/SSR 时自动插入 `theory_depth` 提供线性视图深度。多相机/多窗口用 `setup_pipeline_for(cam, flags)` 建独立管线，直接 `fg->run()` 手动驱动。之后每帧 `object::alive(w->theory())` 驱动整条链。

返回值与所有权：两个 setup 都返回 `theory_frame_graph*`（`new` 出来的，无 RAII 包装）—— `setup_pipeline_for` 返回的 fg **与它的 passes 由调用方负责释放**（`theory_render.h:259`），`setup_default_pipeline` 则把 passes 与 fg 都挂进 `world` 的 theory 子树（`w->add_theory(...)`），随 world 析构。两者都不要重复释放。

### 每帧时序

```
各 theory_pass::life()          → 未注册则 register_pass（置 dirty）+ mark_use
theory_frame_graph::life()(殿后) → 有 dirty/unused：reset + 重新 declare + compile（同帧完成）
                                 → execute（按挂载序跑所有 render）
                                 → 全部 use 清零（下一帧重新标记）
```

theory 动态增删（`add_theory/sub_theory` 或 `used(false)`）在下一帧被 fg diff 掉并重编译，无需手动维护 pass 列表。

### 兼容

`theory_camera_render`（property.h）**不是死代码，但也不是推荐入口**：它内部懒建一条固定的 `shadow + scene + tonemap` 三项管线（`property.cpp:26-37`）并手动驱动，尺寸与后处理都不可配。
- ~~`theory_camera_render`（property.h）是旧相机时代的遗留封装（r449 删除旧 camera 后已废弃，无实际调用点）。~~ **已作废（2026-10-07 复核 r677）**：`theory_camera_render::life()` 会真建并驱动一条内部管线（`property.cpp:21-38`），不是空壳；只是 `test/view` 已改走 `theory_frame_graph`，所以只剩它自己的路径在用。新代码统一用 `setup_pipeline_for / setup_default_pipeline`（见上）。

## 数据规则（ECS 化，设计文档）

现状：场景对象是「树 + 虚函数」，海量实体时虚调用 + 内存分散。计划引入数据驱动：**数据与逻辑分离**，组件连续存储（SoA），系统批量遍历。

### 核心架构

**不分离，统一在树上**：一切皆节点，object 树保持不变。ECS 是树上的一类特殊节点 `model_ecs`，负责草地/粒子/实例等**海量同质数据**的批量渲染；普通节点照旧虚函数（虚函数设计好、效率一般；ECS 效率高、但只用于大数据渲染）。两类节点各自有 theory：`theory_scene` 走树遍历 + 虚函数，`theory_instancing` 读 SoA 批量提交。

```
object（树节点基类，不变）
 ├─ models（普通节点：drawer + 虚函数 life，照旧）
 └─ model_ecs（新：内部持 tuple SoA，批量渲染）
        └─ 草地/粒子/实例：position/color/... 存 SoA 列
```

- **数据归属**：`model_ecs.attach(tuple)` / `detach(tuple)` —— 一类 tuple 只管自己的数据、不在意自己类型；model_ecs 知道自己的数据该给谁管理（不是 `tuple.attach(model_ecs)`）。
- **组件身份用类型**（point/color），**存储 SoA 或 AoS 按访问模式选**：读密集走 SoA（标量列），写密集/随机走 AoS（单类型列）。

### 组件存储：tuple_values

```cpp
// 主模板：多列 SoA
template<class... Columns>
class tuple_values
{
    std::tuple<std::vector<Columns>...> values;
public:
    // 读（值返回，聚合还原语义类型；ind=npos 聚合，否则取单列）
    template<class type, xhqm::index ind = xhqm::npos>
    type value(xhqm::index pos) const
    {
        if constexpr (ind == xhqm::npos)
            return make<type>(pos, std::index_sequence_for<Columns...>{});
        else
            return std::get<ind>(values).at(pos);
    }
    // 读（out 参数，零构造）
    bool get_value(xhqm::index pos, Columns&... args)
    {
        if (pos >= size()) return false;
        get_impl(pos, std::index_sequence_for<Columns...>{}, args...);
        return true;
    }
    // 写（整体写每列，const 引用无拷贝）
    bool set_value(xhqm::index pos, const Columns&... args)
    {
        if (pos >= size()) return false;
        set_impl(pos, std::index_sequence_for<Columns...>{}, args...);
        return true;
    }
    xhqm::size size() const { return std::get<0>(values).size(); }
private:
    template<class type, std::size_t... I>
    type make(xhqm::index pos, std::index_sequence<I...>) const
    { return type{ std::get<I>(values).at(pos)... }; }
    template<std::size_t... I>
    void get_impl(xhqm::index pos, std::index_sequence<I...>, Columns&... args)
    { (args = std::get<I>(values).at(pos), ...); }
    template<std::size_t... I>
    void set_impl(xhqm::index pos, std::index_sequence<I...>, const Columns&... args)
    { (std::get<I>(values).at(pos) = args, ...); }
};

// 偏特化：单列（退化，不包 tuple，可引用读写）
template<class Column>
class tuple_values<Column>
{
    std::vector<Column> values;
public:
    Column&       value_ref(xhqm::index pos)       { return values.at(pos); }
    const Column& value_ref(xhqm::index pos) const { return values.at(pos); }
    Column        value(xhqm::index pos) const     { return values.at(pos); }
};
```

### 四接口语义

| 接口 | 方向 | 适用 | 构造开销 |
|---|---|---|---|
| `value<type,ind>(pos)` | 读（值） | 聚合/取列，通用 | 有（临时对象） |
| `get_value(pos, Columns&...)` | 读（out 参数） | 批量热路径 | 零 |
| `value_ref(pos)` | 读/写（引用） | 单列直接存储 | 零 |
| `set_value(pos, const Columns&...)` | 写 | 多列整体写 | — |

### 返回类型规则

- 引用返回（`value_ref`）只在「直接存储」（单列 AoS）成立；聚合（SoA 多列）只能值返回（临时对象）。
- `type& value_ref(pos)` 与 `const type& value_ref(pos) const` 是合法重载（靠成员 const 性区分）。
- `const type& f() const` 与 `type f() const` 不能共存（签名相同，仅返回类型不同）。
- `get_value/set_value` 的参数类型锁死为 `Columns`（非模板方法），类型安全、无需 `static_assert` 兜底。

### 三类兼容

| 写法 | 底层存储 | 读 | 写 |
|---|---|---|---|
| `tuple_values<f,f,f>` | 3 列 SoA | `value<point>(i)` 聚合 | `set_value(i, x,y,z)` |
| `tuple_values<point>` | 单列 AoS | `value(i)` | `value_ref(i) = ...` |
| `tuple_point`（继承其一） | 同上 | 同上 | 同上 |

### 状态

落地完成（对象树 + 虚函数仍是普通节点主路径；ECS 节点走 model_ecs/theory_instancing 批量路径）。落地顺序：`tuple_values`（主模板 + 偏特化 + 四接口）→ `model_ecs.attach/detach` 闭环 → 单组件最小例子（批量变换 benchmark 对比树遍历）→ 多组件 join → `theory_instancing` 接入 `frame_graph`。

- **已实现**：`tuple_values`（`metaclass/tuple_values.h`，主模板多列 SoA + 单列偏特化 AoS + 四接口，附 push/erase 交换删除等实体管理与数据版本计数）；多组件 join（`xhqm::join_size` 联合容量 + `xhqm::join_each` 两/三组件联合遍历，实体下标对齐、out 参数零构造读取、写回经 `set_value`；`model_ecs::join_each<TupleA,TupleB[,TupleC]>(func)` 按存储类型取回并联合遍历，组件身份用类型——position 用 point、velocity 用 vector 等语义类型区分）；`model_ecs`（`fantasy/model_ecs.h/.cpp`，树上 ECS 节点，`attach/detach` 语义仿 drawer 管理，`create_tuple<Columns...>()` 新建并挂载，`sync_instances()` 从 position 组件（SoA 三列 / AoS point 单列）生成平移矩阵写入 prefab 实例列表走 M3 instancing 路径，数据版本守卫去重同帧重复同步，`render()` 先同步再渲染）；`theory_instancing`（`fantasy/theory_render.h/.cpp`，ECS 批量提交 pass：树遍历收集 model_ecs 节点读 SoA 批量提交，经 `PIPE_INSTANCING` 标志接入 `setup_pipeline_for`/`setup_default_pipeline`，在 scene 前执行；管线未挂本 pass 时 `model_ecs::render` 自同步兜底）。
- **已验证**：`test/ecs_test.cpp`（Catch2，9 用例；断言数见 `DEBUG.md` G7 —— 源码级 53 条 `CHECK`，不是 115）——四接口读写、三类兼容、attach/detach 所有权闭环、sync_instances 两类存储、join_size/join_each（含缺组件短路、异构 AoS/SoA 混组）、theory_instancing 帧图驱动（含数据版本守卫去重与变更重同步）、10 万实体批量变换 benchmark（SoA 批量路径较树遍历显著更快）。

## toon_prefab 卡通渲染

`toon_prefab`（drawer.h）是 prefab 的着色变体：几何/蒙皮/形变/实例化路径完全复用（`toon_vert()` 直接别名 `prefab_vert()`），仅片元换成卡通光照。用法与 prefab 一致，`shader()` 挂静态共享的 toon shader（全实例同一 program）。

- **着色器**：`shader_sources::toon_frag()` 复用 prefab 的纹理层/法线贴图/AO/emissive/colorGrade 逻辑，光照经 `XHQM_TOON_LIGHTING` 宏展开（shader_preprocess 单一事实源，GL 330 / Vulkan 450 / AppleGL 简化版三态）：ramp 分档漫反射（halfLambert 经 `smoothstep(threshold∓softness)` 量化，暗部压到 0.35）、硬边高光（`spec_threshold` 控制，0 关闭）、边缘光（`pow(1-NdotV, rim_power)` × ramp × rim_color），CSM 阴影/衰减/聚光锥与 PBR 路径一致。
- **材质参数**（material_desc，经 UBO `materialToon`/`toonRimColor` 两槽上传，ubo.h 单一布局 GL/VK 自动一致）：`ramp_threshold`（分档阈值，默认 0.5）、`ramp_softness`（过渡软度，默认 0.08，越小边缘越硬）、`rim_power`（边缘光锐度，默认 3.0）、`spec_threshold`（高光阈值，默认 0 关闭）、`rim_color`（边缘光颜色 rgba，alpha 为强度，默认 0 关闭）。
- **已验证**：`test/toon_test.cpp`（Catch2，6 用例 / 31 条断言 —— `grep -c TEST_CASE` = 6、`grep -o 'CHECK\|REQUIRE'` = 31，本项数字实测准确）——UBO 偏移与 GLSL 布局一致、GL/Vulkan/AppleGL 预处理产物（无 XHQM_ 残留）、材质默认值、toon_prefab 静态共享接线（none 后端下用就绪标志防重复创建）。

## camera 相机

`camera`（camera.h）是唯一相机类（旧 `camera` 已于 r449 删除，`render_camera` 改名并入）：

- ~~`camera.h` 的类注释说「旧 camera 保留」~~ **已作废（2026-10-07 复核 r677）**：全仓只有一个 `class xhqm_io camera`（`camera.h:19`），**没有**被保留的旧相机类；`camera.h:5` 那句「（重新实现的相机类，旧 camera 保留）」是残留注释（登记在 `DEBUG.md` G4）。
- 投影默认值实测（`camera.h:22-25`、`camera.cpp:40-58`）：`m_ortho = false`（透视）、`m_fov = xhqm::radians(45)`（**弧度**）、`m_near = 0.01`、`m_far = 10000`、初始视图 `720 × 720 × 100`；`fov/near/far` 的 setter 会就着当前尺寸重做一次 `resize()`。类型别名 `prisma`/`nekome` 都是 `fantasy::camera`（`system.h:140-141`）。

1. **视图矩阵内聚**：`view()` 返回「NDC 缩放 × 自身坐标逆」的完整视图矩阵，`window()` = `proj × view`，theory 无需再手动 `transform(root, camera)`。
2. **默认透视**：`ortho=false`，与后处理 SSAO/SSR/depth 的透视假设一致。
3. **UI 元素跟随相机**：继承 models 复用「drawer 集合 + 跟随相机」机制。`add_ui(drawer*)` 挂按钮/sprite（屏幕位置恒定）；`add_ui(models*)` 挂子树（保留自身 coord 变换，供 gizmo 等带姿态的 UI）。
4. **orbit 单一状态**：`focus/yaw/pitch/dist` 是唯一来源，`life()` 应用到 coord。
5. **交互内化**：`pan/zoom/focus_move/move_operand/operand` + 键盘移动输入（`key_*` 状态，`life()` 每帧应用；**输入本身要宿主转发**——引擎不读键盘事件，`test/view` 负责把按键接到 `key_forward/key_back/.../move_speed`）。

### 跟随相机的原理

UI 元素挂在相机子树，渲染时相机 coord 被视图逆抵消，屏幕位置恒定：

```
system = proj × view_scale × 相机coord⁻¹ × 相机coord × ui局部 = proj × view_scale × ui局部
```

这正是「按钮跟相机、屏幕位置不变」的机制（`add_ui` 实现）。

### 坐标系 gizmo 姿态同步

`theory_gizmo_sync`（theory_render.h）：每帧把 gizmo 的旋转设为相机的逆旋转，让三轴始终指向世界：

```cpp
camera* cam = new camera();
cam->resize(800, 600, 100);
cam->orbit(true);

coord* gizmo = new coord();                     // 三轴箭头（property.h）
for (auto* d : gizmo->handle_drawers()) d->shadow(false);   // UI 不参与阴影
cam->add_ui(gizmo);                             // 挂相机下，屏幕位置固定

auto* gs = new theory_gizmo_sync(cam, gizmo);   // 每帧同步姿态（三轴指向世界）
gs->fg(fg);
fg->register_pass(gs);
```

### 接口

投影：`resize/ortho/fov/near/far/proj`；视图：`view`（内聚）；UI：`add_ui/clear_ui`；拾取：`slected`；轨道控制器：`orbit/orbit_add/dolly/dolly_ratio/focus/focus_shift/world_per_pixel`；背景：`ground/clear`；对象操控：`operand/apply_world/pan/zoom/operand_center`。

### demo

`test/view`（RhiViewport）是「多窗口查看器」：每窗口一个 `camera` + 独立 `theory_frame_graph` 管线（`setup_pipeline_for`），可 `attach_world` 共享同一世界多窗口查看，并挂坐标系 gizmo 演示「跟随相机 + 姿态同步」。

## 约定与注意事项

- **坐标系 z-up**：z 是「上下」（模型高度沿 z，如 CesiumMan z∈[0,1.5]）；正交投影 `near=0`、可见深度是负 z（`z∈[-2d,0]`）。模型要放 `translate(0,0,负值)`，放 `z=+值` 会被深度裁剪整体消失。默认是俯视视角，屏幕显示 x-y 平面。
- **matx 无默认构造**：`std::vector<matrix>(n)`、`std::map` 的 `operator[]` 不可用，改用 `resize(n, matrix(4,4))` / `push_back` / `insert_or_assign`。
- **rgba 字节序 BGR(A)**：`xhqm_rgba(r,g,b,a) = r | g<<8 | b<<16 | a<<24`。
- 引擎源码统一 UTF-8，MSVC 用 `/utf-8` 编译。
- **UBO 常量单一事实源**：所有「需要一致」的 UBO 参数集中在 `backend/ubo.h`，禁止在别处硬编码——
  - 数组大小：`UBO_MAX_LIGHTS=8`、`UBO_MAX_CASCADES=4`、`UBO_MAX_FLAGS=16`、`UBO_MAX_BONES=512`、`UBO_MAX_INSTANCES=64`；
  - flag 语义下标：`FLAG_USE_LIGHTING=0` … `FLAG_HAS_INSTANCES=15`；
  - 字节偏移 `UBO_*` 由上述常量派生，`ubo_glsl()` 据此生成着色器块（`bones[UBO_MAX_BONES]` 等）。实测：`UBO_TRANSFORM = 0`、`UBO_FLAGS = 128`、`UBO_BONES = 384`、`UBO_WORLD = 33152`、`UBO_LIGHTS = 33216`、`UBO_LIGHT_VP = 33728`、`UBO_INSTANCES = 34144`、**`UBO_SIZE = 38240`**（`backend/ubo.h:23-44`）。
  - 着色器源码不写数字：用 `XHQM_FLAG_*` / `XHQM_MAX_LIGHTS` / `XHQM_MAX_CASCADES` 宏，`shader_preprocess` 统一替换成 `ubo.h` 常量；骨骼/灯光上传上限在 `drawer.cpp`/`theory_render.cpp` 也用 `UBO_MAX_BONES` 等常量。改上限只动 `ubo.h`。
  - **本模块有两个写死的 2048 常量**，改它们要一起看：阴影图默认尺寸 `theory_shadow::m_size = 2048`（`theory_render.h:30`）与着色器里 PCF 的 `const vec2 texel = vec2(1.0 / 2048.0)`（`backend/shader_preprocess.cpp:64`、`:170`，**不随尺寸变**）—— 见 `DEBUG.md` A1。
- `read_offscreen` / `slected` 的像素坐标是**左下角原点**；窗口坐标需先翻转 `gl_y = height - y - 1`。
- 运行时 DLL：demo 目录需放 `xhqm.dll`（动态库模式）与 `assimp.dll`。
- **着色器源单一事实源**：`shader_sources.h`（468 行）存 `XHQM_*` 宏版本，`shader_preprocess`（299 行）按后端展开，勿在后端 .cpp 里另写 GLSL。展开出的版本号是 **`#version 330 core`（GL）/ `#version 450`（Vulkan）**（`backend/shader_preprocess.cpp:228`）。宏清单（含 Vulkan 侧的 `set = 0`/binding 号）：
  - `XHQM_VERSION`、`XHQM_UBO_BLOCK`（binding 0）、`XHQM_DEPTH_REMAP`（仅 VK：`gl_Position.z = 0.5*(z+w)`）、`XHQM_SHADOW_UV_FLIP`（仅 VK：`shadowUV.y = 1.0 - shadowUV.y`，见 `fantasy_bugs.md` #48）、`XHQM_INSTANCE_ID`（VK `gl_InstanceIndex` / GL `gl_InstanceID`）；
  - `XHQM_TEX0/1/2` → binding **1/2/3**、`XHQM_SHADOW_MAP` → binding **4**（`sampler2DShadow[UBO_MAX_CASCADES]` 数组）、`XHQM_CUBE_SHADOW` → binding **5**、`XHQM_IRRADIANCE`/`XHQM_PREFILTER` → binding **6/7**、`XHQM_NORMAL_MAP`/`XHQM_AO_MAP` → binding **8/9**；`XHQM_SPRITE_TEX`/`XHQM_COLOR_TEX` 同为 binding **1**（分处独立 program，见 `DEBUG.md` A5）；
  - `XHQM_LIGHTING` / `XHQM_TOON_LIGHTING`：完整逐灯光循环，或 `apple_gl=true` 时的极简漫反射（**仅 `__APPLE__` 且非 Vulkan 时置真**，`drawer.cpp:420-423`）。
  - 注意 `backend/shader_sources.h:6` 的注释写「GLSL 410 / 450」，实际是 **330 / 450**（见 `DEBUG.md` G2）。

## 参考实现

本仓的渲染侧范例集中在 `test/view/`（**独立工程**，不在 `test/CMakeLists.txt` 的 25 个 target 里）：

- `test/view/main.cpp`（943 行；`test/view/` 下另有 `RhiViewport.cpp/.h` 与 `ModelLoader.h`，唯一查看器入口）：`RhiViewport.cpp/.h` 提供 Vulkan QWindow + RHI 渲染 + 离屏拾取，`main.cpp` 负责场景搭建、多窗口/多相机（每窗口一条 `setup_pipeline_for` 管线）、gizmo、以及一批**自检块**（`gpu_hiz` / `gpu_hiz_culler` / `gpu_skinner` / `taa_jitter` / `bc1_encode` / `gltf::load` → `scene_save` → `scene_load` 往返）。本 README 的示例多数照它拼。
- `test/view/ModelLoader.h`：assimp 模型加载（角色/动画）+ LBS 蒙皮调色板采样。
- ~~`RenderWidget/`：Qt QOpenGLWidget + assimp 模型加载（`SkinnedDemo.h` 的 `load_model`）+ LBS 蒙皮动画。~~ **已作废（2026-10-07 复核 r677）**：GL 侧的 `RenderWidget` 查看器**已经合并进 `test/view`**，目录本身不在工作副本里（`test/view/CMakeLists.txt:3-6` 记着「RenderWidget（QOpenGLWidget/GL）与 RhiViewport（QWindow/Vulkan）已合并为统一查看器；RenderWidget 目录为历史版本，不再构建」）。所以本仓现在只有**一个**渲染查看器入口 `test/view/`，GL 与 Vulkan 由后端选择决定，不再是两个 demo 各管一个后端。`fantasy_bugs.md` 里提到的 `RenderWidgetTest.exe` 是那一版的历史构建产物。

> 这两个工程都要有图形上下文才能跑；本仓没有 headless/离屏的渲染回归目标（`DEBUG.md` 与根 `DEBUG.md` C3 都记了这一点）。

## 已知问题与调试记录

**当前仍存在的问题清单已迁到 [`DEBUG.md`](DEBUG.md)**（按 A 功能性缺陷 / B 资源与并发 / C 跨平台 / D 性能 / E 未实现占位 / F 设计取舍 / G 文档出入 分组，每条带 `文件:行号` 证据与复现方式）。本文不再重抄，只保留本仓历史上确实写过、现已作废的说法：

- `fantasy_bugs.md` 是本模块的**历史台账**（215 行，编号 1–53，含 P0/P1 排障全过程；**保留原名原位置、不搬家**）。本文旧版曾写它「53+ 项修复记录」，并称「剩余待定项仅 #21/#22」：
  - ~~`fantasy_bugs.md` 是本模块的静态分析 bug 清单（53+ 项，含 P0/P1 排障全过程）……剩余待定项：#21……#22……~~ **已作废（2026-10-07 复核 r677）**：台账实际编号范围是 **1–53**（不是「53+ 项修复记录」），且「仅 #21/#22 待定」是 `fantasy_bugs.md:9` 与 `:99` 在 2026-08-22 写下的**当轮**结论 —— 该文件其后又追加了 #38–#53（P0 正确性修复、阴影接线与排查、Vulkan teardown/闪烁、UBO 环形槽），所以「只剩两项」这句话在文件内部就不成立了。**#21（`binding=1` 三处别名）与 #22（`ubo_offset("params")` 与 `"tranMatrix"` 共用偏移 0）经本轮复读源码确认仍存在**，现登记在 `DEBUG.md` 的 A5 / B7，编号指向原文件。
  - ~~`fantasy_bugs.md` ……涵盖 colors/blurry/element/rhi_gl/rhi_vk/shader/ubo/command_recorder/gltf/scene_io/animation/lod/gpu_culler/drawer/system 等文件~~：覆盖范围这句仍成立，但「待定项」与「已修项」的划分要看 `DEBUG.md` 的编号对照表，别读旧结论。
- `fantasy_bugs.md` #43（方向光阴影在现代管线内不可见）**在代码层面已修**：#48 的 Vulkan 阴影 UV 翻转（`backend/shader_preprocess.cpp:254`，仅 VK）、#49 的深度专用 render pass external subpass 依赖（`backend/rhi_vk.cpp:3156-3172`）、#50 的 GL 深度专用离屏缺口、#51 的 fence 复位与按帧信号量（`backend/rhi_vk.cpp:1308-1312`）四条修法均在位。**但「阴影是否真的可见」需要 GPU 出图，本仓当前无图形验证入口，未复现** —— 细节与证据位置见 `DEBUG.md` 的「#43 的复核证据」。
- 阴影采样与 Hi-Z 的深度约定（0=近 1=远、Vulkan `XHQM_DEPTH_REMAP`）、UBO 环形槽（slot0 保留为暂存区，环形槽从 1 起分配，**`UBO_SLOTS = 1024`**）等细节以 `fantasy_bugs.md` 的编号条目 + `DEBUG.md` 的复读结论为准。

