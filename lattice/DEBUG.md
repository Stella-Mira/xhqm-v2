# lattice 当前问题清单

lattice（图/层次数据模型基础层）**当前仍存在**的缺陷、约束与取舍台账。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。

同类历史台账（不搬家、不改名，仅在此指向）：仓库根 `通读报告-2026-10-07.md`（全局通读报告，含本模块旧表述）；其它模块各自维护自己的台账（如 `fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md`），与本文件互不替代。

核对环境：g++ 11.4 / `-std=c++17` / 工作副本 r677；本文件所有行号均指 r677 的当前内容。复现程序一律写在仓库外（`/tmp`），不落仓。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. parent_tree 改挂父节点时弹错槽位，childs_loca 被弹空后 size_t 下溢（UB）

- 位置：`lattice/parent_tree.hpp:55`（`old_parent_ptr->childs_loca.pop_back();`，位于 `parent_node::_set_parent_ptr`）
- 现状：父向树的 `childs_loca` 只存 `bool` 占位（`tree_node_` 的 `childs_loca_type` = `select_if<_child_size == 0, std::vector<goto_loca_type>, std::array<...>>`，而父向树的 `goto_loca_type` 退化为 `bool`，见 `lattice/tree.hpp:64/67`），不存「哪个槽对应哪个孩子」；真实父子关系存在孩子自己的 `parent_loca`。因此「从旧父的槽表里删掉这个孩子」无法定位，实现直接 `pop_back()` 弹掉**最后一个**槽，而不是被改挂的那个孩子的槽。旧父的槽数与真实孩子数随即脱节；脱节累积到槽表被弹空后，再弹一次即 `std::vector<bool>` 的 `pop_back()` 空容器调用（无符号下溢）。
- 证据：`A.set_parent_ptr(&Q)` → `old_parent_ptr->childs_loca.pop_back()`。对照 `bind_tree.hpp:47-77` 的同名逻辑用 `for (...) if ((*iter)->goto_loca == this)` 找到**正确的槽**再擦除——`bind_tree` 存的是边的指针，parent_tree 存的是布尔占位，这是两族唯一的语义落差。
- 影响：任何「把已挂在 P 下的节点改挂到别处」的用法（`push_child` 建完树后重挂、节点析构时自动脱离父节点都走这条路径）。轻则 `degree()`/`child_size()` 与实际孩子数不符（`traverse()` 走 `leaf_set` 向上收集，暂不受影响），重则析构尾声对空 `childs_loca` 执行 `pop_back()`，属未定义行为。
- 复现/验证：把下面程序写到仓库外 `/tmp/parent_repro4.cpp`，在仓库根执行 `g++ -std=c++17 -g -O0 -fsanitize=address,undefined -I. -I../depend/nlohmann -DXHQM_DEVICE_CPU -o /tmp/parent_repro4 /tmp/parent_repro4.cpp && /tmp/parent_repro4`。
  ```cpp
  #include "lattice/parent_tree.hpp"
  #include <cstdio>
  int main()
  {
      xhqm::_graph_::parent_node<int> P(1), A(2), B(3), C(4), Q(9);
      P.add_child_ptr(&A); P.add_child_ptr(&B); P.add_child_ptr(&C);
      A.set_parent_ptr(&Q);   // 弹掉 C 的槽（Q 的槽数 +1）
      B.set_parent_ptr(&Q);   // 弹掉 A 的槽
      C.set_parent_ptr(&Q);   // 槽表已空 → pop_back() 下溢
      printf("P.degree=%zu（真子 0）\n", (size_t)P.degree());
      return 0;
  }
  ```
  实测输出（UBSan）：`runtime error: applying non-zero offset 18446744073709551608 to null pointer`，栈顶为 `stl_bvector.h:1059 pop_back()` ← `lattice/parent_tree.hpp:55 _set_parent_ptr`。只用公开容器的等价触发路径：`push_child` 建出 P 的 3 个子 → 逐个改挂走 `set_parent_ptr`。
- 建议：给父向树补一个「第 k 个孩子的槽位」索引（或在 `tree_node_` 层为父向树记录孩子指针），把删槽改成按索引删；短期至少把 `pop_back()` 换成「先查空再弹」并记一条契约注释。

### A2. mesh_node_::adjacent_size() / degree() 在有限邻接（_size != 0）路径缺 return

- 位置：`lattice/mesh.hpp:119-131`（`adjacent_size()`）、`lattice/mesh.hpp:216`（`degree()` 转调 `adjacent_size()`）
- 现状：函数体是
  ```cpp
  constexpr xhqm::count adjacent_size() const
  {
      if constexpr (_size == 0) { return this->adjacent_loca.size(); }
      if constexpr (_size != 0) { /* 数非空槽，循环结束后没有 return */ }
  };
  ```
  `_size != 0` 的实例化里所有路径都不返回，函数返回未定义值（`count` 也是无符号，不会是「-1 哨兵」，而是垃圾值）。
- 证据：读 `mesh.hpp:119-131`；`degree()`/`is_isolated()`/`out_degree()` 全部转发到它（`mesh.hpp:216/218/220`）。同族 `tree_node_::degree()`（`lattice/tree.hpp:167-179`）在循环末有 `return childs_size;`，说明这是 mesh 侧漏写。
- 影响：任何用固定邻接数组实例化网节点的代码（`_size != 0`，例如有限出度的图节点）调 `degree()/is_isolated()/adjacent_size()` 就拿到未定义值——**不抛异常、不像 `npos` 那样可判**，调用处会以为读到的是真实度数。迭代器/遍历（走 `adjacent_list()`，另有一条独立实现）不受影响，所以问题容易潜伏。当前仓库里没有 `_size != 0` 的网节点用例，因此没有测试暴露它。
- 复现/验证：仓库外单文件（`/tmp/noreturn_run.cpp`）：`struct fnode : xhqm::_graph_::mesh_node_<fnode, int, void, mesh_dir::mesh_nullptr, 4>`，补上 `_add_adjacent_ptr`/`_sub_adjacent_ptr` 两个桩后调 `adjacent_size()/degree()/is_isolated()`。实测（g++ 11.4）：`-O0/-O1/-O2/-Os` 四档一致输出 `adjacent_size() = 0, degree() = 0, is_isolated() = 1`（即「永远孤立」），`-Wall -Wextra -Wreturn-type` 也不报缺 return；按标准这是走完非 void 函数末尾的 UB，当前拿到的 0 属实现巧合，不可依赖。
- 建议：与 `tree_node_::degree()` 对齐，在 `_size != 0` 分支末补 `return childs_size;`。

