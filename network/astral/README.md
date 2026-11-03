# astral — 记忆服务内核

> 最后同步：2026-10-07，对照工作副本 r677。

`xhqm` 引擎的 `network/astral` 模块：一个「一切皆节点」的持久化记忆存储内核，提供内存主存、双向关联、惰性加载、CBOR/JSON/mmap 持久化，以及标签语义（`evoke`）、实体语义（`thing`）与只读二分索引。共 9 个源码文件、1890 行（实测 `wc -l`：`astral.h` 93、`kernel.h` 149、`kernel.cpp` 847、`evoke.h` 55、`evoke.cpp` 309、`thing.h` 70、`thing.cpp` 188、`mmap_index.h` 63、`mmap_index.cpp` 116）。

## 模块定位

astral 是记忆服务的底层存储引擎：把任意数据建模为**节点（node）**，节点之间用 `links` 建立双向关联，数据按时间戳追加在 `datas` 数组里。上层（服务端）负责 CRUD、查询与协议，本模块负责**存储结构、并发安全、持久化格式与索引**。

核心设计原则：

- **一切皆节点**：通用格式 `{uuid, info, datas[{time, form, json}], links[{uuid, info}]}`。
- **冷热分离（惰性加载）**：二进制文件中，索引与数据段分离；索引常驻内存，数据段按需回读（"冷节点加热"）。
  **补充（2026-10-07 复核 r677）**：惰性只在 `load_index` 路径成立 —— `load_index` 后 `loaded_size() == 0`（全冷），而 `load_binary` 会把 `m_offset` 里**所有**节点的数据段一次读完（`kernel.cpp:534-558`），返回时已全热；`save_binary` 之后又把 `datas` 为空的节点（`evoke` 的标签节点）重新标成冷（`kernel.cpp:687-690`）。故 `loaded_size()` / `loaded_data()` 的口径是「`m_offset` 里还剩几条」，不等于「真的没读过盘」。
- **读写锁并发**：~~`std::shared_mutex` 读共享、写独占~~ **已作废（2026-10-07 复核 r677）**：持**共享**锁的只有 `count` / `has` / `get` / `link_of` / `data_of` / `loaded_size` / `loaded_data` / `save_mmap`（`kernel.cpp:165/170/175/181/192/741/747/755`）；所有会加热或改库的路径（模板 `select`、`select_uuid`、`select_info`、`update`、`remove`、`ensure`、`create`、`save_json` / `save_binary`、`load_*`）都持 `unique_lock` **排他**锁（`kernel.h:64`、`:80`、`:95`、`:303`、`:321`），原因是加热本身要写 `m_offset`（`kernel.cpp:727`）。读写锁因此只在点查路径上真正并行。内部 `*_impl` 供已加锁方法重入，避免自锁死锁（`evoke.cpp:105` 的注释即为此）。

## 关键类 / API

### 基础类型（`astral.h`）

| 符号 | 说明 |
| --- | --- |
| `text` | `std::string` |
| `json` | `nlohmann::json` |
| `uuid` | 复用 `xhqm::cipher::uuid`（16 字节） |
| `version` | `20260901`，格式版本号 |
| `struct node` | 节点：`uuid`、`info`、`datas`（json 数组）、`links`（`map<uuid, text>`） |

`node` 提供 `to_json(full_data, with_link)`、`datas_json()`（损坏数据段按空数组防护）、`links_uuid()` / `links_info()`。

### `xhqm::astral::kernel`（`kernel.h` / `kernel.cpp`）

节点主存 + 双向关联 + 持久化。关键成员：

