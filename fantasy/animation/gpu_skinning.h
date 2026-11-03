#pragma once
#ifndef XHQM_FANTASY_GPU_SKINNING
#define XHQM_FANTASY_GPU_SKINNING

// GPU 蒙皮（M9）：compute 着色器由每根骨骼的「局部变换 + 父骨骼索引 + 逆绑定矩阵」，
// 在 GPU 上按祖先链累乘算出世界矩阵，再乘逆绑定得蒙皮调色板 palette[i] = world[i] * inv_bind[i]，
// 写入 SSBO（与 gpu_culler 同构，无 CPU 读回即写即用；read_palette 仅供验证/调试读回）。
// 约定：局部/逆绑定矩阵按「转置」上传（与 drawer.cpp 上传 UBO bones 一致），
// 使 GLSL 列主序 mat4 乘与引擎行主序 matrix 乘语义一致。
#include "fantasy.h"
#include "backend.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		// 单根骨骼的 GPU 蒙皮输入（local 与 inv_bind 为引擎 matrix，行主序）
		struct gpu_bone
		{
			matrix local;      // 局部变换（相对父骨骼）
			int parent = -1;   // 父骨骼索引，-1 = 根
			matrix inv_bind;   // 逆绑定矩阵（蒙皮 offset）
		};

		class xhqm_io gpu_skinner
		{
			uint m_program = 0;
			uint m_params_ssbo = 0;   // binding 1：boneCount
			uint m_bones_ssbo = 0;    // binding 2：Bone 数组（只读）
			uint m_output_ssbo = 0;   // binding 3：mat4 palette 数组
			uint m_bone_count = 0;
		public:
			gpu_skinner() = default;
			~gpu_skinner();

			// 上传骨骼列表并创建 compute 管线；失败返回 false 且 error 有原因
			bool build(const std::vector<gpu_bone>& bones, std::string& error);
			// 原地更新骨骼局部变换/父索引/逆绑定（骨骼数量不变）
			void update(const std::vector<gpu_bone>& bones);
			// 调度 compute 计算调色板（帧内走帧命令缓冲，帧外走 one_shot 同步提交）
			void compute();
			// 读回调色板（CPU 侧，验证/调试用；行主序 matrix，与 CPU 计算一致）
			std::vector<matrix> read_palette();
			uint palette_ssbo() const { return m_output_ssbo; }
			uint bone_count() const { return m_bone_count; }
		};
	}
}

#endif
