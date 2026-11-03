#pragma once
#ifndef XHQM_FANTASY_GPU_HIZ_CULLER
#define XHQM_FANTASY_GPU_HIZ_CULLER

// GPU 两阶段遮挡剔除（M7）：compute 读对象 AABB，先视锥剔除（裁剪空间），再对可见对象
// 投影到屏幕、按 Hi-Z 深度金字塔做保守遮挡测试，写 DrawIndexedIndirectCommand（被剔除 indexCount=0）。
// 深度约定 0=近 1=远，Hi-Z 块内取 max（最远）——对象最近深度 > 块 max 则必被遮挡。
#include "fantasy.h"
#include "backend.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		struct gpu_hiz_cull_object
		{
			value center[3];      // 世界空间 AABB 中心
			value half_extent[3]; // 半边长（正值）
			uint cmd[5];          // DrawIndexedIndirectCommand
		};

		class xhqm_io gpu_hiz_culler
		{
			uint m_program = 0;
			uint m_params_ssbo = 0;   // binding 1：objectCount + VP + 屏幕尺寸 + hiz 元数据 + 各层偏移
			uint m_objects_ssbo = 0;  // binding 2：Object 数组
			uint m_output_ssbo = 0;   // binding 4：DrawCommand 数组
			uint m_object_count = 0;
		public:
			gpu_hiz_culler() = default;
			~gpu_hiz_culler();

			bool build(const std::vector<gpu_hiz_cull_object>& objects, std::string& error);
			// view_proj：列向量约定（引擎 matrix）；hiz_ssbo 为 gpu_hiz 的金字塔（全层级连续 float[]）
			void cull(const matrix& view_proj, uint screen_w, uint screen_h, uint hiz_ssbo, uint hiz_w, uint hiz_h, uint hiz_levels);
			void draw_indexed(uint vertex, uint index);
			// 读回每对象可见性（indexCount>0 = 可见；验证/调试用）
			std::vector<bool> read_visible();
			uint object_count() const { return m_object_count; }
		};
	}
}

#endif