- 主存：`node_map m_node_data`（`uuid → node`）、`info_map m_info_index`（`info → uuid 列表`，find-info 索引）、`offset m_offset`（冷节点数据段偏移）。
- 增删改查：`create` / `ensure` / `update` / `remove` / `update_info` / `update_data` / `remove_time` / `remove_node`。
- 查询：`select_uuid` / `select_info` / `select_data` / `select_time` / `select_size`，以及模板化 `select(form, func, ...)`。
- 关联：`set_connect` / `dis_connect`。
- 持久化：`load_json` / `save_json` / `load_binary` / `save_binary` / `load_index` / `data_fetch`。
- 惰性加载：`loaded_size` / `loaded_data`。
- mmap 索引导出：`save_mmap(path)`（定长 v8，供 `mmap_index` 二分查找）。

### `xhqm::astral::evoke`（`evoke.h` / `evoke.cpp`）

标签语义层，继承 `kernel`：`link_node` 与 `data_node` 都是 `node` 的别名（标签节点 `datas` 为空、只被引用；数据节点带 `datas`）。核心：

- `create(form, info, data, links)` / `create(form, info, data, link)`：新建数据节点并复用/新建标签。
- `upsert(form, info, data, links, &upserted)`：按 `info` 精确查重（对 `m_info_index` 的键做**整串相等**查找，`evoke.cpp:67`）；命中则追加 data + 补 links（`upserted=true`），未命中新建（`false`），`out_upserted` 可为 `nullptr`。`info` 不是唯一键，同一 `info` 有多个节点时只取该键 uuid 列表的**首个**（`evoke.cpp:70`），且标签节点与数据节点共用同一张 `info` 索引（`kernel.cpp:35-39`）——`upsert` 可能命中一个同名标签节点，见 `DEBUG.md` A8。
- `remove(form, id, time)`：`time=0` 删整个节点并级联清理关联；`time>0` 删该时间点之前的 data（节点保留）。
- `submit(form, id, links)` / `submit(form, id, link)`：追加关联标签；`submit(form, id, data)`：追加数据；`update(form, id, info)`：更新描述。
- `select_links` / `select_words` / `select_datas` / `select_nodes`：link / info / data 模糊匹配与 uuid 精准匹配，均返回 json。
  **补充（2026-10-07 复核 r677）**：四者只有 `select_words` 走索引 —— 它遍历 `m_info_index` 的**键**做子串匹配、只加热命中节点（`kernel.cpp:318-350`）；`select_links` / `select_datas` 走模板 `select`，即全表扫描 + 逐节点加热 + 只取最新一条 data 的 `dump()` 子串匹配（`kernel.h:97-107`、`kernel.cpp:351-366`、`evoke.cpp:214-246`）；`select_nodes` 是 uuid 点查（`kernel.cpp:300-317`）。
- `rebuild_names()`：重建库内 `name → uuid` 表（识别「被引用 + data 为空」的标签节点），服务重启后必须调用，否则标签会重复创建。

### `xhqm::astral::thing`（`thing.h` / `thing.cpp`）

实体层（`evoke` 之上的认知记忆扩展）：把 node 分成两类——`events`（`info = "event:<事件名>"`，links 里有向父边，data 是事件流，可含子事件引用）与 `object`（`info = "object:<物品名>"`，links 是双向关联，data 是「完整属性快照 + 溯源 prov」流）。提供：

- events：`event_create` / `event_append` / `event_spawn` / `event_set_parent` / `event_parent` / `event_children`；
- object：`object_create` / `object_set` / `object_link` / `object_get`（读取最后一帧 → `snapshot{id, name, attrs}`）；
  **补充（2026-10-07 复核 r677）**：`object_get` 与 `event_children` 建在 `kernel::get()` 上，而 `get()` **不加热冷节点** —— 对 `load_index` 出来的冷库直接调用会拿到 `false` / 空数组（`thing.cpp:111-112`、`:74-76`），必须先 `data_fetch(id)` 或先走一次 `select*`（`query_object` 因先调 `select_info` 而正常）；`event_parent` 读的是 `links`，而 `links` 存在索引里，冷库也能用。详见 `DEBUG.md` A1。
- 身份消解与检索：`merge(a, b, reason, confidence, &primary)`、`query_object(word)`、`query_event(word)`。

