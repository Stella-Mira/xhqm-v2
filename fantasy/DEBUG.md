# xhqm · fantasy 渲染引擎 · 当前问题清单

fantasy 模块（路径 `fantasy`，module id `fantasy`）的**当前仍存在**的问题台账：只记「现在还是坏的 / 还是没写的 / 还是对不上的」，**不记已修项、不做版本历史**。每条都给出可核对的 `文件:行号`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**：本仓的历史台账**不搬家、不改名**。`fantasy/fantasy_bugs.md`（215 行）保留原位，其编号 1–53 就是本模块的问题编号台账 —— 本文件**不重抄它的编号与已修记录**，只把其中**经本轮复核确认仍未修**的条目按新编号引出并指向原编号（如「见 `fantasy_bugs.md` #21」）。
> `network/nebula/DEFERRED.md`（「决定不做」的事项）与根 `DEBUG.md`（模块路径 `.`，含库层面的可复现占位桩与文档漂移）同样保持原位；本文件与它们**不重复记同一件事** —— `fantasy/backend/rhi_vk.h:10` 的「占位」注释已在根 `DEBUG.md` G1 登记，本文件只在 G1 处指向它，不再另立一条。
> 判据纪律：凡写「编译不过 / 某 API 不存在 / 某文件缺失」，必须附复现命令与验证版本；凡写「运行时可见 / 不可见」，必须注明是否在本机复现。本节点**无图形环境**（`fantasy_bugs.md:3` 自陈同一约束），故凡需 GPU 才能断言的项一律标「未复现」。

---

## 附：编号对照（`fantasy_bugs.md` → 本文件）

`fantasy_bugs.md`（215 行）是本模块的**历史台账**，编号 1–53 是该文件的既有编号，**不搬家、不改名、不重编号**。本轮复核后**仍然存在**的条目按本文件的分组重新编号，映射如下（已修项**不迁入**本文件）：

| 原编号 | 原文件说的是 | 本轮复核结论 | 本文件位置 |
|---|---|---|---|
| #21 | `XHQM_TEX0`/`XHQM_SPRITE_TEX`/`XHQM_COLOR_TEX` 都映射 `binding = 1` | **仍存在**（`backend/shader_preprocess.cpp:273`/`:283`/`:285`；三者分处独立 program，当前无实际缺陷） | **A5** |
| #22 | `ubo_offset("params")` 与 `"tranMatrix"` 共用 `UBO_TRANSFORM(0)` | **仍存在**（`backend/ubo.h:75` + 9 处 postfx `uniform_vec4(..., "params", ...)`，靠偏移 0 巧合工作） | **B7** |
| #43 | 方向光阴影在现代管线内不可见 | **代码层面已修**（#48–#51 的修法都在位）；运行期阴影是否可见**未复现** | 见下「#43 的复核证据」 |
| #52 | UBO 环形槽 slot0 别名 | **修法在位**（`backend/rhi_vk.cpp:2566` 起 `slot = 1 + (p.ubo_slot++ % (UBO_SLOTS - 1))`）；常量已变 | **B4** |
| #53 | 环形槽数不足的潜在跨帧竞态 | 常量已从 `UBO_SLOTS 256`、环长 127 涨到 `UBO_SLOTS = 1024`（`backend/rhi_vk.cpp:67`），`MAX_DESC_SETS` 从 8 涨到 **1024**（`:85`）—— 史册里那句数字**已过时** | **B4** |
| #45 | `vkDestroyDevice` 驱动崩溃 → 默认跳过销毁 | 取舍仍在（`backend/rhi_vk.cpp:720`，开关 `XHQM_FULL_DEVICE_DESTROY=1`） | **F1** |

### #43 的复核证据（结论：代码已修，运行期未复现）

`fantasy_bugs.md` #43 记的是「接线、状态、RHI 采样、阴影图生成均验证正常，但快照无阴影」。同文件其后的 #48–#51 给出了根因与修法，本轮逐条在代码里核对，**修法都在位**：

- **#48（阴影图采样 v 轴镜像）**：`backend/shader_preprocess.cpp:254` 把 `XHQM_SHADOW_UV_FLIP` 展开为 `vulkan ? "shadowUV.y = 1.0 - shadowUV.y;" : ""`，即**仅 Vulkan 翻转** —— 与 `shader_preprocess.cpp:251-253` 的注释（VK 离屏翻转视口、GL 不翻）一致；两个采样点 `:62`（PBR 路径）与 `:168`（toon 路径）都用这个宏。
- **#49（深度专用 render pass 缺 external subpass 依赖）**：`backend/rhi_vk.cpp:3156-3172` 有完整的 `deps[0]`（EXTERNAL→0：`FRAGMENT_SHADER/SHADER_READ` → `EARLY_FRAGMENT_TESTS/DEPTH_WRITE`）与 `deps[1]`（0→EXTERNAL：`LATE_FRAGMENT_TESTS/DEPTH_WRITE` → `FRAGMENT_SHADER/SHADER_READ`），带 `VK_DEPENDENCY_BY_REGION_BIT`，`rpci.dependencyCount = (uint)deps.size()`（`:3181`）。
- **#50（GL 后端深度专用离屏三缺口）**：GL 侧 `begin_offscreen` 按离屏尺寸设视口，深度专用路径补 `glClear` 清深度。注意 `backend/rhi_gl.cpp:180-182` 的三处空实现（`destroy_surface`/`begin_frame`/`end_frame`）**不是**缺口 —— 同处注释写明「GL 无需显式表面与帧边界，即时渲染」，`create_surface` 直接 `return true`（`:179`）。**未在代码里逐个复读 `begin_offscreen` 的 GL 分支细节**（未复现）。
- **#51（fence 未复位 + 信号量跨帧复用）**：`backend/rhi_vk.cpp:1308-1312` 的 `vkWaitForFences` 之后紧跟 `vkResetFences`，注释说明「等待返回后 fence 处于已信号态；vkQueueSubmit 要求 fence 未信号，须先复位」；`begin_frame` 里 `++m_frame_serial`、按 `st->frame_index` 索引 `image_avails`/`render_dones`/`fences`（`:1306`、`:1323-1326`）。
- **#41 的 NDC→[0,1] remap**（深度参考修正）仍在：`shader_preprocess.cpp:66`（`0.5 * proj.z + 0.5 - bias`）与 `:172`；PCF 3×3 与 normal offset 同在（`:64-77`）。

**未复现**：本节点无图形上下文（`fantasy_bugs.md:3` 自陈同一约束；唯一查看器 `test/view` 是独立工程，本轮未构建、未运行），「阴影是否真的可见」需要 GPU 出图与逐像素差分，本轮**没有做**。因此本文件既**不**把 #43 记成「已修」（缺运行期证据），也**不**把它记成「仍存在」（代码层面四条的修法都在）——接手者若要复活这条，第一步是出图，而不是回读代码。

### 与其它文档的关系

- `fantasy/fantasy_bugs.md`（本目录，215 行）：**保留原名原位置**，编号 1–53 仍是本模块的问题编号总账（含已修项与修复过程）；本文件只在上表里指向它，不复制其内容。
- 根 `DEBUG.md`：`fantasy/backend/rhi_vk.h:10` 的「占位」注释（本文件 **G1**）、`rhi_vk.cpp` 行数 3669 → 4078（本文件 **G8** 是模块内那一份）均已在根文件登记，两边不重复记证据。
- `network/nebula/DEFERRED.md`：nebula 的「决定不做」台账，与本模块无内容重叠。
- 各子模块 README（`backend/`、`spatial/`、`postfx/`、`animation/`、`lighting/`、`runtime/`）：讲定位与用法；其中已知的过时数字见本文件 **G8** 与 **D2**。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. 阴影 PCF 的 texel 步长硬编码 `1/2048`，与可配置的阴影图尺寸解耦
- 位置：`backend/shader_preprocess.cpp:64`（PBR 路径）、`:170`（toon 路径）；尺寸来源 `theory_render.h:30`（`uint m_size = 2048`）、`theory_render.cpp:182-188`（`size()` 改动即重建阴影图）
- 现状：`theory_shadow::size(uint)` 是公开 setter，改它会真的重建出一张 `size × size` 的阴影图（`theory_render.cpp:182` 的 `if (m_shadow.ready() && m_applied != m_size) m_shadow.reset();`）；但片元里的 PCF 采样步长是 `const vec2 texel = vec2(1.0 / 2048.0);`，**常量，不随尺寸变化**。
- 证据：`grep -n '1.0 / 2048.0' backend/shader_preprocess.cpp` → `:64`、`:170` 两处；`grep -rn '2048' theory_render.h` → `:30 uint m_size = 2048;`。两处 texel 只在一个尺寸下自洽。
- 影响：任何把 `theory_shadow::enabled/size` 拿去调的人（阴影 pass 目前只能手工 `new` 出来改，见 A2 的注）一旦设成 1024/4096，PCF 的 3×3 偏移就按错误像素步长采样 —— 1024 时偏移只有应有值的 1/2（阴影边缘变脏、漏光），4096 时偏移翻倍（阴影边缘过硬、peter-panning 加重）。默认 2048 下无表现，因此很容易漏测。
- 复现/验证：未复现（本节点无 GPU）。静态可断：`grep -n '1.0 / 2048.0' backend/shader_preprocess.cpp`；对照 `theory_render.h:30`。
- 建议：把 texel 尺寸经 UBO（`ubo.h` 的 `materialToon.w` 之后尚有标量槽位可借）或独立 uniform 传进着色器，别再写常量。

