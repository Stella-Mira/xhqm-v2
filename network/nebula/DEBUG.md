# xhqm/nebula 当前问题清单

`network/nebula`（B 世代分布式网络层：addr / data / life / link / find / post / zero / star）**当前仍存在**的缺陷、隐患与取舍台账。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**（本仓历史台账不搬家、不改名，只在此指向）：
> - [`DEFERRED.md`](DEFERRED.md) —— 本模块「讨论过但**没决定做/没排期**」的事项与「顺手改过、可供回退」的记录（§1 三种传输语义、§2 串口 `send_broad`、§3 ipv4 突发丢帧、§4 stella 迁移下一步、§5 顺手改动、§6 r666 复读索引）。本文件**不复制**其内容，只在对应条目里指过去。
> - 本模块 `README.md` 末尾的「复核发现（r666 逐文件复读）」一节是 **r666 的复读记录**（A1/A2/B1–B5 的完整论证、判据、已复现的复现步骤与 C 组订正表），按「历史不删」保留为主表；本文件是它的**「当前仍存在」收敛版**：只写现状、位置、影响与验证方式，不重抄论证。
> - 仓库根 `通读报告-2026-10-07.md` §4 是本次细读的报告（含建议与优先级）；其它模块各自维护自己的台账。
>
> 核对环境：g++ 11.4 / `-std=c++17` / 工作副本 r677；**所有行号均指 r677 的当前内容**。凡写「编不过」的条目一律附命令与输出。本轮**不落任何临时程序**（仓库纪律），因此标注「已复现」的条目指上一轮（r666/r677）跑过的复现，标注「代码级」指逐行读出的确定性缺陷。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `find::graph` 的判死权威归属错（`base` 恒为本节点）→ 正常图判死形同虚设，唯一生效时反而误删活边
- 位置：`find.cpp:374`（`exec_graph` 里 `with_json` 的 `base` 恒为 `this->uuid()`）、`find.cpp:72-89`（`star` 判据）、`find.cpp:95-103`（删边）
- 现状：判「对端 P 的边死没死」时，`base` 该是 **P** 并配套校验 P 报告的完整性；现在恒为「我」。于是两端都不对：完整区域图必然含无关边 ⇒ `star == false` ⇒ 判死根本不生效；唯一满足 `star` 的形态是**不完整的叶子星型报告** ⇒ 在最不该有权威时删掉我那些没出现在报告里的活边。
- 证据：`find.cpp:374` `m_region_graph.with_json(json{ {"base",this->uuid().to_text()}, {"graph",event.value("graph", json())} })`；`find.cpp:88-89` `if (base && (n == base || l == base)) reported.insert(e); else star = false;`；`find.cpp:95` `if (!base || links == 0 || !star || reported.empty()) return;` → `find.cpp:96-103` 擦除碰 `base` 且不在 `reported` 里的边。
- 影响：被误删的边下一轮心跳（`exec_dath` 末行每轮发一条 `deep=1` 的 `layer`）就补回 ⇒ 外在表现是**路由抖动 / 短暂不可达**而非永久断链，所以长期没被发现。**绝大多数情况下判死根本不生效**（凡报告里含一条不含我的边，`star` 就是假），于是死邻居只能等 `m_region_aging`（默认 60s，`find.h:76`）的时长老化；唯一生效的形态恰好是「叶子邻居的不完整星型报告」——除了它，还有 `exec_layer` 在 `size==0` 时注入的自身图（那份报告全碰我，但报告内容就是我自己的快照，删了也是空操作）。
- 复现/验证：上一轮（r666）已用纯内存两步 `with_json` 复现（步骤与输出见 `README.md` 的「复核发现」A1）；本轮只复核代码路径未变（`find.cpp:374` 仍是本节点 uuid），**未重跑**。核对命令：`sed -n '366,386p' find.cpp`。
- 建议：先定口径 —— 以「回图方」为 `base`（并校验它报全了自己的邻居），或维持现状但明确「判死只在 base 自己的星型报告上生效」；未定口径前不要动。

### A2. 远端 `layer` 事件缺 `deep`/`wait` 时 `make_layer` 用 const `operator[]` 读缺键（debug 断言 / release UB）
- 位置：`find.cpp:256-258`（`xhqm::text uuid = layer["uuid"]; xhqm::size deep = layer["deep"]; xhqm::size wait = layer["wait"];`），入口在 `find.cpp:344`（`make_layer(event)`），远端事件来自 `find.cpp:302-306`
- 现状：`exec_layer` 自称「容忍缺失字段：远端事件不可信」，但只对 `uuid`/`from`/`wait` 用了带缺省值的 `value()`；紧接着的 `make_layer(event)` 对**同一个远端事件**做 const `operator[]`（无缺省值）。缺键读在 nlohmann 3.11.2 里是 `JSON_ASSERT(it != m_value.object->end())`，而 `JSON_ASSERT` 默认就是 `assert`：debug 构建断言失败，Release/NDEBUG 下断言消失 → 对 `end()` 解引用（UB）。
- 证据：`network/json.hpp:21351-21358`（const `operator[]`：`JSON_ASSERT(it != m_value.object->end()); return it->second;`）、`network/json.hpp:2544`（`#define JSON_ASSERT(x) assert(x)`）；调用链 `find.cpp:302-307` → `find.cpp:342-344` → `find.cpp:254-258`。
- 影响：任何能给本节点发帧的对端（不需要身份）发一条 `{"type":"layer","uuid":"<可解析的 uuid 文本>"}`（不带 `deep`）即可触发：本节点 `degree() >= 1` 且 `from` 写成本节点 uuid（或 `degree() >= 2` 时任意 `from`）就会走到 `make_layer` 的 `else` 分支。这比 B2 的 `type_error` 更重——那条至少只是抛异常被吞。
- 复现/验证：未复现（代码级；本轮不落临时程序）。核对命令：`sed -n '250,262p;300,346p' find.cpp`。
- 建议：`make_layer` 改成 `layer.value("uuid", nebula::text{})` / `value("deep", xhqm::size(0))` / `value("wait", xhqm::size(0))` 再判类型，与 `exec_layer` 同一口径。

### A3. `data::remove` 的级联收集没有环保护 → 反向 `connect` 出来的环会让它无限递归
- 位置：`data.cpp:53-81`（`collect` 定义 `data.cpp:56-67`，递归调用 `data.cpp:64`、`data.cpp:74`），环的入口是 `data.cpp:83-99`（`connect` 只挡了 `child != parent`，即只挡自环）
- 现状：`collect(p)` 遍历所有 `parent == p` 的窗口并递归下去；只要存在 `A.parent = B` 且 `B.parent = A` 这样的环，`collect` 就会 A→B→A 无限递归，`kill` 向量同时无界增长。
- 证据：`data.cpp:85` `if (child && parent && child != parent)`（只挡自环，不挡成环）；`data.cpp:59-66` 递归无 visited 集合；对照 `data.h:60-61` 的注释「级联移除其全部子通道」——语义上确实是棵树，但 API 没有维持「树」这个不变量。
- 影响：任何把两个通道互相 `connect` 的调用方（写错了、或对同一对通道先后调 `connect(a,b)` 与 `connect(b,a)`）在 `remove()` 时栈溢出崩溃；`channels()`/`select_*` 不受影响，所以坏结构会一直藏着。
- 复现/验证：未复现（代码级）。核对命令：`sed -n '53,99p' data.cpp`。
- 建议：`connect` 里加「祖先链自检」（向上走 `parent` 直到根，遇到 `child` 就拒绝），或给 `collect` 加 visited 集合。

---

## B. 资源与并发

