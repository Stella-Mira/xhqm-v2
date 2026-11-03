# fantasy/animation · 当前问题清单

`fantasy/animation`（module id `fantasy/animation`，module 级）的**当前仍存在**的问题台账：只记「现在还是坏的 / 还是没写的 / 还是对不上的」，**不记已修项、不做版本历史**。每条都给出可核对的 `文件:行号`。

> 范围：只记录**当前仍存在**的问题，不记录已修项、不做版本历史。最后核对：2026-10-07（工作副本 r677）。
>
> **与同类文件的关系**：本仓的历史台账**不搬家、不改名** —— `fantasy/fantasy_bugs.md`（fantasy 静态分析清单 + 逐轮修复记录，`:70-74` 的 animation 三条 #28/#29/#30 均记为已修）与 `network/nebula/DEFERRED.md`（讨论过但决定不做的事项）保持原位，本文件只指过去、不复制内容。与本模块 README 的分工：README 讲「是什么 / 怎么用 / 成熟度一句话」，本文件讲「哪里还坏 / 证据 / 代价」；README 里的「当前问题清单」一节指回本文件。同批建立的模块级 DEBUG.md 见 `DEBUG.md`（根）、`hardware/DEBUG.md`、`lattice/DEBUG.md`、`algebra/geometry/DEBUG.md` 等，彼此不重复记同一件事。
>
> 判据纪律：凡写「编译不过 / 某 API 不存在 / 某行为如何」，必须附复现命令与验证版本。本文件所有代码事实均在 r677 工作副本上逐条读过；四个 TU 的编译结论来自本次实测的 `-fsyntax-only`（见文末「本轮实测数字」）。

---

## A. 功能性缺陷（会算错 / 会崩）

### A1. 奇异 bind 姿态让 `compute_palette()` 产出 0×0 矩阵，下游按 16 元素拷贝即空指针
- 位置：`fantasy/animation/skeleton.cpp:77`（`world[i] * m_bones[i].bind_pose.invmat()`）；`algebra/geometry/linear.h:289-290`（`invmat()` 奇异时 `return matx(0, 0)`）；`algebra/geometry/linear.h:77-84`（维数不匹配的 `operator*` 返回 0×0；不匹配分支 `:80-83`，`matx c(0, 0)` 初值在 `:79`）；消费侧 `fantasy/drawer.cpp:837-838`、`fantasy/theory_render.cpp:349-350`
- 现状：`invmat()` 走部分主元高斯-约当，最大主元 `< xhqm::preci<float>`（= `1e-5f`，`algebra/value.h:39`、`:43`）就返回 **0×0** 矩阵而不是原矩阵/报错。随后 `world[i] * inv` 因 4×4 与 0×0 维数不匹配，走 `linear.h:79-84` 的 `matx c(0,0)` 分支直接返回 0×0（`:82` 构造了一个 `std::runtime_error` 局部对象但**从未 throw**）。于是 `std::vector<matrix>` 里出现 `size()==0`、`data()==nullptr` 的元素。
- 证据：`sed -n '267,291p' algebra/geometry/linear.h`（奇异分支）；`sed -n '76,100p' algebra/geometry/linear.h`（`if (a.m_dimensions[1] != b.m_dimensions[0]) { std::runtime_error error(...); }` 无 throw）；`sed -n '69,74p' metaclass/buffer.h`（`m_buffer = (size == 0 ? nullptr : new type[size])`）；`sed -n '834,839p' fantasy/drawer.cpp`（`matrix transposed = m_bone_palette[b].tramat(); for (i<16) bones[b*16+i] = transposed.data()[i];`）
- 影响：只要某根骨骼的 `bind_pose` 退化（零缩放骨骼——隐藏网格的常见手法、或导入的坏矩阵），该骨骼的 palette 元素就是 0×0；任何按「16 个 float」拷贝它的消费方读到空指针 → 崩溃（不是错值）。`tramat()` 对 0×0 仍返回 0×0（`linear.h:252-260`），不改变结论。
- 复现/验证：**本机未跑（无 GL 上下文/运行环境）**，静态链路已逐跳核对。最小验证片段（需补 `-I<xhqm>/fantasy -I<xhqm>/fantasy/backend -I<xhqm>/fantasy/animation`）：
  `skel.set_bind_pose(0, xhqm::scale(0.0f)); auto p = skel.compute_palette(); assert(p[0].size() == 0);`
- 建议：`compute_palette()` 对奇异 bind 显式兜底（保留单位阵/上一帧值并计数），别让 0×0 逃出模块。

### A2. `solve_ccd` 把**世界空间**旋转直接左乘关节的**局部**姿态：非根关节绕错轴
- 位置：`fantasy/animation/ik.cpp:53-59`（轴由世界位置算出：`:46-55`，写入处 `:59`）
- 现状：`rot` 的轴/角来自世界坐标（`cross(j2e, j2t)`、`angle_between`），却以 `skel.set_local_pose(joint, rot * skel.get_local_pose(joint))` 写入。按本模块约定 `world[i] = world[parent] * local[i]`（`skeleton.cpp:57`），要让关节 i 绕世界轴 R 旋转子树的正确局部更新是 `local'[i] = inv(world[parent]) * R * world[parent] * local[i]`；代码少了 `inv(world[parent]) * … * world[parent]` 这一步共轭，等价于把 R 表达在父级局部坐标系里。
- 证据：`sed -n '31,62p' fantasy/animation/ik.cpp`；`grep -n "world\[b.parent\] \* b.local_pose" fantasy/animation/skeleton.cpp` → `:57`。只有当 `world[parent]` 的旋转部分为单位矩阵（根关节，或其所有祖先都没转过）时两者才等价。
- 影响：链上第 2 个关节起（典型 `{root, spine, arm, hand}`），旋转轴被父级世界旋转"再解释"一次 → 迭代绕着错误的轴走：轻则末端停不到目标附近，重则来回抖动；**不报错、不越界**，只在结果里体现。本仓当前无人调用 `solve_ccd`（`grep -rn "solve_ccd"` 除模块自身外 0 命中），所以没被现场发现。
- 复现/验证：**未复现**。验证配方：两骨链 `root → child`，先把 root 的局部姿态置为绕 Y 轴 90° 的旋转，再对 `{root, child}` 调 `solve_ccd(..., point(0,0,1), 10)`，比较末端世界位置到目标的距离；再与"把轴共轭到父空间"的改法对比，当前实现距离显著更大。
- 建议：改成 `rot = inv(W_parent) * R * W_parent` 后再左乘局部姿态（并可顺手把 `compute_world_poses()` 的结果复用，见 D1）。

### A3. GPU 端 `parent` 索引零校验、父链成环无检测：越界读 + `guard` 静默截断
- 位置：`fantasy/animation/gpu_skinning.cpp:44-51`（着色器 `while (p >= 0 && guard < 1024) { world = bones[p].local * world; p = bones[p].parent; }`）、`:92`（`build` 上传）、`:118`（`update` 上传）
- 现状：`gpu_bone::parent` 只约定「`-1` = 根」，C++ 侧从不校验 `parent < boneCount`，着色器里的 `bones[p]` 也没有上界判断；父链成环（含自环 `parent == i`）时靠 `guard < 1024` 停手，静默产出一份错误 palette。
- 证据：`sed -n '37,53p' fantasy/animation/gpu_skinning.cpp`；`sed -n '84,95p' fantasy/animation/gpu_skinning.cpp`（`o[16] = (std::uint32_t)bones[i].parent;` 原样写盘，无校验）。越界读的后果按后端不同：GL 对 SSBO 越界读返回未定义值（`rhi_gl.cpp:861` 用 `glBufferData(GL_SHADER_STORAGE_BUFFER, …)` 建普通 SSBO，无额外保护）；Vulkan 侧设备创建只挂了 `VK_EXT_robustness2` 的 `nullDescriptor`，**没有启用 `robustBufferAccess`**（`rhi_vk.cpp:895-917`），越界读属未定义行为（可能读到别的绑定内容，也可能触发设备丢失）。
- 影响：资产导入把 `parent` 填错（很常见：索引错位、导入了骨骼表的子集）时，错的是 GPU 侧的一份静默计算，表现为"某根骨骼的蒙皮爆掉/模型抽风"，排查成本高；环状父链则永远不会收敛到正确值。
- 复现/验证：**未复现**（本机无 GPU 上下文）。静态证据即上两处；跑起来验证的办法见 G2（打开 `test/view/main.cpp:466` 的 `#if 0`，把 `bones[1].parent` 改成 `999`，预期 GL 得到垃圾值、Vulkan 行为依赖驱动）。
- 建议：`build`/`update` 上传前校验 `parent ∈ [-1, boneCount)`，着色器补 `p < params.boneCount`，环状父链给一次性告警。

