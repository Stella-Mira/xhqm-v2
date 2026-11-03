# geometry 几何模块

> 最后同步：2026-10-07，对照工作副本 r677。

xhqm 的几何模块：**实用型 + 探索型**双定位。

- **实用型**：3D 计算几何与网格生成，服务渲染（`fantasy`）与拾取/碰撞。
- **探索型**：对算法做高维（N 维）泛化。3D 是 `dim = 3` 的特例，`hyper` 承载一般情况。
- **成熟度**：中高——图元网格化 / NURBS 求值·导数 / 求交度量可用，且有单测基线（见「测试与自检」）；缺高维凸几何（凸包 / Voronoi–Delaunay）、BRep 造型与布尔、网格拓扑与序列化。当前仍存在的问题见 [`DEBUG.md`](DEBUG.md)。

## 目录结构

```
algebra/geometry/
├── geometry.h     581 行   形状体系（shape 派生 + NURBS + hyper 命名空间）+ 声明
├── linear.h       937 行   行主序齐次线性代数（matx/vect/poit + 投影变换）
├── geometry.cpp   1345 行  实现（网格化 / NURBS 求值 / hyper 生成 / 求交度量）
├── brep/          边界表示拓扑层（OCC TopoDS 风格 BRep）
│   ├── brep.h     74 行    vertex/edge/wire/face/shell/solid 六层拓扑（纯结构，无 .cpp）
│   ├── README.md  子模块说明
│   └── DEBUG.md   子模块问题台账（与本文档同一轮整理 brep 时建立）
├── README.md
└── DEBUG.md       本模块当前仍存在的问题台账
```

行数为 `wc -l` 实测（2026-10-07，r677）；`brep/` 的细节问题记在 `brep/DEBUG.md`，本文件与其只记跨层与模块级问题。

**关于 `hyper`**：没有独立的 hyper 头文件。~~hyper 相关头~~ **已作废（2026-10-07 复核 r677）**：`hyper` 只是 `geometry.h:31-45` 里的一个命名空间（`point` / `topol` / `points` / `topols` / `bounds` / `object` + 4 个函数声明），实现落在 `geometry.cpp:765-839`；模块目录里不存在 hyper 文件。

## 依赖

| 依赖 | 用途 |
|---|---|
| `algebra/value.h` | 基础类型与常数（`real/measure/index/count`、`pi`、`preci`、`npos`） |
| `metaclass/template.h`、`metaclass/buffer.h` | `matx/vect/poit` 继承的 `buffer<type>` 容器底座 |
| `<queue>` | `create_sphere` 三角化的 BFS 细分队列 |
| `brep/` → `lattice/ring_list.hpp` | 边环（wire）的环形链表载体 |
| `brep/` → `network/uuid.h` | BRep 节点稳定身份（16 字节 UUID） |

外部服务关系：`tensor`（N 维数值运算；`eig` / `svd` 等分解在 `tensor_base`）与 `hyper` 分工互补；`fantasy` 负责几何的世界变换与实例化渲染（`fantasy/fantasy.h:5-6` 引入本模块；`fantasy/element.cpp:564-572` 用两次 `frustum_grid()` 相加造箭头网格，`fantasy/element.cpp:544-561` 用 `poit * matrix` 逐点摆放）。

~~`analysis`/`soul`（分解/凸优化/流形）~~ **已作废（2026-10-07 复核 r677）**：全仓没有凸优化 / 流形实现——`grep -rn "凸\|流形\|manifold\|convex" algebra/analysis algebra/soul` 0 命中，这些词只出现在根 `README.md` 与本文件「高维泛化探索路线」的规划行里。

## 架构

```
linear.h   —— 线性代数内核（行主序 4×4 齐次矩阵 + 投影/旋转/平移/缩放）
geometry.h —— 三套抽象：
             1. shape 派生：线/面/体图元 + 网格化 *_grid()
             2. NURBS：nurbs 工具 + curve / surface 有理求值与导数
             3. hyper：N 维结构生成（超立方/单纯形/超球采样）
geometry.cpp—— 全部实现（纯头文件类型，实现集中在单 .cpp）
brep/      —— 拓扑层：几何(曲线曲面) 与 拓扑(顶点边面壳实体) 分离，uuid 定身份
```

**设计原则**：几何层「无状态可共享」（curve/surface 只描述形状、不承载位置）；位置与实例化交给 `fantasy`；`brep` 用 UUID 而非指针做跨文件稳定引用。

