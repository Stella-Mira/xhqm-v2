# system 系统原语模块

> 最后同步：2026-10-07，对照工作副本 r677。

## 模块定位

`software/system` 是 xhqm 引擎的**操作系统抽象层（system primitives）**，为上层数学、渲染、网络、信号槽等模块提供与平台无关的系统能力：文件系统、时间与定时、编码转换、进程执行、动态库加载，以及并发基础设施（线程池 / 任务 / 锁）。接口类普遍以 `xhqm_io` 标注（该宏只在 `XHQM_DYNAMIC` 下才展开为导入/导出宏，否则为空，见 `head.h:20-32`；`parallel::dqueue` / `parallel::target` / `parallel::holder` 这类载体不标）。规模：**19 个文件、7 个 `.cpp` + 12 个头**（含 `process/process_base.cpp` 825 行、`threads/threads_pool.cpp` 483 行）；`wc -l` 全模块合计 **3202** 行，**内容行 3207**（`codesys.h` / `filesys.h` / `filesys.cpp` / `threads/lock_mutex.h` / `threads/threads_task.h` 这 5 个文件结尾无换行，`wc -l` 比内容行少 1）。**本文所有「X / Y 行」都按内容行数计**（与读文件工具的行号一致）。

~~所有实现以 `#ifdef XHQM_WINDOWS` 区分 Windows 与 POSIX（Linux/macOS）两套后端，对外统一暴露 `xhqm_io`（动态库导出宏）标注的静态接口。~~ **已作废（2026-10-07 复核 r677）**：平台开关在本模块内**有两套并存** —— `XHQM_WINDOWS`（由 `head.h:104` 在 `_WIN32` 下定义；`process/process_base.cpp:15`、`libmange.cpp:4`、`codesys.cpp:127` 用它）与直接系统宏 `_WIN32` / `__linux__` / `__APPLE__`（`filesys.cpp:5-13` 用它，`codesys.cpp:155-159` 还额外用 `__MACH__` / `__unix__`）；新增平台要改多处，见 `DEBUG.md` 的 C3。

> 注意：与软件层聚合头 `software/system.h`（`xhqm::software::system_root`，管理 message/command/project/utility 四个子根）不同，本目录是**底层原语**，二者命名相近但层级不同。

## 关键类 / API

### 1. `xhqm::codesys` — 编码与字节转换（`codesys.h/.cpp`，41 / 229 行）
- `tosl` / `tonu`：单字节与十六进制文本（0-9A-F，**大写**）互转；`tonu` 用 `< 0x41` 判定，输入须大写。
- `string2hexa` / `hexa2string`：字符串 ⇄ 十六进制字符串（`string2hexa("A") == "41"`）。
- ~~`FILE_BINtoBXT` / `FILE_BXTtoBIN`：二进制文件 ⇄ 十六进制文本文件。~~ **已作废（2026-10-07 复核 r677）**：两个函数**格式互不匹配、往返数据错位** —— 写侧每字节输出 2 个十六进制字符（`codesys.cpp:60` 写 `fwrite(txtdata, 2, 1, bxt)`，那句 `txtdata[2] = ' '` 的空格从未写出），读侧却按“2 位十六进制 + 1 分隔符”每轮读 3 个字符（`codesys.cpp:79-81`）。实测模拟：`41 → 41`（1 字节正好对），`ab cd → ab d8`、`aa bb cc → aa bc`、10 字节进去 7 字节出来。详情与行号见 `DEBUG.md` A2。
- `unicode_to_string` / `string_to_unicode`：UTF-8 ↔ `std::wstring`，实现在 `std::wstring_convert<std::codecvt_utf8<wchar_t>>`（`codesys.cpp:94-102`，`#include <codecvt>` 在 `:4`）。
- `utf8_to_system` / `system_to_utf8`：UTF-8 ↔ 系统本地编码。Windows 走 `MultiByteToWideChar`/`WideCharToMultiByte`（`codesys.cpp:127-153`）；`utf8_to_system` 在 Linux/macOS **原样返回**（Linux 只读 `setlocale(LC_CTYPE, nullptr)` 判一下然后仍 pass-through，`:155-183`）；`system_to_utf8` 非 Windows 分支**无 locale 判定**、直接返回（`:224-227`）—— 两者不对称。
- `split_string`：`char` 版按键切分并**丢弃空项**（`"a,b,,c"` → 3 项，`test/system_test.cpp:31-32` 断言）；`std::string` 版按 `std::regex` 切分并**保留空项**（`codesys.cpp:104-119`），正则非法会抛 `std::regex_error`。

