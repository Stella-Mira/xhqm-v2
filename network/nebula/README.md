# xhqm/nebula 模块（B 世代）

> 最后同步：2026-10-07，对照工作副本 r677。

## 模块定位

`xhqm::nebula` 是 xhqm 引擎的**分布式网络层**：每个真实节点里自带一整套同构栈，节点按自己的组合逻辑挑选使用哪些能力。它只依赖 `xhqm::network`（TCP/UDP/帧协议）、`xhqm::cipher`（uuid）与 `xhqm::astral`（记忆内核），不依赖任何中心服务。

设计原则（代码注释里称「零号协议」）：

- **事件即帧，无序等价**：一切跨节点动作都是 `{type, ...}` 的 JSON 事件；事件队列无限，多 worker 乱序执行合法，帧序不设保障。
- **问不等答**：发送方不等回执（带 ACK 的短连只是可选加速项）；没有路径的事件自然丢弃。
- **软状态**：邻居、边、订阅都由「最近一次听到」维持，过期即自然消失（心跳/老化「顺带」完成，除心跳外无周期线程）。
- **全息节点**：每个节点都持有完整栈（addr/data/life/link/find/post/zero），下层语义声明不动即可随意重构。

**本文件以工作副本的代码为准**（~~`svn` 工作副本 r656 + 之后未提交的改动~~ **已作废（2026-10-07 复核 r677）**：对照的是工作副本 **r677**；旧文的 r656 是本节重写当时的版本号，早已过期），凡与旧描述冲突处以代码为准；**「当前仍存在」的问题清单已收口到 [`DEBUG.md`](DEBUG.md)**（A 功能性缺陷 / B 资源与并发 / C 跨平台 / D 性能 / E 未实现 / F 设计取舍「不修」/ G 文档出入，每条带 `文件:行号`），历史口径见本文件末尾的「复核发现（r666 逐文件复读）」。

---

## 目录结构

```
network/nebula/
├── nebula.h / nebula.cpp      node 基类（uuid/name/info + make_json）+ 类型别名 + version()
├── addr.h                     addr 层聚合头 + addr 类型别名 + addr_less + addr_from/from_addr 声明
├── addr/
│   ├── addr_b.h / .cpp        addr_b 地址抽象：邻居表 + frame_signal/broad_signal + kind 词表 + hardware_allow
│   ├── addr_uuid.h / .cpp     进程内回环地址：进程级注册表，send/broad 直接发射信号（无线程）
│   ├── addr_ipv4.h / .cpp     IPv4：TCP 长连接 + UDP 广播，单 reactor（poll 多路复用）
│   ├── addr_ipv6.h / .cpp     IPv6：TCP 长连接 + 链路本地组播 ff02::1，单 reactor
│   └── addr_serial.h / .cpp   串口：点对点长链路（frame_stream 帧化），每缆读/写两线程
├── data.h / data.cpp          通道树 + 实时窗口 + astral 记忆内核落盘/查询
├── life.h / life.cpp          事件池：life_event 入池 → 线程池 → exec_event（分层分发入口）
├── link.h / link.cpp          L1 有界度邻居图：入网 / 心跳判死 / 事件词表
├── find.h / find.cpp          L2 区域图发现与路由：layer/graph 事件 + dijkstra 最短路径 + 逐跳转发
├── post.h / post.cpp          L3 订阅推送：care/omit/push + 拥有者侧 publish 扇出
├── zero.h / zero.cpp          零号协议顶层：kit 工具表 + logic/proxy/claim/message 词表 + config 便捷构造
└── star.h                     组件组合模板：type（可插拔组件基类）+ star<...>
```

## 分层（继承链）

继承链决定「事件先给谁」；`addr` 是旁挂的地址层（同样派生自 `node`）：

```
node                        身份基类：uuid / name / info / make_json
├── addr_b                  地址抽象：邻居表 + 双信号 + send_broad/send_frame
│   ├── addr_uuid           回环（进程内注册表）
│   ├── addr_ipv4           IPv4（TCP 长连接 + UDP 广播，单 reactor）
│   ├── addr_ipv6           IPv6（TCP 长连接 + 组播，单 reactor）
│   └── addr_serial         串口（帧化字节流，每缆读/写线程）
└── data                    通道树 + 实时窗口 + astral::kernel 记忆内核
    └── life                事件池：life_event 入池 → exec_event 逐层分发
        └── link            L1 有界度邻居图（0<=m<=x<=n）+ 心跳线程
            └── find        L2 区域图发现 / dijkstra 路由 / 逐跳转发
                └── post    L3 订阅推送
                     └── zero      零号协议顶层：kit 工具表 + 词表 + config 组装构造
                            └── star      组件组合模板（type 组件 + star<...>）
```

## 事件流水线

1. **入口** 有两种：
   - 本地自驱：`life_event(make_xxx(...))`（[`life.h`](life.h)）——校验事件是对象且带 `type`，否则丢弃，然后提交事件池；
   - 网络到达：地址信号的槽调 `link::recv_event(uuid, event)`——补 `recv_addr`（来源地址 uuid）与 `recv_time`（纳秒）后转 `life_event`。
2. **分发** `exec_event(json)`（虚函数，返回 `text`）：**基类优先**——`zero::exec_event` 先调 `post::exec_event`，一路到 `find` → `link` → `life`。每层处理自己认识且基类未认领的 `type` 后把返回值置空；返回非空表示「本层不处理」，继续向上抛。
3. **同层自驱/远端**：本地 `life_event(make_xxx(...))` 与远端帧走同一条 `exec_event` 路；`send` 事件负责把载荷投给 `goto` 指定的直连邻居。

事件的 `type` 词表（按归属层）：

