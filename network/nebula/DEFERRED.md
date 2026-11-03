# DEFERRED —— 待定 / 暂不做（记录，不是计划）

> 这里只记录"讨论过但**没决定做、也没排期**"的事项，以及"我顺手改过、你可能想回退"的改动。
> 建这个文件是应要求记录，不代表任何实现承诺；要删就删。

---

## 1. link 层「广播 / 组播 / 点对点」三种语义（讨论过一次，暂不做）

**为什么会被提出**：三种传输现在的差异是用地方性补丁盖住的 ——
ipv4 真广播；ipv6 组播（每个接口一份 → 同机对端收到 N 份重复）；串口点对点（`send_broad` 退化为进程内广播，于是缆上 `bind` 求邻不上线，需要静态挂邻居句柄）。

**三种传输的客观差异**

| 传输 | 域 | 重复投递 | 回程 | 邻居来源 | `x<m` 求邻 |
|---|---|---|---|---|---|
| ipv4 UDP 广播 | L2 子网（共享端口） | 无 | 单播 TCP（payload 带 `send_addr`） | bind/link 握手 | 有意义 |
| ipv6 组播 ff02::1 | 每个接口一个域 | 有（N 接口 N 份） | 单播 TCP | bind/link | 有意义（按域） |
| serial 缆 | 无（点对点） | 无 | 同一根缆 | 首帧 `send_node` | 无意义 |
| uuid（进程内） | 进程 | 无 | 进程内 emit | 注册表 | 半有意义 |

**讨论中形成的要点（未实现）**
- 差异只在两处：**发现**（邻居怎么来）与**扇出**（`send_broad` 往哪去）；投递/心跳/路由不变。
- 可选做法：`addr_b` 加 `mode()/scope()/advertise()`（默认值与现状等价，零行为变化）；`send_broad` 按 `advertise()` 过滤；点对点模式由传输在"学到对端"时投一条与 bind 等价的事件；组播重复由链路层的 `(send_node, id)` 短时去重表消化（link 的 bind/link/ping 目前没有 id）。
- 多域前必须先修的一处口径：`has_neighbor(uuid)` 全局判重 → 同一 peer 进不了第二个域的表；`degree()` 各表求和 → 会重复计。（这属于既有实现的口径问题，与上面三种语义是否引入无关。）
- 讨论中列了 4 个待拍板点：①描述放 `addr_b` 还是 link 按 kind 判；②点对点用"合成 bind"还是新增 peer 事件；③去重表放 link 还是 life、键是否带来源；④最优路径优先级（定向长连 > 点对点 > 组播 > 广播）是否成立。

**状态**：不做。等真需要（串口/多域实际要用）再回来看。

---

## 2. 串口 `send_broad` 取舍（待你定）

现状：点对点无物理广播 → 不退化为"写缆"，只做进程内同 kind 广播；代价是缆上 `bind` 求邻不上线，串口-only 两节点要先静态挂邻居句柄。
备选：3 行改成"广播 = 写本端点自己那条缆"（点对点下广播即发给唯一对端）。
**状态**：保持现状，未动。

## 3. ipv4 突发丢帧（代码已修，回归待重跑）

单 reactor 版在 200 帧紧循环下只到 64/200（= 有界队列上限）。现行代码里 `addr_ipv4` 已有三个手段对付突发：发队列上限提到 `s_outbox = 1024`、**入队即唤醒**（一对回环 UDP 套接字，写 1 字节让 reactor 立刻从 poll 返回）、以及每个 poll 轮次末尾固定做一次 **drain-until-empty**（把每条连接的发队列抽干到 EWOULDBLOCK）。**这是修回归，不是新功能；回归 `addr_ipv4_run` 需重跑确认。**

