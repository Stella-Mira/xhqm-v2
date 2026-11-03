# spatial · 当前问题清单

`fantasy/spatial`（可见性决策层：BVH / CPU+GPU 视锥剔除 / 遮挡剔除 / Hi-Z / LOD / 渲染队列）**当前仍存在**的问题台账：只记「现在还是坏的 / 还是没写的 / 还是对不上的」，**不记已修项、不做版本历史**。每条都给可核对的 `文件:行号`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**：`fantasy/fantasy_bugs.md`（fantasy 静态分析清单 + 逐轮修复记录）保持原名原位、不搬不改；其中与 spatial 相关的 #31/#32/#35 均标为已修，本文件不复制它们，只承接它们**没覆盖到的**现存问题。根模块台账在 `DEBUG.md`，模块级台账同批建立（如 `hardware/DEBUG.md`、`lattice/DEBUG.md`），各自只记本模块。本模块 README 里的历史错说法按本仓惯例在原处划掉标注，不在这里重述。
>
> 判据纪律：凡写「某 API 不存在 / 编译不过 / 会崩」，必须附复现命令。本模块代码可单独语法检查（见文末「本轮核对记录」）。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. 遮挡剔除的深度专用顶点着色器丢掉了裁剪空间 w，深度写入值错误（GL 路径上必然错）
- 位置：`fantasy/backend/shader_sources.h:409`（`occlusion_vert()` 内 `gl_Position = vec4(p.xyz, 1.0);`），调用方 `fantasy/spatial/occlusion.cpp:139`（传入 `clip_from_world`）
- 现状：`p = ubo.transform * vec4(vertex,1.0)` 里的 `ubo.transform` 是**完整的 clip_from_world（proj * view * camera）**，但输出时把 `w` 硬写成 1.0。于是后续硬件除法的分母恒为 1，写入深度缓冲/查询统计的 `z` 不是 NDC z（GL 下是投影矩阵的 z 分量，量级与 [0,1] 深度完全不同）。
- 证据：同文件内其它几何顶点着色器都保留 w —— `shader_sources.h:31`（`gl_Position = ubo.transform * pos;`）、`:102`、`:304`、`:339`、`:371`、`:391`、`:450`（阴影专用 `depth_vert` 用 `vec4 clipPos = ubo.transform * localPos; ... gl_Position = clipPos;`）。全仓只有 `:409` 这一处写 `vec4(p.xyz, 1.0)`。
- 影响：`occlusion_culler` 的遮挡判定依据是「包围盒画出的样本数」，深度值错 ⇒ 遮挡关系错。GL 路径（`XHQM_DEPTH_REMAP` 展开为空）上对象与场景深度不在同一空间，属**必然错**；Vulkan 路径上 `XHQM_DEPTH_REMAP`（`shader_preprocess.cpp:251`）会把 `0.5*(z+w)` 再折一次，恰好把 `w=1` 的退化情形折回 [0,1]，因此**看起来正确**——两条路径的正确性不对等。该件在库内**没有任何调用方**（见 E2），所以当前是「没被触发」而非「已暴露」。
- 复现/验证：静态可复现：`grep -n "gl_Position = " fantasy/backend/shader_sources.h` 对照 `:409` 与 `:450`；运行期需 GL 上下文与深度 FBO，**本轮未跑**（该项依赖 GPU 上下文，本节点只能静态核对）。
- 建议：改成 `gl_Position = p;`（保留 w），与 `depth_vert` 对齐。

### A2. `occlusion_culler` 不绑定自己的深度目标，也不清深度、不设视口，首帧查询在空深度上做
- 位置：`fantasy/spatial/occlusion.cpp:135-160`（整个「记录本帧查询 pass」）
- 现状：`cull_and_record` 只做四件事：`color_write(false)`、`depth_test(true,false)`、`use_program(m_program)`、`uniform_matrix(...)`，然后对每个候选 `query_begin / draw(36 顶点) / query_end`。**没有** `begin_offscreen`、**没有**清深度、**没有** `viewport(...)`，也没有任何「首帧无有效深度先跳过查询」的标记。
- 证据：`occlusion.cpp:136-139` 三步状态设置之后直接进入 `for (auto& c : candidates)` 的绘制循环；全文件无 `begin_offscreen` / `viewport` / 深度清空调用（`grep -n "offscreen\|viewport" fantasy/spatial/occlusion.cpp` → 0 命中）。头注释 `occlusion.h:5-6` 也只说「对每个候选对象渲染其包围盒 + 遮挡查询」，未声明目标由调用方绑定。
- 影响：调用方若在场景 pass 中间调用（`occlusion.h:39` 说 `clip_from_world` 与渲染循环一致，暗示就在渲染循环里用），遮挡盒会被画进**主画面深度**（污染画面深度 + 采样数失真）；首帧的 `visible` 统计来自未清深度的目标，判定不可信。这是该类的**头号接线缺口**，比「一帧延迟」更致命。
- 复现/验证：静态：`grep -n "begin_offscreen\|viewport\|glClear" fantasy/spatial/occlusion.cpp`（0 命中；文件里唯一的 `clear` 是 `:86` 的 `m_slots.clear()`，与深度缓冲无关）。运行期未跑（同 A1，需 GL 上下文）。
- 建议：把「目标绑定/清深度/视口」显式纳入契约（由 `cull_and_record` 自己 `begin_offscreen` 一张深度专用离屏，或在头文件里写死调用前置条件）。

### A3. `cull_and_record` 泄漏渲染状态：`depth_test(true,false)` 与 `use_program` 不复位
- 位置：`fantasy/spatial/occlusion.cpp:137-138`（置位），`:160`（只恢复了 `color_write(true)`）
- 现状：函数入口把深度测试改成「开 + `GL_LESS`」（`rhi_gl.cpp:521-525`：`less_equal=false` → `glDepthFunc(GL_LESS)`），并把 program 换成遮挡专用着色器；退出时只把颜色写恢复为 true。深度函数此后一直是 `GL_LESS`（引擎默认链通常用 `LEQUAL`），program 也仍是遮挡着色器，直到调用方自己重设。
- 证据：`occlusion.cpp:160` 仅一行 `m_rhi->color_write(true);`，其后直接 `m_has_frame = true; return visible;`；`vertex`/`program` 的还原在整个函数里都没有。
- 影响：任何在 `cull_and_record` 之后直接绘制的 pass 都可能落在 `GL_LESS` + 遮挡着色器上——表现为「贴面 z-fighting 变多」「某些物体整体不画」这类无提示的渲染异常。
- 复现/验证：静态：`grep -n "color_write\|depth_test\|use_program" fantasy/spatial/occlusion.cpp` → `:136-138`、`:160`。运行期未跑。
- 建议：入口保存 / 出口恢复（或把遮挡 pass 放进独立离屏后统一复位）。

