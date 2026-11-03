# xhqm · software 软件基础设施模块 · 当前问题清单

software 模块（路径 `software`，module id `software`）的**当前仍存在**的问题台账：只记「现在还是坏的 / 还是没写的 / 还是对不上的」，**不记已修项、不做版本历史**。每条都给出可核对的 `文件:行号`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**：本仓的历史台账**不搬家、不改名** —— `fantasy/fantasy_bugs.md`（fantasy 静态分析 + 修复记录）、`network/nebula/DEFERRED.md`（nebula「决定不做」）保持原位，本文只指过去、不复制内容。本模块没有更早的专属台账（`software/README.md` 的「已知缺口」是它的前身，本次已改为指向本文）。
> 与其它模块 `DEBUG.md`（根 `DEBUG.md`、`xhqm/DEBUG.md`、`fantasy/DEBUG.md`、`hardware/DEBUG.md`、`lattice/DEBUG.md`、`algebra/geometry/brep/DEBUG.md`）之间**不重复记同一件事**：`signal` 的「进程级槽池必须由宿主安装」这一隐式契约已由**根 `DEBUG.md` B1** 登记，本文只在 **A15** 指向它、不重抄；`software/system/README.md`（子模块文档）与代码的出入见本文 **G2**（那个文件不归本文件改）。
> **同批建立的子模块台账**：`software/pattern/DEBUG.md`（module id `software/pattern`）与 `software/system/DEBUG.md`（module id `software/system`）与本文件同批出现，它们把 `pattern/*`、`system/*` 的内部缺陷记到行级、条目更细。本文件是**模块总览层**，凡两边都有的条目按下表**指过去**（本文件的编号独立，摘要保留是为了让模块级 README 的引用有落点；**细节与后续更新以子模块文件为准**）：
>
> | 本文件 | 子模块文件里的同一条 | 关系 |
> |---|---|---|
> | A9（`execute_bat(…, 0)` 假成功） | `software/system/DEBUG.md` A6 | 同一条，子文件有更细的推导 |
> | A11（`dir_list`/`file_list` 口径） | 同上 A9 | 同一条 |
> | A12（`timer::now_time/run_time`） | 同上 A1 | 同一条，子文件给了模拟数值 |
> | A16（`work_directory()` 同名同实现） | 同上 A5 | 同一条 |
> | B3（线程池无排队上限/通道表只增） | 同上 B1、B2、E3、F1 | 本文件只取「默认 `npos` + 通道表按号补齐」两点 |
> | B4（`process` 析构 `wait(10)`） | 同上 F5 | 同一条（子文件按「取舍」登记，本文件按「语义陷阱」登记） |
> | B6（两个读线程并发写 `m_result`、丢空行） | 同上 B3（并发部分） | 子文件不含「丢空行」，那半条是本文件独有 |
> | C1（`_HAS_CXX20` 是 MSVC 专有宏） | 同上 C1 | 同一条 |
> | B8（`command_history<multi_thread>` 执行顺序） | `software/pattern/DEBUG.md` B4、F5、G3 | 同一条，子文件有更细的窗口分析 |
> | B9（`instance_data` 无锁初始化） | 同上 B3 | 同一条 |
> | F2（`observer::change` 的 8 字节位拷贝） | 同上 A1 | **改为指向**：子文件已证明成员函数指针（16 字节）被截断 —— 那是**缺陷**而不是纯取舍，本文件不再替它辩护 |
> | F4（worker 回收时机） | `software/system/DEBUG.md` B7 | 同一条 |
> | F6（`notifier` 只认函数指针） | `software/pattern/DEBUG.md` F3、G2 | 同一条；子文件的 G2 还订正了「无捕获 lambda 可注册」 |
> | G2（`software/system/README.md` 与代码的出入） | `software/system/DEBUG.md`（其 README 订正记录） | 该子文件已在同一批里改掉自己的 README，见下 |
>
> 同理，本文件不再重复子模块台账里那些**纯 `pattern/*`、纯 `system/*`** 的细目（如 `FILE_BINtoBXT` 往返错位、`fopen` 不判空、`submit` 丢弃返回值、`dlopen` 引用泄漏等），它们各自在子文件里。
> 判据纪律：凡写「编译不过 / 某 API 不存在」，必须附复现命令与验证版本（本机 `g++ (Ubuntu 11.4.0) -std=c++17 -fsyntax-only -I. -Isoftware`，glibc 2.35）；凡写「运行时行为」，必须注明是否在本机复现 —— 本节点**没有跑过模块自身的任何测试程序**（`test/system_test.cpp` 等有源码、有 CMake 目标，但本轮未构建未运行），故凡需执行的结论一律标「未复现（读码判定）」或给编译探针。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `datum` 写 `text` 不做任何转义，字段名也不转义 → 含引号/反斜杠/逗号的值写出即损坏
- 位置：`software/datum.h:118-122`（值）、`software/datum.cpp:267-271`（字段名）
- 现状：`save_data<text>` 就是 `data_str = ("\"" + std::string(*data_ptr) + "\"");`，原样包裹双引号而已；`save_json` 拼字段名同样是 `'"' + m_name + '"'`。JSON 里该转义的 `"`、`\`、控制符、以及字段名里的引号，一个都没处理。
- 证据：`datum.h:120` 全行只有字符串拼接，无 `find/replace/escape`；`grep -c "\\\\\"" software/datum.h` 无转义分支；`datum.h:160-166`（读侧）也只是「砍掉首尾字符」，不做反转义 —— 两侧对称地不转义。
- 影响：任何把用户文本（路径、命令行、中文说明）塞进 `datum`/`packet` 元数据再落盘的链路：值里出现 `"` 会让整个 JSON 结构错位，`read_json` 随后按 `_split_json_object_` 切出的字段数对不上 → 直接返回 false（整个对象读不回来）；出现 `,`/`:` 至少污染同一个字段。**不报错、不告警**。
- 复现/验证：未复现（读码判定即可）。想跑最小验证：注册一个 `xhqm::text` 字段赋值 `a"b`，`save_json` 后打印，可见 `{"x":"a"b"}`；也可 `test/system_test.cpp` 的写法加一个用例。
- 建议：在 `save_data<text>`/`read_data<text>` 里成对加最小 JSON 转义（`\" \\ \n \r \t` 与控制符），字段名同样处理。

### A2. `datum::save_list` 对空 `std::vector` 写出的是一个 `]`，不是 `[]`
- 位置：`software/datum.h:225`（`std::string values = "[";`）、`software/datum.h:233`（`values.back() = ']';`）
- 现状：非空时 `values` 形如 `[a,b,`，把最后一个逗号改成 `]` 得到 `[a,b]`；**空 vector 时 `values` 只有一个 `[`，`back()` 就是那个 `[`，被覆盖成 `]`**，输出长度为 1 的 `]`。函数照常 `return save_res;`（`true`）。
- 证据：`datum.h:224-235`；空 vector 不进入循环 → `values.size() == 1` → `values.back() = ']'`。读侧对照：`_split_json_array_("]")`（`datum.cpp:100-192`，非 `[` 开头不剥壳）把 `]` 解析成 1 个元素，所以「空数组」往返也失败（`read_list` 要求 `elems.size() == data_ptr->size()`，`datum.h:251`）。
- 影响：任何被序列化的空 vector 字段产出非法 JSON；若同一次 `save_json` 里还有别的字段，结果是 `{"v":],"a":1}` 这类结构，**整个文件读不回来**。用 `packet`/`datum` 存档的对象只要有一个空列表就中招。
- 复现/验证：未复现（读码判定：`"["` 经 `back()=']'` 只有一个可能结果）。也可在 `test/system_test.cpp` 里加 `std::vector<int> e; datum::save_list(e)` 断言输出 `"[]"` 来复现失败。
- 建议：循环外先判空（`if (data_ptr->empty()) { data_str = "[]"; return true; }`）。

### A3. `datum::read_data<text>` 盲删首尾字符：非字符串值被剪残，单字符值踩 `erase(end()-1)` 的未定义行为
- 位置：`software/datum.h:160-166`
- 现状：整段逻辑是「赋值为 `data_str` → `erase(begin())` → `erase(end()-1)`」，既不检查首尾是不是引号，也不检查长度。`data_str == ""` 只在入口处判过（`datum.h:144`），长度 1 的串能走到这里。
- 证据：`datum.h:162-164`。长度 1 时 `erase(begin())` 之后串已空，`erase(end()-1)` 的迭代器在 `begin()` 之前 —— 标准未定义（libstdc++ 视断言开关可能是崩溃或静默）。
- 影响：① 读一份非本程序写出的 JSON（手改过、或别的工具生成）时，`{"info":null}` 这类非字符串值会被剪成 `ul`，**静默错误**；② 长度 1 的值（如 `{"t":5}` 落到 `text` 字段、或自定义 `sptr` 写了裸字符）触发 UB。
- 复现/验证：未复现（读码判定）。可复现方式：构造 `std::string s; xhqm::datum::read_data<xhqm::text>(std::string("5"), ptr)` 走一遍。
- 建议：改成「首尾都是 `"` 且长度 ≥2 才剥壳，否则报错返回 false」，并在剥壳后做反转义。