| `type` | 层 | 投递方式 | 语义 |
|---|---|---|---|
| `ping` | link | 私发（无 ACK） | 心跳：命中邻居则刷新 `last`、清零 `fail`、置 `alive`；载荷带 `send_node` |
| `bind` | link | 广播 | `x<m` 求邻：收到即乐观挂表（前提 `x<n`），并回一条 `link` |
| `link` | link | 私发（`send` 载荷） | 应答挂表：`x<m` 才挂，否则丢弃（单边关系由心跳超时自愈） |
| `send` | link | 私发 | 包装 `{goto 路由目标, data 载荷}`；发出前盖 `send_time`/`send_node`/`send_addr` |
| `dath` | link | 本地（心跳线程驱动） | 判死清理：`fail = (now-last)/心跳间隔`，超上限移除邻居；find/post 各自 override 追加维护 |
| `layer` | find | 私发逐跳 | 区域图询问；`deep` 递减、`wait` 为预算，事件 id 去重防环 |
| `graph` | find | 私发回程 | 图数据合并：节点按 uuid 幂等覆盖，边规范化后刷新时间戳 |
| `care` / `omit` | post | 定向（find 逐跳中继到 `goal`） | 订阅 / 取消订阅某通道；到达通道拥有者后由它登记订阅者 |
| `push` | post | 定向（find 逐跳中继到 `goal`） | 订阅数据到达 → `care_hander(data, from, channel)` |
| `logic` | zero | 本地或网络 | 执行节点自逻辑 `logic(info)` |
| `proxy` | zero | 私发 | 委托执行 kit 列表，结果经 `message` 回 |
| `claim` | zero | 广播或私发 | 索取节点 `info`（当前只回 `info`，见限制），结果经 `message` 回 |
| `message` | zero | 私发 / 广播（`goto` 空） | 通知 → `data_signal(event)`（整个事件，载荷在 `data` 键） |

## 各层 API

### 1. `node`（[`nebula.h`](nebula.h) / [`nebula.cpp`](nebula.cpp)）

类型别名：`sint/uint/byte/size/text/json/uuid/uuid_less`；`nebula::version()` 返回 `"nebula 1.0"`。

```cpp
class node {
    uuid m_uuid; text m_name; json m_info;
    void uuid(const uuid&); void name(const text&); void info(const json&);
    const uuid& uuid() const; const text& name() const; const json& info() const;
    virtual json make_json() const;   // {uuid, name, info}
    virtual ~node() = default;
};
```

### 2. `addr`（地址层）

```cpp
enum class kind { no, uuid, ipv4, ipv6, serial, bluetooth, nearlink, quantum };
text kind_text(const kind&);  kind text_kind(const text&);  size hardware_allow();
using addr = std::shared_ptr<addr_b>;

class addr_b : public node, public signal_receiver {   // 地址抽象（继承 signal_receiver：销毁自动断槽）
    bool alive(); size fail(); size last(); kind type(); bool is_open();
    void alive(bool); void fail(size); void last(size);
    bool has_neighbor(const uuid&); bool remove_neighbor(const uuid&);
    void insert_neighbor(const shared_ptr<addr_b>&);   // 挂表即 last=now, fail=0, alive=true
    size size_neighbors() const;
    template<typename F> void for_neighbors(F&&, bool with_lock);
    template<typename F> void run_neighbors(const uuid id, F&&, bool with_lock);

    virtual bool opened() = 0;                       // 启动网络 core（幂等）
    virtual void closed() = 0;                       // 关闭通道（停 reactor 并唤醒阻塞）
    virtual bool send_broad(const json&) = 0;        // 广播（无 ACK）
    virtual bool send_frame(const json&) = 0;        // 直达（无 ACK）
    virtual bool send_frame(const json&, sint) = 0;  // 直达（timeout_ms：等「已交给内核」/「队列排空」，非投递确认；addr_uuid 忽略该参数）

    signal<const json&> frame_signal;   // 事件帧到达（TCP 通道）
    signal<const json&> broad_signal;   // 广播帧到达（UDP/组播通道）
};
```

四种实现的实际语义：

| 实现 | 构造 | 定向（`send_frame`） | 广播（`send_broad`） | `make_json` 追加键 |
|---|---|---|---|---|
| `addr_uuid` | `addr_uuid(uuid)` | 按 uuid 查进程级注册表，命中则把帧发到该端点的转发槽 | 进程级静态广播信号（进程内所有 `addr_uuid`） | — |
| `addr_ipv4` | `(host, port, broad)` | 进程内同 uuid 端点有则直投；否则长连接入队（必要时拨号） | UDP 广播 `{broad}`；无 UDP 腿时退化为进程内广播 | `host`/`port`/`broad` |
| `addr_ipv6` | `(host, port, broad)` | 同上（单 reactor） | 链路本地组播 `ff02::1`（每个已加入接口一份）；无组播腿时退化为进程内广播 | `host`/`port`/`broad` |
| `addr_serial` | `(host, port=baud)` | 进程内同 uuid 端点有则直投；否则走本进程持有的那条串口链路 | **只做进程内同 kind 广播**（串口无物理广播，见限制） | `host`/`port`/`baud` |

- **`addr_uuid`**：首个同 uuid 的实例进注册表；之后的实例（含 `addr_from` 造的克隆）把自身的 `frame_signal` 连到注册表里那个端点的转发槽，于是「发向克隆」＝「投给注册者」。`send_frame` 不按 `is_open()` 拦截（克隆句柄永不 `opened()`，一拦邻居方向的帧就全丢）；析构会退注册表（否则留悬垂指针）。回环只在本进程内有效。**注意槽池**：这条克隆转发槽与进程级 `uuid_broad` 用的是**默认 `signal_auto`**（`addr/addr_uuid.cpp:32`、`:34`），异线程触发时会走 `xhqm::signals::pool()` —— 宿主没在 `main` 首行装池（`signals::pool_life()`）即抛 `xhqm::error`；`link::life()` 自己连的收帧槽是显式 `signal_direct`（`link.cpp:35-36`），不受影响。详见 [`DEBUG.md`](DEBUG.md) 的 B 组。
- **`addr_ipv4`**：一个端点 = 一个 TCP 监听 + 一个 UDP 广播套接字 + 一对回环唤醒管，由**一条 reactor 线程**用 poll 多路复用（accept / 读 / 写 / UDP 收在同一份 poll 集合里）；连接一律非阻塞，发队列有界（`s_outbox = 1024`，满丢最旧），入队即写 1 字节唤醒管让本轮把队列抽干（`drain-until-empty`）。两张进程级静态表：`ipv4_registry`（uuid→已打开端点，供进程内直投）与 `ipv4_conns`（对端 uuid→长连接，端点与它的克隆句柄共用同一条）。`reconcile_live` 在轮次之间收敛双方同时拨号产生的重复连接（uuid 较大的一方收掉自己 dial 的那条，且绝不动唯一一条）。空闲 30s 无收发即收连接；`closed()` 到 reactor 真正退出最坏 ≈ `s_react_ms`(200ms) + 本轮 handler 耗时。
- `addr_ipv4` 的 `make_json` 在 `host` 为空/`0.0.0.0`/`*` 时用默认路由出口地址顶替（`local_ipv4_hint`），避免把通配地址播给对端导致对端无法回连。
- **克隆句柄**（`addr_from` 造、被 link 挂进邻居表的那种）只带对端 `host/port/uuid`，是**发送侧路由句柄**：自己不 `opened()`、不进端点注册表，把拨号与收帧交给「把它挂进邻居表的本地端点」；找不到宿主端点时才自己兜底。
- **工厂**：`addr addr_from(const json&)`（按 `kind` 造 `uuid`/`ipv4`/`ipv6`/`serial`，其余 kind 返回空句柄；读 `host`/`port`/`broad`，串口的 `port` 或 `baud` 都认，之后套用 `uuid`/`name`/`info`）、`json from_addr(const std::vector<addr>&)`。

