# xhqm · test 可执行验证层 · 当前问题清单

`test` 模块（模块路径 `test`，module id `test`）的**当前仍存在**的问题台账：只记「现在还是坏的 / 还是没接上的 / 还是对不上的」，**不记已修项、不做版本历史**。每条都给出可核对的 `文件:行号`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**：本仓的历史台账**不搬家、不改名** —— `fantasy/fantasy_bugs.md`（fantasy 的静态分析清单）、`network/nebula/DEFERRED.md`（nebula 决定不做的事项）保持原位，本文件只指过去、不复制内容。各模块的「定位 / 规模 / 结论」在其自己的 `README.md` 里；本文件是 `test` 模块的问题清单。
> 三处**同一事实的引擎侧记录**（本文件从测试侧记，落脚点是夹具/构建，不替代它们）：`test/astral_test.cpp` 与 `test/astral_behavior_test.cpp` 的 `kernel_tag.h` 悬空 = `network/astral/DEBUG.md` E4；`test/thing_demo.cpp` 的 `xhqm::thing::kernel` 不存在 = `network/astral/README.md:194`；`test/polydtor_test.cpp` 的 A 世代 API 与缺源文件 = 根 `README.md:270`。
>
> 判据纪律：凡写「编译不过 / 某 API 不存在」，必须附复现命令。本机为 **Linux / g++ 11.4.0 / cmake 3.22.1**，**无 Qt、无 CUDA、无 Windows**，因此凡需 MSVC / GPU / 图形环境才能断言的项一律标「未实跑」。编译判定统一用不落盘的语法检查：
> `cd test && g++ -std=c++17 -fsyntax-only -DXHQM_PROGRAM -I.. <file>.cpp`
> （`ecs_test` / `toon_test` 另加 `test/CMakeLists.txt:262-270` / `:306-314` 那 7 个 `-I../fantasy*` 目录后同样通过。）

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. 两个 astral 夹具引用已删除的 `kernel_tag.h`，两个 target 当前根本编译不过（36 个 TEST_CASE 全不可达）
- 位置：`astral_test.cpp:3`、`astral_behavior_test.cpp:4`（类型用法见 `astral_test.cpp:12`、`astral_behavior_test.cpp:18`）
- 现状：标签层已由 `kernel_tag` 改名 `evoke`（`network/astral/evoke.h:13`），`network/astral/kernel_tag.h` 在 r677 **不存在**；但 `test/CMakeLists.txt:208-216` 仍为这两个 target 建可执行文件并链 `XHQM_TEST_LIB`。两个文件共 36 个 `TEST_CASE`（5 + 31），全部编不进去。
- 证据：`ls network/astral/` → `astral.h evoke.cpp evoke.h kernel.cpp kernel.h mmap_index.* nebula.h thing.* zero.*`（无 `kernel_tag.h`）；`g++ -std=c++17 -fsyntax-only -DXHQM_PROGRAM -I.. astral_test.cpp` → `fatal error: ../network/astral/kernel_tag.h: 没有那个文件或目录`，`astral_behavior_test.cpp` 同样（`:4:10`）。
- 影响：astral 的单测与行为全量在本轮**一条都没被验证**；CMake 配置阶段不报错，只有真去编这个 target 才炸 —— 最容易被当成「有测试」。
- 复现/验证：上面的 g++ 命令（不产出文件）。
- 建议：夹具迁到 `evoke`（类名 `evoke`、接口同名）后再启用，或先删掉这两个 target。

### A2. `polydtor_test.cpp` 停在 A 世代 nebula API 上，编译报 20 条 `error:`（多态释放回归闸门失效）
- 位置：`polydtor_test.cpp:27`（`nebula::kind::none`）、`:37-46`（`to_json`/`from_json`/`equal_other`/`clone`/`broadcast`/`send` 六个 `override`）、`:54`（`nebula::addr_t<nebula::kind::ipv4>`）
- 现状：当前 `addr_b` 的纯虚接口是 `opened`/`closed`/`send_broad`/`send_frame(const json&)`/`send_frame(const json&, sint)`（`network/nebula/addr/addr_b.h:55-59`），枚举是 `kind::no/uuid/ipv4/ipv6/serial/bluetooth/nearlink/quantum`（同文件 `:14-24`），**全仓没有 `addr_t`**（只在测试文件里出现，见 E1）。
- 证据：`g++ ... -fsyntax-only polydtor_test.cpp 2>&1 | grep -c 'error:'` → **20**（其中 6 条是 `marked 'override', but does not override`），另有 `'none' is not a member of 'xhqm::nebula::kind'; did you mean 'no'?`、`'addr_t' is not a member of 'xhqm::nebula'`、`invalid new-expression of abstract class type`。
- 影响：这个夹具是「经基类指针 delete 派生地址对象」的回归闸门（`polydtor_test.cpp:1-8` 记了 arm64 上零输出 SIGTRAP 的事故）；闸门失效期间该缺陷可以静默回来。附带被它拉进编译面的 `parallel::array/share/dispatch/task`（`:90-128`）也一起失去保护。
- 复现/验证：上面的 g++ 命令。编译期探针（stdin 喂 g++，不落盘）显示 `std::has_virtual_destructor_v<nebula::addr_b>` 当前**仍然为真** —— 虚析构来自基类 `network/nebula/nebula.h:64` 与 `software/signal.h:41`，`addr_b` 自身没有声明；所以改名 + 换 override 集合之后用例应当能过。
- 建议：按当前接口改写 `tracked_addr`，`kind::none` → `kind::no`，并去掉 `addr_t` 断言。

