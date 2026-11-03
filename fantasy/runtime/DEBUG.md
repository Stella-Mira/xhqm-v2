# xhqm/fantasy/runtime — 当前仍存在的问题

`fantasy/runtime`（模块 id `fantasy/runtime`，工作时副本 r677）的**当前仍存在**的问题台账：只记「现在还是坏的 / 还是没写完的 / 还是对不上的」，每条都给出 `文件:行号`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**：本仓的历史台账**不搬家、不改名**，本文件只指过去、不复制内容。`fantasy/fantasy_bugs.md` 覆盖整个 fantasy 子系统（编号 1–53，含 runtime 的 `command_recorder` #23/#24、`gltf_loader` #25/#26/#37、`scene_io` #27/#36，均已修，故本文件不再重复）；`network/nebula/DEFERRED.md` 是「决定不做」的事项，与本模块无关。模块的定位 / 用法 / 依赖 / 成熟度在 [README.md](README.md) 里，本文件只回答问题清单。各模块的 `DEBUG.md` 与本次全仓复核同批建立。
>
> **判据纪律**：凡写「编译不过 / 某 API 不存在 / 某文件缺失 / 某行为一定这样」，必须附复现命令与验证版本。本仓有「结论被推翻后只在别处订正」的前例（`network/README.md:344`、`xhqm/README.md:240`、本模块 README 的三处 `已作废` 标注），故每条都写了验证方式；给不出复现命令的写「未复现」。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `scene_save` 对尺寸不符的属性数组是静默丢弃，往返后数据永久丢失且仍返回 `true`
- 位置：`scene_io.cpp:84`（normals）、`:90`（uvs）、`:96`（colours）、`:102`（tangents）
- 现状：四个可选属性各自用 `if (p->normal().size() == n * 3)` 之类的等式守卫，**不成立就整段不写**，既不报错也不记 `error`；函数末尾 `:114-115` 无条件 `f << root.dump(2); return true;`。
- 证据：`:84` `if (p->normal().size() == n * 3)` / `:90` `if (p->flipuv().size() == n * 3)` / `:96` `if (p->colour().size() == n * 3)` / `:102` `if (p->tangent().size() == n * 3)`；对照读取侧 `scene_load` 用的是同一套等式（`:132-135`）但**会** `continue` 掉不合法项（那侧正确）。
- 影响：任何「只设了 positions、没设 normals」或「顶点色数组长度非 `n*3`」的 prefab，`scene_save` → `scene_load` 往返后属性消失，调用方只看到成功。这是**静默数据损坏**，不是崩溃，所以能一路走到线上。
- 复现/验证：`test/view/main.cpp:354-385` 的 `[GLTF]` / `[SCENE]` 自检只比对 `prefabs` 数量与三角形总数（`:374`），**查不出属性丢失**。最小验证：构造一个只 `size()+vertex()` 的 prefab 走一遍 save/load，检查 `back[0]->normal().empty()` 为真而 `scene_save` 返回 `true`。
- 建议：把静默跳过改成写 `error` 并返回 `false`，或在格式里显式记录「本项缺省」。

### A2. `gltf::load` 遇到带多个 primitive 的 mesh 时，每个 primitive 拿到各自的 material 克隆，与「同材质共享」的直觉相反
- 位置：`gltf_loader.cpp:298`（`p->material(make_material(root, prim["material"].get<size_t>()))`）
- 现状：循环 `for (const json& prim : mesh.value("primitives", json::array()))`（`:235`）里每命中一个 primitive 就调一次 `make_material`，而 `make_material` 内部 `:152` 是 `auto m = std::make_shared<material_desc>();` —— **每次新分配**。同一 `mesh` 的 N 个 primitive 引用的都是同一个 `materials[i]`，却得到 N 份独立 `material_desc`。
- 证据：`:152` `std::make_shared<material_desc>()` 在 `make_material` 内、无缓存；调用点只有 `:298` 一处。
- 影响：调用方若按「同索引材质是同一对象」写逻辑（例如加载后统一改 `metallic`、或按指针去重做材质批处理），会只改到一部分 —— 表现为**同一模型的不同图元外观/批处理数量不一致**。当前无调用方做这件事（见 E2），所以未爆。
- 复现/验证：加载任意含多 primitive mesh 的 `.glb`，比较同 mesh 内两个 prefab 的 `material().get()` 指针是否相等 —— 实测必然不等。**本机未跑该复现**（只做了源码判读）。
- 建议：在 `load()` 内按 glTF material 索引缓存一次 `shared_ptr` 再分发。

### A3. `scene_save` 写完不检查流状态就返回 `true`
- 位置：`scene_io.cpp:112-115`
- 现状：`std::ofstream f(path, std::ios::binary); if (!f) { error = ...; return false; }` 只校验**打开**成功；随后 `:114` `f << root.dump(2);` `:115` `return true;` —— 中途写盘失败（磁盘满、配额、远端断开）不会被发现。
- 证据：`:112` 打开检查、`:114` 写入、`:115` 无任何 `f.good()` / `f.flush()` / `f.close()` 后的状态判断。对照 `gltf_loader.cpp:25` 的 `read_file` 至少写了 `return f.good() || f.eof();`。
- 影响：调用方以为场景已落盘，实际得到截断的 JSON；下次 `scene_load` 走 `json::parse(..., false)` 得到 `is_discarded()` 才报 `invalid scene JSON`（`:123-124`），错误点离根因很远。
- 复现/验证：向一个只读目录或已满卷写 4 GiB JSON（`dump(2)` 后体积会显著膨胀）观察返回值。**未复现**。
- 建议：`f.flush()` 后判 `f.good()`，失败即 `return false` 并填 `error`。

---

## B. 资源与并发

