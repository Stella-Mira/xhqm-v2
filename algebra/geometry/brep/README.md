# brep 边界表示拓扑层

> 最后同步：2026-10-07，对照工作副本 r677。

参考 OCC TopoDS 的 BRep 边界表示，复用 ~~`lattice` 的图/环结构、`geometry` 的曲线曲面、`network` 的 uuid/json~~ **已作废（2026-10-07 复核 r677）**：实际只用两处——`lattice/ring_list.hpp`（wire 的环）与 `network/uuid.h`（节点身份）；`brep.h` 全文 74 行只 include 三个头 `../geometry.h`、`../../../lattice/ring_list.hpp`、`../../../network/uuid.h`（brep.h:6-8），json 未被本模块引用（只是经 `head.h:48` 传递包含）。

成熟度：**低（数据结构声明层）**——74 行里只有 7 个结构体（含基类 `node_base` 与辅助 `orient`）+ 2 个类型别名（`node_id`/`wire`）、没有一个函数，缺容器与 id 索引、遍历/求值、造型/布尔、网格化、序列化。当前问题清单见 [DEBUG.md](DEBUG.md)。

## 定位与分层

| 层 | 职责 | 载体 |
|---|---|---|
| 几何层 | 形状的曲线/曲面/顶点（局部坐标，无位置） | `geometry::poit/curve/surface` |
| **拓扑层（本模块）** | BRep 六层拓扑 + 邻接 + 身份 | `lattice::ring_list` + `network::uuid` |
| 渲染层 | 摆放 + 绘制（世界矩阵/实例化） | `fantasy`（`m_world`/`m_instances`） |
| 数值层 | 高维运算/分解 | `tensor`（按需映射） |

（上表「邻接」目前只是字段：`vertex.edges`/`edge.faces`/`face.shells`/`shell.faces`/`solid.shells` 都是空 `std::vector<node_id>`（brep.h:27/38/56/62/68），没有任何写入或校验代码——见 DEBUG.md E-3。渲染层列已核对存在：`fantasy::system::drawer_world`（fantasy/system.h:88）、`fantasy::drawer::m_world`（fantasy/drawer.h:181）、`fantasy::prefab::m_instances`（fantasy/drawer.h:326）。）

**无 Location**：位置变换与实例化由 `fantasy` 负责（`drawer_world` + `m_instances`），几何层保持纯粹。

## 六层映射

| BRep 层 | 维度 | 几何载体 | 拓扑载体 |
|---|---|---|---|
| Vertex | 0 | `poit` | 节点（uuid 身份） |
| Edge | 1 | `curve` | 节点（from/to + 参数 + 相邻面） |
| Wire | 1 | — | `ring_list<edge*, orient>`（边环） |
| Face | 2 | `surface` | 节点（外环 + 内环） |
| Shell | 2 | — | 面集合（共享边） |
| Solid | 3 | — | 壳集合（闭合） |

（r677 实测：六类的「拓扑载体」全部只是结构体字段；本命名空间只有 `node_id`/`wire` 两个类型别名（brep.h:16/48），`wire` 的行为全部来自 `ring_list`。）

## 关键决策