### A4. Hi-Z 的最近深度折算只对 OpenGL 的 NDC z 成立，Vulkan 下判据会偏向过度剔除
- 位置：`fantasy/spatial/gpu_hiz_culler.cpp:114-115`（`float nearestDepth = 0.5 * (ndcMin.z + 1.0); if (nearestDepth > hizMax) visible = false;`）、`:88`（`ndcMin.z > 1.0` 判定）
- 现状：着色器把 `ndc.z` 当作 GL 形 `[-1,1]` 处理（`0.5*(z+1)` 与 `±1` 范围判定）。但引擎在 Vulkan 路径上把所有离屏渲染的 `gl_Position.z` 重映射为 `0.5*(z+w)`（`shader_preprocess.cpp:251` 的 `XHQM_DEPTH_REMAP`，仅 Vulkan 展开），Vulkan 的 NDC z 已是 `[0,1]`；着色器里没有对应分支。
- 证据：`gpu_hiz_culler.cpp:88` 用 `ndcMin.z > 1.0` 判远平面、`:114` 用 `0.5*(minNdcZ+1)` 折深度；同文件 `:71-82` 只跳过 `cl.w <= 0` 的角点。全文件无 `vulkan` 分支（对比 `gpu_culler.cpp:98`、`gpu_hiz.cpp:63` 都按 `name()[0]=='v'` 选版本）。
- 影响：Vulkan 下 `nearestDepth` 偏大（把近物体算得更远）⇒ 更早判定「被遮挡」⇒ 该画的对象被剔掉（画面缺物）。GL 路径不受影响。
- 复现/验证：需要 Vulkan 设备 + 真深度金字塔，**本轮未复现**；静态证据如上。另有两点使这个偏差在现有验证里**测不出来**：(a) `test/view/main.cpp:581-616` 的 [HIZCULL] 用例走 `matrix(4,4)` 单位阵，`ndc.z == center.z`，恰好同时满足两种约定；(b) 该用例喂的深度 0.9 与「GL 折算 0.75 / Vulkan 折算 0.95」分别落在阈值两侧，所以两种约定都能过——判据的数值差（真实遮挡面存 0.9 时 Vulkan 折算成 0.95，比正确值大 0.05）只有在贴近阈值时才会显形。
- 建议：按后端分别折算（或统一要求调用方传入 `[0,1]`，并在 GL 侧先 `0.5*z+0.5`）。

### A5. `occlusion_culler` 的槽位用对象**地址**做键，但没有对象存活校验，可能画已析构对象的旧 AABB
- 位置：`fantasy/spatial/occlusion.cpp:92-99`（`slot_of`）、`:143`（`same_aabb` 短路条件）
- 现状：`slot_of` 靠 `s->obj == obj` 找槽，找不到就新建；可见性判定与包围盒重建都以这个裸指针为准，`cull_and_record` 全程不接触对象本身（不查 `used()`、不查 `object_id()`），也从不因为对象不在候选里而回收槽。
- 证据：`occlusion.cpp:94` 为唯一比较；重建条件 `if (!s->has_box || !same_aabb(s->world_aabb, c.first))`（`:143`）意味着「AABB 没变就不重建顶点缓冲」。全文件无 `delete`/析构回调/代际计数。
- 影响：对象被 delete 后，若 `candidates` 里已不含它，它的 vb/query 会一直留着（见 B1）；若该地址被新对象复用且新 AABB 恰好相同，则沿用的是死对象的 vb（数值碰巧相同所以看不出）。真正的坑是**调用方按「每帧重建候选列表」的模式用**时，槽表只增不减。
- 复现/验证：静态：`grep -n "s->obj == obj" fantasy/spatial/occlusion.cpp`。运行期未跑。
- 建议：槽改按 `object_id()` 索引，并在 `candidates` 变化时回收消失对象的 vb/query。

### A6. `occlusion_culler` 的 `query_create` 失败静默降级、`threshold` 无范围校验、候选列表被遍历两次
- 位置：`fantasy/spatial/occlusion.cpp:154`（`if (!s->query) continue;`）、`fantasy/spatial/occlusion.h:46`（`threshold` 无钳位）、`fantasy/spatial/occlusion.cpp:129`/`:142`（`slot_of` 被调两次）
- 现状：（a）`query_create` 失败时该对象整帧不记录查询、也**不会重试报错**，`s->queried` 保持 false，于是在 `:131` 里被永久当成「可见」（`(!s->queried)` 分支）。结果：一次查询创建失败 = 该对象永久免疫遮挡剔除，且无任何提示。（b）`threshold(uint)` 直接赋值，样本数是逐帧累加的像素计数，设成很大或很小的值都不会被拦，语义错误时表现为「全部被剔」或「永不剔除」。（c）可见性判定与包围盒重建各遍历一次 `candidates`，`slot_of` 是线性查找（见 D2）。
- 证据：`:154` 的裸 `continue`；`:131` 的 `vis = (!s->queried) || (s->visible > m_threshold)`；`occlusion.h:24` 的 `uint m_threshold = 0` 与 `:46` 的直接赋值。
- 影响：静默劣化型缺陷——不崩、不报错，只是剔除不生效/全生效，排查时看不出原因。
- 复现/验证：静态：`grep -n "queried\|threshold" fantasy/spatial/occlusion.cpp fantasy/spatial/occlusion.h`。运行期未跑。
- 建议：查询创建失败要写回 `error` 或计数上报；`threshold` 加说明与范围校验。