### `xhqm::astral::mmap_index`（`mmap_index.h` / `mmap_index.cpp`）

只读 mmap 索引，面向 10 亿级规模，物理内存只占访问页：

- `index_record`（64 字节，按 uuid 排序）、`str_index_record`（32 字节，按字符串排序）；两条 `static_assert` 把尺寸钉死（`mmap_index.h:32-33`）。
- `open(path)` / `close()` / `count()`；`open` 校验「文件 ≥ 96 字节 + `"ASTRAL"` 魔数 + `base[6] == 8`」（`mmap_index.cpp:49-51`）。
- `get(id, info, data)`：uuid 二分查找，填 info 与 data 本体。
- `find_info(info)`：info 二级索引二分查找，返回所有同 info 节点的 uuid。
  **补充（2026-10-07 复核 r677）**：`index_record` 里还写了 `links_off` / `link_count`（每边 16 字节 uuid，`kernel.cpp:803-806`），但本类**没有任何读取 links 的接口**（`DEBUG.md` E1）；索引里的 data 本体是 `save_mmap` 当场 `to_cbor(n.datas)` 的拷贝，源库里仍是冷节点的部分会写成空数组（`kernel.cpp:798`，`DEBUG.md` A4）。

## 架构

```
┌────────────────────────────────────────────┐
│              服务端 (上层协议)              │
└──────────────────┬─────────────────────────┘
                   │
        ┌──────────▼──────────┐
        │ evoke / thing (语义) │  data_node ↔ link_node；events/object
        └──────────┬──────────┘
                   │
        ┌──────────▼──────────┐
        │     kernel (内核)    │  主存 + 双向 links + info 索引 + 冷热分离
        └──────┬─────────┬────┘
               │         │
   ┌───────────▼───┐  ┌──▼──────────────┐
   │ json/binary   │  │ save_mmap → v8  │
   │ 持久化 (CBOR)  │  │ mmap_index 只读 │
   └───────────────┘  └─────────────────┘
```

**二进制持久化格式（v1）**：`"ASTRAL"` 魔数 + 版本/标志 + `isz`(索引 CBOR 长度) + 索引 CBOR（含每节点 `uuid/info/links/off/size`）+ 数据段（每节点 CBOR）。索引与数据段分离正是冷热加载的基础。读侧只认 `ver == 1`（`kernel.cpp:121`），没有版本迁移/兼容分支；`isz` 与定长记录都是本机字节序直写、没有跨端序转换（`kernel.cpp:594-598`、`mmap_index.cpp:13-21`，见 `DEBUG.md` C1）。写出走「临时文件 `path + ".tmp"` → `rename` 覆盖」，冷节点段从旧文件按 1 MB 分块原样拷贝（`kernel.cpp:588`、`:634-657`、`:669-678`）。

**mmap 索引格式（v8）**：96 字节头 + 定长 data 索引（64B×N）+ info 二级索引（32B×N）+ 变长字符串池 + data blob。头部字段实测偏移：`count` @8、`info_index_off` @24、`pool_off` @32、`blob_off` @40（写见 `kernel.cpp:819-844`，读见 `mmap_index.cpp:53-61`）。

## 子模块

| 文件 | 职责 |
| --- | --- |
| `astral.h` | 基础类型别名 + `node` 结构 |
| `kernel.h` / `kernel.cpp` | 内核：主存、关联、查询、json/binary 持久化、惰性加载、mmap 导出 |
| `evoke.h` / `evoke.cpp` | 标签语义层：data/link 节点、upsert、rebuild_names（原 `kernel_tag`） |
| `thing.h` / `thing.cpp` | 实体语义层：events（父子事件流）/ object（属性快照 + 溯源）、merge、query |
| `mmap_index.h` / `mmap_index.cpp` | 只读 mmap 二级索引与二分查找 |

## 依赖

