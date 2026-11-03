# fantasy 模块静态分析 · 潜在 bug 清单

> 说明：仅静态分析，未运行（fantasy 依赖视觉/图形上下文，本节点无渲染能力）。
> 按文件分组，标注严重度：[高]=崩溃/越界/数据损坏，[中]=逻辑错误/渲染异常，[低]=边界/风格/隐患。
>
> 更新（2026-08-22 第二轮复核）：以 SVN 工作副本源码为基准（当前 r169）逐条核对，
> r170 一批（#14-#17/#19/#28-#33/#35/#36 及 #5/#8、gltf 若干）确认已修并回填；
> 本轮再修 #7/#9/#18/#20/#23/#24，并经 RenderWidget/RhiViewport 双后端实机渲染验收。
> 剩余仅 #21/#22 两项为「潜在耦合、当前无实际缺陷、需描述符/UBO 布局重构」的待定项。

## colors.h / colors.cpp

1. ✅ **[低] colors.h:33 零宽字符** — `fuchsia` 枚举值前混入零宽空格 U+200B，视觉不可见但污染标识符（已修，全文件 U+200B=0）。
2. ✅ **[中] colors.cpp `color::assignment` 越界** — `value = min(value, 1.f)` 后 `value * colors.size()`，当 `value == 1.0f` 时结果恰为 `size`，`ind1 = size` 越界访问 `colors[ind1]`。应钳到 `size - ε` 或改用 `min(size-1)`（已修，ind1 钳到 size-1）。
3. ✅ **[中] colors.cpp `rgba2cmyk` 除零** — `output = 1.f - (rgba / (1.f - output[3]))`，当 K==1（纯黑）时分母为 0，产生 NaN（已修，判空 return）。
4. ✅ **[低] colors.cpp `rgba2hlsa` 除零边界** — 饱和度 `delta / (1 - |2L-1|)`，当 L 趋近 0 或 1 时分母趋近 0；delta 阈值 1e-6 大多能拦住，但浮点边界下仍可能爆大值（已修，分母近 0 置 0）。

## blurry.h / blurry.cpp

5. ✅ **[中] blurry.h `m_name`/`m_info` 同源** — 两字段用同一个 `uuid` 变量做两次 `to_text`，名字与说明永远相等（已修，各自独立 `creat_uuid_v4()`）。
6. ✅ **[低] blurry.h 变量遮蔽类型** — 局部 `uuid` 变量遮蔽类型 `cipher::uuid`（已修）。

## element.h / element.cpp

7. ✅ **[中] element.h `get_scale()` 返回 `m_texture`** — 语义错位（scale 名返回 texture）。已统一重命名为 `get_texture()/set_texture()`（含 poits/lines/ploys/curve）；无外部调用点。
8. ✅ **[低] element.h `curve`/`nurbs` 未用成员** — `m_shape`/`m_count` 未用，已修（在 updata/render 使用）。
9. ✅ **[低] element.h `nurbs` 用 `get_image/set_image`** — 与其它类 `get_scale/set_scale` 不一致。已统一为 `get_texture()/set_texture()`。
10. ✅ **[高] element.cpp `curve::updata` 空集合 UB** — 第 222 行直接 `m_nurbs.nurbs_p().front()` 未判空（`nurbs::updata` 有判空，`curve` 没有），空 NURBS 点列触发未定义行为（已修，empty return）。

## backend/backend.h · rhi.h · rhi.cpp

11. ✅ **[低] rhi.cpp `render_backend_select(none)` 无效** — 选 `backend_kind::none` 时 `target` 保持 nullptr，`active_ptr()` 不变，无法切回桩后端（已修）。

## backend/rhi_gl.cpp

12. ✅ **[中] `bind_texture` 恒绑 GL_TEXTURE_2D** — 立方体深度纹理（点光阴影 `offscreen_cubemap_texture`）经 `bind_texture` 会按 2D 纹理绑定，采样错误。2D 阴影图不受影响（已修，cubemap 单独绑定）。
13. ✅ **[低] `draw_instanced`/`draw_indexed_instanced` 不计 draw call** — 与 `draw`/`draw_indexed` 不一致，`draw_call_count()` 统计漏报实例化绘制（已修）。

## backend/rhi_vk.cpp

