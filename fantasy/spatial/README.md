# spatial —— xhqm 引擎空间管理模块

> 最后同步：2026-10-07，对照工作副本 r677。

## 模块定位

`xhqm::fantasy::spatial` 是渲染管线的「可见性决策层」，负责把庞大的场景对象集合高效地收敛为每一帧真正需要提交给 GPU 的那一小批：

- **空间加速结构**：BVH 包围盒层次，加速射线拾取与视锥查询。
- **可见性裁剪**：CPU 视锥剔除、GPU 视锥剔除、遮挡剔除（CPU 查询式与 GPU Hi-Z 两阶段式）。
- **细节管理**：基于相机距离的 LOD 选择与 cross-fade 混合。
- **绘制序编排**：不透明/透明对象的渲染顺序排序。

模块同时提供了 **CPU 与 GPU 两条剔除路径**：CPU 路径（`spatial_bvh` + `scene_culler` + `occlusion_culler`）适合中小规模、需要 CPU 侧结果（如拾取、逻辑判定）的场景；GPU 路径（`gpu_culler` + `gpu_hiz` + `gpu_hiz_culler`）面向大规模对象，compute 直接写 `DrawIndexedIndirectCommand`，被剔除对象 `indexCount=0`，~~全程无 CPU 回读、无同步~~ **已作废（2026-10-07 复核 r677）**：GPU 剔除**主路径**（`cull` → `draw_indexed`）确实不回读，但（a）`occlusion_culler` 属 CPU 路径，每帧用 `query_result` **阻塞式**读回上一帧样本数（Vulkan 侧 `VK_QUERY_RESULT_WAIT_BIT`）；（b）GPU 路径自带的两个验证用回读 API（`gpu_hiz::read_level` 每次都回传整座金字塔，`gpu_hiz_culler::read_visible` 一次读全部命令）也都是同步阻塞调用。

**成熟度：中高，接口与数据结构自洽，但缺“接进渲染循环”。** `scene_culler` / `occlusion_culler` / `lod_group` / `render_queue` 在库内**零调用方**（全仓 grep 只命中定义与文档），GPU 三件套的唯一使用点是 `test/view/main.cpp` 的诊断块，且 Hi-Z 那半被 `#if 0` 关掉。当前仍存在的问题见 `DEBUG.md`。

## 目录结构

```
spatial/
├── spatial.h / spatial.cpp         # BVH 包围盒层次（射线拾取 / 视锥查询）      37 / 184 行
├── culler.h / culler.cpp           # 场景剔除器（从对象树收集 models，构建 BVH，视锥剔除） 34 / 35 行
├── occlusion.h / occlusion.cpp     # GPU 遮挡剔除（CPU 侧管理 + 深度专用着色器 + 遮挡查询，跨帧复用） 51 / 166 行
├── lod.h / lod.cpp                 # LOD 距离分级与 cross-fade 混合            43 / 53 行
├── render_queue.h / render_queue.cpp # 渲染队列（不透明前→后，透明后→前）      31 / 43 行
├── gpu_culler.h / gpu_culler.cpp   # GPU 视锥剔除（compute 写间接绘制命令）    48 / 160 行
├── gpu_hiz.h / gpu_hiz.cpp         # Hi-Z 深度金字塔（compute 逐级 max 下采样） 45 / 147 行
└── gpu_hiz_culler.h / gpu_hiz_culler.cpp # GPU 两阶段遮挡剔除（视锥 + Hi-Z 测试） 45 / 233 行
```

## 关键类与 API

### `spatial_bvh`（spatial.h）
纯 CPU 的二叉 BVH。以「最长轴 + 中点排序」递归切分，叶子节点最多容纳 4 个对象（`spatial.cpp:99`）。

```cpp
void build(const std::vector<std::pair<aabb, object*>>& items);
void clear();
void query_ray(const point& origin, const vector& dir, std::vector<object*>& out) const;
void query_frustum(const matrix& view_proj, std::vector<object*>& out) const;
size_t node_count() const;
```

