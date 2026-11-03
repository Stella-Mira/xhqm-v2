# xhqm · fantasy / lighting 光照模块

> 最后同步：2026-10-07，对照工作副本 r677。

`lighting` 是 `fantasy` 渲染层的**光照与阴影**子模块，位于分层架构的第 5 层（见父级 `fantasy/README.md:28` 的分层表）。它只负责「光源数据」与「阴影深度图」两件事：定义并缓存送往 GPU 的光源数据，以及为方向光 / 级联方向光 / 点光生成深度阴影图与对应的光空间变换矩阵。

> 注意：本模块**不包含任何光照着色器实现或光照 pass**。真正的光照计算由上层消费本模块产出的光源数组、阴影深度纹理与 view-proj 矩阵来完成 —— 消费点在 `fantasy/theory_render.h`（`theory_shadow:26` / `theory_scene:45` / `theory_ibl:203`，同属 `theory_render` 一张文件），逐 drawer 上传光源数组在 `fantasy/drawer.cpp:690-695`，采样与光照方程在 `fantasy/backend/shader_preprocess.cpp` 生成的着色器里（PCF 3×3 在 `:63-79`，点光 cube 4-tap 在 `:90-100`，toon 路径在 `:169-184`）。

## 模块定位

- **一句话**：为 xhqm 引擎提供「光源环境数据 + 三类阴影图（方向光 / CSM / 点光 cube）」的 CPU 侧资源与数学计算。
- **边界**：
  - 光源数据：`light_desc` 描述 + 场景级 `light` 聚合环境（含 IBL 环境光与开关）。
  - 阴影图：深度专用离屏目标的管理、光空间 view-proj 计算、深度纹理句柄的延迟获取。
  - **不做**：阴影采样 / PCF / 光照方程（均在 shader 与上层 pass 中）。
- **接线现状（2026-10-07 复核 r677）**：引擎里真正接上的只有**单级方向光阴影** —— `theory_shadow` 持有 `shadow_map`（`fantasy/theory_render.h:29`），每帧算一张紧贴场景 AABB 的深度图并恒写 `cascade_count=1`（`fantasy/theory_render.cpp:79`）。`cascaded_shadow_map` 与 `point_shadow_map` 在模块外**没有任何调用方**（只有 `test/view/main.cpp:429`、`:669` 的自检），属库级组件 —— 见 `DEBUG.md` 的 E1 / E2。
- **坐标系**：与引擎一致为 z-up（`shadow.cpp:51` 的 up 取 `(0,0,1)`）、列向量约定（M·v），矩阵列主序存储，上传 GLSL 前经 `tramat()`（光源 VP 的路径见 `fantasy/drawer.cpp:737-740`）。

## 目录与文件

```
lighting/                     （源码合计 533 行，实测 r677；本目录无 CMakeLists.txt）
├── light.h            120 行  光源定义（GPU 上传布局）+ 场景级光照环境
├── shadow.h  47 行 / shadow.cpp  89 行   方向光阴影映射（单张深度图）
├── csm.h     48 行 / csm.cpp     93 行   级联阴影映射（CSM，最多 4 级）
└── point_shadow.h 43 行 / point_shadow.cpp 93 行  点光阴影（立方体深度图，6 面）
```

文件集与构建条目一致：4 个头 + 3 个 `.cpp` 全部由仓库根 `CMakeLists.txt:425-428`（头文件列表）与 `:473-475`（`shadow.cpp` / `csm.cpp` / `point_shadow.cpp`）收录，测试侧另见 `test/CMakeLists.txt:244-246`、`:288-290`；包含路径靠 `CMakeLists.txt:86` 的 `include_directories("./fantasy/lighting/")`（以及 `test/view/CMakeLists.txt:69`、`:84`）。4 个头的卫哨宏与文件名一一对应：`XHQM_FANTASY_LIGHT` / `XHQM_FANTASY_SHADOW` / `XHQM_FANTASY_CSM` / `XHQM_FANTASY_POINT_SHADOW`。