- 引擎内部：`../../head.h`、`../../algebra/itype.h`、`../../algebra/ftype.h`、`../mmap.h`（`xhqm::mmap`）、`../uuid.h`（`cipher::uuid`，16 字节 `std::array<xhqm::byte,16>`，`uuid.h:21`）、`../sha.h`。
- 第三方：`nlohmann/json`（含 CBOR：`to_cbor` / `from_cbor`）。
- 标准库：`<shared_mutex>`、`<chrono>`、`<fstream>`、`<filesystem>`、`<algorithm>`、`<map>`、`<set>`、`<vector>`。
  **补充（2026-10-07 复核 r677）**：其中 `../sha.h`、`<functional>`、`<set>` 在本模块内**零使用**（只在 `astral.h:9/12/15` 的 `#include` 行命中，见 `DEBUG.md` G3）。

## 简要用法示例

从仓库根按模块路径包含（`-I.` 时的真实写法）；本模块由开关 `XHQM_BUILD_NETWORK` 带进库（默认 OFF，见下节）。下面每一行都取自本仓测试里的真实用法（`test/astral_behavior_test.cpp`、`test/astral_test.cpp`、`test/astral_stress_test.cpp`）。

```cpp
#include "network/astral/evoke.h"

using namespace xhqm::astral;

// 1. 打开或加载持久化文件
evoke store;
if (!store.load_binary("memory.astral")) {
    // 新库，忽略
}

// 2. 服务重启后重建标签表
store.rebuild_names();

// 3. 新建带标签的数据节点（links 用 std::vector<text> 显式构造：
//    create 另有「单个 link」重载，花括号列表会二义）
uuid id = store.create("note", "星辉澪", json{{"text", "hello"}},
    std::vector<text>{"agent", "mira"});

// 4. upsert：按 info 查重，命中追加 data
bool upserted = false;
store.upsert("note", "星辉澪", json{{"text", "again"}}, {"agent"}, &upserted);

// 5. 查询
json words = store.select_words({"星辉澪"});   // info 模糊
json datas = store.select_datas({"hello"});    // 最新一条 data 关键字
json links = store.select_links({"agent"});    // 标签模糊
json nodes = store.select_nodes({id});         // uuid 精准

// 6. 持久化（冷热节点都会正确写回）
store.save_binary("memory.astral");

// 7. 导出只读 mmap 索引（供海量只读查询）
store.save_mmap("memory.mmap");
mmap_index idx;
if (idx.open("memory.mmap")) {
    text info; json data;
    if (idx.get(id, info, data)) {
        // 命中
    }
    auto all = idx.find_info("星辉澪");
}
```

冷热两条加载路径的差别（`test/astral_behavior_test.cpp:374-394` 是现成对照）：

```cpp
// A. 全量加载：load_binary 把每个节点的数据段都读进内存，返回后全热
kernel hot;
hot.load_binary("memory.astral");      // 之后 get(id)->datas 直接可用

// B. 懒加载：load_index 只载索引，数据段留在文件里
kernel cold;
cold.load_index("memory.astral");
// cold.loaded_size() == 0，cold.get(id)->datas 是空数组（get 不加热！）
cold.data_fetch(id);                   // 单点加热
// 或
auto rows = cold.select_data({"hello"});   // 全表扫描路径，顺带把所有冷节点读热
```

## 构建与自检

模块由 CMake 开关 `XHQM_BUILD_NETWORK` 带进库（默认 **OFF**，`CMakeLists.txt:42`）；两条构建路径的文件集一致：头 `CMakeLists.txt:333-337` / `xhqm.vcxproj:29-33`，源 `CMakeLists.txt:374-377` / `xhqm.vcxproj:88-91`（9 个文件全在，**没有 astral 专属开关**）。

自检入口（按构建文件接线写出，**本轮未实跑构建**）：`test/` 是独立工程，`astral_stress_test` 链 `../../binary/libxhqm.a`（Windows 为 `binary/xhqm.lib`，`test/CMakeLists.txt:203-221`）：

