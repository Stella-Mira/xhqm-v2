#include "theory_pass.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		theory_pass::theory_pass(std::string name)
			: m_pass_name(std::move(name))
		{
		}
		theory_pass::~theory_pass()
		{
		}

		void theory_pass::life()
		{
			if (!used()) return;          // 停用不标记，交给 fg 下帧 diff 掉
			if (!m_fg) return;
			if (!m_fg->owns(this)) m_fg->register_pass(this);
			m_fg->mark_use(this);
		}

		theory_frame_graph::theory_frame_graph()
		{
		}
		theory_frame_graph::~theory_frame_graph()
		{
		}

		bool theory_frame_graph::owns(const theory_pass* pass_ptr) const
		{
			for (const theory_pass* p : m_registry)
				if (p == pass_ptr) return true;
			return false;
		}

		void theory_frame_graph::register_pass(theory_pass* pass_ptr)
		{
			if (!pass_ptr || owns(pass_ptr)) return;
			m_registry.push_back(pass_ptr);
			m_use.push_back(false);
			m_dirty = true;
		}

		void theory_frame_graph::mark_use(const theory_pass* pass_ptr)
		{
			for (size_t i = 0; i < m_registry.size(); ++i)
				if (m_registry[i] == pass_ptr) { m_use[i] = true; return; }
		}

		void theory_frame_graph::life()
		{
			m_graph.set_backend(render_backend_active());

			// 1. 检测 use=false 的 pass（对应 theory 本帧未标 use = 已从树移除或 used(false) 停用）
			bool has_unused = false;
			for (bool u : m_use)
				if (!u) { has_unused = true; break; }

			if (m_dirty || has_unused)
			{
				// 2. 从注册表剔除未使用项，同步 use 表
				std::vector<theory_pass*> alive;
				std::vector<bool> alive_use;
				alive.reserve(m_registry.size());
				alive_use.reserve(m_registry.size());
				for (size_t i = 0; i < m_registry.size(); ++i)
					if (m_use[i]) { alive.push_back(m_registry[i]); alive_use.push_back(true); }
				m_registry.swap(alive);
				m_use.swap(alive_use);

				// 3. 重建：reset + 重新 declare + compile（同一帧内完成，无空帧）
				m_graph.reset();
				for (theory_pass* p : m_registry)
					p->declare(m_graph);
				m_error.clear();
				m_graph.compile(m_error);
				m_dirty = false;
			}

			// 4. 执行（按拓扑/挂载序跑所有 pass callback）
			m_graph.execute();

			// 5. 全部 use 清零，为下一帧重新标记准备
			m_use.assign(m_registry.size(), false);
		}
	}
}
