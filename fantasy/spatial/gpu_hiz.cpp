#include "gpu_hiz.h"
#include <cstring>
#include <cstdint>

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

		static std::string hiz_shader_source(bool vulkan)
		{
			const char* body = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;

layout(std430, binding = 1) buffer Params
{
    uint srcW; uint srcH;
    uint dstW; uint dstH;
    uint srcOffset; uint dstOffset;
} params;

layout(std430, binding = 2) buffer Depth { float depth[]; } depthBuf;

void main()
{
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;
    if (x >= params.dstW || y >= params.dstH) return;
    uint sx0 = x * 2u, sy0 = y * 2u;
    uint sx1 = min(sx0 + 1u, params.srcW - 1u);
    uint sy1 = min(sy0 + 1u, params.srcH - 1u);
    float d00 = depthBuf.depth[params.srcOffset + sy0 * params.srcW + sx0];
    float d10 = depthBuf.depth[params.srcOffset + sy0 * params.srcW + sx1];
    float d01 = depthBuf.depth[params.srcOffset + sy1 * params.srcW + sx0];
    float d11 = depthBuf.depth[params.srcOffset + sy1 * params.srcW + sx1];
    float m = max(max(d00, d10), max(d01, d11));
    depthBuf.depth[params.dstOffset + y * params.dstW + x] = m;
}
)GLSL";
			std::string version = vulkan ? "#version 450\n" : "#version 430 core\n";
			return version + body;
		}

		gpu_hiz::~gpu_hiz()
		{
			render_backend& rhi = render_backend_active();
			if (m_program) rhi.shader_compute_delete(m_program);
			if (m_params_ssbo) rhi.storage_delete(m_params_ssbo);
			if (m_depth_ssbo) rhi.storage_delete(m_depth_ssbo);
			m_program = m_params_ssbo = m_depth_ssbo = 0;
		}

		bool gpu_hiz::build(uint w, uint h, std::string& error)
		{
			if (!w || !h) { error = "illegal hiz size"; return false; }
			render_backend& rhi = render_backend_active();
			bool vulkan = (rhi.name()[0] == 'v');

			m_program = rhi.shader_compute_create(hiz_shader_source(vulkan), error);
			if (!m_program) return false;

			// 计算层级尺寸与偏移（float 单位）
			m_w = w; m_h = h;
			m_offsets.clear();
			m_level_w.clear();
			m_level_h.clear();
			uint off = 0;
			uint cw = w, ch = h;
			while (cw >= 1 && ch >= 1)
			{
				m_offsets.push_back(off);
				m_level_w.push_back(cw);
				m_level_h.push_back(ch);
				off += cw * ch;
				if (cw == 1 && ch == 1) break;
				cw = (cw + 1) / 2;
				ch = (ch + 1) / 2;
			}

			// params SSBO：6 uint（24 字节）
			std::vector<std::uint32_t> params(6, 0);
			m_params_ssbo = rhi.storage_create(params.size() * 4, params.data(), error);
			if (!m_params_ssbo) return false;

			// depth SSBO：全部层级（float），初始全 0
			std::vector<std::uint32_t> ddata(off, 0);
			m_depth_ssbo = rhi.storage_create(ddata.size() * 4, ddata.data(), error);
			if (!m_depth_ssbo) return false;

			return true;
		}

		void gpu_hiz::upload_level0(const std::vector<value>& depth)
		{
			if (!m_depth_ssbo || depth.size() != (size_t)m_w * m_h) return;
			std::vector<std::uint32_t> d(depth.size(), 0);
			for (size_t i = 0; i < depth.size(); ++i) d[i] = float_bits(depth[i]);
			render_backend_active().storage_update(m_depth_ssbo, d.data(), d.size() * 4);
		}

		void gpu_hiz::build_pyramid()
		{
			if (!m_program || m_level_w.empty()) return;
			render_backend& rhi = render_backend_active();
			for (size_t i = 0; i + 1 < m_level_w.size(); ++i)
			{
				std::vector<std::uint32_t> params(6, 0);
				params[0] = m_level_w[i];
				params[1] = m_level_h[i];
				params[2] = m_level_w[i + 1];
				params[3] = m_level_h[i + 1];
				params[4] = m_offsets[i];
				params[5] = m_offsets[i + 1];
				rhi.storage_update(m_params_ssbo, params.data(), params.size() * 4);
				rhi.compute_storage_bind(m_program, 1, m_params_ssbo);
				rhi.compute_storage_bind(m_program, 2, m_depth_ssbo);
				uint gx = (m_level_w[i + 1] + 7) / 8;
				uint gy = (m_level_h[i + 1] + 7) / 8;
				rhi.compute_dispatch(m_program, gx, gy, 1);
			}
		}

		uint gpu_hiz::level_count() const { return (uint)m_offsets.size(); }
		uint gpu_hiz::depth_ssbo() const { return m_depth_ssbo; }
		uint gpu_hiz::width() const { return m_w; }
		uint gpu_hiz::height() const { return m_h; }

		std::vector<value> gpu_hiz::read_level(uint level)
		{
			std::vector<value> out;
			if (!m_depth_ssbo || level >= m_offsets.size()) return out;
			uint total = m_offsets.back() + m_level_w.back() * m_level_h.back();
			std::vector<value> all(total);
			render_backend_active().storage_read(m_depth_ssbo, all.data(), total * sizeof(value));
			uint off = m_offsets[level];
			uint n = m_level_w[level] * m_level_h[level];
			out.assign(all.begin() + off, all.begin() + off + n);
			return out;
		}
	}
}