---

## B. 资源与并发

### B1. Vulkan 帧内 `compute()` 只是「录制」：紧随其后的 `read_palette()` 读到 dispatch 之前的内容
- 位置：`fantasy/animation/gpu_skinning.cpp:125-134`（`compute()` → `compute_storage_bind(1/2/3)` + `compute_dispatch`）、`:136-153`（`read_palette()` → `storage_read`）；后端 `fantasy/backend/rhi_vk.cpp` 的 `compute_dispatch`（帧激活且 render pass 未开时 `record(cb)`，否则 `one_shot`）、`storage_read`（`rhi_vk.cpp:3016-3030`：`vkDeviceWaitIdle` + `vkMapMemory`）
- 现状：Vulkan 下「帧内 + 非 render pass 期间」的 dispatch 被录进当帧命令缓冲，**不会**因为紧接着的 `read_palette()` 而提交；`storage_read` 只等设备空闲、不触发提交，所以返回的是 dispatch 之前的内容 —— build 时 palette SSBO 被初始化为全 0（`gpu_skinning.cpp:100-101`）。GL 侧的 `glDispatchCompute` 是立即执行（`rhi_gl.cpp:844-852`），没有这个问题。
- 证据：`rhi_vk.cpp` 的 `compute_dispatch` 分支注释「离帧同步 compute（初始化/验证场景）：one_shot 提交并等待，随后 CPU 可读回」；`storage_read` 注释「等待设备空闲后直接映射读回」。
- 影响：把 `compute()` + `read_palette()` 当自检用的人（`read_palette` 的既定用途，`gpu_skinning.h:43-44`）在 Vulkan 帧内会拿到**全 0 / 陈旧值**，而 API 不返回任何错误 —— 比对会"通过"或"失败"得很随机。
- 复现/验证：**未复现**（无 GPU）。按 `test/view/main.cpp:469-504` 的 3 骨骼自检改造：把该块移到 `begin_frame()` 之后、draw 之前再跑（GL 预期一致，Vulkan 预期 MISMATCH）。
- 建议：`read_palette()` 前强制一次同步提交（或把该 API 明确标注为"仅帧外可用"并在帧内返回错误标志）。

### B2. 默认可拷贝 + 析构删资源：拷贝体持有悬垂 id，之后所有调用静默失效
- 位置：`fantasy/animation/gpu_skinning.h:26-47`（只有 `~gpu_skinner();`，未删除/未定义拷贝与赋值）、`gpu_skinning.cpp:59-67`（析构按 id 释放 program + 3 个 SSBO）
- 现状：`gpu_skinner` 在非 `XHQM_DYNAMIC` 构建下 `xhqm_io` 展开为空宏（`head.h:27-31`），因此隐式拷贝构造/赋值可用。两份对象共享同一组 `m_program`/`m_*_ssbo` id：先析构的一侧把资源删掉，后析构的一侧变成**悬垂 id**。各后端的删除是「按 id 查表 + 幂等」，所以**不会**双重释放（`rhi_gl.cpp:836-842`、`:868-873`、`rhi_vk.cpp:2997-3003`），而是那份对象的 `update()`/`compute()`/`read_palette()` 从此全部静默 no-op。
- 证据：`grep -n "gpu_skinner(" fantasy/animation/gpu_skinning.h` → 只有 `=default` 与 `~`；`sed -n '59,67p' fantasy/animation/gpu_skinning.cpp`；`sed -n '836,842p' fantasy/backend/rhi_gl.cpp`（`if (it == m_computes.end()) return;`，`:838`）
- 影响：把 skinner 放进容器/按值传给函数/被 `shared_ptr` 与裸引用混用，就会出现「先退出的那个把它带走」——画面停止蒙皮、无任何报错。同类形状在 `fantasy/spatial/gpu_culler.h:31-32` 也存在（属仓库通行写法，不是本模块独有）。
- 复现/验证：**未复现**。静态验证：`gpu_skinner a; gpu_skinner b = a;`（编译通过即说明可拷贝）；运行期表现为 b 的 compute 无效。
- 建议：`gpu_skinner(const gpu_skinner&) = delete;` + `operator=(const gpu_skinner&) = delete;`（或实现带所有权的移动）。

### B3. `build()` 重复调用泄漏上一组 GPU 资源
- 位置：`fantasy/animation/gpu_skinning.cpp:69-106`（`m_program = …`、`m_params_ssbo = …`、`m_bones_ssbo = …`、`m_output_ssbo = …` 都是直接覆盖）
- 现状：对同一个对象再 `build()`（骨骼数变化后重建是必然要走的路径，见 F6）不释放旧管线与旧 SSBO；旧 id 再无持有者，只有最后一次的成员会被 `~gpu_skinner()`（`:59-67`）释放，前面的全部泄漏在 RHI 的 id 表里直到进程结束。
- 证据：`sed -n '59,106p' fantasy/animation/gpu_skinning.cpp`（析构只删成员当前值；`build` 无前置释放，也无"已 build"判断）
- 影响：每次重建泄漏 1 个 compute program + 3 个 SSBO；骨骼数频繁变化的工具（编辑器/预览器）长时间运行后显存持续增长。
- 复现/验证：**未复现**；静态证据如上。`grep -n "m_program = \|m_params_ssbo = \|storage_delete" fantasy/animation/gpu_skinning.cpp` 可见覆盖点与唯一释放点。
- 建议：`build()` 开头先做一次与析构等价的释放（或要求先 `~gpu_skinner()`）。

### B4. 三处「尺寸不符」被静默丢弃，没有任何返回值/日志
- 位置：`fantasy/animation/gpu_skinning.cpp:110`（`if (!m_bones_ssbo || bones.size() != m_bone_count) return;`）、`fantasy/animation/animation.cpp:93`（`if (times.empty() || positions.size() != times.size()) return;`）、后端 `fantasy/backend/rhi_gl.cpp:877-884`（检查在 `:881`）与 `fantasy/backend/rhi_vk.cpp:3005-3013`（检查在 `:3009`）（`storage_update`/`storage_read` 的 `bytes` 越界 `return`）
- 现状：`animation_clip::set_channel` 在 `positions` 与 `times` 不等长时**整条通道被丢弃**，且 `m_duration` 不参与更新（`animation.cpp:103` 只在成功路径执行）；`gpu_skinner::update` 在骨骼数与 `build` 时不一致时直接返回；后端在长度不符时也直接返回。
- 证据：`sed -n '87,104p' fantasy/animation/animation.cpp`；`sed -n '108,123p' fantasy/animation/gpu_skinning.cpp`；`sed -n '877,884p' fantasy/backend/rhi_gl.cpp`
- 影响：动画资产里少写一个 position，该骨骼**完全不动**且 clip 时长悄悄变短（循环周期错位），排查时看到的是"这根骨头没动画"；骨骼数变化后忘了重建就会一直用旧姿态。三处都不报错、不返回失败。
- 复现/验证：未运行复现；静态：`clip.set_channel(0, {0.f,1.f}, {vect<value>(0,0,0)}, {}, {})` 后 `clip.duration()` 仍为 `0.0f`。
- 建议：至少加一处日志/断言；`set_channel` 改为返回 `bool`。