### A3. ring_list 的 operator> 在空环上解引用空 curr_ptr

- 位置：`lattice/ring_list.hpp:358-361`（`operator >`），空态判据见 `lattice/ring_list.hpp:193`（`empty()` 即 `curr_ptr == nullptr`）
- 现状：`inline ring_list& operator > (node_data& data_) const noexcept { data_ = curr_ptr->data; return *this; }` 对 `curr_ptr` 无判空；同文件 `operator <`（`ring_list.hpp:354-357`）同样直接写 `curr_ptr->data = data_`。而 `curr()`/`front()`/`back()` 等其它访问接口也都不判空（越界契约见 README「越界契约」条），区别在于 `operator >` 声明了 `noexcept`——空环调用即崩溃且不会被异常路径救回。
- 证据：读 `ring_list.hpp:354-361`；`ring_list::pop_curr()`（`ring_list.hpp:280-296`）在删掉最后一项后把 `curr_ptr = nullptr`（第 288 行），即「清空后仍持有容器对象」是正常状态。
- 影响：`ring_list<T> l;` 清空/未使用时用 `operator >>` 读数据（`l >> v;`）直接崩；写接口 `operator <<` 在空容器上写空指针。这是「空容器调用是 UB」的地带，但两个算子都带 `noexcept`，崩溃不会被上层异常处理救回。
- 复现/验证：未复现（三行即可：`xhqm::ring_list<int> l; int v = 1; l >> v;`——读接口对空 `curr_ptr` 解引用）。
- 建议：要么在 `operator </>` 内加空态早退（返回 `*this`），要么把「空容器调用是 UB」写进 README 的越界契约。

### A4. sparse_mesh::edge(i,j) 在边不存在时以 npos 下标取 val，返回野引用

- 位置：`lattice/sparse_mesh.hpp:247-251`（`edge()`），`find_edge` 的失败返回见 `lattice/sparse_mesh.hpp:114/125/129`
- 现状：`_edge_data& edge(i, j) { index(); return m_val[find_edge(i, j)]; }`——`find_edge` 未命中返回 `xhqm::npos`（`algebra/value.h:46`：`static constexpr auto npos = static_cast<xhqm::count>(-1)`），代码直接拿它当 `std::vector` 下标使用，返回越界内存的引用。带边界检查的 `edge_at()`（`sparse_mesh.hpp:252-258`）才抛 `std::out_of_range`。
- 证据：读 `sparse_mesh.hpp:112-130`（未命中返回 `npos`）与 `247-251`（无判空）。同文件自带注释也承认这一点：「边数据（假定边存在；不存在时行为未定义；触发 index 合并）」。
- 影响：调用者按 README「`edge()` 是 `[]` 风格不检查」理解时，写 `g.edge(0, 9) = 1.f` 会静默写坏 `m_val` 之外的内存（bool 边下尤其隐蔽：`store<bool>` 是裸字节缓冲）。
- 复现/验证：未复现（ASan 下执行 `g.add_node(0); g.add_node(1); g.edge(0,1) = true;`——该边不存在——即报堆越界读/写）。
- 建议：`edge()` 保持无检查语义但改成 `assert` + 注释，或让它与 `edge_at()` 一致抛越界。

### A5. 图算法对「不在节点集里的节点 / 退化入参」缺少校验，max_flow(s==s) 会无限循环

- 位置：`lattice/graph_algorithms.hpp:342-389`（`max_flow`，`prev[s] = nullptr` 在 `362`，取增量 `378-380`，回退循环 `379-385`）；同族缺校验处 `dijkstra`（`:168`）、`prim`（`:294` 直接取 `nodes[0]`）、`a_star`（`:437`）
- 现状：全部算法以「全节点集合」为契约（`graph_algorithms.hpp:5-7`），但没有任何入参合法性检查。`max_flow` 里起点以 `prev[s] = nullptr` 登记，增量 `aug` 初值是 `infinity`（`:344/378`）。若调用者传 `s == t`（退化但合法的调用姿态），BFS 的条件 `while (!q.empty() && prev.find(t) == prev.end())`（`:363`）在第一次判断时就成立（`t == s` 已在 `prev` 里），于是**不进循环体**；此时回退循环 `for (auto* v = t; v != s; ...)` 立刻退出，`aug` 保持 `infinity`，随后的残量更新写成 `inf - inf = NaN`（`:383-384`）；下一轮 `it->second > 0` 对 NaN 恒为 false 也不会结束外层 `while (true)`（`:356`）——**外层没有别的出口**。
- 证据：读 `graph_algorithms.hpp:355-388`；实测（`/tmp/maxflow_edge.cpp`，`s = t`）：程序进入无限循环，60 s 未返回（已人工终止）。
- 影响：把同一节点当源与汇调 `max_flow`（编辑图时的常见手滑）会让进程卡死而不是报错；`s`/`t` 取自另一张图时同样没有防线（`prev[t]` 可能为 `nullptr`，参与 `resid[{nullptr, v}]` 查找）。
- 复现/验证：仓库外单文件 `/tmp/maxflow_edge.cpp`：建 2 节点图 `a→b`，调 `xhqm::max_flow(nodes, a, a, cap)` → 挂死（未返回）。
- 建议：入参处加 `assert`/`std::invalid_argument`（`s`、`t` 必须属于 `nodes` 且 `s != t`），并把 `aug` 为 `infinity` 视为非法（无增广路应终止）。

---

## B. 资源与并发

### B1. arrow_mesh / bind_mesh 容器 add_node 用裸 new，容器本身没有析构函数 → 必然泄漏

- 位置：`lattice/arrow_mesh.hpp:123/131`（`add_node` 的 `new node(...)`），`lattice/arrow_mesh.hpp:97-154`（`arrow_mesh` 类体，无 `~arrow_mesh`）；`lattice/bind_mesh.hpp:140/148` 与 `lattice/bind_mesh.hpp:114-171` 同构
- 现状：两个游标容器都只有 `protected: head_ptr / curr_ptr / m_count`，`add_node` 每次 `new`，`reset()`（`arrow_mesh.hpp:153`、`bind_mesh.hpp:170`）只清指针不释放，类体到文件末尾都没有析构函数。同族的链表/树容器都有析构（`next_list.hpp:413`、`bind_list.hpp:579`、`child_tree.hpp:410`、`parent_tree.hpp:491`、`bind_tree.hpp:406`），只有这两个网容器没有。
- 证据：LeakSanitizer 实测（`/tmp/leak_check.cpp`，作用域内各建 2 个节点后离开作用域）：
  ```
  Direct leak of 32 byte(s) 1 object(s)  ← arrow_mesh<int,void>::add_node  lattice/arrow_mesh.hpp:131
  Direct leak of 32 byte(s) 1 object(s)  ← arrow_mesh<int,void>::add_node  lattice/arrow_mesh.hpp:131
  Indirect leak ... ← bind_mesh<int,void>::add_node  lattice/bind_mesh.hpp:148
  ```
