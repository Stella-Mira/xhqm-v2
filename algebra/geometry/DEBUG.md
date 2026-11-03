# geometry 模块 DEBUG —— 当前仍存在的问题

`algebra/geometry`（3D 计算几何 / NURBS / hyper / BRep 结构）的问题台账。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。

与同类文件的关系：`fantasy/fantasy_bugs.md`（渲染侧，带 ✅ 已修状态）与 `network/nebula/DEFERRED.md`（网络侧的「不做/待定」清单）按各自范围保留、不搬家。`brep/` 子模块现在有自己的 `brep/README.md` + `brep/DEBUG.md`（DEBUG.md 是本轮并行整理 brep 时建立的），因此 BRep 的细节问题归 `brep/DEBUG.md`，本文件只在跨层处保留一条指针（见 E 组最后一条）。

本轮结论全部来自**读 r677 工作副本源码**（未编译、未跑测试）；凡未实跑的都写成「未复现」并给出可直接跑的命令或最小触发代码。

---

## A. 功能性缺陷（会算错 / 会崩）

### `curve::creat_point` 未判 `nurbs_k == npos`：空基函数越界读 + 循环上界 SIZE_MAX
- 位置：`geometry.cpp:241-279`（关键 `:248` `:250` `:253` `:257` `:262`）
- 现状：`m_nurbs_k` 的默认值是 `xhqm::npos`（`geometry.h:477`）。此时 `nurbs::find_span` 直接 `return 0`（`geometry.cpp:189`）、`nurbs::base_funs` 直接 `return {}`（`geometry.cpp:207`）；于是 `count start = span - m_nurbs_k`（`:253`）在 `size_t` 上回绕成 1，`for (count i = 0; i <= m_nurbs_k; ++i)`（`:257`）的上界是 `SIZE_MAX` → 循环不终止，且循环体内 `N[i]`（`:262`）读的是空 vector。
- 证据：同文件的 `curve::creat_points` 判了这个值（`geometry.cpp:285-288`）而 `creat_point` 没判；`surface::creat_point` 的三个重载把 `m_nurbs_k[0]/[1]` 直接当循环上界（`:352` `:392` `:437`），`N_u[i]`/`N_v[j]` 同样是空 vector 读取（`:355` `:395` `:444`）。
- 影响：只填了控制点、没设阶数的 curve/surface 一旦求值 → 越界读（崩或垃圾点）+ 事实死循环。这是本模块最容易被踩的一处。
- 复现/验证：未复现（本轮未编译）。最小触发：`geometry::curve c; c.nurbs_p({poit(0,0,0), poit(1,0,0)}); c.creat_point(0.5f);`（k 保持默认 npos）。
- 建议：`creat_point` 开头加 `if (m_nurbs_k == xhqm::npos || m_nurbs_d.empty()) return geometry::poit();`，与 `creat_points` 对齐。

### `matx * matx` 维度不匹配时不抛异常（静默返回 0×0）
- 位置：`linear.h:77-99`（`:80-82`）
- 现状：不匹配分支里 `std::runtime_error error("Illegitimately multiplied matrices !!!");` 只构造、**没有 `throw`**，且 `else` 分支不入，`c` 保持 `matx(0,0)` 返回。
- 证据：同文件 `vect*matx` / `matx*vect` / `poit*matx` / `matx*poit` 四个重载都 `throw std::runtime_error(...)`（`linear.h:439` `:453` `:621` `:635`），只有 `matx*matx` 漏了。
- 影响：维度写错的乘法得到 0×0 矩阵并继续传递（后续 `operator()` 按 `data()[i*cols+j]` 访问会越界，`linear.h:63-70`），比抛异常更难定位。
- 复现/验证：未复现。`matx<float> a(2,3), b(2,3); auto c = a * b;` → `c.dimensions() == {0,0}`。
- 建议：补 `throw`（或 `assert`），与 `vect/poit` 路径一致。

### `vect::normal()` 对零长度无保护 → inf/NaN 静默传播
- 位置：`linear.h:395`；受影响调用点 `linear.h:808`（`rotate_v` 归一化轴）、`geometry.cpp:1095`（`point_plane_distance`）、`geometry.cpp:153`（`sphere_point`）、`geometry.cpp:863-866`（plane_grid 的基向量）、`geometry.cpp:529-534`（`create_sphere` 的中点归一化）
- 现状：`(*this) / length()`，`length() == 0` 时是 0/0 → NaN。
- 证据：`vect::angle` 对零长度有显式保护（`linear.h:399`）、`face::normal`（`geometry.cpp:846`）与 `surface::creat_normal`（`geometry.cpp:1313`）都有退化兜底 —— 只有 `normal()` 没有。
- 影响：`point_plane_distance(p, q, vect(0,0,0))` 返回 NaN；`rotate_v(vect(), a)` 得到全 NaN 矩阵；`sphere_point(vect())` 得 NaN 点。全程不报错。
- 复现/验证：未复现。`vect<float> z; auto n = z.normal();` → NaN。
- 建议：`length() < preci` 时返回零向量或断言，并让上面的调用点自检。

