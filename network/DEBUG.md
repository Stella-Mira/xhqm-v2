# xhqm/network 模块 · 当前问题清单

`xhqm/network` 模块（模块路径 `network`，module id `network`）的**当前仍存在**的问题台账：传输（TCP/UDP/socket/帧协议）+ 加密编码（MD/SHA/AES/DES/Base64/UUID/license）+ astral 记忆内核 + nebula 分布式。只记「现在还是坏的 / 还是没写的 / 还是对不上的」，不记已修项、不做版本历史。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**：本仓历史台账**不搬家、不改名**。`network/nebula/DEFERRED.md`（nebula 讨论过但**决定不做/没排期**的事项）与 `network/nebula/README.md`（B 世代分层细节 + 其 A2 订正）、`network/astral/README.md`、`software/README.md` 保持原位，本文件只指过去、不复制内容。跨模块共有的问题（如 `software/signal.h` 槽池必须由宿主安装、根模块的文档漂移）已收在仓库根 `DEBUG.md`，此处只给指向、不重抄。子模块内已各有一份 README 的细节（分层、词表、组装）不进本文件；本文件只收纳**模块级可复现**的缺陷、桩与出入。
>
> **判据纪律**：凡写「编译不过 / 某 API 不存在 / 会崩」，必须附复现命令与验证版本。本仓出过「结论被推翻后只在别处订正」的前例（见 `network/README.md` 补充说明末条、`xhqm/README.md:240`），故每条都标了验证方式。验证环境：GCC 11.4.0 / Ubuntu 22.04 / glibc 2.35，工作副本 r677。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. **SHA-3 全族（224/256/384/512）没有实现，恒定输出全零**；而 license 正是拿 `sha3_256` 派生 AES 密钥
- 位置：`network/sha.cpp:522-525`（四个 `_sha3_*` 一行空实现）、`network/sha.cpp:112-149`（`init()` 把 sha3 的 `h0..h7` 全置 0）、`network/sha.cpp:101-106` 与 `:515-521`（`preprocess` 的 sha3 分支**直接 `break`**，即不做任何填充/置换）、`network/sha.cpp:152-201`（`exit()` 把 `h0..h7` 按大端拼成 28/32/48/64 字节十六进制）；调用方 `network/license.cpp:767`、`:802`
- 现状：四个入口都是 `xhqm::cipher::sha sha::_sha3_256(const std::vector<xhqm::byte>&) { return (*this); }`——**参数名都没有，输入被丢弃，返回初值状态**。`init()` 对四个 sha3 类型赋 0，`preprocess` 对它们不补位，全程没有 Keccak-f 置换（`grep -c "keccak\|0x800000000000808a" network/sha.cpp` → **0**）。于是输出是一个**与输入无关的常量串**：sha3_224 → 56 个 `0`、sha3_256 → 64 个、sha3_384 → 96 个、sha3_512 → 128 个 `0`。
- 证据（本机实测，GCC 11.4/Ubuntu 22.04，与 `test/network_test.cpp:14-16` 同一套标准向量写法）：

  | 算法 | 输入 | 实测 | 标准向量 |
  |---|---|---|---|
  | md5 | `"abc"` | `900150983cd24fb0d6963f7d28e17f72` | 同（**通过**） |
  | md4 | `"abc"` | `a448017aaf21d8525fc10ae87aa6729d` | 同（**通过**） |
  | md2 | `"abc"` | `da853b0d3f88d99b30283a69e6ded6bb` | 同（**通过**） |
  | sha1 | `"abc"` | `a9993e364706816aba3e25717850c26c9cd0d89d` | 同（**通过**） |
  | sha2-224 | `"abc"` | `23097d22…e36c9da7` | 同（**通过**） |
  | sha2-256 | `"abc"` | `ba7816bf…1f20015ad` | 同（**通过**） |
  | sha2-384 | `"abc"` | `cb00753f…34c825a7` | 同（**通过**） |
  | sha2-512 | `"abc"` | `ddaf35a1…fa54ca49f` | 同（**通过**） |
  | **sha3-224** | `"abc"` | `0000…0000`（56 个 0） | `e642824c3f8cf24a…73b46fdf` |
  | **sha3-256** | `"abc"` | `0000…0000`（64 个 0） | `3a985da74fe225b2…11431532` |
  | **sha3-384** | `"abc"` | `0000…0000`（96 个 0） | `ec01498288516fc9…28376d25` |
  | **sha3-512** | `"abc"` | `0000…0000`（128 个 0） | `b751850b1a57168a…4eec53f0` |

- 影响：**两件事**。(1) 任何用 SHA-3 做完整性/指纹/签名的调用方，看到的是一个「永远等于全零摘要」的常量——同长度不同内容的输入**摘要完全相同**，冲突检测形同不存在，而且**不报错、不告警**。(2) `license.cpp:767` / `:802` 用 `sha(sha3_256)` 的输出当 AES-256 密钥（`aes.set_key(std::vector<byte>(key.begin(), key.end()))`，key 恒为 64 个 `'0'` 字符）：**授权串实际是一把全零密钥的 AES-ECB/CBC 密文**，任何人拿 `sha3_256` 的固定输出就能解开。之所以读写还能互通，只是因为同一条坏路径用于两端。这是本模块最贵的一条。
- 复现/验证：`printf '#include "network/sha.h"\n#include <iostream>\nint main(){ std::cout << std::string(xhqm::cipher::sha(xhqm::cipher::sha_type::sha3_256)("abc")) << "\\n"; }\n' > s3.cpp && g++ -std=c++17 -I<repo> -o s3 s3.cpp network/sha.cpp && ./s3` → 打印 64 个 `0`（编译无警告、退出码 0）。**独立性验证**：把 `"abc"` 换成 `"完全不同的输入/xyz"`，两次输出**逐字节相同**（都是 64 个 `0`），同一次程序里 `sha2_256("abc")` 则给出正确的 `ba7816bf…1f20015ad`。
- 建议：要么补齐 Keccak-f[1600]（`_sha3_*`、`init`、`preprocess` 三处一起改），要么把四个枚举值与 `_sha3_*` 入口删掉/在 `init()` 里抛异常——**不要留「能编过、能跑、结果恒定」的中间态**；`test/network_test.cpp` 补上 SHA-2/SHA-3 的标准向量（现只测 md5/base64/uuid）。