### 2. `xhqm::filesys` — 文件系统（`filesys.h/.cpp`，35 / 318 行）
基于 `std::filesystem` 的**全静态**封装，21 个静态成员：
- 判定：`is_dir` / `is_file` / `is_exists`（`is_file` = 存在且非目录，`:28-36`）。
- 列举（口径不同，务必注意）：`dir_list` 返回**完整路径**（`ite.path().string()`，`:49`）；`file_list` 返回**裸文件名**（`ite.path().filename().string()`，`:61`，`test/system_test.cpp:94` 断言 `files[0] == "test.txt"`）；`path_list` = 二者拼接，列表里混装两种口径（`:65-74`）。三者都**不递归**。
- 增删改：`create_dir` / `create_file` / `delete_dir`（内部是 `remove_all`，**递归删除**，`:143`）/ `delete_file` / `rename` / `move_dir` / `move_file` / `copy_dir` / `copy_file`。移动/复制/改名要求**目标不存在**，否则返回 `false`（`:77`、`:86`、`:95`、`:104`、`:113`）；目标已存在时不会覆盖也不会报错原因。
- 环境：`env_value`（Win `_dupenv_s` / POSIX `getenv`）。
- 平台目录：`data_directory(appname)`（Win `%APPDATA%\appname` / macOS `~/Library/Application Support/appname` / Linux `$XDG_DATA_HOME/appname` 或 `~/.local/share/appname`，失败回退“可执行文件目录/data”，**并自动 `create_directories`**，`:174-218`）、`temp_directory`（`TMPDIR` → `fs::temp_directory_path()`）、~~`path_directory`（可执行文件所在目录，缓存）、`work_directory`~~ **已作废（2026-10-07 复核 r677）**：`work_directory()`（`:276-312`）与 `path_directory()`（`:238-274`）是**逐行相同的两份实现**，都返回**可执行文件所在目录**并各自缓存在函数内 `static fs::path`，`work_directory()` **不返回进程当前工作目录**（那是 `current_path()`）；见 `DEBUG.md` A4。

### 3. `xhqm::timesys` / `xhqm::nowtime` / `xhqm::timer` — 时间与定时（`timesys.h/.cpp`，114 / 84 行）
- 时间戳（自 1970-01-01 epoch）：`nowtime<timetype>()` 模板，以及 `ntime`/`utime`/`mtime`/`stime`（纳/微/毫/秒），全部基于 `std::chrono::system_clock`（`timesys.h:41-59`）。
- 时长别名：`nanos` `micro` `milli` `second` `minute` `hour`，以及 C++20 才有的 `day` / `week` / `year` / `month`（受 `#if _HAS_CXX20` 控制，**该宏是 MSVC 专有**，GCC/Clang 下取不到，见 `DEBUG.md` C1）。
- 休眠：`sleep<timetype>()` 模板与 `nsleep`/`usleep`/`msleep`/`ssleep`。
- 结构化本地时间 `xhqm::nowtime`（`year/month/mday/wday/hour/minu/seco/mill`）与 `systemtime(...)` / `localtime(...)`；`localtime()` 的格式是 **`年/月/日 时:分:秒:毫秒`**（如 `2026/10/7 1:33:0:123`，`timesys.cpp:50-51`，无零填充）。
- `xhqm::timer`：`uptimer()` 重置、`runtime()`/`utimes()`/`mtimes()`/`stimes()` 已耗时长（纳/微/毫/秒）、`starus()` 起点纳秒，以及 `old_time()`（起点墙钟）/ `now_time()`（起点+耗时的“1970 起算”墙钟）/ `run_time()` 文本。
- ~~`run_time()` 给出已耗时 `"00:00:01.234"`。~~ **已作废（2026-10-07 复核 r677）**：`runtime() - m_starns` 是无符号自减、必然回绕（`timesys.cpp:72`），打印出的是无意义的时刻（模拟：10 ms 实际耗时被打印成 `5:58:51.18`），正确写法是只传 `runtime()`。见 `DEBUG.md` A1。
- 字面量：`42_systemtime` 把 epoch 毫秒转本地时间字符串（`inline namespace literals`，`timesys.cpp:76-82`）。

