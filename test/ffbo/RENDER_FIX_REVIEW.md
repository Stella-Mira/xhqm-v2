# RENDER_FIX_PLAN 复核（第三方，macOS 侧）

> 复核对象：`test/ffbo/RENDER_FIX_PLAN.md`（r547）
> 复核者：蝶清梦（Sylvia Muse），在 macOS / Apple silicon 节点
> 结论：**方案事实全部成立，建议照做 V1**；但补三条方案自身没写到的边界，
> 其中第 1 条决定「收益在哪里测得出来」。
> 本文不改 `RENDER_FIX_PLAN.md`、不改任何源码，只记录复核结论与实测数据。

## 0. 一句话

方案的每一条带行锚点的声明我都拉到源码里核对过，**十项全中，无一处事实错误**；
在 macOS 上测得的 38KB 复制单价同时给出一个方案没说的结论：
**V1 的收益只能在 Windows 侧验收，本机能验的只有语义等价性**。

## 1. 逐条核对（10/10 命中）

| # | 方案声明（含行锚点） | 实测 | 结果 |
|---|---|---|---|
| 1 | 4 处整块复制 `rhi_vk.cpp:2470 / 2518 / 2579 / 2622` | 逐行一致（含行尾注释） | ✅ |
| 2 | 4 处前置 `pointSize` 写 `2469 / 2517 / 2578 / 2621` | 一致 | ✅ |
| 3 | `UBO_SIZE = 38240`（`backend/ubo.h:44`） | `UBO_INSTANCES + UBO_MAX_INSTANCES*64` = 34144+4096 | ✅ |
| 4 | 硬编码偏移 `304` 绕过了 `ubo_offset()` | 一致；且 `ubo_flag(FLAG_LIGHT_COUNT)` = `128 + 11*16` = **304** | ✅ |
| 5 | `UBO_SLOTS = 1024`（`rhi_vk.cpp:67`） | 一致；`ustride × UBO_SLOTS` ⇒ 每 program ≈ **39.2 MB** | ✅ |
| 6 | `ubo_dirty`（`rhi_vk.cpp:297`）是死标志位 | 全文件 7 次出现 = 1 声明 + **6 次赋值 + 0 次读取** | ✅ |
| 7 | GL 后端是 CPU 暂存 + 上传 | `rhi_gl.cpp:97 assign` / `:110 if(dirty)` / `:113 glBufferSubData` / `:114` 清位 | ✅ |
| 8 | 布局表 `bones=384` / `world=33152` / `instances=34144` | `ubo.h:29 / 30 / 43` 一致 | ✅ |
| 9 | 6 处 uniform 写入 `2382 / 2393 / 2404 / 2414 / 2424 / 2434`（+2436） | 即 6 个 `uniform_*` 入口，与 6 处 `ubo_dirty` 同行 | ✅ |
| 10 | 复制必须在录制期完成（不能挪到执行期） | 注释原文一致，约束成立 | ✅ |

核对方式：`grep -n` 全文件计数 + `read` 逐段读原文，非抽样。

补充确认（方案没提但影响改法）：`fantasy/fantasy.h:25 using value = xhqm::real`，
`head.h:230 using real = float` ⇒ `sizeof(value) == 4`，
故 `uniform_matrix(..., bones, 512)` = `512*16*4` = 32768 B，正好等于 `UBO_MAX_BONES*64`，
`off + bytes > UBO_SIZE` 的守卫不会误伤。**方案 1.3 节的「32KB 是骨骼」成立。**

## 2. 实测：38KB 复制的单价（macOS / arm64）

方案的核心假设是「`vkMapMemory` 拿到的映射在 Windows 上大概率是 write-combined，**读**极慢」。
我在本机量了这个假设的两端：

```
$ clang++ -std=c++17 -O2 wc_mem_bench.cpp -o wc_mem_bench && ./wc_mem_bench
38,240 字节 memcpy 单价（macOS arm64 / Apple clang）:
  从普通堆内存读      : 0.0000 ms    ← 38KB 全在 L2，几乎免费
  从匿名映射读        : 0.0000 ms
  每次前先冲掉 32MB 缓存: 1.0279 ms   ← 近似「不可缓存读」的上界（含冲刷本身开销）
```

对照方案在 Windows 的实测 **0.156 ms CPU / 0.183 ms 墙钟**：

- 0.156ms **落在两个极端之间**，与「WC 映射、读一次 ~250ns/line、不可预取」的特性吻合，
  也与方案 1.1 节的量级核算（600 cache line × 250ns ≈ 150µs）同数量级。
  ⇒ **Windows 侧的归因成立，那台机器大概率确实拿到了 write-combined 映射。**
- 复现脚本（可重跑，参数在 `main` 里）：`N = 38240`、`IT = 2000`，
  三档分别测「堆读」「匿名映射读」「每轮前冲刷 32MB 破坏缓存后读」。
  第三档数值含冲刷循环自身开销，故只作**上界**看，不能当 WC 实测值读。

## 3. 补三条（方案自身的缺口）

