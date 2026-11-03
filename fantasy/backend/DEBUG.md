# fantasy/backend —— 当前仍存在的问题

`fantasy/backend`（双后端 RHI：`render_backend` 虚接口 + GL/VK 两套实现 + 统一着色器源 / UBO 单一事实源 / `XHQM_*` 宏预处理 / glslang → SPIR-V）在 r677 工作副本下**仍然存在**的缺陷、缺口与取舍台账。用法与 API 见同目录 `README.md`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。

> **与同类文件的关系**：本仓的台账**不搬家、不改名**。`fantasy/fantasy_bugs.md` 是静态分析 + 历轮修复记录（编号 1–43，其中 #21/#22 标「待定」，与本文 F3、G3 是同一批现象，本文只做交叉引用）；`network/nebula/DEFERRED.md` 是「讨论过但决定不做」；根 `DEBUG.md` 与 `xhqm/DEBUG.md`、`hardware/DEBUG.md` 等是各层自己的台账——根 `DEBUG.md` G1 已记 `rhi_vk.h:10` 的旧「占位实现」注释与 264 处 `vk*` 实测，本文不复制，只留指针（G2）。仓库根 `通读报告-2026-10-07.md`（`:52` 的模块表行）与根 `README.md:200-208`（本轮对 fantasy 模块旧表述的复核）是旧表述来源。同目录 `README.md` 里被推翻的旧文一律用 `~~旧文~~ **已作废（2026-10-07 复核 r677）**` 标注、不删历史。

> 核对环境：Ubuntu 22.04 / g++ 11.4 / 工作副本 r677。文中 `wc -l`、`grep`、`sed`、`awk` 的数字都是本节点实测；**本节点没有跑起渲染程序**（未构建 `test/view` 的 Qt 视口），因此凡需 GPU/窗口才能观察到的条目一律写「未复现」，只给静态证据与触发方法；纯静态可判的写「静态可判」。（渲染侧缺无显示/无窗口的回归入口是仓库级问题，根 `DEBUG.md` C3 已记，本文不重复。）

本模块规模（实测 `wc -l`）：16 个源文件合计 7210 行 —— `rhi_vk.cpp` 4078、`rhi_gl.cpp` 1151、`shader_sources.h` 468、`shader_preprocess.cpp` 299、`rhi_no.cpp` 217、`spirv.cpp` 203、`rhi_no.h` 190、`shader.cpp` 167、`ubo.h` 151、`backend.h` 120、`shader.h` 57、`spirv.h` 32、`shader_preprocess.h` 30、`rhi_vk_macos.mm` 17、`rhi_gl.h` 15、`rhi_vk.h` 15。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. 后端工厂返回函数内静态单例，`init()` 失败路径却对它 `delete`（未定义行为）
- 位置：`rhi_no.cpp:120-128`（`try_init_backend` 里的 `delete backend;`）、`rhi_vk.cpp:4072-4076`（`static vk_backend instance;`）、`rhi_gl.cpp:1145-1149`（`static gl_backend instance;`）
- 现状：两个工厂都返回**函数内静态对象**的地址；探测函数在 `init()` 失败时执行 `delete backend`，再返回 false 让回退链试下一个后端。
- 证据：`rhi_no.cpp:126` 的 `delete backend;` 与 `rhi_vk.cpp:4074` / `rhi_gl.cpp:1147` 的 `static … instance;`。触发失败的分支：VK 的 `vkCreateInstance failed` / `no Vulkan physical device` / `no graphics queue family` / `vkCreateDevice failed`（`rhi_vk.cpp:796-920`），GL 的 `glewInit` 失败（`rhi_gl.cpp:126-132`，例如机器上有 Vulkan 设备但当前线程没有 current 的 GL 上下文时）。
- 影响：任何一台「Vulkan 初始化会失败」的机器，在 `AUTO` 构建下走 `vk → gl` 回退时就会对静态存储执行 `operator delete` → glibc 报 `free(): invalid pointer` abort，或析构掉静态后端后留下悬垂指针（此后 `create_vk_backend()` 仍返回同一已析构地址）。即：**设计上要「失败就回退」，实现上失败即崩溃**。
- 复现/验证：未复现（需要 init 失败的环境，且崩点在探测阶段）。静态可判：`sed -n '120,128p' fantasy/backend/rhi_no.cpp` + `sed -n '4072,4076p' fantasy/backend/rhi_vk.cpp`。若要在本机验证，可用 `XHQM_VALIDATION=1` 之外的伪造路径：临时把 `vkCreateInstance` 的扩展名改错（不落仓、只在本地试）。
- 建议：工厂语义改成「`new` 一个后端」或让 `try_init_backend` 不 delete（失败时 `render_backend_retire(backend)` 后返回 false）。

### A2. `begin_frame` 的 acquire 失败会把 fence 停在未信号态 → 下一帧无限等（挂死）
- 位置：`rhi_vk.cpp:1311-1314`（先 wait 再 `vkResetFences`）、`:1326-1327`（acquire 失败直接 `return`）、`:1363-1367`（`end_frame` 因 `frame_active == false` 提前返回）
- 现状：顺序是「等 `fences[frame_index]` → **reset 它** → `vkAcquireNextImageKHR`」；acquire 返回 `VK_ERROR_OUT_OF_DATE_KHR` 等非 SUCCESS/非 SUBOPTIMAL 时只写 `error` 串返回，**没有 submit**，于是那根 fence 再也不会被 signal；本帧 `end_frame()` 直接返回，也不提交。
- 证据：`rhi_vk.cpp:1311` 的 `vkWaitForFences(..., UINT64_MAX)`、`:1314` 的 reset、`:1327` 的早退、`:1367` 的早退；全函数没有任何 `VK_ERROR_OUT_OF_DATE_KHR` / swapchain 重建分支（grep `OUT_OF_DATE` 在 `rhi_vk.cpp` 0 命中）。
- 影响：窗口被缩到 0×0、表面过期、显示器热插拔等触发出错的那一帧之后，**下一次 `begin_frame` 会一直等那根永不 signal 的 fence**（`UINT64_MAX`），进程挂死且不可自愈；同理 `vkQueuePresentKHR` 的错误也没有任何重建路径。
- 复现/验证：未复现（需 `VK_ERROR_OUT_OF_DATE_KHR`）。触发方法：`./build/RhiViewport --backend vulkan`（构建见 `test/README.md:205-211`）后把窗口最小化/把宽高拖到 0，再操作窗口一次。
- 建议：acquire 失败时重建 swapchain 并跳过本帧；reset fence 挪到 submit 之前（两步之间不留早退）。

### A3. `end_frame` 把帧序号推进写在 present 失败的 `if` 里：present 结果被吞、信号量/fence 轮转失效
- 位置：`rhi_vk.cpp:1392-1394`
- 现状：原文是 `VkResult pr = vkQueuePresentKHR(m_queue, &pi); if (pr != VK_SUCCESS) st->frame_index = (st->frame_index + 1) % st->fences.size();` —— **只有 present 失败才推进 `frame_index`**；成功路径 `pr` 从不使用、也不打印，没有任何错误提示。
- 证据：`grep -n "frame_index" fantasy/backend/rhi_vk.cpp` 共 9 处，推进只有 `:1394` 这一处，且被 `if (pr != VK_SUCCESS)` 包住；`image_avails[frame_index]`（`:1326`、`:1377`）、`fences[frame_index]`（`:1311/1314/1383`）都跟着恒为 0。
- 影响：正常运行时 `frame_index` 永远是 0 → `image_avails[0]` / `fences[0]` 被反复复用，与 `:197-201` 注释声明的「每帧在飞一对信号量（与 fence 同轮转）」设计**不符**（复用点恰好被 `begin_frame` 的 fence 等待覆盖，所以实测未观察到画面异常——但轮转不存在了）；present 返回 `SUBOPTIMAL` / `OUT_OF_DATE` / `SURFACE_LOST` 时静默继续，撕裂/黑帧没有任何日志可查。
- 复现/验证：未复现（需 present 出错）；静态可判：`sed -n '1392,1394p' fantasy/backend/rhi_vk.cpp`。
- 建议：把 `frame_index` 推进移出 if；`if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR) fprintf(stderr, …)`。