### 4. `xhqm::parallel::process` — 进程执行（`process/process_base.h/.cpp`，130 / 825 行）
对标 Qt `QProcess` 的跨平台进程封装：
- 三种启动模式：`program(path, args)` 直接执行；`command(line)` 走 shell（Windows `cmd.exe /c`，POSIX `/bin/sh -c`，`process_base.cpp:49-53`）；`command_line(line)` 原始命令行（Windows 整串不套 shell，POSIX 仍用 `sh -c`，`:191-196`、`:257-266`）——引号/管道/中文须原样保留时用它。
- 配置：`workdir` / `environment`（`std::map<text,text>`，POSIX 侧 fork 后 `setenv`，Windows 侧拼宽字符环境块）/ `capture_stdout` / `capture_stderr` / `stderr_stdout`（默认 `true`，即默认把 stderr 并进 stdout 管道）/ `throw_on_error` / `function_i`（改写输入）/ `function_o`（逐行回调）/ `function_b`（原始字节回调，设了它 stdout 就不按行切分、不剥 `\r`）。
- 生命周期：`exec()`（启动，另起读线程并行读管道防写满死锁）、`wait()` / `wait(millis)`、`wait_for(millis)`（探测退出，`process::npos` 表示一直等）、`input(string&)`（写 stdin，返回空串表示成功）、`close_write()`、`active()` / `runing()`（注意拼写，就是这个 `runing`）、`pid()`、`terminate()`、`terminate(grace_ms)`。
- 超时/终止语义：**构造参数 `process(const xhqm::count& millis = process::npos)` 是超时**（`npos` = `SIZE_MAX` 毫秒 ≈ 永不等超时，`process_base.h:17`、`:69`）；POSIX 超时先 `kill(-pid, SIGTERM)` 进程组、1 秒后升级 `SIGKILL`（`process_base.cpp:660-669`），Windows 用 Job 对象整树回收（`:628-629`）。析构函数是 `wait(10)`（`process_base.h:70`）——**对象销毁时若进程仍在跑，只给 10 ms 宽限就强杀**，见 `DEBUG.md` F5。
- 结果 `process::result`：`exit_code` / `exit_stat`（normal/crash，POSIX 由 `WIFSIGNALED` 判、退出码取负信号号）/ `exec_error`（none/fail_start/crashed/timeout/read_error/write_error）/ `exec_msg`（错误描述）/ `exec_time`（毫秒）/ `terminated`（是否被本对象主动终止）/ `term_signal`（0=无，15=温和，9=强杀）/ `exec_out` / `exec_err`，`success()` = `exit_code == 0 && exec_error == none`（`process_base.h:37-49`）。
- 实现：POSIX 用 `fork`+`setpgid`+`dup2`+`execvp`（失败 `_exit(127)`）；~~Windows 用 `CreatePipe`+`CreateProcessA`+匿名管道，并实现命令行参数转义与继承环境块拼接。~~ **已作废（2026-10-07 复核 r677）**：Windows 侧已是 `CreateProcessW` + `CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT`（`process_base.cpp:375-382`），命令行按 CommandLineToArgvW 反规则转义（`:58-91`）、路径显式 `MultiByteToWideChar(CP_UTF8, ...)` 转宽（`:347-355`）、先 `CREATE_SUSPENDED` 起来挂进 `CreateJobObject`+`KILL_ON_JOB_CLOSE` 再 `ResumeThread`（`:398-408`）——整树回收就是靠这个 Job。

