// 数字果蝇全通路：FAFB 大脑（139k 神经元 / 3.87M 边）
//   + MANC 神经索（23.7k 神经元 / 530 万突触）+ FlyBody 身体（MuJoCo）+ fantasy 渲染
//
// 用法：fly_brain_manc_view <fafb_neurons.csv> <fafb_connections.csv> <fafb_cell_types.csv>
//                            <manc_neurons.csv> <manc_connections.csv> <fruitfly.xml>
//                            [--snapshot <png>] [--min-syn <n>]
//
// 通路：
//   FAFB 大脑（DN 输出按类型聚合）→ 配对表（primary_type ↔ MANC Primary Cell Type）
//     → MANC 下行神经元 → MANC 内部（530 万突触）→ motor 层 737 个
//       → 78 个 actuator → MuJoCo → fantasy
//
// 实测核对过的数据事实（决定了下面的映射方式）：
//   1. MANC 的 Flow 列是「纤维相对神经索的方向」，不是感觉/运动：
//        descending(1328) → afferent，ascending(1862)+motor(737) → efferent
//      真正的运动输出层是 Super Class = motor 的 737 个（平均 3464 入 / 56 出突触）：
//        229 ad / 144 fl / 131 hl / 121 ml / 66 wm / 24 nm / 16 hm / 6 xm
//      输出到肌肉是「肌肉级」的：Sub Class = MN-<神经束>-<肌肉名>（如 MN-LegNpT1-Ti_flexor），
//      每块肌肉左右两侧各一组，共 216 组；每侧映射到该侧的 DOF，不能双侧平均。
//   2. DN → motor 有 234,281 个突触，737 个 motor 神经元全部有 DN 输入
//   3. 感觉回灌打给 Super Class = sensory 的 5925 个（出/入突触比 3.67，纯输出型）
//   4. 配对键：FAFB consolidated_cell_types 的 primary_type ↔ MANC 的 Primary Cell Type
//      （DN 精确命中 198 个类型，覆盖 FAFB 506 个 / MANC 505 个 DN 个体）
#include "RhiViewport.h"
#include "vision_system.h"
#include "mushroom_body.h"
#include "../../lattice/sparse_mesh.hpp"

#include <mujoco/mujoco.h>

#include <QGuiApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
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
	std::vector<float> v, s, s_new, cur, th;
	netcsr* t = nullptr;          // 指向共享 CSR（蘑菇体可塑性需要从外部改其 val）
	xhqm::size n = 0;

	void init(xhqm::size count, std::mt19937& rng)
	{
		n = count;
		v.assign(n, 0.0f);
		s.assign(n, 0.0f);
		s_new.assign(n, 0.0f);
		cur.assign(n, 0.0f);
		// 阈值异质性：均匀驱动的确定性 LIF 会全网络同步放电（实测 139k/139k 同帧发放），
		// 给每个神经元一个不同的阈值才能形成发放率分布。
		th.resize(n);
		std::uniform_real_distribution<float> d(0.85f, 1.15f);
		for (xhqm::size i = 0; i < n; ++i) th[i] = d(rng);
	}
};

// 一步 LIF 膜电位更新（par_pool 并行）。cur 由调用方预先填好。
static std::atomic<long long> g_br_fired{ 0 }, g_cd_fired{ 0 };

static void lif_step(population& P, par_pool& pool, float dt, float tau, std::atomic<long long>& fired)
{
	const float dt_inv_tau = dt / tau;
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
			const float nv = P.v[i] + (0.0f - P.v[i]) * dt_inv_tau + dt * I;
			if (nv >= P.th[i]) { P.s_new[i] = 1.0f; P.v[i] = nv - P.th[i]; ++local; }
			else { P.s_new[i] = 0.0f; P.v[i] = nv; }
		}
		if (local) fired.fetch_add(local, std::memory_order_relaxed);
	});
	P.s.swap(P.s_new);
}

// ══════════════════ 全局状态 ══════════════════
static population g_br, g_cd;
static netcsr g_br_csr, g_cd_csr;   // 共享 CSR（g_br_csr.val 可被蘑菇体可塑性修改）
static std::vector<std::string> g_br_types;                  // 大脑细胞类型名（id → 名）
static std::vector<int> g_br_type_of;
static std::unordered_map<u64, xhqm::size> g_br_id2idx;   // FAFB root_id → 网络索引                        // 大脑神经元 → 类型 id
static std::vector<std::string> g_cd_pct;                    // MANC 神经元 → Primary Cell Type
static std::vector<xhqm::size> g_br_dn, g_cd_dn, g_cd_sensory;
static std::vector<xhqm::size> g_br_input;   // FAFB 感觉神经元（大脑输入层）
static std::vector<std::vector<xhqm::size>> g_cd_motor;      // motor 按 Class 分组
static std::vector<std::string> g_cd_motor_cls;
static std::vector<std::string> g_motor_key;   // 每个 MANC 神经元的分组键（Sub Class|Soma side）
static std::vector<int> g_motor_side;          // 每个 motor 组的侧别：-1 左 / +1 右 / 0 未知（步态用）

// 桥接：对每个 MANC 下行神经元，预存它接收的（大脑 DN 索引, 聚合权重）
static std::vector<xhqm::size> g_br_rp;
static std::vector<xhqm::size> g_br_src;
static std::vector<float> g_br_w;

// 突触增益：归一化权重乘它得到电流量级（brain_import / cord_import 共用）
static const float g_syn = 20.0f;

// 视觉子系统（FAFB 光感受器 → 小眼 → raycast → ON/OFF → 注入）
static vision_system g_vis;
static int g_head_body = -1;
static int g_free_dof = -1;       // freejoint 的 qvel 起始下标（判断前进/倒退）
static int g_body_up_body = -1;   // 用于判断身体倾斜（thorax）
static std::vector<bool> g_br_visual;                 // 该 FAFB 神经元是否视觉系
static std::vector<xhqm::size> g_pr_all, g_lam_all;   // 光感受器 / lamina 全部索引（统计用）
static std::vector<xhqm::size> g_t4_all, g_t5_all;   // T4 / T5 方向选择性细胞（运动通路输出）
static bool g_vis_enabled = true;     // 传入 --vis-off 可关闭视觉注入（做对照实验）
static bool g_vis_toggle = false;     // 传入 --vis-toggle 则每 8 秒自动开关，便于同一次运行内对照

// 蘑菇体学习（多巴胺调制可塑性）+ 场景气味源
static mushroom_body g_mb;
static int g_odor_geom = -1;          // 气味源 geom（场景物件；--odor-geom 指定）
static double g_odor_pos[3] = { 0.17, 0.0, 0.14 };   // 气味源世界坐标（XML 里 odor_source）
static bool g_odor_moving = false;    // 气味源是否来回移动（--odor-move）
static float g_odor_conc = 0.0f;      // 当前气味浓度（0~1，写入日志）
static double g_odor_side = 0.0;      // 气味相对头部的左右分量（>0 右），用于转向
static int g_odor_glom = -1;          // 当前激活的嗅小球通道
static float g_da_reward = 0.0f, g_da_punish = 0.0f;
static float g_reward_gain = 8.0f;    // 身体状态 → 多巴胺增益
static float g_odor_gain = 700.0f;    // 气味浓度 → ORN 电流增益

// 步态（中央模式发生器）：按侧别 + 节段给腿部 motor 组分配相位，
// 同侧前/后腿同相、中腿反相（这就是果蝇的三角步态），左右反相。
static bool g_gait_on = true;
static float g_gait_amp = 100.0f;      // 节律调制幅度（实测 40 时步态偏置达 -0.32m，12 时仅 -0.043）
static float g_gait_hz = 4.0f;        // 步频（6Hz 更接近真实果蝇步频，且偏置更小）
static float g_turn_gain = 1.0f;      // 气味方向 → 转向强度
// 转向方式：用**左右腿步态的不对称**（一侧摆幅大、另一侧小）产生转向力矩。
// 选这个是因为它是「能交给神经活动控制」的那一层——只要把 turn 信号换成神经元活动，
// 反应就由果蝇自己产生，而不是外部直接改速度/位置。
// ⚠ 之前用 xfrc_applied 直接推质心的方案已放弃：那是外部遥控，不符合
//   「果蝇自己控制身体对外界做出反应」的要求。
static float g_turn_amp = 0.5f;       // 左右摆幅差比例（0=不转向）
static float g_turn_bias = 0.5f;      // 左右腿整体偏置强度（一侧伸/一侧屈，产生净转向力矩）

// ── 转向指令的神经来源（关键：反应必须由果蝇自己的神经活动决定）──
// 用左右两侧嗅觉投射神经元(ALPN)的放电率差作为转向指令：
//   气味 → ORN → ALPN（左/右触角通路各自的神经活动）→ 腿部不对称 → 转向
// 这样从感觉到动作的链条全部落在网络内部，不是外部算几何量再遥控。
static float g_odor_side_neur = 0.0f;   // 由 ALPN 放电率差算出的转向指令
static float g_odor_side_gain_cd = 2.0f;   // 气味侧向 → 运动侧左右活动的注入增益
// 原默认 40：实测 raw 指令 ≈ 10~17，经 ±0.5 软饱和后恒被钉在端点、不再携带气味信息
// （且未限幅时会把步态打死）。实测 cd-gain=2 时 raw≈0.3~0.4，正好落在软饱和的线性区。
static bool g_turn_invert = false;      // 转向极性翻转（实机二分确定趋近/回避）
static float g_alpn_gain = 200.0f;      // 左右 motor 放电率差 → 转向指令的增益（放电率量级很小，需放大）
static bool g_turn_from_neurons = false;   // 转向指令是否来自神经活动（诊断/日志用）
// 转向指令限幅：下游是 amp_scale = 1±g_turn_amp·cmd、bias = g_turn_bias·cmd，
// 最后 ctrl 被夹到 ±1。实测 cmd≈17 时一侧步态正弦被翻号且放大 7.5 倍，腿被限幅打死
// （path 0.35→0.11、姿态奖励 5.7→1.7）。故在指令上做 tanh 软饱和，限到 ±0.5：
// 小信号区 tanh(x)≈x，正常调参手感不变；大信号区不再摧毁步态。
static const float g_turn_cmd_lim = 0.5f;
static const float g_turn_tau = 0.15f;  // 转向指令低通时间常数（秒）
static float g_turn_filt = 0.0f;        // 低通后的指令（过 tanh 前的直流分量）
static float g_turn_raw = 0.0f;         // 未低通未限幅的瞬时指令（诊断用）
// 步态相位偏移：实测「只开步态、不加推进力」时果蝇 30 秒漂移 -0.214m，
// 即步态本身的推进方向与世界 +x（气味源方向）相反；且步态力远大于后加的外力。
// 故整体加 π 翻转迈步方向（这也是步态调向量的最小改动）。
static double g_gait_phase = 3.14159265358979323846;
// 前进目标速度（m/s）：步态让腿迈步，这里给推进力。
// 已知待调：推力方向尚未与「朝向气味源」对齐——实测无论 fwd_sign 取 ±1，
// 位移都朝 -x（+1 幅度更大且会跃起 z→0.18，-1 温和），方向开关未如预期翻转。
// 需要进一步核对头部局部 +y 与世界坐标系的实际关系。目前默认 -1（温和、姿态稳）。
static float g_fwd_speed = 0.0f;
// 前进方向符号：实测果蝇沿「头部局部 +y」的正方向推会后退（位移朝 -x），
// 说明该模型的头部前向与世界坐标的关系与直觉相反 → 默认取 -1，实测朝 +x 前进。
static float g_fwd_sign = -1.0f;

