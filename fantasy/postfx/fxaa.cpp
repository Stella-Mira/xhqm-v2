#include "fxaa.h"

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

		static const char* fxaa_gl = R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D scene;
uniform vec4 params;   // xy=texelSize
const vec3 rgb2luma = vec3(0.299, 0.587, 0.114);
float luma(vec3 c) { return dot(c, rgb2luma); }
void main() {
    vec2 inv = params.xy;
    vec3 c = texture(scene, fragUV).rgb;
    float m = luma(c);
    float n = luma(texture(scene, fragUV + vec2(0.0, inv.y)).rgb);
    float s = luma(texture(scene, fragUV + vec2(0.0, -inv.y)).rgb);
    float e = luma(texture(scene, fragUV + vec2(inv.x, 0.0)).rgb);
    float w = luma(texture(scene, fragUV + vec2(-inv.x, 0.0)).rgb);

    float lumaMin = min(m, min(n, min(s, min(e, w))));
    float lumaMax = max(m, max(n, max(s, max(e, w))));
    if (lumaMax - lumaMin < 0.0312) { outColor = vec4(c, 1.0); return; }

    float h = abs(n - m) + abs(s - m);   // 水平边缘强度（上下差）
    float v = abs(e - m) + abs(w - m);   // 垂直边缘强度（左右差）
    if (h > v) {
        float t = clamp((n + s - 2.0 * m) / max(h, 0.0001), -0.5, 0.5);
        c = (texture(scene, fragUV + vec2(0.0, inv.y * t)).rgb + c) * 0.5;
    } else {
        float t = clamp((e + w - 2.0 * m) / max(v, 0.0001), -0.5, 0.5);
        c = (texture(scene, fragUV + vec2(inv.x * t, 0.0)).rgb + c) * 0.5;
    }
    outColor = vec4(c, 1.0);
}
)";
		static const char* fxaa_vk = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(std140, set = 0, binding = 0) uniform UBO { vec4 params; } ubo;
layout(set = 0, binding = 1) uniform sampler2D scene;
const vec3 rgb2luma = vec3(0.299, 0.587, 0.114);
float luma(vec3 c) { return dot(c, rgb2luma); }
void main() {
    vec2 inv = ubo.params.xy;
    vec3 c = texture(scene, fragUV).rgb;
    float m = luma(c);
    float n = luma(texture(scene, fragUV + vec2(0.0, inv.y)).rgb);
    float s = luma(texture(scene, fragUV + vec2(0.0, -inv.y)).rgb);
    float e = luma(texture(scene, fragUV + vec2(inv.x, 0.0)).rgb);
    float w = luma(texture(scene, fragUV + vec2(-inv.x, 0.0)).rgb);

    float lumaMin = min(m, min(n, min(s, min(e, w))));
    float lumaMax = max(m, max(n, max(s, max(e, w))));
    if (lumaMax - lumaMin < 0.0312) { outColor = vec4(c, 1.0); return; }

    float h = abs(n - m) + abs(s - m);
    float v = abs(e - m) + abs(w - m);
    if (h > v) {
        float t = clamp((n + s - 2.0 * m) / max(h, 0.0001), -0.5, 0.5);
        c = (texture(scene, fragUV + vec2(0.0, inv.y * t)).rgb + c) * 0.5;
    } else {
        float t = clamp((e + w - 2.0 * m) / max(v, 0.0001), -0.5, 0.5);
        c = (texture(scene, fragUV + vec2(inv.x * t, 0.0)).rgb + c) * 0.5;
    }
    outColor = vec4(c, 1.0);
}
)";

		static const char* blit_gl = R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D scene;
void main() { outColor = texture(scene, fragUV); }
)";
		static const char* blit_vk = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform sampler2D scene;
void main() { outColor = texture(scene, fragUV); }
)";

		fxaa_effect::~fxaa_effect()
		{
			reset();
		}

		bool fxaa_effect::setup(render_backend& rhi, uint w, uint h, std::string& error)
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
			m_target = rhi.create_offscreen(w, h, 1, error, true);
			m_shader_fxaa = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? fxaa_vk : fxaa_gl, error);
			m_shader_blit = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? blit_vk : blit_gl, error);
			if (!m_quad_vb || !m_quad_ib || !m_target || !m_shader_fxaa || !m_shader_blit)
			{
				reset();
				return false;
			}
			m_ready = true;
			return true;
		}

		void fxaa_effect::reset()
		{
			if (m_rhi)
			{
				if (m_quad_vb) m_rhi->vertex_delete(m_quad_vb);
				if (m_quad_ib) m_rhi->index_delete(m_quad_ib);
				if (m_target) m_rhi->delete_offscreen(m_target);
				if (m_shader_fxaa) m_rhi->shader_delete(m_shader_fxaa);
				if (m_shader_blit) m_rhi->shader_delete(m_shader_blit);
			}
			m_quad_vb = m_quad_ib = 0;
			m_target = 0;
			m_shader_fxaa = m_shader_blit = 0;
			m_ready = false;
			m_rhi = nullptr;
		}

		uint fxaa_effect::apply(uint scene_texture)
		{
			if (!m_ready) return 0;
			m_rhi->begin_offscreen(m_target);
			m_rhi->use_program(m_shader_fxaa);
			m_rhi->bind_texture(0, scene_texture);
			m_rhi->uniform_int(m_shader_fxaa, "scene", 0);
			m_rhi->uniform_vec4(m_shader_fxaa, "params", value(1) / value(m_w), value(1) / value(m_h), 0, 0);
			m_rhi->draw_indexed(m_quad_vb, m_quad_ib, rhi_primitive::triangles, 0, 6);
			m_rhi->end_offscreen();
			return m_rhi->offscreen_texture(m_target, 0);
		}

		void fxaa_effect::present(uint texture)
		{
			if (!m_ready) return;
			m_rhi->use_program(m_shader_blit);
			m_rhi->bind_texture(0, texture);
			m_rhi->uniform_int(m_shader_blit, "scene", 0);
			m_rhi->draw_indexed(m_quad_vb, m_quad_ib, rhi_primitive::triangles, 0, 6);
		}
	}
}