### A7. `gpu_hiz_culler` 的 `hiz_levels` 无上限校验，`hiz_w/hiz_h` 与 SSBO 实际尺寸不校验 —— 参数不一致会静默越界读
- 位置：`fantasy/spatial/gpu_hiz_culler.cpp:184-194`（宿主侧重算层偏移）、`:187`（`i < hiz_levels && i < 12`）、着色器 `:29`（`uint hizOffsets[12]`）与 `:98-99`（`level = min(level, max(hiz_levels,1u)-1u); level = min(level, 11u);`）
- 现状：`cull()` 只接收 `hiz_ssbo / hiz_w / hiz_h / hiz_levels` 四个裸参数，**不与任何 `gpu_hiz` 实例做一致性校验**：`hiz_w/hiz_h` 与 SSBO 实际尺寸不符时，着色器会按错误步长在缓冲里读（越界或读错层）；`hiz_levels > 12`（8K 分辨率下 `gpu_hiz::build` 会产出 13 级以上）时宿主循环只填 12 项，第 13 项及其后为 0，着色器端 `min(level,11u)` 只防了 `hizOffsets[]` 数组越界、不防「读到层 0 的数据」。
- 证据：`gpu_hiz_culler.cpp:185-194` 是宿主侧第二份金字塔偏移推算（与 `gpu_hiz.cpp:73-84` 逻辑等价但独立实现）；`gpu_hiz_culler.cpp:187` 的 `i < 12`；`gpu_hiz_culler.cpp:100-101` 的 `lw/lh` 由传入的 `hizW/hizH` 直接移位得出，无边界检查。历史 #35 只修了「数组越界」，未修参数校验。
- 影响：错配时结果静默失真（该画的不画 / 不该画的画），极端参数下读出 SSBO 之外的内容。调用方只要忘了把 `hz.width()/height()/level_count()` 原样传进去就会中招。
- 复现/验证：静态：`grep -n "hizOffsets\|hiz_levels\|hiz_w" fantasy/spatial/gpu_hiz_culler.cpp`；`gpu_hiz_culler.cpp:187` 与 `gpu_hiz.cpp:81` 两份循环对照。运行期未跑。
- 建议：`cull()` 改收 `const gpu_hiz&`（或加一次性一致性断言），宿主侧删掉重复的偏移推算。

---

## B. 资源与并发

### B1. `occlusion_culler::m_slots` 只增不减，对象消失后 vb/query 永不释放
- 位置：`fantasy/spatial/occlusion.cpp:92-99`（新建）、`fantasy/spatial/occlusion.h:21`（`std::vector<slot*> m_slots`）、`fantasy/spatial/occlusion.cpp:74-90`（唯一释放点 `reset()`）
- 现状：槽按对象指针建，只在 `reset()` 里统一 `vertex_delete` + `query_delete` + `delete s`。场景切换若不走 `reset()`（例如只是删掉一批模型），对应槽与其 GPU 资源会一直留到析构。
- 证据：`grep -n "m_slots" fantasy/spatial/occlusion.cpp fantasy/spatial/occlusion.h` → 只有 `push_back`（`:97`）、两次遍历（`:119-123`、`reset` 的 `:78-83`），无 `erase`/`remove`。
- 影响：长跑进程（反复加载/卸载场景而不重建 culler）GPU 资源单调增长。触发条件＝调用方不调 `reset()`。
- 复现/验证：静态：`grep -n "m_slots.erase\|m_slots.clear" fantasy/spatial/occlusion.cpp`（仅 `:86` 的 `clear()`，在 `reset()` 内）。未复现（需长跑场景）。
- 建议：给「本帧未出现在候选里」的槽加 N 帧宽限后回收。

### B2. `cull_and_record` 的早退路径会孤儿化资源
- 位置：`fantasy/spatial/occlusion.cpp:115`（`if (!m_ready) return visible;`）
- 现状：`m_ready` 为假的唯一来源是 `setup()` 里 `shader_create` 失败（`:70`）。此后每次调用都在读结果之前返回，但**已建好的槽（vb/query）不会被释放**；实现者若「先建对象、再 setup、失败后重建 setup」，前一版槽会一直挂着。
- 证据：`occlusion.cpp:63-72`（setup 失败路径只置 `m_ready=false`）、`:115`（早退）；类内没有别的资源清理入口（`reset()` 反而是唯一能清的，但它也会把 `m_rhi` 置 nullptr，见 B3）。
- 影响：资源泄漏 + 静默失效（返回空列表，调用方以为「没有可见对象」）。
- 复现/验证：静态：`grep -n "m_ready" fantasy/spatial/occlusion.cpp` → `:70`、`:88`（`reset` 置 false）、`:115`。未复现。
- 建议：`setup` 失败时把已建资源一并清掉，或在早退前给出明确的错误信号。

### B3. `reset()` 在 `m_rhi` 为空时跳过已创建的 program（泄漏 shader）
- 位置：`fantasy/spatial/occlusion.cpp:74-90`（`:76` 的 `if (m_rhi)` 把 `shader_delete` 一起括进去了）
- 现状：`m_program` 的删除在 `if (m_rhi)` 块内（`:84`）。若 `m_rhi` 为 nullptr 而 `m_program` 非 0，着色器不会被删——这正是 `setup()` 在 `shader_create` 之后、赋值 `m_rhi` 之前失败时的状态（其实 `m_rhi = &rhi` 在 `:65` 先执行，所以当前实现下不可达；但顺序一旦调整就会踩上，属脆弱写法）。
- 证据：`occlusion.cpp:65`（`m_rhi = &rhi;` 先于 `:67` 的 `shader_create`）、`:76-85` 的 `if (m_rhi)` 包住了 `if (m_program)`。
- 影响：当前不可达，重构后可达；归类为「隐患」而非现网缺陷。
- 复现/验证：静态：`awk 'NR>=74 && NR<=90' fantasy/spatial/occlusion.cpp`。未复现。
- 建议：把 program 的删除挪出 `if (m_rhi)`。