### 3. `data`（通道 + 记忆）

```cpp
enum class channel_kind { none, node_c, device, source };   // 三级树：节点通道 ← 设备通道 ← 数据源通道

struct window : node {
    uuid parent; channel_kind kind; std::deque<std::pair<u64, astral::json>> view;  // {纳秒, 帧}
};

// 通道
std::vector<uuid> channels(const uuid& parent) const;
uuid create(channel_kind kind, const text& name = "", const astral::json& info = {});
void remove(const uuid& id);                       // 级联移除内存里的全部子通道
bool connect(const uuid& child, const uuid& parent);
bool updata_info(const uuid& id, const text& name, const astral::json& info);
bool insert_data(const uuid& id, const astral::json& frame);
// 查询（三者同形：[{time, data}, ...]）
astral::json select_message(const uuid& id);       // 最新一帧 {time, data}
astral::json select_windows(const uuid& id);       // 实时滑动窗口
astral::json select_history(const uuid& id);       // 内核落库历史
template<typename F, typename...A> astral::json select_kernal(const uuid& id, F&&, A&&...);
// 窗口上限
void duration(size);  void capacity(size);         // 0 = 不限
```

- `create(channel_kind::node_c)` 强制通道 uuid = 本节点 uuid（节点通道即节点身份）；其余类型现造 v4。
- 每次 `create` / `updata_info` / `insert_data` / `connect` 都同步进 `astral::kernel`（`ensure`/`update_info`/`update_data`/`set_connect`）；`path` 非空时构造读盘、析构落盘 —— 历史不丢。
- **内核条目形状**：`{time, form, json:{...}}`。`insert_data` 写入的内层是 `{type:"insert", data:帧}`，`create`/`updata_info` 写入的内层是 `{type:"ensure"|"update", name, kind, info}`（**不带 `data`**，是建档桩）。查询解包统一由 `data::frame_record` 负责：时间取**外层** `rec.time`，载荷取内层 `json.data`，不带 `data` 的桩条目跳过 —— `select_history` 与 `select_kernal` 共用它，因此与 `select_windows` 同形（`select_kernal` 额外先用调用方的谓词过滤原始条目）。
- 窗口淘汰在 `insert_data` 里「顺带」完成：先按 `capacity` 条数、再按 `duration` 时长（毫秒）从队首淘汰。
- `remove` 只删内存窗口（级联子通道），**内核历史保留**（有意：历史是落库资产）。

### 4. `life`（事件池）

```cpp
size bound_m() const; size bound_n() const;      // 连接下限/上限（构造时保证 n>=m）
void join(); void dath();                        // 等池 / 收摊（析构自动收摊）
void start(); void sleep();                      // 恢复派发 / 暂停派发
virtual text exec_event(const json&);            // 分层分发（返回未认领的 type）
void life_event(const json&);                    // 异步入口：校验后提交事件池
```

池参数来自构造 `(path, m, n, c, a, limt, time)`：`c/a` 是线程数下限/上限，`limt` 是任务上限（`xhqm::npos` = 不限），`time` 是任务保活毫秒。`xhqm::parallel::threads` 对 `c == 0` 或 `a < c` 会抛异常，故 `c/a` 的派生必须保证 `1 <= c <= a`（见 zero 的 `pool_c`/`pool_a`）。

### 5. `link`（L1 有界度邻居图）

```cpp
void heart_time(uint t, uint f, uint s);   // 心跳间隔 ms / 失联上限 / 发送超时 ms（默认 1000/10/1000）
size degree() const;                       // 实际连接数（= 各自身地址的邻居表大小之和）
bool life();  bool stop();                 // 启停：连信号 → 起/停心跳线程 → 开/关地址 reactor
void insert_addr(const addr&);  void remove_addr(const addr&);   // 把地址句柄交给节点（必须外部调用）
bool insert_neighbor(const uuid& a_id, const addr&, bool force);  bool remove_neighbor(const uuid&);
std::vector<uuid> neighbors_uuid() const;  std::vector<addr> neighbors_addr() const;
bool has_neighbor(const uuid&) const;
```

- **地址是外部注入的**：`config` 里的 `answer`/`listen` 键没有代码读取，模块内也没有任何 `insert_addr` 调用者 —— 造出的节点默认没有地址、`degree()` 恒 0，装配方必须自己 `insert_addr(std::make_shared<addr_ipv4>(host, port))` 之类，再调 `life()`。
- `life()` 先 `opened()` 再断后连信号：`frame_signal → link::recv_event`、`broad_signal → link::recv_event`，最后起心跳线程。
- 心跳循环：`send_heart(make_ping())` → `life_event(make_dath())` → `degree() < m` 时 `send_broad(make_bind())`，然后按 `m_t` 补足睡眠。
- 入网规则：收到 `bind` 时 `x<n` 才挂表并回一条 `link`；收到 `link` 时 `x<m` 才挂表；`x` 用 `degree()` 判定，`insert_neighbor` 再兜一次上限（`force` 可越过）。遍历邻居表时先快照，再做阻塞网络 IO。
- `send_heart` 只盖 `send_time`/`send_node`；`send_broad`/`exec_send` 额外按每个自身地址盖 `send_addr`（对端据此建反向句柄）。

### 6. `find`（L2 区域图发现 / 路由）

```cpp
struct graph {
    std::map<uuid, node, uuid_less> nodes;                        // 身份缓存（不过期）
    std::map<edge, std::chrono::steady_clock::time_point> edges;  // 边 → 本地单调戳
    json make_json() const;
    void with_json(const json&);                                  // 幂等合并
    void clear(std::chrono::steady_clock::duration timeout);      // 时长老化剔边
    void clear(const uuid& base, const size& pathcount);          // 按 base 的跳数半径剔边
    std::vector<uuid> short_path(const uuid& from, const uuid& to) const;   // dijkstra，单位权=跳数
};

void updata_graph(size deep, u64 wait);                   // 发一轮 layer 询问
std::vector<uuid> short_path(const uuid& uuid) const;     // 自身 → 目标 的节点序列
std::chrono::seconds region_aging() const;  void region_aging(std::chrono::seconds);   // 默认 60s
```