### 5. `xhqm::execsys` — 进程简化封装（`execsys.h/.cpp`，23 / 61 行）
- `execute_cmd(line)`：命令字符串一步执行返回 `process::result`（内部 `command` + `exec` + `wait`）。
- `execute_program(program, args)`：程序 + 参数一步执行。
- `execute_bat(commands, max_concurrent)`：多命令并发执行（`std::atomic` 下标分发 + `max_concurrent` 个工作线程），异常归一化为 `result.exec_error = fail_start` + `exit_code = -1`（`execsys.cpp:46-51`）。**`max_concurrent == 0` 时一个线程都不起，直接返回默认构造的 `result`（`success() == true`）**，见 `DEBUG.md` A6。

### 6. `xhqm::libmange` / `_libary` / `libary` — 动态库加载（`libmange.h/.cpp`，105 / 167 行）
- `loadlib` / `freelib`：`dlopen(path, RTLD_LAZY)` / `LoadLibraryW`。~~（宽字符，支持中文路径）~~ **已作废（2026-10-07 复核 r677）**：Windows 侧路径经 `std::filesystem::path(libpath).wstring()` 转换（`libmange.cpp:110`），该转换按**系统 ACP** 解释窄串，而引擎内字符串约定是 UTF-8（`codesys.h:8`）；`process_base.cpp:347-355` 为同一目的显式用 `CP_UTF8`，两处口径不一致，见 `DEBUG.md` C2。失败返回 `nullptr` 并把原因存入 `thread_local` 的 `last_error()`（`:14-18`、`:104-117`）。
- `function(id, name)`：`dlsym`/`GetProcAddress` 取符号指针（兜底用）。
- 模板：`function_ptr<return_type, args...>` 取强类型函数指针（零运行时开销）；`function<...>` 一步查符号并调用（查不到抛 `xhqm::error`，`libmange.h:37-42`）。
- `has_function` 探测符号、`self()` 取主程序模块句柄（供插件回调，不可 `freelib`；POSIX 侧每次调用是一次 `dlopen(NULL, RTLD_LAZY)`，见 `DEBUG.md` B5）、`depend(dir)`（Windows `SetDllDirectoryW`，POSIX 空实现 —— dlopen 的依赖搜索由 RPATH/LD_LIBRARY_PATH 决定，`:141-150`）、`error()` 取 `thread_local` 最近错误（为空时回退读 `dlerror()`/`GetLastError()`）。
- RAII 封装 `_libary`（仅移动语义，拷贝构造/赋值 `= delete`，`libmange.h:58-77`），经 `xhqm::resource<_libary, libid>` 包装为可共享的 `libary` 句柄（`libmange.h:102`）。