### A4. 点光 cube 阴影的 far 硬编码 `dist / 10.0`
- 位置：`shader_preprocess.cpp:90-98`（`k_lighting_full` 的点光分支）
- 现状：立方体阴影 PCF 的 4 次采样都写死 `texture(cubeShadow, vec4(l2f + 偏移, dist / 10.0 - bias))`；注释自陈「立方体存线性深度 distance/far」「**far 暂按 10**」「生产需距离写入深度着色器（`gl_FragDepth = dist/far`）配合 6 面渲染」。
- 证据：`grep -n "dist / 10.0" fantasy/backend/shader_preprocess.cpp` → `:95-98` 四行；同一文件 `k_toon_lighting_full`（`:142-226`）**没有**点光 cube 阴影分支 → PBR 与卡通两条光照路径的点光阴影行为不一致。**并且写入侧还没有接上（编译器会过、只是没写完）**：`grep -rc "gl_FragDepth" fantasy/backend/*.cpp` → 0（只在 `shader_preprocess.cpp:91` 的注释里出现过这个词），`shader_sources.h` 里也没有任何把距离写进深度图的着色器；CPU 侧的 far 是调用方传进来的参数（`fantasy/lighting/point_shadow.cpp:39` 的 `compute(const point& light_pos, value near, value far)`，自检程序 `test/view/main.cpp:429-437` 自己给 near/far），用普通投影矩阵渲染 6 面 → 图里存的是**非线性投影深度**，而采样侧按**线性 `dist/far`** 比较。
- 影响：点光源到片元距离超过 10 个世界单位时比较参考值 > 1，比较采样越界 → 阴影表现为全亮/闪变（不可预期）；更根本的是「图里存的深度」与「采样时假设的深度」不是同一个量纲，**即使把 far 改成正确值，点光 cube 阴影也算不对**——这个字面量藏在字符串常量里，等写入侧补齐时极易只改一边。当前上层还没有生产路径打开这条采样分支（`bind_texture(9, …)`/`cubeShadow` 只出现在后端与自检代码里），所以是「潜伏 + 半接线」。
- 复现/验证：未复现（需点光阴影场景；`test/view/main.cpp` 的 `[PSHADOW]` 自检只验证「6 面 VP 有限、纹理取得到」，不看阴影是否真的生效）。静态证据：`grep -n "dist / 10.0\|far 暂按 10" fantasy/backend/shader_preprocess.cpp`。
- 建议：先补距离深度写入侧的着色器（`gl_FragDepth = dist / far`），再把 far 提成宏（如 `XHQM_POINT_SHADOW_FAR`）与 CPU 侧共用同一个常量。

### A5. GL program binary 缓存只有「后端名 + 内容哈希」两类打标，没有 GPU/驱动级失效判据
- 位置：`shader.cpp:96-101`（写盘字段）、`:122-124`（读回字段）、`:147-152`（命中条件）、`backend.h:110-116`（`shader_binary` 只有 `format/blob/backend/key`）
- 现状：缓存文件 = magic `XHQMSHDR` + `version 1` + `backend` + `key` + `format` + `blob`；命中条件是 `binary.backend == backend_name(active_backend()) && binary.key == use_key`（key 是 vertex+fragment 的内容哈希 `shader_key()`，16 个十六进制字符）。
- 证据：`grep -n "GL_VENDOR\|GL_RENDERER\|GL_VERSION" fantasy/backend/*.cpp` → 0 命中（`rhi_vk.cpp:132/811` 的 `m_gpu_name` 写完从不使用，见 E3）；`rhi_gl.cpp:243` 直接把 `bin.format` 交给 `glProgramBinary`，从不查 `GL_PROGRAM_BINARY_FORMATS`。
- 影响：`glProgramBinary` 的规范只保证**同一实现、同一驱动版本**内可移植。换 GPU / 升驱动后同一个缓存文件仍被判为命中：加载失败还算好的（`rhi_gl.cpp:239-256` 返回 0 → `shader_cache::acquire` 回落编译，白花一次 IO），加载成功则行为未定义（错算/闪退）；反向也没有「驱动升级即失效」的机制。
- 复现/验证：未复现（端到端需要有缓存目录并调用 `shader_cache::acquire`，而仓内没有调用点，见 E1）。静态证据：`grep -n "GL_VENDOR\|GL_RENDERER\|GL_VERSION" fantasy/backend/*.cpp`、`sed -n '91,134p' fantasy/backend/shader.cpp`。
- 建议：把 vendor/renderer/driver 版本并入缓存键（或写进文件头并在命中时校验）。

### A6. `render_backend_select()` 只切 `active_ptr`：不 `init()`、不退役旧后端 → 显式选 VK 后所有调用静默 no-op
- 位置：`rhi_no.cpp:194-205`
- 现状：`render_backend_select(kind)` 把工厂返回的单例直接赋给 `active_ptr()`，既不调用 `init()`，也不释放/退役原来的后端；`init()` 只在 `render_backend_active()` **首次**创建后端时被调用（`:163-192`，一旦 `active_ptr()` 非空就再也不会 init）。
- 证据：`rhi_no.cpp:194-205` 全文无 `init`；仓内两个调用点都自己补了初始化并在注释里点明这个坑 —— `test/view/RhiViewport.cpp:129-136`（「后端由构造函数 render_backend_select 选中，但惰性 init 仅在 active_ptr 为空时触发，此处显式初始化」）与 `test/ffbo/RhiViewport.cpp:147-152`（同款）。
- 影响：按 README 字面语义「切到 VK 就行」的调用者会拿到 `m_device == VK_NULL_HANDLE` 的后端：`create_surface` 只回一个 `"Vulkan device not created"`、`draw*` 因 `swapchain_ready()` 为假直接 return → 整帧空白且没有致命错误；GL 路径同理（`m_init == false` 时不做任何 GL 状态设置）。
- 复现/验证：静态可判（上面的行号）；端到端可跑 `./build/RhiViewport --backend vulkan`（去掉那两行 `init` 即复现空白帧——但那是改测试代码，本节点未做）。
- 建议：`select()` 成功后立即 `init()`（失败回退 `none` 并把错误抛给调用者），或至少把「select 不 init」写进 `rhi_no.h` 的注释。

---

## B. 资源与并发