### A2. `base64::decode` 把 `=` 当数据算进位数，且减填充数不做下界检查 → 静默错解 + 可能的 `resize` 下溢
- 位置：`network/base64.cpp:84-101`（`=` 分支仍执行 `chars_processed++`）、`:105-111`（`j < chars_processed - 1`）、`:118-131`（`result.resize(result.size() - padding_count)`）
- 现状：解码时把 `'='` 计入 `chars_processed`，于是 `chars_processed` 不再等于「真实符号数」，输出的字节数按它裁剪；末尾再按「`=` 个数」做一次**无下界检查**的 `resize(size - padding_count)`。`result.size()` 与 `padding_count` 都是无符号，`padding_count > result.size()` 时下溢成 `SIZE_MAX`。
- 证据：实测（`base64.cpp` 直接编入小程序）：

  | 输入 | 实测输出 | 应为 |
  |---|---|---|
  | `"===="` | size=1（未抛） | 非法输入，应失败/空 |
  | `"A==="` | size=1 | 非法输入 |
  | `"AA=A"` | size=2 | 非法输入 |

  `padding_count` 分支到 `result.size() - padding_count` 的那条路径由 `:119-125` 的循环与 `:128` 的 `if` 进入，`"===="` 走的就是它（`result.size()==3`、`padding_count==2`，恰好不溢出才没崩）。
- 影响：任何**不完整/被截断/被拼错**的 base64 都返回一段长度与内容都错的字节，调用方看不出来。`license` 层正是这个调用方：`license.cpp:771` 的 `read_license` 先把授权串 `base64::decode` 再 AES 解密再切分，输入被截断时会走到「解密出垃圾 → 切分不到 3 段 → 返回 false」而不是明确报错；被截断成 `padding_count > result.size()` 的形状时则可能直接抛 `std::length_error`/`std::bad_alloc` 冒到调用方。
- 复现/验证：见上表命令（`g++ -std=c++17 -I<repo> -o b64 b64.cpp network/base64.cpp && ./b64`）。
- 建议：`=` 只在尾部出现时参与裁剪、不参与移位；`resize` 前先 `if (padding_count > result.size()) return {};`。

### A3. `find` 的回程表用 `std::less<uuid>` 作键：整张表永远只装得下 1 条，L2 区域图发现会串台
- 位置：`network/nebula/find.h:77`（`std::map<nebula::uuid, nebula::find::sign> m_back_map;`，**漏了比较器**）；使用处 `network/nebula/find.cpp:311`、`:320`、`:337`、`:349`、`:371`、`:379`、`:221-228`
- 现状：`nebula::uuid` 的 `operator==` 在 `uuid.h:60-68` 被实现为 `uuid_equal(*this, value)`，而 `cipher::uuid::uuid_equal`（`uuid.cpp:11-18`）判的是「两者都是全零」——**任意两个非零 uuid 互相相等**。于是 `std::less<uuid>` 认为所有 uuid 等价，`emplace`/`operator[]` 全部折叠到同一个键。实测 `std::map<uuid,int>` 插入 5 个随机 v4 uuid 后 `size() == 1`；换成 `uuid_less` 才是 5。
- 证据：全仓只有这一处 uuid 键容器漏比较器（其余 17 处都显式给了 `uuid_less`，见 `find.cpp:121,128,178`、`post.h:34,39,40`、`addr_b.h:43`、`data.h:29`、四张 addr 注册表）。实测命令见下。
- 影响：`exec_layer` 的「事件 id 去重」（`:311-328`）在第二条并发询问到达时必然命中同一格 → **直接 `return`**，那条询问永远不转发也不再回图；`exec_graph`（`:371-383`）读到的 `from`/`size` 属于**别人**的询问，会把区域图回给错误的上游、把子计数对错对象减 1。后果是 L2 区域图在多询问并发时合并出错误视图、`short_path` 给出的下一跳不可靠。这正是「算错」类里最贵的一格：单询问链路能过，一并发就错。
- 复现/验证：`g++ -std=c++17 -I<repo> -o lt lt.cpp network/uuid.cpp network/md5.cpp`，其中 `lt.cpp` 用 `std::map<nebula::uuid,int>` 与 `std::map<nebula::uuid,int,uuid_less>` 各插 5 个 `creat_uuid_v4()`；实测输出 `std::less map size = 1`、`uuid_less map size = 5`。
- 建议：`find.h:77` 补上 `, nebula::uuid_less`（一行）。

### A4. `aes::pkcs7_unpad` 接受填充值 `0`，会把合法明文整段删成空
- 位置：`network/aes.cpp:479-480`（`byte padding_len = data.back(); if (padding_len > 16) return data;`）
- 现状：只挡了 `> 16`。`padding_len == 0` 时进入 `:483` 的循环 `for (i = data.size() - 0; i < data.size(); ++i)`——**空循环**，随即 `:489` 用 `data.begin() + data.size() - 0` 构造「裁掉 0 字节」的结果。也就是说 `0` 只被当成「不裁」；但按 PKCS#7 语义 `0` 是非法填充值，出现它说明密文/密钥不对。此时函数**既不报错也不裁**，把未解密的垃圾原样交给上层。
- 证据：`:480` 的判据只有 `> 16`，无 `== 0`；`:483`、`:489` 两处都是 `size() - padding_len`。
- 影响：`license::read_license`（`license.cpp:771-780`）拿解密结果去 `split_string(text, ' ')` 并比对 `license_str[2] == "license"`——垃圾输入靠后续字符串比对兜住，所以**不会崩**，但错误定位会指向「授权串格式不对」而不是「密钥不对」。真正危险的是把它当通用加解密接口使用的调用方：密钥错 ≠ 报错。
- 复现/验证：本机未构造（需先有能产出 `0x00` 尾字节的密文）；`grep -n "padding_len" network/aes.cpp` 可静态确认判据只有 `> 16`。
- 建议：`padding_len` 取 `1..16` 之外一律视为无效（返回原数据或抛错，二选一并写进头注释）。

### A5. `network.cpp` 的 Windows 分支把 `WSACleanup()` 的返回值取反：清完栈仍自称 good
- 位置：`network/network.cpp:32`（`good = !(WSACleanup() == 0);`）
- 现状：POSIX 分支（`:47-53`，`:51` 是 `good = false;`）写的是 `good = false;`（正确）；Windows 分支写的是 `good = !(WSACleanup() == 0)`，即「清理**成功**（返回 0）→ `good = false`」的实际取反——`WSACleanup()` 返回 0 时表达式为假……但 `WSACleanup()` 失败（返回 `SOCKET_ERROR`）时表达式为真，`good_network()` 反而返回 `true`。语义与 POSIX 侧相反。
- 证据：`sed -n '24,34p' network/network.cpp`；`network.cpp:9-12` 的 `good` 是文件级 `bool`，`good_network()` 直接返回它。
- 影响：`init/exit` 引用计数是成对的，`init_network` 每次 `++init`（`tcp_server::listen`、`tcp_client::connect*`、`udp_socket::bind` 各调一次，见 `tcp.cpp:44,76,87`、`udp.cpp:11`），而**这些类从不调 `exit_network()`**（全仓 `grep -rn exit_network` 只有声明与定义，无调用）。所以 Windows 上这个取反目前只污染 `good_network()` 的返回值（表现为「关过一次之后仍自称已初始化」），不影响 WinSock 生命周期；**只有 Windows 能观察到**，本机（Linux/GCC）不属于该编译分支。
- 复现/验证：未复现（本机无 MSVC/Windows）；静态证据 `sed -n '32p' network/network.cpp`（`good = !(WSACleanup() == 0);`）与 `sed -n '51p'`（`good = false;`）对照即可。
- 建议：Windows 分支改成 `good = false;`，与 POSIX 侧对齐。