### B4. `gpu_culler` / `gpu_hiz` / `gpu_hiz_culler` 的析构在**当时**的活动后端上删资源，不缓存自己的后端
- 位置：`fantasy/spatial/gpu_culler.cpp:86-91`、`gpu_hiz.cpp:52-56`、`gpu_hiz_culler.cpp:133-138`（三处都是 `render_backend& rhi = render_backend_active();` 再 `shader_compute_delete/storage_delete`）
- 现状：这三个类只在 `build()` 里临时取一次 `render_backend_active()`，不保存指针；析构时重新取。而 `render_backend_active()` 会**惰性创建**后端，且后端销毁后会退役到 `none` 桩（`rhi_no.cpp:163-192`；`none_backend::name()` 返回 `"none"`，所有 delete 是空实现）。
- 证据：`gpu_culler.cpp:86`、`gpu_hiz.cpp:52`、`gpu_hiz_culler.cpp:133`；对照 `occlusion_culler` 反而缓存了 `m_rhi`（`occlusion.h:19`）。
- 影响：（a）后端已退役 → 删除调用落到 `none` 桩上变成**静默泄漏**；（b）活动后端被换成另一个实例 → 句柄在别的后端的 map 里查不到，同样是静默泄漏（GL 侧 `vertex_delete`/`storage_delete` 都按 id 在 map 里查，查不到直接返回）。触发条件＝宿主在后端销毁后仍持有 culler 对象（跨上下文切换的宿主比进程退出更常见）。
- 复现/验证：静态：`grep -n "render_backend_active" fantasy/spatial/*.cpp`（`occlusion.cpp` 用 `m_rhi`，其余三处都在析构里现取）。未复现（需多上下文宿主）。
- 建议：`build()` 里缓存 `render_backend*`，析构前判空并只对自己那份后端操作。

### B5. 三个 GPU 类的 `build()` 失败路径留下本次已创建的资源
- 位置：`fantasy/spatial/gpu_culler.cpp:100-101`（program 建好 → params SSBO 失败即 return）、`:106-107`、`:124-125`、`:129-130`；`gpu_hiz.cpp:65-66`、`:88-89`、`:93-94`；`gpu_hiz_culler.cpp:147-148`、`:153-154`、`:167-168`、`:172-173`
- 现状：任何一步失败都直接 `return false`，之前已成功创建的 program/SSBO 保存在成员里、不释放；类里也没有「未就绪」标记，重复调用 `build()` 会直接覆盖句柄 ⇒ 旧句柄永久丢失。
- 证据：`gpu_culler.cpp:101/107/125/130` 四处裸 return（同上另两个文件）；三个类都**没有** `reset()`（对比 `occlusion_culler::reset()` 提供完整回收）。
- 影响：资源泄漏；且失败后对象状态不确定（`m_object_count` 仍是 0，后续 `cull/draw` 静默 no-op），调用方只能靠返回值判断。
- 复现/验证：静态：`grep -n "return false" fantasy/spatial/gpu_*.cpp`。未复现。
- 建议：失败时统一走一个 `release()`；或先把局部句柄建好、全成功后再一次性提交到成员。

### B6. 手工打包的 SSBO 布局与 GLSL 声明之间没有编译期校验
- 位置：`fantasy/spatial/gpu_culler.cpp:103-130`（params 28 word / objects 16 word / output 5 word）对应着色器 `:32-56`；`gpu_hiz_culler.cpp:150-173`（params 36 word）对应着色器 `:22-51`；`gpu_hiz.cpp:86-94`（params 6 word）对应着色器 `:21-28`
- 现状：三处「C++ 侧 `std::vector<uint32_t>` 手写偏移 + GLSL 侧 `layout(std430)` 结构体」是两套独立真值来源，没有 `static_assert`、没有布局自检、也没有共享的布局常量头。本轮逐字节核算三处布局**当前是一致的**（params 112B=28 word、objects 64B/对象、output 20B/命令、hiz params 24B=6 word、hiz_culler params 144B=36 word；`mat4` 在 std430 下 16 字节对齐、`hizOffsets[12]` 紧随其后 = `params[24..35]`）。
- 证据：`gpu_culler.cpp:104/110/128` 与 `gpu_culler.cpp:32-56`；`gpu_hiz_culler.cpp:151/157/171/196-205` 与 `gpu_hiz_culler.cpp:22-51`；`gpu_hiz.cpp:87/92` 与 `gpu_hiz.cpp:21-28`。
- 影响：任何人往着色器结构体里插一个字段、或调整 `pad[]`，宿主侧不会报错，结果是**静默读错值**（错矩阵/错平面/错命令），只在画面或剔除结果上体现。Vulkan 对这种错配尤其不宽容（布局必须严格一致）。
- 复现/验证：静态核对（如上）。未跑 GPU 校验层。
- 建议：把三处布局提成共享常量（或加 `static_assert(sizeof(...) == N)` 对应结构体）。

---

## C. 跨平台

### C1. `occlusion_culler::setup` 的后端判断只认首字母 `'v'`，与 `none` 桩同形
- 位置：`fantasy/spatial/occlusion.cpp:66`（`const bool vulkan = (rhi.name()[0] == 'v');`）
- 现状：`none_backend::name()` 返回 `"none"`（`rhi_no.cpp:19`），首字母 `'n'`，于是被当成非 Vulkan（GL 分支）。在 `no/cpu/net` 设备下 `shader_create` 会返回 0（`rhi_no.cpp:33`），`setup` 失败——所以当前不会误编译着色器；但同样的判断式在 `gpu_culler.cpp:98`、`gpu_hiz.cpp:63`、`gpu_hiz_culler.cpp:145` 也各写了一遍（4 份重复），任何一处将来改成「非 v 即 gl」的写法就会在 none/auto 宿主上踩空。
- 证据：`grep -n "name()\[0\]" fantasy/spatial/occlusion.cpp fantasy/spatial/gpu_culler.cpp fantasy/spatial/gpu_hiz.cpp fantasy/spatial/gpu_hiz_culler.cpp` → 4 行（`:66`/`:98`/`:63`/`:145`）；`rhi_no.cpp:19` 的 `"none"`。
- 影响：none/CPU 设备上是「优雅失败」（返回 false），换成别的桩名（如 `"null"`）或空字符串时 `name()[0]` 是 `'\0'`，仍然走 GL 分支、依然是优雅失败；真正的风险是**判断式分散 4 份**、与 `occlusion.h:66` 的单处使用不一致。
- 复现/验证：静态：`grep -rn "name()\[0\]" fantasy/spatial/`。未复现。
- 建议：抽一个 `bool backend_is_vulkan(const render_backend&)`，四处共用。