- `query_ray`：slab 法射线-AABB 相交测试，命中叶子的对象整批追加到 `out`（**只到 AABB 粒度，不返回最近命中**）。
- `query_frustum`：从 `view_proj`（`proj * view * model`）提取 6 视锥平面（`row3 ± rowN`），做保守 AABB-视锥相交判定。
- 内部节点用 `std::vector<node*> children`（2 个）与 `std::vector<object*> objects` 存子节点/对象，节点由裸 `new node()` 分配、`clear()` 统一 `delete`。

### `scene_culler`（culler.h）
封装 `spatial_bvh` 面向场景的入口：~~广度遍历对象树~~ **已作废（2026-10-07 复核 r677）**：`culler.cpp:13` 的注释写「广度遍历」，实际 `for (auto* obj : (*root))` 走的是 `lattice/tree.hpp:94` 的**显式栈迭代 DFS**（`_iterate_dfs`，注释自陈「深树不爆栈」），顺序为「前序子节点 → 根 → 后序子节点」。筛选 `models*`，取局部 AABB 经 `system::transform(root, m)` 变换到世界空间后建树；`aabb::good()` 为假的模型直接跳过（不计入 `m_all`）。

```cpp
void build(object* root);                                  // 从场景根节点收集所有 models
std::vector<object*> cull_frustum(const matrix& view_proj) const;   // 内部走 bvh.query_frustum
size_t node_count() const;                                 // == spatial_bvh::node_count()
size_t object_count() const;                               // == 建树时通过 good() 筛选的模型数
```

### `occlusion_culler`（occlusion.h）
CPU 侧管理的 GPU 遮挡剔除。为每个候选对象建世界空间包围盒（12 三角形 = 36 顶点，`occlusion.cpp:31-38`）与遮挡查询槽，用深度专用着色器（`shader_sources::occlusion_vert/occlusion_frag`：颜色写关、只写深度）把包围盒画进**当前绑定的**深度缓冲，再读回上一帧样本数裁剪本帧。查询结果跨帧复用（一帧延迟）。

```cpp
bool setup(render_backend& rhi, std::string& error);       // 渲染上下文就绪后调用一次
void reset();                                              // 场景重建时清空（释放槽/vb/query/shader）
std::vector<object*> cull_and_record(
    const matrix& clip_from_world,
    const std::vector<std::pair<aabb, object*>>& candidates);
size_t slot_count() const;
void threshold(uint value);                                // 可见阈值（样本数 > threshold，默认 0）
```

调用方需自己保证：遮挡 pass 的深度目标已绑定且已清、`setup()` 与 `reset()` 成对、不依赖 `cull_and_record` 恢复渲染状态（它会留下 `depth_test(true,false)` 与已 `use_program` 的状态）。详见 `DEBUG.md`。

### `lod_group` / `lod_blend_result`（lod.h）
按相机距离选择 LOD 级别，并提供 cross-fade 混合避免硬切跳变。

```cpp
void add_level(value distance, object* obj);               // distance 升序（不做排序校验）
object* select(value distance) const;                      // 无级别返回 nullptr；取 distance <= 查询距离的最后一个级别
lod_blend_result select_blend(value distance, value fade_range) const;
// lod_blend_result { object* from; object* to; value weight; }  weight∈[0,1] 为 to 的权重
// 仅在 [切换点 - fade_range, 切换点] 窗口内 from != to 且 weight 计算生效；窗口外 from == to == 当前级别、weight = 0
```

### `render_queue`（render_queue.h）
按深度与透明标志排序：不透明对象近→远（front-to-back，减少 overdraw），透明对象远→近（back-to-front，保证混合正确）。`order()` 不改自身状态（内部复制后再 `std::sort`），可重复调用。

```cpp
void add(object* obj, value depth, bool transparent = false);   // depth = 到相机距离，越大越远
std::vector<object*> order() const;                             // 不透明在前、透明在后
void clear();
```

### `gpu_culler`（gpu_culler.h）
GPU 视锥剔除。compute 读对象 AABB，测试 6 视锥平面（`d + r < 0` 即剔除），写 `DrawIndexedIndirectCommand` 到 SSBO，随后 `draw_indexed_indirect_storage` 直接消费；被剔除对象 `indexCount=0`，GPU 零开销跳过。**非压缩版**：每条命令一个对象、`count` 恒等于对象数。

