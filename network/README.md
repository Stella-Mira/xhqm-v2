# xhqm/network 模块

> 最后同步：2026-10-07，对照工作副本 r677。

## 模块定位

`xhqm/network` 是 xhqm 引擎的网络与相关基础能力模块。它并非单纯的“套接字库”，而是把 xhqm 中与「连接、消息、身份、加密、记忆、分布式拓扑」相关的能力集中收编于此，形成一条自底向上的完整栈：

- **传输层**：跨平台（Windows / Linux / macOS）TCP、UDP、socket 句柄抽象与帧协议；
- **加密与编码层**：MD2/4/5、SHA-1/2/3、AES、DES、Base64、UUID、软件授权（license）；
- **记忆层（astral）**：以「一切皆节点」为模型的本地记忆内核，支持 JSON / 二进制 / mmap 索引持久化；
- **分布式网络层（nebula）**：地址多态（addr）→ 通道与记忆（data）→ 事件池（life）→ 有界度邻居图（L1 link）→ 区域图发现与路由（L2 find）→ 订阅推送（L3 post）→ 工具封装与节点组装（zero/star），细节见 [`nebula/README.md`](nebula/README.md)。

模块命名空间依次为 `xhqm::network`（传输）、`xhqm::cipher`（加密/编码/UUID）、`xhqm::astral`（记忆）、`xhqm::nebula`（分布式网络）、`xhqm::license`（授权）。

---

## 目录结构

```
network/
─── network.h / network.cpp    平台抽象 + TCP/UDP 原语 + 帧协议（header 内联为主；network.h 505 行）
─── socket.h / socket.cpp      socket 句柄 RAII 与地址解析辅助（**当前无任何调用者**，见下「补充说明」）
─── tcp.h / tcp.cpp            tcp_conn / tcp_server / tcp_client
─── udp.h / udp.cpp            udp_socket / udp_server / udp_client
─── cs.h                       C/S 模式模板（cs_server / cs_client / frame_stream；151 行）
─── mmap.h / mmap.cpp          只读内存映射文件（跨平台）
─── md2/md4/md5.{h,cpp}        MD 系列散列（md2.h 68 行 / md4.h 48 行 / md5.h 61 行）
─── sha.{h,cpp}                SHA-1 / SHA-2 / SHA-3 全系（sha.h 175 行）
─── aes.{h,cpp}                AES-128/192/256（**只有 ECB/CBC**，PKCS#7）
─── des.{h,cpp}                DES（**只有 ECB**，PKCS#7）
─── base64.{h,cpp}             Base64 编解码
─── uuid.{h,cpp}               UUID v1/v2/v3/v4
─── license.{h,cpp}            软件授权（序列号 / 授权码编解码 / 校验）
─── httplib.h                  收编第三方 header-only HTTP（10 509 行；A 世代 SSE 用，当前无引用，备用）
─── json.hpp                   收编第三方 nlohmann/json（引擎全局 JSON）
─── astral/                    记忆内核（astral；细节见 astral/README.md，161 行）
│   ─── astral.h               node 数据模型
│   ─── kernel.h/.cpp          线程安全节点内核（CRUD/持久化/惰性加载）
│   ─── evoke.h/.cpp           标签语义层（原 kernel_tag：create/upsert/submit/select_*/rebuild_names）
│   ─── thing.h/.cpp           实体语义层（events 事件流 / object 属性快照 + 溯源）
│   └── mmap_index.h/.cpp      只读 mmap 二级索引（10 亿级）
└── nebula/                    分布式网络（nebula，B 世代；细节见 nebula/README.md，540 行）
    ─── nebula.h/.cpp          node 身份基类（uuid/name/info + make_json）+ 类型别名
    ─── addr.h + addr/         addr_b 地址抽象（邻居表 + 双信号）+ addr_uuid / addr_ipv4 / addr_ipv6 / addr_serial
    ─── data.h/.cpp            通道树（node/device/source）+ 实时窗口 + astral 记忆内核
    ─── life.h/.cpp            事件池（life_event → 线程池 → exec_event 分层分发）
    ─── link.h/.cpp            L1 有界度邻居图（入网 + 心跳判死 + send/ping/bind/dath 词表）
    ─── find.h/.cpp            L2 区域图发现 + dijkstra 路由 + 逐跳转发（layer/graph）
    ─── post.h/.cpp            L3 订阅推送（care/omit/push + 中继路由）
    ─── zero.h/.cpp            零号协议顶层（kit 表 + 词表 + config 组装构造）
    └── star.h                 组件组合模板（type 组件 + star<...>）
```

文件集口径：CMake 的 `XHQM_HEAD_NETWORK` / `XHQM_BODY_NETWORK`（`CMakeLists.txt:317` / `:359`）与本目录实测**一一对得上**
——`network/` 下 30 个 `.h/.cpp` + `astral/` 9 个 + `nebula/` 13 个 TU，**唯一不在 CMake 清单里的是 `httplib.h`**
（`CMakeLists.txt:33` 的注释把它标为「A 世代备用」）。`XHQM_BUILD_NETWORK` 打开时才收进构建（`:558-560`），但
`network/uuid.{h,cpp}` 与 `network/md5.{h,cpp}` 被 `:546-549` 无条件追加（fantasy/system 也依赖它们）。