14. ✅ **[高] 管线缓存键位重叠** — `layout_hash ^ (topo<<43) ^ (color_write?0:1<<43) ^ (stencil?1<<44) ^ (stencil_write?1<<45)`。`topo` 取值 0/1/3/5 占用 bit 43–45，与 color_write(bit43)、stencil(bit44)、stencil_write(bit45) 直接重叠，不同渲染状态可映射到同一管线键，复用错误管线（已修：topo 移到 <<52，并并入 `active_render_pass()`）。
15. ✅ **[中] `begin_frame`/`end_offscreen` 回 swapchain 时 clearValueCount=2** — MSAA（3 附件：多采样颜色 + resolve + 深度）时深度附件在索引 2，只给 2 个 clear value，深度附件未正确清除（Vulkan 校验层会报）（已修：改用变量 clear_count，MSAA 下含深度）。
16. ✅ **[高] `create_pipeline` 恒绑 swapchain `m_render_pass`** — 所有管线绑定到 swapchain render pass，离屏（MRT/HDR/深度专用/cubemap）render pass 附件格式与数量不同，管线不兼容，Vulkan 离屏渲染（HDR 后处理/阴影/拾取）会失败（已修：离屏管线绑 `o.render_pass`）。
17. ✅ **[低] `bind_texture` 允许 unit=9 → binding=10 越界** — 描述符布局仅 binding 1–9，`unit >= 10` 的护栏应为 `unit >= 9`（已修：`unit >= 9`）。
18. ✅ **[中] `find_memory_type` 失败返回 `~0u`** — 作为 `memoryTypeIndex` 直接传给 `vkAllocateMemory`，分配失败/校验报错而非优雅报错（已修：严格匹配→放宽回退首个候选→0 兜底，不再返回 ~0u）。
19. ✅ **[中] 着色器声明与描述符布局不匹配** — `shader_preprocess` 里 `XHQM_SHADOW_MAP` 展开为 `sampler2DShadow shadowMap[4]`（binding 4 的 4 元素数组），但 `build_program` 描述符布局每个采样器 binding `descriptorCount=1`，CSM + Vulkan 路径存在绑定语义错配（已修：binding 4 count=4，显式 unit→(binding,element)）。

## backend/shader.cpp · shader_preprocess.cpp · ubo.h

20. ✅ **[低] shader.cpp `shader_cache::acquire` 空 cache_dir** — 缓存文件写当前工作目录，路径不可控（已修：cache_dir 为空时禁用持久化缓存，走内存编译）。
21. ⚠️ **[低] shader_preprocess.cpp 绑定冲突** — `XHQM_TEX0`、`XHQM_SPRITE_TEX`、`XHQM_COLOR_TEX` 都映射 binding=1（跨 shader 使用，当前各 shader 独立故未爆，但共享描述符布局时语义耦合）。**待定**：三者分处独立 shader（主 shader 用 TEX0/1/2 占 1/2/3，sprite/color 各自单采样器 binding 1），当前无实际缺陷；改需重排描述符布局并改 sprite/color 绑定路径，无法用现有 demo 验证，留待更大重构。
22. ⚠️ **[低] ubo.h `ubo_offset("params")` 与 `"tranMatrix"` 共用 UBO_TRANSFORM(0)** — postfx 的小 UBO `{vec4 params}` 恰好也在 0 偏移，靠巧合工作，别名歧义。**待定**：params 仅 postfx 用、tranMatrix 仅 mesh 用，无单 shader 同用，当前无实际缺陷；改需在 21824 字节 UBO 内另辟 offset 并改 postfx 绑定，留待更大重构。

## runtime/command_recorder.cpp

23. ✅ **[低] `start()` 无锁写 `m_stop/m_running`** — 与 worker 的锁内读存在数据竞争（通常单线程调用，隐患）。已修：`m_running`/`m_stop` 改 `std::atomic<bool>`，start/stop/record 对状态字段补锁。
24. ✅ **[低] `record()` 未防并发** — `m_remaining` 被整批重置，若两个 `record` 并发会互相覆盖计数。已修：改为累计 `m_remaining += tasks.size()`，并发 record 不再互相覆盖，done wait 谓词 `m_remaining==0` 语义正确。

## runtime/gltf_loader.cpp

25. ✅ **[低] `make_accessor` 只校验起始偏移** — `off >= buf.size()` 未校验 `off + count*stride`，截断 accessor 会越界读（已修）。
26. ✅ **[低] `read_index` 对 `data==nullptr`（稀疏 accessor）无护栏** — `read_float` 已加 `if(!v.data) return 0`，但 `read_index` 未加，`v.data + i*stride` 空指针算术 UB（索引 accessor 一般不稀疏，隐患）（已修）。
37. ✅ **[低] signed normalized 未钳位** — `read_float` 对 `int8` 用 `value/127`、`int16` 用 `value/32767`，-128 会得 -1.0078，未按 glTF 规范钳到 [-1,1]（已修）。

