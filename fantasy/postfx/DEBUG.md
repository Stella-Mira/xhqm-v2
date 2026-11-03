# xhqm · fantasy/postfx · 当前问题清单

`fantasy/postfx`（module id `fantasy/postfx`）是七个全屏后处理 pass 的集合（bloom / tonemap / fxaa / ssao / taa / ssr / compose，7 头 + 7 实现 = 1420 行）。本文件只记**当前仍存在**的问题，每条带 `文件:行号` 证据。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。

**与同类文件的关系**：本仓的历史台账**不搬家、不改名**。
- `fantasy/DEBUG.md`（父台账，模块级）：已记有 **6 条**与本模块直接相关的条目 —— A3（调用方 `&&` 短路）、B6（`"scene"` 句柄被 0 覆盖致整链停摆）、B7（`params` UBO 别名）、C1（Vulkan `viewport()` 空实现影响分屏 present）、D1（Bloom 单级模糊）、E5（`taa_jitter` 无人调用）。本文件**不重抄**，只在 **G6/G7** 指过去，并把模块内部视角的补充写在那两条里。引用一律用「组号 + 条目标题」，不用行号 —— 父台账正在被并发维护，行号会漂移（本轮读到的版本 57881 字节 / md5 `ee1cd182575738852a8b9502751a13e9`）。
- `fantasy/fantasy_bugs.md`（215 行）：本模块的历史 bug 编号总账，编号 #22（postfx `params` 别名）仍为「待定」，本文件只在 G6 指向它。
- 根 `DEBUG.md`（全仓总账）：其 `:258` 曾把本模块的三项算法简化数值（Bloom 单次 3×3 / FXAA 5-tap / SSR 16 步）列为「**未核实**」——本轮已逐条复读源码核实完毕，结论见 F2，三条全部**成立**。
- 各子模块 README 里与本模块有关的过时说法，见本文件 **G1/G2/G5**。

**判据纪律**：本节点无 GL/Vulkan 渲染环境，凡需 GPU 才能断言的项一律标**未复现**并给出静态判据。7 个 TU 已逐个过 `g++ -fsyntax-only -std=c++17`（含全部 include 路径）**全部通过** —— 所以 A/E 组里「没写完/没接上」的条目都是「编译会过、行为不对或不完整」，不是编译错误。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. 六个 pass 不做全屏状态隔离（只有 tonemap 关了深度/混合），GL 下可能整帧被深度测试丢掉
- 位置：`bloom.cpp:199-234`、`fxaa.cpp:153-164`、`ssao.cpp:145-156`、`ssr.cpp:144-157`、`taa.cpp:121-136`、`compose.cpp:102-115`（六个都没做）；对照 `tonemap.cpp:118-120`、`:136-144`（做了）
- 现状：tonemap 在 pass 前后显式 `depth_test(false,false)` + `blend(false)`，注释写明原因是「离屏 FBO 深度未清，开着深度会把全屏 quad 拒掉 → 黑屏」；其余六个 pass 完全不碰状态，直接 `begin_offscreen → use_program → draw_indexed → end_offscreen`。
- 证据：GL 后端初始化就 `glEnable(GL_DEPTH_TEST)`（`backend/rhi_gl.cpp:147`）且此后从不禁用；`begin_offscreen` 只对**深度专用**离屏清深度（`backend/rhi_gl.cpp:984`、`:996-999` 的 `colors.empty()` 分支），带颜色的离屏深度缓冲**从未清过**（内容是未定义的）。后端唯一的清屏入口 `clear()` 是 `COLOR|DEPTH` 一起清（`rhi_gl.cpp:165-170`），场景 pass 调了它（`theory_render.cpp:252` 一起清掉了自己的深度），而这六个后处理 pass **一次都没调**。调用方 `drawer::render_id` 收尾把状态恢复成 `depth_test(true,false)` + `blend(true)`（`drawer.cpp:366-367`），即进入后处理链时深度测试是开的。全屏 quad 的 NDC z=0 映射到窗口深度 0.5，只有缓冲内容 ≥0.5 才通过。Vulkan 侧不受影响：颜色离屏的 render pass 也给深度附件且 `loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR`（`backend/rhi_vk.cpp:3219`）。
- 影响：GL 后端下这六个 pass 出图是否为空，取决于驱动给新 renderbuffer 的初始深度值（若为 0.0，整帧被丢弃 → 黑屏）。只在 GL 跑才会暴露，且症状是全屏无输出而非单 pass 失效。
- 复现/验证：未复现（本机无 GL）。最近的自检靶是 `test/view/main.cpp:637-767`（单帧跑 SSAO/TAA/SSR 并 `read_offscreen` 断言）：GL 后端下若 `[SSAO] … ok=1` / `[SSR] … ok=1` 恒为 0 即命中本条。
- 建议：把 tonemap 那两行状态隔离抽成公共包装，七个 pass 一并走。