```cpp
struct gpu_cull_object {
    value center[3];      // 世界空间 AABB 中心
    value half_extent[3]; // 半边长（正值）
    uint cmd[5];          // indexCount/instanceCount/firstIndex/vertexOffset/baseInstance
};

bool build(const std::vector<gpu_cull_object>& objects, std::string& error);   // 对象列表为空 -> false
void cull(const value planes[6][4]);      // 上传平面后 dispatch，groups = (count+63)/64
void draw_indexed(uint vertex, uint index);
uint object_count() const;

// 从 view_proj 提取 6 视锥平面（约定同 spatial.cpp 的 extract_planes）
xhqm_io void extract_frustum_planes(const matrix& view_proj, value planes[6][4]);
```

### `gpu_hiz`（gpu_hiz.h）
Hi-Z 深度金字塔：compute 逐级把 2×2 深度块下采样为「最远深度」（`max`），全部层级连续存于单一 SSBO（层偏移见 `build()` 的 `m_offsets`）。深度约定 **`0=近 1=远` 的归一化深度**，块内取 `max`（最远）——对象最近深度大于块 `max` 则必被遮挡（保守）。

```cpp
bool build(uint w, uint h, std::string& error);            // level0 尺寸 w×h；尺寸为 0 -> false
void upload_level0(const std::vector<value>& depth);       // size 必须 == w*h，否则静默忽略
void build_pyramid();                                      // 逐级 dispatch（local_size 8x8）
std::vector<value> read_level(uint level);                 // 读回某层（同步；内部先读回整座金字塔）
uint level_count() const;                                  // 例如 1920x1080 -> 11 级（到 1x1）
uint depth_ssbo() const;                                   // 供 gpu_hiz_culler 引用
uint width() const; uint height() const;
```

### `gpu_hiz_culler`（gpu_hiz_culler.h）
GPU 两阶段遮挡剔除：compute 先做裁剪空间视锥剔除（8 角点投影 + NDC 三轴判定），再对可见对象按屏幕矩形取 Hi-Z 层做保守遮挡测试，写 `DrawIndexedIndirectCommand`（被剔除 `indexCount=0`）。裁剪约定为 **w > 0 的 NDC `[-1,1]`³**，归一化深度按 `0.5*(minNdcZ+1)` 折算。

```cpp
struct gpu_hiz_cull_object {
    value center[3];
    value half_extent[3];
    uint cmd[5];
};

bool build(const std::vector<gpu_hiz_cull_object>& objects, std::string& error);
void cull(const matrix& view_proj, uint screen_w, uint screen_h,
          uint hiz_ssbo, uint hiz_w, uint hiz_h, uint hiz_levels);   // hiz_ssbo==0 直接返回
void draw_indexed(uint vertex, uint index);
std::vector<bool> read_visible();                          // 同步读回（每对象 5 uint），验证/调试用
uint object_count() const;
```

`hiz_w/hiz_h/hiz_levels` 必须与 `gpu_hiz` 建金字塔时的参数一致——`cull()` 内部按这三个数**重新推算各层偏移**（`gpu_hiz_culler.cpp:184-194`），不校验 SSBO 实际尺寸。着色器端 `hizOffsets[12]` 为定长数组，`level` 被钳到 11（已防越界），但 `hiz_levels > 12`（8K 级）时 `cull()` 的 C++ 循环只填 12 项。

## 架构

```
                     对象树 (object / models)
                            │
              ┌─────────────┴──────────────┐
              │  CPU 路径                    │  GPU 路径
              ▼                             ▼
      scene_culler::build         gpu_culler / gpu_hiz_culler::build
       (spatial_bvh)               (上传 AABB + 命令到 SSBO)
              │                             │
      cull_frustum(view_proj)      cull(planes) / cull(view_proj, hiz)
              │                             │
   occlusion_culler::cull_and_record   draw_indexed_indirect_storage
   (深度查询，跨帧复用)                  (零回读消费命令)
              │
      render_queue::order
   (不透明前→后 / 透明后→前)
```

