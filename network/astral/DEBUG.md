# astral —— 当前仍存在的问题

`network/astral`（「一切皆节点」的持久化记忆内核：内存主存 + 双向关联 + 冷热分离惰性加载 + JSON/二进制/mmap 持久化 + 只读二分索引）在 r677 工作副本下**仍然存在**的缺陷、缺口与取舍台账。用法见同目录 [`README.md`](README.md)。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
> 与同类文件的关系：本文件与仓库根 `DEBUG.md`、各模块自己的 `README.md` / `DEBUG.md` 是同一体系；`fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` 是历史台账，**不搬家不改名**，本文只在需要时指过去（根 `DEBUG.md:59-64` / `F3` 已从库层面记过「astral 的 `datas` 只增不减」，本文 A2 给的是写路径与消费方的具体行号，不重抄其观测记录）。模块内的历史结论（`save_json` 也会刷新 `m_offset`、`模糊查询一律全表扫描`、`astral 无单测/基准`）判为作废，见 G 组与 `README.md` 的 `已作废` 标注。

本模块规模（实测 `wc -l`）：`kernel.cpp` 847、`evoke.cpp` 309、`thing.cpp` 188、`kernel.h` 149、`mmap_index.cpp` 116、`astral.h` 93、`thing.h` 70、`mmap_index.h` 63、`evoke.h` 55，共 9 文件 / 1890 行；另有 4 个测试夹具（`test/astral_test.cpp` 145、`test/astral_behavior_test.cpp` 507、`test/astral_stress_test.cpp` 196、`test/thing_demo.cpp` 66）。核对环境：工作副本 r677（`svn info` 实测），本节点只做静态核对与 grep 实测，**未实跑构建/测试**（各条已分别注明）。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `get()` / `data_of()` 不加热冷节点：冷库上把「有历史」读成「空」，thing 层随之静默退化
- 位置：`network/astral/kernel.cpp:173-178`（`get`）、`:190-200`（`data_of`）；中招的消费者 `network/astral/thing.cpp:111-112`、`:74-76`、`:130`，`network/nebula/data.cpp:191-205`
- 现状：`load_index`（`:696-700`）只把索引读进 `m_node_data`，每个 node 的 `datas` 保持默认空数组、数据段偏移留在 `m_offset`；而 `get` / `data_of` 不做 `fetch_impl`，返回的 node 里 `datas` 仍是空数组。会加热的入口只有六处：模板 `select`（`kernel.h:99`）、模板 `update` / `remove`（`kernel.h:84`）、`select_uuid`（`kernel.cpp:310`）、`select_info`（`:334`）、`data_fetch`（`:736`）、`evoke::upsert` 命中分支（`evoke.cpp:74`）—— 也就是「必须走到某个会加热的 API」才能看到冷节点的数据。
- 证据：`kernel.cpp:706` 的判据 `if (oit == m_offset.end()) return true;   // 已热`——「热」由 `m_offset` 决定；`get`（`:173-178`）只做一次 `m_node_data.find` 就返回指针。
- 影响：`load_index` 出来的冷库上，`get(id)->datas` / `data_of(id)` 把有历史读成空且无任何信号：`thing::object_get` 直接 `return false`（`thing.cpp:112`），`thing::event_children` 返回空（`:76`），`thing::merge` 因 `object_get` 失败而返回 false（`:130`）；`network/nebula/data.cpp:187-207` 的 `select_history` 也是 `get` + `datas_json()` 直读。注意边界：`query_object` 因先调 `select_info` 加热而侥幸正常（`thing.cpp:169-174`）；`event_parent` 读的是索引里带的 `links`，冷库也能用。
- 复现/验证：未复现（静态可判）。正向对照在 `test/astral_behavior_test.cpp:374-394`（先 `data_fetch` 再 `get`）；核对阈值用 `grep -n "fetch_impl" network/astral/kernel.cpp network/astral/kernel.h`。
- 建议：`get` / `data_of` 补契约注释，或加 `get_fetched()` / `data_of_fetched()` 变体。

### A2. `update_data` 只 `push_back`、`datas` 无上限：长驻写方的内存与落盘单调膨胀（消费方 OOM 根因）
- 位置：`network/astral/kernel.cpp:263-274`（`:271` `node.datas.push_back(rec);`）、`kernel.h:113-114`；消费方 `network/nebula/data.cpp:117-138`（`:126` 每帧一次 `update_data`）、`network/nebula/data.h:26-27`（`m_capacity = 0` / `m_duration = 0` 表示不限）
- 现状：写路径就是无条件的 `push_back(rec)`（`rec = {time, form, json}`），全文件没有对 `datas` 的裁剪/合并/上限判断；唯一的历史裁剪是 `remove_time`（`:275-287`），必须由调用方显式给时间点。nebula 的窗口淘汰只裁内存视图（`data.cpp:130-133` 的 `pop_front`），内核里的历史一条不动。
- 证据：`grep -n "push_back" network/astral/kernel.cpp` → `:271` 是唯一的 datas 写点（其余 `:283/:314/:338/:361/:377/:403` 都在查询/构造临时数组）；`grep -rn "remove_time" --include=*.h --include=*.cpp .` → 除模块自身外只有 `test/astral_test.cpp:97` 与 `test/astral_behavior_test.cpp:280` 两个测试调用，**生产侧零调用**。
- 影响：任何对同一节点持续写入的长驻进程（nebula `data::insert_data` 每帧一条）RSS 与该节点的落盘体积单调增长，最终 OOM；`save_json` / `save_binary` 每次保存都要序列化整段历史，代价随运行时长线性上升。仓库根 `DEBUG.md:59-64` 记了消费方 `stella` 侧 `star_qqbot` 26.8 GB 的观测。
- 复现/验证：上面两条 grep 可直接核对；内存侧未在本仓复现（本仓无长驻节点）。
- 建议：内核加显式上限/快照合并开关（默认关 = 与今天等价，见 F1）。