1. **Edge 是独立节点**（非顶点对）：支持多重边、共享边、非流形，贴近 OCC。
2. **Wire 用 `ring_list`**：环形链表天然表达闭合环，`edge_data` 承载方向。
3. **方向属于引用，不属于对象**：`orient` 挂在 wire 的环边数据上——一条 Edge 被两个 Face 引用时方向相反。
4. **UUID 定身份**：节点带 `cipher::uuid`，~~序列化存 uuid 而非指针（跨文件稳定）~~ **已作废（2026-10-07 复核 r677）**：本模块没有任何序列化代码，可用的只有 uuid 自身的 `to_text()`（network/uuid.h:75，静态版 uuid.h:83）与 `to_uuid()`（uuid.h:85）。身份确为 UUID——`node_base::id`（brep.h:20）。
5. **几何/拓扑分离**：Edge 引用 `curve`、Face 引用 `surface`，几何无状态可共享。
6. **CBOR 二进制序列化**：~~`network/json.hpp`（nlohmann）的 `to_cbor/from_cbor`，两遍法重建引用~~ **已作废（2026-10-07 复核 r677）**：`xhqm::geometry::brep` 里没有任何函数，CBOR 两遍法一行都没有（实测 `serialize(solid{})` 报 `'serialize' was not declared in this scope`）；`json.hpp` 只是传递包含进来的。见下节与 DEBUG.md E-1。
7. **无 Location**：见定位与分层。
8. **共享与一致性**：~~节点指针即身份~~ **已作废（2026-10-07 复核 r677）**：身份是 `node_base::id`（uuid，brep.h:20），指针只出现在几何载体 `curve*`/`surface*`（brep.h:35/53）与 wire 的节点数据 `edge*`（brep.h:48）。一个 Edge 被多个 Face 共享时 `edge.faces` 记录所有相邻面——该字段存在（brep.h:38）但**无任何写入代码**，一致性靠使用者手写（DEBUG.md E-3）。

## 类型草图

```cpp
namespace xhqm::geometry::brep {

using node_id = xhqm::cipher::uuid;

struct node_base {
    node_id id = xhqm::cipher::uuid::creat_uuid_v4();
};

struct vertex : node_base {
    poit position;
    std::vector<node_id> edges;      // 共享此顶点的边
};

struct edge : node_base {
    node_id from, to;                // 端点
    curve* crv = nullptr;            // 几何曲线（局部坐标）
    value t0 = 0, t1 = 1;            // 参数区间
    std::vector<node_id> faces;      // 相邻面（coboundary）
};

struct orient { bool reversed = false; };

using wire = ring_list<edge*, orient>;   // 边环：节点=edge 指针，环边数据=方向

struct face : node_base {
    surface* geom = nullptr;
    wire outer;                      // 外环
    std::vector<wire> inner;         // 内环（洞）
    std::vector<node_id> shells;
};

struct shell : node_base { std::vector<node_id> faces; };
struct solid : node_base { std::vector<node_id> shells; };

}
```

（与实际代码一致；实际写法是 `namespace xhqm { namespace geometry { namespace brep {` 嵌套（brep.h:10-15），需要 C++17——CMakeLists.txt:19 与 test/CMakeLists.txt:8 都设了 `CMAKE_CXX_STANDARD 17`。）

## 序列化（CBOR，两遍法）

**已作废（2026-10-07 复核 r677）**：下面这段里的 `serialize`/`deserialize` 在代码里不存在（`brep.h` 全文无函数定义，实测编译报 `'serialize' was not declared in this scope`），`#include "../network/json.hpp"` 的路径也不对——从本模块目录到 network 是三层（正确形式见 brep.h:8 的 `../../../network/uuid.h`）。保留旧文仅为记录当时设想：

```cpp
#include "../network/json.hpp"
using nlohmann::json;

json serialize(const solid& s);      // 结构 → json（引用存 uuid 文本）
solid deserialize(const json& j);    // json → 结构（先建节点映射，再回填引用）

// 二进制：
auto bytes = json::to_cbor(serialize(s));
auto back   = deserialize(json::from_cbor(bytes));
```

两遍法：写时先落节点（含 uuid），引用字段存 uuid；读时先建 `uuid → 节点` 映射，再按 uuid 回填引用（环引用安全）。

现状（r677）：可用原料只有 `cipher::uuid::to_text()` / `to_uuid()`（network/uuid.h:75/83/85，`uuid_less` 在 uuid.h:98）与传递包含进来的 nlohmann（`head.h:43-50`，未定义 `XHQM_JSON_EXTERNAL` 时用仓内 `network/json.hpp`）。

## 阶段划分