### A4. `datum::read_json` / `read_file` 的失败语义不可靠：字段乱序、缺字段、文件不存在都可能返回 `true`
- 位置：`software/datum.cpp:296-320`（`read_json`）、`software/datum.cpp:322-338`（`read_file`）、`software/datum.cpp:309-315`（静默跳过）
- 现状：`read_json` 只做两件事：JSON 里的字段数必须**等于**注册表条数（`:301`，不等直接 false），然后**按下标一一对应**地比对名字；名字对不上就什么都不做（`if (data_key[0] == "\"" + data_ref.m_name + "\"")` 没有 else），循环结束照样 `return true`。`read_file` 打不开文件也只是得到空串，交给 `read_json` 继续走。
- 证据：`datum.cpp:309`（无 else 分支）、`:317`（无条件 `return true`）；`datum.cpp:325-337`（`if (file.is_open())` 失败后不 return，`json_str` 保持空）；空 `datum`（0 个注册字段）读一个不存在的路径 → `_split_json_object_("")` 得 0 项、`0 == 0` → `return true`。
- 影响：字段顺序一变（人工编辑、别的版本写出、跨语言生成）就**一个字都读不回来，但调用方看到的是成功**；配置文件静默回落到默认值。文件路径写错同理（在 `datum` 为空时甚至直接报成功）。
- 复现/验证：未复现（读码判定）。最小复现：注册两个字段保存一次，手工把两个字段在文件里对调，`read_file` 仍返回 true 且两个成员都没变。
- 建议：改成按名字查表（无序匹配）+ 缺字段/多余字段显式 failed，`read_file` 打不开文件直接 false。

### A5. `datum::value_ptr(name)` 找不存在的名字会越界读；`object::attribute<T>(name)` 同源
- 位置：`software/datum.h:362-365`（`std::find(...) - _datum_.begin()`）、`software/packet.h:44-48`（`attribute<type>()`）
- 现状：`value_ptr<type>(name)` 把 `find` 的结果直接当下标用：`value_ptr<type>(std::find(...) - _datum_.begin())`，而 `value_ptr(index)`（`datum.h:367-371`）不做边界检查就对 `_datum_[index]` 取 `m_dtyp`/`m_dptr`。`find` 未命中时 `end() - begin() == size()`，即**越界一个元素**。
- 证据：`datum.h:364`、`datum.h:369`；`object::attribute<T>(name)` 调的就是 `m_attribute.value_ptr<type>(name)`（`packet.h:47`），随后对返回指针解引用。
- 影响：任何「按名字取元数据」的代码打错一个字、或用旧名字读新版本对象（插件版本不匹配），拿到的是越界的 `datum_data_`：要么读到垃圾指针后解引用崩，要么抛一个 `std::string`（见 A6）逃出常规错误处理。
- 复现/验证：未复现（读码判定）。可用 `_GLIBCXX_ASSERTIONS` 或 ASan 构建一个「注册 a、取 b」的小程序复现。
- 建议：`find` 后先 `if (it == _datum_.end()) { throw xhqm::error(...); }` 再算下标。

### A6. 类型不符/注册失败抛的是 `std::string`，不是 `xhqm::error` —— `catch (std::exception&)` 抓不到，直接 `std::terminate`
- 位置：`software/datum.h:369`（`throw std::string("error type")`）、`software/packet.h:70`（`throw std::string("unknow object_type")`）
- 现状：这两处（以及 `network/nebula/data.*` 里同类写法）抛的是标准库字符串；本模块其它地方（如 `threads` 构造、`signals::pool`、`libmange::function`）统一抛 `xhqm::error`（`head.h:240-243`，`std::runtime_error` 子类）。
- 证据：`datum.h:369`、`packet.h:70`；对照 `signal.cpp:18`、`threads_pool.cpp:25,29,33`、`libmange.h:40` 的 `xhqm::error`。
- 影响：按「引擎错误都是 `xhqm::error`/`std::exception`」写的上层保护代码（`catch (const std::exception& e)`）会漏掉这两类异常 → 异常穿出线程入口或 `main` → `std::terminate`。上述 A5 的越界路径正好会走到 `throw std::string`。
- 复现/验证：未复现（读码判定：`std::string` 不是 `std::exception` 派生）。
- 建议：统一抛 `xhqm::error`（顺手把消息里补上类型名/字段名）。

### A7. `datum::resigt(name, std::vector<T>*)` 记的是**元素**类型标签，反查 `value_ptr<std::vector<T>>` 永远取不到
- 位置：`software/datum.h:344-349`（注册 `packet.m_dtyp = datum_data_v<type>::value;`）、`software/datum.h:367-371`（取用时比对 `datum_data_v<type>`）、`software/datum.h:69`（主模板默认 `file_t`）
- 现状：vector 重载把 `m_dtyp` 写成元素类型 `T` 的标签（如 `std::vector<xhqm::text>` → `text_t`），而取用侧 `value_ptr<std::vector<xhqm::text>>` 用的是 `datum_data_v<std::vector<text>>`，标准里没有 vector 特化 → 落主模板 `file_t`。两者永不相等。
- 证据：`datum.h:346` vs `:369`；`grep -n "datum_data_v<std::vector" software/datum.h` → 0 命中（只有 `:70` 的 `xhqm::datum` 特化）。
- 影响：所有按名字取 vector 字段的代码（含将来 `object::attribute<std::vector<T>>`）都会走 `throw std::string("error type")` 这条线（见 A6）。副作用：vector 字段与同元素类型的标量字段在 `m_dtyp` 上不可区分，A5 那种按标签校验也保护不了它。
- 复现/验证：未复现（读码判定：模板参数不同、无特化即不等）。
- 建议：加 `datum_data_v<std::vector<T>>` 特化（或注册时存 `list_t` 一类新标签），让注册/取用两侧用同一个标签。

### A8. `software::command` 的 redo/undo 状态机会卡死失败命令：失败后既不能再 redo 也不能 undo
- 位置：`software/command.cpp:14-23`（`redo`）、`software/command.cpp:25-34`（`undo`）
- 现状：`redo()` 在 `!_active` 时调 `_redo()`，随后**无条件**把 `m_active = true`，返回值却可能来自失败的 `_redo()`（`m_sucess = false`）。此后：再调 `redo()` 因 `m_active` 为真直接返回 false（不会重试）；调 `undo()` 需要 `m_active && m_sucess`，`m_sucess` 为假也返回 false。`undo()` 则在调用 `_undo()` **之前**就把 `m_active/m_sucess` 双清零，`_undo()` 失败也回不到「已执行」状态。
- 证据：`command.cpp:16-22`（`m_active = true` 在 `m_sucess` 判断之外）、`command.cpp:27-33`（先清状态再 `return this->_undo();`）。
- 影响：派生命令（`_redo/_undo` 是纯虚）只要有一次执行失败，该命令对象永久失效且无诊断 —— 命令容器（`command_root`）本来也没实现，所以影响面止于自己造命令的调用方。
- 复现/验证：未复现（读码判定）。可写一个 `_redo` 首次返回 false 的派生命令验证。
- 建议：`m_active = m_sucess;`（redo 失败保持未激活），undo 改为「先调 `_undo()`、按结果改状态」。

### A9. `execsys::execute_bat(commands, 0)` 不给任何线程，却返回「全部成功」的空结果
- 位置：`software/system/execsys.cpp:29-60`（尤其 `:33` 的 `std::vector<...> results(commands.size());` 与 `:34` 的循环上界）
- 现状：工作线程数就是 `max_concurrent`；为 0 时 `workers` 为空、`while` 里的 `next_command` 永远不被取用，直接 `return results;`。`results` 是默认构造的 `process::result`：`exit_code = 0`、`exec_error = none` → **`success()` 全为 `true`**。
- 证据：`execsys.cpp:33-34,55-59`；`process_base.h:39-48`（默认值与 `success()`）。
- 影响：并发度来自配置/CPU 数时，一旦算出 0（或上游漏配），批量执行会「全部成功」地什么都不做 —— 典型的静默假成功；`execute_bat` 的调用方（仓内目前是 `execsys` 自己与测试）拿到的是空输出 + 成功标记。
- 复现/验证：未复现（读码判定：0 个线程 ⇒ `results` 不被写过）。
- 建议：`max_concurrent == 0` 时按 1 处理（或显式返回 `fail_start`）。

### A10. `message_root::save()` 的文件名只有秒级精度，而且它还是「排空式」写日志 —— 同一秒内两次 save 互相覆盖
- 位置：`software/message.cpp:34-53`（`:38` 路径、`:40` `std::ofstream file(path)`、`:49` `m_messages.pop()`）
- 现状：文件名是 `message/<epoch 秒>.log`；`std::ofstream` 默认截断，同一秒内第二次 `save()` 会**打开同一个文件并清空**它；`save()` 本身是把队列全部写出后 `pop` 掉（排空），所以第二次写进去的是新消息、上一次的内容没了。
- 证据：`message.cpp:36-38`（`to_time_t` → 秒）、`:39-40`（无 `ios::app`）、`:42-50`（边写边 `pop`）。目录 `message/` 也不创建（见 B5）。
- 影响：高频打日志（例如每帧或错误风暴里调 `save()`）会丢日志，且因为「排空语义」，丢的还是**已经离开队列**的那批，无法重写。调用方若把 `save()` 当「落盘且保留队列」用，`message_count()` 会意外归零。
- 复现/验证：未复现（读码判定）。复现：同一秒内连调两次 `save()`，第一次的日志文件内容被覆盖。
- 建议：文件名带毫秒 + 序号、或 `ios::app`；把「排空」写进函数名/注释（`flush_to_log`）。