### A3. `save_json` 的三处问题：吞掉冷节点解码失败（静默写空）、不刷新 `m_offset`、同路径格式转换会污染后续二进制写入
- 位置：`network/astral/kernel.cpp:455-516`（`:483` / `:485-493` / `:502-515` 为三处关键点）；与 `kernel.h:133` 的注释相反
- 现状：
  1. 冷节点回填：`nj = n.to_json()`（`:483`）对冷节点拿到的已是 `datas: []`，只有 `from_cbor` 成功才覆盖；失败走 `catch (...) {}`（`:492`）→ 该节点的历史被写成空数组，函数仍返回 `true`（`:515`）。对照 `save_binary` 在同类读取不足时是**拒绝保存**（`:647-654` 的 `gcount` 校验后删 tmp 返回 false）。
  2. `save_json` 全文不写 `m_offset` / `m_path_fetch`。`m_offset` 的写点只有 `:132`、`:153`（`load_index_impl`）、`:426`（`load_json` 清空）、`:548`、`:556`（`load_binary` 转热）、`:687`、`:690`（`save_binary` 刷新），而 `kernel.h:133` 的注释写的是「非 const：成功后刷新 m_offset 为新布局」。
  3. 由 2 派生的格式转换陷阱：`load_index` / `load_binary` 之后 `save_json` 写到**同一路径** → `m_path_fetch` 指向已是 JSON 文本的文件、`m_offset` 还是旧二进制偏移。后续冷节点加热按旧偏移取字节 → `from_cbor` 抛错被 `catch`（`:721`）吞掉 → 该节点永久停在冷态；后续 `save_binary` 若那些偏移仍落在文件内，会把 JSON 字节当 CBOR 段原样拷进新库（`:638-657`），而守卫只在「读到的字节不足」时生效（`:647`）。
- 证据：`save_json` 全函数体（`:455-516`）对 `m_offset` 无赋值；`load_index_impl:121` 只认 `ver == 1`；`grep -n "m_offset" network/astral/kernel.cpp` 的写点即上述 7 处。
- 影响：JSON 备份在源文件损坏/被改名/被别的格式覆盖时静默丢历史（返回值还是 true，宿主无从发现）；「二进制库 + JSON 备份/就地转换」的自建流程会把偏移表搞脏，症状要到下一次 load 才显现。现有消费方 nebula 走的是 `load_json` / `save_json`（`network/nebula/data.h:40-48`，此路径 `m_offset` 为空）因此不触发第 3 条。
- 复现/验证：未复现（静态可判）。核对命令：`grep -n "m_offset" network/astral/kernel.cpp`；或对冷库执行 `load_binary(a) → save_json(a) → save_binary(a)` 观察后续 `data_fetch` 失败。
- 建议：`catch` 里改为 `return false`（或保留原偏移并报错）；`save_json` 成功后清空 `m_offset` 并刷新 `m_path_fetch`。

### A4. `save_mmap` 不加热也不回填冷节点：导出的 `.mmap` 里这些节点的数据本体是空数组
- 位置：`network/astral/kernel.cpp:753-845`（`:791-807`，`:798` `std::vector<std::uint8_t> cb = astral::json::to_cbor(n.datas);`）
- 现状：`save_mmap` 是 `const` + 共享锁，既不做 `fetch_impl` 也不从原文件拷贝数据段；遍历时直接用内存里的 `n.datas`。冷节点此时是空数组 → blob 里写 1 字节 `0x80`、`index_record.data_size = 1`。
- 证据：`:798-801`；对照 `save_json`（`:485-493`）与 `save_binary`（`:637-657`）都专门处理了冷节点回填/原样拷贝。
- 影响：`load_index` 出来的库直接 `save_mmap` 导出的只读索引里，冷节点全读成「空数组」，而 `mmap_index` 侧没有任何「冷」标记可用于分辨（`mmap_index.cpp:86-88` 直接 `from_cbor` 该段）——只读查询据此给出错误的空结果。
- 复现/验证：未复现。现有两个 mmap 用例（`test/astral_test.cpp:133-144`、`test/astral_behavior_test.cpp:397-415`）都在**全热**库上验证，未覆盖冷库。
- 建议：导出前把冷节点加热（去掉 `const`），或检测到 `m_offset` 非空就直接返回 false。

### A5. `save_mmap` 不做原子替换 + `mmap_index::open` 只校验头部：截断的 `.mmap` 直接越界读
- 位置：写侧 `network/astral/kernel.cpp:825-844`（直接开 `path` + `trunc`）；读侧 `network/astral/mmap_index.cpp:44-63`（`:49-51` 校验，`:58-61` 直接按头部偏移算指针）
- 现状：写侧中途失败（磁盘满 / 进程被杀）会在目标路径留下截断文件，且旧索引已被 `trunc` 掉；读侧只确认「文件 ≥ 96 字节 且 `base[0..5] == "ASTRAL"` 且 `base[6] == 8`」，**不校验** `count` / `info_index_off` / `pool_off` / `blob_off` 是否落在文件内，随后 `get` / `find_info` 按 `count_` 二分并解引用 `pool_ + str_off`、`blob_ + data_off`。
- 证据：`mmap_index.cpp:53-61` 无任何上界比较（只比了 `map_.size() < HEADER_SIZE`）；`kernel.cpp:825` 直接写目标文件，对照 `save_json`（`:474`）与 `save_binary`（`:588`）都是 `path + ".tmp"` + rename。
- 影响：任何头部被改或被截断的 `.mmap`（含写入过程中崩溃）都会变成越界读 → 可能段错误；而 json / binary 两条路径在同样情形下是可见失败或可恢复的。
- 复现/验证：未复现（造一个「96 字节头 + 巨大 count」的文件即可触发；本轮未跑）。
- 建议：`open` 里按文件大小校验四个区间的上界；`save_mmap` 也走 tmp + rename。

### A6. `mmap_index::get` 的 `from_cbor` 没有 try/catch：损坏 blob 以异常穿透 `bool` 接口
- 位置：`network/astral/mmap_index.cpp:86-89`
- 现状：`data = nlohmann::json::from_cbor(cb);` 直接在函数体里调用，而 `get` 的签名是返回 `bool`、调用方按 false 处理错误；内核侧同一操作是包在 try 里的（`kernel.cpp:720-721`、`:544-552`）。
- 证据：`mmap_index.cpp:88`（无 try）；`get` 只在 `!data_idx_ || count_ == 0` 时提前返回 false（`:74`）。
- 影响：blob 被改/截断时抛 `nlohmann::json::parse_error` 穿出只读查询接口，宿主未捕获即终止；调用方写的错误分支永远不走。
- 复现/验证：未复现。
- 建议：包 try/catch，失败返回 false。