### `nurbs::linspace(size)` 在 size ≤ 1 时除零 / 空 vector 取 `back()`
- 位置：`geometry.cpp:231-239`（`:234` `:237`）
- 现状：`step = (stop - star) / (size - 1)`：`size == 1` → 0/0 = NaN，返回 `[NaN]`；`size == 0` → `values` 为空容器，`values.back() = stop` 对空 vector 是 UB。
- 证据：本仓调用方都够大或已有 clamp：`fantasy/element.cpp:255-256`（`if (size < 10) size = 10;`）、`geometry.cpp:604` `:650` `:693` `:736`（`num_u - num_s + 1` = 48 或 9）。
- 影响：`nurbs` 是对外静态工具，外部调用方传 0/1 就 NaN/UB（Debug/ASan 下直接报错）。
- 复现/验证：未复现。`geometry::nurbs::linspace(1, 0, 1)` → `{NaN}`；`geometry::nurbs::linspace(0, 0, 1)` → UB。
- 建议：`size == 0` 返回空、`size == 1` 返回 `{star}`。

### `nurbs::find_span` 无下界校验，节点非单调时不收敛
- 位置：`geometry.cpp:187-204`（`:190` `:196`）
- 现状：`count n = knots.size() - p - 1;` 是 `size_t` 运算，`knots.size() < p + 1` 时下溢成巨值，随后 `knots[n]` / `knots[mid + 1]` 越界读；`while (u < knots[mid] || u >= knots[mid + 1])` 在节点非单调或 `u` 为 NaN 时不会收敛 → 挂死。
- 证据：函数入口只有 `if (p == xhqm::npos) return 0;`（`:189`），没有 `knots.size() >= 2*(p+1)` 之类的校验，也没有单调性断言。
- 影响：节点向量少写几个元素 / 未排序 / 传进 NaN，表现是越界读或死循环，而不是报错。
- 复现/验证：未复现。`geometry::nurbs::find_span(values{0,1}, 3, 0.5f)` → `n = 2 - 3 - 1` 下溢。
- 建议：入口校验 `knots.size() >= 2 * (p + 1)` 并断言单调，`u` 先 `std::clamp` 到节点区间。

### `surface::creat_point` / `creat_derivative` 对空节点向量取 `.back()`
- 位置：`geometry.cpp:419-420`、`:339`、`:379`、`:1237-1238`
- 现状：这些入口只判了阶数与控制点，没判 `m_nurbs_d[0]` / `m_nurbs_d[1]` 非空；`std::vector::back()` 在空容器上是 UB。
- 证据：`curve::creat_derivative`（`:1180`）判了 k 与点列、`surface::creat_derivative`（`:1234-1235`）也判了，但两处都没判节点向量。
- 影响：曲面/曲线只设了控制点（或只设一半）时求值 → UB（崩或垃圾值）。
- 复现/验证：未复现。`geometry::surface s; s.nurbs_s(3); s.nurbs_t(3); s.nurbs_p({{poit(), poit()}}); s.creat_point(0.f, 0.f);`
- 建议：四个入口统一加 `m_nurbs_d[0].empty() || m_nurbs_d[1].empty()` 早退。

### `surface::index_curve` 无下标校验
- 位置：`geometry.cpp:300-332`（`:308` `:309` `:321` `:325`）
- 现状：`m_nurbs_p[ind]`、`m_nurbs_w[ind]`（`is_h = true`）与逐行的 `vector[ind]`（`is_h = false`）都不判断 `ind` 是否越界；`m_nurbs_w` 默认是空（`geometry.h:511`），没设权重时必越界。
- 影响：`index_curve(999)`、或对没设权重的曲面取等参曲线 → 段错误或静默垃圾曲线，无断言。
- 复现/验证：未复现。`geometry::create_sphere_8(1.0f).index_curve(99)`。
- 建议：加 `ind >= m_nurbs_p.size()` 早退（返回空 curve），权重为空时按 1 填充。

### `create_sphere_2` 的参数域在极点处折叠（四个变体里唯一）
- 位置：`geometry.cpp:662-663`（`values_lat = linspace(num_u, -pi/2, pi/2)`、`values_lon = linspace(num_v, 0, pi)`）对比 `_1`（`:616-617`）、`_4`（`:705-706`）、`_8`（`:748-749`）
- 现状：`_2` 用 `z = radius * cos(values_lat)`（`:670`），lat ∈ [-π/2, π/2] 时 z 从 0 升到 radius 再回 0 → 极点落在 **u = 0.5 的内部参数行**，两条边界（u=0/u=1）分别落在赤道的 y≤0 / y≥0 半圈。`_1/_4/_8` 的 z 对参数单调、极点都在参数边缘。
- 证据：`_2` 的像集仍是 {z ≥ 0} 半球（所以「1/2 球面」的命名没错），但 `lat(1-u) = -lat(u)`，与 `v → 1-v` 合成后是 y 反射，即整片曲面在 u=0.5 处折一下；`surface_grid(nu, nv)`（`:1318-1342`）会在该行给出 nv 个重合顶点，相邻四边形塌成三角形（三角化后每对里有一个零面积）。
- 影响：用 `_2` 出网格/法线/面积的人拿到退化三角形与重合顶点（去重得自己做）；`_1/_4/_8` 不受影响。现有测试只在 `create_sphere_1` 的 u=v=0.5 取点（`test/algebra_test.cpp:553-561`），覆盖不到。
- 复现/验证：未复现（静态推导）。可跑 `./build/algebra_test "test_nurbs_derivative"` 确认现有覆盖；验证方法：对 `create_sphere_2(r)` 比较 `creat_point(0.4f, 0.3f)` 与 `creat_point(0.6f, 0.7f)` 是否只差 y 符号。
- 建议：`_2` 改成 lat ∈ [0, π/2] + lon ∈ [0, 2π]（同样覆盖 {z ≥ 0} 半球，极点落在 u=0 边缘，与 `_4/_8` 一致）。