### A6. `socket::ipv4path` 用 `std::stoi` 且不捕异常：非法地址文本会抛到调用方外面
- 位置：`network/socket.cpp:79`、`:83-86`（`post = std::stoi(list[1]);`、`host.value(n) = std::stoi(list[n]);`；元素类型是 `unsigned char`）
- 现状：解析 `"a.b.c.d:port"` 时直接 `std::stoi`，超范围/非数字一律抛 `std::invalid_argument`/`std::out_of_range`；`"h:99999"` 的端口、`"1.2.3.999"` 的八位组都会抛。函数声明是 `bool` 返回值（`socket.h:51`），**调用方会以为失败是返回 false**。
- 证据：`socket.cpp:74-88` 全文无 `try`；`:79` 与 `:83-86` 四/五处 `std::stoi`；返回类型 `bool`。
- 影响：这条路径当前**没有调用者**（`grep -rn "ipv4path\|ipv6path"` 全仓只命中声明与定义），所以今天不炸；一旦上层按 `bool` 语义接上去，任何畸形输入都会变成未捕获异常。属「没接上的地雷」，归 A 组是因为它一旦接上就是崩溃点。
- 复现/验证：`grep -rn "ipv4path" --include=*.h --include=*.cpp .` → 只有 `socket.h:51`、`socket.cpp:74`。
- 建议：换 `std::strtoul` 并检查范围，或把 `stoi` 包进 `try` 后返回 false。

### A7. `tcp_server::accept()` 返回裸 `new`，唯一调用方 `cs_server::run()` 在异常路径上会漏
- 位置：`network/tcp.cpp:57`（`tcp_conn* conn = new tcp_conn();`）、`network/cs.h:97-104`（`run` 里 `std::thread(&cs_server::serve, this, session).detach();`）
- 现状：`accept()` 的所有权是「调用方 delete」（`cs.h:15` 的会话 concept 也这么写）。`cs_server::run`（`cs.h:93-105`）拿到 session 后立刻 `detach` 一个线程交给 `serve`；`serve`（`cs.h:108-121`）正常路径在末尾 `delete session`，但 `std::thread` 构造本身抛（资源不足）时指针无人接管。`run()` 自身是 `while (true)` 且**不看返回值终止条件**，`accept()` 返回 nullptr 时只 `sleep 10ms` 继续（`network/cs.h:98-103`），所以 `tcp_server::recv_timeout` 那个「让 accept 周期返回以便退出」的设计（`tcp.h:59-61` 注释）在 `cs_server` 路径上完全无效——**没有停机开关**。
- 证据：`cs.h:93-105` 无任何终止条件与 `catch`；`tcp.h:58` 的 `accept()` 返回 `tcp_conn*`；`test/cs_test.cpp:22` 的注释自认「阻塞（测试结束时进程退出）」。
- 影响：想用 `cs_server` 的调用方拿不到优雅关停（只能杀进程）；`run()` 里 `m_server.accept()` 在 `session_type` 不是 `tcp_conn` 时也可能返回别的失败形态。示例代码（`README.md` 的 TCP 例子）直接 `cs.run()` 就是**永久阻塞**。
- 复现/验证：`grep -n "while (true)" network/cs.h` → `:95`；`test/cs_test.cpp:27-29` 用 detached 线程 + 进程退出收场，可佐证没有停机路径。
- 建议：给 `cs_server` 加运行标志 + `stop()`（或接受 `std::function<bool()> 继续谓词`），并把 `session` 收进 `std::unique_ptr`。

### A8. license 串靠「空字段被丢弃」的分隔符解析：任一字段为空即整体错位，且编解码两侧的派生 key 来源不同
- 位置：`network/license.cpp:726-748`（`server_to_base64`，7 段 + 字面量 `"server"`）、`:755-763`（`client_to_base64`，4 段 + `"client"`）、`:703-722`（`base64_to_server` 要求 `size() == 8`）、`:739-753`（`base64_to_client` 要求 `size() == 5`）、`:786`（`read_license` 的 key 来源 `license::get_device()`）、`:803`（`save_license` 的 key 来源 `client.device`）；解析器 `software/system/codesys.cpp:104-112`
- 现状：两处编解码都靠 `xhqm::codesys::split_string(text, ' ')` 切分，而它的实现是 `while (std::getline(ss, item, tag)) if (!item.empty()) elems.push_back(item);`——**空字段被静默丢掉**。于是 `publish`/`company`（server 侧）或 `remark`（client 侧）为空时，写侧会写出「两个连续空格」，读侧少一段、后面所有段左移一位，`size() == 8` / `== 5` 的判据不再成立 → `base64_to_server`/`base64_to_client` 返回半填或全空的结构体，`read_license` 最终返回 false。`client.remark` 在头注释里是「被授权备注」（天然可为空），所以这不是理论情况。
- 证据：`sed -n '104,112p' software/system/codesys.cpp`；`grep -n "split_string" network/license.cpp` → `:705`、`:741`、`:775`（三处解析）；判据 `size() == 8`（`:707`）、`size() == 5`（`:743`）。另：`read_license` 只校验 `client.idcard` 非空，**不校验** `server.license`（`license.h:13` 称其为「魔数」），魔数实际未被使用。
- 影响：`save_license`（`:788-805`）在**同一台机器**上写出的授权串，遇到空可选字段时读不回来；`good::operator()(...)`（`:820-866`）会判为无效，客户侧表现为「刚发的授权用不了」。另外读侧的 key 用 `get_device()`、写侧用 `client.device`，当前只因紧接着的 `client_data.device != get_device()` 检查才「碰巧对得上」——两个来源一旦不一致，解密出的是垃圾而不是报错（配合 A4 更隐蔽）。
- 复现/验证：本机未跑（需构造授权串与 `get_device()` 环境）；静态证据＝上表行号 + `split_string` 的丢空实现。对照可行的验证：把 `remark` 置空走一遍 `save_license` → `read_license`，预期 `read_license` 返回 false。
- 建议：改用「不可出现在 base64 字母表里的分隔符 + 不丢空字段」的切分，并把两处 key 来源统一成同一个表达式。

---

## B. 资源与并发