### A11. `filesys::dir_list` 返回全路径、`file_list` 只返回文件名，`path_list` 把两种混在一个表里
- 位置：`software/system/filesys.cpp:41-52`（`:49` `ite.path().string()`）、`software/system/filesys.cpp:53-64`（`:61` `ite.path().filename().string()`）、`software/system/filesys.cpp:65-74`（拼接）
- 现状：同一对 API 的返回值口径不一致；`path_list` 同时含「绝对/相对全路径」和「裸文件名」两类条目。`test/system_test.cpp` 只断言了 `file_list`（`files[0] == "test.txt"`），所以目录那一半没被测试覆盖到。
- 证据：`filesys.cpp:49` vs `:61`，`:69-72` 两者 `insert` 到一起。
- 影响：`dir + "/" + item` 这种最常见写法对目录项必然错（拼成 `dir//abs/path/child`）；`rename/copy/delete` 系列又都是「原样收路径」，于是「列举→移动」的链路要么对文件对、要么对目录错。
- 复现/验证：未复现（读码判定，`ite.path()` 与 `ite.path().filename()` 的差别是标准行为）。
- 建议：统一成「返回全路径」或「返回文件名」一种，另一个改名（如 `dir_path_list`）。

### A12. `timer::now_time()` / `run_time()` 都算不出耗时：一个把已耗时当 epoch，一个多减一次起点并在无符号域下溢
- 位置：`software/system/timesys.cpp:66-69`（`now_time`）、`software/system/timesys.cpp:70-74`（`run_time`）、`software/system/timesys.h:97-102`（`uptimer/runtime` 的定义）
- 现状：`runtime()` 已经是「自 `uptimer()` 起的纳秒数」。`now_time()` 却把它 `/1000000` 当「epoch 毫秒」传给 `timesys::localtime()` → 打印的是 **1970-01-01 + 已耗时** 那个时刻（本机算例：已耗时 5ms → `1970/1/1 8:0:0:5`）。`run_time()` 更绕：`(runtime() - m_starns) / 1000000`，把起点时间**又减了一次**；两个操作数都是 `xhqm::natural`（无符号），`runtime() < m_starns` 必然下溢成接近 `2^64` 的数 → 换算出的时间点在**约公元 2498 年**（本机按 2026-10-07 的 `m_starns ≈ 1.79e18` ns 算得 1.6666e13 ms）。
- 证据：`timesys.cpp:68`（`localtime(this->runtime() / 1000000)`）、`timesys.cpp:72`（`systemtime((this->runtime() - this->m_starns) / 1000000)`）、`timesys.h:97-98`；`typesys` 的 `natural = std::uintmax_t`（`head.h:213` 注释区 / `algebra/value.h`）。
- 影响：`timer` 的文本接口只有 `old_time()` 正确（起点时刻，`timesys.cpp:62-65`）。想做「本轮耗时」日志/耗时报表的人拿到的是 1970 年或 2498 年的时分秒。`software/system/README.md:127` 还在拿它当正确范例（见 G2）。
- 复现/验证：未复现（未运行；算术按上述类型与公式手算，两个结果都可复核）。`test/system_test.cpp` 只测了 `runtime()/mtimes()`，没测文本三件套。
- 建议：`run_time()` 改成 `systemtime(runtime() / 1000000)`（或直接算 h/m/s/ms），`now_time()` 改成 `localtime(timesys::mtime())`。

### A13. `signal.h` 的遮蔽是**包含顺序**问题：先 include `<sys/wait.h>` 会让本头落进 `extern "C"`，实测 5091 个 error
- 位置：`software/signal.h:9-13`（`:10` `#include_next <signal.h>`、`:11` `#include "pattern/instance.h"`、`:13` 模板前置声明）；触发点在 `/usr/include/x86_64-linux-gnu/sys/wait.h:27,36`
- 现状：本文件在 `-I software/` 里与系统头同名，所以 glibc 的 `<sys/wait.h>` 在 `__BEGIN_DECLS`（`extern "C" {`）里 `#include <signal.h>` 时命中的是本文件，本文件又在这一块里 include `pattern/instance.h` → `head.h:33` → `<map>` 等 C++ 头，于是整条链落进 C linkage。**直接 include `<signal.h>`/`<csignal>` 是安全的**（`#include_next` 生效），危险只在「系统头先把它拉进来」这个方向。
- 证据（本机实测，g++ 11.4 / glibc 2.35 / `-I. -Isoftware`）：
  ```sh
  printf '#include <sys/wait.h>\nint main(){return 0;}\n' | g++ -std=c++17 -fsyntax-only -x c++ -I. -Isoftware -
  # → 5091 个 error；包含链 sys/wait.h:36 → signal.h:11 → pattern/instance.h:3 → head.h:33 → <map>
  #   首条 bits/functexcept.h:101: conflicting declaration of C function
  #   随后 ext/numeric_traits.h:50: template with C linkage
  printf '#include "software/signal.h"\n#include <sys/wait.h>\nint main(){return 0;}\n' | g++ -std=c++17 -fsyntax-only -x c++ -I. -Isoftware -
  # → 0 error（guard XHQM_SIGNAL 已置位，extern "C" 里那次整块跳过）
  ```
  因此**「把第 13 行前置声明挪到 `#include_next` 之前」并不够** —— 第一处爆点在 `:11` 引入的 `pattern/instance.h`（`head.h`→`<map>`），要一起挪或改用不 include C++ 头的 friend 方案。`process_base.cpp:22,24` 现在能过，只是因为它先 `<signal.h>` 后 `<sys/wait.h>`，同文件还补了 `kill`/`SIGTERM`/`SIGKILL` 的手工声明（`process_base.cpp:26-40`）。
- 影响：任何一个新 TU 只要在 include 本模块头之前先 include `<sys/wait.h>`（POSIX 下等子进程/取退出码的代码很容易这么写），编译直接崩在 C++ 标准库头里，报错信息完全指不到 `signal.h`。这是本模块对**全仓**的编译期炸弹，跨模块。
- 复现/验证：**已复现**（上面两条命令，本机 g++ 11.4.0）；另见 `network/README.md:344`、`network/nebula/README.md` A2 的同一结论。
- 建议：让本头被 `extern "C"` 包住时也安全：把 `:11` 的 `pattern/instance.h` 与 `:13` 的声明都挪到 `#include_next <signal.h>` 之前，或给 `pool_life` 换一个不依赖前置声明的 friend 方案。

### A14. `event.h` 单独 include 编不过（`xhqm_io` 未定义），仓库自带的转发头 `xhqm/event` 同样编不过
- 位置：`software/event.h:159`（`struct xhqm_io event`），同文件**没有** `#include`（全文 0 个 include）；转发头 `xhqm/event`（一行 `#include "../software/event.h"`）
- 现状：`xhqm_io` 由 `head.h:27-31` 定义，`event.h` 直接用却从不引入；单独包含它的 TU 报 8 个 error（首条 `variable 'xhqm::xhqm_io xhqm::event' has initializer but incomplete type`）。
- 证据（本机实测）：
  ```sh
  printf '#include "software/event.h"\nint main(){return 0;}\n' | g++ -std=c++17 -fsyntax-only -x c++ -I. -Isoftware -   # 8 个 error
  printf '#include "xhqm/event"\nint main(){return 0;}\n'          | g++ -std=c++17 -fsyntax-only -x c++ -I.            # 8 个 error
  ```
  同批探针里其余 29 个模块头单独包含均 0 error（`software/signal.h`、`packet.h`、`system.h`、`pattern/*` 等）。
- 影响：任何人（包括 IDE 的单文件语法检查、只看 `xhqm/event` 的读者）按名字包含这个「事件」头就撞墙，报错还落在 `event` 的结构体上而不是缺 include。
- 复现/验证：**已复现**（上两条命令）。
- 建议：在 `event.h` 顶部加 `#include "../head.h"`（其余模块头都这么干）。

### A15. `signal.cpp` 抛出的「怎么装槽池」指引本身编译不过；仓内也没有任何调用方
- 位置：`software/signal.cpp:17-19`（异常文本 `…请调用 xhqm::getInstance<signals::pool_life>();`）、`software/signal.h:49`（`xhqm_io void pool_life();` —— 它是**函数**）、`software/signal.cpp:22-33`（真正的单例类型叫 `_pool_life_`）
- 现状：池未安装时抛 `xhqm::error("signals::pool 未安装：main 首行请调用 xhqm::getInstance<signals::pool_life>();")`。照抄这句话会得到 `error: no matching function for call to 'getInstance<xhqm::signals::pool_life>()'`（模板实参位置放了函数名）；能编过的入口是 `xhqm::signals::pool_life()`（`signal.cpp:35-38`，内部才调 `getInstance<_pool_life_>()`）。
- 证据（本机实测）：
  ```sh
  printf '#include "software/signal.h"\nint main(){ xhqm::getInstance<xhqm::signals::pool_life>(); return 0; }\n' | g++ -std=c++17 -fsyntax-only -x c++ -I. -Isoftware -   # error: no matching function…
  printf '#include "software/signal.h"\nint main(){ xhqm::signals::pool_life(); return 0; }\n'                     | g++ -std=c++17 -fsyntax-only -x c++ -I. -Isoftware -   # 0 error
  ```
  另外 `grep -rn "pool_life" --include=*.cpp --include=*.h .`（排除 `software/signal.*`）→ **0 命中**：仓内（含 `network/nebula` 的 `frame_signal`/`broad_signal` 链路）没有任何宿主安装过槽池。
