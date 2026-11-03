#include "gpu_skinning.h"
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

		static std::string skin_shader_source(bool vulkan)
		{
			const char* body = R"GLSL(
layout(local_size_x = 64) in;

layout(std430, binding = 1) buffer Params
{
    uint boneCount;
    uint _p0; uint _p1; uint _p2;
} params;

struct Bone
{
    mat4 local;          // 局部变换（相对父骨骼）
    int parent;          // 父骨骼索引，-1 = 根
    int pad0; int pad1; int pad2;
    mat4 invBind;        // 逆绑定矩阵
};
layout(std430, binding = 2) readonly buffer Bones { Bone bones[]; };
layout(std430, binding = 3) buffer Palette { mat4 palette[]; } outPalette;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= params.boneCount) return;
    // 输入按「转置」上传只是行主序→列主序的存储换算，GLSL mat4 即引擎矩阵本身，
    // 列主序乘与引擎乘语义一致：world[i] = local[root]*...*local[i]，palette[i]=world[i]*inv_bind[i]
    mat4 world = bones[i].local;
    int p = bones[i].parent;
    int guard = 0;
    while (p >= 0 && guard < 1024)
    {
        world = bones[p].local * world;
        p = bones[p].parent;
        guard++;
    }
    outPalette.palette[i] = world * bones[i].invBind;
}
)GLSL";
			std::string version = vulkan ? "#version 450\n" : "#version 430 core\n";
			return version + body;
		}

		gpu_skinner::~gpu_skinner()
		{
			render_backend& rhi = render_backend_active();
			if (m_program) rhi.shader_compute_delete(m_program);
			if (m_params_ssbo) rhi.storage_delete(m_params_ssbo);
			if (m_bones_ssbo) rhi.storage_delete(m_bones_ssbo);
			if (m_output_ssbo) rhi.storage_delete(m_output_ssbo);
			m_program = m_params_ssbo = m_bones_ssbo = m_output_ssbo = 0;
		}

		bool gpu_skinner::build(const std::vector<gpu_bone>& bones, std::string& error)
		{
			if (bones.empty()) { error = "empty bones"; return false; }
			render_backend& rhi = render_backend_active();
			bool vulkan = (rhi.name()[0] == 'v');

			m_program = rhi.shader_compute_create(skin_shader_source(vulkan), error);
			if (!m_program) return false;

			// params SSBO：boneCount（16 字节 = 4 word）
			std::vector<std::uint32_t> params(4, 0);
			params[0] = (std::uint32_t)bones.size();
			m_params_ssbo = rhi.storage_create(params.size() * 4, params.data(), error);
			if (!m_params_ssbo) return false;

			// bones SSBO：每骨骼 36 word（144 字节）= local(16) + parent(1) + pad(3) + invBind(16)
			std::vector<std::uint32_t> bdata(bones.size() * 36, 0);
			for (size_t i = 0; i < bones.size(); ++i)
			{
				std::uint32_t* o = bdata.data() + i * 36;
				matrix lt = bones[i].local.tramat();
				matrix ib = bones[i].inv_bind.tramat();
				for (int k = 0; k < 16; ++k) o[k] = float_bits(lt.data()[k]);
				o[16] = (std::uint32_t)bones[i].parent;
				o[17] = o[18] = o[19] = 0;
				for (int k = 0; k < 16; ++k) o[20 + k] = float_bits(ib.data()[k]);
			}
			m_bones_ssbo = rhi.storage_create(bdata.size() * 4, bdata.data(), error);
			if (!m_bones_ssbo) return false;

			// output SSBO：每骨骼 mat4（16 value = 64 字节），初始全 0
			std::vector<std::uint32_t> odata(bones.size() * 16, 0);
			m_output_ssbo = rhi.storage_create(odata.size() * 4, odata.data(), error);
			if (!m_output_ssbo) return false;

			m_bone_count = (uint)bones.size();
			return true;
		}

		void gpu_skinner::update(const std::vector<gpu_bone>& bones)
		{
			if (!m_bones_ssbo || bones.size() != m_bone_count) return;
			std::vector<std::uint32_t> bdata(bones.size() * 36, 0);
			for (size_t i = 0; i < bones.size(); ++i)
			{
				std::uint32_t* o = bdata.data() + i * 36;
				matrix lt = bones[i].local.tramat();
				matrix ib = bones[i].inv_bind.tramat();
				for (int k = 0; k < 16; ++k) o[k] = float_bits(lt.data()[k]);
				o[16] = (std::uint32_t)bones[i].parent;
				o[17] = o[18] = o[19] = 0;
				for (int k = 0; k < 16; ++k) o[20 + k] = float_bits(ib.data()[k]);
			}
			render_backend_active().storage_update(m_bones_ssbo, bdata.data(), bdata.size() * 4);
		}

		void gpu_skinner::compute()
		{
			if (!m_program || !m_bone_count) return;
			render_backend& rhi = render_backend_active();
			rhi.compute_storage_bind(m_program, 1, m_params_ssbo);
			rhi.compute_storage_bind(m_program, 2, m_bones_ssbo);
			rhi.compute_storage_bind(m_program, 3, m_output_ssbo);
			uint groups = (m_bone_count + 63) / 64;
			rhi.compute_dispatch(m_program, groups, 1, 1);
		}

		std::vector<matrix> gpu_skinner::read_palette()
		{
			std::vector<matrix> out;
			if (!m_output_ssbo || !m_bone_count) return out;
			std::vector<value> raw(m_bone_count * 16);
			render_backend_active().storage_read(m_output_ssbo, raw.data(), raw.size() * sizeof(value));
			out.reserve(m_bone_count);
			for (uint b = 0; b < m_bone_count; ++b)
			{
				matrix m(4, 4);
				// SSBO 为 GLSL 列主序：raw[c*4+r] = palette(r,c) → 引擎行主序 m(r,c)
				for (int r = 0; r < 4; ++r)
					for (int c = 0; c < 4; ++c)
						m(r, c) = raw[b * 16 + c * 4 + r];
				out.push_back(m);
			}
			return out;
		}
	}
}