---

## 关键类 / API

### 1. 传输层（`xhqm::network`）

`network.h` 把平台差异收敛为三个句柄级别名，后续代码只依赖抽象，不感知 OS：

| 别名 | Windows | Linux/macOS |
|------|---------|-------------|
| `hander` | `SOCKET` | `int` |
| `error_id` | `int` | `int` |
| `error_socket` | `INVALID_SOCKET` | `-1` |

核心接口（多数为 `inline`）：

- 生命周期：`init_network()` / `exit_network()` / `good_network()`（引用计数管理 WSA 初始化）；
- 底层：`open_socket` / `close_socket` / `error_code`；
- 字节收发（循环定长，Windows `INT_MAX` 分块）：`send_bytes` / `recv_bytes`；
- 地址解析（IPv4/IPv6/主机名统一，优先 IPv6）：`resolve_addr`；
- TCP：`open_tcp`（双栈）、`listen_tcp`、`accept_tcp`、`connect_tcp`、`connect_tcp_auto`，以及非阻塞 + `poll` 超时的 `connect_addr_timeout` / `connect_tcp_timeout` / `connect_tcp_auto_timeout`（注释明确说明 Windows 句柄可能超过 `FD_SETSIZE`，故放弃 `select` 改用 `poll`）；
- UDP：`open_udp`、`bind_udp`、`sendto_udp`、`sendto_broadcast_udp`、`recvfrom_udp`（自动把 IPv4-mapped IPv6 还原成纯 IPv4 文本）；
- 超时：`set_recv_timeout` / `set_send_timeout`（防止对端停止读取时 send 永久阻塞）；
- 帧协议：`put_u32` / `get_u32` / `send_frame` / `recv_frame` —— **4 字节小端长度前缀 + 负载**，帧长上限 1GB 防异常。

`tcp.h` 提供帧级 RAII 类：

- `tcp_conn`：一条已建立连接，`send` / `recv` / `recv_timeout` / `send_timeout` / `close` / `good`，移动语义（不可拷贝），`attach()` 供 accept/connect 后挂接句柄；
- `tcp_server`：`listen(port)` / `accept()`（返回 `tcp_conn*`，调用方负责 delete）/ `recv_timeout`；
- `tcp_client`：`connect(host, port)` / `connect_timeout(host, port, ms)` + 帧级 `send` / `recv`。

`udp.h`：`udp_socket`（`bind` / `send_to` / `broadcast_to` / `recv_from` / `local_port`），`udp_server`（= `listen`）与 `udp_client`（= `open(port=0)`）。

`socket.h`：`sock`（句柄 RAII 基类，析构自动 close）与 `socket`（`af/type/protocol` 参数封装 + `ipv4path` / `ipv6path` 地址文本解析）。~~`ipv6path` 目前为未实现桩~~ **已作废（2026-10-07 复核 r677）**：`ipv6path` **确实是** `return false` 的桩（`socket.cpp:90-94`），但旧文漏了两件更要紧的事——① `ipv4path`（`socket.cpp:74-88`）虽然写完了，**全仓一个调用者都没有**；② 整个 `socket.h`/`socket.cpp` 没有实例化点，IPv4/IPv6/主机名的地址解析实际全走 `network.h:122` 的 `resolve_addr`（`getaddrinfo`，`network.h:122-141`）。也就是说这一对静态函数当前是**未被使用的死码**，不是「可用的地址解析辅助」。

`cs.h`：通用 C/S 模板，传输层可替换：

- `frame_stream<byte_stream>`：把无帧边界的字节流（如串口）包成帧级会话；
- `cs_server<server_type, session_type>`：`run()` 阻塞 accept 循环，每会话独立线程 `serve()`，`handler` 为 `std::string(req) -> std::string(resp)`；
- `cs_client<client_type>`：同步 `request(req, resp)`。

`mmap.h`：`xhqm::mmap` 只读内存映射文件（Windows `MapViewOfFile` / POSIX `mmap`），提供 `open/close/buffer/data/size/good/error`。

### 2. 加密与编码层（`xhqm::cipher`）

统一风格：构造/`preprocess` → `operator()(message)` 返回自身 → `operator std::string()` 输出十六进制摘要。

| 类 | 算法 / 能力 |
|----|-------------|
| `md2` / `md4` / `md5` | 对应 MD 散列，16 字节摘要转 hex（三者对 `"abc"` 均与标准向量一致，本轮实测） |
| `sha` | `sha_type` 枚举有 9 个值（`sha.h:13-26`）：`sha1` / `sha2_224/256/384/512` / `sha3_224/256/384/512`。**实测只有 SHA-1 与 SHA-2 四个是对的**；`sha3_*` 四个是空壳，输出**恒为全零且与输入无关**（`sha.cpp:522-525` 四个 `_sha3_*` 是 `{ return (*this); }`，`init()`/`preprocess` 也不处理）——别拿它做指纹，详见下「补充说明」与 `DEBUG.md` A1 |
| `aes` | AES-128/192/256，`set_key` / `set_mode_ecb` / `set_mode_cbc`，PKCS#7 填充 |
| `des` | 单 DES（56 位密钥，8 字节 key），ECB + PKCS#7 |
| `base64` | `encode` / `decode` / `encode_text` / `decode_text` |
| `uuid` | `creat_uuid_v1/v2/v3/v4`、`to_text` / `to_uuid`、`uuid_equal`、`uuid_less`（`std::map` 键比较器） |