- 影响：踩到的人第一时间看到的建议是错的，会浪费一轮编译；而仓内没有任何调用方意味着**任何跨线程 `signal_auto`/`signal_queued` 一旦被触发就是运行期抛异常**（隐式契约本身的登记见根 `DEBUG.md` B1，此处不重抄）。
- 复现/验证：**编译探针已复现**（上两条）；运行期抛错未复现（本机未跑起任何走异线程槽的宿主）。
- 建议：异常文本改成可编译的 `xhqm::signals::pool_life()`；并考虑把 `pool()` 改成首次调用自动安装（把契约从文档搬进代码）。

### A16. `filesys::work_directory()` 与 `path_directory()` 实现逐行相同，但名字暗示「工作目录」
- 位置：`software/system/filesys.cpp:238-274`（`path_directory`）与 `software/system/filesys.cpp:276-312`（`work_directory`）
- 现状：两个函数各自用一个函数内 `static fs::path cached_path` 缓存，函数体除了变量名没有差别 —— 都是「Windows `GetModuleFileNameW` / Linux `readlink("/proc/self/exe")` / macOS `_NSGetExecutablePath` 取可执行文件路径，再取 `parent_path()`，失败回退 `current_path()`」。真正的进程工作目录是 `current_path()`（`filesys.cpp:314-317`）。
- 证据：两段代码对照（`:245-273` vs `:283-311` 完全同构）；`grep -rn "work_directory" .` → 仓内无调用方（0 命中），所以目前只是「误导的 API」，没有实际受害者。
- 影响：接手者按名字用 `work_directory()` 取 CWD 会拿到 exe 目录（Windows 上还可能是 `PATH_MAX/MAX_PATH` 截断后的父目录）。两份缓存也意味着二者可能在极端情况下不一致（各自独立取一次）。
- 复现/验证：未复现（读码判定，`grep` 已确认无调用方）。
- 建议：`work_directory()` 直接 `return current_path();`（或删掉重复实现并改名 `exe_directory()`）。

---

## B. 资源与并发

### B1. `signal` 的 queued/跨线程 auto 槽只捕获裸 `object*`，存活标志只在 `emit` 时查一次 → 接收者先析构即悬垂
- 位置：`software/signal.h:102-119`（slot 里存的是 `[object, fn](...) { (object->*fn)(v...); }`）、`software/signal.h:146`（唯一一次判活）、`software/signal.h:158-163`（投递任务）
- 现状：`signal_receiver` 的 `shared_ptr<atomic_bool>` 确实能让 `emit` 跳过已销毁的接收者，但**判活发生在 `emit` 派发前**；投递进线程池的 lambda 捕获的是 `invoke`（闭包里的 `object` 裸指针）与参数，任务真正执行时不会再看 `alive`。`disconnect` 也只清槽表，管不到已经在池子队列里的任务。
- 证据：`signal.h:107-109`（`add([object, fn](...) …)`）、`:146`（`if (s.alive && !s.alive->load()) continue;`）、`:160-163`（`pool().submit(0, [invoke = s.invoke, args = …]…)`）—— 提交的闭包里没有 `alive`。
- 影响：任何「跨线程信号 + 短命接收者」组合（例如临时窗口/临时订阅者）都有 use-after-free 风险；线程池越忙（任务排队越久）越容易踩。`software/README.md` 原本「不会悬垂」的说法已据此订正。
- 复现/验证**未复现**（未运行）。最小复现：`pool_life()` 之后起一个 `signal<int>`，`connect(&r, &R::f)` 用 `signal_queued`，`emit` 后立刻析构 `r`，让池子延迟执行。
- 建议：把 `alive` 一起捕获进投递闭包并在执行前再判一次（或投递时持有 `shared_ptr` 生命周期令牌）。

### B2. `system_root` 的 4 个子容器是 `new` 出来的，而对象树不持有所有权、也没有释放入口 → 每次构造泄漏 4 个 packet
- 位置：`software/system.cpp:10-13`（`add_child_ptr(new message_root())` 等 4 行）、`lattice/tree.hpp:798-809`（`_add_child_ptr`/`_sub_child_ptr` 只改父指针）、`lattice/tree.hpp:64-69`（`childs_loca` 是裸指针容器）
- 现状：`derive_tree` 的子表存的是 `node_type*`，`_add_child_ptr` 只做 `child_ptr_->_set_parent_ptr(this)`、`_sub_child_ptr` 只做 `->_set_parent_ptr(nullptr)`；整个 `lattice/tree.hpp` 里没有一处 `delete`（唯二的 "delete" 字样是 `:408-409` 删除拷贝/移动构造）。`software::packet` 的派生类也没有「移除并销毁子节点」的接口，`project_root::move_project`/`utility_root::move_utility` 只是断链。
- 证据：`system.cpp:10-13`；`tree.hpp:798-809`；`grep -c "delete" lattice/tree.hpp` → 2，且都在 `derive_tree_` 的删除函数声明上。
- 影响：`system_root` 每构造一次（`test` 之外的宿主若有多个系统实例，或反复重建）就泄漏 5 个对象（4 个子容器 + 各自的 `datum` 注册表 / uuid）；子容器析构函数永不执行。仓内暂无调用方，所以现在是「埋着的坑」而非现网泄漏。
- 复现/验证：未复现（读码判定 + `grep` 确认无 `delete`）。ASan 下构造/析构一个 `system_root` 即可看到 4 处 leak。
- 建议：给定所有权语义（谁 `new` 谁 `delete`，或 `system_root` 析构里显式 `sub_child_ptr` + `delete`）。

### B3. `threads` 默认没有排队上限，通道表还会随 channel 号无上限增长
- 位置：`software/system/threads/threads_pool.h:105-106`（默认 `_allow_actions = xhqm::npos`）、`software/system/threads/threads_pool.cpp:403-436`（`has_capacity`）、`threads_pool.cpp:69-75`（`_ensure_channel_locked` 按 channel 号 `push_back` 补齐）
- 现状：`xhqm::npos` 是 `(size_t)-1`（`head.h`/`threads_task.h:82`），`has_capacity` 因此恒真 —— 默认构造的池可以无限接任务。通道表按「channel 号」索引，传 `channel = 100000` 会一次性建出 10 万个 `dqueue`。
- 证据：`threads_pool.h:106`（默认值）、`threads_pool.cpp:415-416`（`m_allow_actions == xhqm::npos || …`）、`threads_pool.cpp:69-75`（`while (m_channels.size() <= channel) push_back(...)`）。
- 影响：突发任务（网络回包、逐帧提交）会无界占住堆内存，直到 OOM 才被发现；`submit` 返回的 holder 只是排队，没有背压信号。channel 号来自「业务常量」时一般没事，若来自枚举/配置的稀疏编号就会白建一堆队列。
- 复现/验证：未复现（读码判定）。可复现：池默认构造后循环 `submit` 一百万次，观察 RSS 单调上涨。
- 建议：给默认 `allow_actions` 一个有限值（如 4096），channel 表改成 `map/unordered_map` 或对 channel 号做上限校验。

### B4. `process` 的析构是 `wait(10)`：悄悄把超时改成 10ms，必要时 SIGTERM→1s→SIGKILL 掉整棵进程树
- 位置：`software/system/process/process_base.h:69-70`（`process(const xhqm::count& millis = process::npos); ~process() { this->wait(10); }`）、`process_base.cpp:198-201`（`timeout()` 改写 `hander_ptr->millis`）、`process_base.cpp:650-676`（超时终止路径）
- 现状：`wait(millis)` = `timeout(millis); wait();` —— 析构里这一句会把构造参数/`timeout()` 设过的值**覆盖成 10ms**。若对象出作用域时子进程还在跑：10ms 后 `kill(-pid, SIGTERM)`（进程组）→ 睡 1s → 仍未退则 `SIGKILL`，然后 join 读线程、收退出码、置 `terminated/term_signal = 15`、`exec_error = timeout`。
- 证据：`process_base.h:70`、`process_base.h:102`（`wait` 是 `timeout(value); wait();`）、`process_base.cpp:646-675`。
- 影响：① 「让子进程继续跑、process 对象先出作用域」的写法会**静默杀进程**（长驻服务尤其危险）；② 析构可能要花 ~1s（宽限期）被阻塞，对延迟敏感的线程是意外停顿；③ 已经 `wait()` 过的对象析构是即时的（`active()` 为假），所以问题只在「没 wait 就离开作用域」这一种用法上。
- 复现/验证：未复现（未运行）。复现：`{ process p; p.command("sleep 30"); p.exec(); }` → 作用域结束约 1s 内子进程组被杀，`get_result()` 拿不到了（对象已亡），但 `ps` 可看到 `sleep` 消失。
- 建议：析构只做「有界等待 + 若还在跑就打日志」，或提供 `set_kill_on_destruction(bool)` 之类显式开关；至少别在析构里覆盖调用方设过的 timeout。