### A2. 点光 cube 阴影的 far 在着色器里硬编码为 `10`
- 位置：`backend/shader_preprocess.cpp:95-98`（4 条 `texture(cubeShadow, ..., dist / 10.0 - bias)`，PCF 的 4 个交叉采样点）、注释 `:91-92`；实现侧 `lighting/point_shadow.cpp:39-70`（`compute(light_pos, near, far)` 是参数化的）
- 现状：C++ 侧 `point_shadow_map::compute` 接受任意 `near/far` 并据此建 6 面投影，但着色器采样处的归一化因子写死 `10.0`，源码注释自陈「far 暂按 10」。声明里没有把 far 传给着色器的通路。
- 证据：`grep -n 'dist / 10.0' backend/shader_preprocess.cpp`；`sed -n '90,100p' backend/shader_preprocess.cpp` 含注释「生产需距离写入深度着色器（gl_FragDepth = dist/far）配合 6 面渲染；far 暂按 10」。
- 影响：`far != 10` 的点光源，立方体阴影深度比较整体偏离 —— far 更小时几乎全部判为「被照亮」（阴影消失），far 更大时出现大面积假自阴影。而且点光阴影**从未接入默认管线**（`theory_render.cpp:800-822` 的 `setup_pipeline_for` 只挂 `theory_shadow`，`point_shadow_map` 无实例化点），所以这条只有手工接线的人会撞到。
- 复现/验证：未复现。静态可断：`grep -rn 'point_shadow_map' fantasy/ test/` → 仅 `lighting/point_shadow.h/.cpp` 自身。
- 建议：far 与强度一起经 UBO 传入，并补一条最小点光阴影自检。

### A3. `theory_ssao` / `theory_ssr` 把两个 effect 的 `setup()` 用 `&&` 短路，失败后每帧重试且句柄可能半初始化
- 位置：`theory_render.cpp:487`（ssao：`m_ready = m_ssao.setup(...) && m_compose.setup(...);`）、`theory_render.cpp:520`（ssr：同款 `m_ssr.setup(...) && m_compose.setup(...)`）
- 现状：`&&` 的短路语义决定了 —— 一旦第一个 `setup` 返回 false，第二个（compose）**根本不会被调用**；`m_ready` 保持 false，下一个渲染帧 `if (!m_ready || w != m_w || h != m_h)` 又整段重进，于是每帧都重建前一者的 GPU 资源（vertex/index/offscreen/shader 全建一遍）。若第一个成功、第二个失败，则 `m_ready == false` 但 `m_ssao`/`m_ssr` 内部已经 `m_ready = true`，下一帧再次 `setup()` 时先进 `reset()` 把上一帧刚建好的资源删掉 —— 反复分配/释放，且**从不报错**（`declare()` 里也没有把失败写进 fg 的 error）。
- 证据：`sed -n '483,490p;516,523p' theory_render.cpp`；两个 effect 的 `setup()` 开头都是 `reset()`（`postfx/ssao.cpp:103`、`postfx/ssr.cpp` 同构），失败路径 `if (!...) { reset(); return false; }`（`postfx/ssao.cpp:120-124`）。
- 影响：显存吃紧或驱动拒绝创建时，SAO/SSR 静默失效（画面只是「没效果」），并且每帧做一次完整的资源创建+销毁 —— 配合 B3（帧图每帧重建离屏）会把这种抖动放大成明显掉帧。排查时没有任何日志可看。
- 复现/验证：未复现。可在 `theory_render.cpp:487` 前后各打一行日志，正常机器上把 `setup` 改成返回 false 即可观察到每帧重进。
- 建议：分别 `bool ok_a = ...; bool ok_b = ...;` 求值（不用 `&&`），失败时把原因写进 `fg()->error()` 或至少 `fprintf(stderr, ...)`，并加「失败后不再无条件重试」的退避。

### A4. glTF 加载器不做 primitive `mode` 判定，非 TRIANGLES 的图元会被按三角形读
- 位置：`runtime/gltf_loader.cpp:248-266`（`indices` / `tri_count = idx.count / 3`），全文件无 `"mode"` 字样
- 现状：`walk_node` 读 `POSITION/NORMAL/TEXCOORD_0` 与 `indices`，然后无条件 `tri_count = idx.count / 3`（`:257`）或 `pos.count / 3`（`:262`）。glTF 的 `mesh.primitives[].mode` 默认是 4（TRIANGLES），但可以是 5（TRIANGLE_STRIP）/6（TRIANGLE_FAN）/0–3（点/线）；文件里引用非三角形模式时，索引被三三分组后当作独立三角形绘制。
- 证据：`grep -c '"mode"' runtime/gltf_loader.cpp` → 0；`sed -n '248,266p' runtime/gltf_loader.cpp`。
- 影响：任何用 strip/fan 导出的 glTF 会渲染出错误的蛛网/碎面几何（不崩、不报错），且蒙皮/法线看起来「有东西」很难第一眼判定是读错了。默认 pipeline 的 demo 用 `.glb` 三角形网格，所以一直没暴露。
- 复现/验证：未复现（无 GPU 渲染入口）。静态可断：`grep -n 'mode' runtime/gltf_loader.cpp` → 0 命中。
- 建议：读 `prim.value("mode", 4)`，非 4 要么做 strip/fan → 三角形的索引转换，要么直接 `continue` 并给一条 `error`。

### A5. 三处采样器在 Vulkan 共享 `binding = 1`（与 `fantasy_bugs.md` #21 同一条）
- 位置：`backend/shader_preprocess.cpp:273`（`XHQM_TEX0` → `layout(set = 0, binding = 1) uniform sampler2D prefabTexture_0;`）、`:283`（`XHQM_SPRITE_TEX` → `binding = 1`，`spriteTexture`）、`:285`（`XHQM_COLOR_TEX` → `binding = 1`，`colorTexture`）
- 现状：三个采样器名在 Vulkan 侧都落在 `set = 0, binding = 1`。`XHQM_TEX0/1/2` 只出现在 mesh 着色器（`prefab_frag`/`toon_frag`，占 binding 1/2/3），`XHQM_SPRITE_TEX` 只在 `sprite_frag`、`XHQM_COLOR_TEX` 只在图元着色器 —— 三者各属**独立 program**，描述符布局互不干扰，故当前无实际缺陷；但「同一 binding 号在不同 shader 里指不同的东西」是描述符布局共享的硬障碍。
- 证据：`grep -n 'binding = 1' backend/shader_preprocess.cpp` → 三条命中（`:273`/`:283`/`:285`）；`backend/rhi_vk.cpp:1418-1430` 的描述符布局按 `bindings[1 + i].binding = 1 + i` 依次分配；`backend/rhi_vk.cpp:2250-2262` 的 `unit → binding` 映射（`unit == 9 → binding 5`、`unit == 10 → 6`、`unit == 11 → 7`、`unit 3..6 → binding 4` 数组）也依赖这套编号。
- 影响：把 sprite/图元 shader 与 mesh shader 合并成一个 program（或引入共享描述符布局）时会双向覆盖 —— 症状是纹理串色（全白/取到别的贴图）。今天不会触发。改需重排描述符布局并同步 `backend/rhi_vk.cpp:2250-2262` 的映射与 `drawer.cpp` 的 `bind_texture` 调用点。
- 复现/验证：`grep -n 'binding = 1' fantasy/backend/shader_preprocess.cpp`；**未做** GPU 侧验证（本节点无 GPU）。
- 建议：留待描述符布局重构时一并处理（`fantasy_bugs.md` #21 的「待定」判断本轮维持）。

---

## B. 资源与并发