### B1. `async_loader` 的状态字段全程无锁读写，`start()`/`stop()`/`submit()` 互相竞争
- 位置：`async.cpp:36-38`（`start()`）、`:44-48`（`stop()`）、`:60`（`submit()`）；字段声明 `async.h:29-30`
- 现状：`m_running` / `m_stop` 是**裸 `bool`**（不是 `std::atomic<bool>`，也没有统一放到 `m_mutex` 下）。`start()` 在 `:36-38` 无锁判读并写 `m_stop=false; m_running=true;`；`stop()` 在 `:44` 无锁判读 `m_running`；`submit()` 在 `:58-61` 的锁**内**读 `m_running`。这是典型数据竞争，形式上 UB。
- 证据：`grep -n "m_running\|m_stop" async.cpp` → `:36,37,38,44,47,48,60`，其中 `:36-38`、`:44` 在锁外；对照 `m_queue` 的每次访问（`:14,16,17,61,71`）都在锁内。同一模块的 `command_recorder` 已经把这组字段改成 `std::atomic<bool>`（`command_recorder.h:26-27`，即 `fantasy_bugs.md` 的 #23 修复），`async_loader` **没有**跟着改。
- 影响：触发条件是「主线程之外还有人调 `start()`/`stop()`/`submit()`」——当前无任何调用方（见 D3），所以是隐患不是现症。一旦接入（例如加载线程内提交续加载），worker 的 `m_cv.wait` 谓词（`:14`）可能读不到 `m_stop`，`stop()` 就可能在**队列还有任务**时永久阻塞在 `:15` 的 `return` 之前，或反过来让 `join()` 等到超时。
- 复现/验证：`grep -n "m_running\|m_stop" fantasy/runtime/async.cpp | grep -v "lock"`。构造「一个线程反复 `submit()`、另一线程 `stop()`」的循环跑 TSan 即可命中。**未复现**。
- 建议：照 `command_recorder` 的先例改 `std::atomic<bool>`，并把 `stop()` 的 `m_running=false` 与 `join()` 之间加状态机。

### B2. `async_loader::stop()` 反手清空 `m_completed`，把待执行的 GPU 上传回调直接丢掉
- 位置：`async.cpp:53`（`m_completed.clear();`），配合 `:51` 的 `join()`
- 现状：`join()` 之后队列已被 worker 排空，但 worker 在 `:24` 把每个 `completion` 推进了 `m_completed`；`stop()` 紧接着 `:53` 无条件 `m_completed.clear()`，这些回调**永远不会被 `poll()` 执行**。而 `async.h:40` 明确写着 `completion` 是「在主线程 poll 时执行（GPU 上传）」。
- 证据：`async.cpp:22-25`（worker 推 completion）、`:42-54`（stop 全流程）、`:66-74`（poll 是唯一的消费点）。`stop()` 没有任何「已完成的先跑掉」的步骤。
- 影响：`stop()` 通常在关闭/切场景时调用，此时正在飞的上传回调被丢弃 → GPU 侧的 VBO/纹理**没被创建或没被销毁**，配合引用计数就是句柄泄漏，或在后续复用旧句柄时拿到悬垂 id。当前无调用方故未爆。
- 复现/验证：`submit()` 一个立刻完成的 task、等 `completed()==1` 后 `stop()`、再 `poll()` —— 回调不执行。**未复现**（无调用方，需自建宿主）。
- 建议：`stop()` 里先 `poll()` 一次再清空，或把 `clear()` 改成「交给调用方显式排空」。

### B3. `gpu_timer::begin()` 失败时泄漏已创建的时间戳，`m_ready` 语义不随调用重置
- 位置：`performance.cpp:27-35`、`performance.h:36-38`
- 现状：`begin()` 连续建两个查询 `m_begin = rhi.timestamp_query_create(error); m_end = rhi.timestamp_query_create(error);`（`:30-31`）。若第一个成功、第二个失败，`:32` 的 `m_ready` 为 `false`，但 **`m_begin` 非 0 且没有任何路径回收它** —— 析构只在 `m_begin` 被下一次赋值或对象销毁时处理，而 `m_ready=false` 只挡住 `end_ms()`（`:39`），不触发清理。另外 `m_ready` **不重置为 `false`**：一次失败后重复调 `begin()`，若这次只有一个查询建成，`m_ready` 与两个 id 的组合会错位。
- 证据：`performance.cpp:27-35`（begin 全文）、`:20-25`（析构只删 `m_begin`/`m_end` 非 0 者）、`:37-46`（`end_ms` 首行 `if (!m_ready) return 0.0;`）。`m_ready` 只在 `:32` 被赋值，无置假路径。
- 影响：配合 C1（Vulkan 池 64 槽不回收），「反复 `begin()` 失败重试」会同时泄漏 CPU 侧 map 条目与 GPU 侧查询槽。当前无调用方故未爆。
- 复现/验证：在 `timestamp_query_create` 第二次返回 0 的后端上循环调 `begin()`，观察 `render_backend_active()` 侧的查询数量单调上升。**未复现**（需桩后端）。
- 建议：`begin()` 开头先 `~gpu_timer()` 那份清理逻辑，或失败时立即回收已建的查询。

