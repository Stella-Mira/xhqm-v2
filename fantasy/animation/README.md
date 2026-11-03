# fantasy/animation — 动画模块

> 最后同步：2026-10-07，对照工作副本 r677。

xhqm 引擎的骨骼动画子系统，位于命名空间 `xhqm::fantasy`，负责从「骨骼层级」到「蒙皮调色板」的整条动画链路：骨骼树、关键帧采样、动画混合、逆向运动学（IK）以及 GPU 蒙皮。全部矩阵采用**列向量约定**，两条路径产出的调色板统一为 `palette[i] = world[i] * inv(bind[i])`（CPU 在 `skeleton.cpp:77`，GPU 在 `gpu_skinning.cpp:52`）。

规模（r677 实测 `wc -l`，共 705 行）：`skeleton.h 50`、`skeleton.cpp 87`、`animation.h 59`、`animation.cpp 216`、`ik.h 23`、`ik.cpp 64`、`gpu_skinning.h 51`、`gpu_skinning.cpp 155`。

## 模块定位

| 文件 | 行数 | 职责 |
| --- | --- | --- |
| `skeleton.h/.cpp` | 50 / 87 | 骨骼层级树：父子关系、绑定/局部姿态、世界姿态与蒙皮调色板（CPU） |
| `animation.h/.cpp` | 59 / 216 | 动画 clip（关键帧 + 线性插值采样）、四元数、双 clip 权重混合 |
| `gpu_skinning.h/.cpp` | 51 / 155 | compute 着色器在 GPU 上计算蒙皮调色板，写入 SSBO |
| `ik.h/.cpp` | 23 / 64 | CCD 逆向运动学求解，使骨骼链末端逼近目标点 |

成熟度：中高——CPU 链路（骨骼树 → 采样 → 混合 → 调色板）自洽、四个 TU 在当前 flags 下全部编译通过（0 模块内告警）；缺 clip 播放控制器与资产导入，且 GPU 侧的 palette SSBO 目前**没有任何消费者**。当前仍存在的问题见同目录 `DEBUG.md`。

## 关键类 / API

### `skeleton`（skeleton.h）
骨骼树容器，管理父子层级与姿态。

```cpp
int  add_bone(const std::string& name, int parent);   // 返回骨骼索引，parent=-1 为根
void set_bind_pose(int bone, const matrix& m);        // 绑定姿态（模型空间）
void set_local_pose(int bone, const matrix& m);       // 局部姿态（相对父骨骼）
const matrix& get_local_pose(int bone) const;
const matrix& get_bind_pose(int bone) const;
std::vector<matrix> compute_palette() const;          // palette[i] = world[i] * inv(bind[i])
std::vector<matrix> compute_world_poses() const;      // world = parent_world * local
int  bone_count() const;
```

实现要点：`compute_world()` 从每根根骨骼（`parent < 0`）出发 DFS，父先于子，不依赖骨骼索引顺序；对未被根触达的孤立骨骼有兜底补算（`skeleton.cpp:65-66`）。越界索引不抛错：`set_*` 静默忽略，`get_*` 返回静态单位阵（`skeleton.cpp:34`、`:41`）。`matrix(4, 4)` 是 4×4 **单位阵**（`matx(m,n)` 在 `m == n` 时按单位阵填，`algebra/geometry/linear.h:24-37`）。

注意点：`compute_palette()` 每帧对每根骨骼重新求 `invmat()`（无缓存，见 DEBUG.md D6）；`invmat()` 对奇异矩阵返回 **0×0** 矩阵而不是报错（DEBUG.md A1）。

### `quat`（animation.h）
动画混合用四元数，避免矩阵线性插值的体积塌缩。

```cpp
static quat slerp(const quat& a, const quat& b, value t);   // cosom>0.9995 时退化为 nlerp
static quat from_matrix(const matrix& m);
matrix to_matrix() const;
```

`quat` 目前只被 `animation_blender::blend` 使用；`animation_clip::sample` 不走四元数（见下）。

### `animation_clip`（animation.h）
关键帧动画 clip：每个骨骼一条 `channel`（时间、位置、欧拉旋转、缩放）。

```cpp
void set_channel(int bone,
                 const std::vector<float>& times,       // 升序
                 const std::vector<vect<value>>& positions,
                 const std::vector<vect<value>>& rotations,  // 欧拉角（弧度，x/y/z）
                 const std::vector<vect<value>>& scales);
float duration() const;                             // 各通道 times.back() 的最大值
void sample(float time, skeleton& skel) const;  // 写入局部姿态，调用方随后 compute_palette
```

采样在区间内对 position/rotation/scale **逐分量线性插值**（`rotations` 是欧拉角，故旋转也是分量插值，`animation.cpp:137`），局部姿态构成为 `translate * rotate_z * rotate_y * rotate_x * scale`（`animation.cpp:144-146`）。三点行为需知：