### B1. VK 内存池只增不减：删除任何资源都不把内存还给池
- 位置：`rhi_vk.cpp:139-172`（`pool_alloc` 只推进 `blk.used`，没有 free 路径）、`:174-178` + `:719`（`pool_destroy()` 只在析构时跑）、`:1644-1650` / `:1673-1679` / `:1701-1707` / `:2997-3003`（各 delete 都只销毁 buffer，注释「memory 由内存池统一释放」）
- 现状：host-visible 大块（每块 **64 MiB**，单次需求更大时按需放大，`:154`）子分配；块一旦分配，`used` 单调递增，任何删除都不回收。而 `create_buffer()` 的调用者遍及 `vertex_create` / `index_create` / `indirect_create` / `storage_create` / **每次 `texture_create*` 与 `texture_update` 的 staging**（`:1719`、`:1897`、`:1991`、`:2031`、`:2133`）/ `read_pixels` / `read_offscreen`。
- 证据：`grep -c "vkFreeMemory" fantasy/backend/rhi_vk.cpp` → 14，逐行看：`:176` 是唯一释放**池块**的地方（`pool_destroy`），其余全是离屏/立方体/表面/程序自己 `vkAllocateMemory` 的独占内存；池内内存没有第二处释放。
- 影响：长时间运行 / 流式加载（反复建删纹理、每次拾取读回一像素）会持续吃掉 host-visible 内存且不回落；一次 4×4 浮点纹素的 `texture_update` 也要从 64 MiB 的块里划一段（`create_buffer` 粒度是 `mr.alignment`，不是需求大小），浪费随调用次数线性增长。
- 复现/验证：未复现（需要长跑或循环建删）。可行的探针：循环 `texture_create_float(…,4,4,…)` + `texture_delete` 1000 次后打印 `m_mem_blocks` 的元素个数（需要临时加代码或调试器断点，本节点未做）。
- 建议：给池加 free list，或按「块内全部子分配已释放即 `vkFreeMemory`」回收。

### B2. VK 查询池的 slot 不回收：64 次 `query_create` / 64 次 `timestamp_query_create` 之后永久耗尽（GL 无此问题）
- 位置：`rhi_vk.cpp:225`（`QUERY_COUNT = 64`）、`:2755`（`if (m_query_next >= QUERY_COUNT) { error = "query pool exhausted"; return 0; }`）、`:2757`（`m_query_next++`）、`:2761-2764`（`query_delete` 只 `m_queries.erase`）、`:232`（`TIMESTAMP_COUNT = 64`）、`:2809`、`:2811`、`:2815-2818`
- 现状：两个池都是「发号只前进、删除不退还 index」；删除只是把 id→index 的映射擦掉。
- 证据：`query_create` / `timestamp_query_create` 里唯一的计数变量 `m_query_next` / `m_timestamp_next` 全文件只增；`query_delete` / `timestamp_query_delete` 没有任何 `m_query_next` 回退。
- 影响：① 遮挡剔除在 VK 下是**按对象**建查询（`fantasy/spatial/occlusion.cpp:149-155` 每个对象一次 `query_create`，`:81` 在 `reset()` 里 `query_delete`）：同一次会话里第 65 个查询对象起 `query_create` 返回 0，`occlusion.cpp:154` 直接 `continue` → 那些对象**不再做遮挡判定**（多画，表现为「VK 下剔除莫名失效」）；GL 侧 `rhi_gl.cpp:728-745` 用 GL query 对象、删除即回收，所以「同一场景 GL 正常、VK 掉剔除」。② `fantasy/runtime/performance.cpp:27-35` 的 `gpu_timer` 每次 `begin()` 建 2 个时间戳查询、析构删除，VK 下第 33 次 `begin()` 起 `m_ready` 为假、`end_ms()` 恒 0（该计时器当前在仓内无调用点，属潜伏项）。
- 复现/验证：未复现。触发方法：`./build/RhiViewport --backend vulkan` 载入 >64 个可遮挡对象的场景，看遮挡剔除的行为与 `[OK]/[FAIL]` 输出；或对同一 `occlusion_culler` 反复 `reset()`+重建 65 轮。
- 建议：两个池各维护一个 free index 栈，`*_delete` 时归还。

### B3. GL：`m_tex_meta` 只增不减，纹理名被 GL 复用后元数据污染 `texture_update`
- 位置：`rhi_gl.cpp:502`（`texture_delete` 只 `glDeleteTextures` + `m_cubemap_texs.erase`）、`:41`/`:434`/`:450`（`m_tex_meta` 写入）、`:454-463`（`texture_update` 依赖 `is_float/w/h`）
- 现状：`m_tex_meta[id]` 在 `texture_create` / `texture_create_float` 里写入，**没有任何 erase**；而 `texture_update` 用它判断「这是不是浮点纹理、尺寸多大」。
- 证据：`grep -n "m_tex_meta" fantasy/backend/rhi_gl.cpp` → 写入 2 处、读取 1 处，delete 路径 0 命中；GL 的纹理名（`GLuint`）会被驱动回收复用。
- 影响：长时间流式加载时 map 单调增长（每张纹理一条）；更实际的是**语义污染**——一个复用了旧名字的非浮点纹理会命中旧的 `is_float == true`，于是 `texture_update` 按 `GL_FLOAT` 写 `glTexSubImage2D`，得到错误内容或 `GL_INVALID_OPERATION`（并可能把 `w/h` 检查放到错误尺寸上）。
- 复现/验证：静态可判；仓内没有「删除后再 update 同名纹理」的序列，端到端未复现。
- 建议：`texture_delete` / `delete_offscreen` 里 `m_tex_meta.erase(id)`（离屏纹理同样进过这张表）。

### B4. GL：bindless 句柄只 `glMakeTextureHandleResidentARB`，没有任何解除路径
- 位置：`rhi_gl.cpp:504-516`（`texture_bindless_handle`）、`:502`（`texture_delete`）
- 现状：取句柄时若 `GLEW_ARB_bindless_texture` 可用就 `glGetTextureHandleARB` + `glMakeTextureHandleResidentARB`，句柄不保存、也不 `glMakeTextureHandleNonResidentARB`；`texture_delete` 只 `glDeleteTextures`。
- 证据：`grep -n "ResidentARB" fantasy/backend/rhi_gl.cpp` → 只有 `:511` 一处（make）；调用点 `test/view/main.cpp:627` 拿到句柄后下一行（`:632`）就 `texture_delete` 同一张纹理。
- 影响：句柄驻留会占用驱动侧表项/资源，反复创建-删除贴图会单调累积；删除后句柄仍 resident，若有 shader 还在用它就是悬垂句柄（未定义）。
- 复现/验证：未复现（需 `ARB_bindless_texture` 环境 + 反复建删贴图）。
- 建议：把发出的句柄记在纹理条目里，`texture_delete` 时 `glMakeTextureHandleNonResidentARB`。

### B5. VK：离屏/立方体注册进 `m_textures` 的代理条目与其中的 sampler 永不回收
- 位置：`rhi_vk.cpp:3386-3409`（`offscreen_texture` 惰性注册 `texture_t`，`owns_image = false`，**sampler 归该条目持有**）、`:3295-3310`（`delete_offscreen` 只销毁离屏自己的 image/view/render_pass，不碰 `m_textures`）、`:3764-3787`（立方体同款惰性注册）、`:2240`（`m_textures.erase` 全文件唯一一处，只在 `texture_delete`）
- 现状：每次 `offscreen_texture(id, attachment)` 首次调用都会新建一个 `VkSampler` 并把 `tex_id` 注册进 `m_textures`；`delete_offscreen()` 不删除这些条目，于是条目里的 sampler 泄漏、`m_textures` 单调增长，且这些 id 仍会命中 `bind_texture` 的查找（指向已销毁的 image view）。
- 证据：`grep -n "m_textures.erase" fantasy/backend/rhi_vk.cpp` → 只有 `:2240`；`delete_offscreen`（注册点在 `:3391-3407`）里没有对应的清理；`destroy_textures`（`:661-677`）因 `owns_image == false` 跳过 view/image，但 sampler 仍由该条目持有（`:3403`）。
- 影响：反复重建离屏目标（窗口 resize 会重建 HDR/拾取目标，见 `fantasy/theory_render.cpp:227`）→ `m_textures` 与 `VkSampler` 数量单调增长；上层若缓存了旧的离屏 texture id 并再次绑定，就会采样已销毁的 image view（校验层报错/驱动崩溃）。
- 复现/验证：未复现。触发方法：`./build/RhiViewport --backend vulkan` 反复缩放窗口数百次，观察进程内存与校验层输出（`XHQM_VALIDATION=1`）。
- 建议：`delete_offscreen` / `delete_offscreen_cubemap*` 里回收对应 texture id 条目（含 sampler）。