**⚠ 复读补充（r666）**：上面这条「队满丢最旧」与 `send_tcp` 的**跨解锁窗口**组合出了新问题 —— `send_tcp` 在
`lk.unlock()` 之后仍持有指向 `outbox.front()` 元素的 `const text&`（并用它算出的 `p` 去 `::send`），
而 `send_remote` 的 `pop_front()` 会使该引用失效，同时把 `sent` 清 0 导致字节数归因到下一帧。
即：**突发工况（积压 ≥ 1024）既是丢帧场景，也是这个内存安全/帧失步缺陷的触发前提**。
详见 `README.md` 的「复核发现」B1。回归时除帧数外应一并校验**帧内容完整性**（不能只看到达条数）。

## 4. stella 迁移下一步（记录，未做）

device 第一增量已完成并跑通（`type_device` + `star_device` + `protocol.h`）。
未做：`type_astral`（设备目录软状态）、双角色 `remote_devices()/command()`（指令 + acks 等回执）、`type_miloco`、hub/consumer/media_server 三个角色节点、`type_mtx`（MediaMTX/ffmpeg 数据面）、把 stella 接进构建（`api_nebula/CMakeLists.txt` 仍指向 `../xhqm-pro`）。

---

## 5. 我在这轮里"顺手改过"的，供你判断是否回退

| 文件 | 改动 | 理由 |
|---|---|---|
| `network/nebula/addr/addr_uuid.cpp` | ①`send_frame` 去掉 `m_opened` 拦截 ②析构退注册表 | ①克隆句柄永不 opened → 邻居方向帧全丢（post 集成测试暴露）②表里留悬垂指针 |
| `network/nebula/post.{h,cpp}` | 重写；新增 `publish(data,channel)` / `subscribers(channel)` | 你要求"你来写 post"；这两个接口是扇出与诊断需要 |
| `network/nebula/find.{h,cpp}` | 三个 graph 函数实现 + `exec_relay`/`next_hop`/`routable` | 前者你指定要完成；后者是 post 变薄的前提（选路收口到 L2） |
| `network/cs.h` | `frame_stream` 帧长可配置（模板默认 + `max_size` 成员） | 你要求 |
| `network/nebula/addr/addr_serial.{h,cpp}` | 用 `s_frame_max = 1MB` 实例化 `frame_stream` | 配合上面那条，消掉 1GB resize 风险 |
| `CMakeLists.txt` / `xhqm.vcxproj` / `.filters` | 接入 `addr_uuid`/`addr_serial`/`addr_ipv6`；`.filters` 里把已不存在的 `addr_ptr` 两条换成 `addr_uuid` | 让新文件进构建；`example.h` 已按你要求移出 CMake |
| `network/nebula/tool.cpp` | 两行：`recv_event`→`life_event`、载荷键 `"event"`→`"data"` | 当时是整库唯一挡编译的两行 |
| `software/signal.h` | 未改（`signals::pool()` 未导出这条我只报了，没动） | — |

### 5.1 哥哥点名修复的四项（本轮，2026-10）

| 文件 | 改动 | 理由 |
|---|---|---|
| `network/nebula/zero.cpp` | `zero(config)` 的 `c/a` 改由 `pool_c/pool_a` 从 **入参 / config** 派生；`a` 下限夹到 `c` | ①读未构造的基类成员 `m_m/m_n` 是 UB（nlohmann 的 `value(key, 默认值)` 默认值实参必然求值，config 写了 c/a 也会读到）②去掉随机值后，`m=n=0` 的退化配置会确定的抛 `threads()` 异常，故夹下限 |
| `network/nebula/zero.{h,cpp}` | 新增 `m_logic_mutex`：`exec_logic` 不再借工具表锁；`exec_proxy` 改为**锁内取快照、锁外执行**；`argv`/`uuid`/`from` 的取法改 JSON 缺省 + 判类型 | 数据竞争（无锁读工具表）+ 自死锁面（用户 `logic()` 里 `insert_tool`）。**附带真 bug**：`item.value("argv", "")` 走的是 `get<std::string>()`，object/number/bool 载荷一律抛 `type_error` 被吞成空回执 —— 带参数的工具拿不到入参（实测复现） |
| `network/nebula/data.{h,cpp}` | 新增 `data::frame_record`，`select_history` / `select_kernal` 共用它：时间取**外层** `rec.time`、载荷取内层 `json.data`、无 `data` 的建档桩跳过 | 原实现取内层 `json.time`（恒 0），且对桩条目做 `const operator[]` 缺键读（`JSON_ASSERT`，release 下 UB） |
| `network/nebula/addr/addr_ipv4.cpp`、`addr_ipv6.cpp` | `opened()`/`closed()` 维护 `m_opened`，`addr_b::is_open()` 不再恒假（ipv6 只动这一处） | 三个传输里只有 `addr_serial` 维护了该标志 |
| `network/nebula/README.md`、`network/README.md` | 按工作副本代码重写 nebula 相关章节（addr_ptr→addr_uuid、ipv4 长连、ipv6/serial 已实现、`life_event`/`data_signal`、config 字段、13 个 TU、地址需外部注入等） | README 与代码漂移 |