### B1. `async_loader::m_running` / `m_stop` 是裸 `bool`，跨线程读写构成数据竞争
- 位置：`runtime/async.h:29-30`（`bool m_running = false; bool m_stop = false;`）、读点 `runtime/async.h:48`（`bool running() const { return m_running; }`）、`runtime/async.cpp:36`（`if (m_running) return;`，未持锁）
- 现状：写点都持 `m_mutex`（`async.cpp:46-49` 的 `stop()`、`async.cpp:36-39` 的 `start()`），但 `running()` 是无锁读、`start()` 的 `if (m_running) return;` 也在**取锁之前**；worker 线程里另有 `if (m_stop && m_queue.empty())` 的读（`async.cpp:15`，持锁）。裸 `bool` 的并发读写在 C++ 内存模型下是未定义行为，不保证可见性。同一仓库的 `command_recorder` 在 `fantasy_bugs.md` #23 里已把同类字段改成 `std::atomic<bool>`（`runtime/command_recorder.h:26-27`），`async_loader` **没有跟上**。
- 证据：`grep -n 'atomic' runtime/async.h` → 0 命中；`sed -n '29,31p;48p' runtime/async.h`；对照 `sed -n '26,27p' runtime/command_recorder.h` → `std::atomic<bool> m_running{false}; std::atomic<bool> m_stop{false};`。
- 影响：多线程宿主（一个线程加载、一个线程提交）里 `running()` 可能返回陈旧值，`start()` 可能重复创建线程（`m_thread` 被覆盖 → 旧线程泄漏且在 `stop()` 里 join 不到）。单线程按 `start → submit → poll → stop` 顺序用的实例不会踩到。
- 复现/验证：未复现（竞态类）。静态可断：`grep -n 'atomic' runtime/async.h runtime/async.cpp` → 0。
- 建议：照 `command_recorder.h:26-27` 改成 `std::atomic<bool>`，或把 `running()` 的读也纳入锁。

### B2. `async_loader::stop()` 会丢弃排队中的任务，且 `m_completed` 只在真停过一次时才清
- 位置：`runtime/async.cpp:42-54`（`stop()`）、`runtime/async.h:38`（注释「停止并 join（等待队列清空）」）
- 现状：`stop()` 持锁置 `m_stop = true; m_running = false;`，worker 的退出判据是 `if (m_stop && m_queue.empty()) return;`（`async.cpp:15`）—— 所以它是**先把队列排空再退出**，`join()` 因此可能长时间阻塞（排队 N 个解码任务就要等 N 个跑完）。更重要的是：`if (!m_running) return;` 的早退（`async.cpp:44`）让重复 `stop()` **跳过** `m_completed.clear()`（`:53`），未 `poll()` 的完成回调会一直留在队列里 —— 而 `completed()`（`:82-86`）会把它们报成「待处理」。头文件注释与实现不符：注释说「等待队列清空」，实现是「清空队列后**不**调用那些任务的 completion」。
- 证据：`sed -n '42,54p' runtime/async.cpp`；`sed -n '14,15p;76,86p' runtime/async.cpp`。
- 影响：① 想「立刻停」的宿主会卡住（长任务 × 队列长度）；② 先 `stop()` 再（对象未析构前）调 `completed()` 会读到上次遗留的回调数，据此判断「还有活没干完」；③ 那些被丢弃的回调对应的 GPU 资源上传从未发生 —— 与「静默丢弃」同类，排查时只看到「资源没上来」。
- 复现/验证：未复现。可写一个 `submit([]{std::this_thread::sleep_for(200ms);}, [&]{++n;})` 十连后调 `stop()`，测墙钟与 `completed()`。
- 建议：`stop()` 里无论走哪条分支都清 `m_completed` 并写清语义（丢弃 or 执行），把耗时预期写进注释。

### B3. `frame_graph::execute()` 每个 pass 触点都重建/销毁离屏资源，且**逐帧**如此
- 位置：`runtime/frame_graph.cpp:112-140`（`execute()` 内 `create_offscreen` 分配点 `:124`、`delete_offscreen` 释放点 `:133-136`）；`runtime/frame_graph.cpp:92-106` 计算 `first_use/last_use`
- 现状：`execute()` 按 `first_use == k` 分配、`last_use == k` 释放，而 `first_use/last_use` 是**编译期**算出来的固定 pass 序号 —— 每帧 `execute()` 都从 k=0 走到最后，于是每帧都完整地「建一张离屏 → 用 → 删掉」。释放后 `r.handle = 0`，下一帧再建。前一周期的资源**不跨帧复用**。
- 证据：`sed -n '116,139p' runtime/frame_graph.cpp`；`reset()`（`:149-160`）是唯一跨帧保活的路径，只在 `theory_frame_graph::life()` 因 dirty/unused 重编译时被调用（`theory_pass.cpp:74`）。
- 影响：每个被声明的资源每帧一次 GPU 分配 + 一次释放（GL 是 FBO + 纹理；Vulkan 是 image + view + memory + render pass + framebuffer，见 `backend/rhi_vk.cpp:3312` 起的 `create_offscreen` 分支）。在帧率敏感的场景里这是可观的 CPU/驱动开销与显存碎片来源。当前默认管线的各 pass 都是自己管离屏、**不**用 `frame_graph::add_resource`，所以这条暂时只有自建帧图的人会撞到。
- 复现/验证：未复现。`grep -n 'add_resource' fantasy/` 目前只在 `runtime/frame_graph.h/.cpp` 自身；接一条带 resource 的 pass 后，在 `:124` 打点即可看到每帧命中。
- 建议：把 `handle` 的分配与释放拆开 —— 首次编译时分配、`reset()` 时释放，`execute()` 只做复用。

### B4. 单帧同 program 绘制数超过描述符集环长时，`vkDeviceWaitIdle` 整机停顿
- 位置：`backend/rhi_vk.cpp:67`（`constexpr uint UBO_SLOTS = 1024;`）、`backend/rhi_vk.cpp:85`（`constexpr uint MAX_DESC_SETS = 1024;`）、`backend/rhi_vk.cpp:556-577`（`advance_desc_set` 的兜底分支 `vkDeviceWaitIdle(m_device); p.desc_rot = p.desc_frame_rot;`）
- 现状：注释自陈这是**保留的老行为兜底**（`:566-572`：「单帧就把整个环吃满：环内任何回卷点都还被本帧已录制的命令引用，不可能安全复用 ⇒ 保持老行为（等设备空闲 + 回卷到本帧起点）」）。也就是说：只要某个 program 在**单帧内**发出 > 1024 次绘制，这一帧就会插入一次 `vkDeviceWaitIdle`。同理 UBO 环形槽同为 1024（`:67`），单帧同 program 绘制超过约 1023 次即开始覆写仍在飞的槽位。
- 证据：`grep -n 'UBO_SLOTS\|MAX_DESC_SETS' backend/rhi_vk.cpp | head`；`sed -n '552,578p' backend/rhi_vk.cpp`。
- 影响：大场景（同材质上千 draw 或实例化退化）会看到周期性掉帧尖刺，且只在 Vulkan 出现 —— GL 后端无此结构。注意：常量数值与 `fantasy_bugs.md` #53 记录的当年值（`UBO_SLOTS 256`、环长 127）**已经不同**，史册那句现在是过时数字，本文件按现值记。
- 复现/验证：未复现（需 Vulkan 设备）。静态可断：`grep -n 'UBO_SLOTS = \|MAX_DESC_SETS = ' backend/rhi_vk.cpp`。
- 建议：要么按「单帧 draw 数上限」把环长提成可配置并在超限时改走多次 submit，要么在 `advance_desc_set` 命中兜底时打一条计数日志（现在完全静默）。

### B5. `command_recorder::stop()` 不清理仍在排队的任务，`m_remaining` 可能永久非零
- 位置：`runtime/command_recorder.cpp:49-59`（`stop()`）、`:24-27`（worker 的 `--m_remaining == 0` 通知）、`:72`（`record()` 的等待谓词 `m_remaining == 0`）
- 现状：`stop()` 置 `m_stop`、`notify_all()`、join，但**不动 `m_tasks`**，也不复位 `m_remaining`。worker 的退出判据是 `if (m_stop && m_tasks.empty()) return;`（`:15`），而 `stop()` 之后没有 worker 在跑，队列里剩下的任务就永远躺着、其计数也永远留在 `m_remaining` 里。此外 `record()` 的等待谓词是**全局** `m_remaining == 0`（`:72`）—— 一个 `record()` 会等到「包括别线程后来追加的全部任务」都做完，若同时有另一个线程 `stop()`，这个等待可能永久挂住（没人再递减）。
- 证据：`sed -n '49,59p;61,73p' runtime/command_recorder.cpp`；`runtime/command_recorder.h:25`（`size_t m_remaining = 0;`，非原子但全在锁内访问）。
- 影响：`stop()` / 析构后队列残留任务被静默丢弃（无日志），`m_remaining` 与 `m_tasks.size()` 可长期不一致；把 `record()` 与 `stop()` 放在不同线程的宿主可能死等。`fantasy_bugs.md` #24 修的是并发 `record()` 互相覆盖计数，**没有覆盖 `stop()` 这条路径**。
- 复现/验证：未复现。`start(2)` 后 `record({...10 个 sleep 任务...})` 与 `stop()` 并发调用即可观察。
- 建议：`stop()` 里清空 `m_tasks` 并把 `m_remaining` 归零后 `notify_all()`，或让 `record()` 只等自己那一批（用批次计数）。

