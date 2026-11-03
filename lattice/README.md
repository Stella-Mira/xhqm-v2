# lattice 结构库

> 最后同步：2026-10-07，对照工作副本 r677。

链 / 树 / 网三族图结构 + 线程安全容器 + 图算法层。命名空间：`xhqm::_graph_`（基础与各族的 `*_node_` 模板）、`xhqm`（具体节点变体、迭代器、图算法、容器）。

模块定位：xhgqm 引擎的**图与层次数据模型**基础层——以统一「边类型系统」抽象出链表、树、图三类指针/连续结构，并提供编译期约束、迭代遍历与经典图算法，供上层渲染、网络、系统模块复用。

规模：20 个源文件 / 7996 行（`wc -l lattice/*.h lattice/*.hpp`，r677 实测）；全模块 header-only，无独立 .cpp。

## 目录

（行数为 r677 实测：`wc -l lattice/*.h lattice/*.hpp`）

| 文件 | 行数 | 内容 |
|---|---|---|
| `struct.h` | 250 | 基础：边的类型系统（`edge_dir` / `edge_type_` / `node_s` / `base_data_`） |
| `list.hpp` | 629 | 链表核心（`list_dir` / `list_node_` / `derive_list` / 迭代器） |
| `next_list.hpp` | 427 | 去向链表 `next_node`（仅记录下一项，`list_next`） |
| `prev_list.hpp` | 427 | 来向链表 `prev_node`（仅记录上一项，`list_prev`） |
| `bind_list.hpp` | 593 | 双向链表 `bind_list_node`（记录上下两项，`list_bind`） |
| `ring_list.hpp` | 414（末行无换行，`awk NR` 记 415） | 环形链表 `ring_node`（`list_bind` + 环形） |
| `tree.hpp` | 831 | 树核心（`tree_dir` / `tree_node_` / `derive_tree` / 迭代器 / 先中后序） |
| `child_tree.hpp` | 479 | 子向树 `child_node`（`tree_childs`） |
| `parent_tree.hpp` | 500 | 父向树 `parent_node`（`tree_parent`） |
| `bind_tree.hpp` | 415 | 双向树 `bind_tree_node`（`tree_bind`） |
| `level_tree.hpp` | 466 | 层级树（连续存储：0 模式 CSR 变长 / N 模式固定步长槽） |
| `mesh.hpp` | 613 | 网/图（`mesh_dir` / `mesh_node_` / `derive_mesh` / 关系判定 / 图算法） |
| `arrow_mesh.hpp` | 157 | 单向网 `arrow_node`（`mesh_oneway`）+ `arrow_mesh` 游标容器 |
| `bind_mesh.hpp` | 174 | 双向网 `bind_mesh_node`（`mesh_twoway`）+ `bind_mesh` 游标容器 |
| `directed_mesh.hpp` | 131 | 有向图 `directed_node`（区分「来连接」from /「去连接」goto） |
| `matrix_mesh.hpp` | 302 | 矩阵网 `matrix_mesh`（邻接矩阵，连续存储，支持代数运算） |
| `sparse_mesh.hpp` | 445 | 稀疏网 `sparse_mesh`（COO 三元组 + 惰性 CSR，大稀疏图专用） |
| `graph_algorithms.hpp` | 531 | 图算法层（拓扑排序 / SCC / WCC / 最短路 / MST / 最大流 / 匹配 / A* / 欧拉路径） |
| `queues.h` | 106 | 线程安全队列（FIFO） |
| `stacks.h` | 106 | 线程安全栈（LIFO） |

~~目录表曾缺 `directed_mesh.hpp` / `matrix_mesh.hpp` / `graph_algorithms.hpp`（旧结论称「原文件树缺失」）~~ **已作废（2026-10-07 复核 r677）**：三个文件都在 `lattice/` 下、都能编译并通过测试引用（`test/soul_graph_test.cpp:4-5` 引 directed_mesh + graph_algorithms；`test/structure_test.cpp:7` 引 matrix_mesh）。

**可达性（r677 实测，与本模块功能无关，但影响怎么 include）**：门面头 `xhqm/lattice` 收录了 16 个 lattice 头（不含 `directed_mesh.hpp` / `level_tree.hpp` / `matrix_mesh.hpp` / `sparse_mesh.hpp`），`CMakeLists.txt:232-237` 的 `XHQM_HEAD_CORE` 只列了 6 个（`stacks.h` / `queues.h` / `list.hpp` / `tree.hpp` / `mesh.hpp` / `directed_mesh.hpp`）。所以 `#include "xhqm.h"` 拿不到那 4 个头，要**直接 include** `lattice/xxx.hpp`（本文件示例一律用直接 include 路径）。

## 0. 怎么用（最小可运行）

**include 路径**：仓库根在 `-I` 里时用相对路径 `#include "lattice/xxx.hpp"`（`test/` 就是 `#include "../lattice/xxx.hpp"`）；四个不在门面里的头（`level_tree.hpp` / `matrix_mesh.hpp` / `sparse_mesh.hpp` / `directed_mesh.hpp`）必须显式 include，别指望 `#include "xhqm.h"` 带进来。