### B4. Vulkan 时间戳池 64 槽只增不还，第 33 个 `gpu_timer` 起必然 `begin()` 失败
- 位置：`rhi_vk.cpp:232`（`TIMESTAMP_COUNT = 64`）、`:2809-2811`（分配）、`:2815-2818`（删除）
- 现状：`timestamp_query_create` 用 `m_timestamps[id] = m_timestamp_next++;` 顺次占槽，且只有 `if (m_timestamp_next >= TIMESTAMP_COUNT)` 的仓护栏（`:2809`）；`timestamp_query_delete` 只 `m_timestamps.erase(id);`（`:2817`）—— **不回收池索引**。每个 `gpu_timer` 占 2 槽，故第 33 个计时器会拿到 `error = "timestamp pool exhausted"`。
- 证据：`rhi_vk.cpp:232`（`static constexpr std::uint32_t TIMESTAMP_COUNT = 64;`）、`:2811`（`m_timestamps[id] = m_timestamp_next++;`）、`:2817`（`m_timestamps.erase(id);`，无 `m_timestamp_next` 回退）；对照 GL 后端用 `std::unordered_map<uint, uint> m_timestamp_queries` + `glDeleteQueries`（`rhi_gl.cpp:34,778-786`），**能**真正释放。
- 影响：任何「逐 pass 计时」的宿主（README §7 推荐的用法）跑够 33 个计时器就静默降级为 `end_ms()==0.0`，看起来像「GPU 太快」而不是「池满了」。跨平台差异使 GL 走不出这个症状，更难发现。
- 复现/验证：`grep -n "TIMESTAMP_COUNT\|m_timestamp_next" fantasy/backend/rhi_vk.cpp`。最小复现：在 Vulkan 后端上连续 `gpu_timer` + `begin()` 33 次并打印 `error`。**未复现**（本机无 Vulkan 设备验证）。
- 建议：`delete` 时把池索引压回自由表，或改成环形复用。

### B5. `async_loader` 重启后语义不明：`stop()`→`start()` 能跑通，但上一轮已完成任务的 completion 已在 `stop()` 里被丢，且 `pending()` 无法反映差异
- 位置：`async.cpp:34-40`（`start()`）、`:42-54`（`stop()`）、`async.h:24`（`std::thread m_thread;` 单一成员）
- 现状：源码判读如下。`stop()` 在 `:48` 置 `m_running=false`，`:51` `m_thread.join()` 后该 `std::thread` 处于**已 join（non-joinable）**状态；`start()` 的守卫 `:36` `if (m_running) return;` 此时为假，于是 `:37-38` 重置 `m_stop=false; m_running=true`，`:39` 对 `m_thread` **重新赋值**一个 `std::thread` —— 对已 join 的 `std::thread` 赋值是合法的（内部会先析构旧句柄），所以**重启路径本身能走通**。真正的语义空洞在 `:53`：`stop()` 把 `m_completed.clear()` 无条件清空，于是「上一轮刚刚完成、还没 `poll()` 的 completion」与「上一轮从未提交的任务」在重启后**无法区分** —— 两者都表现为 `completed()==0`。
- 证据：`async.cpp:34-40`、`:42-54`（尤其 `:48`、`:51`、`:53`）；`async.h:38` 的注释「停止并 join（等待队列清空）」没有交代完成回调的去向，`:47` `completed()` 也只回计数。
- 影响：宿主按「关闭加载器 → 重新打开」写控制流（切场景/热重载）时，第二次 `start()` 后无法判断上一轮有多少个 CPU 任务跑完、多少个 GPU 上传回调被吞；体现为**「重开后某些资源没有上传」但没有任何错误**。这部分与 B2 同源，B2 记的是「丢弃」本身，本条记的是「重启后不可辨识」。
- 复现/验证：`start(); submit(A,B); /* 等 completed()==1 */ stop(); start();` → 重启后 `pending()==0 && completed()==0`，A/B 的 completion 永不执行、也无法与「从未提交」区分。**未复现**（无调用方，需自建宿主）。
- 建议：给 `async_loader` 加一个「本轮已丢弃的 completion 数」计数，或把「stop 丢弃未完成回调」写成 `async.h` 的显式契约并提供 `stop(bool drain)`。

> 注：本条初稿曾写作「`stop()` 后 `start()` 静默拒绝」，与 `:36-39` 的实际控制流不符，已按源码订正 —— 记录在此以提醒同类误判（本仓有「结论被推翻后不回填」的前例）。

### B6. `gltf::load` 不清理入参 `out`：同一 `load_result` 复用会累积重复项，失败路径会泄漏已建 prefab
- 位置：`gltf_loader.cpp:312`（签名与入口）、`:374`（push 到 `out.prefabs` / `out.transforms`）、`:376`（失败返回）
- 现状：`load(const std::string& path, load_result& out, std::string& error)` 全程**只往 `out` 里 push，从不 `out.prefabs.clear()` / `out.transforms.clear()`**。另有两处失败点发生在已经开始分配之后：`:376` 的 `if (out.prefabs.empty()) { error = "no mesh loaded"; return false; }`，以及 `walk_node` 里 `:244` 的 `continue`（`POSITION` accessor 读不出来就跳过该 primitive，已建的 `prefab*` 留在 `out` 里）。
- 证据：`:312`（无 clear）、`:374`（`walk_node(... out.prefabs, out.transforms)`）、`:376`（失败但 `out.prefabs` 可能非空）；`gltf_loader.h:21-23` 的 `load_result` 无自定义构造/析构，注释只写「调用方负责 delete」。
- 影响：(1) 同一个 `load_result` 加载两个文件 → `prefabs` / `transforms` 混在一起，`test/view/main.cpp:374` 那种 `back.size() == gr.prefabs.size()` 的比对首当其冲；(2) 失败返回 `false` 时调用方通常直接放弃这个 `out`（`test/view/main.cpp:358` 的 `if (load(...))` 结构就不会进 else 分支清理），已建的 `prefab*` 无人 delete。
- 复现/验证：`gltf::load_result r; gltf::load(A, r, e); gltf::load(A, r, e);` → `r.prefabs.size()` 翻倍；`load_result r; gltf::load("不存在", r, e);` 走 `:315` 早退不产生泄漏，而构造一个「有 nodes 但全部 primitive 的 `POSITION` accessor 越界」的 `.gltf` 走 `:376` 可复现泄漏（**未复现**，需构造畸形文件）。
- 建议：入口处 `out.prefabs.clear(); out.transforms.clear();`，失败路径按已建数量回收。

---

## C. 跨平台

