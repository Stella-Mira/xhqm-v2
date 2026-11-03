# 渲染每绘制开销的改法（**已实施** —— 授权后落地，r548）

> 状态：**已实施并验证**。V1（CPU 暂存）+ V2（按「已用过」的 64B 块复制）已落地，
> 另有 304 硬编码偏移修正、`ploys` 合批、`--shadow-size` 空操作修复。
> 最初记录方案的一版是 r547（当时未动代码）。

## 0. 结果（一句话）

帧图执行 **110ms → 1.6ms（约 65×）**，端到端 **5.5 → 13.6 fps**，
**渲染结果与改前逐像素相同**（同一步号快照 SHA256 一致），**物理轨迹逐位不变**。

| 量 | 改前 | 改后 |
|---|---|---|
| 裸 `rhi.draw()`（3 顶点） | 0.183ms 墙钟 / 0.156ms CPU | **0.004ms / ≈0** |
| `shadow` pass | 54.8ms | 1.06ms |
| `scene` pass | 54.6ms | 0.67ms |
| `tonemap` pass | 0.45ms | 0.17ms |
| 帧图执行合计 | 109.8ms | **1.6ms** |
| 进程 CPU（并行池自旋） | 2839%（32 逻辑核顶满） | 已随 r544 的退避等待解决 |
| 端到端 | 5.5 fps | **13.6 fps** |

**逐像素对照**（同参数 `--sec 3 --snapshot-step 100`）：

```
改前 DLL  _ab_pre.png   EFD3CC34644B16EC32B5553E414A09C1BD3F78E29F6965C86975AA5BE02B0B62
改后 DLL  _final.png    EFD3CC34644B16EC32B5553E414A09C1BD3F78E29F6965C86975AA5BE02B0B62
阴影关    _chk_off.png  A2C7113F…（与上不同 ⇒ 阴影通路确实在起作用）
```

**物理**：`--sec 5` 末位置 `(-0.9860 0.1505 0.5552)`，与基线逐位相同。

## 0.1 实际改了什么（与最初方案的差异）

最初方案写的「只复制脏前缀」是**错的**：`world`(33152) 排在 32KB 的 `bones[512]` 之后，
前缀会到 34000（89%），几乎没收益。**正确且等价的判据是「曾经被写过」而不是「本次改过」**——
UBO 是有状态的块，一次绘制要读到之前设定的字段，而目的地是本次绘制独占的新 slot，
所以必须把「所有可能被着色器读到的块」都带过去（只置位、不清除）。这样
`bones`/`instances` 这两段 32KB+4KB 在不写骨骼/实例时**整段跳过**，主着色器只剩约 1.4KB。

V3（重排布局）因此**不再需要**：跳过未使用块已经拿到了同样的效果，而且不动着色器 std140 声明。
V4（描述符集环）仍未动 —— 现在每帧绘制数降到 445，环 1024 份约每 2 帧耗尽一次，
是否还有感知到的尖刺需要单独量。


## 0.2 V4 也做了：描述符集环改成「每 program 独立 + 回卷零等待」（r553）

**问题**：`MAX_DESC_SETS = 1024` 的环，原来轮转计数 `m_desc_rotation` 是**一个全局计数被所有
program 共用**，消耗速度 = 全场总绘制数/帧。果蝇场景 445 绘制/帧 ⇒ 每约 2.3 帧转满一圈，
触发一次 `vkDeviceWaitIdle`，再回卷到本帧起点。

**改法**：
- 轮转改成**每 program 自己一份**（`program_t::desc_rot`）。每个 program 每帧只用几十到一百多次，
  回卷周期变成 1024/(该 program 每帧绘制数) ≈ 4~6 帧，而不是全场总绘制数决定的 2.3 帧。
- 回卷不再无条件等设备空闲，改用**可判定的安全性**：要覆写的那份集是「上一次回卷」时写的，
  只要从那之后过了 ≥ 在飞帧数那么多帧，那时的帧必然已完成（`begin_frame` 每帧都在等 ic 帧前那根
  fence）⇒ 直接回卷，零等待。达不到就退一次「只等自己提交的帧」（`wait_submitted()`），
  不再用 `vkDeviceWaitIdle`（它会把呈现队列一并排空，FIFO 下可能连带等垂直同步）。
