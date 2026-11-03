# software/system · 当前问题清单

`software/system`（操作系统抽象层：编码 / 文件 / 时间 / 进程 / 动态库 / 线程池）的**当前仍存在**的问题台账：只记「现在还是坏的 / 还是没写的 / 还是对不上的」，每条给出可核对的 `文件:行号`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**：本仓的历史台账**不搬家、不改名** —— `network/nebula/DEFERRED.md`（nebula 讨论过但决定不做/没排期的事项）保持原位，本文件只指过去、不复制内容：其 **B3** 记录的「池满即 `_cancel_target`，`life_event` 返回 void ⇒ 静默丢事件」就是本文件 **B1** 的调用方视角。`fantasy/fantasy_bugs.md` 与 fantasy 侧无关。模块的定位/优势/缺失/用法在 `README.md`；全仓层面的问题在根 `DEBUG.md`。
>
> 判据纪律：每条都标了验证方式。凡写「编译不过 / 某 API 不存在 / 某一行为如何」，均附复现命令与验证版本（本轮实测环境：g++ 11.4.0 / Ubuntu 22.04 / `-std=c++17`；源码全部通过 `g++ -std=c++17 -fsyntax-only -I. <file>`，**没有编译错误** —— 也就是说下面这些都不会被编译器拦住）。跨平台条目只在 Linux 侧复核，Windows 侧一律标明「未复现」。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `timer::run_time()` 把「已耗时」算成了无符号回绕的垃圾时刻
- 位置：`timesys.cpp:70-74`（关键在 `:72`，声明在 `timesys.h:106`）
- 现状：`run_time()` 调的是 `systemtime((this->runtime() - this->m_starns) / 1000000)`。`runtime()` 本身已经是 `ntime() - m_starns`，再减一次 `m_starns`（构造时记下的 epoch 纳秒）必然越界；`m_starns` 与返回值都是 `xhqm::natural` = `std::uintmax_t`，无符号回绕不报错。
- 证据：`sed -n '70,74p' software/system/timesys.cpp`；模拟同算式（`(1<<64)+runtime-start`）得：实际耗时 10 ms 被打印成 `5:58:51.18`，而正确写法 `systemtime(runtime()/1000000)` 得到 `0:0:0.10`。`README.md` 旧示例写的 `t.run_time() == "00:00:01.234"` 即此处的错说法（本轮已作废标注）。
- 影响：任何用 `run_time()` 写日志/上报耗时的代码都拿到无意义的 `时:分:秒.毫秒`（随调用时刻漂移）。本仓当前 0 个调用点（`grep -rn "run_time()" --include=*.cpp --include=*.h .` 只命中定义处），所以是**埋着的坑**而不是正在流血。
- 复现/验证：未复现（无编译前提；结论来自逐行读数 + 同算式模拟）。
- 建议：把 `:72` 的 `(this->runtime() - this->m_starns)` 改回 `this->runtime()`。

### A2. `FILE_BINtoBXT` ↔ `FILE_BXTtoBIN` 格式约定互不匹配，往返必然错位
- 位置：`codesys.cpp:51-70`（写侧，`fwrite` 在 `:60`）与 `codesys.cpp:72-91`（读侧，三次 `fgetc` 在 `:79-81`）
- 现状：写侧每读 1 字节用 `tosl` 生成 2 个十六进制字符，`txtdata[2] = ' '` 被赋成空格后却只写 2 字节（`fwrite(txtdata, 2, 1, bxt)`）——**分隔符从未落盘**；读侧却按「2 位十六进制 + 1 位分隔符」每轮消耗 3 个字符。
- 证据：模拟两侧字节级语义（Python 复刻）实测：`41 → 41`（1 字节恰好对）；`ab cd → ab d8`；`aa bb cc → aa bc`；10 字节输入 → 7 字节输出。两函数在本仓**无调用点**（`grep -rn "FILE_BINtoBXT\|FILE_BXTtoBIN" .` 只命中声明、定义与文档），所以从未被人踩到。
- 影响：拿它做二进制 ⇄ 文本存档的调用方会静默得到损坏数据（不报错、返回值还是 `true`）。
- 复现/验证：未复现（未落盘编译测试）；可用 3 字节二进制文件跑一次「BIN→BXT→BIN」比对字节数自证，或按 `codesys.cpp:60` 与 `:79-81` 的 2/3 字符不对称直接判定。
- 建议：二选一改齐（写侧写 3 字节带分隔符，或读侧每轮只读 2 字符）。

### A3. 两个十六进制转换函数 `fopen` 不判空，目标不可写时 `fwrite`/`fputc` 空指针
- 位置：`codesys.cpp:56`（`bxt = fopen(BXTpath.c_str(), "w")`）、`codesys.cpp:77`（`fil = fopen(BINpath.c_str(), "wb")`）
- 现状：只检查了**入参路径**能否打开，输出文件句柄从不判 `NULL` 就交给 `fwrite`/`fputc`（`:60`、`:85`），`fclose` 也不判空（`:67-68`、`:87-88`）。
- 证据：`sed -n '51,91p' software/system/codesys.cpp`，全段无 `== NULL` / `!= NULL` 检查。
- 影响：目录只读、磁盘满、路径非法时不是返回 `false`，而是段错误（glibc 下 `fwrite(NULL)` 直接崩）。
- 复现/验证：未复现；构造 `FILE_BINtoBXT("/etc/hostname", "/proc/1/x")` 类调用即可触发（无需编译也可由代码判定）。
- 建议：两处 `fopen` 后补判空并返回 `false`。

### A4. `filesys::create_file` 无条件返回 `true`，创建失败也报成功
- 位置：`filesys.cpp:128-138`（关键在 `:133-135`）
- 现状：`std::fstream file(path, std::ios::out | std::ios::binary); file.close(); return true;` —— 流是否打开成功、`close` 是否成功都没看；只有「路径已存在」这一种情况返回 `false`。
- 证据：`sed -n '128,138p' software/system/filesys.cpp`；同文件其它创建类函数（`create_dir`）是 `return std::filesystem::create_directory(path);`，口径不一致。
- 影响：父目录不存在 / 无权限 / 磁盘满时，调用方以为文件建好了，后续写入落空；`test/system_test.cpp:90` 的断言只覆盖成功路径。
- 复现/验证：`filesys::create_file("/nonexistent_dir/x.txt")` 返回 `true`（未编译运行，由代码路径直判）；也可用 `test/system_test.cpp` 的 filesys 用例加一行反例。
- 建议：改成 `return file.is_open();`。

