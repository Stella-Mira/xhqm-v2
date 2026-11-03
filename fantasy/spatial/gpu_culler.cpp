#include "gpu_culler.h"
#include <cstring>
#include <cstdint>

namespace xhqm
{
	namespace fantasy
	{
		void extract_frustum_planes(const matrix& m, value planes[6][4])
		{
			auto row = [&](int r, int c) { return m(r, c); };
			for (int c = 0; c < 4; ++c) planes[0][c] = row(3, c) + row(0, c);	// left
			for (int c = 0; c < 4; ++c) planes[1][c] = row(3, c) - row(0, c);	// right
			for (int c = 0; c < 4; ++c) planes[2][c] = row(3, c) + row(1, c);	// bottom
			for (int c = 0; c < 4; ++c) planes[3][c] = row(3, c) - row(1, c);	// top
			for (int c = 0; c < 4; ++c) planes[4][c] = row(3, c) + row(2, c);	// near
			for (int c = 0; c < 4; ++c) planes[5][c] = row(3, c) - row(2, c);	// far
		}

		static std::uint32_t float_bits(value f)
		{
			std::uint32_t bits = 0;
			std::memcpy(&bits, &f, sizeof(bits));
			return bits;
		}

		static std::string cull_shader_source(bool vulkan)
		{
			const char* body = R"GLSL(
layout(local_size_x = 64) in;

layout(std430, binding = 1) buffer Params
{
    uint objectCount;
    uint _p0; uint _p1; uint _p2;
    vec4 planes[6];
} params;

struct Object
{
    vec4 center;      // xyz = AABB 中心，w 未用
    vec4 halfExtent;  // xyz = 半边长，w 未用
    uint cmd[5];
    uint pad[3];
};
layout(std430, binding = 2) readonly buffer Objects { Object objects[]; };

struct DrawCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    uint vertexOffset;
    uint baseInstance;
};
layout(std430, binding = 3) buffer Output { DrawCommand cmds[]; } output;

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= params.objectCount) return;
    Object o = objects[gid];
    bool visible = true;
    for (int p = 0; p < 6; ++p)
    {
        vec4 pl = params.planes[p];
        float d = dot(pl.xyz, o.center.xyz) + pl.w;
        float r = dot(abs(o.halfExtent.xyz), abs(pl.xyz));
        if (d + r < 0.0) { visible = false; break; }
    }
    DrawCommand c;
    c.indexCount = visible ? o.cmd[0] : 0u;
    c.instanceCount = o.cmd[1];
    c.firstIndex = o.cmd[2];
    c.vertexOffset = o.cmd[3];
    c.baseInstance = o.cmd[4];
    output.cmds[gid] = c;
}
)GLSL";
			std::string version = vulkan ? "#version 450\n" : "#version 430 core\n";
			return version + body;
		}

		gpu_culler::~gpu_culler()
		{
			render_backend& rhi = render_backend_active();
			if (m_program) rhi.shader_compute_delete(m_program);
			if (m_params_ssbo) rhi.storage_delete(m_params_ssbo);
			if (m_objects_ssbo) rhi.storage_delete(m_objects_ssbo);
			if (m_output_ssbo) rhi.storage_delete(m_output_ssbo);
			m_program = m_params_ssbo = m_objects_ssbo = m_output_ssbo = 0;
		}

		bool gpu_culler::build(const std::vector<gpu_cull_object>& objects, std::string& error)
		{
			if (objects.empty()) { error = "empty objects"; return false; }
			render_backend& rhi = render_backend_active();
			bool vulkan = (rhi.name()[0] == 'v');

			m_program = rhi.shader_compute_create(cull_shader_source(vulkan), error);
			if (!m_program) return false;

			// params SSBO：uint objectCount + 3 pad + vec4 planes[6] = 112 字节 = 28 word
			std::vector<std::uint32_t> params(28, 0);
			params[0] = (std::uint32_t)objects.size();
			m_params_ssbo = rhi.storage_create(params.size() * 4, params.data(), error);
			if (!m_params_ssbo) return false;

			// objects SSBO：每对象 16 word（64 字节） = center(4) + halfExtent(4) + cmd(5) + pad(3)
			std::vector<std::uint32_t> objdata(objects.size() * 16, 0);
			for (size_t i = 0; i < objects.size(); ++i)
			{
				std::uint32_t* o = objdata.data() + i * 16;
				o[0] = float_bits(objects[i].center[0]);
				o[1] = float_bits(objects[i].center[1]);
				o[2] = float_bits(objects[i].center[2]);
				o[3] = 0;
				o[4] = float_bits(objects[i].half_extent[0]);
				o[5] = float_bits(objects[i].half_extent[1]);
				o[6] = float_bits(objects[i].half_extent[2]);
				o[7] = 0;
				for (int k = 0; k < 5; ++k) o[8 + k] = objects[i].cmd[k];
			}
			m_objects_ssbo = rhi.storage_create(objdata.size() * 4, objdata.data(), error);
			if (!m_objects_ssbo) return false;

			// output SSBO：每对象 5 uint（20 字节）DrawCommand，初始全 0
			std::vector<std::uint32_t> outcmds(objects.size() * 5, 0);
			m_output_ssbo = rhi.storage_create(outcmds.size() * 4, outcmds.data(), error);
			if (!m_output_ssbo) return false;

			m_object_count = (uint)objects.size();
			return true;
		}

		void gpu_culler::cull(const value planes[6][4])
		{
			if (!m_program || !m_params_ssbo) return;
			render_backend& rhi = render_backend_active();

			std::vector<std::uint32_t> params(28, 0);
			params[0] = (std::uint32_t)m_object_count;
			for (int p = 0; p < 6; ++p)
				for (int c = 0; c < 4; ++c) params[4 + p * 4 + c] = float_bits(planes[p][c]);
			rhi.storage_update(m_params_ssbo, params.data(), params.size() * 4);

			rhi.compute_storage_bind(m_program, 1, m_params_ssbo);
			rhi.compute_storage_bind(m_program, 2, m_objects_ssbo);
			rhi.compute_storage_bind(m_program, 3, m_output_ssbo);
			uint groups = (m_object_count + 63) / 64;
			rhi.compute_dispatch(m_program, groups, 1, 1);
		}

		void gpu_culler::draw_indexed(uint vertex, uint index)
		{
			if (!m_output_ssbo || !m_object_count) return;
			render_backend_active().draw_indexed_indirect_storage(vertex, index, m_output_ssbo, 0, m_object_count);
		}
	}
}