### 3.1 【边界·最重要】收益在 macOS 上测不出来

Apple 芯片是**统一内存**，MoltenVK 下 `HOST_VISIBLE | HOST_COHERENT` 会落在普通可缓存内存，
读侧本来就是 **0.0000 ms**（见上表）。因此：

- V1 在 **Windows + 独显**上是真修（每绘制省 ~0.15ms，量级与方案预期一致）；
- **在 Mac 上接近零收益**，谁想「在 Mac 上跑一遍看快了没」都会得到**假阴性**。

⇒ 收益验收（方案第 4 节的三个命令与判据表）**必须回 Windows 跑**。
本机可验、且值得验的只有**语义等价性**：不闪、不花、物理轨迹逐位不变。

### 3.2 V1 的 `ubo_stage` 必须初始化（`assign(UBO_SIZE, 0)`）

方案 3.1 节只建议给 `ubo_data` 做一次 `memset`，**漏了 stage 自身**。
原实现里 stage 的未写区域取的是「映射内存原值」，改成 CPU 堆内存后就是**堆垃圾**。

- 不会闪屏：着色器只读「声明过且被赋值」的字段，未写区域本来也不参与渲染（方案 3.1 的论证仍成立）；
- **但会让截图逐位对比不可复现** —— 而截图对比正是本机唯一能做的验收手段（见 3.1）。
- ⇒ 建议 `program_t` 里 `ubo_stage.assign(UBO_SIZE, 0)`，
  与 `ubo_data` 的 `memset` 一起做，两端才算「确定值」。

### 3.3 `ubo_stage` 是 per-program，而原 `ubo_data` 的 slot0 是**跨 program 共享**的

方案没提这个差异。它不构成风险，因为它是**净改善**：

- `fantasy_bugs.md` 第 52 条「UBO 环形槽 slot0 别名」的根因，是「所有 program 共用一个 slot0 暂存区」；
- V1 之后每个 program 的槽只由**本 program 的写入**构成，该别名问题从
  「靠小心维护」变成**结构上不可能**；
- 修正后的唯一行为差异：slot0 不再是活数据。已核对**无其它读者依赖 slot0**
  （4 处整块复制是写侧；`ubo_dirty` 无读侧；读取路径只有 ring slot）。

⇒ 建议在方案的「3.1 为什么等价」里补一句「等价性是**逐 program 更强**的等价」，
这是 V1 除性能外的第二个正当理由。

### 3.4 顺手：把 304 的性质在编译期钉死（零成本防回归）

方案建议把 `304` 改成 `ubo_flag(FLAG_LIGHT_COUNT)`，方向对。再补一条：

```cpp
static_assert(ubo_flag(FLAG_LIGHT_COUNT) == 304, "std140 flags 步长变了，304 硬编码需同步");
```

日后谁动了 `std140` 对齐（例如把 `flags` 从 `int[16]` 改成别的），
编译期就炸，而不是等到「多光源数量画错 / 光照异常」才被发现。
`test/toon_test.cpp:26` 已有同类常量断言（`UBO_INSTANCES == UBO_TOON_RIM + 16`），风格一致。

## 4. 建议的验收路径（比跑 `fly_mcns_view` 便宜得多）

方案第 4 节依赖 `fly_mcns_view.exe` + Qt/mujoco/glew，且只有 Windows 能跑。
建议**先加一个不依赖 Qt/MuJoCo 的单测**（xhqm 已有这套基建，`test/toon_test.cpp` 就是）：

```
构造 program → 逐字段调 uniform_* → draw()
断言：① ring slot 字节 == stage 字节
      ② 且 == 逐字段期望值（含 bones / world / lights / instances 各一段）
      ③ 304 处读回的 int == 传入的 light count
```

好处：**「语义等价」变成 CI 可判定**，不必等回 Windows 用眼睛看闪不闪；
同时把 3.4 的 `static_assert` 挂上去，V1/V2/V3 任何后续改动都不会悄悄破语义。

顺序建议：

1. 上面的单测（本机可跑，先绿）；
2. V1（`ubo_stage` + 6 处写入改向 + 4 处读取改源 + `assign(UBO_SIZE,0)`）；
3. 本机复核单测 + `--no-render` 物理轨迹逐位不变；
4. **回 Windows** 跑方案第 4 节的三个命令，看 0.183ms 是否落到 ≤0.02ms。

## 5. 复核边界（我没验的）

- **没验性能收益本身**：本机平台特性决定了测不出（3.1），不冒充验过。
- **没验 `MAX_DESC_SETS = 1024` 环回卷**（方案 V4）：那是另一条正交问题，
  本次只核了它引用的行号（`rhi_vk.cpp:514-523` / `:67`）没错。
- **没验 P-B（`ploys::render` 199 次绘制）**：属于另一独立项，方案已标为「不急」。
- 本机没有独立显卡与 Windows Vulkan 驱动，`vkMapMemory` 的内存类型选择无法在本机复核，
  方案 1.1 节「大概率是 write-combined」的措辞是**恰当的谨慎**，不应改成断言。