### B1. `find` 的两张表只进不出：`m_back_map` 超时项靠 `size=1` 强刷、`m_relay_map` 只在心跳里清
- 位置：`network/nebula/find.h:77-79`；清理处 `network/nebula/find.cpp:221-235`
- 现状：`m_back_map` 的条目在 `exec_graph` 清零后 `erase`（`:379`），但 A2 的比较器缺陷让 `find`/`operator[]` 命中错误格子，**该 erase 常常擦不到真条目**；`exec_dath` 的超时兜底（`:228`）只把 `size` 置 1 并补发一条 `graph`，并不删条目。`m_relay_map` 只在 `exec_dath` 里按 `now` 过期清理（`:231-235`），心跳间隔由 `link::heart_time` 决定（`m_t` 默认 1000ms）。
- 证据：`grep -n "m_back_map\|m_relay_map\|erase" network/nebula/find.cpp` → `m_back_map` 的 erase 只有 `:379` 一处；`m_relay_map` 的 erase 只有 `:233` 一处。
- 影响：进程长时间运行时这两张表随「见过的询问 id / 转发过的事件 id」单调增长；单机 24×7 的节点上这是稳定泄漏。清理窗口依赖心跳周期，而心跳是可配的（`link.h:20` `m_t = 1000`）。
- 复现/验证：未复现（需长跑）；静态计数见上。
- 建议：给 `m_back_map` 的兜底路径加真正的 `erase`（并随 A2 一起修），`m_relay_map` 保持现有心跳清理即可。

### B2. `data` 的析构不做同步：`~data()` 落盘时可能与其他线程的 `insert_data` 撞车
- 位置：`network/nebula/data.h:45-48`（析构里 `m_kernel.save_json(m_path)`）；写方 `network/nebula/data.h:68` / `network/nebula/data.cpp:121-128`、`:142`、`:158`
- 现状：全部写路径都在 `m_data_mutex` 里调 `m_kernel`（`data.cpp:21,36,70,87,103,121,142,158,189` 都是先取 `m_data_mutex`，再进 `m_kernel.update_data`/`ensure`/`set_connect`）。析构是唯一**不取锁**就直接 `save_json` 的入口，而 `kernel::m_mutex` 只保护内核自身、保护不了「谁在析构 `data`」。
- 证据：`grep -n "m_data_mutex\|m_kernel\." network/nebula/data.cpp` 可见所有 `m_kernel.*` 调用都在 `lk(m_data_mutex)` 的作用域内，除了析构里的 `data.h:47`。
- 影响：节点在还有网络帧在途时被销毁（`star`/`zero` 的关停顺序、`life` 的心跳线程尚未 join 完）会与落盘并发读写同一份 `nlohmann::json` 与同一张 `m_node_data`，症状是崩溃或写出截断的 json。触发条件是「销毁与收帧重叠」，不常发生但不可排除。
- 复现/验证：未复现；静态证据见上（`data.h:45-48` 无 `lock_guard`）。
- 建议：关停顺序里先停地址/事件池再析构 `data`（`link::~link` 已经 `dath()+stop()`，把 `data` 的落盘挪到显式 `save()` 里更稳）。

### B3. `mmap_index::open` 不校验头部偏移，越界的索引文件会直接越界读（不是「解析失败」）
- 位置：`network/astral/mmap_index.cpp:49-62`（`data_idx_/info_idx_/pool_/blob_` 全按文件里的裸偏移做指针算术）、`:85-88`（`info.assign(pool_ + rec->info_off, rec->info_size)`、`std::vector(cb)(d, d + rec->data_size)`）
- 现状：`open` 只校验 `size >= 96`、magic `"ASTRAL"`、version `== 8`，随后 `info_idx_ = base + info_index_off` 等**全部无上界检查**；`get`/`find_info` 再按记录里的 `*_off/*_size` 取值，同样无检查。文件格式是 `save_mmap`（`kernel.cpp:752-847`）自产的：96 字节头 + `count*64` 数据索引 + `info_recs*32` 二级索引 + pool + blob。
- 证据：`sed -n '44,63p' network/astral/mmap_index.cpp`（无 `if (... >= map_.size())`）；对照 `save_mmap` 的布局计算 `kernel.cpp:820-836`。
- 影响：索引文件被截断、被同目录其它文件覆盖、或格式代际不合（version 只查 `== 8`，不看 flags/保留位）时，读端拿到的不是「打开失败」而是**进程崩溃**。这里读的是本地文件、不是网络输入，所以是「自伤型」而非「攻击面」，但排查成本高（表现为随机段错误）。
- 复现/验证：未复现（需构造畸形索引文件，本任务禁写文件）；静态证据见上。
- 建议：`open` 里逐个校验 `info_index_off / pool_off / blob_off ≤ map_.size()`，`get`/`find_info` 里校验 `off + size ≤ map_.size()`。

### B4. `addr_ipv4` 没有入向缓冲上限（`addr_ipv6` 有）
- 位置：`network/nebula/addr/addr_ipv4.cpp:677-703`（`c->inbox.append(...)`，只受 1GB 帧长上限约束）；对照 `network/nebula/addr/addr_ipv6.h:63`（`s_read_cap = 4 << 20`）与 `network/nebula/addr/addr_ipv6.cpp:568`（`if (c->inbuf.size() > s_read_cap) return false;`）
- 现状：IPv4 reactor 每轮最多读 4×16KB=64KB（`:678-679` 的注释与循环），解析出的帧长上限是 `1u << 30`（`:701`）；IPv6 侧另加了一条「单连接入向缓冲 > 4MB 直接收连接」的灌水闸。两者对「对端只发长度头不发负载」的行为不同：v4 最多攒到 1GB 才丢连接，v6 攒到 4MB 就丢。
- 证据：`grep -n "s_read_cap" network/nebula/addr/addr_ipv6.cpp` → `:568`（唯一命中）；`grep -n "read_cap" network/nebula/addr/addr_ipv4.cpp` → 0 命中。
- 影响：恶意/故障对端可以在 v4 端点上按 64KB/轮 的速度把进程内存顶到 1GB 才被断开；同机多节点场景下是真实可发生的 OOM。触发条件＝对端（或被攻陷的对端）持续发不完帧的头。
- 复现/验证：未复现；静态证据见上（两侧同一职责的两份实现不对称）。
- 建议：把 `s_read_cap` 那个判据复制到 `addr_ipv4`（并给 `addr_serial` 也补一条）。