### B1. `addr_ipv4::send_tcp` 跨 `lk.unlock()` 持 `outbox.front()` 引用 + 队满 `pop_front()` 清 `sent` → UAF 与帧失步
- 位置：`addr/addr_ipv4.cpp:728-771`（引用在 `:736`、`p` 在 `:737`、解锁在 `:745`、`::send` 在 `:746`、`sent +=` 在 `:751`、与队首比长度在 `:752`）；触发在 `addr/addr_ipv4.cpp:863-869`（队满丢最旧 `:867` + `sent = 0` `:868`）
- 现状：`const text& data = c->outbox.front();` 取的是 deque 元素的引用，`p`/`n` 由它算出；`lk.unlock()` 之后才 `sock_send(c->sock, p, n)`。窗口内若并发 `send_remote` 命中「队满丢最旧」，`pop_front()` 使该元素失效（deque 删除端不保持元素引用/缓冲区）⇒ `::send` 读已释放内存（**内存安全级**）；它同时把 `sent` 清 0，reactor 回来把刚写出的 `r` 字节记到**新的队首帧**上 ⇒ 那帧开头 `r` 字节被跳过 ⇒ 对端长度前缀错位 ⇒ **帧失步**。
- 证据：`addr_ipv4.cpp:736-737`、`:745-746`、`:751-752`；`addr_ipv4.cpp:865-868`（`if (c->outbox.size() >= s_outbox) { c->outbox.pop_front(); c->sent = 0; }`）；队列上限 `addr_ipv4.h:85`（`s_outbox = 1024`）。
- 触发前提与影响：单连接积压 ≥ 1024 帧。这正是该模块被调优的突发工况 —— `DEFERRED.md` §3 记录的回归（200 帧紧循环曾只剩 64）就是为了它把 `s_outbox` 从 64 提到 1024。触发后既有内存安全问题，也有对端解析错位；只有突发 + 队满时才中，平时不可见。
- 复现/验证：未复现（代码级，需并发压满 1024 帧的时序）。同模块 `addr/addr_ipv6.cpp:604-628` 把同一件事做对了：`conn_write` 在**锁内** `std::move` 取出队首（`:615-616`）、写入 reactor 私有的 `pending`，再在锁外 `::send(c->pending.data(), ...)`（`:628`）—— 说明 ipv4 这里是漏改。核对命令：`sed -n '726,772p;860,890p' addr/addr_ipv4.cpp`。
- 建议：照 `addr_ipv6.cpp` 的写法改（锁内 move 到局部/私有缓冲，锁外发送），顺带把 `sent` 的语义收敛到该缓冲上。

### B2. 远端输入只防缺键不防错类型 → `type_error` 被线程池静默吞掉（不崩、无日志、无计数）
- 位置：`find.cpp:305-306`（`event.value("uuid","")` / `value("from","")`）、`find.cpp:316`（`value("wait", ...)`）、`post.cpp:36`（`value("goal","")`）、`zero.cpp:120`（`value("from","")`）、`addr.cpp:12-19`（`text_kind(j.value("kind",""))` 与 `host/port/broad`，入口是 `link` 的 `send_addr`）
- 现状：这些点全部用带缺省值的 `value<T>()`，键存在但类型不符时走 `get<T>()` → 抛 `type_error`；`life_event` 把事件扔进 `m_life_thread.submit<void>(...)`，工作函数里没有 catch，异常在主库线程任务里被 `catch (...)` 捕获后塞进 promise，而 `holder` 无人观察/无析构等待 ⇒ **异常被彻底丢弃**，事件消失且不留痕迹。
- 证据：`life.cpp:18-21`（提交 lambda 里只有 `this->exec_event(event)`）；`software/system/threads/threads_task.h:236-248`（`catch (...)` → `status = failures` → `promise->set_exception`）；`threads_task.h:130-138`、`:252-266`（`holder` 无析构、只有 `future_ptr()`，`life.cpp` 连返回值都没接）。反面范本：`zero.cpp:88-108`（`exec_proxy` 缺省值一律给 `json{}`，再显式 `is_string()` 判类型）。
- 影响：同网任意进程发一条 `{"type":"layer","uuid":123}`（或 `{"type":"claim","from":123}`）即可让本节点丢掉该事件并继续跑 —— 不崩、不报错、无计数；且抛出前已完成的状态变更会留下（半更新）。
- 复现/验证：未复现（代码级）。核对命令：`grep -n 'value("uuid"\|value("from"\|value("goal"\|value("kind"' find.cpp post.cpp zero.cpp addr.cpp`。
- 建议：统一走「`json{}` 缺省 + 显式 `is_string()/is_number()` 判定」，或给 `life_event` 加一层「异常→日志/计数」的兜底。

### B3. `life_event` 返回 void：池满即静默丢事件（`"size": -1` 恰好等于 `npos` = 不限）
- 位置：`life.cpp:14-23`（`m_life_thread.submit<void>(0, ...)` 的返回值被丢弃）、`life.h:23-29`（`limt` 来自构造参数）、`software/system/threads/threads_pool.cpp:403-436`
- 现状：`_submit_` 在「队列已达 `m_allow_actions`」时不入队，直接 `_cancel_target(target_ptr)` 并返回 `false`；`life_event` 不看返回值、也无法把丢事件告诉调用方 ⇒ 池满时事件凭空消失。
- 证据：`threads_pool.cpp:415-417`（`has_capacity = m_allow_actions == xhqm::npos || m_queued_tasks < m_allow_actions`）、`:431-435`（`if (!accepted) { _cancel_target(target_ptr); } return accepted;`）；`life.cpp:18` 丢弃返回值。默认值：`zero.cpp:26`、`zero.cpp:36` 都是 `config.value("size", xhqm::npos)` ⇒ 默认不限、不丢。`"size": -1` 之所以也等于「不限」：`xhqm::count = std::size_t`（`head.h:218`）、`xhqm::npos = static_cast<xhqm::count>(-1)`（`algebra/value.h:46`），而 nlohmann 的整数转换是裸 `static_cast`、**无范围检查**（`network/json.hpp:4620-4622`）。
- 影响：config 里一旦写有限的 `size`，突发超限就静默丢事件（消费方 stella 的若干 `stella.template.json` 已把 `size` 写成 `4096`：`stella/make/star_sense/stella.template.json:6`、`star_sprite:7`、`muse_vtuber:7` 等）；表现为「事件偶尔没反应」，没有任何线索指向丢弃点。
- 复现/验证：未复现（代码级）。核对命令：`sed -n '403,436p' software/system/threads/threads_pool.cpp; sed -n '14,23p' life.cpp`。
- 建议：`life_event` 改成返回 bool（或维护一个丢事件计数/日志），让调用方至少能看见。

### B4. 内核历史只增不减（每帧落库 + `kernel::update_data` 无裁剪 + nebula 从不调 `remove_time`）
- 位置：`data.cpp:117-138`（`m_kernel.update_data(...)` 在 `:126`，窗口淘汰在 `:128-133`）、`network/astral/kernel.cpp:263-274`（`node.datas.push_back(rec)` 在 `:271`）
- 现状：`data::insert_data` **每帧**都写内核；`kernel::update_data` 就是裸 `push_back`，没有任何条数/时长裁剪；`data::capacity`/`data::duration` 只淘汰内存窗口 `view`。nebula 全模块从不调用 `kernel::remove_time`（`kernel.cpp:275-287` 有实现）。
- 证据：`data.cpp:126` 与 `:133`（`while (dur_time > 0 && view.size() && now - view.front().first > dur_time) view.pop_front();` 只对 `view`）；`kernel.cpp:265-273`；全仓 `grep -rn "remove_time"` 只命中 `test/astral_*_test.cpp` 与 `network/astral/evoke.cpp:168`，**nebula 无一处**。
- 影响：任何长期运行的节点，其内核条目数随写入帧数线性增长（并随 `path` 非空落盘到文件）。消费方 stella 记录的 `star_qqbot` 26.8GB OOM 与这是同一条路径（本轮未在本机复现该 OOM）。
- 复现/验证：未复现（代码级）。核对命令：`grep -rn "update_data\|remove_time" network/nebula network/astral | head`；`sed -n '263,287p' network/astral/kernel.cpp`。
- 建议：属产品口径（历史保留是有意设计，见 F1）——要么给内核加按条数/时长的裁剪或快照合并，要么给 `data` 加一个显式的「历史上限」开关，别让默认行为是无限。