---

## C. 跨平台

### C1. GL 路径要 compute 4.3，而仓库内宿主请求的是 GL 3.3 Core：GL 后端下 `build()` 必然失败
- 位置：`fantasy/animation/gpu_skinning.cpp:55`（`vulkan ? "#version 450\n" : "#version 430 core\n"`）；宿主上下文 `test/view/RhiViewport.cpp:37`（`fmt.setVersion(3, 3); fmt.setProfile(QSurfaceFormat::CoreProfile);`，另一份同源副本 `test/ffbo/RhiViewport.cpp:55`）；引擎的版本护栏 `fantasy/backend/rhi_gl.cpp:141-146`（只在 `< 3.3` 时告警，且告警文案写的是 `#version 330 core`）
- 现状：compute 着色器在 GL 下需要 4.3+，而仓库里唯一的两处宿主上下文都请求 **3.3 Core**（Windows 驱动按字面给 3.3；macOS 最多给到 4.1）。引擎从不检查 4.3，只有 `< 3.3` 的告警，因此 GL 后端上 `gpu_skinner::build()` 会在着色器编译阶段失败（错误串由 GL 编译日志给出：`rhi_gl.cpp:813-814` "Compute shader compile error: …"）。同一形状也存在于 `fantasy/spatial/gpu_culler.cpp:80`、`gpu_hiz*`（属仓库通行的 compute 写法）。
- 证据：`sed -n '30,45p' test/view/RhiViewport.cpp`；`sed -n '137,147p' fantasy/backend/rhi_gl.cpp`；`sed -n '805,830p' fantasy/backend/rhi_gl.cpp`
- 影响：**本模块实际可跑通的 GPU 路径只有 Vulkan**（Windows VK / macOS 走 MoltenVK，`CMakeLists.txt:707-711`）；在 GL 后端把它当"零读回 GPU 蒙皮"用的人只会得到 `build FAIL`，而引擎不会提前拦。macOS 的 GL 上限 4.1 属外部事实，**本机未验证**。
- 复现/验证：**未复现**（本机无 GL/Vulkan 上下文）。复现办法：打开 `test/view/main.cpp:466` 的 `#if 0`，用 GL 后端（`RenderWidget`/OpenGL 分支）跑，预期输出 `[GSKIN] build FAIL: Compute shader compile error…`；Vulkan 后端预期 `[GSKIN] GPU skinning OK`。
- 建议：RHI 增加 `supports_compute()` 能力查询（GL 侧判 `gl_major.gl_minor >= 4.3`），模块据此决定走 GPU 还是 CPU 路径。

### C2. 后端类型靠名字首字符嗅探；`none` 桩下失败原因丢失；模块内没有 CPU 回退
- 位置：`fantasy/animation/gpu_skinning.cpp:73`（`bool vulkan = (rhi.name()[0] == 'v');`）；名字来源 `fantasy/backend/rhi_gl.cpp:120`（`"opengl"`）、`rhi_vk.cpp:738`（`"vulkan"`）、`rhi_no.cpp:19`（`"none"`）；桩后端 `rhi_no.cpp:89`（`shader_compute_create` 直接 `return 0`，**不填 error**）
- 现状：`name()` 只有三个取值，判定逻辑成立但脆弱（任何未来以 `v` 开头的后端名都会被当成 Vulkan）；更实际的问题是：当 `render_backend_active()` 落到 `none` 桩（`device` 为 `no/cpu/net` 时，`rhi_no.cpp:163-180`）时，`build()` 返回 `false` 而 `error` 保持调用方传入的空串，调用方拿不到任何原因。模块也**不提供**"GPU 不可用就退回 `skeleton::compute_palette`"的分支（`gpu_skinner` 与 `skeleton` 之间没有任何连接代码）。
- 证据：`grep -rn "name()" fantasy/backend/rhi_gl.cpp fantasy/backend/rhi_vk.cpp fantasy/backend/rhi_no.cpp`；`sed -n '85,96p' fantasy/backend/rhi_no.cpp`；`grep -rn "skeleton" fantasy/animation/gpu_skinning.h fantasy/animation/gpu_skinning.cpp` → 0 命中
- 影响：CPU/无设备环境下（`XHQM_DEVICE=cpu`）走 GPU 蒙皮的代码只会得到一个空错误串的 `false`；换后端或加后端时 `[0]=='v'` 的判断需要人肉记得同步。
- 复现/验证：`grep -n "return \"none\"" fantasy/backend/rhi_no.cpp`；`sed -n '88,90p' fantasy/backend/rhi_no.cpp`（无 error 赋值）。运行期复现需 CPU 设备配置。
- 建议：判定改为显式枚举/RHI 查询；桩后端的 `shader_compute_create` 至少把 `error` 填上。

---

## D. 性能

### D1. `solve_ccd` 每个关节都重算整树世界姿态（O(iter × 链长 × 骨骼数)），且每次重算都分配
- 位置：`fantasy/animation/ik.cpp:36-51`（`for iter` → `for i` → `:42` `std::vector<matrix> world = skel.compute_world_poses();`）
- 现状：`compute_world_poses()` 在**关节循环体内**调用，每次走完整棵树的 DFS；`compute_world()` 每次都新建 `world`（`m_bones.size()` 个矩阵，逐个默认构造 = 逐个堆分配）与 `visited`（`skeleton.cpp:48-49`）。`iterations` 默认 10（`ik.h:18`），且循环**没有收敛提前退出**，末轮达标也照样把剩下的关节/迭代跑完。
- 证据：`sed -n '31,62p' fantasy/animation/ik.cpp`；`sed -n '46,52p' fantasy/animation/skeleton.cpp`
- 影响：按 `iterations=10`、链长 5 根、骨架 100 骨计算：`10 × 4 = 40` 次整树重算 = 4000 次矩阵乘法 + 4000+ 次堆分配/帧/链；多链（四肢）线性叠加。这是本模块最重的一处每帧开销。
- 复现/验证：`grep -n "compute_world_poses()" fantasy/animation/ik.cpp` → 只有 `:42`，在 `:39` 与 `:36` 的两层循环内。运行期计时未做（**未复现**）。
- 建议：每轮（或每次关节更新后）增量更新 joint→end 的世界姿态，缓存 `world[]`；达标即 `break`。

### D2. clip 采样对每个通道线性扫描关键帧（O(K)），已有 `<algorithm>` 却不用二分
- 位置：`fantasy/animation/animation.cpp:119-125`（`for (size_t i = 0; i + 1 < ts.size(); ++i) if (time >= ts[i] && time <= ts[i+1]) { idx = (int)i; break; }`）
- 现状：升序 `times`（文档要求，`animation.h:33`）本可二分或用「上一帧游标」O(1) 推进，实现是每次采样从头扫；`animation.cpp` 顶部已 `#include <algorithm>`（`:2`）但没有用 `std::upper_bound`。采样对每个通道各扫一次（`:111`）。
- 证据：`sed -n '109,129p' fantasy/animation/animation.cpp`
- 影响：K = 每骨骼关键帧数。30 骨 × 每骨 300 帧、每帧采样一次的常见量级 ≈ 9000 次比较/帧；混合时（`blend` 内部采样两次，`:177-178`）翻倍。
- 复现/验证：`sed -n '119,124p' fantasy/animation/animation.cpp`。计时未做（**未复现**）。
- 建议：改 `std::upper_bound` + 每通道缓存上次区间索引。