- 区间查找是**线性扫描** `times`（`animation.cpp:121-124`），无二分/游标缓存；`time` 落在 `times.front()/back()` 之外时钳到首/末区间。
- `sample()` 只写**有通道的骨骼**，未列入的骨骼保留 skeleton 里的旧局部姿态（增量写，见 DEBUG.md F3）。
- `set_channel` 的校验是静默的：`times` 为空或 `positions.size() != times.size()` 时整条通道被丢弃且 `m_duration` 不更新；`rotations`/`scales` 尺寸不符则被 resize 补齐/截断；同一骨骼可被 `set_channel` 多次（追加为新通道，采样时后写覆盖先写）。见 DEBUG.md B4。

### `animation_blender`（animation.h）
两个 clip 的采样按权重混合。

```cpp
static void blend(const animation_clip& a, float time_a,
                  const animation_clip& b, float time_b,
                  float weight, skeleton& out);  // out 需已建好骨骼结构
```

混合策略（M9 修正）：先提取并去除矩阵缩放得到纯旋转，旋转走 `quat::slerp`，缩放与平移走线性插值，再把缩放乘回，保证缩放不丢失、混合不塌缩。`weight` 头注释约定 `∈[0,1]`，实现不钳位（DEBUG.md F5）；每次调用内部深拷贝两份 `skeleton`（DEBUG.md D4）。

### `gpu_bone` / `gpu_skinner`（gpu_skinning.h）
GPU 蒙皮输入与 compute 管线封装。

```cpp
struct gpu_bone {
    matrix local;    // 局部变换（相对父骨骼）
    int    parent;   // 父骨骼索引，-1 = 根
    matrix inv_bind; // 逆绑定矩阵
};

class gpu_skinner {
    bool build(const std::vector<gpu_bone>& bones, std::string& error);
    void update(const std::vector<gpu_bone>& bones);  // 原地更新（骨骼数不变）
    void compute();                                    // 调度 compute 计算调色板
    std::vector<matrix> read_palette();                // CPU 读回，仅验证/调试
    uint palette_ssbo() const;                         // binding 3 输出 SSBO
    uint bone_count() const;
};
```

实测细节（`gpu_skinning.cpp`）：

- 着色器按后端拼版本头：Vulkan `#version 450`，其他 `#version 430 core`（`:55`，后端判定见 `:73`）。`local_size_x = 64`（`:19`），dispatch 组数 `(bone_count + 63) / 64`（`:132`）。
- 三个 SSBO：params（binding 1，16 字节，首字 `boneCount`，`:79-81`）、bones（binding 2，每骨骼 **36 word = 144 字节** = local 16 + parent 1 + pad 3 + invBind 16，`:84-95`）、palette（binding 3，每骨骼 16 word = 64 字节，`:100-101`）。
- 矩阵按 **转置** 上传（`tramat()`，`:89-90`），与 `drawer.cpp:837` 上传 UBO `bones` 的约定一致；`read_palette()` 按列主序还原为引擎行主序（`:147-149`）。
- 着色器按 `parent` 逐级回退累乘世界矩阵，带 `guard < 1024` 硬上限（`:46-51`）；`update()` 每帧重打包并**全量重传**整块 bones SSBO（DEBUG.md D3）。
- `read_palette()` 是同步 GPU→CPU 读回（GL `glGetBufferSubData`、Vulkan `vkDeviceWaitIdle` + map，见 DEBUG.md D5/B1）。

> 当前状态：`palette_ssbo()` 在整个工作副本里**没有调用点**（只有本文件 `:45` 的定义）。网格渲染走的是另一条 CPU 通路——`object::open_skin(true)` + `object::bone_palette(std::vector<matrix>)`（`drawer.h:382`、`:416`）上传到 UBO `bones[UBO_MAX_BONES]`（`UBO_MAX_BONES = 512`，`fantasy/backend/ubo.h:20`；上传点 `drawer.cpp:829-840`、`theory_render.cpp:342-350`）。

### `ik_solver`（ik.h）
```cpp
static void solve_ccd(skeleton& skel, const std::vector<int>& chain,
                      const point& target, int iterations = 10);
```
`chain` 为从根到末端的骨骼索引序列（升序），`target` 为世界空间点，默认 10 次迭代；`chain.size() < 2` 直接返回。每轮从末端前一关节向根回退，绕关节旋转使末端逼近目标。已知两处代价：每个关节都重算一次整树世界姿态（DEBUG.md D1），且旋转是按世界空间轴**左乘关节的局部姿态**（DEBUG.md A2）。

## 架构

