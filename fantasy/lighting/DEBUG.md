# xhqm · fantasy / lighting 当前问题清单

`fantasy/lighting`（光照与阴影：光源数据环境 `light`/`light_desc` + 方向光单级阴影 `shadow_map` + 级联阴影 `cascaded_shadow_map` + 点光 cube 阴影 `point_shadow_map`）在 r677 上**仍然存在**的问题台账 —— 只记「现在还是坏的 / 还是没写的 / 还是对不上的」，每条给出可核对的 `文件:行号`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。

> **与同类文件的关系**：本仓的历史台账**不搬家、不改名** —— `fantasy/fantasy_bugs.md`（fantasy 静态分析清单 + 逐轮修复记录，编号 1–53，215 行）与 `network/nebula/DEFERRED.md`（nebula 讨论过但决定不做/没排期的事项）保持原名原位置，本文件只指过去、不复制内容。
>
> **父级台账 `fantasy/DEBUG.md`（module id `fantasy`）与本文件同批建立，两边不重复记同一件事**。已知重叠/指向关系：本文件 **A4**（点光 far 写死 10）＝父级 **A2**；本文件 **G3**（PCF texel 写死 1/2048）＝父级 **A1**；本文件 **E1 / E2 / E3**（CSM 与点光 cube 阴影未接线、无多光源阴影管理）＝父级 **F3**（「收敛到单一方向光 `shadow_map`，CSM 与点光 cube 保留为可手工接线的组件」）；本文件 **B3 / F3** 与父级 **F4**（光源上限 8、`UBO_MAX_*` 单一事实源）**部分重叠** —— 父级讲「截断与单一事实源的取舍」，本文件补的是模块侧的两个入口（`light` 自己不拦、打包逻辑在模块与 drawer 各有一份）。本文件保留的是**模块侧的 API 契约、本模块自己的证据行与 README 侧影响**，父级那几条是同一件事的全仓视角；其余条目（A1–A3、B1–B2、B4、C1、D1、E4–E5、F1–F2、F4、G1–G2）不与父级文件重叠。`fantasy/backend` 侧的缺陷若**只**在点光 cube 阴影的用法下暴露，记在本文件的 A2 / A3 并标注「跨模块」。本模块的定位与用法见同目录 `README.md`，本文件不重抄。

> 判据纪律：凡写「会崩 / 会算错 / 编译不过」，必须附可复核的证据行或复现命令。下面所有行号与常量都在本工作副本 r677 上实测过。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. `cascaded_shadow_map::light_view_proj(i)` 没有任何越界/空表保护，越界即未定义行为
- 位置：`fantasy/lighting/csm.h:41`
- 现状：实现是 `const matrix& light_view_proj(uint i) const { return m_light_vp[i]; }` —— 直接下标。同文件同族的其它访问器都有保护：`split_far`（`csm.h:42`，`i < m_split.size()` 否则返回 0）、`depth_texture(i)`（`csm.cpp:88`）、`point_shadow_map::face_vp/face_view`（`point_shadow.h:36-37`，越界回退第 0 面）。而 `setup()` 在 `count==0 || count>4` 时直接返回 false（`csm.cpp:15`），`reset()` 会把 `m_light_vp.clear()`（`csm.cpp:39`）→ 此时 `m_light_vp` 为空。
- 证据：`grep -n "light_view_proj" fantasy/lighting/csm.h` → `:41`（读取，无保护）；`grep -n "m_light_vp" fantasy/lighting/csm.cpp` → `:19`（resize）、`:37`/`:39`（clear）、`:67`（写入）。空 vector 的下标访问在 libstdc++ 的 `operator[]` 里不做任何检查。
- 影响：`cascaded_shadow_map c; c.light_view_proj(0);`（未 setup、setup 失败、或 reset 之后）读到非法内存 —— Release 下不报错，只是把一个垃圾矩阵交出去；带 ASan/UBSan 的构建才会当场报错。谁会被坑：把 `cascaded_shadow_map` 接进管线的人 —— 按 `drawer` 现有消费方式（`fantasy/theory_render.cpp:81-84` 的 `lvps.resize(...); lvps[0] = lvp;`），一个越界下标就会把垃圾光 VP 写进所有 drawer 的阴影态，阴影整片错位。
- 复现/验证：**未复现**（静态可判定：对空 `std::vector` 取下标必然是越界读；本节点只做只读核对，未构造临时 TU 运行）。
- 建议：加 `i < m_light_vp.size()` 判断并返回一个静态单位矩阵（与 `face_vp` 的回退风格一致）。