### A7. 加载损坏文件不是「返回 false」而是抛异常（`at()`），且短文件会比较未初始化的 `magic`
- 位置：`network/astral/kernel.cpp:114-116`（`magic`）、`:137`（`root.at("nodes")`）、`:155-156`（`j.at("off")` / `j.at("size")`）、`:434`（`j.at("uuid")`）
- 现状：`load_index_impl` 里唯一的 try/catch 只包 `from_cbor`（`:129-130`）；文件是合法 CBOR 但缺 `nodes` / `off` / `size` 字段时 `at()` 抛 `json::out_of_range`，直接穿出 `load_binary` / `load_index`（两者签名是 `bool`）。`load_json` 同理（`:423-424` 的 try 只包 `in >> root`，`:434` 的 `at("uuid")` 在 try 之外）。另外 `char magic[6]; in.read(magic, 6);` 不检查读了多少字节，文件不足 6 字节时 `std::memcmp` 比较的是未初始化栈内容。
- 证据：同文件其它字段一律用 `value(...)` 容错（`:139`、`:140`、`:147`、`:436`），说明这是遗漏而非风格选择。
- 影响：损坏/手改/跨版本的文件让「加载失败」变成异常（宿主可能崩）；截断文件上比较不确定值属 UB。
- 复现/验证：未复现（可用 `dd` 截断一个已知 `.bin`，或手改索引里的 `off` 字段后调 `load_index`）。
- 建议：整段包 try/catch（或统一 `value()`）；`magic` 初始化并校验 `gcount`。

### A8. `upsert` 可能命中同名标签节点，把标签写成数据节点（随后 `rebuild_names` 认不出 → 重启后同名标签重复创建）
- 位置：`network/astral/evoke.cpp:61-104`（`:67` 精确查 info、`:70` 取 `front()`、`:74-81` 追加 data）；标签入同一张索引见 `kernel.cpp:35-39` + `evoke.cpp:44-48`
- 现状：`create` / `upsert` / `submit` 建标签节点时走 `put_impl`，`index_add` 会把标签的 `info`（例如 `"agent"`）也写进 `m_info_index`；`upsert` 的查重只看 `info` 整串相等、不看节点类型，于是 `upsert(form, "agent", data, ...)` 会命中那个**标签节点**，把数据追加到它身上并以 `upserted = true` 返回标签的 uuid。
- 证据：`kernel.cpp:35-39`（`index_add` 对任何非空 `info` 都入库，不分节点类型）+ `evoke.cpp:44-48`（标签节点同样经 `put_impl`）；此后 `rebuild_names` 的候选判据是「`datas` 为空 + `links` 非空」（`evoke.cpp:298`），该节点已不符；`evoke::remove` 的级联也显式跳过 `datas` 非空者（`evoke.cpp:161`）。
- 影响：任何「数据节点的 info 与既有标签名相同」的用法（`upsert(form, 标签名, ...)`）会让标签变成数据节点：重启后 `rebuild_names` 不再认它，同名标签会被重复创建——正是 `rebuild_names` 想避免的后果；级联删除也不再回收它。
- 复现/验证：未复现（静态可判：`create(form, "x", data, "agent")` 建出标签后再 `upsert(form, "agent", data2, {})`，返回的是标签 uuid 且 `has` 的节点 `datas` 非空）。
- 建议：标签与数据节点分命名空间，或 `upsert` 跳过 `datas` 为空的候选。

### A9. `select_time` 从尾部「遇旧即 break」，依赖时间戳单调，而时间源是 `system_clock`（非单调）
- 位置：`network/astral/kernel.cpp:367-386`（`:373` 逆序遍历、`:381` `break`）；时间源 `:14-19`
- 现状：`select_time(t)` 从 `datas` 末尾往前扫，遇到第一条 `time < t` 就 `break`，即假定 `datas` 按 time 升序；而 `now_stamp()` 用 `std::chrono::system_clock`（可被 NTP/手工调时回拨），`update_data` 又是按插入顺序 `push_back`。
- 证据：`:373-383`（逆序 + `break`）；`:14-19`（`system_clock::now()`，全文无 `steady_clock`）。
- 影响：同一节点上只要出现一条「后写入但 time 更小」的记录（时钟回拨；API 层没有指定 time 写入的入口），`select_time` 就从那条起提前 `break`，静默少返回其后的新记录——调用方无法分辨「没有更多」与「被 break 截断」。
- 复现/验证：未复现（触发依赖系统时钟回拨或宿主直接改 `datas`）。
- 建议：改成不依赖顺序的全量过滤；若换成单调时钟，注意 `time` 是落盘字段、涉及历史数据口径。

---

## B. 资源与并发

### B1. `get()` 返回内部裸指针、锁已释放：跨锁使用（thing 三处、nebula `select_history`）在并发删除下是悬垂访问
- 位置：`network/astral/kernel.cpp:173-178`；使用点 `network/astral/thing.cpp:64`、`:74`、`:111`，`network/nebula/data.cpp:191-205`，`test/astral_stress_test.cpp:133`
- 现状：`std::shared_lock` 是 `get` 的局部变量，`return &(iter->second)` 时已析构。指针指向 `std::map` 节点（同节点地址在 erase 前稳定），但 `del_impl`（`kernel.cpp:79` `m_node_data.erase(iter)`，由 `remove` / `remove_node` / `evoke::remove` 到达）会立刻释放它。
- 证据：`kernel.cpp:66-81`（`del_impl` 的 erase）与 `:173-178`（返回内部指针）；`test/astral_stress_test.cpp:124-143` 的压测夹具本身就是 20 线程 `get` 与 `update_data` 并发（不过它只对指针判空、不解引用字段，所以夹具自身不触发）。
- 影响：任何「`get` 之后跨锁读字段」的调用方在并发删除下是 use-after-free；并发写（`update` 会整体重写 `datas`）同时读则是数据竞争（UB）。
- 复现/验证：未复现。可测入口：`astral_stress_test run <file>` 的夹具加一个删除线程，配 `-fsanitize=address,thread` 跑。
- 建议：提供返回拷贝/快照的接口，或把「持锁句柄」做成显式类型。

### B2. `ensure()` 不是原子的：`update_data` 与插入分属两段临界区，同 uuid 并发会互相覆盖 + `m_info_index` 出现重复项
- 位置：`network/astral/kernel.cpp:214-239`（`:220` 调 `update_data`，`:226-236` 另起一段锁插入）
- 现状：`update_data` 自带锁、返回即解锁；插入是第二个独立临界区。同一 uuid 两线程都走「`update_data` 失败 → 插入」时，后写者用 `m_node_data[n.uuid] = n`（`:233`）整体覆盖前者的 node（前者刚写的 data 记录消失），并且 `index_add`（`:234`）把该 uuid 第二次追加进 `m_info_index[n.info]`。
- 证据：`kernel.cpp:220` 与 `:227` 是两个互不嵌套的锁作用域；`kernel.h:78-88` 的 `update` 模板锁只覆盖自身；`kernel.cpp:35-39` 的 `index_add` 无条件 `push_back`。
- 影响：并发 `ensure` 丢写；`m_info_index` 里的重复 uuid 会让 `select_info` 对同一节点返回两条结果（`kernel.cpp:330-339` 按 id 逐个入结果集，不去重）。消费方：`network/nebula/data.cpp:47` 的 `data::create` 直接调 `ensure`。
- 复现/验证：未复现（静态可判）。
- 建议：把「查/建」合成一段临界区，或复用 `update` 模板。