### D3. `gpu_skinner::update()` 每帧重打包并全量重传整块 bones SSBO
- 位置：`fantasy/animation/gpu_skinning.cpp:108-123`（`:111` 建 `bones.size() * 36` word 的 staging vector、`:112-121` 逐骨骼 `tramat()` + `float_bits`、`:122` 一次 `storage_update` 全量）
- 现状：无脏标记/无部分更新（后端也只提供整块接口：GL `glBufferSubData(offset 0, bytes)` `rhi_gl.cpp:877-884`；Vulkan `vkMapMemory` + `memcpy` `rhi_vk.cpp:3005-3014`）。每根骨骼每帧做 2 次 `tramat()`，而 `tramat()` 按值返回新 `matx`（`algebra/geometry/linear.h:252-260`）= 2 次堆分配；加上 staging vector 1 次。
- 证据：`sed -n '108,123p' fantasy/animation/gpu_skinning.cpp`；`sed -n '877,884p' fantasy/backend/rhi_gl.cpp`
- 影响：512 骨骼时每帧重传 512 × 144 B = 73,728 B，并伴随约 1025 次堆分配（与骨骼数成正比）；`build` 与 `update` 的打包代码是逐行重复的两份，改一处要记得改另一处。
- 复现/验证：`grep -n "tramat()" fantasy/animation/gpu_skinning.cpp` → `:89`、`:90`、`:115`、`:116`。帧计时未做（**未复现**）。
- 建议：抽出打包函数供 `build`/`update` 共用；骨骼姿态未变的通道跳过重传（或让 RHI 支持区间更新）。

### D4. `animation_blender::blend()` 每次调用深拷贝两份 `skeleton`，逐骨骼再走矩阵↔四元数往返
- 位置：`fantasy/animation/animation.cpp:175-178`（`skeleton sa = out; skeleton sb = out; a.sample(...); b.sample(...)`）、`:183-212`（每骨骼 `matrix_scale` + `from_matrix` ×2 + `to_matrix`）
- 现状：每次混合都把整棵骨骼树按值复制两份（`std::vector<bone_node>` 里每根骨骼含 2 个 `matrix` = 2 次堆分配，故约 4N 次分配/调用），随后每根骨骼再做 2 次矩阵→四元数、1 次四元数→矩阵，以及采样阶段每通道 4 次矩阵乘法（`animation.cpp:144-146`，`translate/rotate_*/scale` 均按值返回 `matx`）。
- 证据：`sed -n '170,215p' fantasy/animation/animation.cpp`
- 影响：过渡（cross-fade）期间每帧的固定开销；骨骼数越多、越频繁混合越明显；`sa`/`sb` 只用一次就丢，属可复用的临时量。
- 复现/验证：`grep -n "skeleton sa = out\|skeleton sb = out" fantasy/animation/animation.cpp` → `:175-176`。计时未做（**未复现**）。
- 建议：把两个临时 skeleton 提为可复用的调用方缓冲，或改成"逐骨骼取两份局部姿态再混合"而不复制整棵树。

### D5. `read_palette()` 是同步停顿读回（GL `glGetBufferSubData` / Vulkan `vkDeviceWaitIdle`）
- 位置：`fantasy/animation/gpu_skinning.cpp:136-153`；后端 `fantasy/backend/rhi_gl.cpp:887-896`（`glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT)` + `glGetBufferSubData`）、`fantasy/backend/rhi_vk.cpp:3016-3030`（`vkDeviceWaitIdle` + `vkMapMemory` + `memcpy`）
- 现状：一次读回整块 `bone_count * 16 * 4` 字节，并强制 CPU 等待 GPU：GL 是阻塞式 `glGetBufferSubData`（管线停顿），Vulkan 是 `vkDeviceWaitIdle`（等**整个设备**空闲，不只是这一条 dispatch）。
- 证据：`sed -n '136,153p' fantasy/animation/gpu_skinning.cpp`；`sed -n '887,896p' fantasy/backend/rhi_gl.cpp`；`sed -n '3016,3030p' fantasy/backend/rhi_vk.cpp`
- 影响：按注释只用于验证/调试（`gpu_skinning.h:43-44`）时无妨；一旦被接进逐帧流程（"读回来给 CPU 用"），每帧一次设备级停摆 —— 这也是它没有被接进渲染的合理原因之一（E3）。
- 复现/验证：`grep -n "vkDeviceWaitIdle\|glGetBufferSubData" fantasy/backend/rhi_vk.cpp fantasy/backend/rhi_gl.cpp`。停顿时长未实测（**未复现**）。
- 建议：保留为调试接口即可，但要在头文件里写明"每帧调用会停摆"。

### D6. `compute_palette()` 每帧每骨骼重算 `invmat()`（绑定姿态的逆不随帧变化）
- 位置：`fantasy/animation/skeleton.cpp:70-80`（`:77` `m_bones[i].bind_pose.invmat()`）
- 现状：每次 `compute_palette()` 都对全部骨骼重算逆矩阵，`invmat()` 是部分主元高斯-约当（`linear.h:267-320`），内部还要 `matx temp(*this)` + `matx inv = this->idemat()`（2 次堆分配）并按值返回。绑定姿态是**静态数据**，没有任何缓存成员。
- 证据：`sed -n '70,80p' fantasy/animation/skeleton.cpp`；`sed -n '267,290p' algebra/geometry/linear.h`
- 影响：N 骨骨架每帧 N 次 4×4 消元 + 约 4N 次堆分配（`compute_world()` 的 `world` 数组 N 次、`palette` 数组 N 次、每次 `invmat` 内部 `temp`/`idemat` 各 1 次 = 2N）；叠上 D1/D3 后 CPU 蒙皮路径的固定成本明显高于"矩阵乘一乘"。
- 复现/验证：`grep -n "invmat()" fantasy/animation/skeleton.cpp` → 只有 `:77`；`grep -rn "inv_bind\|invmat" fantasy/animation/skeleton.h` → 无缓存成员。计时未做（**未复现**）。
- 建议：`set_bind_pose` 时预计算并缓存 `inv(bind)`（顺带在 A1 里做奇异兜底）。

---

## E. 未实现 / 占位桩

### E1. 没有 clip 播放控制器（无时间推进 / 循环 / 变速 / 事件 / 交叉淡化）
- 位置：`fantasy/animation/animation.h:25-55`（全模块仅 `animation_clip` + `animation_blender` 两个类型）、`animation.h:41`/`:44`（`duration()` 与 `sample(time, skel)` 都要求调用方自己给时间）
- 现状：模块只提供无状态函数：调用方自己累加时间、自己取模循环、自己做过渡曲线（`animation_blender::blend` 只按**单次权重**混合两个时刻，没有淡入淡出时长、没有状态机、没有事件回调/通知）。`animation_clip` 也没有任何查询接口（`channel` 是私有嵌套类型、`m_channels` 私有，只有 `duration()`）。
- 证据：`grep -rn "play\|tick\|loop\|speed\|fade\|catmull\|hermite\|cubic\|spline" fantasy/animation/*.h fantasy/animation/*.cpp` → **0 命中**（模块里连一个时间推进/插值模式相关标识符都没有；唯一带 update 的标识符是 `gpu_skinner::update`，与播放无关）；`sed -n '25,55p' fantasy/animation/animation.h`
- 影响：所有使用方都得自己写播放器（demo 就没用本模块，见 E2）；"循环/变速/事件"这类需求每接一个宿主重写一遍。
- 复现/验证：`grep -c "" fantasy/animation/animation.h` → 59 行；`grep -n "^[[:space:]]*class \|^[[:space:]]*struct " fantasy/animation/animation.h` → `quat`(:14)、`animation_clip`(:25)、`channel`(:27，私有嵌套)、`animation_blender`(:48)，没有第三个可用的运行时对象。
- 建议：补一个无 RHI 依赖的 `animation_player`（时间推进 + 循环模式 + 双 clip 交叉淡化计时）。