```
┌─────────────┐   sample    ┌──────────────┐  compute_palette  ┌───────────────┐
│ animation_  │ ──────────► │   skeleton   │ ────────────────► │  mat4[] 调色板 │
│ clip/blender │  局部姿态    │  (骨骼层级)   │  world*inv(bind) │  (CPU 蒙皮)    │
└─────────────┘             └──────────────┘                   └───────────────┘
                                                                    ▲
┌─────────────┐   set_local  ┌──────────────┐  世界姿态            │ 同一约定
│  ik_solver  │ ───────────► │   skeleton   │ ────────────────► CPU 参考
│   (CCD)     │   旋转局部姿态 │              │  compute_world_poses│
└─────────────┘             └──────────────┘                   └───────────────┘

┌─────────────┐  build/update ┌──────────────────────────────────────────┐
│ gpu_bone[]  │ ────────────► │ gpu_skinner: params/bones/palette SSBO   │
└─────────────┘               │ compute shader: world = local[root]*…*    │
                              │   local[i]; palette[i]=world*inv_bind[i] │
                              └──────────────────────────────────────────┘
```

- **CPU 路径**：`animation_clip::sample` 写入局部姿态 → `skeleton::compute_palette` 产出调色板。渲染侧真正消费的是**同形状**的 `std::vector<matrix>`（经 `object::bone_palette` → UBO `bones[512]`），但当前填这个 vector 的是 demo 自己的 Assimp 通路（`test/view/ModelLoader.h:329`）—— `compute_palette()` 在本仓没有调用点（`grep -rn "compute_palette" .` 只有定义与注释），`sample()`/`ik_solver` 亦然。
- **GPU 路径**：`gpu_skinner::build` 上传骨骼列表并创建 compute 管线，`compute()` 在 GPU 上按祖先链累乘世界矩阵并乘逆绑定矩阵，结果写入 SSBO。~~与 `gpu_culler` 同构（即写即用，无 CPU 读回）~~ **已作废（2026-10-07 复核 r677）**：结构与 `gpu_culler` 同构，但蒙皮输出**没有消费者**——`palette_ssbo()` 全仓零调用点（`grep -rn "palette_ssbo()"` 只有 `gpu_skinning.h:45` 的定义），"无 CPU 读回"只对 `compute()` 自身成立，`read_palette()` 是同步读回。
- **矩阵上传约定**：局部/逆绑定矩阵按「转置」（`tramat()`）上传，使 GLSL 列主序 `mat4` 乘与引擎行主序 `matrix` 乘语义一致。
- **两条路径无自动桥接**：模块不提供 `skeleton` → `gpu_bone[]` 的转换辅助（parent 与 inv_bind 要调用方自己打包），也没有"GPU 不可用时回退 CPU"的分支。

## 子模块

- **skeleton** — 骨骼层级树与调色板计算（纯 CPU，无 RHI 依赖）。
- **animation** — 关键帧采样、四元数、双 clip 混合（依赖 skeleton）。
- **gpu_skinning** — GPU compute 蒙皮（依赖 backend / rhi_no；compute 需要 GL 4.3+ 或 Vulkan，仓库内 demo 的 GL 上下文是 3.3 Core，见 DEBUG.md C1）。
- **ik** — CCD 逆向运动学（依赖 skeleton）。

## 依赖

- `fantasy.h`（类型别名 `value`/`matrix`/`vector`/`point`/`vect`/`uint` 等；`value = float`）
- `skeleton.h`（animation、ik 依赖）
- `backend.h`、`rhi_no.h`（gpu_skinning 通过 `render_backend_active()` 访问 RHI）
- 代数库 `algebra/geometry/linear.h`：`matx`（`matx(m,n)` 方阵即单位阵）、`tramat()`、`invmat()`（奇异返回 0×0）、`vect`（4 分量，第 4 位恒 0）、`poit`、`translate`、`rotate_x/y/z`、`rotate_v`、`scale`
- C++ 标准库：`<vector>`、`<string>`、`<algorithm>`、`<cmath>`、`<functional>`、`<cstring>`、`<cstdint>`
- 构建：`CMakeLists.txt:433-435`、`:461-466` 在 `XHQM_BUILD_FANTASY` 打开时无条件编入四个 `.cpp`（与渲染后端开关 `_render_gl`/`_render_vk` 无关）。注意头文件清单只列了 `skeleton.h`/`animation.h`/`ik.h`，漏了 `gpu_skinning.h`（DEBUG.md G6）。

## 简要用法示例

include 与编译：本模块的头文件按**扁平名**包含（与 `test/view/main.cpp:7` 的 `#include "gpu_skinning.h"` 一致），需要三个 include 目录（`CMakeLists.txt:82-94`、`test/view/CMakeLists.txt:66-77` 用同一组）：

