# fantasy/postfx 后处理

> 最后同步：2026-10-07，对照工作副本 r677。

## 模块定位

`fantasy/postfx` 是 xhqm 引擎 `fantasy` 渲染模块的**全屏后处理层**，提供一组可独立装配的屏幕空间特效（post-processing effects）。每个特效都是一个自包含的 `*_effect` 类，通过 `render_backend`（RHI 抽象接口）读写离屏渲染目标（offscreen target）与着色器，不直接接触 OpenGL / Vulkan 的具体 API。~~遵循统一的「setup / apply / reset / present」生命周期~~ **已作废（2026-10-07 复核 r677）**：七个类都有 `setup()/reset()`，但只有 `bloom_effect`/`fxaa_effect`/`tonemap_effect` 有 `present()`；`ssao_effect`/`taa_effect`/`ssr_effect` 没有，改用 `target()` 暴露内部离屏 id 供读回与合成（`ssao.h:33`、`taa.h:36`、`ssr.h:32`），何时上屏由调用方决定。规模：7 头 + 7 实现共 **1420 行**（头 281 / 实现 1139，`wc -l` 实测 r677），7 个 TU 都能单独过 `g++ -fsyntax-only -std=c++17`。

模块覆盖七个后处理 pass：

| 子模块 | 头文件 | 作用 |
| ------ | ------ | ---- |
| Bloom | `bloom.h` | 泛光：亮度提取 → 高斯模糊 → 合成 |
| Tonemap | `tonemap.h` | 色调映射：HDR → LDR（Reinhard + gamma） |
| FXAA | `fxaa.h` | 屏幕空间抗锯齿（亮度边缘检测 + 沿边缘混合） |
| SSAO | `ssao.h` | 屏幕空间环境光遮蔽（半球核 + 深度采样） |
| TAA | `taa.h` | 时间抗锯齿（历史帧混合 + 邻域钳制） |
| SSR | `ssr.h` | 屏幕空间反射（重建位置/法线 + 射线步进） |
| Compose | `compose.h` | 通用合成（AO 乘法 / SSR 加法） |

## 统一架构

七个特效共享同一套设计模式，代码高度一致：

1. **全屏四边形（七份逐字复制）**：每个 `setup()` 都创建一个 4 顶点（`pos.xy + uv.xy`，`locations = {2,2}`，顶点数组 `-1,-1,0,0 / 1,-1,1,0 / 1,1,1,1 / -1,1,0,1`）+ 6 索引（`{0,1,2,0,2,3}`）的全屏三角形对，作为后处理绘制的几何载体。实测这七份 quad、七份索引、**14 份 vert 源码字符串**（GL `#version 330 core` / VK `#version 450` 各一份 × 7 文件）都是复制粘贴的，模块内**无公共基类**（`grep ': public' fantasy/postfx` 0 命中）：七个 pass 各持一套 VB/IB，共 7 套。
2. **双着色器后端**：每个 pass 内嵌成对的 GLSL 源码——OpenGL 用 `#version 330 core`（uniform 直接声明），Vulkan 用 `#version 450`（`std140` UBO + descriptor set）。`setup()` 依据 `rhi.name()[0] == 'v'` 判定 Vulkan 后端并选择对应源码（七处同一写法：`bloom.cpp:143`、`compose.cpp:63`、`fxaa.cpp:112`、`ssao.cpp:106`、`ssr.cpp:116`、`taa.cpp:93`、`tonemap.cpp:74`）。
3. **离屏目标**：特效结果写入 `create_offscreen(w, h, 1, ...)` 的内部离屏目标，`apply()` 返回 `offscreen_texture(target, 0)` 供下游采样；`present()` 则用内置 blit 着色器把结果直通到当前帧缓冲（swapchain）。七个 pass 合计 **9 张全尺寸离屏**（实测 `create_offscreen` 调用 9 处：bloom 3 张 `m_target_bright/m_target_blur/m_target_result`，其余各 1 张）。HDR 标志并不统一：`bloom`(`bloom.cpp:158-160`)/`fxaa`(`fxaa.cpp:124`)/`compose`(`compose.cpp:75`) 传 `hdr=true`（RGBA16F），`tonemap`(`tonemap.cpp:86`)/`ssao`(`ssao.cpp:118`) 传 `false`（RGBA8，各自语义正确），而 **`ssr`(`ssr.cpp:123`)/`taa`(`taa.cpp:100`) 也传 `false`**——这两个吃的是 HDR 场景，8 位目标会把 >1 的值截断，属已知问题，见 `DEBUG.md` A 组。
4. **资源管理**：`reset()` 幂等地释放顶点/索引/离屏目标/着色器，`setup()` 失败或析构时统一回收；`m_ready` 标志门控所有 `apply/present`（10 处 guard：7 个 `apply` 返回 0、3 个 `present` 空返回；注意它**没有日志也没有错误通道**，见 `DEBUG.md` B 组）。