### `surface::creat_normal` 的退化兜底在南极点反向
- 位置：`geometry.cpp:1307-1316`（兜底在 `:1313-1314`，返回 `vect(0,0,1)`）
- 现状：`create_sphere_1` 的参数化是 `z = radius * sin(values_lat)`（`:627`），u=0 → lat = -π/2 → 南极点 (0,0,-radius)。此处 `dv`（沿经度）因 `cos(lat) = 0` 而为 0（同时 `creat_derivative` 的 `W == 0` 早退，`:1303` 返回零向量）→ 叉乘为 0 → 走兜底返回 **(0,0,1)，与真实外法线 (0,0,-1) 反向**。同一兜底在北极点（u=1）恰好正确。
- 证据：`_2/_4/_8` 的参数域里 u=0 对应 z = +radius（`:670` `:713` `:756`），所以只有 `_1` 撞上南极点反向。
- 影响：用 `create_sphere_1` 极点法线做光照/碰撞，南极一圈朝里。
- 复现/验证：未复现。`geometry::create_sphere_1(1.0f).creat_normal(0.0f, 0.5f)` → 返回 (0,0,1)，而该参数处的点是 (0,0,-1)。
- 建议：兜底改成按该点位置定方向（`(p - center).normal()`），或至少按极点符号取 `(0,0,±1)`。

### `nurbs::base_funs_deriv` 缺零除保护（与 `base_funs` 行为不一致）
- 位置：`geometry.cpp:1126`（`value temp = ndu[r][j - 1] / ndu[j][r];`）
- 现状：`base_funs` 同一步做了保护 `denom == 0 ? 0 : N[r] / denom`（`:223`，旁边还留着旧的 `preci_v` 版本注释），导数版直接除。
- 影响：重节点/退化节点向量下，导数路径产出 inf/NaN，而基函数路径给 0 —— 两条路径语义不一致；调用方（曲线/曲面导数、`creat_normal`）拿到 NaN 也不报错。
- 复现/验证：未复现。直接调 `geometry::nurbs::base_funs_deriv(knots, 3, u, span, 1)`，`knots` 取全重复（如 `{0,0,0,0,1,1,1,1}`）。
- 建议：与 `base_funs` 共用同一个 `denom == 0` 分支。

### `matx::det` / `matx::invmat` 用**绝对**阈值判奇异 → 小尺度矩阵静默失败
- 位置：`linear.h:211`（`if (max_val < xhqm::preci<type>) return 0;`）、`linear.h:289`（`return matx(0, 0);`）
- 现状：判据是主元绝对值的下限 `preci<float> = 1e-5f`，与矩阵量级无关。`scale(1e-6f)` 生成的 4×4 矩阵主元 1e-6 < 1e-5 → `det()` 返回 0、`invmat()` 返回 **0×0**（`adjmat()` = `invmat()*det()` 因此也是 0×0）。
- 影响：小尺度（毫米/微米/归一化后的坐标）变换矩阵求逆静默失败；接着 `matx(0,0) * poit` 撞 `dimensions[0] != 4` 抛异常（`linear.h:619`），或 `matx(0,0) * matx` 静默给空（见本组第 2 条）—— 报错点离根因很远。
- 复现/验证：未复现。`matx<float> m = scale(1e-6f); auto inv = m.invmat();` → `inv.dimensions() == {0,0}`。
- 建议：奇异判据改相对阈值（如 `preci * 该列最大元`），或返回状态码而不是空矩阵。

### `plane::plane_grid(half, div = 0)` 产 NaN 顶点
- 位置：`geometry.cpp:869-877`（`:872-873`）
- 现状：`fi = value(i) / value(div)` 没有 `div == 0` 保护；`div = 0` 时 `nv = 1`，唯一顶点就是 `0/0` → NaN。
- 证据：同文件的 `surface::surface_grid` 对同样的除法做了保护（`(u_div == 0) ? value(0) : value(i) / value(u_div)`，`geometry.cpp:1325` `:1328`）。
- 影响：`plane_grid(1.0f, 0)` 得到 NaN 网格且不报错，下游渲染/求交全被污染。
- 复现/验证：未复现。`geometry::plane().plane_grid(1.0f, 0)` → `poits()[0]` 为 NaN。
- 建议：比照 `surface_grid` 加 `div == 0` 分支。

### `ray_sphere` 零方向向量返回 true 且 `t = NaN`
- 位置：`geometry.cpp:1075-1090`（`:1079` `:1085-1088`）
- 现状：`rd = 0` 时 `a = 0`、`disc = 0`，`t0 = (-b - 0) / 0 = NaN`、`t1 = NaN`；`t0 < 0 && t1 < 0` 恒 false（NaN 比较恒 false）→ 出参 `t = NaN` 且 `return true`。
- 影响：退化射线被当成命中，命中点算出 NaN；拾取/碰撞拿到 NaN 坐标继续往下算。
- 复现/验证：未复现。`geometry::value t = 0; ray_sphere(poit(0,0,-5), vect(), poit(0,0,0), 1.0f, t);` → `true`，`t` 为 NaN。
- 建议：入口 `a < preci` 直接 `return false`。

---

## B. 资源与并发

### `hyper::hypersphere` 用函数内 `static` RNG：跨线程数据竞争 + 结果依赖调用历史
- 位置：`geometry.cpp:820`（`static std::mt19937 gen(5489u);`）
- 现状：随机引擎是函数级 static（进程生命周期共享、跨调用续流），`std::mt19937` 不线程安全；同函数的 `std::normal_distribution<double> dist(0.0, 1.0);`（`:821`）反而是每次新建。
- 影响：(a) 两个线程同时调 `hypersphere` → 数据竞争（UB：崩、重复点或垃圾值）；(b) 同一入参两次调用结果不同，且取决于中间被谁调用过多少次 → 采样不可复现，回归/数值基线做不了。
- 复现/验证：未复现。多线程各自调 `geometry::hyper::hypersphere(3, 1000, 1.0f)` 比对结果（TSan 下会直接报竞争）。
- 建议：改成局部 `std::mt19937`，种子做成可选参数（默认固定），需要并行时各线程各持一份。