- `layer` 逐跳转发（`deep-1`，除来源外邻居都转，事件 id 去重），`deep<1` 时回 `graph`；回程按 `size` 计数递减（非 root 节点在收到询问时把 `size` 记成 `degree()-1`，即「等几个子图」），归零者把**自己累计的区域图**回给 `from`。询问预算 `wait × size`（夹在 1s..600s）。
- `with_json` 合并远端图：节点按 uuid 幂等合并（多地址取并集）、边规范化（小 uuid 在前）后刷新时间戳；只有「整份报告都与 `base` 相关」（即 `base` 自己的星型图）才具备判死权威，否则只续命不删边。
- `short_path` 把区域图临时适配成 `lattice/graph_algorithms.hpp` 的 dijkstra 节点契约，单位权 = 跳数；起点/终点不在视图或链断则返回空。
- `exec_relay` 是 L2 唯一选路处：带 `goal` 且 `goal != 我` 的事件转发一跳（事件 id 30s 去重防环），`goal == 我` 或无 `goal` 则交上层；`next_hop` 要求下一跳是**真正直连的邻居**（图上边不代表我能发包）。
- `exec_dath` 里顺带维护：超时未回图的询问强制只等自己（`size=1`，注入自身图自完成）、老化 `clear(m_region_aging)`、清理转发去重表、每跳再发一轮 `deep=1` 的 `layer` 刷新软状态。

### 7. `post`（L3 订阅推送）

```cpp
struct push_state { size fail; std::set<uuid, uuid_less> channel; };

size max_fail() const;  void max_fail(size);           // 默认 1
void care(const uuid& owner, const uuid& channel);     // 订阅（goal = 通道拥有者）
void omit(const uuid& owner, const uuid& channel);     // 退订（同上）
void push(const json& data, const uuid& subscriber, const uuid& channel);   // 定向推送
size publish(const json& data, const uuid& channel);   // 拥有者侧：扇出给全部订阅者，返回成功份数
std::vector<uuid> subscribers(const uuid& channel) const;
signal<const json&, const uuid&, const uuid&> care_hander;   // 订阅数据到达（data, from, channel）
```

- 信封 `make_post` 带 `id/make/from/goal/channel/data?`；`send_goal` 先做可达性检查（直连邻居，或图上路径里存在我的一个直连邻居）再入池：`goal == 我` → 本层处理，否则 `find::exec_relay` 逐跳转发（post 自己不算路、不维护途经表）。
- 订阅表是两张索引：`m_channel`（通道 → 订阅者，推送时按通道直接取人）与 `m_subscribers`（订阅者 → 失败数 + 订过的通道）。内层集合必须显式用 `uuid_less`（默认 `std::less<uuid>` 不区分不同 uuid）。
- `care`/`omit` 由**订阅者**发起、`goal` = 通道拥有者；拥有者登记/注销订阅者。推不出去计一次 `fail`，`max_fail` 到了或已无通道则 `dath` 时摘掉。

### 8. `zero`（工具封装 + 组装）

```cpp
class kit : public node {
    kit& ket_make(fun);  kit& ket_name(text);  kit& ket_info(json);  kit& ket_uuid(uuid);
    kit& param(name, type, desc, required);      // string_param / number_param / boolean_param / object_param
    json operator()(const json& input);          // 异常吞掉返回空对象
    operator bool() const;                       // 未绑函数为假
};

std::vector<kit> kitset();  bool insert_tool(const kit&);  bool remove_tool(const kit&);
void broad_claim(const json&);  void create_message(const json&);   // {goto, data}

virtual void logic(const json&) = 0;             // 节点自逻辑（自驱 = life_event(make_logic({}))）
signal<const json&> data_signal;                 // message 到达（整个事件）
```

- kit 以 `uuid().to_text()` 为键，`param` 生成 JSON Schema 片段（`inputSchema`）供模型/前端消费；`insert_tool` 是「新插入=成功、重复=拒绝」的 insert 语义。
- 加锁纪律（两把锁，互不嵌套）：`m_tool_mutex` 只保护工具表，`exec_proxy` **只在锁内取工具快照、在锁外执行**；`m_logic_mutex` 让 `exec_logic` 串行，且不与工具表锁共用 —— 否则用户 `logic()` 里再 `insert_tool` 就是自死锁。
- 词表：`logic`（执行 `logic(info)`）、`proxy`（按 `{uuid, argv}` 列表执行 kit，`argv` **原样下发任意 JSON**、缺省 `{}`，结果经 `message` 回 `from`）、`claim`（把自身 `info` 经 `message` 回 `from`）、`message`（`goto` 空 → 广播；`goto` = 自己 → 本地投递；否则包成 `send` 经路由）。
- `message` 走 `data_signal`（整个事件，载荷在 `data` 键），不是虚函数钩子。

### 9. `star`（组件组合模板）

```cpp
class type : public signal_receiver {            // 可插拔组件基类
    explicit type(zero* hand);                   // hand 为空则抛 xhqm::error("nebula::zero is nullptr")
    virtual text type_name() const = 0;          // 纯虚：如实报出本组件类型名（如 "type_device"）
    virtual json type_info() const;              // 通用声明 {type, kind, text, tools}（tools 取 _hand->kitset()）
    const json& info() const;  void info(const json&);
};

template<typename ...types>
class star : public zero {                       // 组件构造时 new、析构时 delete；不可拷贝
    explicit star(const json& config);           // name = config["name"]（缺省 "star"），info = config["info"]（非 null 时）
    virtual text star_name() const = 0;          // 纯虚：节点自己的类型名（如 "star_device"）
    virtual json zero_info() override;           // claim 应答正文：info["list"] = {uuid, star, name, types, tools}
};
```