### A3. `gpu_smoke` 不校验实际命中的后端：DLL 未编 CUDA 时静默退化成 CPU 对照，仍然返回 0
- 位置：`gpu_smoke.cpp:26-31`（`tensor_backend_select(device::gpu)` 后只 `printf` 后端名）、`:230-232`（退出码只看 `failures`）
- 现状：`tensor_backend_select` 在目标后端创建失败时**静默保持原后端**；`device::gpu` 在无 CUDA 的构建里回退 CPU 或 no 后端。于是本该是「no 后端 vs GPU 后端」的对照，实际变成「no 后端 vs cpu 后端」，而 stdout 只留下一个 `backend = …` 行，没有任何断言。
- 证据：`algebra/tensor/backend/tensor_backend.cpp:33-44`（`case device::gpu:` → `XHQM_TENSOR_CUDA` 才 `create_gpu_backend()`，否则 `TENSOR_WITH_CPU` 下 `create_cpu_backend()`，再否则 no），同文件 `:70-75`（`if (target) active_ptr() = target;`）。
- 影响：看到 `GPU conv smoke: ALL PASS` 的人会认为 CUDA 卷积路径已验证；CI 也无法从退出码区分「真 GPU 对照」和「CPU 自比」。
- 复现/验证：拿一个未编 CUDA 的 `binary/xhqm.dll` 跑 `gpu_smoke` → 打印 `backend = cpu` 且退出码 0（本机无 Windows/CUDA，**未实跑**；回退分支由 `tensor_backend.cpp:33-44` 的 `#if` 结构确证）。
- 建议：加 `CHECK(std::string(tensor_backend_active().name()) == "gpu")`，不是 gpu 就直接非零退出。

### A4. `thing_demo.cpp` 用的 `xhqm::thing` 命名空间与 `thing::kernel` 都已不存在，编译不过
- 位置：`thing_demo.cpp:7`（`using namespace xhqm::thing;`）、`:11`（`thing::kernel k;`）；对照 `network/astral/thing.h:31-41`
- 现状：`thing` 是 `xhqm::astral` 下的**类**（`class thing : public astral::kernel`），内核类名是 `astral::kernel`；没有 `xhqm::thing` 命名空间，也没有 `thing::kernel`。
- 证据：`g++ ... -fsyntax-only thing_demo.cpp` → `error: 'thing' is not a namespace-name; did you mean 'string'?`、`'thing' has not been declared`。
- 影响：事件图 / 对象快照流 / 合并 / 持久化往返这个 demo 跑不到；在它接进构建之前，thing 语义没有手工可跑的验证入口。
- 复现/验证：上面的 g++ 命令（`network/astral/README.md:194` 从引擎侧记了同一事实）。
- 建议：改 `xhqm::astral::thing k;` 并接进 CMake，或删掉该文件；顺带把落盘路径从 `/tmp/thing_demo.json`（`:56,:59`）改成 `filesys::temp_directory()`。

### A5. `sparse_mesh_test.cpp` 全部检查是 `assert`，按本工程惯例接进 Release 构建后会变成空跑
- 位置：`sparse_mesh_test.cpp:21-35`、`:48-59`、`:63`、`:77-81`（22 行含 `assert`，21 处 `assert(`；另 `:4` 是 `#include <cassert>`）；工程强制 Release 见 `test/CMakeLists.txt:4-6`
- 现状：该文件是手写 `int main()` 程序（自陈「仅编译，不链接」，`:1`），用 `assert` + `std::puts` 判定；当前没有任何 target（见 E4）。
- 证据：`grep -c assert sparse_mesh_test.cpp` → 22（`grep -o 'assert(' | wc -l` → 21）；Release 构建下 CMake 会带 `-DNDEBUG`（MSVC `/DNDEBUG`），`assert` 展开为空语句 —— 手工 `g++ -O2 -DNDEBUG -fsyntax-only` 通过、无任何检查生效。
- 影响：现在它编不到；一旦「顺手加个 `add_executable`」，它会以一个永远通过的空壳进场，比不进套件更糟。
- 复现/验证：`grep -n 'assert' sparse_mesh_test.cpp`。
- 建议：改用 `test.h` 的 `CHECK`/`CHECK_NEAR` 再接进 CMake。

