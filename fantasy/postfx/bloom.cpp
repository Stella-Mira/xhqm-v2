#include "bloom.h"

namespace xhqm
{
	namespace fantasy
	{
		// ---- 全屏四边形顶点着色器 ----
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

		// 亮度提取（阈值 + 平滑截断）
		static const char* bright_gl = R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D scene;
uniform vec4 params;   // x=threshold
void main() {
    vec3 c = texture(scene, fragUV).rgb;
    float b = max(c.r, max(c.g, c.b));
    float w = smoothstep(params.x, params.x + 0.5, b);
    outColor = vec4(c * w, 1.0);
}
)";
		static const char* bright_vk = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(std140, set = 0, binding = 0) uniform UBO { vec4 params; } ubo;
layout(set = 0, binding = 1) uniform sampler2D scene;
void main() {
    vec3 c = texture(scene, fragUV).rgb;
    float b = max(c.r, max(c.g, c.b));
    float w = smoothstep(ubo.params.x, ubo.params.x + 0.5, b);
    outColor = vec4(c * w, 1.0);
}
)";

		// 3x3 高斯模糊（texelSize = params.xy）
		static const char* blur_gl = R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D scene;
uniform vec4 params;   // xy=texelSize
void main() {
    vec2 t = params.xy;
    vec3 sum = texture(scene, fragUV).rgb * 4.0;
    sum += texture(scene, fragUV + vec2(t.x, 0.0)).rgb * 2.0;
    sum += texture(scene, fragUV - vec2(t.x, 0.0)).rgb * 2.0;
    sum += texture(scene, fragUV + vec2(0.0, t.y)).rgb * 2.0;
    sum += texture(scene, fragUV - vec2(0.0, t.y)).rgb * 2.0;
    sum += texture(scene, fragUV + t).rgb;
    sum += texture(scene, fragUV - t).rgb;
    sum += texture(scene, fragUV + vec2(t.x, -t.y)).rgb;
    sum += texture(scene, fragUV + vec2(-t.x, t.y)).rgb;
    outColor = vec4(sum / 16.0, 1.0);
}
)";
		static const char* blur_vk = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(std140, set = 0, binding = 0) uniform UBO { vec4 params; } ubo;
layout(set = 0, binding = 1) uniform sampler2D scene;
void main() {
    vec2 t = ubo.params.xy;
    vec3 sum = texture(scene, fragUV).rgb * 4.0;
    sum += texture(scene, fragUV + vec2(t.x, 0.0)).rgb * 2.0;
    sum += texture(scene, fragUV - vec2(t.x, 0.0)).rgb * 2.0;
    sum += texture(scene, fragUV + vec2(0.0, t.y)).rgb * 2.0;
    sum += texture(scene, fragUV - vec2(0.0, t.y)).rgb * 2.0;
    sum += texture(scene, fragUV + t).rgb;
    sum += texture(scene, fragUV - t).rgb;
    sum += texture(scene, fragUV + vec2(t.x, -t.y)).rgb;
    sum += texture(scene, fragUV + vec2(-t.x, t.y)).rgb;
    outColor = vec4(sum / 16.0, 1.0);
}
)";

		// 合成（scene + intensity * bloom）
		static const char* composite_gl = R"(
#version 330 core
in vec2 fragUV; out vec4 outColor;
uniform sampler2D scene;
uniform sampler2D bloom;
uniform vec4 params;   // x=intensity
void main() {
    vec3 s = texture(scene, fragUV).rgb;
    vec3 b = texture(bloom, fragUV).rgb;
    outColor = vec4(s + b * params.x, 1.0);
}
)";
		static const char* composite_vk = R"(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(std140, set = 0, binding = 0) uniform UBO { vec4 params; } ubo;