- 影响：任何用 `arrow_mesh`/`bind_mesh` 当「临时图容器」而不是「长生命周期图」的代码，容器作用域结束即丢节点；节点自带的出边（`arrow_node` 的 `loca_type*`）也随节点一起泄漏。跨模块复用（渲染/网络里建临时图）时是稳定漏点。
- 复现/验证：仓库外 `/tmp/leak_check.cpp`（作用域内 `arrow_mesh`/`bind_mesh` 各建 2 节点后离开作用域），在仓库根执行 `g++ -std=c++17 -g -O0 -fsanitize=leak -I. -I../depend/nlohmann -DXHQM_DEVICE_CPU -o /tmp/leak_check /tmp/leak_check.cpp && /tmp/leak_check`。
- 建议：加 `~arrow_mesh() { reset-or-delete }` 走「删所有节点」语义，或改名 `*_view` 明确非拥有；`reset()` 的语义（只清游标）保留但写清注释。

### B2. 容器 reset()/clear() 的两种语义并存，同一模块内不一致

- 位置：`lattice/arrow_mesh.hpp:152-153`、`lattice/bind_mesh.hpp:169-170`（`reset()` 不释放，注释「由 graph 负责释放」）vs `lattice/next_list.hpp:137-150`、`lattice/bind_list.hpp:215`、`lattice/child_tree.hpp:205`、`lattice/parent_tree.hpp:161`、`lattice/bind_tree.hpp:161`（`clear()` 递归 delete 全部节点）
- 现状：链表/树容器是「拥有型」（容器析构即删节点，见各文件 `~*() noexcept { clear(); }`）；网族容器是「游标型」（节点归属外部）。同一模块里 `clear()` 与 `reset()` 名字不同、语义相反，且游标型没有任何显式标记。
- 证据：读上述各文件对应行；`arrow_mesh.hpp:152` 的原注释即「重置（不释放 node，由 graph 负责释放）」。
- 影响：接手者按 `clear()` 的直觉找 `arrow_mesh.clear()` 会找不到（不存在该方法），改用 `reset()` 又误以为节点已被释放 → 与 B1 叠加成泄漏。
- 复现/验证：不适用（接口语义问题）；`grep -n "reset()" lattice/*.hpp` 只有两处，`grep -n "void clear()" lattice/*.hpp` 有 7 处。
- 建议：游标型容器的方法名统一为 `detach()`/`reset_cursor()`，并在 README 的容器语义表里点明「拥有 / 非拥有」。

### B3. 节点所有权完全裸露，模块内没有任何智能指针或 RAII 包装

- 位置：全模块；`lattice/arrow_mesh.hpp:53/75/123/131`、`lattice/bind_mesh.hpp:47/52/64/68/87/91/140/148`、`lattice/next_list.hpp:65/74/83/92/143/178...`、`lattice/child_tree.hpp:59/109/131/148`、`lattice/bind_tree.hpp:68/73/167/194...`、`lattice/level_tree.hpp`（唯一例外：树是 `std::vector` 连续存储，无裸 new）
- 现状：链/树/网族的节点与边对象一律 `new`/`delete` 手工配对；`grep -c "new \|delete "` 统计：`bind_list.hpp` 26 处、`next_list.hpp` 24 处、`child_tree.hpp` 15 处、`bind_mesh.hpp` 12 处、`arrow_mesh.hpp` 6 处。全模块 0 处 `unique_ptr`/`shared_ptr`。
- 证据：`grep -n "new \|delete " lattice/*.h *.hpp`（数量见上）；`grep -rn "unique_ptr\|shared_ptr" lattice/` 无结果（除 README 外）。
- 影响：异常路径（自定义 `node_data` 的构造抛异常时）与半途 `return false` 的失败分支都可能漏删；容器析构与节点析构互相调用（`bind_mesh_node::~bind_mesh_node` 会去改邻居的表，`bind_mesh.hpp:76-94`）使所有权链条难以静态推理。
- 复现/验证：未复现（属架构性观察，非单点触发）。
- 建议：至少给「容器拥有节点」的族加一层 `node_ptr` 别名 + 明确注释，不要求改成智能指针（节点身份即指针，见 F1）。

### B4. 线程安全只覆盖 queues/stacks，且没有阻塞语义

- 位置：`lattice/queues.h:13/20-33`、`lattice/stacks.h:13/20-33`
- 现状：仅这两个容器带 `std::mutex`（每个公开方法 `lock_guard` 一把锁），其余容器与节点全无同步。`get()`/`pop()` 在空容器上立即返回 `false`（`queues.h:62`、`stacks.h:88`），没有条件变量、没有超时/等待接口。
- 证据：读 `queues.h`/`stacks.h` 全文（各 106 行）；`grep -n "condition_variable" lattice/*` 无结果。
- 影响：多线程生产-消费场景只能自己轮询；把节点/图结构跨线程共享时没有任何保护（模块文档也没写「哪些类型线程安全」）。
- 复现/验证：不适用（能力缺失）。
- 建议：README 里明确「只有 queues/stacks 有锁，且为非阻塞」；需要等待语义请在上层加条件变量。

---

## C. 跨平台

### C1. 模块内无任何平台分支，但也无平台验证记录