### B5. `addr_uuid::opened()` 恒返回 `true`：赋值当返回值
- 位置：`network/nebula/addr/addr_uuid.cpp:52`（`return (m_opened = true);`）
- 现状：这是**赋值表达式**，无论此前状态如何都返回 `true`。横向对照：`network/nebula/addr/addr_ipv4.cpp:233`、`network/nebula/addr/addr_ipv6.cpp:296`、`network/nebula/addr/addr_serial.cpp:604` 都是显式 `m_opened = true;` 后再各自返回成功，语义是「置运行标志」。
- 证据：`grep -rn "m_opened" network/nebula/addr/` → `network/nebula/addr/addr_uuid.cpp:52` 是唯一的「赋值 + 返回」复合写法；`network/nebula/addr/addr_b.h:61` 的 `is_open()` 读的就是这个标志。
- 影响：`addr_uuid` 的 `opened()` 语义（幂等、可重复调）被破坏——第二次调用也返回「新开成功」。当前调用方（`link::life` 的 `addr->opened()`）不看返回值，所以不崩；但任何用 `is_open()`/`opened()` 判断「是否已启动」的上层都会被误导。属「会算错」但后果轻，放在 B 组是因为它污染的是运行状态标志。
- 复现/验证：`sed -n '50,53p' network/nebula/addr/addr_uuid.cpp`。
- 建议：拆成两行（`m_opened = true; return true;`）。

### B6. `kit::operator()` 与 `cs_server::serve` 用 `catch (...)` 吞掉一切，故障只剩「空对象」
- 位置：`network/nebula/zero.h:54-62`（`try { return make(input); } catch (...) {} return json::object();`）、`network/cs.h:113-119`（`catch (...) { break; }`）
- 现状：工具调用异常被折成**空 json 对象**——与「工具正常返回空对象」不可区分；`cs_server` 的 handler 异常被折成「断开这条连接」（`cs.h:118`），没有日志、没有错误帧。
- 证据：两处都是 `catch (...)` 无 `catch (const std::exception&)`、无记录。
- 影响：远端（可能不可信）的事件让工具内部抛错时，调用方看到的是「成功但结果为空」，排障时没有任何线索；`network/nebula/zero.cpp:106-115` 的 `exec_proxy` 又包了一层 try，异常信息彻底丢失。触发条件＝工具实现里有任何会抛的路径（nlohmann 的 `type_error`、`std::stoi`、分配失败）。
- 复现/验证：静态可见（`network/nebula/zero.h:56-61`、`network/cs.h:118`）；未跑触发用例。
- 建议：至少把异常类型/消息塞进回执（如 `{"error": e.what()}`）或落一条 stderr，别静默。

---

## C. 跨平台

### C1. Windows 分支的 `exit_network` 语义与 POSIX 相反（同 A4，此条只记跨平台口径差异）
- 位置：`network/network.cpp:32`（Windows）、`:47-53`（POSIX）
- 现状：见 A5。此处只强调**两侧不对称**这一事实本身：同一函数、同一引用计数，两侧对 `good` 的终值判断不同，而本仓没有 Windows 侧的回归测试（`test/` 全部是 Linux/MSVC 二选一的 `if(MSVC)` 分支，见 `test/CMakeLists.txt`）。
- 证据：`sed -n '24,53p' network/network.cpp`。
- 影响：只有 Windows 能观察到；本机（GCC/Linux）不编译该分支。
- 复现/验证：未复现（本机无 MSVC）。
- 建议：与 A5 合并处理，改完在 MSVC 上过一遍链接。

### C2. `license::disk_serial()`（Linux）硬编码 `/sys/block/sda/...`，非 sda 的机器上恒为空
- 位置：`network/license.cpp:429-441`（`std::ifstream serial_file("/sys/block/sda/device/serial");`）
- 现状：只认 `sda`。NVMe（`/dev/nvme0n1`）、`vda`（KVM）、`mmcblk*`、多盘机器的 `/dev/sdb` 全部返回空串；而 `get_device()`（`:689-701`）在 `system_serial()` 判定为「已知占位符」时才把四项串起来，`disk_serial()` 为空只是少一段指纹——不会崩，但**同一台机器重装后可能算出同一个 idcard**（指纹变弱）。
- 证据：`grep -n "sda" network/license.cpp` → `:429`（函数）、`:434`（`sda` 字面量）两处命中；`_is_know` 的占位符表在 `:95-105`。
- 影响：依赖 `get_device()`（`network/license.cpp:689`）做授权绑定的部署在非 sda 机器上指纹偏弱；属「算得不够准」而非「算错」。
- 复现/验证：`cat /sys/block/sda/device/serial`（本机若为 NVMe 则不存在）；`grep -n "sda" network/license.cpp`。
- 建议：用 `std::filesystem` 枚举 `/sys/block/*` 取第一个可读的 `device/serial`。

### C3. IPv6 组播在「每个接口一份」的域里会重复投递，`/proc/cpuinfo` 的 `serial` 行也非全平台存在
- 位置：`network/nebula/addr/addr_ipv6.h:64`（`s_group = "ff02::1"`）、`network/nebula/addr/addr_ipv6.h:80`（`m_ifaces` 每个接口一份）、`network/license.cpp:408-427`（`cpu_serial()` 扫 `/proc/cpuinfo` 的 `"serial"` 行）
- 现状：IPv6 侧按接口逐个加入 `ff02::1`，同一台机上多节点会收到 N 份重复广播（`network/nebula/DEFERRED.md` §1 的表格已把这条列为「组播重复投递」的既定差异）；`cpu_serial()` 只认 `/proc/cpuinfo` 里含 `serial` 的行（ARM 平台常见，x86 通常没有此字段）→ x86 上恒为空。
- 证据：`grep -n "s_group\|m_ifaces" network/nebula/addr/addr_ipv6.h`；`sed -n '408,427p' network/license.cpp`。
- 影响：IPv6 发现侧的重复帧要靠上层容忍（`post`/`find` 都有 id 去重，除了 A1 那条坏掉的表）；指纹侧只是少一段。
- 复现/验证：`grep -n "serial" /proc/cpuinfo`（本机 x86 无命中即证）。
- 建议：重复投递已在 `DEFERRED.md` 有处置方向，不在本文件重复；指纹侧可回落到 `board_serial()`/`system_serial()`。

---

## D. 性能

### D1. `publish` 对每个订阅者重建一份 `const nebula::json&` 参数（`create_message` 链路同构）
- 位置：`network/nebula/post.cpp:147-163`（`publish` 循环体 `send_goal(subscriber, make_post("push", subscriber, channel, data))`），`network/nebula/zero.cpp:156-174`（`create_message` 每次 `make_message` 都重建整份事件）
- 现状：扇出 N 个订阅者就构造 N 份信封（`nlohmann::json` 深拷贝一次 `data`）。`life_event`（`network/nebula/life.cpp:14-23`）再按值捕获整份事件进 lambda 并 `submit` 到线程池——每帧至少两次整份 json 拷贝 + 一次堆分配。
- 证据：`grep -n "post::publish" network/nebula/post.cpp` → `:147`；`sed -n '14,23p' network/nebula/life.cpp`。
- 影响：通道扇出大、帧大的场景（传感器高频上报）成本与订阅者数线性放大；LAN 级规模下可接受，但这是热路径上最容易省的一块。
- 复现/验证：未做测量（本任务禁建基准文件）；静态可见拷贝点。
- 建议：扇出时共享一份 `shared_ptr<const json>`，或把 `data` 提到循环外只构一次信封骨架。