### A5. `filesys::work_directory()` 与 `path_directory()` 是同一份实现，返回的不是工作目录
- 位置：`filesys.cpp:276-312`（`work_directory`）对 `filesys.cpp:238-274`（`path_directory`）；声明在 `filesys.h:28-30`
- 现状：两个函数**逐行相同**：Win 取 `GetModuleFileNameW`、Linux 读 `/proc/self/exe`、macOS `_NSGetExecutablePath` + `realpath`，都取 `parent_path()`，都缓存在各自的函数内 `static fs::path`。也就是说 `work_directory()` 返回的是**可执行文件所在目录**，与进程当前工作目录无关（后者是 `current_path()`，`:314-317`）。
- 证据：`diff <(sed -n '238,274p' software/system/filesys.cpp) <(sed -n '276,312p' software/system/filesys.cpp)` 只输出 1 行差异（函数名 `path_directory` ↔ `work_directory`），函数体逐行相同；`grep -rn "work_directory" --include=*.cpp --include=*.h .` 除本模块外 **0 调用点**。
- 影响：接手者按名字用 `work_directory()` 做相对路径基准，会得到 exe 目录 —— PowerShell/服务化启动时通常与期望的 CWD 不同。当前无人调用，属埋坑。
- 复现/验证：`grep -rn "work_directory\|path_directory" --include=*.cpp --include=*.h .`（本仓 0 调用点）。
- 建议：`work_directory()` 改返回 `fs::current_path()`（或在文档里明确它等价于 `path_directory()`）。

### A6. `execsys::execute_bat(commands, 0)` 一条命令都不执行，却返回「全部成功」
- 位置：`execsys.cpp:29-60`（工作线程创建在 `:34-54`）
- 现状：循环 `for (xhqm::size i = 0; i < max_concurrent; ++i)` 建线程；`max_concurrent == 0` 时 0 个线程，随后 `results` 原样返回 —— 而 `results` 是 `std::vector<process::result>(commands.size())`，默认构造的 `result.exit_code == 0 && exec_error == none`，即 `success()` 为 `true`。
- 证据：`sed -n '29,60p' software/system/execsys.cpp`；`process_base.h:37-48` 的默认值。
- 影响：并发度取自配置/CPU 数的上层代码在 `max_concurrent == 0` 时会静默跳过全部命令并报告成功，是「批量任务莫名没跑」类故障的典型形态。
- 复现/验证：`xhqm::execsys::execute_bat({"echo a"}, 0)` 应返回 `size()==1` 且 `success()==true`（未编译运行，由代码路径直判）。
- 建议：`max_concurrent == 0` 时直接返回 `exec_error = fail_start` 的批量结果（或抛 `xhqm::error`）。

### A7. `process` 对象复用（第二次 `exec()`）时 `reaped` 未复位，`wait()` 跳过 `waitpid`
- 位置：`process_base.cpp:150-151`（成员 `reaped` / `reaped_status`）、`:252`（`exec()` 只重置 `m_result`）、`:640-645`（`wait()` 的提前返回分支）、`:764-766`（`wait_for()` 置位处）
- 现状：`wait_for()` 用 `waitpid(WNOHANG)` 顺手回收子进程时会把 `reaped = true` 与退出状态存进 `hander`；`wait()` 见到 `reaped` 就直接沿用存下的状态、不再 `waitpid`。但 `exec()` 重新拉起子进程时**没有把 `reaped` 清回 `false`**，于是第二次运行会立刻用上一次的退出码走完 `wait()`。
- 证据：`grep -n "reaped" software/system/process/process_base.cpp` → 只有 `:150`、`:151`、`:640`、`:643`、`:764`、`:765` 六处，**无复位点**；`sed -n '248,253p'` 显示 `exec()` 仅 `m_result = {};`。
- 影响：同一个 `process` 对象「起 → `wait_for` 探测到退出 → 再 `exec`」的用法会拿到上一轮的 `exit_code`/`exit_stat`，且新子进程不被回收（僵尸）。`execsys` 每次都新建对象，所以这条只在手工复用对象时命中。
- 复现/验证：未复现；可写一个两次 `exec()`（其间 `wait_for(0)`）的用例，比对两次 `get_result().exit_code`。
- 建议：在 `exec()` 成功拉起子进程后 `hander_ptr->reaped = false;`。

### A8. `filesys` 全是 `bool` 返回，却让 `std::filesystem` 的异常直接穿透
- 位置：`filesys.h:10-30`（21 个静态成员，除列举/查询外全返回 `bool`）；抛错重载集中在 `filesys.cpp:46`、`:58`、`:79`、`:88`、`:97`、`:106`、`:115`、`:124`、`:143`、`:152`、`:214`、`:235`、`:316`
- 现状：全文件**零** `try`/`catch`（`grep -rn "try\|catch" software/system/filesys.cpp` 无命中），而 `rename`/`copy`/`copy_file`/`create_directory`/`remove_all`/`remove`/`create_directories`/`directory_iterator`/`temp_directory_path`/`current_path` 用的都是**不接收 `error_code` 的抛错重载**：权限不足、路径过长、目标被占用等都会抛 `std::filesystem::filesystem_error`。
- 证据：上面 13 处行号 + 「无 try/catch」的 grep 结果；对照 `codesys.h:8` 只承诺 UTF-8 字符串语义，没有任何「可能抛异常」的说明。
- 影响：调用方按 `bool` 语义写代码（`if (!filesys::copy_dir(a,b)) ...`）就不会有 catch，异常一路穿到最外层 → `std::terminate`；目录权限受限/长路径的部署环境下最容易被触发。
- 复现/验证：未复现（触发需要构造权限受限目录）；代码判定明确：`filesys.cpp:44-50` 的 `directory_iterator` 构造在无读权限目录上抛。
- 建议：要么改用 `error_code` 重载并统一返回 `bool`，要么在头文件里明写「本类会抛 `filesystem_error`」。

### A9. `file_list` 返回裸文件名、`dir_list` 返回完整路径，`path_list` 把两种口径混在一起
- 位置：`filesys.cpp:41-52`（`dir_list`，`:49` 用 `ite.path().string()`）、`:53-64`（`file_list`，`:61` 用 `ite.path().filename().string()`）、`:65-74`（`path_list` 直接拼接两者）
- 现状：同一族「列举」接口对返回值的含义不一致：目录项是绝对/相对全路径，文件项只有名字。
- 证据：`test/system_test.cpp:93-94` 断言 `filesys::file_list(dir)[0] == "test.txt"`（裸名）；`:49` 与 `:61` 的取值方式不同；`path_list`（`:69-72`）把两份列表首尾相接。
- 影响：`path_list` 的结果既不能统一当文件名用，也不能统一当路径拼；按 `dir_list` 的语义处理 `file_list` 会得到相对于 CWD 的错路径。
- 复现/验证：未复现（读代码即可判定；`test/system_test.cpp:93` 是既有行为的书面证据）。
- 建议：至少给三者写上返回值口径注释；更彻底的做法是统一返回全路径（或另给 `*_name` 变体）。