**编译**：全模块 header-only，C++17（`CMakeLists.txt:19`）。除了 `matrix_mesh.hpp` / `sparse_mesh.hpp` 的代数运算（`xhqm::matrix` / `tensor_base::sparse_matrix`，定义在 `algebra/tensor/` 里）之外，其余头文件只依赖 `head.h` + `algebra/value.h` + `metaclass/*`；需要链 tensor 后端符号时链 `algebra/ftype.cpp + algebra/tensor/backend/tensor_backend.cpp + tensor_backend_no.cpp`（实测：`XHQM_BUILD_TENSOR=OFF`（默认）与 `-DXHQM_DEVICE_NO` 两种宏变体下 `test/sparse_mesh_test.cpp` 都编得过、跑得过）。

**四条先记住的约定**：

1. **`xhqm::npos` 是「无/越界」哨兵**：定义在 `algebra/value.h:46`（`static_cast<xhqm::count>(-1)`，`count = size_t`）；`parent(i)`、`child(i,k)`、`adjacent_index()`、`shortest_path()` 等失败都返回它。判空请用 `== xhqm::npos` 而不是 `< 0`。
2. **迭代器解引用拿到什么，取决于节点数据是不是 `void`**：节点数据为 `void` 时 `*it` 给 `node*`，否则给节点数据的引用（`value_type = node* / node_data`，见 `list.hpp:406-410`、`tree.hpp:452-456`）。`std::distance`/`std::count`/range-for/`std::vector` 范围构造都可用（`test/structure_test.cpp:181-246` 实测 5 件套 traits 正确）。
3. **容器分「拥有型」与「非拥有型」**：链族（`next_list`/`prev_list`/`bind_list`/`ring_list`）与树族（`child_tree`/`parent_tree`/`bind_tree`）是拥有型——析构时递归 `delete` 全部节点（`clear()` 同义）；`arrow_mesh`/`bind_mesh` 是非拥有型游标容器——`add_node` 用裸 `new`、`reset()` 只清游标、**容器没有析构函数**，节点自己管（否则泄漏，`DEBUG.md` B1）。
4. **节点禁止拷贝/移动（指针即身份）**：`derive_*` 与各 `*_node` 都 delete 了拷贝/移动（`mesh.hpp:295-296`、`tree.hpp:408-409`、`directed_mesh.hpp:45-48`），`std::vector<node>` 这类写法不可用；图算法的 `std::vector<node*>` 必须是地址稳定的节点集合。

**跑测试**：`test/structure_test.cpp`（链/树/网/方向变体/图算法/traits，实测 70 断言 7 用例全过）需要链 `algebra/ftype.cpp`；`test/structure_extended_test.cpp`（四链 + 三树 + queues/stacks，实测 59 断言 10 用例全过）零依赖直接编。`test/sparse_mesh_test.cpp` 自述「仅编译，不链接」，且**没进 `test/CMakeLists.txt` 的 target**，要手工编（见 §4 稀疏网）。

## 1. 基础（struct.h）

### 边的类型系统

三族的「无向/单向/双向」本质是**边的类型**，统一由 `edge_dir` 承载：

| `edge_dir` | 语义 | 存储的指针 |
|---|---|---|
| `edge_null` | 无链接 | 不存指针（`from_loca`/`goto_loca` 为 bool 占位） |
| `edge_from` | 来连通 | 记录来源 `from_loca` |
| `edge_goto` | 去连通 | 记录目标 `goto_loca` |
| `edge_bind` | 双连通 | 记录来源+目标 `from_loca` + `goto_loca` |

- `base_data_<T>`：节点/边数据容器，`void` 特化为无数据。
- `edge_type_<node_loca, edge_data, edge_dir>`：边对象，按 `edge_dir` 四个特化。
- `node_s<node_type, node_data, edge_data, edge_dir>`：类型计算器，核心是

```cpp
loca_type = select_if<(edge_null == edge_dir), node_type, edge_type>::type;
```

即 **无链接时直接存节点指针，其余方向存边对象指针**（边对象记录来源/目标）。

## 2. 链（list.hpp + 四个变体）

`list_dir` 映射到 `edge_dir`：`list_null→edge_null`、`list_prev→edge_from`、`list_next→edge_goto`、`list_bind→edge_bind`。

| 类型 | 方向 | 说明 |
|---|---|---|
| `list_node_<node_type, node_data, edge_data, list_dir>` | 模板 | 链节点基础（CRTP） |
| `derive_list<node_type>` | list_null | 通用继承用链表（双向维护，直接存节点指针） |
| `next_node<data, edge>` | list_next | 去向链表，仅记录下一项 |
| `prev_node<data, edge>` | list_prev | 来向链表，仅记录上一项 |
| `bind_list_node<data, edge>` | list_bind | 双向链表，记录上下两项 |
| `ring_node<data, edge>` | list_bind | 环形链表 |

迭代器：`prev_iterator`（向上）、`next_iterator`（向下），支持 `++/--/+=/-=/*/->`（`+=`/`-=` 收 `xhqm::count`，另有 `+/-`）。别名 `list_iterator = next_iterator`（`list.hpp:561`）。

> ~~`list_iterator` 是链族的唯一通用别名~~ **已作废（2026-10-07 复核 r677）**：树族另有 `tree_pre_iterator` / `tree_mid_iterator` / `tree_bak_iterator`（`tree.hpp:739/743/747`，模板参数 `<树类型, index>`：`index=0` 先序、`index=1` 中序、`index=xhqm::npos` 后序），与 BFS 默认别名 `tree_iterator = tree_bfs_iterator`、`tree_dfs_iterator` 并列；四者都通过 `std::distance` 实测（`test/structure_test.cpp:213-217`）。