### B3. `select*` 把 `fetch_impl` 的失败丢掉：源文件不可读或段损坏时静默漏节点，与「本来就没数据」不可区分
- 位置：`network/astral/kernel.h:99`（模板 `select`）、`network/astral/kernel.cpp:310`、`:334`（`select_uuid` / `select_info`）、`network/astral/evoke.cpp:297`（`rebuild_names`）
- 现状：`fetch_impl` 在「源路径为空 / 文件打不开 / `from_cbor` 抛异常 / 解出来不是数组」时保持冷状态并返回 `false`（`kernel.cpp:707`、`:721`、`:722-726`），四个调用点都不接收返回值；随后这些节点因 `datas` 为空被 `continue` 跳过（`:311`、`:335`），或被 `rebuild_names` 当成「不是标签」（`evoke.cpp:298`）。
- 证据：`kernel.cpp:703-729` 的失败分支与上述调用点（返回值均未使用）。
- 影响：`m_path_fetch` 失效（源文件被删/改名/被别的格式覆盖）或数据段损坏后，`select_*` 静默漏结果、`rebuild_names` 静默漏标签（重启后同名标签重复创建）；宿主拿到的只是「没查到」，没有任何失败信号。
- 复现/验证：未复现（把 `load_index` 的源文件改名后调 `select_words`，观察结果为空而非报错；本轮未跑）。
- 建议：把加热失败计数/错误码暴露给调用方。

### B4. `del_impl` 只回删「被删节点自己记的边」：单向边（thing 的父边、父流里的子引用）删完仍指向已消失的 uuid
- 位置：`network/astral/kernel.cpp:66-81`（`:71-78`）；单向边来源 `network/astral/thing.cpp:52-60`（`:57` `n.links[parent] = "parent";`）
- 现状：`del_impl` 的清理方向是「遍历被删节点的 `links`，把每个邻居表里指向我的那条边删掉」。而 `thing::event_set_parent` 只写子→父一条边（设计见 `thing.h:6-9`：父边单向、子事件引用留在父的 **data 流**里），父节点的 `links` 里根本没有子节点；于是删父之后，子节点的 `links` 仍留着已消失的父 uuid；删子之后，父流里的 `{type:"child", uuid}` 引用也仍指向已消失的 uuid。
- 证据：`kernel.cpp:71-78`（只按被删节点的 links 回删）；`thing.cpp:62-69`（`event_parent` 把 links 里第一个 `info == "parent"` 的 uuid 原样返回，不校验存在性）；`thing.cpp:80-84`（`event_children` 从 data 流取 uuid，同样不校验）。对称边没有这个问题：`set_connect_impl` 是双向写（`kernel.cpp:83-91`）。
- 影响：thing 层「删事件」后 `event_parent` / `event_children` 返回幽灵 uuid，调用方拿去 `has()` / `get()` 全是 false；表现为父子关系在图里不闭合。
- 复现/验证：未复现（静态可判：`event_create(name, c, parent)` → `remove_node(parent)` → `event_parent(child)` 返回被删 uuid）。
- 建议：删除时做反向扫描（或删除前先快照反向引用）；`thing` 的父边也可改用 `set_connect` 建双向。

### B5. `event_spawn` 三步各自加锁、返回值全丢：失败/崩溃留下半成品关系，无效 parent 也返回有效 child
- 位置：`network/astral/thing.cpp:39-50`
- 现状：`event_create` → `update_data(parent, ref)` → `event_set_parent(child, parent)`，三步各自加锁，且后两步返回值都不检查（`:47`、`:48`）。parent 不存在时两步都失败被丢弃，函数仍返回一个有效的 child uuid（`:49`）。
- 证据：`thing.cpp:42-48`；对照 `kernel.cpp:220` 的 `ensure` 至少检查了 `update_data` 的返回值，`thing.cpp:26-27` 的 `event_create` 也检查了 parent 是否全零。
- 影响：thing 层任何调用方；进程在两步之间退出/崩溃会留下「有子节点无父边」或「父流有引用无子节点」的半成品，且没有任何标志位可查。
- 复现/验证：未复现（静态可判：用随机 uuid 当 parent 调 `event_spawn`，返回的 child 存在而 `event_parent(child)` 为空）。
- 建议：先校验 parent 存在，再把三步收进一段临界区。

### B6. `loaded_size()` / `loaded_data()` 的口径把「空 `datas` 的标签节点」算成冷（`save_binary` 之后必现）
- 位置：`network/astral/kernel.cpp:685-691`（保存后重建 `m_offset`）、`:739-749`（口径）
- 现状：`save_binary` 成功后重建 `m_offset` 的判据只有 `n.datas.empty()`；而 `evoke` 的标签节点 `datas` 恒为空（`evoke.cpp:44-47` 只设 `info`）→ 每次保存后所有标签节点都被重新标成「未加载」。
- 证据：`kernel.cpp:687-690` + `evoke.cpp:44-48`；`kernel.cpp:742`（`count - m_offset.size()`）、`:748`（`m_offset.find(id) == end`）。测试口径见 `test/astral_behavior_test.cpp:385-391`。
- 影响：用这两个接口判断「热数据占比 / 是否需要预热」的宿主拿到错数；对标签节点调 `data_fetch` 会白读一次磁盘（读回一个空数组后 `m_offset` 才被清）。
- 复现/验证：未复现（静态可判：建带标签的库 → `save_binary` → `loaded_size() < count()`）。
- 建议：给标签节点单独标记，或把判据改成「`m_offset` 有项且 `datas` 确实未载入」。

### B7. `save_mmap` 把全库聚合进内存（`nodes` 全量拷贝 + `pool` + `blob`）：亿级库导出峰值 ≈ 全库数据 + 索引
- 位置：`network/astral/kernel.cpp:758-760`（`:760` `for (const auto& [id, n] : m_node_data) nodes.push_back(n);`）、`:775-807`、`:840-843`
- 现状：导出前先把所有 node **拷贝**进 `std::vector<node>`，再把整份 `pool`（info 字符串 + 每条边 16 字节 uuid）与整份 `blob`（每节点 `to_cbor(datas)`）堆在内存里，最后一次性写出。
- 证据：`:758-760`、`:791-807`、`:842-843`；`node` 内含 `json datas`（深拷贝），所以代价不止 1 倍。
- 影响：为「10 亿级」库导出索引时峰值内存 ≈ 全库数据 + 索引，与 mmap 路径「物理内存只占访问页」的定位相反。对照同文件的 `save_binary` 是两遍流式写（`:584-587` 的注释即为此设计），`test/astral_stress_test.cpp:54` 的造库器也特意避免全量入内存。
- 复现/验证：未复现（静态可判；可用 `astral_stress_test gen` 造的库再调 `save_mmap` 观察 RSS）。
- 建议：改成两遍流式写（先算偏移与记录，再逐节点落盘）。