### D2. `exec_proxy` 每个工具调用都深拷贝一份 `kit`（含 `std::function` 与 schema）
- 位置：`network/nebula/zero.cpp:99-105`（`zero::kit tool;` + `tool = iter->second;`）
- 现状：为避免持锁调用工具，先把 `kit` **按值拷出**再在锁外 `tool(argv)`；锁只覆盖拷贝本身。`kit` 继承 `nebula::node`（含 uuid/name/info 三份 json）+ 一个 `std::function` + 一份 schema json，每次都整份复制。
- 证据：`sed -n '99,115p' network/nebula/zero.cpp`。
- 影响：代理调用频率高的节点上，每次都是几份 json 的分配/释放；这是为「锁外执行」付的明确代价（见 F 组关于锁的取舍）。
- 复现/验证：静态可见。
- 建议：工具表存 `std::shared_ptr<const kit>`，锁外只持句柄。

### D3. 诊断接口在持锁状态下做全表扫描并拷贝
- 位置：`network/nebula/addr/addr_ipv4.cpp:216`、`:224`（`conn_count()`/`conn_total()` 遍历进程级连接表）、`:932`（`for (auto& kv : ipv4_registry) list.push_back(kv.second)`）
- 现状：`conn_count()` 每次调用在持有连接表锁的情况下遍历整表并填 `std::set`；`network/nebula/addr/addr_ipv6.cpp:200`、`network/nebula/addr/addr_serial.cpp:543,554` 同构。
- 证据：`grep -n "ipv4_conns\|ipv4_registry" network/nebula/addr/addr_ipv4.cpp` → `:216,:224,:932` 三处在循环内直接读静态表。
- 影响：连接数（LAN 级，几十到几百）下可忽略；但这是「诊断用」接口，长期挂在监控循环里就会与 reactor 争锁。
- 复现/验证：未测量。
- 建议：明确标注为「诊断用、勿入热路径」，或改成只在被调用时快照计数（atomic 计数）。

---

## E. 未实现 / 占位桩

### E1. `socket::ipv6path` 是 `return false` 的桩，`ipv4path` 也没接上
- 位置：`network/socket.cpp:90-94`（`bool socket::ipv6path(...) { return false; }`）、`:74-88`（`ipv4path`）
- 现状：IPv6 文本解析完全没写（编译通过、链接得到，只是永不成功）；`ipv4path` 写完了但**全仓无调用者**，`socket` 类本身也无实例化点（`grep -rn "ipv6path\|ipv4path"` 只命中 `socket.h:51-52` 与 `socket.cpp:74,90`）。
- 证据：`grep -rn "ipv4path\|ipv6path\|xhqm::network::socket\b" --include=*.h --include=*.cpp .` → 只有该头/实现自身。
- 影响：按 README 的目录表以为「socket 地址解析辅助」可用的人会踩空；实际地址解析全走 `network.h:122` 的 `resolve_addr`（`getaddrinfo`），与 `socket.h` 无关。这条是**编译能过、只是没写完/没接上**，明确标注。
- 复现/验证：`grep -rn "ipv6path" .` → 三处（声明/定义/无调用）。
- 建议：要么补 IPv6 分支（`inet_pton`），要么在 `socket.h` 头部注明「未使用，勿依赖」。

### E2. AES 只到 ECB/CBC；`CFB/OFB/CTR` 是不可达的枚举成员，DES 只有 ECB
- 位置：`network/aes.h:26`（`enum class mode { ECB, CBC, CFB, OFB, CTR }`）、`network/aes.h:32-33`（**只有** `set_mode_ecb()` / `set_mode_cbc()` 两个公开设置器）、`network/aes.cpp:148`、`network/aes.cpp:176`（`if (m_mode == mode::CBC)`，否则一律按 ECB 走）；`network/des.h`（无任何 mode 成员与设置器）
- 现状：`CFB/OFB/CTR` 三个枚举值**没有任何设置入口**（私有 `m_mode`、无 friend），因此当前是死枚举；`encrypt`/`decrypt` 对除 CBC 外的任何值都按 ECB 处理。DES 侧连 mode 枚举都没有，`network/des.h:37` 的注释「模式与填充 (仿照AES结构，但DES标准模式为ECB)」只是注释，实现是纯 ECB + PKCS#7。
- 证据：`grep -n "mode" network/aes.cpp` → 只有 `:148`、`:176` 两处判断与 `:194-248` 的 CBC 实现；`grep -n "mode\|CFB\|OFB\|CTR" network/des.cpp network/des.h` → 0 命中。
- 影响：按头文件枚举名以为能选 CFB/OFB/CTR 的调用方拿到的**静默是 ECB**（ECB 泄露明文模式，安全性更差而调用方毫无察觉）。属「没写完」，明确标注。
- 复现/验证：`grep -n "set_mode" network/aes.h` → 只有 `:32-33` 两个。
- 建议：删掉三个枚举值，或补实现并加设置器；二选一，别留中间态。

### E3. 全模块没有 TLS / 安全传输
- 位置：`network/network.h`、`network/tcp.h`、`network/udp.h`、`network/nebula/addr/addr_ipv4.cpp`（明文 TCP + UDP 广播）
- 现状：所有传输都是明文 socket；`grep -rni "tls\|ssl\|openssl" network/*.h network/*.cpp network/astral network/nebula` 的命中**全部是 `ioctlsocket` 里的 "sSL" 子串**（`network.h:247,259`、`addr_ipv4.cpp:101`、`addr_ipv6.cpp:34`），没有任何 TLS 库或握手代码。收编的 `httplib.h` 支持 TLS，但当前无引用（见 README 的依赖段）。
- 证据：上述 grep；`network/` 下无第三方位库依赖（`ls network/` 只有 `httplib.h`、`json.hpp` 两份收编头）。
- 影响：跨机部署时事件帧（含节点 info、通道数据）与 license 串全在明文链路上；license 的 AES 只护住了授权串本体，不护传输。文档必须写明，别让部署方以为「有 AES 就等于有安全传输」。
- 复现/验证：`grep -rnic "tls\|openssl" network/` 逐文件为 0。
- 建议：接入 TLS 前不要在同域多播/广播链路上传敏感载荷；README 里已按「缺 TLS」记。

