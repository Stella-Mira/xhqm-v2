#pragma once
#ifndef XHQM_FANTASY_OCCLUSION
#define XHQM_FANTASY_OCCLUSION

// 遮挡剔除（GPU 两阶段）：对每个候选对象渲染其世界空间包围盒 + 遮挡查询，
// 用上一帧读回的样本数裁剪本帧渲染集。查询结果跨帧复用（一帧延迟）。
#include "fantasy.h"
#include "blurry.h"
#include "drawer.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		class occlusion_culler
		{
			struct slot;
			render_backend* m_rhi = nullptr;
			uint m_program = 0;                 // 深度专用着色器（颜色写关，只写深度）
			std::vector<slot*> m_slots;         // 候选对象槽（跨帧持久）
			bool m_ready = false;               // setup 是否完成
			bool m_has_frame = false;           // 是否记录过至少一帧查询
			uint m_threshold = 0;               // 可见阈值（样本数 > threshold）

			slot* slot_of(object* obj);                       // 找/建槽
			void rebuild_box(slot& s, const aabb& box);       // 重建世界空间包围盒
		public:
			occlusion_culler() = default;
			~occlusion_culler();

			// 创建深度专用着色器（需在渲染上下文就绪后调用一次）
			bool setup(render_backend& rhi, std::string& error);

			// 清空所有槽与查询（场景重建时调用）
			void reset();

			// 单帧入口：读上一帧查询结果 → 记录本帧查询 → 返回本帧可见对象。
			// clip_from_world = proj * view * camera（世界 → 裁剪），与渲染循环一致。
			// candidates = 视锥剔除后的 (世界 AABB, object) 列表。
			// 首帧无上一帧结果，返回全部候选。
			std::vector<object*> cull_and_record(const matrix& clip_from_world,
				const std::vector<std::pair<aabb, object*>>& candidates);

			size_t slot_count() const { return m_slots.size(); }
			void threshold(uint value) { m_threshold = value; }
		};
	}
}

#endif