---

## C. 跨平台

### C1. mmap 索引读侧硬编码小端、写侧本机字节序直写：大端宿主读写不一致
- 位置：写侧 `network/astral/kernel.cpp:828-843`（`out.write(reinterpret_cast<const char*>(&count), 8)`、定长记录整体写出）、`:594-598`（`.bin` 的 `isz`）；读侧 `network/astral/mmap_index.cpp:13-21`（`read_u64` 按字节 `<< 8*i` 拼装 = 小端）
- 现状：写侧所有 `xhqm::u64` 字段都是本机字节序直写，`index_record` / `str_index_record` 也靠 `#pragma pack(push,1)` 的整体 `write`（`mmap_index.h:10-33`）；读侧 `.bin` 用同样方式读（自洽，但文件不跨端序），`mmap_index` 的 `read_u64` 则明确按小端拼装 —— 即「写用本机序、读用小端」，只在 LE 上一致。
- 证据：`kernel.cpp:832-843` vs `mmap_index.cpp:13-21`；`mmap_index.h:32-33` 的两条 `static_assert` 只钉尺寸（64/32）不钉端序。
- 影响：x86-64 / ARM64（小端）无感；任何大端宿主上 `.mmap` 的 `count` / `info_index_off` / `pool_off` / `blob_off` / `str_off` / `data_size` 都解析成垃圾（`open` 还可能因魔数校验通过而继续），表现为崩溃或乱码数据。
- 复现/验证：未复现（需要大端宿主；本轮仅在 x86-64 上静态核对）。
- 建议：读写两侧统一显式小端序列化（或在头部加端序标志）。

### C2. `mmap_index` 是整文件映射：10 亿级索引只在 64 位宿主上成立
- 位置：`network/astral/mmap_index.cpp:44-63`（映射 + 指针算术）；底层 `network/mmap.cpp:75`（POSIX `::mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)`）、`:52`（Windows `MapViewOfFile(..., FILE_MAP_READ, 0, 0, 0)`）
- 现状：索引文件整体映射，`data_idx_` / `info_idx_` / `pool_` / `blob_` 都是 `base + 偏移` 的裸指针，`open` 对文件大小只要求 ≥ 96 字节。
- 证据：`mmap.cpp:52`、`:75`；`mmap_index.cpp:58-61`。
- 影响：32 位宿主上仅 data 索引（64 B × N）在 N 到亿级就已超过地址空间，`open` 会失败或映射被截断后越界（与 A5 叠加）；README 的「面向 10 亿级」实际隐含「64 位宿主」。
- 复现/验证：未复现（未在 32 位宿主上构建）。
- 建议：把「需 64 位宿主 + 索引文件大小上限」写进契约（并在 `open` 里校验 `st_size` 与头部一致性）。

### C3. `save_json` / `save_binary` 在 Windows 的 rename 回退是先 `remove` 再 `rename`：两步之间崩溃会丢整个库文件
- 位置：`network/astral/kernel.cpp:500-509`（`save_json`）、`:669-678`（`save_binary`）
- 现状：先试 `std::filesystem::rename(tmp, path)`——POSIX 下是原子覆盖；失败（Windows 上覆盖已存在文件会失败）则 `remove(path)` 再 `rename(tmp, path)`。若在 `remove` 之后、`rename` 之前进程被杀/掉电，目标库文件消失（只剩 `.tmp`，而代码在 rename 再次失败时还会 `remove(tmp)`：`:510-514`、`:679-683`）。
- 证据：`:500-509`、`:669-678` 的注释明确写了「优先 rename 直接覆盖（POSIX 原子），失败再 remove+rename（Windows 兼容）」。
- 影响：Windows 宿主在保存窗口内的非正常退出会丢掉整个库（文件不存在），POSIX 宿主不会；回退本身失败是可见的（返回 false），但「已删、未换」的中间态不可见。
- 复现/验证：未复现（未在 Windows 上构建/运行）。
- 建议：Windows 用 `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING)` / `ReplaceFile` 一步替换，而不是 remove + rename。

---

## D. 性能

### D1. 冷节点加热是「每节点一次 `ifstream` 打开 + 重读文件头」：全表扫描类查询 = N 次 open/seek
- 位置：`network/astral/kernel.cpp:703-729`（`:709` 每次调用来一次 `std::ifstream in(m_path_fetch, std::ios::binary)`，`:711-714` 重读 `isz`，`:716-718` 再 seek 到目标段）；调用点 `kernel.h:99`、`kernel.cpp:310`、`:334`、`evoke.cpp:297`
- 现状：`fetch_impl` 不缓存文件句柄，也不缓存 `blob_off`；模板 `select` 又对 `m_node_data` 里**每个**节点调一次（`kernel.h:97-99`）。
- 证据：`kernel.cpp:709-718`；`kernel.h:99`；`kernel.cpp:351-413`（`select_data` / `select_time` / `select_size` 都经由模板 `select`）；`evoke.cpp:214-246`（`select_links` 同）；`evoke.cpp:297`（`rebuild_names` 对被引用节点同样逐个加热）。
- 影响：冷库上任何一次全表类查询 = N 次 `open` + N 次读头 + 全部节点读入内存；`m_offset` 只减不增，没有冷却/驱逐，**惰性加载在查询路径上等于一次性全量加载**，且内存单向上涨。
- 复现/验证：未复现。廉价验法：冷库上调一次 `select_data({...})` 前后比较 `loaded_size()`（会从 0 跳到接近 `count()`），或 `strace -c -e trace=openat,read` 数系统调用。
- 建议：把源文件句柄与 `blob_off` 缓存成成员，或按批预读。

### D2. 查询路径的重复序列化：`select_data` 每节点 `dump()` 一次，`evoke::select_*` 又把结果 `to_json()` 一遍
- 位置：`network/astral/kernel.cpp:359`（`node.datas.back().dump().find(word)`）；`network/astral/evoke.cpp:241-245`、`:252-256`、`:263-267`、`:274-278`
- 现状：`select_data` 的谓词对每个节点构造一次最新记录的完整 JSON 字符串（`dump()` 序列化整棵子树）再做子串查找；`evoke::select_links` / `select_words` / `select_datas` / `select_nodes` 拿到 `node_list` 后又逐节点 `to_json()` 组装返回数组（结果集第二次序列化）。
- 证据：`kernel.cpp:359`；`evoke.cpp:243`、`:254`、`:265`、`:276`。
- 影响：全表扫描类查询的开销里序列化占大头（N 次 `dump()`），命中集再序列化一遍；命中多时是成倍的临时字符串分配。
- 复现/验证：未复现（可在夹具里对 `select_data` 计时；本轮未跑）。
- 建议：改用 json 对象遍历/cbor 直查做子串判断，或先按 info 索引缩小候选集。