### `hyper::hypercube` 没有维度上限：2^dim 顶点 + 2·dim·2^(dim-1) 索引
- 位置：`geometry.cpp:772-796`（`:776` `:777` `:784-785`）
- 现状：`nverts = 1 << dim`，顶点数随 dim 指数增长，`bounds` 里 2·dim 个面各存 2^(dim-1) 个索引；没有上限检查、没有提前拒绝。
- 证据：`dim ≥ 64` 时 `xhqm::count(1) << dim` 自身就是 UB（`count = size_t`，64 位，`head.h:218`）；实际会先撞内存：dim=20 约 10^6 顶点（每点 20 个 float + vector 头）≈ 100 MB 量级，dim=24 已经到「顶点 ~1.5 GB + 索引 ~3 GB」量级（按 float 4 B、索引 8 B 估算）。
- 影响：`hypercube(30)` 直接 bad_alloc / OOM-kill；把高维当“随便试试”的人很容易一次吃满内存。
- 复现/验证：未复现（估算见上）。`geometry::hyper::hypercube(30)`。
- 建议：加 `dim` 上限（例如 > 20 直接拒绝并报错），或改成惰性生成面。

### `sphere::sphere_grid` 的去重表用「容忍相等 + 精确哈希」的 `poit` 作键
- 位置：`geometry.h:50-64`（`poit_hash` 只哈希 x/y/z 的精确位值）+ `linear.h:674-679`（`poit::operator==` 用 `preci = 1e-5` 容差）+ 使用点 `geometry.cpp:126-142`
- 现状：`std::unordered_map<poit, size_t, poit_hash>` 的相等判定是 ±1e-5 容差、哈希是精确值 → 违反「相等必同哈希」的容器前提：位值相近但不同的两个点落进不同桶，`find` 找不到 → 同一位置被写成两个顶点。
- 证据：本算法产生的重合点都由同一条边的两端相加归一化而来（IEEE754 加法可交换 → 位值相同），所以当前不发作；一旦顶点来自别的路径（外部导入网格、两个 `grid` 相加、加了 `m_poit` 之后再入表）就会漏合并。本仓唯一使用者 `sphere::sphere_grid` 目前既无调用方也无测试（`grep -rn "sphere_grid("` 只命中 `geometry.h:242` 与 `geometry.cpp:121`）。
- 影响：静默产生重合顶点/裂缝（拓扑计数、法线平均、焊接都受影响），且不报错。
- 复现/验证：未复现。可构造两点入表观察两条记录：`std::unordered_map<poit, std::size_t, poit_hash> m; m[poit(1,0,0)] = 0; m[poit(1 + 1e-6f, 0, 0)] = 1;`（`operator==` 认为相等，哈希不同 → 表内两条）。
- 建议：要么只用精确相等，要么把容差并进哈希（如按 1e-5 量化后哈希）。

### `extract_rote` 的自引用初始化（当前不发作的写法隐患）
- 位置：`linear.h:905-907`
- 现状：`vect<type> row0 = row0.row(matrix, 0).normal();`（row1/row2 同）—— 在变量自己的初始化式里用该变量调成员函数。这里的 `row()` 是 static 成员，不读对象，所以当前行为等价于 `vect<type>::row(matrix, 0).normal()`（功能正确）。
- 证据：同类调用 `vect<type> col0; col0.col(matrix, 0);`（`linear.h:920-922`）写成两步，只有 `extract_rote` 写成自引用；`poit::row` 没有调用方。
- 影响：当前不发作；一旦 `row()` 从 static 改成非 static（或编译器对自引用初始化告警升级为错误），就会真的读未初始化内存。
- 复现/验证：未复现（未在本机编译该文件）。
- 建议：改成 `vect<type>::row(matrix, 0).normal()` 显式静态调用。

---

## C. 跨平台

### `hyper` 的 4 个自由函数缺 `xhqm_io`（导出宏）→ MSVC 动态库下不导出
- 位置：`geometry.h:41-44`（`dimension` / `hypercube` / `simplex` / `hypersphere`，四个声明都没有宏）对照 `geometry.h:557-577`（`create_sphere*` / `ray_*` / `point_plane_distance` / `signed_volume` 全部带 `xhqm_io`）
- 现状：`xhqm_io` = `XHQMCLASS`（`head.h:27-30`）= MSVC 下 `dllexport`/`dllimport`，非 MSVC 下为空。`XHQM_DYNAMIC` 在 VS 工程里默认开（`xhqm.vcxproj:235` `:253` 的 `XHQM_DYNAMIC;XHQM_PROGRAM`），CMake 侧也支持（`CMakeLists.txt:584-587`）。因此 Windows DLL 里这 4 个函数既无导出标记、也不进 `xhqm.lib` 导入库 → 消费方链接期 `LNK2019`；Linux（`XHQMEXPORT` 为空）不受影响。
- 证据：`grep -c "xhqm_io" algebra/geometry/geometry.h` = 29，全部落在 shape 派生类、`nurbs`/`curve`/`surface`、`create_sphere*`、求交度量上；`hyper` 命名空间 4 个声明一处都没标。
- 影响：动态库使用者在 MSVC 上无法调用高维 4 函数（`test/algebra_test.cpp:16-25` 直接编源码，所以测试不暴露这个问题）。
- 复现/验证：未复现。VS 下建一个只定义 `XHQM_DYNAMIC`（不带 `XHQM_PROGRAM`）的消费工程调 `hyper::hypercube` → LNK2019。
- 建议：给这 4 个声明补 `xhqm_io`，并顺手扫一遍其它模块有没有同样的漏标。