> **用之前先看这一条**：`sha(sha_type::sha3_*)` 返回全零常量，`socket.h` 的一对 `ipv4path`/`ipv6path` 全仓无调用者，
> `aes` 没有 CFB/OFB/CTR。`license.cpp` 的 AES 密钥正好来自 `sha3_256`（`license.cpp:767`/`:802`），所以授权串
> 实际是**全零密钥**加密的——上线前务必先读 `DEBUG.md` 的 A1 与 E1/E3。

### 3. 授权层（`xhqm::license`）

- 结构体 `server`（授权方：版本/软件/范围/时间范围）与 `client`（被授权方：标识/客户/设备/备注）；
- 硬件指纹：`system_serial` / `board_serial` / `disk_serial` / `cpu_serial`、`get_idcard`（唯一标识）、`get_device`；
- 编解码：`server_to_base64` / `base64_to_server` / `client_to_base64` / `base64_to_client`；
- 读写校验：`read_license` / `save_license` / `good`（可注入版本/软件/范围/时间四个校验谓词）。

### 4. 记忆层（`xhqm::astral`）

「一切皆节点」模型（`astral.h`）：

```cpp
struct node {
    uuid   uuid;                     // 节点标识（未生成 = 全零，由 kernel 补 v4）
    text   info;                     // 描述备注
    json   datas = json::array();    // 真实数据 [{time, form, json}, ...]
    std::map<uuid, text, uuid_less> links;   // 关联节点 {uuid → info}
};
```

- `kernel`：线程安全（`std::shared_mutex` 读写锁）节点主存，支持「增删改查 + 双向关联」；查询 `select_uuid/select_info/select_data/select_time/select_size`；持久化 `load_json/save_json`、`load_binary/save_binary`、`save_mmap`（定长索引，供 mmap_index 二分）；惰性加载（冷节点 `fetch_impl` 加热）。
- `evoke`（`evoke.h` / `evoke.cpp`，原 `kernel_tag`）：在 `kernel` 上叠加标签语义——`create` / `upsert`（按 info 查重）/ `remove`（`time=0` 级联删）/ `submit`（补 links 或 data）/ `update`（改 info）、`select_links` / `select_words` / `select_datas` / `select_nodes`、`rebuild_names()`（重启后重建 name→uuid 表，否则标签重复创建）。
- `thing`（`thing.h` / `thing.cpp`）：实体层——`events`（父子事件流 + 有向父边）与 `object`（属性快照流 + 溯源 `prov`，读取取最后一帧 `snapshot`），另有 `merge` 身份消解与 `query_object` / `query_event` 检索。
- `mmap_index`：只读索引，`index_record`（64 字节，按 uuid 排序）+ `str_index_record`（32 字节，info 二级索引），`get(uuid)` 二分 + `find_info(info)`，物理内存只占访问页，面向 10 亿级节点。

### 5. 分布式网络层（`xhqm::nebula`）

B 世代分层栈（自下而上逐层继承，`addr` 旁挂；模块细节见 [`nebula/README.md`](nebula/README.md)）：