### E4. 无基准（benchmark）设施；`network/` 目录内也没有测试文件
- 位置：模块内 `network/`（无任何 `*_test.cpp`）；测试全在 `test/`（独立 CMake 工程，根 `CMakeLists.txt` 不 `add_subdirectory`，见 `test/CMakeLists.txt:153` 的注释）
- 现状：`test/network_test.cpp`（53 行，md5/base64/uuid）、`test/cs_test.cpp`（42 行，TCP loopback echo）、`test/astral_test.cpp`（145）、`test/astral_behavior_test.cpp`（507）、`test/astral_stress_test.cpp`（196）覆盖算法、C/S、记忆内核，且都已在 `test/CMakeLists.txt:145`、`:208`、`:213`、`:218`、`:223` 登记；**没有**任何 benchmark 目标（`grep -rn "benchmark" CMakeLists.txt test/CMakeLists.txt` → 0 命中，`test/` 无 `add_test`/`enable_testing`）。
- 证据：`ls test/ | grep -i "bench"` → 空；`grep -n "add_test\|enable_testing" CMakeLists.txt test/CMakeLists.txt` → 0 命中。
- 影响：D 组三条性能问题与 A6 的「无停机开关」都容易改坏而无人发现；`addr_ipv4` 的 s_outbox 从 64 调到 1024 这类回归只能靠手写探针（仓里有 `build/nebula_b_check.cpp`、`build/star_contract_check.cpp` 两份**未受控**的探针源）。
- 复现/验证：上述 grep；`test/sport`、`test/view`、`test/nebula_api` 是各自独立的工程。
- 建议：把 `build/*_check.cpp` 收进 `test/` 并加进 `test/CMakeLists.txt`，先把「能跑」固定下来，基准再谈。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. `addr_uuid` 的 `send_frame` 不看 `is_open()`
- 位置：`network/nebula/addr/addr_uuid.cpp:65-71`（含注释「不按 m_opened 拦截」）
- 现状：克隆句柄（`addr_from` 造、被 `link` 挂进邻居表的那种）本来就不会调 `opened()`；它的 `frame_signal` 在构造时已连到实体端点的转发槽（`:32`），一拦就丢掉邻居方向的全部帧。
- 理由：克隆句柄的「可发」是路由契约的一部分，「未 opened」≠「不可用」。代价：`is_open()` 不能再当「发送前合法性判据」用（配合 B5 的赋值笔误更要注意）。
- 验证：`sed -n '60,76p' network/nebula/addr/addr_uuid.cpp`。

### F2. 帧协议固定 1GB 上限，且各传输上限不一致
- 位置：`network/network.h:497`（`if (len > (1u << 30)) return false;`）、`network/cs.h:37`（`frame_max_default = 1u << 30`）、`network/nebula/addr/addr_serial.h:164`（`s_frame_max = 1u << 20`）
- 现状：`recv_frame` 与 `frame_stream` 默认都接受 1GB 帧；串口侧收紧到 1MB（`frame_stream<serial_stream, s_frame_max>`），IPv4 侧同样是 1GB（`addr_ipv4.cpp:701`），IPv6 侧靠 `s_read_cap` 4MB 提前收连接。
- 理由：帧协议是 TCP/串口/CS/nebula 共用的一条约定，改小会连坐；串口是低速链路，1MB 已经远超其带宽。代价：默认 1GB 对「对端只发头」的灌水只给了一个很宽松的上界（B4）。
- 验证：`grep -n "1u << 30\|1u << 20\|4 << 20" network/network.h network/cs.h network/nebula/addr/*.h network/nebula/addr/*.cpp`。

### F3. 异常一律折成「空对象 / 断开连接」而不是向上抛
- 位置：`network/nebula/zero.h:54-62`、`network/cs.h:113-119`、`network/nebula/addr/addr_ipv4.cpp:713-720`（`catch (...) {}`）
- 现状：网络线程与工具执行都在**后台线程**里跑，异常冒出去会 `std::terminate` 整个进程。
- 理由：零号协议「无序、允许丢」的前提是坏帧不能杀进程——一个不可信远端就能让节点退出，代价更高。代价：故障被降级成「静默空结果」（B6），需要靠上层契约（如 `license_str[2] == "license"` 这类字符串比对）兜底。
- 验证：`sed -n '106,120p' network/cs.h`；`sed -n '713,721p' network/nebula/addr/addr_ipv4.cpp`。

### F4. 持锁只做内存操作，阻塞 IO 一律在锁外
- 位置：`network/nebula/addr/addr_ipv4.h:31-36`（锁序声明）、`network/nebula/zero.cpp:100-115`（工具表锁外调用）、`network/nebula/post.cpp` 的 `publish`/`send_goal`
- 现状：锁序固定为「端点表 → 连接表 → m_live → conn::mutex」，工具执行与网络发送都在锁外；`zero::exec_claim` 特意复用 `m_logic_mutex` 而不是新增成员（`zero.h:111-113` 的注释解释了「不新增成员以免类布局变化」）。
- 理由：阻塞 `connect`/`send` 最长可拖 1 秒（`m_s = 1000`，`addr_ipv4.h:92`），持锁等待会把整张连接表冻住。代价：每次都要「先拷贝快照再锁外执行」，于是有了 D2 那份 `kit` 深拷贝；也带来 `data` 析构那种「漏取锁」的风险（B2）。
- 验证：`sed -n '31,36p' network/nebula/addr/addr_ipv4.h`。

### F5. `link` 的心跳是独立线程、收发 reactor 归 `addr`
- 位置：`network/nebula/link.h:6`（注释「link 自己只持有心跳线程（ping），收发 reactor 全部归 addr」）、`link.h:24`
- 现状：心跳不并入 `life` 的事件池（`network/nebula/link.h:24` 的注释理由：「发心跳需要实时性」），因此每进程固定多一条线程。
- 理由：心跳阻塞在事件池队列尾部时判死会失准（`m_f` 次失联即摘邻居，`network/nebula/link.h:19`；`m_t` 心跳间隔见 `:20`）。代价：线程数不可再减，串口/IPv4/IPv6 各自还有 reactor 线程，关停要逐层 join。
- 验证：`grep -n "m_ping_thread\|m_ping_runing" network/nebula/link.cpp`。

### F6. `life_event` 按值捕获事件、事件池无序执行
- 位置：`network/nebula/life.cpp:14-23`
- 现状：只在 `event.contains("type")` 时投池（`life.cpp:16`）；投出后不保证顺序（注释「零号协议无序、事件队列无限」）。
- 理由：多 worker 无序是协议的一部分，上层（link/find/post）都按「乱序到达结果一致」设计（`find::graph::with_json` 的幂等合并注释即为此）。代价：D1 的每帧拷贝；且任何依赖事件顺序的上层逻辑都是错的（`find` 的 `m_back_map` 计数就假设了某种顺序）。
- 验证：`sed -n '14,23p' network/nebula/life.cpp`。

---

## G. 文档与代码的出入