---

## C. 跨平台

### C1. VK 交换链颜色格式硬编码 `B8G8R8A8_UNORM`，从不查询表面支持格式
- 位置：`rhi_vk.cpp:214`（`VkFormat m_color_format = VK_FORMAT_B8G8R8A8_UNORM;`）、`:1009-1029`（swapchain 直接用）、`:1047`、`:1120`（image view / MSAA 附件同格式）
- 现状：只查了 `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`（`:1003`），没有查 `vkGetPhysicalDeviceSurfaceFormatsKHR`/`vkGetPhysicalDeviceSurfacePresentModesKHR`；色彩空间固定 `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`，present mode 固定 `FIFO`。
- 证据：`grep -c "vkGetPhysicalDeviceSurfaceFormatsKHR\|vkGetPhysicalDeviceSurfacePresentModesKHR" fantasy/backend/rhi_vk.cpp` → 0；`:1021` 用了 `caps.currentTransform`，格式没用 cap 里的任何信息。
- 影响：表面不支持该格式（部分 ARM/Android/远端/软渲驱动只报 `R8G8B8A8_UNORM` 或 10 bit 格式）时 `vkCreateSwapchainKHR` 直接失败 → `create_surface` 失败 → 该平台上 VK 后端**完全不可用**（回退链上只剩 GL/none）。FIFO 固定则意味着无法用 mailbox/immediate 降延迟。
- 复现/验证：未复现（本机 NVIDIA 支持 B8G8R8A8）。可在目标平台用 `vulkaninfo | grep -A4 "Surface Formats"` 查证。
- 建议：按 `vkGetPhysicalDeviceSurfaceFormatsKHR` 挑首选格式（含 sRGB 变体），present mode 同理。

### C2. GL core profile 下 `line_size()` 实际无效（`glLineWidth` 只能 1.0）
- 位置：`rhi_gl.cpp:682`（`virtual void line_size(value s) override { glLineWidth(s); }`）；对照 `rhi_vk.cpp:839-845`（按物理设备能力请求 `wideLines`）+ `:4044`（`VK_DYNAMIC_STATE_LINE_WIDTH`）+ `:2581/2630`（每 draw 下发）
- 现状：GL 侧只有一句 `glLineWidth`；仓内的 GL 窗口是 core profile 3.3（`test/view/RhiViewport.cpp:36-42` 显式设 `QSurfaceFormat::CoreProfile`）。
- 证据：`grep -n "glLineWidth\|wideLines\|LINE_WIDTH" fantasy/backend/rhi_gl.cpp fantasy/backend/rhi_vk.cpp`；core profile 下 `GL_ALIASED_LINE_WIDTH_RANGE` 只有 `[1,1]`，`glLineWidth(2)` 会 `GL_INVALID_VALUE` 并被忽略。
- 影响：线元素（`point_line_ploy` / `curve` / `nurbs` 都用 `GL_LINES`）的线宽在两个后端**不一致**：VK 上生效（设备支持时），GL core 上恒为 1 像素——上层调 `line_size()` 在 GL 下等于空操作，跨后端画面对不上。
- 复现/验证：未复现（需 GL core 上下文 + 读回像素）；静态可判 + 查 OpenGL 3.3 core 规范。
- 建议：GL 侧用四边形/几何着色实现宽线，或在 README/头注释里明确「线宽只在 VK 生效」。

### C3. `set_msaa()` 在两个后端语义不同（GL 只是开关，VK 才是真实采样数）
- 位置：`rhi_gl.cpp:549-553`（`samples > 1` → `glEnable(GL_MULTISAMPLE)`）、`rhi_vk.cpp:3814-3829`（clamp 到 1/2/4/8 并记录，`create_surface` 时按它建多采样附件）
- 现状：VK 侧 `m_msaa` 参与 image/render pass/pipeline 的创建（多采样附件 + resolve，`rhi_vk.cpp:1090-1204`），且必须在 `create_surface` **之前**调用才有效；GL 侧只翻 `GL_MULTISAMPLE` 状态，真正的样本数由窗口/上下文的像素格式决定。**另外这两条路径仓内都从没被用过**（属「能编译、没人接上」的一类）：`grep -rn "set_msaa" --include=*.cpp --include=*.h test/ fantasy/` → 只有接口声明与两份实现，**0 个调用点**；测试视口的窗口格式也没设 `setSamples`（`test/view/RhiViewport.cpp:36-42` 只设 version/profile/alpha/depth/stencil）。
- 证据：两处实现原文；`rhi_no.h:100` 的接口注释已经写了「需在 create_surface 前调用」，但没写 GL 侧不成立、也没写无人调用。
- 影响：同一个 `set_msaa(4)` 在 GL 下不保证 4×，在 VK 下是 4×；跨后端的 MSAA 质量/性能不可比，调 GL 侧的采样数只能改窗口创建参数（后端改不了）。「1=关闭」在 GL 下是 `glDisable(GL_MULTISAMPLE)`，对已按多采样像素格式建的上下文也不一定真的关掉。VK 的多采样代码路径至今没有仓库内的验证手段（`test/view/main.cpp` 的 21 个 `[XXX]` 标签——含 `[OK]`/`[FAIL]` 两个状态标记——里没有 MSAA 项）。
- 复现/验证：未复现（需两个后端各跑一次 + 看边缘锯齿/查询 `GL_SAMPLES`）；静态证据：`grep -rn "set_msaa" test/ fantasy/`。
- 建议：在 `rhi_no.h` 的 `set_msaa` 注释里写清两后端差异，GL 侧的 `samples` 值由调用方窗口负责。

---

## D. 性能

### D1. GL 每次 uniform 改动都整块上传 38240 B（VK 已优化成 64 B 块增量，GL 没做）
- 位置：`rhi_gl.cpp:106-117`（`bind_ubo()`：`dirty` 就 `glBufferSubData(GL_UNIFORM_BUFFER, 0, UBO_SIZE, …)`）、`:562-679`（任一 `uniform_*` 命中 UBO 就 `dirty = true`）；对照 `rhi_vk.cpp:69-78` 的注释（记录同源问题：整块 38240 B 从 write-combined 映射内存拷贝，实测 0.156 ms CPU/draw）与 `:2435-2467`（`ubo_write` 只标记 64 B 块、`emit_ubo` 只搬写过的区间）
- 现状：GL 侧的上传粒度是整个 `UBO_SIZE`（38240 B，其中 32768 B 是 `bones[512]`、4096 B 是 `instances[64]`，不写骨骼/实例时全是死区）；而引擎是「逐对象设 uniform 再 draw」，所以每个 draw 都会把 dirty 置位、下一 draw 前整块上传。
- 证据：`rhi_gl.cpp:113` 的 `glBufferSubData(..., 0, UBO_SIZE, it->second.data.data())`（无区间参数）；`rhi_vk.cpp:76-78` 已经算出 `UBO_BLK = 64`、`UBO_BLKS = 598`、`UBO_USED_WORDS = 10` 并只用「写过」的块。
- 影响：主 pass ~190 draw/帧的 MMD 场景（`rhi_vk.cpp:66` 的实测注释）在 GL 下每帧要额外搬 ≈190×38 KB ≈ 7 MB（≈116000 条 cache line）的 CPU/驱动拷贝，而 VK 路径只搬 ≈190×1.4 KB；GL 后端在逐对象 uniform 的负载下 CPU 时间被这一条吃满。
- 复现/验证：未复现（需 GL 环境 + 帧计时）；可对照 `test/view` 的 `--watch` 面板（`test/README.md:140-145` 的开关表）。
- 建议：把 VK 那套「64 B 块位图」搬到 GL，只 `glBufferSubData` 变化区间。

