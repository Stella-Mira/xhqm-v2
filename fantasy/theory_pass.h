#pragma once
#ifndef XHQM_FANTASY_THEORY_PASS
#define XHQM_FANTASY_THEORY_PASS

// theory_pass：可编排渲染 pass 的抽象基类。
// 每个渲染功能（阴影 / 主渲染 / 后处理）派生为独立 theory，平级挂到 world 的 theory 子树，
// 由 theory_frame_graph 统一注册、diff 增删、重建编译、按拓扑/挂载序执行。
//
// 每帧时序（theory_frame_graph 挂载在 theory 子树最后）：
//   各 theory_pass::life()  → 若未注册则 register_pass（触发 dirty），并 mark_use
//   theory_frame_graph::life() → 有 dirty 或 unused 则「reset + 重新 declare + compile」（同帧完成）
//                              → execute（跑所有 pass callback）
//                              → 全部 use 清零（下一帧重新标记）
#include "system.h"
#include "frame_graph.h"
#include <unordered_map>
#include <vector>

namespace xhqm
{
	namespace fantasy
	{
		class theory_frame_graph;

		class xhqm_io theory_pass : public theory
		{
			theory_frame_graph* m_fg = nullptr;
			std::string m_pass_name;
		public:
			explicit theory_pass(std::string name);
			virtual ~theory_pass();

			void fg(theory_frame_graph* value) { m_fg = value; }
			theory_frame_graph* fg(void) const { return m_fg; }
			const std::string& pass_name(void) const { return m_pass_name; }

			// 子类实现：向 frame_graph 声明资源与 pass（仅重建时调用，非每帧）
			virtual void declare(frame_graph& graph) = 0;
			// 子类实现：渲染（作为 pass 的 callback，每帧 execute 时调用）
			virtual void render() = 0;

			// 统一骨架：停用不标记；首次注册；每帧标 use
			virtual void life() override;
		};

		// 管线编排器：持有 frame_graph + theory_pass 注册表 + 跨 pass 命名纹理句柄表。
		// 挂载在 theory 子树最后（life 最后执行）。
		class xhqm_io theory_frame_graph : public theory
		{
			frame_graph m_graph;
			std::vector<theory_pass*> m_registry;
			std::vector<bool> m_use;
			bool m_dirty = true;
			std::unordered_map<std::string, uint> m_handles;
			std::string m_error;
		public:
			explicit theory_frame_graph();
			virtual ~theory_frame_graph();

			void set_backend(render_backend& rhi) { m_graph.set_backend(rhi); }

			bool owns(const theory_pass* pass_ptr) const;
			void register_pass(theory_pass* pass_ptr);
			void mark_use(const theory_pass* pass_ptr);
			void mark_dirty(void) { m_dirty = true; }

			frame_graph& graph(void) { return m_graph; }

			// 已注册 pass 列表（setup 时预注册；供挂树 / 手动驱动 / 释放）
			const std::vector<theory_pass*>& passes(void) const { return m_registry; }

			// 手动驱动整条管线（多相机/多窗口：每个窗口每帧调用一次）：
			// 先标记各 pass 本帧使用，再走统一的 diff/compile/execute 时序。
			// 与挂 world theory 树 + alive() 驱动互斥使用同一 fg（二选一）。
			void run()
			{
				for (theory_pass* p : m_registry) p->life();
				life();
			}

			// 跨 pass 命名纹理句柄表：后处理链「读 scene → 写回 scene」式传递
			void set_handle(const std::string& name, uint texture) { m_handles[name] = texture; }
			uint handle(const std::string& name) const
			{
				auto it = m_handles.find(name);
				return it == m_handles.end() ? 0 : it->second;
			}

			const std::string& error(void) const { return m_error; }

			virtual void life() override;
		};
	}
}

#endif