### B6. 任一「读 scene 写回 scene」的 pass 失败时会把 `"scene"` 句柄覆盖成 0，整条后处理链（含 present）静默停摆
- 位置：`theory_render.cpp:430`（`m_bloom.setup(rhi, w, h, err);` —— 返回值被丢弃）、`:435-436`（`uint out = m_bloom.apply(scene); fg()->set_handle("scene", out);` —— `out == 0` 也照写）；同型写法在 `theory_fxaa`（`:462-463`）、`theory_taa`（`:557-560`）、`theory_ssao`（`:492-496`）、`theory_ssr`（`:525-529`）同样存在
- 现状：`bloom_effect::apply()` 在 `!m_ready` 时返回 0（`postfx/bloom.cpp:201`；`m_ready` 由 `setup` 失败路径的 `reset()` 置 false，`postfx/bloom.cpp:173-191` 的 `m_ready = false` 在 `:190`）。于是「Bloom 离屏创建失败」→ `out = 0` → `set_handle("scene", 0)` → 后续 `theory_fxaa`/`theory_taa`/`theory_tonemap` 的 `if (!scene) return;`（`theory_render.cpp:450`、`:543`、`:386`）全部早退 → **永远不 present**（`theory_tonemap::render` 在 `:398-407` 才走到 `m_tonemap.present(ldr)`）。
- 证据：`sed -n '417,437p' theory_render.cpp`（无返回值检查）；`sed -n '199,201p' postfx/bloom.cpp`（`apply` 的 `if (!m_ready) return 0;`）；对照 `theory_render.cpp:233` —— `theory_scene` 对同类失败**有**显式日志（`[scene] HDR offscreen FAIL`），后处理这几个 pass 一条都没有。
- 影响：症状不是「bloom 没效果」而是**整屏冻结在最后一帧**，且无任何日志；排查时最容易往交换链/帧同步方向找错。触发条件＝任一后处理 effect 的 `setup` 失败（显存不足、驱动拒绝创建、或 `w/h` 为 0 —— `view_wh` 用 1 兜底所以后者不至于）。
- 复现/验证：未复现。静态可断：`grep -n 'set_handle("scene"' theory_render.cpp` 逐处看是否判过空（`:436`/`:463`/`:496`/`:528`/`:560` 均未判）。
- 建议：`set_handle("scene", out)` 前判 `out`（为 0 时保留旧句柄并打一条与 `[scene] HDR offscreen FAIL` 同格式的日志），setup 的返回值也一并检查。

### B7. postfx 的 `params` 与 mesh 的 `tranMatrix` 共用 UBO 偏移 0 的别名歧义（与 `fantasy_bugs.md` #22 同一条）
- 位置：`backend/ubo.h:75`（`if (n == "tranMatrix" || n == "params") return (int)UBO_TRANSFORM;`；`UBO_TRANSFORM = 0` 定义在 `backend/ubo.h:23`）；写入点 `postfx/bloom.cpp:208`/`:218`/`:230`、`postfx/compose.cpp:111`、`postfx/fxaa.cpp:160`、`postfx/ssao.cpp:152`、`postfx/ssr.cpp:153`、`postfx/taa.cpp:132`、`postfx/tonemap.cpp:125`
- 现状：每个 postfx shader **自带**一个 16 字节的小 UBO（`postfx/ssao.cpp:74`：`layout(std140, set = 0, binding = 0) uniform UBO { vec4 params; } ubo;`），`params` 落在该块的偏移 0；mesh 路径的 `tranMatrix` 落在**大** UBO（`UBO_SIZE = 38240`，`backend/ubo.h:44`）的偏移 0。两者靠「从不出现在同一个 shader 里」而工作，是**同一个名→偏移映射表**里两个语义无关的名字撞到同一个数字。
- 证据：`sed -n '71,80p' backend/ubo.h`；`sed -n '74,75p' postfx/ssao.cpp`（自持 UBO 声明）与 `sed -n '152p'` 的写入；`grep -rn 'uniform_vec4(.*"params"' fantasy/postfx/` → 9 处命中。
- 影响：任何一次「让 postfx 复用引擎大 UBO」的重构都会让 `params` 静默写到 `tranMatrix` 槽上（或反之），症状是几何整体错位/整屏被错误矩阵覆盖 —— 与 `fantasy_bugs.md` #52 观察到的画面同型。今天不触发。改需在 38240 字节的大 UBO 内另辟 16 字节槽并改 postfx 的绑定路径。
- 复现/验证：`grep -n 'params' fantasy/backend/ubo.h`；`grep -rn 'uniform_vec4(.*"params"' fantasy/postfx/`。
- 建议：留待 postfx 重构时一并处理 —— 最省事的做法是给 postfx 的 16 字节块一个独立名字，不走 `ubo_offset`（`fantasy_bugs.md` #22 的「待定」判断本轮维持）。

---

## C. 跨平台

### C1. Vulkan 后端的 `viewport()` / `winding()` 是空实现，分屏 present 在 Vulkan 上静默失效
- 位置：`backend/rhi_vk.cpp:3845`（`virtual void viewport(sint, sint, sint, sint) override {}`）、`backend/rhi_vk.cpp:3846`（`virtual void winding(bool) override {}`）
- 现状：Vulkan 的 viewport/scissor 走动态状态（`create_pipeline` 里 `VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, ... }`，`backend/rhi_vk.cpp:4044`），后端在 `begin_frame`（`:1356`）与各 `begin_offscreen*`（`:3339`、`:3378`、`:3756`）里自己 `vkCmdSetViewport`，所以接口层的 `viewport()` 被留空。而 `theory_tonemap::render` 正是用 `rhi.viewport(m_vp_x, m_vp_y, ...)` 实现**分屏 present 子区域**（`theory_render.cpp:403-406`）—— 这条路径在 GL 上生效（`backend/rhi_gl.cpp:171` 是真 `glViewport`），在 Vulkan 上被吃掉。
- 证据：`grep -n 'viewport' backend/rhi_vk.cpp backend/rhi_gl.cpp` → VK `:3845` 空实现，GL `:171` 真实现；`sed -n '402,407p' theory_render.cpp`（`m_vp_x >= 0` 时调 `rhi.viewport`）。
- 影响：用 `theory_tonemap::viewport(x,y,w,h)` 做多相机分屏/画中画的宿主，在 Vulkan 上每个相机都铺满整窗（互相覆盖），只有最后一个可见；GL 上正常。`test/view` 的多窗口走的是「每窗口一个 fg + 整屏 present」，绕开了这条，所以没暴露。
- 复现/验证：未复现（需 Vulkan 设备）。静态可断：`sed -n '3845,3846p' backend/rhi_vk.cpp`。
- 建议：Vulkan 侧实现 `viewport()`（记下矩形，在下一个 `vkCmdSetViewport` 处生效），或把分屏改为在 `present` 前经动态状态设置。

### C2. `rhi_vk_macos_metal_layer` 只有 .mm 提供定义，非 Apple 平台是「有声明无定义」
- 位置：`backend/rhi_vk.cpp:19`（无条件 `void* rhi_vk_macos_metal_layer(void* nsview_ptr);` 前置声明）、`:965-967`（调用点在 `#elif defined(__APPLE__)` 分支内）；定义只存在于 `backend/rhi_vk_macos.mm:5`，而该文件仅在 Apple 平台被 CMake 收录（`CMakeLists.txt:709-711`，含 `enable_language(OBJCXX)` 与 `-fobjc-arc`）
- 现状：非 Apple 平台编译 `rhi_vk.cpp` 时，`:19` 的声明**可见但无定义**，调用点被 `#elif defined(__APPLE__)` 排除，所以不产生未解析符号 —— 当前**不会**链接失败。这是一条脆弱的隐式耦合：若有人把该调用移出条件块，非 Apple 构建会立刻在链接期炸。
- 证据：`grep -rn 'rhi_vk_macos_metal_layer' fantasy/` → `rhi_vk.cpp:19`（声明）+ `:967`（调用）+ `rhi_vk_macos.mm:5`（定义）；`sed -n '705,712p' CMakeLists.txt`。
- 影响：接手者在 Linux/Windows 上找不到该符号的定义去读，容易误判为「缺文件」；改动 surface 创建路径时容易踩到链接失败。
- 复现/验证：`grep -rn 'rhi_vk_macos_metal_layer' fantasy/`（应只有三处）；非 Apple 下不链接失败 —— **本机未跑构建**。
- 建议：把 `:19` 的声明也放进 `#if defined(__APPLE__)`，让「谁提供」在代码里显式。