方向语义（重要）：`prev_list` 的遍历起点是「尾」（`begin()=stop`，向前走），因此 `push_back` 的新元素出现在遍历首位（实测：`prev_list{1,2,3}` 再 `push_back(4)`，`*begin() == 4`、`size() == 4`，遍历得 4,1,2,3）。这是来向链的方向特性，非缺陷。

## 3. 树（tree.hpp + 三个变体）

`tree_dir` 映射：`tree_nullptr→edge_null`、`tree_parent→edge_from`、`tree_childs→edge_goto`、`tree_bind→edge_bind`。

| 类型 | 方向 | 说明 |
|---|---|---|
| `tree_node_<node_type, node_data, edge_data, tree_dir, child_size>` | 模板 | 树节点基础（CRTP，child_size=0 无限邻接，否则 std::array 有限） |
| `derive_tree<node_type>` | tree_nullptr | 通用继承用树（父/子双向维护） |
| `child_node<data, edge>` | tree_childs | 子向树 |
| `parent_node<data, edge>` | tree_parent | 父向树 |
| `bind_tree_node<data, edge>` | tree_bind | 双向树 |

节点接口：`has_parent/degree/root_ptr/parent_ptr/child_ptr/childs`、`is_relatives/is_sibling/is_ancestor/is_parent_of/is_child_of/is_root/is_leaf`（`is_parent_of(ptr)`=我是 ptr 的父，`is_child_of(ptr)`=我是 ptr 的子）。遍历：`iterate_bfs/dfs`、先序 `iterate_pre`/中序 `iterate_mid`/后序 `iterate_bak`。迭代器：`tree_bfs_iterator`/`tree_dfs_iterator`，别名 `tree_iterator = tree_bfs_iterator`。

`bind_tree` 是既能向上也能向下移动的完整双向树：父/子为共享的双向边（`edge_bind`，`from_loca=父`、`goto_loca=子`），同一棵树自根可 `to_child`、自叶可 `to_parent` 双向溯源；支持 `push_child/push_parent/push_root`、`pop_child/pop_curr`、`edge_of_parent/edge_of_child`。

### 层级树（level_tree.hpp）

连续存储的紧凑树（层序布局），节点数据存于 `std::vector`，O(1) 下标访问：

| 模式 | `_child_size` | 布局 | 建树/删除复杂度 |
|---|---|---|---|
| CSR 变长 | `0` | `child_first` 前缀 + `child_list` 孩子序列 | 每次 `emplace_child` 都是 O(N)（孩子序列搬移 + **全量前缀重建**，`level_tree.hpp:183-195`）；`remove_leaf` O(N)（`level_tree.hpp:143-151` 同样重建前缀） |
| 固定步长 | `N` | `child_slots[i*N+k]`（npos=空槽） | `emplace_child` O(1)（找首个空槽）；`remove_leaf` 收缩槽 O(N)（`level_tree.hpp:129-136`） |

> ~~「无中间删除（clear 全清）」~~ **已作废（2026-10-07 复核 r677）**：`remove_leaf(i)` 已实现并可用（`level_tree.hpp:94-153`），两种布局各有一套下标重映射；仅叶子可删，删子树请后序逐个删（接口清单见下）。文件头 `level_tree.hpp:11-12` 仍是旧说法。

接口：`emplace_root/emplace_child`（`emplace_root` 仅空树可建、根恒为下标 0，重复调用返回 `xhqm::npos`；`emplace_child(parent, ...)` 父下标非法时返回 `npos`）、`remove_leaf`、`node/node_at/parent/parent_at`、`degree/child/childs/edge`、`is_root/is_leaf`、连续指针 `nodes/parents/child_first/child_list/child_slots`（只读视图，供序列化/外部消费）、遍历 `iterate_pre/mid/bak/level/bfs`（下标迭代，深树不爆栈，`iterate_level`/`iterate_bfs` 返回该层节点数/层数）、`reserve/clear`、下标迭代器（range-for 解引用得节点数据）。

复杂度小结：CSR 模式 = 建树 O(N²) 总量 / 遍历 O(子数) / `degree` O(1)；N 模式 = 建树 O(1) 每次 / 空间 O(N×child_size) / `degree` O(child_size) 扫描。**结构稳定、建好后高频遍历** 用 CSR；**固定叉数（四叉/八叉/骨骼）** 用定长槽。

## 4. 网 / 图（mesh.hpp + 变体）

`mesh_dir` 映射：`mesh_nullptr→edge_null`、`mesh_oneway→edge_goto`、`mesh_twoway→edge_bind`。

| 类型 | 方向 | 说明 |
|---|---|---|
| `mesh_node_<node_type, node_data, edge_data, mesh_dir, size>` | 模板 | 图节点基础（CRTP，size=0 无限邻接，否则有限） |
| `derive_mesh<node_type>` | mesh_nullptr | 通用继承用图（无向，直接存节点指针） |
| `arrow_node<data, edge>` | mesh_oneway | 单向网节点（有向边，仅记录目标），容器别名 `arrow_mesh` |
| `bind_mesh_node<data, edge>` | mesh_twoway | 双向网节点（无向边，bind edge 双向维护），容器别名 `bind_mesh` |
| `directed_node<data>` | —（独立实现） | 有向图节点，区分「来连接」与「去连接」（见下） |
| `matrix_mesh<node, edge>` | —（独立实现） | 邻接矩阵表示（见下） |
| `sparse_mesh<node, edge>` | —（独立实现） | COO 稀疏邻接 + 惰性 CSR 索引（见下） |