// 分段计时（每帧累加秒数，用于定位性能瓶颈）
struct frame_time
{
	double fill = 0, vis = 0, lif_br = 0, cd_in = 0, lif_cd = 0, ctrl = 0, phys = 0, pose = 0;
	int n = 0;
};
static frame_time g_ft;

// ══════════════════ FAFB 大脑导入 ══════════════════
static bool brain_import(const char* neurons_path, const char* conn_path, const char* types_path,
	const char* cls_path, u64 min_syn)
{
	std::unordered_map<u64, xhqm::size>& id2idx = g_br_id2idx;
	id2idx.reserve(150000);
	{
		std::ifstream in(neurons_path);
		if (!in.is_open()) { std::fprintf(stderr, "cannot open %s\n", neurons_path); return false; }
		std::string line;
		std::getline(in, line);
		const int c_id = field_index(line, "root_id");
		if (c_id < 0) { std::fprintf(stderr, "FAFB neurons: no root_id column\n"); return false; }
		xhqm::size k = 0;
		while (std::getline(in, line))
		{
			const u64 rid = csv_u64(line, c_id);
			if (!rid || id2idx.count(rid)) continue;
			id2idx.emplace(rid, k++);
		}
	}
	const xhqm::size N = id2idx.size();
	std::mt19937 rng_br(20300303u);   // 固定种子，可复现
	g_br.init(N, rng_br);
	g_br_type_of.assign(N, -1);
	std::printf("FAFB: neurons=%zu\n", N);

	// 输入层：super_class = sensory 的神经元（从 classification.csv 读）。
	// 大脑本身没有外部输入就完全静息（背景电流 60 的稳态 0.6 < 阈值 0.85），
	// 所以喂它：感觉神经元从身体来（这里先用恒定驱动代表"有输入"）。
	g_br_input.clear();
	{
		std::ifstream in(cls_path);
		if (!in.is_open()) { std::fprintf(stderr, "cannot open %s\n", cls_path); return false; }
		std::string line;
		std::getline(in, line);
		const int c_id = field_index(line, "root_id");
		const int c_sc = field_index(line, "super_class");
		if (c_id < 0 || c_sc < 0) { std::fprintf(stderr, "classification: missing columns\n"); return false; }
		while (std::getline(in, line))
		{
			auto it = id2idx.find(csv_u64(line, c_id));
			if (it == id2idx.end()) continue;
			const std::string sc = csv_field(line, c_sc);
			if (sc == "sensory" || sc == "sensory_ascending")
				g_br_input.push_back(it->second);
		}
		std::printf("FAFB 输入层: sensory=%zu\n", g_br_input.size());
	}

	xhqm::sparse_mesh<u64, u64> g;
	for (xhqm::size i = 0; i < N; ++i) g.add_node((u64)i);
	{
		std::ifstream in(conn_path);
		if (!in.is_open()) { std::fprintf(stderr, "cannot open %s\n", conn_path); return false; }
		std::string line;
		std::getline(in, line);
		const int c_pre = field_index(line, "pre_root_id");
		const int c_post = field_index(line, "post_root_id");
		const int c_syn = field_index(line, "syn_count");
		if (c_pre < 0 || c_post < 0 || c_syn < 0) { std::fprintf(stderr, "FAFB conn: missing columns\n"); return false; }
		std::size_t rows = 0, kept = 0;
		while (std::getline(in, line))
		{
			++rows;
			const u64 syn = csv_u64(line, c_syn);
			if (syn < min_syn) continue;
			auto it = id2idx.find(csv_u64(line, c_pre));
			if (it == id2idx.end()) continue;
			auto jt = id2idx.find(csv_u64(line, c_post));
			if (jt == id2idx.end()) continue;
			g.add_edge(it->second, jt->second, syn);
			++kept;
		}
		std::printf("FAFB connections: %zu 行, 保留 %zu 条 (syn>=%llu)\n", rows, kept, (unsigned long long)min_syn);
	}
	g.rebuild_index();
	std::printf("FAFB graph: nodes=%zu nnz=%zu\n", g.size(), g.nnz());

	{
		std::ifstream in(types_path);
		if (!in.is_open()) { std::fprintf(stderr, "cannot open %s\n", types_path); return false; }
		std::string line;
		std::getline(in, line);
		const int c_id = field_index(line, "root_id");
		const int c_t = field_index(line, "primary_type");
		if (c_id < 0 || c_t < 0) { std::fprintf(stderr, "cell types: missing columns\n"); return false; }
		std::unordered_map<std::string, int> tids;
		std::size_t marked = 0;
		while (std::getline(in, line))
		{
			auto it = id2idx.find(csv_u64(line, c_id));
			if (it == id2idx.end()) continue;
			const std::string t = csv_field(line, c_t);
			if (t.empty()) continue;
			int id;
			auto ti = tids.find(t);
			if (ti == tids.end()) { id = (int)g_br_types.size(); tids.emplace(t, id); g_br_types.push_back(t); }
			else id = ti->second;
			g_br_type_of[it->second] = id;
			++marked;
		}
		std::printf("FAFB cell types: %zu 个神经元已标注, %zu 个类型名\n", marked, g_br_types.size());
	}

	std::vector<double> in_sum(N, 0.0), out_sum(N, 0.0);
	g.for_each_edge([&](xhqm::size, xhqm::size j, u64 v) { in_sum[j] += (double)v; });
	g.for_each_edge([&](xhqm::size i, xhqm::size, u64 v) { out_sum[i] += (double)v; });
	// 突触前归一化 + 显式增益：归一化本身丢掉了绝对尺度（单条突触变成 1/出度 ~ 0.001，
	// 太弱，实测信号传不过 2~3 跳）。这里每条突触 = g_syn * syn_count / 该神经元总输出，
	// 即一个神经元全发放时向下游贡献 g_syn 量级的电流。
	std::vector<std::pair<xhqm::size, xhqm::size>> e;
	std::vector<float> w;
	g.for_each_edge([&](xhqm::size i, xhqm::size j, u64 v)
	{
		if (out_sum[i] <= 0.0) return;
		e.emplace_back(i, j);
		w.push_back((float)(g_syn * (double)v / out_sum[i]));
	});
	g_br_csr.build(e, w, N);
	g_br.t = &g_br_csr;
	std::printf("FAFB transposed CSR: edges=%zu\n", e.size());
	return true;
}

// ══════════════════ MANC 神经索导入 ══════════════════
static bool cord_import(const char* neurons_path, const char* conn_path)
{
	std::unordered_map<u64, xhqm::size> id2idx;
	id2idx.reserve(26000);
	std::vector<std::string> sc_names, cls_names;
	std::unordered_map<std::string, int> sc_ids, cls_ids;
	std::vector<int> sc_of, cls_of;

	std::ifstream in(neurons_path);
	if (!in.is_open()) { std::fprintf(stderr, "cannot open %s\n", neurons_path); return false; }
	std::string line;
	std::getline(in, line);
	const int c_id = field_index(line, "Root ID");
	const int c_sc = field_index(line, "Super Class");
	const int c_cls = field_index(line, "Class");
	const int c_pct = field_index(line, "Primary Cell Type");
	const int c_sub = field_index(line, "Sub Class");
	const int c_side = field_index(line, "Soma side");
	if (c_id < 0 || c_sc < 0 || c_pct < 0) { std::fprintf(stderr, "MANC neurons: missing columns\n"); return false; }
	while (std::getline(in, line))
	{
		const u64 rid = csv_u64(line, c_id);
		if (!rid || id2idx.count(rid)) continue;
		const std::string sc = csv_field(line, c_sc);
		const std::string cl = (c_cls >= 0) ? csv_field(line, c_cls) : std::string();
		int sid;
		auto si = sc_ids.find(sc);
		if (si == sc_ids.end()) { sid = (int)sc_names.size(); sc_ids.emplace(sc, sid); sc_names.push_back(sc); }
		else sid = si->second;
		int cid;
		auto ci = cls_ids.find(cl);
		if (ci == cls_ids.end()) { cid = (int)cls_names.size(); cls_ids.emplace(cl, cid); cls_names.push_back(cl); }
		else cid = ci->second;
		id2idx.emplace(rid, sc_of.size());
		sc_of.push_back(sid);
		cls_of.push_back(cid);
		g_cd_pct.push_back(csv_field(line, c_pct));
		g_motor_key.push_back((c_sub >= 0 ? csv_field(line, c_sub) : std::string())
			+ "|" + (c_side >= 0 ? csv_field(line, c_side) : std::string()));
	}
	const xhqm::size N = sc_of.size();
	std::mt19937 rng_cd(20300303u);
	g_cd.init(N, rng_cd);
	std::printf("MANC: neurons=%zu\n", N);

	xhqm::sparse_mesh<u64, std::uint32_t> g;
	for (xhqm::size i = 0; i < N; ++i) g.add_node((u64)i);
	{
		std::ifstream in2(conn_path);
		if (!in2.is_open()) { std::fprintf(stderr, "cannot open %s\n", conn_path); return false; }
		std::string l2;
		std::getline(in2, l2);
		const int c_pre = field_index(l2, "pre_root_id");
		const int c_post = field_index(l2, "post_root_id");
		const int c_syn = field_index(l2, "syn_count");
		if (c_pre < 0 || c_post < 0 || c_syn < 0) { std::fprintf(stderr, "MANC conn: missing columns\n"); return false; }
		std::size_t kept = 0;
		while (std::getline(in2, l2))
		{
			auto it = id2idx.find(csv_u64(l2, c_pre));
			if (it == id2idx.end()) continue;
			auto jt = id2idx.find(csv_u64(l2, c_post));
			if (jt == id2idx.end()) continue;
			const u64 syn = csv_u64(l2, c_syn);
			if (!syn) continue;
			g.add_edge(it->second, jt->second, (std::uint32_t)syn);
			++kept;
		}
		std::printf("MANC connections: 保留 %zu 条\n", kept);
	}
	g.rebuild_index();
	std::printf("MANC graph: nodes=%zu nnz=%zu\n", g.size(), g.nnz());

	const int sc_desc = sc_ids.count("descending") ? sc_ids["descending"] : -1;
	const int sc_sens = sc_ids.count("sensory") ? sc_ids["sensory"] : -1;
	const int sc_mot = sc_ids.count("motor") ? sc_ids["motor"] : -1;
	std::unordered_map<std::string, int> cls2grp;
	std::size_t n_motor = 0;
	for (xhqm::size i = 0; i < N; ++i)
	{
		if (sc_of[i] == sc_desc) g_cd_dn.push_back(i);
		else if (sc_of[i] == sc_sens) g_cd_sensory.push_back(i);
		else if (sc_of[i] == sc_mot)
		{
			++n_motor;
			// 组键 = Sub Class + Soma side：同肌肉的左右两侧必须各自成组，
			// 否则左右腿会被同一信号驱动、无法行走
			auto it = cls2grp.find(g_motor_key[i]);
			if (it == cls2grp.end())
			{
				const int gi = (int)g_cd_motor.size();
				cls2grp.emplace(g_motor_key[i], gi);
				g_cd_motor.emplace_back();
				g_cd_motor_cls.push_back(g_motor_key[i]);
				g_cd_motor.back().push_back(i);
				// 侧别（步态需要左右反相）：从分组键尾部 "<Sub Class>|<side>" 取
				{
					const std::size_t b2 = g_motor_key[i].rfind('|');
					const std::string sd = (b2 == std::string::npos) ? std::string() : g_motor_key[i].substr(b2 + 1);
					g_motor_side.push_back(sd == "left" ? -1 : (sd == "right" ? 1 : 0));
				}
			}
			else g_cd_motor[it->second].push_back(i);
		}
	}
	std::printf("MANC: descending=%zu sensory=%zu motor=%zu (%zu 组)\n",
		g_cd_dn.size(), g_cd_sensory.size(), n_motor, g_cd_motor.size());

	std::vector<double> in_sum(N, 0.0), out_sum(N, 0.0);
	g.for_each_edge([&](xhqm::size, xhqm::size j, std::uint32_t v) { in_sum[j] += (double)v; });
	g.for_each_edge([&](xhqm::size i, xhqm::size, std::uint32_t v) { out_sum[i] += (double)v; });
	std::vector<std::pair<xhqm::size, xhqm::size>> e;
	std::vector<float> w;
	g.for_each_edge([&](xhqm::size i, xhqm::size j, std::uint32_t v)
	{
		// 与 FAFB 侧一致的突触前归一化 + 同一增益
		if (out_sum[i] <= 0.0) return;
		e.emplace_back(i, j);
		w.push_back((float)(g_syn * (double)v / out_sum[i]));
	});
	g_cd_csr.build(e, w, N);
	g_cd.t = &g_cd_csr;
	std::printf("MANC transposed CSR: edges=%zu\n", e.size());
	return true;
}

