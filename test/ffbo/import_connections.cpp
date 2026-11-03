// FlyWire 连接组导入器：导入层 mesh<uint>（精确验证）→ 转 mesh<float>（计算）
// 用法：import_connections <neurons.csv> <connections.csv>
//
// 分层：
//   导入层 sparse_mesh<uint64, uint32>：add_edge + index() 合并重复边（uint 精确累加）
//   验证：节点数 / 边数 / sum(syn) / 度数 / 连通性
//   计算层 sparse_mesh<uint64, float>：验证通过后转 float，供 SpMV / 归一化等后续计算
#include "../../lattice/sparse_mesh.hpp"
#include "../../software/system/threads.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>

static bool read_neurons(xhqm::sparse_mesh<std::uint64_t, std::uint32_t>& g,
	std::unordered_map<std::uint64_t, xhqm::size>& id2idx, const char* path)
{
	std::ifstream in(path);
	if (!in.is_open()) { std::fprintf(stderr, "cannot open %s\n", path); return false; }
	std::string line;
	std::getline(in, line);          // header: root_id,group,nt_type,...
	while (std::getline(in, line))
	{
		const std::size_t p = line.find(',');
		if (p == std::string::npos) continue;
		const std::uint64_t rid = std::stoull(line.substr(0, p));
		if (id2idx.find(rid) == id2idx.end())
			id2idx.emplace(rid, g.add_node(rid));
	}
	return true;
}

static bool read_connections(xhqm::sparse_mesh<std::uint64_t, std::uint32_t>& g,
	const std::unordered_map<std::uint64_t, xhqm::size>& id2idx, const char* path, xhqm::size& rows)
{
	std::ifstream in(path);
	if (!in.is_open()) { std::fprintf(stderr, "cannot open %s\n", path); return false; }
	std::string line;
	std::getline(in, line);          // header: pre_root_id,post_root_id,neuropil,syn_count,nt_type
	while (std::getline(in, line))
	{
		const std::size_t p1 = line.find(',');
		const std::size_t p2 = line.find(',', p1 + 1);
		const std::size_t p3 = line.find(',', p2 + 1);
		const std::size_t p4 = line.find(',', p3 + 1);
		if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos || p4 == std::string::npos)
			continue;
		const std::uint64_t pre = std::stoull(line.substr(0, p1));
		const std::uint64_t post = std::stoull(line.substr(p1 + 1, p2 - p1 - 1));
		const std::uint32_t syn = static_cast<std::uint32_t>(std::stoul(line.substr(p3 + 1, p4 - p3 - 1)));

		auto it = id2idx.find(pre);
		auto jt = id2idx.find(post);
		if (it == id2idx.end() || jt == id2idx.end()) continue;   // 数据不一致：跳过
		g.add_edge(it->second, jt->second, syn);
		++rows;
	}
	return true;
}

// 通用常驻并行池：原子代次 + 自旋等待，避免 threads 池 submit 的 future/promise 分配
class par_pool
{
	unsigned m_n;
	std::vector<std::thread> m_workers;
	std::atomic<int> m_gen{ 0 };
	std::atomic<int> m_done{ 0 };
	std::atomic<bool> m_stop{ false };

	std::function<void(xhqm::size, xhqm::size)> m_body;
	std::vector<xhqm::size> m_begin, m_end;

public:
	explicit par_pool(unsigned n) : m_n(n), m_begin(n), m_end(n)
	{
		m_workers.reserve(n);
		for (unsigned t = 0; t < n; ++t)
			m_workers.emplace_back([this, t]()
			{
				int last = 0;
				while (true)
				{
					while (m_gen.load(std::memory_order_acquire) == last
						&& !m_stop.load(std::memory_order_acquire))
						std::this_thread::yield();
					if (m_stop.load(std::memory_order_acquire)) return;
					last = m_gen.load(std::memory_order_acquire);
					m_body(m_begin[t], m_end[t]);
					m_done.fetch_add(1, std::memory_order_release);
				}
			});
	}

	~par_pool()
	{
		m_stop.store(true, std::memory_order_release);
		m_gen.fetch_add(1, std::memory_order_release);
		for (auto& t : m_workers) t.join();
	}