### B5. 写盘路径都不建目录：`packet::save` / `message_root::save` / `utility_root::save`，且落盘结果被丢弃
- 位置：`software/packet.cpp:36-40`、`software/message.cpp:38-40`、`software/utility.cpp:36-48`
- 现状：三处都是 `current_path() + "/packet|message|utility/..."` 直接 `ofstream`/`save_file`，从不 `create_dir`/`create_directories`；`datum::save_file` 的 `bool` 返回值在 `packet.cpp:39` 被直接丢掉（`message`/`utility` 只判了 `file.is_open()`，失败也无声）。而 `filesys::create_dir` 用的是 `create_directory`（父目录不存在即失败、已存在也返回 false，`filesys.cpp:119-127`），想「确保目录存在」还要自己逐级判断。
- 证据：`packet.cpp:38-39`；`message.cpp:38-40`（目录不存在 → `is_open()` 假 → 整批消息既不写、也不 `pop`，下次再试仍是同样结果）；`utility.cpp:36-37`；`filesys.cpp:119-127`。
- 影响：第一次部署/换工作目录后，存档、日志、工具清单**静默不落盘**，调用方拿不到任何失败信号（`packet::save` 是 `void`）。`test/system_test.cpp` 也不覆盖这三个路径（它只测 `filesys` 自己的 create/delete）。
- 复现/验证：未复现（读码判定）。复现：在空目录里 `packet::save()`，可见 `packet/` 目录不存在、也没有文件。
- 建议：三处 `save` 先 `std::filesystem::create_directories(parent)`，并把失败通过返回值/日志暴露出来。

### B6. `process` 的两个读线程并发写 `m_result`、共用同一个输出回调，还丢空行
- 位置：`software/system/process/process_base.cpp:518-560`（`thread_out`）、`:562-597`（`thread_err`）、`:121-137`（`consume_lines`，`:131` 丢空行）、`:511-515`（两线程启动）
- 现状：`stderr_stdout(false)` 时 `thread_out` 与 `thread_err` 是两个独立线程，二者都在写 `proc.m_result.exec_error` / `exec_msg`（`:529-530`、`:540-541`、`:573-574`、`:584-585`，`std::string` 赋值、无同步），也都会调 `proc.m_function_o`（`:552`,`:558` / `:590`,`:595`）—— 用户回调于是被两个线程同时进入；主线程可能正在 `wait()` 里读 `m_result`。另外 `consume_lines` 只推入 `!line.empty()` 的行（`:131`），各段的空行被丢掉。
- 证据：`process_base.cpp:529-530,540-541`（out 侧写）、`:573-574,584-585`（err 侧写）、`:552/590`（同一 `m_function_o`）；`:131`（`if (!line.empty())`）。
- 影响：① 读错误路径上的 `std::string` 竞争赋值是真实的 UB（现实中多为偶发错乱文本）；② 用户回调不是线程安全时（例如往 `std::string`/容器里追加）会撞车，而 API 没写这一点；③ 空行丢失让「行号对齐」类用法（日志比对、逐行协议）错位。
- 复现/验证：未复现（未运行）。TSan 下跑一个往 stderr 大量输出的命令（`stderr_stdout(false)`）应能报出 `m_result` 的竞争。
- 建议：`m_result` 的读错误字段改成原子/加锁；把 `function_o` 的并发语义写进头文件注释或令其串行化；空行保留（`if (capture) lines.push_back(line);` 不该带 empty 判断）。

### B7. 自定义 `target` 抛出的异常在 `_worker_loop` 里被静默吞掉
- 位置：`software/system/threads/threads_pool.cpp:221-231`
- 现状：worker 执行 `target_ptr->action()` 时用 `catch (...)` 兜住一切，注释写的是「holder 生成的 action 会自行捕获异常；防御自定义 target 泄漏异常」。也就是说：`holder` 路径的异常已经进了 `promise`（调用方 `future_ptr()->get()` 能拿到），但**手工构造 `parallel::target` 并经 `_submit_`（派生池）提交的任务**，其异常只留在 catch 里，谁也不知道。
- 证据：`threads_pool.cpp:221-231`；对照 `threads_task.h:236-248`（holder 的 action 自己 `set_exception`）。
- 影响：派生 `threads` 用自定义 target 的代码会把「任务失败」当成功；排障时日志里什么都没有。
- 复现/验证：未复现（读码判定）。
- 建议：至少把 `target` 的 `status` 置成 `failures` 并留一个可查询的错误通道（或加一个 `std::function<void(std::exception_ptr)>` 钩子）。

### B8. `pattern::command_history<multi_thread>::execute` 在锁外执行命令 → 撤销栈的顺序可能不是执行顺序
- 位置：`software/pattern/command.h:83-89`（`execute`）、`:91-104`（`undo`）、`:106-119`（`redo`）
- 现状：`execute` 先 `cmd.execute()`（锁外，故意避免长持锁），再进锁压栈。两个线程并发时，实际执行顺序与压栈顺序可以相反 —— 先执行完的线程可能后拿到锁。
- 证据：`pattern/command.h:85-88`；`undo/redo` 则是「锁内取命令、锁外执行、再锁内换栈」（`:93-103`、`:108-118`），同一时刻两个 `undo` / `undo`+`redo` 交错也会让两个 `std::function` 并发跑，栈内容与真实状态可能不一致。
- 影响：多线程下 `undo()` 的语义是「撤销最后压栈的那条」而不是「撤销最后执行的那条」；如果命令之间有依赖（先建后删），回滚顺序错会出错。单线程特化（默认）无此问题。
- 复现/验证：未复现（读码判定）。TSan/交错注入可复现。
- 建议：要么在锁内执行（接受长持锁），要么显式规定「多线程 command_history 只在单一线程执行命令」。

### B9. `instance_manger` 的全局注册表指针是无锁惰性初始化（数据竞争，后果有限）
- 位置：`software/pattern/instance.cpp:20-23`（`if(!instance_data) instance_data = new _instance_data();`）、`software/pattern/instance.h:19-25`（`instance<T>::get()` 里的函数内 static）
- 现状：`instance_data` 是裸全局指针，构造没有同步；两个线程同时首次进入 `instance_manger` 会各 `new` 一份，后写的覆盖先写的（前者泄漏，且可能丢掉已注册的实例）。`instance<T>::get()` 里的 `static void* classinstance` 本身是线程安全初始化的（C++11 保证），注册表内部也有 `m_instance_mutex`，所以竞争窗口只在全局指针这一处。
- 证据：`instance.cpp:20-23`；`instance.cpp:38-45`（`instance_manger` 内部才有锁）。
- 影响：只在「进程启动后多线程同时首次取两个不同单例」时可能丢注册/泄漏；因为 `regist<T>`/`getInstance` 调用几乎都发生在静态初始化或主线程里，现实中很难命中。注意这里是**故意不释放**（`new` 之后不 delete，避免静态析构顺序问题，见 `signal.cpp:8-9` 的同类注释），不要顺手改成 `static _instance_data` 而引入析构顺序风险。
- 复现/验证：未复现（读码判定）。
- 建议：改成函数内 `static _instance_data* data = new _instance_data();`（C++11 保证线程安全且仍然不析构）。

---

## C. 跨平台

### C1. `timesys::day/week/year/month` 只在 MSVC 存在：判据是 MSVC STL 的 `_HAS_CXX20`
- 位置：`software/system/timesys.h:31-36`
- 现状：`#if _HAS_CXX20` 包住 `using day/week/year/month = std::chrono::days/weeks/years/months;`。`_HAS_CXX20` 是 MSVC STL 的内部宏，GCC/libstdc++ 与 libc++ **都不定义**它，于是 Linux/macOS 上即便 `-std=c++20` 这四个别名也不存在（`_HAS_CXX20` 宏未定义时整块被跳过，且 `-Wundef` 之外不会有告警）。
- 证据（本机实测）：
  ```sh
  echo | g++ -std=c++20 -dM -E -x c++ - | grep -c "_HAS_CXX20"     # 0（未定义）
  printf '#include "software/system/timesys.h"\nint main(){ xhqm::timesys::day d(1); return (int)d.count(); }\n' \
    | g++ -std=c++20 -fsyntax-only -x c++ -I. -Isoftware -         # error: 'day' is not a member of 'xhqm::timesys'
  ```
- 影响：跨平台代码里用 `timesys::day` 之类只在 MSVC 上编得过 —— 典型的「本机能过、CI/他机报错」；而上游（`xhqm/threads`、nebula）目前没人用它，所以还没爆。
- 复现/验证：**已复现**（上两条命令）。
- 建议：改成 `#if __cplusplus >= 202002L`（或 `__cpp_lib_chrono >= 201907L`）。