- ~~旧文只列了 `class type` 的 `type(zero* hand)` / `info()` 与 `class star` 的 `star(config)`（name/info 取自 config）。~~ **已作废（2026-10-07 复核 r677）**：那三件事本身没错，但**不全** —— `star.h` 里 `type_name()`（`:32`）与 `star_name()`（`:78`）是纯虚、`type_info()`（`:39-49`）与 `zero_info()`（`:79-109`）由基类给出实现；照旧文写组件/节点会编不过（见下）。
- `type_name()` 与 `star_name()` **都是纯虚**（`star.h:32`、`star.h:78`）：谁忘了写就编不过，而不是悄悄借基类的名字。
- `star::zero_info()`（`star.h:79-109`）就地实现 `zero` 的纯虚 `zero_info()`：**节点名不在顶层**，在 `info["list"]["name"]`（`info["list"]["types"]` 是各组件 `type_info()` 的数组）；JSON 键仍叫 `types`，但局部变量必须叫别的名字 —— 局部变量与模板参数 `...types` 同名会遮蔽模板参数，GCC 11 直接报错（实测会让 stella 32 个目标全挂）。
- 组件（`type` 派生）在自己构造里注册工具、连 `care_hander`/`data_signal`；`star<...>` 负责组合与生命周期。`zero::logic` 仍是纯虚，具体节点（`star<...>` 的派生类）自己实现；**直接派生 `zero` 的节点还必须实现 `zero_info()`**（`zero.h:172` 纯虚），否则是抽象类、`my_node node(config);` 编不过。

---

## 用法示例：自定义节点

```cpp
#include "network/nebula/zero.h"
#include "network/nebula/addr.h"

class my_node : public xhqm::nebula::zero
{
public:
    explicit my_node(const xhqm::nebula::json& config)
        : xhqm::nebula::zero(config, 3, 9)          // m=3, n=9（c/a 由 pool_c/pool_a 派生，不读 config 里的 c/a）
    {
        name("my_node");
        info(xhqm::nebula::json{ { "text", "demo" } });
    }

    // 地址必须由装配方注入：config 里的 answer/listen 无人读取。
    // 这段是消费方 stella 的真实写法（star_device.h:50-59）：地址写在 config["addr"] 数组里，
    // 自身地址的 uuid 必须取自节点 uuid（缺省补上；对端才用克隆句柄）。
    void attach(const xhqm::nebula::json& config)
    {
        for (const auto& a : config.value("addr", xhqm::nebula::json::array()))
        {
            if (!a.is_object()) continue;
            xhqm::nebula::json one = a;
            if (!one.contains("uuid") || one.value("uuid", "").empty())
            {
                one["uuid"] = this->uuid().to_text();
            }
            if (xhqm::nebula::addr ad = xhqm::nebula::addr_from(one)) { insert_addr(ad); }
        }
    }

    // zero::logic 纯虚：本节点自逻辑（自驱 = life_event(make_logic(json::object()))）
    virtual void logic(const xhqm::nebula::json& value) override
    {
        auto ch = create(xhqm::nebula::data::channel_kind::source, "sensor.0");
        insert_data(ch, xhqm::nebula::json{ { "v", 36.5 } });
    }

    // zero::zero_info 纯虚：claim 应答的正文（远端 claim 到了就把它经 message 回给发起方）
    virtual xhqm::nebula::json zero_info() override
    {
        return xhqm::nebula::json{ { "uuid", uuid().to_text() }, { "name", name() }, { "info", info() } };
    }

    // 订阅数据到达（care/push 的结果）
    void on_care(const xhqm::nebula::json& data, const xhqm::nebula::uuid& from,
        const xhqm::nebula::uuid& channel)
    {
        insert_data(channel, data);
    }
};

xhqm::nebula::json config = {
    { "path",   "node.mem" },                       // 空串 = 纯内存
    { "m", 3 }, { "n", 9 },                         // 连接下限/上限（n < m 时构造内夹回 m）
    { "size", xhqm::npos },                         // 池任务上限（npos = 不限；"size":-1 经 nlohmann 转 size_t 也恰等于 npos）
    { "time", 1000 },                               // 池任务保活毫秒
    { "addr", xhqm::nebula::json::array({ xhqm::nebula::json{
        { "kind", "ipv4" }, { "host", "0.0.0.0" }, { "port", 9000 }, { "broad", 9100 } } }) }
};

xhqm::signals::pool_life();     // 宿主 main 首行：装进程级信号线程池（用到 addr_uuid 回环/异线程槽时必须，见「构建」）
my_node node(config);
node.attach(config);
node.care_hander.connect(&node, &my_node::on_care, xhqm::signal_type::signal_direct);
node.life();                            // 启动地址 reactor + 事件信号 + 心跳线程
node.start();                           // 启动事件池派发
// 运行期：node.broad_claim(xhqm::nebula::json{ { "broad", true } });      // 广播索取各节点声明（应答走 data_signal）
//         node.create_message({ { "goto", peer }, { "data", ... } });    // 定向消息（goto = 自己 → 本地投递）
node.dath();                            // 收摊（等池退出、关 reactor、停心跳）
```

> **已作废（2026-10-07 复核 r677）**：旧示例的 `my_node` 只实现了 `logic()`（旧文还写着 `node.claim({ {"broad", true} })`）；实际 `zero::zero_info()` 自 r676 起是**纯虚**（`zero.h:172`）⇒ 旧示例是抽象类、`my_node node(config);` 编不过（同一原因让 `build/nebula_b_check.cpp` 也编不过，见 [`DEBUG.md`](DEBUG.md) 的 G 组），而 `claim` 这个成员函数**从来不存在**，真实入口是 `broad_claim(...)`（`zero.h:164`）与 `make_claim(...)`（`zero.h:117`）。上面已补齐。

只做回环联调时用 `addr_uuid`：`addr_from({"kind":"uuid","uuid":...})` 造的是**克隆句柄**——能发向已注册的对端，但自己收不到直达帧。要让同进程的两个节点互相直达，需自己 `std::make_shared<xhqm::nebula::addr_uuid>()` 建端点并 `insert_addr`。

## 线程模型

| 线程 | 归属 | 说明 |
|---|---|---|
| 事件池 | `life`（`xhqm::parallel::threads`） | 所有 `life_event` 在这里执行 `exec_event`；多 worker 乱序合法 |
| 心跳线程 | `link` | 每 `m_t` 毫秒：发 `ping` + 本地 `dath` +（`x<m` 时）广播 `bind` |
| reactor 线程 | `addr_ipv4` / `addr_ipv6` | 一条 poll 线程管住 监听 accept + 长连读写 + UDP/组播收 + 唤醒管 |
| 每缆 2 线程 | `addr_serial` | 读线程（`frame_stream::recv` → `frame_signal`）+ 写线程（发队列 → `frame_stream::send`，兼空闲看门狗）；唤醒粒度 100ms |
| 无 | `addr_uuid` | 回环直接发信号，不起线程 |

