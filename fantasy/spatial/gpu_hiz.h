#pragma once
#ifndef XHQM_FANTASY_GPU_HIZ
#define XHQM_FANTASY_GPU_HIZ

// Hi-Z 深度金字塔（M7）：compute 着色器逐级把 2x2 深度块下采样为「最远深度」（max），
// 全部层级连续存于单一 SSBO。为 GPU 两阶段遮挡剔除提供保守遮挡判断：
// 深度约定 0=近 1=远，块内取 max（最远）——对象深度 > 块 max 则必被遮挡。
#include "fantasy.h"
#include "backend.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		class xhqm_io gpu_hiz
		{
			uint m_program = 0;
			uint m_params_ssbo = 0;   // binding 1：srcW/srcH/dstW/dstH/srcOffset/dstOffset
			uint m_depth_ssbo = 0;    // binding 2：全部层级连续 depth[]
			uint m_w = 0, m_h = 0;
			std::vector<uint> m_offsets;   // 每层起始（float 单位）
			std::vector<uint> m_level_w;   // 每层宽
			std::vector<uint> m_level_h;   // 每层高
		public:
			gpu_hiz() = default;
			~gpu_hiz();

			// 按 level0 尺寸 w×h 分配金字塔 + 创建 compute 管线；失败返回 false
			bool build(uint w, uint h, std::string& error);
			// 上传 level 0 深度（size == w*h）
			void upload_level0(const std::vector<value>& depth);
			// 逐级 dispatch 构建完整金字塔
			void build_pyramid();
			// 读回某一层（验证/调试用；level 0 = 原始，level k = 2^k 下采样）
			std::vector<value> read_level(uint level);
			uint level_count() const;
			uint depth_ssbo() const;
			uint width() const;
			uint height() const;
		};
	}
}

#endif
