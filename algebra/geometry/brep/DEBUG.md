# DEBUG —— algebra/geometry/brep 当前仍存在的问题

一句话定位：`brep.h`（74 行）只是 BRep 拓扑元素的**类型声明**——7 个结构体（`node_base`/`vertex`/`edge`/`orient`/`face`/`shell`/`solid`）+ 2 个别名（`node_id`/`wire`）、没有一个函数；本文件记录这份声明层里**现在仍然存在**的缺陷、缺口与取舍。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。

与同类文件的关系：本仓另外两份台账**不搬家、不改名**——`fantasy/fantasy_bugs.md`（带 ✅/严重度的历史 bug 清单，含已修项）与 `network/nebula/DEFERRED.md`（讨论过但没决定做的事）。本文件只管本模块「当下真实存在」的问题；本模块的用法与事实核对结果见同目录 [README.md](README.md)。

核对方式：只读源码 + `g++ -fsyntax-only`（从 stdin 读片段，**未新建任何文件**）。凡标注「未复现」的条目都只做了代码路径/类型层面的确认，没有运行期证据。

---

## A. 功能性缺陷（会算错 / 会崩）

### A-1 拷贝 `wire` 会静默丢掉全部方向（`orient` 全被重置为 false）

- 位置：`lattice/ring_list.hpp:393-397`（拷贝构造）、`lattice/ring_list.hpp:383-386`（迭代器区间构造）、`algebra/geometry/brep/brep.h:48`（`wire = ring_list<edge*, orient>`）、`brep.h:51-55`（`face` 按值持有 `wire outer` / `std::vector<wire> inner`）
- 现状：`ring_list` 的拷贝构造是 `ring_list(copy_.begin(), copy_.end())`，而区间构造只做 `push_next(*first_)`——只复制**节点数据**（这里是 `edge*`）。方向存在环边对象（`loca_type::data`）里，由 `ring_node` 的构造函数 `new loca_type()` 默认建成，拷贝路径**完全不经过它**。于是 `wire b = a;` 拿到的是新建节点组成的新环（顺序从 a 的 `curr` 起），但每条环边都是默认 `orient{reversed=false}`。
- 证据：`ring_list.hpp:383-397` 两段构造代码；方向唯一的读写口是 `edge_of_prev()/edge_of_next()`（`ring_list.hpp:164-190`），直接取 `curr_ptr->next_edge()->data`，与拷贝无关。
- 影响：任何 `face` 的按值传递/拷贝（`face` 含 `wire outer` 与 `std::vector<wire> inner`）都会把所有环方向清零 → 面定向/边方向判断静默反转，编译器不会有任何提示。反向使用同一条边的两个面，拷贝后方向全变 false。
- 复现/验证：**未复现**（需运行期）。验证片段：`wire a; a.push_next(&e); a.edge_of_next().reversed = true; wire b = a; assert(b.edge_of_next().reversed == true);` —— 按代码路径预期失败。类型层面本轮已实测：`wire` 拷贝构造可用、拷贝赋值被删除（见 A-3）。
- 建议：`ring_list` 拷贝构造补上环边数据；或在 brep 侧禁用 `wire`/`face` 的隐式拷贝，只留显式 `clone`。

### A-2 空 `wire` 上调用 `curr()`/`edge_of_next()`/`edge_of_prev()` 会解引用空指针

- 位置：`lattice/ring_list.hpp:164`、`171`、`178`、`185`（四个 `edge_of_*` 重载）、`211-212`（`curr()`）、`338-360`（`<<`/`>>`/`<`/`>` 运算符同样直接解 `curr_ptr`）、经 `algebra/geometry/brep/brep.h:48` 暴露给本模块使用者
- 现状：这些访问器一律 `curr_ptr->...`，没有 `empty()` 判断；而 `curr_ptr == nullptr` 正是 `wire w;`（默认构造）以及最后一个 `pop_*` 之后的状态。同一头文件里与之相邻的位移接口**是判空的**（`prev()` `ring_list.hpp:323-328`、`next()` `330-335` 都 `if (curr_ptr == nullptr) return false;`），只有取值接口不判。
- 证据：`inline node_data_ref curr() { return curr_ptr->data; };`（`ring_list.hpp:211`）；`edge_data_ref edge_of_next() { if constexpr (...) return curr_ptr->next_edge()->data; }`（`ring_list.hpp:185-189`，`empty()` 在其上 193 行就有，未被调用）。
- 影响：`wire w; w.edge_of_next().reversed = true;` 这类「先建环、后设方向」的自然写法会直接崩（UB）。现有测试只在 `push_next` 之后访问（`test/algebra_test.cpp:686-697`），所以没踩到。
- 复现/验证：**未复现**（需运行期）。片段：`wire w; auto& o = w.edge_of_next();`
- 建议：在 brep 侧约定「取值前先 `empty()`」，或上游给这些访问器加断言/判空返回。