## 子模块

### brep —— 边界表示拓扑层

参考 OCC TopoDS 的 BRep 边界表示，见 [`brep/README.md`](brep/README.md)。六层映射：`vertex → edge → wire → face → shell → solid`。

```cpp
namespace xhqm::geometry::brep {
    struct vertex : node_base { poit position; std::vector<node_id> edges; };
    struct edge   : node_base { node_id from, to; curve* crv; value t0, t1; std::vector<node_id> faces; };
    using wire     = ring_list<edge*, orient>;          // 边环：节点=edge 指针，环边数据=方向
    struct face   : node_base { surface* geom; wire outer; std::vector<wire> inner; std::vector<node_id> shells; };
    struct shell  : node_base { std::vector<node_id> faces; };
    struct solid  : node_base { std::vector<node_id> shells; };
}
```

## 关键类 / API

### linear.h — 线性代数内核

| 类 | 说明 |
|---|---|
| `matx<type>` | 矩阵，m==n 自动单位化；`det()`（LU 列主元）、`invmat()`（高斯-约当）、`tramat()`、`adjmat()`、`kidmat()` |
| `vect<type>` | 向量，4 分量（`[3]=0`）；`dot` / `mul`（叉乘）/ `length` / `normal` / `angle` |
| `poit<type>` | 点，4 分量（`[3]=1`）；`poit - poit` 得向量 |

变换与投影：`perspective` / `ortho` / `rotate_x/y/z/v` / `translate` / `scale` / `extract_move/rote/zoom` / `radians` / `degrees`。

### geometry.h — 形状体系

- `shape` 基类（`type()`），派生：`line` / `face` / `cube` / `grid`（面网格）/ `mesh`（体网格）/ `circle` / `sphere` / `frustum`。
- 基础图元（阶段二已落地）：`plane` / `box` / `cylinder` / `cone` / `torus` / `capsule` / `quad`，均提供 `*_grid()` 三角形网格化，`box::contain()` 做点包含测试。
- `face` 度量：`normal()`（右手定则 A→B→C）、`area()`、`centroid()`。
- NURBS：`nurbs` 静态工具（`find_span` / `base_funs` / `base_funs_deriv` / `linspace`）+ `curve` / `surface`。
  - `curve::creat_point/creat_points/creat_derivative`（有理曲线求值与 1/2 阶导数）。
  - `surface::creat_point/creat_points/creat_derivative/creat_normal/surface_grid/index_curve`（双参数求值、一阶偏导、单位法线、等参网格化）。
- `create_sphere` 系列：`create_sphere(radius, resolution)` 返回三角面列表（八分象限镜像细分，`resolution ≤ 0` 会先 clamp，见 `geometry.cpp:515-516`）；`create_sphere(radius)` 是曲面版本、指向 `create_sphere_1`。四个 NURBS 球面的实际覆盖范围（实测 `geometry.cpp:616-763`）：`_1` 全球面（z 从 -r 到 +r，极点落在参数两端，50×50 控制点）、`_2` 半球 `{z ≥ 0}`、`_4` 四分之一球 `{y ≥ 0, z ≥ 0}`、`_8` 八分之一球 `{x,y,z ≥ 0}`（后三者 11×11 控制点，极点落在参数边缘，**只有 `_2` 的参数域在极点折叠**，见 DEBUG 的 A 组）。

### 求交与度量

| 函数 | 说明 |
|---|---|
| `ray_triangle` | 射线-三角形（Möller–Trumbore），返回最近参数 t |
| `ray_sphere` | 射线-球，返回最近命中参数 t |
| `point_plane_distance` | 点到平面有符号距离 |
| `signed_volume` | 四面体有向体积 `(B-A)·((C-A)×(D-A))/6` |

参数约定（实测 `geometry.cpp:1055-1106`）：`ray_triangle` / `ray_sphere` 的 `rd` **不归一化**，返回的 `t` 以 `|rd|` 为单位（测试里传单位向量，`t` 即距离）；两者都只取 `t ≥ 0`（`ray_triangle:1072` 以 `t >= 0` 收尾；`ray_sphere:1087-1088` 取 `t0`、为负则取 `t1`，两者都负返回 `false`——射线起点在球内时命中出射点）；`point_plane_distance` 内部对 `plane_norm` 归一化，返回有符号距离。

### hyper — N 维几何结构层

与 `tensor`（N 维代数）互补：`tensor` 管数值运算，`hyper` 管结构组织。