### MSVC 工程文件里**完全没有**本模块的内容（与 CMake 核心清单不一致）
- 位置：`xhqm.vcxproj:22-26`（只列 `algebra\ctype.h`/`ftype.h`/`itype.h`/`mathf.h`/`value.h`）、`:84-86`（只列 `ctype.cpp`/`ftype.cpp`/`itype.cpp`）；`grep -n "geometry\|brep" xhqm.vcxproj xhqm.vcxproj.filters` → 0 命中
- 现状：CMake 的 `XHQM_HEAD_CORE` 含 `algebra/geometry/geometry.h`、`linear.h`（`CMakeLists.txt:222-223`），`XHQM_BODY_CORE` 含 `algebra/geometry/geometry.cpp`（`:254`，注释标明「核心（恒编译）」）；VS 工程一个都没写，`xhqm.sln:6` 里也只有一个 `xhqm.vcxproj` 工程。
- 影响：MSVC/VS 侧编不出 geometry 符号（用到它的模块只能靠 CMake 构建）；IDE 里本模块不进项目、无索引；两侧文件集不一致，容易再次出现「某平台能编、另一平台缺源文件」。
- 复现/验证：`grep -c "geometry" xhqm.vcxproj` → 0；CMake 侧 `cmake --build build-shared` 正常（本轮未跑）。
- 建议：把 `algebra/geometry/{geometry.h,linear.h,geometry.cpp}` 补进 vcxproj/filters 与 CMake 对齐（是否纳入 `brep/brep.h` 一并定）。

### `geometry.h:7` 的 `#undef hyper` 是永久摘宏（与 `metaclass/template.h` 的 push/pop 做法不一致）
- 位置：`geometry.h:7`；对照 `metaclass/template.h:80-88`
- 现状：Windows SDK `<rpcndr.h>` 把 `hyper` 定义成 MIDL 基类型宏（`#define hyper __int64`，`metaclass/template.h:80-82` 的注释有记载）。`template.h` 用 `#pragma push_macro("hyper") / #undef hyper / #pragma pop_macro("hyper")` 只在自己那段声明期间摘掉；`geometry.h:7` 是裸 `#undef hyper`，摘掉后**不恢复**。
- 影响：同一个 TU 里若在 `geometry.h` 之后引入依赖该宏的 SDK/MIDL 代码，宏已不存在；`template.h` 的 `pop_macro` 届时恢复的也是「未定义」状态（它的 push 已发生在本模块之后）。本机 Linux 头文件没有这个宏（`grep -rn "define hyper" /usr/include` 0 命中），**触发条件未实测**。
- 复现/验证：未复现（未确认的项）。
- 建议：改成与 `template.h` 相同的 push/undef/pop 三段式。

---

## D. 性能

### 默认 `create_sphere(radius)` 走 50×50 控制点的 `create_sphere_1`（其余变体是 11×11）
- 位置：`geometry.cpp:585-588`（`create_sphere(Radius)` → `create_sphere_1`）、`:595-596`（`num_u = num_v = 50`）对照 `:641-642` `:684-685` `:727-728`（`11 × 11`）
- 现状：默认球面曲面 2500 个控制点、每方向 54 个节点（`3 + (50-3+1) + 3`）；`_2/_4/_8` 是 121 个控制点、每方向 15 个节点。求值代价只与阶数（3×3）相关，差的是构造：2500 次 `poit` 构造 + 50 次逐行 `std::vector` 分配（`nurbs_p().resize(num_u, std::vector<poit>(num_v))`，`:614`）。
- 影响：所有 `create_sphere(radius)` 调用点（含渲染侧）都多付约 20× 的构造/分配；只想要一张球面占位时用 11×11 变体更省。
- 复现/验证：未复现（未做基准，规模对比来自上列常量）。
- 建议：默认改指 `_8`（或把控制点数做成参数）。

### `sphere::sphere_grid` 每顶点两次哈希查找、容器无预留
- 位置：`geometry.cpp:126-142`（`:130` `:134` `:138` 的 `find` 与 `:132` `:136` `:140` `:142` 的 `operator[]`）
- 现状：每个三角形的每个顶点先 `find` 再 `operator[]`（后者会再哈希一次、必要时插入），即每顶点 2 次哈希、每三角形 6 次；`unordered_map` 与 `poits()`/`faces()` 都没有 `reserve`。
- 影响：resolution 越小越明显 —— 半径 1、resolution 0.01 时叶子三角形约 2.9×10^5（按 `4π / (0.43 · resolution²)` 估算），对应约 1.7×10^6 次哈希 + 反复扩容。
- 复现/验证：未复现（规模为估算）。
- 建议：用 `try_emplace`/迭代器把「查一次、写一次」合一，并按估算三角形数 `reserve`。

### `circle::circle_points` 在循环内重建旋转矩阵
- 位置：`geometry.cpp:99-105`（`rotate_v(axis, theta)` 在 `for` 里，`:104`）
- 现状：`theta` 已在循环外算好，但每点仍调一次 `rotate_v`——内部要做 `axis.normal()`（含 `sqrt`）并构造整个 4×4 矩阵，然后只用来乘一个点。
- 影响：`count` 越大越浪费（`circle_grid`/`frustum_grid` 都走这条路径，frustum 一次调两遍 `circle_points`）。
- 复现/验证：未复现（无基准）。
- 建议：把矩阵提到循环外算一次，循环内只做 `point * matrix`。