### A2. 点光 cube 阴影在 GL 后端每面**不复位深度**，采到的是上一帧/未初始化内容（跨模块：缺陷在 backend，只在点光阴影用法下暴露）
- 位置：`fantasy/backend/rhi_gl.cpp:1112-1134`（`begin_offscreen_cubemap`，无清深度），对照 `fantasy/backend/rhi_gl.cpp:984-1002`（2D 深度专用离屏会清）
- 现状：GL 的 2D 深度专用离屏在 `begin_offscreen` 里显式 `glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);`（`rhi_gl.cpp:996-999`），这是当年修 #50 时加的；**cubemap 分支没有等价动作**。Vulkan 侧靠 render pass 的 `loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR`（`fantasy/backend/rhi_vk.cpp:3554`）+ `clear.depthStencil = {1.0f, 0}`（`:3741`）覆盖，所以 GL/VK 行为不一致。
- 证据：`grep -n "glClear" fantasy/backend/rhi_gl.cpp` → 仅 `:168-169`（`begin_frame`）与 `:998-999`（2D 离屏），`1112-1134` 段内为 0。
- 影响：`point_shadow_map::begin(face)` + 渲染几何 + `end()` 之后，没被几何写到的纹素保留上一帧或未初始化深度 → 点光遮挡判定错误（全亮/暗斑闪烁），且只在 GL 后端出现。触发条件：GL 后端 + 任何点光 cube 阴影用法。
- 复现/验证：`grep -n "glClear" fantasy/backend/rhi_gl.cpp`（应为 4 条、无 cubemap 段）。**端到端未复现**（需 GPU + GL 上下文）；注意现有自检 `test/view/main.cpp:668-749` **抓不到**这一点 —— 它用一个铺满整面的 quad（`gl_FragDepth = 0.9`，`:679`）填满 6 个面，任何陈旧值都被覆盖。
- 建议：`begin_offscreen_cubemap` 的深度分支补 `glClearDepth(1.0)/glClear(GL_DEPTH_BUFFER_BIT)`，与 2D 路径对齐。

### A3. GL 后端 `begin_offscreen_cubemap` 不保存前序 FBO/视口，`end_offscreen()` 恢复到的是「上一次 2D 离屏之前」的目标（跨模块）
- 位置：`fantasy/backend/rhi_gl.cpp:1112-1134`（cubemap begin，未保存）、`:984-987`（2D begin 保存 `m_prev_fbo`/`m_prev_viewport`）、`:1004-1008`（`end_offscreen` 恢复这两个成员）
- 现状：GL 侧只有 2D `begin_offscreen` 会记录进入前的 framebuffer/视口；cubemap begin 直接 `glBindFramebuffer(cube.fbo)` + `glViewport(0,0,ms,ms)`（`:1116`、`:1131`），不记录任何东西，也没有「当前是 cubemap」的标志位。于是随后的 `end_offscreen()` 恢复的是陈旧的 `m_prev_fbo`/`m_prev_viewport`（初值 `0`/`{0,0,0,0}`，`rhi_gl.cpp:65-66`）。
- 证据：`grep -n "m_prev_fbo\|m_prev_viewport" fantasy/backend/rhi_gl.cpp` → 只有 `:65`、`:66`、`:986`、`:987`、`:1006`、`:1007`，全部在 2D 路径上。Vulkan 侧用 `m_offscreen_active`/`m_cubemap_active` 双标志兜底（`fantasy/backend/rhi_vk.cpp:3351`、`:3354-3355`）→ 后端行为不一致。
- 影响：在已绑定的离屏 pass 内使用点光阴影（典型顺序：`begin_offscreen(HDR)` → 逐面 `psm.begin(f)/end()` → 继续往 HDR 画）时，GL 侧会先回到 HDR 绑定的**前一个**目标（通常就是默认帧缓冲）→ 后续绘制画到错误目标，画面丢失/闪烁。谁会被坑：任何在 HDR/后处理离屏之间插点光阴影的宿主。
- 复现/验证：**未复现**（需 GL 运行环境）。可核对的判据：帧内 `begin_offscreen(A)` → `psm.begin(0); psm.end();` → 用 `read_offscreen(A, ...)` 检查是否仍读得到 A 的内容（若已回到 swapchain，读到的是窗口内容）。
- 建议：cubemap begin/end 走与 2D 相同的保存/恢复（或按 VK 那样加一个「cubemap 活跃」标志）。

### A4. 点光阴影的远平面被采样侧写死成 10，模块 API 却接受任意 `far` —— 传别的值静默算错（跨模块）
- 位置：`fantasy/lighting/point_shadow.cpp:39-51`（`compute(light_pos, near, far)` 对 `far` 无任何约束），消费侧 `fantasy/backend/shader_preprocess.cpp:91-98`
- 现状：着色器采样 cube 阴影时固定用 `dist / 10.0 - bias` 当比较值（4 次 tap 都是），同处注释自陈「生产需距离写入深度着色器（`gl_FragDepth = dist/far`）配合 6 面渲染；**far 暂按 10**」（`shader_preprocess.cpp:91`）。也就是说「far=10」这个契约只存在于 shader 里，模块这一侧既没有默认值、没有校验、也没有宏统一下发。
- 证据：`grep -n "暂按 10\|10.0 - bias" fantasy/backend/shader_preprocess.cpp` → `:91`、`:95-98`；`grep -n "compute(" test/view/main.cpp` → `:433` 传 `0.1f, 50.0f`（只做 VP 有限性检查）、`:701` 传 `0.1f, 10.0f`（真正做遮挡采样，与 shader 的 10 对齐）；而本模块 README 的示例原来写的是 `50.0f`（本次已订正为 10.0f）。
- 影响：谁按「随便传个远平面」的直觉传 `far=50` 并把纹理接到 `samplerCubeShadow` 上，深度归一化就与 shader 的 `/10.0` 不匹配 → 点光遮挡要么全亮要么全黑（不报错、不告警）。触发条件：任何 far≠10 的点光阴影启用。
- 复现/验证：`grep -n "10.0 - bias" fantasy/backend/shader_preprocess.cpp` 与 `grep -n "psm.compute" test/view/main.cpp` 对照即可；**端到端未复现**（需 GPU）。
- 建议：把归一化常数提到模块常量（如 `POINT_SHADOW_FAR`）并在 `compute()` 里断言/纠正，或让 shader 用宏接收同一个值。
- 与父级的分工：同一条已在 `fantasy/DEBUG.md` **A2** 登记（全仓视角）；本文件这一条只保留**本模块侧**的要点 —— `point_shadow.cpp:39-51` 对 `far` 无约束、本模块 README 的示例曾因此写成 `50.0f`（本次已订正为 `10.0f`）。