## runtime/scene_io.cpp

27. ✅ **[低] `scene_load` 失败时泄漏已分配 prefab** — `meshes.empty()` 返回 false 前未释放已 new 的对象（已修：全部校验守卫前移，唯一失败点在 alloc 之前）。
36. ✅ **[中] `scene_load` 无数组尺寸校验** — `pos[t*9+...]`、`norm[i*3+...]`、`uv[i*2+...]` 等对畸形 JSON 直接按索引访问，nlohmann 越界抛异常（未捕获）会崩溃调用方（已修：先校验 positions/normals/uvs/colours/tangents 尺寸再读）。

## animation/animation.cpp

28. ✅ **[中] `set_channel` 未校验 rotations/scales 尺寸** — 只校验 positions 与 times 一致，rotations/scales 尺寸错会传到 `sample()` 越界（已修：rotations/scales resize 对齐）。
29. ✅ **[高] `sample()` 单关键帧 UB** — `ts.size()==1` 时 `idx = ts.size() - 2 = -1`，`positions[-1]` 未定义行为（已修：三元钳到 0）。
30. ✅ **[中] `animation_blender::blend` 丢失缩放** — 用四元数 slerp 只混合旋转，平移线性插值，但缩放分量完全丢弃（已修：去缩放取纯旋转 slerp，lerp 混合缩放乘回）。

## spatial/lod.cpp

31. ✅ **[中] `select_blend` 过渡窗口锚点错误** — `upper_d` 在 `distance >= lv.distance` 分支里更新，实际记录的是「lower 层」的距离，而 cross-fade 应以「upper 层」的切换距离为锚点（已修：upper_d 仅在 else 分支取 upper 层距离）。

## spatial/spatial.cpp · gpu_culler.cpp

32. ✅ **[低] 视锥平面提取依赖矩阵约定** — `extract_planes`/`extract_frustum_planes` 用行提取（Gribb-Hartmann 列向量约定），与引擎列主序一致。**已核验无需改动**（行提取与列主序 M*v 约定一致）。
35. ✅ **[中] gpu_hiz_culler.cpp `hizOffsets[12]` 越界** — 着色器 `uint hizOffsets[12]` 固定 12 级，`hiz_levels > 12`（8K 及以上）时 `hizOffsets[level]` 越界读（已修：着色器端 `level=min(level,11u)` 与 C++ `i<12`、offsets[12] 两端对齐）。

## drawer.cpp

33. ✅ **[中] `_texture::reset(vector<vector<rgba>>)` 宽高互换** — `w_size = image.size()`（行数）当宽度、`h_size = image[0].size()`（列数）当高度，非正方形纹理被转置上传（已修：h_size=行数、w_size=列数）。

## system.cpp

34. ✅ **[低] `scenes::~scenes` 手动 delete 子节点** — `recursion_dfs` 手动删除 m_theory/m_target/m_camera，若 `object` 析构也释放子节点则双重释放（依赖 object 析构语义，需确认）。**已核验无双重释放**：object::~object 仅置 m_object=0，tree 基类析构不释放 childs 指针所指对象，每个子节点只 delete 一次。

---

## 说明

- 非 fantasy 模块测试已全部完成（system/structure/algebra/network/metaclass/hardware，r148–r153）。
- 本清单未覆盖：着色器源码正确性（shader_sources.h 的 GLSL 语义）、Vulkan 运行时行为（无设备可验）、`_gl_.h`（GL 函数加载）。
- **当前剩余未修：仅 #21、#22**（潜在耦合/别名，均经源码核验为「当前无实际缺陷」，改需描述符/UBO 布局重构）。
- 建议后续：若推进描述符共享或 postfx 重构，一并解决 #21/#22；当前动它们风险大于收益。

---

## P0 正确性修复（2026-08-24 第三轮，r196 大更新后）

按「GL/Vulkan 行为不对称 + Vulkan 正确性」优先修复 5 项，双 demo（RenderWidget GL / RhiViewport Vulkan）回归无回归：