- **身份 `node`**：`uuid` / `name` / `info` + `make_json()`，全栈共用的序列化基类。
- **地址 `addr_b`（`addr/`）**：邻居表（`insert/remove/has/for_neighbors/run_neighbors`、`size_neighbors`、`alive/fail/last`、`is_open`）+ 双信号 `frame_signal`（定向事件帧）/ `broad_signal`（广播/组播帧）；`kind` 词表 `no/uuid/ipv4/ipv6/serial/bluetooth/nearlink/quantum` 配 `kind_text` / `text_kind` 文本互转。实现四个：`addr_uuid`（进程内回环，注册表直发信号，无线程）、`addr_ipv4`（TCP 长连接 + UDP 广播，单 reactor）、`addr_ipv6`（TCP 长连接 + 链路本地组播，单 reactor）、`addr_serial`（串口帧化字节流，每缆读/写两线程）。`using addr = std::shared_ptr<addr_b>`，`addr_from(json)` / `from_addr(list)` 负责序列化；地址由装配方 `link::insert_addr` 注入（config 里的 `answer`/`listen` 无人读取）。
- **数据 `data`**：三级通道树（`channel_kind::node_c/device/source`，节点通道 uuid = 节点身份）+ 每通道实时窗口 `window`（`deque<{纳秒, 帧}>`，按 `capacity` 条数 / `duration` 时长淘汰）；`create/remove（级联）/connect/channels/updata_info/insert_data` 全部同步进 `astral::kernel`，`select_message/select_windows/select_history/select_kernal` 负责实时与历史查询。
- **事件池 `life`**：`life_event` 校验 `type` 后投进 `xhqm::parallel::threads`（`c/a` 线程数上下限、`limt/time` 任务上限与保活），网络侧由 `link::recv_event(uuid, event)` 补 `recv_addr`/`recv_time` 转投；`exec_event` 虚函数做分层分发（基类优先），`bound_m/bound_n` 给出连接下限/上限。
- **L1 `link`**：有界度邻居图（`0<=m<=x<=n`）。心跳线程每 `m_t` 毫秒发 `ping`、本地 `dath` 判死（`fail=(now-last)/间隔`，超 `m_f` 移除邻居）、`x<m` 时广播 `bind` 求邻；收到 `bind`/`link` 时 `x<n`/`x<m` 才挂表。事件词表 `send/dath/ping/bind/link`，只认 uuid 与边。
- **L2 `find`**：区域图软状态（节点按 uuid 幂等覆盖、边带本地单调戳、默认 60 s 老化）。`layer` 逐跳询问（`deep` 递减 + 事件 id 去重防环）→ `graph` 回程合并；`short_path` 复用 `xhqm::dijkstra`（单位权 = 跳数）给出节点序列，`post` 的中继路由就建立在它上面。
- **L3 `post`**：订阅推送。信封 `{type, id, from, goal, channel, data?}`；`care`/`omit` 由订阅者发起、`goal` = 通道拥有者，拥有者维护 `m_channel`（通道→订阅者）与 `m_subscribers`（订阅者→`push_state{fail, channel}`）；`push`/`publish` 经 `find::exec_relay` 逐跳中继到 `goal`，发不出去计一次 `fail`，超 `max_fail` 或已无通道的订阅者在 `dath` 里清理；数据到达走信号 `care_hander(data, from, channel)`（不是虚钩子）。
- **顶层 `zero`**：`kit` 工具表（`ket_make/ket_name/ket_info/ket_uuid` + `string_param/number_param/boolean_param/object_param` 生成 JSON Schema），词表 `logic`（自逻辑）/ `proxy`（委托执行 kit，`argv` 原样下发、结果经 `message` 回）/ `claim`（回本节点 `info`）/ `message`（通知，`goto` 空 = 广播）；纯虚 `logic()` 由用户节点实现（`message` 到达走信号 `data_signal`，没有 `on_message` 虚函数）。
- **组装 `zero` / 组件模板 `star`**：`zero(config)` 从 JSON 配置构造（实际只读 `path/m/n/c/a/size/time`），`zero(config, m, n)` 按 `hardware_allow()` 派生池线程数（`c=min(m+1,硬件)`、`a=max(c,min(n*2,硬件))`）；`star<...>` 是组件组合模板（`type` 派生组件各自注册工具/连信号，`star` 管生命周期）。

---

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                   应用层 / 具体节点程序                       │
──────────────────────────────────────────────────────────────┤
│ nebula 节点栈（node → data → life → link → find → post →     │
│   zero → star，逐层继承，事件基类优先分发）             │
│   ── nebula::addr  地址多态（addr_uuid 回环 / ipv4·ipv6 单 reactor / serial）│
│   └─ nebula::data  通道树 + 实时窗口 + astral 记忆内核          │
──────────────────────────────────────────────────────────────┤
│ 传输层：xhqm::network（tcp/udp/socket/cs + 帧协议）            │
──────────────────────────────────────────────────────────────┤
│ 加密/编码/身份：xhqm::cipher（md*/sha/aes/des/base64/uuid）    │
──────────────────────────────────────────────────────────────┤
│ 持久化：xhqm::mmap + xhqm::astral（kernel / mmap_index）      │
──────────────────────────────────────────────────────────────┤
│ 第三方收编：httplib.h、json.hpp（nlohmann）                   │
└─────────────────────────────────────────────────────────────┘
```

设计要点：

1. **平台差异单点收敛**：`network.h` 顶部两个平台块定义 `hander` 等抽象，其余代码跨平台无 `#ifdef` 分支（超时连接、帧收发均如此）。
2. **帧协议全局统一**：4 字节小端长度前缀是 TCP、串口（`frame_stream`）、`cs_server`、nebula 事件通道共享的消息边界约定。
3. **继承分层 + 事件词表**：nebula 用 `node → data → life → link → find → post → zero` 逐层继承，`exec_event` 基类优先分发，每层只认自己的 `type`（`send/dath/ping/bind/link` → `layer/graph` → `care/omit/push` → `logic/proxy/claim/message`）；地址是旁挂句柄（`addr`），换传输不动上层。
4. **事件驱动 + 单心跳线程**：L2/L3 的维护（区域图老化、订阅过期、判死）全部挂在心跳驱动的 `dath` 事件上「顺带」完成；除 `link` 的心跳线程、ipv4/ipv6 每个端点一条 reactor 线程、串口每条缆的读/写两线程外没有周期线程。
5. **锁的边界**：astral `kernel` 用 `shared_mutex`；nebula 的邻居表 / 区域图 / 订阅表 / 通道表 / 工具表各自加锁，且持锁只做内存操作——阻塞网络 IO（拨号 connect 超时最长 1 s）一律先快照、再在锁外发；`zero` 的工具执行也在锁外（工具内部可再注册工具）。
6. **内存友好**：astral 惰性加载 + mmap 只读索引，面向海量节点时物理内存只占访问页。