---

## B. 资源与并发

### B1. `astral_stress_test` 每段压测开头清零计数，退出码只反映最后一段（7:3 阶段的读失败被吞掉）
- 位置：`astral_stress_test.cpp:147`（`g_read = 0; g_write = 0; g_read_fail = 0;`）、`:174-175`（先 7:3 再 3:7）、`:191`（`return (g_read_fail == 0) ? 0 : 1;`）
- 现状：`run_ratio()` 每次进入都清零三个计数器，`main` 只用最后一段（3:7）的 `g_read_fail` 决定退出码；`[done] … fail=` 打印的也是最后一段的数。
- 证据：`:147` 清零、`:174-175` 连续两次 `run_ratio`、`:191` 判定 —— 三段原文可直接对照。
- 影响：20 线程混合读写阶段出现的读失败（索引段与数据段不一致这类回归）不会让压测返回非零，结论偏乐观。
- 复现/验证：`./build/astral_stress_test gen /tmp/x.bin 1000000 && ./build/astral_stress_test run /tmp/x.bin`（本机**未实跑**：需要先建 1e6 节点库；计数语义由 `:147`/`:191` 直接确证）。
- 建议：计数器只在 `main` 里清零一次，退出码用累计失败数。

### B2. `cs_test` 固定端口 + detached 服务线程 + 无上限自旋：端口被占时整个用例**挂死**而不是失败
- 位置：`cs_test.cpp:16`（`if (!srv.listen(19527)) return;`）、`:22`（`server.run()` 阻塞）、`:27-29`（`detach()` + `while (!g_listening) sleep(5ms)`）、`:32`（连 `127.0.0.1:19527`）
- 现状：`listen` 失败时 `server_main` 直接 return，`g_listening` 永远为 false；主线程在只有 5ms 步进、**没有超时**的循环里等；服务线程是 detached 的，用例结束后进程仍在监听。
- 证据：`:16` 的提前 return 与 `:29` 的无限循环是唯一的同步点。
- 影响：机器上已占用 19527（或并发跑两个 `cs_test`）→ 该用例挂住，Catch2 没有超时机制，只能强杀；固定端口也让并行运行不可能。
- 复现/验证：先 `nc -l 127.0.0.1 19527`（或 `python3 -m http.server 19527`）再跑 `cs_test` → 卡在 `:29`（本机**未实跑**，需先构建；代码路径由 `:16`/`:29` 确证）。
- 建议：`listen` 失败也置 `g_listening = true`，让用例以 `CHECK(false)` 失败；自旋加次数上限；端口改 0 让系统分配。

### B3. `astral_test` 的三个持久化临时文件写在**当前工作目录**且从不清理
- 位置：`astral_test.cpp:113`（`astral_test_tmp.json`）、`:126`（`astral_test_tmp.bin`）、`:134`（`astral_test_tmp.mmap`）
- 现状：三个都是相对路径；用例结束没有任何删除动作；同目录 `test/` 的忽略规则只有 `build` 一条，并没有忽略这些名字。
- 证据：`svn propget svn:ignore test` → `build`（单条）；`:113/:126/:134` 三个文件名原文。
- 影响：在 `test/` 下跑一次就多 3 个未版本控制文件（`svn status` 变脏）；`.mmap` 还被 `mmap_index idx`（`:136-137`）打开着。
- 复现/验证：该 target 当前编不过（A1），所以**暂时跑不出残留**；修好编译后跑一次即出现 —— 位置由 `:113/:126/:134` 确证，产出行为未实跑。
- 建议：写到 `filesys::temp_directory()` 下，并在用例末尾清理。

### B4. `astral_stress_test gen` 对规模无上限，按 count 线性写盘并一次性分配等长缓冲
- 位置：`astral_stress_test.cpp:68-121`（单个 `std::ofstream` 一路写完）、`:116`（`std::vector<char> blob(count, (char)0x80);`）、`:120`（打印 `size=%.2fGB`）、`:185`（`strtoull(argv[3], …)` 原样接受 count）
- 现状：`gen <file> <count>` 的 count 不做任何上限检查，节点流与数据段都按 count 线性增长（文件自陈 1e8 节点的量级在注释 `:3-5`）。
- 证据：`:87-105` 每节点一次 CBOR 写、`:116-117` 一次性 `count` 字节缓冲、`:120` 自己打印 GB 数。
- 影响：误用一个过大的 count 会写满磁盘；`ofstream` 的失败状态也没有检查（写满/中途失败会静默产出残库）。体积按「索引条目 × count + 数据段 count 字节」线性增长 —— 单条索引是 `{uuid, info, links, off, size}` 的 CBOR map，**具体体积本机未实测**（见下）。
- 复现/验证：`./build/astral_stress_test gen /tmp/astral_big.bin 1000000000`（**本机未跑**：会写出数十 GB 的库，故未按 verbatim 执行；「无上限」这一点由 `:185` 的 `strtoull` 与 `:87-117` 的无界循环确证）。
- 建议：生成前按 count 估算体积并拒绝超阈值，或加显式 `--limit`。