38. ✅ **[中] cubeShadow(binding5) Vulkan bind_texture 死线** — 描述符布局已声明 binding5(cubeShadow)，但 unit→binding 映射（0..2→1..3、3..6→binding4 数组、7/8→8/9）无 unit 到 binding5，点光 cubemap 阴影在 Vulkan 不可采样。已修：护栏放宽到 `unit>=10`，加 `unit==9 → binding5(cubeShadow)`。
39. ✅ **[高] UBO 跨帧主机数据竞态** — `vkCmdUpdateBuffer` 在命令执行期读共享 slot0 主机指针，begin_frame 只等当前帧 fence，跨帧 1 帧窗口竞态。已修：改为记录期 `memcpy` 直写 host-coherent 映射槽位（各 draw 独立槽，GPU 读槽 N 而非 slot0），消除执行期读共享区。
40. ✅ **[中] clear() GL/Vulkan 不一致** — Vulkan `clear()` 空实现 + render pass 清除色硬编码 {0.05,0.05,0.1,1}，GL 尊重 rgba。已修：加 `m_clear_color` 成员，`clear(rgba)` 按 BGR(A) 字节序记录，render pass clearValue 改用该颜色。
41. ✅ **[中] 阴影深度参考 [-1,1] 对 [0,1] 错配** — CSM 采样 `proj.z`(NDC [-1,1]) 对比深度纹理存的 [0,1]（GL 硬件/Vulkan XHQM_DEPTH_REMAP 均归一化到 [0,1]），系统性深度偏差。已修：采样参考改 `0.5*proj.z+0.5 - bias`。
42. ✅ **[中] compute 在 render pass 内 vkCmdDispatch** — `compute_dispatch` 在 `m_frame_active` 时录进当前命令缓冲，但此刻 render pass 仍开（begin_frame 已 BeginRenderPass），在 render pass 内 dispatch 校验层报错。已修：加 `m_render_pass_open` 标志跟踪所有 Begin/EndRenderPass，pass 打开时 compute 改走 one_shot（同步但正确）；异步 compute-before-draw 重组留待 P1 接线。

---

## P1 阴影接线（2026-08-24，未提交/部分完成）

对 fantasy 组件库做首轮「默认渲染管线接线」，方向光阴影优先。改动（全部在 engine，demo 不改引擎）：

- `property.h`：`theory_camera_render` 增加 `shadow_map m_shadow`（单方向光，紧贴场景）+ `bool m_shadow_wired`；include `shadow.h`。
- `shadow.h`：`matrix m_light_vp` 默认初值 `matrix(4,4)`（原裸成员依赖 matrix 无参构造，导致 `shadow_map` 默认构造被删 C2280；此前 `cascaded_shadow_map` 用 vector 型成员所以没触发）。
- `property.cpp` `theory_camera_render::life()`：HDR 主渲染前插入 —— 收集场景第一个方向光 → 广度遍历各模型 `updata_aabb().access(system::transform(root,m))` 合并场景世界 AABB → `shadow_map::setup(2048,2048)` → `compute_light_view_proj(dir, scene_box)` 得紧贴光 VP → `m_shadow.begin()/traverse(lightVP)/models::drawer()/end()` 生成深度图 → `apply_shadow_state` 写每个 drawer 的 `cascade_count=1/shadow_texs[0]/light_vps[0]/cascade_splits[0]/camera_pos/lights([sun])`。

**已验证（RhiViewport Vulkan，--snapshot，多轮逐步排查）**：
- 编译链接通过（xhqm.dll r18:39），运行无崩溃。heap 崩溃 `0xc0000374` 根因是 **RhiViewport.exe 未随新 `property.h` 重编的 ABI sizeof 错配**（`new theory_camera_render` 按旧 sizeof 分配、新 DLL 写越界），重编 RhiViewport 后消失。
- 阴影态正确引导（调试期打印）：`[CSM] setup ready=1`、`sun_dir=(0,-0.6,-0.8)`、`drawer cascade=4 tex0=15 vps=4 split0=19.98`；改单级后 `cascade_count=1`。
- **RHI 方向光 2D 阴影采样路径正常**：新增 `[CSM]` 自检（sampler2DShadow shadowMap[0]，深度专用离屏填 0.9、比较 0.95 → 遮蔽），`[CSM] 2D shadow sample=0.00 ok=1`。
- **阴影图生成正常**：`XHQM_LIGHTVIEW` 调试把光视角场景渲染到 HDR，级联3（或紧贴 shadow_map）的光视角里**能看到地面/盒/角色/坐标轴** —— 场景几何确实写进了阴影深度图。
- P0 自检全绿（[HIZ][HIZCULL][BINDLESS][SSAO][TAA][SSR][PCF] ok=1），无回归。