### A10. `timesys::systemtime()` 里 `std::tm` 未初始化且不判 `localtime_r` 失败
- 位置：`timesys.cpp:16-21`（`std::tm local_time;` 无初值；`localtime_s`/`localtime_r` 的返回值都没检查），字段读取在 `:29-36`
- 现状：时间戳超出实现支持范围时 `localtime_r` 返回 `NULL`（`local_time` 保持未初始化），随后 `:29-36` 逐字段读取 → 未定义行为/垃圾值。入口 `localtime(xhqm::natural ms)` 对 `ms` 无任何范围校验（`timesys.h:87-88`），`process::npos` 之类的宏（`SIZE_MAX`）传进来就命中。
- 证据：`sed -n '7,39p' software/system/timesys.cpp`；`grep -n "nullptr\|NULL\|if (" software/system/timesys.cpp` 在 `systemtime` 内无命中。
- 影响：任何把配置里的「大整数/哨兵值」当毫秒传进 `timesys::localtime()/systemtime()` 的代码会得到随机的年月日时分秒（甚至负/溢出），且不报错。
- 复现/验证：未复现（需指定平台实测 `localtime_r` 的越界行为）；代码判定明确：无初值 + 无 NULL 检查。
- 建议：`std::tm local_time{};` 加判空返回零值结构。

---

## B. 资源与并发

### B1. 排队满（或池非 `life`）时提交被静默取消 —— `submit` 丢弃 `_submit_` 的返回值
- 位置：`threads_pool.cpp:403-436`（容量判定 `:415-416`，接受分支 `:417-428`，拒绝即 `_cancel_target(target_ptr)` 在 `:431-435`）；调用侧 `threads_pool.h:119-134`（`:123`、`:132` 丢弃返回值）、`:137-146`（`submit_list` 同样丢弃）
- 现状：`has_capacity = (m_allow_actions == npos) || (m_queued_tasks < m_allow_actions)`，且要求 `_state() == life`；不满足就取消任务并 `return false`。`submit()` 拿到这个 `false` 后**什么都不做**，返回的 `holder` 里 promise 已被塞入 `xhqm::error("Task is canceled")`（`threads_task.h:167-201`）—— 调用方只要不读 `future`，任务就无声消失。批量版 `_submit_` 只在 `:477-480` 取消溢出的那些，`submit_list` 连「有多少被丢」都拿不到。注意 **`stop()` 暂停状态下提交同样会被取消**（`_state() != life`）。
- 证据：`sed -n '403,436p' software/system/threads/threads_pool.cpp`；`sed -n '119,146p' software/system/threads/threads_pool.h`。跨模块实证：`network/nebula/life.cpp:14-21`（`life_event` 返回 `void`，`:18` 的 `submit<void>` 丢弃 holder，池容量即配置里的 `limt`，构造见 `life.h:23-24`）；`software/signal.h:160`（`signal_queued` 丢弃 holder）、`:173-177`（`signal_waited` 只 `wait()` 不 `get()`，取消异常同样被吞掉）；`software/signal.cpp:25-27`（进程级池 `threads(2, 4)`，`m_allow_actions` 取默认 `npos` 不设上限）。
- 影响：`network/nebula` 的 `life_event` 丢事件（`DEFERRED.md` 的 B3 即此条）、`software/signal` 的异线程槽不投递；两者都表现为「偶发丢事件/偶发不执行」，无日志、无异常 —— 排查成本极高。
- 复现/验证：构造 `xhqm::parallel::threads pool(1, 1, 1, std::chrono::milliseconds(1000));`（单 worker、排队上限 1），第一个任务先阻塞（`std::this_thread::sleep_for(100ms)`）再提第二个：第二次 `submit()` 不报错、不阻塞，但该 `holder.future_ptr()->get()` 抛 `Task is canceled`；`stop()` 之后提交同样如此。可在 `test/system_test.cpp` 的 threads 用例上扩展。
- 建议：给 `submit*` 加返回值（或让 `submit_task` 语义一致），至少让调用方能区分「已排队」与「被取消」。

### B2. 通道表按提交下标只增不减，worker 每轮还要遍历整张表
- 位置：`threads_pool.cpp:69-75`（`_ensure_channel_locked`：`while (m_channels.size() <= channel) push_back`）、`:419`、`:459`（提交时调用）、`:182-205`（worker 取任务时线性扫描）
- 现状：通道下标是 `std::size_t` 且无上限，提交 `submit(100000, ...)` 会在池内造出 10 万个 `dqueue`（各自带 `std::mutex` 与 `std::deque`），且**永不回收**；worker 每取一个任务都要从轮转起点扫过整张表。
- 证据：`sed -n '69,75p;182,205p' software/system/threads/threads_pool.cpp`；`m_channels` 除 `dath()` 清队列外（`:374-382`）没有缩容路径。
- 影响：上层若用「类型 id / 设备序号」当下标（nebula 的 `addr`/`life` 就按通道分发），稀疏大下标会带来内存与取任务开销双涨；单次 10 万通道约等于每次取任务 10 万次取模 + 加锁尝试。
- 复现/验证：未复现；`grep -n "m_channels" software/system/threads/threads_pool.cpp` 可复核「只增」结论。
- 建议：给通道数设上限并在超限时抛错，或改用 map 只物化实际用到的通道。

### B3. `process` 的两个读线程并发写同一个 `m_result`、并发调 `function_o`
- 位置：`process_base.cpp:511-515`（起 stdout/stderr 两个读线程）、`:529-530` 与 `:540-541`（stdout 线程写 `exec_error`/`exec_msg`）、`:573-574` 与 `:584-585`（stderr 线程同样写）、`:552`/`:590`（两线程各自并发调用 `m_function_o`）
- 现状：`m_result` 是普通 `process::result`（`std::string exec_msg` + 两个 `std::vector`），`m_stderr_stdout == false` 时两个线程会**同时**写 `exec_error`/`exec_msg`；`m_function_o` 也被两个线程同时调用。头文件对回调的线程安全性没有任何说明（`process_base.h:63-64`、`:96-98` 只讲了 binary/line 语义）。
- 证据：`sed -n '518,597p' software/system/process/process_base.cpp`；`grep -n "m_result.exec_error\|m_result.exec_msg" software/system/process/process_base.cpp` 命中 `:529`、`:530`、`:540`、`:541`、`:573`、`:574`、`:584`、`:585`。
- 影响：两个管道同时读失败（或一个失败一个成功）时 `std::string` 并发赋值是数据竞争（UB，可能得到半截串或崩溃）；调用方把「往同一个容器里 push」写进 `function_o` 会偶发竞争。
- 复现/验证：未复现（需在高负载下让两个管道同时报错）；可用 TSan/`-fsanitize=thread` 跑一个 stderr 大量输出的子进程尝试命中。
- 建议：给 `m_result` 加锁（或把错误信息改成原子的一次性写入），并在头注释里写明 `function_o` 可能被并发调用。