---

## C. 跨平台

### C1. `astral_stress_test` 的内存读数在非 Windows 恒为 `0.00GB`
- 位置：`astral_stress_test.cpp:33-41`（`working_set_gb()` 主体整段被 `#ifdef _WIN32` 包住，非 Windows 直接落到 `return 0.0;`）、调用点 `:172`、`:177`
- 现状：只有 Windows 走 `GetProcessMemoryInfo`；Linux/macOS 上 `rss=%.2fGB` 恒打印 `0.00`。
- 证据：`:35-39` 的 `#ifdef _WIN32` 与 `:40` 的 `return 0.0;`。
- 影响：Linux 上跑压测看不到内存曲线 —— `load_index` 是否把索引/数据全读进内存、20 线程 `update_data` 是否让内存只增不减，全都没有读数。
- 建议：非 Windows 分支读 `/proc/self/statm` 或 `getrusage(RUSAGE_SELF)`。

### C2. `view/main.cpp` 的默认模型与场景临时文件是 Windows 盘符绝对路径
- 位置：`view/main.cpp:42`（`k_default_model = "D:/WorkSpace/Model3D/KayKit-Character-Animations/…/KayKit_AnimatedCharacter_v1.2.glb"`）、`:46`（gizmo 模型 `D:/WorkSpace/Model3D/CoordinateGizmo/gizmo.glb`）、`:365`（场景序列化临时文件 `D:/WorkSpace/xhqm_v2/binary/_scene_test.json`）
- 现状：无参启动与 gizmo 自检都直接用这两个绝对路径；场景 round-trip 自检写死在同一盘符的另一个目录。
- 证据：`grep -n 'D:/' view/main.cpp` → 3 处（`:42`、`:46`、`:365`）。
- 影响：非 Windows 或该盘符不存在时，`[GLTF]`/`[SCENE]` 自检直接走失败分支（`:386`、`:380`），`--snapshot` 拍出来是空场景；这类「跑起来没报错但什么都没验」最难发现。
- 建议：改成参数 / 环境变量 / 随包相对路径。

### C3. `ffbo` 子工程写死 Windows 布局（Qt / MSVC / mujoco / 静态库），非 Windows 无法配置
- 位置：`ffbo/CMakeLists.txt:12-13`（`QT_ROOT "C:/Qt/5.15.2/msvc2019_64"`、`MUJOCO_ROOT "D:/WorkSpace/_research/digital-fruit-fly/depend/mujoco"`）、`:16`（`find_package(Qt5 REQUIRED COMPONENTS Gui)`）、`:11`（`../../../binary/xhqm.lib`）、`:32-38`（`FLY_LIBS`：`Qt5::Gui` / `xhqm.lib` / `glew32.lib` / `opengl32` / `mujoco.lib`）、`:29`（`${MUJOCO_ROOT}/include`）
- 现状：所有依赖路径都是字面量、没有 CACHE 变量；`MUJOCO_ROOT` 还指向仓库外**另一棵树**（`digital-fruit-fly`）。
- 证据：上述行原文；`ffbo/CMakeLists.txt:9-13` 的 5 个 `set(...)` 没有一个带 `CACHE STRING`。
- 影响：Linux/macOS 上无法配置/构建该子工程（只能读代码）；Windows 上若那两棵外部目录不在原位，同样失败。
- 复现/验证：本机**未实跑 configure**（无 Qt5）；结论来自配置文本。
- 建议：全部改成 `set(... CACHE ...)` + `find_package`，与 `view/CMakeLists.txt:23` 的 `QT_ROOT "QT_PATH/" CACHE STRING` 写法对齐。

---

## D. 性能