### D2. VK 管线缓存把 `render_pass` 散列入键：重建离屏/立方体就新增一批管线，旧管线不回收
- 位置：`rhi_vk.cpp:255-272`（`pipeline_key` 里并入 `active_render_pass()` 的散列）、`:306`（`std::unordered_map<size_t, VkPipeline> pipelines`）、`:2553-2557`（miss 就 `create_pipeline`）、`:630-631` / `:1563`（只有 `destroy_programs` / `shader_delete` 才销毁管线）
- 现状：键 = 顶点布局哈希 + 拓扑 + 渲染状态位段 + **render pass 指针的散列**；离屏目标与立方体目标各自 `vkCreateRenderPass`（`create_offscreen` / `create_offscreen_cubemap*`），每次重建都产生新的 render pass → 同一 program 下每个新 pass 都会新建一份管线，旧的留在 map 里直到 program 被删。
- 证据：`rhi_vk.cpp:257-271`（`rp_hash` 参与键）、`:306`（map 无淘汰）、`:630-631`（唯一批量销毁点）。
- 影响：反复 resize（`fantasy/theory_render.cpp:227` 会重建 HDR 目标）或反复重建拾取/阴影目标时，管线对象与驱动侧编译产物单调增长（显存 + CPU），且每次重建后第一帧要现场编译新管线 → 卡顿尖刺。
- 复现/验证：未复现。触发方法：`./build/RhiViewport --backend vulkan` 反复拖拽缩放窗口，观察内存曲线与首帧卡顿；或用 `XHQM_VALIDATION=1` + 校验层统计管线数。
- 建议：管线缓存键里用 render pass 的**等价类**（比如「目标布局类型 + 附件格式数量」）而不是指针，或给离屏目标复用一份 render pass。

### D3. VK 的读回路径整设备同步 + 每次新建 staging（拾取每帧一次就掉帧）
- 位置：`rhi_vk.cpp:3436-3491`（`read_offscreen`：`vkDeviceWaitIdle` + 新建 staging + `one_shot` 提交并等待）、`:3847-3908`（`read_pixels` 同款）、`:3016-3027`（`storage_read` 也 `vkDeviceWaitIdle`）
- 现状：每次读回都 `vkDeviceWaitIdle`（排空整条队列）→ `one_shot`（新建命令池 + 提交 + `vkQueueWaitIdle` + 销毁池）→ 映射读回，且 staging buffer 从内存池新划（见 B1）。
- 证据：上述行号；`one_shot` 的定义在 `:492-523`（每次建/销命令池 + `vkQueueWaitIdle`）。
- 影响：GPU 驱动的拾取（点选、GPU 剔除回读）每帧一次就会把 CPU/GPU 流水线整段打断（帧率断崖），并同时把池内存往前推；GL 侧的 `read_offscreen` 只是 `glReadPixels`（`rhi_gl.cpp:1024-1034`），两后端在此行为差异极大。
- 复现/验证：未复现。触发方法：`./build/RhiViewport --backend vulkan` 开启每帧拾取（鼠标划过视口）对比帧时间。
- 建议：读回改成「跨帧延迟 + 环形读回缓冲」，避免每帧 `vkDeviceWaitIdle`。

---

## E. 未实现 / 占位桩

> 本组都是「**能编译、能跑过初始化，但没接上或用不到**」的东西，标注在条目标题里。

### E1. 着色器预编译缓存整条链在仓内没有任何调用点（已实现、未接线）
- 位置：`shader.h:14-53`（`shader_hash` / `shader_key` / `shader_factory` / `default_shader_factory` / `shader_cache`）、`shader.cpp:43-166`（全部实现）
- 现状：五个符号都有完整实现（含落盘格式、命中校验、失败回落编译），但生产路径没有一处调用 `shader_cache::acquire`。
- 证据：`grep -rn "shader_cache\|default_shader_factory\|shader_factory\|shader_key\|shader_hash" --include=*.cpp --include=*.h --include=*.mm . | grep -v "^./fantasy/backend/"` → 0 命中；只有 `shader_get_binary` 被 `fantasy/drawer.cpp:189` 用（取二进制，但不落盘）。
- 影响：README/上层以为「首次编译落盘、之后直接加载」在生效，实际每次启动都全量重编译 GLSL（VK 下还要跑一遍 glslang），启动耗时与缓存目录策略都没人定；A5 的失效打标问题也因为没人用而暂时“看不见”。
- 复现/验证：`grep -rn "shader_cache" --include=*.cpp --include=*.h --include=*.mm . | grep -v "^./fantasy/backend/"` → 0 行（实测）。静态可判。
- 建议：把 `prefab::shader()` / `element.cpp` 的建 shader 路径改走 `shader_cache::acquire`，或把 README 的「缓存」明确写成「已实现未接线」。

### E2. VK 后端 `supports_binary()` 恒 `false`，而 SPIR-V 打包/解包已经写完了（半接线）
- 位置：`rhi_vk.cpp:739`（`virtual bool supports_binary() const noexcept override { return false; }`）、`:1530-1555`（`shader_create(const shader_binary&)` 解包 `[vs_size][vs_spv][fs_size][fs_spv]`）、`:1576-1594`（`shader_get_binary` 打包，`out.format = 1`、`out.backend = "vulkan"`）
- 现状：VK 侧的二进制**读写两侧都实现了**，但能力位是关的；而 `default_shader_factory::supports_binary()`（`shader.cpp:85-89`）只认 `opengl`，所以 `shader_cache::acquire` 在 VK 下永不落盘。
- 证据：上述三处原文；`:1576-1594` 的打包逻辑与 `:1530-1555` 的校验（`vs_size == 0 || fs_size == 0 || remain < vs_size + fs_size`）都完整。
- 影响：VK 后端每次启动都重新编译 GLSL→SPIR-V（glslang 全量走一遍），预编译缓存在 VK 上是死代码；两处能力位（后端 `supports_binary` 与工厂 `supports_binary`）口径也不一致。
- 复现/验证：静态可判；`grep -n "supports_binary" fantasy/backend/*.cpp fantasy/backend/*.h`。
- 建议：要么打开（`return true` 并接线 `shader_cache`），要么在 `rhi_vk.h`/README 里标注「VK 二进制缓存未启用（SPIR-V 打包已备）」。

