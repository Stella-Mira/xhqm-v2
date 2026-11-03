#include "ssao.h"

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

		// 视图空间半球核（z 朝相机），16 采样
		static const char* kernel_glsl = R"(
const int KERNEL_N = 16;
const vec3 KERNEL[16] = vec3[16](
    vec3( 0.5381,  0.1856,  0.4319),
    vec3( 0.1379,  0.2486,  0.4430),
    vec3( 0.3371,  0.5679,  0.2057),
    vec3(-0.6999, -0.0451,  0.2660),
    vec3( 0.0689, -0.1598,  0.8547),
    vec3( 0.0560,  0.0069,  0.3843),
    vec3(-0.0146,  0.1402,  0.3762),
    vec3( 0.0100, -0.1924,  0.4344),
    vec3(-0.3577, -0.5301,  0.4358),
    vec3(-0.3169,  0.1063,  0.4158),
    vec3( 0.0103, -0.5869,  0.4046),
    vec3(-0.0897, -0.4940,  0.4287),
    vec3( 0.7119, -0.0154,  0.4918),
    vec3(-0.0533,  0.0596,  0.5411),
    vec3( 0.0352, -0.0631,  0.5460),
    vec3(-0.4776,  0.2847,  0.4271)
);
)";

		static const std::string ssao_gl = std::string(R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D depthTex;
uniform vec4 params;   // x=aspect y=tanHalfFov z=radius w=bias
)") + kernel_glsl + R"(
void main() {
    float depth = texture(depthTex, fragUV).r;
    if (depth <= 0.0) { outColor = vec4(1.0, 1.0, 1.0, 1.0); return; }
    vec3 pos = vec3((fragUV * 2.0 - 1.0) * vec2(params.x, 1.0) * params.y * depth, -depth);
    float ao = 0.0;
    for (int i = 0; i < KERNEL_N; ++i) {
        vec3 sp = pos + KERNEL[i] * params.z;
        float sd = -sp.z;
        if (sd <= 0.0) continue;
        vec2 suv = (sp.xy / (vec2(params.x, 1.0) * params.y * sd)) * 0.5 + 0.5;
        float sdepth = texture(depthTex, suv).r;
        float rangeCheck = smoothstep(0.0, 1.0, params.z / abs(sd - sdepth));
        ao += (sdepth < sd - params.w) ? rangeCheck : 0.0;
    }
    ao = 1.0 - ao / float(KERNEL_N);
    outColor = vec4(ao, ao, ao, 1.0);
}
)";

		static const std::string ssao_vk = std::string(R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(std140, set = 0, binding = 0) uniform UBO { vec4 params; } ubo;
layout(set = 0, binding = 1) uniform sampler2D depthTex;
)") + kernel_glsl + R"(
void main() {
    float depth = texture(depthTex, fragUV).r;
    if (depth <= 0.0) { outColor = vec4(1.0, 1.0, 1.0, 1.0); return; }
    vec3 pos = vec3((fragUV * 2.0 - 1.0) * vec2(ubo.params.x, 1.0) * ubo.params.y * depth, -depth);
    float ao = 0.0;
    for (int i = 0; i < KERNEL_N; ++i) {
        vec3 sp = pos + KERNEL[i] * ubo.params.z;
        float sd = -sp.z;
        if (sd <= 0.0) continue;
        vec2 suv = (sp.xy / (vec2(ubo.params.x, 1.0) * ubo.params.y * sd)) * 0.5 + 0.5;
        float sdepth = texture(depthTex, suv).r;
        float rangeCheck = smoothstep(0.0, 1.0, ubo.params.z / abs(sd - sdepth));
        ao += (sdepth < sd - ubo.params.w) ? rangeCheck : 0.0;
    }
    ao = 1.0 - ao / float(KERNEL_N);
    outColor = vec4(ao, ao, ao, 1.0);
}
)";

		ssao_effect::~ssao_effect()
		{
			reset();
		}

		bool ssao_effect::setup(render_backend& rhi, uint w, uint h, std::string& error)
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
			m_target = rhi.create_offscreen(w, h, 1, error, false);   // AO 输出 RGBA8
			m_shader = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? ssao_vk : ssao_gl, error);
			if (!m_quad_vb || !m_quad_ib || !m_target || !m_shader)
			{
				reset();
				return false;
			}
			m_ready = true;
			return true;
		}

		void ssao_effect::reset()
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

		uint ssao_effect::apply(uint depth_tex, value aspect, value tan_half_fov, value radius, value bias)
		{
			if (!m_ready) return 0;
			m_rhi->begin_offscreen(m_target);
			m_rhi->use_program(m_shader);
			m_rhi->bind_texture(0, depth_tex);
			m_rhi->uniform_int(m_shader, "depthTex", 0);
			m_rhi->uniform_vec4(m_shader, "params", aspect, tan_half_fov, radius, bias);
			m_rhi->draw_indexed(m_quad_vb, m_quad_ib, rhi_primitive::triangles, 0, 6);
			m_rhi->end_offscreen();
			return m_rhi->offscreen_texture(m_target, 0);
		}
	}
}