---

## D. 性能

### D1. Bloom 只有单级全分辨率 3×3 模糊，没有下采样 mip 链
- 位置：`postfx/bloom.cpp:50-87`（`blur_gl` / `blur_vk`，9 tap 3×3 高斯，`sum/16.0`）、`postfx/bloom.cpp:203-235`（`apply()`：亮度提取 → **一次**模糊 → 合成，三个离屏**同为全分辨率**）
- 现状：`apply()` 的三步都是 `m_target_bright = m_target_blur = m_target_result`（尺寸来自 `setup(rhi, w, h)`，与窗口同尺寸，见 `postfx/bloom.h`），模糊只有一次 9 tap 卷积。物理上「大范围柔光」需要多级下采样 + 逐级模糊，这里用单级近似。
- 证据：`sed -n '203,235p' postfx/bloom.cpp`；`grep -n 'KERNEL_N\|blur' postfx/bloom.cpp | head`。
- 影响：泛光的**半径**被限制在几个像素内（看起来像「亮边发白」而不是辉光）；同时三次全分辨率 pass 在 4K 下是三个 4K 纹理读写，成本明显高于「半/四分之一分辨率 + 上采样合成」的常规做法。属算法简化，不是算错。
- 复现/验证：未复现（需 GPU 出图比对）。静态可断：`sed -n '203,235p' postfx/bloom.cpp` 只有一处 `apply(m_shader_blur, ...)`。
- 建议：按 1/2、1/4 逐级下采样模糊再上采样合成；或至少把三张离屏改成 1/2 分辨率。

### D2. CPU 剔除与排序模块在默认管线上完全没接线（「能力存在但生产路径不用」）
- 位置：`spatial/culler.h:13-33`（`scene_culler`）、`spatial/render_queue.h:10-31`（`render_queue`）、`spatial/occlusion.h`（`occlusion_culler`）、`spatial/lod.h:21-47`（`lod_group`）；默认主渲染 `theory_render.cpp:253-254` 只做 `system::traverse` + `models::drawer(root)`
- 现状：`theory_scene::render` 逐帧对整棵子树 `traverse` 后无条件 `models::drawer(root)` —— 没有视锥剔除、没有排序、没有 LOD 选择、没有遮挡查询。上述四个类都编译在 `xhqm` 库里（`CMakeLists.txt:460-472`），但**在引擎与测试里都没有实例化点**（`gpu_culler`/`gpu_hiz_culler`/`gpu_skinner` 仅由 `test/view/main.cpp:533/585/492` 的自检块使用）。
- 证据：`grep -rn 'scene_culler\|render_queue\|lod_group\|occlusion_culler' fantasy/ test/view/` 除自身头/实现外**无实例化**；`grep -rn 'gpu_culler\b' fantasy/*.cpp fantasy/*/*.cpp` 命中为 0。
- 影响：大场景下每帧提交全部对象（draw call 与顶点量随场景线性增长），LOD/排序能力在库里闲置。对上层的实际含义是：**用这些类需要自己接**（`scene_culler::build` + `cull_frustum` → 自行按结果过滤），文档若说「管线已做剔除」会误导。
- 复现/验证：`grep -rn 'scene_culler' fantasy/ | grep -v 'spatial/'; grep -rn 'lod_group' fantasy/ | grep -v 'spatial/'` → 0。
- 建议：要么把 `scene_culler` 接进 `theory_scene`（一次 `build` + 每帧 `cull_frustum`），要么在 `spatial/README.md` 里明确「这些是可选组件、默认管线未接」。

---

## E. 未实现 / 占位桩

> 本组全是「现在编译会过、只是没写完或没接上」的东西。

### E1. `theory_ibl` 的外部环境贴图路径是空壳：设了 `environment_map` 就整段跳过
- 位置：`theory_render.cpp:576-577`（`// 外部环境贴图（GPU 纹理）：CPU 卷积无法采样，需 GPU 卷积（后续）；本次仅程序化生成。` + `if (m_environment_map != 0) return;`）
- 现状：`theory_ibl` 只实现「程序化渐变天空（`:616-632`）→ CPU 半球余弦卷积（`:636-698`）→ CPU GGX 预过滤（`:701-772`）」这一条。若调用方用 `set_equirect(...)` 提供了全景图，能被 CPU 采样并转成 cubemap（`:583-613`）；但只要调用方**显式传入**一张已有 cubemap（`environment_map(uint)`，`theory_render.h:222`），`render()` 就在 `:577` 直接 return —— 不设任何 fg 句柄、不写 `light_env`，辐射度/预过滤/BRDF LUT 三件套全都不产出。
- 证据：`sed -n '570,578p' theory_render.cpp`；`grep -n 'environment_map' theory_render.h theory_render.cpp` → 仅 setter 与这一处早退。
- 影响：按头文件注释「非 0=外部提供」使用的人会得到**完全没有 IBL** 的场景（`light_env->irradiance()` 保持 0 → `drawer.cpp:795` 的 `mat_flags[3]` 为 0 → 着色器不采样 irradiance），且无任何日志。同时 `theory_ibl` **不在** `setup_pipeline_for` 的组装序里（`theory_render.cpp:811-820`），要手工挂。
- 复现/验证：未复现。静态可断：`sed -n '576,577p' theory_render.cpp`。
- 建议：至少在 `:577` 打一条 `[ibl] external environment_map unsupported`，或落地 GPU 卷积路径。

### E2. BRDF LUT 声明了、句柄也留着，但从不创建、从不绑定
- 位置：`theory_render.h:209`（`uint m_brdf_lut = 0;  // BRDF 积分 LUT（2D，NdotV × roughness）`）、`theory_render.h:211`（`uint m_brdf_shader = 0;  // BRDF LUT shader`）、`theory_render.h:227`（`uint brdf_lut(void) const`）
- 现状：`theory_ibl::render()` 从头到尾没有给 `m_brdf_lut` / `m_brdf_shader` 赋值，也没 `set_handle("brdf_lut", ...)`；`drawer.cpp` 也没有任何 `bind_texture` 到 BRDF LUT 的调用（`:846-853` 只绑了 0/1/2、irradiance(10)、prefiltered(11)、normalMap(7)、aoMap(8)）。
- 证据：`grep -n 'brdf' theory_render.cpp drawer.cpp backend/shader_preprocess.cpp` → 只在 `theory_render.h` 的成员声明处；`sed -n '846,853p' drawer.cpp`。
- 影响：PBR 的 split-sum 镜面反射少了 BRDF 的第二项 —— 镜面会偏亮/偏白（能量不守恒），且镜面强度不随粗糙度按预期衰减。属「算法没写完」，不崩、不报错。注意 `theory_render.h:203-204` 的类注释**声称**已产出 BRDF LUT，与实现不符（见 G3）。
- 复现/验证：未复现。静态可断：`grep -c 'brdf' theory_render.cpp` → 0。
- 建议：要么落地 LUT 生成 + 绑定，要么把 `theory_render.h:203-204` 的注释与 `brdf_lut()` 一起标注「未实现」。

### E3. GPU 驱动的 indirect 消费路径只有自检在用，生产管线一律走 CPU 侧 `draw_indexed`
- 位置：`backend/rhi_no.h:147`（`draw_indexed_indirect_storage` 接口）、两个后端实现 `backend/rhi_gl.cpp:388`、`backend/rhi_vk.cpp:2696`；调用者只有 `spatial/gpu_culler.cpp:154`、`spatial/gpu_hiz_culler.cpp:219`、`animation/gpu_skinning.*`，而这些类只被 `test/view/main.cpp:492/533/585/603` 的自检块实例化
- 现状：接口与 compute 侧（写 `DrawIndexedIndirectCommand`/SSBO）都实现了，GL 甚至算了 `GL_DRAW_INDIRECT_BUFFER` 的完整路径（`backend/rhi_gl.cpp:352-362`、`:388`）；但主渲染 `theory_scene`/`theory_depth`/`theory_shadow` 都是逐 drawer `rhi.draw_indexed(...)`（`theory_render.cpp:366`）。
- 证据：`grep -rn 'draw_indexed_indirect_storage' --include='*.cpp' fantasy/` → 仅后端实现与上述三个剔除/蒙皮类；`grep -rn 'gpu_skinner' fantasy/drawer.cpp` → 0。
- 影响：能力齐备但**未接**。按 README 的「GPU 驱动路径」理解为「管线已零 CPU 回读」会高估现状：真正的零读回只在 `test/view` 的自检里跑过。
- 复现/验证：`grep -rn 'draw_indexed_indirect_storage' test/ fantasy/ | grep -v backend`。
- 建议：在文档里把这条从「管线做法」降级为「可选加速组件（未接线）」。