### C1. `gltf_loader` 用 `std::ifstream(std::string)` 打开资源路径，非 ASCII 路径在 Windows 上必然打不开
- 位置：`gltf_loader.cpp:18`（主文件）、`:364`（外部 `.bin`）
- 现状：`std::ifstream f(path, std::ios::binary);` 与 `read_file(base + uri, bin)` 都只接受 `std::string`。C++17 的 `basic_ifstream` 没有「按 UTF-8 解释窄字符串」的保证，MSVC 的 `std::ifstream(const std::string&)` 走窄字符 API，按当前 ANSI 代码页解释，中文/日文路径会直接失败。
- 证据：`gltf_loader.cpp:18`、`:364`；`scene_io.cpp:112`（`std::ofstream f(path, ...)`）与 `:121`（读）同理。整个模块 **0 处**用 `std::filesystem::path` 或 `wchar_t`。
- 影响：资产路径含非 ASCII（本仓 `test/view/main.cpp:365` 就在用 `"D:/WorkSpace/xhqm_v2/binary/_scene_test.json"` 这类硬编码路径）时，`gltf::load` 返回 `cannot read file` / `scene_save` 返回 `cannot write file`，而同样的路径用资源管理器能打开 —— 典型的「换了台机器就坏」。
- 复现/验证：把一个 `.glb` 放到含中文的目录，调 `gltf::load` 观察 `cannot read file: ...`。**未在 Windows 上复现**（本机为 Linux 工作副本，此条依 C++17 标准与 MSVC 实现语义判读）。
- 建议：统一改走 `std::filesystem::u8path`。

### C2. `gltf_loader.cpp` 的 JSON 字节流在三条路径上用两种容器，改这里前必须先确认 MSVC 下的容器语义
- 位置：`gltf_loader.cpp:333-334`（GLB 块）、`:340`（纯 `.gltf` 回退）、`:344`（喂给 `json::parse`）
- 现状：`json_bytes` 的类型是 `std::vector<std::uint8_t>`（`:317`），三条赋值路径各不相同 —— `:333` `json_bytes.assign(file.data() + off, file.data() + off + clen);`（范围赋值）、`:334` `buffers.emplace_back(file.data() + off, file.data() + off + clen);`（迭代器对构造，目标是 `std::vector<std::vector<std::uint8_t>>`）、`:340` `json_bytes = file;`（整体拷贝）。最后 `:344` `json::parse(json_bytes.begin(), json_bytes.end(), nullptr, false)` 按字节迭代器解析。
- 证据：`:317`（声明）、`:333`、`:334`、`:340`、`:344`；文件头 `:2-6` 只 include 了 `<fstream> <cstring> <cstdint> <array> <memory>`。
- 影响：**当前无已知现症**（三种写法在 libstdc++/libc++/MSVC 上都是合法且等价的字节序列）。记它的理由是这条路径**同时**承载「GLB 分块解析」与「纯 JSON 直读」两种输入（`:320` 的 `memcmp(file.data(), "glTF", 4)` 判据），而 `.gltf` 文本的 UTF-8 字节要原样进 `json::parse` —— 任何「把 `json_bytes` 改成 `std::string` 或反过来」的清理动作，都必须先确认 `assign` / 迭代器对构造 / `json::parse` 三者在目标编译器上对 `uint8_t` 与 `char` 的处理一致，否则会引入只在某一个编译器上出现的解析失败。
- 复现/验证：`sed -n '317p;333,334p;340p;344p' fantasy/runtime/gltf_loader.cpp`；用一个含 BOM/非 ASCII 材质名的 `.gltf` 与一个 `.glb` 各跑一次 `gltf::load`，两者结果应一致。**未复现差异**（本机 Linux 工作副本，未在 MSVC 上交叉验证）。
- 建议：把 `json_bytes` 统一成 `std::vector<std::uint8_t>` 单一路径并加一行注释说明选它的原因。

### C3. `async_loader` 在 worker 线程里执行用户 task，异常会直接 std::terminate 整个进程
- 位置：`async.cpp:20`（`if (j.task) j.task();`）
- 现状：`worker()` 的循环体**没有 try/catch**。`task` 是调用方传进来的 `std::function<void()>`（`async.h:41`），注释写明「后台：读文件、解码、构建 CPU 数据」—— 文件 IO 与图像解码是最常抛异常的路径。
- 证据：`async.cpp:7-27`（worker 全文，无异常处理）；`:20` 是唯一的用户代码执行点。
- 影响：后台线程抛出异常 → `std::terminate()` → 进程**无栈回溯地死掉**。跨平台表现一致（都是 terminate），但与主线程出错（可被外层 catch）的不对称会让宿主误判为「随机崩溃」。当前无调用方故未爆。
- 复现/验证：`loader.start(); loader.submit([]{ throw std::runtime_error("x"); }, {});` → 进程终止。**未复现**。
- 建议：把 `task()` 包在 try/catch 里，把异常转成「该 job 的 completion 不执行 + 记错误」的显式状态。

### C4. GL 与 Vulkan 的时间戳查询生命周期不对称，同一个 `gpu_timer` 用法在两端行为不同
- 位置：`rhi_gl.cpp:778-786`（GL 删除真释放）vs `rhi_vk.cpp:2815-2818`（VK 删除不回收槽）；消费侧 `performance.cpp:20-25`
- 现状：GL 后端 `timestamp_query_delete` 会 `glDeleteQueries` 并把 `std::unordered_map<uint, uint>` 条目 `erase`（`rhi_gl.cpp:33-35` 声明、`:778-786` 实现），`gpu_timer` 的 RAII 语义成立；VK 后端删条目不还池索引（详见 B4）。
- 证据：`grep -n "timestamp" fantasy/backend/rhi_gl.h` → **0 命中**（GL 的成员在 `.cpp:33-35`，是 `rhi_gl.cpp` 内的局部结构）；`grep -n "TIMESTAMP_COUNT" fantasy/backend/rhi_vk.cpp` → `:232`。
- 影响：把 README §7 的 `gpu_timer` 用法照搬到 Vulkan 后端的宿主上，前 32 次工作、之后全部静默返回 0；GL 后端则永远正常。**双后端行为不一致是这类「只在一边坏」问题的最常见来源**（本仓已有同类前例，见 `fantasy_bugs.md` #12/#40/#50）。
- 复现/验证：`grep -n "timestamp" fantasy/backend/rhi_gl.h fantasy/backend/rhi_gl.cpp`；`grep -n "TIMESTAMP_COUNT\|m_timestamp_next" fantasy/backend/rhi_vk.cpp`。**未在设备上复现**。
- 建议：把「池槽可回收」作为后端契约写进 `rhi_no.h:171-174` 的注释，两端各自满足。