行为验证：`build/nebula_b_check.cpp`（临时程序，不进源码树/构建系统）覆盖上述四项 + `is_open`，`ALL PASS`；`cmake --build build --target xhqm` 通过。

### 5.2 本轮**未**动（哥哥明确不修 / 保持现状）

- 内存风险：ipv4 接收长度上限 1GB、ipv6 64MB、收满前入向缓冲无硬上限 —— 只确认无泄漏，不加帧长闸。
- 串口：`send_broad` 不写缆（缆上求邻不成立）、reader 对 `frame_stream::recv` 失败当可重试（超长帧失步到 idle）、对端设备名拒绝拨号。
- ipv6：`make_json` 的 `host` 缺省为空（无 `local_ipv4_hint` 等价物）、`s_outbox = 64`、`conn::peer` 非原子。
- `data::remove` 只删内存窗口、内核历史保留 —— **确认是有意的**。
- 上游 r657/r658 改了 `software/signal.{h,cpp}`：`signals::pool()` 由惰性 static 改为「未安装即抛」，新增安装入口 `signals::pool_life()`。xhqm-v2 / api_nebula / xhqm-pro 三棵树里没人调用它，但 **stella 的每个 main 都在首行调 `pool_life()`**（`make/star_*/main.cpp`），即这套「显式安装」就是为 stella 加的，所以任何走异线程槽的路径都会抛 —— 不在本轮改动范围，供哥哥定。~~同一提交把 `software/signal.h` 第 1 行的模板前置声明放在了 include guard 之外，使 `software/system/process/process_base.cpp` 编译失败（`sys/wait.h` 在 `extern "C"` 里包含被同名头遮蔽的 `<signal.h>` → `template with C linkage`），即当前 `libxhqm.a` 无法整库重建的原因；一行挪进 guard 即可（已实测）。~~

**↑ 以上这段已作废（r666 复读订正）**：描述与代码不符、结论也不成立。

- 现行 `software/signal.h` 的 guard **就在第 1 行**（`#ifndef XHQM_SIGNAL`），模板前置声明在第 13 行、**在 guard 之内** —— 原文说的「第 1 行在 guard 之外」那一行不存在，照着它改会改错地方。
- 实测 `cmake --build build --target xhqm` **成功**，`touch software/system/process/process_base.cpp` 后强制重编该 TU **也通过**，`libxhqm.a` 正常重新链接 —— 「无法整库重建」不成立。
- **但底层隐患是真的，形态是「包含顺序」而非「guard 位置」**：若某 TU **先** `#include <sys/wait.h>`，glibc 会在自己的 `extern "C"` 里拉入 `software/signal.h`，于是第 13 行的前置声明落进 C linkage → `conflicting declaration of C function`（独立探针已复现）。`process_base.cpp` 现在能过，**只是因为**它在 `<sys/wait.h>` 之前先 include 了 `<signal.h>`，guard 已置位、第二次进入被整个跳过。
- 正确方向：让 `signal.h` 被 `extern "C"` 包住时也安全 —— 最小改动是把第 13 行的前置声明挪到第 10 行 `#include_next <signal.h>` **之前**，或改用不依赖该前置声明的 friend 方案。