### B5. `addr_b::insert_neighbor` 持容器锁再取元素锁（潜在，非活锁）
- 位置：`addr/addr_b.h:107-113`（容器锁 `:109`，元素锁在 `:111-112` 的 `ev->last(...)` / `ev->fail(0)` / `ev->alive(true)` 里）
- 现状：加锁顺序是「容器（`this`）→ 元素（`ev`）」；`link::exec_ping`（`link.cpp:329-335`）与 `link::exec_dath`（`link.cpp:304-313`）经 `run_neighbors` / `for_neighbors`（同样持容器锁后再回调元素访问器）走的是同一顺序。
- 证据：`addr_b.h:107-113`；`addr_b.h:68-91`（元素访问器各自持 `m_mutex`）；`link.cpp:307-313`、`:331-334`。
- 影响：当前调用图里**反向序不出现**（元素恒为 `addr_from` 造的克隆句柄，而克隆永不进 `m_addr_listen`），所以**不构成活锁**；但这条纪律只是口头约定，将来任何「持元素锁去动容器」的写法都会立刻变成死锁。
- 复现/验证：无需复现（锁序分析）。核对命令：`sed -n '107,113p' addr/addr_b.h`。
- 建议：在 `addr_b.h` 的加锁纪律注释里显式写明「容器锁 → 元素锁，禁止反向」，或 `insert_neighbor` 改成「先挂表，再在锁外摸元素」。

### B6. 真正无闸的是 ipv4：单帧放行 1GB，且入向缓冲在收满整帧前没有硬上限
- 位置：`addr/addr_ipv4.cpp:701`（`if (len > (1u << 30)) { conn_drop(c); return; }`）、`addr/addr_ipv4.cpp:674-723`（`c->inbox.append(...)` 在 `:682`，`if (c->inbox.size() < c->need) break;` 在 `:706`）
- 现状：ipv4 只拒绝「声明长度 > 1GiB」，之后必须等 `inbox` 收满 `need` 才解析；对端声明 1GiB 再慢慢灌字节，`inbox` 就会一路涨到 1GiB（本轮未加任何其它上限）。对照：ipv6 单帧上限 64MiB（`addr_ipv6.cpp:574`、`:622`，`1u << 26`）且整条入向缓冲另有 4MiB 闸（`addr/addr_ipv6.h:63` `s_read_cap = 4 << 20`；`addr_ipv6.cpp:568` `if (c->inbuf.size() > s_read_cap) return false;` 不解析、直接收连接）；串口单帧 1MiB（`addr/addr_serial.h:164` `s_frame_max = 1u << 20`）。发队列上限：ipv4 1024（`addr_ipv4.h:85`）、ipv6 64（`addr_ipv6.h:60`）、串口 64（`addr_serial.h:163`）。
- 证据：`sed -n '674,723p' addr/addr_ipv4.cpp`；数字见上面的常量定义行。
- 影响：同一网段里任意进程（不需要身份）都能把本节点单连接内存顶到 ~1GiB —— **无闸的是 ipv4**，不是 ipv6（旧文「ipv6 到 64MB、收满前无硬上限」已作废，见 `README.md` 的 C 组订正表）。
- 复现/验证：未复现（代码级；需要构造慢速灌包的 TCP 对端）。核对命令：`sed -n '695,710p' addr/addr_ipv4.cpp`。
- 建议：给 ipv4 的 `inbox` 加与 ipv6 同形的 `s_read_cap`，并把单帧上限收到与 ipv6/串口同一量级。

### B7. `addr_uuid` 的克隆转发槽与进程级 `uuid_broad` 用默认 `signal_auto` → 未装 `pool_life()` 即抛 `xhqm::error`
- 位置：`addr/addr_uuid.cpp:32`（`frame_signal.connect(it->second, &addr_uuid::_frame_signal_);`）、`addr/addr_uuid.cpp:34`（`uuid_broad.connect(this, &addr_uuid::_broad_signal_);`）
- 现状：这两处都没有给 `signal_type` 实参 ⇒ 取默认 `signal_auto`（`software/signal.h:90/97/104/113`）。`emit` 时 `signal_auto` 会按「注册线程 == 当前线程」塌缩成 `direct`，否则变 `queued` 并走 `pool().submit(...)`（`signal.h:148-163`）；而 `signals::pool()` 自 r658 起在未安装时**直接抛** `xhqm::error`（`software/signal.cpp:17-18`），安装入口只有 `signals::pool_life()`（`signal.cpp:35-38`，宿主 `main` 首行调）。对照：`link::life()` 连的收帧槽是**显式** `signal_direct`（`link.cpp:35-36`），不受槽池影响。
- 证据：`addr_uuid.cpp:32/34`（默认实参）；`software/signal.h:90`、`:148-163`；`software/signal.cpp:14-20`、`:35-38`；`link.cpp:35-36`。消费方不对称点（stella 树，`/home/xhqm/DeepSeek/stella`）：`grep -rn pool_life make` 命中 16 处，分布在 `make/mira_agent/main.cpp:34`、`web_main.cpp:83`、`star_sense/main.cpp:60`、`star_wxbot/main.cpp:184`、`star_sprite/main.cpp:92`、`star_echo/main.cpp:8`、`star_kitset/main.cpp:40`、`star_vtuber/main.cpp:184`、`star_astral/server_main.cpp:51`、`muse_vtuber/main.cpp:47`、`star_qqbot/main.cpp:184`、`star_center/main.cpp:37` + `ctl_main.cpp:68`、`star_spdlog/main.cpp:50`、`star_miloco/main.cpp:76`、`star_bilibili/main.cpp:108` —— **唯独 `make/star_device/main.cpp` 全链路没有**。
- 影响：触发条件是「用 `addr_uuid`（kind=`uuid`）＋ 异线程发射」——`uuid_broad` 的发射点之一就是心跳线程（`link.cpp:175`/`:179` → `link::send_broad` `:214-224` → `addr->send_broad`），克隆转发槽的发射点是事件池 worker（`link::exec_send` → `neighbor->send_frame`）。抛出的异常在事件池里被 `catch (...)` 吞掉（同 B2），从心跳线程冒出的异常没有捕获点（`link::ping_runing` 无 try）⇒ 按 C++ 语义会 `std::terminate`。**后果未在本机复现（未确认）**：本机没跑「stella star_device + addr_uuid + 异线程发射」这条组合。
- 复现/验证：未复现（未确认项）。可核对的部分已实测：`signal.cpp:17-18` 的抛错文案建议的 `xhqm::getInstance<signals::pool_life>()` **本身编不过**（`printf '#include "software/signal.h"\n#include "software/pattern/instance.h"\nint main(){ xhqm::getInstance<xhqm::signals::pool_life>(); }\n' | g++ -std=c++17 -fsyntax-only -I. -I../depend -x c++ -` → `error: type/value mismatch ... expected a type, got 'xhqm::signals::pool_life'`），正确写法只有 `xhqm::signals::pool_life();`。
- 建议：消费方入口统一补 `pool_life()`（stella 侧 `star_device` 是唯一漏的）；模块侧若要更稳，可把这两处显式写成 `signal_direct` 或给 `addr_uuid` 的 `frame_signal` 传私有池。