---

## D. 性能

### D1. `frame_graph::execute()` 每个 pass 线性扫全部资源两遍，整体 O(passes × resources)
- 位置：`frame_graph.cpp:119-126`（分配扫描）、`:131-138`（释放扫描）
- 现状：每个 pass 的循环体先 `for (auto& r : m_resources) { if (r.first_use == (int)k) {...} }`，再执行 callback，再 `for (auto& r : m_resources) { if (r.last_use == (int)k) {...} }`。`compile()` 已经把生命周期算成连续区间，但 `execute()` 没有用它做增量索引。
- 证据：`:119`、`:131` 两条 `for (auto& r : m_resources)` 都在 `for (size_t k = 0; k < n; ++k)` 内（`:116`）。对照 `compile()` 的 `:92` 那份扫描是**一次性**的。
- 影响：`theory_render.cpp` 现在挂 12 个 pass、0 个资源，所以 `m_resources` 为空、内层空转无成本；一旦按 README §4 的声明式用法加上每个 pass 若干资源（GBuffer MRT + 后处理链，十几个资源量级），每帧就是 12 × 十几个 × 2 次条件判断 —— 单帧开销不大但**每帧固定发生**，且随资源数线性放大。这是「机制没被用起来」的次因（主因见 D2）。
- 复现/验证：`grep -n "for (auto& r : m_resources)" fantasy/runtime/frame_graph.cpp` → `:92,119,131,153`，只有 `:92` 在 `compile()` 里。**未做计时**（无宿主）。
- 建议：`compile()` 里另存「第 k 个 pass 要分配/释放哪些资源」的索引数组，`execute()` 按索引取。

### D2. `frame_graph::execute()` 每帧重新创建并销毁全部离屏资源，无跨帧复用
- 位置：`frame_graph.cpp:124`（`r.handle = m_rhi->create_offscreen(...)`）、`:135`（`m_rhi->delete_offscreen(r.handle)`）
- 现状：分配/释放都发生在 `execute()` 内，`execute()` 每次调用都会把 `handle` 从 0 建起来再归 0。`compile()` 只算生命周期区间，**没有任何跨帧保留**；`reset()`（`:149-160`）也是销毁语义。
- 证据：`:124` 建、`:135` 删、`:133` 的 `if (r.last_use == (int)k && r.handle)` 保证一帧内配对释放。生产侧 `theory_pass.cpp:83` **每帧**调 `m_graph.execute()`（同一文件 `:73-79` 只在 dirty/unused 时重建声明，`execute` 不在该分支里）。
- 影响：一旦按声明式用法接线，**每帧都要新建再删掉整条后处理链的 render target**（GBuffer MRT + HDR + bloom 各级），分配是 GPU 内存 + 描述符/图像视图的创建，代价远大于绘制本身。这正是当前 12 个 pass 全部传空 reads/writes 的**根本原因** —— 机制开着就每帧重建。
- 复现/验证：`sed -n '73,84p' fantasy/theory_pass.cpp`（每帧 `execute()`）；`sed -n '116,139p' fantasy/runtime/frame_graph.cpp`（建/删都在循环内）。**未做计时**。
- 建议：把 `handle` 的提升到「声明变更时才重建」的时机（`theory_frame_graph` 的 dirty 分支正是现成挂点）。

### D3. `asset_manager` 每次查找都拷一份 `shared_ptr`，且未命中时白分配一个空资源
- 位置：`asset_manager.h:24-28`（`find_material`）、`:32-36`（`find_texture`）
- 现状：`find_material` 按值返回 `material`（即 `std::shared_ptr<material_desc>`），未命中返回 `material()`；`find_texture` 按值返回 `texture`，未命中返回 `texture()` —— 而 `texture` 是 `resource<_texture, id>`，其默认构造是**真的 `std::make_shared<_texture>()` + 一次完整删除**（`metaclass/resource.h:25` 的 `resource() : share_ptr(std::make_shared<_type>())`，`_texture::~_texture` 走 `drawer.cpp:227` 的 `reset()`）。也就是说**一次未命中的查询 = 一次堆分配 + 一次堆释放**，而查询语义上应该是只读的。
- 证据：`asset_manager.h:24-28`、`:32-36`；`metaclass/resource.h:25`；`fantasy/drawer.h:142`（`using texture = fantasy::resource<fantasy::_texture, fantasy::id>;`）、`:106-124`（`_texture` 的默认构造与析构声明）。
- 影响：`has_material()` / `has_texture()` 才是无分配的判读方式，但签名相似的 `find_*` 更容易被顺手用在循环里；按值返回的 `shared_ptr` 拷贝也意味着每次查找都有一次原子引用计数增减。当前 `asset_manager` 在本模块外**零引用**（见 G3），所以是「接进来才生效」的问题。
- 复现/验证：`grep -n "find_material\|find_texture" fantasy/runtime/asset_manager.h`；`sed -n '28p' metaclass/resource.h`。**未做分配计数**。
- 建议：加 `const material* lookup_material(const std::string&) const`（返回指针、未命中 `nullptr`）作为热路径入口。

---

## E. 未实现 / 占位桩

