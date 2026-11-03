#include "tonemap.h"

namespace xhqm
{
	namespace fantasy
	{
		static const char* vert_gl = R"(
#version 330 core
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;
out vec2 fragUV;
void main() { fragUV = uv; gl_Position = vec4(pos, 0.0, 1.0); }
)";
		static const char* vert_vk = R"(
#version 450
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec2 fragUV;
void main() { fragUV = uv; gl_Position = vec4(pos, 0.0, 1.0); }
)";

		// Reinhard tonemap + gamma（M5 独立 pass）
		static const char* tonemap_gl = R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D scene;
uniform vec4 params;   // x = exposure
void main() {
    vec4 tex = texture(scene, fragUV);
    vec3 hdr = tex.rgb * params.x;
    vec3 mapped = hdr / (hdr + vec3(1.0));
    outColor = vec4(pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2)), tex.a);   // 保留 alpha（透明支持）
}
)";
		static const char* tonemap_vk = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(std140, set = 0, binding = 0) uniform UBO { vec4 params; } ubo;
layout(set = 0, binding = 1) uniform sampler2D scene;
void main() {
    vec4 tex = texture(scene, fragUV);
    vec3 hdr = tex.rgb * ubo.params.x;
    vec3 mapped = hdr / (hdr + vec3(1.0));
    outColor = vec4(pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2)), tex.a);   // 保留 alpha（透明支持）
}
)";

		// 纯 blit（LDR → 当前目标，不做 tonemap/gamma）
		static const char* blit_gl = R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D scene;
void main() { outColor = texture(scene, fragUV); }   // 保留 alpha（透明支持）
)";
		static const char* blit_vk = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform sampler2D scene;
void main() { outColor = texture(scene, fragUV); }   // 保留 alpha（透明支持）
)";

		tonemap_effect::~tonemap_effect()
		{
			reset();
		}

		bool tonemap_effect::setup(render_backend& rhi, uint w, uint h, std::string& error)
		{
			reset();
			m_rhi = &rhi;
			m_w = w; m_h = h;
			bool vk = (rhi.name()[0] == 'v');

			std::vector<value> quad = {
				-1, -1, 0, 0,
				 1, -1, 1, 0,
				 1,  1, 1, 1,
				-1,  1, 0, 1,
			};
			std::vector<uint> locations = { 2, 2 };
			m_quad_vb = rhi.vertex_create(quad, locations, error);
			std::vector<uint> indices = { 0, 1, 2, 0, 2, 3 };
			m_quad_ib = rhi.index_create(indices, error);
			m_target = rhi.create_offscreen(w, h, 1, error, false);  // LDR 输出用 RGBA8
			m_shader = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? tonemap_vk : tonemap_gl, error);
			m_shader_blit = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? blit_vk : blit_gl, error);
			if (!m_quad_vb || !m_quad_ib || !m_target || !m_shader || !m_shader_blit)
			{
				reset();
				return false;
			}
			m_ready = true;
			return true;
		}

		void tonemap_effect::reset()
		{
			if (m_rhi)
			{
				if (m_quad_vb) m_rhi->vertex_delete(m_quad_vb);
				if (m_quad_ib) m_rhi->index_delete(m_quad_ib);
				if (m_target) m_rhi->delete_offscreen(m_target);
				if (m_shader) m_rhi->shader_delete(m_shader);
				if (m_shader_blit) m_rhi->shader_delete(m_shader_blit);
			}
			m_quad_vb = m_quad_ib = 0;
			m_target = 0;
			m_shader = m_shader_blit = 0;
			m_ready = false;
			m_rhi = nullptr;
		}

		uint tonemap_effect::apply(uint hdr_texture, value exposure)
		{
			if (!m_ready) return 0;
			// 全屏 pass：关深度与混合（离屏 FBO 深度未清，开着深度会把全屏 quad 拒掉 → 黑屏）
			m_rhi->depth_test(false, false);
			m_rhi->blend(false);
			m_rhi->begin_offscreen(m_target);
			m_rhi->use_program(m_shader);
			m_rhi->bind_texture(0, hdr_texture);
			m_rhi->uniform_int(m_shader, "scene", 0);
			m_rhi->uniform_vec4(m_shader, "params", exposure, 0, 0, 0);
			m_rhi->draw_indexed(m_quad_vb, m_quad_ib, rhi_primitive::triangles, 0, 6);
			m_rhi->end_offscreen();
			m_rhi->depth_test(true, false);
			m_rhi->blend(true);
			return m_rhi->offscreen_texture(m_target, 0);
		}

		void tonemap_effect::present(uint texture)
		{
			if (!m_ready) return;
			// 全屏 blit：同样关深度与混合，避免被目标帧缓冲深度拒掉
			m_rhi->depth_test(false, false);
			m_rhi->blend(false);
			m_rhi->use_program(m_shader_blit);
			m_rhi->bind_texture(0, texture);
			m_rhi->uniform_int(m_shader_blit, "scene", 0);
			m_rhi->draw_indexed(m_quad_vb, m_quad_ib, rhi_primitive::triangles, 0, 6);
			m_rhi->depth_test(true, false);
			m_rhi->blend(true);
		}
	}
}