### E3. 死字段：`program_t::ubo_dirty` 声明后从不读写、`m_gpu_name` 只写不读
- 位置：`rhi_vk.cpp:311`（`bool ubo_dirty = false;`，注释写「已废弃：只被赋值、从未被读取」）、`:132` + `:811`（`m_gpu_name = props.deviceName;`）
- 现状：`ubo_dirty` 全文件除了声明只有 0 处引用（**连赋值都没有**，注释本身也不准）；`m_gpu_name` 赋值后没有任何读取点（不打日志、不进缓存键、不上报）。
- 证据：`grep -n "ubo_dirty" fantasy/backend/rhi_vk.cpp` → 仅 `:311`；`grep -n "m_gpu_name" fantasy/backend/rhi_vk.cpp` → `:132`、`:811`。
- 影响：读代码的人被这两处误导（注释说有赋值、实际没有）；`m_gpu_name` 不读意味着后端连「跑在哪块卡上」都不打印，A5/C1 那类「选中了哪块设备/哪种格式」的问题在现场没有线索。
- 复现/验证：静态可判（上面两条 grep）。
- 建议：删 `ubo_dirty`；把 `m_gpu_name` 打进启动日志并（配合 A5）写进缓存打标。

### E4. VK compute 描述符集声明了 binding 0 的 UBO，但没有任何写入路径（当前无 shader 用到）
- 位置：`rhi_vk.cpp:2849-2860`（`bindings[0] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`）、`:2871-2875`（池里给它留了 1 个 descriptor）、`:3029-3049`（`compute_storage_bind` 只处理 binding 1..4）
- 现状：compute 描述符集布局是「binding 0 = UBO + binding 1..4 = SSBO」，但全后端没有任何向 compute 的 binding 0 写 `VkDescriptorBufferInfo` 的代码（`vkUpdateDescriptorSets` 在 compute 路径只出现在 `:3048` 且 `dstBinding` 限 1..4）。
- 证据：`grep -n "DstBinding\|dstBinding" fantasy/backend/rhi_vk.cpp` 里 compute 相关的只有 `:3043`；引擎现有 compute shader 只用 SSBO（`fantasy/animation/gpu_skinning.cpp:21/34/35` 与 `fantasy/spatial/gpu_culler.cpp:32/46/56` 全是 `layout(std430, binding = 1..3)`，无 `binding = 0` 的 UBO）。
- 影响：目前**无实际缺陷**（没人用 binding 0）；一旦有新 compute shader 声明 `layout(std140, binding = 0) uniform …`，就会用到「从未更新」的描述符（VUID-vkCmdDispatch-None-08114），表现为读到全 0/未定义值，且报错只在校验层可见。
- 复现/验证：静态可判（上面两条 grep）。标注：**编译器会过、只是没接上**。
- 建议：要么删掉 binding 0 的 UBO 声明，要么补一条 compute UBO 上传/绑定接口。

### E5. 若干 RHI 接口在仓内没有任何调用点（已实现、没人用）
- 位置：`rhi_no.h` 的接口声明 + 两后端实现；无调用点的有 `indirect_create`（`rhi_no.h:143`）、`indirect_delete`（`:144`）、`draw_indexed_indirect`（`:145`）、`draw_instanced`（`:139`）、`create_offscreen_cubemap`（`:116`，M10 IBL 的立方体**颜色**离屏）
- 现状：这些方法在 `fantasy/backend` 之外的仓内 0 命中；对照有调用点的近亲：`draw_indexed_indirect_storage` 2 处（`fantasy/spatial/gpu_culler.cpp:157`、`fantasy/spatial/gpu_hiz_culler.cpp:219`）、`draw_indexed_instanced` 6 处、`create_offscreen_cubemap_depth` 1 处（`fantasy/lighting/point_shadow.cpp:17`）。`set_msaa` 也是 0 调用点（已在 C3 记，那边讲的是两后端语义差异）。
- 证据：实测（在仓库根跑）`for m in indirect_create indirect_delete draw_indexed_indirect draw_instanced create_offscreen_cubemap; do grep -rn "\.$m(\|->$m(" --include=*.cpp --include=*.h --include=*.mm . | grep -v "^./build" | grep -v "^./fantasy/backend/" | wc -l; done` → `0 0 0 0 0`。
- 影响：这些路径没有任何仓库内代码走过，等于**零回归覆盖**：indirect 系列（`rhi_no.h:142-147` 的 M3/M7 设计）已被 SSBO 版本取代；IBL 的彩色立方体离屏与 `samplerCube irradiance/prefiltered` 采样端（`shader_preprocess.cpp:292-295`）也都还没有生产调用方。谁去动这两条路径，不会有测试兜底。
- 复现/验证：上面那行 shell（实测输出 `0 0 0 0 0`）。标注：**编译器会过、只是没人用**。
- 建议：要么删掉死接口收敛 RHI 面积，要么在 README 里标注「未接生产路径」。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. `vk_backend` 是单类单体（4078 行文件里一个类占 3947 行）
- 位置：`rhi_vk.cpp:124-4070`（类体，含 87 个虚函数的实现 + 内存池/表面表/程序表/查询池/立方体表）
- 现状：设备、内存池、多窗口表面、程序/管线/描述符、纹理、离屏、立方体、查询、时间戳、compute 全部是同一个类的成员，没有拆子对象或 pimpl。
- 理由（为什么现在这样是对的）：所有 Vulkan 资源都在同一个 TU 里创建/销毁，生命周期顺序（surface → framebuffer → render pass → image → memory pool → device）能在一个文件里核对完；`fantasy_bugs.md` #14–#19 那一轮修的就是顺序/键位类问题，拆开反而更难对全局。代价：单文件改动面大、编译慢、无法单测子系统（测试只能整块链接，见 `test/CMakeLists.txt:273-305` 只敢链 `rhi_no/shader/shader_preprocess`）。
- 复现/验证：`wc -l fantasy/backend/rhi_vk.cpp` → 4078；`grep -c "virtual.*= 0;" fantasy/backend/rhi_no.h` → 87。

### F2. UBO 环形槽 + 逐 draw 固化（1024 槽、1023 环形、slot0 保留）
- 位置：`rhi_vk.cpp:60-78`（`UBO_SLOTS`/`UBO_BLK`/`UBO_BLKS` 注释）、`:2564-2571`（每 draw 分配 slot 并 `emit_ubo`）、`:560-579`（描述符集回卷）
- 现状：每个 draw 独占一个 UBO slot 并把「写过的 64 B 块」搬进去，代价是每 draw 一次 ≈1.4 KB 的 CPU 拷贝；单帧同 program 超过 511 次 draw 时环形过冲，退回 `vkDeviceWaitIdle` + 回卷到帧起点（`:568`）。
- 理由：不这么做就是 `fantasy_bugs.md` #52/#53 那两个已修的闪烁根因（后画覆盖先画、跨帧覆写在飞槽）；用 CPU 侧暂存 + 位图把「整块 38 KB」压到「实际写过的十几块」，是当前性价比最高的方案。代价：单帧同 program 的 draw 数有硬上限（511），超了就掉帧尖刺；UBO 内存占用 = `UBO_SIZE × 1024`（每 program 一份，四舍五入到 `minUniformBufferOffsetAlignment`）。
- 复现/验证：`grep -n "UBO_SLOTS\|UBO_BLK =\|UBO_USED_WORDS" fantasy/backend/rhi_vk.cpp`。

### F3. `ubo_offset()` 让 `"tranMatrix"` 与 `"params"` 共用偏移 0（别名）
- 位置：`ubo.h:75`（`if (n == "tranMatrix" || n == "params") return (int)UBO_TRANSFORM;`）
- 现状：两个名字映射同一字节偏移；postfx 的 `{vec4 params}` 恰好落在 0 偏移，靠「没有哪个 shader 同时用这两个名字」工作。
- 理由：postfx 的小 UBO 与 mesh 的 `tranMatrix` 各自独立使用，不共享 program；改布局要动 postfx 的绑定与 UBO 偏移表，收益为零。代价：名字→偏移表里出现歧义别名，新代码若在同一 shader 里同时用 `params` 与 `tranMatrix` 会静默互相覆盖（`fantasy/fantasy_bugs.md` #22 记的就是这一条，标「待定」）。
- 复现/验证：`grep -n '"params"' fantasy/backend/ubo.h` → `:75`。

