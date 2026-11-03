#include "compose.h"

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

		static const char* frag_gl = R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D baseTex;
uniform sampler2D overlayTex;
uniform vec4 params;   // x = mode: 0=multiply(AO) 1=add(SSR)
void main()
{
    vec4 base = texture(baseTex, fragUV);
    vec4 over = texture(overlayTex, fragUV);
    vec3 c = (int(params.x + 0.5) == 0) ? (base.rgb * over.rgb) : (base.rgb + over.rgb);
    outColor = vec4(c, base.a);
}
)";

		static const char* frag_vk = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(std140, set = 0, binding = 0) uniform UBO { vec4 params; } ubo;
layout(set = 0, binding = 1) uniform sampler2D baseTex;
layout(set = 0, binding = 2) uniform sampler2D overlayTex;
void main()
{
    vec4 base = texture(baseTex, fragUV);
    vec4 over = texture(overlayTex, fragUV);
    vec3 c = (int(ubo.params.x + 0.5) == 0) ? (base.rgb * over.rgb) : (base.rgb + over.rgb);
    outColor = vec4(c, base.a);
}
)";

		compose_effect::~compose_effect()
		{
			reset();
		}

		bool compose_effect::setup(render_backend& rhi, uint w, uint h, std::string& error)
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
			m_target = rhi.create_offscreen(w, h, 1, error, true);   // HDR，与场景一致
			m_shader = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? frag_vk : frag_gl, error);
			if (!m_quad_vb || !m_quad_ib || !m_target || !m_shader)
			{
				reset();
				return false;
			}
			m_ready = true;
			return true;
		}

		void compose_effect::reset()
		{
			if (m_rhi)
			{
				if (m_quad_vb) m_rhi->vertex_delete(m_quad_vb);
				if (m_quad_ib) m_rhi->index_delete(m_quad_ib);
				if (m_target) m_rhi->delete_offscreen(m_target);
				if (m_shader) m_rhi->shader_delete(m_shader);
			}
			m_quad_vb = m_quad_ib = 0;
			m_target = 0;
			m_shader = 0;
			m_ready = false;
			m_rhi = nullptr;
		}

		uint compose_effect::apply(uint base_tex, uint overlay_tex, int mode)
		{
			if (!m_ready) return 0;
			m_rhi->begin_offscreen(m_target);
			m_rhi->use_program(m_shader);
			m_rhi->bind_texture(0, base_tex);
			m_rhi->bind_texture(1, overlay_tex);
			m_rhi->uniform_int(m_shader, "baseTex", 0);
			m_rhi->uniform_int(m_shader, "overlayTex", 1);
			m_rhi->uniform_vec4(m_shader, "params", static_cast<value>(mode), 0.0f, 0.0f, 0.0f);
			m_rhi->draw_indexed(m_quad_vb, m_quad_ib, rhi_primitive::triangles, 0, 6);
			m_rhi->end_offscreen();
			return m_rhi->offscreen_texture(m_target, 0);
		}
	}
}