- 位置：`grep -rn "#ifdef _MSC_VER\|#if defined(_WIN32)\|__linux__\|__APPLE__" lattice/*.h lattice/*.hpp` → 无结果
- 现状：lattice 全部为纯标准库 C++17（`<vector>/<map>/<set>/<queue>/<stack>/<mutex>/<algorithm>/<limits>/<stdexcept>/<iterator>/<utility>/<type_traits>`，见各文件 include 区），没有 `#pragma once` 之外的条件编译（各文件首行 `#pragma once` + 传统 `#ifndef XHQM_*` 双保险）。模块级唯一的跨平台注意点在仓库根：`CMakeLists.txt:50-56` 记录 MSVC 下 `software/signal.h` 会遮蔽系统 `<signal.h>`（把 UCRT 头目录前置解决），与 lattice 无关。
- 证据：上述 grep 无结果；`CMakeLists.txt:50-76` 的 MSVC 注释块与处理分支。
- 影响：本模块自身可移植性好；但 r677 只在 g++ 11.4/Linux 上做过编译验证（本文件全部复现实验的环境），MSVC 侧无本地证据。
- 复现/验证：未复现（无 Windows 环境）。
- 建议：把「lattice 为 header-only 纯标准库」写进 README 依赖章节（已补），MSVC 验证留给 CI。

---

## D. 性能

### D1. level_tree CSR 模式每次 emplace_child 都全量重建 child_first 前缀

- 位置：`lattice/level_tree.hpp:181-196`（`emplace_child` 的 `if constexpr (_child_size == 0)` 分支），其中 `191-195` 就是「按 `m_parent` 重算度数 + 重建前缀」；`remove_leaf` 的 `143-151` 做同样的事
- 现状：插一个孩子 = `m_child_list.insert`（O(孩子总数) 搬移）+ `deg` 计数（遍历全部孩子）+ `m_child_first.assign` 全量重写（O(N)）。建一棵 N 节点树因此是 O(N²)。
- 证据：读 `level_tree.hpp:183-196`；代码注释自己写了「孩子插入父区间末尾（中间插入 O(N)：孩子序列搬移 + 前缀重建）」。
- 影响：`level_tree` 的定位是「结构稳定、高频遍历」，建树/序列化前的构造成本随 N 平方增长；10 万节点的骨骼/场景树构造会是明显卡点。
- 复现/验证：可量：`for (int i=0;i<N;++i) t.emplace_child(0, i);` 计时，N 翻倍时间约 4 倍（未跑计时，仅按代码路径推断；`test/structure_test.cpp:231-235` 只建 3 个节点）。
- 建议：保留「建好后不动」的定位（见 F4），或改成先收集再一次性 build（提供 `build(parent_list)` 之类的批量入口）。

### D2. level_tree N 模式（固定步长）的槽表是 N×count 连续存储，深树/稀疏叉会浪费

- 位置：`lattice/level_tree.hpp:59`（`m_child_slots`）、`lattice/level_tree.hpp:200-206`（按需 `resize`）、`lattice/level_tree.hpp:244-258`（`degree` 要遍历 `_child_size` 个槽）
- 现状：N 模式下每个节点固定占 `_child_size` 个 `xhqm::size` 槽（未用的存 `npos`）；`degree()` 是 O(_child_size) 扫描而不是 O(1)（对比 CSR 模式 O(1)：`m_child_first[i+1]-m_child_first[i]`）。`remove_leaf` 时还要整块擦除 + 全表重映射（`level_tree.hpp:129-136`）。
- 证据：读上述行；doc 注释「_child_size == N：N 叉树，固定步长槽位（child_slots[i*N+k]，npos=空槽）」（`level_tree.hpp:8`）。
- 影响：四叉/八叉场景合理；把 `_child_size` 开大（如 64）但实际稀疏时，内存 = N×64×8B，且每次 `degree()` 扫 64 个槽。
- 复现/验证：本模块实测 `level_tree<int,void,4>`：`degree(0)` 对 1 个孩子仍扫 4 槽（`/tmp/struct_check.cpp` 已跑通，N=4、`child(0,1)==npos`）；复杂度按代码路径可数。
- 建议：README 选型表已写明空间代价 O(N×child_size)（保留），无代码改动必要。

### D3. directed_node 的建边/拆边/邻接检查都是线性扫描，且枚举接口整份拷贝

- 位置：`lattice/directed_mesh.hpp:99-110`（`from_index`/`goto_index` 线性）、`lattice/directed_mesh.hpp:51-96`（add/sub 各带一次 `*_index` 扫描）、`lattice/directed_mesh.hpp:113-115`（`from_list()`/`goto_list()`/`adjacent_list()` 按值返回 `std::vector<node_ptr>` 拷贝）
- 现状：`add_goto(to)` = `goto_index` 扫描 O(出度) + `to->from_index` 扫描 O(入度)；`sub_goto` 用 `erase(begin()+i)` 搬移整个 vector（O(度)）。`adjacent_list()` 每次调用都复制一份出边表——而图算法的契约正是「逐节点调 `adjacent_list()`」（`graph_algorithms.hpp:36`、`71`、`119`、`:151`……），于是每个算法的每个节点都分配一次 vector。
- 证据：读上述行；`graph_algorithms.hpp` 全篇 `auto adj = u->adjacent_list();` 或 `for (auto* v : u->adjacent_list())`。
- 影响：大图（万级节点、十万级边）跑 SCC/WCC/Kruskal 时，邻接表拷贝的分配次数 = 算法访问次数，常数很大；`arrow_node`/`bind_mesh_node` 的 `adjacent_list()` 同样按值返回（`mesh.hpp:133-143`），是整个算法层的共同开销。
- 复现/验证：未复现（未做计时对比）。
- 建议：算法层改收「邻接视图」（返回 `const std::vector<node*>&` 或 回调 `for_each_adjacent`），属接口级改动，先记录。

### D4. graph_algorithms 全部用 std::map 存节点关系，且 floyd_warshall 预建 N×N 表

- 位置：`lattice/graph_algorithms.hpp:33`（拓扑入度 `std::map<node*, index>`）、`:112/117`（SCC 用两个 map）、`:140`（WCC 并查集用 map）、`:170-171`（dijkstra 两个 map）、`:213`（Floyd N×N 个 map 行）、`:298-300`（prim 三个 map）、`:345`（最大流残量 map）、`:440-442`（A* 三个容器）
- 现状：节点身份一律用 `std::map<node*, ...>` 关联，单次查/插是 O(log N) 且常数大、节点多时缓存不友好；`floyd_warshall` 在预处理阶段就写入 N×N 条 `dist[u][v] = INF`（`graph_algorithms.hpp:214-216`，共 N 个 map 各 N 项），即 O(N²) 个 `std::map` 结点的一次性分配。
- 证据：读上述行；实测 `/tmp/algo_check.cpp`：5 节点图上 `floyd_warshall` 返回 5 行（每行 5 项），`dijkstra` 返回 5 项。
- 影响：N=1000 时 Floyd 光初始化就是 100 万个 map 结点（约几十 MB 级 + 极慢）；拓扑/SCC 在 N=1e5 时每次查表都走红黑树。
- 复现/验证：可量（构造 N 节点空图调 `floyd_warshall(nodes)`，计时随 N² 增长）；本文件只做了正确性验证（5 节点）。
- 建议：节点指针→序号的一次性映射（`map` 建一次）或让调用方直接传序号邻接表；短期在 README 的算法表注明「复杂度含 `std::map` 的 log 因子」。