- 单帧同 program 绘制 >1024（整环被本帧吃满）时保留老行为兜底：此时环里任何回卷点都还被本帧
  已录制的命令引用，不可能安全复用。

**实测（新加的诊断量，`--sec 10`，500~600 帧）**：

| | 平均 | 首帧 | 稳态单帧最大 | >20ms 帧数 |
|---|---|---|---|---|
| 旧（全局环 + vkDeviceWaitIdle） | 1.6ms | 9.5ms | — | 0 |
| 新（每 program + 零等待） | 1.6ms | 9.8ms | **2.5ms** | 0 |

直接观测回卷行为（临时探针）：交换链只有 **2** 张图（在飞 2 帧），每次回卷的 gap 都是 4~6 ≥ 2
⇒ **全部走零等待快路径，一次 `vkDeviceWaitIdle` 都没有**。

**诚实结论**：在这一个场景里，**A/B 没有测出可感知的差别**（旧路径的单帧最大也是 9.5ms，同样
>20ms 的帧为 0）—— 因为每帧 GPU 只落后约 1.6ms，设备空闲很快就返回了。所以这次改动是
**结构性**的收益，不是这一个场景的提速：
1. 旧代码的环消耗速度由**全场总绘制数**决定。任何单帧总绘制数 >1024 的场景（9 角色 MMD 场景
   主 pass ~190 + 阴影 ~190、或任何更复杂的场景），旧代码是**每帧**都触发一次设备空闲；
   新代码按 program 计，正常负载下根本不回卷。
2. 把 `vkDeviceWaitIdle`（设备级、含呈现队列）换成只等自己提交的 fence。
3. 回卷从「无条件等」变成「可判定安全则零等待」。

正确性：图像 SHA256 仍是 `EFD3CC34…`（与改动前一致），物理末位置不变。


### 1.1 病灶

`fantasy/backend/rhi_vk.cpp`，4 个绘制函数各有一份（`draw_instanced` / `draw_indexed_instanced`
只是薄封装，转发到前两个，没有自己的复制）：

| 行 | 所在函数 | 内容 |
|---|---|---|
| 2470 | `draw()`（2440 起） | `std::memcpy((char*)p.ubo_data + dynamicOffset, p.ubo_data, UBO_SIZE);` |
| 2518 | `draw_indexed()`（2488 起） | 同上 |
| 2579 | `draw_indexed_indirect()`（2552 起） | 同上 |
| 2622 | `draw_indexed_indirect_storage()`（2595 起） | 同上 |

紧邻这 4 处之前各有一行同款前置写入（同一模式复制了 4 遍）：

```cpp
std::memcpy((char*)p.ubo_data + UBO_CAMERA_POS + 12, &m_point_size, sizeof(float));   // 2469 / 2517 / 2578 / 2621
```

- `UBO_SIZE = 38240`（`backend/ubo.h:44`）
- `ubo_data` 是 `vkMapMemory` 得到的**常驻映射指针**（`rhi_vk.cpp:1430`），
  内存类型取 `HOST_VISIBLE | HOST_COHERENT`（`rhi_vk.cpp:1421`）——在 Windows 上大概率是
  write-combined，**读**它非常慢。
- 环形缓冲 `ubo_bytes = ustride × UBO_SLOTS`，`UBO_SLOTS = 1024`（`rhi_vk.cpp:67`）
  ⇒ 每个 program 约 **39 MB**；逐绘制换槽 ⇒ TLB / cache 持续失效。

量级核算：38240 B ≈ 600 条 cache line，不可缓存读 ≈ 600 × 250ns ≈ 150µs，与实测 0.156ms CPU 相符。

### 1.2 为什么是「整块复制」——必须保留的约束

`memcpy` 旁的注释解释了来历：