| 类型 | 说明 |
|---|---|
| `point` | `std::vector<value>`，任意维顶点 |
| `topol` | `std::vector<index>`，顶点索引集 |
| `points` / `topols` / `bounds` | 顶点集合 / 拓扑集合 / 边界集合（多层） |
| `object` | `pair<points, bounds>`，几何对象 |

第 0 层算法（已实现，`geometry.cpp:765-839`）：

| 函数 | 实测语义 |
|---|---|
| `dimension(p)` | 返回 `p.size()` |
| `hypercube(dim, half = 1)` | 2^dim 个 ±half 顶点；`bounds[0]` 里 2·dim 个面，每面 2^(dim-1) 个顶点；`dim == 0` 返回空对象（`geometry.cpp:775`） |
| `simplex(dim)` | dim+1 个顶点（原点 + 各单位轴）；`bounds[0]` 只有 **1** 条含全部 dim+1 顶点的索引，不是 dim+1 个 (dim-1) 维面（与 `hypercube` 的 2·dim 面对不齐，见 DEBUG 的 E 组） |
| `hypersphere(dim, samples, radius = 1)` | 标准正态采样 → 按范数缩放到 `radius`；`dim == 0` 返回 samples 个空点 |

## 用法示例

```cpp
#include "../algebra/geometry/geometry.h"
using namespace xhqm;
using namespace xhqm::geometry;

// 1) 行主序变换：P * (T*R*S) 表示先缩放、再旋转、最后平移（矩阵从右往左作用到点）
matx M = translate(1.0f, 2.0f, 3.0f) * rotate_z(radians(45.0f)) * scale(2.0f);
poit  P = poit(1, 0, 0) * M;                 // 行向量右乘矩阵

// 2) 图元网格化
sphere  s(1.0f);
value   res = 0.05f;
grid    g = s.sphere_grid(res);              // 三角化球面（顶点去重）
box     b(poit(0,0,0), poit(1,1,1));
grid    bg = b.box_grid();                   // 6 面 12 三角形
torus   t(2.0f, 1.0f);
grid    tg = t.torus_grid();                 // 大环16段 × 管截面12段

// 3) NURBS 曲面（球体八分片）
surface sphere = create_sphere_8(1.0f);
poit   p  = sphere.creat_point(0.5f, 0.5f);
vect   n  = sphere.creat_normal(0.5f, 0.5f);
grid   sg = sphere.surface_grid(16, 16);     // 等参四边形 → 三角形

// 4) 求交
value t;
if (ray_triangle(poit(0,0,-5), vect(0,0,1),
                 poit(-1,-1,0), poit(1,-1,0), poit(0,1,0), t)) {
    poit hit = poit(0,0,-5) + vect(0,0,1) * t;
}

// 5) 高维结构生成
hyper::object cube4 = hyper::hypercube(4, 1.0f);   // 四维超立方
hyper::points sph7 = hyper::hypersphere(7, 1000);  // 7 维球面采样
```

**引入路径**（仓内实测的两种）：

| 方式 | 写法 | 说明 |
|---|---|---|
| 相对路径 | `#include "../algebra/geometry/geometry.h"` | `test/algebra_test.cpp:4`、`fantasy/fantasy.h:6` 的写法，相对**包含它的文件**；`geometry.h:5` 内部再引 `linear.h` |
| 门面头 | `#include "xhqm/geometry"` | `xhqm/geometry`（4 行）只含 `geometry.h` + `linear.h`，**不含** `brep/brep.h` |

BRep 要单独引：`#include "../algebra/geometry/brep/brep.h"`（`test/algebra_test.cpp:5`）；它依赖 `lattice/ring_list.hpp` 与 `network/uuid.h`，所以要么编入 `network/uuid.cpp`，要么链接预编译库。

**最小可运行示例**（逐行取自 `test/algebra_test.cpp` 的既有用例，注释里是它的断言值）：