---

## E. 未实现 / 占位桩

### E1. graph_algorithms.hpp 文件头注释仍写「MST、最大流等未来扩展」，实际 11 个算法全在

- 位置：`lattice/graph_algorithms.hpp:8-9`
- 现状：文件头「现有：拓扑排序 / 强连通分量 / 弱连通分量 / Dijkstra / Floyd-Warshall；MST、最大流等未来扩展」；而实际文件里另有 `kruskal`（`:249`）、`prim`（`:294`）、`max_flow`（`:342`）、`bipartite_matching`（`:416`）、`a_star`（`:437`）、`euler_path`（`:478`），共 11 个算法，全部编译通过并实测正确（见「已验证事实」）。
- 证据：`grep -n "^	[a-z].*(" lattice/graph_algorithms.hpp` 列出 11 个自由函数；`/tmp/algo_check.cpp` 实测：拓扑 0 1 2 3 4、SCC 4 分量、WCC 1 分量、dijkstra 7.0、Floyd 7.0、kruskal/prim 各 2 条边、max_flow 5.0、匹配 2 条、A* 7.0、euler 长度 4。
- 影响：读文件头的人会以为只有 5 个算法可用（旧 README 也继承了这个说法）。
- 复现/验证：见上（`/tmp/algo_check.cpp` 全跑通）。
- 建议：改文件头注释（本次未改代码，按铁律只动两份文档；已在 README 的 G 组说明）。

### E2. mesh.hpp / tree.hpp 里的枚举与内联调试类残留

- 位置：`lattice/mesh.hpp:23-28`（注释掉的 `mesh_type` 枚举）、`lattice/mesh.hpp:605-612`（`class test_mesh : public derive_mesh<test_mesh>`，公开在命名空间里）、`lattice/list.hpp:622-627`（`class test_list`）、`lattice/tree.hpp:21-28`（注释掉的 `tree_type` 枚举）
- 现状：三处「多叉树/层级树」类型的枚举都被注释保留；`test_mesh`/`test_list` 是给手测用的示例派生类，带 `char value`，编译进每个 include 了 `mesh.hpp` / `list.hpp` 的 TU。
- 证据：读上述行；`test_mesh` 在 `mesh.hpp` 命名空间 `xhqm` 内直接定义（非 `detail`）。
- 影响：`xhqm::test_mesh`/`xhqm::test_list` 会成为公共名字（万一同名即 ODR 冲突）；注释掉的枚举让人误以为「层级树/多叉树」另有入口（其实对应 `level_tree.hpp`）。
- 复现/验证：不适用。
- 建议：移到 `_graph_` 细节命名空间或删掉；本次仅记录。

### E3. level_tree 的「无中间删除」注释与实现不符（实现有 remove_leaf）

- 位置：`lattice/level_tree.hpp:11-12`（文件头「无中间删除（clear 全清）」）vs `lattice/level_tree.hpp:94-153`（`remove_leaf` 完整实现，含两种布局的下标重映射）
- 现状：文件头写「结构稳定场景设计：emplace 建树 O(N)（中间插入），无中间删除（clear 全清）；频繁增删中间节点请用 child_tree/bind_tree 指针树」，但类里 `remove_leaf` 已实现且可用（仅叶子可删，删子树要后序逐个删）。
- 证据：读 `level_tree.hpp:11-12` 与 `94-153`；`test/structure_test.cpp` 未覆盖 `remove_leaf`。
- 影响：读者会以为只能整树 `clear()`，不敢用 `remove_leaf`；也用不到它那套下标重映射的正确性保证（本文件未审计该路径的正确性）。
- 复现/验证：未复现（无 `remove_leaf` 用例）。
- 建议：修正文件头注释；补一个 `remove_leaf` 用例（含 CSR/N 两模式的下标重映射）。

### E4. child_tree 的 `fork_tree` 前向声明无对应实现

- 位置：`lattice/child_tree.hpp`（文件头注释与类名列表里出现 `fork_tree`，实际只有 `child_tree` 与内部 `child_node`）
- 现状：按 README 的族表，多叉树＝`child_tree`；`fork_tree` 只在注释/历史命名里出现，无类定义。
- 证据：`grep -n "fork_tree" lattice/*.hpp` → 仅出现在 child_tree.hpp 的文件头注释。
- 影响：几乎无（纯命名残留），但会让「树族到底有几种」的读者困惑。
- 复现/验证：不适用。
- 建议：清理注释。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 节点禁止拷贝/移动，指针即身份

- 位置：`lattice/struct.h:20-22`（族约定注释）、`lattice/mesh.hpp:295-296`（`derive_mesh_` 显式 delete 拷贝/移动）、`lattice/tree.hpp:408-409`（`derive_tree_` 同）、`lattice/directed_mesh.hpp:45-48`（`directed_node` 四个全 delete）、`lattice/list.hpp:365-374` 区段（`derive_list_` 同）
- 取舍理由：整个模块的边对象、迭代器、算法都靠节点地址做键（`std::map<node*, ...>`）与身份判断（`is_parent_of`、`is_reachable`）；若允许拷贝，副本会带着指向原图邻接的指针，任何一次拷贝都制造一张不一致的图。禁止拷贝让「一个节点对象 = 图里一个顶点」成为硬约束。
- 代价：容器不能按值存节点；`std::vector<node>`、`std::map<key, node>` 这类写法直接不可用；节点必须堆分配或用外部容器保证地址稳定（`level_tree`/`matrix_mesh` 因此改用「下标即身份」的连续布局）。
- 结论：**不修**。这是全模块一致的身份模型，改它等于重写算法层的键类型。

### F2. 容器拥有节点 vs 节点级 API 由调用方掌握生命周期