## 关键类 / API

### 1. 光源数据（`light.h`）

| 名称 | 说明 |
|---|---|
| `light_type` | 枚举：`directional=0 / point=1 / spot=2`（`light.h:15-20`） |
| `MAX_LIGHTS` | `8`，UBO 固定数组容量上限（`light.h:23`；与 `fantasy/backend/ubo.h:17` 的 `UBO_MAX_LIGHTS=8` 是两个独立常量，无 `static_assert` 关联） |
| `LIGHT_BYTES` | `64`，每光源 GPU 布局字节数（= 4 × vec4，`light.h:25`）；全仓只在 `fantasy/backend/rhi_no.h:133` 的注释里被引用一次 |
| `light_desc` | 单光源描述：`type / color / position / direction / cutoff / outer_cutoff`；显式 inline 构造避免多 TU 强符号冲突，默认值 `color=0xffffffff`、`position=(0,0,0)`、`direction=(0,0,-1)`、`cutoff=0.9f`、`outer_cutoff=0.8f`（`light.h:37-44`） |
| `light` | 场景级光照环境：光源列表 + IBL 环境光 + 光照开关 + CPU 缓存；默认 `ambient=0x505050`、`reflection=1.0f`、`enabled=true`、`irradiance=0`、`prefiltered=0`（`light.h:53-58`） |

`light` 主要接口：

```cpp
// 光源列表
void lights(const std::vector<light_desc>& value);
const std::vector<light_desc>& lights() const;
void add_light(const light_desc& l);
void clear_lights();
void set_directional(const light_desc& l);  // 替换已有方向光，无则追加
uint light_count() const;

// IBL 环境光
void ambient(const rgba& v); const rgba& ambient() const;      // 默认 0x505050
void reflection(value v);    value reflection() const;         // 默认 1.0f；drawer 写进 ambientColor.w
void irradiance(uint v);     uint irradiance() const;          // irradiance cubemap 句柄（drawer 绑 unit 10）
void prefiltered(uint v);    uint prefiltered() const;         // prefiltered cubemap 句柄（drawer 绑 unit 11）

// 光照开关
void enabled(bool v); bool enabled() const;

// CPU 缓存：lights → float 数组（每光 16 value，与 uniform_lights std140 布局一致）
const std::vector<value>& light_data() const;                  // 长度 = light_count()*16；写入口后引用可能失效（DEBUG.md B4）
```

### 2. 方向光阴影（`shadow.h/.cpp`）

| 名称 | 说明 |
|---|---|
| `shadow_map` | 单张方向光深度阴影图 |

```cpp
bool setup(render_backend& rhi, uint w, uint h, std::string& error);
void reset();

// 计算方向光正交 view-proj：覆盖世界 AABB（中心 + 包围球半径），视线沿 direction
// 细节：光位 = 中心 - dir*radius；|dot(dir,up)|>0.99 时 up 换成 (0,1,0)；正交范围 [-r,r]×[-r,r]×[0,2r]
// 退化保护：radius < 1e-4 时取 1.0（shadow.cpp:42）
static matrix compute_light_view_proj(const point& direction, const aabb& world);

void begin(); void end();                              // 阴影 pass 内渲染场景深度
const matrix& light_view_proj() const;
void light_view_proj(const matrix& vp);
uint depth_texture();                                  // 深度纹理句柄（供 bind_texture）；未 setup 时返回 0
bool ready() const;
uint width() const; uint height() const;
```

### 3. 级联阴影映射（`csm.h/.cpp`）

| 名称 | 说明 |
|---|---|
| `cascaded_shadow_map` | 多级深度目标：近处高精度、远处低精度，最多 4 级 |

