#include "taa.h"

namespace xhqm
{
	namespace fantasy
	{
		std::array<value, 2> taa_jitter(uint frame_index)
		{
			// Halton 2,3 序列 → [0,1)，再映射到 [-0.5, 0.5]
			auto halton = [](uint idx, uint base) -> value
			{
				value f = 1.0f;
				value r = 0.0f;
				uint i = idx;
				while (i > 0)
				{
					f /= value(base);
					r += f * value(i % base);
					i /= base;
				}
				return r;
			};
			value x = halton(frame_index + 1, 2) - 0.5f;
			value y = halton(frame_index + 1, 3) - 0.5f;
			return { x, y };
		}

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

		static const char* taa_gl = R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D current;
uniform sampler2D history;
uniform vec4 params;   // x=alpha y=texelX z=texelY
void main() {
    vec3 cur = texture(current, fragUV).rgb;
    vec3 hist = texture(history, fragUV).rgb;
    vec3 mn = hist, mx = hist;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            vec3 h = texture(history, fragUV + vec2(float(dx) * params.y, float(dy) * params.z)).rgb;
            mn = min(mn, h);
            mx = max(mx, h);
        }
    cur = clamp(cur, mn, mx);
    outColor = vec4(mix(hist, cur, params.x), 1.0);
}
)";

		static const char* taa_vk = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(std140, set = 0, binding = 0) uniform UBO { vec4 params; } ubo;
layout(set = 0, binding = 1) uniform sampler2D current;
layout(set = 0, binding = 2) uniform sampler2D history;
void main() {
    vec3 cur = texture(current, fragUV).rgb;
    vec3 hist = texture(history, fragUV).rgb;
    vec3 mn = hist, mx = hist;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            vec3 h = texture(history, fragUV + vec2(float(dx) * ubo.params.y, float(dy) * ubo.params.z)).rgb;
            mn = min(mn, h);
            mx = max(mx, h);
        }
    cur = clamp(cur, mn, mx);
    outColor = vec4(mix(hist, cur, ubo.params.x), 1.0);
}
)";

		taa_effect::~taa_effect() { reset(); }

		bool taa_effect::setup(render_backend& rhi, uint w, uint h, std::string& error)
		{
			reset();
			m_rhi = &rhi;
			m_w = w; m_h = h;
			bool vk = (rhi.name()[0] == 'v');

			std::vector<value> quad = { -1, -1, 0, 0,  1, -1, 1, 0,  1, 1, 1, 1,  -1, 1, 0, 1 };
			std::vector<uint> locations = { 2, 2 };
			m_quad_vb = rhi.vertex_create(quad, locations, error);
			std::vector<uint> indices = { 0, 1, 2, 0, 2, 3 };
			m_quad_ib = rhi.index_create(indices, error);
			m_target = rhi.create_offscreen(w, h, 1, error, false);
			m_shader = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? taa_vk : taa_gl, error);
			if (!m_quad_vb || !m_quad_ib || !m_target || !m_shader) { reset(); return false; }
			m_ready = true;
			return true;
		}

		void taa_effect::reset()
		{
			if (m_rhi)
			{
				if (m_quad_vb) m_rhi->vertex_delete(m_quad_vb);
				if (m_quad_ib) m_rhi->index_delete(m_quad_ib);
				if (m_target) m_rhi->delete_offscreen(m_target);
				if (m_shader) m_rhi->shader_delete(m_shader);
			}
			m_quad_vb = m_quad_ib = m_target = m_shader = 0;
			m_ready = false;
			m_rhi = nullptr;
		}

		uint taa_effect::apply(uint current_tex, uint history_tex, value alpha)
		{
			if (!m_ready) return 0;
			value texel_x = 1.0f / value(m_w);
			value texel_y = 1.0f / value(m_h);
			m_rhi->begin_offscreen(m_target);
			m_rhi->use_program(m_shader);
			m_rhi->bind_texture(0, current_tex);
			m_rhi->bind_texture(1, history_tex);
			m_rhi->uniform_int(m_shader, "current", 0);
			m_rhi->uniform_int(m_shader, "history", 1);
			m_rhi->uniform_vec4(m_shader, "params", alpha, texel_x, texel_y, 0);
			m_rhi->draw_indexed(m_quad_vb, m_quad_ib, rhi_primitive::triangles, 0, 6);
			m_rhi->end_offscreen();
			return m_rhi->offscreen_texture(m_target, 0);
		}
	}
}