### E4. `drawer::tonemap` 是可写开关，但没有任何着色器读它（ACES 分支不存在）
- 位置：`drawer.h:191`（`int m_tonemap = 0;  // 色调映射：0=Reinhard 1=ACES filmic`）、写入 `drawer.cpp:774`（`rhi.uniform_int(program, "tonemapMode", m_tonemap);`）、映射 `backend/ubo.h:66` 与 `:92`；全仓无 `ACES` 实现
- 现状：`tonemapMode` 被写进 UBO 的 `FLAG_TONEMAP` 槽位，但 `shader_preprocess.cpp` 与 `shader_sources.h` 里**没有任何** `XHQM_FLAG_TONEMAP` / `tonemapMode` 的读取点；色调映射实际由独立的 `theory_tonemap` pass 做 Reinhard + gamma（`postfx/tonemap.cpp:22-32`）。
- 证据：`grep -rn 'tonemapMode\|FLAG_TONEMAP' backend/ drawer.cpp drawer.h` → 只有 `ubo.h:66/92` 与 `drawer.cpp:774`；`grep -rn 'ACES' fantasy/` → 只有 `drawer.h:191` 的注释。
- 影响：设 `drawer->tonemap(1)` 后画面毫无变化（既不崩也不报错），容易被当成「ACES 没调好」而不是「没实现」。
- 复现/验证：`grep -rn 'XHQM_FLAG_TONEMAP' fantasy/` → 0 命中。
- 建议：删掉注释里的「1=ACES filmic」或在 `theory_tonemap` 里真正实现模式选择。

### E5. TAA 的 Halton jitter 有实现但引擎侧无人调用（`theory_taa` 不做亚像素偏移）
- 位置：`postfx/taa.cpp:7-25`（`taa_jitter(frame_index)`，Halton 2/3，返回 [-0.5,0.5]）、`postfx/taa.h:6`（注释「jitter 由 taa_jitter 提供亚像素偏移」）；消费侧 `theory_render.cpp:556-559`（`theory_taa::render` 只用 `m_history` 做混合并 `++m_frame`，**不用 `m_frame` 做 jitter**）
- 现状：`taa_effect::apply(current, history, alpha)`（`postfx/taa.cpp:120-137`）只做当前帧与历史帧的邻域钳制混合；采样位置在整个 `theory_scene` 里都没有被偏移（`camera::window()` 与 `system::traverse` 都不接收 jitter）。
- 证据：`grep -rn 'taa_jitter' fantasy/ test/` → 定义在 `postfx/taa.cpp:7`，唯一调用点是 `test/view/main.cpp:652` 的自检；`sed -n '546,561p' theory_render.cpp` 中 `m_frame` 仅自增。
- 影响：TAA 退化为「历史帧指数混合 + 钳制」，**没有**时间超采样，因此静止画面里锯齿不会被抹平、移动物体的鬼影抑制也缺少应有的多样性；观感上像「模糊的抗锯齿」而不是 TAA。属没接上，不崩。
- 复现/验证：未复现。静态可断：`grep -rn 'taa_jitter' fantasy/` → 仅 `postfx/taa.h` 声明 + `postfx/taa.cpp:7` 定义。
- 建议：在 `theory_scene` 里用 `taa_jitter(frame)` 偏一个亚像素量（写进 `camera::window()` 的前乘矩阵即可），或在文档里明确 TAA 为「历史混合（无 jitter）」。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 进程退出默认跳过 `vkDestroyDevice` / `vkDestroyInstance`，GPU 资源交给 OS 回收
- 位置：`backend/rhi_vk.cpp:720-731`（`if (!getenv("XHQM_FULL_DEVICE_DESTROY")) { m_device = VK_NULL_HANDLE; m_instance = VK_NULL_HANDLE; } else { vkDestroyDevice / vkDestroyInstance }`）
- 现状：默认路径下应用侧自己的资源（离屏/cubemap/管线/描述符池/内存池）**都**被显式销毁（`:700-719`），只有设备与实例这两个「容器」对象不销毁；`XHQM_FULL_DEVICE_DESTROY=1` 才走完整销毁。末了调 `render_backend_retire(this)`（`:732-736`）把 active 指针退到 none 桩。
- 理由（为什么现在这样是对的）：这条不是懒 —— 是**驱动缺陷的实际规避**。注释与 `fantasy_bugs.md` #45 记录了同一结论：NVIDIA 32.0.15.9186 在完整销毁路径上于 `vkDestroyDevice` 内部崩溃（EIP=0），而应用侧销毁顺序已逐项核对（离屏/立方体目标先于内存池、按 `owns_image` 保护、销毁前 `vkDeviceWaitIdle`）。进程退出时 OS 一定会回收 GPU 资源，跳过这两个调用是引擎界的标准退出策略，代价为零（除进程级别）。
- 代价：① 内存检查工具（Valgrind/ASAN 的 Vulkan 层、`VK_LAYER_KHRONOS_validation` 的泄漏检查）会把「设备未销毁」报成泄漏，噪音需要自己滤；② 多实例/热重载场景（同一进程反复 `init`/析构后端）走的是另一条路径，本取舍只覆盖进程退出。
- 验证：`sed -n '717,737p' backend/rhi_vk.cpp`；开关名 `grep -rn 'XHQM_FULL_DEVICE_DESTROY' fantasy/` → 1 处。
- 建议：保持现状；把这条取舍写进 `backend/README.md`，免得每次做内存审计都重新排查一遍。

### F2. `frame_graph` 的 `m_graph` 在管线里从不声明资源，pass 一律自管离屏（`frame_graph::add_resource` 无调用者）
- 位置：`theory_render.cpp:130`（`graph.add_pass("shadow", {}, {}, ...)`，reads/writes 全空）、`:211`、`:264`、`:287`、`:379`、`:414`、`:443`、`:470`、`:503`、`:536`、`:567`、`:783`（全部 pass 同款）；`runtime/frame_graph.cpp:13-21`（`add_resource`）在 `fantasy/` 内无调用者
- 现状：`theory_frame_graph` 的编排职责实际只有「注册表 diff + 拓扑序 + 顺序执行 callback」—— 因为所有 pass 的 reads/writes 都是空列表，`frame_graph::compile` 里的依赖图恒为无边（`runtime/frame_graph.cpp:56-89`），顺序＝挂载序；纹理传递走的是 `theory_frame_graph::m_handles` 命名句柄表（`theory_pass.h:82-87`），与 `frame_graph` 无关。
- 理由（为什么现在这样是对的）：pass 的资源尺寸依赖运行时视口（`view_wh(camera)`，`theory_render.cpp:91-97`），而 `add_resource(w, h, color_count)` 是**编译期**固定尺寸（`runtime/frame_graph.h:48`）—— 窗口一 resize 就得整图重编译。各 pass 自管离屏换来的是「resize 只重建自己那一张」（如 `theory_scene` 的 `if (!m_hdr || w != m_w || h != m_h)`，`theory_render.cpp:222`），且能对 HDR/深度专用/cubemap 这些形态各异的附件各写各的创建参数，不用把 `frame_graph` 的接口撑大。命名句柄表同时让「读 scene 写回 scene」的链式表达比显式资源依赖更短。
- 代价：`frame_graph` 的 `add_resource` / `resource_handle` / `first_use`/`last_use` 生命周期这一整套（也是本模块对外宣传的「声明式 pass 编排 + 资源生命周期管理」）在生产路径上**完全没被使用**，属于双份记账；自管离屏意味着没有统一的资源复用与别名（本来 `frame_graph` 能在 pass 之间复用同一张离屏）。另见 B3：一旦真的用 `add_resource`，每帧都会重建资源。
- 验证：`grep -rn 'add_resource' fantasy/ test/` → 仅 `runtime/frame_graph.h/.cpp`；`grep -n 'add_pass(' theory_render.cpp` → 12 处，reads/writes 均为 `{}`。
- 建议：保持现状，但在 `runtime/README.md` / 主 README 里把 `frame_graph` 描述为「pass 依赖排序 + 命名句柄传递」；`add_resource` 的资源生命周期路径标注为「未接线，另见 DEBUG.md B3」。