节点接口：`degree/out_degree/adjacent_size`、`is_isolated/is_reachable/is_connected`、`adjacent_ptr/adjacent_index/adjacent_list/adjacent_edge/in_adjacent`、`connected_size/shortest_path`、`iterate_bfs/dfs`。迭代器：`mesh_bfs_iterator`/`mesh_dfs_iterator`，别名 `mesh_iterator = mesh_dfs_iterator`（`mesh.hpp:544`）。`is_reachable` 返回「能否经有向邻接到达」，`is_connected` 是双向互相可达，`shortest_path` 返回无权 BFS 边数（不可达返回 `xhqm::npos`）。

自环：**指针网族态度不一（实测）**——`bind_mesh_node` 与 `derive_mesh` 显式拒绝自环（返回 false，`bind_mesh.hpp:43`、`mesh.hpp:568`），`directed_node::add_goto/add_from` 也拒绝（`directed_mesh.hpp:53/64`），但 **`arrow_node` 会接受自环**（`arrow_mesh.hpp:40-58` 没有自环判定；实测 `add_adjacent_ptr(&self)` 返回 true、`degree() == 1`）；`matrix_mesh` / `sparse_mesh` 的 `add_edge(i, i)` 合法（对角线 / COO 自环）。按类查而不是按族假设（见 DEBUG.md F7）。

`arrow_mesh`/`bind_mesh` 是游标容器：`head_ptr` 入口 + `curr_ptr` 游标，接口 `add_node(data, to_curr=false)`（首个节点自动成为游标）、`curr()`、`curr_adjacent(pos)`、`begin/end`（自 `head_ptr` 遍历）、`reset()`。`bind_mesh` 建边自动双向维护并拒绝自环。

> ~~「`reset` 不释放节点，由外部 graph 负责释放」是这两个容器唯一的资源注意点~~ **已作废（2026-10-07 复核 r677）**：`reset()` 只清指针（`arrow_mesh.hpp:152-153`、`bind_mesh.hpp:169-170`）**且容器没有析构函数**——`add_node` 用裸 `new`（`arrow_mesh.hpp:123/131`、`bind_mesh.hpp:140/148`），容器作用域结束即泄漏节点与其出边。LeakSanitizer 实测两族各泄漏（见 DEBUG.md B1）。它们是「非拥有型」容器，节点生命周期必须由调用方自己管（建议读完 B1 再用）。

### 有向图节点（directed_mesh.hpp）

`directed_node<_node_data>` 独立于 mesh_dir 体系，显式区分边方向（数据流 from → goto）：

- 建边双向登记：`add_goto(to)` 登记 `this` 的去连接 + `to` 的来连接；`add_from(from)` 反之（都是「已存在则不叠加」的幂等语义，`:54/65`）。
- `from_list()` 谁指向我（输入/上游）、`goto_list()` 我指向谁（输出/下游）；两者**按值返回 `std::vector<node_ptr>` 拷贝**，`adjacent_list()` 同样返回去连接的拷贝（`:113-115`），兼容图算法的拓扑排序。
- `in_degree/out_degree/degree`、`in_from/in_goto`、`is_source`（无输入）/`is_sink`（无输出）、`sub_goto/sub_from` 拆边。
- 正向沿去连接下游、反向沿来连接上游遍历，适合计算图正反向传播。

> ~~「独立实现」只说明它没走 mesh_dir~~ **已作废（2026-10-07 复核 r677）**：独立是**有意为之**——同一节点要同时持有入边表与出边表，而 `mesh_dir` 的一个节点只能有一种边方向（塞进去会变成第五种方向，污染所有 `if constexpr` 分派）。代价（当前实测）：**边无数据**（`directed_node<_node_data>` 只有一个模板参数，要带权得仿 `arrow_node` 自己扩），**不提供遍历/迭代器**（`iterate_bfs/dfs` 挂在 `mesh_node_` 上，它没有），**不进 `arrow_mesh`/`bind_mesh` 容器**。需要「有向 + 权重」时改用 `arrow_node<int, float>`（见 DEBUG.md F4）。

### 矩阵网（matrix_mesh.hpp）

`matrix_mesh<_node_data, _edge_data=bool>` 邻接矩阵表示：节点数据连续存储（`std::vector`），邻接矩阵由 `xhqm::matrix<_edge_data>` 承载（行优先，`m_matrix(i,j)` 表示 i→j 的边）。