### D1. 两条 diffusion 用例在单测里跑上千步训练（3000 / 1500 步），时长随机器浮动
- 位置：`soul_diffusion_test.cpp:63`（`batch = 64, steps = 3000`）、`soul_diffusion_image_test.cpp:35`（`batch = 16, steps = 1500`，UNet 16×16、`base_ch = 16`）
- 现状：两条都用固定种子训练到收敛再断言（`soul_diffusion_test.cpp:80-82`、`soul_diffusion_image_test.cpp:52-54`），不是快速单元检查；`soul_diffusion_image_test` 还额外编入 CPU 后端（`test/CMakeLists.txt:116-134`）。
- 证据：上述常量原文与训练循环（`soul_diffusion_test.cpp:70-76`、`soul_diffusion_image_test.cpp:42-48`）。
- 影响：它们大概率是「跑一遍全部测试」里最慢的两条；Catch2 无超时，慢机器上时间被它们主导。**本机未实测时长**（未构建，也无 CPU 后端基准）。
- 建议：降到能稳定收敛的步数，或把收敛性检查挪到 bench 程序。

### D2. `ecs_test` 在 TEST_CASE 里建 10 万节点做 benchmark，且只打印耗时、无任何阈值断言
- 位置：`ecs_test.cpp:303`（`constexpr xhqm::count N = 100000;`）、`:306-316`（10 万次 `new fantasy::models()` + `add_child_ptr`）、`:324-328`（10 万次 `tuple_values::push`）、`:342-344`（只 `std::cout` 打印 `tree_traverse` / `soa_batch`）、`:347-354`（只对结果抽样断言）
- 现状：耗时数据只打印不判定；用例的断言全部落在「结果一致」上（`:347-354`），性能回归不会让任何用例失败。
- 证据：上述行原文；`grep -n 'tree_ms\|ecs_ms' ecs_test.cpp` 只有 `:321/:340/:343`（都是取值与打印）。
- 影响：单次运行的内存/时间开销固定在测试里（10 万堆对象），而产出无人消费 —— 既慢又不起回归作用。
- 复现/验证：本机**未实跑**；规模常量与打印行为由 `:303`/`:342` 确证。
- 建议：N 降到 1e4 或挪到 bench；若要防回归，加一条宽松上界断言。

---

## E. 未实现 / 占位桩

### E1. `test/nebula_api/` 整棵树没有构建入口，且测试源停在已被删除的 API 世代上（死代码）
- 位置：`nebula_api/nebula_api_test.cpp:28`（`xhqm::nebula::link_b`）、`:39`（`nebula::addr_t<nebula::kind::ipv4>`）、`:58/:111/:243/:448/:457/:466/:586/:592`（`nebula::link_t<nebula::kind::ipv4>`）、`:454/:455/:522/:591`（`/home/xhqm/Space/xhqm-pro/depend/mediamtx/mediamtx`）
- 现状：`nebula_api/` 下没有 `CMakeLists.txt`，`test/CMakeLists.txt` 里也搜不到该目录；测试文件用的 `link_b`/`link_t`/`addr_t` 三个名字**在引擎里已不存在**（全只在测试文件出现）。
- 证据：`find nebula_api -name 'CMakeLists.txt' -o -name '*.cmake' -o -name 'Makefile'` → 0 命中；`grep -rn '\b\(link_b\|link_t\|addr_t\)\b' --include=*.h --include=*.cpp . | grep -v '^./test/'` → 0 命中。
- 影响：`nebula_api/README.md`（14KB）与 `PROTOCOL.md`（6.7KB）描述的接口契约没有任何可执行验证，接手者会以为它在构建里；四行硬编码路径还指向仓库外另一棵树（`xhqm-pro`）的 mediamtx 可执行文件。
- 复现/验证：上面两条 find/grep。
- 建议：要么补一个 target 并把测试迁到 B 世代 API（`link`/`addr_*`/`zero`），要么在 `nebula_api/README.md` 顶部标明「未接构建、API 已过期」。

### E2. `view/main.cpp` 有 5 段渲染自检被 `#if 0` 折叠（含 bindless / SSAO+TAA / Hi-Z 遮挡剔除 / GPU 蒙皮）
- 位置：`view/main.cpp:466-519`（`#if 0`…`#endif`，内含 `[GSKIN] :465`）、`:522-768`（内含 `[HIZ] :521`、`[HIZCULL] :581`、`[BINDLESS] :618`、`[SSAO+TAA] :637`）
- 现状：被折叠区里的判定 printf 在 `:509`、`:573`、`:610`、`:631`、`:730`、`:736`、`:743`；**当前真正会执行的只有 7 段**：`[BC1] :329`、`[GLTF] :354`、`[SCENE] :364`、`[BLEND] :390`、`[CUBE] :414`、`[PSHADOW] :427`、`[FLOAT] :450`。
- 证据：`grep -n '#if 0' view/main.cpp` → `:466`、`:522`；`grep -n '#endif' view/main.cpp` → `:519`、`:768`（另外 `:24/:37/:146/:155/:168` 是其它条件编译块）。
- 影响：GPU 蒙皮、Hi-Z 金字塔、两阶段遮挡剔除、bindless 句柄、SSAO/TAA/SSR 读回**当前没有任何自动化验证**；README 的「就地验证」清单若不区分，会让人以为它们在跑。
- 复现/验证：上面两条 grep 的行号对照。
- 建议：把折叠块拆成 headless 用例（与 `test/README.md` §7 的建议方向一致）。