### A-3 `wire` 与 `face` 不可拷贝赋值（编译期硬错误，不是静默错）

- 位置：`lattice/ring_list.hpp:146`（类）、`ring_list.hpp:363`（用户声明的 move 赋值）、`ring_list.hpp:398-404`（move 构造）、`algebra/geometry/brep/brep.h:51-57`（`face` 含 `wire outer` / `std::vector<wire> inner`）
- 现状：`ring_list` 自己声明了 move 构造与 move 赋值，于是 `operator=(const ring_list&)` 被隐式定义为 deleted；`face` 因为持有 `wire`，其隐式拷贝赋值也随之 deleted（拷贝构造仍然可用）。
- 证据：GCC 11.4 `-fsyntax-only` 实测两条报错——`use of deleted function 'constexpr xhqm::ring_list<edge*, orient>& ...::operator=(const ring_list&)'`（`<stdin>`，由 `w2 = w1;` 触发）与 `error: use of deleted function 'xhqm::geometry::brep::face& xhqm::geometry::brep::face::operator=(const face&)'`（`f1 = f1;` 触发，注明 "implicitly deleted because the default definition would be ill-formed"）。
- 影响：想把 `brep::face` 放进需要赋值的容器/算法（`std::vector<face>` 的拷贝赋值、`std::map` 的 `operator[]` 赋值等）会编译失败；`std::vector<wire>` 的拷贝赋值同理。`push_back`/`emplace_back`/`erase`（走移动）不受影响。
- 复现/验证：本轮**已复现**（零落盘）：

  ```
  cd /home/xhqm/DeepSeek/xhqm-v2
  echo '#include "algebra/geometry/brep/brep.h"
  using namespace xhqm::geometry::brep;
  int main(){ wire a,b; b = a; face f1,f2 = f1; f1 = f1; return 0; }' \
  | g++ -fsyntax-only -x c++ -std=c++17 -I. -
  ```
- 建议：上游补 `ring_list& operator=(const ring_list&)`（同时解决 A-1 的环边数据）；或本模块显式删除拷贝、只保留移动语义。

---

## B. 资源与并发

### B-1 所有权真空：几何与边都是裸指针，模块既不管释放也不防悬垂

- 位置：`algebra/geometry/brep/brep.h:35`（`geometry::curve* crv = nullptr`）、`brep.h:53`（`geometry::surface* geom = nullptr`）、`brep.h:48`（`wire = ring_list<edge*, orient>`，节点数据是 `edge*`）
- 现状：全模块没有析构、没有 `unique_ptr`/`shared_ptr`、没有 owning 容器；六个节点结构体都是可拷贝的值类型，拷贝时指针被浅拷贝。`brep.h` 里唯一的函数调用是默认成员初始化 `creat_uuid_v4()`（`brep.h:20`）。
- 证据：`grep -n "(" algebra/geometry/brep/brep.h` 只命中 `brep.h:20`；全文件无 `delete`/`unique_ptr`/`shared_ptr`/`~`。
- 影响：谁 own `curve`/`surface`/`edge`、活多久，全靠调用方自律；一次 `delete crv` 就让所有引用它的 `edge` 悬垂，且没有任何检测；把 `wire` 拷一份就得到两套共享同一批 `edge*` 的环（A-1 的姊妹问题）。
- 复现/验证：未复现（结构性事实，读代码即可确认）。
- 建议：先在 README/契约里写明「几何对象由调用方持有、`edge` 由外部容器持有」，或引入 owning 表 + 弱引用 id。