### F4. Apple GL 上的极简光照开关（`apple_gl = true`）
- 位置：`shader_preprocess.cpp:22-35`（`k_lighting_simple`）、`:131-141`（`k_toon_lighting_simple`）、`:229-230`（替换点）；调用方 `fantasy/drawer.cpp:420-424` / `:442-445`（`#ifdef __APPLE__` 且非 Vulkan）
- 现状：macOS 的 OpenGL 后端上，PBR/toon 片元只剩「第一盏方向光的兰伯特项」（简单版）/「ramp 漫反射」（toon 版）：没有阴影、没有点光/聚光衰减、没有高光、没有边缘光。
- 理由：注释写明 Apple GL 驱动上「UBO 灯光数组的结构体拷贝与动态索引会杀死片元」（`shader_preprocess.cpp:24-25`、`:129-130`），也就是说完整循环在 macOS GL 上根本出不了画面；这个开关让 macOS GL 至少能渲染。代价：macOS 用户吃不到引擎绝大部分光照特性，且这是**跨后端能力矩阵上最大的缺口**——同一份场景在 macOS GL 与 Windows/VK 上观感差异巨大；`apple_gl` 只对 `prefab`/`toon` 两个 shader 生效（其它 shader 走默认 false）。
- 复现/验证：`grep -n "apple_gl" fantasy/drawer.cpp fantasy/backend/shader_preprocess.cpp`；`test/toon_test.cpp:67-73` 有一个「AppleGL 预处理产物」用例（只验证产物含 ramp、无残留宏，不验证画面）。

### F5. VK 析构默认**不**销毁 `VkDevice` / `VkInstance`（`XHQM_FULL_DEVICE_DESTROY` 开关）
- 位置：`rhi_vk.cpp:720-730`
- 现状：`~vk_backend()` 释放完自己的 resource 后，若环境变量 `XHQM_FULL_DEVICE_DESTROY` 未置位就把 `m_device`/`m_instance` 置空、**跳过** `vkDestroyDevice`/`vkDestroyInstance`；置 `=1` 才走完整销毁。
- 理由：注释记录 NVIDIA 驱动（32.0.15.9186）在应用完整销毁路径后于 `vkDestroyDevice` 内部崩溃（EIP=0），应用侧销毁顺序已逐项核对无双重销毁/悬空，属驱动缺陷；跳过销毁让进程能正常退出。代价：进程退出不释放设备资源（由 OS 回收），DLL 卸载/重载会累积；校验层与 ASAN 也看不到完整销毁路径，A1/B5 这类顺序问题更难在退出时暴露。
- 复现/验证：`setenv XHQM_FULL_DEVICE_DESTROY=1` 后跑 `./build/RhiViewport --backend vulkan` 对比是否崩溃（NVIDIA 环境；本节点未跑）。

### F6. 深度/深度模板格式按设备能力选，而不是硬编码
- 位置：`rhi_vk.cpp:274-290`（`pick_depth_format`）、`:817-818`（init 时确定 `m_depth_stencil_format`/`m_depth_format`）、`:1059`/`:1138`/`:3099`（使用点）
- 现状：按 `D32_SFLOAT_S8_UINT → D24_UNORM_S8_UINT → D16_UNORM_S8_UINT`（带 stencil）/ `D32_SFLOAT → X8_D24_UNORM_PACK32 → D16_UNORM`（不带）逐个查 `VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT`，取第一个支持项，都不支持才回退 D32。
- 理由：MoltenVK 不支持 `D24_UNORM_S8_UINT`/`D16_UNORM_S8_UINT`，硬编码会让 render pass / image view / image 创建全部报校验错误（注释就写在 `:815-816`）。代价：`m_depth_format` 的实际值随设备变，任何依赖「深度格式是 D32_SFLOAT」的代码都必须改成读成员（现状 `create_default_depth_texture` 已经这么做），跨机器复现问题时需要先打印这个值。
- 复现/验证：`grep -n "pick_depth_format\|m_depth_format\|m_depth_stencil_format" fantasy/backend/rhi_vk.cpp`。

### F7. VK 侧没有 bindless：`texture_bindless_handle()` 返回纹理 id 当索引代理
- 位置：`rhi_vk.cpp:2243-2247`（`return (std::uint64_t)id;`）；`rhi_no.h:90-91` 的注释也写明「Vulkan 返回纹理 id（descriptor 索引代理）」；GL 侧是真句柄（`rhi_gl.cpp:504-516`）
- 现状：两个后端返回的「句柄」语义完全不同：GL 是 64 位真实纹理句柄，VK 是纹理 id。
- 理由：Vulkan 上要真 bindless 需要 descriptor indexing（`VK_EXT_descriptor_indexing` + 非常量描述符索引），引擎当前没有这套基础设施；返回 id 让上层接口统一。代价：任何把该值当「GPU 句柄」存进 SSBO/UBO 再在着色器里解引用的用法在 VK 下完全失效（`test/view/main.cpp:629-630` 的自检就显式区分了两种语义）。这是接口层的语义取舍，不是缺陷。
- 复现/验证：`grep -n "texture_bindless_handle" fantasy/backend/*.cpp test/view/main.cpp`。

---

## G. 文档与代码的出入

### G1. 本轮已订正的 README 旧说法（**索引**，不是「仍存在的问题」）
- 位置：`fantasy/backend/README.md`（本文件同目录）
- 现状：四处旧文已按本仓约定用 `~~旧文~~ **已作废（2026-10-07 复核 r677）**` 标注、未删历史：① 文件规模「17 个文件约 6609 行 / `rhi_vk.cpp` 3669 行」→ 实测 16 个源文件 7210 行、`rhi_vk.cpp` 4078 行；② 「Vulkan 1.0」→ 实例申请 `VK_API_VERSION_1_3`（`rhi_vk.cpp:749`）；③ 「环形 UBO slot … slot0 为 CPU 暂存区」→ CPU 暂存已移到 `program_t::ubo_stage`（`rhi_vk.cpp:312-317`）；④ 「描述符集轮转：每帧在飞一份」→ 实为每 program 1024 份、逐绘制轮转（`rhi_vk.cpp:85`、`:560-579`）。用法示例里的两处错误（`uniform_matrix(..., "transform", …)` 应为 `"tranMatrix"`；`create_surface(nullptr, …)` 在 VK 下必然失败）也一并标注订正。
- 证据：`grep -n "已作废（2026-10-07 复核 r677）" fantasy/backend/README.md`。
- 影响：无（已订正）；列在此处只为让接手的人知道「README 的历史是怎么错的」。
- 复现/验证：`grep -n "已作废" fantasy/backend/README.md`。
- 建议：无需动作。

### G2. `rhi_vk.h:10` 的「当前为占位实现」注释与 4078 行实现不符（**指针**，已在根台账记录）
- 位置：`rhi_vk.h:10`
- 现状：注释仍是 `// 工厂：创建 Vulkan 后端（当前为占位实现）`，而 `rhi_vk.cpp` 是完整实现、`create_vk_backend()` 返回真实单例。
- 证据：根 `DEBUG.md` G1 已给出实测（`grep -c "vkCreate\|vkCmd\|vkDestroy\|vkAllocate" fantasy/backend/rhi_vk.cpp` → 264，`:4072-4076` 单例）；本文不复制，只留指针（本仓台账不重复记账）。
- 影响：只影响读者（会误以为 VK 后端不可用），不影响运行。
- 复现/验证：`sed -n '10p' fantasy/backend/rhi_vk.h`。
- 建议：改注释（属根 `DEBUG.md` G1 的处置范围）。