### B8. `conn::peer` / `conn::node` 是非原子 `uuid`，由 reactor 写、发送侧读（ipv4 与 ipv6 同形）
- 位置：`addr/addr_ipv6.h:47`（`nebula::uuid peer{};`）、`addr/addr_ipv6.cpp:421-427`（`learn_conn` 写 `c->peer = peer`）、`addr/addr_ipv6.cpp:126-131`（`ipv6_conn_find` 读 `c->peer`）、`addr/addr_ipv4.h:65-66`（`peer`/`node`）、`addr/addr_ipv4.cpp:532-535`（`learn_conn` 写）、`addr/addr_ipv4.cpp:64-66`/`:74`、`:838`（读）
- 现状：这两个字段都声明为非原子 `uuid`；写入在 reactor 线程（`conn_read` → `learn_conn`），读取在任一线程（发送侧 `send_remote`/`ipv4_conn_put`/`ipv6_conn_put`）。`send_remote` 读 `one->peer` 时只持 `m_live_mutex`，而写侧不持该锁 ⇒ 数据竞争（`uuid` 是 16 字节对象，撕裂读会让路由键指向错误的对端）。
- 证据：`addr_ipv6.h:47`、`addr_ipv6.cpp:421-427`、`:126-131`；`addr_ipv4.h:65-66`、`addr_ipv4.cpp:532-535`、`:838`。README 旧文只在 ipv6 一节记了这条，**ipv4 同形**（本轮补充核对）。
- 影响：并发拨号/接入瞬间有极小窗口读到半更新的 `peer`；后果是选错/选不到连接，表现为偶发丢帧而非崩溃，极难定位。
- 复现/验证：未复现（代码级数据竞争）。核对命令：`grep -n "c->peer\|one->peer\|c->node" addr/addr_ipv4.cpp addr/addr_ipv6.cpp`。
- 建议：两个字段改 `std::atomic<nebula::uuid>`（或用连接锁统一保护读写）。

---

## C. 跨平台

### C1. POSIX 下未知波特率被静默替换为 115200（Windows 会打开失败）→ 「开着的死缆」
- 位置：`addr/addr_serial.cpp:31`（`static speed_t serial_speed(const sint& baud)`，其 `default: return B115200;`）、`addr/addr_serial.cpp:184-185`（`::cfsetispeed/::cfsetospeed(&tty, serial_speed(m_port))`）、`addr/addr_serial.cpp:134`（Windows 侧 `dcb.BaudRate = static_cast<DWORD>(m_port);`）
- 现状：POSIX 只把 `B110 … B3000000` 里**该平台定义了宏的那些**速率映射过去，其余一律回落 B115200；Windows 侧把 `m_port` 原样交给 `SetCommState`（是否被接受取决于串口驱动——按 Win32 语义，驱动不支持的速率会让 `SetCommState` 失败 ⇒ `open()` 返回 false，**本机未实测**）。于是同为 `addr_serial(host, 250000)`（250000 不在映射表里）：Windows 侧预期明确报错，POSIX 侧**静默按 115200 打开**（`opened()` 返回真、`make_json` 还照播 `port/baud = 250000`），两侧 4 字节长度前缀随即使帧边界错乱。
- 证据：`sed -n '/static speed_t serial_speed/,/^		}$/p' addr/addr_serial.cpp`（末行 `default: return B115200;`）；`grep -n "cfsetispeed\|BaudRate\|tcsetattr" addr/addr_serial.cpp`；`cfsetispeed/cfsetospeed` 的返回值也被忽略（只看随后的 `tcsetattr`，`:188`）。
- 影响：只有用「未映射速率」在 POSIX 开缆的使用者会中；表现为「缆开了、连接看着正常、帧一个都不出来」，日志里全是成功。
- 复现/验证：未复现（需真实串口或虚拟串口对 + 未映射速率；`serial_mem_stream` 假流绕过波特率，测不出）。核对命令：`sed -n '31,85p' addr/addr_serial.cpp`（`serial_speed` 全文，末行 `default: return B115200;`）。
- 建议：`serial_speed` 改成「未知速率返回 0/抛出」，让 `open()` 如实失败；或至少把实际生效速率写进 `make_json`/`info`。

---

## D. 性能

### D1. `short_path` / `next_hop` 每次调用都现建整图适配层（无缓存）
- 位置：`find.cpp:160-213`（`short_path`：每调用 `make_unique` 一份全图节点 + 双向邻接）、`find.cpp:387-398`（`next_hop`）、`find.cpp:446-450`（对外 `short_path` 取 `m_find_mutex` 后转调）
- 现状：区域图每次查询都重建 `std::map<uuid, unique_ptr<path_node>>` + 邻接表，跑完 `dijkstra` 就丢；`routable`（`find.cpp:399-402`）也是如此，而 `post::send_goal`（`post.cpp:27`）在**每次**投递前都调 `routable`。
- 证据：`find.cpp:170-196`（适配层构建）、`:196`（`xhqm::dijkstra`）、`:399-402`、`post.cpp:22-32`。区域图规模是 LAN 级，单次 O(V+E) 可忽略；但它在「每帧 publish → 每个订阅者一次可达性检查 → 再逐跳 `next_hop`」的路径上被反复执行。
- 影响：节点多/订阅者多时是纯浪费的 CPU（不是正确性问题）；当前实测量级未采集（未做基准）。
- 复现/验证：未复现（未做基准测试）。核对命令：`sed -n '160,213p;387,402p' find.cpp`。
- 建议：给 `m_region_graph` 加「版本号 + 上次 dijkstra 结果」缓存，或退一步缓存 `next_hop` 结果。

### D2. `select_history` / `select_kernal` 全量物化：内核数组按值整份拷贝
- 位置：`data.cpp:187-207`（`select_history`）、`data.h:79-100`（`select_kernal`）、`network/astral/astral.h:53-56`（`datas_json()`）
- 现状：`datas_json()` **按值返回整份 `datas` 数组**（每调用一次拷贝一次），随后循环再往新数组里 push `{time, data}`；一次查询的内存峰值 ≈ 该通道全部历史的两倍。与 B4 组合后这是最容易先炸的地方。
- 证据：`astral.h:53-56`（`inline json datas_json() const { return datas.is_array() ? datas : astral::json::array(); }` —— 返回类型是 `json` 不是引用）；`data.cpp:191-205`；`data.h:84-97`。
- 影响：历史长的通道一旦被查询（`nebula_ctl`/诊断/前端拉历史），进程内存瞬间翻倍；`select_kernal` 也一样（多一个谓词过滤）。
- 复现/验证：未复现（代码级；可用长历史通道 + 一次 `select_history` 观察 RSS，但不落临时程序）。核对命令：`sed -n '187,207p' data.cpp; sed -n '46,60p' network/astral/astral.h`。
- 建议：给内核加「按时间窗口/条数分页」的查询接口，别让查询天然是全量的。