layout(set = 0, binding = 1) uniform sampler2D scene;
layout(set = 0, binding = 2) uniform sampler2D bloom;
void main() {
    vec3 s = texture(scene, fragUV).rgb;
    vec3 b = texture(bloom, fragUV).rgb;
    outColor = vec4(s + b * ubo.params.x, 1.0);
}
)";

		// 直通
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

		bloom_effect::~bloom_effect()
		{
			reset();
		}

		bool bloom_effect::setup(render_backend& rhi, uint w, uint h, std::string& error)
		{
			reset();
			m_rhi = &rhi;
			m_w = w; m_h = h;
			bool vk = (rhi.name()[0] == 'v');

			// 全屏四边形：4 顶点 (pos.xy, uv.xy) + 6 索引
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
			if (!m_quad_vb || !m_quad_ib) { reset(); return false; }

			m_target_bright = rhi.create_offscreen(w, h, 1, error, true);
			m_target_blur = rhi.create_offscreen(w, h, 1, error, true);
			m_target_result = rhi.create_offscreen(w, h, 1, error, true);
			if (!m_target_bright || !m_target_blur || !m_target_result) { reset(); return false; }

			m_shader_bright = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? bright_vk : bright_gl, error);
			m_shader_blur = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? blur_vk : blur_gl, error);
			m_shader_composite = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? composite_vk : composite_gl, error);
			m_shader_blit = rhi.shader_create(vk ? vert_vk : vert_gl, vk ? blit_vk : blit_gl, error);
			if (!m_shader_bright || !m_shader_blur || !m_shader_composite || !m_shader_blit) { reset(); return false; }

			m_ready = true;
			return true;
		}

		void bloom_effect::reset()
		{
			if (m_rhi)
			{
				if (m_quad_vb) m_rhi->vertex_delete(m_quad_vb);
				if (m_quad_ib) m_rhi->index_delete(m_quad_ib);
				if (m_target_bright) m_rhi->delete_offscreen(m_target_bright);
				if (m_target_blur) m_rhi->delete_offscreen(m_target_blur);
				if (m_target_result) m_rhi->delete_offscreen(m_target_result);
				if (m_shader_bright) m_rhi->shader_delete(m_shader_bright);
				if (m_shader_blur) m_rhi->shader_delete(m_shader_blur);
				if (m_shader_composite) m_rhi->shader_delete(m_shader_composite);
				if (m_shader_blit) m_rhi->shader_delete(m_shader_blit);
			}
			m_quad_vb = m_quad_ib = 0;
			m_target_bright = m_target_blur = m_target_result = 0;
			m_shader_bright = m_shader_blur = m_shader_composite = m_shader_blit = 0;
			m_ready = false;
			m_rhi = nullptr;
		}

		void bloom_effect::draw_quad()
		{
			m_rhi->draw_indexed(m_quad_vb, m_quad_ib, rhi_primitive::triangles, 0, 6);
		}

		uint bloom_effect::apply(uint scene_texture)
		{
			if (!m_ready) return 0;

			// 1) 亮度提取：scene → bright
			m_rhi->begin_offscreen(m_target_bright);
			m_rhi->use_program(m_shader_bright);
			m_rhi->bind_texture(0, scene_texture);
			m_rhi->uniform_int(m_shader_bright, "scene", 0);
			m_rhi->uniform_vec4(m_shader_bright, "params", m_threshold, 0, 0, 0);
			draw_quad();
			m_rhi->end_offscreen();
			uint bright_tex = m_rhi->offscreen_texture(m_target_bright, 0);

			// 2) 模糊：bright → blur
			m_rhi->begin_offscreen(m_target_blur);
			m_rhi->use_program(m_shader_blur);
			m_rhi->bind_texture(0, bright_tex);
			m_rhi->uniform_int(m_shader_blur, "scene", 0);
			m_rhi->uniform_vec4(m_shader_blur, "params", value(1) / value(m_w), value(1) / value(m_h), 0, 0);
			draw_quad();
			m_rhi->end_offscreen();
			uint blur_tex = m_rhi->offscreen_texture(m_target_blur, 0);

			// 3) 合成：scene + bloom → result
			m_rhi->begin_offscreen(m_target_result);
			m_rhi->use_program(m_shader_composite);
			m_rhi->bind_texture(0, scene_texture);
			m_rhi->bind_texture(1, blur_tex);
			m_rhi->uniform_int(m_shader_composite, "scene", 0);
			m_rhi->uniform_int(m_shader_composite, "bloom", 1);
			m_rhi->uniform_vec4(m_shader_composite, "params", m_intensity, 0, 0, 0);
			draw_quad();
			m_rhi->end_offscreen();
			return m_rhi->offscreen_texture(m_target_result, 0);
		}

		void bloom_effect::present(uint texture)
		{
			if (!m_ready) return;
			m_rhi->use_program(m_shader_blit);
			m_rhi->bind_texture(0, texture);
			m_rhi->uniform_int(m_shader_blit, "scene", 0);
			draw_quad();
		}
	}
}