跨线程关闭依赖超时唤醒（Windows 上 `close` 不唤醒阻塞中的 accept/recv/poll），`closed()` 到线程真正退出最坏 ≈ poll 超时（ipv4/ipv6 200ms、串口 100ms）+ 本轮 handler 耗时。

资源回收（无泄漏是硬要求）：

- 套接字：ipv4/ipv6 的 监听 / 广播（组播）/ 唤醒管 句柄都在 `closed()` 里 reactor join **之后**关闭，`opened()` 的每条失败分支也各自 `sock_close` 掉刚建的句柄；串口句柄刻意不在 `closed()` 关，而在 `serial_port_stream` 析构里关（避免「句柄先关、读线程还阻塞在 ReadFile」）。
- 连接/链路：ipv4/ipv6 的 `conn` 是 `shared_ptr`，同时挂在 本对象表（`m_in`）/ reactor 表（`m_live`）/ 进程级表 三处，摘除统一走 `stop_conn`（清标志 + 关套接字 + 退进程级表）与 `conn_drop`；`closed()` 按 `owner == this` 把进程级表里的残留扫干净，串口另有 `~conn` 兜底 join 两线程。
- 线程：reactor / 心跳 / 读写线程全部在 `closed()`/`stop()` 里 join，最坏延迟即上面的 poll 超时。
- 信号槽：`~signal_receiver` 只把 alive 标志置假，**不从槽表里移除**（emit 时跳过）——所以需要反复连接的场合必须显式 `disconnect_all()`（`link::life()` 就是这么做的），`addr_uuid` 用的是 `disconnect(this)`。

## 构建

- 13 个 TU：`nebula.cpp`、`addr.cpp`、`addr/addr_b.cpp`、`addr/addr_uuid.cpp`、`addr/addr_ipv4.cpp`、`addr/addr_ipv6.cpp`、`addr/addr_serial.cpp`、`data.cpp`、`life.cpp`、`link.cpp`、`find.cpp`、`post.cpp`、`zero.cpp`。
- 根 `CMakeLists.txt` 的 `XHQM_HEAD_NETWORK` / `XHQM_BODY_NETWORK` 与 `xhqm.vcxproj`（含 `.filters`）收录同一份文件集（`XHQM_BUILD_NETWORK=ON`）。
- 只开 NETWORK 时还需要 `software/signal.cpp`（`signals::pool()`，`frame_signal`/`broad_signal` 的槽池），它已在 `XHQM_BODY_CORE` 里无条件编入。
- **槽池要显式安装**（r658 起）：`signals::pool()` 在未安装时直接抛 `xhqm::error`（`software/signal.cpp:17-18`），宿主 `main` 首行必须调 `xhqm::signals::pool_life();`（声明 `software/signal.h:49`，实现 `software/signal.cpp:35-38`，内部是 `xhqm::getInstance<_pool_life_>()`）。**注意**：`signal.cpp:18` 的抛错文案建议的 `xhqm::getInstance<signals::pool_life>();` **本身编不过**（`signals::pool_life` 是函数不是类型；实测 GCC 报 `type/value mismatch ... expected a type, got 'xhqm::signals::pool_life'`）——照抛错文案抄会撞墙，写函数调用即可。
- nebula 自己**不装**池（库不替宿主做决定，模块内无 `pool_life` 调用）：`link::life()` 连的收帧槽是显式 `signal_direct`（`link.cpp:35-36`），不受影响；但 `addr_uuid` 的克隆转发槽与进程级 `uuid_broad` 用的是默认 `signal_auto`（`addr_uuid.cpp:32`、`:34`）——异线程触发时会走池（`signal.h:148-163`），未安装即抛。**已知不对称点**：消费方 stella 的 `make/star_device/main.cpp` 全链路没调 `pool_life()`（其余 `make/star_*/main.cpp` 都调了），后果未在本机复现；见 [`DEBUG.md`](DEBUG.md) 的 B 组。

## 依赖

- 引擎内部：`../../head.h`、`../uuid.h`、`../../algebra/itype.h`、`../../lattice/graph_algorithms.hpp`（dijkstra）、`../astral/kernel.h`（记忆内核）、`../../software/signal.h`（信号槽）、`../../software/system/{timesys,filesys,threads,process}.h`；传输实现另用 `../tcp.h`、`../udp.h`、`../cs.h`（`frame_stream`）。
- 第三方：`../json.hpp`（nlohmann 3.11.2，经 `head.h` 引入）。

## 与 A 世代的差异（迁移参考）

| A 世代 | B 世代 |
|---|---|
| `addr_t<kind>` 主模板 + `link_b`/`link_t<kind>` | `addr_b` 抽象 + `addr_uuid`/`addr_ipv4`/`addr_ipv6`/`addr_serial` 具体类；`link` 单类，地址由句柄注入（`insert_addr`） |
| `zero` 作 L1 基础类 | `zero` 是组装层，L1 归 `link` |
| `route`（同步发现，含线程池） | 无：`find` 单类，事件驱动 + dijkstra + 逐跳 `exec_relay` |
| `server` / `sensor`（数据层 + 采集节点） | `data`（通道/窗口/记忆）+ 用户节点（`zero`/`star`） |
| `notify` / `sse_server` / `sse_client` | 已移除；推送由 `post` 承载（`httplib.h` 保留但当前无人引用） |
| `store` / `astral_store` / `query` | 已移除；落库与历史查询并入 `data` + `astral::kernel` |
| `tool` 命名空间 + `tool::kit` | 并入 `zero`：`zero::kit` |

## 已知限制 / 待办

**当前仍存在的问题清单在 [`DEBUG.md`](DEBUG.md)**（A 功能性缺陷 / B 资源与并发 / C 跨平台 / D 性能 / E 未实现 / F 设计取舍「不修 + 理由」/ G 文档与代码的出入，每条带 `文件:行号`、影响与验证方式）——本节不再重抄。