> 记录期写 host-coherent 槽，消除 `vkCmdUpdateBuffer` 执行期读共享 slot0 的跨帧竞态

即：**数据必须在命令录制期就固化进本绘制独占的 slot**，执行期不能再读共享的 slot0。
任何改法都必须保持这一点，否则会重新引入跨帧竞态（表现为闪烁 / 花屏）。

### 1.3 UBO 布局：38KB 里 32KB 是骨骼

`backend/ubo.h:23-44`：

| 偏移 | 字段 | 大小 |
|---|---|---|
| 0 | `tranMatrix` | 64 |
| 64 / 80 / 96 / 112 | color / select / lightRGB / lightXYZ | 16×4 |
| 128 | `flags[16]` | 256 |
| **384** | **`bones[512]`** | **32768** |
| 33152 | `world` | 64 |
| 33216 | `lights[8]` | 512 |
| 33728 | `lightVP[4]` | 256 |
| 33984 | cascadeSplits / cameraPos / colorGrade / ambient / material×5 / toonRim | ~160 |
| 34144 | `instances[64]` | 4096 |
| 38240 | 合计 | |

**关键点**：`world`（每绘制都写）在 33152，排在 32KB 骨骼数组**之后**。
所以「只复制到最高已写偏移」这种最省事的做法，前缀依然会到 ~34000（89%），几乎没用。
真正有效的只有「按脏区间复制」或「重排布局」。

### 1.4 可照抄的既有正确实现：GL 后端

`backend/rhi_gl.cpp`：

```cpp
// 73:      struct program_ubo_gl { ... std::vector<char> data; bool dirty; ... }
// 627:     std::memcpy(it->second.data.data() + off, &v, sizeof(v));   // uniform 写 CPU 内存
// 106-117: bind_ubo()  绘制前：if (dirty) { glBufferSubData(..., UBO_SIZE, data.data()); dirty = false; }
```

- uniform 写在**普通堆内存**里（cached）；
- 每绘制只上传一次（且有真正的 dirty 守卫）。

顺带：`rhi_vk.cpp` 的 `ubo_dirty`（`rhi_vk.cpp:297`）**只被赋值 true、从没被读过**，是死标志位。

## 2. 方案

### V1（第一步，改动最小，先量收益）：CPU 暂存 + 上传

把 Vulkan 侧改成与 GL 同构。

1. `program_t` 加 `std::vector<char> ubo_stage;`（`UBO_SIZE` 字节，创建 program 时 `assign(UBO_SIZE, 0)`），
   位置在 `rhi_vk.cpp:282` 起的 `program_t` 内。
2. 6 处 uniform 写入（`rhi_vk.cpp:2382 / 2393 / 2404 / 2414 / 2424 / 2434` 与 2436）
   目标从 `it->second.ubo_data + off` 改为 `it->second.ubo_stage.data() + off`。
3. 4 处绘制路径里的两次 `memcpy` 改为：

   ```cpp
   std::memcpy(p.ubo_stage.data() + UBO_CAMERA_POS + 12, &m_point_size, sizeof(float));
   std::memcpy((char*)p.ubo_data + dynamicOffset, p.ubo_stage.data(), UBO_SIZE);
   ```

   读侧从「不可缓存映射内存」变成 L2 命中的 37KB（连续、顺序、可预取，约 1–2µs）。
4. program 销毁处（`rhi_vk.cpp:1502` 附近）无需改：`ubo_stage` 随对象析构。

**预期**：0.156ms → 约 0.002–0.01ms/绘制（写侧仍是 WC 顺序写，很快）。
`--no-shadow` 下 85 绘制从 16.7ms 降到 ~1ms。**风险极低**，语义与 GL 一致。

**必须自查**：`rhi_vk.cpp:2436` 有一个硬编码偏移 `memcpy(ubo_data + 304, &lc, sizeof(lc))`
（写 `FLAG_LIGHT_COUNT` 的 int）。它绕过了 `ubo_offset()`，改 V1 时容易漏改这一处，
漏了会表现为「多光源数量错误 / 光照异常」。建议顺手把它改成 `ubo_flag(FLAG_LIGHT_COUNT)`。