### 7. `xhqm::parallel::threads` 线程池（`threads/threads_pool.h/.cpp`，177 / 483 行）
- 构造参数与默认值：`threads(cored_threads = 1, allow_threads = 1, allow_actions = xhqm::npos, keep_alive = 1000ms)`（`threads_pool.h:105-106`，`npos` = 不限制排队数）。三条校验会抛 `xhqm::error`：核心 ≥ 1、`allow_threads >= cored_threads`、`keep_alive >= 0`（`threads_pool.cpp:23-34`）。
- 状态机：`life`（运行）/ `stop`（暂停，worker 空转不取任务）/ `join`（排空并退出）/ `dath`（立即取消排队任务并退出）；**`join()`/`dath()` 之后池不可复用**（`life()` 只在 `stop` 状态下才恢复，`threads_pool.cpp:302-305`）。
- 提交：`submit(channel, func, args...)` 返回 `parallel::holder<return_type>`（`future_ptr()`、`set_cancel()`）；`submit_list(channel, holders)` 批量；`submit_task(channel, holder, timeout = 0ms)`（`0` 只提交不等、`max()` 等到完成、其他值超时则 `set_cancel()`，`threads_pool.h:148-172`）。
- 多通道（channel）队列：worker 轮转扫描公平取任务（`m_scan_seed`，`threads_pool.cpp:193-205`）；`_ensure_workers_locked` 按 `running + queued` 需求弹性扩缩非核心线程（上限 `allow_threads`，下限 `cored_threads`，`:113-115`）；核心线程常驻。通道表按下标**只增不减**地补齐（`:69-75`）。
- ~~池满时提交会被拒绝。~~ **补充（2026-10-07 复核 r677）**：队列数达 `m_allow_actions`（或池状态不是 `life`）时，`_submit_` 会**取消该任务并 `return false`**（`threads_pool.cpp:415-416` / `:431-435`），但 `submit()` / `submit_list()` **丢弃这个返回值**（`threads_pool.h:123`、`:132`、`:145`）—— 调用方不读 `future` 就什么也不知道（`holder` 的 promise 里被塞了 `xhqm::error("Task is canceled")`，丢弃 holder 等于静默丢任务）。这是 `network/nebula` 的 `life_event` 静默丢事件的模块侧根因，见 `DEBUG.md` B1 与 `network/nebula/DEFERRED.md` 的 B3。
- 任务载体 `parallel::target`（状态 `create/cancel/runing/failures/successes`，`std::atomic_uchar` + CAS 保证状态只转换一次）与 `parallel::holder`（`std::promise`/`std::future`，动作与取消都经 `set_exception` 回传异常）。
- 防御：任务抛异常被捕获不外泄（自定义 target 的 `action` 抛异常会被 worker 吞掉，`:221-231`）；`join`/`dath` 在池内任务中调用会抛 `xhqm::error`（`thread_local current_pool` 检测，`:329-332`、`:361-364`）。

### 8. 其余并发辅助
- `xhqm::exist` / `exist_this<type>`（`threads/exist.h`，87 行）：后台辅线程骨架，`enter`/`leave` 管理生命周期，纯虚 `redoexcept`/`runexcept`/`undoexcept`（或 `exist_this` 的回调版）；`runexist` 是 `redoexcept(); while (m_star) runexcept(); undoexcept();`。
- `xhqm::lock_mutex`（= `std::shared_mutex`）与 `xhqm::lock_get`（共享读）/ `xhqm::lock_set`（独占写）（`threads/lock_mutex.h`，41 行）：RAII 读写锁，构造函数加锁、析构解锁。

## 架构

```
software/system/                                               内容行数
├── codesys.h / codesys.cpp        编码 / 十六进制 / 平台字符集     41 / 229
├── filesys.h / filesys.cpp        文件系统（std::filesystem 封装）35 / 318
├── timesys.h / timesys.cpp        时间戳 / 休眠 / 定时器          114 / 84
├── execsys.h / execsys.cpp        进程执行简化封装（依赖 process）23 / 61
├── libmange.h / libmange.cpp      动态库加载与符号解析（RAII+resource）105 / 167
├── process.h                      ↓ 聚合头（只 include process_base.h）  7
│   └── process/process_base.h / .cpp   跨平台进程执行            130 / 825
└── threads.h                      ↓ 聚合头（exist+lock_mutex+task+pool）10
    ├── threads/exist.h            后台线程骨架                    87
    ├── threads/lock_mutex.h       共享读写锁 RAII                 41
    ├── threads/threads_task.h     target / holder 任务载体        270
    └── threads/threads_pool.h / .cpp  多通道线程池               177 / 483
```