### E2. 没有资产导入：模块内零解析代码，引擎的 glTF 加载器也不认蒙皮/动画
- 位置：`fantasy/animation/`（8 个文件，`grep -rni "gltf\|fbx\|assimp\|collada"` → 0 命中）；引擎侧 `fantasy/runtime/gltf_loader.h:6` 自陈"v1 范围：静态网格（POSITION/NORMAL/TEXCOORD_0 + 索引）+ PBR metallic-roughness 材质 + 节点层级变换"，`gltf_loader.cpp` 里 `skins|animations|JOINTS` 命中数为 0
- 现状：模块只能吃"已经解析好、已经排好父子顺序"的数据。仓库里唯一把资产喂进渲染的通路是 **demo 侧** `test/view/ModelLoader.h`（Assimp；`:306-332` 自建 `palette = node_world * mOffsetMatrix`（`palette.push_back(node_world * offset)` 在 `:329`），`:331-332` 交给 `open_skin`/`bone_palette`），它**完全绕过**本模块的 `skeleton`/`animation_clip`/`ik`；demo 的默认模型路径还硬编码为 Windows 盘符（`test/view/main.cpp:41`）。
- 证据：`grep -rni "gltf\|fbx\|assimp\|collada" fantasy/animation/*.h fantasy/animation/*.cpp`（空）；`grep -rn "JOINTS\|WEIGHTS\|skinnedMesh" fantasy/runtime/gltf_loader.*`（空）；`sed -n '306,333p' test/view/ModelLoader.h`
- 影响：任何要走"引擎自带动画"的项目，得自己写 glTF skin/animation 解析并把结果塞进 `gpu_bone[]`/`skeleton`；Engine 的 glTF 通路目前只对静态网格有效。
- 复现/验证：`grep -rli "assimp" --include=*.h --include=*.cpp fantasy/` → 0 命中（Assimp 只出现在 `test/`、历史台账与各模块文档里）。
- 建议：先把 glTF 的 `skins`/`animations` 解析接进 `gltf_loader`（输出直接喂 `skeleton`），再谈播放器。

### E3. `palette_ssbo()` 零消费者：GPU 蒙皮的输出没有任何渲染通路接到它
- 位置：`fantasy/animation/gpu_skinning.h:45`（定义）；全仓 `grep -rn "palette_ssbo()"` → **只有这一处定义**，0 个调用点。实际渲染走 CPU 通路：`fantasy/drawer.h:381-382`（`open_skin`）、`:415-416`（`bone_palette`），上传点 `fantasy/drawer.cpp:829-840` 与 `fantasy/theory_render.cpp:342-350`
- 现状：渲染侧把 `std::vector<matrix>` 调色板放进 **UBO** `bones[UBO_MAX_BONES]`（`UBO_MAX_BONES = 512`，`fantasy/backend/ubo.h:20`），顶点着色器按 `ubo.bones[skinIndex.x + 0.5]` 采样（`fantasy/backend/shader_sources.h:24-28`）；`gpu_skinner` 写出的 SSBO 没有任何 draw 绑定它。附带一个语义差：渲染侧对超过 512 根骨骼**静默截断**（`bone_count = min(palette.size(), 512)`，`drawer.cpp:833`、`theory_render.cpp:345`），而 GPU 侧无此上限（组数按 `(n+63)/64` 自算，`gpu_skinning.cpp:132`）——同一副骨架在两条路径上行为不同。
- 证据：`grep -rn "palette_ssbo()" --include=*.h --include=*.cpp .` → `fantasy/animation/gpu_skinning.h:45` 一条；`sed -n '829,840p' fantasy/drawer.cpp`
- 影响："palette SSBO 未接入网格渲染"确实成立：GPU 侧算完的调色板没有出口，用 `gpu_skinner` 的人必须自己写顶点着色器的 SSBO 采样路径；>512 骨骼的骨架在 CPU 路径上会掉骨骼且不报错。
- 复现/验证：`grep -rn "palette_ssbo\(\)" .`；`grep -rn "UBO_MAX_BONES" fantasy/backend/ubo.h fantasy/drawer.cpp`。
- 建议：要么把 SSBO 采样接进 `shader_sources.h` 的骨架路径（GL/VK 两版），要么明确把 `gpu_skinner` 标为"未接线实验件"。

### E4. IK 只有 CCD：无 FABRIK / 解析式两骨 / 关节限位 / pole 约束
- 位置：`fantasy/animation/ik.h:13-19`（`ik_solver` 只有一个 `solve_ccd`）
- 现状：`grep -rni "fabrik\|jacobian\|two_bone\|angle_limit\|pole\|limit" fantasy/animation/*.h fantasy/animation/*.cpp` → 0 命中；`solve_ccd` 也没有关节的角度/扭转限制参数，链末端只能"尽量靠近"目标（没有位置容差参数、没有失败返回）。
- 证据：`cat fantasy/animation/ik.h`（23 行，全文一个类一个静态函数）；`grep -n "iterations" fantasy/animation/ik.h` → `:18`（唯一参数，默认 10）
- 影响：需要"膝盖只向一侧弯"这类视觉约束的场景无法用现成 API；CCD 的贪婪特性在长链上也容易抖动（叠上 A2 更明显）。
- 复现/验证：`grep -rni "fabrik\|pole\|limit" fantasy/animation/ik.h fantasy/animation/ik.cpp` → 空（注意必须限定到源码：本台账 DEBUG.md 自己含这些词）。
- 建议：至少补关节限位（在 `rot` 之后钳制局部欧拉角）与一个 `tol`/`bool` 返回值。

### E5. 骨骼名只写不读；`animation_clip` 没有任何查询接口
- 位置：`fantasy/animation/skeleton.h:16`（`std::string name;`）、`skeleton.cpp:11`（唯一写入点 `b.name = name;`）；`animation.h:27-28`（`struct channel;` 私有前向声明 + `std::vector<channel> m_channels;` 私有）
- 现状：`add_bone` 收下的名字既不校验重复也不提供按名查找（全模块无 `find_bone`/`get_bone(name)`），`bone_node::name` 存下来后无人读取；`animation_clip` 只有 `duration()` 一个 getter，无法枚举通道、无法合并/裁剪 clip、无法知道某骨骼是否被动画（连"这根骨骼有没有通道"都要自己维护）。
- 证据：`grep -n "\.name" fantasy/animation/skeleton.cpp` → 仅 `:11`（`b.name = name;`）；`grep -n "name" fantasy/animation/skeleton.cpp fantasy/animation/skeleton.h` → 8 条命中，其中 4 条是 `namespace` 关键字（`skeleton.cpp:4/:6`、`skeleton.h:8/:10`），另 4 条是骨名的形参/成员/赋值（`skeleton.cpp:8/:11`、`skeleton.h:16/:30`）—— **没有一处是读骨名**，全模块不存在 `find_bone`/`get_bone`；`sed -n '25,30p' fantasy/animation/animation.h`
- 影响：从资产按骨名装配骨骼树的人只能自己维护 `name → index` 表；写混合/裁剪工具的人拿不到通道信息。
- 复现/验证：`grep -n "\.name" fantasy/animation/skeleton.cpp` → 只有 `:11`（`b.name = name;`）；`grep -n "name" fantasy/animation/skeleton.*` 的 7 条命中里另有 4 条是 `namespace` 关键字，与本条无关。
- 建议：补 `int find_bone(const std::string&) const`（并给重名告警），以及 `channel_count()`/按骨骼查询之类的最小查询面。

---

## F. 当前设计取舍（**不修**，写明理由与代价）