### B4. `exec_out` / `exec_err` 无上限累积
- 位置：`process_base.h:59-61`（`capture_stdout` / `capture_stderr` 默认 `true`）、`process_base.cpp:552-559`、`:590-596`（逐行 push，只在 `capture_*` 为真时）
- 现状：捕获是全量 `push_back`，没有行数/字节上限，也没有「只保留尾部 N 行」的开关；只有 `function_o`/`function_b` 回调路径能让调用方自己丢数据（但那要求把 `capture_stdout(false)` 关掉）。
- 证据：`sed -n '552,559p;590,596p' software/system/process/process_base.cpp`；结构体定义 `process_base.h:46-47`。
- 影响：长驻/高频输出的子进程（ffmpeg、mediamtx、日志转储）用默认配置时内存只增；`execsys::execute_bat` 批量跑时是 N 份同时增长。
- 复现/验证：未复现；可直接跑 `proc.command("yes")` 之类无限输出命令观察 RSS 增长（需要在调用方 `wait` 前中断）。
- 建议：加「保留尾部 N 行/最大 M 字节」的上限参数，或在头注释里把「默认全量捕获」写成必须显式关掉的成本。

### B5. `libmange::self()` 在 POSIX 每次调用泄漏一次 `dlopen` 引用
- 位置：`libmange.cpp:153-160`（`return dlopen(NULL, RTLD_LAZY);`）
- 现状：`dlopen(NULL, ...)` 每次都会增加主程序的引用计数并返回一个新句柄；而 `self()` 的句柄按文档「不可 `freelib`」（`README.md` 注意事项），于是每次调用都多一份不会被释放的引用。
- 证据：`sed -n '153,160p' software/system/libmange.cpp`；`test/system_test.cpp:120-121` 就在测 `self() != nullptr`（测试里调一次即留一份引用）。
- 影响：把 `self()` 当「拿主程序句柄」在热路径反复调用的插件框架会缓慢泄漏句柄（Windows 侧 `GetModuleHandleW` 不增引用，无此问题）。
- 复现/验证：未复现（需统计 `/proc/self/maps` 或 `dlopen` 返回值变化）；代码判定明确。
- 建议：POSIX 侧用函数内 static 缓存一次 `dlopen(NULL)` 的结果。

### B6. `filesys` 的函数内 `static` 路径缓存无锁读写
- 位置：`filesys.cpp:240-243`、`:249`/`:256`/`:264`、`:269-271`（`path_directory`）与 `:278-281`、`:287`/`:294`/`:302`（`work_directory`）
- 现状：`static fs::path cached_path;` 之后是「读 `empty()` → 赋值」的非同步序列；C++11 只保证**初始化**线程安全，不保证后续赋值的并发安全。
- 证据：`sed -n '238,245p;276,283p' software/system/filesys.cpp`；文件内无任何 `mutex`/`call_once`（`grep -n "mutex\|call_once" software/system/filesys.cpp` 无命中）。
- 影响：两个线程首次并发调用 `path_directory()`/`work_directory()` 时是数据竞争（UB，`fs::path` 内部含引用计数的 `std::string`）；当前仓内 0 调用点，属潜在项。
- 复现/验证：未复现；`-fsanitize=thread` + 两线程首调可复现。
- 建议：改成 `static const fs::path cached = ...;`（一次性初始化）或用 `std::call_once`。

### B7. 已退出的 worker 只在下次 submit/life/join 时才回收
- 位置：`threads_pool.cpp:248-270`（`_reap_closed_workers`）、调用点 `:410`（每次 submit）、`:299`（`life`）、`:335`（`join`）、`:171-174`（非核心线程保活超时后退出循环）
- 现状：worker 线程对象是 `std::unique_ptr<parallel::worker>` 存在 `m_workers` 里；线程退出后置状态 `close`（`:238`），但只有上述三个路径会把它从 `m_workers` 摘掉并 join。
- 证据：`sed -n '248,270p;405,412p' software/system/threads/threads_pool.cpp`；`grep -n "_reap_closed_workers" software/system/threads/*.cpp software/system/threads/*.h`。
- 影响：一次突发（比如瞬时 4 个任务）之后空闲的池会长期保留已结束的线程对象（每个含 `std::thread` 的句柄与栈资源已被 OS 释放，但对象与 join 状态留着）；长期只提交不 join 的池内存占用不回落。
- 复现/验证：未复现；可观察 `threads(1, 8, npos, 10ms)` 提交一批后 `m_workers.size()`（需要调试器/临时打印）。
- 建议：把回收挪进保活超时路径（线程自己退出时顺手置状态，由池在下一次唤醒时清理），或提供显式 `reap()`。

### B8. `join()` / `dath()` 之后池不可复用，之后所有提交静默丢弃
- 位置：`threads_pool.cpp:302-305`（`life()` 只在 `state == stop` 时恢复）、`:345`/`:354`（`join()` 结束把状态置为 `dath`）、`:359-401`（`dath`）、`:414`（提交要求 `state == life`）
- 现状：`join()` 的终点是 `state::dath`（等价于「死亡」），`life()` 对 `dath` 直接 `return`；此后 `_submit_` 一律走「取消 + 返回 false」分支。
- 证据：`sed -n '296,314p;336,357p' software/system/threads/threads_pool.cpp`；配合 B1 的丢弃路径。
- 影响：把 `join()` 当作「等一下再继续用」的调用方，之后所有任务无声消失（且 `submit` 不报错）。语义上 `stop()`+`life()` 才是暂停/恢复。
- 复现/验证：`pool.join(); pool.submit<void>(0, [](){});` 后不报错、任务不执行（可由 `_state()` 判定，未编译运行）。
- 建议：在 `submit*` 文档里写明「本池一次性」，或让 `life()` 支持从 `dath` 重建（更危险，不推荐）。

---

## C. 跨平台