### B-2 模块内没有任何同步设施，也没有并发约定

- 位置：`algebra/geometry/brep/brep.h:27`、`38`、`56`、`62`、`68`（五处 `std::vector<node_id>` 邻接表）、`brep.h:18-21`（`node_base`）
- 现状：`grep -n "mutex\|atomic\|thread\|lock" brep.h` → 0 命中；节点是普通可拷贝值类型，邻接表是普通 `vector`。唯一与线程相关的行为在 UUID 一侧：`creat_uuid_v4` 用 `static thread_local std::mt19937_64`（`network/uuid.cpp:206`），即 id 生成本身是每线程一份引擎。
- 证据：上面的 grep 结果；`network/uuid.cpp:202-213`。
- 影响：多线程并行建拓扑时，哪怕改的是不同节点的 `faces`/`edges`，也需要调用方自己加锁；两个线程同时给同一个 `face` 追加内环就是数据竞争（无上限、无检测）。
- 复现/验证：未复现（读代码 + grep）。
- 建议：即使代码不动，也应在 README 明确「本模块不做同步，并发由调用方保证」。

---

## D. 性能

### D-1 每个节点默认构造都会实参求值一次 `std::random_device`

- 位置：`algebra/geometry/brep/brep.h:20`（`node_id id = xhqm::cipher::uuid::creat_uuid_v4();`）、`network/uuid.h:94`（形参默认值 `std::random_device seed = std::random_device{}`）、`network/uuid.cpp:206-213`（引擎是 `static thread_local`，只有**首次**调用才用 seed）
- 现状：UUID 引擎早已改成 thread_local（首次构造、用 4 个 `random_device` 值 + `high_resolution_clock` 混种），但默认实参是**调用点求值**的——于是每次 `vertex v;`/`edge e;` 都会额外构造（并析构）一个 `std::random_device`，而它的值从第二次调用起就被丢弃。
- 证据：`creat_uuid_v4` 的签名与实现（`uuid.h:94`、`uuid.cpp:202-213`）；调用点在默认成员初始化器（`brep.h:20`），任何节点的构造都会走到。
- 影响：节点量级上到百万时，每次多一次 random_device 构造/析构（libstdc++ 下是一次随机源打开/关闭的开销）；小规模无感。**具体耗时未实测。**
- 复现/验证：未复现（读签名+实现即可确认求值时机）。
- 建议：上游把 seed 改成惰性获取（或去掉默认实参），本模块无需改动。

### D-2 `wire` 每个元素固定两次堆分配，且无连续存储

- 位置：`lattice/ring_list.hpp:68-84`（`ring_node` 两个构造函数里都 `new loca_type()`）、`ring_list.hpp:215-270`（四个 `push_*` 各 `new list_node(...)`）、`ring_list.hpp:272-306`（`pop_*` 各 `delete`）、`algebra/geometry/brep/brep.h:48`
- 现状：`wire = ring_list<edge*, orient>`；插入一个元素 = 一个新节点 `new` + 一个新环边 `new`（`loca_type`，方向就存在这个独立对象里）；没有 reserve、没有对象池、没有连续存储；`clear()` 逐个 `delete`。
- 证据：上述构造与增删代码；`lattice/level_tree.hpp` 是仓内已有的「连续存储」方案（README:123 的复用清单也提到了它），但本模块没有用。
- 影响：面环/体环规模大时分配次数≈元素数×2，节点散布在堆上 → 遍历局部性差、分配器压力大；这也是本模块将来上量时最先要换的一环。
- 复现/验证：未复现（读代码）。
- 建议：上量后换连续存储方案（如 `lattice/level_tree.hpp`）或加对象池；本期不动。

---

## E. 未实现 / 占位桩

### E-1 序列化与反序列化 0 行（README 描述的那套 API 不存在）