- O(1) 查询任意两节点邻接/边；`add_node`（`emplace_node`）扩展矩阵 O(N²)、`remove_node` 收缩 O(N²)（`matrix_mesh.hpp:77-90` / `45-65`，都是整表搬移）。
- 接口：`add_node/emplace_node/remove_node`（`add_node` 返回新节点下标；`remove_node(i)` 后 i 之后的节点下标前移）、`add_edge/remove_edge`、`node/node_at/edge/edge_at/adjacent/adjacent_at`、`adjacent_list/out_degree/in_degree`、`reachable/reachable_component`、连续指针 `nodes()/matrix()/matrix_ref()`、迭代器（range-for 可用，解引用得节点数据）。
- `matrix_ref()` 暴露邻接矩阵引用，经 `xhqm::matrix` 获得代数能力（矩阵乘 = 路径数等），运算走 tensor 后端。实测 3 节点 0→1→2 的图 `matrix_ref() * matrix_ref()` 后 `(0,2) == 1`（路径数）。
- 移动语义：`matrix_mesh` 有显式移动构造/赋值，**移动后源对象 `size() == 0`**（`m_count` 被清零，`matrix_mesh.hpp:239-255`）。

### 稀疏网（sparse_mesh.hpp）

`sparse_mesh<_node_data, _edge_data=bool>` COO 稀疏邻接 + 惰性 CSR 索引：节点数据连续存储（`std::vector`），边为 COO 三元组（行=源 pre、列=目标 post、值）。

- 内存 O(nnz)（对比 matrix_mesh 的 O(N²)），`add_edge` O(1) 追加；`index()` 按 `(row,col)` 排序、合并重复边（bool 取或，数值累加）、建 CSR 行偏移与入度。实测：`add_edge(0,1,0.5f)` + `add_edge(0,1,0.3f)` 后 `nnz()==2`、`out_degree(0)==1`、`edge_at(0,1)==0.8f`。
- 出度/入度 O(1)（index 后）、邻接遍历 O(出度)、单边定位二分 O(log 出度)、`remove_edge` O(log 出度) 定位 + O(1) 尾交换删除、`remove_node` O(nnz) 全表压缩（`:147-164`）；需建索引的查询（度数/邻接表/可达/边引用）为非 const（惰性触发 index）。
- 接口：`add_node/emplace_node/remove_node`、`add_edge/set_edge/remove_edge`、`node/node_at/edge/edge_at/adjacent/adjacent_at`、`adjacent_list/out_degree/in_degree`、`reachable/reachable_component`、`for_each_edge`、`nnz/row/col/val/row_ptr/indexed/rebuild_index`、`to_sparse_matrix()`（导出 `tensor_base::sparse_matrix`，复用 SpMV 后端）、迭代器（range-for 可用）。注意 `edge(i,j)` 假定边存在（不存在时行为未定义，用 `edge_at` 才抛 `std::out_of_range`，见 DEBUG.md A4）。
- 适合连接组等大稀疏图；稠密代数（路径计数、det）仍用 matrix_mesh。
- 移动语义同 matrix_mesh：移动后源对象 `size() == 0`、`nnz() == 0`（`sparse_mesh.hpp:373-399`）。对应测试 `test/sparse_mesh_test.cpp` 自述「仅编译，不链接」，且当前**没有接进 `test/CMakeLists.txt` 的任何 target**。

## 5. 图算法（graph_algorithms.hpp）

自由函数，与结构族分离。**契约**：算法以「全节点集合 `std::vector<node*>`」为输入，节点需提供 `adjacent_list()`（有向邻接，出边）；带权算法通过 `weight(u, k)` 回调返回 u 第 k 条出边的权重（默认单位权 = BFS 语义）。

**`k` 的含义（重要）**：`k` 就是 `u->adjacent_list()[k]` 的下标（`graph_algorithms.hpp:182-186` 先取 `adj` 再按 `k` 索引），所以带权图直接用 `u->adjacent_edge(k)->data` 即可与算法对齐。**节点集合由算法用节点地址做键**（内部一律 `std::map<node*, ...>` / `std::set<node*>`），因此传入的必须是同一张图里的、地址稳定的节点。

| 算法 | 说明 | 复杂度/备注 |
|---|---|---|
| `topological_sort(nodes)` | 拓扑排序（Kahn） | 有环返回空（`order.size() != nodes.size()` 即判空返回） |
| `strongly_connected_components(nodes)` | 强连通分量（Kosaraju，迭代 DFS 防栈溢出） | 返回 `vector<vector<node*>>`；第二次 DFS 走反向图 |
| `weakly_connected_components(nodes)` | 弱连通分量（忽略边方向，并查集 + 路径压缩） | |
| `dijkstra(nodes, start[, weight])` | 非负权单源最短路 | 返回 `{dist, prev}`；**`dist` 只含可达节点**（起点 `0.0`，不可达者无条目，查询前先 `find`） |
| `floyd_warshall(nodes[, weight])` | 全源最短路（可负权无负环） | `dist[u][v]`：`u==v` 为 0、不可达为 `infinity`；初始化即建 N×N 条目 |
| `kruskal(nodes[, weight])` | 无向带权 MST（边排序 + 并查集） | 双向网重复边由并查集去重；返回 MST 边集（连通图 N-1 条） |
| `prim(nodes[, weight])` | 无向带权 MST（堆优化） | 起点取 `nodes[0]`；不连通时只覆盖 `nodes[0]` 所在分量 |
| `max_flow(nodes, s, t, capacity)` | 最大流（Edmonds-Karp，残量网络 BFS 增广） | `capacity(u,k)` 契约同 `weight`；残量按 `(u,v)` 对聚合，平行边累加；返回流值 |
| `bipartite_matching(left, right)` | 二分图最大匹配（匈牙利算法） | 边自 left 指向 right；返回匹配边对 `(left, right)` |
| `a_star(nodes, start, goal, weight, heuristic)` | 启发式最短路（启发须可采纳） | 返回 `{g, prev}`；`dist[goal]` 最优，其余节点是探索期部分值 |
| `euler_path(nodes)` | 有向图欧拉路径/回路（Hierholzer，度数判定） | 不存在返回空；**只认有向度数**，无向网（双向网）通常被判不存在 |