| 子阶段 | 内容 | 依赖 |
|---|---|---|
| 四-A | Vertex/Edge/Wire 线框拓扑 + 序列化 | 本模块 |
| 四-B | Face/Shell/Solid 面体拓扑 + 邻接/边界遍历 | 四-A |
| 四-C | 造型：Extrude/Revolve/Sweep/Loft | 四-B |
| 四-D | 布尔运算：Fuse/Common/Cut/Xor | 四-B + 求交补全 |

**状态（2026-10-07 实测 r677）**：四-A 只落地 `vertex`/`edge`/`wire` 三个类型（brep.h:24-28/31-39/48），序列化 0 行；四-B 只落地 `face`/`shell`/`solid` 三个类型（brep.h:51-69），邻接/边界遍历 0 行，且这三类**全仓无任何实例化点**——唯一使用点是 test/algebra_test.cpp:668 的 `test_brep_wire`，只用 `vertex`/`edge`/`wire`；四-C/四-D 全仓 grep 无 `extrude|revolve|sweep|loft|fuse` 等符号，0 行（见 DEBUG.md E-4/E-5）。

## 复用清单

| 模块 | 复用 |
|---|---|
| `lattice` | `ring_list`（边环）、`bind_mesh`（邻接）、`level_tree`（连续存储/序列化） |
| `geometry` | `poit/curve/surface`（几何载体）、`matx`（变换，渲染层用） |
| `network` | `uuid`（身份）、`json.hpp`（CBOR 序列化） |
| `tensor` | 拓扑代数（邻接矩阵/同调，按需映射） |
| `fantasy` | 位置变换 + 实例化（渲染层，本模块不涉及） |

（r677 实测：上表**只有 `lattice::ring_list`（brep.h:7）与 `network::uuid`（brep.h:8）真的被用**，`geometry` 一侧用的是 `poit`/`curve`/`surface`（brep.h:26/35/53）；`grep -n "bind_mesh|level_tree|matx|json|tensor|fantasy" brep.h` 0 命中——这些符号在各自模块里都存在（lattice/bind_mesh.hpp、lattice/level_tree.hpp、geometry.h:22、network/json.hpp），但本模块没有引用。上表是「打算复用」的清单，不是现状。）

## 怎么用（最小可运行示例）

头文件路径与链接依赖：

```cpp
#include "algebra/geometry/brep/brep.h"   // 相对仓库根；test/algebra_test.cpp:5 用的是 "../algebra/geometry/brep/brep.h"
                                          // 头内自带相对 include，所以编译只需 -I 仓库根（test/CMakeLists.txt:14）
```

`brep.h` **不是**纯声明即可用：`node_base` 的默认成员初始化调用 `cipher::uuid::creat_uuid_v4()`（brep.h:20），实现在 `network/uuid.cpp`，而 `uuid.cpp` 又用到 `md5`（uuid.cpp:2/163），所以自己起工程时要把 `network/uuid.cpp` + `network/md5.cpp` 一起编入（test/CMakeLists.txt:23-24 就是这么列的）；只 include 不链接会在 `creat_uuid_v4`/`uuid_equal` 上缺符号。

下面这段与 `test/algebra_test.cpp:668-703` 的 `test_brep_wire` 同源（已用 `g++ -fsyntax-only -std=c++17 -I.` 实测通过）：