---

## E. 未实现 / 占位桩

### `hyper` 只有第 0 层：凸包 / Voronoi–Delaunay / 双描述 / 组合拓扑都没有
- 位置：`geometry.h:31-45`（只有 4 个声明）、`geometry.cpp:765-839`（只有 4 个实现）
- 现状：本文件 README 的「高维泛化探索路线」把第 1/2/3 层写作「规划中 / 远期」，代码侧确实只有超立方 / 单纯形 / 超球三个生成器；`grep -rn "hull\|voronoi\|delaunay\|convex" algebra/geometry` → 0 命中。
- 影响：把「N 维泛化」当成高维几何工具集来用的人会扑空（没有凸包就没有高维相交/最近点/体积的入口）。
- 复现/验证：`grep -rn "hull\|voronoi\|delaunay" algebra/geometry`（0 命中）。
- 建议：按 README 的层表推进第 1 层，先做凸包 + 单纯形重心坐标。

### `hyper::simplex` 的 `bounds` 不是边界（1 条全顶点索引，`hypercube` 是 2·dim 个面）
- 位置：`geometry.cpp:808-812`
- 现状：`simplex(3)` 返回 `bounds[0].size() == 1`、`bounds[0][0].size() == 4`；3-单纯形的边界本应是 4 个三角形面（`hypercube(3)` 已经按 2·dim = 6 个四边形面给出对应结构）。**编译器会过、只是没写完**。
- 证据：`test/algebra_test.cpp:431-434` 断言了现状（`second[0].size() == 1` 且 `[0][0].size() == 4`），所以这不是回归，而是「把半成品钉住了」。
- 影响：按 `hypercube` 的约定读 `bounds` 的消费者（遍历面、数面、算欧拉特征）在 `simplex` 上拿到的是错的面结构。
- 复现/验证：`./build/algebra_test "test_hyper"`（当前会通过，因为断言写的就是现状）。
- 建议：生成 dim+1 个 (dim-1) 维面（每个去掉一个顶点），并同步改测试。

### BRep 只有结构：无造型、无布尔、无遍历/邻接、无序列化实现
- 位置：`brep/brep.h:16-69`（`node_id`/`wire` 两个别名 + 7 个 struct；`brep/` 下**没有 .cpp**）
- 现状：本模块内只有结构声明，唯一使用点是 `test/algebra_test.cpp:5`（include）与 `:668-703`（`test_brep_wire` 只用到 `vertex`/`edge`/`wire`）；`grep -rniE "extrude|revolve|sweep|loft|fuse|boolean" algebra/geometry` 除文档外 0 命中。**编译器会过、只是没接上**。
- 影响：`brep/README.md` 的「阶段划分」小节里四-A…四-D 只有线框结构本身落地，造型与布尔都没有；别把 brep 当可用的拓扑层。
- 复现/验证：`grep -rn "brep::" --include=*.h --include=*.cpp .`（只有测试与 `brep.h` 自身）；`grep -rniE "extrude|revolve|fuse" algebra/geometry`（除文档 0 命中）。
- 建议：细节与逐条进展见 `brep/DEBUG.md`（E 组），本文件不再重复。

### `geometry::mesh`（体网格）没有生成器
- 位置：`geometry.cpp:26-50`（只有 `mesh operator+`）、`geometry.h:181-200`（只有存取器）
- 现状：模块内没有任何产生 `m_cubes` 的算法 —— `plane/box/cylinder/cone/torus/capsule/quad` 都产出 `grid`（面网格），没有 `*_mesh()`；消费侧只有 `fantasy/element.h:98` 的 `show_mesh`。**编译器会过、只是没接上**。
- 影响：想要体网格（四面体/六面体剖分）的人无路可走；`mesh` 目前是「数据结构 + 拼接」。
- 复现/验证：`grep -rn "geometry::mesh" --include=*.h --include=*.cpp .`（只有 `operator+` 与 `show_mesh`）。
- 建议：README 的图元表里把 `mesh` 标为「无生成器」，或先给 `box` 加 `box_mesh()`。

### `frustum::angle()` / `m_angle` 设了没用：`frustum_grid` 从不读它
- 位置：`geometry.h:251` `:264` `:271`（字段与访问器）对照 `geometry.cpp:157-184`（`frustum_grid` 只读 `m_radiusT/m_radiusD/m_count/m_centerT/m_centerD/m_vectorA`）
- 现状：`m_angle` 默认 0，全仓没有第二处引用（`grep -rn "m_angle" algebra/geometry` 只有这三行）。**编译器会过、只是没接上**。
- 影响：以为设了 `angle()` 能得到「扇形/扭转棱台」的人，拿到的是与不设完全相同的网格。
- 复现/验证：`grep -rn "m_angle" algebra/geometry`。
- 建议：要么实现（按角度扭转），要么删字段并在 README 注明。

### `cube`（四点图元）没有 `*_grid()`，全仓无调用方
- 位置：`geometry.h:131-163`（四个点 + `type()` 返回 "cube"）
- 现状：既没有 `cube_grid()`（对比 `quad::quad_grid`，`geometry.h:456`），也没有文档条目；全仓 `grep -rn "\bcube\b"` 在模块内只剩定义（`test`/`fantasy` 命中都是「立方体阴影」等无关名词）。**编译器会过、只是没接上**。
- 影响：形状体系里多一个拿不出网格、也没人用的类型；按 `shape*` 泛化遍历/序列化时会遇到它。
- 复现/验证：`grep -rn "geometry::cube" --include=*.h --include=*.cpp .`（0 命中）。
- 建议：补 `cube_grid()`，或在 README 的形状表里标「未接网格」。