- 位置：`lattice/child_tree.hpp:205-215`、`341-365`（`clear()` 后序 delete、`sub_child()` 摘除子树并移交所有权）、`lattice/parent_tree.hpp:161-190`（先按 `leaf_set` 收集再删）、`lattice/next_list.hpp:413-420`（析构自删节点）vs `lattice/arrow_mesh.hpp:152-153`（`reset()` 不删）
- 取舍理由：链/树族是「容器 = 结构本身」，容器析构就该把结构删干净（否则用户容易漏删）；网族把节点当「图的顶点」，一张图常由多个容器/游标视图拼起来（`arrow_mesh` 只持有入口，邻接是节点自己维护的），容器无法也不该单方面决定删哪些节点。
- 代价：两类容器的生命周期语义相反，必须靠文档与命名区分（见 B2）；`sub_child()` 这类「摘除并移交所有权」的接口要求调用方确实接管。
- 结论：**不修**。语义本身就是两种用途；缺的是命名区分（`reset` → `detach`），记在 B2。

### F3. level_tree 两套布局（CSR 变长 / 固定步长槽）并存，用 `if constexpr` 分流

- 位置：`lattice/level_tree.hpp:71-92`（`reserve`/`clear` 分支）、`:99-153`（`remove_leaf` 分支）、`:163-216`（`emplace_*` 分支）、`:262-310`（`child`/`edge` 分支）
- 取舍理由：CSR（`_child_size == 0`）给任意叉数 + O(1) `degree()`；固定步长（`_child_size == N`）给可预测的槽地址，适合四叉/八叉/骨骼这类固定叉数。两套布局对外 API 完全一致（同一批方法名，`if constexpr` 分流），与 `tree_node_` 的 `_child_size` 语义对齐（`level_tree.hpp:9` 的注释即此意）。
- 代价：同一份逻辑写两遍（改一处要改两处，`remove_leaf`/`emplace_child` 各有两套下标重映射）；模板二义与调试都变重。
- 结论：**不修**。两套布局的复杂度/内存特性确实不同（README 选型表已分开列），合并会牺牲其中一种。

### F4. directed_node 独立于 mesh_dir 体系（自建 from/goto 双向登记）

- 位置：`lattice/directed_mesh.hpp:27-127`（`directed_node : base_data_<_node_data>`，自带 `from_loca`/`goto_loca` 两个 `std::vector<node_ptr>`，边无数据）
- 取舍理由：`mesh_dir` 体系的抽象是「边的类型」（`edge_null/from/goto/bind` + 可选 edge_data，见 `struct.h:83-118`），一个节点只能有一种边方向；有向图的语义要求同一节点**同时**持有入边表与出边表（数据流的 from→goto），且要能 O(1) 兼容图算法约定的 `adjacent_list()`＝出边（`directed_mesh.hpp:10`、`:115`）。硬套 `mesh_dir` 会得到一个既非 `mesh_oneway` 也非 `mesh_twoway` 的第五种方向，反而污染所有 `if constexpr` 分派。
- 代价：两套体系割裂——`directed_node` 不接受 `edge_data`（要带权就得仿 `arrow_node` 自己扩，`directed_mesh.hpp:26` 的注释就是这句）、不提供迭代器/遍历（`iterate_bfs/dfs` 在 `mesh_node_` 上，它没有）、不参与 `arrow_mesh`/`bind_mesh` 容器。用有向图 + 权重时必须改用 `arrow_node`。
- 结论：**不修**（记为有意割裂）。当前 README 用「独立实现」表述，已补上理由与「无 edge_data / 无迭代器」两条代价。

### F5. 无统一序列化 I/O，只提供连续指针视图

- 位置：`lattice/level_tree.hpp:312-320`（`nodes()/parents()/child_first()/child_list()/child_slots()`）、`lattice/matrix_mesh.hpp:152-160`（`nodes()/matrix()/matrix_ref()`）、`lattice/sparse_mesh.hpp:274-282`（`nodes()/row()/col()/val()/row_ptr()`、`:365` `to_sparse_matrix()`）、`lattice/directed_mesh.hpp:113-115`（`from_list()/goto_list()` 值返回）
- 取舍理由：三种连续布局（层序数组、邻接矩阵、COO/CSR）各自的「自然字节形态」不同，统一成一套 `save/load` 只能二选一：要么强制共同格式（牺牲紧凑性与代数后端的零拷贝），要么只暴露裸视图，让上层按目标格式（磁盘格式、tensor 后端、网络包）自行落盘。当前选了后者——**把布局暴露出来，把 I/O 留给上层**。
- 代价：跨节点/跨进程传递一张图没有统一入口；每个使用方都要自己按 `nodes()+parents()` 之类的组合写读写（且这些指针的语义因布局而异：CSR 模式 `child_first` 是 N+1 前缀、N 模式是 `child_slots`）。指针接口是 `const` 视图，反向（从字节重建树）完全没有 API。
- 结论：**不修**（有意为之）。模块保持 header-only 且不引入文件格式；如需要，建议在上层（`network/` 侧）实现并只依赖这些指针视图。

### F6. 图算法只吃指针节点集合，index 型图（matrix_mesh / sparse_mesh）不参与

- 位置：`lattice/graph_algorithms.hpp:5-7`（契约）、`:31`（`topological_sort` 收 `std::vector<_node_type*>`）；`lattice/matrix_mesh.hpp:191-237`（自带 `reachable`/`reachable_component` 的 BFS）、`lattice/sparse_mesh.hpp:306-358`（同）
- 取舍理由：算法层用节点地址做键并调用 `node->adjacent_list()`/`weight(u,k)`，这对指针图（`derive_mesh`/`arrow`/`bind`/`directed`）是零适配；而 `matrix_mesh`/`sparse_mesh` 的身份是**下标**，它们已经各自实现了一份可达性（按值语义、可拷贝、可移动），语义更贴合稠密/稀疏代数。
- 代价：两套图世界不互通——用 matrix/sparse 建图就拿不到拓扑排序/SCC/MST/最大流；反过来 `matrix_mesh` 的矩阵代数（路径计数）也只有它自己有。同一条算法在本模块存在两处实现（`reachable` 在 mesh.hpp 是 BFS-on-pointers、在 matrix/sparse 是 BFS-on-indices）。
- 结论：**不修**（有意分层）。需要「下标图 + 经典算法」的组合时，得先转成指针图（当前无转换 API，属已知缺口）。

### F7. 指针网族对自环（self-loop）态度不一：derived/bind/directed 拒绝，arrow 接受

