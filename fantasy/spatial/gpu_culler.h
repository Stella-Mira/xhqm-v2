#pragma once
#ifndef XHQM_FANTASY_GPU_CULLER
#define XHQM_FANTASY_GPU_CULLER

// GPU 视锥剔除（M3）：compute 读对象 AABB，写 DrawIndexedIndirectCommand 到 SSBO，
// 随后 draw_indexed_indirect_storage 直接消费，全程无 CPU 读回、无同步。
// 当前为非压缩版：每对象一条命令，被剔除者 indexCount=0（GPU 零开销跳过）。
#include "fantasy.h"
#include "backend.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		struct gpu_cull_object
		{
			value center[3];      // 世界空间 AABB 中心
			value half_extent[3]; // 半边长（正值）
			uint cmd[5];          // DrawIndexedIndirectCommand：indexCount/instanceCount/firstIndex/vertexOffset/baseInstance
		};

		class xhqm_io gpu_culler
		{
			uint m_program = 0;
			uint m_params_ssbo = 0;   // binding 1：objectCount + 6 视锥平面
			uint m_objects_ssbo = 0;  // binding 2：Object 数组
			uint m_output_ssbo = 0;   // binding 3：DrawCommand 数组（非压缩）
			uint m_object_count = 0;
		public:
			gpu_culler() = default;
			~gpu_culler();

			// 上传对象列表并创建 compute 管线；失败返回 false 且 error 有原因
			bool build(const std::vector<gpu_cull_object>& objects, std::string& error);
			// 上传世界空间视锥平面（约定同 spatial.cpp 的 extract_planes）并 dispatch
			void cull(const value planes[6][4]);
			// 用剔除结果绘制（indexed），indirect 命令源为输出 SSBO
			void draw_indexed(uint vertex, uint index);
			uint object_count() const { return m_object_count; }
		};

		// 从 view_proj（列向量约定，row3±rowN）提取 6 个视锥平面，供 gpu_culler::cull 使用
		xhqm_io void extract_frustum_planes(const matrix& view_proj, value planes[6][4]);
	}
}

#endif