与上层编排的关系（补记）：本模块只提供「可独立装配的单个 pass」，**自己不编排**。仓库里的编排器在上层 `fantasy/theory_render.h`/`.cpp`：`PIPE_BLOOM/PIPE_FXAA/PIPE_SSAO/PIPE_SSR/PIPE_TAA/PIPE_INSTANCING` 位掩码（`theory_render.h:248-253`）+ 组装入口 `setup_pipeline_for(cam, flags)` / `setup_default_pipeline(world, flags)`（`theory_render.h:260-263`），真实顺序 `shadow → [instancing] → scene → [depth] → [ssao] → [ssr] → [bloom] → [fxaa] → [taa] → tonemap`（`theory_render.cpp:812-820`）。因此根 `README.md:237` 记的「无统一编排器」**已作废（2026-10-07 复核 r677）**；根 README 属其它模块文档，本轮按约束未改。

## 关键类 / API

所有类均位于命名空间 `xhqm::fantasy`，持有 `render_backend* m_rhi` 与全屏四边形资源。

### bloom_effect

```
bool setup(render_backend& rhi, uint w, uint h, std::string& error);
void reset();
uint apply(uint scene_texture);   // 返回合成结果纹理（离屏）
void present(uint texture);       // 直通到 swapchain
void threshold(value v);          // 亮度提取阈值
void intensity(value v);          // 泛光强度
bool ready() const;
uint width() const; uint height() const;
```

三段管线：`scene → bright`（`smoothstep(threshold, threshold+0.5, 亮度)` 提取高亮，亮度取 RGB 最大值）→ `bright → blur`（3×3 高斯，权重 中心 4 / 四邻 2 / 四角 1，归一 `/16`）→ `scene + intensity * blur`（合成，输出 alpha 恒 1）。内部持有 `m_target_bright / m_target_blur / m_target_result` 三个离屏目标（均 RGBA16F）。默认值：阈值 `1.0`、强度 `1.0`（`bloom.h:26-27`），由 `threshold()/intensity()` 改。注意这次模糊是**一次全分辨率**的、半径固定 1 texel（`bloom.cpp:51-89`、`:218` 传 `1/w, 1/h`），没有降采样/多级，详见 `DEBUG.md` D 组。

### tonemap_effect

```
bool setup(render_backend& rhi, uint w, uint h, std::string& error);
uint apply(uint hdr_texture, value exposure = 1.0f);  // 返回 LDR 纹理
void present(uint texture);
bool ready() const;
```

`apply` 做 Reinhard 色调映射 `hdr/(hdr+1)` 后执行 `pow(..., 1/2.2)` gamma 校正，**保留 alpha**（透明支持）；输出目标是 RGBA8（`hdr=false`，`tonemap.cpp:86`）。关键实现细节：全屏 pass 期间显式关闭深度测试与混合，避免离屏 FBO 深度把全屏 quad 拒掉导致黑屏，结束后恢复（`tonemap.cpp:118-120`、`:136-144`）——这是**模块内唯一做了状态隔离的类**，其余六个 pass 不做，见 `DEBUG.md` A 组。`present()` 同样关深度/混合，然后**无条件**恢复成 `depth_test(true,false)` + `blend(true)`。

### fxaa_effect

```
bool setup(render_backend& rhi, uint w, uint h, std::string& error);
uint apply(uint scene_texture);
void present(uint texture);
bool ready() const;
```