### E3. `test/view/build/` 是未入库的构建残留（`svn:ignore` 掩盖，它不是源码树）
- 位置：`test/view/build/CMakeCache.txt`（18472 B）、`test/view/build/CMakeFiles/`、`test/view/build/Makefile`（6059 B）、`test/view/build/cmake_install.cmake`（1651 B）
- 现状：这是一次 Linux 上 `cmake -B build` 的产物（mtime 2026-10-02 18:45/18:55），`build` 被忽略规则盖住，所以 `svn status` 不会报它。
- 证据：`svn propget svn:ignore test/view` → `build`；`svn status test/view/build` → `I`（ignored）；`svn info test/view/build` → `W155010: 找不到节点`（未入库）。
- 影响：复跑 `cmake -B build` 会沿用里面的缓存（含当时写死的 `QT_ROOT`/`XHQM_LIB` 等路径），「配置为什么对不上」会被它绕进去。
- 复现/验证：上面两条 svn 命令（只读；**未做任何 svn 写操作**）。
- 建议：直接删掉该目录，或改用 `build/` 以外的名字。
- 说明：本条的类别是「工作副本残留」而非占位桩，归入本节是为了不打乱既有分组。

### E4. `sparse_mesh_test.cpp` 与 `thing_demo.cpp` 没有任何构建入口（编不到、跑不到）
- 位置：`test/CMakeLists.txt`（全文无这两个文件名）；文件本体 `sparse_mesh_test.cpp`（86 行）、`thing_demo.cpp`（66 行）
- 现状：两个手写 `int main()` 程序都不在任何 `add_executable` 里；`test/` 根目录 27 个 `.cpp` 里只有它们没有 target。
- 证据：`grep -rn 'sparse_mesh_test\|thing_demo' test/CMakeLists.txt` → 0 命中；`grep -c add_executable test/CMakeLists.txt` → 25。
- 影响：「看一眼以为在套件里、其实没人跑」。两者还各自带一个硬问题：`thing_demo.cpp` 现在编不过（A4），`sparse_mesh_test.cpp` 的检查全是会在 Release 下消失的 `assert`（A5）。
- 建议：要么接进 CMake 并改成 Catch2 夹具，要么删掉。

### E5. `test/ffbo` 的 `svn:ignore` 第一条带 UTF-8 BOM，`*.exe` 这条规则实际上不生效
- 位置：`svn propget svn:ignore test/ffbo` 的第一行（属性值以 `c3 af c2 bb c2 bf` 开头，即被重复编码的 BOM `\uFEFF`，紧跟 `*.exe`）
- 现状：整条规则的字面量是 `ï»¿*.exe`，而不是 `*.exe`；其余 8 条（`*.obj`、`build`、`*.txt`、`*.csv`、`*.png`、`*.jpg`、`*.log`、`_dend_exp`）正常。
- 证据：`svn propget svn:ignore test/ffbo | head -1 | hexdump -C` → `c3 af c2 bb c2 bf 2a 2e 65 78 65 0a`（`ï»¿*.exe\n`）；对照 `svn propget svn:ignore -R test` 输出的同一行也带 BOM。
- 影响：按 `ffbo/README.md` 手工编译 `mcns_net` / `fly_mcns_view` 后产生的 `.exe` **不会被忽略**，会在 `svn status` 里以 `?` 出现（本次实测工作副本里暂无 `.exe`，所以还没暴露）。
- 复现/验证：上面的 `hexdump` 命令（只读，未改任何属性）。
- 建议：把第一条重写成不带 BOM 的 `*.exe`（`svn propset svn:ignore`）；这属于工作副本属性，本轮未改。
- 说明：本条的类别是「忽略规则未生效」而非占位桩，归入本节是为了不打乱既有分组。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 一 test 一可执行、没有 `enable_testing()`/`add_test()`，必须逐个手跑
- 位置：`test/CMakeLists.txt:2-3`（强制 Release 的注释：跨 DLL 边界的 ABI 错乱）、全文 `grep -c 'enable_testing\|add_test'` = 0、各 target 的源文件列表（如 `:16-25` 的 `algebra_test` 直接列了 7 个 `../…` 源文件）
- 为什么现在这样是对的：① 链 `binary/xhqm.lib`（DLL 导入库）与直接编入引擎源文件是两套宏/ABI 世界（`XHQM_DYNAMIC` vs `XHQM_PROGRAM`），塞进一个 ctest 会踩 ABI 坑；② 每个 target 自带最小源文件闭包（本文件共引用 47 个不同的 `../…` 源文件），ctest 无法表达「哪些用例需要先有主库」这个前置。
- 代价：25 个可执行文件没有统一汇总（本仓无 `CTestTestfile.cmake`、无 CI 配置），"全绿"只能靠人逐个跑；同一份引擎源被重复编译很多次；跨 target 无法共享 fixture。