- 本节 2026-10-07 之前的 **11 条旧文已逐条核对**：结论仍成立的按性质搬进 `DEBUG.md`（F 组的「不修」条目正是旧「已知限制」里那些有意为之的取舍）；被推翻的（如「ipv6 收满前无硬上限」）原文与订正留在下面「复核发现（r666 逐文件复读）」的 C 组订正表里，未删除。
- 旧文里还有一条**本轮无法核对**的（外部树 `api_nebula`）：原文逐字保留在 [`DEBUG.md`](DEBUG.md) 的 G8，连同核对结论（本工作副本与仓库根都没有 `api_nebula/`）。
- `test/polydtor_test.cpp` / `test/nebula_api/` 仍是 A 世代 API（`addr_t`/`to_json`/`clone`）、且 `test/CMakeLists.txt` 的 `polydtor_test` 目标缺 `addr/` 下 5 个 TU ⇒ 该目标链不过：这条仍在，归 `DEBUG.md` 的 E3。
- 成熟度：**中高** —— 四层能力（传输/路由/订阅/工具）都有具体实现且各传输的线程模型、有界队列、唤醒与收摊路径完整，缺的是 **`find` 判死口径**（现会误删活边）、**内核历史裁剪**（只增不减）与 **ipv4 的帧长/入向缓冲闸**；消费方需要自己补的只有「地址装配」与「`pool_life()`」两件事。

---

## 复核发现（r666 逐文件复读）

> **本节是 r666 的复读记录，按「历史不删」保留为主表**（论证过程、判据与已复现步骤都在这里）；
> 「**当前仍存在**」的收敛版在 [`DEBUG.md`](DEBUG.md)（同一批缺陷编号不变：A1 / B1–B5、帧长闸口径、F 组「不修」取舍、以及本轮新发现的槽池不对称点）。
>
> 本节记录**对工作副本 r666 逐文件复读**得到的缺陷与订正，不是设计意图说明。
> 分级：**已复现** = 本次写了独立复现程序并跑出结果；**代码级** = 逐行读出的确定性缺陷，未造运行复现。
> 复现程序与 `build/nebula_b_check.cpp` 同一惯例：临时程序，不进源码树、不进构建系统。

### A. 已复现

#### A1. `find::graph` 的「判死」会误删活边 → L2 路由抖动

**判据只查了一半。** `exec_graph` 恒以 `base = 本节点` 调 `with_json`（[`find.cpp`](find.cpp) 内 `m_region_graph.with_json(json{ {"base",this->uuid().to_text()}, ... })`），
而「判死权威」的条件是「**报告里的每一条边**都碰 `base`」（[`find.cpp`](find.cpp) 中 `bool star = true;` → `if (base && (n == base || l == base)) reported.insert(e); else star = false;` → `if (!base || links == 0 || !star || reported.empty()) return;`）。
它只验证「报告的边是否都碰我」，**不验证「我的边是否都报全了」**。

于是**只有一个邻居的叶子**回图时，报告恰好只含 `{我, 叶子}` ⇒ `star == true` ⇒ 判死分支执行，
把**我那些没出现在这份报告里的其他边**当死边删掉。

**复现**（纯内存，两个 `with_json` 调用即可，无需开工、无需真实网络）：

```
step1 喂入 {me,M} 后：边数=1  含{me,M}=YES
step2 喂入 {me,L} 后：边数=1  含{me,M}=NO   含{me,L}=YES
=> {me,M} 被误删（BUG 复现）
```

**根因是权威的归属错了。** 判「对端 P 的边死没死」，`base` 该是 **P**（并配套校验 P 报告的完整性）；
现在恒为「我」，于是两头都不对：

- 正常的完整区域图必然含无关边 ⇒ `star == false` ⇒ 判死**形同虚设**（绝大多数情况下根本不生效）；
- 唯一能满足 `star` 的形态反而是**不完整的叶子星型报告** ⇒ 在**最不该有权威**的时候执行删除。

因为心跳每轮都会发一条 `deep=1` 的 `layer` 刷新（`exec_dath` 末行），被误删的边下一轮就补回，
所以外在表现是**路由抖动 / 短暂不可达**而非永久断链 —— 这也是它一直没被发现的原因。

顺带：[`find.h`](find.h) 的 `graph::clear(const uuid& base, const size& pathcount)`（按跳数半径剔边）
**全仓无调用者**，是死代码；视图收缩目前实际只靠 `clear(timeout)` 的时长老化。

#### A2. `software/signal.h` 的遮蔽是「包含顺序」问题（订正 DEFERRED）

`DEFERRED.md` 曾记「`signal.h` 第 1 行的模板前置声明在 include guard **之外**，使 `process_base.cpp` 编译失败，
即当前 `libxhqm.a` 无法整库重建」。**该描述与代码不符、结论也已过时**：

- 现行 `../../software/signal.h` 的 guard 就在第 1 行（`#ifndef XHQM_SIGNAL`），前置声明在第 13 行、**在 guard 之内**；
- 实测 `cmake --build build --target xhqm` 成功，且 `touch software/system/process/process_base.cpp`
  后强制重编该 TU **也通过**，`libxhqm.a` 正常重新链接。

但底层隐患是真的，只是形态不同 —— **它是包含顺序问题**：若某 TU **先** `#include <sys/wait.h>`，
glibc 会在自己的 `extern "C"` 里拉入 `software/signal.h`，于是第 13 行的
`namespace xhqm { template <typename T> class instance; }` 落进 C linkage ⇒
`conflicting declaration of C function ...`（已用独立探针复现，报错从 `<sys/wait.h>` → `software/signal.h` → `<map>` → `functexcept.h` 一路冒出来）。

`process_base.cpp` 现在能过，**只是因为**它在 `#include <sys/wait.h>` **之前**先 include 了 `<signal.h>`：
guard 已置位，第二次进入被整个跳过（`extern "C"` 里的那次不生效）。

**所以「把第 1 行移进 guard」这个修法修不好 —— 那一行根本不存在。** 正确方向是让 `signal.h` 被
`extern "C"` 包住时也安全，最小改动是把第 13 行的前置声明挪到第 10 行 `#include_next <signal.h>` **之前**，
或改用不依赖该前置声明的 friend 方案。

### B. 代码级缺陷

#### B1. `addr_ipv4::send_tcp` 跨解锁窗口持 deque 元素的悬垂引用 + 破坏帧同步

```
addr/addr_ipv4.cpp   const text& data = c->outbox.front();   // 引用进 deque 元素
addr/addr_ipv4.cpp   const char* p = data.data() + c->sent;
addr/addr_ipv4.cpp   lk.unlock();                            // ← 窗口打开
addr/addr_ipv4.cpp   const int r = sock_send(c->sock, p, n); // ← 可能读已释放内存
addr/addr_ipv4.cpp   c->sent += static_cast<xhqm::size>(r);  // ← 字节数归因到「另一帧」
```