### E1. `gltf_loader` 只回填 `POSITION`/`NORMAL`/`TEXCOORD_0`，`COLOR_0` / `TANGENT` / `TEXCOORD_1` 全部丢弃
- 位置：`gltf_loader.cpp:238-241`（唯一读取的三个属性名）
- 现状：属性读取只有 `attrs.contains("POSITION")`（`:238`）、`"NORMAL"`（`:240`）、`"TEXCOORD_0"`（`:241`）三处。`COLOR_0` / `TANGENT` / `TEXCOORD_1` / `TEXCOORD_2` / `JOINTS_0` / `WEIGHTS_0` 在全文 **0 命中**。
- 证据：`grep -n "POSITION\|NORMAL\|TEXCOORD\|COLOR_0\|TANGENT\|JOINTS\|WEIGHTS" fantasy/runtime/gltf_loader.cpp` → 只命中 `:238,240,241`（对照 `gltf_loader.h:7` 的自陈「v1 范围：静态网格」）。
- 影响：**引擎侧能力是齐的、缺口在加载器**：`prefab` 有 `colour()` / `tangent()` / `skin_index()` / `skin_weight()` / 多套纹理位（`drawer.h:253-460` 区段），`scene_io` 也会序列化 `colours` / `tangents`（`scene_io.cpp:96-107`），`test/view/ModelLoader.h` 走的是 Assimp 那条路。所以带顶点色或切线贴图的 glTF 经原生加载器进来会**静默丢属性** —— 不报错，只在渲染结果上体现。「现在编译器会过、只是没接完」。
- 复现/验证：`test/view/main.cpp:354-385` 的 `[GLTF]` 自检只打印 `prefabs` 数量与三角形总数（`:362`），**不会**暴露属性缺失。最小验证：加载含 `COLOR_0` 的 glTF，检查 `p->colour().empty()`。
- 建议：至少补 `TANGENT`（法线贴图 TBN 直接依赖）与 `COLOR_0`，两者 prefab 侧接口已就绪。

### E2. `async_loader` / `command_recorder` / `asset_manager` / `gpu_timer` / `performance_stats` 在本模块外零引用
- 位置：`async.h:17`、`command_recorder.h:18`、`asset_manager.h:16`、`performance.h:14`、`performance.h:34`
- 现状：五个组件全部编译进库，但全仓范围内**没有任何调用方**。模块被真正使用的只有 `frame_graph`（经 `theory_pass.h:50`）、`gltf_loader` / `scene_io` / `bc1`（`test/view/main.cpp:329-385`）。
- 证据：`grep -rn "async_loader\|command_recorder\|asset_manager\|performance_stats\|gpu_timer" --include=*.h --include=*.cpp --include=*.mm . | grep -v "fantasy/runtime/"` → **0 命中**。
- 影响：这五件是「库能力已就绪、接线未做」。它们的问题（B1/B2/B3/B5/C3/D3）因此全部处于**潜伏态**：没有现症，但也没有任何回归网兜住。接手者容易误判为「已在用、应该可靠」。
- 复现/验证：上面那条 `grep`（去掉 `grep -v` 后可见全部命中都在本模块内）。
- 建议：按 P 优先级逐个接线，接一个补一个最小宿主自检。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. `compile()` 把「读自己写的资源」判为错误，读写混合必须拆 pass —— 这是有意的
- 位置：`frame_graph.cpp:65`（`if (p == (int)i) { error = "pass '" + m_passes[i].name + "' reads its own write '" + rname + "'"; return false; }`）
- 现状：一个 pass 同时声明同一资源的 reads 和 writes 会被直接判为编译失败；`frame_graph.h:54-55` 的注释把 `execute()` 描述为「首次写入分配 / 最后读取释放」，是**单写者**模型。
- **为什么现在这样是对的**：单写者让「生产者在哪」无需歧义（`producer[]` 是 `std::vector<int>`，`compile()` 的 `:47-49` 就靠「已有生产者即报错」保证），拓扑排序的边集定义干净，`execute()` 的分配点唯一。若允许自读自写，就必须引入「先读后写 / 先写后读」的语义区分（真帧图要靠 `read-before-write` 判定传递依赖），复杂度上一个台阶。
- **代价**：`theory_render.cpp` 里 `tonemap` / `bloom` / `ssao` / `ssr` / `taa` 这些「读回写同一张 HDR」的后处理必须拆成两个 pass；这正是它们现在**全部传空 reads/writes、改用 `theory_frame_graph` 的 `set_handle`/`handle` 命名表**（`theory_pass.h:82-87`）的原因。
- 复现/验证：`grep -n "reads its own write" fantasy/runtime/frame_graph.cpp` → `:65`。

### F2. `frame_graph::execute()` 不绑定渲染状态，callback 是 `std::function<void()>` 且资源靠名字查
- 位置：`frame_graph.h:34`（`std::function<void()> callback;`）、`:49`（`add_pass` 签名）、`:58`（`resource_handle`）
- 现状：callback 不收参数、不返回；pass 内要拿资源必须调 `fg.resource_handle(name)`；离屏的 `begin_offscreen` / `end_offscreen` / 视口设置全部由 callback 自己做（`frame_graph.cpp:129` 只是 `if (p.callback) p.callback();`）。
- **为什么现在这样是对的**：帧图因此对「资源是离屏纹理、纹理数组还是别的」完全无感，RHI 差异（GL 的 FBO / Vulkan 的 render pass）全部留在 callback 里，帧图本身保持 header-only 语义且不依赖任何具体后端 —— 对 header-only 为主的跨平台库存活率优先。`resource_handle` 返回 `uint` 也与 `rhi_no.h:103` 的 `create_offscreen` 返回类型一致。
- **代价**：帧图无法验证「pass 真的按声明用了资源」（声明与实际无关，如 G1 所示），也无法自动插入同步/barrier；命名查找是 `unordered_map` 哈希 + 字符串比较，每 pass 一次。
- 复现/验证：`grep -n "resource_handle\|callback" fantasy/runtime/frame_graph.h` → `:34,49,58`。