- 七个 `.cpp` 的分工：`process_base.cpp`、`threads_pool.cpp`、`codesys.cpp`、`filesys.cpp`、`timesys.cpp`、`execsys.cpp` 在 `CMakeLists.txt:256-261` 的 `XHQM_BODY_CORE`（恒编），`libmange.cpp` 在 `CMakeLists.txt:533` 的 `XHQM_BODY_SYSTEM`（要开 `-DXHQM_BUILD_SYSTEM=ON`）；`xhqm.vcxproj:119-125` 七个都在，两套构建对得上。
- 分层：上层（`software/signal.h`、`network/nebula/*`、`software/*.cpp`）依赖本模块；`signal` 的异线程槽即经 `parallel::threads` 池派发（`software/signal.h:160`、`:173`，进程级池在 `software/signal.cpp:25-27` 建为 `threads(2, 4)`）。
- 平台抽象集中在 `.cpp`，头文件接口统一（但开关宏有两套，见 `## 注意事项`）。
- 模块内自包含类型别名（`byte`/`text`/`count`/`size` 等）来自 `head.h`；`natural`/`integer`/`npos` 来自 `algebra/value.h`（`:46`、`:56-57`）；`parallel::target::state` 等状态用 `std::atomic_uchar` + CAS 保证线程安全。

## 子模块

| 子模块 | 文件 | 职责 |
|---|---|---|
| 编码 | `codesys.*` | 字节/十六进制/UTF-8/系统编码转换、字符串切分 |
| 文件 | `filesys.*` | 目录/文件判定与增删改查、环境变量、平台标准目录 |
| 时间 | `timesys.*` | epoch 时间戳、休眠、结构化本地时间、计时器 |
| 进程 | `process/*`、`execsys.*` | 跨平台进程执行与并发批量执行 |
| 动态库 | `libmange.*` | dlopen/LoadLibrary 加载、符号解析、RAII 句柄 |
| 线程 | `threads/*` | 线程池、任务载体、读写锁、后台线程骨架 |

## 依赖

- `../../head.h`：基础类型别名（`byte`/`text`/`count`/`size`…）、`XHQM_WINDOWS` 平台宏（`:104`）、`xhqm_io` 导出宏（`:20-32`）、`xhqm::error`。
- `../../algebra/value.h`：~~`value` 类型（`timesys.h`、`process_base.h` 使用）~~ **已作废（2026-10-07 复核 r677）**：本模块只用到该头里的**数值别名与 `npos`**（`natural`/`integer`/`npos`，`algebra/value.h:46`、`:56-57`；`timesys.h:3`、`process_base.h:6`、`threads/threads_task.h:91` 引它）；全模块 **0 处**使用 `xhqm::value` / `xhqm::number`（`grep -n "xhqm::value\|xhqm::number" software/system/*.cpp software/system/*/*.cpp` 无命中）。
- `../../metaclass/resource.h`：`libmange.h` 中 `libary` 的可共享资源包装（`resource<_libary, libid>`）。
- `../pattern/instance.h`：~~`libmange.cpp` 内部使用~~ **已作废（2026-10-07 复核 r677）**：`libmange.cpp:2` 确实 include 了它，但文件内**没有任何 `getInstance`/instance 使用点**（grep 只命中那一行 include），是多余依赖。
- 标准库：`<filesystem>`、`<thread>`、`<chrono>`、`<regex>`、`<codecvt>`、`<shared_mutex>`、`<mutex>`、`<condition_variable>`、`<future>`、`<atomic>`、`<deque>`、`<dlfcn.h>`(POSIX，`libmange.cpp:7`) / `<windows.h>`(Win)；进程侧另有 `<fcntl.h>`、`<sys/wait.h>`、`<signal.h>`(POSIX) 与 `<windows.h>`(Win)。

