#include "gpu_hiz_culler.h"
#include <cstring>
#include <cstdint>
#include <cmath>

namespace xhqm
{
	namespace fantasy
	{
		static std::uint32_t float_bits(value f)
		{
			std::uint32_t bits = 0;
			std::memcpy(&bits, &f, sizeof(bits));
			return bits;
		}

		static std::string hiz_cull_shader_source(bool vulkan)
		{
			const char* body = R"GLSL(
layout(local_size_x = 64) in;

layout(std430, binding = 1) buffer Params
{
    uint objectCount;
    uint screenW; uint screenH;
    uint hizW; uint hizH; uint hizLevels;
    uint _p0; uint _p1;
    mat4 viewProj;
    uint hizOffsets[12];
} params;

struct Object
{
    vec4 center;
    vec4 halfExtent;
    uint cmd[5];
    uint pad[3];
};
layout(std430, binding = 2) readonly buffer Objects { Object objects[]; };

layout(std430, binding = 3) readonly buffer Depth { float depth[]; } hiz;

struct DrawCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    uint vertexOffset;
    uint baseInstance;
};
layout(std430, binding = 4) buffer OutCmds { DrawCommand cmds[]; } outCmds;

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= params.objectCount) return;
    Object o = objects[gid];

    vec3 c = o.center.xyz;
    vec3 h = o.halfExtent.xyz;
    vec3 corners[8];
    corners[0] = c + vec3(-h.x, -h.y, -h.z);
    corners[1] = c + vec3( h.x, -h.y, -h.z);
    corners[2] = c + vec3(-h.x,  h.y, -h.z);
    corners[3] = c + vec3( h.x,  h.y, -h.z);
    corners[4] = c + vec3(-h.x, -h.y,  h.z);
    corners[5] = c + vec3( h.x, -h.y,  h.z);
    corners[6] = c + vec3(-h.x,  h.y,  h.z);
    corners[7] = c + vec3( h.x,  h.y,  h.z);

    vec3 ndcMin = vec3( 1e30);
    vec3 ndcMax = vec3(-1e30);
    int validCount = 0;
    for (int i = 0; i < 8; ++i)
    {
        vec4 cl = params.viewProj * vec4(corners[i], 1.0);
        if (cl.w <= 0.0) continue;   // 相机后方：保守跳过
        vec3 ndc = cl.xyz / cl.w;
        ndcMin = min(ndcMin, ndc);
        ndcMax = max(ndcMax, ndc);
        validCount++;
    }
    if (validCount == 0) { outCmds.cmds[gid].indexCount = 0; return; }

    bool visible = true;
    if (ndcMax.x < -1.0 || ndcMin.x > 1.0) visible = false;
    if (ndcMax.y < -1.0 || ndcMin.y > 1.0) visible = false;
    if (ndcMax.z < -1.0 || ndcMin.z > 1.0) visible = false;

    // Hi-Z 遮挡测试（0=近 1=远；最近深度 = 0.5*(minNdcZ+1)）
    if (visible)
    {
        vec2 scrMin = (ndcMin.xy * 0.5 + 0.5) * vec2(params.screenW, params.screenH);
        vec2 scrMax = (ndcMax.xy * 0.5 + 0.5) * vec2(params.screenW, params.screenH);
        float rectW = max(scrMax.x - scrMin.x, 1.0);
        float rectH = max(scrMax.y - scrMin.y, 1.0);
        uint level = uint(ceil(log2(max(rectW, rectH))));
        level = min(level, max(params.hizLevels, 1u) - 1u);
        level = min(level, 11u);   // hizOffsets 数组长度 12，防止 >12 级越界
        uint lw = (params.hizW + (1u << level) - 1u) >> level;
        uint lh = (params.hizH + (1u << level) - 1u) >> level;
        uint lx0 = uint(clamp(scrMin.x, 0.0, float(params.screenW) - 1.0)) >> level;
        uint ly0 = uint(clamp(scrMin.y, 0.0, float(params.screenH) - 1.0)) >> level;
        uint lx1 = uint(clamp(scrMax.x, 0.0, float(params.screenW) - 1.0)) >> level;
        uint ly1 = uint(clamp(scrMax.y, 0.0, float(params.screenH) - 1.0)) >> level;
        lx1 = min(lx1, lw - 1u);
        ly1 = min(ly1, lh - 1u);
        uint base = params.hizOffsets[level];
        float m00 = hiz.depth[base + ly0 * lw + lx0];
        float m10 = hiz.depth[base + ly0 * lw + lx1];
        float m01 = hiz.depth[base + ly1 * lw + lx0];
        float m11 = hiz.depth[base + ly1 * lw + lx1];
        float hizMax = max(max(m00, m10), max(m01, m11));
        float nearestDepth = 0.5 * (ndcMin.z + 1.0);
        if (nearestDepth > hizMax) visible = false;   // 对象最近点仍在块最远深度之后 → 被遮挡
    }

