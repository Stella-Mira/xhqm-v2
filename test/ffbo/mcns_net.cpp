// MaleCNS v1.0 网络验证程序（只有神经网络，不含身体与渲染）
//
// 为什么单独一个程序：先确认最底层的两件事做对了，再往上接躯体。
//   1. 突触符号：MaleCNS 每条突触都带突触前的递质类型，GABA/谷氨酸/组胺为抑制性。
//      我们此前的实现是 I += w*s 且权重全正 —— 等于在跑一个没有任何抑制的连接组。
//      本程序提供 --abs-w 开关做 A/B 对照（|w| = 复现旧的全兴奋网络）。
//   2. LIF 工作点：稳态 v_ss = tau * I（欧拉离散），把 I_bg 解析地设成 m*V_th/tau，
//      使静息膜电位离阈值有明确裕度（旧的 88 对应 v_ss = 0.88，几乎贴死阈值）。
//
// 用法：mcns_net <malecns目录> [--sec 5] [--bg-m 0.45] [--syn-gain 60]
//                  [--abs-w] [--thr-het 0.15] [--inject 名称 幅度] [--quiet]

#include "../../head.h"
#include "cell_backend.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <functional>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using u64 = std::uint64_t;

// ══════════════════ 常驻并行池（原子代次 + 自旋）══════════════════
class par_pool
{
	unsigned m_n;
	std::vector<std::thread> m_workers;
	std::atomic<int> m_gen{ 0 }, m_done{ 0 };
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
		m_gen.fetch_add(1, std::memory_order_release);
		while (m_done.load(std::memory_order_acquire) < static_cast<int>(m_n))
			std::this_thread::yield();
	}
};

// ══════════════════ CSV 工具 ══════════════════
static std::string csv_field(const std::string& line, int k)
{
	std::string cur;
	int idx = 0;
	bool in_quote = false;
	for (std::size_t i = 0; i <= line.size(); ++i)
	{
		const char c = (i < line.size()) ? line[i] : ',';
		if (in_quote)
		{
			if (c == '"') { in_quote = false; continue; }
			cur += c;
			continue;
		}
		if (c == '"') { in_quote = true; continue; }
		if (c == ',' || i == line.size())
		{
			if (idx == k) return cur;
			++idx;
			cur.clear();
		}
		else cur += c;
	}
	return std::string();
}

static int field_index(const std::string& header, const char* name)
{
	std::size_t p = 0;
	int idx = 0;
	const std::string target = name;
	while (p <= header.size())
	{
		std::size_t q = header.find(',', p);
		if (q == std::string::npos) q = header.size();
		std::string cell = header.substr(p, q - p);
		if (cell.size() >= 2 && cell.front() == '"') cell = cell.substr(1, cell.size() - 2);
		if (cell == target) return idx;
		++idx;
		p = q + 1;
	}
	return -1;
}

// 只取整数列（零拷贝，避免每行构造 std::string）
static u64 csv_u64(const std::string& line, int k)
{
	int idx = 0;
	bool in_quote = false;
	std::size_t start = std::string::npos;
	for (std::size_t i = 0; i <= line.size(); ++i)
	{
		const char c = (i < line.size()) ? line[i] : ',';
		if (in_quote) { if (c == '"') in_quote = false; continue; }
		if (c == '"') { in_quote = true; continue; }
		if (c == ',' || i == line.size())
		{
			if (idx == k) return (start == std::string::npos) ? 0 : std::strtoull(line.c_str() + start, nullptr, 10);
			++idx;
			start = std::string::npos;
		}
		else if (start == std::string::npos) start = i;
	}
	return 0;
}


// ══════════════════ 网络 ══════════════════
// 转置 CSR：行按「突触前」聚合。只有发放的神经元才遍历自己的出边，
// 即稀疏×稀疏，而不是稠密矩阵乘稀疏矩阵 —— 性能关键。
struct netcsr
{
	std::vector<xhqm::size> rp, col;
	std::vector<float> val;

	void build(const std::vector<std::pair<xhqm::size, xhqm::size>>& e,
		const std::vector<float>& w, xhqm::size n)
	{
		rp.assign(n + 1, 0);
		for (std::size_t k = 0; k < e.size(); ++k) ++rp[e[k].first + 1];
		for (xhqm::size i = 1; i <= n; ++i) rp[i] += rp[i - 1];
		col.resize(e.size());
		val.resize(e.size());
		std::vector<xhqm::size> cur(rp.begin(), rp.end() - 1);
		for (std::size_t k = 0; k < e.size(); ++k)
		{
			const xhqm::size o = cur[e[k].first]++;
			col[o] = e[k].second;
			val[o] = w[k];
		}
	}
};

struct population
{
	std::vector<float> v, s, s_new, cur, th, tau;   // tau = 逐神经元膜时间常数
	netcsr* t = nullptr;          // 指向共享 CSR（蘑菇体可塑性需要从外部改其 val）
	xhqm::size n = 0;

	void init(xhqm::size count, std::mt19937& rng)
	{
		n = count;
		v.assign(n, 0.0f);
		s.assign(n, 0.0f);
		s_new.assign(n, 0.0f);
		cur.assign(n, 0.0f);
		tau.assign(n, 0.100f);
		// 阈值异质性：均匀驱动的确定性 LIF 会全网络同步放电（实测 139k/139k 同帧发放），
		// 给每个神经元一个不同的阈值才能形成发放率分布。
		th.resize(n);
		std::uniform_real_distribution<float> d(0.85f, 1.15f);
		for (xhqm::size i = 0; i < n; ++i) th[i] = d(rng);
	}
};static std::atomic<long long> g_br_fired{ 0 }, g_cd_fired{ 0 };

static void lif_step(population& P, par_pool& pool, float dt, float tau, std::atomic<long long>& fired)
{
	(void)tau;                       // 改用 P.tau[i]（逐神经元）
	fired.store(0, std::memory_order_relaxed);
	pool.run(P.n, [&](xhqm::size b, xhqm::size e)
	{
		long long local = 0;
		for (xhqm::size i = b; i < e; ++i)
		{
			float I = P.cur[i];
			const netcsr& T = *P.t;
			for (xhqm::size k = T.rp[i]; k < T.rp[i + 1]; ++k)
				I += T.val[k] * P.s[T.col[k]];
			const float nv = P.v[i] + (0.0f - P.v[i]) * (dt / P.tau[i]) + dt * I;   // 逐神经元 tau
			if (nv >= P.th[i]) { P.s_new[i] = 1.0f; P.v[i] = 0.0f; ++local; }   // 复位到静息（非减阈值：后者会让 v 无界增长）
			else { P.s_new[i] = 0.0f; P.v[i] = nv; }
		}
		if (local) fired.fetch_add(local, std::memory_order_relaxed);
	});
	P.s.swap(P.s_new);
}

// ══════════════════ MaleCNS 装载 ══════════════════
static std::vector<std::string> g_superclass;   // 每神经元 superclass（统计用）
static std::vector<std::string> g_type;
static std::vector<std::string> g_side;         // left / right
static std::vector<float> g_cx, g_cy;           // soma 坐标（多数光感受器没有，仅供诊断）
static std::vector<int> g_h1, g_h2, g_colflag;  // 光学柱编号（小眼拓扑，来自 visual_columns.txt）
static std::unordered_map<std::string, std::vector<xhqm::size>> g_groups;

// 引号感知的 CSV 字段切分（neurons.csv 用标准 CSV 转义写过）
static std::vector<std::string> csv_split(const std::string& line)
{
	std::vector<std::string> out;
	std::string cur;
	bool q = false;
	for (std::size_t i = 0; i < line.size(); ++i)
	{
		const char c = line[i];
		if (q)
		{
			if (c == '"')
			{
				if (i + 1 < line.size() && line[i + 1] == '"') { cur.push_back('"'); ++i; }
				else q = false;
			}
			else cur.push_back(c);
		}
		else if (c == '"') q = true;
		else if (c == ',') { out.push_back(cur); cur.clear(); }
		else cur.push_back(c);
	}
	out.push_back(cur);
	return out;
}

static bool load_csr(const std::string& path, netcsr& T, xhqm::size& n)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) { std::fprintf(stderr, "打不开 %s\n", path.c_str()); return false; }
	char magic[4];
	f.read(magic, 4);
	if (std::memcmp(magic, "MNC1", 4) != 0) { std::fprintf(stderr, "magic 不对\n"); return false; }
	std::uint32_t ver = 0, pad = 0;
	std::uint64_t nn = 0, nnz = 0;
	f.read((char*)&ver, 4); f.read((char*)&pad, 4);
	f.read((char*)&nn, 8); f.read((char*)&nnz, 8);
	n = (xhqm::size)nn;
	T.rp.resize(n + 1);
	T.col.resize((std::size_t)nnz);
	T.val.resize((std::size_t)nnz);
	std::vector<std::uint64_t> rp64(n + 1);
	f.read((char*)rp64.data(), (std::streamsize)((n + 1) * 8));
	for (xhqm::size i = 0; i <= n; ++i) T.rp[i] = (xhqm::size)rp64[i];
	std::vector<std::uint32_t> c32((std::size_t)nnz);
	f.read((char*)c32.data(), (std::streamsize)(nnz * 4));
	for (std::size_t i = 0; i < (std::size_t)nnz; ++i) T.col[i] = (xhqm::size)c32[i];
	f.read((char*)T.val.data(), (std::streamsize)(nnz * 4));
	std::printf("csr.bin: n=%zu nnz=%zu  行和=%zu\n", n, (std::size_t)nnz, (std::size_t)T.rp[n]);
	return true;
}

static bool load_neurons(const std::string& path)
{
	std::ifstream f(path);
	if (!f) return false;
	std::string line;
	std::getline(f, line);
	g_superclass.clear(); g_type.clear();
	while (std::getline(f, line))
	{
		if (line.empty()) continue;
		const std::vector<std::string> c = csv_split(line);
		if (c.size() < 4) continue;
		const xhqm::size i = (xhqm::size)std::strtoull(c[0].c_str(), nullptr, 10);
		if (g_type.size() <= i)
		{
			g_type.resize(i + 1); g_superclass.resize(i + 1); g_side.resize(i + 1);
			g_cx.resize(i + 1); g_cy.resize(i + 1);
		}
		g_type[i] = c[2];
		g_superclass[i] = c[3];
		g_side[i] = (c.size() > 5) ? c[5] : std::string();
		// 注意列序：idx,id,type,superclass,class,side,nt,sign,x,y,z,measured,...
		// 坐标在 8/9，不是 4/5（4 是 class、5 是 side —— 取错列会让方位角全为 0）
		if (c.size() > 9) { g_cx[i] = (float)atof(c[8].c_str()); g_cy[i] = (float)atof(c[9].c_str()); }
	}
	std::printf("neurons.csv: %zu 行\n", g_type.size());
	return true;
}

// 光学柱编号：光感受器的小眼拓扑。6006 个光感受器里只有 28 个有实测 soma 坐标
// （它们在视网膜里，不在 CNS 电镜体积内），所以方位角必须来自光学柱表，
// 不能用坐标算 —— 否则所有光感受器会落到同一方位，移动光栅退化成全局闪光。
static bool load_columns(const std::string& path, xhqm::size n)
{
	std::ifstream f(path);
	if (!f) { std::fprintf(stderr, "打不开 %s（空间结构刺激不可用）\n", path.c_str()); return false; }
	g_h1.assign(n, 0); g_h2.assign(n, 0); g_colflag.assign(n, 0);
	std::string line;
	std::size_t cnt = 0;
	while (std::getline(f, line))
	{
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ss(line);
		std::size_t i; std::string sd; int a = 0, b = 0, fl = 0;
		if (!(ss >> i >> sd >> a >> b >> fl)) continue;
		if (i >= n) continue;
		g_h1[i] = a; g_h2[i] = b; g_colflag[i] = fl;
		if (g_side.size() > i) g_side[i] = sd;
		++cnt;
	}
	std::printf("光学柱: %zu 个光感受器有柱编号\n", cnt);
	return cnt > 0;
}