- **CPU 路径**：`spatial_bvh` 负责空间查询，`scene_culler` 负责建树与视锥剔除，`occlusion_culler` 负责 GPU 遮挡查询（结果仍回 CPU 使用），`render_queue` 负责排序。
- **GPU 路径**：`gpu_culler`（仅视锥）与 `gpu_hiz_culler`（视锥 + Hi-Z 遮挡）把剔除结果直接写为间接绘制命令，`gpu_hiz` 提供金字塔深度缓冲，三者以 SSBO binding 松耦合协作（`gpu_culler` 绑定 1/2/3；`gpu_hiz` 绑定 1/2；`gpu_hiz_culler` 绑定 1/2/3/4）。
- **接线现状**：上图的箭头是**能力**而非**已接线**——库内没有任何 pass 调用它们；已落地的唯一入口在 `test/view/main.cpp`（[HIZCULL] 诊断块，`main.cpp:581-616`，其中 `gpu_hiz` 自身那个块 `main.cpp:521-579` 被 `#if 0` 关掉）。

## 依赖

| 依赖 | 用途 |
| --- | --- |
| `fantasy.h` | 基础类型 `value` / `matrix` / `vector` / `point`（`real = float`） |
| `blurry.h` | `object` 基类与 `derive_tree` 树遍历（迭代器实现来自 `lattice/tree.hpp`） |
| `drawer.h` | `aabb`（`attach` / `access` / `good`） |
| `system.h` | `models`、`system::transform`（世界变换求解） |
| `backend.h` / `rhi_no.h` | `render_backend`：着色器/存储缓冲/compute/查询/间接绘制 API（`gpu_hiz.h`/`gpu_hiz_culler.h` 进模块头的 include 路径后可见） |
| `shader_preprocess.h` / `shader_sources.h` | 遮挡剔除深度专用着色器源（`occlusion.cpp` 的依赖；这两个头在 `element.cpp`、`drawer.cpp`、`theory_render.cpp` 也用） |

渲染后端为 OpenGL 与 Vulkan 双实现，运行时经 `render_backend_active()` 调度；`gpu_culler` / `gpu_hiz` / `gpu_hiz_culler` 的 compute 着色器在源码中以 GLSL 字符串内联，按 `rhi.name()[0]=='v'` 选择 `#version 450` 或 `#version 430 core`；`occlusion.cpp` 的顶点/片元着色器走 `shader_preprocess` 的宏展开（`rhi.name()[0]=='v'` 时展开 `XHQM_DEPTH_REMAP` = `gl_Position.z = 0.5*(gl_Position.z + gl_Position.w)`）。

## 用法

### 1) CPU：建树 + 视锥剔除 + 排队

下面这段只用真实存在的 API（`scenes`/`models` 的用法取自 `test/view/main.cpp:276-288`，矩阵组合取自 `fantasy/property.cpp:56` 的 `proj() * view()`）：

```cpp
#include "fantasy/spatial/culler.h"
#include "fantasy/spatial/render_queue.h"

using namespace xhqm::fantasy;

scenes scene;                                  // 场景根（fantasy::scenes : system）
models* box = new models();
scene.add_target(box);                         // 只接受 models*

scene_culler culler;
culler.build(&scene);                          // 收 models 的 world AABB 建 BVH；root 为 null 直接返回
matrix view_proj = scene.camera()->proj() * scene.camera()->view();
std::vector<object*> visible = culler.cull_frustum(view_proj);   // 返回的是 models*（object*）

render_queue rq;
for (object* o : visible)
    rq.add(o, /*depth=*/cam_dist(o), /*transparent=*/false);     // depth 为到相机距离
std::vector<object*> ordered = rq.order();     // 不透明近→远，透明在后且远→近
```

### 2) LOD 选择与 cross-fade

```cpp
#include "fantasy/spatial/lod.h"

lod_group group;
group.add_level(0.0f, lod_high);               // distance 必须升序传入，add_level 不排序
group.add_level(50.0f, lod_mid);
group.add_level(200.0f, lod_low);

object* cur = group.select(distance);                        // 硬切
lod_blend_result b = group.select_blend(distance, 20.0f);    // 距离切换点前 20 单位内 from!=to
// b.from / b.to 按 b.weight 混合；窗口外 b.from == b.to、b.weight == 0
```