```bash
cmake -S . -B build -DXHQM_BUILD_NETWORK=ON -DXHQM_BUILD_TENSOR=ON -DXHQM_DEVICE=AUTO && cmake --build build -j
cd test && cmake -S . -B build && cmake --build build -j --target astral_stress_test
./build/astral_stress_test gen /tmp/astral.bin 1000000   # 流式造库（不把全量节点载入内存）
./build/astral_stress_test run /tmp/astral.bin           # load_index 后 20 线程 × 10000 次，7:3 / 3:7 两轮
```

`astral_stress_test` 是当前唯一编得过的 astral 夹具；另两个靶子的源文件仍 `#include` 已删除的 `kernel_tag.h`（`astral_test.cpp:3`、`astral_behavior_test.cpp:4`，`test/README.md:58` 已记为待迁移），`test/thing_demo.cpp` 用的 `xhqm::thing::kernel` 也已不存在（见 `DEBUG.md` E4）。

## 当前问题

本模块**当前仍存在**的缺陷、取舍与文档漂移记在同目录 [`DEBUG.md`](DEBUG.md)，此处不重抄（35 条：A 功能缺陷 / B 资源并发 / C 跨平台 / D 性能 / E 未实现 / F 设计取舍 / G 文档出入）。库层面的同一条（`datas` 只增不减）另见仓库根 `DEBUG.md:59-64`；历史台账 `fantasy/fantasy_bugs.md`、`network/nebula/DEFERRED.md` 保持原名原位置，只被指向、不被搬动。

## 说明

- **线程安全**：所有公共方法均加 `std::shared_mutex`；模板方法 `update` / `remove` / `select` 在持锁下调用 `fetch_impl`，故冷节点加热是重入安全的。
  **补充（2026-10-07 复核 r677）**：`get()` 返回的是主存内部节点的**裸指针**（`kernel.cpp:173-178`），锁在 `return` 时就释放了 —— 该指针只在「调用者自己保证没人在动这张表」时可用；跨锁使用（`thing.cpp:64/74/111`、`network/nebula/data.cpp:191`）在并发 `remove_node` 下是悬垂访问（`DEBUG.md` B1）。要跨锁就用 `data_of()` 取拷贝，或自己持外部锁。
- **命名变更**：原 `kernel_tag.h` / `kernel_tag.cpp` 已改名 `evoke.h` / `evoke.cpp`（类 `kernel_tag` → `evoke`，功能不变）；`astral::thing` 是后加的实体语义层。仓库其它位置仍有旧名残留（两个测试夹具 + `test/README.md:58`），见 `DEBUG.md` E4 / G4。
- **冷节点一致性**：`save_json` / `save_binary` 若原文件不可读会拒绝保存，避免把冷节点写成空数据；~~保存成功后刷新 `m_offset` 为新布局~~ **已作废（2026-10-07 复核 r677）**：只有 `save_binary` 在成功后刷新 `m_offset`（`kernel.cpp:685-691`，返回 `false` 时不动），`save_json` 全文不碰 `m_offset`（`grep -n m_offset network/astral/kernel.cpp` 的写点只有 `:132` / `:153` / `:426` / `:548` / `:556` / `:687` / `:690`）。因此「先 `load_index` / `load_binary`、再 `save_json` 到**同一路径**」的格式转换会让 `m_path_fetch` 指向已改写成 JSON 的文件、而 `m_offset` 还是旧二进制偏移：后续加热解码失败（保持冷态），后续 `save_binary` 则可能把 JSON 字节当 CBOR 段拷进新库（`kernel.cpp:638-657`，见 `DEBUG.md` A3）。
- **损坏防护**：`datas` 非数组一律按空数组处理；单节点 CBOR 解码失败保持冷状态，不中断整体加载（`fetch_impl` 的失败会被调用方丢掉，见 `DEBUG.md` B3）。