```cpp
bool setup(render_backend& rhi, uint count, uint w, uint h, std::string& error);
// count 只接受 1..4，越界置 error="illegal cascade count" 并返回 false（csm.cpp:15）
void reset();

// 以相机为球心，按对数/线性混合(lambda)分割 near..far，逐级算光空间 VP
// lambda=1 纯对数、0 纯线性；far<=near 时静默不改动；每级半径=该级 far 分割距离
void compute(const point& camera_pos, const point& direction, value near, value far, value lambda);

void begin(uint i); void end();                        // 渲染第 i 级深度（i 越界时 begin 直接返回）
uint count() const;
uint depth_texture(uint i);                            // i 越界返回 0（csm.cpp:88）
const matrix& light_view_proj(uint i) const;           // 注意：i 越界/空表无保护（csm.h:41，见 DEBUG.md A1）
value split_far(uint i) const;                         // 供 shader 选级；i 越界返回 0
bool ready() const;
```

### 4. 点光阴影（`point_shadow.h/.cpp`）

| 名称 | 说明 |
|---|---|
| `point_shadow_map` | 立方体深度阴影图（6 面透视，90° FOV），与 CSM 同构 |

```cpp
bool setup(render_backend& rhi, uint size, std::string& error);
// size==0 置 error="illegal cube size" 并返回 false（point_shadow.cpp:15）
void reset();

// 由点光位置 + 近远平面计算 6 面 view-proj（覆盖整个球面）
// 6 面方向/朝上表按 OpenGL cubemap 约定（point_shadow.cpp:44-49）；投影固定 90° FOV、aspect=1
// 注意 far 不是自由参数：采样侧写死 dist/10.0（见 DEBUG.md A4）
void compute(const point& light_pos, value near, value far);

void begin(uint face); void end();                     // face 0..5（+X/-X/+Y/-Y/+Z/-Z），face>5 时 begin 直接返回
uint depth_texture();                                  // 未 setup 时返回 0
const matrix& face_vp(uint i) const;                   // i 越界回退到第 0 面（point_shadow.h:36）
const matrix& face_view(uint i) const;                 // 距离写入深度着色器用；越界同样回退第 0 面
bool ready() const;
```

## 架构

三者（`shadow_map` / `cascaded_shadow_map` / `point_shadow_map`）遵循同一套**深度离屏目标生命周期**模式：

1. `setup()`：向 `render_backend` 申请深度专用离屏目标（`create_offscreen(w,h,0)` 或 `create_offscreen_cubemap_depth(size)`）。深度专用的事实证据：GL 后端在 `color_count==0` 时建 `GL_DEPTH_COMPONENT24` 并置 `COMPARE_REF_TO_TEXTURE/LEQUAL`（`fantasy/backend/rhi_gl.cpp:919-932`），cubemap 分支同理（`:1044-1051`）；VK 后端用 `m_depth_format` + 仅深度附件。注意三条分支用的**不是同一组 API**：2D 分支是 `create_offscreen` / `begin_offscreen` / `delete_offscreen` / `offscreen_depth_texture`，cube 分支是 `create_offscreen_cubemap_depth` / `begin_offscreen_cubemap` / `delete_offscreen_cubemap_depth` / `offscreen_cubemap_texture`（声明见 `fantasy/backend/rhi_no.h:103-119`）。
2. `compute()`：纯 CPU 数学，算出光空间 view-proj（方向光正交、CSM 逐级球心正交、点光 6 面透视）。
3. `begin()` / `end()`：绑定深度目标，`color_write(false)` 关闭颜色写只写深度，阴影 pass 内渲染场景深度。
4. `depth_texture()`：**延迟创建**深度纹理采样句柄（首次调用才 `offscreen_depth_texture` / `offscreen_cubemap_texture`），供片元着色器采样；目标无效时返回 `0`。
5. `reset()` / 析构：释放离屏资源（`delete_offscreen` / `delete_offscreen_cubemap_depth`），避免 GPU 资源泄漏；`render_backend*` 是裸指针，析构会无条件回调后端（见 `DEBUG.md` B1）。