简版 FXAA：**5 抽头**（中心 + 上/下/左/右，`fxaa.cpp:33-36`），以亮度（`0.299/0.587/0.114` 加权）计算中心与四邻域的 `lumaMin/lumaMax`，低于 `0.0312`（硬编码字面量，`fxaa.cpp:40`）直接透传；否则比较水平/垂直边缘强度，沿较强方向做 `t ∈ [-0.5,0.5]` 的线性混合平滑，输出 alpha 恒 1。注意 `0.0312` 是 LDR 经验阈值，而自带管线把它排在 tonemap **之前**（输入是 HDR RGBA16F），阈值语义会随亮度漂移，见 `DEBUG.md` A 组。

### ssao_effect

```
bool setup(render_backend& rhi, uint w, uint h, std::string& error);
uint apply(uint depth_tex, value aspect, value tan_half_fov, value radius, value bias);
uint target() const;   // 供验证读回
```

输入 `depth_tex` 约定为浮点纹理（RGBA32F），R 通道存**线性视图深度**（正数，0=近）。片元由深度重建视图空间位置，用 `dFdx/dFdy` 重建法线，在 16 采样半球核（`KERNEL_N=16` 与 16 项常量表硬编码在 shader 字符串里，`ssao.cpp:23-43`，**核值不可调**）内采样深度做遮挡累积，带 `smoothstep` 范围检查与 bias，输出 AO 灰度（0=遮蔽，1=无遮蔽），alpha 恒 1。AO 输出为 RGBA8（`ssao.cpp:118`）。`aspect`/`tan_half_fov` 由调用方给（自带管线写死 45° 的一半，见 `DEBUG.md` A 组）；`target()` 是内部离屏 id，配 `render_backend::read_offscreen` 读回验证（真实用例 `test/view/main.cpp:727`）。`depth_tex` 的格式/线性性**不做运行时校验**。

### taa_effect

```
std::array<value,2> taa_jitter(uint frame_index);   // Halton(2,3) 亚像素偏移 ∈ [-0.5,0.5]
bool setup(render_backend& rhi, uint w, uint h, std::string& error);
uint apply(uint current_tex, uint history_tex, value alpha);
uint target() const;
```

`taa_jitter` 用 Halton 2/3 序列生成亚像素 jitter（`taa.cpp:7-26`：`halton(frame_index+1, base) - 0.5`，实际落在 `[-0.5, 0.5)`），供相机投影矩阵微调实现多帧采样——但**库内没有任何调用者**（`theory_taa` 不用它），全仓只有 `test/view/main.cpp:652` 调，见 `DEBUG.md` E 组。`apply` 对当前帧做 3×3 历史邻域 min/max 钳制以抑制移动物体鬼影，再按 `mix(hist, cur, alpha)` 做指数滑动平均（历史权重 `1-alpha`），输出 alpha 恒 1。历史帧**不持久化在本模块**：`apply` 每次都要调用方传入 `history_tex`，仓库自带管线由 `theory_taa::m_history` 持有（`theory_render.cpp:554-558`，resize 时清 0）。输出目标是 RGBA8（`taa.cpp:100`），对 HDR 场景会截断，见 `DEBUG.md` A 组。

### ssr_effect

```
bool setup(render_backend& rhi, uint w, uint h, std::string& error);
uint apply(uint color_tex, uint depth_tex, value aspect, value tan_half_fov, value step_size, value bias);
uint target() const;
```

输入 `color_tex`（RGBA32F）+ `depth_tex`（RGBA32F，R=视图深度）——自带管线实际传的是 RGBA16F 的 HDR 场景（见 `DEBUG.md` G 组）。片元重建位置与法线后 `reflect(viewDir, normal)` 得到反射方向，在屏幕空间沿射线步进（**固定 16 步**，`ssr.cpp:47`，无命中细化、无早退除边界）采样颜色/深度求交；出屏取边缘天空色（`clamp` 后采样），射线落到表面之后视为命中，未命中则输出全黑，alpha 恒 1。输出目标是 RGBA8（`ssr.cpp:123`），反射高光被截断，见 `DEBUG.md` A 组。

### compose_effect

```
bool setup(render_backend& rhi, uint w, uint h, std::string& error);
uint apply(uint base_tex, uint overlay_tex, int mode);   // mode 0=multiply, 1=add
bool ready() const;
```