---

## B. 资源与并发

### B1. 三个阴影类都持有裸 `render_backend*`，析构无条件回调后端 —— 后端先死即悬垂
- 位置：`fantasy/lighting/shadow.cpp:25-29`、`fantasy/lighting/csm.cpp:33-36`、`fantasy/lighting/point_shadow.cpp:28-32`（`reset()` 里的 `m_rhi->delete_offscreen(...)` / `m_rhi->delete_offscreen_cubemap_depth(...)`）；成员声明 `shadow.h:16`、`csm.h:18`、`point_shadow.h:16`
- 现状：`m_rhi` 是裸指针，`reset()` 只判断 `m_rhi && m_target`，不判断后端是否还活着；析构函数直接调 `reset()`（`shadow.cpp:7-10`、`csm.cpp:7-10`、`point_shadow.cpp:7-10`）。
- 证据：`grep -n "m_rhi->delete" fantasy/lighting/*.cpp` → `shadow.cpp:27`、`csm.cpp:35`、`point_shadow.cpp:30`。
- 影响：宿主按「先销毁 `render_backend`，再销毁持有阴影对象的场景」的顺序收尾时（DLL 卸载同理），析构会调用已销毁对象的方法 → 崩溃或释放错对象。当前引擎里 `theory_shadow` 的析构顺序由 `property.cpp` 的 pass 列表决定，`m_shadow` 是值成员，谁先谁后没有任何编译期约束。
- 复现/验证：**未复现**（需要构造「后端先销毁」的宿主顺序；本节点未跑渲染宿主）。
- 建议：把后端引用换成句柄/弱引用，或在 `render_backend` 析构时统一通知（当前无此机制）。

### B2. `end()` 与 `begin()` 不对称：`begin()` 提前返回后 `end()` 仍会 `end_offscreen()` + 打开颜色写
- 位置：`fantasy/lighting/shadow.cpp:68-80`、`fantasy/lighting/csm.cpp:72-84`、`fantasy/lighting/point_shadow.cpp:72-84`
- 现状：`begin()` 在目标无效时提前返回（`shadow.cpp:70` 的 `if (!m_rhi || !m_target) return;`，`csm.cpp:74` 的 `i >= m_targets.size()`，`point_shadow.cpp:74` 的 `face > 5`），**没有绑定任何离屏、也没有关颜色写**；而 `end()` 只判断 `m_rhi`，随后无条件执行 `color_write(true)` + `end_offscreen()`。
- 证据：`sed -n '68,80p' fantasy/lighting/shadow.cpp`（begin 早退 / end 无对称判断）。
- 影响：`setup()` 失败、`size` 传 0、级联下标写错时，一次「空配对的 begin/end」就把后端状态带偏：GL 的 `end_offscreen()` 会 `glBindFramebuffer(m_prev_fbo)` 并恢复陈旧视口（与 A3 同一处代码），后续渲染画错目标。谁会被坑：把 `begin/end` 包在 `if` 里的宿主（以为自己只是「没画」）。
- 复现/验证：**未复现**（GL 运行环境下可用 `glGetIntegerv(GL_FRAMEBUFFER_BINDING)` 前后比对；本节点无 GL 上下文）。
- 建议：给三个类加一个 `m_in_pass` 标志，`end()` 里与之对称地判断（或让 `end()` 不碰状态直接返回）。

### B3. 光源数量无上限：`light` 不拦 `MAX_LIGHTS`，多出来的灯在上传时被静默丢弃
- 位置：`fantasy/lighting/light.h:70`（`add_light` 直接 `push_back`，无上限）、`light.h:79`（`light_count()` 报全量）、`light.h:100`（`m_light_data.assign(m_lights.size()*16, 0)` 也无上限），消费侧 `fantasy/drawer.cpp:694`、`fantasy/backend/rhi_gl.cpp:658`、`fantasy/backend/rhi_vk.cpp:2531`
- 现状：`MAX_LIGHTS=8`（`light.h:23`）只是一个常量；真正的截断发生在两处上传路径上（`count > MAX_LIGHTS` / `UBO_MAX_LIGHTS` 时截到 8），`light` 本身既不拒绝第 9 盏灯也不回报，`light_count()` 仍返回 12。
- 证据：`grep -n "MAX_LIGHTS" fantasy/lighting/light.h fantasy/drawer.cpp fantasy/backend/rhi_gl.cpp fantasy/backend/rhi_vk.cpp` → `light.h:23`（定义，全模块无其它引用）、`drawer.cpp:694`、`rhi_gl.cpp:658`、`rhi_vk.cpp:2531`（截断）。UBO 侧 `flags[FLAG_LIGHT_COUNT]` 写的是截断后的值（`rhi_gl.cpp:659-661`、`rhi_vk.cpp:2535-2537`），所以着色器与上传是一致的 —— 只是**宿主看到的光源数与实际渲染数不同**。
- 影响：宿主用 `light_count()` 做 UI、日志或「有没有超上限」的判断时会拿到偏大的数；`light_data()` 还按未截断的 n 分配（12 盏 = 768 B，无上限保护）。触发条件：光源数 >8。
- 复现/验证：**未复现运行**；静态可判定（`grep` 上述四处的截断点即可）。
- 建议：`add_light()` 里钳到 `MAX_LIGHTS` 并让 `light_count()` 反映实际可上传的数量（或返回截断后的值）。
- 与父级的分工：父级 `fantasy/DEBUG.md` **F4** 从「截断是当前取舍」的全仓视角记了这件事；本文件这一条只补模块侧的入口（`light.h:70` 的 `push_back` 无上限、`light_count()` 报全量、`light_data()` 按未截断的 n 分配）。