    DrawCommand dc;
    dc.indexCount = visible ? o.cmd[0] : 0u;
    dc.instanceCount = o.cmd[1];
    dc.firstIndex = o.cmd[2];
    dc.vertexOffset = o.cmd[3];
    dc.baseInstance = o.cmd[4];
    outCmds.cmds[gid] = dc;
}
)GLSL";
			std::string version = vulkan ? "#version 450\n" : "#version 430 core\n";
			return version + body;
		}

		gpu_hiz_culler::~gpu_hiz_culler()
		{
			render_backend& rhi = render_backend_active();
			if (m_program) rhi.shader_compute_delete(m_program);
			if (m_params_ssbo) rhi.storage_delete(m_params_ssbo);
			if (m_objects_ssbo) rhi.storage_delete(m_objects_ssbo);
			if (m_output_ssbo) rhi.storage_delete(m_output_ssbo);
			m_program = m_params_ssbo = m_objects_ssbo = m_output_ssbo = 0;
		}

		bool gpu_hiz_culler::build(const std::vector<gpu_hiz_cull_object>& objects, std::string& error)
		{
			if (objects.empty()) { error = "empty objects"; return false; }
			render_backend& rhi = render_backend_active();
			bool vulkan = (rhi.name()[0] == 'v');

			m_program = rhi.shader_compute_create(hiz_cull_shader_source(vulkan), error);
			if (!m_program) return false;

			// params SSBO：36 uint（144 字节）
			std::vector<std::uint32_t> params(36, 0);
			params[0] = (std::uint32_t)objects.size();
			m_params_ssbo = rhi.storage_create(params.size() * 4, params.data(), error);
			if (!m_params_ssbo) return false;

			// objects SSBO：每对象 16 word（64 字节）= center(4) + halfExtent(4) + cmd(5) + pad(3)
			std::vector<std::uint32_t> objdata(objects.size() * 16, 0);
			for (size_t i = 0; i < objects.size(); ++i)
			{
				std::uint32_t* o = objdata.data() + i * 16;
				for (int k = 0; k < 3; ++k) o[k] = float_bits(objects[i].center[k]);
				o[3] = 0;
				for (int k = 0; k < 3; ++k) o[4 + k] = float_bits(objects[i].half_extent[k]);
				o[7] = 0;
				for (int k = 0; k < 5; ++k) o[8 + k] = objects[i].cmd[k];
			}
			m_objects_ssbo = rhi.storage_create(objdata.size() * 4, objdata.data(), error);
			if (!m_objects_ssbo) return false;

			// output SSBO：每对象 5 uint（20 字节）DrawCommand
			std::vector<std::uint32_t> outcmds(objects.size() * 5, 0);
			m_output_ssbo = rhi.storage_create(outcmds.size() * 4, outcmds.data(), error);
			if (!m_output_ssbo) return false;

			m_object_count = (uint)objects.size();
			return true;
		}

		void gpu_hiz_culler::cull(const matrix& view_proj, uint screen_w, uint screen_h, uint hiz_ssbo, uint hiz_w, uint hiz_h, uint hiz_levels)
		{
			if (!m_program || !m_object_count || !hiz_ssbo) return;
			render_backend& rhi = render_backend_active();

			// 计算 Hi-Z 各层偏移（float 单位，与 gpu_hiz 一致）
			std::vector<std::uint32_t> offsets(12, 0);
			uint off = 0, cw = hiz_w, ch = hiz_h;
			for (uint i = 0; i < hiz_levels && i < 12; ++i)
			{
				offsets[i] = off;
				off += cw * ch;
				if (cw == 1 && ch == 1) break;
				cw = (cw + 1) / 2;
				ch = (ch + 1) / 2;
			}

			std::vector<std::uint32_t> params(36, 0);
			params[0] = (std::uint32_t)m_object_count;
			params[1] = screen_w;
			params[2] = screen_h;
			params[3] = hiz_w;
			params[4] = hiz_h;
			params[5] = hiz_levels;
			matrix vpt = view_proj.tramat();
			for (int i = 0; i < 16; ++i) params[8 + i] = float_bits(vpt.data()[i]);
			for (uint i = 0; i < 12; ++i) params[24 + i] = offsets[i];
			rhi.storage_update(m_params_ssbo, params.data(), params.size() * 4);

			rhi.compute_storage_bind(m_program, 1, m_params_ssbo);
			rhi.compute_storage_bind(m_program, 2, m_objects_ssbo);
			rhi.compute_storage_bind(m_program, 3, hiz_ssbo);
			rhi.compute_storage_bind(m_program, 4, m_output_ssbo);
			uint groups = (m_object_count + 63) / 64;
			rhi.compute_dispatch(m_program, groups, 1, 1);
		}

		void gpu_hiz_culler::draw_indexed(uint vertex, uint index)
		{
			if (!m_output_ssbo || !m_object_count) return;
			render_backend_active().draw_indexed_indirect_storage(vertex, index, m_output_ssbo, 0, m_object_count);
		}

		std::vector<bool> gpu_hiz_culler::read_visible()
		{
			std::vector<bool> out;
			if (!m_output_ssbo || !m_object_count) return out;
			std::vector<std::uint32_t> raw(m_object_count * 5, 0);
			render_backend_active().storage_read(m_output_ssbo, raw.data(), raw.size() * 4);
			out.resize(m_object_count);
			for (uint i = 0; i < m_object_count; ++i) out[i] = (raw[i * 5] != 0);
			return out;
		}
	}
}