### C1. `#if _HAS_CXX20` 是 MSVC 专有宏：GCC/Clang 下 C++20 也没有 `day/week/year/month`
- 位置：`timesys.h:31-36`
- 现状：`#if _HAS_CXX20` 把 `std::chrono::days/weeks/years/months` 四个别名圈在里层；`_HAS_CXX20` 由 MSVC STL 提供，libstdc++/libc++ 不定义它，因此 GCC/Clang 下（即便 `-std=c++20`）这四个别名不存在。
- 证据：实测 `printf '#include "software/system/timesys.h"\nint main(){ (void)xhqm::timesys::day(1); return 0; }\n' | g++ -x c++ - -fsyntax-only -I. -std=c++20` → `error: 'day' is not a member of 'xhqm::timesys'`；同一命令加 `-D_HAS_CXX20=1` 通过（g++ 11.4.0）。
- 影响：跨平台代码用 `xhqm::timesys::day` 之类会在非 MSVC 上编译失败；而 `#if` 用的是未定义宏 → 静默退化，不报警。
- 复现/验证：上面两条 `g++ -x c++ - -fsyntax-only` 命令（无需落盘）。
- 建议：改成 `#if __cplusplus >= 202002L`（并保留 MSVC 的判断）。

### C2. Windows 侧动态库路径按 ACP 解释，与引擎的 UTF-8 约定不一致
- 位置：`libmange.cpp:108-114`（`:110` 的 `std::filesystem::path(libpath).wstring()`），对照 `process_base.cpp:347-355`（显式 `MultiByteToWideChar(CP_UTF8, ...)`）
- 现状：`loadlib` 拿到的是 UTF-8 `std::string`（`codesys.h:8` 明确「string 的编码方式为 utf8」），但 `fs::path(std::string)` 在 Windows 上按**系统 ACP** 解释窄串再转宽；`depend()`（`:144`）同样写法。进程启动那边为同一目的专门写了 `CP_UTF8` 转换，两处口径不一致。
- 证据：`sed -n '104,117p' software/system/libmange.cpp`；`sed -n '344,356p' software/system/process/process_base.cpp`；注释 `libmange.cpp:109` 自称「无条件走宽字符，识别中文路径（避免 setlocale 的全局副作用）」，但注释没说宽字符的来源是 ACP。
- 影响：系统区域设置非 UTF-8 时，含中文的插件路径转宽后乱码 → `LoadLibraryW` 失败、`error()` 只给系统错误码；同一程序里 `process` 却能正常起中文路径的子进程，表现为「有的地方中文路径行、有的不行」。
- 复现/验证：未复现（本轮无 Windows 环境/编译器，只有 Linux 侧 g++）。
- 建议：与 `process_base.cpp` 一致，改走 `MultiByteToWideChar(CP_UTF8, ...)`。

### C3. 平台开关两套并存（`XHQM_WINDOWS` 与 `_WIN32`/`__linux__`/`__APPLE__`）
- 位置：`head.h:104`（`_WIN32` 下 `#define XHQM_WINDOWS`）；本模块内 `process/process_base.cpp:15`、`libmange.cpp:4`、`codesys.cpp:127` 用 `XHQM_WINDOWS`；`codesys.cpp:155`/`:159` 再用 `__APPLE__`/`__MACH__`/`__linux__`/`__unix__`；`filesys.cpp:5-13` 整段用 `_WIN32`/`__linux__`/`__APPLE__`
- 现状：同一个「选 Windows 还是 POSIX 分支」的意图有两套写法，且 `codesys.h` 的文档注释（`:24-30`）只提 Windows/Linux/macOS 三平台，与实现的五分支不完全对应。
- 证据：`grep -rn "XHQM_WINDOWS\|_WIN32\|__linux__\|__APPLE__\|__MACH__\|__unix__" --include=*.h --include=*.cpp software/system/ | wc -l` = **45** 处；用 `XHQM_WINDOWS` 的 4 个文件是 `timesys.cpp`、`process/process_base.cpp`、`libmange.cpp`、`codesys.cpp`，用 `_WIN32`/`__linux__`/`__APPLE__` 的 2 个文件是 `codesys.cpp`、`filesys.cpp`（`codesys.cpp` 两套都用）。
- 影响：新增平台（Android/Harmony 等）时要改多处；漏一处就落到 `#else` 分支静默走错实现。
- 复现/验证：`grep -rn "_WIN32\|XHQM_WINDOWS" software/system/`。
- 建议：统一到 `head.h` 的平台宏（或在每个 `.cpp` 头部加一行 `#if defined(XHQM_WINDOWS)` 的说明）。

### C4. 因 `software/signal.h` 遮蔽系统 `<signal.h>`，进程侧硬编码信号编号
- 位置：`process_base.cpp:26-37`（`extern "C" int kill(pid_t, int);` + `#ifndef SIGTERM #define SIGTERM 15` / `SIGKILL 9`）；构建侧补丁在根 `CMakeLists.txt:49-72`（MSVC 把 UCRT 头目录提到最前）
- 现状：include 路径里 `software/` 在系统目录之前，`<signal.h>` 会命中信号槽头，于是进程代码手写 `kill` 声明并按 POSIX 固定值补 `SIGTERM=15`/`SIGKILL=9`（注释自陈「Linux/macOS 一致」）。
- 证据：`sed -n '19,38p' software/system/process/process_base.cpp`；`CMakeLists.txt:49-72` 的注释与 `XHQM_UCRT_CANDIDATES` 补丁。
- 影响：编号固定的前提是 Linux/macOS；换到其它 POSIX（如某些 BSD 变体/嵌入式 libc）编号不同时，`kill` 会送错信号 —— 杀不掉子进程而不是报错。
- 复现/验证：未复现（无其它 POSIX 环境）；Linux 侧编号正确（15/9）。
- 建议：让遮蔽问题在构建层彻底解决（如 `-I` 顺序），信号编号回归 `<csignal>`。