---

## E. 未实现 / 占位桩

### E1. mmap v8 的 links 段（`links_off` / `link_count`，每边 16 字节）写了没有读取接口
- 位置：写侧 `network/astral/kernel.cpp:803-806`（`rec.links_off` / `rec.link_count` / `put_uuid`）、`:787-789`；读侧 `network/astral/mmap_index.h:36-59`（只有 `open` / `close` / `count` / `get` / `find_info`）
- 现状：导出时把每条边的 16 字节 uuid 追加进 `pool` 并记偏移，但 `mmap_index` 没有任何访问器把它们读出来（`get` 只回 `info` + `data`）。
- 证据：`kernel.cpp:803-806`、`:787-789`；`grep -n "links" network/astral/mmap_index.h` → 0 命中。
- 影响：这部分索引体积白占（每边 16 B × 边数）；「用只读索引做关联查询」目前无法落地，只能回内存 / `.bin` 走 `link_of`。
- 复现/验证：未复现（静态核对，命令如上）。
- 建议：要么补 `links(id, ...)` 访问器，要么先不写这段以省体积。

### E2. `evoke.cpp` 两个静态函数零调用（死代码）
- 位置：`network/astral/evoke.cpp:7-11`（`latest_time`）、`:13-20`（`top_latest`）
- 现状：两个 `static` 函数在本文件与全仓都没有调用点；`select_info` 的时间降序排序是另写的一份内联 lambda（`kernel.cpp:342-348`）。
- 证据：`grep -rn "latest_time\|top_latest" network/astral`（本轮实测：只有 `evoke.cpp:7` 与 `:13` 两处定义）。
- 影响：读代码时容易以为存在「按最新时间排序的公共路径」而去找它们。
- 复现/验证：上面这条 grep 即可。
- 建议：删掉，或把 `select_info` 的内联排序改为调用它。

### E3. `kernel::remove_node` 里留着注释掉的旧实现（占位桩）
- 位置：`network/astral/kernel.cpp:288-298`（`:290-293` 为注释块）
- 现状：旧谓词 `node.uuid == id` 被注释保留，现行实现改用恒真谓词 `return true`（`:294-297`）；两者语义等价（`remove` 模板已按 id 找到节点才会调谓词，`kernel.h:65-73`）。
- 证据：`:288-298`。
- 影响：看起来像「两种删法可切换」，实际是死注释；改这段的人容易误判。
- 复现/验证：`grep -n "remove_node" network/astral/kernel.cpp`。
- 建议：删注释，只留现行实现（或明确注释为何保留）。

### E4. 测试夹具未接线/已过时：3 个里只有 1 个能编
- 位置：`test/astral_test.cpp:3-4`、`test/astral_behavior_test.cpp:4`、`test/thing_demo.cpp:2` / `:7` / `:11` / `:58`；构建 `test/CMakeLists.txt:208-221`
- 现状：
  - `test/astral_test.cpp` 与 `test/astral_behavior_test.cpp` 仍 `#include "../network/astral/kernel_tag.h"` 并用 `kernel_tag`（`astral_test.cpp:12`、`astral_behavior_test.cpp:18` 等）——该头在 r677 **已不存在**（模块目录里没有，`CMakeLists.txt:333-337` 的头列表里也没有），这两个 target（`test/CMakeLists.txt:208-216`）按现状编不过；`test/README.md:58` 已记录「测试源仍写 `kernel_tag.h`/`kernel_tag`，待随测试迁移一并改名」。
  - `test/thing_demo.cpp` 用 `xhqm::thing::kernel`（`:7` `using namespace xhqm::thing;`、`:11` `thing::kernel k;`、`:58` `thing::kernel k2;`），而现行 `thing` 是 `xhqm::astral` 下的**类**、没有 `kernel` 成员（`thing.h:24-36`）；它也不在任何 CMake target 里（`grep -rn thing_demo` 全仓只命中它自己）。
  - 能编过并跑的是 `test/astral_stress_test.cpp`（`:6-7` 只引 `kernel.h` / `mmap_index.h`，`gen` / `run` 两个子命令，20 线程 × 10000 次、读写比 7:3 / 3:7）。
- 证据：上述行号 + `grep -n "astral_test\|astral_behavior_test\|astral_stress_test" test/CMakeLists.txt`（实测只命中 `:208`、`:213`、`:218` 三个 target）。
- 影响：astral 的可执行回归覆盖实际只剩并发压测一条；json / 二进制 / mmap 往返、级联删除、upsert、冷热加载这些**代码里已有**的验证项当前没有任何可跑夹具守护（改坏不会有人拦）。
- 复现/验证：`grep -n "kernel_tag" test/*.cpp` 与 `ls network/astral/kernel_tag.h`；要坐实「编不过」需按 `test/CMakeLists.txt` 配一次构建（**本轮未实跑构建**，结论来自 include 目标文件不存在 + 类型不存在）。
- 建议：把两个夹具迁到 `evoke` 名（`test/README.md:58` 的待办），并给 `thing_demo` 补 target 或改到 `xhqm::astral::thing`。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 内核 `datas` 只追加、不裁剪
- 位置：`network/astral/kernel.cpp:271`（写）、`:275-287`（唯一显式裁剪 `remove_time`）
- 现状：写路径无条件追加，压缩/合并/上限一概没有（详见 A2）。
- 理由：历史可追溯；写路径不必回读旧数据，也就不必为压缩背额外的锁与 IO；`remove_time` 已提供显式的、调用方主导的裁剪口径（`network/nebula` 的窗口语义就是「视图裁剪、历史留档」）。
- 代价：RSS 与磁盘随运行时长单调上升；消费方已被咬过（根 `DEBUG.md:59-64` 记的 `star_qqbot` 26.8 GB）。
- 证据：`push_back` 是唯一 datas 写点（`:271`），全文件无对 `datas` 的 `erase`/`resize`/上限判断。
- 影响：长驻写方（nebula `data::insert_data` 每帧一条）的 RSS 与落盘随运行时长增长（A2）；库越大 `save_*` 越慢（F3）。
- 复现/验证：`grep -n "push_back" network/astral/kernel.cpp`。
- 建议：不改成默认裁剪，只补一个显式上限/合并开关（默认关 = 与今天等价，属产品口径）。