### A2. 未关混合 + compose 保留 base.alpha：透明背景会与目标里的陈旧内容相加
- 位置：`compose.cpp:33`（`outColor = vec4(c, base.a)`）、`compose.cpp:102-115`（不改 blend 状态）
- 现状：compose 保留 base 的 alpha，但 pass 期间不关混合；GL 后端的混合因子是预乘形式 `glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, …)`（`backend/rhi_gl.cpp:149-151`，初始化即 `glEnable(GL_BLEND)`）。
- 证据：场景 HDR 离屏用**透明黑**清屏 —— `camera_clear(m_camera, xhqm_rgba(0, 0, 0, 0))`（`theory_render.cpp:252`，纹理在 `:227` 创建），即无几何处 `base.a == 0`；有几何处的 alpha 取自材质 `ubo.materialPBR.w`（`backend/shader_sources.h:149` 的 `float alpha = ubo.materialPBR.w;`、`:198` 的 `finalColor.a = alpha;`，且 `:208-211` 会 discard `a < 0.1` 的片元），所以只有 a≥0.1 才画得出来，a<1 的像素一律命中本条。混合结果 = 本次颜色 + 目标的**全部旧内容 × (1-a)**（a=0 时旧内容全量相加）。
- 影响：任何 a<1 的像素上，合成结果都被目标残留污染 → 脏色/不收敛。触发条件：(a) 场景存在 alpha<1 的材质（半透明），或 (b) 无几何的背景像素 —— 默认相机还会在清屏后画一张全屏背景 sprite（`camera.h:41` 的 `sprite m_background`、`:61` 的 `clear()`），它是否把背景像素的 alpha 抬到 1 **未核实**；没有背景（或背景也有透明）的宿主一定暴露。Vulkan 侧取决于管线混合状态（未核实）。
- 复现/验证：未复现。静态判据 = 清屏 alpha（`theory_render.cpp:252`）+ 混合因子（`rhi_gl.cpp:149-151`）+ 保留 alpha（`compose.cpp:33`）。
- 建议：全屏 pass 进 pass 前 `blend(false)`，或把 compose 输出 alpha 固定为 1。

### A3. TAA 的输出目标是 RGBA8，HDR 场景在这里被截断到 [0,1]
- 位置：`taa.cpp:100`（`create_offscreen(w, h, 1, error, false)`）
- 现状：TAA 读的是 HDR 场景纹理，写出的却是有符号 8 位目标；同模块其它读 HDR 的 pass（`bloom.cpp:158-160`、`fxaa.cpp:124`、`compose.cpp:75`）都传 `hdr=true`。
- 证据：格式落地在 `backend/rhi_gl.cpp:942-945`（`hdr ? GL_RGBA16F : GL_RGBA8` + `GL_UNSIGNED_BYTE`）与 `backend/rhi_vk.cpp:3068`/`:3090`/`:3206`（`R16G16B16A16_SFLOAT : R8G8B8A8_UNORM`）；自带管线在 `theory_render.cpp:557` 把 HDR 的 `"scene"` 交给它，而 tonemap 在它之后（`theory_render.cpp:819-820`）。
- 影响：>1.0 的高光经 TAA 后被钳到 1.0，随后 tonemap 拿到的已不是原始 HDR 能量（曝光/泛光口径随之失真）；alpha 恒写 1，透明信息丢失。
- 复现/验证：未复现。静态可断：`sed -n '100p' fantasy/postfx/taa.cpp`（第 5 个实参 `false`）。
- 建议：与 bloom/fxaa 一致传 `hdr=true`。

### A4. SSR 的输出目标也是 RGBA8，反射高光被截断
- 位置：`ssr.cpp:123`（`create_offscreen(w, h, 1, error, false)`）；头注释契约见 `ssr.h:6`
- 现状：SSR 输入是 HDR 场景（`theory_render.cpp:525` 传 `"scene"`），输出落在 8 位目标上，再由 compose 以加法并回场景（`theory_render.cpp:527`）。
- 证据：同 A3 的格式落地（`backend/rhi_gl.cpp:942-945`、`backend/rhi_vk.cpp:3068/3090/3206`）。
- 影响：反射里 >1 的镜面高光被截断，加回场景后仍是 LDR 反射 —— 高亮光源/夜景的镜面「发灰发死」。
- 复现/验证：未复现。静态可断：`sed -n '123p' fantasy/postfx/ssr.cpp`。
- 建议：与 compose 一致传 `hdr=true`。

### A5. SSAO/SSR 的相机参数被调用方写死成 45° 半 FOV，相机一改就整体错位
- 位置：`theory_render.cpp:491`、`:524`（两处 `std::tan(xhqm::radians(value(22.5)))` 字面量）
- 现状：`ssao_effect::apply` / `ssr_effect::apply` 把 `aspect` 与 `tan_half_fov` 当参数收（`ssao.h:31-32`、`ssr.h:30-31`），设计上没错；但仓库里唯一调用者不读相机，直接写死 22.5°。
- 证据：相机默认 `m_fov = xhqm::radians(value(45))`（`camera.h:23`）且可随时 `fov(v)` 改（`camera.h:50-51`）；投影用 `m_fov` 算（`camera.cpp:55` 的 `xhqm::perspective(m_fov, …)`），世界坐标重建另有 `std::tan(m_fov * 0.5)`（`camera.cpp:148`）。
- 影响：任何非 45° 垂直 FOV 的相机（分屏、望远镜、编辑器预览）下，SSAO 半球核尺度与 SSR 的位置重建/投影都按错视锥算 → AO 强度与反射错位；不报错、不崩，只能肉眼发现。
- 复现/验证：未复现。可跑 `test/view` 并把相机 fov 改成 60°，观察 SSAO/SSR 输出变化。
- 建议：从相机取 `fov()` 算半角，或给两个 theory_pass 加 fov 入参。