**未完成**：`--snapshot` 出图（角色/红色遮挡盒/石块/地面正常）但**始终无可见阴影**。已逐项排除：
- 深度专用 render pass `finalLayout` 已是 `SHADER_READ_ONLY_OPTIMAL`（rhi_vk.cpp:2522）→ 布局无问题（曾误判为布局不匹配，撤销了 end_offscreen 迁移 barrier）。
- 深度比较采样器已开（`sd.compare=true`, rhi_vk.cpp:2766）；深度写开（`m_depth=true`, rhi_vk.cpp:3239）。
- 「带颜色输出的 shader 能否写深度进深度专用离屏」：`[CSM]` 深度 shader 带 `out vec4` 后仍 `ok=1` → 能写深度（排除管线兼容性）。
- 矩阵约定（行主序 `operator()(i,j)=data[i*cols+j]`、标准 `*`、标准 `tramat`）：推导得 GLSL `transform`=引擎矩阵本身、`transform*localPos=M*localPos`，故 depth=`lightVP*worldPos`、采样=`ubo.lightVP*fragWorldPos=lightVP*worldPos`，两者**一致**（排除矩阵错配）。
- 级联覆盖/分辨率：改紧贴场景的 shadow_map 后仍无阴影 → 排除「远级联太粗」。

**结论**：接线逻辑与 RHI 管线在此会话可验证层面全部正确，阴影仍不可见指向 GPU 采样/深度值/光 VP 的**隐蔽一致性**问题（需着色器回读或 Vulkan 校验层才能定位）。demo 接收面已开光（`make_quad`->`open_light(true)`）并统一光源。**建议下一步**：用最小阴影自检（单独 lit 地面+遮挡盒+方向光，逐像素读 `[CSM]`），或开 Vulkan 校验层查阴影 pass 是否有隐藏校验错误；留意 `prefab::render` 采样用 `uniform_lights`+`uv` 与 `XHQM_SHADOW_MAP` 绑定，验证 shader 里 `ubo.lightVP*cascadeSplits/cameraPos` 实际值。

43. ⚠️ **[高] 方向光阴影在现代管线内不可见（引擎 CSM/shadow 采样未产生遮挡）** — 接线、状态、RHI 采样、阴影图生成均验证正常，但快照无阴影。需 GPU 级调试定位（着色器回读 / Vulkan validation layer）。候选深挖：`prefab_frag` CSM 块的 `ubo.lightVP[cascade] * fragWorldPos` 与深度图实际存储深度是否严格对齐（含 NDC→[0,1] remap 与 ortho near/far）、`flags[11/12]` 是否真到了 shader、`bind_texture(3+c, shadow_tex)` 是否绑定到当前 program 的正确描述符。

---

## P1 稳定性回归（2026-08-24，用户实测）

用户实测：RhiViewport 出现「乱闪橙色三角形 + 有阴影 + 运行一段时间后崩溃」，RenderWidget 直接崩溃。排查结论：

- **RenderWidgetTest.exe `0xc0000374`（堆损坏）= 旧构建**：`RenderWidgetTest.exe`(17:22) 未随新 `property.h`(18:38) 重编，`new theory_camera_render` 按旧 sizeof 分配而新 DLL 写 `m_shadow` → 堆越界。**已修**：重编 RenderWidgetTest.exe(18:50)。根因是 demo 不跟踪 `property.h` 头文件依赖（ninja 依赖图缺 header 边），改头文件后不重编 → ABI sizeof 错配；**每次改 `property.h` 都须强改所有 demo**。
- **RhiViewport TEST.exe `0xc0000005`（nvoglv64.dll GL 驱动）+ 乱闪**：RhiViewport 显式选 Vulkan，但交互运行一段时间后崩在 GL 驱动。`--snapshot`（~9帧退出）不复现，该崩溃是**逐帧累积**问题 —— 阴影 pass 每帧 `traverse(lightVP)+models::drawer+离屏 begin/end`，长时间帧循环下累积资源/状态问题（或阴影 acne 触发驱动）。**已采取**：给阴影接线加 `XHQM_SHADOW=1` 环境变量开关，**默认关**（无阴影 pass → 稳定基线，实测快照正常）；开启才跑阴影。阴影功能本身正确（开启时阴影可见、`[CSM] ok=1`），但逐帧集成稳定性需后续 pass（减少逐帧离屏 begin/end、消除 acne、或按需渲染阴影图而非每帧）。

**注意**：demo 的 `make_quad` 已改 `open_light(true)`（供阴影接收面），`RhiViewport main.cpp` 加了 `[CSM]` 自检与测试遮挡盒（已移除）。这些 demo 改动均在本地项目（不入库）。

## P1 阴影实测 + 蒙皮 NaN 修复（2026-08-24）