窗口内若并发 `send_remote` 命中**队满丢最旧**（`if (c->outbox.size() >= s_outbox)` → `c->outbox.pop_front(); c->sent = 0;`）：

- `deque::pop_front()` **使 `front()` 的引用失效**（deque 只在两端插入时保持元素引用有效，**删除端元素不保持**）⇒ `::send` 读已释放内存，**内存安全级**；
- 它同时把 `sent` 清 0，reactor 回来后把「刚写出的 `r` 字节」记到**新的队首帧**上 ⇒ 那帧开头 `r` 字节被跳过 ⇒ 对端收到坏的长度前缀 ⇒ **帧失步**。

触发前提是单连接积压 ≥ `s_outbox`(1024) 帧。注意 `DEFERRED.md` 第 3 条记录的正是他们在调这个突发场景
（200 帧紧循环曾被丢到只剩 64，才把 `s_outbox` 从 64 提到 1024），所以**该前提正是这套代码被调优的目标工况**。

**同模块的 `addr_ipv6` 把同一件事做对了**：`addr/addr_ipv6.cpp` 的 `conn_write` 在**锁内** `std::move`
取出队首元素、再写入 reactor 私有的 `pending`，全程不跨解锁窗口持引用。两相对照说明 ipv4 这里是**漏改**，
而不是有意的性能取舍（`p`/`n` 完全可以像 ipv6 那样先拷进一个局部 buffer 再解锁）。

#### B2. 远端输入加固不完整，且失败被静默吞掉

`find::exec_layer` 的注释自称「容忍缺失字段：远端事件不可信」，但它只防**缺键**（`.value(k, "")` 给默认值），
**不防错类型** —— 远端发 `{"type":"layer","uuid":123}` 就会在 `get<std::string>()` 抛 `type_error`。同类未加固点：

| 位置 | 取值 |
|---|---|
| `find.cpp` `exec_layer` | `event.value("uuid","")` / `value("from","")` |
| `post.cpp` `this_post` | `event.value("goal","")` |
| `zero.cpp` `exec_claim` | `event.value("from","")` |
| `addr.cpp` `addr_from` | `j.value("kind","")` / `value("host","")` / `value("port",0)` …（入口是 `link` 的 `send_addr`） |

（`zero::exec_proxy` 是**做对了的范本**：缺省值一律给 `nebula::json{}` 再显式 `is_string()` 判类型。）

好消息是**不会崩**：异常被 `software/system/threads/threads_task.h` 的 `catch (...)` 捕获塞进 promise。
坏消息是 `holder` 无析构等待，**异常被彻底静默丢弃** —— 事件消失、无日志、无计数，且抛出前已完成的部分状态变更会留下。

#### B3. `life_event` 在池满时静默丢事件

`software/system/threads/threads_pool.cpp` 的 `_submit_`：队列达 `m_allow_actions` 即
`_cancel_target(target_ptr)` 并返回 `false`；而 `life::life_event` **返回 void、丢弃该返回值**（`life.cpp`）。

`limt` 默认 `xhqm::npos`（两个 `zero` 便捷构造都如此）⇒ 不限、不丢。但 config 一旦写了有限 `size`
（消费方 stella 各节点已把它从 `-1` 收到 `4096`），**突发超限就静默丢事件**。
注意 `"size": -1` 经 nlohmann 转 `size_t` 得 `SIZE_MAX`，**恰等于 `xhqm::npos`**（实测），即「不限」而非「无效值」。

#### B4. 内核历史只增不减（与消费方的 OOM 同源）

`data::insert_data` **每帧**都写内核（`m_kernel.update_data(...)`），而
`network/astral/kernel.cpp` 的 `kernel::update_data` 是裸 `node.datas.push_back(rec)`，**没有任何裁剪**；
nebula 也从不调用 `kernel::remove_time`（全仓仅测试与 `evoke` 用它）。
`data::capacity` / `data::duration` 只淘汰**内存窗口** `view`，不碰内核 ——
README 说「remove 只删内存窗口、内核历史保留」是**有意**的，但代价就是**只会涨**。
消费方 stella 记录的 `star_qqbot` 26.8GB OOM 与这是同一条路径。

#### B5. `addr_b::insert_neighbor` 的锁序（潜在，非活锁）

`addr/addr_b.h` 的 `insert_neighbor` 持**容器**（`this`）的锁，再去取**元素**（`ev`）的锁
（`ev->last(...)` / `ev->fail(0)` / `ev->alive(true)` 各自持锁）；`link::exec_ping` / `link::exec_dath`
经 `run_neighbors` / `for_neighbors` 走同一「容器 → 元素」序。

当前调用图里**反向序不出现**（元素恒为 `addr_from` 造的克隆句柄，而克隆永不进 `m_addr_listen`），
所以**不构成活锁**；但这是靠约定维持的隐式纪律，建议在 `addr_b.h` 的加锁纪律注释里显式写明。

### C. 文档订正汇总

| 位置 | 旧文 | 实际 |
|---|---|---|
| 本文「已知限制」旧文的「帧长上限」条（2026-10-07 已收口到 [`DEBUG.md`](DEBUG.md) 的 B 组） | ipv6 到 64MB、收满前无硬上限 | ipv6 另有 4MB `s_read_cap` 闸；**无闸的是 ipv4** |
| `DEFERRED.md` §5.2 | `signal.h` 第 1 行前置声明在 guard 外、`libxhqm.a` 无法整库重建 | guard 在第 1 行、声明在第 13 行 guard 内；**实测可整库重建**；真实隐患是包含顺序 |
| 本文「已知限制」旧文（同上，已收口到 `DEBUG.md` 的 D 组） | `find` 每次合并/老化后重算路由 | 实际 `short_path` 每次调用现建 dijkstra 适配层、**无缓存**；语义等价、机制不同 |
| `DEFERRED.md` §3 | ipv4 突发丢帧「代码已修，回归待重跑」 | 除三个手段外，该机制还引入了 B1 的悬垂引用；回归时应一并覆盖 |

### D. 轻微 / 健壮性

- `link.cpp` `exec_bind`：忽略 `insert_neighbor` 的返回值，挂表失败仍回一条 `link`。
- `data.cpp` `remove`：局部变量注释写 `{通道 uuid, 内核 form}` 但只存 uuid；递归 `collect` 无环保护（`connect` 只挡了自环）。
- `link.h`：`std::atomic<bool> m_ping_runing;` 声明处无初值，靠构造初始化列表兜住 —— 正确但脆弱。