### A6. FXAA 排在 tonemap 之前，却用为 LDR 调的绝对亮度阈值
- 位置：`fxaa.cpp:40`（`if (lumaMax - lumaMin < 0.0312)`）、`:73`（VK 版同一字面量）；顺序见 `theory_render.cpp:818`（fxaa）→ `:820`（tonemap）
- 现状：0.0312 是固定字面量，判据是「邻域亮度极差」；而输入是 HDR 线性能量（RGBA16F 的 `"scene"`）。
- 证据：亮度直接算在线性值上 —— `dot(rgb, vec3(0.299,0.587,0.114))`（`fxaa.cpp:27-39`），阈值却按 LDR [0,1] 量级给。
- 影响：同一个 0.0312 在 HDR 域里随亮度漂移 —— 高亮区几乎处处判成边缘（过度混合、发糊），暗部弱边缘漏检。质量级问题，不崩。
- 复现/验证：未复现。静态可断：`grep -n '0.0312' fantasy/postfx/fxaa.cpp` → `:40`、`:73`。
- 建议：把 FXAA 挪到 tonemap 之后，或阈值按曝光归一。

---

## B. 资源与并发

### B1. 未 ready / 失败一律静默返回，模块内零日志、零错误通道
- 位置：10 处 guard —— `bloom.cpp:201`、`:238`、`compose.cpp:104`、`fxaa.cpp:155`、`:168`、`ssao.cpp:147`、`ssr.cpp:146`、`taa.cpp:123`、`tonemap.cpp:117`、`:135`
- 现状：7 个 `apply` 未 ready 时 `return 0`，3 个 `present` 未 ready 时直接返回；`setup()` 只把后端给的错误串写进 `error` 引用，`apply`/`present` 这条路径完全不报。
- 证据：`grep -n 'log\|printf\|fprintf\|std::cerr\|cout' fantasy/postfx/*.cpp fantasy/postfx/*.h` → **0 命中**（整个模块没有一行日志）。链式影响（任一 pass 失败 → `set_handle("scene", 0)` → 后续 pass 全早退、永不 present）已由父台账 `fantasy/DEBUG.md` **B6** 完整记录，本条不重复，只补模块侧事实：**模块自身没有把失败暴露出去的通道**。
- 影响：调用方拿到纹理 id `0` 当正常纹理用，或 `present` 静默什么都不做；现场没有任何日志可追溯。
- 复现/验证：未复现。静态可断：上面 10 处 guard + 0 日志两条命令。
- 建议：给 `apply` 加错误出口（返回 bool 或出参），或至少一条 stderr。

### B2. 输入纹理契约不做校验（浮点格式、线性视图深度、历史帧尺寸/时序）
- 位置：`ssao.h:7` + `ssao.cpp:145-156`、`ssr.h:6` + `ssr.cpp:144-157`、`taa.h:5-6` + `taa.cpp:121-136`
- 现状：`apply` 收到 id 就 `bind_texture` 并画，不检查格式/尺寸/深度是否线性、历史帧是否与当前帧同尺寸同时序。
- 证据：三个 `apply` 的函数体只有 `begin_offscreen → use_program → bind_texture → uniform → draw_indexed → end_offscreen`（如 `ssao.cpp:148-155`），没有任何校验分支。
- 影响：传 RGBA8 纹理、传非线性深度、或历史帧尺寸不符时，输出是「看起来有点不对」的画面（AO 全白/全黑、反射串位、TAA 拖影），排查无线索。
- 复现/验证：未复现。可仿 `test/view/main.cpp:644-666` 的构造，把 `texture_create_float` 换成 8 位纹理自证。
- 建议：debug 构建下加尺寸/格式断言。

### B3. effect 缓存 setup 时的 `render_backend*`，后端切换后不感知
- 位置：`bloom.h:15`、`compose.h:15`、`fxaa.h:15`、`ssao.h:18`、`ssr.h:17`、`taa.h:21`、`tonemap.h:15`（七个 `m_rhi` 成员）；释放路径如 `bloom.cpp:173-192`、`taa.cpp:107-119`
- 现状：`setup()` 存下 `&rhi`，`reset()` 只把该指针置空；模块内没有「后端变了要重建」的守卫。
- 证据：后端的运行时选择是全局可改的 —— `backend.h:63-69` 的 `active_backend_ref()` / `active_backend(kind)`、`rhi_no.h:178` 的 `render_backend_active()`；而所有句柄（VB/IB/离屏/shader）都绑在具体后端实现上。
- 影响：多后端/多窗口程序在运行中切后端（或后端对象先于 effect 析构）后，继续沿用旧 effect 就是打到已废弃对象上 —— `m_ready` 仍为 true，没有任何拦截。
- 复现/验证：未复现。静态可断：`grep -n 'm_rhi' fantasy/postfx/*.h`。
- 建议：记录 setup 时的后端身份，`apply` 前比对，不一致即返回失败。

---

## C. 跨平台