- 位置：`lattice/bind_mesh.hpp:43`（`if (adjacent_ptr_ == static_cast<node_type*>(this)) return false;`）、`lattice/mesh.hpp:568`（`derive_mesh_` 的 `_add_adjacent_ptr` 同）、`lattice/arrow_mesh.hpp:40-58`（`arrow_node::_add_adjacent_ptr` 只有空指针与重复判定，**没有自环判定**）
- 现状（实测 `/tmp/selfloop_check2.cpp`，r677 / g++ 11.4）：
  - `arrow_node<int> a; a.add_adjacent_ptr(&a)` → 返回 `true`，`degree() == 1`、`adjacent_list().size() == 1`（自己邻接自己，能建进去）；
  - `bind_mesh_node<int> b; b.add_adjacent_ptr(&b)` → 返回 `false`（`bind_mesh.hpp:43` 显式拒绝）；
  - `derive_mesh` 派生类同理返回 `false`（`mesh.hpp:568`）；
  - `directed_node<int> e; e.add_goto(&e)` → 返回 `false`（`directed_mesh.hpp:53`）。
  - `matrix_mesh` / `sparse_mesh` 的 `add_edge(i, i)` 合法（对角线 / COO 自环）。
- 取舍理由：无向自环在遍历/度数/生成树语义里都要特判（`degree()` 记 1 还是 2、MST 该不该收），`bind_mesh` 的「双向自动维护」若允许自环会立刻在对方表里重复登记自己，故显式拒绝；矩阵/稀疏网按「矩阵对角线」处理自环天然合法。
- 代价：同一张图用 `bind_mesh` 建不了自环、换 `arrow_mesh` 就能建，且建进去之后 `degree()` 会把自环算作 1（有向语义正确、无向语义下与 `bind_mesh` 的度口径不同）；跨族「自环是否合法」没有单一答案，只能按类查。
- 结论：**不修**（记录为准）。要让三族一致，应在 `arrow_node::_add_adjacent_ptr` 补 `if (adjacent_ptr_ == static_cast<node_type*>(this)) return false;`。

### F8. level_tree/matrix_mesh 移动后源对象 size() 清零，但内部容器仍是 moved-from 状态

- 位置：`lattice/matrix_mesh.hpp:239-255`（move 构造/赋值后 `other.m_count = 0`）、`lattice/sparse_mesh.hpp:373-399`（move 后 `m_count = 0; m_indexed = false;`）、`lattice/level_tree.hpp:417-421`（直接 `= default`，**没有**清零语义）
- 取舍理由：`matrix_mesh`/`sparse_mesh` 的 `size()` 读 `m_count` 而不是 `m_nodes.size()`，把源 `m_count` 清零可保证「移动后源 `size()==0`、`empty()==true`」，避免「size 与数据不一致」的危险中间态（`matrix_mesh.hpp:243` 的注释即此意）。
- 代价：与标准库「moved-from 对象有效但未指定」的惯例不同（这里被指定为「空」）；`level_tree` 没跟这条约定（move 后源 `size()` 取决于 `std::vector` 的实现，通常是 0 但不是承诺），三者的移动后语义不统一。
- 结论：**不修**（语义更强且安全）。不一致已记录；README 的接口清单里注明 matrix/sparse 的「移动后源为空」与 level_tree 未承诺。

---

## G. 文档与代码的出入

### G1. README 曾称 directed_mesh/matrix_mesh/graph_algorithms 文件缺失

- 位置：`lattice/README.md`（旧版本目录与章节表）；对应实际文件 `lattice/directed_mesh.hpp`（131 行）、`lattice/matrix_mesh.hpp`（302 行）、`lattice/graph_algorithms.hpp`（531 行）
- 现状：三个文件在工作副本 r677 中都在，且都编译通过、被测试引用（`test/soul_graph_test.cpp:4-5` 引 directed_mesh + graph_algorithms，`test/structure_test.cpp:7` 引 matrix_mesh）。
- 证据：`ls -l lattice/{directed_mesh,matrix_mesh,graph_algorithms}.hpp` 存在；`test/sparse_mesh_test.cpp`（独立可执行）、`test/soul_graph_test.cpp` 的 include 行；本次实测 `/tmp/algo_check.cpp`、`/tmp/struct_check.cpp` 用它们跑通。
- 影响：接手者会以为有向图/矩阵图/算法层要自己补，重复实现。
- 复现/验证：`grep -n "directed_mesh\|matrix_mesh\|graph_algorithms" test/*.cpp CMakeLists.txt`
- 建议：README 已订正（见 README 的目录表与 §4/§5，作废标注）。

### G2. graph_algorithms.hpp 文件头自述只有 5 个算法

- 位置：`lattice/graph_algorithms.hpp:8-9`
- 现状：与 E1 同一条（这里是「文档与代码出入」视角）：文件头列 5 个，实际 11 个。
- 证据：见 E1。
- 影响：见 E1。
- 复现/验证：见 E1。
- 建议：改注释（需动源码，本次不动）。

### G3. README 用法示例里的 `link()` 等 API 不存在；树迭代器别名漏列

- 位置：`lattice/README.md`「§10 用法示例」的 `bind_list` 示例（旧文 `a.link(&b); b.link(&c);`）
- 现状：`bind_list_node` 没有 `link()` 方法（`grep -n "link" lattice/bind_list.hpp` 无结果）；挂接接口是 `set_prev_ptr`/`set_next_ptr`（`lattice/list.hpp:97/123/202/206`，继承给所有链节点）。实测可编译的等价写法：
  ```cpp
  xhqm::bind_list_node<int> a, b, c;
  a.set_next_ptr(&b);
  b.set_next_ptr(&c);          // b 自动记录 prev = a，c 自动记录 prev = b
  for (auto& v : a) { /* a.next 方向遍历：a, b, c（void 数据时解引用得节点指针） */ }
  ```
  另：README §2 只列了 `prev_iterator`/`next_iterator`/`list_iterator`，漏了树族的 `tree_pre_iterator`/`tree_mid_iterator`/`tree_bak_iterator`（`lattice/tree.hpp:739/743/747`，模板参数 `_tree_type, index`，`index=0` 先序、`index=xhqm::npos` 后序）与 `tree_dfs_index_iterator` 的中序用法（`test/structure_test.cpp:214-217` 用了全部四种）。
- 证据：`grep -n "link" lattice/*.hpp`（无 `link()` 定义）；`tree.hpp:739-750`；`test/structure_test.cpp:213-219` 实测 `std::distance` 通过。
- 影响：照抄旧示例会编译失败（C2039/no member named 'link'）。
- 复现/验证：`/tmp/lat_check.cpp`（本次）用真实 API 重写的示例全部编译运行通过。
- 建议：README 已改（改用真实 API + 补迭代器别名）。