**教训（值得留给后来的自己）**：这条记录当初把「某次编译失败的现场」当成了「稳定的代码事实」写下来，既没记下触发它的 TU，也没在修复后回填 —— 于是文档比代码晚了整整一个版本。凡写「某某导致编译失败」，必须同时写清**复现命令**与**验证时的版本**。
- 同类隐患未改（不在点名范围）：`zero::exec_claim` 的 `event.value("from", "")` 对非字符串 `from` 会抛；`addr_uuid` 的进程内直投依赖注册表语义。

---

## 6. r666 逐文件复读的发现（待定：修 / 不修）

> 完整描述、**复现步骤与实测输出**在 `README.md` 的「复核发现（r666 逐文件复读）」一节，这里只列索引与需要你拍板的点。
> 本文件是「记录暂不做」，不是待办清单；下面这些我**只报告、一处未改**。

| # | 位置 | 一句话 | 级别 | 复现 |
|---|---|---|---|---|
| B1 | `addr/addr_ipv4.cpp` `send_tcp` | 跨 `lk.unlock()` 仍持 `outbox.front()` 引用，`pop_front()` 使其失效（UAF）；同时 `sent` 被清 0 导致帧失步 | **内存安全** | 代码级 |
| B2 | `find.cpp` / `post.cpp` / `zero.cpp` / `addr.cpp` | 远端输入只防缺键、不防错类型 → `type_error`；被线程池 catch 后**静默丢弃**（不崩、无日志） | 健壮性 | 代码级 |
| B3 | `life.cpp` + `software/system/threads/threads_pool.cpp` | 池满即 `_cancel_target`，而 `life_event` 返回 void ⇒ **静默丢事件**（`limt=npos` 时不丢） | 语义 | 代码级 |
| B4 | `data.cpp` → `astral/kernel.cpp` | 每帧写内核、内核 `datas.push_back` **无裁剪**，`capacity/duration` 只裁内存窗口 ⇒ 只增不减 | **资源** | 代码级 |
| B5 | `addr/addr_b.h` `insert_neighbor` | 持容器锁再取元素锁；当前调用图无反向序 ⇒ 潜在而非活锁 | 隐患 | — |
| A1 | `find.cpp` `with_json` / `exec_graph` | 「判死」归属错（`base` 恒为我）⇒ 正常情况形同虚设，唯一生效时**误删活边** | **正确性** | **已复现** |
| A2 | `software/signal.h` | 遮蔽隐患 = 包含顺序问题，与 guard 位置无关 | 隐患 | **已复现** |
| D | `link.cpp` / `data.cpp` / `link.h` | 忽略 `insert_neighbor` 返回值；`remove` 注释与实现不符 + 递归无环保护；`m_ping_runing` 无声明初值 | 轻微 | — |

**建议的优先级**（供你拍板，我不擅自动手）：

1. **B1** —— 唯一的内存安全级问题，且触发工况正是这套代码被调优的突发场景；修法已经写在同模块 `addr_ipv6.cpp` 里（锁内 move 出来、写 reactor 私有的 `pending`），照抄即可，风险低。
2. **A1** —— 唯一**已复现**的正确性缺陷，会造成路由抖动；修法涉及语义（`base` 该记谁 + 权威要不要校验「报全」），需要你先定口径。
3. **B4** —— 消费方已经在为它 OOM（stella 的 `star_qqbot` 26.8GB）；但「历史保留」是有意设计，属于**产品口径**决定，不是纯技术修复。
4. **B3** —— 影响面取决于 config 里的 `size`；若确定所有消费者都用有限值，应给 `life_event` 加返回值或计数，别让它继续静默。
5. B2 / B5 / D —— 加固与注释级，可随时做。