### D3. 心跳轮次里的全量图操作（每秒一次全表老化 + 一轮 `layer` + 整图合并）
- 位置：`find.cpp:215-239`（`exec_dath`：`:229` `m_region_graph.clear(m_region_aging)`、`:237-238` 每轮 `make_graph`/`make_layer`）、`find.cpp:44-104`（`with_json` 每次整份合并）
- 现状：心跳线程每 `m_t`（默认 1000ms）注入一条 `dath`；`find::exec_dath` 每次都遍历全部边做时长老化、遍历 `m_back_map`/`m_relay_map`、再注入一条 `deep=1` 的 `layer`。每个收到的 `graph` 事件都触发一次全量节点/边合并（`with_json`）。
- 证据：`link.cpp:169-185`（心跳循环）、`find.cpp:229`、`:237-238`、`:44-104`；`m_region_aging` 默认 60s（`find.h:76`）。
- 影响：LAN 规模（几十节点/几百边）下开销可忽略；但这是「无周期线程、心跳顺带完成」这条设计原则的直接代价 —— 图越大，每秒的固定开销越大，且它跑在心跳线程上（会推迟 `ping` 的节拍）。
- 复现/验证：未复现（未做基准）。核对命令：`sed -n '215,239p' find.cpp`。
- 建议：老化改成「按到期时间的小根堆/分片扫描」，`with_json` 合并加「报告与上次相同则跳过」的短路。

---

## E. 未实现 / 占位桩

### E1. 「广播 / 组播 / 点对点」三语义统一（`mode()/scope()/advertise()`）没有落点
- 位置：`addr/addr_b.h:36-162`（接口里没有 `mode/scope/advertise` 之类）；差异点实际分布在 `addr/addr_ipv4.cpp:918-936`、`addr/addr_ipv6.cpp:736-784`（`send_broad`）、`addr/addr_serial.cpp:880-894`
- 现状：三种传输的发现/扇出差异仍由各实现的地方性补丁承载（ipv4 真广播 / ipv6 组播 / 串口进程内广播），`addr_b` 接口层没有描述「我在哪个域、往哪扇出」的抽象。
- 证据：`grep -n "mode()\|scope()\|advertise" addr/addr_b.h` → 0 命中；`addr_serial.cpp:882-886` 的注释自陈这是取舍。`DEFERRED.md` §1 记录了讨论结论与 4 个待拍板点，状态「**不做**，等真需要再回来看」。
- 影响：新增传输（或要多域）时必须再写一遍地方性补丁；不会算错，但每加一种传输就多一处特例。
- 复现/验证：无需复现（接口缺失）。核对命令：`grep -rn "advertise\|scope()" network/nebula | head`。
- 建议：等串口/多域真的要用时再按 `DEFERRED.md` §1 的方案落地；现在保持现状是对的。

### E2. 多地址 / 多域下的邻居口径没适配：`has_neighbor` 全局判重 → 同一 peer 进不了第二个域
- 位置：`link.cpp:157-167`（`has_neighbor` 任一地址命中即真）、`link.cpp:116-129`（`insert_neighbor` 用它做判重）、`link.cpp:15-23`（`degree()` 各地址邻居表求和）
- 现状：同一 uuid 的对端若同时出现在两条地址（例如 ipv4 与串口），只有第一个域的邻居表能挂上它；`degree()` 又是各表求和 —— 一旦绕过判重就会重复计。
- 证据：`link.cpp:119`（`if (this->has_neighbor(id->uuid())) return false;`）、`link.cpp:15-23`；`DEFERRED.md` §1 已把这两点记为「既有实现的口径问题」，与是否引入三语义无关。
- 影响：多地址节点（`star_device` 的 `config["addr"]` 是数组，可带多条）在跨域冗余路径下拿不到第二条可用链路 —— 冗余设计失效，但不会算错。
- 复现/验证：未复现（代码级）。核对命令：`sed -n '15,23p;116,129p;157,167p' link.cpp`。
- 建议：邻居表键从 uuid 改成 `(域, uuid)`（或在 `addr_b` 层记录域），`degree()` 相应去重；属设计改动，需先拍板。

### E3. 测试没迁到 B 世代：`polydtor_test` 目标缺 `addr/` 下 5 个 TU，`test/nebula_api/` 没有构建入口
- 位置：`test/CMakeLists.txt:156-172`（`add_executable(polydtor_test ...)` 的源列表里 nebula 侧只有 `../network/nebula/addr.cpp`）、`test/polydtor_test.cpp:37/40/54/76`（用 A 世代的 `to_json`/`clone`/`addr_t<kind>`）、`test/nebula_api/`（目录里只有源码，无 `CMakeLists.txt`）
- 现状：测试文件仍按 A 世代 API 写（`addr_t<nebula::kind::ipv4>`、`clone()`、`to_json()`），而符号在 `addr/` 下 5 个 TU 里、目标却没列 ⇒ 链接必缺符号。`test/nebula_api/nebula_api_test.cpp:39/286/309` 同样用 A 世代 API。
- 证据：`sed -n '150,172p' test/CMakeLists.txt`；`grep -n "addr_t\|clone\|to_json" test/polydtor_test.cpp test/nebula_api/nebula_api_test.cpp`；`ls test/nebula_api`（无构建文件）。
- 影响：这两个目标编不出/链不出，模块没有可跑的回归入口（本仓无 CI）；B 世代的回归目前只能靠 `build/` 下的临时自检程序（且见 G1）。
- 复现/验证：`cmake -S test -B /tmp/tb && cmake --build /tmp/tb --target polydtor_test`（预期链接期缺 `addr_b`/`addr_ipv4` 等符号；本轮未跑，按源列表即可判定）。核对命令：`sed -n '156,172p' test/CMakeLists.txt`。
- 建议：把 `polydtor_test` 迁到 B 世代 API 并补齐源列表（或把 `addr/` 下 5 个 TU 加进去），`test/nebula_api/` 要么建目标、要么标注为「历史参考、不构建」。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

> 这一组是**有意为之**的决定与语义取舍，不是缺陷。理由取自代码/注释里的原始口径，代价如实写出；将来若要改，先翻 `DEFERRED.md` 看当时记的待拍板点。

### F1. `data::remove` 只删内存窗口，内核历史保留
- 位置：`data.cpp:53-81`（`remove` 完全不碰 `m_kernel`）、`data.h:60-61` 注释
- 取舍：历史是落库资产，删通道不等于销毁历史（`DEFERRED.md` §5.2 已确认「**是有意的**」）。
- 代价：与 B4 是同一条路 —— 历史只会涨；`remove` 之后 `select_history` 仍能查到该通道的全部旧帧。
- 建议：不修（维持）；要控规模就在内核侧加裁剪（B4）。

### F2. 串口 `send_broad` 不写缆（点对点没有物理广播）
- 位置：`addr/addr_serial.cpp:880-894`（只做进程内同 kind 端点广播）
- 取舍：一条缆只有一个对端，「写串口总线」会把帧丢给不带路由信息的未知设备；对齐 ipv4「无 UDP 腿 → 进程内广播」的既有纪律（`DEFERRED.md` §2 记录了备选与结论「保持现状，未动」）。
- 代价：缆上 `bind` 求邻不上线 ⇒ 串口直连的两个节点要先静态挂邻居句柄，或等对端先发帧（首帧 `send_node` 学得 uuid 后回程才能复用那条缆，`addr_serial.cpp` `send_remote` ①②③）。
- 建议：不修。