后续实测（RhiViewport 交互截图）：
- **阴影生效**：截图 1 地面右下有暗色阴影（角色/石块投影），方向光阴影机制工作正常。
- **巨大橙色三角形乱闪 + 运行后崩溃**：截图 2/3 显示一根巨大橙/黄三角形从角色处延伸覆盖屏幕。根因**与阴影无关**，是**动画角色蒙皮**问题：`ModelLoader.h::sample_palette` 用 Assimp `aiQuaternion::Interpolate`（四元数球插），两帧四元数反平行/退化或缩放异常时插值出 NaN/Inf/超大骨骼矩阵 → 蒙皮顶点被拉到屏幕外（巨大三角形）；且阴影 pass 每帧双渲染把此 NaN 写进阴影深度图 → 驱动崩溃。
- **已修（防御）**：`sample_palette` 检测每个骨骼矩阵元素 `std::isfinite` 且 `|v|<=1e6f`，异常时该骨骼退回单位阵，阻断拉伸三角形与阴影图 NaN → 崩溃。
- **清理**：移除调试红盒（遮挡测试）与 `[CSM]` 自检块（怀疑残留深度纹理绑定）；`RenderWidgetTest.exe` 随新 `property.h` 重编（修旧构建堆损坏）。

**结论**：方向光阴影接入正确且已见成效（阴影可见）；剩余稳定性问题（巨大三角形/崩溃）源于动画蒙皮 NaN，已在 demo 侧加防御；引擎侧阴影 pass 逐帧离屏 begin/end 的长期稳定性仍建议后续 pass（按需/降频渲染阴影图）。

---

## Vulkan teardown 崩溃 + 闪烁根因修复（2026-08-24，已提交）

RhiViewport 退出崩溃与偶发橙色闪烁，两根因 + 若干加固：

- **44. ✅ [高] 静态析构顺序崩溃（xhqm.dll+0x1155A2）** — vk_backend 为函数级静态（main 中构造→先析构），`sprite::_shader` 等 DLL 加载期 inline 静态晚析构，其 `reset()` 调 `shader_delete` 打在已销毁后端的悬空 map 上。已修：`rhi.h/rhi.cpp` 新增 `render_backend_retire(self)`，后端析构末尾把 active 指针退役到 none 桩；GL 后端同构待加固。
- **45. ✅ [高] vkDestroyDevice 驱动内部崩溃（EIP=0，NVIDIA 32.0.15.9186）** — 应用侧资源销毁顺序经逐项核对（离屏/立方体目标先于内存池销毁、专用内存补释放、destroy_textures 按 owns_image 保护、销毁前 vkDeviceWaitIdle），崩溃仍在驱动内部；二分证实属驱动缺陷。已修：析构默认跳过 vkDestroyDevice/vkDestroyInstance（进程退出时 OS 回收 GPU 资源，引擎界标准退出策略），`XHQM_FULL_DEVICE_DESTROY=1` 可强制完整销毁用于调试。
- **46. ✅ [高] 描述符集跨帧竞态（橙色单帧闪烁根因）** — begin_frame 只等 fence[N-ic]，上一帧提交可能仍在 GPU 执行，而录制期 `vkUpdateDescriptorSets` 改写**同一份**描述符集 = 规范未定义行为 → 偶发单帧采样错乱（屏幕实拍天空变双骨棒面色 #BA8F2F；读回永远干净；GPU 串行化后闪消失）。已修：每 program 8 份描述符集按帧轮转（`MAX_DESC_SETS=8`），binding 0 UBO 创建时一次性写入全部份，draw 用 dynamicOffset 区分数据槽。修复后多轮连拍 0 闪。
- **47. ✅ [低] destroy_textures 曾无条件销毁 owns_image=false 的离屏注册纹理 view/image** — 与 delete_offscreen 构成二次销毁。已修：按 owns_image 保护。

**构建链**：完整 Debug 链已打通——debug glew（depend/glew.zip）+ 自编译 debug glslang/SPIRV-Tools（glslang 16.5.0 + SPIRV-Tools b707790 + SPIRV-Headers 29981f6，源码在 `depend/vulkan/glslang_src`，产物 `depend/vulkan/glslang/lib/Debug`）；引擎 CMakeLists 按 `CMAKE_BUILD_TYPE` 自动切换 Debug/Release 依赖路径；Release 链接也生成 PDB。注意：demo 与引擎必须同为 Debug 或同为 Release（MSVC 调试 STL std::vector 32B vs 24B，跨 DLL 传容器会读到垃圾尺寸）。

---

## 阴影不可见根因定位 + 修复（2026-08-25，重接线后）

重接线后 `--snapshot` 始终无可见阴影。逐项核对并修复：