### F3. 光照阴影收敛到单一「方向光 shadow_map」，CSM 与点光 cube 阴影保留为可手工接线的组件
- 位置：`theory_render.cpp:811`（组装序只 `add(new theory_shadow(cam))`）、`lighting/csm.*`（`cascaded_shadow_map`，最多 4 级，`csm.cpp:15`）、`lighting/point_shadow.*`；drawer 侧的阴影态是**单级**（`theory_render.cpp:79-85`：`d->cascade_count(1)`、`shadow_texs()[0]`、`light_vps` resize 到 1、`cascade_splits()[0] = 0`）
- 现状：`apply_shadow_state` 把级联数**写死为 1**，尽管着色器（`backend/shader_preprocess.cpp:47-70`）与 drawer 的 UBO 槽位（`drawer.h:183-187` 的 `m_shadow_texs[4]` / `m_cascade_splits[4]` / `m_light_vps`）都支持到 4 级。`cascaded_shadow_map::setup` 的 `count>4` 会被拒（`csm.cpp:15`），说明 4 级是设计上限。
- 理由（为什么现在这样是对的）：单级阴影在「一个方向光 + 紧贴场景的正交投影」下已经够用（`shadow_map::compute_light_view_proj` 用场景世界 AABB 的包围球定 `near=0/far=2r`，`lighting/shadow.cpp:35-66`，紧跟场景因此深度精度好），而 CSM 要处理级联分片、逐级 VP、跨 pass 的 split 表与单帧内的多次深度 pass —— 复杂度换来的收益只有在「大场景 + 远距离阴影」才体现。先跑通单级再上 CSM 是把不可验证面收窄的合理顺序（`fantasy_bugs.md` #43/#48–#51 那一串阴影问题正是这种复杂度的成本）。
- 代价：远处物体的阴影分辨率随场景 AABB 增大而快速下降；`cascade_count` 恒 1 意味着着色器里那段 4 级选级逻辑（`shader_preprocess.cpp:49-59`）永远走 `cascade = 0`，是死分支；点光 cube 阴影（`point_shadow_map`）完全没有接线，其着色器路径还带着 A2 的硬编码 far。
- 验证：`grep -n 'cascade_count(' theory_render.cpp` → `:46`（批量置 0）、`:65`、`:78`、`:79`（置 1）；`grep -rn 'cascaded_shadow_map\|point_shadow_map' fantasy/ test/` → 仅各自 `lighting/` 头/实现。
- 建议：保持单级默认；在 README 里把 CSM/点光阴影明确成「已实现、未接入默认管线」的可选组件。

### F4. 光源数组上限 8，`UBO_MAX_*` 全部收敛到 `ubo.h` 单一事实源
- 位置：`backend/ubo.h:17-21`（`UBO_MAX_LIGHTS=8`、`UBO_MAX_CASCADES=4`、`UBO_MAX_FLAGS=16`、`UBO_MAX_BONES=512`、`UBO_MAX_INSTANCES=64`）、`backend/ubo.h:71-113`（`ubo_offset` 名→偏移映射）、`:117-147`（`ubo_glsl` 生成同布局的 GLSL 块）
- 现状：着色器源码不写数字 —— `shader_preprocess.cpp:246-252` 把 `XHQM_MAX_LIGHTS/XHQM_MAX_CASCADES/XHQM_FLAG_*` 替换成 `ubo.h` 常量；上传侧也复用同一常量（`theory_render.cpp:344` 的 `const count max_bones = UBO_MAX_BONES;`、`:356` 的 `UBO_MAX_INSTANCES`）。全部按 std140 步长手算偏移（如 `UBO_FLAGS=128`、`UBO_BONES=384`、`UBO_SIZE=38240`）。
- 理由（为什么现在这样是对的）：std140 的数组步长（`int[]` 与 `mat4[]` 都是 16 字节步长）与结构体对齐（`vec3 cameraPos; float pointSize;` 共享一个 16 字节槽，`ubo.h:135`）很容易在 GL/VK 两个后端各手写一遍且**悄悄分叉** —— 单一事实源把「改上限」变成只动头的常量，并且 `ubo_offset("params")` 那种名→偏移的集中映射让 `uniform_*` 的字符串调用不至于散落各处。
- 代价：① 偏移是手算的数字，`ubo_glsl()` 的声明顺序必须与常量顺序严格对齐，改一个字段就可能要连带改后面所有常量（整块是 38240 字节的**单** UBO，见 `theory_render.cpp` 里一 draw 一次的 memcpy 路径）；② 8 盏光源对某些场景偏少，超出的光源被 `i < XHQM_MAX_LIGHTS` 静默截断（`shader_preprocess.cpp:45` 的循环上界）；③ 上限不是运行时检测的 —— 超了只会少画，不报错。
- 验证：`grep -rn 'UBO_MAX_' fantasy/backend/ubo.h | head`；`grep -rn 'max_bones\|UBO_MAX_INSTANCES' fantasy/theory_render.cpp`。
- 建议：保持单一事实源；给「光源数 > 8 被截断」加一条一次性日志。

---

## G. 文档与代码的出入

### G1. `backend/rhi_vk.h:10` 的注释仍自称「占位实现」（本文件不另立条目，指向根 DEBUG.md）
- 位置：`backend/rhi_vk.h:10`（`// 工厂：创建 Vulkan 后端（当前为占位实现）`）；事实侧 `backend/rhi_vk.cpp`（4078 行，`create_vk_backend()` 在 `:4072-4076` 返回真实静态单例）
- 现状：注释与实现完全不符。本条**已由根 `DEBUG.md` G1 登记**（含 264 处 `vk*` 调用的统计与复现命令），本文件按「不重复记同一件事」的约定只指过去，不再复制证据。
- 影响与建议：见根 `DEBUG.md` G1（删掉那半句注释）。
- 交叉引用：根 `DEBUG.md` G3 第 2 条也记录了这一条曾导致顶层 `README.md` 把整个 Vulkan 后端记为「占位」，与 `rhi_vk.cpp` 行数（3669 → 4078）一起在同轮订正。

### G2. `backend/shader_sources.h:6` 写「展开为 GLSL 410」，实际是 330 core / 450
- 位置：`backend/shader_sources.h:5-6`（`// 源码含 XHQM_* 宏，经 shader_preprocess(src, vulkan) 展开为 GLSL 410 / 450。`）；实现 `backend/shader_preprocess.cpp:228`（`vulkan ? "#version 450" : "#version 330 core"`）
- 现状：GL 侧一直是 `#version 330 core`，从未生成 410。`backend/README.md` 与主 README 采用的「330 / 450」说法才是对的，只有这行头注释是 410。
- 证据：`grep -n 'XHQM_VERSION' backend/shader_preprocess.cpp` → `:228` 一处；`sed -n '5,6p' backend/shader_sources.h`。
- 影响：按注释去写 GLSL 410 特性的人会撞编译错误；也可能让人以为引擎要求 GL 4.1 上下文（实际 3.3 即可）。
- 复现/验证：`sed -n '228p' backend/shader_preprocess.cpp`。
- 建议：把这行改成「330 core / 450」。

### G3. `theory_render.h` 的 `theory_ibl` 类注释声称产出 prefiltered mip 链与 BRDF LUT，实现是单粗糙度 + 无 LUT
- 位置：`theory_render.h:200-204`（「再经 GPU 重要性采样生成 prefiltered cubemap（mip 链，specular IBL）与 BRDF 积分 LUT，供 prefab_frag 做 split-sum 镜面反射」）、成员注释 `:214`（`uint m_mip_levels = 5;  // prefilter mip 级数（每级一个粗糙度）`）；实现 `theory_render.cpp:701-772`
- 现状：实现里 `const value roughness = 0.5f;`（`theory_render.cpp:705`）是唯一的粗糙度，`pf_size = 64`（`:703`）单级，**没有 mip 链**；`m_mip_levels` 这个成员从头到尾没被读过（`grep -n 'm_mip_levels' theory_render.*` → 仅声明）。BRDF LUT 见 E2（从不创建）。另外 `:200-201` 还写着「经 GPU 重要性采样」，实际整段是 **CPU** 侧双循环（`:735-762`）。
- 证据：`grep -n 'm_mip_levels' theory_render.h theory_render.cpp`；`sed -n '701,705p;770,772p' theory_render.cpp`；`grep -c 'brdf' theory_render.cpp` → 0。
- 影响：接手者按注释以为有 mip 链和 LUT，去 `light_env->prefiltered()` 取任意 mip 会拿到同一张图；也会以为 IBL 走 GPU（实际每次 `theory_ibl` 首帧在 CPU 上做 6 面 × 32² × 32 采样 + 6 面 × 64² × 32 采样的循环，可感知的卡顿）。
- 复现/验证：静态可断（上述 grep）；**未跑**运行时耗时。
- 建议：注释改成实情（「单粗糙度 0.5 的 CPU 预过滤；mip 链与 BRDF LUT 未实现」），或补 `m_mip_levels` 的实现。