### `grid` / `mesh` 无相等性、无序列化
- 位置：`geometry.h:165-200`（只有 `operator+` 两个 friend 声明）
- 现状：`grep -nE "operator==|serialize|json" algebra/geometry/geometry.h algebra/geometry/geometry.cpp` → 0 命中；网格只能靠逐字段比，也没有序列化接口（BRep 侧同样没有，见本组第 3 条）。**编译器会过、只是没接上**。
- 影响：网格的回归基线、缓存/资源去重、跨进程传递都没有现成手段。
- 复现/验证：`grep -nE "operator==|serialize" algebra/geometry/geometry.h`（0 命中）。
- 建议：先补 `grid` 的 `operator==`（顶点 + 面逐元素，容差用 `preci`）。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### 行主序 + 行向量右乘，与 GLSL/OpenGL 的列主序相反：上传前必须 `tramat()`
- 位置：`linear.h:63-70`（元素 `(i,j) = data()[i*cols + j]`）、`:843-845`（平移写 (0,3)/(1,3)/(2,3)）、`:434-463`（`vect*matx` 要求行向量、`matx*vect` 要求列向量）
- 为什么现在这样是对的：内核里所有变换（`translate`/`rotate_x/y/z/v`/`scale`/`perspective`/`ortho`）共用同一个约定，链式写法 `translate * rotate_z * scale` 与数学书写顺序一致，内部自洽；与图形 API 的差异集中在渲染上传层一次性转置即可（`fantasy/fantasy_bugs.md:184` 明确记录了「上传 GLSL std140 前必须 `tramat()`」，且 drawer 的 transform/bones/lightVP 路径都这么做）。
- 代价：每个上传点都要记得转置，漏了就是静默的转置错误（画面上很难看出是矩阵约定问题）；`vect*matx` 与 `matx*vect` 两套语义并存，且 `matx*vect` 会先算列向量再 `transposed()` 后返回行向量（`linear.h:461` `:643`），容易误用。

### 几何无状态：形状不持有位置，位置由调用方/`fantasy` 持有
- 位置：`geometry.cpp:144-147`（`sphere_grid` 先算局部网格、最后整体加 `m_poit`）、`:992`（torus 同样）
- 为什么现在这样是对的：`curve`/`surface` 只描述形状 → 可共享、可实例化，摆放交给渲染层（`fantasy/element.cpp:544-561` 用 `poit * matrix` 逐点变换后 `show_grid`）；BRep 的 `edge::crv`/`face::geom` 也是裸指针引用几何，靠 uuid 定身份（`brep/README.md:14`、`:35`）。
- 代价：几何模块里“有位置”和“没位置”两类类型混着（`circle`/`sphere`/`box` 存 `m_poit`，`curve`/`surface` 不存），使用者要自己判断；拾取/碰撞要么在局部空间算，要么像 `sphere_grid` 那样临时复制点集再整体平移。

### `grid::faces()` 是变长索引表，`frustum_grid` 会混排 3 顶点与 4 顶点面
- 位置：`geometry.h:16-17`（`topol = std::vector<index>`）、`geometry.cpp:175-181`（同一个循环里 push 3 索引面与 4 索引面）
- 为什么现在这样是对的：面用变长索引表表达，三角形/四边形/多边形都能装；`fantasy` 的绘制正是按「连续同顶点数」分批（`fantasy/element.h:82` 的 `_draw_runs_` 注释），四边形的棱台网格靠这个消费（`fantasy/element.cpp:564-572` 的 arrow 就是两个 `frustum_grid()` 相加后直接 `show_grid`）。
- 代价：任何「一条面 = 3 个索引」的假设都是错的（逐面 `ray_triangle`、法线累加、面积求和都要按顶点数分派）；模块内也没有「四边形拆三角形」的工具函数，需要的人得自己写。

### `sphere::sphere_grid(value& resolution)` 用非 const 引用回传 clamp 后的分辨率
- 位置：`geometry.h:242`、`geometry.cpp:121-125`
- 为什么现在这样是对的：`resolution < 0`（约定为“自动”）时函数内部算出 `m_radius / 10` 并写回调用方变量，调用者事后能看到实际用的值，省一个有出参的结构。
- 代价：形参不能接受字面量/临时量（`s.sphere_grid(0.05f)` 编译不过，必须先给变量，README 的示例就是这么写的）；`create_sphere` 内部还会再 clamp 一次（`geometry.cpp:515-516`，≤0 抬到 `max(0.001, Radius/10)`），这一次**不会**回流 —— 变量里的值与真正用的值仍可能不一致。

### 几何值类型固定为 `float`（`value = xhqm::real = float`，精度 `preci<float> = 1e-5f`）
- 位置：`head.h:230`、`algebra/value.h:39` `:43`；`geometry.h:15`（`using value = xhqm::real`）
- 为什么现在这样是对的：与渲染侧 `fantasy::value` 同精度，几何数据可直接喂 GPU，省一次转换；`poit` 是 4 分量齐次、float 下正好 16 字节。同时 `measure = double` 留给需要双精度的模块（`algebra/value.h:58`）。
- 代价：大坐标/小尺度场景精度吃紧（世界坐标到 1e5 后 float 的分辨率约 0.01）；容差是**绝对**值（`preci = 1e-5`），小尺度几何会被误判相等或奇异（见 A 组的 `det/invmat`）；`hypersphere` 采样也只把 double 结果落成 float 存（`geometry.cpp:829` `:834`）。