### C1. 四个类缺 `xhqm_io` 导出宏（`XHQM_DYNAMIC` + MSVC 下动态库消费方链不到）
- 位置：缺的四处 `bloom.h:13`、`fxaa.h:13`、`tonemap.h:13`、`compose.h:14`；有的三处 `ssao.h:16`、`taa.h:19`、`ssr.h:15`（`taa_jitter` 亦在 `taa.h:17` 标注）
- 现状：同一模块内导出标注不一致。`xhqm_io` 定义在 `head.h:27-31`（`XHQM_DYNAMIC` 下展开为 `XHQMCLASS` = `__declspec(dllexport/dllimport)`，`head.h:15-25`；非 Windows 平台为空宏）。
- 证据：库是动态库 —— `CMakeLists.txt:588` 的 `add_library(xhqm SHARED …)`；`grep -c 'xhqm_io' fantasy/postfx/*.h` 三个头 1、四个头 0。
- 影响：`XHQM_DYNAMIC` + MSVC 下，外部程序 include 这四个头去 link 会找不到成员函数符号（LNK2019）；SSAO/TAA/SSR 正常。Linux 侧宏为空，无感。
- 复现/验证：未复现（本机无 MSVC）。静态可断：`head.h:27-31` + 四处 `class` 声明。
- 建议：七个 `*_effect` 统一加（或统一不加并写进约定）。

### C2. 后端判定靠 `rhi.name()[0] == 'v'` 字符串嗅探，七处分散
- 位置：`bloom.cpp:143`、`compose.cpp:63`、`fxaa.cpp:112`、`ssao.cpp:106`、`ssr.cpp:116`、`taa.cpp:93`、`tonemap.cpp:74`
- 现状：`setup()` 里 `bool vk = (rhi.name()[0] == 'v');`，据此选 GL330 / VK450 的着色器源码。
- 证据：仓库已有结构化接口却没人用 —— `backend_kind` 与 `active_backend()`（`backend/backend.h:29`、`:63-69`）、`backend_name()`（`:72-78`）；`name()` 现有实现非空（`rhi_gl.cpp:120` "opengl"、`rhi_vk.cpp:738` "vulkan"、`rhi_no.cpp:19` "none"），故当前可工作。
- 影响：① 将来任何以 'v' 开头的后端名（如 "vulkan-gl"、软后端 "vertex"）会被误判成 Vulkan；② `name()` 若返回空串则 `[0]` 是越界读（未定义行为，当前实现不会）；③ 判定逻辑散在七处，改一处漏六处。
- 复现/验证：未复现。静态可断：`grep -n "name()\[0\]" fantasy/postfx/*.cpp` → 7 处。
- 建议：改用 `active_backend() == backend_kind::vulkan`，或收敛到一个公共函数。

### C3. MSVC 工程里没有本模块；CMake 头清单也少列三个头
- 位置：`xhqm.vcxproj`（grep `postfx` **0 命中**；全文件 `fantasy` 只出现 6 次且全在 glew 的 include/lib 路径里）、`xhqm.vcxproj.filters`（同样 0）、`CMakeLists.txt:429-432`（头清单只有 bloom/fxaa/tonemap/compose）、`CMakeLists.txt:476-482`（七个 .cpp 齐全）
- 现状：Windows 侧只能靠 CMake 构建（或手工把文件加进工程）；CMake 的 HEAD 列表漏了 `ssao.h/taa.h/ssr.h`，而它们的 .cpp 在 BODY 里，所以**编译不受影响**，缺的只是 IDE 树/安装头集。
- 证据：`grep -c postfx xhqm.vcxproj` → 0；`grep -n postfx CMakeLists.txt` → `:87`（`-I fantasy/postfx/`）、`:429-432`、`:476-482`。
- 影响：接手人在 VS 里找不到后处理源码；把「头文件集」当安装/发布清单的流程会漏三个头。
- 复现/验证：`grep -n postfx xhqm.vcxproj xhqm.vcxproj.filters`（0 行）；`grep -n postfx CMakeLists.txt`。
- 建议：补全 CMake 头清单；若要支持 MSVC，把 7 头 + 7 cpp 加进 `xhqm.vcxproj`（本轮按约束未动）。

---

## D. 性能

### D1. 整链 9 张全尺寸离屏，无人复用
- 位置：`bloom.cpp:158-160`（3 张）、`fxaa.cpp:124`、`tonemap.cpp:86`、`ssao.cpp:118`、`ssr.cpp:123`、`taa.cpp:100`、`compose.cpp:75`（各 1 张）
- 现状：全部按视口尺寸分配，无降采样、无复用、无池化；resize 由调用方 `reset()+setup()` 重建（`theory_render.cpp:426-432` 等）。
- 证据：`grep -c create_offscreen fantasy/postfx/*.cpp` 合计 **9**；格式为 RGBA16F 五张（bloom×3 / fxaa / compose）、RGBA8 四张（tonemap / ssao / ssr / taa），落地见 `backend/rhi_gl.cpp:942-945`。
- 影响：1920×1080 下本模块自己占 ≈111 MiB（RGBA16F 5×15.8 MiB + RGBA8 4×7.9 MiB），不含 `theory_scene` 的 HDR 场景（`theory_render.cpp:227`）与 `theory_depth` 深度目标（`:304`）；显存吃紧的平台直接体现为掉帧。
- 复现/验证：未复现。核算公式 `w*h*8` / `w*h*4` 字节（`w*h = 1920×1080`）。
- 建议：bloom 走半分辨率/金字塔；离屏目标做成资源池。