**光源数据流**：`light` 持有 `std::vector<light_desc>`，并在 `lights()`（`light.h:68`）/ `add_light()`（`:70`）/ `clear_lights()`（`:71`）/ `set_directional()`（`:73-78`）等写入口置 `m_light_data_dirty = true`；`light_data()` 首次访问时把每光序列化为 16 个 `value`，之后缓存复用，避免逐 drawer 每帧重复构造。16 个 float 的实际用法是 `p[0..2]=color.rgb`、`p[3]=type`、`p[4..6]=position`、`p[8..10]=direction`、`p[12]=cutoff`、`p[13]=outer_cutoff`，`p[7]/p[11]/p[14]/p[15]` 恒为 0 —— 即 4 个 vec4 的 std140 布局，与 `fantasy/backend/ubo.h:120` 的 `struct Light { vec4 colorType; vec4 position; vec4 direction; vec4 spot; }` 严格对齐（证据：`light.h:104-110`；消费侧 `fantasy/backend/rhi_gl.cpp:664-678` 沿 `+0/+4/+8/+12` 取四个 vec4）。上传通道是 `rhi.uniform_lights(program, count, data)`（`fantasy/backend/rhi_no.h:134`），`count` 在 L2 侧被截到 `UBO_MAX_LIGHTS`（`rhi_gl.cpp:658`、`rhi_vk.cpp:2531`），drawer 侧先截到 `MAX_LIGHTS`（`fantasy/drawer.cpp:694`）。

**职责分离**：`light` 是「跨帧持久的数据」，与「渲染 pass」分离，被 `theory_scene` / `theory_shadow` / `theory_ibl` 消费。

## 子模块

| 子模块 | 文件 | 职责 |
|---|---|---|
| light | `light.h` | 光源类型 / 描述 / 场景级光照环境（含 IBL 与 CPU 缓存） |
| shadow | `shadow.h/.cpp` | 方向光单张阴影图 |
| csm | `csm.h/.cpp` | 级联阴影映射（对数/线性混合分割 + 逐级球心覆盖） |
| point_shadow | `point_shadow.h/.cpp` | 点光立方体阴影（6 面透视） |

## 依赖

| 依赖 | 用途 |
|---|---|
| `fantasy.h` | 基础类型别名：`value`/`real`=float/`uint`/`rgba`/`matrix`/`point`/`vect`/`aabb`（`fantasy.h:20-28`；`aabb` 实际声明在 `fantasy/drawer.h:28-38`，无构造函数） |
| `colors.h` | `color::rgba2array()` 把 `rgba` 解包为 `[r,g,b,a]`（`light.h:104`） |
| `drawer.h` | 渲染后端与绘制上下文声明（只有 `shadow.h:7` / `csm.h:8` 引；`point_shadow.h` 不引） |
| `rhi_no.h` | `render_backend` 虚接口：`create_offscreen` / `begin_offscreen` / `offscreen_depth_texture` / `create_offscreen_cubemap_depth` / `begin_offscreen_cubemap` / `offscreen_cubemap_texture` / `delete_offscreen_cubemap_depth` / `color_write` 等（`fantasy/backend/rhi_no.h:96-121`）；代码里的写法是短路径 `#include "rhi_no.h"` |
| `algebra`（经 `fantasy.h:4-5`） | `matx/vect/poit` 及 `ortho`（`algebra/geometry/linear.h:733`）/ `perspective`（`:709`）/ `radians`（`:691`） |

外部依赖为引擎内兄弟模块，无第三方库依赖。

## 简要用法示例