- 位置：`algebra/geometry/brep/brep.h:74`（整个头文件就 74 行，无任何函数）、`README.md:42`、`README.md:88-102`
- 现状：`xhqm::geometry::brep` 内只有 7 个结构体（`node_base`/`vertex`/`edge`/`orient`/`face`/`shell`/`solid`）+ `node_id`/`wire` 两个别名，没有 `serialize`/`deserialize`、没有 `to_json`/`from_json`、没有任何 CBOR 调用。可用的原料只有 `cipher::uuid::to_text()`/`to_uuid()`（`network/uuid.h:75`、`85`；静态版 `to_text` 在 `uuid.h:83`）与传递包含进来的 nlohmann（`head.h:43-50`，未定义 `XHQM_JSON_EXTERNAL` 时用仓内 `network/json.hpp`）。
- 证据：`grep -n "(" algebra/geometry/brep/brep.h` 仅 `brep.h:20`；`grep -n "json\|cbor\|serialize" brep.h` 0 命中；实测 `serialize(solid{})` → `error: 'serialize' was not declared in this scope`。
- 影响：按 README 抄用法的人第一行就编译失败；「跨文件稳定引用」目前只是设计意图，没有落地路径。
- 复现/验证：本轮**已复现**（编译期负例，零落盘）：

  ```
  cd /home/xhqm/DeepSeek/xhqm-v2
  printf '#include "algebra/geometry/brep/brep.h"\nusing namespace xhqm::geometry::brep;\nint main(){ solid s; serialize(s); return 0; }\n' \
  | g++ -fsyntax-only -x c++ -std=c++17 -I. -
  # 实测输出：error: 'serialize' was not declared in this scope
  ```
- 建议：README 已按本仓风格标注作废；实现落地前不要再宣称已支持。

### E-2 没有容器、没有 id→节点索引，六类元素拼不成一个可用的拓扑

- 位置：`algebra/geometry/brep/brep.h:16`（`node_id`）、`brep.h:18-21`（`node_base`）、`brep.h:24-69`（六类拓扑元素的声明）
- 现状：模块只定义「元素长什么样」，没有任何聚合类型（无 `mesh`/`body`/`topology`/`shape`）；`node_id` 是 uuid，但没有 `id→node` 的表，也没有解析函数。更紧的一环：uuid **没有 `std::hash` 特化**（`network/uuid.h` 只提供 `uuid_less` 比较器，`uuid.h:98`），所以连索引容器都得挑着用。
- 证据：`std::map<node_id, vertex, xhqm::cipher::uuid_less>` 编译通过；`std::unordered_map<node_id, vertex>` 编译失败（`std::hash<xhqm::cipher::uuid>` 缺失，报 implicit deleted 构造）。两条都用 `-fsyntax-only` 实测。
- 影响：想从 `edge.from` 找顶点，必须自己维护表；漏了就是静默找不到 / 退化成 O(N) 线性扫描；本模块也没有「求值/遍历」的入口可调。
- 复现/验证：本轮**部分复现**（类型与容器可用性已实测；运行期解析路径无）：

  ```
  cd /home/xhqm/DeepSeek/xhqm-v2
  printf '#include "algebra/geometry/brep/brep.h"\n#include <map>\n#include <unordered_map>\nusing namespace xhqm::geometry::brep;\nint main(){ std::map<node_id,vertex,xhqm::cipher::uuid_less> a; a[vertex().id]=vertex(); std::unordered_map<node_id,vertex> b; return 0; }\n' \
  | g++ -fsyntax-only -x c++ -std=c++17 -I. -
  # 实测：map 那行通过；unordered_map 那行报 std::hash<xhqm::cipher::uuid> 缺失（implicit deleted 构造）
  ```
- 建议：先补 owning 容器（`id→节点` + 去重插入 + 反向邻接维护），再谈造型。

### E-3 没有遍历 / 邻接 / 边界 / 求值算法（邻接字段全靠手写）