### G4. `camera.h:5` 写「旧 camera 保留」，代码里没有第二个相机类
- 位置：`camera.h:5`（`// 棱镜相机（重新实现的相机类，旧 camera 保留）。`）；同文件 `:5-11` 的「设计要点（相比旧 camera 的改进）」逐条对比一个不存在的类型
- 现状：全仓只有一个 `class xhqm_io camera`（`camera.h:19`），没有任何被保留的旧相机类；`camera` 是 `fantasy::models` 子类（`camera.h:19`），另有类型别名 `using prisma = fantasy::camera; using nekome = fantasy::camera;`（`system.h:140-141`）。主 README 在 `:458` 的说法（「唯一相机类，旧 camera 已于 r449 删除，`render_camera` 改名并入」）与代码一致；`camera.h:5` 这句是残留。
- 证据：`grep -rn 'class xhqm_io camera' fantasy/` → 仅 `camera.h:19`；`grep -rn 'render_camera' --include='*.h' --include='*.cpp' .`（排除 `.svn`）→ 0 命中（该名字只活在 `test/README.md:124` 与主 README 的历史叙述里）。
- 影响：接手者会去找一个不存在的「旧 camera」，或以为存在双相机迁移双轨；`test/README.md:124` 的 `render_camera` 同样对不上（该文档不属本模块，此处只登记）。
- 复现/验证：`grep -rn 'class xhqm_io camera\|render_camera' --include='*.h' --include='*.cpp' fantasy/ test/`。
- 建议：把这半句注释删掉，或改写为「旧 camera 已于 r449 删除」。

### G5. `runtime/async.h:38` 的注释「停止并 join（等待队列清空）」与实现语义不符
- 位置：`runtime/async.h:38`（`void stop();   // 停止并 join（等待队列清空）`）；实现 `runtime/async.cpp:42-54`
- 现状：实现是「置停 → `join()`（worker 先把队列**跑完**才退出）→ `m_completed.clear()`」，即**排空队列但不执行它们的 completion**，与 B2 是同一条事实的两面。注释里的「等待队列清空」字面上判对了（队列确实被跑空），但没有说「跑空的代价是同步阻塞」也**没有**说「completion 被丢弃」。
- 证据：`sed -n '36,54p' runtime/async.cpp`；`sed -n '37,39p' runtime/async.h`。
- 影响：宿主按注释理解为「很快返回」而把 `stop()` 放在主循环里，会撞上长任务阻塞；按注释理解为「停在队列中途」而期待 `completed()` 清零的人也会落空。
- 复现/验证：见 B2。
- 建议：注释改为「停止：join 后台线程（**会先跑完队列中已提交的 task**），丢弃未 poll 的 completion 回调」。

### G6. `theory_pass.h:60` 的 `theory_frame_graph::set_backend` 只有一帧寿命（被 `life()` 每帧覆盖）
- 位置：`theory_pass.h:60`（`void set_backend(render_backend& rhi) { m_graph.set_backend(rhi); }`）、`theory_pass.cpp:54`（`life()` 内每帧 `m_graph.set_backend(render_backend_active());`）
- 现状：`life()` 每帧都把当前 active 后端注入 `frame_graph`，所以 `set_backend()` 这个公开 API 的效果**活不过一帧**。它不会造成错误结果（因为 `life()` 的注入总是「正确的那一个」），但这是一个有误导性的公开接口。
- 证据：`sed -n '52,56p' theory_pass.cpp`；`grep -n 'set_backend' theory_pass.h theory_pass.cpp`。
- 影响：想让 fg 用非 active 后端（例如软件后端出图）的人会失败且无提示。
- 复现/验证：`grep -rn 'set_backend' fantasy/ test/` → 仅定义处（含 `runtime/frame_graph.h:47` 的底层实现）。
- 建议：把 `set_backend` 从公开接口移除（改成私有/删除），或让 `life()` 只在未显式设置时注入。

### G7. `test/ecs_test.cpp` 的断言数被主 README 记为 115，源码里只有 53 条内联 `CHECK`（其余靠循环重复）
- 位置：`fantasy/README.md:446`（「`test/ecs_test.cpp`（Catch2，9 用例 115 断言）」）；实际 `test/ecs_test.cpp`：`grep -c 'TEST_CASE'` → 9（用例数对），`grep -o 'CHECK(' | wc -l` → 53，`CHECK_NEAR` 等宏少量
- 现状：用例数（9）与文件规模（`:300` 起的 `test_ecs_benchmark`，`N = 100000`）都对得上；「115 断言」若指运行期实际执行的断言数，则来自 `:347-353` 那种**循环内** `CHECK_NEAR`（每实体 3 条 × 循环次数），源码级只有 53 条 `CHECK` + 少量 `CHECK_NEAR`/`CHECK_NE`。两种口径都不是 115。
- 证据：`cd test && grep -c TEST_CASE ecs_test.cpp` → 9；`grep -o 'CHECK(' ecs_test.cpp | wc -l` → 53；`sed -n '340,355p' ecs_test.cpp`。
- 影响：数字对不上会让人以为测试缩水了（实际没有），属文档口径问题。对照 `test/toon_test.cpp` 的「6 用例 31 断言」是**准确**的（`grep -c TEST_CASE` → 6、`grep -o 'CHECK\|REQUIRE'` → 31）。
- 复现/验证：`grep -c 'TEST_CASE' test/ecs_test.cpp; grep -o 'CHECK(' test/ecs_test.cpp | wc -l`。
- 建议：主 README 改成「9 用例、源码级 53 条断言（另含循环内断言）」，或给出实际运行输出的断言计数。

### G8. `fantasy/backend/README.md` 的文件数/行数已被同批复核订正，`rhi_vk.h` 的占位注释仍在（本组最新状态）
- 位置：`backend/README.md:8`（**现已带作废标记**）、`backend/rhi_vk.h:10`
- 现状：本文件初次写就时 `backend/README.md:5`/`:91` 还写着「17 个文件约 6609 行、`rhi_vk.cpp` 3669 行」；**在本模块复核的同时，`backend/` 子模块的那一份 README 已被同一批复核改掉** —— `backend/README.md:8` 现在是 `~~17 个文件约 6609 行（rhi_vk.cpp 3669 行…）~~ **已作废（2026-10-07 复核 r677）**：源文件实测合计 **7210 行**，`rhi_vk.cpp` 已是 **4078 行**…`。实测复核该数字**正确**：`backend/` 下 17 个源文件（不含同批新建的 `DEBUG.md`）合计 **7210 行**（`find . -maxdepth 1 -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' \) | xargs wc -l`），`rhi_vk.cpp` 4078、`rhi_gl.cpp` 1151。
- 证据：`grep -n '3669' backend/README.md` → 只命中 `:8` 的作废行；`find fantasy/backend -maxdepth 1 -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' \) | wc -l` → 17；上述 `wc -l` → 7210。
- 影响：这一条**已闭合**（模块内的旧数字订正了），留下的唯一活口是 `backend/rhi_vk.h:10` 那半句「（当前为占位实现）」—— 见 **G1**（并指向根 `DEBUG.md` G1）。之所以仍列在本文件，是给接手者留一条「数字漂移 ≠ 结论漂移」的对照：行数订正了，注释没订正。
- 复现/验证：`grep -n '3669' fantasy/backend/README.md`；`find fantasy/backend -maxdepth 1 -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' \) | xargs wc -l | tail -1`。
- 建议：删掉 `backend/rhi_vk.h:10` 里的「（当前为占位实现）」（其余数字无需再动）。

> 关于本文件的落盘范围：本轮只写了 `fantasy/README.md` 与 `fantasy/DEBUG.md` 两个文件；`backend/`、`spatial/`、`postfx/`、`animation/`、`lighting/`、`runtime/` 六个子模块的 `README.md`/`DEBUG.md` 变更**不是本轮所为**（属同批全仓复核的另一路，`svn status` 里与 `fantasy/DEBUG.md` 同批出现）。子模块台账与本文件的分工：子模块只记自己目录内的问题，本文件记跨子模块的接线与总体取舍（如 F2/F3/D2/E3）。

---