// ══════════════════ 配对表（类型级聚合）══════════════════
// 语义：同一类型下，FAFB 的 k 个 DN 输出先平均（各 1/k），
//       再均分给 MANC 的 m 个同名 DN（各 1/m）。
//       故单个 FAFB DN 对单个 MANC DN 的权重 = 1/(k*m)。
struct link { xhqm::size cd, br; float w; };

static void build_bridge()
{
	std::unordered_map<std::string, std::vector<xhqm::size>> cd_by_type;
	for (xhqm::size i : g_cd_dn)
		if (!g_cd_pct[i].empty()) cd_by_type[g_cd_pct[i]].push_back(i);

	std::unordered_map<int, std::vector<xhqm::size>> br_by_type;
	for (xhqm::size i = 0; i < g_br.n; ++i)
		if (g_br_type_of[i] >= 0) br_by_type[g_br_type_of[i]].push_back(i);

	std::unordered_map<int, float> type_w;
	std::size_t n_pair = 0, n_br = 0, n_cd = 0, n_edge = 0;
	for (auto& kv : br_by_type)
	{
		const std::string& t = g_br_types[kv.first];
		auto it = cd_by_type.find(t);
		if (it == cd_by_type.end()) continue;
		++n_pair;
		n_br += kv.second.size();
		n_cd += it->second.size();
		n_edge += kv.second.size() * it->second.size();
		type_w.emplace(kv.first, 1.0f / ((float)kv.second.size() * (float)it->second.size()));
	}

	std::vector<link> links;
	links.reserve(n_edge);
	g_br_dn.clear();
	for (auto& kv : br_by_type)
	{
		auto tit = type_w.find(kv.first);
		if (tit == type_w.end()) continue;
		auto it = cd_by_type.find(g_br_types[kv.first]);
		if (it == cd_by_type.end()) continue;
		const float w = tit->second;
		for (xhqm::size b : kv.second)
		{
			g_br_dn.push_back(b);
			for (xhqm::size tgt : it->second)
				links.push_back(link{ tgt, b, w });
		}
	}
	// 同 (cd, br) 合并（同类型内多重出现时权重累加）
	std::sort(links.begin(), links.end(), [](const link& a, const link& b)
	{
		return (a.cd != b.cd) ? (a.cd < b.cd) : (a.br < b.br);
	});
	std::vector<link> merged;
	merged.reserve(links.size());
	for (const link& L : links)
	{
		if (!merged.empty() && merged.back().cd == L.cd && merged.back().br == L.br)
			merged.back().w += L.w;
		else merged.push_back(L);
	}

	g_br_rp.assign(g_cd.n + 1, 0);
	for (const link& L : merged) ++g_br_rp[L.cd + 1];
	for (xhqm::size i = 1; i <= g_cd.n; ++i) g_br_rp[i] += g_br_rp[i - 1];
	g_br_src.resize(merged.size());
	g_br_w.resize(merged.size());
	std::vector<xhqm::size> cursor(g_br_rp.begin(), g_br_rp.end() - 1);
	for (const link& L : merged)
	{
		const xhqm::size o = cursor[L.cd]++;
		g_br_src[o] = L.br;
		g_br_w[o] = L.w;
	}
	std::printf("bridge: 配对类型=%zu | FAFB DN=%zu | MANC DN=%zu | 聚合边=%zu（去重后 %zu）\n",
		n_pair, n_br, n_cd, links.size(), merged.size());
}

// ══════════════════ MuJoCo / fantasy 侧 ══════════════════
static mjModel* g_m = nullptr;
static mjData* g_d = nullptr;
static std::vector<xhqm::fantasy::models*> g_body_models;
static const float g_scale = 50.0f;
static double g_center[3] = { 0, 0, 0 };
static bool g_follow = true;


static const float g_motor_gain = 4.0f;   // motor 相对静息偏离的放大倍数（静息零位在主循环里自适应）

static xhqm::fantasy::prefab* mesh_to_prefab(mjModel* m, int mesh_id, xhqm::rgba color)
{
	using namespace xhqm;
	using namespace xhqm::fantasy;
	const int vadr = m->mesh_vertadr[mesh_id], vnum = m->mesh_vertnum[mesh_id];
	const int fadr = m->mesh_faceadr[mesh_id], fnum = m->mesh_facenum[mesh_id];
	if (vnum <= 0 || fnum <= 0) return nullptr;
	prefab* p = new prefab();
	p->size((count)fnum);
	std::vector<std::array<point, 3>> pv(fnum);
	for (int f = 0; f < fnum; ++f)
		for (int k = 0; k < 3; ++k)
		{
			const int vi = m->mesh_face[3 * (fadr + f) + k];
			const float* v = m->mesh_vert + 3 * (vadr + vi);
			pv[f][k] = point((value)v[0], (value)v[1], (value)v[2]);
		}
	p->vertex(pv);
	if (m->mesh_normal)
	{
		std::vector<std::array<value, 3>> pn((std::size_t)fnum * 3);
		for (int f = 0; f < fnum; ++f)
			for (int k = 0; k < 3; ++k)
			{
				const int vi = m->mesh_face[3 * (fadr + f) + k];
				const float* n = m->mesh_normal + 3 * (vadr + vi);
				pn[(std::size_t)f * 3 + k] = { (value)n[0], (value)n[1], (value)n[2] };
			}
		p->normal(pn);
	}
	p->open_face(true); p->open_line(false); p->open_poit(false); p->open_light(true);
	p->color_face(color);
	return p;
}

static void quat_rotate(const double* q, double x, double y, double z, double& ox, double& oy, double& oz)
{
	const double w = q[0], qx = q[1], qy = q[2], qz = q[3];
	ox = (1 - 2 * (qy * qy + qz * qz)) * x + 2 * (qx * qy - qz * w) * y + 2 * (qx * qz + qy * w) * z;
	oy = 2 * (qx * qy + qz * w) * x + (1 - 2 * (qx * qx + qz * qz)) * y + 2 * (qy * qz - qx * w) * z;
	oz = 2 * (qx * qz - qy * w) * x + 2 * (qy * qz + qx * w) * y + (1 - 2 * (qx * qx + qy * qy)) * z;
}

static xhqm::fantasy::matrix body_world_matrix(int b)
{
	const double* xpos = g_d->xpos + 3 * b;
	const double* xmat = g_d->xmat + 9 * b;
	xhqm::fantasy::matrix mat(4, 4);
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			mat(i, j) = (float)xmat[3 * i + j] * g_scale;
	mat(0, 3) = (float)((xpos[0] - g_center[0]) * g_scale);
	mat(1, 3) = (float)((xpos[1] - g_center[1]) * g_scale);
	mat(2, 3) = (float)((xpos[2] - g_center[2]) * g_scale);
	mat(3, 3) = 1.0f;
	return mat;
}



// ══════════════════ 肌肉级映射 ══════════════════
// MANC 的 motor 神经元用「神经束 + 肌肉名」标注（Sub Class = MN-<神经束>-<肌肉名>），
// 如 MN-LegNpT1-Ti_flexor / MN-WTct-DLM_c-f / MN-UTct-hg3。
// 这里把「肌肉 → 该侧各 DOF」做成规则表，再按胸节（T1/T2/T3）与侧别展开成具体 actuator 名。
//
// 关键约束：MANC 每块肌肉在左右两侧各有一组同名神经元，而 MuJoCo 每条腿的 DOF 也分左右，
// 所以必须用 Soma side 精确映射，不能用双侧平均（否则左右腿会被同一信号驱动、无法行走）。
//
// 规则按数组顺序匹配，肌肉名精确命中首个即止（顺序敏感：Acc._ti_flexor 必须排在 Ti_flexor 之前，
// 否则会被前缀式规则抢走——虽然本表用精确匹配，仍按"特殊在前"排列以免日后改成前缀匹配时踩坑）。
struct muscle_rule
{
	const char* muscle;   // MANC 的肌肉名（精确匹配）
	const char* target;   // 目标：腿/haltere 为 DOF 名（%c = 侧别字母），其它为完整 actuator 名
	float weight;
	int scope;            // 0=腿（展开成 <dof>_T<n>_<side>）、1=翅（<dof> 带 %c）、2=haltere、3=完整名
};