### F2. `tensor_cpu_test` 用「运行时能力自检 → `WARN` → `return`」跳过仅 SIMD 层实现的用例
- 位置：`tensor_cpu_test.cpp:131-140`（`int_unary_available()`）、`:142-149`（`f16_unary_available()`）、`:164-168`、`:402`、`:452`（三处门）
- 为什么现在这样是对的：无 SIMD 层的构建里标量 switch 不写结果，早前直接读栈上未初始化值判定是「侥幸通过」（作者在 `:162-163` 写明了这件事）；做能力门比让用例假绿更诚实，用例也留在册。
- 代价：Linux/macOS 上这些用例**永不执行**，而 Catch2 的 `WARN` 不影响退出码，绿报告里看不出覆盖缺口；与 CMakeLists 注释「真实命中 SIMD 层」的说法对不上（见 G1）。

### F3. `gpu_smoke` 只在 MSVC 下建 target
- 位置：`test/CMakeLists.txt:60-69`（`if(MSVC)` 包住 `add_executable` + `XHQM_DYNAMIC` + `/UXHQM_PROGRAM` + 链 `binary/xhqm.lib`）
- 为什么现在这样是对的：它按 dllimport 语义链主库，并靠 `/UXHQM_PROGRAM` 撤掉全局定义；`/U` 是 MSVC 选项，gcc 会把 `/UXHQM_PROGRAM` 当成链接输入报错（`:61` 注释），而且 Linux 侧只有 `binary/libxhqm.a`、没有 `xhqm.lib`。
- 代价：非 Windows 平台**完全没有** GPU 冒烟入口；Windows 侧也缺「没编 CUDA 就不算通过」的判定（见 A3）。

### F4. `test/view/` 与 `test/ffbo/` 各持一份 `RhiViewport`（已经分叉）
- 位置：`view/RhiViewport.h`（106 行）+ `view/RhiViewport.cpp`（600 行）对 `ffbo/RhiViewport.h`（144 行）+ `ffbo/RhiViewport.cpp`（687 行）；ffbo 版多出 `frame_count()`/`frame_ms_fg()`/`frame_ms_cpu_fg()`/`frame_ms_fg_max()`/`frames_slow()`/`per_pass()`/`pass_ms()`/`gizmo()`（`ffbo/RhiViewport.h:47-77`）
- 为什么现在这样是对的：ffbo 的视口用来跑仿真帧，需要逐 pass 计时与「默认关掉 gizmo」；后者有实测依据 —— gizmo 的 4 个 drawer 每帧发 199 次 draw、每次 0.196ms，约 39ms/pass（`ffbo/RhiViewport.h:72-77` 的注释），纯仿真不需要。查看器要的是拾取/选中/多窗口，两者取舍不同。
- 代价：同一层 QWindow 壳两份实现，公共修复要改两处；`attach_world`/`snapshot_to`/`watch_orange*` 这些同名 API 的语义漂移没有测试兜底（`view/` 与 `ffbo/` 都没有自己的 README/DEBUG，只有本模块这两份文档提到）。

### F5. `polydtor_test` 的源文件清单是手工维护的「最小闭包」
- 位置：`test/CMakeLists.txt:156-172`（14 个引擎源文件 + 夹具本身：`../network/nebula/addr.cpp`、`uuid/md5/network/socket/tcp/udp`、`timesys/execsys/process_base/threads_pool`、`geometry/ftype/ctype`）
- 为什么现在这样是对的：`test/` 是独立工程（自带 `project()`，根工程不 `add_subdirectory` 它），主库靶子 `xhqm` 在这里不可见，而 `binary/xhqm.lib` 只在 Windows 存在，所以只能显式列源文件（注释 `:152-155` 就是这么写的）。
- 代价：闭包随依赖漂移 —— 现在缺 `network/nebula/addr/` 下**全部 5 个 `.cpp`**（`addr_b.cpp`、`addr_ipv4.cpp`、`addr_ipv6.cpp`、`addr_serial.cpp`、`addr_uuid.cpp`）。而列进去的 `addr.cpp` 恰好需要它们：`addr.cpp:14-19` 的 `addr_from` 用 `std::make_shared<addr_uuid/addr_ipv4/addr_ipv6/addr_serial>` 构造四类地址（四个构造函数都在各自的 `.cpp` 里定义），`addr_b::make_json`（`addr_b.cpp:39`）又是 `addr_b` 的 key function —— 即 vtable 也落在 `addr_b.cpp`。因此**即便 A2 的 API 名修好，链接仍会缺符号**。