### F2. 查询路径持排他锁
- 位置：`network/astral/kernel.h:95`（模板 `select`）、`:303`、`:321`（`select_uuid` / `select_info`）
- 现状：这些「读」接口用 `std::unique_lock`，与写互斥（点查用的 `count` / `has` / `get` / `link_of` / `data_of` 才走共享锁）。
- 理由：查询会对冷节点加热，而加热要改 `m_offset`（`kernel.cpp:727`）与 `datas`——持共享锁去改成员就是数据竞争；头文件注释明写「查询对冷节点自动加热（fetch_impl），非 const：查询可能触发按需加载」（`kernel.h:91-92`）。
- 代价：并发只读在 `select*` 上被串行化，读写锁退化为互斥；长查询会阻塞写（D1 又放大了单次查询耗时）。
- 证据：`select`（`kernel.h:95`）/ `select_uuid`（`kernel.cpp:303`）/ `select_info`（`:321`）用 `unique_lock`，点查类（`:165`/`:170`/`:175`/`:181`/`:192`）用 `shared_lock`。
- 影响：两个线程同时查同一个小库也要排队；冷库上单次 `select_data` 会因 D1 逐节点 open 而变长，期间所有写被挡住。
- 复现/验证：`grep -n "unique_lock\|shared_lock" network/astral/kernel.h network/astral/kernel.cpp`。
- 建议：若将来要并行读，先在点查路径（`get` / `data_of`）上加只读不加热的版本。

### F3. 保存是「整库重写 + 临时文件 + 原子替换」，没有增量写入与压缩
- 位置：`network/astral/kernel.cpp:474-509`（`save_json`）、`:588-678`（`save_binary` 两遍流式写）
- 现状：每次保存都重写全量索引与数据段（冷节点从旧文件原样分块拷贝，热节点 `to_cbor`），写出后 `rename` 覆盖；没有 WAL、没有增量 append、没有磁盘压缩。
- 理由：写入路径不依赖旧文件的可写性与一致性（只读旧库 + 原子替换），崩溃最多丢这一轮的 tmp；冷段原样拷贝避免了「读进来再写出去」的解码放大，这也是冷热分离能落地的前提。
- 代价：`save_*` 是 O(全库) 且需要 2 倍磁盘峰值；库越大保存越慢，且 `datas` 只增不减（F1）会让每次重写都搬全量历史。
- 证据：`save_json` 在 `:474` 建 `path + ".tmp"`，`save_binary` 在 `:588` 建 tmp，两者都用 `:502-509` / `:670-678` 的 rename 收尾；`save_binary` 的注释（`:584-587`）明写「全程无全量 blob / 全量索引 json 聚合」。
- 影响：写频繁 + 库大时，保存期间目标文件不可用（整库重写）、临时文件再占一份磁盘；崩溃只丢这一轮（这正是接受它的理由）。
- 复现/验证：`grep -n "\.tmp\|rename" network/astral/kernel.cpp`。
- 建议：保持整库重写；等真的有亿级写场景再谈增量（届时要连带 F1 的上限一起设计）。

### F4. 标签查重只做 `info` 整串相等
- 位置：`network/astral/evoke.cpp:67`（`m_info_index.find(info)`）；对照子串查询在 `kernel.cpp:328`
- 现状：`upsert` 的「命中」判据是 info 完全相同；大小写、同义词、包含关系都算不同标签（`select_words` 才是子串匹配）。
- 理由：`m_info_index` 是 `std::map<text, vector<uuid>>`，整串查是 O(log n) 且不需要倒排/分词，标签名由建库方掌握，语义边界清楚。
- 代价：命名不统一就会造出重复标签（`agent` / `Agent` / `agents`）；`info` 一对多时 `upsert` 只认 `front()`（`evoke.cpp:70`），同一 info 的多个节点会被并到一个上（与 A8 的标签碰撞相邻但不同：这条讲「同名不同物被并」的设计必然性）。
- 证据：`evoke.cpp:67` 只有一处 `m_info_index.find(info)`，是整串查；子串查在 `kernel.cpp:328`（`key.find(word) != text::npos`，另一个 API）。
- 影响：依赖 `upsert` 做「同名归并」的调用方必须自己保证命名规范（大小写/复数/前后空格），否则库里会攒出多个同义标签、要靠 `rebuild_names` 的引用计数取最大者来归并（`evoke.cpp:292-306`）。
- 复现/验证：`grep -n "m_info_index.find" network/astral/evoke.cpp`。
- 建议：不改判定方式；把命名归一化（大小写/复数）放在调用方。

### F5. `load_binary` 一次性全量加热（惰性只由 `load_index` 提供）
- 位置：`network/astral/kernel.cpp:518-561`（`:534-558` 对 `m_offset` 里所有节点逐个读出并 `m_offset.erase`）
- 现状：`load_binary` 的名字即语义——把 `.bin` 完整读进内存，返回后 `loaded_size() == count()`；想要「索引常驻、数据按需」必须改用 `load_index`。
- 理由：两种入口对应两种使用场景（小库/求简单 vs 大库/求内存），分开比给 `load_binary` 加开关更直白；`load_index` 需要的 `m_offset` / `m_path_fetch` 也正好是这条路径建立的。
- 代价：大库上误用 `load_binary` 会一次性吃满内存（且按节点逐个 `from_cbor`，峰值 = 全库 + 单节点副本）；文档若笼统说「惰性加载」容易让人以为 `load_binary` 也是惰性的（本模块 README 已按实测口径改写）。
- 证据：`:534-558` 对 `m_offset` 的每个 id 都 `seekg` + `read` + `from_cbor`，成功后 `m_offset.erase`（`:548`、`:556`）——遍历结束时偏移表为空，即全热。
- 影响：把「惰性」当成 `load_binary` 的属性去设计内存预算的宿主会在首载时超预算（亿级库直接 OOM）；反过来 `load_index` 的代价转移到 D1 的逐节点加热。
- 复现/验证：`grep -n "m_offset.erase" network/astral/kernel.cpp`（`:548`、`:556`）+ `test/astral_behavior_test.cpp:382-392`（冷热对照用的是 `load_index`）。
- 建议：保持两个入口；在宿主侧只按库规模选路径。

