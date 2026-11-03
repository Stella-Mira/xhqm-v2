#pragma once
#ifndef XHQM_FANTASY_BLOOM
#define XHQM_FANTASY_BLOOM

// Bloom 后处理：亮度提取 → 高斯模糊 → 合成；输入场景纹理，输出合成结果纹理
#include "fantasy.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		class bloom_effect
		{
			render_backend* m_rhi = nullptr;
			uint m_w = 0, m_h = 0;
			uint m_target_bright = 0;   // 亮度提取结果
			uint m_target_blur = 0;     // 模糊结果
			uint m_target_result = 0;   // 合成结果
			uint m_quad_vb = 0;         // 全屏四边形顶点
			uint m_quad_ib = 0;         // 全屏四边形索引
			uint m_shader_bright = 0;
			uint m_shader_blur = 0;
			uint m_shader_composite = 0;
			uint m_shader_blit = 0;
			value m_threshold = 1.0;
			value m_intensity = 1.0;
			bool m_ready = false;

			void draw_quad();
		public:
			bloom_effect() = default;
			~bloom_effect();

			bool setup(render_backend& rhi, uint w, uint h, std::string& error);
			void reset();

			// 对场景纹理做 bloom，返回合成结果纹理（离屏）
			uint apply(uint scene_texture);
			// 全屏直通到当前 framebuffer（swapchain）
			void present(uint texture);

			void threshold(value v) { m_threshold = v; }
			void intensity(value v) { m_intensity = v; }
			bool ready() const { return m_ready; }
			uint width() const { return m_w; }
			uint height() const { return m_h; }
		};
	}
}

#endif