- 位置：`algebra/geometry/brep/brep.h:27`、`38`、`56`、`62`、`68`（五处邻接 `vector<node_id>` 字段）、`brep.h:36-37`（`edge.t0/t1`）、`brep.h:54-55`（`face.outer/inner`）
- 现状：`vertex.edges`/`edge.faces`/`face.shells`/`shell.faces`/`solid.shells` 只是字段，没有写入、没有反向同步、没有闭合校验；没有 wire→face 定向、没有 shell 闭合检查、没有 solid 体积/包围盒；`geometry` 侧虽然有求值函数（`curve::creat_point` `geometry.h:499`、`surface::creat_point` `geometry.h:540-542`、`surface_grid` `geometry.h:549`），但 `edge.t0/t1` 从未被任何代码读取——参数区间目前是纯装饰。
- 证据：`grep -n "t0\|t1" brep.h` 只有 `brep.h:36-37` 两行声明；全仓 `brep::` 只有 `test/algebra_test.cpp:670` 一处使用。
- 影响：写进 `edge.faces` 之后没人维护反向的 `vertex.edges`，双向数据不一致是**默认状态**且无检测；`t0/t1` 有值但无人消费，容易被误以为已经在用。
- 复现/验证：未复现（grep + 读代码）。
- 建议：同 E-2（容器带上邻接维护），或至少在 README 标注「字段需自填」。

### E-4 造型（Extrude/Revolve/Sweep/Loft）与布尔（Fuse/Common/Cut/Xor）0 行

- 位置：`README.md:114-115`（阶段表四-C/四-D）、`README.md:117`（状态行）、`algebra/geometry/brep/brep.h:74`
- 现状：本模块与 `geometry` 侧都没有相关符号。
- 证据：`grep -n "extrude\|revolve\|sweep\|loft\|fuse" -r --include=*.h --include=*.hpp --include=*.cpp .` → 全仓仅 4 处命中，全是 `diffuse` 里的子串（`test/view/ModelLoader.h:138`、`fantasy/theory_render.h:207`、`fantasy/backend/shader_preprocess.cpp:126`、`fantasy/backend/shader_sources.h:166`），无任何几何侧实现。
- 影响：README 的阶段表容易被读成「已有规划中的实现」，实际一行都没有；把它当能力清单来做方案会踩空。
- 复现/验证：`grep -rn "extrude\|revolve\|loft\|sweep" --include=*.h --include=*.hpp --include=*.cpp .`
- 建议：无（属规划范畴，README 已补状态）。

### E-5 `face`/`shell`/`solid` 三类从未被任何代码实例化

- 位置：`algebra/geometry/brep/brep.h:51-69`
- 现状：全仓唯一使用点是 `test/algebra_test.cpp:668-703` 的 `TEST_CASE("test_brep_wire")`，只碰 `vertex`/`edge`/`wire`；`brep::face`/`brep::shell`/`brep::solid` 除定义外零引用（注意别与 `geometry::face`（`geometry.h:98` 的三角面图元，被 `test/algebra_test.cpp` 的几何用例大量使用）混淆，测试里出现在几何用例中的 `face` 都是后者）。
- 证据：`grep -rn "brep::\|using namespace geometry::brep" --include=*.h --include=*.hpp --include=*.cpp .` → 仅 `test/algebra_test.cpp:670`。
- 影响：这三类没有任何编译/运行验证；字段拼写、类型不匹配这类问题只能靠人读（本轮 `-fsyntax-only` 覆盖了它们的解析，但构造/使用路径仍无人跑）。
- 复现/验证：grep；`g++ -fsyntax-only -std=c++17 -DXHQM_PROGRAM -I. test/algebra_test.cpp` 退出码 0（只证明能编过，不证明这三个类型被用过）。
- 建议：补一个最小自检（构造 + 字段往返 + 一条 wire 挂到 face 上）。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F-1 拓扑只存几何指针（`curve*`/`surface*`），不存几何值

- 位置：`algebra/geometry/brep/brep.h:35`、`brep.h:53`
- 为什么现在这样是对的：几何是无状态、可共享的（一个 `curve` 可以被多条 `edge` 引用；换几何载体——NURBS 改网格——不用动拓扑）；拓扑节点保持小且拷贝便宜；这与 OCC「几何/拓扑分离、几何可共享」的做法一致。
- 代价：生命周期与所有权完全外置（B-1），拷贝语义是「共享」而不是「深拷」，且 `nullptr`（`brep.h:35/53` 的默认值）是合法状态——一个「没有几何」的边与「几何被删了」的边无法区分。
- 状态：保持。要改先得有 owning 容器（E-2）。

### F-2 身份用 UUID，不用指针