```cpp
#include "algebra/geometry/brep/brep.h"
#include <map>

using namespace xhqm;              // test/algebra_test.cpp:11 同款
using namespace geometry::brep;    // test/algebra_test.cpp:670 同款

int main()
{
    vertex v0, v1;                          // 默认构造即分配 uuid（brep.h:20）
    v0.position = geometry::poit(0, 0, 0);  // position 默认是 (0,0,0,1)（linear.h:512 三个默认实参全 0，第 4 分量固定 1 → linear.h:518）
    v1.position = geometry::poit(1, 0, 0);

    edge e;
    e.from = v0.id;      // 端点用 uuid 引用，不是指针（brep.h:33-34）
    e.to   = v1.id;
    e.t1   = 1;          // 参数区间默认 t0=0、t1=1（brep.h:36-37）；目前没有任何代码读它
    e.crv  = nullptr;    // 几何载体可选（brep.h:35）

    edge e2; e2.from = v1.id; e2.to = v0.id;

    wire w;                  // wire = ring_list<edge*, orient>（brep.h:48）
    w.push_next(&e);         // 节点数据是 edge*：edge 对象的生命周期由调用方负责
    w.push_next(&e2);
    // w.size() == 2

    w.edge_of_next().reversed = true;   // 方向挂在环边（loca）上，可读写（ring_list.hpp:185）
    for (auto* pe : w) { (void)pe; }    // 元素类型实测为 edge*&；环遍历回到起点即止（begin() 起于 curr，end() 是 nullptr 哨兵）

    std::map<node_id, vertex, cipher::uuid_less> index;   // uuid 无 std::hash，必须给比较器
    index[v0.id] = v0;                                    // std::unordered_map<node_id, ...> 编译失败

    return (w.size() == 2 && index.size() == 1) ? 0 : 1;
}
```

参数与取值要点（全部实测）：
- `node_id` = `xhqm::cipher::uuid`（16 字节 `std::array<unsigned char,16>`，brep.h:16 / network/uuid.h:21 / head.h:233）；`vertex v;` 就有随机身份，比较用 `==`/`!=`（uuid.h:60/65），`to_text()` 出文本（uuid.h:75）。
- `geometry::value` = `xhqm::real`（geometry.h:15）；`edge.t0/t1` 默认 `0`/`1`（brep.h:36-37）。
- `wire` 的可用成员来自 `ring_list`：`push_prev/push_next`、`pop_prev/pop_curr/pop_next`、`size()`/`empty()`/`clear()`、`curr()`、`edge_of_prev()`/`edge_of_next()`、`begin()/end()`（lattice/ring_list.hpp:193-330、338-360）。
- 遍历顺序从 `curr`（最后 push 的那一项）开始沿环走，回到起点即结束——环没有「第一项」的概念（ring_list.hpp:203-206、229-241，迭代器终止条件在 112-124）。
- 索引只能 `std::map<node_id, T, cipher::uuid_less>`；要哈希表得自备 `std::hash`（uuid.h 无特化）。

## 构建与自检

- 本模块是**头文件、且不在任何构建清单里**：`brep.h` 未列入 `CMakeLists.txt` 的 `XHQM_HEAD_CORE`/`XHQM_HEAD`（CMakeLists.txt:209-248、543-549），也未出现在 `xhqm.vcxproj`/`xhqm.sln`/`test/CMakeLists.txt` 的源文件列表中——库靶子 `add_library(xhqm ... ${XHQM_HEAD} ${XHQM_BODY})`（CMakeLists.txt:588/593）不含它。
- 全仓唯一的使用点与自检程序：`test/algebra_test.cpp:668` 的 `TEST_CASE("test_brep_wire")`（Catch2，`test/catch.hpp` 随仓）。跑法：

  ```
  cmake -S test -B <构建目录>            # test/ 是独立工程，自带 project()（test/CMakeLists.txt:7）
  cmake --build <构建目录> --target algebra_test
  <构建目录>/algebra_test test_brep_wire
  ```

  **本轮未运行**（只做只读核对）：`g++ -fsyntax-only -std=c++17 -DXHQM_PROGRAM -I. test/algebra_test.cpp` 退出码 0，即整份测试 TU 在 GCC 11.4/Linux 下语法与语义检查通过；`test_brep_wire` 的实际执行结果未验证。

## 当前问题清单

见 [DEBUG.md](DEBUG.md)——只记当前仍存在的问题，不含已修项与版本历史（同批核对 r677）。