实测（r677 / g++ 11.4）：5 节点带权有向图 `dijkstra n0→n4 = 7.0`、`floyd` 同值、`max_flow = 5.0`、二分匹配 2 条边、`a_star = 7.0`、`euler_path` 长度 4（0 1 2 3）；无向三节点链的 `euler_path` 返回空（双向边的入度/出度各计一次，度数判定不接受）。

> ~~「现有：拓扑排序 / 强连通分量 / 弱连通分量 / Dijkstra / Floyd-Warshall；MST、最大流等未来扩展」（文件头 `graph_algorithms.hpp:8-9`）~~ **已作废（2026-10-07 复核 r677）**：**11 个算法全部已实现且实测正确**（上表），文件头注释还停在 5 个。**注意**：`matrix_mesh` / `sparse_mesh` 是「下标即身份」的图，**不参与本算法层**（它们各自实现了 `reachable`/`reachable_component`），要用经典算法得先转成指针图（见 DEBUG.md F5/F6）。

复杂度注意：算法内部一律用 `std::map<node*, ...>` 关联（单次查表 O(log N)），且每个节点都要 `adjacent_list()` 拷一份出边表；`floyd_warshall` 预建 N×N 条目。规模大时应视为比教科书复杂度多一个 `log N` 因子与可观常数（见 DEBUG.md D3/D4）。

## 6. 容器（queues.h / stacks.h）

`queues<type>`（FIFO）与 `stacks<type>`（LIFO），线程安全（每个公开方法一把 `std::mutex` + `std::lock_guard`）。

接口：查询 `size` / `empty` / `clear`；入 `set(const&)` / `set(&&)` / `set_if(fn, value)`；出 `get(type&)` / `get_if(fn, type&)` / `pop()` / `pop_if(fn)`（`get*` 移动取出并返回 bool，`pop*` 仅弹出；`set_if`/`get_if`/`pop_if` 的谓词是 `std::function<bool(const type&)>`）。

**非阻塞**：空容器上 `get`/`pop` 立即返回 `false`，没有条件变量、没有超时/等待接口——多线程生产-消费要自己轮询。此外**只有这两个容器有锁**，节点与其它容器一律无同步（见 DEBUG.md B4）。

## 7. 依赖

- 引擎内部（按文件实测）：`struct.h` 引 `../head.h`、`../algebra/value.h`（提供 `xhqm::npos`）、`../metaclass/compute.h`、`../metaclass/template.h`、`../metaclass/function/class_type.h`；`matrix_mesh.hpp` 额外引 `../algebra/tensor/matrix.h`；`sparse_mesh.hpp` 额外引 `../algebra/tensor/sparse.h` + `../algebra/mathf.h`；`parent_tree.hpp` 引 `ring_list.hpp`；`arrow_mesh.hpp`/`bind_mesh.hpp` 引 `mesh.hpp`，而 `mesh.hpp` 反向引 `graph_algorithms.hpp`（`mesh.hpp:14`，所以只 include `mesh.hpp` 也会拿到 11 个算法）。
- 标准库：`<vector> <map> <set> <queue> <stack> <mutex> <functional> <algorithm> <limits> <utility> <stdexcept> <iterator> <type_traits>`。
- 全模块 header-only（20 个 `.h`/`.hpp`，无独立 `.cpp`，7996 行）；`queues.h`/`stacks.h` 只依赖标准库，可脱离整个引擎（不引 `head.h`）单独 include。
- `matrix_mesh` / `sparse_mesh` 的代数运算要吃 tensor 后端符号：默认 `XHQM_BUILD_TENSOR=OFF`（`CMakeLists.txt:41`）时走 no 串行桩（`tensor_backend_no.cpp`，`CMakeLists.txt:266-269` 的官方注释：「张量 no 后端恒编入……等价于『TENSOR 开 + DEVICE=NO』的串行桩，matrix/tensor 运算可链接运行」），实测 `-DXHQM_DEVICE_CPU` 与 `-DXHQM_DEVICE_NO` 两种宏变体下都通过。

## 8. 设计模式

- **CRTP**：`derive_xxx` / `*_node` 通过继承自身实现静态多态，节点禁止拷贝/移动（指针即身份）。
- **模板元**：`select_if` / `compute` / `is_same` 在编译期计算存储类型（`loca_type`）；`mesh_dir`/`tree_dir`/`list_dir` → `edge_dir` 的映射由 `*_dir_value<>` 特化在编译期完成（`mesh.hpp:39-41`、`tree.hpp:40-43`、`list.hpp:38-41`）。
- **编译期约束**：非法组合用 `static_assert` 拦截——无路径存数据（`struct.h:94`「无路径时无法存储数据」）、`level_tree::edge()` 在无路径数据时报「level_tree 无路径数据」（`level_tree.hpp:289/301`）、未知 dock 类型的迭代器（`mesh.hpp:327`、`tree.hpp:459`）。
- **方向变体**：`next_node/prev_node/bind_list_node/ring_node`（链）、`child_node/parent_node/bind_tree_node`（树）、`arrow_node/bind_mesh_node`（网）是对应方向的现成节点类，统一继承 `xxx_node_<..., xxx_dir>`。
- **两套 `if constexpr` 分流策略**：连续家族按「布局」分流（`level_tree` 的 `_child_size == 0` 与否）、指针家族按「方向」分流（`_edge_dir`/`_tree_dir`），分派全在编译期。