### G3. `shader_preprocess.h` 的「宏清单」不全，且 sampler 绑定注释与实际描述符布局不符
- 位置：`shader_preprocess.h:7-17`（宏清单，10 组）、`:14-17`（`XHQM_TEX0/1/2` 写「VK 绑定 binding 1..3」、`XHQM_SHADOW_MAP` 写「VK 绑定 binding 4..7」）；`rhi_vk.cpp:1416`（注释「binding 4-7 = 阴影采样器（级联）」）
- 现状：预处理实际做 47 次替换（`grep -c "s = replace_all" fantasy/backend/shader_preprocess.cpp` → 47），清单里没有 `XHQM_TOON_LIGHTING`、`XHQM_IRRADIANCE`、`XHQM_NORMAL_MAP`、`XHQM_AO_MAP`、`XHQM_CUBE_SHADOW`、`XHQM_PREFILTER`、`XHQM_INSTANCE_ID`、`XHQM_MAX_LIGHTS`/`XHQM_MAX_CASCADES` 以及 15 个 `XHQM_FLAG_*`；且 `shadowMap` 在 VK 侧是**一个 binding（4）+ `descriptorCount = 4` 的数组**（`rhi_vk.cpp:1422-1429`），不是 binding 4..7 —— binding 5/6/7 分别是 `cubeShadow`/`irradianceMap`/`prefilteredMap`（`shader_preprocess.cpp:290-295`）。
- 证据：`sed -n '7,17p' fantasy/backend/shader_preprocess.h` 与 `sed -n '1416,1429p' fantasy/backend/rhi_vk.cpp` 对照；`grep -c "s = replace_all" fantasy/backend/shader_preprocess.cpp` → 47。
- 影响：读注释的人会把「4..7」当成「4 张级联各占一个 binding」，据此写绑定代码就会把 cube shadow 打到 binding 5 语义上（`unit_binding()` 的 `:2251-2261` 才是权威映射）；新加宏时也容易忘记补清单。
- 复现/验证：上述两条 `sed` 可直接看。
- 建议：注释改成「binding 4（4 元素数组）+ 5/6/7」，并把 47 个宏列全或改指 `shader_preprocess.cpp`。

### G4. `spirv.cpp` 声称避免依赖 `glslang-default-resource-limits`，但 Linux/macOS 构建仍链接它
- 位置：`spirv.cpp:27-28`（注释与手写资源上限的理由）；`CMakeLists.txt:712-714`（macOS）与 `:717-718`（Linux）都把 `glslang-default-resource-limits` 链进 `xhqm`；Windows 分支 `CMakeLists.txt:687-696` 不链（与注释一致）
- 现状：`default_resource_limits()`（`spirv.cpp:29-148`）自建 `TBuiltInResource` 回避了该库的 `_Find_first_of_pos_vectorized` STL 符号问题，但非 Windows 的链接行仍然引用这个库（从未被调用）。
- 证据：`sed -n '27,28p' fantasy/backend/spirv.cpp`、`sed -n '712,718p' CMakeLists.txt`；`grep -rn "GetDefaultResourceLimits\|DefaultTBuiltInResource" fantasy/backend/` → 0 命中。
- 影响：只影响构建（多一个链接依赖，换 glslang 发行包时可能因缺该库而配置失败），不影响运行；注释与构建脚本口径不一致会让接手的人以为「Windows 与 Linux 用同一套链接」。
- 复现/验证：`grep -n "glslang-default-resource-limits" CMakeLists.txt` → `:713`、`:718`。
- 建议：要么从链接行去掉这个库，要么在注释里补一句「仅 Windows 不链」。

### G5. 历史台账 `fantasy/fantasy_bugs.md` 的数字与当前代码脱节
- 位置：`fantasy/fantasy_bugs.md:52`（#22：「`ubo.h` `ubo_offset("params")` 与 `"tranMatrix"` 共用 `UBO_TRANSFORM(0)`」段里写「21824 字节 UBO」）、`:51`（#21：`XHQM_TEX0`/`XHQM_SPRITE_TEX`/`XHQM_COLOR_TEX` 都映射 binding=1）
- 现状：`UBO_SIZE` 现为 **38240**（`ubo.h:44`），21824 是更早的布局数字；#21 的「三个宏都映射 binding 1」**今天依然成立**（`shader_preprocess.cpp:272-285`，三者分处不同 shader），它和 #22 的「待定」结论都还没变。
- 证据：`grep -n "21824" fantasy/fantasy_bugs.md`；`grep -n "UBO_SIZE" fantasy/backend/ubo.h` → `:44`；`grep -n "binding = 1" fantasy/backend/shader_preprocess.cpp`。
- 影响：按台账里的字节数去算偏移会对不上（差 16416 字节）；#21/#22 的「待定」结论与本文 F3 是同一现象，读者容易以为是新问题。
- 复现/验证：上述 grep 可直接看。**该台账按本仓约定不搬家、不改名，本文只做交叉引用。**
- 建议：在 `fantasy_bugs.md` 的 #22 处补一行「UBO 现为 38240 字节，见 `ubo.h:44`」（属该文件的处置范围）。

### G6. Windows 的 VS 工程里没有本模块任何一个源文件（本模块只有 CMake 一条构建路径）
- 位置：`xhqm.vcxproj`（98 条 `ClCompile`/`ClInclude`）与 `CMakeLists.txt:602-629`
- 现状：`grep -c "fantasy" xhqm.vcxproj` → 只有 6 处，全是 `D:\MarineRecord\libary\xhqm\.\fantasy\glew\…` 的 `IncludePath`/`LibraryPath`（`:185-197`，另一台机器的硬编码路径）；**一条 `fantasy\backend\*` 的编译项都没有**，也没有 `BUILD_WITH_VULKAN`/`BUILD_WITH_OPENGL` 的预处理器定义。真正构建本模块的只有 CMake（`CMakeLists.txt:602-629` 按 `XHQM_DEVICE`+`XHQM_RENDER_BACKEND` 决定 `rhi_gl.cpp` / `rhi_vk.cpp`+`spirv.cpp` 是否入表；`rhi_no.cpp`/`shader.cpp`/`shader_preprocess.cpp` 恒在）。
- 证据：`grep -n "fantasy" xhqm.vcxproj`、`grep -c "ClCompile Include\|ClInclude Include" xhqm.vcxproj` → 98；`grep -rn "rhi_vk.cpp\|rhi_gl.cpp" xhqm.vcxproj` → 0 命中。
- 影响：在 Windows 上按 `xhqm.sln` 构建的人拿不到 RHI（也没有那两个宏），会以为后端「不存在」；两套构建口径不一致也让「改了 CMake 忘了 vcxproj」成为常态（反之亦然）。硬编码盘符路径一节见根 `DEBUG.md` C2，本文不重复。
- 复现/验证：`grep -c "fantasy" xhqm.vcxproj`（6）、`grep -rn "backend" xhqm.vcxproj`（0）。
- 建议：要么把本模块的 4 个 TU + 宏定义补进 vcxproj，要么在 README 里写明「Windows 也走 CMake」。