- 位置：`algebra/geometry/brep/brep.h:16`、`brep.h:20`；比较器在 `network/uuid.h:98`
- 为什么现在这样是对的：跨文件/跨进程/跨机稳定引用，是序列化、去重、网络同步、持久化的前提；指针做身份一旦落盘就失效。README:44 已订正「节点指针即身份」的旧说法。
- 代价：解析引用必须有外部索引（E-2），`unordered_map` 需自备 `std::hash`，每节点多 16 字节，且引用失去编译器可达性检查。

### F-3 方向属于引用（`orient` 挂在 `wire` 的环边/loca 上），不是 `edge` 的成员

- 位置：`algebra/geometry/brep/brep.h:42-45`（`orient`）、`brep.h:48`（`ring_list<edge*, orient>`）、`lattice/ring_list.hpp:164-190`
- 为什么现在这样是对的：同一条件 Edge 被两个 Face 反向使用时方向不同，方向天然属于「此次引用」而不是对象——这正是 OCC `TopoDS_Shape` + `Orientation` 的语义；环边（loca）恰好为「引用级数据」提供了位置。
- 代价：方向不在遍历元素里（元素是 `edge*`，方向要另取 `edge_of_next()/edge_of_prev()`），任何环的拷贝/重建都必须显式搬运环边数据，否则静默清零（A-1）；一条 wire 也只保存一份方向。

### F-4 `node_base::id` 在构造时即生成（默认成员初始化）

- 位置：`algebra/geometry/brep/brep.h:20`
- 为什么现在这样是对的：任何节点天生有稳定身份，插入容器无需额外仪式，没有「忘了设 id」的中间状态；测试第一件事就是断言 `v0.id != v1.id`（`test/algebra_test.cpp:681-682`）。
- 代价：每个节点一次 UUID 生成（D-1）；无法表达「空身份」（虽然 `uuid` 有 `operator bool` 可以判全零，但本模块不会产生全零 id）；**拷贝节点会连 id 一起复制**——两个对象同 id，若同时插进同一个 id 索引会撞键（这一点必须在容器层自己防）。

### F-5 本模块只做数据契约，不做算法

- 位置：`algebra/geometry/brep/brep.h:18-69`（只有数据成员）
- 为什么现在这样是对的：先稳住跨模块的引用格式（uuid + 几何指针），与 `geometry`/`fantasy` 解耦；上层（渲染/网格化）可以按自己的节奏接。
- 代价：容器、遍历、求值、造型、布尔、网格化、序列化全都在本层缺失（E-1~E-4），BRep 现阶段只能当「结构体字典」用；README 的阶段表容易被误读为已完成（已补状态行，README:117）。

### F-6 `wire` 用双向环链表（`ring_list`）而不是连续存储（`level_tree`）

- 位置：`algebra/geometry/brep/brep.h:48`、`lattice/ring_list.hpp:146`、对照 `lattice/level_tree.hpp`
- 为什么现在这样是对的：环天然闭合（无需哨兵判断首尾），增删 O(1)，而且**环边（loca）正好提供了挂「引用级数据」的位置**——`orient` 需要它（F-3），连续存储方案反而要多设计一列 parallel array。
- 代价：每元素 2 次堆分配、无 reserve、局部性差（D-2）；`ring_list` 的拷贝不带环边数据（A-1）；遍历起点是 `curr` 而不是「第一项」（环无首项，遍历顺序取决于最后 push 的位置，`ring_list.hpp:203-206`）。

---

## G. 文档与代码的出入

### G-1 README「序列化（CBOR，两遍法）」整节描述的 API 不存在（含一处错的 include 路径）

- 位置：`README.md:88-106`（作废标注在 `README.md:90`，旧文代码块 `README.md:92-102`，错的路径在 `README.md:93`）
- 现状：`serialize(const solid&)` / `deserialize(const json&)` 在代码里不存在；旧文写的 `#include "../network/json.hpp"` 相对本模块目录也不对——从 `algebra/geometry/brep/` 到 network 是三层，`brep.h:8` 的写法是 `../../../network/uuid.h`。
- 证据：实测 `serialize(solid{})` 报 `'serialize' was not declared in this scope`；`brep.h:6-8` 三个 include 的实际形态。
- 影响：照抄 README 必编译失败。
- 复现/验证：见 E-1 的命令。
- 建议：已于本轮在 README 标注作废并保留旧文，实现后再改回。