## 9. 复杂度与选型

| 组件 | 中间插入 | 删除 | 空间 | 适合场景 |
|---|---|---|---|---|
| 链族（next/prev/bind/ring） | O(1) | O(1) | O(N) | 频繁增删、游标移动 |
| child_tree / parent_tree / bind_tree | O(1)（换父断链） | O(子树) | O(N) | 指针树、结构频繁变更 |
| level_tree（0 模式 CSR） | **O(N)**（前缀全量重建，每次 `emplace_child`） | 仅叶子 O(N) | O(N+边) | 建好后高频遍历、序列化 |
| level_tree（N 模式定长槽） | O(1)（找首个空槽） | 仅叶子 O(N) | O(N×child_size) | 固定叉数（四叉/八叉/骨骼） |
| matrix_mesh | **O(N²)**（矩阵扩展） | O(N²)（收缩） | O(N²) | 稠密图、邻接矩阵代数（路径计数） |
| sparse_mesh | O(1)（追加边） | 删边 O(log 出度)+尾交换；删点 O(nnz) | O(N+nnz) | 大稀疏图、连接组、SpMV 迭代 |
| derive_mesh / arrow_mesh / bind_mesh | O(1) | O(度) | O(N+边) | 稀疏图、遍历、图算法 |
| directed_node | O(度)（`*_index` 线性查重） | O(度)（vector erase） | O(N+边) | 计算图、正反向传播（无边数据、无遍历） |

- **成熟度：中高。** 三族抽象与 11 个图算法齐全、测试覆盖链/树/网主干；缺的是有向图的边数据与统一序列化 I/O，加上几处单点缺陷（父向树改挂父节点的槽位错位最要紧，见 `DEBUG.md` A1）。
- **连续存储（level_tree / matrix_mesh / sparse_mesh）与「频繁增删中间节点」互斥**：结构稳定、高频遍历选连续布局；频繁增删选指针树。
- **遍历回调风格**：树族（derive/child/parent/bind）`traverse`/`iterate_*` 回调收 `node*`（`child_tree.hpp:349`、`parent_tree.hpp:409`、`bind_tree.hpp:348`）；`derive_tree` 家族还可用自由函数 `xhqm::iterate_pre/mid/bak(node*, fn)`（`tree.hpp:556/571/599`，先/中/后序，显式栈不爆栈）；数组族（level_tree）`iterate_*` 回调收下标；matrix_mesh / sparse_mesh 按下标操作。
- **边数据类型**：`void`=无路径（`node_s` 里直接存节点指针，不建边对象）；bool=无权（GF(2) 代数）；char/int=精确路径计数与小权重；float=大权重（tensor 后端加速）。
- **越界契约**：`node()/edge()/adjacent()/parent()` 等 `[]` 风格接口不检查（归用户管理）；`node_at/edge_at/adjacent_at/parent_at` 检查并抛 `std::out_of_range`（标准库风格）。已知一处例外：`sparse_mesh::edge(i,j)` 在边不存在时会以 `npos` 取下标（行为未定义，见 `DEBUG.md` A4）。

## 10. 用法示例

以下示例全部取自真实 API 并**实测编译 + 运行通过**（r677 / g++ 11.4 / C++17；等价于仓库外单文件程序 `#include "lattice/..."`）。

### 通用继承用链（derive_list）

```cpp
#include "lattice/list.hpp"
class node : public xhqm::derive_list<node> { public: int id = 0; node(int i = 0) : id(i) {} };
node a(0), b(1), c(2);
a.set_next_ptr(&b);      // 挂接接口是 set_next_ptr/set_prev_ptr，没有 link()
b.set_next_ptr(&c);      // b 自动记录 prev=a，c 自动记录 prev=b
// a.next_ptr() == &b、b.prev_ptr() == &a、c.prev_ptr() == &b
// a.first_ptr() == &a、a.last_ptr() == &c、c.in_list(&a) == true
```

### 链表容器（next_list / prev_list）

```cpp
#include "lattice/next_list.hpp"
xhqm::next_list<int> nl = { 1, 2, 3, 4 };      // 初始化列表（有推导指引）
std::distance(nl.begin(), nl.end());           // 4（迭代器满足 forward_iterator traits）
nl.curr_star();                                // 游标回到首项
nl.curr();                                     // 1
nl.next();                                     // 游标后移一项，返回 bool
```

### 环形链表（range-for 合法，自动停在起点）

```cpp
#include "lattice/ring_list.hpp"
xhqm::ring_list<int> rl = { 1, 2, 3 };
std::distance(rl.begin(), rl.end());           // 3
int sum = 0; for (auto& v : rl) sum += v;      // 6（迭代器 ++ 走满一圈后判定 nullptr 收尾）
```

