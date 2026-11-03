#include "ssr.h"

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

		static const char* ssr_gl = R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D colorTex;
uniform sampler2D depthTex;
uniform vec4 params;   // x=aspect y=tanHalfFov z=stepSize w=bias

vec3 reconstruct(float depth, vec2 uv) {
    return vec3((uv * 2.0 - 1.0) * vec2(params.x, 1.0) * params.y * depth, -depth);
}
vec2 project(vec3 p) {
    float d = -p.z;
    return (p.xy / (vec2(params.x, 1.0) * params.y * d)) * 0.5 + 0.5;
}

void main() {
    float depth = texture(depthTex, fragUV).r;
    if (depth <= 0.0) { outColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    vec3 pos = reconstruct(depth, fragUV);
    vec3 normal = normalize(cross(dFdx(pos), dFdy(pos)));
    vec3 viewDir = normalize(pos);
    vec3 reflDir = reflect(viewDir, normal);

    vec3 rayPos = pos;
    vec3 reflCol = vec3(0.0);
    for (int i = 0; i < 16; ++i) {
        rayPos += reflDir * params.z;
        vec2 ruv = project(rayPos);
        if (ruv.x < 0.0 || ruv.x > 1.0 || ruv.y < 0.0 || ruv.y > 1.0) {
            reflCol = texture(colorTex, clamp(ruv, 0.0, 1.0)).rgb;   // 出屏 → 取边缘天空色
            break;
        }
        float rayDepth = -rayPos.z;
        float sDepth = texture(depthTex, ruv).r;
        if (rayDepth > sDepth + params.w) {   // 射线落到表面之后 → 命中
            reflCol = texture(colorTex, ruv).rgb;
            break;
        }
    }
    outColor = vec4(reflCol, 1.0);
}
)";

		static const char* ssr_vk = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(std140, set = 0, binding = 0) uniform UBO { vec4 params; } ubo;
layout(set = 0, binding = 1) uniform sampler2D colorTex;
layout(set = 0, binding = 2) uniform sampler2D depthTex;

vec3 reconstruct(float depth, vec2 uv) {
    return vec3((uv * 2.0 - 1.0) * vec2(ubo.params.x, 1.0) * ubo.params.y * depth, -depth);
}
vec2 project(vec3 p) {
    float d = -p.z;
    return (p.xy / (vec2(ubo.params.x, 1.0) * ubo.params.y * d)) * 0.5 + 0.5;
}

void main() {
    float depth = texture(depthTex, fragUV).r;
    if (depth <= 0.0) { outColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    vec3 pos = reconstruct(depth, fragUV);
    vec3 normal = normalize(cross(dFdx(pos), dFdy(pos)));
    vec3 viewDir = normalize(pos);
    vec3 reflDir = reflect(viewDir, normal);

    vec3 rayPos = pos;
    vec3 reflCol = vec3(0.0);
    for (int i = 0; i < 16; ++i) {
        rayPos += reflDir * ubo.params.z;
        vec2 ruv = project(rayPos);
        if (ruv.x < 0.0 || ruv.x > 1.0 || ruv.y < 0.0 || ruv.y > 1.0) {
            reflCol = texture(colorTex, clamp(ruv, 0.0, 1.0)).rgb;
            break;
        }
        float rayDepth = -rayPos.z;
        float sDepth = texture(depthTex, ruv).r;
        if (rayDepth > sDepth + ubo.params.w) {
            reflCol = texture(colorTex, ruv).rgb;
            break;
        }
    }
    outColor = vec4(reflCol, 1.0);
}
)";

		ssr_effect::~ssr_effect() { reset(); }

		bool ssr_effect::setup(render_backend& rhi, uint w, uint h, std::string& error)
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
			m_shader = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? ssr_vk : ssr_gl, error);
			if (!m_quad_vb || !m_quad_ib || !m_target || !m_shader) { reset(); return false; }
			m_ready = true;
			return true;
		}

		void ssr_effect::reset()
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

		uint ssr_effect::apply(uint color_tex, uint depth_tex, value aspect, value tan_half_fov, value step_size, value bias)
		{
			if (!m_ready) return 0;
			m_rhi->begin_offscreen(m_target);
			m_rhi->use_program(m_shader);
			m_rhi->bind_texture(0, color_tex);
			m_rhi->bind_texture(1, depth_tex);
			m_rhi->uniform_int(m_shader, "colorTex", 0);
			m_rhi->uniform_int(m_shader, "depthTex", 1);
			m_rhi->uniform_vec4(m_shader, "params", aspect, tan_half_fov, step_size, bias);
			m_rhi->draw_indexed(m_quad_vb, m_quad_ib, rhi_primitive::triangles, 0, 6);
			m_rhi->end_offscreen();
			return m_rhi->offscreen_texture(m_target, 0);
		}
	}
}
