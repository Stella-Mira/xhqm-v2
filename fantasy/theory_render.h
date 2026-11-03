#pragma once
#ifndef XHQM_FANTASY_THEORY_RENDER
#define XHQM_FANTASY_THEORY_RENDER

// 具体渲染 pass：方向光阴影 / 主渲染 / 后处理（tonemap / bloom / fxaa / ssao / ssr / taa）。
// 每个 pass 派生自 theory_pass，通过句柄表（theory_frame_graph::set_handle/handle）链式传递纹理。
//
// 链式约定：主渲染写 "scene"；bloom/fxaa/ssr/taa 读 "scene" 后写回 "scene"；tonemap 读 "scene" 呈现。
// ssao 读 "depth"（线性视图深度，需独立深度 pass 提供，未接线时跳过）写 "ao"。
#include "theory_pass.h"
#include "shadow.h"
#include "tonemap.h"
#include "bloom.h"
#include "fxaa.h"
#include "ssao.h"
#include "taa.h"
#include "ssr.h"
#include "compose.h"
#include "property.h"

namespace xhqm
{
	namespace fantasy
	{
		// 方向光阴影 pass（前置）：生成紧贴场景的阴影深度图并写进各 drawer 阴影态
		class xhqm_io theory_shadow : public theory_pass
		{
			object* m_camera = nullptr;
			shadow_map m_shadow;
			uint m_size = 2048;
			uint m_applied = 0;   // 已生效的阴影图尺寸（size() 改动后据此重建）
			bool m_enabled = true;
			light* m_light_env = nullptr;
		public:
			explicit theory_shadow(object* cam) : theory_pass("shadow"), m_camera(cam) {}
			void camera_ptr(object* cam) { m_camera = cam; }
			void light_env(light* v) { m_light_env = v; }
			void enabled(bool v) { m_enabled = v; }
			void size(uint v) { m_size = v; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// 主渲染 pass：把场景绘制到 HDR 离屏，输出到句柄表 "scene"
		class xhqm_io theory_scene : public theory_pass
		{
			object* m_camera = nullptr;
			uint m_hdr = 0;
			uint m_w = 0, m_h = 0;
			light* m_light_env = nullptr;
		public:
			explicit theory_scene(object* cam) : theory_pass("scene"), m_camera(cam) {}
			virtual ~theory_scene();
			void camera_ptr(object* cam) { m_camera = cam; }
			void light_env(light* v) { m_light_env = v; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// ECS 批量提交 pass：树遍历收集 model_ecs 节点，读 SoA 批量提交实例数据（sync_instances）。
		// 须在 theory_scene 之前执行（scene 遍历时 model_ecs::render 直接消费已同步的实例列表，
		// 数据版本守卫保证同帧不重复同步）；管线未挂本 pass 时 model_ecs::render 自同步兜底。
		class xhqm_io theory_instancing : public theory_pass
		{
			object* m_root = nullptr;   // 遍历根（model_ecs 节点所在子树，通常取相机 root_ptr）
		public:
			explicit theory_instancing(object* root) : theory_pass("instancing"), m_root(root) {}
			void root_ptr(object* root) { m_root = root; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// 线性视图深度 pass：渲染场景网格到浮点离屏，R 通道存线性视图深度（-viewPos.z，正数 0=近），写 "depth"
		class xhqm_io theory_depth : public theory_pass
		{
			object* m_camera = nullptr;
			uint m_target = 0;
			uint m_w = 0, m_h = 0;
			shader m_shader;
		public:
			explicit theory_depth(object* cam) : theory_pass("depth"), m_camera(cam) {}
			virtual ~theory_depth();
			void camera_ptr(object* cam) { m_camera = cam; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// tonemap pass（后置）：读 "scene" HDR → tonemap → present 到 swapchain
		class xhqm_io theory_tonemap : public theory_pass
		{
			object* m_camera = nullptr;
			tonemap_effect m_tonemap;
			uint m_w = 0, m_h = 0;
			value m_exposure = 1.0f;
			bool m_present = true;         // true: present 到当前窗口 swapchain；false: 仅留 LDR 句柄（离屏相机）
			uint m_ldr = 0;                // 最近一次 LDR 输出纹理（present=false 时供调用方取用）
			sint m_vp_x = -1, m_vp_y = -1; // 分屏 viewport 子区域（x<0 = 整屏）
			uint m_vp_w = 0, m_vp_h = 0;
		public:
			explicit theory_tonemap(object* cam) : theory_pass("tonemap"), m_camera(cam) {}
			void camera_ptr(object* cam) { m_camera = cam; }
			void exposure(value v) { m_exposure = v; }
			// 多相机：present 开关（离线相机关掉，避免多个 tonemap 互抢 swapchain）
			void present(bool v) { m_present = v; }
			bool present(void) const { return m_present; }
			uint ldr_texture(void) const { return m_ldr; }
			// 分屏：present 到窗口子区域（x<0 恢复整屏）
			void viewport(sint x, sint y, uint w, uint h) { m_vp_x = x; m_vp_y = y; m_vp_w = w; m_vp_h = h; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// bloom pass：读 "scene" → bloom → 写回 "scene"
		class xhqm_io theory_bloom : public theory_pass
		{
			object* m_camera = nullptr;
			bloom_effect m_bloom;
			uint m_w = 0, m_h = 0;
			value m_threshold = 1.0f;
			value m_intensity = 1.0f;
		public:
			explicit theory_bloom(object* cam) : theory_pass("bloom"), m_camera(cam) {}
			void camera_ptr(object* cam) { m_camera = cam; }
			void threshold(value v) { m_threshold = v; }
			void intensity(value v) { m_intensity = v; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// fxaa pass：读 "scene" → fxaa → 写回 "scene"
		class xhqm_io theory_fxaa : public theory_pass
		{
			object* m_camera = nullptr;
			fxaa_effect m_fxaa;
			uint m_w = 0, m_h = 0;
		public:
			explicit theory_fxaa(object* cam) : theory_pass("fxaa"), m_camera(cam) {}
			void camera_ptr(object* cam) { m_camera = cam; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// ssao pass：读 "depth"（线性视图深度）→ ssao → 写 "ao"
		// 注：depth 源需独立深度 pass 生成线性视图深度并 set_handle("depth")；未提供时本 pass 跳过。
		class xhqm_io theory_ssao : public theory_pass
		{
			object* m_camera = nullptr;
			ssao_effect m_ssao;
			compose_effect m_compose;   // ao × scene
			value m_radius = 0.5f;
			value m_bias = 0.025f;
			uint m_w = 0, m_h = 0;
			bool m_ready = false;
		public:
			explicit theory_ssao(object* cam) : theory_pass("ssao"), m_camera(cam) {}
			void camera_ptr(object* cam) { m_camera = cam; }
			void radius(value v) { m_radius = v; }
			void bias(value v) { m_bias = v; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// ssr pass：读 "scene" + "depth" → ssr → 写回 "scene"
		class xhqm_io theory_ssr : public theory_pass
		{
			object* m_camera = nullptr;
			ssr_effect m_ssr;
			compose_effect m_compose;   // reflection + scene
			value m_step_size = 0.02f;
			value m_bias = 0.001f;
			uint m_w = 0, m_h = 0;
			bool m_ready = false;
		public:
			explicit theory_ssr(object* cam) : theory_pass("ssr"), m_camera(cam) {}
			void camera_ptr(object* cam) { m_camera = cam; }
			void step_size(value v) { m_step_size = v; }
			void bias(value v) { m_bias = v; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// taa pass：读 "scene" 与历史帧 → taa → 写回 "scene"
		class xhqm_io theory_taa : public theory_pass
		{
			object* m_camera = nullptr;
			taa_effect m_taa;
			uint m_history = 0;
			uint m_frame = 0;
			value m_alpha = 0.1f;
			uint m_w = 0, m_h = 0;
			bool m_ready = false;
		public:
			explicit theory_taa(object* cam) : theory_pass("taa"), m_camera(cam) {}
			void camera_ptr(object* cam) { m_camera = cam; }
			void alpha(value v) { m_alpha = v; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// 环境光照（IBL）pass：程序化生成环境贴图（渐变天空）→ CPU 侧辐照度卷积（漫反射 IBL），
		// 输出 irradiance cubemap；再经 GPU 重要性采样生成 prefiltered cubemap（mip 链，specular IBL）
		// 与 BRDF 积分 LUT，供 prefab_frag 做 split-sum 镜面反射。
		class xhqm_io theory_ibl : public theory_pass
		{
			light* m_light_env = nullptr;
			uint m_environment_map = 0;   // 环境贴图 cubemap 句柄（0=程序化生成；非 0=外部提供）
			uint m_irradiance = 0;        // 辐照度 cubemap（diffuse IBL）
			uint m_prefiltered = 0;       // 预过滤 cubemap（mip 链，specular IBL）
			uint m_brdf_lut = 0;          // BRDF 积分 LUT（2D，NdotV × roughness）
			uint m_prefilter_shader = 0;  // prefilter 卷积 shader
			uint m_brdf_shader = 0;       // BRDF LUT shader
			uint m_quad_vb = 0, m_quad_ib = 0;   // 全屏 quad
			uint m_size = 32;             // irradiance 分辨率
			uint m_mip_levels = 5;        // prefilter mip 级数（每级一个粗糙度）
			bool m_done = false;          // 一次性预计算标记
			// equirectangular 全景图数据（可选，替代程序化天空；行主序 [y][x]）
			std::vector<std::vector<rgba>> m_equirect;
			uint m_equirect_w = 0, m_equirect_h = 0;
		public:
			explicit theory_ibl(light* env) : theory_pass("ibl"), m_light_env(env) {}
			void light_env(light* v) { m_light_env = v; }
			void environment_map(uint cubemap) { m_environment_map = cubemap; }
			void set_equirect(const std::vector<std::vector<rgba>>& equirect, uint w, uint h) { m_equirect = equirect; m_equirect_w = w; m_equirect_h = h; }
			void size(uint v) { m_size = v; }
			uint irradiance(void) const { return m_irradiance; }
			uint prefiltered(void) const { return m_prefiltered; }
			uint brdf_lut(void) const { return m_brdf_lut; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// 同步坐标系 gizmo 姿态：每帧把 gizmo 的旋转设为相机的逆旋转，
		// 让 gizmo 三轴始终指向世界坐标系（gizmo 挂相机下 add_ui，屏幕位置固定）。
		class xhqm_io theory_gizmo_sync : public theory_pass
		{
			object* m_camera = nullptr;   // camera 或 camera
			coord* m_gizmo = nullptr;     // 坐标系 gizmo（三轴箭头，property.h 的 coord）
			point m_screen_pos = point(2.5f, -2.0f, -5.0f);   // 屏幕右下角（相机空间）
		public:
			explicit theory_gizmo_sync(object* cam, coord* gizmo) : theory_pass("gizmo_sync"), m_camera(cam), m_gizmo(gizmo) {}
			void gizmo(coord* g) { m_gizmo = g; }
			void screen_pos(const point& p) { m_screen_pos = p; }
			virtual void declare(frame_graph& graph) override;
			virtual void render() override;
		};

		// 后处理开关位掩码
		inline constexpr uint PIPE_BLOOM = 1u << 0;
		inline constexpr uint PIPE_FXAA = 1u << 1;
		inline constexpr uint PIPE_SSAO = 1u << 2;
		inline constexpr uint PIPE_SSR = 1u << 3;
		inline constexpr uint PIPE_TAA = 1u << 4;
		inline constexpr uint PIPE_INSTANCING = 1u << 5;   // ECS 批量提交（场景含 model_ecs 节点时开启）

		// 便捷组装：为指定相机建一条独立管线（theory_frame_graph + shadow/[instancing]/scene/[depth]/[ssao]/[ssr]/[bloom]/[fxaa]/[taa]/tonemap）。
		// pass 全部预注册进 fg：多相机/多窗口场景直接 fg->run() 手动驱动（不挂 world 树）。
		// flags 按 PIPE_* 位组合；含 SSAO/SSR 时自动插入 theory_depth 提供线性视图深度；
		// 含 INSTANCING 时在 scene 前插入 theory_instancing（ECS 批量提交，场景含 model_ecs 节点时开启）。
		// 返回的 fg 及其 passes 由调用方负责释放。
		xhqm_io theory_frame_graph* setup_pipeline_for(object* cam, uint flags = 0);

		// 便捷组装（默认相机，挂 world theory 树，alive() 驱动）：阴影 → 场景 → [depth] → [ssao] → [ssr] → [bloom] → [fxaa] → [taa] → tonemap → fg（殿后）。
		xhqm_io theory_frame_graph* setup_default_pipeline(world* w, uint flags = 0);
	}
}

#endif