### D2. Bloom 只有单级全分辨率 3×3 模糊，半径实测固定 1 texel（父台账 D1 已记，本条补实测）
- 位置：`bloom.cpp:51-89`（模糊 shader）、`:218`（`params.xy = 1/w, 1/h`）
- 现状：`apply()` 三步都是全分辨率，模糊只有一次九抽头卷积（中心 4 / 四邻 2 / 四角 1，`sum/16.0`），**采样偏移就是 1 个 texel**，没有半径参数、没有 mip 链。
- 证据：父台账 `fantasy/DEBUG.md` **D1** 已记「单级 3×3、无 mip 链」；本条补充的可量化事实是 `bloom.cpp:218` 传的 `1/w, 1/h` —— 因此物理半径就是 1 像素（父条目说的「几个像素内」还要更紧），且 shader 内无 `KERNEL_N`（那是 `ssao.cpp:24` 的常量，父条目 D1 的证据命令里写错了文件）。
- 影响：泛光观感等同「轻度提亮 + 1 像素晕」，而三张全分辨率目标的成本已经付掉 —— 质量/性能比差。不是算错。
- 复现/验证：未复现。静态可断：`sed -n '218p' fantasy/postfx/bloom.cpp`；`grep -c KERNEL_N fantasy/postfx/bloom.cpp` → 0。
- 建议：降采样 + 多趟高斯（或 mip 链），把半径外露成参数。

### D3. 七个 pass 无共享资源：7 套 quad + 12 个着色器程序 + 14 份 vert 源码
- 位置：7 处 quad/索引创建（`bloom.cpp:146-155`、`fxaa.cpp:114-123`、`ssao.cpp:108-117`、`ssr.cpp:118-122`、`taa.cpp:95-99`、`tonemap.cpp:76-85`、`compose.cpp:65-74`）+ 各文件顶部的 vert 字符串（如 `bloom.cpp:8-21`）
- 现状：quad/索引/vert 各自复制，程序也各自创建，无任何共享或缓存。
- 证据：`grep -c shader_create fantasy/postfx/*.cpp` → bloom 4、fxaa 2、tonemap 2、其余各 1，**合计 12 个 program**；`grep -c "layout(location = 0) in vec2 pos;" fantasy/postfx/*.cpp` → 每文件 2（GL/VK 各一），**合计 14 份 vert 源码**。
- 影响：每帧每 pass 至少一次 `use_program` + 2~3 次 `bind_texture` + uniform 上传，无批处理；GPU 侧多 7 份 VB/IB 与 12 个程序的编译/驻留开销（首帧卡顿来源之一）。
- 复现/验证：未复现。静态计数命令如上（可直接跑）。
- 建议：抽公共 quad/程序缓存（属 F1 的取舍面，改前先读 F1 的理由）。

### D4. 采样/迭代数全是硬编码常量，SSR 无命中细化
- 位置：`ssao.cpp:56`（`i < KERNEL_N`，N=16，表在 `:25-42`）、`ssr.cpp:47`（`i < 16`）、`fxaa.cpp:33-36`（中心 + 四邻 = 5 抽头）、`taa.cpp:53-58`（3×3 历史 9 次 + 当前 1 次）
- 现状：四个 pass 的代价固定；SSR 命中即 break，未命中跑满 16 步，无二分细化、无 mip 加速。
- 证据：四处循环上界都是字面量/常量，且 `apply` 的 `params` 只带 aspect/fov/stepSize/bias，无采样数入口（`ssr.h:30-31`）。
- 影响：SSR 的反射质量与代价都不可调；SSAO 固定 16 采样意味着无法用更少采样换帧率（移动端）。质量与性能双向天花板。
- 复现/验证：未复现。静态可断：`grep -n "i < 16\|i < KERNEL_N" fantasy/postfx/ssr.cpp fantasy/postfx/ssao.cpp`。
- 建议：把迭代/采样数外露成成员（shader 里用循环变量）。

---

## E. 未实现 / 占位桩

> 模块内 `grep -n "TODO\|FIXME\|XXX\|占位\|桩\|未实现\|暂不\|尚未" fantasy/postfx` 是 **0 命中** —— 没有任何自陈的未完成标记；下面两条是复读源码时发现的**没有注释标记**的缺件。

### E1. `taa_jitter` 没接上：库内零调用者（父台账 E5 已记，本条补模块内视角）
- 位置：声明 `taa.h:17`、实现 `taa.cpp:7-26`、唯一使用者 `test/view/main.cpp:652`；消费侧 `theory_render.cpp:539-561`
- 现状：父台账 `fantasy/DEBUG.md` **E5** 已完整记录「引擎侧无人调用 jitter」；本条补充模块内的事实：`taa_effect::apply` 的签名是 `(current_tex, history_tex, alpha)`（`taa.cpp:121`），**没有 jitter 入口**，即模块自身也无法把抖动用起来——不在模块内补参数，调用方即使算出了 jitter 也没有地方传。
- 证据：`grep -rn taa_jitter fantasy/ test/`（排除 `.svn`）→ 仅 `taa.h:17` 声明、`taa.cpp:7` 定义、`test/view/main.cpp:652` 自检（加两份 README 提及）；`sed -n '121p' fantasy/postfx/taa.cpp`。
- 影响：引擎自带管线的 TAA 退化为纯时域混合，亚像素采样收益为零，只剩糊与鬼影风险。功能看起来在跑，所以不会被发现。
- 复现/验证：未复现。静态可断：`grep -rn taa_jitter fantasy/postfx/`（声明 + 定义，无调用）。
- 建议：给 `apply` 加 jitter 入参，或按父台账 E5 的建议在 `theory_scene` 侧偏移投影。