通用合成 pass，输出为 HDR 离屏目标（RGBA16F，与场景一致，`compose.cpp:75`）。`mode 0` = 乘法（AO：`scene × ao`），`mode 1` = 加法（SSR：`scene + reflection`），保留 base 的 alpha。判定写的是 `int(params.x + 0.5) == 0 ? 乘 : 加`（`compose.cpp:32`）——**非 0 一律走加法**，不报错，见 `DEBUG.md` F 组。自带管线里 SSAO 与 SSR 各持一个实例（`theory_render.cpp:487-495`、`:520-528`）。

## 依赖

- `fantasy.h`：`namespace xhqm::fantasy` 下的基础类型别名（`uint / sint / value / rgba / values`），`value.h:25` 定义 `value = xhqm::real`。
- `backend/rhi_no.h`：`render_backend` 抽象接口（离屏目标、着色器、顶点/索引、纹理绑定、uniform 上传、绘制），`render_backend_active()` 在 `rhi_no.h:178`。
- `backend/backend.h`：`backend_kind` 枚举与 `active_backend()` / `backend_name()`（`backend.h:29`、`:63-78`）；本模块没用它们，用的是 `rhi.name()[0] == 'v'` 字符串嗅探（见 `DEBUG.md` C 组）。
- ~~`backend/backend.h` 提供 `xhqm_io` 导出宏（SSAO/TAA/SSR 头文件使用）~~ **已作废（2026-10-07 复核 r677）**：`xhqm_io` 定义在 `head.h:27-31`（`XHQM_DYNAMIC` 下展开为 `XHQMCLASS` = dllexport/import），经 `algebra/value.h:3` → `fantasy.h` 传递可见；在 `backend/backend.h` 里 grep `xhqm_io` 是 **0 命中**。另外注意：`ssao.h:16`/`taa.h:19`/`ssr.h:15` 加了 `xhqm_io`，而 `bloom.h:13`/`fxaa.h:13`/`tonemap.h:13`/`compose.h:14` **没加**，见 `DEBUG.md` C 组。

## 用法示例

下面是全链最小可运行写法，**逐字取自代码/测试里的真实用法**并已用 `g++ -fsyntax-only -std=c++17` 对着本仓头文件验过（`setup()` 与 `apply()` 的返回值在实际使用中都要判，这里为可读性省略了错误分支）。