### G4. CMakeLists.txt 只登记 6 个 lattice 头文件，其余 14 个不在 IDE 工程里

- 位置：`CMakeLists.txt:232-237`（`XHQM_HEAD_CORE` 里的 `lattice/stacks.h`、`queues.h`、`list.hpp`、`tree.hpp`、`mesh.hpp`、`directed_mesh.hpp`）
- 现状：模块共 20 个源文件；`level_tree.hpp`、`matrix_mesh.hpp`、`sparse_mesh.hpp`、`graph_algorithms.hpp`、`arrow_mesh.hpp`、`bind_mesh.hpp`、`next_list.hpp`、`prev_list.hpp`、`bind_list.hpp`、`ring_list.hpp`、`child_tree.hpp`、`parent_tree.hpp`、`bind_tree.hpp`、`struct.h` 均未列入（与门面头 `xhqm/lattice` 的覆盖范围也不一致：门面收录了 `arrow_mesh.hpp`/`bind_mesh.hpp`/`graph_algorithms.hpp` 等 16 个，漏 `directed_mesh.hpp`/`level_tree.hpp`/`matrix_mesh.hpp`/`sparse_mesh.hpp`；见 README 的「可达性」段）。因为全模块 header-only、只靠 include 生效，功能不受影响，但从 CMake/MSVC/IDE 视角看不到这些文件（VS 里不显示、也不参与「头文件是否被改动」的依赖跟踪）。
- 证据：`grep -n "lattice" CMakeLists.txt` 只有那 6 行；`ls lattice/*.h*` 共 20 个。
- 影响：IDE 侧「文件缺失」的错觉（与 G1 的旧结论可能是同源），以及改动这些头不会触发 CMake 重新配置（header-only 下影响有限）。
- 复现/验证：`grep -c "lattice/" CMakeLists.txt` → 6。
- 建议：把其余头文件补进 `XHQM_HEAD_CORE`（需改 CMakeLists，本次不动）。

### G5. 各文件头注释与实际类型/实现不符（历史命名残留）

- 位置：`lattice/mesh.hpp:1-8`（注释仍写「普通网：继承用网 双向网 单向网 矩阵网」）、`lattice/tree.hpp:1-9` 与 `lattice/struct.h:33-43`（「多叉树 层级树」「有限邻接网/来向网/去向网」等族命名）、`lattice/next_list.hpp:1-8`、`lattice/ring_list.hpp:1-11`（第 4 行「用途：双向链表」，实际是环形链表）、`lattice/sparse_mesh.hpp` 无问题
- 现状：这些注释描述的是早期「族 × 方向」的矩阵式命名（如「来向网/去向网」），而 r677 的实际类型是 `arrow_node`/`bind_mesh_node`/`derive_mesh`/`matrix_mesh`/`sparse_mesh`；`ring_list.hpp` 的文件头仍写「双向链表」。
- 证据：读各文件首段注释与其中的类型定义（`class ring_list`、`struct arrow_node`）。
- 影响：新读者会去找不存在的类型名（「来向网」）；`ring_list` 的用途标注直接错位。
- 复现/验证：不适用。
- 建议：README 的目录表已给出「文件 → 实际内容」的权威对照，注释修正需动源码（本次不动）。

---

## 已验证事实（供复核，非问题）

本次核对中实测确认**正确**、因而不进上表的关键数字与契约（同一环境：g++ 11.4 / C++17 / r677）：

- 模块规模：20 个源文件 / 7996 行（`wc -l lattice/*.h lattice/*.hpp`，含 466 行 `level_tree.hpp`、531 行 `graph_algorithms.hpp`、613 行 `mesh.hpp`、831 行 `tree.hpp`）。
- 11 个图算法全部可用：拓扑排序（Kahn，有环返回空）、SCC（Kosaraju 迭代 DFS）、WCC（并查集 + 路径压缩）、Dijkstra、Floyd-Warshall、Kruskal、Prim、Edmonds-Karp 最大流、匈牙利匹配、A*、Hierholzer 欧拉路径。实测 5 节点带权有向图：dijkstra n0→n4 = 7.0、floyd 同值、max_flow = 5.0、匹配 2 条边、A* = 7.0、欧拉路径长度 4（0 1 2 3）、无向三节点链的欧拉路径返回空（判定只认有向度数，见 README §5 备注）。
- 权重回调契约：`weight(u, k)` 的 `k` 就是 `u->adjacent_list()[k]` 的下标（`graph_algorithms.hpp:183-186`），因此边数据可直接 `u->adjacent_edge(k)->data` 取（实测 `arrow_node<int,float>` 上生效）。
- matrix_mesh：`add_node` 扩展矩阵 O(N²)、`remove_node` 收缩 O(N²)（`matrix_mesh.hpp:77-90` / `45-65`，均为整表搬移）；`matrix_ref()` 的矩阵乘确实给出路径数（3 节点 0→1→2 的图，平方后 (0,2) = 1）；`matrix()` 返回底层连续指针（`xhqm::matrix<int,float>` 实测 `matrix()[1] == 0.25` 对应 `(0,1)`）。
- sparse_mesh：`add_edge` O(1) 追加、重复边在 `index()` 时合并（bool 取或、数值 `xhqm::sum` 累加，`sparse_mesh.hpp:72-94`，实测 0.5+0.3 → 0.8、`out_degree` 合并后为 1）；`remove_node` O(nnz) 全表压缩（`:147-164`）；移动后源清零（`:374-399`）；测试 `test/sparse_mesh_test.cpp` 自述「仅编译，不链接」。
- level_tree：CSR 模式与 N 模式实测均正确（CSR：`degree(0)=2`、`edge(0,0)=1.5`；N=4：`child(0,1)==npos`、`edge(0,0)=9.0`）；`remove_leaf` 存在且两模式下都有下标重映射。
- `prev_list` 的方向语义：`{1,2,3}` 后 `push_back(4)` 遍历得 `4,1,2,3`（实测打印 `prev_list push_back(4) 遍历首项 = 4, size=4`），与 README §2 的说明一致（非缺陷）。
- `ring_list` 可 range-for（实测 `std::distance(rl.begin(), rl.end()) == 3`）；`test/structure_extended_test.cpp:82` 的注释「ring_iterator 缺 operator++，无法 range-for」已作废（`ring_list.hpp:137-140` 有 `++/--`）。