// 模型真实的 78 个 actuator（actuator_dump 导出，不是照 XML 文本猜的）：
//   head=14（含 1 个 free 根关节执行器则是 15）  wing=6  abdomen=2
//   legT1/T2/T3 各 18、18、16：每条腿每侧 8 个 DOF（coxa_abduct/coxa_twist/coxa/
//     femur_twist/femur/tibia/tarsus/tarsus2）
//   末尾 8 个 adhere_* 是黏附器（唇瓣/爪），非肌肉
static const muscle_rule g_rules[] =
{
	// —— 腿：<dof>_T<n>_<side> ——
	{ "Acc._ti_flexor",                 "tibia",          0.6f, 0 },
	{ "Acc._tr_flexor",                 "femur",          0.6f, 0 },
	{ "Ti_flexor",                      "tibia",          1.0f, 0 },
	{ "Ti_extensor",                    "tibia",          1.0f, 0 },
	{ "Tr_flexor",                      "femur",          1.0f, 0 },
	{ "Tr_extensor",                    "femur",          1.0f, 0 },
	{ "Tergotr.",                       "femur_twist",    1.0f, 0 },
	{ "Sternotrochanter",               "femur_twist",    1.0f, 0 },
	{ "TTM",                            "femur_twist",    1.0f, 0 },
	{ "Fe_reductor",                    "coxa_twist",     1.0f, 0 },
	{ "Sternal_anterior_rotator",       "coxa_twist",     1.0f, 0 },
	{ "Sternal_posterior_rotator",      "coxa_twist",     1.0f, 0 },
	{ "Sternal_adductor",               "coxa",           1.0f, 0 },
	{ "Pleural_remotor/abductor",       "coxa_abduct",    1.0f, 0 },
	{ "Tergopleural/Pleural_promotor",  "coxa_abduct",    0.8f, 0 },
	{ "Ta_depressor",                   "tarsus",         1.0f, 0 },
	{ "Ta_levator",                     "tarsus",         1.0f, 0 },
	{ "front_leg",                      "coxa_abduct",    0.5f, 0 },
	{ "middle_leg",                     "coxa_abduct",    0.5f, 0 },
	{ "hind_leg",                       "coxa_abduct",    0.5f, 0 },
	// —— 翅：间接飞行肌；模型里翅是简化驱动（gainprm 直驱关节） ——
	{ "DLM_a,_b",                       "wing_pitch_%c",  1.0f, 1 },
	{ "DLM_c-f",                        "wing_pitch_%c",  1.0f, 1 },
	{ "DVM_1a-c",                       "wing_roll_%c",   1.0f, 1 },
	{ "DVM_2a,_b",                      "wing_roll_%c",   1.0f, 1 },
	{ "DVM_3a,_b",                      "wing_roll_%c",   1.0f, 1 },
	{ "wing",                           "wing_yaw_%c",    1.0f, 1 },
	// haltere：模型的 78 个 actuator 里没有 haltere（actuator_dump 核实），
	// 故 wing/haltere 肌一律不映射 haltere；翅的偏航由 tergopleural 系承担
	// —— 头部 / 颈部 ——
	{ "neck",                           "head",           1.0f, 3 },
	{ "TH1",                            "head",           1.0f, 3 },
	{ "TH2",                            "head_twist",     1.0f, 3 },
	{ "AD",                             "head_abduct",    1.0f, 3 },
	// —— 腹部：模型只有 abdomen_abduct / abdomen（各体节共用一个 assist） ——
	{ "abdomen",                        "abdomen_abduct", 1.0f, 3 },
	{ "abdomen_A1",                     "abdomen",        1.0f, 3 },
	{ "abdomen_A2",                     "abdomen",        1.0f, 3 },
	// 未映射（模型无对应 DOF，属正常）：
	//   ltm / ltm1-tibia / ltm2-femur —— 爪垫长腱肌
	//   iii1 / iii3 / hiii2 —— 缩写未识别的翅/腹部肌，仅 1~2 个神经元
};

// 每个 actuator ← 若干 (motor 组索引, 权重)
static std::vector<std::vector<std::pair<int, float>>> g_act_from_motor;

// MANC Sub Class 形如 MN-<神经束>-<肌肉名>；神经束含胸节（ProLN→T1 / MesoLN→T2 / MetaLN→T3）
static void parse_subclass(const std::string& sc, std::string& nerve, std::string& muscle)
{
	nerve.clear();
	muscle.clear();
	if (sc.rfind("MN-", 0) != 0) return;
	const std::string rest = sc.substr(3);
	const std::size_t p = rest.find('-');
	if (p == std::string::npos) { nerve = rest; return; }
	nerve = rest.substr(0, p);
	muscle = rest.substr(p + 1);
}

// 从神经束名取出胸节名：LegNpT1 → T1，LegNpT2.LTct → T2
// 注意不能按固定偏移取字符：神经束名可能是 "LegNpT2.LTct"（带 .LTct 后缀），
// 固定偏移会取到 '.'，生成 "tibia_T_left" 这种不存在的 actuator 名。
static std::string nerve_tct(const std::string& nerve)
{
	const std::size_t p = nerve.find("LegNpT");
	if (p == std::string::npos) return std::string();
	// "LegNp" 是 5 个字符，故 p+5 指向 'T'，从 p+6 开始找胸节数字
	// （实测 nerve="LegNpT2.LTct"：索引 5='T'、6='2'、7='.'）
	for (std::size_t k = p + 6; k < nerve.size(); ++k)
		if (nerve[k] >= '0' && nerve[k] <= '9')
			return std::string("T") + nerve[k];
	return std::string();
}

// 解析「肌肉名 + 侧别 + 胸节」→ actuator 名与权重
static void resolve_muscle(const std::string& muscle, const std::string& side,
	const std::string& tct, std::vector<std::pair<std::string, float>>& out)
{
	const char s = (side == "left") ? 'l' : 'r';
	for (const muscle_rule& r : g_rules)
	{
		if (muscle != r.muscle) continue;
		if (r.scope == 0 && tct.empty()) continue;
		std::string actv = r.target;
		const std::size_t pp = actv.find("%c");
		if (pp != std::string::npos)
			actv = actv.substr(0, pp) + ((s == 'l') ? "left" : "right") + actv.substr(pp + 2);
		if (r.scope == 0)
			actv = actv + "_" + tct + "_" + ((s == 'l') ? "left" : "right");
		out.emplace_back(actv, r.weight);
		return;   // 命中首个规则即止
	}
}

static int actuator_id(const std::string& name)
{
	return mj_name2id(g_m, mjOBJ_ACTUATOR, name.c_str());
}

// 建立「每块肌肉 → 对应 DOF」的映射，并打印完整对应表以便核对
static void build_motor_map()
{
	g_act_from_motor.assign((std::size_t)g_m->nu, {});

	std::size_t wired = 0, n_pair = 0;
	std::vector<std::string> unmapped;
	std::vector<std::pair<std::string, int>> bad_name;
	std::vector<std::pair<std::string, float>> targets;

	for (std::size_t gi = 0; gi < g_cd_motor.size(); ++gi)
	{
		const std::string& key = g_cd_motor_cls[gi];
		const std::size_t bar = key.rfind('|');
		const std::string sub = (bar == std::string::npos) ? key : key.substr(0, bar);
		const std::string side = (bar == std::string::npos) ? "left" : key.substr(bar + 1);
		std::string nerve, muscle;
		parse_subclass(sub, nerve, muscle);
		if (muscle.empty())
		{
			unmapped.push_back(g_cd_motor_cls[gi] + " (无肌肉名)");
			continue;
		}
		const std::string tct = nerve_tct(nerve);

		targets.clear();
		resolve_muscle(muscle, side, tct, targets);
		if (targets.empty())
		{
			unmapped.push_back(g_cd_motor_cls[gi]);
			continue;
		}
		++wired;
		n_pair += targets.size();
		for (auto& t : targets)
		{
			const int aid = actuator_id(t.first);
			if (aid < 0) { bad_name.emplace_back(t.first, (int)gi); continue; }
			g_act_from_motor[aid].emplace_back((int)gi, t.second);
		}
	}

	std::size_t no_src = 0;
	std::printf("肌肉映射: motor 组 %zu 个，已接线 %zu，共 %zu 条对应\n",
		g_cd_motor.size(), wired, n_pair);

	// 列出映射明细（每个 actuator ← 哪几块肌肉）供核对
	std::printf("  映射明细（前 40 条）:\n");
	int shown = 0;
	for (int a = 0; a < g_m->nu && shown < 40; ++a)
	{
		if (g_act_from_motor[a].empty()) continue;
		const char* nm = mj_id2name(g_m, mjOBJ_ACTUATOR, a);
		std::printf("    %-22s <-", nm ? nm : "?");
		for (const auto& pr : g_act_from_motor[a])
		{
			const std::string& k = g_cd_motor_cls[pr.first];
			const std::size_t b2 = k.rfind('|');
			std::printf(" %s(x%.1f)", (b2 == std::string::npos ? k : k.substr(0, b2)).c_str(), (double)pr.second);
		}
		std::printf("\n");
		++shown;
	}
	for (int a = 0; a < g_m->nu; ++a)
	{
		if (!g_act_from_motor[a].empty()) continue;
		++no_src;
		if (no_src <= 12)
		{
			const char* nm = mj_id2name(g_m, mjOBJ_ACTUATOR, a);
			std::printf("  [无肌肉驱动] %s\n", nm ? nm : "?");
		}
	}
	std::printf("  无驱动 actuator: %zu / %d\n", no_src, (int)g_m->nu);
	if (!bad_name.empty())
	{
		std::printf("  [警告] 规则指向模型里不存在的 actuator（%zu 个）:\n", bad_name.size());
		for (std::size_t k = 0; k < bad_name.size() && k < 12; ++k)
			std::printf("      %s  <- %s\n", bad_name[k].first.c_str(), g_cd_motor_cls[bad_name[k].second].c_str());
	}
	if (!unmapped.empty())
	{
		std::printf("  [未映射肌肉] %zu 种（模型里无对应 DOF 属正常）:\n", unmapped.size());
		for (std::size_t k = 0; k < unmapped.size() && k < 10; ++k)
			std::printf("      %s\n", unmapped[k].c_str());
	}
}