> ~~`#include "fantasy/lighting/light.h"`（其余三个头同理）~~ **已作废（2026-10-07 复核 r677）**：`grep -rn '#include "fantasy/'` 在本工作副本 0 命中，引擎与测试一律用短路径 `#include "light.h"`，靠 `include_directories("./fantasy/lighting/")`（`CMakeLists.txt:86`）解析。
>
> ~~`aabb world_box(point(-10,-10,-10), point(10,10,10));`~~ **已作废（2026-10-07 复核 r677）**：`aabb` **没有构造函数**（`fantasy/drawer.h:28-38` 只有公有的 `point min; point max;`），工程标准是 C++17（`test/view/CMakeLists.txt:17`），这句编译不过；按下面的逐字段赋值写。本模块自己的 `csm.cpp:64-66` 也是这么填的。

下面每段都取自仓库里的真实调用点（`fantasy/theory_render.cpp:183-197` 方向光、`test/view/main.cpp:260-267` 光源列表、`:429-447` / `:701-717` 点光、`fantasy/theory_render.h:30-39` 的 pass 用法），不是凭 API 想象出来的。

```cpp
#include "light.h"
#include "shadow.h"
#include "csm.h"
#include "point_shadow.h"
using namespace xhqm::fantasy;

render_backend& rhi = render_backend_active();   // theory_render.cpp:138 的取法

// 1) 场景光照环境（默认 ambient=0x505050 / reflection=1.0f / enabled=true）
light env;
light_desc sun;
sun.type = light_type::directional;              // directional=0 / point=1 / spot=2
sun.color = 0xffffffff;
sun.direction = point(0.0f, -0.6f, -0.8f);       // 光线行进方向（光→场景），会被单位化
env.add_light(sun);
env.ambient(0x505050);
env.irradiance(irr_cubemap_handle);              // theory_ibl 产出的句柄；0=无 → 走常数 ambient
env.prefiltered(pre_handle);                     // 镜面反射采样源
// 把环境交给管线/绘制体（两条真实接线路径）
// theory_shadow* sh = ...; sh->light_env(&env); sh->size(2048); sh->enabled(true);
// drawer* d = ...;         d->light_env(&env);   // 不设则退回 drawer 自持光源列表

// 每光 16 个 value 的 std140 数组（内部脏标记缓存，由 drawer 每帧上传）
const std::vector<value>& data = env.light_data();               // 长度 = light_count()*16
// rhi.uniform_lights(program, count, data.data());              // count 会被截到 MAX_LIGHTS=8

// 2) 方向光阴影（引擎内实际接线的等价写法，theory_render.cpp:183-197）
shadow_map sm;
std::string err;
if (sm.setup(rhi, 2048, 2048, err)) {            // color_count=0 → 深度专用离屏
    aabb world_box;                              // aabb 无构造函数，只能逐字段赋值
    world_box.min = point(-10, -10, -10);
    world_box.max = point(10, 10, 10);
    sm.light_view_proj(shadow_map::compute_light_view_proj(sun.direction, world_box));
    sm.begin();
    // ... 遍历场景、把几何按 lvp 画进深度图（theory_render.cpp:192-196 的 traverse + drawer）...
    sm.end();
    uint depth_tex = sm.depth_texture();         // 延迟创建；再 bind 给着色器采样
}

// 3) CSM（4 级；当前没有 in-tree 调用方，见 DEBUG.md E1）
cascaded_shadow_map csm;
if (csm.setup(rhi, 4, 2048, 2048, err)) {        // count 只接受 1..4（csm.cpp:15）
    csm.compute(camera_pos, sun.direction, 0.1f, 500.0f, 0.5f);  // (相机位, 光方向, near, far, lambda)
    for (uint i = 0; i < csm.count(); ++i) {     // 用 count() 循环，别写成固定 4
        csm.begin(i);
        // ... 渲染第 i 级深度 ...
        csm.end();
        uint tex_i = csm.depth_texture(i);       // 越界返回 0
        const matrix& vp_i = csm.light_view_proj(i);   // ⚠ 越界无保护（DEBUG.md A1）
        value split_i = csm.split_far(i);        // 第 i 级 far 分割距离，供 shader 选级
    }
}

// 4) 点光 cube 阴影（6 面；当前只在 test/view 自检里跑，见 DEBUG.md E2）
point_shadow_map psm;
if (psm.setup(rhi, 1024, err)) {                 // size==0 会返回 false
    psm.compute(point_light_pos, 0.1f, 10.0f);   // far 必须与采样侧一致：shader 写死 dist/10.0（DEBUG.md A4）
    for (uint face = 0; face < 6; ++face) {
        psm.begin(face);                         // +X/-X/+Y/-Y/+Z/-Z
        // ... 渲染该面深度（面内需自行写线性距离，工程里暂无该着色器）...
        psm.end();
    }
    uint cube_tex = psm.depth_texture();         // 立方体深度纹理（samplerCubeShadow）
}
```