### C2. MinGW：`signal.h` 既不 `include_next`（`_WIN32` 被排除），也享受不到 CMake 给 MSVC 的头目录兜底
- 位置：`software/signal.h:9`（`#if !defined(_WIN32) && !defined(_MSC_VER) && (defined(__GNUC__) || defined(__clang__))`）、`CMakeLists.txt:51-75`（`if(MSVC)` 才把 UCRT 头目录前置）
- 现状：这条 `#if` 把「MinGW」（`__GNUC__` 且 `_WIN32`）判成「不需要处理」：既不 `include_next <signal.h>`、也不引入 `pattern/instance.h`；而 CMake 的规避手段（把真正的系统 `signal.h` 目录排到 `-I` 最前）只在 `MSVC` 分支里做。于是 MinGW 上本文件仍然遮蔽系统头，且不会把真头拉进来 → 同 MSVC 当年那种 `SIGTERM`/`signal()`/`raise` 缺失的失败形态。
- 证据：`signal.h:9`；`CMakeLists.txt:51-75`（`if(MSVC)`）+ `:95`（`include_directories("./software/")`，非 MSVC 平台无条件把 `software/` 放进搜索路径）。
- 影响：只在用 MinGW 构建时才暴露（本机无 MinGW，**未复现**）；一旦发生，报错位置同样落在系统头的使用者身上。
- 复现/验证：未复现（无 MinGW 工具链）。
- 建议：把判据从 `_WIN32` 改成 `_MSC_VER`（MinGW 有 `include_next`），或在 MinGW 分支里补 `#include_next`。

### C3. `codesys` 的 `std::wstring_convert` / `std::codecvt_utf8` 自 C++17 起被标准弃用
- 位置：`software/system/codesys.cpp:94-102`（`unicode_to_string` / `string_to_unicode`）、`:3-4`（`<locale>`、`<codecvt>`）
- 现状：两个转换函数用 `std::wstring_convert<std::codecvt_utf8<wchar_t>>`，C++17 起该整套设施标记为 deprecated；本机 GCC 11.4 实测**不产生任何告警**（含 `-Wdeprecated-declarations`），所以现状是「编得过、也没有提示」，风险留到换工具链时。注意 `:173-183` 的注释解释了另一处（`codecvt<wchar_t,char>` 构造器在 libstdc++ 下 protected），Linux 因此直接原样返回。
- 证据：`codesys.cpp:96,101`；`printf … | g++ -std=c++17 -fsyntax-only -Wdeprecated-declarations -x c++ -I. -Isoftware - | grep -c deprecated` → 0。
- 影响：纯前向风险：`<codecvt>` 在未来标准/工具链里被移除时，`test/system_test.cpp` 里那条 UTF-8 ↔ `wstring` 往返用例会先编译失败（它是该功能唯一的用例）。
- 复现/验证：**已复现**（本机零告警这一点）；弃用本身是标准事实。
- 建议：迁移到 `MultiByteToWideChar`/`WideCharToMultiByte`（Windows）与手写/平台 API（POSIX），或至少加注释标注迁移点。

---

## D. 性能

### D1. `filesys::dir_list`/`file_list` 逐项 `status()`，`path_list` 还会把同一批条目列两遍
- 位置：`software/system/filesys.cpp:41-52`、`:53-64`、`:65-74`
- 现状：两个列举函数都是「构造 `directory_iterator` → 对每个 entry 调一次 `ite.status()`（一次 `stat` 系统调用）→ 过滤」；`path_list` 直接调用 `dir_list` 再调用 `file_list`，对同一目录做**两次**完整遍历 + 两次 `stat` 风暴。
- 证据：`filesys.cpp:46-49`（dir）、`:58-61`（file）、`:69-72`（两次调用）。
- 影响：目录项上千（插件目录、缓存目录、`file_list(".")` 之类的误用）时开销线性放大；`test/system_test.cpp` 只在临时目录里跑了 1 个文件，感觉不到。
- 复现/验证：未复现（读码判定）。
- 建议：`path_list` 用一次遍历按 `is_directory()` 分桶；需要类型时用 `entry.is_directory()`（缓存了 `status`）而不是重新 `status()`。

### D2. `execute_bat` 的线程数完全等于调用方给的 `max_concurrent`，没有上限也没有与任务数取小
- 位置：`software/system/execsys.cpp:34`（`for (xhqm::size i = 0; i < max_concurrent; ++i)`）
- 现状：`max_concurrent = 1000` 且只有 3 条命令时，也会起 1000 个线程（其中 997 个立刻 `fetch_add` 拿到越界下标后退出）。`threads` 池有 `m_allow_threads` 上限，这里没有。
- 证据：`execsys.cpp:31-54`；对照 `software/system/README.md:45` 说它是「`std::atomic` 下标分发 + 工作线程」——没错，但没说线程数就是入参值。
- 影响：配置里把并发度写成大数（或 `hardware` 侧的核数探测异常偏大）会瞬间创建大量线程 → 内存/调度开销，且短命线程的创建销毁本身就比干活贵。
- 复现/验证：未复现（读码判定）。
- 建议：`workers = std::min(max_concurrent, commands.size())`，并给它一个硬上限。

### D3. `signal::emit` 每次都整体拷贝槽表（`std::function` + `shared_ptr`），即使没有任何 queued 槽
- 位置：`software/signal.h:137-143`
- 现状：`emit` 的第一件事是 `snapshot = m_slots;`（在锁内做一次 `std::vector<slot>` 全量拷贝，slot 里含 `std::function`、`shared_ptr`、`thread::id`）。这样做的收益是「槽回调里可以安全 connect/disconnect」（正确性），代价是**每条信号每次发射**都要付一次分配 + N 份 `std::function`/`shared_ptr` 引用计数的钱。
- 证据：`signal.h:139-143`；`slot` 定义 `signal.h:57-63`。
- 影响：高频信号（逐帧、逐包）在槽很多时会有可测量的分配压力；`network/nebula` 的 `frame_signal`/`broad_signal` 是逐帧级信号。仓内暂无基准测试量化（`test/` 里没有 signal 的用例）。
- 复现/验证：未复现（未做基准）。
- 建议：只在「存在 queued/waited 槽」或「正在派发」时才拷贝（例如加一个 `m_emitting` 计数 + 只在需要时快照），或改用 `shared_ptr<const vector<slot>>` 的 COW 快照。

---

## E. 未实现 / 占位桩

> 本组全是「现在编译器会过、只是没写完或没接上」的东西。

### E1. `software::command` / `command_root` 的 save/read 与 `command_root` 的 redo/undo 都是空函数体
- 位置：`software/command.cpp:36-42`（`command::save/read`）、`software/command.cpp:65-85`（`command_root::redo()`、`redo(command*)`、`redo(std::string)`、`undo()`、`save()`、`read()`）
- 现状：六个函数（外加三个重载）函数体全空，返回类型 `void`，调用方拿不到任何「没实现」的信号。类里也没有 redo/undo 栈成员（`command.h:34-49` 只有继承来的 packet 状态）。
- 证据：`command.cpp:65-85`（逐行为 `{ }`）、`command.h:43-48`（接口声明）。
- 影响：把 `command_root` 当命令容器用的代码（含 `system_root::get_command_root()` 的调用者）全部静默无操作。
- 复现/验证：未复现（读码判定；`grep -rn "get_command_root" .` → 除定义外 0 命中）。
- 建议：要么实现，要么让这些接口返回 bool/抛 `xhqm::error("not implemented")`。

### E2. `project_root` / `message_root` / `utility` 的 save/read 也是空壳
- 位置：`software/project.cpp:24-30`、`software/message.cpp:54-57`、`software/utility.cpp:17-23`（`utility::save/read`）
- 现状：全部空函数体。对照之下 `message_root::save`（`message.cpp:34-53`）与 `utility_root::save/read`（`utility.cpp:34-69`）是有实现的。
- 证据：`project.cpp:24-30`、`message.cpp:54-57`、`utility.cpp:17-23`。
- 影响：工程容器的持久化完全不存在；消息队列没有「读回历史」的能力（只有单向写日志）。
- 复现/验证：未复现（读码判定）。
- 建议：同上（实现或显式拒绝）。

### E3. `utility_root::read` 的插件加载链只有一个 `int a = 0;` 占位
- 位置：`software/utility.cpp:50-69`（占位在 `:64`）
- 现状：读出第一条 uuid → `libmange::loadlib(current_path() + "/utility/" + uuid)` → `libmange::function(id, "create_object")` → 拿到函数指针后 `if (make_utility) { int a = 0; }` 结束：**不构造对象、不挂进对象树、不调 `enter()`**。此外 `file >> uuid` 只取第一个 token（多行清单里后续 uuids 被忽略，`:56`），且这条路径上没有 `libid` 的所有权管理（加载进来的库句柄没有交给 `_libary`，`:60`）。
- 证据：`utility.cpp:52-66`；`utility.h:19-23`（`utility::enter/leave/rece` 是纯虚，意味着任何被创建的工具都必须实现它们）。
- 影响：插件的「读清单 → 加载 → 实例化 → 入树」链路只完成了前一半，任何依赖 `utility_root::read()` 恢复插件的宿主都拿不到工具对象；而 `utility_root::save()` 写出的清单（`:40-45`）看起来是完整的，容易让人以为闭环了。
- 复现/验证：未复现（读码判定；`grep -rn "utility.list" .` 只命中这两处）。
- 建议：补齐 `create_object` 调用 + `object_type` 校验 + `push_utility` + `enter()`，并把句柄交给 `xhqm::libary` 管理。