---

## 依赖

- **引擎内部**：
  - `../head.h`（基础类型 `text/byte/size/u64`、`xhqm_io` 导出宏、JSON 引入开关 `XHQM_JSON_EXTERNAL`）；
  - `../metaclass/buffer.h`（`socket.h` 的地址解析）；
  - `../algebra/itype.h`、`../algebra/ftype.h`（astral）；
  - `../software/signal.h`（nebula 的 `frame_signal` / `broad_signal` 槽池；只开 NETWORK 时 `software/signal.cpp` 由 `XHQM_BODY_CORE` 无条件编入）、`../software/system/{timesys,filesys,threads,process}.h`（nebula 时间/文件/线程池）；
  - `../lattice/graph_algorithms.hpp`（`find.h` 复用 `xhqm::dijkstra`）。
- **第三方（收编 vendored）**：`httplib.h`（header-only HTTP；A 世代 SSE 用，当前无引用）、`json.hpp`（nlohmann/json，引擎全局 JSON，默认由 `head.h` 引用本文件）。
- **系统**：Windows 需 `ws2_32.lib`（WinSock2），POSIX 用 `sys/socket.h` / `netinet/in.h` / `sys/mman.h` 等。
- **编译选项**：`XHQM_JSON_EXTERNAL`（使用外部 nlohmann）、`XHQM_PROGRAM` / `XHQM_DYNAMIC`（导出/导入宏）。

---

## 简要用法示例

### TCP 服务端 + 客户端（cs_server）

```cpp
#include "network/tcp.h"
#include "network/cs.h"

using namespace xhqm::network;

// 服务端：监听 9000，回显大写
tcp_server server;
server.listen(9000);

cs_server<tcp_server, tcp_conn> cs(std::move(server), [](const std::string& req) {
    std::string resp = req;
    for (auto& c : resp) c = toupper((unsigned char)c);
    return resp;
});
cs.run();   // 阻塞监听循环

// 客户端
tcp_client client;
client.connect("127.0.0.1", 9000);

cs_client<tcp_client> cc(std::move(client));
std::string resp;
if (cc.request("hello", resp)) {
    // resp == "HELLO"
}
```

### 帧级收发 / UDP

```cpp
#include "network/tcp.h"
#include "network/udp.h"

tcp_client c;
if (c.connect_timeout("example.com", 9000, 2000)) {
    c.send("ping");
    std::string r; c.recv(r);
}

udp_socket u;
u.bind(0);                              // 临时端口
u.send_to("hi", "127.0.0.1", 9001);     // 定向
u.broadcast_to("who-is-there", 9002);   // 局域网广播
```

### 加密 / UUID

```cpp
#include "network/sha.h"
#include "network/aes.h"
#include "network/uuid.h"

using namespace xhqm::cipher;

std::string h = sha(sha_type::sha3_256)("hello");   // ⚠ 全零！见下「补充说明」；改用 sha2_256
std::string g = sha(sha_type::sha2_256)("hello");   // 十六进制摘要（已核标准向量）

aes a(aes::aes_256);
a.set_key({ /* 32 字节密钥 */ });
a.set_mode_cbc();
auto enc = a.encrypt({ /* 明文 */ });
auto dec = a.decrypt(enc);

auto id = uuid::creat_uuid_v4();
std::string s = uuid::to_text(id);   // "xxxxxxxx-xxxx-..."
```

### astral 记忆内核

```cpp
#include "network/astral/kernel.h"

xhqm::astral::kernel mem;
auto id = mem.create("note", "我的备注", xhqm::astral::json{{"text", "hello"}});

// 幂等建档：form + uuid 为键，data 是节点的一条 {time, form, json} 记录
mem.ensure("note", id, "我的备注",
    xhqm::astral::json{{"type", "insert"}, {"data", xhqm::astral::json{{"v", 2}}}});

mem.update_info("note", id, "改过的备注");
mem.update_data("note", id,
    xhqm::astral::json{{"type", "insert"}, {"data", xhqm::astral::json{{"v", 3}}}});
mem.set_connect(id, other_id);                 // 双向关联

auto found = mem.select_info({"改过的备注"});   // node_list：命中节点的最新一条 data
mem.save_json("/tmp/mem.json");

// 冷启动：从文件恢复（data 本体按需加载），标签语义层要重建 name→uuid 表
xhqm::astral::evoke tag;
tag.load_json("/tmp/mem.json");
tag.rebuild_names();                 // 不调它，重启后同名标签会被重复创建（evoke.h:47-49 的说明）
auto uid = tag.upsert("note", "我的备注",                    // 按 info 精确查重
    xhqm::astral::json{{"v", 4}}, {"关联标签"}, nullptr);     // links + 结果布尔（可为 nullptr）
```

