#include "frame_graph.h"
#include <queue>

namespace xhqm
{
	namespace fantasy
	{
		frame_graph::~frame_graph()
		{
			reset();
		}

		void frame_graph::add_resource(const std::string& name, uint w, uint h, uint color_count)
		{
			if (m_res_index.count(name)) return;
			resource r;
			r.name = name;
			r.w = w; r.h = h; r.color_count = color_count;
			m_res_index[name] = m_resources.size();
			m_resources.push_back(std::move(r));
		}

		void frame_graph::add_pass(const std::string& name, std::vector<std::string> reads, std::vector<std::string> writes, std::function<void()> callback)
		{
			pass p;
			p.name = name;
			p.reads = std::move(reads);
			p.writes = std::move(writes);
			p.callback = std::move(callback);
			m_passes.push_back(std::move(p));
			m_compiled = false;
		}

		bool frame_graph::compile(std::string& error)
		{
			m_order.clear();
			size_t n = m_passes.size();
			if (n == 0) { m_compiled = true; return true; }

			// 资源生产者：唯一写该资源的 pass 下标
			std::vector<int> producer(m_resources.size(), -1);
			for (size_t i = 0; i < n; ++i)
			{
				for (auto& wname : m_passes[i].writes)
				{
					auto it = m_res_index.find(wname);
					if (it == m_res_index.end()) { error = "pass '" + m_passes[i].name + "' writes unknown resource '" + wname + "'"; return false; }
					if (producer[it->second] >= 0) { error = "resource '" + wname + "' written by multiple passes"; return false; }
					producer[it->second] = (int)i;
				}
			}

			// 依赖图：reader ← producer
			std::vector<std::vector<size_t>> dependents(n);   // pass i 的后继
			std::vector<int> in_degree(n, 0);
			for (size_t i = 0; i < n; ++i)
			{
				std::vector<int> deps;
				for (auto& rname : m_passes[i].reads)
				{
					auto it = m_res_index.find(rname);
					if (it == m_res_index.end()) { error = "pass '" + m_passes[i].name + "' reads unknown resource '" + rname + "'"; return false; }
					int p = producer[it->second];
					if (p < 0) { error = "resource '" + rname + "' is read but never written"; return false; }
					if (p == (int)i) { error = "pass '" + m_passes[i].name + "' reads its own write '" + rname + "'"; return false; }
					bool dup = false;
					for (int d : deps) if (d == p) { dup = true; break; }
					if (!dup) deps.push_back(p);
				}
				for (int p : deps)
				{
					dependents[p].push_back(i);
					in_degree[i]++;
				}
			}

			// Kahn 拓扑排序
			std::queue<size_t> q;
			for (size_t i = 0; i < n; ++i) if (in_degree[i] == 0) q.push(i);
			while (!q.empty())
			{
				size_t cur = q.front(); q.pop();
				m_order.push_back(cur);
				for (size_t next : dependents[cur])
				{
					if (--in_degree[next] == 0) q.push(next);
				}
			}
			if (m_order.size() != n) { error = "cycle detected in frame graph"; m_order.clear(); return false; }

			// 资源生命周期：first_use / last_use（按排序后序号）
			for (auto& r : m_resources) { r.first_use = -1; r.last_use = -1; }
			for (size_t k = 0; k < m_order.size(); ++k)
			{
				const pass& p = m_passes[m_order[k]];
				auto touch = [&](const std::string& rn)
				{
					auto it = m_res_index.find(rn);
					if (it == m_res_index.end()) return;
					resource& r = m_resources[it->second];
					if (r.first_use < 0) r.first_use = (int)k;
					r.last_use = (int)k;
				};
				for (auto& rn : p.reads) touch(rn);
				for (auto& rn : p.writes) touch(rn);
			}

			m_compiled = true;
			return true;
		}

		void frame_graph::execute()
		{
			if (!m_compiled || !m_rhi) return;
			size_t n = m_order.size();
			for (size_t k = 0; k < n; ++k)
			{
				// 首次触碰：分配离屏
				for (auto& r : m_resources)
				{
					if (r.first_use == (int)k)
					{
						std::string err;
						r.handle = m_rhi->create_offscreen(r.w, r.h, r.color_count, err);
					}
				}
				// 执行 pass
				pass& p = m_passes[m_order[k]];
				if (p.callback) p.callback();
				// 最后触碰：释放
				for (auto& r : m_resources)
				{
					if (r.last_use == (int)k && r.handle)
					{
						m_rhi->delete_offscreen(r.handle);
						r.handle = 0;
					}
				}
			}
		}

		uint frame_graph::resource_handle(const std::string& name) const
		{
			auto it = m_res_index.find(name);
			if (it == m_res_index.end()) return 0;
			return m_resources[it->second].handle;
		}

		void frame_graph::reset()
		{
			if (m_rhi)
			{
				for (auto& r : m_resources) { if (r.handle) { m_rhi->delete_offscreen(r.handle); r.handle = 0; } }
			}
			m_resources.clear();
			m_passes.clear();
			m_res_index.clear();
			m_order.clear();
			m_compiled = false;
		}
	}
}