### E2. TAA 的历史帧持久化 / ping-pong / 有效性全都不在模块内（**未实现**，旧说法为真）
- 位置：`taa.h:34-35`（`apply` 要 `history_tex`）、`taa.cpp:121-136`（无 ping-pong、无历史失效标记）；模块内只有 1 张离屏（`taa.cpp:100`，成员只有 `m_target`，`taa.h:24`）
- 现状：模块不持有历史，也不判断历史是否可用；仓库自带调用方把上一帧输出存在 `theory_taa::m_history`，resize 时清 0、首帧用当前帧顶替（`theory_render.cpp:554-558`）。
- 证据：`sed -n '539,561p' theory_render.cpp`；`taa.cpp` 全文 138 行里只有 3×3 clamp + `mix`（`:49-60`），没有重投影/运动矢量/深度拒绝/历史矩形校验。
- 影响：直接使用 `taa_effect` 的人必须自己实现 ping-pong 与首帧/resize 处理，否则拿到无效历史；相机移动时没有运动矢量与深度拒绝，鬼影只能靠 3×3 clamp 压。旧文档「缺历史帧持久化（TAA history 由调用方管理）」**成立**。
- 复现/验证：未复现。静态可断：`sed -n '20,30p' fantasy/postfx/taa.h`（成员列表无第二张目标）。
- 建议：模块内提供可选的历史 ping-pong，或至少在头注释写明调用方义务。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 七个 pass 各自复制 fullscreen quad 与 vert 源码，无公共基类
- 位置：`bloom.cpp:8-21` + `:146-155`、`fxaa.cpp:7-20` + `:114-123`、`ssao.cpp:7-20` + `:108-117`、`ssr.cpp:7-20` + `:118-122`、`taa.cpp:28-41` + `:95-99`、`tonemap.cpp:7-20` + `:76-85`、`compose.cpp:7-20` + `:65-74`
- 现状：实测 7 份 quad、7 份索引、14 份 vert 字符串、7 套 VB/IB；模块内 `grep ': public'` **0 命中**（无继承、无虚函数）。
- 理由（为什么现在这样是对的）：本模块的定位是「**可独立装配**」—— 每个 pass 是一个自包含 TU，删掉/替换任何一个都不牵动其它，头文件零继承、零虚表、零公共依赖面；对「按需挑几个用」的后处理场景，一次性几十行的复制成本小于抽象成本（基类还要先裁定生命周期、错误通道、状态策略，而七个 pass 的需求并不一致，见 F5）。
- 代价：改一处要改七处（数字如上）；7 套 GPU 资源与 12 个程序的重复（D3）；新增 pass 靠复制粘贴，缺编译期约束防漏改。
- 验证：`grep -c "layout(location = 0) in vec2 pos;" fantasy/postfx/*.cpp`；`grep -n ": public" fantasy/postfx/*.h`。
- 建议：保持现状；若将来抽基类，先在 README 里给出迁移判据。

### F2. 算法刻意简化（Bloom 单次 3×3、FXAA 5-tap 简版、SSR 16 步、SSAO 16 采样硬编码核）
- 位置：`bloom.cpp:51-89`、`fxaa.cpp:22-53`、`ssr.cpp:47`、`ssao.cpp:23-43`
- 现状：四项都是固定字面量/常量表 —— bloom 九抽头 /16 单趟；FXAA 只有中心 + 四邻；SSR 固定 16 步；SSAO 的 `KERNEL_N=16` 与 16 项半球核写死在 GLSL 字符串里。父台账 `fantasy/DEBUG.md` D1 与根 `DEBUG.md:258` 都提到过这些数值，本轮三项**全部复核为真**。
- 理由（为什么现在这样是对的）：整套实现不依赖降采样链、mip 生成、深度金字塔或 compute，纯 fragment + 全屏 quad，任何 GL3.3/VK1.0 后端都能跑；全模块 1420 行、七个 TU 可单独过 `g++ -fsyntax-only`（实测），适合当**基线实现**与跨后端一致性对照物。参数不可调也正是它「无状态、无配置面」的一部分。
- 代价：质量上限低（泛光半径 1 texel、SSR 无命中细化、SSAO 核不可调）；调参必须改 shader 字符串（`ssao.cpp:25-42` 的常量表、`fxaa.cpp:40` 的 0.0312、`tonemap.cpp:32` 的 1/2.2 gamma）。
- 验证：`grep -n "KERNEL\[16\]\|0.0312\|1.0 / 2.2\|i < 16" fantasy/postfx/*.cpp`。
- 建议：保持简化基线，另开可选的高质量路径。

### F3. 模块内不做编排，中间纹理与顺序交给上层 `fantasy/theory_render.*`
- 位置：`theory_render.h:247-253`（`PIPE_*` 位掩码）、`theory_render.h:260-263`（`setup_pipeline_for` / `setup_default_pipeline`）、`theory_render.cpp:812-820`（实际顺序）
- 现状：postfx 只出 pass；编排、开关、resize 重建、命名句柄串联（`"scene"`/`"depth"`/`"ao"`/`"ssr"`）都在上层 theory_pass 里。
- 理由（为什么现在这样是对的）：后处理只该管「一张纹理进、一张纹理出」；资源生命周期归帧图（`theory_pass`/`frame_graph`）才能和阴影/深度/多相机共用同一套注册-diff-执行机制；多窗口/多相机可直接 `fg->run()` 手动驱动（`theory_render.h:256`）。把编排塞进 postfx 会让「同一 pass 服务多条不同顺序的管线」不可能。
- 代价：单用 postfx 要自己管 9 张中间纹理与顺序（本模块 README 的用法示例走的就是这条路）；根 `README.md:237` 因此长期记「无统一编排器」（已作废，见 G2）。
- 验证：`grep -n "PIPE_\|setup_pipeline_for" fantasy/theory_render.h`。
- 建议：保持分工；在 README 里写明「编排在上层」（已补）。