### B4. `light_data()` 返回内部 vector 的引用，但「写入口后引用失效」这条契约没写在接口上
- 位置：`fantasy/lighting/light.h:96-115`（`assign` 会重新分配），写入口 `light.h:68-78`
- 现状：返回 `const std::vector<value>&`；`m_light_data_dirty` 时用 `assign(m_lights.size()*16, 0)` 重建 —— `assign` 的重新分配会让先前拿到的引用/`data()` 指针变成悬垂。`drawer.cpp:695` 是每帧现取现用（安全），所以当前引擎不触发。
- 证据：`sed -n '96,115p' fantasy/lighting/light.h`（脏时 `assign` + 返回同一成员的引用）。
- 影响：宿主若缓存 `const auto& d = env.light_data();` 或 `const value* p = ...data();`，之后任何 `add_light/lights/clear_lights/set_directional` 都可能让 `p` 失效（读到释放内存）。触发条件：跨帧/跨写入持有该引用或指针。
- 复现/验证：**未复现**；可静态判定（`assign` 语义 + 返回引用）。
- 建议：在 README/接口注释里写明「写入口调用后引用与 `data()` 均失效」，或干脆按值返回/加版本号。

---

## C. 跨平台

### C1. 导出宏不对称：`light` / `point_shadow_map` 标了 `xhqm_io`，`shadow_map` / `cascaded_shadow_map` 没标，而导出类 `theory_shadow` 用 `shadow_map` 当值成员
- 位置：`fantasy/lighting/light.h:51`（`class xhqm_io light`）、`fantasy/lighting/point_shadow.h:14`（`class xhqm_io point_shadow_map`）对照 `fantasy/lighting/shadow.h:14`（`class shadow_map`）、`fantasy/lighting/csm.h:16`（`class cascaded_shadow_map`）；使用处 `fantasy/theory_render.h:26`（`class xhqm_io theory_shadow`）与 `:29`（`shadow_map m_shadow;`）
- 现状：`xhqm_io` 展开为 `XHQMCLASS`（`head.h:28`）或空（`head.h:30`），窗口版 DLL 构建会走导出分支；库可以编成 SHARED（`CMakeLists.txt:588` `add_library(xhqm SHARED ...)`，工作副本里也确实存在 `build-shared/` 配置）。
- 证据：`grep -n "class xhqm_io" fantasy/lighting/*.h` → 仅 `light.h:51`、`point_shadow.h:14`；`grep -n "class shadow_map\|class cascaded_shadow_map" fantasy/lighting/*.h` → `shadow.h:14`、`csm.h:16`（无 `xhqm_io`）。
- 影响：MSVC + DLL 配置下，非导出类型作为导出类的值成员/公开接口 → C4251 一类警告，且跨 DLL 调用 `shadow_map` 的内联成员（`light_view_proj`/`ready`/`width` 等，`shadow.h:37-42`）可能因未导出而链接失败或 ODR 不一致。Linux/GCC 静态库下无影响。
- 复现/验证：**未复现**（本节点为 Linux，无 MSVC 与 DLL 配置；仅静态判定，未在 Windows 上验证）。
- 建议：给 `shadow_map` / `cascaded_shadow_map` 补 `xhqm_io`，或明确它们只是内部类型（不进公开头接口）。

---

## D. 性能

### D1. 点光阴影一次刷新要 6 次离屏 pass（VK 侧图像已是 6 layer，却仍按面逐个 framebuffer 起 render pass）
- 位置：`fantasy/lighting/point_shadow.cpp:54-69`（每面单独算 VP）、`:72-77`（每面单独 `begin_offscreen_cubemap`）、`fantasy/backend/rhi_vk.cpp:3507`（`arrayLayers = 6`）、`:3745`（`c.framebuffers[face * c.mip_levels + mip]`）、`fantasy/backend/rhi_gl.cpp:1112-1134`（每面 `glFramebufferTexture2D` 换附件）
- 现状：cube 深度图像已经具备 6 layer + `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`（`rhi_vk.cpp:3513`），但绘制路径是「每个 face 一个 framebuffer、一次 `vkCmdBeginRenderPass`」；GL 侧每面切一次附件与视口。
- 证据：上面四处行号 + `grep -n "for (uint face" fantasy/lighting/point_shadow.cpp`（消费侧 `test/view/main.cpp:711-717` 也是逐面 6 次 begin/end）。
- 影响：接入点光阴影后每帧固定 6 次离屏切换 + 6 次场景遍历绘制；VK 本可一次分层渲染省掉 5 次，但这需要模块暴露「layer 视图 / 一次画完」的接口。
- 复现/验证：**未复现**（需 GPU 计时；`test/ffbo/fly_mcns_view.cpp:1101-1150` 里有一套把 shadow pass 拆开计时的现成量具，可复用来量）。
- 建议：VK 侧走分层渲染（每面一个 image view + 单 render pass），GL 侧保持逐面。

---

## E. 未实现 / 占位桩