### F1. 采样走欧拉角分量线性插值，而不是四元数 —— 旋转走样是"已知代价"，不是漏改
- 位置：`fantasy/animation/animation.cpp:135-137`（分量 `lerp`）、`:143-146`（固定 `translate * rotate_z * rotate_y * rotate_x * scale`）
- 为什么现在这样是对的：`set_channel` 的公共契约就是"每个关键帧一个欧拉三元组"（`animation.h:33-39`），数据源（资产/手工表）也是欧拉；改成四元数要动公共 API 与所有调用方数据，而 `quat` 已在混合路径上（`animation.cpp:198`）覆盖了"两姿态之间"的插值需求——**采样是"同一通道内的关键帧插值"，混合是"两个结果之间"的插值**，后者才是体积塌缩高发处（M9 修正的正是后者）。
- 代价：大角度（相邻关键帧角差接近/超过 180°）或快速旋转时，欧拉分量线性插值会经过非预期姿态（万向节锁、角速度不匀）；README/根 README 里称之为"旋转走样"。此外欧拉插值的结果与 `quat::slerp` 混合的结果不在同一语义上（采样出的姿态再被 slerp 混合，是两套近似叠加）。
- 证据：`sed -n '130,148p' fantasy/animation/animation.cpp`；`sed -n '186,199p' fantasy/animation/animation.cpp`（混合走 slerp）
- 验证：`grep -n "quat" fantasy/animation/animation.cpp` → 只出现在 `slerp/from_matrix/to_matrix` 定义与 `blend` 里，`sample` 无四元数。
- 建议：**不动**。若将来要动，优先加"四元数通道"的新 API 而不是改 `set_channel` 语义。

### F2. GPU 输入用独立的 `gpu_bone`，不复用 `skeleton` —— 两条链零耦合是刻意的
- 位置：`fantasy/animation/gpu_skinning.h:19-24`（`gpu_bone` 自带 `local/parent/inv_bind`）；`skeleton` 无任何 RHI 依赖（`skeleton.h:6` 只含 `fantasy.h`），`gpu_skinner` 不引用 `skeleton`（`grep -rn "skeleton" fantasy/animation/gpu_skinning.*` → 0）
- 为什么现在这样是对的：CPU 侧可以纯头文件/无图形环境使用与测试，GPU 侧可以只依赖 RHI；两边各自只需"每骨骼一个局部矩阵 + 父索引 + 逆绑定"这一最小约定，互不牵制（`skeleton` 不被迫暴露 GPU 布局，`gpu_skinner` 不被迫依赖 `std::string` 骨名等）。
- 代价：没有 `skeleton → gpu_bone[]` 的转换辅助（`inv_bind` 要调用方另存一份，`skeleton` 只存 `bind_pose`），也没有"GPU 不可用则回落 CPU"的分支 —— 使用者必须自己写这层胶水（见 E3/C2）。两套世界姿态累乘（CPU DFS / GPU 祖先链循环）也各写了一遍。
- 证据：`grep -rn "skeleton" fantasy/animation/gpu_skinning.h fantasy/animation/gpu_skinning.cpp` → 空；`sed -n '19,33p' fantasy/animation/gpu_skinning.h`
- 验证：`grep -rn "to_gpu_bone\|from_skeleton" --include=*.h --include=*.cpp fantasy/` → 0 命中（不存在这样的辅助函数）。
- 建议：**不动**。真要用起来，加一个自由函数 `std::vector<gpu_bone> to_gpu_bones(const skeleton&)` 即可，别把两条链并成一个类。

### F3. 采样/混合是"增量写"：未涉及通道的骨骼保留 skeleton 里的旧姿态
- 位置：`fantasy/animation/animation.cpp:109-148`（只对 `m_channels` 里的骨骼 `set_local_pose`）、`:175-178`（`blend` 从 `out` 复制出 `sa/sb` 再采样）
- 为什么现在这样是对的：这样才允许"部分通道 clip"（例如只驱动上半身）与"未被动画管理的骨骼"（挂点、道具、程序化姿态）共存：调用方可以在采样前把自己的姿态写好，采样只覆盖它管的那几根，不必为每根骨骼都准备关键帧。
- 代价：切 clip 时，一个 clip 里**没有通道**的骨骼会残留上一个 clip 的姿态——循环播放 A 再切 B 时表现为"某些关节卡在 A 的最后一帧"，而且不报错；`blend` 也一样（两个 clip 都没覆盖的骨骼，结果就是 `out` 的旧值）。
- 证据：`sed -n '109,118p' fantasy/animation/animation.cpp`（循环体只遍历 `m_channels`）；`sed -n '174,179p' fantasy/animation/animation.cpp`
- 验证：`grep -n "set_local_pose" fantasy/animation/animation.cpp` → `:147`（sample）、`:212`（blend），都在通道循环里。
- 建议：**不改语义**；给 `animation_clip` 加一个 `reset(skeleton&)`（把无通道骨骼写回绑定/单位姿态）供需要的调用方显式调用。

### F4. 混合前用"列长"提取缩放（去缩放取纯旋转，再乘回）
- 位置：`fantasy/animation/animation.cpp:161-168`（`matrix_scale`）、`:186-208`（去缩放 → slerp → 缩放/平移 lerp → 乘回）
- 为什么现在这样是对的：本模块自己采出的局部姿态形状固定为 `T * Rz * Ry * Rx * S`（`:143-146`），此时 3×3 部分的三列长度**恰好**是 `sx, sy, sz`，`matrix_scale` 在这个约定下是精确的、零依赖、无分解迭代；M9 用它解决了"直接对矩阵逐元素 lerp 导致缩放丢失/体积塌缩"（历史台账 `fantasy_bugs.md:74` #30 记为已修）。
- 代价：对来自外部（不遵守上述乘积顺序、含剪切或非均匀缩放叠加任意旋转）的矩阵，列长 ≠ 真实缩放，去缩放会让旋转矩阵非正交，`from_matrix→slerp→to_matrix` 一路放大误差；`matrix_scale` 对退化列（长度 < `1e-8f`）直接把逆缩放取 1.0（`:192-193`），也不报错。
- 证据：`sed -n '161,168p' fantasy/animation/animation.cpp`；`sed -n '186,208p' fantasy/animation/animation.cpp`
- 验证：`grep -n "matrix_scale" fantasy/animation/animation.cpp` → `:162` 定义、`:187-188` 使用。
- 建议：**不动**。若接入外部矩阵，先在调用方做一次极分解或正交化。

### F5. `blend` 的 `weight` 不钳位（头注释只写约定 `[0,1]`）
- 位置：`fantasy/animation/animation.h:47`（注释里的 `weight∈[0,1]` 前置条件；同一行的"矩阵线性插值"是 G1 的陈旧描述）、`animation.cpp:170-213`（`weight` 直接传进 `slerp`/`lerp`，无钳制）
- 为什么现在这样是对的：过渡曲线由调用方控制（缓动、按帧率的 dt 累积都在外面），静默钳位会把调用方的动力学错误藏起来；`quat::slerp` 在 `t∉[0,1]` 时退化为外插，语义上仍有定义，不会崩。
- 代价：`weight = 1.2` 或 `-0.1` 会静默产出外插姿态（旋转越过目标、缩放被外插成负值等），排查时看起来像"混合算法坏了"。
- 证据：`sed -n '170,175p' fantasy/animation/animation.cpp`；`sed -n '198,211p' fantasy/animation/animation.cpp`（`weight` 未钳）
- 验证：`grep -n "weight" fantasy/animation/animation.cpp` → 全部是直接使用，无 `min/max`。
- 建议：**不改行为**；在 README/头注释里把"不钳位"写明（本次 README 已补）。