### F4. `compose_effect::apply` 的 mode 不校验：0=乘法，非 0 一律加法
- 位置：`compose.cpp:32`（`(int(params.x + 0.5) == 0) ? (base.rgb * over.rgb) : (base.rgb + over.rgb)`）、`compose.h:30-31`
- 现状：只有两种模式，用统一的 `params` 通道传（与全模块的 UBO 约定一致，见 G6），非 0 值静默走加法。
- 理由（为什么现在这样是对的）：模式只有两个，`== 0 ? A : B` 比在 shader 里加分支/再开 uniform 更省；`+0.5` 取整是为对齐后端的 `uniform_vec4` 上传路径，`params` 通道本来就要留给模式。
- 代价：调用方传 `2`/`-1` 不报错，静默变成加法合成（表现是过曝而不是断言）。
- 验证：`grep -n "params.x + 0.5" fantasy/postfx/compose.cpp`。
- 建议：保持；在头注释里写明「非 0 = 加法」。

### F5. `present()` 只在三个类里存在，另外三个用 `target()`
- 位置：有 `present()`：`bloom.h:41`、`fxaa.h:31`、`tonemap.h:32`；用 `target()`：`ssao.h:33`、`taa.h:36`、`ssr.h:32`
- 现状：SSAO/SSR/TAA 的结果语义上是中间结果（要乘法/加法并回场景，或作为下一帧历史），不直接上屏。
- 理由（为什么现在这样是对的）：这三个 pass 的正确用法就是回到 `"scene"` 句柄（`theory_render.cpp:495`、`:528`、`:560`），给它们 `present` 反而诱导错误用法；`target()` 同时满足验证读回（`test/view/main.cpp:727`、`:733`、`:740`）。
- 代价：API 面不齐，写文档/写调用方容易误以为七件套同构（本模块 README 旧文就这么写了，见 G1）。
- 验证：`grep -n "present(" fantasy/postfx/*.h`。
- 建议：保持；README 已按实际 API 逐个列出。

---

## G. 文档与代码的出入

### G1. 本模块 README 的三处旧说法已就地标注作废（历史保留）
- 位置：`README.md:7`（原「遵循统一的 setup / apply / reset / present 生命周期」）、`README.md:119`（原「`backend/backend.h` 提供 `xhqm_io` 导出宏」）、`README.md:199`（原用法示例末段的装配顺序示意）
- 现状：三处都在原句上划掉 + 标注「**已作废（2026-10-07 复核 r677）**」并写出事实订正；本轮另按实际代码补齐了行数、常量、每个类的 API 与用法示例。
- 证据：`grep -n "已作废" fantasy/postfx/README.md` → **4 处**（第 4 处是「无统一编排器」的补记，见 G2）。
- 影响：照旧顺序接线的人会把 bloom 放到 taa 之后（那时 HDR 已被 8 位目标截断，泛光阈值语义全变）；照旧 API 列表写代码的人会找不到 `ssao.present()`。
- 复现/验证：`grep -n "已作废" fantasy/postfx/README.md`。
- 建议：无（已订正）。

### G2. 根 `README.md` §3.6 有两条与本模块代码不符（本轮按约束未改）
- 位置：根 `README.md:235`（「统一 setup/apply/reset/present 生命周期」）、根 `README.md:237`（「无统一编排器」）
- 现状：根 `README.md:237` 的另外两句成立（TAA history 由调用方管理 → E2；apply 未 ready 静默返回无日志 → B1）；不成立的是生命周期与编排器这两条。
- 证据：`present()` 只有三个类有（F5）；编排器在 `theory_render.h:260-263` / `theory_render.cpp:812-820`（F3）。
- 影响：根 README 是全仓入口，这两条会让人误判模块边界与装配方式。
- 复现/验证：对读 `README.md:233-238` 与 `fantasy/theory_render.h:247-263`。
- 建议：下次能改根 README 时按本仓风格标注作废（本轮铁律只允许写本模块两份文档）。

### G3. 头注释里的里程碑代号 M5/M8 在仓库内无定义
- 位置：`tonemap.h:5`（「独立 tonemap pass（M5）」）、`ssao.h:5`（「屏幕空间环境光遮蔽（M8）」）、`taa.h:5`（「时间抗锯齿（M8）」）、`ssr.h:5`（「屏幕空间反射（M8，可选）」）
- 现状：M5/M8 只出现在这四个头注释里；全仓 `.md` 中 `M5` 仅命中 `test/nebula_api/README.md:107`、`:182`（那是另一个项目的「合成节点」里程碑，与本模块无关），`M8` 无任何定义。
- 证据：`grep -rn "M5\|M8" --include=*.md .`（排除 `.svn`）→ 2 行命中，均指别的事；代码里无 M 号映射表。
- 影响：接手人无法判断这些代号是版本里程碑、需求编号还是内部代号，也无法确认「M5 的 tonemap」是否还有未落地的后续改动。
- 复现/验证：命令如上；**未确认**（需问作者）。
- 建议：换成可检索的说明，或补一份 M 号对照。