### 3) GPU：视锥剔除（`gpu_culler`）

```cpp
#include "fantasy/spatial/gpu_culler.h"

std::vector<gpu_cull_object> objs(1);
objs[0].center[0] = 0; objs[0].center[1] = 0; objs[0].center[2] = 0;
objs[0].half_extent[0] = 1; objs[0].half_extent[1] = 1; objs[0].half_extent[2] = 1;
objs[0].cmd[0] = 36; objs[0].cmd[1] = 1;       // indexCount / instanceCount
objs[0].cmd[2] = 0;  objs[0].cmd[3] = 0;       // firstIndex / vertexOffset
objs[0].cmd[4] = 0;                            // baseInstance

gpu_culler gc;
std::string err;
if (!gc.build(objs, err)) { /* err 有原因；空列表也会失败 */ }

value planes[6][4];
extract_frustum_planes(scene.camera()->proj() * scene.camera()->view(), planes);  // 世界空间视锥
gc.cull(planes);
gc.draw_indexed(vb, ib);                       // 间接绘制；被剔除者 indexCount=0
```

### 4) GPU：Hi-Z 两阶段遮挡剔除（`gpu_hiz` + `gpu_hiz_culler`）

参数与判据取自 `test/view/main.cpp:584-616` 的 [HIZCULL] 块（该块当前被包在 `main.cpp:522` 的 `#if 0` 里，未执行）：

```cpp
#include "fantasy/spatial/gpu_hiz.h"
#include "fantasy/spatial/gpu_hiz_culler.h"

// (a) 建 4x4 的 Hi-Z：深度值必须是 [0,1] 归一化（0=近 1=远），由调用方提供
gpu_hiz hz;
std::string err;
if (hz.build(4, 4, err))
{
    hz.upload_level0(std::vector<value>(16, 0.9f));   // 铺一层“距离 0.9 的遮挡面”
    hz.build_pyramid();                              // 4x4 -> 2x2 -> 1x1，共 3 级
}

// (b) 三对象：near(z=0.5) / far(z=0.95) / 视锥外(x=2)
std::vector<gpu_hiz_cull_object> objs(3);
for (auto& o : objs)
{
    o.half_extent[0] = o.half_extent[1] = o.half_extent[2] = 0.01f;
    o.cmd[0] = 6; o.cmd[1] = 1; o.cmd[2] = o.cmd[3] = o.cmd[4] = 0;   // indexCount=6, instanceCount=1
}
objs[0].center[2] = 0.5f; objs[1].center[2] = 0.95f; objs[2].center[0] = 2.0f;

gpu_hiz_culler gc;
if (gc.build(objs, err))
{
    gc.cull(matrix(4, 4),                            // view_proj（此处用单位阵）
            4, 4,                                    // 屏幕尺寸
            hz.depth_ssbo(), hz.width(), hz.height(), hz.level_count());
    gc.draw_indexed(vb, ib);
    std::vector<bool> vis = gc.read_visible();       // 同步回读，仅验证/调试用
    // 期望 vis = {true, false, false}：z=0.5 的最近深度 0.75 < 0.9 可见；
    // z=0.95 折算 0.975 > 0.9 被判遮挡；x=2 出视锥
}
```

## 注意事项

- `spatial_bvh` 内部用裸 `new node()` + 手动 `delete`，生命周期由 `clear()` / 析构统一管理，勿手动持有 `node*`。树遍历本身是显式栈迭代（不爆栈），递归只在建树/查询内部。
- `occlusion_culler` 的结果带**一帧延迟**（首帧返回全部候选），且 `cull_and_record` 只使用**调用方当前绑定的**深度目标，不会自己切离屏、清深度或设视口。
- 视锥平面提取的约定为「列向量约定，`row3 ± rowN`」，`spatial.cpp:44-61` 与 `gpu_culler.cpp:9-18` 各有一份等价实现，修改时需保持同步。
- `gpu_hiz_culler::cull` 的 `hiz_*` 三个参数必须与建金字塔时一致；不一致不会报错，只会静默读错层。
- 当前仍存在的问题（含上述几条的证据与复现方式）集中在 `DEBUG.md`。