记忆内核的真实行为由 `test/astral_test.cpp`（145 行）、`test/astral_behavior_test.cpp`（507 行）、
`test/astral_stress_test.cpp`（196 行，20 线程 × 10000 次并发读写）覆盖；三者都在 `test/CMakeLists.txt`
登记（`:208`/`:213`/`:218`），且**必须**先构建主库产出 `binary/xhqm.lib`（`test/README.md:182`）。

### nebula 节点（数据通道 + 查询）

```cpp
#include "network/nebula/zero.h"

// 自定义节点：实现纯虚 logic()；message（proxy/claim 回执）到达走 data_signal
class my_node : public xhqm::nebula::zero
{
public:
    explicit my_node(const xhqm::nebula::json& config)
        : xhqm::nebula::zero(config, 3, 9)          // m=3, n=9（c/a 按 hardware_allow 派生）
    {
    }
    virtual void logic(const xhqm::nebula::json&) override {}   // 自逻辑：life_event(make_logic({}))
    void on_message(const xhqm::nebula::json& event)            // proxy/claim 的回执（整个 message 事件）
    {
        const auto& payload = event["data"];                    // 载荷在 data 键
    }
    void on_care(const xhqm::nebula::json& data, const xhqm::nebula::uuid& from,
        const xhqm::nebula::uuid& channel)                      // 订阅推送到达
    {
        insert_data(channel, data);
    }
};

xhqm::nebula::json config = {
    { "path", "node.mem" }, { "m", 3 }, { "n", 9 }, { "c", 2 }, { "a", 18 },
    { "size", xhqm::npos }, { "time", 1000 }
};                                   // answer/listen 已无人读取：地址要自己注入
                                     // name/info 也要自己给（zero 构造不读它们，见 star）

my_node node(config);
node.name("我的节点");                                     // node 基类提供（nebula.h:57）
node.insert_addr(std::make_shared<xhqm::nebula::addr_ipv4>("0.0.0.0", 9000));        // TCP 定向
node.insert_addr(std::make_shared<xhqm::nebula::addr_ipv4>("0.0.0.0", 9000, 9100));  // + UDP 广播
node.care_hander.connect(&node, &my_node::on_care, xhqm::signal_type::signal_direct);
node.data_signal.connect(&node, &my_node::on_message, xhqm::signal_type::signal_direct);
node.start();                        // 事件池开始派发（life.h:33）
node.life();                         // 逐地址 opened() + 挂帧信号 + 起心跳线程（link.cpp:31-53）
                                     // 注意 insert_addr 只是入表（link.cpp 的 insert_addr），
                                     // 真正 opened() 发生在 life() 里——两者顺序不能反

// 注册一个工具（工具表键 = kit 的 uuid 文本；重复 uuid 拒绝）
xhqm::nebula::zero::kit echo;
echo.ket_uuid(xhqm::nebula::uuid::creat_uuid_v4())
    .ket_name("echo")
    .string_param("text", "要回显的内容")
    .ket_make([](const xhqm::nebula::json& in) {
        return xhqm::nebula::json{{"echo", in.value("text", std::string{})}};
    });
node.insert_tool(echo);              // zero.h:148-156：空 kit 返回 false，重复 uuid 返回 false

auto ch = node.create(xhqm::nebula::data::channel_kind::source, "温度通道");
node.insert_data(ch, xhqm::nebula::json{{"value", 36.5}});
auto now = node.select_message(ch);              // 最新一帧 {time, data}
auto his = node.select_history(ch);              // 落库历史 [{time, data}]（时间取内核记录戳）

xhqm::nebula::uuid owner = /* 通道拥有者 uuid */;
node.care(owner, ch);                            // 订阅：care 中继到拥有者，由它登记「我 + 该通道」
node.publish(data, ch);                          // 拥有者侧：扇出该通道数据给全部订阅者（返回成功份数）
```

### nebula 组件组装（star）

`star<...>` 把「一个节点 + 若干可插拔能力」拼起来：每个类型参数是一个 `type` 派生组件，构造时 `new`、析构时 `delete`，
`zero_info()` 会把每个组件的 `type_info()` 收进 `info()["list"]["types"]`。`type_name()` 是**纯虚**（`star.h:32`）——
不写就编不过，这是故意的。真实用法可对照 `build/star_contract_check.cpp`（本地探针，未受控）：

```cpp
#include "network/nebula/star.h"

class my_type : public xhqm::nebula::type
{
public:
    explicit my_type(xhqm::nebula::zero* hand) : xhqm::nebula::type(hand)
    {
        this->info(xhqm::nebula::json{{"kind", "demo"}, {"text", "组件自述"}});
    }
    virtual xhqm::nebula::text type_name() const override { return "my_type"; }
};

class my_star : public xhqm::nebula::star<my_type>
{
public:
    explicit my_star(const xhqm::nebula::json& config) : xhqm::nebula::star<my_type>(config) {}
    virtual xhqm::nebula::text star_name() const override { return "my_star"; }   // 也是纯虚
    virtual void logic(const xhqm::nebula::json&) override {}
};

xhqm::nebula::json config = {
    { "path", "" }, { "m", 1 }, { "n", 2 }, { "c", 2 }, { "a", 4 },
    { "size", xhqm::npos }, { "time", 1000 },
    { "name", "probe" }, { "info", xhqm::nebula::json{{"text", "探针节点"}} }
};                                   // star 会读 name/info（star.h:65-69），zero 不会

my_star node(config);
node.start();
node.logic_round();                  // 探针里是 life_event(make_logic(...))（make_* 是 protected）
auto zi = node.zero_info();          // zi["list"]["star"]=="my_star"、zi["list"]["types"][0]["type"]=="my_type"
                                     // 顶层**没有** "name" 键：名字走 node.name() 或 zi["list"]["name"]
```