### F3. ipv6 用组播近似广播：每个已加入接口各发一份（重复投递）
- 位置：`addr/addr_ipv6.cpp:736-784`（`send_broad`：对 `m_ifaces` 逐个 `IPV6_MULTICAST_IF` + `sendto`，全部失败/无接口则退化进程内广播）、`addr_ipv6.h:64`（`s_group = "ff02::1"`）
- 取舍：IPv6 没有 IPv4 式广播；「冗余换可用性」保证同机回环与各 LAN 接口都覆盖。去重留给上层（`link` 的 `bind`/`link`/`ping` 目前没有事件 id 可去重，见 `DEFERRED.md` §1）。
- 代价：同一对端会收到 N 份（N = 成功加入组播的接口数）；重复事件靠软状态幂等吸收。
- 建议：不修（要减少重复再按 `DEFERRED.md` §1 做 `(send_node, id)` 短时去重表）。

### F4. `kind` 词表先行：`bluetooth` / `nearlink` / `quantum` 只有词与文本互转，没有实现类
- 位置：`addr/addr_b.h:14-24`（枚举）、`addr/addr_b.cpp:12-37`（`kind_text`/`text_kind` 全词表）、`addr/addr.cpp:10-21`（`addr_from` 只造 `uuid`/`ipv4`/`ipv6`/`serial`，其余 `default: a = nullptr;`）
- 取舍：词表先于实现存在，`addr_from` 对未知 kind 返回空句柄是**明确契约**（调用方用 `if (addr ad = addr_from(j))` 判空，见 `link.cpp:351`、`:376`）。
- 代价：看到 `"kind":"quantum"` 的配置不会报错，只会静默变成「没有地址」。
- 建议：不修。

### F5. 没有节点间鉴权与加密（uuid 即凭证）
- 位置：`link.cpp:321-362`（`exec_ping` 只看 `send_node`：`:326`；`exec_bind`/`exec_link` 只看 `send_node`/`send_addr`/`recv_addr`：`:344-345`、`:369-370`）、`zero.cpp:118-134`（`claim` 回给 `from` 不校验身份）、`addr.cpp:7-33`（`addr_from` 直接按载荷里的 kind/host/port 造句柄，不校验来源）
- 取舍：零号协议面向可信内网：事件即帧、软状态、无中心服务；加签名/加密会引入密钥分发与握手状态，与「问不等答、乱序等价」的模型冲突。模块内除 `xhqm::cipher::uuid`（`find.cpp:305-306`、`:368`）外没有任何加解密代码。
- 代价：同网任何进程都能伪造 `send_node` 冒充任意节点、读改帧、发 `claim`/`proxy` 索取信息或触发工具执行；uuid 只作身份不保密。
- 建议：不修（如需安全，应在部署侧用隔离网络/隧道，而不是改这一层）。

### F6. 问不等答、不设投递确认
- 位置：`link.cpp:199-212`（`send_heart` 只看 `send_frame` 返回值）、`link.cpp:273-298`（`exec_send`）、`addr_b.h:57-59`（`send_frame` 家族契约）
- 取舍：`send_frame(value, timeout_ms)` 的 `timeout` 只等「已交给内核/队列排空」（ipv4 等 `outbox` 空、ipv6 等 `pending` 落内核、串口等队列排空、`addr_uuid` 直接忽略该参数），返回值是**本端连接可用性**，不是对端收到；丢帧由软状态 + 心跳自愈（`dath` 判死 + 重新 `bind`）。
- 代价：丢帧不可知、不可重传；上层要确认必须自己加回执（消费方用 `message` 当回执）。
- 建议：不修。

### F7. 有界发队列「满丢最旧」（ipv4 1024 / ipv6 64 / 串口 64）
- 位置：`addr_ipv4.h:85`（`s_outbox = 1024`）、`addr_ipv4.cpp:865-869`；`addr_ipv6.h:60`（`s_outbox = 64`）、`addr_ipv6.cpp:693-694`；`addr_serial.h:163`（`s_outbox = 64`）、`addr_serial.cpp:853-857`（`send_remote` 的入队分支）
- 取舍：零号协议允许丢与乱序，队列必须有界（内存可控）；配合「入队即唤醒」与 drain-until-empty 吸收突发（`DEFERRED.md` §3 的调优结论）。
- 代价：突发超过上限就丢最旧的帧（无告警）；ipv4 那条「丢最旧 + `sent = 0`」还与 B1 的悬垂引用组合成内存安全问题 —— 取舍本身没问题，实现没跟上。
- 建议：不修取舍；修 B1 的实现。

### F8. `post::max_fail` 缺省 1：一次不可达即摘订阅者
- 位置：`post.h:37`（`xhqm::size m_max_fail = 1;`）、`post.cpp:97-124`（`dath` 时 `fail >= m_max_fail` 即摘）
- 取舍：订阅是软状态，摘掉比留着更好（投不出去的订阅者不该永久占表）；恢复靠订阅者重新 `care`（`post.cpp:41-52` 里 `care` 会把 `fail` 清零）。
- 代价：瞬时抖动（路由刚变、邻居刚断）就可能摘掉订阅者，需要上层重订。
- 建议：不修（要更宽容的调用方自己 `max_fail(n)`）。

### F9. `claim` 只回 `zero_info()`，不额外带工具声明
- 位置：`zero.cpp:118-134`（`exec_claim`：`info = this->zero_info();` → `create_message({goto, data:info})`，全程不碰 `m_tool_kitset`）、`zero.h:9` 的注释口径
- 取舍：`zero` 层只负责把「节点的自我声明」原样回给 `from`；工具表的落点由具体节点决定 —— `star::zero_info()`（`star.h:89-104`）确实把 `this->kitset()` 的 `make_json()` 数组塞进 `info["list"]["tools"]`，所以 `star` 系节点的 claim 应答**带**工具表（在 `list.tools`，不在顶层）；直接派生 `zero` 的节点若无此实现就只有 `info`。
- 代价：想拿工具表必须知道「`star` 的声明在 `list.tools`」这个形状（消费方 stella 正是按 `list.*` 取的：`stella/type/mcpcpp/type_mcpcpp.cpp:154-158` 先调 `_hand->zero_info()`，再读 `info()["list"]` 的 `uuid`/`star`）；顶层没有 `tools` 键。
- 建议：不修（形状变了会破线上格式）。

### F10. 地址装配外部化：`config` 的 `answer`/`listen` 无人读取
- 位置：`link.cpp:71-78`（`insert_addr` 只能外部调）、`zero.cpp:23-39`（两个 config 便捷构造都不读 `answer`/`listen`）、全模块 `grep "answer"`/`"listen"` 在配置层 0 命中
- 取舍：一个节点可持多个地址（`star_device` 的 `config["addr"]` 是数组），地址实现也可能在库外，所以「造地址」这件事不该由基类猜；`insert_addr` 之后必须调 `life()` 才算开工（`link.cpp:25-52`）。
- 代价：只 `zero(config)` 造出来的节点是孤立的（本地能力齐全、入不了网），而且不报错 —— 新手最容易踩。
- 建议：不修（README 的用法示例已给出消费方真实写法）。

### F11. 各传输的进程级静态表（回环/复用只在本进程内有效）
- 位置：`addr/addr_uuid.cpp:7-9`（`uuid_mutex`/`uuid_broad`/`uuid_registry`）、`addr_ipv4.cpp:25-29`（`ipv4_mutex`/`ipv4_registry`/`ipv4_conn_mutex`/`ipv4_conns`）、ipv6/串口同构
- 取舍：同 uuid 直投避免同进程绕 socket（联调/单测可离线跑，见 `addr_uuid.cpp`）；表键是 uuid，所以同进程内「端点与它的克隆句柄共用同一条长连」。
- 代价：**跨进程不共享**：这些表只对本进程有效，uuid 也不构成任何跨进程唯一性保证；析构必须自己退表（`addr_uuid.cpp:37-47` 就是这么做的，否则留悬垂指针）。
- 建议：不修。