## 注意事项

- 所有离屏目标必须成对 `begin/end`，否则影响后续渲染状态（`color_write` 开关尤其如此）。注意 `begin()` 在目标无效时会提前返回而 `end()` 不会 —— 未配对的 `end()` 在 GL 后端会把渲染目标恢复到上一次 2D 离屏之前的目标（见 `DEBUG.md` B2 / A3）。
- `light_desc` 的 `cutoff` / `outer_cutoff` 存的是**角度的余弦值**（默认内角 0.9 / 外角 0.8），非弧度/角度。证据链：定义与注释在 `light.h:33-34`、默认值在 `light.h:42-43`、写进第 4 个 vec4 的 `x/y` 在 `light.h:110`，消费它的着色器直接把 `cosTheta` 与这两个值比较（`fantasy/backend/shader_preprocess.cpp:105-106`，toon 路径 `:196-197`）。**内角余弦必须大于外角余弦**（cos 单调递减），填反或按弧度填（如 0.52/0.79）不会报错，只会静默得到硬边/全黑的锥体 —— 详见 `DEBUG.md` F1。
- CSM 的 `split_far(i)` 返回第 i 级 far 分割距离，用于着色器端按片元深度选择级联层；`i` 越界返回 `0`。
- `depth_texture()` 延迟创建句柄，须在 `setup` 成功且目标有效后调用，否则返回 `0`。
- 本模块只产深度图，**不含 PCF/软阴影/texel-snapping**：`grep -rni "pcf\|texel\|snap" fantasy/lighting/` 0 命中。引擎里实际做 PCF 的是着色器侧（CSM 3×3 + normal offset：`fantasy/backend/shader_preprocess.cpp:63-79`；点光 cube 4-tap：`:90-100`），其 texel 步长写死 `1.0/2048.0`（`:64`、`:170`），与可配置的阴影图尺寸脱钩（见 `DEBUG.md` G3）。
- 光源数超过 8 时**不会报错**：`light` 不拦、上传时被截到 `MAX_LIGHTS`（见 `DEBUG.md` B3）；聚光灯没有阴影（`DEBUG.md` E4）。

## 当前问题清单

一句话结论：**成熟度中高** —— 数据层（`light`/`light_desc`）与三类深度图的生命周期一致，引擎里唯一接线的是方向光单级阴影（`theory_shadow`，修法在位；运行期可见性见 `fantasy/fantasy_bugs.md` #48–#51 的历史记录，本轮未复现）；**缺** CSM 接线、点光 cube 阴影接线与聚光灯阴影，**也没有** texel-snapping（PCF 在 shader 侧，不在本模块）。

当前仍存在的问题（功能性缺陷 / 资源与并发 / 跨平台 / 性能 / 未实现占位 / 设计取舍 / 文档出入，**共 22 条，每条带 `文件:行号` 证据**）见同目录 `DEBUG.md`，本文件不重抄。历史台账 `fantasy/fantasy_bugs.md`（fantasy 静态分析 + 逐轮修复记录）与 `network/nebula/DEFERRED.md`（决定不做/没排期）保持原名原位置，只被指引、不被搬动。