### F3. 错误处理全部走「`bool` + `std::string& error` 传引用」，且各组件重数不一致
- 位置：`gltf_loader.h:26`、`scene_io.h:18,22`（三处 `bool f(..., std::string& error)`）；`frame_graph.h:52`（`bool compile(std::string& error)`）；`performance.h:43`（`bool begin(std::string& error)`）；对比 `command_recorder.h:39`、`async.h:41`、`asset_manager.h:23`（**无** error 出参，靠静默返回 / 空对象）
- 现状：「传引用分散」这个判断成立且可量化：4 个文件共 6 个带 `std::string& error` 的出参入口，另 3 个文件的入口**完全没有**错误通道。同一模块内三种风格并存（出参 / 静默 / 空值）。
- **为什么现在这样是对的**：这是引擎既有约定（`rhi_no.h:103` 的 `create_offscreen(uint,uint,uint,std::string&,bool)` 同款），改错误模型要动全仓的 `xhqm_io` 边界与 DLL ABI；header-only 为主的项目里引入异常作为主通道会与 `async_loader` 的线程模型（C3）和 RHI 的 C 风格返回混用打架。**不改是对的**。
- **代价**：调用方拿不到「失败原因的分类」（`scene_save` 的「属性被静默丢弃」与「文件写成功」返回同一个 `true`，见 A1/A3）；无 error 出参的三个组件（`asset_manager` / `async_loader` / `command_recorder`）失败时只能靠返回值/空对象语义区分。真正的缺陷是**每个入口的契约不一致**，而不是「用了传引用」。
- 复现/验证：`grep -n "std::string& error" fantasy/runtime/*.h`。

### F4. 帧图没有资源别名复用 —— 且在 F1 的单写者模型下强行加会先撞语义问题
- 位置：`frame_graph.cpp:119-138`（每资源独立 `create_offscreen`），`frame_graph.h:21-28`（`resource` 结构里没有「可共享 / 可复用」标记）
- 现状：两个生命周期不相交的资源（`first_use`/`last_use` 区间不重叠）本可共用一块离屏，当前各建一块。`resource` 结构只有 `name/w/h/color_count/handle/first_use/last_use` 七个字段，**没有** `transient` / `aliased` / `pool` 之类的标记。
- **为什么现在这样是对的**：（1）`execute()` 的释放时机是「最后触碰的 pass 执行**之后**」（`:131-138`），所以一个资源在其最后一个**读**者跑完前仍然活着 —— 别名规划要在这个粒度上做正确性证明，而不是简单的区间不交；（2）因为 F1，生产管线里成对出现的 pass（读回写）已被拆成两个独立 pass，资源生命周期的「间隙」比表面看起来更碎；（3）在当前「每帧全建全删」的代价结构下（D2），别名能省的只是 `create_offscreen` 的次数而**不能省跨帧重建** —— 先把 handle 提到声明变更时才重建（D2 的建议）比加别名收益大得多。**先修 D2 再谈别名**。
- **代价**：多 pass 管线每帧多建若干张同尺寸离屏；显存峰值高于理论必要值。
- 复现/验证：`grep -n "alias\|transient\|pool" fantasy/runtime/frame_graph.h fantasy/runtime/frame_graph.cpp` → **0 命中**。

---

## G. 文档与代码的出入

### G1. 生产管线的 12 个 pass 全部传空 reads / writes，README §4 示例描述的机制在生产中未被触发
- 位置：`fantasy/theory_render.cpp:130,211,264,287,379,414,443,470,503,536,567,783`
- 现状：`theory_render.cpp` 里每一处都是 `graph.add_pass("<name>", {}, {}, [this] { render(); });` —— **reads 和 writes 都是空 initializer_list**。全仓（排除本模块）`add_resource` **0 命中**、`add_pass` **12 处全是空声明**。
- 证据：`grep -rn "add_pass\|add_resource" --include=*.cpp --include=*.h fantasy/ | grep -v "runtime/"` → 12 条，全部形如 `{}, {}`；`grep -c "add_resource" fantasy/theory_render.cpp` → 0。
- 影响：`compile()` 的依赖推导 / 多写者检查 / 自读检查 / 成环检查、`execute()` 的资源生命周期管理**在真实管线里全是空转**（`m_resources` 为空）。由此推出两个结论：(1) README §4 那段「声明式帧图」的描述在**库层面**成立、在**生产层面**是未启用的能力；(2) `frame_graph` 的资源路径没有任何生产使用者的回归保护 —— 一旦有人按 README 示例接线，D1/D2 的成本立刻显现，且没有现成测试能告诉他哪里错了（见 G3）。
- 复现/验证：上面两条 `grep`。`test/ecs_test.cpp:269-296` 是唯一覆盖 `theory_frame_graph` 的 CMake 目标，它注册 `theory_instancing` 并 `fg.run()`，**同样不声明任何资源**。

### G2. `gltf_loader.cpp` 用 `nlohmann::json` 却不 include，依赖靠 `fantasy.h` 间接传递
- 位置：`gltf_loader.cpp:14`（`using json = nlohmann::json;`），对照 `scene_io.cpp:5`（`#include "../network/json.hpp"`）
- 现状：`gltf_loader.cpp` 的文件头只 include 了 5 个标准库头（`:2-6`）和 `"gltf_loader.h"`，全文没有 `#include ".../json.hpp"`。它能编过是因为 `gltf_loader.h:8` → `fantasy.h:4` → `algebra/value.h` → `head.h:43-49` 的条件包含会带进 `nlohmann/json.hpp`（默认分支 `./network/json.hpp`，即仓内自带的 24639 行那份）。
- 证据：`grep -rn "json.hpp" fantasy/` → 只有 `scene_io.cpp:5` 一条；`sed -n '43,49p' head.h` 显示 `#ifdef XHQM_JSON_EXTERNAL` 分支；`grep -rn "XHQM_JSON_EXTERNAL" .` → 只有 `head.h` 自己（全仓无人定义该宏）。
- 影响：同一模块里 `scene_io.cpp` 用仓内 `network/json.hpp`、`gltf_loader.cpp` 用 `head.h` 分发的同一份 —— 当前一致，但**依赖是隐式的**：任何调整 `fantasy.h` / `value.h` / `head.h` 包含链的动作都会同时打断 `gltf_loader.cpp`，而看这个文件的人不会预期它有 json 依赖。另注：`CMakeLists.txt:96` 的 `include_directories("../depend/nlohmann/")` 指向的目录只有 `json.hpp` 单文件（无嵌套 `nlohmann/` 子目录），所以 `head.h:46` 的 `#include <nlohmann/json.hpp>` 只有在 `depend/nlohmann/nlohmann/json.hpp` 存在时才可用 —— 当前无人定义宏所以走不到。
- 复现/验证：`grep -rn "json.hpp" fantasy/runtime/`；`sed -n '43,49p' head.h`；`ls ../depend/nlohmann/`。