### F12. ipv6 的 `make_json` 不做 `host` 兜底（没有 ipv4 的 `local_ipv4_hint` 等价物）
- 位置：`addr/addr_ipv6.cpp:175-182` 的 `make_json`（`j["host"] = m_host;` 原样播出）、对照 `addr_ipv4.cpp:189-201`（通配/空 host 时用 `local_ipv4_hint()` 顶替）
- 取舍：IPv6 侧的主要发现路径是组播 + 首帧学身份；对端拨入的连接会进进程级连接表（`addr_ipv6.cpp:421-427` → `ipv6_conn_put`），回程直接复用它（TCP 全双工），所以「host 为空」不影响「对端先连我」这条主路径。
- 代价：双方都想主动拨号（各自先发 `send`/`care`）时，`host` 为空/通配的一方建不出可拨地址 ⇒ 定向帧发不出（`send_remote` 需要 `!host.empty() && port > 0`，`addr_ipv6.cpp:673`）。
- 建议：不修（`DEFERRED.md` §5.2 已列「未动」）。

### F13. ipv4 的 `local_ipv4_hint` 用「UDP connect 到 8.8.8.8:53 + getsockname」推导本机对外地址
- 位置：`addr/addr_ipv4.cpp:161-186`（`local_ipv4_hint`）、调用点 `:189-201`
- 取舍：宁可把一个「对端能回连」的地址播出去，也不要把 `0.0.0.0`/`*` 播出去（对端拿通配地址无法拨号）；实现只要一次 UDP connect，不发包、无外部依赖。
- 代价：机器没有默认路由（或 UDP 被策略拦）时推导结果为空，此时 `make_json` 仍会播出原来的通配 `host` —— 兜底失效，且没有告警。
- 建议：不修；如需更强的兜底，可退化为「取第一个非回环接口地址」。

### F14. 区域图的**节点身份缓存**不过期（只有边会老化）
- 位置：`find.h:62`（`nodes`：`uuid → 事件端点（身份缓存）`）、`find.cpp:105-113`（`clear(timeout)` 只擦边）、`find.cpp:114-119` 的注释（「界外节点留作身份缓存」）、`find.cpp:61-69`/`:85-86`（收到就建档）
- 取舍：删了节点身份会让 `short_path` 的端点缺失（`find.cpp:168` 端点不在视图 → 直接返回空；`make_json` 的注释也说明「有边无点」的图会让对端路由失效），所以身份只增不删是有意的。
- 代价：见过的 uuid 永久驻留（含每个节点缓存的 `addr` 句柄与 `info`）。消费方多数节点每次启动都换 v4 uuid（只有 `star_device` 按配置钉住 uuid），所以长期运行 + 频繁重启会让这份缓存缓慢增长 —— 量级远小于 B4，但同样是「只增不减」。
- 建议：不修（真要收敛，就在 `clear(timeout)` 里顺带删「无边的节点」，但那会退回上面那条缺陷）。

---

## G. 文档与代码的出入

### G1. `DEFERRED.md` 里「自检 ALL PASS」已失效：`build/nebula_b_check.cpp` 在 r677 编不过
- 位置：`DEFERRED.md:78`（「行为验证：`build/nebula_b_check.cpp` … `ALL PASS`」）、根因 `zero.h:172`（`virtual nebula::json zero_info() = 0;`）、失效的测试夹具 `build/nebula_b_check.cpp:26-71`（`check_node` 只实现了 `logic()`）
- 现状：r676 起 `zero::zero_info()` 是纯虚，而这份自检程序的 `check_node` 没有实现它 ⇒ 它是抽象类，`check_node node(config);` 编不过；`DEFERRED.md:78` 的「ALL PASS」对这个版本不再成立（该程序是 `build/` 下的本地临时件，不受版本控制，所以 `svn status` 看不到）。
- 证据（本轮实测）：`g++ -std=c++17 -fsyntax-only -I. -I../depend build/nebula_b_check.cpp` →
  `error: cannot declare variable 'node' to be of abstract type 'check_node'` / `note: pure within 'check_node': 'virtual xhqm::nebula::json xhqm::nebula::zero::zero_info()'`（`zero.h:172`）。
- 影响：接手者按 `DEFERRED.md` 复跑自检会直接撞编译错误，误以为环境坏了；README 的旧用法示例是同一处病（本轮已在 README 补齐 `zero_info()` 并加了订正说明）。
- 复现/验证：见上面的命令与输出（可直接重跑）。
- 建议：给 `check_node` 补一个 `zero_info()` override（一行），并在 `DEFERRED.md` 该行标注「r676 后需补 `zero_info()` 才可复跑」——`DEFERRED.md` 按纪律本轮未改，留给上游。

### G2. `find.h` 头注释「路由 = 合并图上 dijkstra（每次合并/老化后重算）」与实现不符
- 位置：`find.h:7`（原文逐字为 `//   - 路由 = 合并图上 dijkstrap每次合并/老化后重算）` —— `dijkstrap` 与结尾的孤立括号是文件自带的笔误）
- 现状：实现里没有任何「合并/老化后重算」的动作，也没有缓存 —— `short_path` 每次调用现建 dijkstra 适配层（`find.cpp:170-196`）。
- 证据：`find.h:7` vs `find.cpp:160-213`、`:446-450`。
- 影响：读注释的人会以为路由是「预计算 + 事件驱动刷新」，据此做性能假设（比如以为查询是 O(1)）会错。
- 复现/验证：`sed -n '4,8p' find.h` 与 `sed -n '170,196p' find.cpp` 对照即可。
- 建议：注释改成「路由 = 每次查询现建 dijkstra（无缓存）」。

### G3. `graph::clear(base, pathcount)` 全仓无调用者（死代码），而注释/文档把它当成可用的视图收缩手段
- 位置：`find.h:68`（声明）、`find.cpp:114-159`（实现，注释详细描述了「视图被限制在 base 的 pathcount 邻域内」）
- 现状：全仓（含 stella/其它模块）没有一处调用它；视图收缩实际只靠 `clear(timeout)` 的时长老化（`find.cpp:229`）。
- 证据：`grep -rn "clear(base\|graph::clear" --include=*.h --include=*.cpp .` 只命中 `find.h:68` 与 `find.cpp:105`、`:114`（声明 + 两个实现，无调用）。
- 影响：看起来「L2 有半径收缩能力」，实际从未启用；`README.md` 的 API 块与 `find.h` 注释都会让人误判。
- 复现/验证：上面的 grep（0 个调用者）。
- 建议：要么接上（`exec_dath` 里按需调用），要么删掉并在 `find.h` 注明「未启用」。

### G4. `data.cpp:55` 注释说局部变量存「{通道 uuid, 内核 form}」，实际只存 uuid
- 位置：`data.cpp:55`（`std::vector<nebula::uuid> kill;   // {通道 uuid, 内核 form}`）、使用在 `:63`、`:74`、`:75-78`
- 现状：`kill` 只 push 通道 uuid，从不存 form；`remove` 也从不碰内核（见 F1）。
- 证据：`sed -n '53,81p' data.cpp`。
- 影响：注释暗示 `remove` 会顺带清内核条目，与 F1 的实际语义相反 —— 想「连历史一起删」的人会写错。
- 复现/验证：同上。
- 建议：注释改成「通道 uuid 列表（只删内存窗口）」。