## 简要用法示例

以下片段都取自仓内真实用法（`test/system_test.cpp`、`software/signal.h`、`test/CMakeLists.txt:190-199` 的目标源列表），可直接放进代码：

```cpp
#include "software/system/filesys.h"
#include "software/system/timesys.h"
#include "software/system/codesys.h"
#include "software/system/execsys.h"
#include "software/system/libmange.h"
#include "software/system/process/process_base.h"   // 或聚合头 "software/system/process.h"
#include "software/system/threads/threads_pool.h"  // 或聚合头 "software/system/threads.h"

// 1. 文件系统（全静态类；返回 bool，失败无原因）
if (xhqm::filesys::is_exists("config.ini")) { /* ... */ }
xhqm::filesys::create_dir("out");                        // 目标已存在 → false
std::vector<std::string> names = xhqm::filesys::file_list("out");   // 裸文件名
std::vector<std::string> dirs  = xhqm::filesys::dir_list("out");    // 完整路径
std::string data = xhqm::filesys::data_directory("myapp");          // 平台数据目录，并自动创建

// 2. 时间与计时（timestamp 单位用别名指定，时间戳都是 UTC 毫秒/纳秒）
xhqm::natural ms = xhqm::timesys::mtime();                        // epoch 毫秒
xhqm::natural ns = xhqm::timesys::nowtime<std::chrono::nanoseconds>();
std::string now = xhqm::timesys::localtime();                     // "2026/10/7 1:33:0:123"
xhqm::nowtime t = xhqm::timesys::systemtime(ms);                  // 拆成 year/month/.../mill
xhqm::timesys::msleep(10);                                        // 休眠 10ms（nsleep/usleep/ssleep 同族）
xhqm::timesys::sleep<std::chrono::milliseconds>(10);              // 模板版

xhqm::timer tm;              // 构造即开始计时
// ... 耗时操作 ...
xhqm::natural used_ns = tm.runtime();       // 已耗时纳秒（mtimes()/stimes() 是毫秒/秒）
xhqm::natural used_ms = tm.mtimes();

// 3. 编码
std::string hex = xhqm::codesys::string2hexa("A");                  // "41"
std::string raw = xhqm::codesys::hexa2string("41");                 // "A"
std::wstring w = xhqm::codesys::string_to_unicode("中文测试");     // UTF-8 → wstring
std::vector<std::string> parts = xhqm::codesys::split_string("a,b,,c", ',');   // 3 项（丢空）

// 4. 进程执行（一步式）与手动控制
xhqm::parallel::process::result r = xhqm::execsys::execute_cmd("echo hello_xhqm");
if (r.success()) { /* r.exec_out 是按行的 vector<string>，r.exec_err 同理 */ }

xhqm::parallel::process proc;                 // 构造参数是超时毫秒，默认 npos=不超时
proc.program("/usr/bin/ffmpeg", {"-i", "in.mp4", "-f", "null", "-"});   // 或 proc.command("a | b")
proc.capture_stdout(true);
proc.function_o([](const std::string& line) { /* 每行回调（会与 stderr 线程并发） */ });
proc.exec();
proc.wait();                                  // 或 proc.wait_for(2000) 探测
if (proc.active()) { proc.terminate(3000); }  // 温和终止→宽限→强杀（POSIX 杀进程组）

// 5. 动态库（RAII：析构自动 freelib；句柄不可拷贝，只能移动）
xhqm::libid id = xhqm::libmange::loadlib("plugin.so");     // 失败返回 nullptr，原因看 libmange::error()
if (id) {
    if (xhqm::libmange::has_function(id, "create_object")) {
        using fn_t = int(*)(int);
        fn_t fn = xhqm::libmange::function_ptr<int, int>(id, "double_it");   // 查一次，零开销调用
        int out = fn ? fn(21) : -1;
    }
    xhqm::libmange::freelib(id);
}

// 6. 线程池（cored, allow, allow_actions, keep_alive）
xhqm::parallel::threads pool(2, 4);                       // 2 核心 / 最多 4 线程 / 排队不限 / 保活 1000ms
auto h = pool.submit<int>(0, []() { return 42; });         // 通道 0；返回 holder<int>
h.future_ptr()->wait();
int v = h.future_ptr()->get();                             // 42（被取消时这里会抛 xhqm::error）

auto hv = pool.submit(1, []() { /* 无返回值也行，模板参数可省 */ });
hv.future_ptr()->wait();

std::vector<xhqm::parallel::holder<int>> batch;
for (int i = 0; i < 10; ++i)
    batch.push_back(pool.submit<int>(i % 2, [i]() { return i * i; }));
pool.submit_list<int>(0, batch);        // 已有 holder 批量重新入队
pool.submit_task(0, h, std::chrono::milliseconds(100));   // 提交 + 最多等 100ms，超时则取消
pool.stop();                            // 暂停（worker 不再取任务）
pool.life();                            // 从 stop 恢复
pool.join();                            // 排空并退出；此后池不可复用，submit 会被静默丢弃
```