### `linear.h` 的变换函数在头里声明为 `static`（内部链接）
- 位置：`linear.h:709` `:733` `:751` `:768` `:784` `:801` `:839` `:851` `:858` `:872`（`perspective`/`ortho`/`rotate_*`/`translate`/`scale`）
- 为什么现在这样是对的：header-only 库，`static` 让每个 TU 各拿一份，绝不会跨 TU 撞 ODR；这些函数都是短小的内联候选，去重收益有限。
- 代价：每个包含 `linear.h` 的 TU 都生成一份（本模块被 `fantasy/fantasy.h`、`metaclass/template.h` 等广泛包含 → 实际是「每个 TU 一套变换函数」）；不能跨 TU 取这些函数的地址；将来若把它们放进 DLL 导出面，导出/导入语义会各写各的。

### `nurbs` 用 `k == npos` 表示「不是 NURBS，控制点即离散点列」的双语义
- 位置：`geometry.cpp:285-288`（`curve::creat_points` 在 `k == npos` 时直接返回控制点）、`:470-495`（`surface::creat_points` 的四种 k 组合）
- 为什么现在这样是对的：同一个容器既能当 NURBS 控制点、又能当离散网格点用，`fantasy` 的曲线/曲面绘制依赖这个行为（先判定 `nurbs_k() != npos` 再决定要不要写 UV 属性，`fantasy/element.cpp:264-270`、`:385-395`）。
- 代价：同一个 k 值在 `creat_point`（连续求值）与 `creat_points`（离散返回）上行为完全不同 —— 前者还会死循环/越界（见 A 组第 1 条）；调用方必须自己判 k，容易漏。

### `create_sphere` 依赖 `std::queue` 的 deque 引用稳定性（边读边 push）
- 位置：`geometry.cpp:518-522`（`std::queue<face> Triangles;` + `geometry::face& triangle = Triangles.front();`）、`:535-538`（读取该引用后继续 `push` 4 个子三角形）
- 为什么现在这样是对的：`std::queue` 默认底层是 `std::deque`，`push_back` 不失效已有元素的引用，所以可以就地改象限符号（`:545-578`）再 push 8 份，省一次 `face` 拷贝。
- 代价：这个安全性完全依赖底层容器是 deque —— 谁把 `std::queue<face>` 换成 `std::vector`/`std::stack`（或自己在循环里 `pop` 后继续用 `triangle`），立刻变成悬垂引用。代码里没有注释说明这一点。

---

## G. 文档与代码的出入

### 根 `README.md` 说本模块「缺单测基线 / 缺求交·度量·NURBS 单测与数值基线」——实际已有 9 个用例
- 位置：根 `README.md` 的 §2.5 algebra/geometry 段「劣势」「建议」两行（本轮读到的工作副本版本在 `:162` / `:164`；该文件正被并行修改，引用行号可能漂移，以小节名为准）；实际覆盖见 `test/algebra_test.cpp:163-191` `:361-405` `:407-414` `:416-455` `:457-488` `:490-535` `:537-567` `:668-703`；`test/CMakeLists.txt:16-25` 把 `../algebra/geometry/geometry.cpp` 编进 `algebra_test`
- 现状：`grep -n "^TEST_CASE" test/algebra_test.cpp` 共 24 个用例，几何相关 9 个（线性/几何基础、圆朝向、create_sphere clamp 回归、poit_hash、hyper、face 度量+求交、七图元网格数、NURBS 导数与 surface_grid、BRep wire）；`test/README.md` 的 `algebra_test` 行也写明覆盖 `linear / geometry`。
- 影响：按旧结论安排工作会重复投入「补单测」；真正缺的是 `sphere_grid`（无调用方无测试）、`create_sphere_2/4/8` 的几何断言（只断言控制点数 > 0）、极点/退化输入用例（A 组多条都属这类）。
- 复现/验证：`cd test && cmake -B build -G Ninja && cmake --build build && ./build/algebra_test`（Catch2 支持 `"test_primes_grid"` 之类的过滤；本轮未跑）。
- 建议：把根 README 这条改成「测试存在但覆盖不均：sphere_grid 无覆盖、退化/极点未覆盖」。

### `poit::row()` 把 `[3]` 置 0，与本文件「poit 存 [3]=1」的约定不符
- 位置：`linear.h:652-662`（`:660` `pot[3] = 0;`）对照 `linear.h:518`（构造函数 `[3] = 1`）与本文件「坐标与矩阵约定」节；`poit::col()` 则保留 `[3]=1`（`linear.h:663-672`）
- 现状：`poit::row()` 返回的“点”其实是 w=0 的方向量语义，与 `vect` 的 w 相同；`poit::row`/`poit::col` 目前**没有任何调用方**（`grep -rn "\.row(\|\.col("` 只命中 `linear.h:898-929` 的注释与 `vect` 用法，`extract_rote` 用的是 `vect::row`）。
- 影响：谁的代码用 `poit::row()` 取矩阵行再当点用（减点得向量、平移、`sphere_point`），就会带上 w=0 的语义混淆；现在没人用，所以是文档/约定层面的坑。
- 复现/验证：`grep -rn "poit::row\|poit::col" --include=*.h --include=*.cpp .`（0 命中）。
- 建议：约定里写清「`row/col` 取的是矩阵元素、不是齐次点」，或让两者都按 w=1 返回。