### C5. 平台目录探测用固定 `MAX_PATH` 缓冲
- 位置：`filesys.cpp:223-226`（`GetTempPathW(MAX_PATH, buffer)`）、`:247-249`（`GetModuleFileNameW`）、`:284-287`（同）
- 现状：Windows 三个调用都用 `MAX_PATH`（260）栈缓冲，返回值超出即放弃（`length < MAX_PATH` 判定不成立时落到回退分支 `current_path()`）。
- 证据：`sed -n '220,236p;245,250p;283,288p' software/system/filesys.cpp`。
- 影响：Windows 长路径（`\\?\` 或启用长路径策略）下 `path_directory()` 静默退化为 CWD 或临时目录，进而影响 `data_directory()` 的兜底路径；其余 POSIX 侧用 `PATH_MAX`（Linux 4096），问题不突出。
- 复现/验证：未复现（无 Windows 环境）。
- 建议：改用 `GetModuleFileNameW(nullptr, nullptr, 0)` + 动态 `std::wstring` 的两次调用写法。

---

## D. 性能

### D1. 每次取任务都要线性扫描整张通道表
- 位置：`threads_pool.cpp:193-205`
- 现状：worker 醒来后用 `m_scan_seed.fetch_add` 取起点，然后 `for (step = 0; step < channel_count; ++step)` 逐通道加锁尝试取队首；空通道也要加一次锁（`std::lock_guard<std::mutex> channel_lock(m_channels[index]->mutex)` 在 `if (!queue.empty())` **之前**，即每轮都为每个通道抢一次锁）。
- 证据：`sed -n '192,206p' software/system/threads/threads_pool.cpp`；通道数由 B2 决定可被大下标拉大。
- 影响：任务本身很短、通道数又多时，取任务的开销（N 次加锁 + 取模）会盖过任务本身；典型场景是「按设备/类型分通道 + 大量稀疏下标」。
- 复现/验证：未复现；可在 `test/system_test.cpp` 的 threads 用例里用 1000 个通道各提 1 个任务做对比。
- 建议：给 `dqueue` 加「非空」原子标志，扫描时先读标志再加锁；或维护一张有待处理的通道下标表。

### D2. 提交路径的固定开销：每次都做一次全量 worker 回收 + 锁内逐条入队（轻微）
- 位置：`threads_pool.cpp:410`（`_reap_closed_workers()`，自身要锁 `m_wake_mutex` 并遍历 `m_workers`，`:248-267`）、`:462-469`（`submit_list` 在持通道锁时逐条 `push_back` + `++m_queued_tasks`）
- 现状：每次提交（含批量）都先遍历一次 worker 向量找 `close` 状态；批量入队是循环单条 push，而不是一次性 splice。
- 证据：`sed -n '248,267p;405,412p;446,481p' software/system/threads/threads_pool.cpp`。
- 影响：高频小任务提交时，锁与遍历的常数开销占比偏高；批量提交上千条时会长时间持有通道锁。
- 复现/验证：未复现（需基准）；由代码路径直接判定。
- 建议：回收改为按需（例如每 N 次提交或 `m_workers` 变化后触发）；批量入队改成先构造临时 deque 再整段拼接。

---

## E. 未实现 / 占位桩

### E1. 两个成员/计数器是「只写不读」的死状态
- 位置：`threads_pool.h:31`（`std::atomic_bool m_shared{ false };`）、`threads_pool.h:78`（`std::size_t m_idle_workers{ 0 };`）
- 现状：`m_shared` 全仓只在声明处出现（无任何读写）；`m_idle_workers` 只在 `threads_pool.cpp:143` 自增、`:163` 自减，**没有任何读者**（既不参与 `_ensure_workers_locked` 的需求计算，也不对外暴露）。
- 证据：`grep -rn "m_shared\|m_idle_workers" software/system/` → 仅 `threads_pool.h:31`、`:78`、`threads_pool.cpp:143`、`:163`。
- 影响：读者会误以为「空闲线程数」参与了弹性扩缩或可被查询（实际需求只看 `running + queued`，`:113-115`）。
- 复现/验证：`grep -rn "m_idle_workers" software/system/`。
- 建议：要么删掉，要么给 `m_idle_workers` 加一个访问器让它的存在有意义。

### E2. `FILE_BXTtoBIN` 里的 EOF 判据对 `unsigned char` 永假（死分支）
- 位置：`codesys.cpp:82-83`（`if (txtdata[0] == -1) break;`，`txtdata` 是 `xhqm::byte txtdata[4]`，`:75`）
- 现状：`xhqm::byte` = `unsigned char`（`head.h:233`），`fgetc` 的 `-1` 赋进来变成 `0xFF`，提升为 `int` 后是 `255`，永远不等于 `-1`；循环实际靠 `while (!feof(bxt))`（`:86`）退出。
- 证据：`sed -n '72,91p' software/system/codesys.cpp`；类型别名 `head.h:233`。
- 影响：这行是无效防御 —— 一旦 `while` 条件被改动（或换成 `for (;;)`），EOF 就再无人拦，会无限写入垃圾字节。
- 复现/验证：可由类型语义直接判定；`g++ -std=c++17 -fsyntax-only` 不报错（`-Wtype-limits` 之类的提示要看具体编译器与优化级别，本轮未触发）。
- 建议：改成 `if (txtdata[0] == 0xFF && feof(bxt)) break;` 或直接用 `int` 存 `fgetc` 的返回值。

### E3. 线程池没有任务优先级，也没有 per-channel 容量上限
- 位置：`threads_pool.h:69`（唯一的全局上限 `m_allow_actions`）、`threads_pool.cpp:415-416`、`:451-455`（批量版的同一判定）、`:69-75`（通道只建不管）
- 现状：容量是**整个池**共用一个计数 `m_queued_tasks`，与通道无关；取任务顺序是「轮转起点 + FIFO」，没有任何优先级/权重/饥饿保护手段；`submit` 也没有「插队/高优先」入口。
- 证据：`sed -n '64,96p' software/system/threads/threads_pool.h`；`grep -rn "priority\|priorit" software/system/threads/` 无命中。
- 影响：一条通道被大量低优先任务灌满后，同一池里其它通道的紧急任务只能与它们共享 `m_allow_actions` 额度、按 FIFO 排队（`signal` 的槽派发、`nebula` 的 `life_event` 与业务任务共用一个池时就是这种结构）。
- 复现/验证：`grep -rn "priority" software/system/threads/`（无命中）+ 读 `_worker_loop` 的取任务循环。
- 建议：短期先在文档里写明「池内任务不分优先级」；要做优先级需给 `dqueue` 换成带权队列（属于接口变更，另开设计）。

### E4. 进程侧没有信号量、没有信号处理器注册、没有「向任意进程发信号」的 API
- 位置：`process_base.h:110-125`（终止三件套 `pid()` / `wait_for()` / `terminate()` / `terminate(grace_ms)`）
- 现状：能做到的是「起进程 / 读管道 / 写 stdin / 探测退出 / 温和或强制终止**自己起的**子进程组」；`grep -rn "sem_\|semaphore\|sigaction\|signal(" --include=*.h --include=*.cpp software/system/` 无命中（POSIX 侧只有 `kill(-pid, ...)` 用于终止），也没有「按 pid 发 SIGUSR1/信号量唤醒」这类接口。
- 证据：`grep -rn "sem_\|semaphore\|sigaction" --include=*.h --include=*.cpp software/system/`（无命中）；`process_base.cpp:660`、`:664`、`:784`、`:798` 的 `kill` 全是终止用途。旧说法「进程类无跨平台信号/信号量/进程组管理」中的**进程组与终止部分已不成立**（`setpgid` `:455`/`:497`、Windows Job 对象 `:399-408`），剩下这一条仍然成立。
- 影响：需要「通知子进程重载配置」（SIGUSR1/HUP）或「父子进程共享计数/握手」的场景，只能杀掉重起或另找 IPC。
- 复现/验证：`grep -rn "sigaction\|sem_post\|sem_wait" --include=*.h --include=*.cpp software/system/`（无命中）。
- 建议：不改现有语义，按需补一个 `signal_to_process(pid, sig)`（POSIX `kill(pid, sig)` / Windows 用 `GenerateConsoleCtrlEvent` 或事件对象）即可。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 排队满即「取消任务并拒绝」，而不是阻塞或丢最旧
- 位置：`threads_pool.cpp:415-416`、`:431-435`
- 现状（为什么现在这样是对的）：`m_allow_actions` 的意义就是给排队量画一条硬线 —— 生产者若被允许无界入队，内存与延迟都不可控；「满即拒」把背压决策留给调用方，也避免了「阻塞在提交点导致正在派发任务的线程死锁」这一类经典问题（`signal_waited` 就是同步等待线程池的调用方，若池满再阻塞提交，就是自锁）。
- 证据：`sed -n '413,436p' software/system/threads/threads_pool.cpp`（`has_capacity` 判定 → 接受分支 → `_cancel_target` + `return accepted`）；调用方丢弃返回值的证据见 B1 与 F2。
- 影响（代价）：调用方不检查返回值（B1）时表现为静默丢任务；`submit_list` 连「丢了几条」都拿不到。**要修的是「把结果告诉调用方」，不是把拒绝改成阻塞。**
- 复现/验证：见 B1。
- 建议：保留拒绝语义，补返回值/计数即可。

### F2. 取消通过 `promise.set_exception` 传递，而不是返回值或回调
- 位置：`threads_task.h:167-201`（`cancel`）、`:262-265`（`future_ptr`）
- 现状（为什么现在这样是对的）：`holder` 同时承载「结果」与「失败/取消」，调用方 `get()` 一次就能区分成功、异常、被取消；不需要额外的事件回调或状态查询接口，`submit` 的签名也保持干净。
- 证据：`sed -n '167,201p;257,266p' software/system/threads/threads_task.h`（`cancel` = CAS 置 `cancel` 态 + `set_exception`；`future_ptr()` 是唯一的观察出口）。
- 影响（代价）：**不读 `future` 就等于没收到通知**（丢弃 holder = 丢弃取消通知，`life.cpp:18`、`signal.h:160` 都是这种用法）。设计上只能靠「谁关心就谁读」的纪律，无法强制。
- 复现/验证：见 B1。
- 建议：不修；新调用方一律读 `future` 或改用带返回值的 `submit_task`。

### F3. 池状态枚举故意复用 worker 状态枚举的数值
- 位置：`threads_pool.h:57-63`（`dath = worker::state::close`、`life = worker::state::alive`、`stop = worker::state::sleep`）对 `threads_pool.h:22-27`
- 现状（为什么现在这样是对的）：池和 worker 共用同一套 `std::atomic_uchar` 存储与 `xhqm_uchar` 转换，省掉一次映射；`stop`（池暂停）与 `sleep`（worker 空闲）在语义上确实同源。
- 证据：`sed -n '55,63p;90,93p' software/system/threads/threads_pool.h`（枚举值别名 + `_state()` 统一转换）；`grep -n "xhqm_uchar(threads::state" software/system/threads/threads_pool.cpp` 显示读写都走这条路径。
- 影响（代价）：读代码时 `m_status` 的取值含义取决于「这是池还是 worker」，`join` 是唯一不与 worker 状态对齐的枚举（值为 3，`:62`），排查时容易看错（例如把池的 `stop` 当成 worker 的 `sleep`）。
- 复现/验证：读 `threads_pool.h:57-63` 与 `:90-93`（`_state()` 统一转换）。
- 建议：不修；若将来动这段代码，建议加一行注释固定这个映射关系。

### F4. `filesys` 对外统一「返回 `bool`」而不定义自己的错误码
- 位置：`filesys.h:10-30`（除 `env_value`/目录查询外全部 `bool` 或 `vector<string>`）
- 现状（为什么现在这样是对的）：上层（`software/packet.cpp`、`utility.cpp`、`message.cpp`、`nebula/*`）需要的是「成没成」这一个判断，函数式风格好读好组合；失败原因需要深挖时可以用平台 API 自查。
- 证据：`sed -n '10,30p' software/system/filesys.h`（21 个静态成员，仅 `dir_list`/`file_list`/`path_list`/`env_value`/目录查询返回非 `bool`）；`grep -c "try\|catch" software/system/filesys.cpp` = 0。
- 影响（代价）：失败原因全部丢失（`rename` 目标已存在、源不存在、权限不足都是同一个 `false`），并且 `std::filesystem` 自己的异常仍会穿透（A8）——「不抛异常」这个印象是错的。
- 复现/验证：见 A8。
- 建议：不修接口；至少在头文件里补一句「本类不捕获 `std::filesystem::filesystem_error`」。

### F5. `process` 析构只给 10 ms 宽限就强杀子进程
- 位置：`process_base.h:70`（`~process() { this->wait(10); }`）配合 `process_base.cpp:599-676`（POSIX 超时分支：到点先 `SIGTERM` 进程组、睡 1 秒、必要时 `SIGKILL`），Windows 侧 `:626-635`（`WaitForSingleObject(10ms)` 后 `TerminateJobObject`）
- 现状（为什么现在这样是对的）：`process` 是**同步封装**，析构必须保证不留下无人回收的子进程与读线程（否则进程退出时会挂在 `join` 上）；长驻子进程的正确用法是先 `wait_for`/`terminate` 再析构 —— 头文件 `:110-125` 正是为这个场景提供的。
- 证据：`sed -n '70p' software/system/process/process_base.h`（`~process() { this->wait(10); }`）与 `sed -n '646,676p' software/system/process/process_base.cpp`（POSIX 超时分支：`SIGTERM` → 睡 1 秒 → 必要时 `SIGKILL`）；Windows 侧 `:626-635`。
- 影响（代价）：把「长驻子进程」交给一个短生命周期的 `process` 对象管理时，对象一析构子进程就被杀掉（10 ms 后 `SIGTERM`、最多再等 1 秒 `SIGKILL`，析构本身可能阻塞约 1 秒）；这个行为在头文件里没有明写。
- 复现/验证：可由 `process_base.h:70` + `process_base.cpp:646-676` 直接判定；实测方式：起一个 `sleep 30` 子进程后让对象离开作用域，观察子进程被 SIGTERM/SIGKILL。
- 建议：不修；在头文件 `:69-70` 补一行「析构 = `wait(10)`，长驻进程请显式 `terminate()`」。

### F6. `software/signal` 的进程级槽派发池固定 `threads(2, 4)` 且排队不限
- 位置：`software/signal.cpp:25-27`（`new xhqm::parallel::threads(core = 2, max = 4)`，第三个参数走默认 `npos`）
- 现状（为什么现在这样是对的）：槽派发是引擎到处都会碰的公共路径，池太大等于给每个进程都留一堆线程；不限制排队量保证了 `emit` 永不因为「队列满」而丢事件（B1 那条路径不会被触发）。
- 证据：`sed -n '22,38p' software/signal.cpp`（`_pool_life_(core = 2, max = 4)` 构造时第三个参数即 `allow_actions` 用缺省值）；`sed -n '105,106p' software/system/threads/threads_pool.h` 给出该缺省是 `xhqm::npos`。
- 影响（代价）：槽函数只要有人耗时，后续队列就线性堆积（内存与延迟都无人兜底）；池一旦 `join`/`dath`（正常退出时 `g_pool = nullptr` 只是摘除安装，`signal.cpp:29-31`），之后的 `emit` 会静默不投递（B1 + F2 组合）。
- 复现/验证：读 `software/signal.cpp:22-38`；跨模块影响见 B1。
- 建议：不修；若将来出现「槽函数慢」的现场，先看这里的排队深度。

### F7. `libmange::function(id, name)` 的兜底语义：`id` 为空在 POSIX 上仍有结果
- 位置：`libmange.h:19-20`（注释自称「仅作兜底」）、`libmange.cpp:132-139`（`dlsym(id, name)`；`id == nullptr` 时 POSIX 语义是**搜索主程序与全局作用域**）
- 现状（为什么现在这样是对的）：保留 `void*` 句柄接口便于「查一次符号存起来」的旧调用方式（`software/utility.cpp:60-61` 就是 `loadlib` + `function` 两步）。
- 证据：`sed -n '19,20p' software/system/libmange.h`（「返回符号指针（void*，仅作兜底）」）与 `sed -n '132,139p' software/system/libmange.cpp`（无 `id` 判空，直接 `dlsym`/`GetProcAddress`）。
- 影响（代价）：句柄传错（`nullptr`、或用错了库的句柄）在 POSIX 上不报错，可能命中主程序/其它已加载库的同名符号 —— 表现为「插件里的函数没被调用，主程序的同名函数被调了」。
- 复现/验证：未复现；POSIX `dlsym(NULL, ...)` 语义可查 `man dlsym`。
- 建议：不修；调用点自行保证句柄来源（本仓唯一的两步用法在 `software/utility.cpp:60-61`，句柄来自同一个 `loadlib`）。

---

## G. 文档与代码的出入

### G1. 本模块 `README.md` 的旧文已于本轮订正（只做索引）
- 位置：`software/system/README.md`（8 处标注分别在 `:9`、`:18`、`:29`、`:37`、`:47`、`:55`、`:111`、`:113`）
- 现状：原文与实测不一致处已就地订正，凡被推翻的旧说法保留原文并标注 `~~旧文~~ **已作废（2026-10-07 复核 r677）**`，共 **8 处**（按行序，括号内是新 `README.md` 的行号）：① 模块定位「所有实现以 `#ifdef XHQM_WINDOWS` 区分」（`:9`）；② `FILE_BINtoBXT`/`FILE_BXTtoBIN`「二进制 ⇄ 十六进制文本」的对称暗示（`:18`）；③ `path_directory`/`work_directory` 并列的暗示（`:29`）；④ `timer` 文本接口「`run_time()` 给出已耗时」（`:37`）；⑤ 进程实现「Windows 用 `CreateProcessA`」（`:47`）；⑥ `loadlib`「宽字符，支持中文路径」（`:55`）；⑦ 依赖项「`algebra/value.h` 提供 `value` 类型」（`:111`）；⑧ 依赖项「`pattern/instance.h` 被 `libmange.cpp` 内部使用」（`:113`）。其余若干条（线程池默认参数、`process` 的 `result` 字段与终止接口、列举函数口径、文件行数、`split_string` 两种语义）属补精确，不是推翻。
- 证据：`grep -n "已作废（2026-10-07 复核 r677）" software/system/README.md`。
- 影响：接手者若只看旧版本文档，会按已作废的说法使用 `work_directory()`/`run_time()`/`CreateProcessA`。
- 复现/验证：`grep -c "已作废" software/system/README.md`。
- 建议：不修（这是本仓既定的历史保留风格）。

### G2. `process_base.h` 的两处注释与实现不符
- 位置：`process_base.h:123-125`（`terminate(grace_ms)`「返回是否已退出」）对 `process_base.cpp:791-808`；`process_base.h:45`（`term_signal`：0=无，15=温和，9=强杀）对 `process_base.cpp:660-669`
- 现状：`terminate(grace_ms)` 的三条返回路径**都是 `return true`**（`:803`/`:807`，加上开头未启动时的 `:793`），无法表达「没退干净」；超时路径先发 `SIGTERM`，1 秒后若仍活着就升级 `SIGKILL`（`:662-666`），但 `term_signal` 恒定写 `15`（`:669`，注释自称「必要时已升级为 SIGKILL」），Windows 超时路径写 `9`（`:631`）；`terminate(grace_ms)` 若走到强杀，`m_result.term_signal` 也停在 `15`（`:800-801` 之后不再更新，`:805` 调用 `terminate()` 会覆盖成 9 —— 顺序上恰好正确，但依赖调用顺序而非显式赋值）。
- 证据：`sed -n '45p;110,126p' software/system/process/process_base.h`；`sed -n '660,670p;786,808p' software/system/process/process_base.cpp`。
- 影响：调用方无法用返回值区分「温和退出」与「被强杀」，日志里看到的 `term_signal == 15` 也可能其实是 `SIGKILL` 的结果（POSIX 超时路径）。
- 复现/验证：`sed -n '791,808p' software/system/process/process_base.cpp`（三条 return 全为 true）。
- 建议：`terminate(grace_ms)` 返回 `this->runing() == false`（或 `wait_for` 的结果），并在真正升级为强杀时把 `term_signal` 改成 9。