### C2. `matrix` / `vector` 的手工转置（`tramat()`）在两处 GPU 上传路径上无断言
- 位置：`fantasy/spatial/occlusion.cpp:139`（`clip_from_world.tramat().data()`）、`fantasy/spatial/gpu_hiz_culler.cpp:203-204`（`matrix vpt = view_proj.tramat(); ... params[8+i] = float_bits(vpt.data()[i]);`）
- 现状：引擎 `matrix` 是行主序存储（`algebra/geometry/linear.h:63-68`），GLSL 期望列主序，`tramat()` 转置本身是对的；但两处上传都直接取平铺 `data()`，没有维度断言（`matrix` 允许非 4x4）。若调用方传 3x3 或非方阵，会静默读越界/传错。
- 证据：`linear.h:252-260`（`tramat` 实现）、`occlusion.cpp:139`（把 `.data()` 直送 `uniform_matrix`，后者声明「列主序」）、`gpu_hiz_culler.cpp:204`（按 `[8..23]` 固定 16 个 float 位置填）。
- 影响：传错矩阵大小 = 静默错变换（对象位置/裁剪结果全错），无编译期或运行期提示。Vulkan 与 GL 表现相同，属「跨平台一致地错」。
- 复现/验证：静态：`grep -n "tramat" fantasy/spatial/*.cpp`。
- 建议：上传前断言 `m_dimensions == {4,4}`（或改为只接受 4x4 的窄接口）。

---

## D. 性能

### D1. BVH 建树/查询的递归 `std::function` 每层拷贝并切分列表
- 位置：`fantasy/spatial/spatial.cpp:91-134`（`build` 的 `std::function<node*(...)> rec`）、`:114`（`std::vector<std::pair<aabb,object*>> l = list;`）、`:126-127`（左右两份新 vector）、`:149-160` / `:169-180`（查询也用 `std::function` 递归）
- 现状：每个内部节点都会（a）整体复制 `list`，（b）为左右子树各建一个新 vector，（c）按元素中心 `std::sort`。对象数 N 时分配量随 N log N 增长。查询侧每次调用构造 `std::function`（可能堆分配）并递归。
- 证据：`spatial.cpp:114/123/126/127`；叶子阈值 `:99`（`list.size() <= 4`）。
- 影响：场景大、且**每帧重建** BVH 时（`scene_culler::build` 就是全量重建），CPU 时间与内存抖动明显；这也是 README 曾称「栈压力」的同源问题（真实代价在堆分配与递归，不在栈）。
- 复现/验证：静态：`awk 'NR>=91 && NR<=134' fantasy/spatial/spatial.cpp`。**未做性能实测**。
- 建议：改成「索引数组 + 原地 nth_element/划分」，或至少让 `rec` 接收 vector 的迭代区间。

### D2. `slot_of` 线性查找 × 两次遍历候选 ⇒ O(n²)
- 位置：`fantasy/spatial/occlusion.cpp:92-99`（线性查找 + `push_back` 新建）、`:127-133` 与 `:140-159`（每个候选各调一次）
- 现状：单帧内对 `candidates` 遍历两遍，每遍每个对象都做一次线性扫描；n 个候选、m 个槽时是 O(n·m)。
- 证据：`occlusion.cpp:94`（`for (auto* s : m_slots) if (s->obj == obj)`）；`grep -n "slot_of" fantasy/spatial/occlusion.cpp` → `:129`、`:142`。
- 影响：候选数上千时 CPU 侧剔除本身成为瓶颈（遮挡剔除的收益被抵消）。
- 复现/验证：静态计数；**未做性能实测**。
- 建议：改 `std::unordered_map<object*, slot*>`（键同 A5 应换成 `object_id()`）。

### D3. 遮挡查询结果读回是阻塞式的，CPU 无法提前一帧
- 位置：`fantasy/spatial/occlusion.cpp:122`（每帧对每个已记录槽读一次 `query_result`）；后端实现：`fantasy/backend/rhi_vk.cpp:2783-2791`（`vkGetQueryPoolResults` 带 `VK_QUERY_RESULT_WAIT_BIT`）、`fantasy/backend/rhi_gl.cpp:759-766`（`glGetQueryObjectuiv(GL_QUERY_RESULT)`）
- 现状：GL 的 `GL_QUERY_RESULT` 与 Vulkan 的 `WAIT_BIT` 都会**等 GPU 完成**该查询才返回；`cull_and_record` 在帧首对全部槽同步读一遍。所谓的「跨帧复用避免 stall」并不成立——只是把 stall 从「本帧查询」挪到了「本帧读上一帧」。
- 证据：`rhi_no.h:168` 的注释自陈「同步读回样本数」；`rhi_vk.cpp:2789` 的 flag 组合。
- 影响：每帧一次全槽同步等待，GPU 有空闲间隙也用不上；遮挡剔除省下的 draw 可能抵不过这次往返。这也是模块「零回读、无同步」说法的反例（README 已订正）。
- 复现/验证：静态：`grep -n "QUERY_RESULT_WAIT_BIT" fantasy/backend/rhi_vk.cpp`、`grep -n "glGetQueryObjectuiv" fantasy/backend/rhi_gl.cpp`。未做帧时间实测。
- 建议：改用 `GL_QUERY_RESULT_AVAILABLE`/无 `WAIT_BIT` 的非阻塞读，未就绪则沿用更早的结果。

### D4. Hi-Z 调试读回代价 = 整座金字塔
- 位置：`fantasy/spatial/gpu_hiz.cpp:134-145`（`read_level` 先 `storage_read` 整个 `total`，再 `assign` 目标层）
- 现状：即使只要顶层 1x1，也会把 level0 起的全部层读回 CPU。
- 证据：`gpu_hiz.cpp:138-143`；`total = m_offsets.back() + 末层像素数`（`:138`）。
- 影响：1920×1080 金字塔 ≈ 276 万 float ≈ 11 MB/次；只用于调试时无所谓，一旦被误用进帧循环就是灾难。
- 复现/验证：静态；**未实测带宽**。
- 建议：按层 `storage_read` 偏移区间（`storage_read` 没有 offset 参数，需扩接口或加一个分层 SSBO）。

### D5. GPU 剔除非压缩：被剔除对象仍占绘制命令槽位
- 位置：`fantasy/spatial/gpu_culler.cpp:72-77` 与 `:157`（`draw_indexed_indirect_storage(vertex, index, m_output_ssbo, 0, m_object_count)`）、`gpu_hiz_culler.cpp:119-124` 与 `:219`
- 现状：每个对象恒定一条 `DrawIndexedIndirectCommand`，被剔除者 `indexCount=0`。indirect 绘制调用数 = 对象数（不是可见数）。
- 证据：两处 `count` 传的都是 `m_object_count`；着色器无条件写 `outCmds.cmds[gid]`。
- 影响：对象数极大时，间接绘制的命令扫描开销线性增长；GPU 端「零开销跳过」只省了像素/顶点，不省命令处理。
- 复现/验证：静态。未实测。
- 建议：加 compact pass（前缀和 + 可见计数）后用可见数当 draw count。