	// 把 [0, total) 均分成 m_n 块，worker 并行执行 body(begin, end)
	void run(xhqm::size total, std::function<void(xhqm::size, xhqm::size)> body)
	{
		m_body = std::move(body);
		const xhqm::size blk = (total + m_n - 1) / m_n;
		for (unsigned t = 0; t < m_n; ++t)
		{
			m_begin[t] = t * blk;
			m_end[t] = (m_begin[t] + blk < total) ? (m_begin[t] + blk) : total;
		}
		m_done.store(0, std::memory_order_release);
		m_gen.fetch_add(1, std::memory_order_release);   // 发布任务
		while (m_done.load(std::memory_order_acquire) < static_cast<int>(m_n))
			std::this_thread::yield();                   // 等全部完成
	}
};

int main(int argc, char** argv)
{
	const char* neurons_path = (argc > 1) ? argv[1] : "neurons.csv";
	const char* conn_path = (argc > 2) ? argv[2] : "connections.csv";

	// —— 1. 导入层：mesh<uint> ——
	xhqm::sparse_mesh<std::uint64_t, std::uint32_t> g_u;
	std::unordered_map<std::uint64_t, xhqm::size> id2idx;
	id2idx.reserve(140000);

	auto t0 = std::chrono::steady_clock::now();
	if (!read_neurons(g_u, id2idx, neurons_path)) return 1;
	const xhqm::size n_neurons = g_u.size();
	auto t1 = std::chrono::steady_clock::now();

	xhqm::size rows = 0;
	if (!read_connections(g_u, id2idx, conn_path, rows)) return 1;
	auto t2 = std::chrono::steady_clock::now();

	g_u.rebuild_index();             // 合并重复边（uint 精确累加）+ 建 CSR
	auto t3 = std::chrono::steady_clock::now();

	// —— 2. 验证导入层（uint 精确）——
	std::uint64_t syn_total = 0;
	xhqm::size max_out = 0, max_in = 0, isolated = 0;
	std::uint64_t deg_out_sum = 0;
	g_u.for_each_edge([&](xhqm::size, xhqm::size, std::uint32_t v) { syn_total += v; });
	for (xhqm::size i = 0; i < g_u.size(); ++i)
	{
		const xhqm::size od = g_u.out_degree(i);
		const xhqm::size idg = g_u.in_degree(i);
		if (od > max_out) max_out = od;
		if (idg > max_in) max_in = idg;
		if (od == 0 && idg == 0) ++isolated;
		deg_out_sum += od;
	}

	std::printf("=== import (mesh<uint>) ===\n");
	std::printf("neurons(csv)   = %zu\n", n_neurons);
	std::printf("nodes(total)   = %zu\n", g_u.size());
	std::printf("isolated       = %zu\n", isolated);
	std::printf("raw conn rows  = %zu\n", rows);
	std::printf("nnz(merged)    = %zu\n", g_u.nnz());
	std::printf("sum syn        = %llu\n", (unsigned long long)syn_total);
	std::printf("max out/in     = %zu / %zu\n", max_out, max_in);
	std::printf("avg out deg    = %.4f\n", (double)deg_out_sum / (double)g_u.size());
	std::printf("load neurons   = %.3f s\n", std::chrono::duration<double>(t1 - t0).count());
	std::printf("load conn      = %.3f s\n", std::chrono::duration<double>(t2 - t1).count());
	std::printf("index          = %.3f s\n", std::chrono::duration<double>(t3 - t2).count());

	if (g_u.size() > 0)
	{
		xhqm::size hub = 0;
		for (xhqm::size i = 0; i < g_u.size(); ++i)
			if (g_u.out_degree(i) > g_u.out_degree(hub)) hub = i;
		auto comp = g_u.reachable_component(hub);
		std::printf("hub %zu (out=%zu) reachable = %zu\n", hub, g_u.out_degree(hub), comp.size());
	}

	// —— 3. 正式转换：mesh<uint> → mesh<float> ——
	xhqm::sparse_mesh<std::uint64_t, float> g_f;
	for (xhqm::size i = 0; i < g_u.size(); ++i)
		g_f.add_node(g_u.node(i));
	g_u.for_each_edge([&](xhqm::size i, xhqm::size j, std::uint32_t v) {
		g_f.add_edge(i, j, static_cast<float>(v));
	});
	g_f.rebuild_index();             // 无重复边，只建 CSR
	auto t4 = std::chrono::steady_clock::now();

	std::printf("=== compute (mesh<float>) ===\n");
	std::printf("convert nodes/edges = %zu / %zu\n", g_f.size(), g_f.nnz());
	std::printf("convert time = %.3f s\n", std::chrono::duration<double>(t4 - t3).count());

	// —— 4. SpMV 测试（float 层，显式走 GPU 后端）——
	{
		// 显式选 GPU 后端（本机 RTX 3090 Ti + CUDA 12.6）
		xhqm::tensor_base::tensor_backend_select(xhqm::hardware::device::gpu);
		auto& be = xhqm::tensor_base::tensor_backend_active();
		std::printf("tensor backend = %s\n", be.name());

		auto A = g_f.to_sparse_matrix();
		xhqm::vector<float> x(g_f.size(), 1.0f);
		const auto t5 = std::chrono::steady_clock::now();
		xhqm::vector<float> y = A * x;
		const auto t6 = std::chrono::steady_clock::now();
		double ysum = 0;
		for (xhqm::size i = 0; i < y.size(); ++i) ysum += y[i];
		std::printf("SpMV y=A*1   sum(y)=%.0f (expect %llu)  %s  time=%.3f s\n",
			ysum, (unsigned long long)syn_total,
			(static_cast<std::uint64_t>(ysum + 0.5) == syn_total ? "OK" : "MISMATCH"),
			std::chrono::duration<double>(t6 - t5).count());
	}

	// —— 5. CSR SpMV（应用层，用 row_ptr/col/val 顺序访问，cache 友好）——
	{
		const auto& rp = g_f.row_ptr();
		const auto& cc = g_f.col();
		const auto& vv = g_f.val();
		std::vector<float> x2(g_f.size(), 1.0f);

		// 串行
		std::vector<float> y2(g_f.size(), 0.0f);
		const auto t7 = std::chrono::steady_clock::now();
		for (xhqm::size i = 0; i < g_f.size(); ++i)
		{
			float acc = 0.0f;
			for (xhqm::size k = rp[i]; k < rp[i + 1]; ++k)
				acc += vv[k] * x2[cc[k]];
			y2[i] = acc;
		}
		const auto t8 = std::chrono::steady_clock::now();
		double ysum2 = 0;
		for (xhqm::size i = 0; i < y2.size(); ++i) ysum2 += y2[i];
		std::printf("CSR SpMV(串行)   sum(y)=%.0f (expect %llu)  %s  time=%.3f s\n",
			ysum2, (unsigned long long)syn_total,
			(static_cast<std::uint64_t>(ysum2 + 0.5) == syn_total ? "OK" : "MISMATCH"),
			std::chrono::duration<double>(t8 - t7).count());

		// 并行（xhqm::parallel::threads 线程池，提前创建 worker 复用，避免 per-call 线程创建）
		const unsigned nthreads = std::thread::hardware_concurrency();
		xhqm::parallel::threads pool(1, nthreads > 0 ? nthreads : 4);
		pool.life();
		std::vector<float> y3(g_f.size(), 0.0f);
		const auto t9 = std::chrono::steady_clock::now();
		const xhqm::size blk = (g_f.size() + nthreads - 1) / nthreads;
		std::vector<xhqm::parallel::holder<void>> tasks;
		tasks.reserve(nthreads);
		for (unsigned t = 0; t < nthreads; ++t)
		{
			const xhqm::size b = t * blk;
			const xhqm::size e = (b + blk < g_f.size()) ? (b + blk) : g_f.size();
			if (b >= e) break;
			tasks.push_back(pool.submit(0, [&rp, &cc, &vv, &x2, &y3](xhqm::size b, xhqm::size e)
			{
				for (xhqm::size i = b; i < e; ++i)
				{
					float acc = 0.0f;
					for (xhqm::size k = rp[i]; k < rp[i + 1]; ++k)
						acc += vv[k] * x2[cc[k]];
					y3[i] = acc;
				}
			}, b, e));
		}
		for (auto& h : tasks) h.future_ptr()->wait();
		const auto t10 = std::chrono::steady_clock::now();
		double ysum3 = 0;
		for (xhqm::size i = 0; i < y3.size(); ++i) ysum3 += y3[i];
		std::printf("CSR SpMV(线程池) sum(y)=%.0f (expect %llu)  %s  time=%.3f s\n",
			ysum3, (unsigned long long)syn_total,
			(static_cast<std::uint64_t>(ysum3 + 0.5) == syn_total ? "OK" : "MISMATCH"),
			std::chrono::duration<double>(t10 - t9).count());
	}

	// —— 6. 常驻 worker 池 SpMV（原子代次同步，无 future/promise）——
	{
		const auto& rp = g_f.row_ptr();
		const auto& cc = g_f.col();
		const auto& vv = g_f.val();
		std::vector<float> x3(g_f.size(), 1.0f);
		std::vector<float> y4(g_f.size(), 0.0f);

		const unsigned nthreads = std::thread::hardware_concurrency();
		par_pool pool(nthreads > 0 ? nthreads : 4);
		auto spmv_body = [&](xhqm::size b, xhqm::size e)
		{
			for (xhqm::size i = b; i < e; ++i)
			{
				float acc = 0.0f;
				for (xhqm::size k = rp[i]; k < rp[i + 1]; ++k)
					acc += vv[k] * x3[cc[k]];
				y4[i] = acc;
			}
		};
		pool.run(g_f.size(), spmv_body);   // 预热：让 worker 进入等待态

		const auto t11 = std::chrono::steady_clock::now();
		pool.run(g_f.size(), spmv_body);
		const auto t12 = std::chrono::steady_clock::now();
		double ysum4 = 0;
		for (xhqm::size i = 0; i < y4.size(); ++i) ysum4 += y4[i];
		std::printf("CSR SpMV(常驻池) sum(y)=%.0f (expect %llu)  %s  time=%.3f s\n",
			ysum4, (unsigned long long)syn_total,
			(static_cast<std::uint64_t>(ysum4 + 0.5) == syn_total ? "OK" : "MISMATCH"),
			std::chrono::duration<double>(t12 - t11).count());
	}

	// —— 7. LIF 神经元步进（合并 SpMV + 更新，单次并行调度；背景输入保证活动）——
	{
		const auto& rp = g_f.row_ptr();
		const auto& cc = g_f.col();
		const auto& vv = g_f.val();
		const unsigned nthreads = std::thread::hardware_concurrency();
		par_pool pool(nthreads > 0 ? nthreads : 4);

		const float dt = 0.001f, tau = 0.010f, v_th = 1.0f;
		const float scale = 0.001f;    // 突触权重缩放
		const float I_bg = 200.0f;     // 背景输入（保证发放活动）
		const xhqm::size n = g_f.size();

		std::vector<float> v(n, 0.0f);   // 膜电位
		std::vector<float> s(n, 0.0f);   // 发放（0/1）
		std::atomic<xhqm::size> total_spikes{ 0 };

		for (xhqm::size i = 0; i < n && i < 1000; ++i) s[i] = 1.0f;   // 初始刺激

		const int steps = 200;
		auto t13 = std::chrono::steady_clock::now();
		for (int step = 0; step < steps; ++step)
		{
			pool.run(n, [&](xhqm::size b, xhqm::size e)
			{
				for (xhqm::size i = b; i < e; ++i)
				{
					float I = 0.0f;
					for (xhqm::size k = rp[i]; k < rp[i + 1]; ++k)
						I += vv[k] * s[cc[k]];            // 突触传播（SpMV）
					v[i] += dt * ((0.0f - v[i]) / tau + I * scale + I_bg);
					if (v[i] >= v_th)
					{
						s[i] = 1.0f;
						v[i] = 0.0f;
						total_spikes.fetch_add(1, std::memory_order_relaxed);
					}
					else
						s[i] = 0.0f;
				}
			});
		}
		auto t14 = std::chrono::steady_clock::now();
		const double avg_ms = std::chrono::duration<double>(t14 - t13).count() * 1000.0 / steps;
		std::printf("LIF %d steps  avg=%.3f ms/step  (%.1f Hz)  total_spikes=%zu\n",
			steps, avg_ms, 1000.0 / avg_ms, total_spikes.load());
	}
	return 0;
}