```cpp
using namespace geometry;

geometry::value t = 0;
ray_sphere(geometry::poit(0, 0, -5), geometry::vect(0, 0, 1), geometry::poit(0, 0, 0), 1.0f, t); // t == 4
ray_triangle(geometry::poit(0.2f, 0.2f, -1.0f), geometry::vect(0, 0, 1),                        // t == 1
             geometry::poit(0, 0, 0), geometry::poit(1, 0, 0), geometry::poit(0, 1, 0), t);
point_plane_distance(geometry::poit(0, 0, 3), geometry::poit(0, 0, 0), geometry::vect(0, 0, 1)); // == 3
signed_volume(geometry::poit(0, 0, 0), geometry::poit(1, 0, 0),
              geometry::poit(0, 1, 0), geometry::poit(0, 0, 1));                                 // == 1/6

auto cube = geometry::hyper::hypercube(3, 1.0f);   // first.size()==8，second[0].size()==6，每面 4 顶点
auto sim  = geometry::hyper::simplex(3);           // first.size()==4
auto sph  = geometry::hyper::hypersphere(3, 64, 2.0f);  // 64 个 3 维点，范数 ≈ 2
```

返回值语义：`ray_triangle` / `ray_sphere` 返回 `bool` 并**出参**写 `t`；`point_plane_distance` / `signed_volume` 直接返回 `value`（= `float`）。`sphere::sphere_grid` 的形参是非 const 引用（`geometry.h:242`，会写回调用方的 `resolution`），所以要先有变量再传，不能直接传字面量。

## 测试与自检

无 ctest / CI，测试是独立可执行文件（`test/README.md`）。几何相关的 9 个用例都在 `test/algebra_test.cpp`，由 `test/CMakeLists.txt:16-25` 的 `algebra_test` 目标编入 `../algebra/geometry/geometry.cpp`：

| 用例（行号） | 覆盖 |
|---|---|
| `test_linear_geometry` (:163-191) | `vect`/`matx` 基础、`circle_point`、`circle_grid(4)` 面数、`sphere_point`、`nurbs::find_span`/`base_funs`（单位分解和 ≈ 1）、`create_sphere_1/4` 控制点非空 |
| `test_circle_orientation` (:361-393) | `circle::pose` 四种朝向（+z / +x / +y / -z）下的圆点位置 |
| `test_create_sphere_guard` (:395-405) | `resolution` = 0 / -1 / 0.5 的死循环回归 |
| `test_poit_hash` (:407-414) | `poit_hash` 的 XOR 可交换回归 |
| `test_hyper` (:416-455) | `hypercube`/`simplex`/`hypersphere`/`dimension` 的结构与范数 |
| `test_face_measure_intersect` (:457-488) | `face::normal/area/centroid`、`ray_sphere`(t=4)、`ray_triangle`(t=1)、`point_plane_distance`(±3)、`signed_volume`(1/6) |
| `test_primitives_grid` (:490-535) | `plane/box/cylinder/cone/torus/capsule/quad` 的顶点数与面数精确断言 |
| `test_nurbs_derivative` (:537-567) | 直线曲线一阶导 = 2、球面法线径向（`|dot|≈1`、`|n|=1`）、`surface_grid(4,4)` = 25 顶点 / 32 三角形 |
| `test_brep_wire` (:668-703) | BRep：uuid 唯一、`wire` push_next/`size`/`edge_of_next().reversed`/环遍历 |

```bash
cd test && cmake -B build -G Ninja && cmake --build build   # 或 -G "Visual Studio 17 2022"
./build/algebra_test                                       # 跑全部 24 个用例
./build/algebra_test "test_face_measure_intersect"         # 只跑求交/度量用例（Catch2 过滤）
```

> 旧说法（根 `README.md` 的 §2.5 algebra/geometry 段「劣势」「建议」两行——本轮读到的工作副本版本在 `:162` / `:164`，该文件正被并行修改、行号可能漂移）：~~缺单测基线，NURBS 导数/求交回归风险高~~ / ~~补求交/度量/NURBS 单测与数值基线~~。**已作废（2026-10-07 复核 r677）**：`test/algebra_test.cpp` 已有上表 9 个用例，求交、度量、NURBS 导数与 `surface_grid` 都有带容差的数值断言；仍缺的是 `sphere::sphere_grid`（全仓只有声明与定义、无调用方也无测试）、`create_sphere_2/4/8` 的几何断言（只断言了控制点数 > 0）与 `hyper` 的 `simplex` 边界语义（见 [`DEBUG.md`](DEBUG.md)）。

## 坐标与矩阵约定

- **行主序**：平移分量在 `(0,3)/(1,3)/(2,3)`；`perspective` 的深度项在 `(2,3)`、透视除法项在 `(3,2)`。
- **齐次 w**：`poit` 存 `[3]=1`，`vect` 存 `[3]=0`，用于区分点/向量。
- **z-up**：3D 空间 z 为上下；`ortho` 的 `(2,2) = -2/(far-near)` 反 Z，near=0 时可见深度为负。