---

## E. 未实现 / 占位桩

### E1. `gpu_hiz` 没有「从引擎深度资源取深度」的路径（缺深度缓冲读取）
- 位置：`fantasy/spatial/gpu_hiz.cpp:99-105`（`upload_level0` 只接受宿主给的一份 `std::vector<value>`，`size != w*h` 时静默 `return`）；引擎侧只有 `rhi_no.h:110` 的 `offscreen_depth_texture(id)` 与 `:121` 的 `read_offscreen`（颜色附件单像素）
- 现状：Hi-Z 的输入完全靠调用方自己准备。引擎里**没有任何 API** 能把深度纹理读成 `[0,1]` 数组或采样进 SSBO（`grep -n "depth_texture\|read_depth" fantasy/backend/rhi_no.h` 只命中 `:110` 的 `offscreen_depth_texture`（拿纹理句柄）；读回类只有 `:121` 的 `read_offscreen`（颜色附件、单像素）；全仓 `upload_level0` 的调用点只有 `test/view/main.cpp:537`）。
- 证据：`gpu_hiz.cpp:101` 的静默 return；`rhi_no.h:110`、`:121`；测试里的调用点是**手写的 4×4 常量表**（`main.cpp:526-537` 的 `l0`，不是真实深度），而且它整个包在 `main.cpp:522` 的 `#if 0` 里。
- 影响：谁想真用 Hi-Z 遮挡剔除，都得自己补齐「深度资源 → `[0,1]` 数组 → SSBO」这一整段（含 `proj.z` 约定换算）；这段不属于模块，也没有示例。属**缺件**而非坏件，但对使用者是硬门槛。
- 复现/验证：静态：`grep -rn "upload_level0" --include=*.cpp --include=*.h .`（不含文档时只有定义与测试）。未复现。
- 建议：补一个「深度纹理 → Hi-Z level0」的接入点（或至少在头文件写明数据来源与量纲约定）。

### E2. 端到端集成缺位：没有一个剔除件被渲染循环调用
- 位置：库内零调用方 —— `scene_culler` / `spatial_bvh` / `occlusion_culler` / `lod_group` / `render_queue` 全仓只命中定义与文档；`gpu_culler` / `gpu_hiz` / `gpu_hiz_culler` 的唯一使用点是 `test/view/main.cpp`
- 现状：`test/view/main.cpp:581-616` 的 [HIZCULL] 块是模块唯一的 GPU 剔除端到端用例（4×4 金字塔 + 3 个对象 + 单位 VP），但它被嵌在 `main.cpp:522` 的 `#if 0`（到 `:768` 的 `#endif`）之内 ⇒ **不执行**；同一段里 [HIZ] 自身的正确性块（`main.cpp:521-579`）也在同一个 `#if 0` 里。`gpu_culler` 连这种块都没有。
- 证据：`grep -n "#if 0" test/view/main.cpp` → `:466`、`:522`；`grep -rn "gpu_culler\|scene_culler\|occlusion_culler\|lod_group\|render_queue" --include=*.cpp --include=*.h .` 去掉定义与测试后为空；`property.cpp` / `theory_render.cpp` 里没有剔除调用。
- 影响：（a）三类 GPU 剔除的 GL/Vulkan 行为差异（A1/A4 那类）没有任何自动化或半自动验证；（b）想接线的接手者没有参考实现，只能照着头文件猜调用顺序与前置条件（A2 就是这么来的）。
- 复现/验证：`grep -n "#if 0" test/view/main.cpp`；`grep -rn "scene_culler" --include=*.cpp --include=*.h . | grep -v fantasy/spatial`（应为空）。已核对，**未运行**。
- 建议：先把 [HIZCULL] 块从 `#if 0` 里挪出来（它本身就能跑：把 `main.cpp:584-616` 那段整体移到 `:616` 的 `#endif` 之后即可，无需新增文件），再给 `gpu_culler` 补一个同规模用例。

### E3. `gpu_hiz` 的 `read_level` / `gpu_hiz_culler::read_visible` 之外，模块里没有任何自检/单测
- 位置：`fantasy/spatial/*` 无测试文件；`test/CMakeLists.txt` 里 `spatial` 只作为 include 目录出现（`:266`、`:310`），没有任何 spatial 测试目标
- 现状：模块的所有验证都依赖 `test/view/main.cpp` 的诊断 printf，而那两处 [HIZ]/[HIZCULL] 里只有一处（HIZCULL）在 `#if 0` 之外、且仍是诊断打印不是断言；CPU 侧四个件（BVH/场景剔除/LOD/渲染队列）没有任何验证代码。
- 证据：`ls test | grep -i spatial` → 空；`grep -n "spatial" test/CMakeLists.txt` → 只有 `:266`、`:310` 两条 include 目录（无 `add_executable(*spatial*)`）。
- 影响：CPU 侧改动（例如 D1 的构建重构）没有任何回归网。
- 复现/验证：`grep -n "add_executable" test/CMakeLists.txt | grep -i spatial`（空）。已核对。
- 建议：给 CPU 四件加一个最小 `spatial_test`（BVH 命中数、LOD 边界、队列顺序都是纯 CPU 可断言的）。