### E1. `cascaded_shadow_map` 是孤儿类：模块外 0 个调用方，但 UBO / shader / drawer 都已为 4 级准备好（「编译过、只是没接上」）
- 位置：`fantasy/lighting/csm.h:16`（类）、`fantasy/lighting/csm.cpp`（整个 TU）；消费侧的预留：`fantasy/theory_render.h:29`（只用一个 `shadow_map`）、`fantasy/theory_render.cpp:79`（恒写 `cascade_count(1)`）、`fantasy/drawer.h:183-186`（`m_cascade_count` / `m_shadow_texs[4]` / `m_light_vps` / `m_cascade_splits[4]`）、`fantasy/backend/ubo.h:18`（`UBO_MAX_CASCADES = 4`）、`fantasy/backend/shader_preprocess.cpp:47-79`（4 级选择 + 3×3 PCF 已就绪）
- 现状：CPU 侧能算 4 级 VP/分割距离，shader 侧能选级采样，`drawer` 侧能收 4 张纹理 —— 中间没有任何代码把 `cascaded_shadow_map` 和这三者接起来；`test/CMakeLists.txt:245`、`:289` 只把它编进测试目标（保证链接覆盖），`test/view/main.cpp` 也没用它。
- 证据：`grep -rn "cascaded_shadow_map" --include=*.h --include=*.cpp . | grep -v "fantasy/lighting"` → 只有 `fantasy/lighting/point_shadow.h:6` 的一句注释命中，代码调用 0 处。
- 影响：接手的人看 `fantasy/README.md:260`（把 CSM 列为「支持」的能力）会以为级联阴影已经在跑；实际任何场景都是单级。要接上需要自己写「每帧算 N 级 + 逐级渲染 + 写 4 组纹理/VP/分割」这段。
- 复现/验证：上条 `grep` 即复现（0 命中）；另可 `grep -n "cascade_count" fantasy/theory_render.cpp` → 只有 `:65`、`:78`（写 0）与 `:79`（写 1）。
- 建议：要么在 `theory_shadow` 里接上 2–4 级，要么在父级 README 与本模块 README 里标成「库级组件、未接线」。
- 与父级的分工：父级 `fantasy/DEBUG.md` **F3** 从全仓视角记了同一个取舍（「收敛到单一方向光 `shadow_map`，CSM 与点光 cube 保留为可手工接线的组件」）；本文件这一条只补**模块侧**的可核对事实（0 个调用方、shader/UBO/drawer 已就绪的具体行号）。

### E2. 点光 cube 阴影同样没有 in-tree 调用方，而且**缺少「把距离写进深度」的着色器路径**（shader 注释自陈未完成）
- 位置：`fantasy/lighting/point_shadow.h:14`、`fantasy/lighting/point_shadow.cpp`（整 TU）；唯一使用者 `test/view/main.cpp:429`、`:669`；缺失的写深度着色器见 `fantasy/backend/shader_preprocess.cpp:91`
- 现状：采样侧（`samplerCubeShadow` 声明与 4-tap 采样，`shader_preprocess.cpp:90-100`、`:290-291`）已经写好，但产生这张深度图需要的「面内写入线性距离」着色器在工程里不存在 —— shader 注释原文：「生产需距离写入深度着色器（`gl_FragDepth = dist/far`）配合 6 面渲染；far 暂按 10」。仓库里唯一的 `gl_FragDepth` 写在 `test/view/main.cpp:679`/`:687`，是自检用的常量 `0.9`。另外引擎侧也**没有任何把 cube 阴影纹理喂给 shader 的绑定路径**。
- 证据：`grep -rn "gl_FragDepth" --include=*.cpp --include=*.h .`（去 build 目录）→ 只有上述 2 处测试常量 + `shader_preprocess.cpp:91` 的注释；`grep -rn "bind_texture(9" fantasy/` → 0 命中（测试自检用的是自己的 shader + `rb.bind_texture(4, cube_tex)`，`test/view/main.cpp:720`）。
- 影响：想把点光阴影用起来，必须自己补一个写距离的深度着色器 + 自己接纹理绑定；「库里已经有实现」是错觉的一半。
- 复现/验证：`grep -rn "bind_texture(9" fantasy/`（0 命中）、`grep -n "暂按 10" fantasy/backend/shader_preprocess.cpp`（1 命中）。
- 建议：补写距离的深度着色器与 draw 接口的 cube 阴影槽，或在 README 里明确标「未接线」。
- 与父级的分工：同属父级 `fantasy/DEBUG.md` **F3** 的「保留为可手工接线的组件」；本文件这一条补的是**缺失件的具体位置**（写距离着色器不存在、cube 纹理无绑定路径）。

### E3. 点光阴影没有逐面视锥裁剪，也没有多光源阴影管理（分层渲染见 D1）
- 位置：`fantasy/lighting/point_shadow.cpp:39-70`（只算 6 个 VP，无可见集）、`:72-77`（`begin(face)` 只做绑定）；模块内无任何「阴影图池/多光源」类型（`fantasy/lighting/light.h` 只存光源描述字段，不带阴影图句柄）
- 现状：6 面各自渲染时必须把**整个场景**画一遍（消费侧 `test/view/main.cpp:711-717` 就是 6 次全量 draw）；`light`/`light_desc` 里没有任何「这张阴影图属于哪盏灯」的字段。`theory_shadow` 也只取第一盏方向光（`fantasy/theory_render.cpp:147-148` 找到就 `break`），所以引擎里最多只有一张方向光阴影图。
- 证据：`grep -rni "cull\|frustum\|multiview\|layer" fantasy/lighting/` → 0 命中；`grep -n "has_dir = true; break" fantasy/theory_render.cpp` → `:148`。
- 影响：点光阴影接上后每面都是全场景绘制（浪费，D1 量的就是这笔）；「≤8 光源」里有第二盏方向光、任何点光/聚光时，没有地方安放它们的阴影图。触发条件：接入点光/多灯阴影之前必须先补这一层。
- 复现/验证：上两条 `grep` 即复现。
- 建议：先补 per-face 可见集 + 阴影图池（灯→图的映射），再谈分层。