### V2（第二步，收益再翻倍）：按脏区间复制

V1 之后瓶颈就只剩 37KB × 绘制的复制与 WC 写。再加脏区间：

1. `program_t` 加 `uint64_t ubo_dirty_mask[UBO_SIZE/64/64 + 1]`（38KB / 64B 粒度 = 598 位 ≈ 10 个 u64），
   每个 uniform 写入时置位对应区间。
2. 绘制时遍历置位区间，逐段 `memcpy(src = ubo_stage, dst = ubo_data + slot + off, len)`，然后清 mask。
3. 粒度取 64B（cache line），区间合并后典型一次绘制 10–20 段、合计 1–2KB。

**预期**：每绘制从 37KB 降到 ~2KB。**风险低**（mask 只影响复制范围，见 3.1 的正确性论证）。

### V3（可选，结构性）：重排布局 + 缩小 UBO

把常用字段提到前面、`bones[512]` 与 `instances[64]` 挪到最后，让「只复制已写前缀」变得有效
（典型前缀 ≈ 1.3KB）。更彻底的做法是按 `UBO_MAX_BONES` 编译两套着色器（无骨骼 / 有骨骼）。

**代价**：`ubo.h` 的偏移常量与 `ubo_layout_string()`（`ubo.h:130` 起）以及 GLSL/VK 的 std140 声明
必须同步改，**影响 GL 与 Vulkan 两侧**，回归面最大。建议放到 V1/V2 验证之后再评估。

### V4（正交，另一处每帧停等）：描述符集环 1024 份

`rhi_vk.cpp:514-523` `advance_desc_set()`：绘制数超过 `MAX_DESC_SETS = 1024` 时
`vkDeviceWaitIdle` 等 GPU 空闲再回卷。当前每帧 173（关 gizmo 关阴影）～571（全开）次绘制，
即**每 2–6 帧就会整机停等一次**。单独作为一个「平滑度」问题查（掉帧尖刺而非平均帧率）。
方向：环大小按「单帧最大绘制数」对齐，回卷只在确认本帧 fence 已过之后做。

## 3. 正确性论证与风险

### 3.1 为什么 V1/V2 语义等价

- 原语义：slot N 必须等于「本次绘制的完整 uniform 状态」= slot0 的内容。
- V1：slot0 改由 CPU 堆内存充当来源，内容与原先「写进映射内存的 slot0」逐字节相同
  （同样的 `off`、同样的 `bytes`、同样的写入顺序）。**更强的是**：CPU 堆内存不参与 GPU 可见性，
  原先注释担心的「执行期读共享 slot0」在这个方案里根本不存在。
- V2：只复制置位过的 64B 区间。着色器只会读**声明过且被赋值**的字段；
  未写过的字节在 V1 里也是「映射内存的原始内容」（本来就不确定）。
  为了让 [hi, UBO_SIZE) 变成确定值，建议在创建时 `std::memset(p.ubo_data, 0, ubo_bytes)`
  一次（39MB，一次性 ~10ms），此后未写区域恒为 0，两侧行为完全一致。

### 3.2 风险清单

| 风险 | 说明 | 缓解 |
|---|---|---|
| 漏改 304 硬编码偏移 | 见 2.V1 自查 | 改成 `ubo_flag(FLAG_LIGHT_COUNT)` |
| 4 份复制粘贴代码不同步 | 同一模式在 4 个绘制函数里重复 | 抽成一个 `inline void emit_draw_ubo(program_t&, uint dynamicOffset)` |
| 环回卷与在飞帧 | `advance_desc_set()` 的 GPU 停等 | 不在本次改动范围（V4 单独做） |
| 多窗口 | 每个 surface 各自 `cmd_bufs`，UBO 是 **per-program 全局**（`m_programs`），与窗口无关 | 现状即如此，V1/V2 不改这个结构 |
| MoltenVK / 其它平台 | `ubo_stage` 是纯 CPU 侧，不引入平台差异；反而去掉了「双重 map 失败」那类坑 | — |