### E4. `gpu_hiz.h` / `gpu_hiz_culler.h` 未进构建清单的头文件列表
- 位置：`CMakeLists.txt:419-424`（`XHQM_HEAD_FANTASY` 只列了 `spatial.h`/`culler.h`/`render_queue.h`/`lod.h`/`occlusion.h`/`gpu_culler.h`）
- 现状：两个 Hi-Z 头没被列进 `XHQM_HEAD_FANTASY`（`.cpp` 在 `CMakeLists.txt:471-472` 里是有的；`xhqm.vcxproj` 整个工程不含 spatial 任何文件）。实际编译不受影响——`CMakeLists.txt:85` 的 `include_directories("./fantasy/spatial/")` 让 `test/view/main.cpp:8-9` 的裸 `#include "gpu_hiz.h"` 能解析。
- 证据：`grep -n "gpu_hiz" CMakeLists.txt xhqm.vcxproj` → 只有 `CMakeLists.txt:471`、`:472` 两个 .cpp；`grep -c "spatial" xhqm.vcxproj` → 输出 `0`（退出码 1，即无匹配）。
- 影响：IDE/安装目标里看不到这两个头，接手者容易以为它们是临时文件。
- 复现/验证：`grep -n "gpu_hiz" CMakeLists.txt xhqm.vcxproj`。已核对。
- 建议：补进 `XHQM_HEAD_FANTASY`。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. BVH 用「最长轴 + 中位切分」，不用 SAH、不做增量更新
- 位置：`fantasy/spatial/spatial.cpp:108-127`（选最长轴、按中心 `std::sort`、`mid = size/2` 二分）、`:99`（叶子 ≤ 4）
- 现状与理由：构建是 O(n log n) 的确定过程，实现短到可一眼看完，没有任何启发式参数；对「每帧全量重建」的用法（`scene_culler::build` 就是全量重建）来说，SAH 的构建代价通常反而更贵。当前无增量更新的触发方（没人按需局部更新）。
- 代价：包围盒重叠比 SAH 多、遍历时要访问更多叶子；对象分布极端时（长条 + 聚集）退化。真要做增量更新，得先定义「哪些对象变了」的失效粒度，当前对象树没有这个信号。

### F2. 用中点切分而非「按对象数均分」的压缩式间接绘制
- 位置：`gpu_culler.cpp:72-77`、`gpu_hiz_culler.cpp:119-124`
- 现状与理由：被剔除的对象保留命令槽位、只把 `indexCount` 置 0，这样「SSBO 下标 == 对象下标」的对应关系保持稳定——`read_visible()`（`gpu_hiz_culler.cpp:229`）和上层「按对象查命令」都依赖这个约定，调试与验证也因此简单。
- 代价：indirect 命令数 = 对象数（D5），没有 compact pass；GPU 侧省了顶点/像素，没省命令扫描。

### F3. 遮挡剔除坚持「上一帧深度 + 跨帧复用」的查询式方案
- 位置：`occlusion.h:5-6`、`occlusion.cpp:117-124`（先读上一帧）、`:131`（首帧 `m_has_frame` 为假 ⇒ 全可见）
- 现状与理由：遮挡关系天然是「本帧画完才知道本帧」的问题，用上一帧结果换取「不需要同帧依赖、不需要深度预pass」。对静止或缓慢变化的场景几乎等价，且实现简单（`occlusion.cpp` 只有 166 行）。
- 代价：首帧全绘；遮挡关系突变（镜头旋转、大物体消失）时会短暂过度剔除或过度绘制；配合 D3 的同步读回，还多了一次 GPU 往返。

### F4. Hi-Z 判据用「块内取 max（最远）」的最保守形式
- 位置：`fantasy/spatial/gpu_hiz.cpp:42`（着色器 `float m = max(max(d00,d10), max(d01,d11));`）、`gpu_hiz_culler.cpp:113-115`
- 现状与理由：`max` 下采样保证「块内真实最远深度 ≤ 块值」，于是「对象最近深度 > 块值 ⇒ 必被遮挡」是**保守正确**的（不会误剔）。同时它让金字塔对单点采样也安全（相邻四块一起取 max 只是更保守）。
- 代价：判定精度低，部分被遮挡的对象会因为 block 里有一个近像素而被判可见（漏剔多、误剔零）。这与 Hi-Z 的初衷（用最大深度做保守剔除）一致，属有意的正确性优先。

### F5. CPU 视锥剔除在 AABB 粒度上保守判定（相交即保留）
- 位置：`spatial.cpp:64-82`（8 顶点只要有一个在平面正半空间就算「未完全在背面」）、`culler.h:25`（注释「返回视锥内（含相交）的 object」）
- 现状与理由：对象粒度是 AABB，精确判定需要三角形级测试；保守保留能保证「不误剔」，这对渲染正确性是硬要求。
- 代价：跨视锥边界的对象总是被提交（走完整管线），剔除率在相机贴地/大物体场景下偏低。

### F6. `query_ray` 返回「命中的叶子里的全部对象」，不返回最近命中
- 位置：`spatial.cpp:146-162`（命中即 `out.push_back`，不排序、不求最小 t）
- 现状与理由：唯一的射线测试是 AABB slab（`spatial.cpp:17-41`），没有三角形级求交；接口语义是「拿到候选集」，由上层决定怎么用（穿透式拾取、区域选择都能直接用）。
- 代价：调用方要自己排序/精化；对「只想要最近物体」的用法多一层工作，且名字容易让人误以为返回精确命中。

### F7. `render_queue::order()` 每次调用都完整复制 + 排序，且 `std::sort` 对同深度对象不稳定
- 位置：`render_queue.cpp:18-41`（`std::vector<item> sorted = m_items;` 后 `std::sort`）
- 现状与理由：刻意把 `order()` 做成 const 查询（不落状态），调用方可以自由决定何时排序/排几次（分透明/不透明两趟都能复用）。代价是 O(n log n) 的复制 + 排序在每次调用时发生。`std::sort` 不稳定：深度完全相同的两个对象，先后顺序在**同一次排序内**不保证与插入顺序一致（多次调用只要输入相同，结果相同，所以不是「每帧抖动」问题）。
- 代价：想要「同深度保持插入顺序」的调用方需要改用 `std::stable_sort` 或在 `item` 里加序号；这样改会让排序略慢，故当前不动。

### F8. 遮挡槽位以对象**地址**为键，不做存活/回收管理
- 位置：`occlusion.cpp:92-99`
- 现状与理由：调用方按指针传候选（`candidates` 就是 `std::vector<std::pair<aabb, object*>>`），直接用指针比较是最省事的实现；对象一旦销毁，地址复用是调用方的责任（引擎的 `object` 本来就用裸指针管理子树）。
- 代价：见 A5/B1——长跑场景里槽只增不减，且没有「对象已死」的显式信号可用。

---

## G. 文档与代码的出入