## 命名约定

统一长度美学（刻意设计，非拼写漂移）：

- 基础类型 4 字母：`poit` / `vect` / `matx`。
- 矩阵操作 6 字母（`xxmat`）：`idemat` / `tramat` / `adjmat` / `invmat` / `kidmat`。

新增标识符沿用此风格。

## 编码坑（防再犯）

- **`rotate_v(axis, angle)` 的角度是弧度**，不是余弦值；对齐向量时需 `std::acos(cos_theta)`，否则朝向错误（见 `circle::circle_point`）。
- **递归细分必须 clamp resolution**：`create_sphere` 的细分条件是"边长 > resolution"，resolution ≤ 0 会死循环。
- **哈希组合不要用 XOR**：`hash(x)^hash(y)^hash(z)` 可交换导致 `(x,y,z)` 任意排列同 hash；用 hash_combine（`seed ^= h + 0x9e3779b9 + (seed<<6) + (seed>>2)`）。
- **几何值类型是 `float`**（`xhqm::real = float`，`head.h:230`），精度 `preci<float> = 1e-5f`；区别于 `xhqm::measure = double`。注意 `matx::det/invmat` 的奇异判据是**绝对**阈值 `preci`（`linear.h:211/289`），小尺度矩阵会被判成奇异（见 DEBUG 的 A 组）。
- **`grid::faces()` 是变长索引表**（`topol = std::vector<index>`）：7 个「阶段二图元」都只产出三角形，但 `frustum::frustum_grid` 会混排 3 顶点与 4 顶点面（`geometry.cpp:176/180`）。遍历 `faces()` 的代码不要假定每条都是 3 个索引（`fantasy` 的 `drawer::_draw_runs_` 就是按「连续同顶点数」分批绘制）。

## 高维泛化探索路线

| 层 | 方向 | 状态 |
|---|---|---|
| 第 0 层 | 3D 算法的直接对偶（超立方 / 单纯形 / 超球） | 已实现 |
| 第 1 层 | 高维凸几何（凸包 / 双描述 / Voronoi–Delaunay） | 规划中 |
| 第 2 层 | 组合拓扑 / 单纯同调（复形 / 持久同调 / TDA） | 远期 |
| 第 3 层 | 高维测度与采样（球体积 / SO(n) 采样 / 反直觉现象） | 规划中 |

边界：数值分解（SVD/eig）归 `tensor`（`tensor_base::eig/svd`）；~~凸优化、流形学习算法归 `analysis` / `soul`~~ **已作废（2026-10-07 复核 r677）**：全仓没有凸优化 / 流形实现（见上「依赖」节的复核命令），`hyper` 只做结构生成 + 组合拓扑 + 采样测度。

## 演进路线图

1. **阶段一**：P0 修复 + 测试基线 + hyper 第 0 层 —— 已落地（`test/algebra_test.cpp` 的 9 个几何用例，见「测试与自检」）
2. **阶段二**：基础图元（plane/box/cylinder/cone/torus/capsule/quad）+ 求交/度量 —— 已实现
3. **阶段三**：NURBS 导数（曲率/法线）+ 曲面网格化 —— 已实现（`creat_derivative/creat_normal/surface_grid`）
4. **阶段四**：网格拓扑（法线/焊接/邻接）+ 序列化/相等性 + BRep 拓扑（见 `brep/`）—— **部分落地**：`brep/brep.h` 的六层结构已定义（`test/algebra_test.cpp:668-703` 对 `wire` 边环有冒烟测试）；网格拓扑、`grid`/`mesh` 的相等性与序列化**未实现**（`grep -nE "operator==|serialize|json" geometry.h geometry.cpp` 0 命中）
5. **阶段五**：高维凸几何（第 1 层）
6. **阶段六**：组合拓扑/同调（第 2 层）

## 当前问题

本模块**当前仍存在**的问题（功能性缺陷 / 资源与并发 / 跨平台 / 性能 / 未实现桩 / 设计取舍 / 文档出入，逐条带 `文件:行号` 证据与复现方式）统一记在 [`DEBUG.md`](DEBUG.md)，本节不再重抄。历史台账不搬家，按各自范围保留：`brep/DEBUG.md`（BRep 子模块）、`fantasy/fantasy_bugs.md`（渲染侧）、`network/nebula/DEFERRED.md`（网络侧）。