### 3.3 影响面与回滚

- 改动集中在 `fantasy/backend/rhi_vk.cpp`（1 个文件，约 15 行）+ 可选 `backend/ubo.h`。
- 不新增依赖、不改公开接口、不改 GL 路径。
- 回滚 = 还原该文件；也可用 `XHQM_*` 环境变量做开关先灰度（若需要）。

## 4. 验收（用已有开关，可直接跑）

前置：`cd D:\WorkSpace\xhqm_v2\xhqm\test\ffbo`，PATH 需含 Qt/mujoco/glew。

```powershell
# A. 裸绘制单价（核心判据）：现状 0.183ms 墙钟 / 0.156ms CPU
.\build\fly_mcns_view.exe <malecns> <fruitfly.xml> --sec 2 --bench-off 200 --hide-after 0 --no-gizmo

# B. 逐 pass（现状：shadow 54.8 / scene 54.6）
.\build\fly_mcns_view.exe <malecns> <fruitfly.xml> --sec 3 --perf-pass

# C. 端到端帧率（现状：全开 5.5fps / --no-gizmo 9.3 / +--no-shadow 11.2）
.\build\fly_mcns_view.exe <malecns> <fruitfly.xml> --sec 5 --no-gizmo --no-shadow
```

判据：

| 量 | 现状（r544） | 改后目标 |
|---|---|---|
| 裸 draw 单价 | 0.183ms | ≤ 0.02ms |
| `--no-gizmo --no-shadow` 帧图执行 | 17.4ms（其中 85×0.196≈16.7） | ≤ 3ms |
| 全开（含 gizmo 199 绘制）帧图执行 | 110ms | ≤ 10ms |
| 端到端 | 5.5 / 9.3 / 11.2 fps | 全开应接近 `--no-render` 的水平 |

**物理轨迹必须逐位不变**：渲染改动不得影响仿真。r544 已验证 `--no-gizmo`、`--no-shadow`
前后末位置都是 `(-0.9860 0.1505 0.5552)`，改完底层后要用同一命令复核这一条。

## 5. 独立项 P-B：`ploys` 每个 topol 一次 draw

- 现场：`fantasy/element.cpp:167` `ploys::render()` 对 `m_topols` 逐个 `rhi.draw()`，
  一个 `coord` gizmo（4 个 drawer：1 个 `poits` + 3 个 `arrow`）发出 **199 次绘制**。
- `arrow::make()`（`element.cpp:544`）是 2 个 11 段 frustum 网格，几十个 quad，每个 quad 一个 topol。
- 改法（两步，第二步才是正经解）：
  1. **按连续同顶点数的 topol 合并成 run**，每种图元一次 `rhi.draw`：quad 网格从 ~66 次降到 1–3 次。
     改动小（`ploys::updata()` 里预计算 run 表 + `render()` 里按 run 遍历）。
  2. 正经解：建立索引缓冲，一次 `draw_indexed`。`prefab` 已经有 `m_index`（`drawer.h:318`）可参照。
- 收益：gizmo 78ms/帧 → 基本归零。**但这只是通用性的问题**：任何用 `ploys`（线框/点云/几何辅助体）
  的场景都会遇到，与果蝇无关。我们这边已有 `--no-gizmo` 兜底，不急。

## 6. 现阶段已可用的替代（不改底层）

| 手段 | 效果 |
|---|---|
| `--no-gizmo` | 不创建 gizmo。5.5 → 9.3 fps |
| `--no-shadow` | `theory_shadow::enabled(false)`（公开 API）。9.3 → 11.2 fps |
| `--no-render` | 纯仿真跑实验，不快照 |
| 并行池退避等待（已默认） | 32 核不再被自旋顶满；MuJoCo 60.5 → 45.2ms |

不改底层时，**渲染已不再是最大单项**：`--no-gizmo --no-shadow` 下每步 ≈ 8(raycast) + 18(网络)
+ 45(MuJoCo) + 17(渲染) ≈ 88ms，MuJoCo 变成第一大头。