### G5. `link.h:40` 的 `recv_event` 注释是复制粘贴错：「发送一条广播（公开，无 ACK）」
- 位置：`link.h:40`（`virtual void recv_event(const nebula::uuid&, const nebula::json&);	//发送一条广播（公开，无 ACK）`）
- 现状：`recv_event` 是**收**帧入口（`link.cpp:226-232`：补 `recv_time`/`recv_addr` 后转 `life_event`），不是发送。
- 证据：`link.h:38-40`（三条注释里只有 `recv_event` 那条与函数名矛盾）vs `link.cpp:226-232`。
- 影响：轻微（注释级），但会误导对「谁负责发/谁负责收」的判断。
- 复现/验证：`sed -n '38,40p' link.h`。
- 建议：改成「收到一条帧（补 recv_time/recv_addr 后入事件池）」。

### G6. `addr_b.h:59` 把带超时的 `send_frame` 注释成「带 ACK」
- 位置：`addr/addr_b.h:59`（`virtual bool send_frame(const json& value, sint timeout_ms) = 0;	// 发送数据，地址是邻居地址（带 ACK）`）
- 现状：三处实现都只是等**本端**队列/内核：ipv4 等 `outbox` 排空（`addr_ipv4.cpp:881-887`，返回值 `c->good`）、ipv6 等 `pending` 落内核（`addr_ipv6.cpp:698-705`）、串口等队列排空（`addr_serial.cpp:853-870` 的 `send_remote`），`addr_uuid` 直接忽略该参数（`addr_uuid.cpp:73-76`）。没有任何「对端已收到」的确认。
- 证据：`addr_b.h:57-59` vs 三处实现；`README.md` 的 API 块已按「等『已交给内核』，非投递确认」写。
- 影响：按注释理解成「带 ACK 的可靠发送」会在丢帧时误判成功。
- 复现/验证：`sed -n '55,60p' addr/addr_b.h` 与 `sed -n '881,888p' addr/addr_ipv4.cpp` 对照。
- 建议：注释改成「等本端写出队列（非投递确认）」。

### G7. `zero.h` 头注释「claim 应答里带 tools 声明」与 `zero::exec_claim` 不符
- 位置：`zero.h:9`（`//     claim   索取（goto 的 uuid/name/info/tool，结果经 message 返回）`）、`zero.h:12`（`//         挂到节点上即全网可见（claim 应答里带 tools 声明）`）、实现 `zero.cpp:118-134`
- 现状：`zero::exec_claim` 只把 `zero_info()` 原样回给 `from`，完全不绑定 `m_tool_kitset`；工具表是否出现在应答里取决于节点的 `zero_info()` 实现 —— `star::zero_info()`（`star.h:89-104`）确实在 `info["list"]["tools"]` 里带了（见 F9）。
- 证据：`zero.cpp:128-133`（锁内只取 `zero_info()`）vs `zero.h:9`/`:12` 的注释。
- 影响：照 `zero.h` 的注释以为 `exec_claim` 自己会附加 `tools`、或以为顶层有 `tools` 键，都会写错消费侧解析。
- 复现/验证：`sed -n '118,134p' zero.cpp` 与 `sed -n '89,104p' star.h` 对照。
- 建议：注释写明「应答 = 节点自己的 `zero_info()`；`star` 的实现把工具表放在 `info["list"]["tools"]`」。

### G8. README 里关于 `api_nebula` 的论断在本工作副本内无法核对
- 位置：原 `README.md`「已知限制 / 待办」最后一条（2026-10-07 已收口到本节，`README.md` 里只留指向）。原文逐字如下，**未删除**：
  > `- 测试与消费方未迁移：`test/polydtor_test.cpp`、`test/nebula_api/` 仍是 A 世代 API（`addr_t`/`to_json`/`clone`）；`api_nebula` 的 CMake 仍指向 `../xhqm-pro/xhqm`，其 `server/action_node.h` 按 9 参 `zero(path,m,n,c,a,answer,listen,size,time)` 构造，而现在的构造函数是 7 参。`
- 现状：本工作副本与仓库根都**没有** `api_nebula/`（`svn ls ^/` 列出的顶层为 CMakeLists.txt / README.md / algebra / fantasy / hardware / head.cpp / head.h / lattice / metaclass / network / software / test / xhqm / xhqm.h / xhqm.sln / xhqm.vcxproj*），`test/nebula_api/` 里也没有构建文件；全树内 `xhqm-pro` 只出现在 `.svn/pristine/` 的历史副本里（即 `DEFERRED.md` 的旧版本）。
- 证据：`svn ls ^/`；`ls test/nebula_api`（client/common/server/nebula_api_test.cpp/PROTOCOL.md/README.md）；`grep -rn "xhqm-pro" --include=*`（仅 `.svn/pristine/**`）。该句里可核对的那一半**为真**：现在的 `zero` 构造函数确实是 7 参（`zero.h:131`：`path,m,n,c,a,size,time`），A 世代那套 `answer/listen` 也确实不在 cfg 里（见 F10）。
- 影响：`api_nebula` 那一半**未确认**（既不能证真也不能证伪）—— 接手者若照原句去找 `api_nebula/CMakeLists.txt` 会找不到。「`test/polydtor_test.cpp`、`test/nebula_api/` 仍是 A 世代 API」这一半已核实（见 E3）。
- 复现/验证：上面的三条命令（本轮实测）。
- 建议：等能访问那棵树时再回填（带上版本号与命令），否则在 README 里明确标「外部树，未核对」。

---

## 附：本轮核对过的「没有问题」项（避免下次重复怀疑）

- 模块内 **没有** `TODO` / `FIXME` / `XXX` / 「占位」/「桩」标记（`grep -rn "TODO\|FIXME\|XXX\|占位\|未实现\|暂不\|尚未\|桩" network/nebula --include=*.h --include=*.cpp` 只命中 `zero.h:129`、`zero.cpp:11`、`data.h:33/78`、`data.cpp:175`、`addr_ipv6.h:51` 这些「尚未构造 / 建档桩 / 尚未落内核」的正常注释）。
- `CMakeLists.txt` 的 `XHQM_HEAD_NETWORK`（`:341-354`，14 个头，含 `star.h`）与 `XHQM_BODY_NETWORK`（`:378-390`，13 个 TU）和 `xhqm.vcxproj`（ClInclude 14 / ClCompile 13）+ `.filters` 收录同一份文件集；开关宏名是 `XHQM_BUILD_NETWORK`（`CMakeLists.txt:42`，`-DXHQM_BUILD_NETWORK` 在 `:557-558`）；`software/signal.cpp` 在 `XHQM_BODY_CORE` 里无条件编入（`set(XHQM_BODY_CORE …)` 自 `:250` 起，`software/signal.cpp` 在 `:264`）。
- `addr_uuid` 的「首个实例进注册表、克隆连转发槽、析构退表、`send_frame` 不按 `is_open()` 拦截」四件事与 README 描述一致（`addr_uuid.cpp:22-47`、`:65-71`）。
- ipv4 的 `reconcile_live` 收敛规则与 README 一致（uuid 大的一侧丢自己 dial 的那条，且要求存在接入连接才动手：`addr_ipv4.cpp:500-517`）。
- ipv4 的句柄关闭顺序与 README 一致（`closed()` 先停连接、join reactor，最后关 监听/UDP/唤醒管：`addr_ipv4.cpp:313-380`）。
- 串口「对端播来的设备名被拒绝拨号、只有注入流/工厂才建链」与 README 一致（`addr_serial.cpp:845-852` 的 `send_remote` ③：只有 `(m_factory || m_inject)` 才允许按邻居句柄里的 host 开缆）。
- `link.h:23` 的 `std::atomic<bool> m_ping_runing;` 声明处无初值，但构造初始化列表给了 `m_ping_runing{ false }`（`link.h:43`）—— 正确但脆弱，非缺陷。