### G1. 模块 README 自身的「零回读/无同步」与「广度遍历」说法（本轮已订正）
- 位置：`fantasy/spatial/README.md:12`（原「全程无 CPU 回读、无同步」）、`:45`（原「广度遍历对象树」）、`:55`（原遮挡剔除描述）、`:215-217` 原「注意事项」
- 现状：README 已按 r677 复核改写，并在原处保留划掉的旧说法。这里只留索引，不重抄。
- 证据：`fantasy/spatial/README.md` 的首行同步标记「> 最后同步：2026-10-07，对照工作副本 r677。」与各处 `~~…~~ **已作废（2026-10-07 复核 r677）**` 标注。
- 影响：接手者若只看旧副本（例如从 r6xx 检出）仍会读到错的性能叙事。
- 复现/验证：`grep -n "已作废" fantasy/spatial/README.md`。
- 建议：无（已在本轮完成）。

### G2. 根 README 与 fantasy README 仍带旧 spatial 说法（**本模块未改这两个文件**）
- 位置：`README.md:222-224`（「spatial_bvh 裸 new/delete 管理、栈压力；遮挡剔除一帧延迟易漏绘…slot_of 线性查找 O(n²)」「extract_planes 两份重复实现」「gpu_hiz_culler 缺深度缓冲读取/端到端集成」）、`README.md:221`（「GPU 剔除零读回」）、`fantasy/README.md:44` 与 `:287`（「经 `draw_indexed_indirect_storage` 零 CPU 回读消费」）
- 现状：逐条核实结果——**成立**：`slot_of` 线性查找（D2）、`extract_planes` 两份等价实现（`spatial.cpp:44-61` 与 `gpu_culler.cpp:9-18`）、BVH 无 SAH/增量更新（F1）、`gpu_hiz_culler` 缺深度缓冲读取（E1）、缺端到端集成与单测（E2/E3）、GPU 着色器内联且 params 布局手打包（B6）。**部分是误读**：(a)「裸 new/delete 管理」属实但「栈压力」不属实——建树/查询的递归深度是 O(log n)、树遍历走显式栈（`lattice/tree.hpp:94` 注释「深树不爆栈」），真实代价是堆分配（D1）；(b)「零 CPU 回读」见 G1/README 订正——GPU 剔除主路径确实零回读，但遮挡查询每帧同步读回（D3），且 `gpu_culler`/`gpu_hiz*`/`render_queue`/`lod_group`/`scene_culler` 在库内**没有任何调用方**，所以「消费」尚未真正发生（E2）。
- 证据：上述行号 + `grep -rn "scene_culler\|gpu_culler" --include=*.cpp .`（去掉定义/测试后为空）。
- 影响：根文档的「劣势/缺失」是接手者最常读的一段，其中「栈压力」「零读回消费」两处会误导判断。
- 复现/验证：`awk 'NR>=220 && NR<=225' README.md`；`grep -n "零 CPU 回读" fantasy/README.md`；`grep -rn "scene_culler\|gpu_culler" --include=*.cpp --include=*.h . | grep -v fantasy/spatial`（除 `test/view/main.cpp` 的 `gpu_hiz*` 外为空）。
- 建议：由根/fantasy 文档维护者在原处按同一格式标注作废（不在本次授权范围内，故只在此登记）。

### G3. `fantasy/fantasy_bugs.md` 的 spatial 条目已全部标「已修」，与当前代码状态不冲突但需要承接
- 位置：`fantasy/fantasy_bugs.md:76-83`（#31 LOD 过渡锚点、#32 平面提取约定、#35 `hizOffsets[12]` 越界）
- 现状：逐条核对**确认已修**：#31 的 `upper_d` 只在 `else` 分支取上层距离（`lod.cpp:36-40`）；#32 的行提取与列主序一致（`extract_planes` 用 `m(r,c)` 取行，与 `linear.h:63-68` 行主序一致）；#35 着色器端已有 `min(level,11u)`（`gpu_hiz_culler.cpp:99`）、宿主 `i < 12`（`:187`）与 `offsets(12)`（`:185`）。但 #35 只修了数组越界，**参数一致性问题仍在**（A7）。
- 证据：`lod.cpp:36-40`、`spatial.cpp:44-61`、`gpu_hiz_culler.cpp:99/185/187`。
- 影响：看历史台账会以为 Hi-Z 那条已经彻底干净。
- 复现/验证：`awk 'NR>=76 && NR<=84' fantasy/fantasy_bugs.md` 对照上述行号。
- 建议：A7 修完后不必回头改历史台账（本仓惯例：历史台账不搬家、不改名、不回填）。

---

## 本轮核对记录（怎么核的）

- 通读：`fantasy/spatial` 全部 17 个文件（8 组 .h/.cpp + README.md）逐行读完；行数以 `wc -l` 实测（见 README 的目录结构表）。
- 模块可达性：`grep -rn "scene_culler\|occlusion_culler\|spatial_bvh\|gpu_culler\|gpu_hiz\|render_queue\|lod_group" --include=*.h --include=*.cpp .`（排除文档与 `build/`）→ 除模块自身定义外，只有 `test/view/main.cpp` 用到 `gpu_hiz*`。
- 编译性：本模块 8 个头文件可单独通过 C++17 语法检查（无需 GPU 库）：
  ```
  cd /home/xhqm/DeepSeek/xhqm-v2 && printf '#include "fantasy/spatial/spatial.h"\n#include "fantasy/spatial/culler.h"\n#include "fantasy/spatial/occlusion.h"\n#include "fantasy/spatial/lod.h"\n#include "fantasy/spatial/render_queue.h"\n#include "fantasy/spatial/gpu_culler.h"\n#include "fantasy/spatial/gpu_hiz.h"\n#include "fantasy/spatial/gpu_hiz_culler.h"\nint main(){return 0;}\n' | g++ -fsyntax-only -x c++ -std=c++17 -I. -Ifantasy -Ifantasy/backend -Ifantasy/spatial -Ifantasy/lighting -Ifantasy/postfx -Ifantasy/animation -Ifantasy/runtime -Isoftware -Ihardware -Imetaclass -
  ```
  → 退出码 0，无输出（g++ 11.4.0）。
- 未做的验证（诚实标注）：本节点无 GPU/窗口环境，**所有运行期行为（A1/A2/A3/A4/D3/D4）均为静态核对，未实机复现**；`test/view/main.cpp` 的 [HIZCULL] 块处于 `#if 0` 内，本轮**未运行**该诊断程序。