### G3. `frame_graph` 的资源声明路径零测试，`compile()` 的四条错误分支没有任何回归覆盖
- 位置：`test/ecs_test.cpp:269-296`（唯一覆盖点）、`test/CMakeLists.txt:229`（`ecs_test` 目标）；被测的 `frame_graph.cpp:47-89` 四条错误分支
- 现状：全仓只有 `test/ecs_test.cpp:258-296` 的 `test_theory_instancing` 碰帧图，它构造 `theory_frame_graph`、注册一个 pass、调 4 次 `fg.run()` 验证实例同步与去重 —— **不声明任何资源、不碰 `compile()` 的失败路径**。`add_resource` / `resource_handle` / 拓扑序 / 多写者 / 未知资源 / 自读 / 成环 **全部无测试**。`test/view/main.cpp`（唯一用了 `gltf::load`/`scene_io`/`bc1` 的地方，`:329-385`）**不是** `test/CMakeLists.txt` 里的目标（该文件 25 个 `add_executable` 无 `test/view/`）。
- 证据：`grep -rn "add_resource" test/` → 0；`grep -n "add_executable" test/CMakeLists.txt | wc -l` → 25，无 `view`；`grep -n "frame_graph\|fg\." test/ecs_test.cpp` → `:269,270,271,272,275,281,286,292`（全是 `register_pass` / `run`，无资源）。
- 影响：「缺自动化单测」这条**部分成立、需要修正**：CLI 断言测试确实存在（`ecs_test` 是真 CMake 目标，覆盖 `theory_frame_graph` 的驱动与去重），真正的缺口是 **`frame_graph` 的资源管理 + 全部错误分支**，以及 `gltf_loader` / `scene_io` / `bc1` 只在**非目标**的手工程序 `test/view/main.cpp` 里被顺带跑（那 3 个组件的属性完整性问题 A1/E1 正是被这个覆盖结构漏掉的）。不做版本历史，仅记当前状态。
- 复现/验证：`grep -rn "add_resource" test/ fantasy/ --include=*.cpp | grep -v runtime/frame_graph`；`grep -n "add_executable" test/CMakeLists.txt`。

### G4. 本模块的源文件在构建清单里不连续，`performance.cpp` 与其余 5 个隔了 16 行
- 位置：`CMakeLists.txt:467`（`fantasy/runtime/performance.cpp`）、`:483-488`（`async.cpp` / `frame_graph.cpp` / `command_recorder.cpp` / `gltf_loader.cpp` / `bc1.cpp` / `scene_io.cpp`）；头文件侧则是连续的 `:436-443`
- 现状：模块的 6 个 `.cpp` 被拆成两段 —— `performance.cpp` 单独排在 `XHQM_BODY_FANTASY` 的前段（`:467`），后面插了 15 个 spatial / lighting / postfx 源文件（`:468-482`），剩下 5 个 runtime 源文件才出现在 `:483-488`。8 个头文件则是连续的 `:436-443`。
- 证据：`sed -n '467p;483,488p' CMakeLists.txt` → 6 条 runtime 源；`sed -n '468,482p' CMakeLists.txt` → 15 条其它模块源，全夹在中间；`sed -n '436,443p' CMakeLists.txt` → 8 条连续 runtime 头。
- 影响：按「模块连续块」的思路在这个文件里做增删（例如把整段 `fantasy/runtime/*.cpp` 一起挪位或删除）会**漏掉 `:467` 那一条**，表现为「runtime 的 `gpu_timer` / `performance_stats` 突然链接不上」而其余 5 个 TU 正常 —— 这类「只有部分 TU 被纳入」的症状很难第一眼归因到构建清单。不修（构建清单的排序是仓库既有状态，改动风险大于收益），仅记录以免下次踩。
- 复现/验证：`grep -n "fantasy/runtime/" CMakeLists.txt` → `:89`（include 目录）、`:436-443`（8 头）、`:467` + `:483-488`（6 源），共 16 条。
- 建议：下次整理构建清单时把这 6 个源文件并到一处，与本文件 G1~G3 一并处理。

---

> 本轮**未核实**、因而未写入以上任何一组的待办：`test/view/main.cpp` 那条 `[GLTF]`/`[SCENE]` 自检在任何平台上实际跑通与否（该文件不在 `test/CMakeLists.txt` 的目标里，未找到可复现的构建入口）；`bc1_encode` 的压缩质量（只有 `test/view/main.cpp:338-347` 打了一个 MSE，无基准阈值可判优劣，故未列为缺陷）；Vulkan 时间戳池实际耗尽行为（B4/C4 依源码判读，本机无 Vulkan 设备）；`async_loader` 各竞态（B1/B2/B5/C3）的运行时复现（无调用方宿主，未构造）；非 ASCII 路径在 Windows 上的实测失败（C1，依标准与 MSVC 语义判读）；`gltf_loader.cpp` 的 JSON 字节流在 MSVC 下与 GCC/Clang 是否行为一致（C2，本机只有 Linux 工具链）。这些都需要各自的复现命令，留给下一轮。