### E4. `threads_task.h` 里那套 `#ifndef XHQM_HEAD` 兜底定义永远不生效（死代码）
- 位置：`software/system/threads/threads_task.h:4`（无条件 `#include "../../../head.h"`）、`:24`（`#ifndef XHQM_HEAD`）、`:24-92`（兜底的类型别名/常量/`xhqm::error` 定义）、`:91`（`#else #include "../../../algebra/value.h"`）
- 现状：同文件第 4 行已经无条件包含 `head.h`，而 `head.h:1-2` 第一件事就是 `#define XHQM_HEAD`；所以到第 24 行时 `XHQM_HEAD` 必然已定义，`#ifndef` 块**恒不成立**，`#else` 分支（`algebra/value.h`）才是唯一会被执行的那一支。
- 证据（编译探针，本机）：`printf '#include "software/system/threads/threads_task.h"\nxhqm::sure s = 1.0;\nint main(){return 0;}\n' | g++ -std=c++17 -fsyntax-only -x c++ -I. -Isoftware -` → `error: 'sure' in namespace 'xhqm' does not name a type`（`sure` 只定义在兜底块里）；先包含 `head.h` 再包含它同样 0 error / 无差异。
- 影响：死代码本身无害，但它是一颗哑弹 —— 一旦有人挪走 `:4` 的 include（或把 `head.h` 改成条件包含），这套自制定义就会与 `head.h` 的类型别名并存（`coord`/`topol`/`shape` 在兜底块里是 `std::vector`、在 `head.h` 里是模板别名，语义不同），届时才会炸。
- 复现/验证：**编译探针已复现**（上面那条）。
- 建议：删掉 `:24-92` 的兜底块（保留 `:4` 的 include），或把它换成 `#error` 提示。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. POSIX 侧用 `fork` + `execvp` 自己搭管道，而不是 `posix_spawn`
- 位置：`software/system/process/process_base.cpp:416-505`（管道 + `fork` + 子进程里 `setpgid/dup2/chdir/setenv/execvp`）
- 为什么现在这样是对的：引擎要的是「能起得来也停得掉」的进程封装 —— 子进程必须先 `setpgid(0, 0)` 独立成组（`:455`，父进程 `:497` 再补一次避免竞态），超时/SIGTERM 才能 `kill(-pid, …)` 杀掉**整棵**进程树（`:660,664,784,798`）；这套语义用 `posix_spawn` 表达要绕（需要 `POSIX_SPAWN_SETPGROUP` + 手工处理 fd 继承），而当前实现把三端管道、工作目录、环境变量、退出码全都收在一个函数里，读起来是一处能改的地方。
- 代价：子进程里只允许调用 async-signal-safe 的接口，而这里在 `fork` 之后调了 `setenv`（`:478-481`）、构造 `std::vector<char*>`/`std::string` 相关操作并最终 `execvp`（`:484-492`）—— 在多线程宿主里，若 `fork` 那一刻别的线程正持着 malloc/环境锁，子进程可能死锁（`execvp` 之前的任何分配都可能卡住）。本机**未复现**（没构造出那种锁竞态）。
- 复现/验证：未复现。理论复现路径：多线程宿主里高频分配 + 高频 `process::exec()`，偶发子进程卡住。
- 建议：不修（若真要动，是把 `setenv` 换成预先算好的 `envp` 数组 + `execvpe`，属于另一轮工程）。

### F2. （已移交子模块台账）`observer::change` 的 `signal` 是「按位拷贝 8 字节」，不是函数指针的完整值
- 位置：`software/pattern/observer.h:22,27-37,84-98`（`changed_type = void*`，`std::memcpy(&signal, &function, sizeof(changed_type))`）
- 现状：本条目的细节由 **`software/pattern/DEBUG.md` A1** 负责 —— 那个文件实测了三种实参的尺寸（无捕获 lambda 1 字节、普通函数指针 8 字节、**成员函数指针 16 字节**），结论是**截断即缺陷**，而不是「设计取舍」。此前本文件把它归到 F 组（取舍）是判错了组：`pattern/README.md` 的示例 `change(&Button::press)` 恰好就是 16 字节的成员函数指针。
- 证据：`software/pattern/DEBUG.md` A1（含实测尺寸与包含链）。
- 影响：见该文件；本文件不再复述。
- 复现/验证：见该文件。
- 建议：按该文件的 A1 修（本文件只保留「别把它当纯取舍」这条订正）。

### F3. `datum` 的读写接口用「指针的引用」（`const T*&` / `T*&`）与 `std::function` 间接层
- 位置：`software/datum.h:46-47`（`sptr/rptr` 类型）、`:95-97`（`save_json(const xhqm::datum*&, std::string&)`）、`:142`（`read_data(const std::string&, type*&)`）、`:272-274`（`resigt` 的可选回调）
- 为什么现在这样是对的：`datum` 是「名称 → 任意类型的就地指针」的注册表，注册时只存 `void*` + 两个 `std::function`，靠统一签名把「所有类型」收敛到同一张表（`packet` 的元数据、`m_attribute` 都靠它），也允许调用方传自定义 `sptr/rptr` 覆盖默认序列化。指针的引用是这套统一签名里传递「指针本身」的必然结果（见 `read_json` 要能改写调用方的指针 `:97`、`:170`）。
- 代价：① 每个字段的每次读写都走一次 `std::function` 间接调用（`datum.h:274,311` 等），无法内联；② 签名反直觉（用户得写 `const xhqm::datum* ptr = &d; save_json(ptr, json);` 而不能直接传 `&d`）；③ 类型信息只剩一个枚举标签，错配只能运行期发现（正是 A5/A6/A7 的温床）。
- 复现/验证：编译探针已验证「按签名传左值即编译通过」（`software/README.md` 的用法示例）。
- 建议：不修（同一轮里别动别人的调用点）；新增接口时优先考虑 `template<typename T> save(T&)` 的编译期版本。

### F4. `threads` 的空闲非核心线程「自己退出 + 提交路径顺手回收」，没有集中回收线程
- 位置：`software/system/threads/threads_pool.cpp:171-174`（空闲到期 `break` 退出）、`:238-245`（收尾时 `--m_live_workers`）、`:248-270`（`_reap_closed_workers`，在 `life/join/dath/_submit_` 里被调）
- 为什么现在这样是对的：回收动作放在调用路径上（而不是守护线程）就省掉了「谁在什么时候扫表」的额外同步，且 `_reap_closed_workers` 明确「离开池锁后再析构并 join」（`:268-269`，避免与退出路径互锁）；核心线程是 `milliseconds::max()` 永不超时（`:84`），所以稳定态不抖。
- 代价：已退出的 worker（`std::thread` 已结束但对象还在 `m_workers` 里）要等下一次 `_submit_`/`life`/`join`/`dath` 才被 join 回收。若池长期空闲且期间反复「扩到 8 线程 → 全部超时退出」，峰值期占用的线程对象会短暂滞留（内存是线程栈已释放的 `worker` 结构，量很小），`threads_state()` 之类的计数也只在下次提交时才归位。
- 复现/验证：未复现（读码判定）。
- 建议：不修。若要做诊断，可加一个显式的 `reap()` 公开接口给监控用。

### F5. `signal.h` 与系统头同名、靠 `include_next` 兜底（而不是改名/挪目录）
- 位置：`software/signal.h:1-13`（文件名与 guard）、`CMakeLists.txt:95`（`include_directories("./software/")`）
- 为什么现在这样是对的：`#include "software/signal.h"` / `#include "signal.h"` 两种写法都能命中（上层与 stella 的调用点用的是后者），且非 Windows 编译器上 `#include_next <signal.h>` 能把真正的系统头拉回来，于是「只用系统信号」的 TU 不受影响（本机实测 `<signal.h>`、`<csignal>` 单独包含均 0 error）。改名（如 `siglslot.h`）会波及所有既有调用点与上层树。
- 代价：**包含顺序敏感**（A13：先 `<sys/wait.h>` 即 5091 error），且 MSVC 侧要 CMake 特判 UCRT 头目录、MinGW 无兜底（C2）。这条代价是实打实的：它让「编译失败」出现在与 `signal.h` 无关的文件里。
- 复现/验证：已复现（A13 的两条命令）。
- 建议：不修（先按 A13 的「把 C++ 头挪出 `extern "C"` 区间」做最小加固即可，不必改名）。

### F6. `notifier` 只接受函数指针，不支持成员函数/`std::function`
- 位置：`software/pattern/notifier.h:20-21,29-40`（`function_ptr` / `function_std` 来自 `metaclass/function/function_type.h`）
- 为什么现在这样是对的：注册表里存 `pair<receiver, function_ptr>`，并用**函数指针本身**做 `remove` 的键（`:36-39`）；只要指针，就能零成本比较与去重，也不引入「对象生命周期归谁管」的问题（`observer` 那条路才有 `regist/remove` 的对象指针）。测试也顺着这个模型写：`test/pattern_test.cpp` 用全局函数 + 全局累加器（`g_sum`/`g_atomic_sum`）。
- 代价：想通知「某个对象的成员函数」时只能靠全局状态或手写 `std::function` 适配层（`receiver(std::function)` 只在内部构造，公开接口收的是函数指针）；无法按对象注销。
- 复现/验证：编译探针与 `test/pattern_test.cpp` 的用法一致（`n.regist(&fn)` 编译通过）。
- 建议：不修；需要对象语义时用 `xhqm::observer`（它的 `regist(subject*)`/`remove(subject*)` 就是为此准备的）。

---

## G. 文档与代码的出入