### E4. 聚光灯完全没有阴影（`light_type::spot` 只参与锥体着色）
- 位置：`fantasy/lighting/light.h:19`（`spot = 2`）、`light.h:33-34`/`light.h:110`（只提供余弦阈值）；锥体衰减在 `fantasy/backend/shader_preprocess.cpp:102-108` 与 `:193-200`
- 现状：本模块只有方向光（`shadow_map`）与点光（`point_shadow_map`）两种阴影载体，没有 spot 阴影类/文件；`lighting/` 目录里 4 个头 3 个 `.cpp` 就是全部（`ls fantasy/lighting`）。
- 证据：`grep -rn "spot" fantasy/lighting/` → 只有 `light.h:19`（枚举）、`:33`、`:34`（注释）、`:110`（写进数据）；`grep -rni "spot_shadow" .` → 0 命中。
- 影响：聚光灯能亮不能挡；需要聚光阴影的宿主只能自己写，或用点光 cube 阴影近似（但那套本身也还没接线，见 E2）。
- 复现/验证：`ls fantasy/lighting` + 上两条 `grep`。
- 建议：无（明确记「未实现」即可，别让它悬在文档的「支持」列表里）。

### E5. CSM 没有 texel snapping（相机一移动，级联/方向光的纹素采样点就漂移）
- 位置：`fantasy/lighting/csm.cpp:59-69`（每级半径 = 该级 far 分割距离，中心直接取 `camera_pos`）、`fantasy/lighting/shadow.cpp:35-66`（中心直接取 AABB 中心，未量化到纹素网格；正交半宽直接用 `radius`，`shadow.cpp:64`）
- 现状：光空间 VP 每帧随相机位置连续变化，没有任何「把中心对齐到 `2r/分辨率` 网格」的动作；也没有暴露开关或文档说明。
- 证据：`grep -rni "snap\|texel" fantasy/lighting/` → 0 命中（对比：着色器侧 PCF 反而写死了 texel 步长，见 G3）。
- 影响：方向光阴影（以及未来接入的 CSM）在相机平移时纹素中心漂移 → 阴影边缘逐帧抖动/shimmer；当前没有任何缓解手段（既无 snapping，也无「固定尺寸 AABB」等替代）。
- 复现/验证：**未复现**（需 GPU 逐帧比对；`fantasy/fantasy_bugs.md:215` 记了本仓可用的「`--still` + 逐帧像素比对」手法，可复用来量抖动）。
- 建议：把光空间中心量化到纹素网格（texel snapping）并暴露开关，或至少在文档里写明这个已知代价。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. `cutoff` / `outer_cutoff` 存的是**余弦**而不是角度
- 位置：`fantasy/lighting/light.h:33-34`（注释即「聚光内角余弦 / 聚光外角余弦」）、`light.h:42-43`（默认内 0.9f / 外 0.8f）、`light.h:110`（写进每光第 4 个 vec4 的 `.x/.y`）；消费侧 `fantasy/backend/shader_preprocess.cpp:105-106` 与 `:196-197`
- 现状与证据链：`light_data()` 把两个值写进 `p[12]/p[13]`（`light.h:110`），对应 UBO 里 `struct Light { … vec4 spot; }` 的 `spot.x/spot.y`（`fantasy/backend/ubo.h:120`），着色器直接拿它们与 `cosTheta = dot(normalize(lt.direction.xyz), -lightDir)` 比较：`epsilon = lt.spot.x - lt.spot.y; intensity = clamp((cosTheta - lt.spot.y) / max(epsilon, 0.0001), 0, 1)`。
- 为什么现在这样是对的：片元里本来就要算 `dot()`，存余弦可以省掉每片元一次 `acos()`；std140 里也只占两个 float，不需要额外字段或类型。
- 代价：命名（`cutoff` / `outer_cutoff`）读起来像角度。宿主按「度/弧度」填（比如内 30°/外 45° → 0.52/0.79）会得到 `epsilon < 0`，被 `max(epsilon, 0.0001)` 兜住，结果是硬边或整锥全黑 —— **不报错、不告警**；另外内角余弦必须**大于**外角余弦（cos 单调递减），填反即静默错光。模块与着色器都没有任何范围校验、`assert` 或文档外的检查。
- 建议（不修）：保留余弦语义，但至少在 `add_light`/渲染前断一下 `cutoff > outer_cutoff` 且两者 ∈ (0,1]。