### G1. `README.md` 里「AES 只实现 ECB/CBC」「`ipv6path` 是桩」「缺 TLS」「缺单测/基准」四条旧表述已逐条复核并标注
- 位置：`network/README.md` 的「补充说明」段（`:415-432`，旧说法就地标注在 `:418`、`:420`、`:424`）、「目录结构」段（`:22-23`）、「关键类/API」的 `sha` 行与 `socket.h` 行（`:110`、`:93`）；`grep -c "已作废（2026-10-07 复核 r677）" network/README.md` → **3**（`:418` 的 AES 模式、`:424` 的 signal.h 遮蔽，另 `:420` 用「保留并收窄」这一等价标注）
- 现状：四条中三条**成立但需要收窄口径**、一条（缺单测）**方向对但措辞错**：
  - AES：成立（见 E3），但旧文写「其余模式为占位」不准确——`CFB/OFB/CTR` 连设置入口都没有，是**不可达的枚举**而非占位实现。
  - `ipv6path`：成立（`socket.cpp:90-94` 就是 `return false`），但旧文没写「`ipv4path` 同样无调用者、`socket.h` 整体未被使用」（E1）。
  - 缺 TLS：成立（E3），且 grep 命中全为 `ioctlsocket` 子串，容易误判成「有」。
  - 缺单测/基准：**措辞错**。`test/` 下有 5 个覆盖本模块的目标（`network_test`/`cs_test`/`astral_test`/`astral_behavior_test`/`astral_stress_test`，已在 `test/CMakeLists.txt:145,208,213,218,223` 登记）；真实缺口是**没有 benchmark**、且 `network/` 目录内没有测试文件、根 `CMakeLists.txt` 不接入 `test/`（E4）。**本轮又发现两处 test/README 与实际不符**：`test/README.md:53` 把 `network_test` 描述为「md5 / base64 / uuid」，实际文件也只测这三样——于是 **md2/md4/sha 全系（含 A1 那条坏掉的 SHA-3）没有任何测试**；SHA-3 之所以能长期没人发现，正是因为这条描述把「覆盖了网络模块的纯算法部分」写成了既成事实。
- 证据：`grep -n "network_test\|cs_test\|astral_test" test/CMakeLists.txt` → `:145`、`:208`、`:213`、`:218`、`:223`；`grep -rn "benchmark" CMakeLists.txt test/CMakeLists.txt` → 0 命中；`grep -cn "sha" test/network_test.cpp` → 0 命中。
- 影响：接手者按旧文会去找「不存在的测试目录约定」，或误判「有 CFB 可选」或「SHA 系已被测过」。
- 复现/验证：见上三条 grep。
- 建议：README 已按上述口径改写；后续新增测试目标时同步这一节，并给 `network_test.cpp` 补 SHA-2/SHA-3 向量（A1）。

### G2. `network.h` / `tcp.h` 里两处「select」注释与实现的 `poll` 不符（本节只记源码注释漂移，不影响行为）
- 位置：`network/network.h:305`（注释「非阻塞 connect + **select** 超时」）、`network/tcp.h:85`（注释「connect 非阻塞 + **select**」）
- 现状：两处实现走的都是 `poll`/`WSAPoll`（`network.h:271-281`），且 `:240-241` 的注释自己解释了为什么**不能**用 `select`（Windows 句柄超过 `FD_SETSIZE`）。即同文件内一处说不用 select、另两处说用了 select。
- 证据：`grep -n "select\|poll(" network/network.h` → `:240-241`（解释弃用 select）、`:271-281`（poll 实现）、`:305`（残留的 select 措辞）；`tcp.h:85` 同样。
- 影响：只误导读注释的人（会以为存在 select 分支）。不属代码缺陷，故放 G 组。
- 复现/验证：上述 grep。
- 建议：把两处注释的 «select» 改成 «poll»。

### G3. `mmap.h` 的 include guard 名是 `XHQM_SOUL`，且构造函数在 `open()` 之后才初始化 `size_`
- 位置：`network/mmap.h:2-3`（`#ifndef XHQM_SOUL / #define XHQM_SOUL`）、`network/mmap.h:20-28`（构造函数体内 `open(path)` 在 `info_ = ""` / `size_ = 0` **之前**）
- 现状：guard 名与「soul 模块」重名（`xhqm/soul.h` 一族在用同一批名字），靠头文件路径区分；构造顺序上，成员初始化列表没写 `size_`，`open()` 会先把它设成真实大小，随后构造函数体里的 `size_ = 0` 再把它**改回 0**——所以带路径构造出来的 `mmap` 是 `good()==true` 但 `size()==0`。
- 证据：`sed -n '1,30p' network/mmap.h`；`mmap.cpp:83` 里 `open()` 赋值 `size_`，`mmap.cpp:109-112` 的 `buffer()` 依赖 `size_`。
- 影响：`mmap(path)` 这种一次性写法拿不到数据（`buffer()` 里 `pos <= size_` 即 `0 <= 0`，`size_ - pos` 为 0，返回空）；现有调用方都走「默认构造 + 显式 `open()`」（`astral/mmap_index.h:53` 的 `map_` 就是默认构造），所以今天没暴露。
- 复现/验证：`sed -n '20,28p' network/mmap.h`（顺序可见）；本机未跑（避免与「只写两个文件」的约束冲突）。
- 建议：把 `info_ = ""` / `size_ = 0` 挪进初始化列表或移到 `open()` 之前，并把 guard 名改成 `XHQM_NETWORK_MMAP`。

### G4. `network/astral/README.md` 与 `network/nebula/README.md` 覆盖的范围与本文件的分工
- 位置：`network/astral/README.md`（161 行，astral 内核/持久化/索引细节）、`network/nebula/README.md`（540 行，B 世代分层 + 其 A2 对 `signal.h` 的订正）、`network/nebula/DEFERRED.md`（122 行，决定不做的事项）
- 现状：三份都是活跃文档、位置正确；本文件**不复制**它们的内容，只在交叉处给指向：`signal.h` 遮蔽与槽池安装 → 仓库根 `DEBUG.md`（B1、C1、F4）与 `network/nebula/README.md` A2；nebula 三种传输语义差异 → `network/nebula/DEFERRED.md` §1；astral 惰性加载与 mmap 索引格式 → `network/astral/README.md`。
- 证据：`wc -l network/README.md network/astral/README.md network/nebula/README.md network/nebula/DEFERRED.md` → `346 / 161 / 540 / 122`（本轮复核时 README 已由维护者改写 344 行那段，文件仍是 346 行）。
- 影响：避免同一条问题在四份档里各写一遍、各自漂移。
- 复现/验证：`ls network/*.md network/astral/*.md network/nebula/*.md`。
- 建议：新增模块级问题一律进本文件；子模块内部细节留在各自 README。