### G1. `software/README.md` 本轮订正的旧说法（本次已改，留档备查）
- 位置：`software/README.md`（本轮修改处，逐条都带 2026-10-07 作废标注）
- 现状：以下旧表述与代码不符，已在 README 原处用 `~~旧文~~ **已作废（2026-10-07 复核 r677）**：<事实>` 的形式订正，未直接删除历史：
  1. `localtime()` 返回 `"YYYY-MM-DD HH:MM:SS"` → 实际是 `"YYYY/M/D H:M:S:mill"`（`timesys.cpp:46-52`，与子文档 `software/system/README.md:122` 一致）。
  2. 「`save_list` 对空列表会产生 `[]`」→ 实际产出一个 `]`（A2）。
  3. datum 示例注释 `{"a":1,"b":true,"v":[1.0,2.0,3.0]}` → 浮点走 `std::to_string`，实际 `1.000000` 形式（`datum.h:135`）。
  4. 「异线程槽默认走全局单例 `pool()`」→ 池必须由宿主 `pool_life()` 显式安装，否则抛异常（A15 / 根 `DEBUG.md` B1）。
  5. 「`signal.h` 遮蔽系统头依赖 `include_next`，（只需）注意遮蔽行为」→ `include_next` 确实生效（直接 include 安全），真正的坑是**包含顺序**（A13）。
  6. 「对象析构即自动断开……不会悬垂」→ 只对同线程 `direct` 槽成立（B1）。
  7. 「无单元测试/示例工程」→ 有 `test/system_test.cpp`、`test/pattern_test.cpp`、`test/pattern_extended_test.cpp` 及对应 CMake 目标（`test/CMakeLists.txt:178-198`），只是没有 `add_test`/CI。
  8. 「无顶层聚合头」→ 模块自身确实没有 `software.h`，但仓库级聚合头 `xhqm.h`（`XHQM_USE_SOFTWARE`）→ `xhqm/software` 覆盖了本模块（缺 `software/system.h`，见 G4）。
  9. 目录结构图漏了 `system/threads.h`、`system/process.h` 两个聚合头（已补）。
- 证据：`git`/`svn diff software/README.md` 可见本轮全部改动；每条订正后的事实都能在本文对应条目里找到行号。
- 影响：接手者此前会照着错说法写代码（尤其 1/2/4/5 四条会直接导致错误结论）。
- 复现/验证：读 README 中带「已作废」的段落 + 本文对应条目。
- 建议：继续保持「只订正不删除」的写法；下一轮若代码被修好，改的是这里的行号与状态，而不是撤掉历史。

### G2. `software/system/README.md` 与代码的出入：同批的子模块复核已自行订正（本文件不再登记）
- 位置：`software/system/README.md`（现 `:20`、`:36-37`、`:47`）与 `software/system/DEBUG.md`
- 现状：本文件上一稿曾记「system 子文档四处与代码不符（locale 透传、`CreateProcessA`、`localtime` 格式、`run_time()` 示例）」。**同批建立的 `software/system/DEBUG.md` 已在它自己的 README 里把这三处改掉了**：`:47` 的 Windows 侧写明 `CreateProcessW` + `CREATE_*` 标志 + Job 整树回收、`CreateProcessA` 一句已用 `~~…~~ **已作废（2026-10-07 复核 r677）**` 标注；`:37` 的 `run_time()` 示例同样标作废并给出回绕结论；`:20` 改成「`utf8_to_system` 在 Linux/macOS 原样返回、`system_to_utf8` 非 Windows 分支无 locale 判定」；`:122` 的 `localtime` 示例（`"2025/9/9 1:33:0:123"`）本来就与代码一致。本文件因此把 G2 降为「指过去」，避免两个台账各记一份同名条目。
- 证据：`grep -n "CreateProcess\|run_time\|已作废" software/system/README.md` → `:37`、`:47` 两处作废标注在位；`software/system/DEBUG.md` 的 A1/B3/F5 等条目为对应细节。
- 影响：无遗留核对项；本模块 `README.md` 里对 `localtime` 格式的订正（G1-1）与子文件现已一致。
- 复现/验证：读两个文件即可（本轮已逐行对照）。
- 建议：模块级 README/DEBUG 与子模块台账的重叠条目，一律按本文件开头那张对照表指过去。

### G3. `xhqm_regist` 宏的第三个参数 `_regist` 从未被使用
- 位置：`software/packet.h:127`（`#define xhqm_regist(type, space, _regist) namespace space { xhqm::regist<space::type> class_##type(#space "::" #type); }`）、`:128`（被注释掉的旧版本）、调用处 `software/packet.cpp:50`（`xhqm_regist(packet, xhqm::software, );`，第三参留空）
- 现状：宏体里只用到 `type` 与 `space`，`_regist` 一个字都没引用；调用处因此可以传空实参。注册键是全限定名字符串 `#space "::" #type`，与 `object_type::name<T>()`（`packet.h:84-87`）返回的 `regist<T>::regist_type` 一致。
- 证据：`packet.h:127` vs `:128`（历史版本里第三个参数是拼接用的 `XHQMCONNECTSTR(space::,type)`）；`grep -rn "xhqm_regist" .` → 只有 `packet.cpp:50` 一处调用，第三参为空。
- 影响：读宏的人会以为第三个参数是「注册表对象」之类，从而纠结该传什么；IDE 也无法提示。功能上无影响。
- 复现/验证：编译探针（`#include "software/packet.h"` 单独包含 0 error）。
- 建议：删掉第三个形参（或改成默认参数），把 `:128` 的注释行一并清理。

### G4. 文件集/清单对不上的两处：`xhqm/software` 缺 `software/system.h`；CMake 源清单没收 6 个 pattern 头
- 位置：`xhqm/software`（23 行转发，逐行 `#include "../software/..."`）；`CMakeLists.txt:494-517`（`XHQM_HEAD_SYSTEM`，`:571` 并入 `XHQM_HEAD`）
- 现状：① 聚合头列出了 `datum/event/signal/packet/message/command/project/utility`、`system/{timesys,filesys,codesys,process,execsys,libmange}`、`pattern/{notifier,observer,instance,thread_mode,factory,strategy,command,chain,state_machine}`，**唯独没有 `software/system.h`** —— 而 `#define XHQM_USE_SOFTWARE` + 根 `xhqm.h` 是官方推荐的聚合入口，走它拿不到 `system_root`（四类容器的唯一入口）。② CMake 的 `XHQM_HEAD_SYSTEM` 没列 `software/pattern/chain.h`、`command.h`、`factory.h`、`state_machine.h`、`strategy.h`、`thread_mode.h`（纯头文件不影响编译，但 IDE/安装清单会看不到这 6 个文件）；同批核对过：清单里列的 40 个 `software/*` 文件**全部存在于磁盘**，没有悬空引用。
- 证据：`cat xhqm/software`（无 `system.h` 行）；`grep -n "XHQM_HEAD_SYSTEM" CMakeLists.txt`（`:494`、`:571`）；脚本比对 CMake 清单 vs 磁盘（40 命中 / 6 未列）。
- 影响：`xhqm/software` 的使用者会缺 `system_root` 并困惑「四类容器从哪来」；只影响可发现性与安装清单，不影响编译。
- 复现/验证：**已复现**（读文件 + 清单比对脚本）；编译探针 `#define XHQM_USE_SOFTWARE` + `#include "xhqm.h"` → 0 error（但 `xhqm::software::system_root` 在该 TU 里不可见）。
- 建议：往 `xhqm/software` 补一行 `#include "../software/system.h"`，并把 6 个 pattern 头补进 `XHQM_HEAD_SYSTEM`。

---

## 附：本轮未核实 / 未复现的项（留给下一轮，别当成结论）

- `libmange::function(id, name)` 与 `has_function` **不校验 `id`**：`loadlib` 失败返回 `nullptr` 后，`_libary::function(...)` 会把空句柄原样传给 `dlsym`（`libmange.cpp:132-139`、`libmange.h:19-26,90-94`）。glibc 把 `NULL` 当 `RTLD_DEFAULT`（全局符号查找）还是直接返回错误，本机**未运行验证**；若前者成立，失败的加载会变成「在全局作用域里找同名符号」，是静默错符号的温床。
- `process::input()` 只调一次 `write`/`WriteFile`（`process_base.cpp:238-243`），不处理部分写与 `EINTR`；短写时返回错误字符串但不重试。未复现。
- `codesys::FILE_BINtoBXT` 对 0 字节输入会把 `fgetc` 的 `EOF(-1)` 当数据写出 `FF`；`hexa2string` 对奇数长度的输入会拿 `asistr[len]`（`\0`）。两者都是畸形输入下的产物，未复现（`test/system_test.cpp` 只覆盖了内存里的 `string2hexa/hexa2string` 往返）。
- 模块自身的测试（`test/system_test.cpp`、`test/pattern_test.cpp`、`test/pattern_extended_test.cpp`）本轮**没有构建也没有运行**；因此 A/B/C 各组里凡涉及运行时行为的条目，状态都是「读码判定」或「编译探针」，不是「跑出来的」。
- 本机编译探针的固定命令行（供复核）：`g++ (Ubuntu 11.4.0-1ubuntu1~22.04.3) -std=c++17 -fsyntax-only -x c++ -I. -Isoftware -`，工作目录 `xhqm-v2`（glibc 2.35）。除 `software/event.h` 与「先 `<sys/wait.h>`」两种情形外，30 个模块头单独包含均 0 error。