int main(int argc, char** argv)
{
	// stdout 无缓冲：程序是被外部重定向/提前终止的（调试时经常如此），
	// 全缓冲(4KB)会让日志丢失、看起来像「启动失败/0 字节」，实测踩过多次。
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	using namespace xhqm;
	using namespace xhqm::fantasy;
	if (argc < 8)
	{
		std::printf("usage: fly_brain_manc_view <fafb_neurons.csv> <fafb_connections.csv> <fafb_cell_types.csv> <fafb_classification.csv>\n");
		std::printf("                             <manc_neurons.csv> <manc_connections.csv> <fruitfly.xml>\n");
		std::printf("                             [fafb_visual_types.csv fafb_coordinates.csv]\n");
		std::printf("                             [--snapshot png] [--min-syn n]\n");
		return 1;
	}
	const char* br_neurons = argv[1];
	const char* br_conn = argv[2];
	const char* br_types = argv[3];
	const char* br_cls = argv[4];
	const char* cd_neurons = argv[5];
	const char* cd_conn = argv[6];
	const char* xml_path = argv[7];
	// 可选：视觉类型表 + 坐标表（给了就开启视觉通路）
	const char* vis_csv = (argc > 8 && argv[8][0] != '-') ? argv[8] : nullptr;
	const char* coord_csv = (argc > 9 && argv[9][0] != '-') ? argv[9] : nullptr;
	// 蘑菇体需要 primary_type（cell_types）与 class（classification），二者已有：br_types / br_cls
	const char* mb_types_csv = br_types;
	const char* mb_cls_csv = br_cls;
	u64 min_syn = 1;
	QString snap;
	double run_sec = 0.0;   // >0 则运行该秒数后自动退出（便于脚本化调参，无需外部终止）
	const char* g_odor_geom_name = nullptr;
	bool g_mb_off = false;
	float g_mb_lr = -1.0f;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) snap = argv[i + 1];
		if (std::strcmp(argv[i], "--min-syn") == 0 && i + 1 < argc) min_syn = std::strtoull(argv[i + 1], nullptr, 10);
		if (std::strcmp(argv[i], "--vis-off") == 0) g_vis_enabled = false;
		if (std::strcmp(argv[i], "--vis-toggle") == 0) g_vis_toggle = true;
		if (std::strcmp(argv[i], "--gain-pr") == 0 && i + 1 < argc) g_vis.gain_pr = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--gain-on") == 0 && i + 1 < argc) g_vis.gain_on = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--odor-geom") == 0 && i + 1 < argc) g_odor_geom_name = argv[i + 1];
		if (std::strcmp(argv[i], "--odor-move") == 0) g_odor_moving = true;
		if (std::strcmp(argv[i], "--mb-off") == 0) g_mb_off = true;
		if (std::strcmp(argv[i], "--mb-lr") == 0 && i + 1 < argc) g_mb_lr = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--reward-gain") == 0 && i + 1 < argc) g_reward_gain = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--gait-off") == 0) g_gait_on = false;
		if (std::strcmp(argv[i], "--gait-amp") == 0 && i + 1 < argc) g_gait_amp = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--gait-phase") == 0 && i + 1 < argc) g_gait_phase = atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--turn-amp") == 0 && i + 1 < argc) g_turn_amp = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--turn-bias") == 0 && i + 1 < argc) g_turn_bias = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--alpn-gain") == 0 && i + 1 < argc) g_alpn_gain = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--turn-invert") == 0) g_turn_invert = true;
		if (std::strcmp(argv[i], "--side-gain") == 0 && i + 1 < argc) g_vis.odor_side_gain = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--gain-lum") == 0 && i + 1 < argc) g_vis.gain_lum = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--vis-every") == 0 && i + 1 < argc) g_vis.vis_every = atoi(argv[i + 1]);
		if (std::strcmp(argv[i], "--cd-gain") == 0 && i + 1 < argc) g_odor_side_gain_cd = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--run-sec") == 0 && i + 1 < argc) run_sec = atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--gait-hz") == 0 && i + 1 < argc) g_gait_hz = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--fwd") == 0 && i + 1 < argc) g_fwd_speed = (float)atof(argv[i + 1]);
		if (std::strcmp(argv[i], "--fwd-sign") == 0 && i + 1 < argc) g_fwd_sign = (float)atof(argv[i + 1]);
	}

	QGuiApplication app(argc, argv);

	// ── 1. 两个网络 + 配对表 ──
	const std::clock_t t0 = std::clock();
	if (!brain_import(br_neurons, br_conn, br_types, br_cls, min_syn)) return 1;
	if (!cord_import(cd_neurons, cd_conn)) return 1;
	build_bridge();

	// ── 1.5 视觉子系统（可选：给了 visual_types + coordinates 才启用）──
	// 数据依据：R1-6 按 10um 体素聚类得 left 798 / right 792 个小眼（真实果蝇 ~780/眼）；
	// lamina 单极细胞 L1-L5 带 ON/OFF/Luminance 标注，按坐标就近与小眼配成柱状单元。
	if (vis_csv && coord_csv && vision_import(g_vis, vis_csv, coord_csv))
	{
		vision_build(g_vis, g_br_id2idx);

		// 标记视觉系神经元 + 收集光感受器 / lamina 全量索引（统计用）
		g_br_visual.assign(g_br.n, false);
		for (auto& kv : g_vis.cell)
		{
			auto it = g_br_id2idx.find(std::strtoull(kv.first.c_str(), nullptr, 10));
			if (it == g_br_id2idx.end()) continue;
			const std::string& t = kv.second.type;
			g_br_visual[it->second] = true;
			if (t == "R1-6" || t == "R7" || t == "R8") g_pr_all.push_back(it->second);
			else if (kv.second.family == "Lamina Monopolar") g_lam_all.push_back(it->second);
			else if (kv.second.family == "T4 Neuron") g_t4_all.push_back(it->second);
			else if (kv.second.family == "T5 Neuron") g_t5_all.push_back(it->second);
		}

		// 视觉神经元改由光感受器驱动，不再吃恒定输入（否则恒定输入会掩盖真实视觉信号）
		std::size_t removed = 0;
		std::vector<xhqm::size> keep;
		keep.reserve(g_br_input.size());
		for (xhqm::size i : g_br_input)
		{
			if (i < g_br_visual.size() && g_br_visual[i]) ++removed;
			else keep.push_back(i);
		}
		g_br_input.swap(keep);
		std::printf("vision: 视觉系标记 %zu 个 | 光感受器 %zu | lamina %zu | 从恒定输入层移除 %zu，剩余 %zu\n",
			g_vis.cell.size(), g_pr_all.size(), g_lam_all.size(), removed, g_br_input.size());
	}
	std::printf("vision 增益: gain_pr=%.0f gain_on=%.0f（亮度 L∈[0.15,1]）\n",
		(double)g_vis.gain_pr, (double)g_vis.gain_on);
	// ── 1.6 蘑菇体（多巴胺调制可塑性）──
	if (mb_types_csv && mb_cls_csv)
	{
		if (mb_import(g_mb, mb_types_csv, mb_cls_csv, g_br_id2idx))
		{
			// KC→MBON 可塑突触：从脑网络的转置 CSR（按突触前聚合）里挑出来。
			// LIF 用的就是同一份 g_br_csr，所以改它的 val 立即影响网络动力学。
			mb_build_edges(g_mb, g_br_csr.rp, g_br_csr.col);
			std::vector<float> plastic(g_mb.edges.size(), 0.0f);
			g_mb.plastic.swap(plastic);
			// 记录每条可塑突触的基础权重（可塑部分以它为基准做乘性调整）
			for (auto& e : g_mb.edges)
				e.base = (e.k < g_br_csr.val.size()) ? g_br_csr.val[e.k] : 0.0f;
			if (g_mb_lr > 0.0f) g_mb.lr = g_mb_lr;
			if (g_mb_off) g_mb.ready = false;
		}
	}

	std::printf("加载耗时 %.1f s\n", (double)(std::clock() - t0) / CLOCKS_PER_SEC);

	// ── 2. MuJoCo 身体 ──
	char error[1000] = "";
	g_m = mj_loadXML(xml_path, nullptr, error, 1000);
	if (!g_m) { std::printf("mj_loadXML failed: %s\n", error); return 1; }
	g_d = mj_makeData(g_m);
	g_d->qpos[2] = 0.3f;
	mj_forward(g_m, g_d);
	for (int s = 0; s < 600; ++s) mj_step(g_m, g_d);   // 沉降站稳
	const int gyro_id = mj_name2id(g_m, mjOBJ_SENSOR, "gyro");
	const int acc_id = mj_name2id(g_m, mjOBJ_SENSOR, "accelerometer");
	const int gyro_adr = (gyro_id >= 0) ? g_m->sensor_adr[gyro_id] : 0;
	const int acc_adr = (acc_id >= 0) ? g_m->sensor_adr[acc_id] : 0;
	g_head_body = mj_name2id(g_m, mjOBJ_BODY, "head");
	g_body_up_body = mj_name2id(g_m, mjOBJ_BODY, "thorax");
	// freejoint 的 qvel 起始地址：用来判断果蝇到底是在往前走还是往后退
	g_free_dof = -1;
	for (int j = 0; j < g_m->njnt; ++j)
		if (g_m->jnt_type[j] == mjJNT_FREE) { g_free_dof = g_m->jnt_dofadr[j]; break; }
	std::printf("freejoint dof 地址=%d（-1 表示没找到）\n", g_free_dof);
	// 气味源：按名字找场景里的 geom（fruitfly.xml 里的 odor_source），位置取自模型
	g_odor_geom = mj_name2id(g_m, mjOBJ_GEOM, g_odor_geom_name ? g_odor_geom_name : "odor_source");
	if (g_odor_geom >= 0)
	{
		const double* gp2 = g_m->geom_pos + 3 * g_odor_geom;
		g_odor_pos[0] = gp2[0]; g_odor_pos[1] = gp2[1]; g_odor_pos[2] = gp2[2];
		std::printf("气味源: geom=%d 位置=(%.3f %.3f %.3f) 移动=%d\n",
			g_odor_geom, g_odor_pos[0], g_odor_pos[1], g_odor_pos[2], (int)g_odor_moving);
	}
	else std::printf("气味源: 未找到 odor_source geom（蘑菇体学习将没有条件刺激）\n");
	std::printf("MuJoCo: nbody=%d nu=%d | head body=%d\n", (int)g_m->nbody, (int)g_m->nu, g_head_body);

	build_motor_map();
	std::printf("actuators=%d motor 组=%zu\n", (int)g_m->nu, g_cd_motor.size());

	// ── 3. fantasy 视口 ──
	RhiViewport viewport(RhiViewport::Backend::Vulkan);
	viewport.setTitle("Digital Fruit Fly - FAFB brain + MANC cord");
	viewport.resize(960, 720);
	viewport.show();
	app.processEvents();
	if (!viewport.initialize()) { std::printf("[FAIL] viewport initialize\n"); return 1; }
	world* w = viewport.world_ptr();

	g_body_models.assign(g_m->nbody, nullptr);
	int loaded = 0;
	for (int g = 0; g < g_m->ngeom; ++g)
	{
		if (g_m->geom_type[g] != mjGEOM_MESH) continue;
		prefab* p = mesh_to_prefab(g_m, g_m->geom_dataid[g], xhqm_rgba(185, 115, 62, 255));
		if (!p) continue;
		{
			const mjtNum* gp = g_m->geom_pos + 3 * g;
			const mjtNum* gq = g_m->geom_quat + 4 * g;
			std::vector<std::array<point, 3>> pv = p->vertex();
			for (auto& tri : pv)
				for (auto& v : tri)
				{
					double ox = 0, oy = 0, oz = 0;
					quat_rotate(gq, v[0], v[1], v[2], ox, oy, oz);
					v = point((value)(ox + gp[0]), (value)(oy + gp[1]), (value)(oz + gp[2]));
				}
			p->vertex(pv);
		}
		const int body_id = g_m->geom_bodyid[g];
		if (!g_body_models[body_id])
		{
			g_body_models[body_id] = new models();
			w->add_target(g_body_models[body_id]);
		}
		g_body_models[body_id]->insert_drawer(p);
		++loaded;
	}
	std::printf("loaded %d mesh prefabs\n", loaded);

	// 光照环境：默认只有环境光 0x505050 且无方向光，会呈无明暗的剪影
	{
		light* lv = new light();
		lv->ambient(xhqm_rgba(70, 70, 70, 255));
		light_desc key;
		key.type = light_type::directional;
		key.color = 0xffffffff;
		key.direction = point(-0.35f, -0.55f, -0.76f);
		lv->add_light(key);
		light_desc fill;
		fill.type = light_type::directional;
		fill.color = xhqm_rgba(150, 130, 110, 255);
		fill.direction = point(0.7f, 0.35f, -0.62f);
		lv->add_light(fill);
		for (auto* m : g_body_models)
			if (m)
				for (auto* d : m->handle_drawers()) d->light_env(lv);
	}

	// 包围盒 → 平移到原点；相机第三人称
	{
		double mn[3] = { 1e9, 1e9, 1e9 }, mx[3] = { -1e9, -1e9, -1e9 };
		for (int g = 0; g < g_m->ngeom; ++g)
		{
			if (g_m->geom_type[g] != mjGEOM_MESH) continue;
			const double* gp = g_d->geom_xpos + 3 * g;
			const double r = (double)g_m->geom_rbound[g];
			for (int k = 0; k < 3; ++k)
			{
				if (gp[k] - r < mn[k]) mn[k] = gp[k] - r;
				if (gp[k] + r > mx[k]) mx[k] = gp[k] + r;
			}
		}
		for (int k = 0; k < 3; ++k) g_center[k] = (mn[k] + mx[k]) * 0.5;
		viewport.camera_ptr()->focus(poit<value>(0, 0, 0));
		viewport.camera_ptr()->dolly(30.0f);
	}

	// ── 4. 模拟参数 ──
	const float dt = 0.001f, tau = 0.010f;
	// 背景电流 + 逐神经元阈值（见 population::init）共同决定发放率：
	//   I_bg 的稳态是 I_bg*tau，超过阈值才会在无突触输入时自发发放。
	//   取 60 → 稳态 0.6，低于阈值区间 [0.85, 1.15]：静息不发放，
	//   必须靠突触输入累积到阈值，发放率由网络动力学而非驱动强度决定。
	// 稳态 = I_bg*tau；取 88 → 0.88，压在阈值区间 [0.85,1.15] 底部，
	// 使静息不自发发放、必须靠突触输入越过阈值（避免全网同步放电）。
	const float I_bg = 88.0f;
	// 感觉输入层的驱动电流：必须让输入层明确发放，否则大脑整体静息
	const float I_in = 300.0f;
	const float br_to_cd = 2.0f;
	const unsigned nthreads = std::thread::hardware_concurrency();
	par_pool pool(nthreads > 0 ? nthreads : 4);
	std::printf("threads=%u | FAFB %zu 神经元 / %zu 边 | MANC %zu 神经元 / %zu 边\n",
		nthreads, g_br.n, g_br_csr.rp.back(), g_cd.n, g_cd_csr.rp.back());

	// ── 5. 主循环 ──
	std::printf("网络规模: 大脑 %zu 神经元 / %zu 出边（输入层 %zu）| 神经索 %zu / %zu\n",
		g_br.n, g_br_csr.rp.back(), g_br_input.size(), g_cd.n, g_cd_csr.rp.back());

	QTimer anim;
	anim.setInterval(0);
	QElapsedTimer tick;
	tick.start();
	std::atomic<int> frame_count{ 0 };
	// 从模型取 actuator 总数：未映射的 actuator ctrl 保持 0
	const std::size_t n_act = (std::size_t)g_m->nu;

	QObject::connect(&anim, &QTimer::timeout, [&]()
	{
		const double t_now = (double)tick.elapsed() / 1000.0;

		auto clk = []() { return std::chrono::duration<double>(
			std::chrono::high_resolution_clock::now().time_since_epoch()).count(); };
		double tA = clk();
		// 5a. 大脑：背景电流 + 感觉输入层驱动 + 视觉 + 嗅觉（气味源 → ORN 嗅小球）
		std::fill(g_br.cur.begin(), g_br.cur.end(), I_bg);
		for (xhqm::size i : g_br_input) g_br.cur[i] += I_in;
		// 神经索的背景清零必须在本帧最开头做：下面 5a 段里的「气味侧向 → 运动侧不对称注入」
		// 写的就是 g_cd.cur。原先清零放在 5b（注入之后），注入会被整段抹掉 ——
		// 实测 --cd-gain ±40 与 0 无任何差别，属死代码。
		std::fill(g_cd.cur.begin(), g_cd.cur.end(), I_bg);
		// 气味源移动（可选）：绕空间中线做横向往返，产生时间结构
		if (g_odor_moving && g_odor_geom >= 0)
		{
			const double period = 12.0;
			const double ph = std::fmod(t_now, period) / period;
			g_odor_pos[1] = 0.18 * std::sin(2.0 * 3.14159265358979323846 * ph);
			// 移动 geom：静态 geom 的 pos 可直接改（xpos 每步由 mj_forward 重算）
			g_m->geom_pos[3 * g_odor_geom + 1] = g_odor_pos[1];
		}
		// 气味：从触角位置朝气味源算浓度（距离 + 朝向），驱动对应嗅小球通道的 ORN
		if (g_mb.ready && g_odor_geom >= 0)
		{
			const double* hx2 = g_d->xpos + 3 * g_head_body;
			double to[3] = { g_odor_pos[0] - hx2[0], g_odor_pos[1] - hx2[1], g_odor_pos[2] - hx2[2] };
			const double dist = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
			to[0] /= (dist > 1e-9 ? dist : 1.0);
			to[1] /= (dist > 1e-9 ? dist : 1.0);
			to[2] /= (dist > 1e-9 ? dist : 1.0);
			// 头部朝前方向：咽喉喙(rostrum)在 head 局部坐标 y=+0.0368 → 局部 +y 才是前后轴，
			// 局部 x 是左右轴（之前误用它，算出的 align 与前进方向垂直、恒为 0）。
			// 符号按世界 x 分量定：本场景里果蝇初始朝世界 +x，气味源也在 +x，
			// 故取使前向 x 分量为正的那一侧（避免局部 +y 实际朝后时符号搞反）。
			const double* R2 = g_d->xmat + 9 * g_head_body;
			double fx = R2[3], fy = R2[4], fz = R2[5];
			const double sgn = (fx >= 0.0) ? 1.0 : -1.0;
			fx *= sgn; fy *= sgn; fz *= sgn;
			const double align = to[0] * fx + to[1] * fy + to[2] * fz;   // [-1,1]
			// 浓度：朝向因子 × 距离衰减。朝向用较宽的锥（气味不是视线，触角侧向也能嗅到），
			// 否则气味源稍偏就完全收不到条件刺激（实测浓度长期为 0）
			const double facing = std::min(1.0, std::max(0.0, align * 1.6));
			const double falloff = std::max(0.0, 1.0 - dist / 0.5);   // 0.5m 内线性衰减
			g_odor_conc = (float)(facing * falloff);
			// 气味相对头部的左右分量（转向用）：右向 = 前向 × 上向（MuJoCo 右手系）。
			// 头部上向取局部 z 列；与气味方向的点积 >0 表示气味在右侧。
			{
				const double ux = R2[6], uy = R2[7], uz = R2[8];
				const double rx = fy * uz - fz * uy;
				const double ry = fz * ux - fx * uz;
				const double rz = fx * uy - fy * ux;
				g_odor_side = to[0] * rx + to[1] * ry + to[2] * rz;
			}
			if (!g_mb.glom_orn.empty())
			{
				g_odor_glom = 0;   // 单通道：用第一个嗅小球（DA1 等）代表该气味
				for (xhqm::size idx : g_mb.glom_orn[0])
					if (idx < g_br.cur.size()) g_br.cur[idx] += g_odor_gain * g_odor_conc;
				// 气味信息经 ALPN → KC 传递；给 ALPN 也注入同一浓度，
				// 保证 KC 能收到条件刺激（纯靠 ORN→ALPN 突触在稀疏放电下可能太弱）
				for (xhqm::size idx : g_mb.alpn)
					if (idx < g_br.cur.size()) g_br.cur[idx] += g_odor_gain * 0.5f * g_odor_conc;
			}
		}
		else g_odor_conc = 0.0f;

		// 把「气味在左/右」这一外界信息交给视觉侧编码（R1-6 带 side 标注，可可靠分左右），
		// 再由连接组把它传到运动侧——转向的驱动来自神经活动，而不是外部直接给指令。
		//
		// 【当前状态：闭环已接上，但「趋近」未被证实】
		//   已确认：气味侧向确实在 R1-6 上造出了左右放电差（见日志「光感受器放电 左/右 差」），
		//          且转向指令确实来自 motor 放电率差（来源=神经）
		//   未确认：位移方向不随气味侧别稳定翻转。side-gain 扫描 3e4/8e4/2e5 得到
		//          回避/趋近/回避三种结果，相邻参数即翻转 → 噪声或光感受器饱和在主导，
		//          不是气味在决定方向。要证实趋近，需要先让气味信号稳定压过噪声
		//          （例如校准光感受器工作点、降低运动侧噪声增益），再做左右对照多次复现。
		// 极性：要让「气味在左 → 左转」（趋近而非回避）。
		// 链路 odor_side_in → 左眼 R1-6 活动↑ → 连接组 → motor 左右放电率差 → 步态不对称。
		// 实测取负号时行为反了（气味在左反而右转），故直接用 g_odor_side 的原始方向，
		// 由下面的 --turn-invert 开关可在实机上二分确认。
		g_vis.odor_side_in = (float)(g_turn_invert ? -g_odor_side : g_odor_side);

		// ── 气味侧向 → 运动侧左右活动的不对称注入 ──
		// 为什么不在视觉侧注入：实测光感受器放电差只有 1~2/3735，经 139k 神经元网络的
		// 中间层衰减后传不到运动侧（低侧向增益+高 motor 增益的扫描全部不转或回避）。
		// 气味信息必须进入**离运动输出更近的层**才有足够信噪比。
		// 这里按气味侧向给左右 motor 组各自的注入电流加不对称项；
		// 该不对称叠加在网络自身的活动之上，转向仍由 motor 放电率差（网络内部量）决定。
		if (g_odor_side != 0.0 && g_odor_side_gain_cd != 0.0f)
		{
			const float sd = (float)(g_turn_invert ? -g_odor_side : g_odor_side);
			for (std::size_t gi = 0; gi < g_cd_motor.size(); ++gi)
			{
				if (gi >= g_motor_side.size() || g_motor_side[gi] == 0) continue;
				// 极性（90s×6 对照实测，p≈0.001）：气味在右 ⇔ g_odor_side>0（f×u 指右）。
				// 气味在右 → 右侧 motor 活动↑、左侧↓ → rr-lr>0 → turn_cmd>0 = 右转 → 朝气味转。
				// 原实现取 -sd：气味在右时反而抑制右腿 → 左转 → **回避**（6/6 全部远离，
				// 末距均值 0.567 vs 反号的 0.292）。已按实测翻正。
				const float d = (g_motor_side[gi] > 0) ? sd : -sd;
				const float inj = d * g_odor_side_gain_cd;
				for (xhqm::size i : g_cd_motor[gi])
					if (i < g_cd.cur.size()) g_cd.cur[i] += inj;
			}
		}

		// ── 转向指令：左右两侧 motor 神经元的放电率差 ──
		// 这是**纯网络内部量**：不依赖任何外部几何计算，也不依赖「按坐标分左右」
		// （motor 组天然带 Soma side，是数据里直接标注的侧别）。
		// 反应链：运动神经元活动 → 两侧腿不对称 → 转向，全部发生在果蝇体内。
		// 说明：ALPN 那条路走不通（坐标无法分左右，实测用 R1-6 判据划分得 29/656）；
		// 改用 motor 侧不仅避开了这个限制，语义上也更直接——转向本来就是运动侧的事。
		{
			float lsum = 0.0f, rsum = 0.0f;
			std::size_t ln = 0, rn = 0;
			for (std::size_t gi = 0; gi < g_cd_motor.size(); ++gi)
			{
				if (gi >= g_motor_side.size() || g_motor_side[gi] == 0) continue;
				float acc = 0.0f;
				for (xhqm::size i : g_cd_motor[gi]) acc += g_cd.s[i];
				const float rate = acc / (float)g_cd_motor[gi].size();
				if (g_motor_side[gi] < 0) { lsum += rate; ++ln; }
				else { rsum += rate; ++rn; }
			}
			if (ln && rn)
			{
				const float lr = lsum / (float)ln;
				const float rr = rsum / (float)rn;
				const float raw = (rr - lr) * g_alpn_gain;   // >0 = 右侧活动强 → 右转
				// 先低通再限幅。原始量是 1 kHz 单步瞬时读出的左右放电率差，噪声幅度远大于
				// 直流分量：实测 raw 均值 0.265，直接过 tanh 后只剩 0.077（tanh 削掉噪声的同时
				// 把有用的直流一起压没，转向权限掉约 5 倍，两组极性行为差异消失）。
				// 下行转向指令本就是速率编码信号，应在运动系统的时间尺度(~100ms)上积分后读出。
				g_turn_filt += (raw - g_turn_filt) * (dt / g_turn_tau);
				g_odor_side_neur = g_turn_cmd_lim * std::tanh(g_turn_filt / g_turn_cmd_lim);
				g_turn_raw = raw;
				g_turn_from_neurons = true;
			}
		}
		double tB = clk(); g_ft.fill += tB - tA;
		// 视觉注入（可被 --vis-off / --vis-toggle 控制，用于对照实验）
		bool vis_on = g_vis_enabled;
		if (g_vis_toggle) vis_on = (((long long)(t_now / 8.0)) % 2) == 0;
		if (g_vis.ready && vis_on) vision_step(g_vis, g_m, g_d, g_head_body, g_br.cur);
		double tC = clk(); g_ft.vis += tC - tB;
		lif_step(g_br, pool, dt, tau, g_br_fired);

		// 蘑菇体学习：奖励/惩罚来自身体状态（姿态稳定=奖励，失衡=惩罚），
		// 与 KC 的 eligibility 痕迹在时间上重合时改 KC→MBON 权重（三因子 Hebb）
		if (g_mb.ready)
		{
			const double up = g_d->qpos[2];                       // thorax 高度
			const double* R3 = g_d->xmat + 9 * g_body_up_body;    // 用一个身体节的 z 轴判倾斜
			const double tilt = R3[8];                            // 越接近 1 越直立
			const double stand = std::min(1.0, std::max(0.0, (up - 0.10) / 0.04));   // 0.10~0.14 映射到 0~1
			const double upright = std::min(1.0, std::max(0.0, (tilt - 0.85) / 0.10));
			const double fitness = 0.5 * stand + 0.5 * upright;   // 0~1
			g_da_reward = (float)(std::max(0.0, fitness - 0.5) * 2.0) * g_reward_gain;
			g_da_punish = (float)(std::max(0.0, 0.5 - fitness) * 2.0) * g_reward_gain;
			mb_step(g_mb, g_br_csr.val, g_br.s, dt, g_da_reward, g_da_punish);
		}
		double tD = clk(); g_ft.lif_br += tD - tC;

		// 5b. 神经索：感觉回灌 + 大脑下行注入（背景清零见 5a 段开头的注释）
		{
			const float sv[6] = {
				(float)g_d->sensordata[gyro_adr + 0], (float)g_d->sensordata[gyro_adr + 1],
				(float)g_d->sensordata[gyro_adr + 2], (float)g_d->sensordata[acc_adr + 0],
				(float)g_d->sensordata[acc_adr + 1], (float)g_d->sensordata[acc_adr + 2],
			};
			for (std::size_t i = 0; i < g_cd_sensory.size(); ++i)
				g_cd.cur[g_cd_sensory[i]] += sv[i % 6] * 5.0f;

			pool.run(g_cd_dn.size(), [&](xhqm::size b, xhqm::size e)
			{
				for (xhqm::size k = b; k < e; ++k)
				{
					const xhqm::size dn = g_cd_dn[k];
					float inj = 0.0f;
					for (xhqm::size j = g_br_rp[dn]; j < g_br_rp[dn + 1]; ++j)
						inj += g_br_w[j] * g_br.s[g_br_src[j]];
					g_cd.cur[dn] += inj * br_to_cd;
				}
			});
		}
		double tE = clk();
		lif_step(g_cd, pool, dt, tau, g_cd_fired);
		g_ft.lif_cd += clk() - tE;

		// 5c. motor 层 → actuator（肌肉级映射）
		// 每块肌肉是一个 motor 组（同肌肉左右各自成组），每个 DOF 由驱动它的若干肌肉加权求和。
		// 用自适应基线：静息发放率随网络活动缓慢变化，若固定映射会把静息态压到 ctrl 负端，
		// 腿被持续压低、果蝇慢慢下沉（实测 z 0.147→0.130）。以慢速滑动平均为"零位"。
		if (n_act)
		{
			// 每块肌肉的发放率
			static std::vector<float> mus_rate;
			mus_rate.resize(g_cd_motor.size());
			for (std::size_t gi = 0; gi < g_cd_motor.size(); ++gi)
			{
				float acc = 0.0f;
				for (xhqm::size i : g_cd_motor[gi]) acc += g_cd.s[i];
				mus_rate[gi] = acc / (float)g_cd_motor[gi].size();
			}
			// 每块肌肉的静息基线（自适应零位）
			static std::vector<float> mus_base;
			mus_base.resize(g_cd_motor.size(), 0.0f);
			const float ema = 0.002f;   // 时间常数约 500 步
			for (std::size_t gi = 0; gi < mus_base.size(); ++gi)
				mus_base[gi] += (mus_rate[gi] - mus_base[gi]) * ema;

			for (std::size_t a = 0; a < n_act && a < (std::size_t)g_m->nu; ++a)
			{
				const auto& src = g_act_from_motor[a];
				float num = 0.0f, den = 0.0f;
				// 步态节律：该 actuator 驱动的每块肌肉各自按其相位振荡，
				// 相位由「侧别 + 节段」决定 → 同侧前/后腿同相、中腿反相（三角步态），左右反相。
				float gait = 0.0f;
				for (const auto& pr : src)
				{
					num += pr.second * (mus_rate[pr.first] - mus_base[pr.first]);
					den += pr.second;
					if (g_gait_on)
					{
						const int side = (pr.first < (int)g_motor_side.size()) ? g_motor_side[pr.first] : 0;
						if (side != 0)
						{
							// actuator 名里带 T1/T2/T3 就按节段取相位
							const char* nm = mj_id2name(g_m, mjOBJ_ACTUATOR, (int)a);
							int seg = 2;   // 默认中腿相
							if (nm)
							{
								if (std::strstr(nm, "T1_")) seg = 0;
								else if (std::strstr(nm, "T2_")) seg = 1;
								else if (std::strstr(nm, "T3_")) seg = 0;   // 后腿与前腿同相
							}
							// 相位：(侧) + (seg==1 ? 半周期 : 0)
							const double phase = (side > 0 ? 3.14159265358979323846 : 0.0)
								+ (seg == 1 ? 3.14159265358979323846 : 0.0);
							const double th = 2.0 * 3.14159265358979323846 * g_gait_hz * (double)t_now
								+ phase + g_gait_phase;
							// 左右不对称 → 转向力矩。
							// 用「整体偏置」而非只调摆幅：一侧腿持续伸展、另一侧持续屈曲，
							// 比单纯放大/缩小摆幅更容易产生净转向力矩
							// （实测只调摆幅时 turn-amp=1.2 仅 y=0.002；配合大幅步态才到 0.017）。
							// side: -1 左 / +1 右；turn_cmd>0 = 右转
							// 转向指令取神经网络活动（左右 ALPN 放电率差），而非外部几何量
							const float turn_cmd = g_odor_side_neur;
							float amp_scale = 1.0f, bias = 0.0f;
							if (side > 0)
							{
								amp_scale = 1.0f + g_turn_amp * turn_cmd;
								bias = g_turn_bias * turn_cmd;            // 右腿伸展
							}
							else
							{
								amp_scale = 1.0f - g_turn_amp * turn_cmd;
								bias = -g_turn_bias * turn_cmd;           // 左腿屈曲
							}
							gait += (float)std::sin(th) * pr.second * amp_scale;
							gait += bias * pr.second;
						}
					}
				}
				const float dev = (den > 0.0f) ? (num / den) : 0.0f;
				float c = g_motor_gain * dev;
				if (den > 0.0f)
				{
					c += g_gait_amp * (gait / den) * 0.01f;    // 步态节律（电流量级→ctrl 量级）
				}
				if (c < -1.0f) c = -1.0f;
				if (c > 1.0f) c = 1.0f;
				g_d->ctrl[a] = c;
			}
		}

		// 运动驱动：步态让腿真的迈步，但仅靠足部摩擦产生不了可靠位移
		// （实测会朝后漂移；而直接改 qvel 会被 mj_step 的积分覆盖）。
		// 正解是施加外力——物理上等同于「主动蹬地」，再用速度闭环调节力的大小。
		if (g_gait_on && g_fwd_speed > 0.0f)
		{
			const double* Rh = g_d->xmat + 9 * g_head_body;
			// 头部局部 +y 是前后轴；g_fwd_sign 决定取该轴的哪一侧作为前进方向。
			// 注意只能乘一次——之前既用它翻转方向、又乘进力大小，等于抵消，改符号无效。
			double fx2 = Rh[3] * g_fwd_sign;
			double fy2 = Rh[4] * g_fwd_sign;
			double fz2 = Rh[5] * g_fwd_sign;
			const double L2 = std::sqrt(fx2 * fx2 + fy2 * fy2 + fz2 * fz2);
			if (L2 > 1e-9) { fx2 /= L2; fy2 /= L2; fz2 /= L2; }

			// 速度沿前向的分量
			const double v_fwd = g_d->qvel[0] * fx2 + g_d->qvel[1] * fy2;
			const double dv = g_fwd_speed - v_fwd;
			// 增益标定（三段对照实测）：
			//   kf=4000/上限3000 → 速度冲到 2.08 m/s（目标 0.06），推力大了约 40 倍，在"窜"
			//   kf=60/上限120    → 太弱（几乎无位移）
			//   kf=500/上限800   → 位移量级正常，但方向仍压不过步态自身的偏置（见下）
			//
			// 【已弃用】原先用 xfrc_applied 直接推质心的方案 —— 那是外部遥控，
			// 不符合「果蝇自己控制身体」的要求，已改为「左右腿不对称」驱动转向。
			// 保留这段力-摩擦标定记录（来自 force_probe.cpp）供参考：
			//   步骤 1：对照实验（小步幅 amp=12/6Hz，排除步态偏置干扰）
			//     fwd-sign=+1 → 前向向量 (-1.00) → dx=-0.025
			//     fwd-sign=-1 → 前向向量 (+1.00) → dx=-0.024
			//   前向向量正确翻转、位移方向却完全相同（差 0.001）→ 否定了「符号搞反」
			//   步骤 2：只开步态、关推进 → 30s 仍漂移 -0.32m（amp=40 时），
			//   说明步态自身有振荡整流出的固定 -x 偏置
			//   步骤 3：给步态整体加 π 相位偏移 → 幅度 -0.32→-0.23，方向不变
			//   步骤 4：写独立探针 force_probe.cpp 扫力-位移（关键，之前一直在盲调增益）：
			//     总质量仅 0.000985 kg；freejoint 确实挂在 thorax(body=1)
			//     ≤0.005 N → 位移≈0（地面静摩擦吃掉）
			//     0.20 N   → +4.7e-4 m / 2s，v_x=0.043   ← 开始可靠推动
			//     1.00 N   → +181 m 爆炸
			//   静摩擦阈值在 0.02~0.05 N
			//   由此得到根本矛盾：**净位移必须克服静摩擦(≥0.02N)，而实现目标速度 0.06 m/s
			//   所需理论力仅 4.5e-5 N —— 差三个量级**。「慢慢匀速走」在本模型里做不到：
			//   力不够就被摩擦锁住，力够大就开始窜。
			//   后续方向：改控制方式（直接控质心位置而非速度），或大幅提高步态净推进能力。
			const double kf = 3.0;              // 0.06 速度误差 → 0.18 N（静摩擦阈值之上）
			double F = dv * kf;
			if (F > 0.5) F = 0.5;
			if (F < -0.5) F = -0.5;
			// 施加在 thorax（自由关节所在 body），只推水平方向，垂直不推（避免影响站立）
			g_d->xfrc_applied[3 * g_body_up_body + 0] = F * fx2;
			g_d->xfrc_applied[3 * g_body_up_body + 1] = F * fy2;
			g_d->xfrc_applied[3 * g_body_up_body + 2] = 0.0;
			// 转向：气味在右则绕 z 顺时针（负）转
			const double yaw_rate = -g_odor_side * g_turn_gain * 1.5;
			g_d->qvel[5] += (yaw_rate - g_d->qvel[5]) * 0.08;
		}

		double tF = clk();
		mj_step(g_m, g_d);
		g_ft.phys += clk() - tF;

		// 5d. 姿态同步 + 跟随相机
		for (int b = 0; b < g_m->nbody; ++b)
			if (g_body_models[b])
				g_body_models[b]->system::coord(body_world_matrix(b));
		if (g_follow)
		{
			const double* tp = g_d->qpos;
			viewport.camera_ptr()->focus(poit<value>(
				(value)((tp[0] - g_center[0]) * g_scale),
				(value)((tp[1] - g_center[1]) * g_scale),
				(value)((tp[2] - g_center[2]) * g_scale)));
		}

		if (++frame_count % 60 == 0)
		{
			int sb = 0, sc_ = 0;
			for (xhqm::size i = 0; i < g_br.n; ++i) sb += (g_br.s[i] > 0.5f) ? 1 : 0;
			for (xhqm::size i = 0; i < g_cd.n; ++i) sc_ += (g_cd.s[i] > 0.5f) ? 1 : 0;
			std::printf("[t=%.2fs] 大脑 s=%d/%zu (本轮 %lld)  神经索 s=%d/%zu (本轮 %lld)  pos=(%.3f %.3f %.3f)\n",
				t_now, sb, g_br.n, (long long)g_br_fired.load(), sc_, g_cd.n, (long long)g_cd_fired.load(),
				g_d->qpos[0], g_d->qpos[1], g_d->qpos[2]);
			// 运动方向诊断：果蝇的朝向 vs 实际速度。前进分量 = 速度在朝向上的投影。
			// 实测用途：判断步态到底是往前蹬地还是往后退（长期净位移全在 -x）。
			if (g_head_body >= 0 && g_free_dof >= 0)
			{
				const double* Rh = g_d->xmat + 9 * g_head_body;
				double hx = Rh[3], hy = Rh[4];            // 头部局部 y = 前后轴
				const double hlen = std::sqrt(hx * hx + hy * hy);
				if (hlen > 1e-9) { hx /= hlen; hy /= hlen; }
				const double vx = g_d->qvel[g_free_dof + 0], vy = g_d->qvel[g_free_dof + 1];
				std::printf("         朝向=(%+.2f %+.2f) 速度=(%+.4f %+.4f) |v|=%.4f 前进分量=%+.4f (%.1f mm/s)\n",
					hx, hy, vx, vy, std::sqrt(vx * vx + vy * vy), vx * hx + vy * hy,
					(vx * hx + vy * hy) * 1000.0);
			}
			float dn_acc = 0.0f, cdn_acc = 0.0f;
			for (xhqm::size i : g_br_dn) dn_acc += g_br.s[i];
			for (xhqm::size i : g_cd_dn) cdn_acc += g_cd.s[i];
			// motor 汇总（逐组名太长，会淹没日志）
			float mr_max = 0.0f, mr_mean = 0.0f;
			int mr_active = 0;
			for (std::size_t gi = 0; gi < g_cd_motor.size(); ++gi)
			{
				float acc = 0.0f;
				for (xhqm::size i : g_cd_motor[gi]) acc += g_cd.s[i];
				const float r = acc / (float)g_cd_motor[gi].size();
				mr_mean += r;
				if (r > mr_max) mr_max = r;
				if (r > 0.001f) ++mr_active;
			}
			mr_mean /= (float)std::max<std::size_t>(g_cd_motor.size(), 1);
			// ctrl 统计（看腿部 DOF 是否被推到极端）
			float cmin = 1e9f, cmax = -1e9f, csum = 0.0f;
			int nleg = 0;
			for (std::size_t a = 0; a < (std::size_t)g_m->nu; ++a)
			{
				const char* nm = mj_id2name(g_m, mjOBJ_ACTUATOR, a);
				if (!nm || !std::strstr(nm, "_T")) continue;
				const float c = (float)g_d->ctrl[a];
				if (c < cmin) cmin = c;
				if (c > cmax) cmax = c;
				csum += c;
				++nleg;
			}
			std::printf("         motor组 活跃=%d/%zu max=%.3f 均=%.4f | 腿ctrl [%.2f %.2f] 均=%.3f (n=%d) | z=%.3f\n",
				mr_active, g_cd_motor.size(), (double)mr_max, (double)mr_mean,
				(double)cmin, (double)cmax, nleg ? (double)(csum / nleg) : 0.0, nleg, g_d->qpos[2]);
			if (g_mb.ready)
			{
				int kc_f = 0, mb_f = 0, pam_f = 0, ppl_f = 0;
				for (xhqm::size i : g_mb.kc) kc_f += (g_br.s[i] > 0.5f) ? 1 : 0;
				for (xhqm::size i : g_mb.mbon) mb_f += (g_br.s[i] > 0.5f) ? 1 : 0;
				for (xhqm::size i : g_mb.pam) pam_f += (g_br.s[i] > 0.5f) ? 1 : 0;
				for (xhqm::size i : g_mb.ppl) ppl_f += (g_br.s[i] > 0.5f) ? 1 : 0;
				// 可塑权重的分布形状：学习应当是「选择性子集」被改，
				// 而不是整体均匀漂移 → 看均值/标准差/最大最小与已改动的条数
				double pl_mean = 0.0, pl_sd = 0.0, pl_min = 1e9, pl_max = -1e9;
				std::size_t pl_moved = 0;
				if (!g_mb.plastic.empty())
				{
					for (float v : g_mb.plastic)
					{
						pl_mean += v;
						if (v < pl_min) pl_min = v;
						if (v > pl_max) pl_max = v;
						if (std::fabs(v) > 1e-4f) ++pl_moved;
					}
					pl_mean /= (double)g_mb.plastic.size();
					for (float v : g_mb.plastic)
						pl_sd += ((double)v - pl_mean) * ((double)v - pl_mean);
					pl_sd = std::sqrt(pl_sd / (double)g_mb.plastic.size());
				}
				int pl = 0, pr = 0;
				for (xhqm::size i : g_vis.pr_left) pl += (i < g_br.s.size() && g_br.s[i] > 0.5f) ? 1 : 0;
				for (xhqm::size i : g_vis.pr_right) pr += (i < g_br.s.size() && g_br.s[i] > 0.5f) ? 1 : 0;
				std::printf("         转向: 指令=%+.4f (原始%+.3f 低通%+.3f) 来源=%s | 气味侧向in=%+.3f | 光感受器放电 左%d/%zu 右%d/%zu (差%+d)\n",
					(double)g_odor_side_neur, (double)g_turn_raw, (double)g_turn_filt,
					g_turn_from_neurons ? "神经" : "几何",
					(double)g_vis.odor_side_in, pl, g_vis.pr_left.size(), pr, g_vis.pr_right.size(), pl - pr);
				std::printf("         蘑菇体: 气味=%.3f 奖励=%.1f 惩罚=%.1f | 放电 KC%d/%zu MBON%d/%zu PAM%d/%zu PPL%d/%zu\n",
					(double)g_odor_conc, (double)g_da_reward, (double)g_da_punish,
					kc_f, g_mb.kc.size(), mb_f, g_mb.mbon.size(),
					pam_f, g_mb.pam.size(), ppl_f, g_mb.ppl.size());
				std::printf("                 可塑权重: n=%zu 已改动=%zu 均=%+.4f 标准差=%.4f 范围=[%+.4f %+.4f] 更新=%lld\n",
					g_mb.edges.size(), pl_moved, pl_mean, pl_sd, pl_min, pl_max, g_mb.n_updates);
			}
			if (g_vis.ready)
			{
				auto count_sp = [&](const std::vector<xhqm::size>& v)
				{
					int c = 0;
					for (xhqm::size i : v) if (i < g_br.s.size() && g_br.s[i] > 0.5f) ++c;
					return c;
				};
				std::printf("         视觉[%s]: 小眼=%d 柱=%zu 差分×1e4 ON=%.2f OFF=%.2f | 放电 光感受器%d/%zu lamina%d/%zu T4 %d/%zu T5 %d/%zu | every=%d\n",
					vis_on ? "开" : "关",
					g_vis.n_ommatidia, g_vis.n_cart, (double)g_vis.last_on * 1e4, (double)g_vis.last_off * 1e4,
					count_sp(g_pr_all), g_pr_all.size(), count_sp(g_lam_all), g_lam_all.size(),
					count_sp(g_t4_all), g_t4_all.size(), count_sp(g_t5_all), g_t5_all.size(),
					g_vis.vis_every);
			}
		}
	});
	anim.start();

	if (!snap.isEmpty())
	{
		QTimer::singleShot(1500, [&viewport, snap]() {
			viewport.snapshot_to(snap.toStdString());
			QCoreApplication::quit();
		});
		viewport.start();
		return app.exec();
	}
	viewport.start();
	// 自动退出：脚本化调参用。程序自己退出 → 缓冲区正常 flush，不会丢日志
	if (run_sec > 0.0)
	{
		QTimer::singleShot((int)(run_sec * 1000.0), []() { QCoreApplication::quit(); });
		std::printf("[run-sec] 将在 %.1f 秒后自动退出\n", run_sec);
	}
	return app.exec();
}