- **48. ✅ [高] 阴影图采样 v 轴上下镜像** — 引擎离屏（含深度专用）一律用 Y 翻转视口（`vp.y=o.h; vp.height=-o.h`，引擎 `read_offscreen` 注释「图像存储为左上角原点」自证约定），NDC +y 写入图像 v=0；CSM 采样 `shadowUV = proj.xy*0.5+0.5` 却把 NDC +y 映射到 v=1 → 阴影图被垂直镜像读取。角色/石块居图心时镜像阴影错位、甚至藏到物体身下。已修：`shader_sources.h` CSM 采样前 `shadowUV.y = 1.0 - shadowUV.y`。修复后可视化实测阴影图案（角色/石块投影暗斑）清晰可见。注意：`fantasy-back` 版本无此翻转也能见阴影（当年实测场景物体不在图心，镜像阴影仍在可见位置）——不代表该写法正确。
- **49. ✅ [中] 深度专用 render pass 缺 external subpass 依赖** — 颜色离屏 render pass 有显式依赖（作者注释「避免采样到未完成/黑的内容」），深度专用分支创建时漏加：深度写（LATE_FRAGMENT_TESTS / DEPTH_STENCIL_ATTACHMENT_WRITE）到主 pass 片元采样（FRAGMENT_SHADER / SHADER_READ）的同步链无保障，布局转 SHADER_READ_ONLY 与采样间存在规范未定义窗口。已修：按深度语义补两条依赖（external→0：FRAGMENT_SHADER/SHADER_READ → EARLY_FRAGMENT_TESTS/DEPTH_WRITE；0→external：LATE_FRAGMENT_TESTS/DEPTH_WRITE → FRAGMENT_SHADER/SHADER_READ），`BY_REGION`。
- **矩阵约定一锤定音**（algebra/geometry/linear.h）：`translate` 平移写入 **column 3**（列向量约定 M·v，`trans(0,3)=tx` 等）；`matx*matx` 标准数学积；`vect*matx` 行向量、`matx*vect` 列向量两套都支持；`ortho` 标准 GL 公式（near→NDC-1/far→NDC+1，可见深度负 z）；上传 GLSL std140 前必须 `tramat()`（引擎行主序元素 (i,j)=data[i*cols+j]，GLSL 列主序），drawer 的 transform/bones/lightVP 路径均正确 tramat。`shadow_map::compute_light_view_proj`（lookAt 行写入 + proj*view + near=0/far=2r）与采样侧 `0.5*proj.z+0.5`、存储侧 `gl_Position.z=0.5*(z+w)`（XHQM_DEPTH_REMAP）三方一致，数学无错。
- **「部分暗化非全黑」不是 bug**：片元着色器光照循环后叠加 `ubo.ambientColor` 环境光，阴影只消方向光项，阴影区亮度 = 环境光兜底（实测暗化约 30-45/255）。判断阴影是否生效勿用「是否全黑」作标准。

**验证**：临时可视化（`radiance=vec3(shadow)*10` + 关环境光）快照中阴影图案清晰：角色/石块投影为地板上的成组暗斑，位置与几何吻合；A/B 差分（`XHQM_NOSHADOW` 临时开关）确认暗化全部来自阴影采样路径。诊断打印与开关已全部清理。

---

## 双后端阴影收尾 + 闪烁复发修复（2026-08-25，r200 后）

r200 后用户实测：RenderWidget(GL) 无阴影、RhiViewport(Vulkan) 阴影正常但闪烁复发（同 r199 前症状）。逐项定位：

- **50. ✅ [高] GL 后端深度专用离屏三个缺口**（RenderWidget 无阴影根因）：
  1) `begin_offscreen` 不设视口——2048² 阴影 FBO 沿用窗口视口渲染，深度只写进 820² 角落，采样全命中清空值 far=1.0 → 全部判为被照亮；已修：offscreen_gl 记录 w/h，begin_offscreen 按离屏尺寸 glViewport，end_offscreen 恢复原视口。
  2) 深度图从不清理——Vulkan 有 loadOp=CLEAR，GL 无等价物，深度图为陈旧/未定义内容；已修：depth_only 离屏 begin 时 glClearDepth(1.0)+glClear(GL_DEPTH_BUFFER_BIT)。
  3) 采样 v 翻转误伤 GL——#48 的 `shadowUV.y=1-v` 只对 Vulkan 翻转视口成立，GL 视口不翻转（NDC -y 在纹理 v=0，采样天然对齐），GL 上会把阴影镜像（物体居图心时藏到身下）。已修：改为预处理宏 `XHQM_SHADOW_UV_FLIP`（VK=翻转 / GL=空），shader_preprocess 展开。