### F2. CSM 用「以相机为球心的球」覆盖，而不是视锥切片贴合
- 位置：`fantasy/lighting/csm.cpp:59-69`（`r = split[i+1]`，`box = camera_pos ± r`，再交给 `shadow_map::compute_light_view_proj`）
- 现状：每级的覆盖体是一个以相机为球心、半径等于该级 far 分割距离的立方体（AABB）；半径只跟分割距离有关，与相机 FOV、宽高比、朝向**都无关**。
- 为什么现在这样是对的：球（用 AABB 表达）对相机**旋转完全不变** —— 相机只转不移动时，光 VP 一模一样，天然没有旋转引起的纹素漂移；实现只有 4 行，还完整复用了 `shadow_map::compute_light_view_proj`（球是 AABB 的一个特例），不需要引入视锥 8 点/凸包拟合。在「先把级联接上、再谈质量」的当前阶段，这个选择是合理的。
- 代价：覆盖体与视锥不贴 → 同样的 2048² 纹理要覆盖整球，纹素密度低于视锥贴合，窄 FOV 时浪费尤其明显（远景级联分辨率不足，需靠加大分辨率或缩 far 补偿）；也因为没有视锥，无法用「相机视锥与场景 AABB 求交」来进一步收紧。
- 建议（不修）：接线上 CSM 之后，若确定要提质量，再改成视锥贴合（会与 E5 的 texel snapping 一起做更划算）。

### F3. 每光 16 个 float 的打包逻辑存在**三份**（模块一份、drawer 旧路径一份，偏移常量一份）
- 位置：`fantasy/lighting/light.h:100-111`（新路径：`light_data()` 缓存）、`fantasy/drawer.cpp:700-720`（旧路径：drawer 自持光源时现场打包）、`fantasy/backend/ubo.h:31-32`（`UBO_LIGHTS`/`UBO_LIGHT_VP` 用 `* 64` 手工算偏移）、`fantasy/backend/ubo.h:120`（GLSL 侧 `struct Light { vec4×4 }`）
- 现状：同一份布局在 4 个地方各写了一遍（`p[0..2]`/`p[3]`/`p[4..6]`/`p[8..10]`/`p[12]`/`p[13]`，以及 `64`、`16` 这些魔数）；`LIGHT_BYTES=64`（`light.h:25`）只有注释引用、没有参与计算，三处之间没有 `static_assert`。
- 为什么现在这样是对的：「有 `light_env` 就用环境缓存、没有就退回 drawer 自持光源」的兼容路径不能砍（旧宿主/旧 demo 直接用 `drawer->lights(...)`，`fantasy/drawer.cpp:700-716`），两套入口并存就意味着两份打包；把偏移常量放在 `ubo.h` 也是为了让 GLSL 块声明与字节偏移在同一文件里对齐。
- 代价：改布局（加字段、换顺序、改 `MAX_LIGHTS`）必须**同时**改三处 + GLSL 声明，漏一处不会编译报错，只会静默错位；`MAX_CASCADES` 同理（`csm.cpp:15` 的硬编码 4 与 `ubo.h:18` 的 `UBO_MAX_CASCADES=4` 无关联）。
- 建议（不修）：短期靠注释互指；真要收敛就让 `drawer` 旧路径也调 `light::light_data()` 的同一实现，并给 `MAX_LIGHTS`/`MAX_CASCADES` 加 `static_assert`。
- 与父级的分工：父级 `fantasy/DEBUG.md` **F4** 讲的是 `backend/ubo.h` 作为单一事实源（本文件不重复那部分推导）；本文件这一条只讲**模块侧也各存一份打包/上限常量**这件事。

### F4. 阴影图每帧整张重渲，没有脏标记/降频，也不按需重建（pass 侧取舍）
- 位置：`fantasy/theory_render.cpp:182-197`（只有 `size` 变化才重建**目标**；内容每帧重画）
- 现状：`theory_shadow::render()` 每帧都做「合并场景 AABB → 算 VP → `begin()` → `traverse(lightVP)` + `models::drawer()` → `end()`」；唯一的缓存判断是 `if (m_shadow.ready() && m_applied != m_size) m_shadow.reset();`（尺寸变了才重建离屏目标），场景内容变化没有任何检测。
- 为什么现在这样是对的：场景是动态的（动画、相机、实例都在动），逐帧重渲是最简单且永远正确的做法；并且它让「阴影 pass 与主 pass 用同一套遍历」这件事不需要额外的版本号/脏标记基础设施。`fantasy/fantasy_bugs.md:151` 记的「逐帧阴影 pass 与 GL/VK 状态机问题叠加」已经在该处单独处理过。
- 代价：每帧固定多一次全场景深度 pass（`test/ffbo/DESIGN_NOTES.md:514-516` 与 `test/ffbo/RENDER_FIX_PLAN.md:281` 记：用公开 API `theory_shadow::enabled(false)` 关掉 shadow pass，fly demo 9.3 → 11.2 fps，即阴影 pass 约占该场景帧时的 1/6）；离屏 `begin/end` 每帧各一次，历史上有过稳定性问题（`fantasy/fantasy_bugs.md:151`）。
- 建议（不修）：需要时再给宿主一个「阴影图降频/按需刷新」的开关；当前默认行为保持不变。

---

## G. 文档与代码的出入