### G-2 README 旧文「节点指针即身份」与代码矛盾

- 位置：`README.md:44`（已订正）、矛盾点 `algebra/geometry/brep/brep.h:20`、指针的真身 `brep.h:35`/`53`/`48`
- 现状：身份是 `node_base::id`（uuid），指针只出现在几何载体与 `wire` 的节点数据上。
- 证据：`node_id id = xhqm::cipher::uuid::creat_uuid_v4();`（`brep.h:20`）。
- 影响：接手的人可能拿 `edge*` 当键使用，一旦落盘/跨进程就失效。
- 复现/验证：读代码即可。
- 建议：README 已订正；同类口径冲突另见 G-5。

### G-3 README「复用清单」列的大部分东西本模块一个都没用

- 位置：`README.md:119-129`（表在 `121-127`，实测说明在 `129`）
- 现状：`brep.h` 只 include 三个头（`brep.h:6-8`），实际用到的是 `lattice::ring_list`、`network::uuid` 与 `geometry` 的 `poit`/`curve`/`surface`；表里的 `bind_mesh`、`level_tree`、`matx`、`json.hpp`、`tensor`、`fantasy` 在本模块 0 引用（`json.hpp` 只是经 `head.h:48` 传递包含）。
- 证据：`grep -n "bind_mesh\|level_tree\|matx\|json\|tensor\|fantasy" algebra/geometry/brep/brep.h` → 0 命中；这些符号在各自模块里确实存在（`lattice/bind_mesh.hpp`、`lattice/level_tree.hpp`、`geometry.h:22`、`network/json.hpp`）。
- 影响：按这张表估依赖/工作量会同时高估依赖面、低估待办量。
- 复现/验证：上面那条 grep。
- 建议：README 已加「实测只有两项被真正使用」的说明；表保留为规划。

### G-4 README「阶段划分」表没有状态列，易被读成已完成

- 位置：`README.md:110-115`（表）、`README.md:117`（本轮补的状态行）
- 现状：四-A 只有三个结构体（序列化 0 行）；四-B 只有三个结构体（邻接/边界遍历 0 行）；四-C/四-D 完全 0 行。
- 证据：`brep.h:24-69`；全仓无 `extrude|revolve|sweep|loft|fuse` 符号（E-4）。
- 影响：进度误判，见 E-4/E-5。
- 复现/验证：grep（同 E-4）。
- 建议：README 已补状态行；后续每阶段落地时同步该行。

### G-5 上层 `algebra/geometry/README.md` 的 brep 简介与本模块 README 旧文口径冲突

- 位置：`algebra/geometry/README.md:45`（"brep 用 UUID 而非指针做跨文件稳定引用"）、`algebra/geometry/README.md:54-61`（类型草图）、本模块 `README.md:44`（旧文「节点指针即身份」）
- 现状：上层那句是对的（身份确实是 uuid），与本模块 README 旧文直接冲突；类型草图与本模块代码一致、无出入。按本次任务范围**不改其它模块的文档**，只在此登记。
- 证据：`algebra/geometry/README.md:45` 原文；`brep.h:20`。
- 影响：两份文档给出相反的印象，新人会以先读到的那份为准。
- 复现/验证：读两份文档 + `brep.h:20`。
- 建议：本模块侧已订正（G-2）；上层那句可保留不动。

---

## 未列入的分组

- **C. 跨平台**：本轮无条目，故整组省掉（不是漏写）。理由是 `brep.h` 全是可移植声明：唯一宏是 include guard `XHQM_GEOMETRY_BREP`（`brep.h:2-3`，`brep.h:74` 收尾），全仓只此一处、无重名；头内三个 include 用 `/` 分隔（MSVC 也接受）；结构体不做导出标注（`xhqm_io` 只出现在 `geometry`/`uuid` 等带 .cpp 的类型上）；与平台沾边的只有随机源与高精度时钟，实现在 `network/uuid.cpp:202-213`，不属本模块。**非本机平台未实测，故不写平台结论。**
- **A 组里「现在编译器会过、只是没写完/没接上」的部分**（E-1/E-3/E-4/E-5）按要求归入 E 并已明确标注。