```
-I<xhqm>  -I<xhqm>/fantasy  -I<xhqm>/fantasy/backend  -I<xhqm>/fantasy/animation
```

```cpp
#include "skeleton.h"       // fantasy/animation/skeleton.h
#include "animation.h"      // fantasy/animation/animation.h
#include "ik.h"             // fantasy/animation/ik.h
#include "gpu_skinning.h"   // fantasy/animation/gpu_skinning.h（连带 backend.h / rhi_no.h）

using namespace xhqm::fantasy;

// 1. 骨骼树：add_bone(name, parent) 返回索引，parent = -1 为根
skeleton skel;
int root = skel.add_bone("root", -1);
int arm  = skel.add_bone("arm", root);
skel.set_bind_pose(root, matrix(4, 4));                     // 4×4 单位阵
skel.set_bind_pose(arm,  xhqm::translate(0.0f, 1.0f, 0.0f)); // 绑定姿态（模型空间）

// 2. 动画 clip 采样：每骨骼一条通道，times 升序，rotations 是欧拉角（弧度）
animation_clip clip;
clip.set_channel(arm,
                 /*times*/     {0.0f, 1.0f},
                 /*positions*/ {vect<value>(0, 0, 0), vect<value>(0, 0, 1)},
                 /*rotations*/ {vect<value>(0, 0, 0), vect<value>(0, 0, 0.5f)},
                 /*scales*/    {vect<value>(1, 1, 1), vect<value>(1, 1, 1)});
float dur = clip.duration();          // 1.0f（= 各通道 times.back() 最大值）
clip.sample(0.25f, skel);             // 只写有通道的骨骼；其余骨骼保持旧局部姿态
std::vector<matrix> palette = skel.compute_palette();   // palette[i] = world[i] * inv(bind[i])

// 3. 混合：out 需已建好骨骼结构，weight 约定 [0,1]（实现不钳位）
animation_clip clip_b = clip;         // 第二个 clip
animation_blender::blend(clip, 0.25f, clip_b, 0.5f, 0.7f, skel);

// 4. IK：chain 为根→末端升序索引，target 是世界空间点，默认 10 次迭代
ik_solver::solve_ccd(skel, {root, arm}, point(1.0f, 0.0f, 0.0f), 10);

// 5. 送渲染（当前唯一被消费的通路）：CPU palette → object 的 UBO bones[512]
//    p->open_skin(true);
//    p->bone_palette(skel.compute_palette());   // 见 test/view/ModelLoader.h:331-332、main.cpp:937

// 6. GPU 蒙皮：gpu_bone{local, parent, inv_bind}；与 skeleton 无桥接，需自己打包
std::vector<gpu_bone> bones(2, gpu_bone{ matrix(4, 4), -1, matrix(4, 4) });
bones[0].local = xhqm::translate(1.0f, 0.0f, 0.0f);
bones[0].parent = -1;
bones[0].inv_bind = matrix(4, 4);
bones[1].local = xhqm::translate(0.0f, 2.0f, 0.0f);
bones[1].parent = 0;                                    // 父骨骼索引
bones[1].inv_bind = xhqm::translate(-1.0f, 0.0f, 0.0f); // 逆绑定矩阵
gpu_skinner gs;
std::string err;
if (gs.build(bones, err))          // 建 compute 管线 + params/bones/palette 三个 SSBO
{
    gs.update(bones);              // 每帧重传全部 36 word/骨骼（骨骼数必须等于 build 时的数量）
    gs.compute();                  // GL：立即执行；Vulkan：帧内录制 / 帧外 one_shot
    std::vector<matrix> gp = gs.read_palette();   // 仅验证/调试；Vulkan 帧内读回是 dispatch 前的内容
    // gs.palette_ssbo() 是 binding 3 的输出 SSBO，但当前全仓无消费者（见上文“当前状态”）
}
```

> 说明：`read_palette()` 与 CPU 参考实现逐元素比对的代码在 `test/view/main.cpp:465-519`（3 骨骼链，容差 `1e-4`），但整块被 `#if 0` 包住 —— ~~`gpu_skinner` 的 GPU↔CPU 一致性已在 `test/view/main.cpp` 中与 CPU 参考实现做逐元素比对验证（容差 `1e-4`）~~ **已作废（2026-10-07 复核 r677）**：该自检当前不参与编译、不运行，模块在本仓没有任何自动化测试（见 DEBUG.md G2）。

## 当前问题清单

「当前仍存在」的问题台账见同目录 **`DEBUG.md`**（功能性缺陷 / 资源与并发 / 跨平台 / 性能 / 未实现 / 设计取舍 / 文档出入，每条带 `文件:行号`）。历史台账 `../fantasy_bugs.md`（animation.cpp 侧 #28/#29/#30 记为已修）保持原名原位置，本模块不再重复记录。