```cpp
// include 路径：两种写法都可用 —— CMakeLists.txt:84 给了 -I fantasy/，:87 给了 -I fantasy/postfx/
//   #include "postfx/bloom.h"   ← 本示例
//   #include "bloom.h"         ← theory_render.h:12-18 用的裸名写法
#include "postfx/ssao.h"
#include "postfx/ssr.h"
#include "postfx/compose.h"
#include "postfx/bloom.h"
#include "postfx/fxaa.h"
#include "postfx/taa.h"
#include "postfx/tonemap.h"
#include <cmath>    // std::tan

// 顺序与参数取自自带管线 theory_render.cpp:812-820 与 :490-527
void frame(xhqm::fantasy::uint scene_hdr,    // 几何 pass 产出的 HDR 场景纹理（RGBA16F，theory_render.cpp:227）
           xhqm::fantasy::uint depth_tex,    // 线性视图深度，R 通道正数 0=近（theory_depth 产出，:304）
           xhqm::fantasy::uint history_tex,  // 上一帧 TAA 输出；首帧传 0
           xhqm::fantasy::uint frame_index)
{
    xhqm::fantasy::render_backend& rhi = xhqm::fantasy::render_backend_active();   // rhi_no.h:178
    std::string err;
    const xhqm::fantasy::uint w = 1920, h = 1080;
    const xhqm::fantasy::value aspect = xhqm::fantasy::value(w) / xhqm::fantasy::value(h);
    const xhqm::fantasy::value tan_half_fov = std::tan(xhqm::radians(xhqm::fantasy::value(22.5)));  // 45° 默认 fov 的一半（camera.h:23）

    // 1) SSAO → compose(0=乘法)：scene × ao
    xhqm::fantasy::ssao_effect ssao;
    xhqm::fantasy::compose_effect compose_ao;
    ssao.setup(rhi, w, h, err);
    compose_ao.setup(rhi, w, h, err);
    xhqm::fantasy::uint ao = ssao.apply(depth_tex, aspect, tan_half_fov, /*radius=*/0.5f, /*bias=*/0.025f);
    xhqm::fantasy::uint scene = compose_ao.apply(scene_hdr, ao, /*mode=*/0);        // 0=multiply

    // 2) SSR → compose(1=加法)：scene + reflection
    xhqm::fantasy::ssr_effect ssr;
    xhqm::fantasy::compose_effect compose_ssr;
    ssr.setup(rhi, w, h, err);
    compose_ssr.setup(rhi, w, h, err);
    xhqm::fantasy::uint refl = ssr.apply(scene, depth_tex, aspect, tan_half_fov, /*step_size=*/0.02f, /*bias=*/0.001f);
    if (refl) scene = compose_ssr.apply(scene, refl, /*mode=*/1);                   // 1=add

    // 3) Bloom → FXAA → TAA（bloom 默认 threshold/intensity 都是 1.0，bloom.h:26-27）
    xhqm::fantasy::bloom_effect bloom;
    bloom.setup(rhi, w, h, err);
    bloom.threshold(1.0f);
    bloom.intensity(1.0f);
    xhqm::fantasy::uint bloomed = bloom.apply(scene);

    xhqm::fantasy::fxaa_effect fxaa;
    fxaa.setup(rhi, w, h, err);
    xhqm::fantasy::uint aa = fxaa.apply(bloomed);

    xhqm::fantasy::taa_effect taa;
    taa.setup(rhi, w, h, err);
    std::array<xhqm::fantasy::value, 2> jitter = xhqm::fantasy::taa_jitter(frame_index);   // [-0.5,0.5)
    (void)jitter;                       // jitter 要自己喂给相机投影矩阵：库内没人调它，见 DEBUG.md E 组
    xhqm::fantasy::uint history = history_tex ? history_tex : aa;   // 首帧退化为当前帧（同 theory_render.cpp:556）
    xhqm::fantasy::uint taad = taa.apply(aa, history, /*alpha=*/0.1f);   // 历史权重 = 1-alpha
    history_tex = taad;                 // 历史帧由调用方持有

    // 4) tonemap 到 LDR，再 present 到 swapchain
    xhqm::fantasy::tonemap_effect tonemap;
    tonemap.setup(rhi, w, h, err);
    xhqm::fantasy::uint ldr = tonemap.apply(taad, /*exposure=*/1.0f);
    tonemap.present(ldr);

    // 验证读回：target() 是内部离屏 id，配 read_offscreen（真实用例 test/view/main.cpp:707-743）
    xhqm::fantasy::uint pixel = 0;
    rhi.read_offscreen(ssao.target(), w / 2, h / 2, &pixel);   // AO 灰度在低 8 位
}
```

要点：`setup(rhi, w, h, error)` 的 `error` 只在后端创建失败时被写入；`apply`/`present` 在未 ready 时**静默返回 0 / 空返回**（无日志），所以调用方要么先问 `ready()`（只有 bloom/fxaa/tonemap/compose 有），要么判 `apply` 的返回值。SSAO/TAA/SSR 没有 `ready()`，用 `target()` 或返回值为 0 判失败。

自带管线（`setup_pipeline_for` / `setup_default_pipeline`）直接复用这七件套，装配顺序 `shadow → [instancing] → scene → [depth] → [ssao] → [ssr] → [bloom] → [fxaa] → [taa] → tonemap`（`theory_render.cpp:812-820`）。~~典型渲染帧装配顺序（示意）：几何渲染产出 HDR 场景纹理与线性深度 → `ssao.apply` → `ssr.apply` → `compose`（AO 乘法 / SSR 加法）→ `taa.apply`（含 `taa_jitter`）→ `fxaa.apply`（可选）→ `bloom.apply` → `tonemap.apply + present`~~ **已作废（2026-10-07 复核 r677）**：原文把 bloom 放在 fxaa/taa 之后、taa 放在 fxaa 之前，且假定 `taa_jitter` 被使用——实际顺序是 **bloom → fxaa → taa**，`taa_jitter` 在管线里无人调用。各 pass 之间通过 `offscreen_texture()` 返回的纹理句柄衔接，上层用帧图句柄 `"scene"` 串起来（`theory_render.cpp:436`、`:463`、`:495`、`:528`、`:560`）。