---

## 补充说明

- **`sha3_*` 全族没有实现，输出恒为全零（本轮实测新发现）**：`sha_type` 枚举有 9 个值，但 `sha.cpp:522-525` 的四个 `_sha3_224/256/384/512` 都是一行 `{ return (*this); }`（连参数名都没有），`init()` 对四个 sha3 类型把 `h0..h7` 全置 0（`sha.cpp:140-149`），`preprocess` 的 sha3 分支直接 `break`——不做填充、不做置换（全仓 `grep -c "keccak" network/sha.cpp` = 0）。于是 `sha(sha_type::sha3_256)("abc")` 打印 **64 个 `0`**。实测 12 个算法的对照：md2/md4/md5、sha1、sha2-224/256/384/512 **八个全部与标准向量一致**，四个 sha3 **全部是等长全零**。危害最大的一处是 `license.cpp:767`/`:802` 拿 `sha3_256` 的输出当 AES-256 密钥——**授权串实际由全零密钥加密**，读写互通只是因为两端都走同一条坏路径。详见 `DEBUG.md` A1。
- **AES 模式**：~~头文件声明了 `CFB/OFB/CTR`，但当前 `aes.cpp` 仅实现 `ECB` 与 `CBC`，其余模式为占位~~ **已作废（2026-10-07 复核 r677）**：结论对、措辞错。**只有 ECB 与 CBC**（`aes.cpp:148`/`:176` 两个 `if (m_mode == mode::CBC)`，否则一律走 ECB），但 `CFB/OFB/CTR` 不是「占位实现」而是**不可达的死枚举**——`aes.h:26` 的 `enum class mode { ECB, CBC, CFB, OFB, CTR }` 是私有的，而公开设置器只有 `set_mode_ecb()`/`set_mode_cbc()` 两个（`aes.h:32-33`），没有 friend、没有任何路径能选中那三个值。DES 侧连 mode 枚举都没有，`des.h:37` 的注释「模式与填充 (仿照AES结构，但DES标准模式为ECB)」只是注释，实现是纯 ECB + PKCS#7。
- **缺 TLS / 安全传输**：`network/` 全模块是明文 TCP + UDP 广播，没有任何 TLS/SSL 代码——`grep -rni "tls\|ssl" network/*.h network/*.cpp network/astral network/nebula` 的命中**全部**是 `ioctlsocket` 里的子串（`network.h:247`、`:259`、`addr_ipv4.cpp:101`、`addr_ipv6.cpp:34`），照结果数容易误判成「有」。跨机部署时事件帧（含节点 info、通道数据）与 license 串都走明文链路。详见 `DEBUG.md` E3。
- **`ipv6path`**：~~`socket.h` 中的 IPv6 文本解析目前是未实现桩（返回 false）~~ **保留并收窄（2026-10-07 复核 r677）**：成立——`socket.cpp:90-94` 就是 `return false;`。但更该记的是「整条 socket 地址解析链路没有调用者」，详见上一节 `socket.h` 那行与 `DEBUG.md` 的 E1。
- **帧协议上限**：帧长上限在**三个不同位置各写了一次**，值不一致——`network.h:497` 的 `recv_frame` 用 `1u << 30`（1GB，硬编码）、`cs.h:37` 的 `frame_stream` 模板默认参 `frame_max_default = 1u << 30`（可被 `max_size` 成员运行时改写，0 = 不限）、串口侧 `addr_serial.h:164` 收紧到 `s_frame_max = 1u << 20`（1MB），IPv4 侧沿用 1GB（`addr_ipv4.cpp:701`）、IPv6 侧另加一条 4MB 入向缓冲闸（`addr_ipv6.h:63`）。**IPv4 侧没有那条闸**，见 `DEBUG.md` B4。
- **`kind` 词表未全覆盖**：`bluetooth / nearlink / quantum` 只有 `kind_text` / `text_kind` 文本互转，没有实现类；`addr_from` 造 `uuid`（回环）/ `ipv4` / `ipv6` / `serial` 四种。
- **信号槽池要显式安装**（r657/r658 起）：`xhqm::signals::pool()` 改为「未安装即抛 `xhqm::error`」（原来是惰性局部 static），装了才返回池子；安装入口是 `xhqm::signals::pool_life()`（内部走单例模板，池 2 核 / 最多 4 线程），宿主 `main` 首行调用即可。此改动会影响所有用异线程槽（`signal_queued`/`signal_auto` 跨线程）的模块，nebula 的 `addr_uuid` 克隆转发槽与 `uuid_broad` 就属此类。调用方范例见 stella：`make/star_*/main.cpp` 每个 main 首行都调了 `pool_life()`。
- **`software/signal.h` 与系统 `<signal.h>` 同名导致的遮蔽（2026-10-07 复核 r677 二次订正）**：~~r658 的 `software/signal.h` 会让 `process_base.cpp` 编译失败~~ **已作废（2026-10-07 复核 r677）**，现行说法「模板前置声明在 guard 之内、上文说的『第 1 行在 guard 之外』那一行不存在」也**不准确**：`software/signal.h:1-2` 是 `#ifndef XHQM_SIGNAL` / `#define`，`:9-12` 是 `#if !defined(_WIN32) && …` 包住 `#include_next <signal.h>` + `pattern/instance.h` 的小块，而 `:13` 的前置声明 `namespace xhqm { template <typename T> class instance; }` 就在 `:12` 的 `#endif` **之后**——外层 guard 之内、内层条件块之外，两句话都能自洽，所以原文那个括号是**在细节上把话说死了**。更要紧的是「这条怎么修」：实测**把 `:13` 那行挪到 `#include_next` 之前并不能修好**（见下）。
  真正的机理（本次实测、含最小复现）：本仓 CMake 把 `software/` 放进 `-I` 搜索路径（`CMakeLists.txt:95` 的 `include_directories("./software/")`，实际编译行 `-I…/xhqm-v2/./software`），于是 glibc 的 `<sys/wait.h>` 在自己的 `__BEGIN_DECLS`（= `extern "C" {`，`sys/wait.h:27`）里 `#include <signal.h>` 时，**尖括号查找直接命中项目自己的 `software/signal.h`**；该头被从 C linkage 里首次包含时 guard 尚未定义，于是 `:11` 的 `pattern/instance.h` → `head.h` → `<map>` 一整套 STL 头全落进 `extern "C"`。
  - 最小复现（Linux/GCC 11.4，与仓库真实 `-I` 组合一致）：`printf '#include <sys/wait.h>\nint main(){return 0;}\n' > w.cpp && g++ -std=c++17 -I<repo> -I<repo>/software -c w.cpp` → **5091 条 error**（首条 `functexcept.h:101: conflicting declaration of C function … __throw_ios_failure`，并附 `template with C linkage`；进入链为 `sys/wait.h:36 → software/signal.h:11 → pattern/instance.h:3 → head.h:33 → <map>`）。去掉 `-I<repo>/software` 或改成 `#include <signal.h>` 先于 `<sys/wait.h>` → **0 条 error**。
  - 实测不起作用的改法：把 `:13` 的前置声明挪到 `#include_next` 之前、把 `pattern/instance.h`/`head.h` 挪到前面、甚至删掉 `#include_next` 那一行、把「拉 STL 的 include」整体上移——**五种改法在真实 `-I` 组合下全是 5091 条 error**（新引入的自引用会在 C linkage 里重入）。生效的只有两条：① TU 里让系统 `<signal.h>` 先落地（在 `extern "C"` 之前就解析成系统头）；② 让 `software/` 不出现在 `-I` 里，或把该头改名/移出搜索路径。`CMakeLists.txt:57-76` 的 MSVC 分支正是②的思路（把 UCRT 头目录 `BEFORE` 前置），**非 MSVC 侧没有对应处置**。
  - 现状为什么没炸：仓库现有的 TU 都在 `extern "C"` 到来之前就把 `<signal.h>`/本头解析完了（`network/network.cpp:3` 的 `<csignal>`、`software/system/process/process_base.cpp` 的 `<cerrno>` 一侧）。也就是说**包含顺序是隐式接口**，谁写一句 `#include <sys/wait.h>` 谁就踩。
  - 跨模块：缺陷横跨 `CMakeLists.txt` + `software/signal.h` + 每个 TU 的 include 顺序，故详细条目收在仓库根 `DEBUG.md`（B1 / C1 / F4）与 `nebula/README.md` 的 A2；本条只记「网络侧会跟着挂」这个事实。
  - **教训**：凡写「某某导致编译失败」，必须同时写清**完整编译命令与验证版本**——原记录把「某次编译失败的现场」当成「稳定的代码事实」，第一次订正又把「某一行的位置」当成了根因，两次都少了最小复现。
- **A 世代已移除**：`notify / sse_server / sse_client / store / query / astral_store / server / sensor` 不再存在——推送由 `post` 承载、落库与历史查询由 `data` + `astral::kernel` 承载；`httplib.h` 保留但当前无引用。
- **当前问题清单不在这里**：本模块「现在还是坏的 / 还是没写的 / 还是对不上的」一律记在同目录 `DEBUG.md`（A 功能性缺陷 / B 资源与并发 / C 跨平台 / D 性能 / E 未实现桩 / F 设计取舍 / G 文档出入，每条带 `文件:行号`）；nebula 的「讨论过但决定不做」另见 `nebula/DEFERRED.md`（122 行），本文件只做总览与用法，不重抄。