### F6. 标签复用逻辑在 `evoke` 里内联了四份（没抽公共函数）
- 位置：`network/astral/evoke.cpp:34-52`（`create`）、`:83-100`（`upsert` 命中分支）、`:115-132`（`upsert` 未命中分支）、`:176-193`（`submit`）
- 现状：四段代码逐字重复「查 `m_name_uuid` → 有则复用 uuid → 无则建 `link_node` + `put_impl` + 写名册 → `set_connect_impl`」，只有目标 uuid 的变量名不同。
- 理由：`upsert` / `submit` 进入时已持有 `m_mutex`，而 `std::shared_mutex` 非递归 —— 直接调 `create` 会自锁死锁（`evoke.cpp:105` 的注释「本函数已持锁，调 create 会重入加锁死锁」把这点写明了），所以持锁路径里内联是当前锁设计的必要代价；要抽公共函数得先有一个「不加锁的 `*_impl`」（内核那层已是这个套路：`put_impl` / `set_connect_impl` / `fetch_impl`）。
- 代价：四条路径的标签复用规则必须手动保持同步（空标签跳过、名册写入时机、`put_impl` 调用次序），任何一处漂移都会变成「同库不同入口行为不一致」。
- 证据：`grep -n "m_name_uuid.find" network/astral/evoke.cpp` → `:38`、`:86`、`:118`、`:180` 四处复用分支；配套的 `m_name_uuid[...] = ...` 在 `:48`、`:97`、`:129`、`:190`。
- 影响：改标签语义（例如 A8 的「跳过标签节点」修复、F4 的命名归一化）要四处同时改；漏改的入口会静默保留旧行为。
- 复现/验证：上面这条 grep。
- 建议：抽一个不加锁的 `link_impl(id, link)`，四个入口统一走它。

---

## G. 文档与代码的出入

### G1. `kernel.h:133` 的注释称 `save_json`「成功后刷新 `m_offset` 为新布局」，实现没有
- 位置：`network/astral/kernel.h:133`（注释）vs `network/astral/kernel.cpp:455-516`（实现）
- 现状：注释把 `save_json` 与 `save_binary`（`:135`）写成同一行为，但 `m_offset` 的写点只有 `:132` / `:153` / `:426` / `:548` / `:556` / `:687` / `:690`，都不在 `save_json` 体内；`save_json` 也不更新 `m_path_fetch`。
- 证据：`grep -n "m_offset" network/astral/kernel.cpp`（写点如上）；后果见 A3。
- 影响：读头文件的人会按注释以为 JSON 保存后偏移表已同步，从而放心做「load_binary → save_json 同路径 → save_binary」这类操作。
- 复现/验证：上面这条 grep；本模块 README 的 `已作废` 标注已同步订正。
- 建议：改注释或改实现（两者取其一，别停在注释）。

### G2. 仓库根 `README.md:260-262` 的 §4.2 旧结论与本模块现状不符（三条）
- 位置：仓库根 `README.md:260`（优势）、`:261`（劣势）、`:262`（缺失）
- 现状：
  - `:260`「三种持久化统一、保存原子 rename 并刷新 m_offset」——「原子 rename」只对 `save_json` / `save_binary` 成立（`kernel.cpp:502-509`、`:670-678`），`save_mmap` 是直接 `trunc` 目标文件、既无 tmp 也无 rename（`kernel.cpp:825`，见 A5）；「刷新 m_offset」只有 `save_binary` 做得到（`kernel.cpp:685-691`），`save_json` 全文不碰（见 G1 / A3）。同句「损坏数据段防护完善」也只是「`datas` 非数组按空数组处理」这一层：缺字段抛异常（A7）、`.mmap` 无边界校验（A5）、`from_cbor` 在 mmap 侧未包 try（A6）。
  - `:261`「info/标签查重依赖精确匹配，模糊查询全表扫描」——前半成立（`evoke.cpp:67` 整串相等），后半**对 info 不成立**：`select_words` 走 `m_info_index` 的键做子串匹配、只加热命中节点（`kernel.cpp:318-350`），并不扫全表；只有 link / data 两类（`select_links` / `select_datas`）是全表扫描 + 全量加热。
  - `:262`「无单测/基准」——**不成立**：本模块有 4 个夹具 / 3 个 CMake target（`test/astral_stress_test.cpp` 是能跑的并发压测兼基准，`test/CMakeLists.txt:208-221`），只有 `astral_test` / `astral_behavior_test` / `thing_demo` 三个处于编不过或未接线状态（E4）。同句「无二进制版本兼容策略文档」成立：读侧只认 `ver == 1`（`kernel.cpp:121`），仓库内除这句结论外没有迁移说明（`grep -rn "版本兼容\|兼容策略" --include=*.md .` 实测只命中本句与本文件）。
- 证据：上述行号 + `grep -n "astral" README.md`；模块内用例清单见 `test/README.md:57-59`。
- 影响：跨模块的读者会低估本模块的文档/测试现状、高估信息查询的成本，并误以为三条保存路径都有原子替换。
- 复现/验证：上面两条 grep 与 `test/` 目录实测。
- 建议：根 `README.md` 的下轮复核按本节口径订正（本模块只能改自己的文档，见文件开头的写权限约束）。

### G3. 依赖清单虚高：`../sha.h`、`<functional>`、`<set>` 在本模块内零使用
- 位置：`network/astral/astral.h:9`（`#include "../sha.h"`）、`:12`（`<functional>`）、`:15`（`<set>`）
- 现状：三个 include 在 9 个源文件里只有 include 行本身命中，没有任何 `sha` / `std::function` / `std::set` 的使用；`<map>`（`node::links`、`kernel::node_map`）与 `<vector>` 是真依赖。
- 证据：`grep -n "sha\|function\|std::set" network/astral/*.h network/astral/*.cpp`（只有 include 行与 `shared_mutex` 的误命中）。
- 影响：传递依赖变多（`sha.h` 会带进 `../md5.h` 等），编译时间与耦合面变大；读者从依赖列表推断模块能力时会高估（例如以为 uuid 生成用了 SHA）。
- 复现/验证：上面这条 grep；本模块 README 的「依赖」节已加实测补充。
- 建议：删掉未使用的 include（属代码改动，本轮不动）。

### G4. `test/README.md:57-59` 的 astral 测试清单漏了 `test/thing_demo.cpp`
- 位置：`test/README.md:57-59`（只列 `astral_test` / `astral_behavior_test` / `astral_stress_test` 三个 target）
- 现状：`test/thing_demo.cpp`（66 行，thing 层的演示程序）既没被列出、也没有任何 CMake target（`grep -rn thing_demo` 全仓只命中它自己），而且用的还是已不存在的 `xhqm::thing::kernel`（E4）。
- 证据：`test/README.md:57-59`；`grep -rn thing_demo .`；`test/CMakeLists.txt` 无该文件。
- 影响：照测试清单评估覆盖面的读者会看不到这个「看着像示例、实际跑不了」的文件；`test/README.md:58` 已注明另外两个夹具的旧名待办，但没提这个。
- 复现/验证：上面两条 grep。
- 建议：在 `test/README.md` 里补一条（或在 E4 的迁移里一并处理）。