### F6. `update()` 只在骨骼数不变时原地更新，骨骼数变化必须重新 `build()`
- 位置：`fantasy/animation/gpu_skinning.h:39-40`（注释"骨骼数量不变"）、`gpu_skinning.cpp:110`（数量不符直接 return）
- 为什么现在这样是对的：管线与三个 SSBO 的尺寸在 `build` 时定死，`update` 只做数据面重传，省掉每帧重建管线/描述符（Vulkan 侧还要重建 descriptor set）；骨骼数在运行期几乎不变，"每帧重建"才是浪费。
- 代价：骨骼数确实变化时（换模型、LOD 换骨骼集）必须 `build`，而重复 `build` 会泄漏上一组资源（B3）；配合 B4 的静默 return，忘记重建的表现是"一直用旧姿态"。
- 证据：`sed -n '37,47p' fantasy/animation/gpu_skinning.h`；`sed -n '108,111p' fantasy/animation/gpu_skinning.cpp`
- 验证：`grep -n "m_bone_count" fantasy/animation/gpu_skinning.cpp` → `:104` 赋值、`:110`/`:127`/`:139` 判断、`:132` 算组数、`:140-143` 读回，尺寸在 build 后不变。
- 建议：**保持语义**，但给 `update` 加一条"数量不符"的告警（与 B4 同一处修复）。

---

## G. 文档与代码的出入

### G1. `animation.h:47` 的注释还写着"矩阵线性插值"，实现早已是 slerp
- 位置：`fantasy/animation/animation.h:47`（`// 动画混合：两个 clip 的采样按权重混合（矩阵线性插值，weight∈[0,1]）`）vs 实现 `fantasy/animation/animation.cpp:186-208`
- 现状：M9 修正把混合改成了"去缩放 → `quat::slerp` → 缩放/平移 lerp → 乘回"，头文件注释没跟着改；同一文件的 `animation.h:13` 注释倒是记了"slerp 替换矩阵线性插值"。`animation.cpp:151-159` 里的 `lerp_matrix` 是 M9 之前的逐元素 lerp 残留，现在**无人调用**（死代码，且 GCC `-Wall -Wextra` 不告警）。
- 证据：`sed -n '45,50p' fantasy/animation/animation.h`；`grep -n "lerp_matrix" fantasy/animation/animation.cpp` → 仅 `:152` 一处（定义，无使用）
- 影响：只读头文件的人会以为混合是线性插值，照此推断"混合会塌缩"并做出错误决定；`lerp_matrix` 留着会让后来者以为它在用。
- 复现/验证：`grep -n "lerp_matrix" fantasy/animation/animation.cpp`；`g++ -std=c++17 -DXHQM_PROGRAM -I. -Ifantasy -Ifantasy/backend -Ihardware -Isoftware -I../depend/glew/include -I../depend/nlohmann -Wall -Wextra -fsyntax-only fantasy/animation/animation.cpp` → 0 条"未使用"告警（所以没人被提醒）。
- 建议：改注释为"slerp 混合旋转 + 线性混合缩放/平移"，并删掉 `lerp_matrix`。

### G2. 本模块 README 曾声称"GPU↔CPU 一致性已自动比对（容差 1e-4）"——那段代码在 `#if 0` 里
- 位置：`test/view/main.cpp:465-519`（`// [GSKIN] M9 GPU 蒙皮验证…` 紧跟 `:466 #if 0`，`:519 #endif`；比对在 `:503-505`，容差 `1e-4f` 在 `:504`；`read_palette()` 在 `:497`）；旧表述原在本模块 README 第 175 行
- 现状：整块自检被 `#if 0` 停用（同文件 `:522` 起是另一块同样停用的 Hi-Z 自检），因此**当前没有任何自动化的 GPU↔CPU 一致性验证**在跑。此外本仓没有任何测试目标编入本模块的 `.cpp`：`test/CMakeLists.txt` 的 `ecs_test`/`toon_test` 与 `test/ffbo/CMakeLists.txt:25` 只在 include 目录里挂了 `fantasy/animation`，源文件列表里没有 `skeleton.cpp`/`animation.cpp`/`ik.cpp`/`gpu_skinning.cpp`（`grep -rn "animation" test/CMakeLists.txt test/view/CMakeLists.txt test/ffbo/CMakeLists.txt` → 5 处**全是 include 目录**：`test/CMakeLists.txt:268`、`:312`、`test/view/CMakeLists.txt:71`、`:86`、`test/ffbo/CMakeLists.txt:25`；没有任何 `animation/*.cpp` 源文件项）。
- 证据：`grep -n "GSKIN\|^#if 0\|^#endif" test/view/main.cpp` → `:465/:466/:497/:504/:519`；`grep -rn "animation" test/CMakeLists.txt test/view/CMakeLists.txt test/ffbo/CMakeLists.txt` → 5 处目录项、0 处源文件项
- 影响：接手者按 README 会以为"有回归网"，实际四个 TU 只有"能编译"这一层保障（主库构建里编译，见 `build/CMakeFiles/xhqm.dir/link.txt` 含四个 `.o`），CPU 侧（`skeleton`/`animation_clip`/`ik`）**零测试**。
- 复现/验证：`sed -n '465,467p;496,506p;519p' test/view/main.cpp`；`grep -rn "solve_ccd\|animation_clip\|compute_palette" test/` → 0 命中。
- 建议：把该块挪出 `#if 0` 并给它一个能跑的目标（或至少让 CPU 三件套进 `test/`），README 的"自动验证"表述已按本文件订正（见本模块 README 的作废标注）。

### G3. 父模块 `fantasy/README.md` 说 `animation_clip` 走"四元数 slerp 采样"——与代码不符
- 位置：`fantasy/README.md`（本次读到 `:375`：`- \`skeleton\` 描述骨骼层级；\`animation_clip\` 做关键帧 + 四元数 slerp 采样；\`animation_blender\` 做过渡混合（保留缩放分量）；\`ik_solver\` 做 CCD。`）
- 现状：采样是欧拉角分量线性插值（`animation.cpp:137`），`quat::slerp` 只用在 `animation_blender::blend`（`animation.cpp:198`）。同一段里 `:376`（"`gpu_skinner` 把调色板计算搬到 compute 着色器 …结果写 SSBO"）与 `:377`（"最简用法：每帧更新 `bone_palette` 即可驱动 LBS 蒙皮"）都成立，唯独这一句把两条路径记混了。
- 证据：`grep -n "四元数 slerp 采样" fantasy/README.md`（命中父 README 动画小节的 `skeleton …` 那一行）；`grep -n "quat::slerp" fantasy/animation/animation.cpp` → `:198`（在 `blend` 内）。
- 影响：读者按父 README 描述去调 `animation_clip`，会预期"旋转已经 slerp 过"，从而在大角度关键帧上放过插值走样（F1）。
- 复现/验证：`grep -n -A3 "### 动画" fantasy/README.md`。**父 README 在本会话期间正被并行编辑（行号会漂移），引用以引文为准。**
- 建议：父 README 改为"关键帧 + 欧拉分量线性插值采样，slerp 用于双 clip 混合"。**本次只允许改本模块的两份文档，故不在本次修改**，登记于此待父模块维护者处理。

### G4. 根 `README.md:242-244` 的旧表述：四条"劣势"+四条"缺失"逐条复核结果
- 位置：`README.md:242`（优势）、`:243`（劣势）、`:244`（缺失）（本次读到的行号；根 README 同期也在被并行编辑，引用以引文为准）
- 现状（逐条核对 r677 的结论）：
  - `:243` 的四条**全部为真**，本文件已逐条落成条目：① `solve_ccd` 每关节重算整树（D1）；② clip 采样欧拉角线性插值 + 线性搜索（F1 定性、D2 性能）；③ `gpu_skinner` 每帧全量重传（D3）；④ `read_palette` 依赖 GPU→CPU 读回（D5）。
  - `:244` 的四条**全部为真**：无播放控制器（E1）、无资产导入（E2）、palette SSBO 未接入网格渲染（E3）、IK 仅 CCD（E4）。
  - `:242` 的两条需要加限定：⑤"CPU/GPU 蒙皮数学约定一致并有 1e-4 容差自动验证"——约定一致为真（`skeleton.cpp:77` 与 `gpu_skinning.cpp:52`），但"自动验证"当前不编译（G2）；⑥"GPU 蒙皮走 compute+SSBO 零读回"——`compute()` 自身不读回为真，但输出无消费者（E3），而 `read_palette()` 是同步读回（D5）。