static bool load_groups(const std::string& path){
	std::ifstream f(path);
	if (!f) return false;
	std::string line;
	while (std::getline(f, line))
	{
		if (line.empty() || line[0] == '#') continue;
		for (char& c : line) if (c == ' ') c = ',';
		const std::vector<std::string> rest = csv_split(line);
		if (rest.size() < 2) continue;
		std::vector<xhqm::size> v;
		for (std::size_t i = 2; i < rest.size(); ++i)
			if (!rest[i].empty()) v.push_back((xhqm::size)std::strtoull(rest[i].c_str(), nullptr, 10));
		g_groups[rest[0]] = v;
	}
	for (auto& kv : g_groups)
		std::printf("组 %-24s %zu\n", kv.first.c_str(), kv.second.size());
	return true;
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc < 2) { std::fprintf(stderr, "用法: mcns_net <malecns目录> [选项]\n"); return 2; }
	const std::string dir = argv[1];
	double sec = 5.0, bg_m = 0.45, syn_gain = 60.0, thr_het = 0.15;
	// dt/tau 默认取 fly64/oranta-fly 的标度（dt=20ms、tau=100ms）：LIF 的膜时间常数
	// 是 100ms 量级，用 1ms 步长既无必要，也让每步全量遍历 2558 万条边贵 20 倍。
	double dt_a = 0.020, tau_a = 0.100;
	// 视觉通路的异质时间常数（默认 0 = 不改，即全网络统一 100ms）。
	// 生物学依据：真实果蝇里 lamina L1/L2 快（~10ms）、medulla 的 Tm 系列慢，
	// 这个快慢差正是 T4/T5 方向选择性所需的「延迟线」。全用同一个 tau 时两条输入
	// 没有时间差，HRC 相关器直接抵消 —— 实测运动光栅下 Tm3/T4c 与对照无法区分。
	double tau_vis = 0.0;                // 光感受器 + 视叶内部（ol_sensory / ol_intrinsic）
	double tau_delay = 0.0;              // 延迟线细胞 Tm1/Tm2/Tm3/Tm4/Tm9
	double lp_tau = 0.100;               // 读出的肌肉级低通时间常数（秒）
	// 背景噪声：确定性 LIF 在亚阈值静息时永不发放（实测全网 0 发放），
	// fly64 用 OU 噪声（有效 σ≈0.022）维持自发活动。这里按同一量级给膜电位加高斯扰动。
	double noise_sd = 0.022;
	double mod_hz = 0.0;                 // >0 时把注入变成方波（模拟光的有无），用于看去抑制
	std::string pattern = "step";        // off | step | flicker | stripe
	double flicker_hz = 5.0;             // 每个光感受器独立随机通断的平均切换率
	double stripe_hz = 4.0;              // 条纹图案的移动频率（真实运动刺激）
	double rev_block = 0.80;             // 反向光栅的【反转块长】均值（秒）——必须远大于网络时间常数
	double stripe_period = 0.8;          // 条纹空间周期（弧度）
	double grating_deg = 0.0;            // 光栅条纹方向（0 = 沿方位轴=水平条纹，90 = 垂直条纹）
	// 光适应（光感受器自身的性质，不是对大脑的干预）：果蝇光感受器有钙依赖的慢增益控制，
	// 功能效果是「响应相对最近平均亮度的变化」而不是绝对亮度 —— 即对比度编码。
	// 这决定了共模（全局闪光）与差模（空间图样）在受体就已经被区别对待。
	double loom_max = 40.0;              // looming 暗盘的最大半径（度）
	bool label_random = false;           // 对照：打乱相位标签（检验解码流程是否有偏）
	double adapt_tau = 0.0;              // 0 = 关闭
	double adapt_c0 = 0.10;              // 暗处对比度分母下限，避免除零
	std::string log_csv;                 // 逐步记录，用于「输入是否真的驱动读出」的受控分析
	bool abs_w = false, quiet = false;
	std::string norm = "in";             // in | out | raw（见下方归一化说明）
	std::string inj_name;
	double inj_amp = 0.0;
	std::string inj_class;               // 按 superclass 注入（对应 PyTorch 参考实现的 ol_sensory）
	// ── 秩-K 树突亚基（--dend）──
	// 见 DESIGN_NOTES 第十四节与 dend_branch.h。默认 K=1 ⇒ 点神经元，行为与加这个开关之前逐位相同。
	int dend_k = 1;
	bool dend_identity = false;          // 只分组、不加分支非线性（数学上应严格等于 K=1）
	unsigned long long dend_seed = 0;    // 分支阈值 ϑ 的随机流种子（与 torch.Generator(seed) 对齐）
	bool dend_drive_check = false;        // 量具自检：分支路径与逐边路径是否同一个算式
	// ── 电流型模式（--cell-model cur）的两个标定系数（见 cell_backend.h 的 bio::cur_mode）──
	// α = 1 − cur_alpha 是膜电位的保留比例；cur_kI 是驱动项的系数。参考实现的步进是
	//   v ← (1−1/τ)·v + I（I 不带 dt 因子），所以这两个系数必须【标定】而不是假定。
	double cur_alpha_g = 0.2, cur_kI_g = 0.02;
	std::string cell_model = "lif";      // lif | bio（bio 见 cell_backend.h）
	double bio_gain = 1.0;               // bio 模式的全局突触增益（把基线发放率校回 LIF 水平）
	bool bio_graded = false;             // bio 模式下是否把光感受器/LMC 改成分级输出
	bool graded_proj = false;            // 分级链是否再往下延一级（含 visual_projection）
	float graded_base = 0.0f;            // 传 s_k − graded_base·静息输出：0=绝对电平，1=纯偏移
	float graded_amp = 1.0f;             // 位相性增益（乘在偏移量上）
	float norm_gain = 0.0f;              // 分流归一化增益（自身兴奋驱动 → 成比例分流抑制）
	double stim_off_after = 0.0;         // >0 时在该秒数后停止注入（记忆/自持测试）
	static bool g_stim_off_printed = false;
	bool hrc_wire = false;               // bio 模式下给 T4/T5 接慢电压依赖通道（HRC 第二条腿）
	double hrc_shift = 0.0;              // 显式空间基线：慢腿相对快腿的方位偏移 Δc（度）
	float t4_gain = 1.0f;                // 只放大 T4/T5 入边权重（把 T4 抬到 HRC 有效的工作点）
	// 按通路放大：{前类型 superclass, 后类型 superclass, 倍数}，可重复给
	struct path_gain_t { std::string pre, post; float x; };
	std::vector<path_gain_t> path_gains;
	std::string az_bin;                  // cell_az.bin：每个细胞的方位角（offline_hrc_geom.py 导出）
	for (int i = 2; i < argc; ++i)
	{
		if (!std::strcmp(argv[i], "--sec") && i + 1 < argc) sec = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--cell-model") && i + 1 < argc) cell_model = argv[++i];
		else if (!std::strcmp(argv[i], "--bio-gain") && i + 1 < argc) bio_gain = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--bio-graded")) bio_graded = true;
		else if (!std::strcmp(argv[i], "--graded-proj")) graded_proj = true;
		else if (!std::strcmp(argv[i], "--graded-base") && i + 1 < argc) graded_base = (float)atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--graded-amp") && i + 1 < argc) graded_amp = (float)atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--norm-gain") && i + 1 < argc) norm_gain = (float)atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--stim-off-after") && i + 1 < argc) stim_off_after = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--hrc-wire")) hrc_wire = true;
		else if (!std::strcmp(argv[i], "--hrc-shift") && i + 1 < argc) hrc_shift = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--t4-gain") && i + 1 < argc) t4_gain = (float)atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--path-gain") && i + 3 < argc)
		{
			path_gain_t pg; pg.pre = argv[++i]; pg.post = argv[++i]; pg.x = (float)atof(argv[++i]);
			path_gains.push_back(pg);
		}
		else if (!std::strcmp(argv[i], "--az-bin") && i + 1 < argc) az_bin = argv[++i];
		else if (!std::strcmp(argv[i], "--bg-m") && i + 1 < argc) bg_m = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--syn-gain") && i + 1 < argc) syn_gain = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--thr-het") && i + 1 < argc) thr_het = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--dt") && i + 1 < argc) dt_a = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--tau") && i + 1 < argc) tau_a = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--tau-vis") && i + 1 < argc) tau_vis = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--tau-delay") && i + 1 < argc) tau_delay = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--lp-tau") && i + 1 < argc) lp_tau = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--noise-sd") && i + 1 < argc) noise_sd = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--noise-off")) noise_sd = 0.0;
		else if (!std::strcmp(argv[i], "--abs-w")) abs_w = true;
		else if (!std::strcmp(argv[i], "--norm") && i + 1 < argc) norm = argv[++i];
		else if (!std::strcmp(argv[i], "--quiet")) quiet = true;
		else if (!std::strcmp(argv[i], "--inject") && i + 2 < argc) { inj_name = argv[++i]; inj_amp = atof(argv[++i]); }
		else if (!std::strcmp(argv[i], "--inject-class") && i + 2 < argc) { inj_class = argv[++i]; inj_amp = atof(argv[++i]); }
		else if (!std::strcmp(argv[i], "--dend") && i + 1 < argc) dend_k = atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--dend-identity")) dend_identity = true;
		else if (!std::strcmp(argv[i], "--dend-seed") && i + 1 < argc) dend_seed = std::strtoull(argv[++i], nullptr, 10);
		else if (!std::strcmp(argv[i], "--dend-drive-check")) dend_drive_check = true;
		else if (!std::strcmp(argv[i], "--cur-alpha") && i + 1 < argc) cur_alpha_g = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--cur-ki") && i + 1 < argc) cur_kI_g = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--mod-hz") && i + 1 < argc) mod_hz = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--pattern") && i + 1 < argc) pattern = argv[++i];
		else if (!std::strcmp(argv[i], "--flicker-hz") && i + 1 < argc) flicker_hz = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--stripe-hz") && i + 1 < argc) stripe_hz = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--rev-block") && i + 1 < argc) rev_block = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--stripe-period") && i + 1 < argc) stripe_period = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--grating-deg") && i + 1 < argc) grating_deg = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--adapt-tau") && i + 1 < argc) adapt_tau = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--loom-max") && i + 1 < argc) loom_max = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--label-random")) label_random = true;
		else if (!std::strcmp(argv[i], "--adapt-c0") && i + 1 < argc) adapt_c0 = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--log-csv") && i + 1 < argc) log_csv = argv[++i];
	}
	// --hrc-shift 要把慢腿换到偏移 Δc 的同类细胞，前提是那些边被标成了慢通道。
	// 必须在下面的接线块【之前】打开，所以放在解析之后而不是接线块里（踩过：写在块里就太晚了）。
	if (hrc_shift != 0.0 && !hrc_wire)
	{
		hrc_wire = true;
		std::printf("★ --hrc-shift 需要慢腿通道 ⇒ 已自动开启 --hrc-wire\n");
		if (az_bin.empty()) az_bin = dir + "\\cell_az.bin";   // 与 csr.bin 同目录（offline_hrc_geom.py 写在那里）
	}

	netcsr T;
	xhqm::size n = 0;
	if (!load_csr(dir + "\\csr.bin", T, n)) return 1;
	if (!load_neurons(dir + "\\neurons.csv")) return 1;
	if (!load_groups(dir + "\\groups.txt")) return 1;
	load_columns(dir + "\\visual_columns.txt", n);

	// --inject-class <superclass>：按解剖分区注入。PyTorch 参考实现（torch_bio_brain.py）用的
	// 就是 superclass == 'ol_sensory'（光感受器，6098 个），而数据目录的 groups.txt 里只有
	// visual_R1_8 那类【视觉组】、没有 superclass 组，所以这里就地建一个同名组，好让两边
	// 说的是同一个群体（否则「不一致」根本归因不了）。
	if (!inj_class.empty())
	{
		std::vector<xhqm::size> ids;
		for (xhqm::size i = 0; i < n && i < g_superclass.size(); ++i)
			if (g_superclass[i] == inj_class) ids.push_back(i);
		if (ids.empty()) { std::fprintf(stderr, "没有 superclass = %s 的神经元\n", inj_class.c_str()); return 1; }
		g_groups[inj_class] = ids;
		inj_name = inj_class;
		std::printf("--inject-class: superclass %s 共 %zu 个神经元（注入幅度 %.1f）\n",
			inj_class.c_str(), ids.size(), inj_amp);
	}

	double w_pos = 0, w_neg = 0, w_abs = 0;
	for (float v : T.val) { if (v >= 0) w_pos += v; else w_neg += -v; w_abs += std::fabs(v); }
	std::printf("权重: 正 %.0f (%.1f%%)  负 %.0f (%.1f%%)  绝对值合计 %.0f\n",
		w_pos, 100.0 * w_pos / w_abs, w_neg, 100.0 * w_neg / w_abs, w_abs);

	// ── 归一化口径 ──
	// csr.bin 里的权重已按「突触后神经元入度」归一化，即每行 |权重| 和 = 1。
	// 这会把「稀疏但强」的通路系统性稀释：中央脑枢纽神经元上万条入边，
	// 视觉投射神经元给它的每一条只占 1/10000，而密集的递归背景被相对放大。
	// 这里提供三种口径做对照（均以「行绝对和的中位数 = 1」对齐，保证工作点可比）：
	//   in  : 入度归一化（现状，fly64 口径）
	//   out : 出度归一化（每条突触前神经元把自己的总权重 1 均分给各目标）
	//   raw : 保留原始突触数（只做全局均值对齐，强度分布不被抹平）
	if (norm != "in")
	{
		std::vector<float> in_abs(n, 0.0f), out_abs(n, 0.0f);
		auto read_f32 = [](const std::string& p, std::vector<float>& v, xhqm::size cnt)
		{
			std::ifstream f(p, std::ios::binary);
			if (!f) return false;
			v.resize(cnt);
			f.read((char*)v.data(), (std::streamsize)(cnt * sizeof(float)));
			return (bool)f;
		};
		const std::string dir2 = dir + "\\";
		if (!read_f32(dir2 + "incoming_abs.bin", in_abs, n) || !read_f32(dir2 + "out_abs.bin", out_abs, n))
		{
			std::fprintf(stderr, "缺少 incoming_abs.bin / out_abs.bin，无法切换归一化\n");
			return 1;
		}
		for (xhqm::size i = 0; i < n; ++i)
		{
			if (i % 20000 == 0) std::printf("  归一化 %zu/%zu\r", (std::size_t)i, (std::size_t)n);
			for (xhqm::size k = T.rp[i]; k < T.rp[i + 1]; ++k)
			{
				float raw = T.val[k] * in_abs[i];        // 还原原始突触数（带符号）
				T.val[k] = (norm == "raw") ? raw
					: (out_abs[T.col[k]] > 0.0f ? raw / out_abs[T.col[k]] : 0.0f);
			}
		}
		std::printf("\n");
		// 全局对齐：让「行绝对和的中位数」= 1，与入度口径同一个量级
		std::vector<float> rsum(n, 0.0f);
		for (xhqm::size i = 0; i < n; ++i)
			for (xhqm::size k = T.rp[i]; k < T.rp[i + 1]; ++k) rsum[i] += std::fabs(T.val[k]);
		std::vector<float> srt(rsum);
		std::sort(srt.begin(), srt.end());
		const float med = std::max(srt[n / 2], 1e-9f);
		for (float& v : T.val) v /= med;
		std::printf("归一化口径=%s  行绝对和中位=%.1f 均值=%.1f p99=%.1f 最大=%.1f（已除以中位对齐）\n",
			norm.c_str(), (double)med, (double)(std::accumulate(rsum.begin(), rsum.end(), 0.0) / n),
			(double)srt[(std::size_t)(n * 0.99)], (double)srt.back());
	}
	if (abs_w)
	{
		for (float& v : T.val) v = std::fabs(v);
		std::printf("★ --abs-w：已把负权重取绝对值（复现旧的全兴奋网络，用于对照）\n");
	}
	// 突触增益：行内权重已被归一化到 |和|=1，故乘 g_syn 后「全部输入同时发放」等效驱动 = g_syn
	for (float& v : T.val) v *= (float)syn_gain;

	// ── 秩-K 树突亚基（--dend，见 dend_branch.h 与 DESIGN_NOTES 第十四节）──
	// ★ 必须在【权重归一化与 syn_gain 之后】构建：分支 CSR 里存的是带符号的【有效权重】，
	//   与主 CSR 的 val 严格同源，这样 h_β 与逐边累加说的才是同一个量。
	// K=1（默认）时不建结构、dend 保持 nullptr ⇒ bio_step / LIF 走原路径。
	dendbranch::dend DEND;
	const bool dend_on = (dend_k > 1);
	if (dend_on)
	{
		if (dend_k > dendbranch::DEND_MAX_K)
		{
			std::fprintf(stderr, "★ --dend %d 超过编译期上界 %d，请改 dend_branch.h 的 DEND_MAX_K 后重编\n",
				dend_k, (int)dendbranch::DEND_MAX_K);
			return 1;
		}
		DEND.build(n, T.rp.data(), T.col.data(), T.val.data(), g_superclass,
			dend_k, dend_identity ? dendbranch::DM_IDENTITY : dendbranch::DM_RELU, (std::uint64_t)dend_seed);
		dendbranch::dend::self_check();
		// 统计：分支数、每分支边数、阈值抽样是否落在 U(−0.1,0.1)（判据：非空即对不上就说明流没走对）
		std::size_t nb = 0, nuniq = 0;
		float thmin = 1e9f, thmax = -1e9f;
		for (std::size_t i = 0; i < DEND.brp.size() - 1; ++i)
		{
			const std::size_t c = DEND.brp[i + 1] - DEND.brp[i];
			if (c) { ++nb; nuniq += c; }
		}
		for (float t : DEND.br_th) { thmin = std::min(thmin, t); thmax = std::max(thmax, t); }
		std::size_t nall = 0;
		for (std::size_t i = 0; i < n; ++i) nall += (std::size_t)(T.rp[i + 1] - T.rp[i]);
		std::printf("秩-K 树突: K=%d 模式=%s | 分支划分=按突触前 superclass 取最多的 K−1 种（其余归分支 0）| "
			"分支阈值 ϑ∈[%.4f, %.4f]（期望 ⊂ [−0.1, 0.1]）| 非空分支 %zu / %zu，覆盖边 %zu（= 非零元数 %zu，"
			"每条边只属于一个分支 ⇒ 总突触数不变）\n",
			dend_k, dend_identity ? "identity（只分组，无分支非线性）" : "relu（分支整流）",
			(double)thmin, (double)thmax, nb, DEND.brp.size() / (std::size_t)dend_k,
			nuniq, nall);
		if (cell_model != "bio")
			std::printf("  ★ mode=rank-%d 需要 --cell-model bio —— 当前是 %s，树突结构【不会被使用】，"
				"这一跑等于点神经元，别把它当秩-K 的结果。\n", dend_k, cell_model.c_str());
	}

	// ── 量具自检：分支路径与逐边路径是否描述同一个算式（--dend-drive-check）──
	// 判据不依赖任何网络动力学：给定【同一个】突触前状态 s，分别算
	//   ① 主 CSR 顺序的线性求和  D1 = Σ_k (val_k·syn_gain)·Y_k
	//   ② 分支路径              D2 = branch_mod(...) 给出的「分支对胞体的驱动」
	// 然后报相对误差。identity 模式下 D1 与 D2 必须【同一个算式】（差异只应来自浮点加法次序）；
	// relu 模式下若 D1 == D2，说明分支非线性【没有被用上】—— 那才是真的量具坏了。
	// 这一条比「跑两遍网络看数字像不像」强得多：网络是混沌的，浮点次序差异会被放大成
	// 完全不同的活动（本项目刚踩过：K=8 identity 与 K=1 的显著数 164 对 1335，成因就是这个）。
	if (dend_drive_check && dend_on)
	{
		std::vector<float> s(n, 0.0f), mod(dendbranch::DEND_MAX_K);
		for (xhqm::size i = 0; i < n; ++i)
			s[i] = (float)(((i * 2654435761u) >> 8 & 0xFFu) / 255.0);   // 固定的非平凡图案
		double mx_id = 0.0, mx_rl = 0.0, mxz_id = 0.0, mxz_rl = 0.0;
		double mx_neg = 0.0;                 // ★ 只看负权重（抑制）那一半的偏差
		std::size_t ndiff_id = 0, ndiff_rl = 0, nzh = 0;
		for (xhqm::size i = 0; i < n; ++i)
		{
			float d1 = 0.0f;
			for (xhqm::size k = T.rp[i]; k < T.rp[i + 1]; ++k) d1 += T.val[k] * s[T.col[k]];
			float bias = 0.0f;
			const float z = dendbranch::branch_mod(DEND, i, s.data(), mod.data(), bias);
			float d2 = 0.0f, d1_neg = 0.0f, d2_neg = 0.0f;
			for (xhqm::size k = T.rp[i]; k < T.rp[i + 1]; ++k)
			{
				d2 += T.val[k] * mod[DEND.edge_b[k]] * s[T.col[k]];   // 带符号权重 × 分支调制因子
				if (T.val[k] < 0.0f)
				{
					d1_neg += T.val[k] * s[T.col[k]];
					d2_neg += T.val[k] * mod[DEND.edge_b[k]] * s[T.col[k]];
				}
			}
			d2 -= bias;
			// ★ 只比【总量】是不够的，这一课很贵：曾经有个 bug 让【抑制边整体反号而兴奋边不变】，
			//   两边加起来的总和几乎不变，于是这个静态自检报了「identity 成立」而实际不成立
			//   （真正的差异要靠逐步 dump 才看得到）。现在把负权重那一半单独比一次 ——
			//   任何单通道反号都会在这里暴露。
			mx_neg = std::max(mx_neg, std::fabs((double)d2_neg - (double)d1_neg)
				/ std::max(1e-6, (double)std::fabs(d1_neg)));
			const double sc = std::max(1e-6, (double)std::fabs(d1));
			const double e_id = (i < DEND.brp.size()) ? std::fabs((double)d1 - (double)d2) / sc : 0.0;
			const double e_rl = std::fabs((double)d1 - (double)z) / sc;
			if (dend_identity) { mx_id = std::max(mx_id, e_id); if (e_id > 1e-5) ++ndiff_id; }
			else { mx_rl = std::max(mx_rl, e_rl); if (e_rl > 1e-3) ++ndiff_rl; if (std::fabs(z) > 1e-6) ++nzh; }
			mxz_id = std::max(mxz_id, (double)std::fabs(z));
			mxz_rl = std::max(mxz_rl, (double)std::fabs(d2));
		}
		std::printf("★ 量具自检（--dend-drive-check，同一个 s、无网络动力学）:\n");
		std::printf("  抑制半边（Σ_{w<0} w·mod·s vs Σ_{w<0} w·s）最大相对偏差 = %.3e"
			"  ← 专门用来抓「某个通道整体反号」那类 bug（只比总量会漏掉它）\n", mx_neg);
		if (dend_identity)
			std::printf("  identity: 逐边求和 vs 分支路径的最大相对误差 = %.3e（超 1e-5 的细胞 %zu / %zu）；"
				"分支驱动最大 |z| = %.4f\n  ⇒ %s\n", mx_id, ndiff_id, (std::size_t)n, mxz_id,
				(mx_id < 1e-5) ? "两者是同一个算式（差异只剩浮点加法次序）—— identity 对照成立"
				               : "★ 不是同一个算式，identity 对照不成立，先别往下做实验");
		else
			std::printf("  relu: 逐边求和(点神经元) vs 分支路径(秩 K) 的最大相对误差 = %.3e，其中分支驱动非零的细胞 %zu；"
				"|z| 最大 %.4f；相对误差 > 1e-3 的细胞 %zu\n  ⇒ %s\n", mx_rl, nzh, mxz_rl, ndiff_rl,
				(nzh > 0 && ndiff_rl > 0) ? "分支非线性确实改变了驱动（量具有分辨力）"
				                          : "★ 两条路径给出同一个数 —— 分支非线性【没生效】，先别往下做实验");
	}

	const float dt = (float)dt_a, tau = (float)tau_a, V_th = 1.0f;
	population P;
	std::mt19937 rng(20260303u);
	P.init(n, rng);
	if (thr_het > 0.0)
	{
		std::uniform_real_distribution<float> d((float)(1.0 - thr_het), (float)(1.0 + thr_het));
		for (xhqm::size i = 0; i < n; ++i) P.th[i] = V_th * d(rng);
	}
	else for (xhqm::size i = 0; i < n; ++i) P.th[i] = V_th;
	P.t = &T;
	// 视觉通路的异质时间常数（可选）。改 tau 会让静息 v_ss = tau*I_bg 一起变，
	// 所以背景电流必须按神经元各自补偿：I_bg_i = m*V_th/tau_i，静息才仍落在 m*V_th。
	if (tau_vis > 0.0 || tau_delay > 0.0)
	{
		std::size_t nvv = 0, nd = 0;
		for (xhqm::size i = 0; i < n; ++i)
		{
			const std::string sc = (i < g_superclass.size()) ? g_superclass[i] : "";
			const std::string tp = (i < g_type.size()) ? g_type[i] : "";
			if (tau_vis > 0.0 && (sc == "ol_sensory" || sc == "ol_intrinsic"))
			{ P.tau[i] = (float)tau_vis; ++nvv; }
			if (tau_delay > 0.0 && (tp == "Tm1" || tp == "Tm2" || tp == "Tm3" || tp == "Tm4" || tp == "Tm9"))
			{ P.tau[i] = (float)tau_delay; ++nd; }
		}
		std::printf("异质 tau: 视叶 %zu 个 (%.0fms)，延迟线 %zu 个 (%.0fms)\n",
			nvv, tau_vis * 1000, nd, tau_delay * 1000);
	}
	// 逐神经元背景电流：I_bg_i = m*V_th/tau_i ⇒ 静息 v_ss = tau_i*I_bg_i = m*V_th（一致）
	std::vector<float> I_bg_i(n);
	{
		std::size_t n_clamped = 0; float tau_min = 1e9f;
		for (xhqm::size i = 0; i < n; ++i)
		{
			// ★ 数值稳定性守卫：显式欧拉要求 dt/tau < 1，否则 v_new = v + (0-v)*(dt/tau) 会变成
			//   「每步把 v 乘一个绝对值大于 1 的负数」→ 膜电位在正负之间振荡（实测 τ=15ms 配 dt=20ms
			//   时全网活跃率从 54% 跳到 80%、所有响应消失）。安全区取 tau >= 3*dt。
			if (P.tau[i] < 3.0f * dt)
			{
				tau_min = std::min(tau_min, P.tau[i]);
				P.tau[i] = 3.0f * dt;
				++n_clamped;
			}
			I_bg_i[i] = (float)(bg_m * V_th / P.tau[i]);
		}
		if (n_clamped)
			std::printf("★ 警告：%zu 个神经元的 tau(最小 %.0fms) < 3*dt(%.0fms)，已钳到 3*dt —— "
				"否则显式欧拉会失稳（膜电位正负振荡）\n",
				n_clamped, tau_min * 1000, 3.0f * dt * 1000);
	}

	const float I_bg = (float)(bg_m * V_th / tau);      // ⇒ 静息 v_ss = tau*I_bg = m*V_th
	std::printf("LIF: dt=%.4fs tau=%.4fs V_th=%.2f  I_bg=%.2f ⇒ 静息 v_ss=%.3f（裕度 %.1f%%）\n",
		dt, tau, V_th, I_bg, (double)bg_m, 100.0 * (1.0 - bg_m));
	// ── 细胞模型后端（--cell-model lif|bio）──
	// lif 是回归基准；bio 把突触电流脉冲换成电导通道（test/cellsim）。
	// 接口与对齐规则见 cell_backend.h 的文件头。
	cellbackend::bio BIO;
	const bool use_bio = (cell_model == "bio");
	// ── 电流型模式（--cell-model cur）：与 PyTorch 参考严格同构，用来做「同细胞模型」对齐实验 ──
	// 见 cell_backend.h 里 bio::cur_mode 的说明。要点：**不加 I_bg、不加噪声**（参考就是没有），
	// v 从 0 起（参考初始化 v=0），输出 clamp(v,0,1)，权重即电流（w 已含 syn_gain）。
	const bool use_cur = (cell_model == "cur");
	if (use_cur)
	{
		BIO.init(n, P.th, dt, (float)(bg_m * V_th), tau, 1.0);   // gain=1：电流型不额外缩放
		BIO.cur_mode = (dend_on && !dend_identity) ? 2 : 1;      // 2 = 秩-K 分支；1 = 线性求和
		BIO.cur_alpha = (float)cur_alpha_g;
		BIO.cur_kI = (float)cur_kI_g;
		BIO.dend = dend_on ? &DEND : nullptr;
		for (xhqm::size i = 0; i < n; ++i)
		{
			BIO.c[i].v = 0.0f; BIO.c[i].out = 0.0f;      // 参考：v 从 0 起
			BIO.p[i].graded = 1;                          // 参考：**所有**细胞都是 clamped 分级输出
			BIO.p[i].graded_v0 = 0.0f;                    // 输出 = clamp(v, 0, 1)，与参考的 s=clamp(nv,0,1) 一致
			BIO.p[i].graded_v1 = 1.0f;
		}
		std::printf("细胞模型: cur（电流型，与 torch_bio_brain.py 的 BioBrain 同构）| "
			"v←(1−%.3f)·v+dt·I，I=Σw·s（权重即电流）| 无 I_bg、无噪声 | 输出 clamp(v,0,1) | 树突: %s\n",
			(double)BIO.cur_alpha,
			(dend_on && !dend_identity) ? "秩-K 分支（branch_mod 替换线性求和）"
			                            : (dend_on ? "identity（只分组＝线性求和）" : "K=1 点神经元"));
	}
	else if (use_bio)
	{
		BIO.init(n, P.th, dt, (float)(bg_m * V_th), tau, bio_gain);
		BIO.norm_gain = norm_gain;
		BIO.dend = dend_on ? &DEND : nullptr;      // 秩-K 树突（默认 nullptr = 点神经元，逐位不变）
		if (norm_gain > 0.0f)
			std::printf("分流归一化: β=%.3f（每个神经元按自身兴奋电导 β 倍加分流抑制）"
				"—— 用于把「视觉只占驱动 6%%」变成「相对构成驱动输出」\n", norm_gain);
		std::printf("细胞模型: bio（电导型，test/cellsim）| 分级输出: %s | 全局突触增益 %.2f\n",
			bio_graded ? "开" : "关", bio_gain);
		BIO.self_check();
	}
	else std::printf("细胞模型: lif（回归基准）\n");
	// ── HRC 接线（--hrc-wire，仅 bio 模式）：给 T4/T5 的「第二条腿」指定慢电压依赖通道 ──
	// 依据是已验证的连接组解剖：T4a ← Mi1 28.0% + Tm3 12.1% + Mi9 9.0%。
	// 经典相关器需要「一条快 + 一条慢且带电压依赖」：Mi1 走快通道，Tm3/Tm9 走慢通道。
	// 不接这条线时 bio 只会用快通道（τ=5ms），与 LIF 性质相同 ⇒ 在线性求和下
	// 漂移正反两个方向的平均发放率严格相同，均率解码器【原理上】测不出方向。
	if (use_bio && hrc_wire)
	{
		auto is_t4t5 = [](const std::string& t) { return t.rfind("T4", 0) == 0 || t.rfind("T5", 0) == 0; };
		auto is_slow_pre = [](const std::string& t) { return t == "Tm3" || t == "Tm9" || t == "Tm2"; };
		std::size_t n_slow = 0, n_t4t5 = 0, n_fast = 0;
		for (xhqm::size i = 0; i < n; ++i)
		{
			const std::string post = (i < g_type.size()) ? g_type[i] : "";
			if (!is_t4t5(post)) continue;
			++n_t4t5;
			for (xhqm::size k = P.t->rp[i]; k < P.t->rp[i + 1]; ++k)
			{
				const xhqm::size pre = P.t->col[k];
				const std::string pt = (pre < g_type.size()) ? g_type[pre] : "";
				if (P.t->val[k] >= 0.0f && is_slow_pre(pt)) ++n_slow; else ++n_fast;
			}
		}
		BIO.edge_chan.assign(P.t->val.size(), 0xFF);
		for (xhqm::size i = 0; i < n; ++i)
		{
			const std::string post = (i < g_type.size()) ? g_type[i] : "";
			if (!is_t4t5(post)) continue;
			for (xhqm::size k = P.t->rp[i]; k < P.t->rp[i + 1]; ++k)
			{
				const xhqm::size pre = P.t->col[k];
				const std::string pt = (pre < g_type.size()) ? g_type[pre] : "";
				if (P.t->val[k] >= 0.0f && is_slow_pre(pt)) BIO.edge_chan[k] = (unsigned char)cellsim::CH_NMDA;
			}
		}
		std::printf("HRC 接线: T4/T5 共 %zu 个，入边 %zu 条（慢腿 %zu，快腿 %zu）| 慢腿 = 来自 Tm3/Tm9/Tm2 的兴奋性突触，"
			"通道 NMDA τ=%.0fms 电压依赖\n", n_t4t5, n_slow + n_fast, n_slow, n_fast, BIO.ch[cellsim::CH_NMDA].tau * 1000);
	}
	// ── 分级（模拟）视觉链：光感受器 → LMC(lamina) → 髓质 都是分级细胞，第一个发放脉冲的是 T4/T5 ──
	// 这是果蝇的真实架构，也是「视觉信号传不下去」的直接解法：
	// 实测脉冲模式下光感受器 25 Hz，而紧邻的 L1-L4 只有 0.15~0.64 Hz（6 秒 1~4 个脉冲），
	// 下游根本没有脉冲信号可传；分级链不需要脉冲，模拟量直接往下送。
	// 作用域：ol_sensory（光感受器）+ lamina L1-L5 + 髓质各型（Mi/Tm/T1/T2/T3/T4 之前的那些）。
	// T4/T5 保持脉冲（它们是第一个发放的细胞，HRC 的巧合检测靠它们），下游也保持脉冲。
	std::vector<char> g_sensory;
	float photo_i_cal = 0.0f;
	if (use_bio && bio_graded)
	{
		g_sensory.assign(n, 0);
		BIO.out_rest.assign(n, 0.0f);      // 脉冲细胞静息输出为 0；分级细胞在下面填 0.5；传感器为 0
		BIO.dev_base = graded_base;
		BIO.dev_amp = graded_amp;
		auto in_scope = [&](const std::string& sc, const std::string& tp) -> int
		{
			// 1 = 光感受器（传感器：输出=光强）；2 = 分级中间细胞；0 = 不参与（保持脉冲）
			if (sc == "ol_sensory") return 1;
			// 视叶投射：实测「ol_intrinsic 有信号 d′=234.6 而 visual_projection 为 0.0」，
			// 即断口已经移到 视叶内部 → 视叶投射 这一跳，与第一级同一类问题（模拟量过不了脉冲阈值）。
			// 用 --graded-proj 把分级链再往下延一级来验证这个判断。
			if (sc == "visual_projection") return graded_proj ? 2 : 0;
			const bool lamina = (tp.size() >= 2 && tp[0] == 'L' && tp[1] >= '1' && tp[1] <= '5' && tp.size() <= 2);
			const bool medulla = (tp.rfind("Mi", 0) == 0 || tp.rfind("Tm", 0) == 0 ||
				tp.rfind("T1", 0) == 0 || tp.rfind("T2", 0) == 0 || tp.rfind("T3", 0) == 0);
			return (lamina || medulla) ? 2 : 0;
		};
		const float HALF = 0.26f;      // 分级输出的半量程：静息处输出 0.5，±0.26 电位走满 0~1
		// 光感受器：输出要严格随光强线性（驱动 1.0 → 电位走满 2*HALF）。
		// 稳态 v = v_rest + I*tau_m ⇒ I = 2*HALF/tau_m。用 inj_amp=80 时 I*tau_m = 8，
		// 比 2*HALF=0.52 大 15 倍 ⇒ 分级输出瞬间饱和成常量，等于没有视觉信号。
		photo_i_cal = BIO.p.empty() ? 0.0f : (2.0f * HALF) / BIO.p[0].tau_m;
		std::size_t nsens = 0, ngrad = 0;
		for (xhqm::size i = 0; i < n; ++i)
		{
			const std::string sc = (i < g_superclass.size()) ? g_superclass[i] : "";
			const std::string tp = (i < g_type.size()) ? g_type[i] : "";
			const int k = in_scope(sc, tp);
			if (!k) continue;
			cellsim::cell_param& q = BIO.p[i];
			q.graded = 1;
			if (k == 1)
			{
				g_sensory[i] = 1;
				q.graded_v0 = q.v_rest;              // 暗 → 0
				q.graded_v1 = q.v_rest + 2.0f * HALF; // 全亮 → 1
				q.v_th = 1e9f;                       // 分级细胞不再判阈值（保险，edge 分支已提前返回）
				++nsens;
			}
			else
			{
				q.graded_v0 = q.v_rest - HALF;        // 静息 → 0.5（工作在响应区中部，而不是被压死）
				q.graded_v1 = q.v_rest + HALF;
				q.v_th = 1e9f;
				if (BIO.out_rest.size() > i) BIO.out_rest[i] = 0.5f;   // 静息输出，dev_mode 下要减掉
				++ngrad;
			}
		}
		std::printf("分级视觉链: 光感受器 %zu 个（输出=光强）| LMC+髓质 %zu 个（静息输出 0.5，±%.2f 走满量程）| "
			"光感受器注入标定 I=%.1f（原来是 inj_amp=%.1f，会把分级量程瞬间打穿）\n",
			nsens, ngrad, HALF, photo_i_cal, inj_amp);
	}
	// ── 按【通路】放大权重（--path-gain 前类型 后类型 倍数，可重复）──
	// 为什么需要它：实测逐级衰减 7~10 倍，机制是「每一级的驱动被【未被调制的背景/递归输入】主导」。
	// 所以该放大的是「视觉这条通路」，而不是「某几级细胞的全部输入」——
	// 后者（--t4-gain）会把背景一起放大，等于没改善信噪比。
	// 例：--path-gain visual_projection cb_intrinsic 5
	for (const auto& pg : path_gains)
	{
		std::size_t nn2 = 0; double sum0 = 0.0, sum1 = 0.0;
		for (xhqm::size i = 0; i < n; ++i)
		{
			const std::string post = (i < g_superclass.size()) ? g_superclass[i] : "";
			if (post != pg.post) continue;
			for (xhqm::size k = P.t->rp[i]; k < P.t->rp[i + 1]; ++k)
			{
				const xhqm::size pre = P.t->col[k];
				const std::string ps = (pre < g_superclass.size()) ? g_superclass[pre] : "";
				if (ps != pg.pre) continue;
				sum0 += std::fabs(P.t->val[k]);
				P.t->val[k] *= pg.x;
				sum1 += std::fabs(P.t->val[k]);
				++nn2;
			}
		}
		std::printf("通路增益 %s → %s: ×%.2f，作用 %zu 条边（|权重|合计 %.0f → %.0f）\n",
			pg.pre.c_str(), pg.post.c_str(), pg.x, nn2, sum0, sum1);
	}
	// ── T4/T5 输入增益（--t4-gain）：只放大 T4/T5 自己的入边权重 ──
	// 为什么不用全局 --syn-gain：cellsim 台架给出定量目标 —— T4 输出 0.02 时方向指数只有
	// ≈0.005（测不出），输出 0.36 时才到 ≈0.03。要把 T4 抬到那个工作点需要约 8 倍增益，
	// 但全局 syn-gain 放大到 320 会把整个网络推到数值发散（实测 Mi1 出现 -nan）。
	// 只放大 T4/T5 的入边就能达到同样的工作点，而不动其它细胞。
	if (t4_gain != 1.0f)
	{
		std::size_t nrow = 0, nedge = 0;
		for (xhqm::size i = 0; i < n; ++i)
		{
			const std::string t = (i < g_type.size()) ? g_type[i] : "";
			if (t.rfind("T4", 0) != 0 && t.rfind("T5", 0) != 0) continue;
			++nrow;
			for (xhqm::size k = P.t->rp[i]; k < P.t->rp[i + 1]; ++k) { P.t->val[k] *= t4_gain; ++nedge; }
		}
		std::printf("T4/T5 输入增益: ×%.2f，作用 %zu 个细胞、%zu 条入边（只放大这些行，不动网络其它部分）\n",
			t4_gain, nrow, nedge);
	}
	// 为什么需要：离线几何测量（offline_hrc_geom.py）实测两条腿的平均空间偏移只有 0.2~0.3°，
	// 而细胞间散布 ~5.5° —— 即「一条腿取 Mi1、另一条腿取 Tm3」根本没有空间基线。
	// 而 cellsim 的台架阴性对照早就证明：时间非对称本身产生不了方向选择性（方向指数恒 0.00）。
	// HRC 要求 Δt = Δx/v，所以这里把缺失的 Δx 显式补上：保留连接组的类型构成与权重，
	// 只把慢腿（Tm3/Tm9/Tm2）的柱归属按方位角平移 Δc。
	// ★ 内部对照：a 取 +Δc、d 取 −Δc（b/c 同理）——
	//   只有 T4a 与 T4d 呈现出【相反】的方向偏好，才说明是真的方向选择性；同号就是伪影。
	if (use_bio && hrc_shift != 0.0 && !az_bin.empty())
	{
		if (!hrc_wire) hrc_wire = true;   // 见参数解析后的自动开启说明
		std::vector<float> caz(n, std::numeric_limits<float>::quiet_NaN());
		{
			std::ifstream f(az_bin, std::ios::binary);
			if (!f)
				std::printf("★ 显式空间基线【失败】：打不开 %s —— 换边不会发生。"
					"请先用 offline_hrc_geom.py 生成 cell_az.bin，或用 --az-bin 指定路径。\n", az_bin.c_str());
			else
			{
				f.read((char*)caz.data(), (std::streamsize)n * sizeof(float));
				std::size_t nok = 0;
				for (xhqm::size i = 0; i < n; ++i) if (std::isfinite(caz[i])) ++nok;
				std::printf("显式空间基线: 载入 %s，%zu/%zu 个细胞有方位角\n", az_bin.c_str(), nok, (std::size_t)n);
				// 文件读失败/不完整时不要静默 —— 踩过：路径写错导致「重接 0 条」而毫无提示
				if (nok < n / 2) std::printf("★ 只有不到一半的细胞有方位角，换边基本不会发生，请检查该文件\n");
			}
		}
		const char* slowt[] = { "Tm3", "Tm9", "Tm2" };
		std::unordered_map<std::string, std::vector<xhqm::size>> st;
		for (xhqm::size i = 0; i < n; ++i)
		{
			const std::string t = (i < g_type.size()) ? g_type[i] : "";
			for (const char* s : slowt)
				if (t == s && std::isfinite(caz[i])) { st[t].push_back(i); break; }
		}
		for (auto& kv : st)
			std::sort(kv.second.begin(), kv.second.end(),
				[&](xhqm::size a2, xhqm::size b2) { return caz[a2] < caz[b2]; });
		auto sign_of = [](const std::string& t) -> double
		{
			if (t == "T4a" || t == "T5a" || t == "T4b" || t == "T5b") return +1.0;
			if (t == "T4c" || t == "T5c" || t == "T4d" || t == "T5d") return -1.0;
			return 0.0;
		};
		std::size_t nrew = 0, ncell = 0;
		for (xhqm::size i = 0; i < n; ++i)
		{
			const std::string t = (i < g_type.size()) ? g_type[i] : "";
			const double sg = sign_of(t);
			if (sg == 0.0 || !std::isfinite(caz[i])) continue;
			const double target = (double)caz[i] + sg * hrc_shift;
			bool touched = false;
			for (xhqm::size k = P.t->rp[i]; k < P.t->rp[i + 1]; ++k)
			{
				if (P.t->val[k] < 0.0f) continue;                 // 只动兴奋性慢腿
				const xhqm::size pre = P.t->col[k];
				const std::string pt = (pre < g_type.size()) ? g_type[pre] : "";
				auto it2 = st.find(pt);
				if (it2 == st.end() || it2->second.empty()) continue;
				const std::vector<xhqm::size>& lst = it2->second;
				std::size_t lo = 0, hi = lst.size() - 1;
				while (lo < hi)
				{
					const std::size_t mid = (lo + hi) / 2;
					if (caz[lst[mid]] < target) lo = mid + 1; else hi = mid;
				}
				std::size_t best = lo;
				if (lo > 0 && std::fabs(caz[lst[lo - 1]] - target) < std::fabs(caz[lst[lo]] - target)) best = lo - 1;
				P.t->col[k] = lst[best];
				++nrew; touched = true;
			}
			if (touched) ++ncell;
		}
		const double dtau = BIO.ch[cellsim::CH_NMDA].tau - BIO.ch[cellsim::CH_FAST_EXC].tau;
		std::printf("显式空间基线: Δc=%.2f° 重接 %zu 条慢腿边（%zu 个 T4/T5）| 符号 a/b=+、c/d=−\n",
			hrc_shift, nrew, ncell);
		std::printf("  匹配漂移速度 v = Δc/Δt = %.2f°/%.0fms = %.1f°/s；当前刺激 %.2f rad × %.2f Hz = %.1f°/s\n",
			hrc_shift, dtau * 1000, hrc_shift / dtau, stripe_period, stripe_hz,
			stripe_period * stripe_hz * 180.0 / 3.14159265358979323846);
	}
	// 噪声按等效电流加入（lif_step 内部就是 v += dt*I，故 I_noise = 膜电位扰动/dt）
	const float I_noise = (float)(noise_sd / dt);
	std::printf("背景噪声: 膜电位 σ=%.4f ⇒ 等效电流 σ=%.2f（fly64 的 OU 同量级）\n", noise_sd, I_noise);
	if (!inj_name.empty())
	{
		auto it = g_groups.find(inj_name);
		if (it == g_groups.end()) { std::fprintf(stderr, "没有组 %s\n", inj_name.c_str()); return 1; }
		std::printf("注入 %s（%zu 个神经元）电流 %.1f\n", inj_name.c_str(), it->second.size(), inj_amp);
	}

	// 噪声表：预生成 100 万个高斯样本，按 (神经元, 步) 散列取用，避免每步都调 randn
	const std::size_t NMASK = (1u << 20) - 1;
	std::vector<float> noise_tab(NMASK + 1);
	{
		std::normal_distribution<float> nd(0.0f, 1.0f);
		for (std::size_t i = 0; i <= NMASK; ++i) noise_tab[i] = nd(rng);
	}

	static std::atomic<long long> fired{ 0 };
	// 逐神经元的「有光相位 / 无光相位」发放累计：用来回答「光的信号在网络里到底走到多少神经元」。
	// 只看群体的平均值会掩盖掉「少数神经元被强烈调制」这种情况，逐神经元统计才看得清。
	std::vector<double> on_sum(n, 0.0), off_sum(n, 0.0);
	long long on_n = 0, off_n = 0;
	// ★ 平方和：为了算【经验方差】。
	// 原来标准误用伯努利方差 p(1−p)：对脉冲细胞（每步输出 0/1）正确，
	// 但对【分级细胞】（每步输出是 0~1 的连续量）不成立 —— 实测同一个量在校验时
	// 符号翻成 −333。用 Var(out)/n 才对两类细胞都成立。
	std::vector<double> on_sq(n, 0.0), off_sq(n, 0.0);
	// 滞后一阶乘积和：用来估自相关，进而折算有效样本数。
	// ★ 为什么必须有这一步（实测教训）：膜时间常数 100ms = 5 步，而我把每一步当独立样本，
	//   有效样本数被高估约 √5 倍 ⇒ z 被系统性放大 ⇒ 标称 3σ 实际只有 ~1.4σ。
	//   症状：注入 0 的对照也给出 ol_intrinsic 显著 7570 个、群体 d′ 437（本该接近 0）。
	//   换成随机块长标签也没用（437 对 410），所以成因是自相关不是周期锁相。
	std::vector<double> on_xy(n, 0.0), off_xy(n, 0.0);
	std::vector<double> on_sq2(n, 0.0), off_sq2(n, 0.0), on_xy2(n, 0.0), off_xy2(n, 0.0);
	std::vector<double> prev_out(n, 0.0);      // 上一步的输出（算滞后一阶乘积用）
	// 一个标签窗口的累计量（和 / 平方和 / 滞后一阶乘积和 / 步数）：打包传参，避免一长串数组。
	struct acc { const std::vector<double>* s; const std::vector<double>* q; const std::vector<double>* xy; double n; };
	// 均值方差的估计：经验方差 + AR(1) 自相关修正（有效样本数 n_eff = n(1−ρ)/(1+ρ)）。
	// 用经验方差而非伯努利方差 p(1−p)：后者假设每步输出 0/1，对分级细胞不成立
	// （用伯努利公式时分级链的 CV d′ 符号翻成 −333）。
	auto var_of_mean = [](const std::vector<double>& s_, const std::vector<double>& q_,
	                      const std::vector<double>& xy_, double cnt, xhqm::size i) -> double
	{
		if (cnt <= 2.0) return 0.0;
		const double m = s_[i] / cnt;
		const double v = std::max(q_[i] / cnt - m * m, 0.0);
		if (v <= 1e-15) return 0.0;
		const double rho = std::min(std::max((xy_[i] / cnt - m * m) / v, 0.0), 0.98);
		return v * ((1.0 + rho) / (1.0 - rho)) / cnt;
	};
	// 交叉验证用：把记录切成前后两半，各自累计。用一半估解码权重、在另一半上评估，
	// 否则 d' 会被乐观偏置（这正是前面犯过的错：同一份数据既估权重又评估）。
	std::vector<double> on_sum2(n, 0.0), off_sum2(n, 0.0);
	long long on_n2 = 0, off_n2 = 0;
	long long half_steps = 0;                 // 交叉验证的分界步（下面定完 steps 后赋值）
	const unsigned nthreads = std::max(1u, std::thread::hardware_concurrency());
	par_pool pool(nthreads);
	const long long steps = (long long)(sec / dt);
	half_steps = steps / 2;

	std::vector<double> rate_sum(n, 0.0);          // 每神经元累计发放次数
	// 每神经元累计【输出值】：对脉冲细胞 P.s∈{0,1}，对分级细胞 P.s∈[0,1] 是模拟量。
	// 均输出 = out_sum/steps 对两类细胞都成立；脉冲细胞乘 1/dt 就是发放率。
	// 为什么单独要它：rate_sum 只统计 P.s>0.5 的步数，分级链会算出 641Hz 这种不可能的「发放率」
	// （真实含义是「模拟输出有 64% 的步数高于 0.5」），那一列对分级细胞是误导。
	std::vector<double> out_sum(n, 0.0);
	// 稳态段（后半程）的输出累计：用来回答「是不是工作点被推高了」。
	// 与 PyTorch 参考实现的「稳态活动」同口径（那边取 x[200:] 的均值，即刺激之后的稳定段）；
	// 这里同样【跳过前半程】，把首帧/预热段分开记 —— 本项目栽过「首帧被当成稳态」的跟头。
	std::vector<double> steady_sum(n, 0.0);
	long long steady_n = 0;
	std::unordered_map<std::string, std::pair<double, double>> sc_stat;   // superclass → (发放累计, 细胞数)
	long long total_spikes = 0;
	double t_last = 0.0;
	std::vector<long long> hist(11, 0);            // 发放率直方图（Hz，0/1/2.../10+）
	static double g_vmax = 0.0, g_vmax_show = 0.0;
	static xhqm::size g_n99 = 0;
	static int g_light_on = -1;          // -1 = 无调制（恒定注入）
	std::ofstream csv;
	if (!log_csv.empty())
	{
		csv.open(log_csv, std::ios::binary);
		csv << "t,light,n_spike,motor_spikes,motor_n,dn_spikes,dn_n,light_sum,motor_lp,dn_lp,dn_wlp,motor_wlp" << std::endl;
	}
	// 每个注入细胞的当前光强 + 由实测 soma 坐标算出的方位角（stripe 图案用）
	bool g_balanced_neutral = false;      // 等长窗口设计下需要丢弃的中性段
	double g_drift_phase = 0.0;           // 反向光栅的累积相位（保证方向反转时不跳变）
	std::vector<float> g_light(n, 0.0f);
	std::vector<float> g_adapt(n, 0.0f);        // 光适应状态（每光感受器的近期平均亮度）
	std::vector<double> g_az(n, 0.0), g_el(n, 0.0);
	// ── 反向光栅的方向标签：块长随机 + 每个半程两个方向严格 50/50 ──
	//
	// ★ 为什么必须改掉固定周期翻转（原实现 rdir = fmod(t·stripe_hz,1)<0.5 ? +1 : −1）：
	//   实测症状：注入 80 的 ol_intrinsic d′ = 304.1，把注入幅度改成 0 之后 = 304.0 —— 逐位相同，
	//   即那个 d′ 与刺激无关。交叉验证抓不住它（两半各自内部自洽），打乱标签也抓不住。
	//   原因有两条，第二条是关键：
	//     1) 标签的块长由 stripe_hz 的默认值 4.0 决定 ⇒ 只有 0.125s，比网络时间常数（τ=100ms）还短，
	//        网络根本跟不上，「方向」在神经活动里几乎不存在，剩下的只是标签自身的周期结构。
	//        反转块长必须与漂移速度分开（--rev-block，默认 0.8s ≫ τ）。
	//     2) 那个块长是确定性的、且与步网格可通约，于是解码器能把「步序号」本身解出来
	//        —— 这是一个人造的不变量，与刺激无关，所以幅度 0 时同样出现。
	//   修法：块长在均值 ±50% 内随机（不与任何固定周期锁相）+ 每个半程两个方向的总时长严格相等。
	//   ★ 另一处坑：交错时遇到 0 长段不能 break。第一版写了 break，结果剩下的步全留成默认 +1，
	//     打印出来第 1 半程是 +1 250 步 / −1 0 步。（所以现在把平衡打印出来当判据，不是靠推理。）
	std::vector<signed char> rev_dir;
	// ★ step 也用同一套随机块长。原来 step 的标签是固定周期方波（fmod(t·mod_hz,1)<0.5，
	//   块长恰好 1 秒、与步网格可通约）—— 这正是第 3 类错误（周期标签与网络自身振荡锁相），
	//   我在 reverse 上修过但漏了 step。之所以当时没发现，是因为旧的伯努利方差统计量
	//   对分级细胞太保守，把对照的 d′ 压成了 0.0；换成经验方差后对照立刻给出 d′ 410.5。
	if (pattern == "reverse" || pattern == "step")
	{
		rev_dir.assign((std::size_t)steps, +1);
		auto urand01 = [&]() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); };
		for (int half = 0; half < 2; ++half)
		{
			const long long b0 = (half == 0) ? 0 : half_steps;
			const long long b1 = (half == 0) ? half_steps : steps;
			const long long bspan = b1 - b0;
			if (bspan < 4) continue;
			const long long per = bspan / 2;                       // 每个方向恰好这么多步
			const long long mean = std::max<long long>(1, (long long)(rev_block / dt));
			auto make_segs = [&](long long total)
			{
				std::vector<long long> out;
				while (total > 0)
				{
					long long len = (long long)((double)mean * (0.5 + urand01()));
					if (len < 1) len = 1;
					if (len > total) len = total;
					out.push_back(len);
					total -= len;
				}
				return out;
			};
			std::vector<long long> seg[2] = { make_segs(per), make_segs(per) };
			std::size_t p0 = 0, p1 = 0;
			long long pos = b0, nrev = 0, prev_dir = 0;
			while ((p0 < seg[0].size() || p1 < seg[1].size()) && pos < b1)
			{
				const bool take0 = (p0 >= seg[0].size()) ? false
					: (p1 >= seg[1].size() ? true : (urand01() < 0.5));
				const int d = take0 ? 0 : 1;
				const long long len = take0 ? seg[0][p0++] : seg[1][p1++];
				if (len <= 0) continue;
				const long long e = std::min(b1, pos + len);
				for (long long q = pos; q < e; ++q) rev_dir[(std::size_t)q] = (d == 0) ? +1 : -1;
				if (prev_dir != 0 && d != prev_dir) ++nrev;
				prev_dir = d;
				pos = e;
			}
			while (pos < b1) { rev_dir[(std::size_t)pos] = (urand01() < 0.5) ? +1 : -1; ++pos; }   // 奇数步的余数
			long long cp = 0, cm = 0;
			for (long long q = b0; q < b1; ++q) { if (rev_dir[(std::size_t)q] > 0) ++cp; else ++cm; }
			std::printf("  方向标签 第%d半程: +1 %lld 步 / −1 %lld 步（差 %lld）| 反转 %lld 次，均块长 %.2fs（%.0f 步）\n",
				half + 1, cp, cm, cp - cm, nrev, rev_block, (double)mean);
		}
		std::printf("  ★ 判据：两个半程的「差」都必须 ≤1 步。差大就说明占空比不平衡，会给出与刺激无关的假 d′。\n");
	}
	// 解剖学读出权重：每个神经元的出边突触总数（它投给下游的解剖学强度）。
	// 用固定权重而不是从数据拟合：拟合权重没有因果性、也不代表真实下游接线。
	// 固定权重下的 d' 无需交叉验证（没有被数据调过）。
	std::vector<float> g_outw(n, 1.0f);
	{
		std::ifstream f(dir + "\\out_abs.bin", std::ios::binary);
		if (f)
		{
			f.read((char*)g_outw.data(), (std::streamsize)(n * sizeof(float)));
			std::printf("读出权重: 已载入出边突触总数（解剖学权重）\n");
		}
		else std::printf("读出权重: 未找到 out_abs.bin，退化为等权\n");
	}
	double g_light_sum = 0.0;
	float g_motor_lp = 0.0f, g_dn_lp = 0.0f;      // 肌肉级低通读出（诊断/日志用）
	float g_dn_wlp = 0.0f, g_mot_wlp = 0.0f;      // 解剖学加权（出边突触总数）的低通读出
	if (!inj_name.empty())
	{
		const std::vector<xhqm::size>& g = g_groups[inj_name];
		double cx = 0.0, cy = 0.0; std::size_t cnt = 0;
		for (xhqm::size i : g)
			if (i < g_cx.size() && (g_cx[i] != 0.0f || g_cy[i] != 0.0f)) { cx += g_cx[i]; cy += g_cy[i]; ++cnt; }
		if (cnt) { cx /= (double)cnt; cy /= (double)cnt; }
		for (xhqm::size i : g)
		{
			if (i >= n) continue;
			// 方位/仰角取自光学柱编号。h1 水平、h2 垂直；左右眼朝向相反。
			// 角度映射是近似配准（与 ornata/fly 同一量级的口径），不是实测光学轴：
			// 单眼水平约 143.5°（左眼 −135°→+8.5°），垂直约 ±72°。
			const int h1 = (i < g_h1.size()) ? g_h1[i] : 0;
			const int h2 = (i < g_h2.size()) ? g_h2[i] : 0;
			const bool left = (i < g_side.size() && g_side[i] == "left");
			const double u = (h1 - 2) / 34.0;      // h1 ∈ [2,36]
			const double v = (h2 - 1) / 38.0;      // h2 ∈ [1,39]
			const double az_deg = left ? (-135.0 + u * 143.5) : (8.5 - u * 143.5);
			g_az[i] = az_deg * 3.14159265358979323846 / 180.0;
			g_el[i] = (72.0 - v * 144.0) * 3.14159265358979323846 / 180.0;
		}
		std::printf("注入图案 %s | 有坐标的注入细胞 %zu | 幅度 %.1f\n", pattern.c_str(), cnt, inj_amp);
		if (pattern == "flicker") std::printf("  flicker 切换率 %.1f Hz\n", flicker_hz);
		if (pattern == "stripe") std::printf("  stripe %.1f Hz 空间周期 %.2f rad\n", stripe_hz, stripe_period);
		if (pattern == "reverse") std::printf("  reverse %.1f Hz 空间周期 %.2f rad 条纹方向 %.0f°（标签=漂移方向）\n", stripe_hz, stripe_period, grating_deg);
		if (adapt_tau > 0.0) std::printf("  光适应: tau=%.2fs c0=%.2f（注入 = 对比度而非绝对亮度）\n", adapt_tau, adapt_c0);
	}
	const std::vector<xhqm::size>* mot = nullptr;
	const std::vector<xhqm::size>* dnv = nullptr;
	{
		auto it = g_groups.find("motor_all"); if (it != g_groups.end()) mot = &it->second;
		auto it2 = g_groups.find("descending_all"); if (it2 != g_groups.end()) dnv = &it2->second;
	}
	auto wsum = [&](const std::vector<xhqm::size>* v)
	{
		double s = 0.0;
		if (v) for (xhqm::size i : *v) if (i < n) s += (double)g_outw[i];
		return (s > 0.0) ? s : 1.0;
	};
	const double dn_wtot = wsum(dnv), mot_wtot = wsum(mot);

	auto clk = []() { return std::chrono::duration<double>(std::chrono::high_resolution_clock::now().time_since_epoch()).count(); };
	const double t0 = clk();

	for (long long st = 0; st < steps; ++st)
	{
		std::fill(P.cur.begin(), P.cur.end(), I_bg);
		// 背景电流逐神经元（tau 异构时保证静息 v = m*V_th 一致）
		for (xhqm::size i = 0; i < n; ++i) P.cur[i] = I_bg_i[i];
		if (I_noise > 0.0f)
		{
			const unsigned h = (unsigned)(st * 2654435761u);
			for (xhqm::size i = 0; i < n; ++i)
				P.cur[i] += I_noise * noise_tab[(std::size_t)((h + (unsigned)i * 2246822519u) & NMASK)];
		}
		if (!inj_name.empty())
		{
			// 光注入 = 光强（0~1）× 幅度。果蝇视觉靠「光的变化」而不是「光的强弱」：
			// 光感受器是组胺能（抑制性），稳态光照只会持续压制下游；
			// 只有光照变化时，抑制的加/撤才会在下游产生可传播的瞬态。
			// 因此这里提供三种时序图案，让网络自己承担 ON/OFF 的分离：
			//   step    : 全局方波（最简的"有光/无光"）
			//   flicker : 每个光感受器独立随机通断 —— 最强的时序对比
			//   stripe  : 用实测 soma 坐标算出的方位角做移动条纹 —— 真实运动刺激
			const unsigned h2 = (unsigned)(st * 2654435761u);
			// 反向光栅：方向标签来自随机块长序列（见主循环前的构造与说明）。
			// 相位用累积量保证方向反转时不跳变。
			const int rdir = (pattern == "reverse" && !rev_dir.empty())
				? (int)rev_dir[(std::size_t)std::min<long long>(st, (long long)rev_dir.size() - 1)]
				: ((std::fmod((st + 1) * (double)dt * stripe_hz, 1.0) < 0.5) ? 1 : -1);
			if (pattern == "reverse")
				g_drift_phase += rdir * 2.0 * 3.14159265358979323846 * stripe_hz * (double)dt;
			// 记忆/自持测试：--stim-off-after <秒> 之后停止注入，看网络是否留下与刺激相关的状态。
			// 三种可能，必须能分开：(a) 活动衰减回基线 ⇒ 无记忆（环路增益 <1）
			// (b) 活动自持且【与刺激相关】⇒ 选择性吸引子（正是要的）
			// (c) 活动自持但与刺激无关、各超类同频 ⇒ 饱和假象（实测踩过一次：全网 85% 每步发放、
			//     所有超类恰好同频，那是环路增益 >1 的退化形态，不携带信息）
			const bool stim_on = (stim_off_after <= 0.0) || ((double)(st + 1) * dt < stim_off_after);
			if (stim_off_after > 0.0 && !stim_on && !g_stim_off_printed)
			{
				g_stim_off_printed = true;
				std::printf("[t=%.2fs] ★ 刺激已停止注入（--stim-off-after %.2f）—— 之后的活动全部来自网络内部\n",
					(double)(st + 1) * dt, stim_off_after);
			}
			for (xhqm::size i : g_groups[inj_name])
			{
				if (i >= P.cur.size()) continue;
				float lt = 1.0f;
				if (pattern == "step")
					lt = (!rev_dir.empty() && st < (long long)rev_dir.size())
						? ((rev_dir[(std::size_t)st] > 0) ? 1.0f : 0.0f)     // 随机块长，避免周期锁相
						: ((mod_hz > 0.0) ? ((std::fmod((st + 1) * (double)dt * mod_hz, 1.0) < 0.5) ? 1.0f : 0.0f) : 1.0f);
				else if (pattern == "flicker")
				{
					if (noise_tab[(std::size_t)((h2 + (unsigned)i * 2246822519u) & NMASK)] * 0.5f + 0.5f
						< (float)(flicker_hz * dt)) g_light[i] = 1.0f - g_light[i];
					lt = g_light[i];
				}
				else if (pattern == "stripe")
					lt = 0.5f + 0.5f * (float)std::sin(g_az[i] * (2.0 * 3.14159265358979323846 / stripe_period)
						- 2.0 * 3.14159265358979323846 * stripe_hz * (st + 1) * (double)dt);
				else if (pattern == "loom")
				{
					// 逼近（looming）刺激：视野中心一个暗盘随时间扩张。
					// 这是 DNp03/DNp04/DNp05 这类视觉下行神经元真正的诱发刺激——
					// 用全局亮度阶跃去测它们是找错了钥匙（实测视觉占比最高的那批 DN
					// 对全局闪光的可解码性反而更低，就是这个原因）。
					const double r = std::sqrt(g_az[i] * g_az[i] + g_el[i] * g_el[i]);
					const double ph = std::fmod((st + 1) * (double)dt * stripe_hz, 1.0);
					const double R0 = loom_max * 3.14159265358979323846 / 180.0;
					// 逼近 = 暗盘【扩张】：R 从 0 涨到 R0。原先写成 R0*(1-ph/0.8) 是收缩（反向）。
					const double R = (ph < 0.8) ? (R0 * (ph / 0.8)) : 0.0;
					lt = (r > R) ? 1.0f : 0.0f;
				}
				else if (pattern == "reverse")
				{
					// 全场反向光栅：整幅光栅先朝一个方向漂移、再朝反方向漂移，周期反转。
					// 这是方向选择性（T4/T5）的经典刺激，而且【全场覆盖】——
					// 前面实测局部刺激（覆盖 2.7% 视野）每级衰减 7~10 倍传不动、
					// 全场方波能传但没有任何时空结构。反向光栅同时满足两者。
					// 相位用累积量以保证方向反转时不跳变。
					const double k = 2.0 * 3.14159265358979323846 / stripe_period;
					const double proj = g_az[i] * std::cos(grating_deg * 3.14159265358979323846 / 180.0)
						+ g_el[i] * std::sin(grating_deg * 3.14159265358979323846 / 180.0);
					lt = 0.5f + 0.5f * (float)std::sin(k * proj - g_drift_phase);
				}
				else if (pattern == "grating")
				{
					// 真正的移动光栅：相位沿视场内的一个方向线性变化（所以是空间结构化的），
					// 整体随时间平移（所以有真实运动）。这才是方向选择性通路（T4/T5）该看的刺激。
					// stripe_period 单位是弧度，stripe_hz 是条纹漂移频率。
					const double k = 2.0 * 3.14159265358979323846 / stripe_period;
					const double proj = g_az[i] * std::cos(grating_deg * 3.14159265358979323846 / 180.0)
						+ g_el[i] * std::sin(grating_deg * 3.14159265358979323846 / 180.0);
					lt = 0.5f + 0.5f * (float)std::sin(k * proj
						- 2.0 * 3.14159265358979323846 * stripe_hz * (st + 1) * (double)dt);
				}
				g_light[i] = lt;
				// 光适应：注入电流正比于「相对近期平均亮度的对比度」，而不是绝对亮度。
				// 稳态光会被适应掉（只剩瞬态），持续交替的空间图样则能维持。
				float drive = lt;
				if (adapt_tau > 0.0)
				{
					const float alpha = (float)(dt / adapt_tau);
					g_adapt[i] += (lt - g_adapt[i]) * alpha;
					float c = (lt - g_adapt[i]) / (g_adapt[i] + (float)adapt_c0);
					if (c > 1.0f) c = 1.0f;
					if (c < -1.0f) c = -1.0f;
					drive = c;
					g_light[i] = c;
				}
				// 分级光感受器：它的输出正比于光强（0~1），所以注入电流按「驱动 1.0 恰好把分级输出
				// 顶到 1.0」标定，而不是沿用脉冲模式那个一步就把量程打穿的幅度（inj_amp=80 一步
				// 给 v 加 1.6，而分级量程只有 0.52 ⇒ 输出饱和成常量，等于没有视觉信号）。
				// ★ 必须保留 inj_amp 的比例：第一版写成 P.cur[i] = photo_i_cal*drive 漏掉了幅度，
				//   结果注入 80 与注入 0 逐位相同、幅度 0 对照彻底失效。这里把 inj_amp=80 定义为满光。
				if (i < n && i < g_sensory.size() && g_sensory[i])
					P.cur[i] = stim_on ? (photo_i_cal * drive * ((float)inj_amp / 80.0f)) : 0.0f;
				else
					P.cur[i] += stim_on ? ((float)inj_amp * drive) : 0.0f;
			}
			g_light_sum = 0.0;
			for (xhqm::size i : g_groups[inj_name]) if (i < n) g_light_sum += g_light[i];
			// 刺激相位标签（解码分析要用）。注意不能一律按「平均亮度」判：
			// loom 的暗盘只占视野一小块，平均亮度几乎恒定，那样标签会失效（实测该表直接不打印）。
			if (pattern == "loom")
			{
				const double ph2 = std::fmod((st + 1) * (double)dt * stripe_hz, 1.0);
				// ★ 相位窗口必须【等长】（balanced）。实测教训：
				//   80/20 这种不平衡的窗口会让「网络自身的慢漂移」在两个字相位上产生系统性差别，
				//   于是交叉验证 d' 看起来很大（motor 62），但把注入幅度改成 0 后数字一模一样
				//   —— 整个结果是假的。打乱标签对照抓不住它（打乱恰好把占空比变成 50/50）。
				//   唯一可靠的做法是：等长窗口 + 幅度 0 对照。
				g_light_on = (ph2 >= 0.40 && ph2 < 0.80) ? 1 : 0;    // 两窗口各占 40%
				g_balanced_neutral = !(ph2 < 0.80);
			}
			else if (pattern == "reverse")
			{
				// 标签 = 漂移方向（+1 / −1）。时长的 50/50 由主循环前的随机块长序列保证
				// （不能靠「各占一半时间，天然等长」——固定周期时它不成立，那正是假 d′ 的来源）。
				g_light_on = (rdir > 0) ? 1 : 0;
				g_balanced_neutral = false;
			}
			else g_light_on = (g_light_sum > 0.5 * (double)g_groups[inj_name].size()) ? 1 : 0;
			// 对照：把相位标签打乱（与刺激完全无关）。若解码流程无偏，此时 d' 应≈0。
			// 这是判断「交叉验证 d' 是不是被流程缺陷虚高」的关键对照，必做。
			if (label_random && (((unsigned)(st * 2654435761u) >> 13) & 1u)) g_light_on = 1 - g_light_on;
		}
		if (use_bio || use_cur)
		{
			// ★ 对齐：bio 的漏电直接拉向 v_rest = bg_m*V_th，所以外部电流里【不能】再放 I_bg_i，
			//   否则静息被顶到两倍。减掉之后零输入稳态与 LIF 严格相同（见 cell_backend.h 文件头）。
			//   电流型模式（cur）更是要【全部去掉】背景电流与噪声：参考实现没有这两样，
			//   只留刺激电流本身（P.cur 里的注入部分），否则两边不是同一个模型。
			if (use_cur)
				for (xhqm::size i = 0; i < n; ++i) P.cur[i] -= (I_bg_i[i] + (I_noise * noise_tab[(std::size_t)(((unsigned)(st * 2654435761u) + (unsigned)i * 2246822519u) & NMASK)]));
			else
				for (xhqm::size i = 0; i < n; ++i) P.cur[i] -= I_bg_i[i];
			cellbackend::bio_step(BIO, n, P.t->rp.data(), P.t->col.data(), P.t->val.data(),
				P.cur.data(), P.s.data(), P.s_new.data(), [&](xhqm::size c, auto fn) { pool.run(c, fn); }, fired);
			P.s.swap(P.s_new);
		}
		else lif_step(P, pool, dt, tau, fired);
		// 逐神经元分相位累计（有光 / 无光）
		if (!inj_name.empty() && !g_balanced_neutral)
		{
			std::vector<double>& osum = (st < half_steps) ? on_sum : on_sum2;
			std::vector<double>& fsum = (st < half_steps) ? off_sum : off_sum2;
			std::vector<double>& osq = (st < half_steps) ? on_sq : on_sq2;
			std::vector<double>& fsq = (st < half_steps) ? off_sq : off_sq2;
			std::vector<double>& oxy = (st < half_steps) ? on_xy : on_xy2;
			std::vector<double>& fxy = (st < half_steps) ? off_xy : off_xy2;
			long long& a1 = (st < half_steps) ? on_n : on_n2;
			long long& a2 = (st < half_steps) ? off_n : off_n2;
			if (g_light_on)
			{
				for (xhqm::size i = 0; i < n; ++i)
				{ const double s2 = P.s[i]; osum[i] += s2; osq[i] += s2 * s2; oxy[i] += s2 * prev_out[i]; }
				++a1;
			}
			else
			{
				for (xhqm::size i = 0; i < n; ++i)
				{ const double s2 = P.s[i]; fsum[i] += s2; fsq[i] += s2 * s2; fxy[i] += s2 * prev_out[i]; }
				++a2;
			}
		}
		for (xhqm::size i = 0; i < n; ++i) prev_out[i] = P.s[i];
		const long long k = fired.load(std::memory_order_relaxed);
		total_spikes += k;
		// 注意：--quiet 只关每秒一行的进度输出，不能关统计累加
		// （曾把这里也套上 !quiet，导致所有汇总表变成 0）
		// ★ 只统计【脉冲】细胞的发放：分级细胞的 P.s 是 0~1 的模拟量，静息恰好 0.5，
		//   浮点微偏就会被 P.s>0.5 计成「发放」⇒ 总发放/均Hz 全是假的。
		//   实测被这个骗过两次：分级链下「总发放 800 万」其实脉冲细胞是 0.00 Hz。
		for (xhqm::size i = 0; i < n; ++i)
		{
			out_sum[i] += P.s[i];
			const bool graded = (i < BIO.p.size() && BIO.p[i].graded);
			if (!graded && P.s[i] > 0.5f) rate_sum[i] += 1.0;
		}
		// 稳态段（st >= half_steps）单独累计
		if (st >= half_steps)
		{
			for (xhqm::size i = 0; i < n; ++i) steady_sum[i] += P.s[i];
			++steady_n;
		}
		const double tnow = (st + 1) * dt;
		// 诊断：非注入群体的膜电位最高值 —— 直接看「驱动离阈值还差多少」，
		// 比只看发放数更能判断是量级不足还是被抑制压住。
		if (st % 5 == 0)
		{
			double vmax = 0.0;
			xhqm::size n99 = 0;
			for (xhqm::size i = 0; i < n; ++i)
			{
				if (P.v[i] > vmax) vmax = P.v[i];
				if (P.v[i] > 0.9 * V_th) ++n99;
			}
			if (vmax > g_vmax) g_vmax = vmax;
			g_vmax_show = vmax;
			g_n99 = n99;
		}
		// CSV 每步都写：折叠分析需要足够的时间分辨率（每 5 步写一次时
		// 采样间隔 100ms、Nyquist 5Hz，2Hz 以上的响应会被折叠成假零）
		{
			long long ms = 0, ds = 0;
			double mw = 0.0, dw = 0.0;
			if (mot) for (xhqm::size i : *mot) if (P.s[i] > 0.5f) { ++ms; mw += (double)g_outw[i]; }
			if (dnv) for (xhqm::size i : *dnv) if (P.s[i] > 0.5f) { ++ds; dw += (double)g_outw[i]; }
			// 肌肉级低通读出：真实肌肉对神经指令做时间积分，不可能是单步瞬时值。
			// 这与 fly64 的 13-tick(260ms) 滚动窗同性质，也等价于下游的一阶突触滤波。
			// 这是「读出」而非「干预」：网络内部一个字节都没动。
			const float a_lp = (float)(dt / lp_tau);
			g_motor_lp += ((float)ms - g_motor_lp) * a_lp;
			g_dn_lp += ((float)ds - g_dn_lp) * a_lp;
			g_dn_wlp += ((float)(dw / dn_wtot * (dnv ? dnv->size() : 1)) - g_dn_wlp) * a_lp;
			g_mot_wlp += ((float)(mw / mot_wtot * (mot ? mot->size() : 1)) - g_mot_wlp) * a_lp;
			csv << tnow << ',' << g_light_on << ',' << k << ',' << ms << ','
				<< (mot ? mot->size() : 0) << ',' << ds << ',' << (dnv ? dnv->size() : 0)
				<< ',' << g_light_sum << ',' << g_motor_lp << ',' << g_dn_lp
				<< ',' << g_dn_wlp << ',' << g_mot_wlp << std::endl;
		}
		if (tnow - t_last >= 1.0)
		{
			t_last = tnow;
			std::printf("[t=%5.1fs] 本步发放 %lld / %zu (%.3f%%)  累计 %lld  峰值 v=%.3f  近阈(v>0.9) %zu  实时倍速 %.1fx\n",
				tnow, k, n, 100.0 * (double)k / (double)n, total_spikes, g_vmax_show, g_n99,
				tnow / std::max(clk() - t0, 1e-9));
			if (k == 0) std::printf("           ★ 全网静默\n");
			else if ((double)k / (double)n > 0.9) std::printf("           ★ 全网同步发放（工作点过高）\n");
		}
	}
	const double wall = clk() - t0;

	// ── 汇总 ──
	double rs = 0; long long active = 0, over50 = 0;
	for (xhqm::size i = 0; i < n; ++i)
	{
		const double hz = rate_sum[i] / sec;
		rs += hz;
		if (hz > 0.0) ++active;
		if (hz > 50.0) ++over50;
		const int b = (int)std::min(10.0, std::floor(hz));
		++hist[b];
	}
	std::printf("\n══ 汇总 ══\n");
	std::printf("总发放 %lld  仿真 %.1fs 耗时 %.2fs（%.1fx 实时）  平均发放率 %.2f Hz/神经元\n",
		total_spikes, sec, wall, sec / std::max(wall, 1e-9), rs / (double)n);
	std::printf("有发放的神经元 %lld (%.1f%%)  发放率>50Hz 的 %lld (%.1f%%)\n",
		active, 100.0 * active / (double)n, over50, 100.0 * over50 / (double)n);
	std::printf("发放率直方图(Hz): ");
	for (int b = 0; b < 11; ++b) std::printf("%d%s:%lld ", b, (b == 10 ? "+" : ""), hist[b]);
	std::printf("\n");

	// 按 superclass 统计
	for (xhqm::size i = 0; i < n && i < g_superclass.size(); ++i)
	{
		auto& e = sc_stat[g_superclass[i]];
		e.first += rate_sum[i] / sec;
		e.second += 1.0;
	}
	std::vector<std::pair<std::string, std::pair<double, double>>> sc(sc_stat.begin(), sc_stat.end());
	std::sort(sc.begin(), sc.end(), [](const auto& a, const auto& b)
	{ return a.second.second > b.second.second; });
	std::printf("\n按 superclass（按细胞数排序）:\n");
	std::printf("  %-22s %8s %8s %10s\n", "superclass", "细胞", "总Hz", "均Hz");
	for (auto& kv : sc)
		std::printf("  %-22s %8.0f %8.1f %10.2f\n", kv.first.c_str(), kv.second.second,
			kv.second.first, kv.second.first / std::max(kv.second.second, 1.0));

	// 关键功能群
	std::printf("\n关键群发放率:\n");
	for (const char* g : { "visual_R1_8", "forward_DNg100", "turn_left_DNa02_DNg13",
	                       "turn_right_DNa02_DNg13", "jump_DNp01_DNp10", "motor_all", "descending_all" })
	{
		auto it = g_groups.find(g);
		if (it == g_groups.end()) continue;
		double s = 0, mx = 0;
		for (xhqm::size i : it->second) { const double hz = rate_sum[i] / sec; s += hz; mx = std::max(mx, hz); }
		std::printf("  %-24s n=%-6zu 均 %6.2f Hz  最大 %6.2f Hz\n", g, it->second.size(),
			s / std::max((double)it->second.size(), 1.0), mx);
	}

	// ── 逐神经元的光调制分布 ──
	if (on_n > 0 && off_n > 0)
	{
		struct md { xhqm::size i; double d; double p_on; double p_off; double se; };
		std::vector<md> v;
		v.reserve(n);
		// 逐神经元的标准误：经验方差 + AR(1) 自相关修正（见上面 var_of_mean 的说明）
		for (xhqm::size i = 0; i < n; ++i)
		{
			const double pon = on_sum[i] / (double)on_n, poff = off_sum[i] / (double)off_n;
			if (pon == 0.0 && poff == 0.0) continue;
			const double se_i = std::sqrt(std::max(
				var_of_mean(on_sum, on_sq, on_xy, (double)on_n, i)
				+ var_of_mean(off_sum, off_sq, off_xy, (double)off_n, i), 1e-12));
			v.push_back(md{ i, pon - poff, pon, poff, se_i });
		}
		std::sort(v.begin(), v.end(), [](const md& a, const md& b) { return std::fabs(a.d) > std::fabs(b.d); });
		int n3 = 0, n5 = 0;
		for (const md& e : v) { if (std::fabs(e.d) > 3 * e.se) ++n3; if (std::fabs(e.d) > 5 * e.se) ++n5; }
		std::printf("\n══ 逐神经元光调制（有光 %.0f 步 / 无光 %.0f 步，标准误按各自发放率算）══\n",
			(double)on_n, (double)off_n);
		std::printf("有发放的神经元 %zu；|Δ|>3σ 的 %d，>5σ 的 %d\n", v.size(), n3, n5);

		// ══ 与 PyTorch 参考实现（torch_bio_brain.py）同结构的对照汇总（--dend 扫描用）══
		// 每格 = 显著数 / 假阳性期望(对照显著数, 稳态平均活动)。
		// 内容全部复用上面【同一批修正后的统计量】（经验方差 + AR(1) 折算有效样本数、
		// 假阳性期望 = 有输出细胞数 × 0.27%、标签随机化对照），不另写一套口径。
		// 对照组 = 同一参数再跑一次加 --label-random；稳态活动 = 后半程平均输出。
		{
			std::printf("\n══ 秩-K 对照汇总（格式：显著数/假阳性期望(对照,R)  R=稳态平均输出）══\n");
			std::printf("%-24s %s\n", "superclass", "有输出  显著(>3σ)  假阳性期望  平均|Δ|  群体d'  稳态均输出");
			for (const char* L : { "ol_sensory", "ol_intrinsic", "visual_projection", "cb_intrinsic",
			                       "descending_neuron", "vnc_motor" })
			{
				long long nout = 0, nsig = 0;
				double dsum = 0.0, adsum = 0.0, ssum = 0.0;
				long long scnt = 0;
				for (const md& e : v)
				{
					if (e.i >= g_superclass.size() || g_superclass[e.i] != L) continue;
					++nout;                                   // 有输出（有光或无光相位非零）
					adsum += std::fabs(e.d);
					dsum += (e.d / e.se) * (e.d / e.se) - 1.0;
					if (std::fabs(e.d) > 3 * e.se) ++nsig;
				}
				for (xhqm::size i = 0; i < n && i < g_superclass.size(); ++i)
					if (g_superclass[i] == L)
					{
						ssum += (steady_n > 0) ? (steady_sum[i] / (double)steady_n) : 0.0;
						++scnt;
					}
				std::printf("%-24s %6lld  %8lld  %10.0f  %8.4f  %7.1f  %10.4f\n", L, nout, nsig,
					(double)nout * 0.0027, (nout > 0) ? adsum / (double)nout : 0.0,
					std::sqrt(std::max(0.0, dsum)), (scnt > 0) ? ssum / (double)scnt : 0.0);
			}
			// 全网合计：显著数 / 假阳性期望（对照组的这两个数由外层脚本从 --label-random 那一跑取）
			std::printf("%-24s %6zu  %8d  %10.0f  %8.4f  %7.1f  %10.4f\n", "全网",
				v.size(), n3, (double)v.size() * 0.0027,
				(v.empty() ? 0.0 : [&] { double s = 0; for (const md& e : v) s += std::fabs(e.d); return s / v.size(); }()),
				std::sqrt(std::max(0.0, [&] { double s = 0; for (const md& e : v) s += (e.d / e.se) * (e.d / e.se) - 1.0; return s; }())),
				(steady_n > 0) ? std::accumulate(steady_sum.begin(), steady_sum.end(), 0.0) / ((double)steady_n * (double)n) : 0.0);
			const std::string dend_tag = dend_on
				? ("K=" + std::to_string(dend_k) + (dend_identity ? " identity" : " relu"))
				: std::string("K=1 点神经元");
			std::printf("配置: 细胞模型=%s  syn_gain=%.1f  注入=%s 幅度=%.1f  图案=%s  树突=%s  标签随机化=%s\n",
				cell_model.c_str(), syn_gain, inj_name.c_str(), inj_amp, pattern.c_str(), dend_tag.c_str(),
				label_random ? "是（对照组）" : "否（信号组）");
			std::printf("判据：对照组（--label-random，与实验同参数同时长）的显著数必须 ≈ 假阳性期望；\n"
				"      否则这个量不含刺激信息。稳态均输出用来排除「只是工作点被推高」。\n");
		}
		// 按 superclass 汇总
		std::unordered_map<std::string, std::array<double, 3>> agg;   // 类型 → (个数, |Δ|和, 显著个数)
		for (const md& e : v)
		{
			const std::string sc = (e.i < g_superclass.size()) ? g_superclass[e.i] : "?";
			auto& a = agg[sc];
			a[0] += 1; a[1] += std::fabs(e.d); if (std::fabs(e.d) > 3 * e.se) a[2] += 1;
		}
		std::vector<std::pair<std::string, std::array<double, 3>>> ag(agg.begin(), agg.end());
		std::sort(ag.begin(), ag.end(), [](const auto& a, const auto& b) { return a.second[2] > b.second[2]; });
		std::printf("%-22s %8s %10s %10s %10s\n", "superclass", "有发放", "显著(>3σ)", "平均|Δ|", "群体 d'");
		// 群体 d'：Σ(z²−1) 再开方（独立假设下最优线性解码的信噪比，【扣掉噪声底】）
		// ★ 不能直接 Σz²：纯噪声下 E[z²]=1，于是 Σz²≈N、开方≈√N，
		//   这一项与刺激无关、却随细胞数增长 —— 实测 89275 个 ol_intrinsic 恒给出 ≈300
		//   （√89275 = 298.8；注入 80 得 304.1、注入 0 得 304.0，逐位相同，信号被完全淹掉）。
		//   这正是「幅度 0 对照」能抓出来的那类错误：对照不改数字，说明那个数字不含刺激信息。
		//   扣掉 N 之后才是真实群体信噪比：无信号 → 0，有信号 → 单调反映信息量。
		std::unordered_map<std::string, double> dprime;
		for (const md& e : v)
		{
			const std::string sc = (e.i < g_superclass.size()) ? g_superclass[e.i] : "?";
			const double z = e.d / e.se;
			dprime[sc] += z * z - 1.0;
		}
		for (auto& kv : ag)
			if (kv.second[2] > 0 || kv.second[0] > 200)
				std::printf("%-22s %8.0f %10.0f %10.4f %10.1f\n", kv.first.c_str(),
					kv.second[0], kv.second[2], kv.second[1] / kv.second[0],
					std::sqrt(std::max(0.0, dprime[kv.first])));

		// ── 交叉验证的线性解码器 ──
		// 用一半数据估权重 w_i = Δμ_i / σ_i²（最优线性判别），在另一半上评估 d'。
		// 这回答的是「光的信息能不能被一个下游读出器真正取出来」，
		// 且不受乐观偏置影响 —— 单细胞不显著但群体可分，是脉冲群体编码的常态。
		if (on_n > 0 && off_n > 0 && on_n2 > 0 && off_n2 > 0)
		{
			// 样本内统计用两半段合并（on_sum 只含前半段）
			std::vector<double> on_tot(n), off_tot(n);
			for (xhqm::size i = 0; i < n; ++i) { on_tot[i] = on_sum[i] + on_sum2[i]; off_tot[i] = off_sum[i] + off_sum2[i]; }
			// 统一的交叉验证评估：给定一个【布尔掩码】（超类筛选与指定群体都先化成掩码，
			// 避免在 n 的循环里做 std::find 变成 O(n·|ids|)）。
			// 权重用【经验方差】而非伯努利方差 —— 后者对分级细胞不成立。
			auto eval_cv = [&](const std::vector<double>& oA, const std::vector<double>& fA,
				const std::vector<double>& oAq, const std::vector<double>& fAq,
				const std::vector<double>& oAxy, const std::vector<double>& fAxy, double nA,
				const std::vector<double>& oB, const std::vector<double>& fB,
				const std::vector<double>& oBq, const std::vector<double>& fBq,
				const std::vector<double>& oBxy, const std::vector<double>& fBxy, double nB,
				const std::vector<char>& mask, double& dp_out) -> int
			{
				double delta = 0.0, var = 0.0;
				int cnt = 0;
				for (xhqm::size i = 0; i < n; ++i)
				{
					if (!mask[i]) continue;
					const double mA = oA[i] / nA, fA2 = fA[i] / nA;
					// 经验方差 + AR(1) 自相关修正
					const double sA2 = std::max(var_of_mean(oA, oAq, oAxy, nA, i)
						+ var_of_mean(fA, fAq, fAxy, nA, i), 1e-12);
					if (sA2 <= 1e-12) continue;
					const double w = (mA - fA2) / sA2;                 // 训练半段估的权重
					const double mB = oB[i] / nB, fB2 = fB[i] / nB;
					const double sB2 = std::max(var_of_mean(oB, oBq, oBxy, nB, i)
						+ var_of_mean(fB, fBq, fBxy, nB, i), 1e-12);
					delta += w * (mB - fB2);
					var += w * w * sB2;
					++cnt;
				}
				dp_out = (var > 1e-12) ? delta / std::sqrt(var) : 0.0;
				return cnt;
			};
			std::printf("\n══ 交叉验证线性解码（前半估权重 / 后半评估，反之亦然）══\n");
			std::printf("%-26s %8s %14s %14s\n", "群体", "神经元", "d'(半→半)", "d'(反向)");
			// 前几项按 superclass，后几项按「解剖学选出的群体」（视觉输入占比高的下行神经元）
			std::vector<std::pair<std::string, const std::vector<xhqm::size>*>> pops;
			for (const char* s : { "ol_sensory", "ol_intrinsic", "visual_projection", "cb_intrinsic",
			                       "descending_neuron", "vnc_motor" })
				pops.push_back(std::make_pair(std::string(s), (const std::vector<xhqm::size>*)nullptr));
			for (const char* g : { "dn_visual_10", "dn_visual_20", "dn_visual_30" })
			{
				auto it = g_groups.find(g);
				if (it != g_groups.end()) pops.push_back(std::make_pair(std::string("组:") + g, &it->second));
			}
			for (auto& pr : pops)
			{
				// 先把「超类筛选」或「指定群体」化成布尔掩码（O(n) 或 O(n+|ids|)，只做一次）
				std::vector<char> mask(n, 0);
				if (pr.second)
					for (xhqm::size i : *pr.second) if (i < n) mask[i] = 1;
				else
					for (xhqm::size i = 0; i < n; ++i)
						if (i < g_superclass.size() && g_superclass[i] == pr.first) mask[i] = 1;
				double d1 = 0, d2 = 0;
				const int c1 = eval_cv(on_sum, off_sum, on_sq, off_sq, on_xy, off_xy, (double)on_n,
					on_sum2, off_sum2, on_sq2, off_sq2, on_xy2, off_xy2, (double)on_n2, mask, d1);
				eval_cv(on_sum2, off_sum2, on_sq2, off_sq2, on_xy2, off_xy2, (double)on_n2,
					on_sum, off_sum, on_sq, off_sq, on_xy, off_xy, (double)on_n, mask, d2);
				std::printf("%-26s %8d %14.1f %14.1f\n", pr.first.c_str(), c1, d1, d2);
			}

			// ── 按【细胞类型】细分，把断点定位到具体哪一层 ──
			// superclass 太粗（ol_intrinsic 里混着 lamina L1-L5、medulla 各型、T4/T5、lobula），
			// 只有按类型看才知道信号是在哪一步掉的。
			{
				std::unordered_map<std::string, std::vector<xhqm::size>> bytype;
				for (xhqm::size i = 0; i < n; ++i)
					if (i < g_type.size() && !g_type[i].empty()) bytype[g_type[i]].push_back(i);
				std::vector<std::pair<std::string, std::vector<xhqm::size>>> tv(bytype.begin(), bytype.end());
				std::sort(tv.begin(), tv.end(), [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
				std::printf("\n── 按细胞类型（视觉相关，按数量排序前 24）──\n");
				// 加上【绝对发放率】与【有发放数】：如果某一型的细胞根本不发放，它的 Δ 必然为 0，
				// 那和权重/通道/细胞模型都无关。没有这两列时会误把「静默」当成「信号传不过去」。
				std::printf("%-16s %8s %10s %6s %9s %10s %14s %14s\n", "类型", "数量", "输出均值", "分级", "有输出", "平均|Δ|", "CV d'(半→半)", "CV d'(反向)");
				auto cvtype = [&](const std::vector<xhqm::size>& ids,
				                  const std::vector<double>& oA, const std::vector<double>& fA,
				                  const std::vector<double>& oAq, const std::vector<double>& fAq,
				                  const std::vector<double>& oAxy, const std::vector<double>& fAxy, double nA,
				                  const std::vector<double>& oB, const std::vector<double>& fB,
				                  const std::vector<double>& oBq, const std::vector<double>& fBq,
				                  const std::vector<double>& oBxy, const std::vector<double>& fBxy, double nB) -> double
				{
					double delta = 0.0, var = 0.0;
					for (xhqm::size i : ids)
					{
						if (i >= n) continue;
						const double mA = oA[i] / nA, fA2 = fA[i] / nA;
						const double sA2 = std::max(var_of_mean(oA, oAq, oAxy, nA, i)
							+ var_of_mean(fA, fAq, fAxy, nA, i), 1e-12);
						if (sA2 <= 1e-12) continue;
						const double w = (mA - fA2) / sA2;
						const double mB = oB[i] / nB, fB2 = fB[i] / nB;
						const double sB2 = std::max(var_of_mean(oB, oBq, oBxy, nB, i)
							+ var_of_mean(fB, fBq, fBxy, nB, i), 1e-12);
						delta += w * (mB - fB2); var += w * w * sB2;
					}
					return (var > 1e-12) ? delta / std::sqrt(var) : 0.0;
				};
				int shown = 0;
				for (auto& kv : tv)
				{
					if (kv.second.size() < 60) break;
					const std::string& t = kv.first;
					const bool vis = (t.size() >= 1 && (t[0] == 'L' || t[0] == 'T' || t[0] == 'D' ||
					                                    t.rfind("R", 0) == 0 || t.rfind("Mi", 0) == 0));
					if (!vis) continue;
					const double a1 = cvtype(kv.second, on_sum, off_sum, on_sq, off_sq, on_xy, off_xy, (double)on_n,
						on_sum2, off_sum2, on_sq2, off_sq2, on_xy2, off_xy2, (double)on_n2);
					const double a2 = cvtype(kv.second, on_sum2, off_sum2, on_sq2, off_sq2, on_xy2, off_xy2, (double)on_n2,
						on_sum, off_sum, on_sq, off_sq, on_xy, off_xy, (double)on_n);
					double os = 0.0; std::size_t nfire = 0; double ad = 0.0; int ngrad = 0;
					for (xhqm::size i : kv.second)
					{
						if (i >= n) continue;
						os += out_sum[i]; if (out_sum[i] > 0.0) ++nfire;
						if (i < BIO.p.size() && BIO.p[i].graded) ++ngrad;
						ad += std::fabs(on_sum[i] / (double)std::max(on_n, 1LL) - off_sum[i] / (double)std::max(off_n, 1LL));
					}
					const double ncell = (double)std::max<std::size_t>(kv.second.size(), 1);
					// 平均|Δ| 对【分级】细胞同样成立（CV d′ 的伯努利方差假设对连续输出不成立，
					// 实测会让符号翻成 −333；分级链请以 平均|Δ| 为准）。
					std::printf("%-16s %8zu %10.4f %6s %9zu %10.4f %14.1f %14.1f\n", t.c_str(), kv.second.size(),
						os / ncell / (double)std::max(steps, 1LL), (ngrad > 0) ? "是" : "否", nfire, ad / ncell, a1, a2);
					if (++shown >= 24) break;
				}
			}
		}
		std::printf("\n调制最强的前 20 个神经元:\n");
		for (std::size_t k = 0; k < v.size() && k < 20; ++k)
			std::printf("  #%-7zu %-22s %-16s 有光%.4f 无光%.4f Δ%+.4f (%.1fσ)\n",
				(std::size_t)v[k].i, (v[k].i < g_superclass.size() ? g_superclass[v[k].i].c_str() : "?"),
				(v[k].i < g_type.size() ? g_type[v[k].i].c_str() : "?"),
				v[k].p_on, v[k].p_off, v[k].d, std::fabs(v[k].d) / v[k].se);
	}
	return 0;
}