- **51. ✅ [高] 帧同步链两处规范级缺陷**（RhiViewport 闪烁复发）：
  1) vkQueueSubmit 提交前 fence 未复位——begin_frame 的 vkWaitForFences 返回后 fence 处于已信号态，随后 vkQueueSubmit 用已信号 fence 提交属规范未定义（VUID-vkQueueSubmit-fence-00064）；驱动若不复位则后续等待立即返回、帧间同步全失 → 描述符集/命令缓冲/UBO 槽全部竞态 → 橙色乱闪（与 r199 前同症状）。已修：等待后立即 `vkResetFences`。
  2) `m_image_avail`/`m_render_done` 单份信号量跨帧复用——fence 只覆盖 submit 完成、不覆盖 present；第 F 帧 submit 重新 signal `m_render_done` 时，第 F-1 帧的 present 可能仍在等同一信号量（二值信号量重复 signal = 未定义行为）→ 显示引擎侧错乱/闪烁。已修：改为按帧信号量对 `m_image_avails[ic]`/`m_render_dones[ic]`（与 fence 同轮转，acquire/submit/present 均用 `m_frame_index` 索引）。两项均为规范级缺陷，阴影 pass 增加的绘制量改变时序后从「驱动容忍」翻转为可见，必须修。
- **demo 侧（不入库）**：RenderWidget 的 `make_quad` 地面 `open_light(false)` → `true`（与 RhiViewport 一致，否则地面不参与光照自然无阴影接收）；RenderWidget 的 gizmo 未设 `shadow(false)`（与 RhiViewport 不一致，会让阴影包围盒被 gizmo 撑大，建议补上）。

**验证**：GL 端阴影 pass 运行正常（16 次深度绘制），快照局部差分 827 像素暗化（Δ≈37，ambient 兜底）；Vulkan 端快照回归阴影仍在（与无阴影基线差分 10.5%，动画帧不同故数值有差）。闪烁为交互期竞态，快照无法复现，需用户交互确认修复后是否消除。

---

## UBO 环形槽 slot0 别名 + 环长不足（2026-08-25）

r201 修复后用户反馈：静置不动也持续闪烁。用 `--still`（冻结动画）+ `--dumpframes` 逐帧读回 swapchain 二分定位——冻结动画后帧间仍呈周期振荡，且逐像素对比发现**单个 draw 的几何在相邻帧间跳变**：地面平面偶发以错误矩阵渲染（覆盖全屏 / 出现在角色位置 / 移位），天空与角色像素不变。周期、孤立、内容为「别的 draw 的矩阵」→ 锁定 UBO 环形槽。

- **52. ✅ [高] UBO 环形槽 slot0 别名（单帧几何跳变根因）** — `uniform_*` 全部写入 `p.ubo_data`（slot0，CPU 暂存区），draw 记录期把 slot0 memcpy 到环形槽并用 dynamicOffset 指向该槽；但 `slot = p.ubo_slot++ % UBO_SLOTS` 会命中 **slot0 本身**。slot0 是暂存区，后续每个 draw 的 uniform 写入都在覆写它——GPU 执行到 slot0 那个 draw 时，读到的已是后面 draw（甚至下一帧）的矩阵/灯光/纹理索引 → 该 draw 以别人的数据渲染（地面变角色位、角色贴地面色、整屏被错误矩阵平面覆盖）。周期 = 每 128 次同 program draw 一次（≈每 5~7 帧），与 dump 观察到的事件节奏吻合。已修：slot0 保留为暂存区，环形槽从 1 起分配（`slot = 1 + (p.ubo_slot++ % (UBO_SLOTS - 1))`）。
- **53. ✅ [中] 环形槽数不足的潜在跨帧竞态** — CPU 可领先 GPU 一帧录制（双 fence），若单帧同 program draw 数超过环长一半，录制 N+1 帧会覆写 N 帧仍在读的槽。128 槽仅支持 64 draw/帧，边缘场景即破。已修：`UBO_SLOTS` 128→256（255 环形槽，支持 ≤127 draw/帧），并在 begin_frame 记录各 program 起始计数（`ubo_frame_start`）供后续检测。

**验证**：`--still` + 阴影开 + `--dumpframes` 500 帧逐像素比对——亮度差 0、零差异像素（修复前同条件下每 2~5 帧即出现单 draw 跳变）。阴影路径（17 深度 draw）与主路径共用同一修复；slot0 别名同样解释 r201 前阴影单帧闪变（深度 pass 的 draw 命中 slot0 时阴影图错乱一帧）。