自检入口（本模块唯一的测试）：`test/system_test.cpp`（194 行，7 个 `TEST_CASE`：codesys / timesys / filesys / libmange / execsys / process / threads），目标定义在 `test/CMakeLists.txt:190-199`，可直接 `cmake --build <build> --target system_test` 后运行。

## 注意事项

- `process` 的 `wait()` 会先关闭 stdin 写端再回收管道；`command` 模式依赖 shell，参数不可控时优先用 `program` 模式；`command_line` 是“整串直用”的第二档（Windows 不套 shell）。
- `process` 默认 `capture_stderr && stderr_stdout` 都开，stderr 会并进 stdout 管道；`function_o` 在 stdout/stderr 两个读线程里**并发**调用（`m_stderr_stdout == false` 时），回调必须是线程安全的。
- `libmange::self()` 返回的句柄不可 `freelib`。
- 线程池 `join`/`dath` 不允许在其自身任务中调用，否则抛 `xhqm::error`；**`join`/`dath` 之后池不可复用**，之后的 `submit` 全部被取消（静默）。要“停一下再开工”请用 `stop()` + `life()`。
- 提交失败（池非 `life` 或排队数达 `m_allow_actions`）时 `submit()` **不报错也不阻塞**：不检查 `future` 就等于丢任务；`network/nebula/life.cpp:18` 与 `software/signal.h:160` 都是这种用法。
- `filesys` 的 `file_list` 返回裸文件名、`dir_list` 返回完整路径、`path_list` 混装；`rename`/`move_*`/`copy_*` 在目标已存在时一律返回 `false`。
- `codesys` 的 `unicode_*` 仍用 `std::codecvt_utf8`（`codesys.cpp:4`、`:96`、`:101`）：实测 g++ 11.4 `-std=c++17/20/23 -fsyntax-only` 均无诊断，但该设施在 C++17 起已弃用、C++26 移除，长期需迁移（细节与影响见 `DEBUG.md`）。
- 平台开关有两套（`XHQM_WINDOWS` 与 `_WIN32`/`__linux__`/`__APPLE__`），加平台时两处都要看。

## 当前问题清单

本模块**当前仍存在**的问题（功能缺陷 / 资源并发 / 跨平台 / 性能 / 未实现 / 有意取舍 / 文档出入）**统一记在 `DEBUG.md`**，本文件不重复罗列。历史台账保持原位、不改名：`network/nebula/DEFERRED.md`（nebula 侧的“池满即取消 → life_event 静默丢事件”记在其 B3，本模块 `DEBUG.md` 的 B1 是同一条问题的模块侧）。全仓层面的复核说明与“已作废”订正见仓库根 `../../README.md:13` 和根 `../../DEBUG.md`。