### G4. 头注释的输入格式契约与实务口径不一致（RGBA32F vs RGBA16F）
- 位置：`ssao.h:7`（「输入 depth_tex 约定为浮点纹理（RGBA32F）」）、`ssr.h:6`（「输入 color_tex（RGBA32F）+ depth_tex（RGBA32F）」）
- 现状：仓库内唯一调用方传的都是 **RGBA16F** —— 场景 HDR 在 `theory_render.cpp:227` 创建（`create_offscreen(…, true)`），线性视图深度在 `:304` 创建（同样 `true`），两处分别交给 `ssao.apply`/`ssr.apply`（`:492`、`:525`）。
- 证据：`create_offscreen` 的 hdr 标志决定 RGBA16F/RGBA8（`rhi_gl.cpp:942-945`、`rhi_vk.cpp:3068/3090/3206`）——仓库内**没有**任何路径产出 RGBA32F 纹理。
- 影响：功能上都能跑（shader 只按 `sampler2D` 采样），但精度口径不同：文档说的 32F 是理论契约，实际是半精度；深度经 RGBA16F 量化会放大 SSAO 的 `smoothstep` 范围检查误差（小半径时更明显），排查容易被文档带偏。
- 复现/验证：对读 `ssao.h:7`/`ssr.h:6` 与 `theory_render.cpp:227`、`:304`。
- 建议：契约改成「浮点纹理（仓库内为 RGBA16F）」，或让调用方按文档传 32F。

### G5. 本模块 README 依赖节把 `xhqm_io` 记在 `backend/backend.h`（已就地订正）
- 位置：原 `README.md` 依赖节第三条；实际定义处 `head.h:27-31`，`backend/backend.h` 里 grep `xhqm_io` **0 命中**
- 现状：已在原句上划掉并写明实际位置，同时点出「四个头缺 `xhqm_io`」这个真实差异（C1）。
- 证据：`head.h:15-31`；`grep -n xhqm_io fantasy/backend/backend.h` → 无输出。
- 影响：照文档去找宏会找不到；不知道宏从哪来，也看不出「加/不加 `xhqm_io`」的差异（C1）。
- 复现/验证：`grep -rn "define xhqm_io" .`（排除 `.svn`）→ `head.h:28`、`:30`。
- 建议：无（已订正）。

### G6. postfx 的 `params` 与 mesh 的 `tranMatrix` 共用 UBO 偏移 0（指路，不重复）
- 位置：本模块写入点共 **9 处** —— `bloom.cpp:208`/`:218`/`:230`、`compose.cpp:111`、`fxaa.cpp:160`、`ssao.cpp:152`、`ssr.cpp:153`、`taa.cpp:132`、`tonemap.cpp:125`；映射表 `backend/ubo.h:75`
- 现状：本条已由 **两处**父级台账完整记录 —— `fantasy/fantasy_bugs.md` **#22**（判定「潜在耦合、当前无实际缺陷、改需布局重构」，本轮维持）与 `fantasy/DEBUG.md` **B7**（含 `ubo.h` 偏移细节与 9 处写入点的复核）。本台账**不复制**其证据。
- 证据：`grep -rn 'uniform_vec4(.*"params"' fantasy/postfx/` → 9 处。
- 影响：推进描述符共享或 postfx UBO 重构时会撞上；当前不动风险更小。
- 复现/验证：见 `fantasy/DEBUG.md` B7 与 `fantasy_bugs.md:52`。
- 建议：按父台账为准，本模块只做指路。

### G7. 与父台账重叠的其余条目（指路，不重复）
- 位置与现状：以下条目的事实与证据都已在 `fantasy/DEBUG.md` 记全，本模块只在文档层面指过去 ——
  - **A3**（`theory_render.cpp:487`/`:520` 用 `&&` 短路两个 effect 的 `setup()`，失败后每帧重建资源、从不报错）：这是**调用方**的缺陷，但直接决定本模块 `ssao_effect`/`ssr_effect` 的资源是否被反复创建销毁。
  - **B6**（任一「读 scene 写回 scene」的 pass 失败 → `set_handle("scene", 0)` → 整链含 present 静默停摆，后处理 pass 一条日志都没有）：本文件 B1 只留模块侧事实，链式影响以父台账为准。
  - **C1**（Vulkan `viewport()`/`winding()` 空实现）：本模块唯一的上屏路径 `tonemap_effect::present()` 由 `theory_tonemap::render` 用 `rhi.viewport()` 做分屏（`theory_render.cpp:403-406`），因此「分屏 present 在 Vulkan 上失效」这条虽然代码在 backend，症状落在本模块的呈现环节。
  - **D1**（Bloom 单级 3×3、无 mip 链）→ 本文件 D2 补了实测半径 1 texel。
  - **E5**（`taa_jitter` 引擎侧无人调用）→ 本文件 E1 补了「`apply` 无 jitter 入参」。
- 证据：`grep -n "A3\.\|B6\.\|C1\.\|D1\.\|E5\." fantasy/DEBUG.md`。
- 影响：两边若各记一遍容易分叉（本仓已有「结论被推翻后只改一处」的前例，见 `fantasy/DEBUG.md:333`），故按父台账纪律只指路。
- 复现/验证：对读 `fantasy/DEBUG.md` 对应条目。
- 建议：本模块修完任一条时，记得同步父台账的对应条目。