### G1. 父级 `fantasy/README.md` 把 CSM 列为「支持」的能力，同文件另一处又写明实际是单级
- 位置：`fantasy/README.md:266`（「支持：…级联阴影（`cascaded_shadow_map`，最多 4 级）、点光 cube 阴影（`point_shadow_map`）…」）、`fantasy/README.md:82`（把 `shadow_map`/`cascaded_shadow_map`/`point_shadow_map` 并列成一项能力）对照 `fantasy/README.md:311`（「`theory_shadow` | 方向光阴影（前置，**单级** shadow_map）」）
- 现状：`:311` 是对的（并且与代码一致，见 E1），`:266`/`:82` 的「支持」措辞会被读成「管线里已经在用」。（父级 README 与本文件同批在同步，行号会漂移 —— 以上是 2026-10-07 本节核对时的行号，以引文为准。）
- 证据：`grep -n "级联阴影\|cascaded_shadow_map" fantasy/README.md`；再对照 `grep -rn "cascaded_shadow_map" --include=*.cpp .`（0 调用）。
- 影响：接手者按父级 README 判断「CSM 已完成」，直接跳过接线工作。
- 复现/验证：上两条 `grep` 即复现。（本文件不修改父级文档 —— 铁律只允许改本模块 README/DEBUG；父级 README 的订正需由对应模块的维护者做。）
- 建议：在 `fantasy/README.md:260` 的 CSM/点光阴影后加「（库级组件，当前未接线）」一句。

### G2. 历史台账 `fantasy/fantasy_bugs.md` 的「剩余未修」自述与其中仍标 ⚠️ 的阴影条目互相矛盾（#43 的根因已由 #48–#51 定位并修掉）
- 位置：`fantasy/fantasy_bugs.md:99`（「**当前剩余未修：仅 #21、#22**」）、`:9`（同义自述）对照 `:140`（#43 仍标 ⚠️ **[高] 方向光阴影在现代管线内不可见**）；后续定位与修法在同文件 `:182-187`（#48 采样 v 轴镜像修复 ＋ 当时「修复后可视化实测阴影图案清晰可见」）与 `:195-204`（#50 GL 深度专用离屏三个缺口修复 ＋ 双后端快照差分）
- 现状：`:140` 的 #43 是 2026-08-25 之前的中间结论，之后 #48–#51 找到并修掉了根因（代码层面修法都在位，父级 `fantasy/DEBUG.md` 的「#43 的复核证据」一节逐条核过），但 `:140` 那一行没有回填状态；`:99` 的「仅 #21/#22」显然也没算上 #43。
- 证据：同文件三处行号自相矛盾（一条说只剩 2 项、一条还挂着 ⚠️ 高优先级阴影缺陷）。
- 影响：照 `:140` 去查「阴影不可见」会白查（根因已修）；照 `:149`/`:187` 里提到的 `XHQM_SHADOW=1`、`XHQM_NOSHADOW` 开关去复现也会扑空 —— 这两个开关在当前工作副本里 0 命中（`grep -rn "XHQM_NOSHADOW\|XHQM_SHADOW=" --include=*.cpp --include=*.h .` 无结果，只在该 .md 自身的描述里出现），它们当时就是「本地 demo 改动、不入库、事后已清理」。注意「运行期阴影是否真的可见」本轮**未复现**（无图形环境），与父级的复核结论一致。
- 复现/验证：`grep -n "剩余未修\|方向光阴影在现代管线内不可见" fantasy/fantasy_bugs.md`；`grep -rn "XHQM_SHADOW=\|XHQM_NOSHADOW" --include=*.cpp --include=*.h .`。
- 建议：把 #43 回填为「根因已由 #48–#51 修复（运行期待复现）」，并给 `:99` 的剩余项计数加上 #43 的归属说明。（历史台账不改名不搬家，由 fantasy 维护者在原位订正。）

### G3. 阴影图尺寸可配、点光阴影 far 可传，但采样侧的 texel 步长与归一化常数都写死在 shader 里，文档从未写明这层约束
- 位置：可配的一侧 —— `fantasy/theory_render.h:30`（`m_size = 2048`）、`:39`（`void size(uint v)`，`test/ffbo/fly_mcns_view.cpp:816`/`:1032` 的 `--shadow-size` 就是走它）；写死的一侧 —— `fantasy/backend/shader_preprocess.cpp:64`、`:170`（`const vec2 texel = vec2(1.0 / 2048.0);`）与 `:91-98`（`dist / 10.0`）
- 现状：PCF 的 3×3 采样步长是固定 `1/2048`（UV），与 `theory_shadow::size()` 的实际取值无关；点光 far 固定 10，与 `point_shadow_map::compute()` 收到的 `far` 无关。README / 父级 README 都没有提这两条约束。
- 证据：`grep -n "2048" fantasy/backend/shader_preprocess.cpp` → 只有 `:64`、`:170` 两处硬编码（没有任何 `XHQM_SHADOW_SIZE` 一类的宏替换，对比同文件 `:249-254` 的 `XHQM_MAX_LIGHTS`/`XHQM_SHADOW_UV_FLIP` 是走宏的）。
- 影响：`--shadow-size 4096` 不会报错，但 PCF 步长变成 0.5 纹素（3×3 核退化成几乎无模糊），`--shadow-size 1024` 则变成 2 纹素（跳采邻纹素）——即「改了尺寸，阴影质量悄悄变成另一个算法」；点光 far 同理（见 A4）。谁会被坑：按 `--shadow-size` 做质量/性能权衡的人。
- 复现/验证：`grep -n "2048\|暂按 10" fantasy/backend/shader_preprocess.cpp`；`grep -n "shadow_size" test/ffbo/fly_mcns_view.cpp`。
- 建议：把尺寸与 far 通过宏/常量从模块下发到 shader（与 `XHQM_MAX_LIGHTS` 同一套路），或至少在 README 的注意事项里写明这两条约束。
- 与父级的分工：texel 写死这一半已在 `fantasy/DEBUG.md` **A1** 登记；本文件这一条保留的是**文档侧缺口**（README/父级 README 都没写「改 size 会改变 PCF 有效半径」）与 `theory_shadow::size()` 这个可配入口的证据行。