---

## G. 文档与代码的出入

### G1. 「验证调度链真实命中 SIMD 层」只在 MSVC 成立，Linux/mac 构建里这些用例会被跳过
- 位置：`test/CMakeLists.txt:35-36`（tensor_cpu_test 上方注释）、`:45-56`（MSVC 才给 `TENSOR_WITH_SIMD`/`TENSOR_WITH_BLAS` + `/arch:AVX2`，`else` 分支只给 `XHQM_DEVICE_CPU TENSOR_WITH_CPU`）、`tensor_cpu_test.cpp:1-3`（文件头注释）
- 现状：`else` 分支（`:55`）没有 `TENSOR_WITH_SIMD`，而文件里三个能力门（`:164-168`、`:402`、`:452`）在没有 SIMD 层时会 `WARN` + `return`；`:13-15` 只断言后端名等于 `"cpu"`（两边都成立，不构成 SIMD 覆盖）。
- 证据：上述行原文；`grep -n 'WARN(' tensor_cpu_test.cpp` → 3 处。
- 影响：把「CPU/SIMD 调度链已验证」当结论的人会高估覆盖 —— 真实的 SIMD 覆盖只存在于 MSVC + `/arch:AVX2` 构建。
- 建议：注释改成「MSVC 构建验证 SIMD 命中；其它平台只验证标量/多线程路径」。

### G2. `structure_extended_test.cpp` 同一文件里对 `traverse` 回调参数的两处说明互相矛盾
- 位置：`structure_extended_test.cpp:97`（`// child_tree 的 traverse 传节点指针（非 data，与 parent_tree 不一致）`）与 `:113`（`// 树族 traverse 统一传节点指针`）
- 现状：两族**都**传节点指针，`:97` 的括注是错的。
- 证据：`lattice/tree.hpp:106` 与 `:143` 是 `function_(node, …)`（`node` 为 `__node_type*`，即 `child_tree::traverse` 最终走的路径 `child_tree.hpp:349-352` → `iterate_dfs`）；`lattice/parent_tree.hpp:429` 是 `function_(traverse_ptr, …)`（`node_ptr`）。
- 影响：照 `:97` 写回调的人会以为 child_tree 给的是 `data`；用 `auto` 形参时不会报错，只是悄悄拿到指针。
- 建议：删掉 `:97` 的括注。

### G3. `metaclass_test.cpp` 的注释还留着「复制构造可能丢 dimensions」的旧疑虑，实际不会丢
- 位置：`metaclass_test.cpp:73`（`// 复制构造应保留 dimensions（可能丢，记录）`）
- 现状：拷贝构造显式复制了维度数组，用例本身也断言保留并通过。
- 证据：`metaclass/buffer.h:116-119`（`buffer_dim(const buffer_dim& _copy) noexcept : m_dimensions(_copy.m_dimensions), m_buffer(_copy.m_buffer)`）；用例 `metaclass_test.cpp:74-76` 断言 `d2.size() == 12` 且 `dimensions() == {3,4}`。
- 影响：注释与紧跟其后的断言打架，读者会怀疑这个检查是「碰巧过的」。
- 建议：删掉「（可能丢，记录）」。

### G4. `polydtor_test.cpp` 头注释写的修法（`addr.h` 加 `virtual ~addr_b() = default;`）已随重构失效
- 位置：`polydtor_test.cpp:8`；对照 `network/nebula/addr/addr_b.h:36`（`class addr_b : public node, public xhqm::software::signal_receiver`，**自身没有** `virtual ~` 声明）与 `network/nebula/nebula.h:64`（`virtual ~node() = default;`）、`software/signal.h:41`（`virtual ~signal_receiver()`）
- 现状：结论（经基类指针 `delete` 派生对象安全）仍然成立，但提供虚析构的已经不是 `addr_b` 自己，而是基类 `node`（另一基类 `signal_receiver` 也有虚析构）。
- 证据：`grep -n 'virtual ~' network/nebula/addr/addr_b.h` → 0 命中；编译期探针（`printf … | g++ -std=c++17 -fsyntax-only -DXHQM_PROGRAM -I.. -x c++ -`，不落盘）`static_assert(std::has_virtual_destructor_v<xhqm::nebula::addr_b>)` 通过。
- 影响：照注释去 `addr.h` 找那行声明会找不到，容易误判成「回归了」；真正要守住的是 `nebula.h:64` 的基类虚析构不被去掉。
- 建议：注释改成「虚析构由基类 `node`（`network/nebula/nebula.h:64`）提供，勿删」。