- 证据：`sed -n '241,245p' README.md`；本文件 A1/A2/A3/D1-D6/E1-E5/G2 各条的行号。
- 影响：根 README 的"优势/劣势"是接手者最先读的一段，⑤⑥ 不加限定时容易把"实验件"当"已闭环"。
- 复现/验证：`sed -n '242,244p' README.md`。
- 建议：根 README 的 ⑤⑥ 补上"自检在 `#if 0` 内、SSBO 无消费者"两处限定（**不在本次允许修改的文件内**，登记于此）。

### G5. `gpu_skinning.h:7` 的注释「无 CPU 读回即写即用」与同文件 `read_palette()` 并存，易被读成"已闭环"
- 位置：`fantasy/animation/gpu_skinning.h:5-9`（`// …写入 SSBO（与 gpu_culler 同构，无 CPU 读回即写即用；read_palette 仅供验证/调试读回）`）、`:43-45`（`read_palette()` 与 `palette_ssbo()` 声明）
- 现状：注释本身没写错（`compute()` 确实不读回），但"即写即用"是照抄 `gpu_culler` 的说法——`gpu_culler` 的输出真的被 indirect draw 消费（`gpu_culler.cpp:157` 的 `draw_indexed_indirect_storage(vertex, index, m_output_ssbo, 0, m_object_count)`），`gpu_skinner` 的输出**没有**消费者（E3）。而 `read_palette()` 恰恰是"读回"，且是同步停顿读回（D5）。父模块把这条前提讲得更满：`fantasy/README.md:51`（本次读到）把 `gpu_skinner` 与两个 culler 并列，称它们"把结果写入 SSBO，再经 `draw_indexed_indirect_storage` 零 CPU 读回消费"——对 `gpu_skinner` 不成立（同文件 `:398` 的同类表述就只列了两个 culler，是对的）；该行还称"这条链目前只有 `test/view/main.cpp` 的自检块在跑"，而那个自检块正是 `#if 0`（见 G2）。
- 证据：`sed -n '5,9p' fantasy/animation/gpu_skinning.h`；`grep -rn "palette_ssbo()" .` → 只有 `:45` 定义；`sed -n '51p;398p' fantasy/README.md`；`grep -n "draw_indexed_indirect_storage" fantasy/spatial/gpu_culler.cpp` → `:157`。
- 影响：读注释/父 README 的人会以为默认路径已经是"GPU 算完直接给顶点着色器"，于是不去接那条线（E3 的成因之一）。
- 复现/验证：`grep -rn "draw_indexed_indirect_storage" fantasy/spatial/gpu_culler.cpp`（有消费）对比 `grep -rn "palette_ssbo()" .`（无消费）。
- 建议：注释改为"输出写在 SSBO 里，但当前没有渲染侧消费者；read_palette 仅供帧外验证"。本模块 README 已按此订正。

### G6. 构建清单漏了 `gpu_skinning.h`（头文件清单与实际文件集不符）
- 位置：`CMakeLists.txt:433-435`（`XHQM_HEAD_FANTASY` 里只有 `"fantasy/animation/skeleton.h"`、`"fantasy/animation/animation.h"`、`"fantasy/animation/ik.h"`）、对照 `:424` 已列 `"fantasy/spatial/gpu_culler.h"`、`:461-466`（`XHQM_BODY_FANTASY` 里四个 `.cpp` 齐全）
- 现状：同一模块 4 个头文件只登记了 3 个，`gpu_skinning.h` 缺失（`fantasy/spatial/gpu_hiz.h`、`gpu_hiz_culler.h` 也不在清单里，属同类疏漏）。这些清单在 `:565` 被 `list(APPEND XHQM_HEAD …)` 并最终传给 `add_library`，头文件不参与编译，所以**不影响能不能编过**，影响的是 IDE 工程视图/安装与"文件集是否完整"的自检。另需知道：模块的 4 个 `.cpp` 在 `XHQM_BUILD_FANTASY` 打开时**无条件**编入（与 `_render_gl`/`_render_vk` 无关），因此 GL-only 构建里 `gpu_skinning.cpp` 也照样编译（`build/CMakeFiles/xhqm.dir/link.txt` 含 `gpu_skinning.cpp.o`、不含 `rhi_vk.cpp.o`）。
- 证据：`sed -n '433,435p;461,466p' CMakeLists.txt`；`grep -n "gpu_skinning.h" CMakeLists.txt` → 0 命中；`grep -c "gpu_skinning.cpp.o" build/CMakeFiles/xhqm.dir/link.txt`。
- 影响：按清单找"模块有哪些头"的人会漏掉 GPU 蒙皮；打包/安装只按清单复制时 `gpu_skinning.h` 会缺席。
- 复现/验证：`grep -n "animation/" CMakeLists.txt`（应看到 3 头 + 4 源）。
- 建议：把 `"fantasy/animation/gpu_skinning.h"` 补进 `:435` 之后（`gpu_hiz*` 同理，属其它模块）。

---

## 本轮实测数字（可复核）

- 文件规模（`wc -l`，r677）：`skeleton.h 50`、`skeleton.cpp 87`、`animation.h 59`、`animation.cpp 216`、`ik.h 23`、`ik.cpp 64`、`gpu_skinning.h 51`、`gpu_skinning.cpp 155`，合计 **705 行**（本模块 README 本次改动前 175 行、改动后 227 行；本台账 307 行）。
- 编译：`g++ (Ubuntu 11.4.0) -std=c++17 -DXHQM_PROGRAM -I. -Ifantasy -Ifantasy/backend -Ihardware -Isoftware -I../depend/glew/include -I../depend/nlohmann -Wall -Wextra -fsyntax-only` 逐个跑四个 TU → 全部 `exit 0`；模块内 **0 条警告**，输出里仅有的 3 条 warning 全部来自 `algebra/geometry/geometry.h:254-258`（`-Wreorder`，属其它模块）。
- 常量/上限：`local_size_x = 64`；dispatch 组数 `(n + 63) / 64`；着色器父链 `guard < 1024`；每骨骼 bones 记录 36 word = 144 B；params 16 B；palette 每骨骼 64 B；`solve_ccd` 默认 `iterations = 10`；`slerp` 的 nlerp 阈值 `cosom > 0.9995f`；`matrix_scale` 的退化阈值 `1e-8f`；`invmat` 奇异阈值 `xhqm::preci<float> = 1e-5f`；CPU 渲染侧 `UBO_MAX_BONES = 512`。
- 调用点（全仓 `grep`）：`palette_ssbo()` → 1（仅定义）；`solve_ccd`/`animation_clip`/`animation_blender`/`compute_palette` 在本模块之外 → 0；`gpu_skinner`/`read_palette` → 仅 `test/view/main.cpp:492/:497`，且在 `#if 0` 内。
- 宿主平台请求：`test/view/RhiViewport.cpp:37` 与 `test/ffbo/RhiViewport.cpp:55` 均为 GL **3.3 Core**；引擎后端名 `"opengl"`/`"vulkan"`/`"none"`；Vulkan 设备创建只启用 `robustness2.nullDescriptor`，未启用 `robustBufferAccess`。