### 树（child_tree 容器 / derive_tree 继承）

```cpp
#include "lattice/child_tree.hpp"
xhqm::child_tree<int> t;
t.push_child(1);              // 空树：建根，curr = 根
t.push_child(2, true);        // 根的子节点，并把 curr 移到子
t.push_child(3, false);       // curr 的子节点，curr 不动
t.size();                     // 3
t.root();                     // 1
int cnt = 0;
t.traverse([&](xhqm::child_tree<int>::node_ptr) { ++cnt; });   // 回调收 node*，自 curr 深度遍历
// 注意：traverse 从 curr 出发（不是根）。上面 curr 停在节点 2，所以 cnt == 2；
//       想遍历整树先调 t.curr_root()（---> cnt == 3）
```

### 层级树（连续存储，CSR / N 叉两种模式）

```cpp
#include "lattice/level_tree.hpp"
xhqm::level_tree<int, void, 0> t;      // 第 3 个模板参数 0 = CSR 变长；填 4 就是四叉定长槽
t.emplace_root(0);                     // 仅空树可建，根恒为下标 0，返回 0
auto i = t.emplace_child(0, 1);        // 在父下标 0 下加孩子，返回新下标（i == 1）
t.emplace_child(0, 2);
t.degree(0);                           // 2；t.child(0, 3) == xhqm::npos（越界）
t.iterate_pre(0, [](xhqm::size idx) { /* 回调收下标，深树不爆栈 */ });
xhqm::level_tree<int, float, 0> te;    // 带边数据：edge(i, k) 与孩子序列平行
```

### 带权有向图 + 图算法（weight 的 k 就是 adjacent_list 的下标）

```cpp
#include "lattice/arrow_mesh.hpp"
#include "lattice/graph_algorithms.hpp"
xhqm::arrow_mesh<int, float> g;                 // 节点数据 int，边数据 float
auto* n0 = g.add_node(0);
auto* n1 = g.add_node(1);
auto* n2 = g.add_node(2);
n0->add_adjacent_ptr(n1);                       // 0→1
n1->add_adjacent_ptr(n2);                       // 1→2
n0->adjacent_edge(0)->data = 1.5f;              // 0 的第 0 条出边的权重
n1->adjacent_edge(0)->data = 2.5f;
std::vector<decltype(n0)> nodes = { n2, n0, n1 };// 全节点集合（乱序即可）
auto order = xhqm::topological_sort(nodes);     // 拓扑序；有环返回空
auto W = [](auto* u, xhqm::size k) { return u->adjacent_edge(k)->data; };
auto [dist, prev] = xhqm::dijkstra(nodes, n0, W);   // dist 只含可达节点，n2 == 4.0
```

### 有向图节点（directed_node，计算图正反向）

```cpp
#include "lattice/directed_mesh.hpp"
xhqm::_graph_::directed_node<std::string> a(std::string("a")), c(std::string("c"));
a.add_goto(&c);                     // 建边 a→c：a 记去连接，c 记来连接
a.out_degree();                     // 1（我指向谁）
c.in_degree();                      // 1（谁指向我）
a.is_source(); c.is_sink();         // true / true
a.adjacent_list()[0] == &c;         // 邻接表 = 出边，直接喂给图算法
```

### 线程安全队列

```cpp
#include "lattice/queues.h"
xhqm::queues<int> q;
q.set(42);
int out = 0;
if (q.get(out)) { /* out == 42，get 会移动取出 */ }
```

### 常见编译期报错对照（本模块历史清单，保留）

| 错误 | 根因 | 解法 |
|---|---|---|
| C7683 无法创建对 void 的引用 | 边数据 void 时返回 `void&`（`std::conditional` 两分支均实例化） | 用偏特化辅助（如 `level_tree_edge_ref`，`level_tree.hpp:36-43`） |
| C2248 无法访问 private typedef | 容器 typedef 区漏 `public:`（遮蔽基类） | 补 `public:` |
| C2675 一元 ++ 未定义 | 该迭代器缺 `operator++` | 补 `++/--`（链/树/网/环形四族迭代器现都有） |
| LNK2019 tensor_backend_active | 独立程序未链 tensor 后端 | 链 `algebra/tensor/backend/tensor_backend.cpp + tensor_backend_no.cpp` |
| LNK2019 float_16 构造 | tensor 侧引入 `algebra/value.h` 静态初始化 | 链 `algebra/ftype.cpp` |
| 未定义符号 `xhqm::size`/`count` | 只 include 了 `lattice/queues.h`/`stacks.h`（不引 `head.h`）却用了 `xhqm::size` | 加 `#include "head.h"`，或只用 `size_t` |

## 11. 当前问题清单

本模块**当前仍存在**的缺陷、资源/并发问题、未实现项与有意取舍，一律记在 [`DEBUG.md`](DEBUG.md)（A 功能性缺陷 / B 资源与并发 / C 跨平台 / D 性能 / E 未实现占位 / F 设计取舍 / G 文档与代码出入），本文件不重抄。

本文件里凡标 `~~旧文~~` + **已作废（2026-10-07 复核 r677）** 的段落，都是被 r677 代码推翻的旧说法，保留原文供接手者对照；结论一律以代码与 `DEBUG.md` 为准。
