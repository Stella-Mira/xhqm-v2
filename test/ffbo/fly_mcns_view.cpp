// 数字果蝇全通路（MaleCNS v1.0 版）
//   MaleCNS 单一整合连接组（166,700 神经元 / 25,582,938 突触，带突触符号）
//     + FlyBody 身体（MuJoCo）+ fantasy 渲染 + 复眼 raycast 视觉
//
// 与归档版 fly_brain_manc_view 的区别（都是结构性修正，不是调参）：
//   1. 连接组换成 MaleCNS v1.0 单一体数据集，去掉 FAFB+MANC 的「类型名桥接」整层；
//   2. 突触带符号（GABA/谷氨酸/组胺为抑制）——归档版权重全正，实测会让 99.8% 神经元
//      以 48Hz 饱和，信号退化成直流；
//   3. 权重按突触后入度归一化（每行 |权重| 和 = 1，与 fly64/ornata 同口径），
//      使阈值标定有意义；
//   4. LIF 复位到 0（归档版用减阈值复位，强驱动下 v 无界增长到 16.6、一批神经元粘死满发）；
//   5. 工作点解析设定 I_bg = m*V_th/tau，静息 v_ss = m*V_th；
//   6. 视觉是真实 raycast → 光感受器电流注入，没有 ON/OFF 柱状 hack；
//      光感受器是组胺能（抑制性），果蝇视觉靠光的变化，故注入的是真实亮度序列。
//
// 用法：fly_mcns_view <malecns目录> <fruitfly.xml> [选项]
//   --sec N        运行 N 秒后退出（脚本化用）
//   --bg-m M       静息电位占阈值的比例（默认 0.86）
//   --syn-gain G   突触总增益（默认 40）
//   --vis-off      关闭视觉注入（对照）
//   --predator     启用逼近刺激（球体向果蝇逼近，周期往复）
//   --snapshot png 渲染一帧后退出

#include "RhiViewport.h"
#include <theory_render.h>      // theory_shadow（--no-shadow / --shadow-size 直接操作管线 pass）
#include "cell_backend.h"       // 可切换细胞模型后端（lif / bio + 分级视觉链）

#include <mujoco/mujoco.h>

#include <QGuiApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using u64 = std::uint64_t;
// 本线程累计 CPU 时间（毫秒）——与 RhiViewport.cpp 里那个同名同作用，这里单独一份给基准用
static double thread_cpu_ms()
{
#if defined(_WIN32)
	FILETIME c, e, k, u;
	if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0.0;
	auto f = [](const FILETIME& t) { return (double)(((unsigned long long)t.dwHighDateTime << 32) | t.dwLowDateTime); };
	return (f(k) + f(u)) / 10000.0;
#else
	return 0.0;
#endif
}using xhqm::fantasy::models;
using xhqm::fantasy::prefab;
using xhqm::fantasy::world;
using xhqm::fantasy::light;
using xhqm::fantasy::light_desc;
using xhqm::fantasy::light_type;
using xhqm::fantasy::point;
using xhqm::fantasy::value;
// poit 由 xhqm 命名空间提供，在 main 里用 using namespace 引入（与归档程序同一做法）
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
	// 空闲等待策略。实测教训：原来无条件 std::this_thread::yield() 自旋，
	// 32 个逻辑核被 32 个 worker 顶满（实测进程吃掉 2839% CPU），
	// 主线程渲染被同核 SMT 兄弟线程挤，帧图执行时间翻倍。
	// sleep：自旋一小段（吃低延迟）后让出，避免长时间占满核心。
	enum class wait_mode { spin, sleep };
	wait_mode m_wait = wait_mode::sleep;
	explicit par_pool(unsigned n) : m_n(n), m_begin(n), m_end(n)
	{
		m_workers.reserve(n);
		for (unsigned t = 0; t < n; ++t)
			m_workers.emplace_back([this, t]()
			{
				int last = 0;
				while (true)
				{
					int spins = 0;
					while (m_gen.load(std::memory_order_acquire) == last
						&& !m_stop.load(std::memory_order_acquire))
					{
						if (m_wait == wait_mode::spin) { std::this_thread::yield(); continue; }
						// 先自旋 2000 次（覆盖短任务间隙），再按 0/1ms 阶梯退避
						if (++spins < 2000) { std::this_thread::yield(); continue; }
						std::this_thread::sleep_for(std::chrono::microseconds(spins < 4000 ? 0 : 200));
					}
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
			if (nv >= P.th[i]) { P.s_new[i] = 1.0f; P.v[i] = 0.0f; ++local; }   // 复位到静息（非减阈值）
			else { P.s_new[i] = 0.0f; P.v[i] = nv; }
		}
		if (local) fired.fetch_add(local, std::memory_order_relaxed);
	});
	P.s.swap(P.s_new);
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



// ══════════════════ 本体补充的全局量 ══════════════════
static int g_head_body = -1, g_body_up_body = -1, g_predator_geom = -1;
static double m_gp0[3] = { 0, 0, 0 };
static double g_dark_frac = 0.0;                      // 视野里被暗物覆盖的比例（刺激强度）
	static std::unordered_map<std::string, double> g_dp;  // 逐 superclass 的群体 d'
static double g_loom_phase = 0.0;                     // 逼近周期相位（0~0.8 为逼近段）
static float g_occlusion = 1.0f;                      // 逼近物体造成的整体照度衰减（遮挡天空）
// 步态（外部节律）。已知取舍：真正的步态节律应由腹神经索的 CPG 产生，
// 这里先用外部振荡器替代（属「身体侧」而非「脑内干预」），转向由神经活动决定。
static float g_gait_amp = 100.0f, g_gait_hz = 4.0f;
static double g_gait_phase = 3.14159265358979323846;
// ══════════════════ 肌肉级映射 ══════════════════
// ══════════════════ MaleCNS 网络 / 肌肉组 / 复眼 全局量 ══════════════════
// 注意：必须定义在「肌肉级映射」之前 —— 那张表会用到 g_cd_motor。
static std::vector<std::string> g_superclass, g_type, g_side;
static std::unordered_map<std::string, std::vector<xhqm::size>> g_groups;
static std::vector<std::vector<xhqm::size>> g_cd_motor;      // 肌肉组（Sub Class|side）
static std::vector<std::string> g_cd_motor_cls;
static std::vector<int> g_motor_side;

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


// ══════════════════ MaleCNS 装载 ══════════════════
static std::vector<std::string> split_ws(const std::string& s)
{
	std::vector<std::string> out; std::istringstream ss(s); std::string t;
	while (ss >> t) out.push_back(t);
	return out;
}

static bool load_csr(const std::string& path, netcsr& T, xhqm::size& n)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) { std::fprintf(stderr, "打不开 %s\n", path.c_str()); return false; }
	char magic[4]; f.read(magic, 4);
	if (std::memcmp(magic, "MNC1", 4) != 0) { std::fprintf(stderr, "csr magic 不对\n"); return false; }
	std::uint32_t ver = 0, pad = 0; std::uint64_t nn = 0, nnz = 0;
	f.read((char*)&ver, 4); f.read((char*)&pad, 4);
	f.read((char*)&nn, 8); f.read((char*)&nnz, 8);
	n = (xhqm::size)nn;
	T.rp.resize(n + 1); T.col.resize((std::size_t)nnz); T.val.resize((std::size_t)nnz);
	std::vector<std::uint64_t> rp64(n + 1);
	f.read((char*)rp64.data(), (std::streamsize)((n + 1) * 8));
	for (xhqm::size i = 0; i <= n; ++i) T.rp[i] = (xhqm::size)rp64[i];
	std::vector<std::uint32_t> c32((std::size_t)nnz);
	f.read((char*)c32.data(), (std::streamsize)(nnz * 4));
	for (std::size_t i = 0; i < (std::size_t)nnz; ++i) T.col[i] = (xhqm::size)c32[i];
	f.read((char*)T.val.data(), (std::streamsize)(nnz * 4));
	std::printf("csr.bin: n=%zu nnz=%zu\n", n, (std::size_t)nnz);
	return true;
}

static bool load_neurons(const std::string& path, xhqm::size n)
{
	std::ifstream f(path);
	if (!f) return false;
	std::string line; std::getline(f, line);
	g_type.assign(n, ""); g_superclass.assign(n, ""); g_side.assign(n, "");
	while (std::getline(f, line))
	{
		if (line.empty()) continue;
		// 简单 CSV 拆分（type/superclass/side 都不含逗号）
		std::vector<std::string> c; std::string cur; bool q = false;
		for (char ch : line)
		{
			if (ch == '"') { q = !q; continue; }
			if (ch == ',' && !q) { c.push_back(cur); cur.clear(); continue; }
			cur.push_back(ch);
		}
		c.push_back(cur);
		if (c.size() < 6) continue;
		const xhqm::size i = (xhqm::size)std::strtoull(c[0].c_str(), nullptr, 10);
		if (i >= n) continue;
		g_type[i] = c[2]; g_superclass[i] = c[3]; g_side[i] = c[5];
	}
	return true;
}

static bool load_groups(const std::string& path)
{
	std::ifstream f(path);
	if (!f) return false;
	std::string line;
	while (std::getline(f, line))
	{
		if (line.empty() || line[0] == '#') continue;
		std::vector<std::string> t = split_ws(line);
		if (t.size() < 2) continue;
		std::vector<xhqm::size> v;
		for (std::size_t i = 2; i < t.size(); ++i) v.push_back((xhqm::size)std::strtoull(t[i].c_str(), nullptr, 10));
		g_groups[t[0]] = v;
	}
	return true;
}

// 肌肉组：Sub Class 形如 MN-LegNpT1-Ti_flexor（含肌肉名与胸节），侧别来自 MANC 的 Soma side
static bool load_motor_groups(const std::string& path)
{
	std::ifstream f(path);
	if (!f) return false;
	std::string line;
	std::unordered_map<std::string, int> key2grp;
	while (std::getline(f, line))
	{
		if (line.empty() || line[0] == '#') continue;
		std::vector<std::string> t = split_ws(line);
		if (t.size() < 2) continue;
		const std::string key = t[0];                       // <Sub Class>|<side>
		const std::size_t bar = key.rfind('|');
		const std::string sd = (bar == std::string::npos) ? std::string() : key.substr(bar + 1);
		auto it = key2grp.find(key);
		int gi;
		if (it == key2grp.end())
		{
			gi = (int)g_cd_motor.size();
			key2grp.emplace(key, gi);
			g_cd_motor.emplace_back();
			g_cd_motor_cls.push_back(key);
			g_motor_side.push_back(sd == "left" ? -1 : (sd == "right" ? 1 : 0));
		}
		else gi = it->second;
		for (std::size_t i = 2; i < t.size(); ++i)
			g_cd_motor[gi].push_back((xhqm::size)std::strtoull(t[i].c_str(), nullptr, 10));
	}
	std::printf("肌肉组: %zu 组\n", g_cd_motor.size());
	return !g_cd_motor.empty();
}

// ══════════════════ 复眼（光学柱 → 小眼方向）══════════════════
// 光感受器只有 28/6006 有实测 soma 坐标（在视网膜内、不在 CNS 电镜体积），
// 所以小眼方向必须来自光学柱编号，不能用坐标。
struct ommatidium
{
	double az = 0, el = 0;                 // 方位/仰角（弧度；az>0 = 果蝇右侧，0 = 正前方）
	std::vector<xhqm::size> pr;            // 该小眼的全部光感受器
	int side = 0;                          // -1 左 / +1 右
};

static std::vector<ommatidium> g_om;

static bool build_ommatidia(const std::string& path)
{
	std::ifstream f(path);
	if (!f) { std::fprintf(stderr, "打不开 %s\n", path.c_str()); return false; }
	std::unordered_map<long long, std::size_t> key2om;
	std::string line;
	while (std::getline(f, line))
	{
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ss(line);
		std::size_t i; std::string sd; int h1 = 0, h2 = 0, fl = 0;
		if (!(ss >> i >> sd >> h1 >> h2 >> fl)) continue;
		// 侧别在本项目的数据里可能是 'L'/'R'（光学柱表原始字母）也可能是 'left'/'right'，两种都收
		const bool is_left = (sd == "left" || sd == "L" || sd == "l");
		const long long k = (long long)h1 * 1000 + h2 + (is_left ? 1000000 : 0);
		auto it = key2om.find(k);
		std::size_t oi;
		if (it == key2om.end())
		{
			oi = g_om.size();
			key2om.emplace(k, oi);
			ommatidium o;
			o.side = is_left ? -1 : 1;
			const double u = (h1 - 2) / 34.0, v = (h2 - 1) / 38.0;
			const double az_deg = is_left ? (-135.0 + u * 143.5) : (8.5 - u * 143.5);
			o.az = az_deg * 3.14159265358979323846 / 180.0;
			o.el = (72.0 - v * 144.0) * 3.14159265358979323846 / 180.0;
			g_om.push_back(o);
		}
		else oi = it->second;
		g_om[oi].pr.push_back(i);
	}
	std::printf("复眼: %zu 个小眼（光学柱）\n", g_om.size());
	// 校验左右：左眼小眼的方位角应整体偏负（az<0 = 左侧）
	{
		double sl = 0, sr = 0; std::size_t nl = 0, nr = 0;
		for (auto& o : g_om) { if (o.side < 0) { sl += o.az; ++nl; } else { sr += o.az; ++nr; } }
		std::printf("  左眼均方位 %.1f°（%zu 个）右眼均方位 %.1f°（%zu 个）\n",
			(nl ? sl / nl : 0.0) * 180 / 3.14159265358979323846, nl,
			(nr ? sr / nr : 0.0) * 180 / 3.14159265358979323846, nr);
	}
	return !g_om.empty();
}

// 把方位/仰角（头部局部系）转成世界方向：局部 x = 右，y = 前，z = 上
static void eye_dir_world(const double* R, double az, double el, double* out)
{
	const double lx = std::sin(az) * std::cos(el);
	const double ly = std::cos(az) * std::cos(el);
	const double lz = std::sin(el);
	out[0] = R[0] * lx + R[3] * ly + R[6] * lz;
	out[1] = R[1] * lx + R[4] * ly + R[7] * lz;
	out[2] = R[2] * lx + R[5] * ly + R[8] * lz;
}

// ══════════════════ 主程序 ══════════════════
int main(int argc, char** argv)
{
	using namespace xhqm;
	using namespace xhqm::fantasy;
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc < 3) { std::fprintf(stderr, "用法: fly_mcns_view <malecns目录> <fruitfly.xml> [选项]\n"); return 2; }
	const std::string dir = argv[1], xml_path = argv[2];
	double sec = 0.0, bg_m = 0.86, syn_gain = 40.0, vis_gain = 5.0;
	bool vis_on = true, predator = false, tether = false, label_random = false;
	bool synth_inject = false;                // --inject-synth：绕过 raycast，用 mcns_net 同款合成 loom
	bool label_mode_net = false;              // --label-mode-net：相位窗口用 mcns_net 的 80/20 口径
	// 细胞模型后端（与 mcns_net 同一套）：默认 lif 保持原路径不变，bio 用 test/cellsim 的电导型模型。
	// 分级视觉链把光感受器变成「输出=亮度」的传感器、LMC/髓质变成分级细胞 —— 实测这是让视觉信号
	// 跨过第一级突触的唯一办法（脉冲模式下光感受器 25Hz 而 L1-L4 只 0.15~0.64Hz）。
	std::string cell_model = "lif";
	bool bio_graded = false;
	float graded_base = 0.0f, graded_amp = 1.0f, norm_gain = 0.0f;
	// 传感器偏差增益：photo_i_cal 对应的是「亮度偏差走满整个分级量程」，
	// 而实际场景的逐小眼亮度偏差只有 ±0.05 量级 ⇒ 默认增益下视觉注入对网络几乎无影响
	// （实测开/关视觉的活动差只有 0.7%、轨迹逐位相同）。
	float sensor_gain = 1.0f;
	bool no_render = false;                  // --no-render：不起视口帧循环（纯仿真实验）
	double phys_dt = 0.0;                    // >0 时覆盖 MuJoCo 时间步（xml 默认 0.0001s）
	bool no_light = false;                   // --no-light：不加光源（隔离阴影 pass 开销）
	bool no_shadow = false;                  // --no-shadow：关掉 shadow pass（隔离阴影开销）
	int shadow_size = 0;                     // >0 时改阴影图分辨率（默认 2048）
	int bench_bind = 0;                      // >0 时测 N 次 bind_texture（= 1 次 vkUpdateDescriptorSets）
	int threads = 0;                         // >0 时限制并行池线程数（默认 hardware_concurrency）
	bool pool_spin = false;                  // --pool-spin：worker 空闲时自旋（旧行为，吃满 CPU）
	int hide_after = -1;                     // >=0 时只显示前 K 个模型（拆固定开销 vs 逐模型开销）
	bool dump_drawers = false;               // --dump-drawers：打印每个对象的 drawer 数
	double asym_motor = 0.0;                 // --asym-motor A：正对照，见主循环里的说明
	double g_loom_period = 4.0;              // --loom-period T：loom 周期（两相窗口 = 0.375T）
	double decoder_gain = 0.0;               // --decoder-gain K：解码器闭环增益（0=只估计不施加）
	bool decoder_permute = false;            // --decoder-permute：把权重随机置换（对照）
	double homeo_tau = 0.0;                  // --homeo-tau S：每类死区自稳时间常数（0=关）
	double homeo_lo = 0.05, homeo_hi = 0.50; // 死区（落在带内就不动）
	bool perf_pass = false;                  // --perf-pass：逐 pass 计时（绕开 fg->run 的整段计时）
	int bench_off = 0;                       // >0 时做离屏/traverse/绘制步骤级基准
	bool no_gizmo = false;                   // --no-gizmo：隐藏相机坐标 gizmo
	std::string snap, log_csv;
	int snap_step = 0;                       // >0 时在第 N 个神经步快照（A/B 对照用，不随帧率漂）
	for (int i = 3; i < argc; ++i)
	{
		if (!std::strcmp(argv[i], "--sec") && i + 1 < argc) sec = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--bg-m") && i + 1 < argc) bg_m = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--syn-gain") && i + 1 < argc) syn_gain = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--vis-gain") && i + 1 < argc) vis_gain = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--vis-off")) vis_on = false;
		else if (!std::strcmp(argv[i], "--predator")) predator = true;
		else if (!std::strcmp(argv[i], "--tether")) tether = true;
		else if (!std::strcmp(argv[i], "--label-random")) label_random = true;
		else if (!std::strcmp(argv[i], "--inject-synth")) synth_inject = true;
		else if (!std::strcmp(argv[i], "--label-mode-net")) label_mode_net = true;
		else if (!std::strcmp(argv[i], "--cell-model") && i + 1 < argc) cell_model = argv[++i];
		else if (!std::strcmp(argv[i], "--bio-graded")) bio_graded = true;
		else if (!std::strcmp(argv[i], "--graded-base") && i + 1 < argc) graded_base = (float)atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--graded-amp") && i + 1 < argc) graded_amp = (float)atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--norm-gain") && i + 1 < argc) norm_gain = (float)atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--sensor-gain") && i + 1 < argc) sensor_gain = (float)atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--no-render")) no_render = true;
		else if (!std::strcmp(argv[i], "--phys-dt") && i + 1 < argc) phys_dt = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--no-light")) no_light = true;
		else if (!std::strcmp(argv[i], "--no-shadow")) no_shadow = true;
		else if (!std::strcmp(argv[i], "--shadow-size") && i + 1 < argc) shadow_size = atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--bench-bind") && i + 1 < argc) bench_bind = atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--pool-spin")) pool_spin = true;
		else if (!std::strcmp(argv[i], "--hide-after") && i + 1 < argc) hide_after = atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--dump-drawers")) dump_drawers = true;
		else if (!std::strcmp(argv[i], "--perf-pass")) perf_pass = true;
		else if (!std::strcmp(argv[i], "--bench-off") && i + 1 < argc) bench_off = atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--no-gizmo")) no_gizmo = true;
		else if (!std::strcmp(argv[i], "--asym-motor") && i + 1 < argc) asym_motor = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--loom-period") && i + 1 < argc) g_loom_period = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--decoder-gain") && i + 1 < argc) decoder_gain = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--decoder-permute")) decoder_permute = true;
		else if (!std::strcmp(argv[i], "--homeo-tau") && i + 1 < argc) homeo_tau = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--homeo-lo") && i + 1 < argc) homeo_lo = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--homeo-hi") && i + 1 < argc) homeo_hi = atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--log-csv") && i + 1 < argc) log_csv = argv[++i];
		else if (!std::strcmp(argv[i], "--snapshot") && i + 1 < argc) snap = argv[++i];
		else if (!std::strcmp(argv[i], "--snapshot-step") && i + 1 < argc) snap_step = atoi(argv[++i]);
	}

	// ── 1. 网络 ──
	netcsr T; xhqm::size n = 0;
	if (!load_csr(dir + "\\csr.bin", T, n)) return 1;
	if (!load_neurons(dir + "\\neurons.csv", n)) return 1;
	if (!load_groups(dir + "\\groups.txt")) return 1;
	if (!load_motor_groups(dir + "\\motor_groups.txt")) return 1;
	if (vis_on && !build_ommatidia(dir + "\\visual_columns.txt")) return 1;
	for (float& v : T.val) v *= (float)syn_gain;

	const float dt = 0.020f, tau = 0.100f, V_th = 1.0f;
	const float I_bg = (float)(bg_m * V_th / tau);
	population P;
	std::mt19937 rng(20260303u);
	P.init(n, rng);
	{
		std::uniform_real_distribution<float> d(0.85f, 1.15f);
		for (xhqm::size i = 0; i < n; ++i) P.th[i] = V_th * d(rng);
	}
	P.t = &T;
	// ── 细胞模型后端 ──
	// lif：原封不动的既有路径（回归基准，必须复现旧数字）
	// bio：test/cellsim 的电导型模型；配 --bio-graded 时启用分级视觉链
	const bool use_bio = (cell_model == "bio");
	cellbackend::bio BIO;
	std::vector<char> g_sensory;
	float photo_i_cal = 0.0f;
	if (use_bio)
	{
		BIO.init(n, P.th, dt, (float)(bg_m * V_th), tau, 1.0f);
		BIO.norm_gain = norm_gain;
		if (bio_graded)
		{
			// 分级作用域与 mcns_net 一致：光感受器=传感器（输出=亮度），LMC(L1-L5)+髓质(Mi/Tm/T1/T2/T3)=分级，
			// T4/T5 及下游保持脉冲（它们是第一个发放的细胞）。
			const float HALF = 0.26f;
			photo_i_cal = ((2.0f * HALF) / tau) * sensor_gain;
			BIO.out_rest.assign(n, 0.0f);
			BIO.dev_base = graded_base;
			BIO.dev_amp = graded_amp;
			g_sensory.assign(n, 0);
			std::size_t nsens = 0, ngrad = 0;
			for (xhqm::size i = 0; i < n; ++i)
			{
				const std::string& sc = g_superclass[i];
				const std::string& tp = g_type[i];
				int k = 0;
				if (sc == "ol_sensory") k = 1;
				else
				{
					const bool lamina = (tp.size() == 2 && tp[0] == 'L' && tp[1] >= '1' && tp[1] <= '5');
					const bool medulla = (tp.rfind("Mi", 0) == 0 || tp.rfind("Tm", 0) == 0 ||
						tp.rfind("T1", 0) == 0 || tp.rfind("T2", 0) == 0 || tp.rfind("T3", 0) == 0);
					if (lamina || medulla) k = 2;
				}
				if (!k) continue;
				cellsim::cell_param& q = BIO.p[i];
				q.graded = 1;
				q.v_th = 1e9f;
				// 传感器用【双极】量程：静息=量程中点（输出 0.5），既能报变亮也能报变暗。
				// 第一版设成 v_rest→+0.52（只能报变亮），结果环境亮度被当常量送下去 ⇒ 网络饱和
				// （实测本步发放 34858/166700 = 20.9%）。out_rest=0.5 后 dev_base 才能对它生效。
				if (k == 1) { g_sensory[i] = 1; q.graded_v0 = q.v_rest - HALF; q.graded_v1 = q.v_rest + HALF; BIO.out_rest[i] = 0.5f; ++nsens; }
				else { q.graded_v0 = q.v_rest - HALF; q.graded_v1 = q.v_rest + HALF; BIO.out_rest[i] = 0.5f; ++ngrad; }
			}
			std::printf("分级视觉链: 光感受器 %zu 个（传感器，输出=亮度，标定 I=%.1f）| LMC+髓质 %zu 个（分级）"
				"| base=%.2f amp=%.2f norm=%.3f\n", nsens, photo_i_cal, ngrad, graded_base, graded_amp, norm_gain);
		}
		else std::printf("细胞模型: bio（电导型，无分级链）norm=%.3f\n", norm_gain);
		BIO.self_check();
	}
	static std::atomic<long long> fired{ 0 };
	const unsigned nthreads = (threads > 0)
		? std::min((unsigned)threads, std::max(1u, std::thread::hardware_concurrency()))
		: std::max(1u, std::thread::hardware_concurrency());
	par_pool pool(nthreads);
	pool.m_wait = pool_spin ? par_pool::wait_mode::spin : par_pool::wait_mode::sleep;
	std::printf("并行池: %u 线程，空闲等待=%s\n", nthreads, pool_spin ? "spin(自旋)" : "sleep(退避)");
	// 背景噪声：确定性 LIF 在亚阈值静息时永不发放（实测全网 0 发放）
	const std::size_t NMASK = (1u << 20) - 1;
	std::vector<float> noise_tab(NMASK + 1);
	{ std::normal_distribution<float> nd(0.0f, 1.0f); for (std::size_t i = 0; i <= NMASK; ++i) noise_tab[i] = nd(rng); }
	const float I_noise = (float)(0.022 / dt);
	std::printf("网络: %zu 神经元 / %zu 突触 | dt=%.0fms tau=%.0fms I_bg=%.2f（静息 v_ss=%.2f）| 线程 %u\n",
		n, (std::size_t)T.rp[n], dt * 1000, tau * 1000, I_bg, (double)bg_m, nthreads);

	// ── 2. 视觉索引：光感受器 → 所属小眼 ──
	std::vector<std::vector<int>> om_of(n);
	for (std::size_t k = 0; k < g_om.size(); ++k)
		for (xhqm::size i : g_om[k].pr) if (i < n) om_of[i].push_back((int)k);

	QGuiApplication app(argc, argv);

	// ── 3. MuJoCo 身体 ──
	char error[1000] = "";
	g_m = mj_loadXML(xml_path.c_str(), nullptr, error, 1000);
	if (!g_m) { std::printf("mj_loadXML 失败: %s\n", error); return 1; }
	const double dt_phys_xml = g_m->opt.timestep;   // xml 原值（--phys-dt 覆盖前记下来）
	g_d = mj_makeData(g_m);
	g_d->qpos[2] = 0.3f;
	mj_forward(g_m, g_d);
	for (int s = 0; s < 600; ++s) mj_step(g_m, g_d);      // 沉降站稳
	g_head_body = mj_name2id(g_m, mjOBJ_BODY, "head");
	g_body_up_body = mj_name2id(g_m, mjOBJ_BODY, "thorax");
	g_predator_geom = mj_name2id(g_m, mjOBJ_GEOM, "odor_source");
	std::printf("MuJoCo: nbody=%d nu=%d head=%d 捕食者 geom=%d\n",
		(int)g_m->nbody, (int)g_m->nu, g_head_body, g_predator_geom);
	if (g_predator_geom >= 0)
	{
		m_gp0[0] = g_m->geom_pos[3 * g_predator_geom + 0];
		m_gp0[1] = g_m->geom_pos[3 * g_predator_geom + 1];
		m_gp0[2] = g_m->geom_pos[3 * g_predator_geom + 2];
		// 半径从 0.02m 放大到 0.06m：原半径在 0.35m 处只占约 6.5° 视角，
		// 覆盖视野不到 0.3%，逼近时也才 3%，根本形不成 looming。
		// 0.06m 在 0.35m 处约 19°、逼近到 0.09m 时约 74°，才是真正的逼近刺激。
		for (int k = 0; k < 3; ++k) g_m->geom_size[3 * g_predator_geom + k] = 0.06;
		std::printf("捕食者: geom=%d 半径 0.06m\n", g_predator_geom);
	}
	build_motor_map();
	std::printf("actuators=%d 肌肉组=%zu\n", (int)g_m->nu, g_cd_motor.size());

	// ── 4. fantasy 视口 ──
	RhiViewport viewport(RhiViewport::Backend::Vulkan);
	viewport.setTitle("Digital Fruit Fly - MaleCNS");
	viewport.resize(960, 720);
	viewport.show();
	// 实测教训：coord gizmo 只有 4 个 drawer，但 ploys 每个 topol 发一次 rhi.draw ⇒ 199 draw/帧，
	// 而引擎每次 draw 固定 ~0.196ms（Vulkan 后端每绘制把 38240 字节 UBO 从 host-visible 内存整块复制），
	// 于是 gizmo 独占 78ms/帧（两个 pass），比果蝇本体还贵。纯仿真视口默认不要它。
	if (no_gizmo) { viewport.gizmo(false); std::printf("★ --no-gizmo：不创建坐标 gizmo\n"); }
	app.processEvents();
	if (!viewport.initialize()) { std::printf("[FAIL] viewport initialize\n"); return 1; }
	// ★ 启动视口自己的渲染帧循环（RhiViewport::start() → m_timer(35ms) → frame()）。
	//   漏掉这一句的后果：窗口从来没渲染过 —— 一直是白的，而且【只有鼠标点击产生的
	//   expose 事件才会逼它重绘一帧】（因为 mouse 处理里会调 frame）。实测就是这样被发现的。
	//   快照（snapshot_to）走的是离屏路径（头文件写着「无头读回，不受窗口可见性影响」），
	//   所以快照一直正常、掩盖了这个缺陷。
	// --no-render：不起视口帧循环（纯仿真跑实验用）。实测渲染每帧约 105ms —— 占每步总耗时
	// （约 190ms）的 55%，而网络只占 17ms。要看画面就受它限制，要跑实验就关掉它。
	if (!no_render) viewport.start();
	else std::printf("★ 已关闭视口渲染（--no-render）：窗口不会刷新，用于纯仿真实验\n");
	if (perf_pass) { viewport.per_pass(true); std::printf("★ --perf-pass：逐 pass 计时（绕开 fg->run 的整段计时）\n"); }
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
		const int bid = g_m->geom_bodyid[g];
		if (!g_body_models[bid]) { g_body_models[bid] = new models(); w->add_target(g_body_models[bid]); }
		g_body_models[bid]->insert_drawer(p);
		++loaded;
	}
	std::printf("载入 %d 个 mesh\n", loaded);
	{
		light* lv = new light();
		lv->ambient(xhqm_rgba(70, 70, 70, 255));
		light_desc key; key.type = light_type::directional; key.color = 0xffffffff;
		key.direction = point(-0.35f, -0.55f, -0.76f);
		light_desc fill; fill.type = light_type::directional; fill.color = xhqm_rgba(150, 130, 110, 255);
		fill.direction = point(0.7f, 0.35f, -0.62f);
		// --no-light：不加任何光源，用来隔离【阴影 pass 的开销】（无光源则无级联可渲）。
		// 实测帧图执行要 109ms 而呈现只 0.3ms，管线只是 shadow→scene→tonemap，
		// 所以必须先把 shadow 那一半摘掉看时间。
		if (no_light) std::printf("★ --no-light：未加光源（隔离阴影开销用，画面会是黑的）\n");
		else { lv->add_light(key); lv->add_light(fill); }
		for (auto* m : g_body_models)
			if (m) for (auto* d : m->handle_drawers()) d->light_env(lv);
	}
	// --no-shadow / --shadow-size：直接操作管线里的 theory_shadow pass（公开 API，不动 xhqm 底层）。
	// 目的：把「帧图执行 109ms」拆成 shadow / scene 两半 —— 管线只有 shadow→scene→tonemap。
	// 注意 --no-light 并不能去掉阴影：theory_shadow 在无光源时走 gather_directional 的默认方向光，
	// 阴影图照样渲（所以 --no-light 前后 109.6 → 112.2ms 几乎不变）。
	if (no_shadow || shadow_size > 0)
	{
		for (auto* p : viewport.frame_graph_ptr()->passes())
		{
			auto* sh = dynamic_cast<theory_shadow*>(p);
			if (!sh) continue;
			if (no_shadow) sh->enabled(false);
			if (shadow_size > 0) sh->size((uint)shadow_size);
			std::printf("★ shadow pass: enabled=%d size=%d\n", no_shadow ? 0 : 1, shadow_size > 0 ? shadow_size : 2048);
		}
	}
	// --bench-bind N：直接量「每次绘制固定要做的 12 次 vkUpdateDescriptorSets」的单价。
	// 依据：帧图执行时间与绘制数严格线性（285 绘制 → 55.3ms，571 绘制 → 110.0ms，
	// 每绘制 0.193ms），且与阴影图分辨率完全无关（512/1024/2048/4096 全是 110ms）⇒
	// 不是填充率，是每绘制的 CPU/驱动固定开销。rhi_vk 的 draw 路径里唯一按绘制固定付 12 次
	// 的是 flush_textures → write_texture_binding → vkUpdateDescriptorSets。
	// bind_texture 走的正是 write_texture_binding，所以这个基准直接测该假设。
	// 用公开 RHI 接口测量，不改 xhqm 底层。
	if (bench_bind > 0)
	{
		render_backend& rhi = render_backend_active();
		uint prog = 0;
		for (auto* m : g_body_models)
		{
			if (!m) continue;
			for (auto* d : m->handle_drawers()) { prog = (uint)d->shader_id(); break; }
			if (prog) break;
		}
		std::printf("★ bench-bind: program=%u N=%d\n", prog, bench_bind);
		if (prog)
		{
			rhi.use_program(prog);
			// 预热
			for (int i = 0; i < 100; ++i) rhi.bind_texture(0, 0);
			const double t0 = std::chrono::duration<double, std::milli>(
				std::chrono::high_resolution_clock::now().time_since_epoch()).count();
			for (int i = 0; i < bench_bind; ++i) rhi.bind_texture(0, 0);
			const double t1 = std::chrono::duration<double, std::milli>(
				std::chrono::high_resolution_clock::now().time_since_epoch()).count();
			const double us = (t1 - t0) * 1000.0 / bench_bind;
			std::printf("★ bench-bind: %.2f us/次（%d 次共 %.1f ms）⇒ 每绘制 12 次 = %.3f ms\n",
				us, bench_bind, t1 - t0, us * 12.0 / 1000.0);
		}
	}
	// --hide-after K：只让前 K 个模型可见。用来把「帧图执行 110ms」拆成
	// 固定开销（清屏/离屏/tonemap）与随模型数增长的部分 —— 后者才说明是逐绘制开销。
	// --dump-drawers：广度遍历相机根，按对象打印类型 + drawer 数。
	// 动机：hide-after 0（果蝇 mesh 全隐）时每帧仍有 401 次绘制、固定吃 76.8ms，
	// 必须查清这 400 次绘制到底是谁发的（--hide-after 只关 g_body_models）。
	// （--no-gizmo 已在 initialize 之前生效：根本不创建 gizmo。这里不再重复处理。）
	if (dump_drawers)
	{
		size_t total_d = 0;
		std::printf("★ drawer 清单（相机根广度遍历）:\n");
		for (auto* o : (*viewport.camera_ptr()->root_ptr()))
		{
			auto* mm = dynamic_cast<models*>(o);
			if (!mm) { std::printf("    [%s] (非 models)\n", typeid(*o).name()); continue; }
			size_t dc = mm->handle_drawers().size();
			total_d += dc;
			std::printf("    [%s] show=%d drawers=%zu\n", typeid(*o).name(), mm->show() ? 1 : 0, dc);
		}
		std::printf("★ drawer 合计 %zu（每帧上屏绘制数会再乘 pass 数）\n", total_d);
	}
	if (hide_after >= 0)
	{
		size_t total = 0, shown = 0, drawers = 0;
		for (size_t i = 0; i < g_body_models.size(); ++i)
		{
			if (!g_body_models[i]) continue;
			++total;
			if ((int)shown < hide_after) { ++shown; drawers += g_body_models[i]->handle_drawers().size(); }
			else g_body_models[i]->show(false);
		}
		std::printf("★ hide-after %d：可见模型 %zu/%zu，可见 drawer %zu\n", hide_after, shown, total, drawers);
	}
	// --bench-off N：把 shadow/scene 两个 pass 的「固定 38ms」拆开量。
	// 现象：hide-after 0（果蝇 mesh 全隐）时 shadow 38.3ms、scene 38.1ms，两者都在 CPU 上烧；
	// 全可见时各 54.8ms（增量 16.5ms = 85 drawer × 0.19ms）。所以每 pass 有个约 38ms 的固定项，
	// 与几何无关、与阴影图分辨率无关。这里逐个复现 pass 内部步骤并单独计时。
	if (bench_off > 0)
	{
		using namespace xhqm::fantasy;
		render_backend& rhi = render_backend_active();
		camera* cam = viewport.camera_ptr();
		object* root = cam->root_ptr();
		std::string berr;
		uint tgt = rhi.create_offscreen(960, 720, 1, berr, true);
		std::printf("★ bench-off：target=%u err='%s' N=%d\n", tgt, berr.c_str(), bench_off);
		auto now = []() { return std::chrono::duration<double, std::milli>(
			std::chrono::high_resolution_clock::now().time_since_epoch()).count(); };
		auto rep = [&](const char* label, double w, double c) {
			std::printf("    %-28s 墙钟 %8.3f ms/次 | CPU %8.3f ms/次\n", label, w / bench_off, c / bench_off);
		};
		std::string ferr;
		rhi.begin_frame(ferr);
		if (!tgt || !ferr.empty())
		{
			std::printf("★ bench-off：begin_frame 失败 '%s'\n", ferr.c_str());
		}
		else
		{
			matrix window = cam->window();

			// A. 离屏切换（shadow/scene 各做一次 begin+end）
			{
				double w0 = now(), c0 = (double)0; (void)c0;
				const double p0 = thread_cpu_ms();
				for (int i = 0; i < bench_off; ++i) { rhi.begin_offscreen(tgt); rhi.end_offscreen(); }
				rep("A begin+end_offscreen", now() - w0, thread_cpu_ms() - p0);
			}
			// B. 清屏
			{
				double w0 = now();
				const double p0 = thread_cpu_ms();
				for (int i = 0; i < bench_off; ++i) rhi.clear(xhqm_rgba(0, 0, 0, 0));
				rep("B clear", now() - w0, thread_cpu_ms() - p0);
			}
			// C. system::traverse（每 pass 一次）
			{
				double w0 = now();
				const double p0 = thread_cpu_ms();
				for (int i = 0; i < bench_off; ++i) system::traverse(window, root, matrix(4, 4));
				rep("C traverse", now() - w0, thread_cpu_ms() - p0);
			}
			// D. updata_aabb 合并（shadow 的 AABB 收集循环）
			{
				double w0 = now();
				const double p0 = thread_cpu_ms();
				for (int i = 0; i < bench_off; ++i)
				{
					aabb box; bool have = false;
					for (auto* o : (*root))
					{
						auto* m = dynamic_cast<models*>(o);
						if (!m) continue;
						aabb local = m->updata_aabb();
						if (!local.good()) continue;
						matrix wm = system::transform(root, m);
						aabb world = local.access(wm);
						if (!have) { box = world; have = true; } else box.attach(world);
					}
					if (!have) { volatile int z = 0; (void)z; }
				}
				rep("D AABB merge", now() - w0, thread_cpu_ms() - p0);
			}
			// E. traverse + models::drawer（离屏内，每 pass 的绘制主体）
			{
				rhi.begin_offscreen(tgt);
				rhi.reset_draw_call_count();
				double w0 = now();
				const double p0 = thread_cpu_ms();
				for (int i = 0; i < bench_off; ++i)
				{
					system::traverse(window, root, matrix(4, 4));
					models::drawer(root);
				}
				rep("E traverse+drawer", now() - w0, thread_cpu_ms() - p0);
				std::printf("        E 期间绘制数 %u（共 %d 次）⇒ 每次 %.4f ms\n",
					rhi.draw_call_count(), bench_off, (now() - w0) / bench_off / (rhi.draw_call_count() / (double)bench_off ? rhi.draw_call_count() / (double)bench_off : 1.0));
				rhi.end_offscreen();
			}
			// F. 只有 begin_offscreen（不含 end）的对照：确认 A 不是被 end 主导
			{
				double w0 = now();
				const double p0 = thread_cpu_ms();
				for (int i = 0; i < bench_off; ++i) { rhi.begin_offscreen(tgt); rhi.end_offscreen(); }
				rep("F A 重复（对照）", now() - w0, thread_cpu_ms() - p0);
			}
			// G. 裸 rhi.draw：3 顶点小三角形，单独量「一次绘制调用」的固定开销。
			// 已知：gizmo 199 次绘制/帧 = 39ms（0.196ms/次），果蝇 85 drawer 增量 = 0.194ms/次，
			// 两种完全不同的 drawer 类型得到同一个单价 ⇒ 开销在共用的 draw 路径里，不在 drawer 里。
			{
				uint gprog = 0;
				for (auto* m : g_body_models)
				{
					if (!m) continue;
					for (auto* d : m->handle_drawers()) { gprog = (uint)d->shader_id(); break; }
					if (gprog) break;
				}
				std::string verr;
				std::vector<value> verts = { 0, 0, 0, 1,  1, 0, 0, 1,  0, 1, 0, 1 };
				std::vector<uint> locs = { 3, 1 };
				uint vb = rhi.vertex_create(verts, locs, verr);
				rhi.use_program(gprog);
				rhi.begin_offscreen(tgt);
				rhi.reset_draw_call_count();
				double w0 = now();
				const double p0 = thread_cpu_ms();
				for (int i = 0; i < bench_off; ++i) rhi.draw(vb, rhi_primitive::triangles, 0, 3);
				rep("G 裸 draw(3 顶点)", now() - w0, thread_cpu_ms() - p0);
				std::printf("        G 期间绘制数 %u（program=%u vb=%u err='%s'）\n",
					rhi.draw_call_count(), gprog, vb, verr.c_str());
				rhi.end_offscreen();
				rhi.use_program(0);
				if (vb) rhi.vertex_delete(vb);
			}
			rhi.end_frame();
			std::printf("★ bench-off 结束\n");
		}
		if (tgt) rhi.delete_offscreen(tgt);
	}
	for (int k = 0; k < 3; ++k) g_center[k] = 0.0;
	viewport.camera_ptr()->focus(poit<value>(0, 0, 0));
	viewport.camera_ptr()->dolly(30.0f);

	// ── 5. 统计与主循环 ──
	long long frame = 0;
	long long half_frames = 0;                // 交叉验证分界
	long long on_n = 0, off_n = 0;
	std::vector<double> on_sum(n, 0.0), off_sum(n, 0.0);
	// 交叉验证用：把记录切成前后两半，各自累计（用一半估解码权重、在另一半评估）
	long long on_n2 = 0, off_n2 = 0;
	std::vector<double> on_sum2(n, 0.0), off_sum2(n, 0.0);
	// ── 显著性统计的修正量（从 mcns_net 移植，原来本程序用的是伯努利方差）──
	// 两处错必须一起修（详见 README 诊断纪律、DESIGN_NOTES 第十一节）：
	//   #7 用伯努利方差 p(1−p)：那假设每步输出是 0/1，对【分级细胞】（输出 0~1 连续量）不成立。
	//      症状：分级链下 CV d′ 符号翻成 −333。
	//   #8 漏掉时间自相关：膜时间常数 100ms = 5 步，把每步当独立样本会把有效样本数高估约 5 倍、
	//      σ 低估约 2.2 倍（标称 3σ 实际只有 ~1.4σ）。
	// 修法：经验方差 E[out²]−E[out]² ＋ AR(1) 折算 n_eff = n(1−ρ)/(1+ρ)。
	// 只修 #7 会更糟（对照从 182 涨到 7570 显著），所以两者必须同时生效。
	std::vector<double> on_sq(n, 0.0), off_sq(n, 0.0), on_xy(n, 0.0), off_xy(n, 0.0);
	std::vector<double> on_sq2(n, 0.0), off_sq2(n, 0.0), on_xy2(n, 0.0), off_xy2(n, 0.0);
	std::vector<double> prev_out(n, 0.0);     // 上一步输出（算滞后一阶乘积用）
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
	auto var_of_one = [&](bool on, bool first_half, xhqm::size i) -> double
	{
		if (on) return first_half ? var_of_mean(on_sum, on_sq, on_xy, (double)on_n, i)
		                          : var_of_mean(on_sum2, on_sq2, on_xy2, (double)on_n2, i);
		return first_half ? var_of_mean(off_sum, off_sq, off_xy, (double)off_n, i)
		                  : var_of_mean(off_sum2, off_sq2, off_xy2, (double)off_n2, i);
	};
	std::vector<float> ray_dir;
	std::vector<double> ray_dist, ray_nrm;
	std::vector<int> ray_geom;
	std::vector<float> lum;
	std::vector<float> g_lum_avg;            // 逐小眼亮度的滑动平均（传感器只报相对它的变化）
	std::vector<float> pr_cur(n, 0.0f);       // 每个光感受器本帧的注入电流
	double t_sim = 0.0, t_pred = 0.0;
	// 物理时间步：xml 里是 0.0001s（0.1ms），于是每个神经步要跑 200 次 mj_step ——
	// 实测这占每步 60ms（关渲染后总耗时的 70%）。--phys-dt 可以覆盖它。
	// 风险：接触丰富的腿需要足够小的时间步，改大会不稳；所以要对比轨迹确认。
	if (phys_dt > 0.0) { g_m->opt.timestep = phys_dt; std::printf("物理时间步覆盖: %.4fs（xml 原值 %.4fs）\n", phys_dt, dt_phys_xml); }
	const double dt_phys = g_m->opt.timestep;
	const int phys_per_neural = (int)std::round(dt / dt_phys);
	std::printf("物理: 每个神经步 %d 次 mj_step（dt=%.0fms / dt_phys=%.2fms）\n",
		phys_per_neural, dt * 1000, dt_phys * 1000);
	half_frames = (sec > 0.0) ? (long long)(sec / dt * 0.5) : 0;
	double last_log = 0.0;
	// 系留：把果蝇固定在原地（腿仍能动）。研究果蝇视觉的经典做法 ——
	// 只有把自身运动去掉，刺激强度才不被果蝇自己的移动污染。
	const double qpos0[3] = { g_d->qpos[0], g_d->qpos[1], g_d->qpos[2] };
	double qpos_rest[7] = { 0 };
	if (tether) for (int k = 0; k < 7; ++k) qpos_rest[k] = g_d->qpos[k];
	std::ofstream csv;
	if (!log_csv.empty())
	{
		csv.open(log_csv, std::ios::binary);
		csv << "t,loom_phase,dark_frac,n_spike,motor_spikes,dn_spikes,muscle_mean,x,y,z,yaw,ctrl_left,ctrl_right,asym_ctrl" << std::endl;
	}
	// 行为方向性：按 actuator 名后缀分左右，比较两相的左右驱动差。
	// 这才是「转向指令」——肌肉 ctrl 的左右差就是身体真正接收到的转向信号。
	std::vector<int> act_left, act_right;
	for (int a = 0; a < g_m->nu; ++a)
	{
		const char* nm = mj_id2name(g_m, mjOBJ_ACTUATOR, a);
		if (!nm) continue;
		const std::string s(nm);
		if (s.size() > 5 && s.compare(s.size() - 5, 5, "_left") == 0) act_left.push_back(a);
		else if (s.size() > 6 && s.compare(s.size() - 6, 6, "_right") == 0) act_right.push_back(a);
	}
	// ── 每类死区自稳（--homeo-tau>0 开启，见 DESIGN_NOTES 第二十节）──
	// 把 g_superclass 映射成类编号，交给后端按类调节每细胞的兴奋性倍率。
	if (homeo_tau > 0.0 && use_bio)
	{
		std::unordered_map<std::string, int> cls;
		BIO.cell_class.assign(n, -1);
		for (xhqm::size i = 0; i < n; ++i)
		{
			if (g_superclass[i].empty()) continue;
			auto it = cls.find(g_superclass[i]);
			if (it == cls.end()) { int id = (int)cls.size(); cls[g_superclass[i]] = id; it = cls.find(g_superclass[i]); }
			BIO.cell_class[i] = it->second;
		}
		BIO.n_class = (int)cls.size();
		BIO.homeo_h.assign(n, 1.0f);
		BIO.homeo_tau = (float)homeo_tau;
		BIO.homeo_lo = (float)homeo_lo;
		BIO.homeo_hi = (float)homeo_hi;
		std::printf("★ 每类自稳: τ=%.1fs 死区[%.3f, %.3f] 类数=%d（%zu 个细胞参与）\n",
			homeo_tau, homeo_lo, homeo_hi, BIO.n_class, n);
	}
	else if (homeo_tau > 0.0)
		std::printf("★ --homeo-tau 只在 --cell-model bio 下有效（当前不是 bio），已忽略\n");
	std::printf("左右 actuator: 左 %zu 右 %zu\n", act_left.size(), act_right.size());
	// ── 解码器闭环（--decoder-gain K，规格见 DESIGN_NOTES 第十八节）──
	// 只读视叶（ol_intrinsic）状态、**不向网络注入任何东西**；权重在前半段用 Fisher 判别方向估，
	// 后半段才把 dec 变成左右不对称控制量（反乐观偏置：估与评分开）。
	std::vector<int> dec_cells;
	for (xhqm::size i = 0; i < n; i += 32)
		if (g_superclass[i] == "ol_intrinsic") dec_cells.push_back((int)i);
	const std::size_t dc = dec_cells.size();
	std::vector<double> dc_on(dc, 0.0), dc_off(dc, 0.0), dc_onq(dc, 0.0), dc_offq(dc, 0.0);
	std::vector<double> dc_w(dc, 0.0);
	long long dc_n_on = 0, dc_n_off = 0;
	double dc_mA = 0.0, dc_norm = 0.0;
	std::size_t dc_keep = 0;                 // 实际保留的细胞数（判别力前 N）
	bool dc_ready = false;
	std::printf("解码器: 抽样 %zu 个 ol_intrinsic 细胞（每 32 取 1）%s\n", dc,
		(decoder_gain > 0.0) ? "" : "（--decoder-gain 未开，仅估计不施加转向）");
	double as_on = 0.0, as_off = 0.0, as2_on = 0.0, as2_off = 0.0;
	long long asn_on = 0, asn_off = 0, asn2_on = 0, asn2_off = 0;
	// 行为判据的显著性（与神经统计同一套：经验方差 + AR(1) 折算 n_eff）。
	// 只报"均值差"是不够的：没有标准误就分不清"真差"与"噪声里的差"。
	double as_on_sq = 0.0, as_off_sq = 0.0, as_on_xy = 0.0, as_off_xy = 0.0;
	double as2_on_sq = 0.0, as2_off_sq = 0.0, as2_on_xy = 0.0, as2_off_xy = 0.0;
	double as_prev_blk = 0.0;                 // 上一【块】的左右驱动差（算滞后一阶乘积）
	// 步态周期块累加器（统计单位是块，不是神经步 —— 见主循环里的说明）
	double blk_sum = 0.0; int blk_n = 0, blk_on = 0, blk_off = 0;
	const int gait_block = std::max(1, (int)std::lround(2.0 / (g_gait_hz * dt)));   // 2 个步态周期
	double asym_inj_on = 0.0; long long asym_inj_n = 0;   // 记录实际注入了多少（自检）
	double dec_apply_sum = 0.0; long long dec_apply_n = 0;   // 解码器实际施加量（自检）
	double dd_on = 0.0, dd_off = 0.0, dd_onq = 0.0, dd_offq = 0.0;
	long long dd_n_on = 0, dd_n_off = 0;                     // 施加量本身的信噪比自检
	bool rnd2 = false;                        // 本帧是否用了打乱标签（供行为统计复用）
	double dk_on = 0.0, dk_off = 0.0, pr_i_on = 0.0, pr_i_off = 0.0;   // 两相的暗物覆盖与注入电流（刺激到位与否的诊断）
	long long dk_n_on = 0, dk_n_off = 0;                               // 上面两个量的独立样本计数
	QElapsedTimer wall; wall.start();
	// 分段计时累加器（诊断「一卡一卡」是谁吃的）：ray=视觉 raycast / net=网络一步 /
	// act=肌肉映射 / phys=MuJoCo 物理 / other=其余（含渲染被事件循环穿插的部分）
	double acc_ray = 0.0, acc_net = 0.0, acc_act = 0.0, acc_phys = 0.0, acc_t0 = 0.0;
	long long nsteps = 0;
	auto clk = []() { return std::chrono::duration<double>(std::chrono::high_resolution_clock::now().time_since_epoch()).count(); };
	std::printf("系留=%d 记录=%s\n", (int)tether, log_csv.empty() ? "无" : log_csv.c_str());

	QTimer anim;
	anim.setInterval(0);
	QObject::connect(&anim, &QTimer::timeout, [&]()
	{
		const double t0 = clk();
		double tprev = t0;                 // 顺序分段计时的游标（5a→5b→5c→5d 依次推进）
		// 5a. 视觉：复眼 raycast → 光感受器电流
		std::fill(pr_cur.begin(), pr_cur.end(), 0.0f);
		if (synth_inject && !g_om.empty())
		{
			// 判别实验用：绕过 raycast，直接按 mcns_net 同一套公式生成合成全场 loom
			// （r > R 为亮、r < R 为暗，R 随周期扩张；幅度 vis_gain，默认 80 与 mcns_net 一致）。
			// 目的：在同一程序/同一身体/同步态下，只换视觉输入来源，判断差距出在
			// 「视觉编码」还是「程序别处的差别」。
			const double ph = std::fmod(t_sim + dt, 4.0) / 4.0;
			const double R0 = 40.0 * 3.14159265358979323846 / 180.0;
			const double R = (ph < 0.8) ? (R0 * (ph / 0.8)) : 0.0;
			double dark = 0.0;
			for (std::size_t k = 0; k < g_om.size(); ++k)
			{
				const double r = std::sqrt(g_om[k].az * g_om[k].az + g_om[k].el * g_om[k].el);
				const float lt = (r > R) ? 1.0f : 0.0f;
				if (lt < 0.5f) dark += 1.0;
				for (xhqm::size i : g_om[k].pr) if (i < n) pr_cur[i] += (float)vis_gain * lt;
			}
			g_dark_frac = dark / (double)g_om.size();
		}
		else if (vis_on && g_head_body >= 0 && !g_om.empty())
		{
			const double* hx = g_d->xpos + 3 * g_head_body;
			const double* R = g_d->xmat + 9 * g_head_body;
			const std::size_t no = g_om.size();
			ray_dir.resize(3 * no); ray_dist.assign(no, 0.0); ray_nrm.assign(3 * no, 0.0);
			ray_geom.assign(no, -1); lum.assign(no, 1.0f);
		if (g_lum_avg.size() != no) g_lum_avg.assign(no, 0.75f);   // 初值取场景中灰附近的估计
			// 两眼各自的原点：头部局部 x = 右（模型里 eye_left 在 x=-0.0219）
			for (int e = 0; e < 2; ++e)
			{
				const double ox_l = (e == 0) ? -0.0219 : 0.0219;
				const double origin[3] = {
					hx[0] + R[0] * ox_l, hx[1] + R[1] * ox_l, hx[2] + R[2] * ox_l };
				std::size_t cnt = 0;
				std::vector<std::size_t> idx;
				for (std::size_t k = 0; k < no; ++k)
					if ((e == 0 && g_om[k].side < 0) || (e == 1 && g_om[k].side >= 0))
					{
						double d3[3];
						eye_dir_world(R, g_om[k].az, g_om[k].el, d3);
						ray_dir[3 * cnt] = d3[0]; ray_dir[3 * cnt + 1] = d3[1]; ray_dir[3 * cnt + 2] = d3[2];
						idx.push_back(k); ++cnt;
					}
				if (!cnt) continue;
				std::vector<int> gid(cnt, -1); std::vector<double> dist(cnt, 0.0), nrm(3 * cnt, 0.0);
				std::vector<double> dirs(3 * cnt);
				for (std::size_t k = 0; k < cnt; ++k)
				{ dirs[3 * k] = ray_dir[3 * k]; dirs[3 * k + 1] = ray_dir[3 * k + 1]; dirs[3 * k + 2] = ray_dir[3 * k + 2]; }
				mj_multiRay(g_m, g_d, origin, dirs.data(), nullptr, 1, g_head_body,
					gid.data(), dist.data(), nrm.data(), (int)cnt, 5.0);
				for (std::size_t k = 0; k < cnt; ++k)
				{
					float L;
					if (gid[k] < 0) L = 1.0f;                       // 未命中 = 天空
					else
					{
						const float* c = g_m->geom_rgba + 4 * gid[k];
						// 用场景里 geom 自己的颜色当亮度（捕食者是暗色，地面中灰，天空最亮）
						L = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
						if (gid[k] == g_predator_geom) L *= 0.25f;   // 逼近物体本身额外压暗
						// 逼近物体还会【遮挡光源、压低整体照度】——这是物理上真实的效应
						// （一个贴近的暗物会挡住天空）。没有这一项时，捕食者最多只挡住约 30%
						// 视野，其余天空/地面两相不变，总视觉驱动只能被调制约 15%，
						// 而实测网络对 15% 的调制毫无反应（能奏效的合成刺激调制深度约 100%）。
						L *= g_occlusion;
					}
					lum[idx[k]] = L;
					// 逐小眼的亮度滑动平均（EMA，约 1 秒）：传感器只报【相对局部平均的变化】。
					// 这是果蝇视觉的原理（本文件头注释就写着「果蝇视觉靠光的变化而不是光的强弱」），
					// 也是防止恒定环境亮度把网络灌饱和的唯一办法。
					float dev = L;
					if (!g_lum_avg.empty())
					{
						g_lum_avg[idx[k]] += (L - g_lum_avg[idx[k]]) * 0.05f;
						dev = L - g_lum_avg[idx[k]];
					}
					for (xhqm::size i : g_om[idx[k]].pr)
					{
						if (i >= n) continue;
						// 分级传感器：注入按「输出=亮度偏差」标定，静息在量程中点。
						// 脉冲模式沿用 vis_gain 与绝对亮度（原路径不变）。
						if (!g_sensory.empty() && g_sensory[i]) pr_cur[i] += photo_i_cal * dev;
						else pr_cur[i] += (float)vis_gain * L;
					}
				}
			}
			// 相位标签：视野里被暗物覆盖的比例（用于解码诊断，与刺激无关地定义）
			double dark = 0.0;
			for (std::size_t k = 0; k < no; ++k) if (lum[k] < 0.4f) dark += 1.0;
			g_dark_frac = dark / (double)no;
		}
		else g_dark_frac = 0.0;
		acc_ray += clk() - tprev; tprev = clk();   // 5a 结束

		// 5b. 网络一步
		{
			const unsigned h2 = (unsigned)(frame * 2654435761u);
			for (xhqm::size i = 0; i < n; ++i)
			{
				float I = I_bg + pr_cur[i];
				I += I_noise * noise_tab[(std::size_t)((h2 + (unsigned)i * 2246822519u) & NMASK)];
				P.cur[i] = I;
			}
			if (use_bio)
			{
				// ★ 对齐：bio 的漏电直接拉向 v_rest = bg_m*V_th，所以外部电流里【不能】再放 I_bg。
				// （分级传感器的电流在上面已按 photo_i_cal 标定，噪声也一并保留。）
				for (xhqm::size i = 0; i < n; ++i) P.cur[i] -= I_bg;
				cellbackend::bio_step(BIO, (std::size_t)n, T.rp.data(), T.col.data(), T.val.data(),
					P.cur.data(), P.s.data(), P.s_new.data(),
					[&](xhqm::size c, auto fn) { pool.run(c, fn); }, fired);
				P.s.swap(P.s_new);
			}
			else lif_step(P, pool, dt, tau, fired);
			// 解码统计：用逼近周期本身当相位标签（确定、与果蝇自身运动无关），
			// 而不是用「暗物覆盖比例」阈值 —— 后者会被果蝇的位移污染。
			const bool dark_phase = predator ? (g_loom_phase < 0.25) : false;      // 0 = 很近
			const bool neutral = predator && (g_loom_phase > 0.25 && g_loom_phase < 0.75);
			// 对照：把相位标签随机化（与刺激无关）。解码流程若无偏，此时 CV d' 应≈0。
			const bool rnd = label_random && (((unsigned)(frame * 2654435761u) >> 13) & 1u);
			const bool dp2 = rnd ? !dark_phase : dark_phase;
			rnd2 = rnd;
			std::vector<double>& osum = (frame < half_frames) ? on_sum : on_sum2;
			std::vector<double>& fsum = (frame < half_frames) ? off_sum : off_sum2;
			std::vector<double>& osq = (frame < half_frames) ? on_sq : on_sq2;
			std::vector<double>& fsq = (frame < half_frames) ? off_sq : off_sq2;
			std::vector<double>& oxy = (frame < half_frames) ? on_xy : on_xy2;
			std::vector<double>& fxy = (frame < half_frames) ? off_xy : off_xy2;
			long long& a1 = (frame < half_frames) ? on_n : on_n2;
			long long& a2 = (frame < half_frames) ? off_n : off_n2;
			if (!neutral)
			{
				if (dp2)
				{
					for (xhqm::size i = 0; i < n; ++i)
					{ const double v = P.s[i]; osum[i] += v; osq[i] += v * v; oxy[i] += v * prev_out[i]; }
					++a1;
				}
				else
				{
					for (xhqm::size i = 0; i < n; ++i)
					{ const double v = P.s[i]; fsum[i] += v; fsq[i] += v * v; fxy[i] += v * prev_out[i]; }
					++a2;
				}
			}
			// prev_out 每步都更新（不只在计样本的步）：滞后一阶乘积要的是真实相邻步
			for (xhqm::size i = 0; i < n; ++i) prev_out[i] = P.s[i];
		}
		t_sim += dt;
		++frame;

		// --snapshot-step N：在第 N 个神经步快照。
		// 动机：原来的 --snapshot 走 QTimer(1500ms) 墙钟，帧率一变（改渲染性能）快照时刻的
		// 仿真状态就变了，改前/改后的图没法逐像素比。按步号取快照才能做真正的 A/B 对照。
		if (snap_step > 0 && frame == snap_step && !snap.empty())
		{
			viewport.snapshot_to(snap);
			std::printf("★ --snapshot-step %d：已写 %s\n", snap_step, snap.c_str());
		}

		// 5c. motor 层 → actuator（肌肉级映射）
		acc_net += clk() - tprev; tprev = clk();   // 5b 结束
		if (g_m->nu > 0)
		{
			static std::vector<float> mus_rate, mus_base;
			mus_rate.resize(g_cd_motor.size());
			mus_base.resize(g_cd_motor.size(), 0.0f);
			for (std::size_t gi = 0; gi < g_cd_motor.size(); ++gi)
			{
				float acc = 0.0f;
				for (xhqm::size i : g_cd_motor[gi]) if (i < n) acc += P.s[i];
				mus_rate[gi] = acc / (float)std::max<std::size_t>(g_cd_motor[gi].size(), 1);
			}
			const float ema = 0.02f, alpha = (float)(dt / 0.100);
			for (std::size_t gi = 0; gi < mus_base.size(); ++gi)
				mus_base[gi] += (mus_rate[gi] - mus_base[gi]) * ema;
			for (std::size_t a = 0; a < (std::size_t)g_m->nu; ++a)
			{
				const auto& src = g_act_from_motor[a];
				float num = 0.0f, den = 0.0f, gait = 0.0f;
				// 双侧对称零位：用同一块肌肉左右两组的均值当基线，保留左右差（转向信号）
				for (const auto& pr : src)
				{
					num += pr.second * (mus_rate[pr.first] - mus_base[pr.first]);
					den += pr.second;
					const int side = (pr.first < (int)g_motor_side.size()) ? g_motor_side[pr.first] : 0;
					if (side != 0)
					{
						const char* nm = mj_id2name(g_m, mjOBJ_ACTUATOR, (int)a);
						int seg = 2;
						if (nm)
						{
							if (std::strstr(nm, "T1_")) seg = 0;
							else if (std::strstr(nm, "T2_")) seg = 1;
							else if (std::strstr(nm, "T3_")) seg = 0;
						}
						const double phase = (side > 0 ? 3.14159265358979323846 : 0.0)
							+ (seg == 1 ? 3.14159265358979323846 : 0.0);
						const double th = 2.0 * 3.14159265358979323846 * g_gait_hz * t_sim + phase + g_gait_phase;
						gait += (float)std::sin(th) * pr.second;
					}
				}
				float c = g_motor_gain * ((den > 0.0f) ? (num / den) : 0.0f);
				if (den > 0.0f) c += g_gait_amp * (gait / den) * 0.01f;
				c = std::max(-1.0f, std::min(1.0f, c));
				g_d->ctrl[a] = c;
			}
			// ★ 正对照（--asym-motor A）：只在「很近」相位给左右 actuator 注入不对称控制量。
			// 作用点在 actuator 侧（大脑的下游），所以这不是"干涉果蝇大脑内部"，而是**校准测量链**：
			//   判据若能测出这个已知的、与刺激锁相的转向信号，才说明"测不出反应"是有意义的结论；
			//   否则"没反应"与"读出器坏了"无法区分（本项目栽过 9 次量具问题，这条不能省）。
			// 幅度从小到大扫，还能给出这套读出的【灵敏度下限】——知道"多大的转向才够被看见"。
			if (asym_motor > 0.0 && predator && g_loom_phase < 0.25)
			{
				const float d = (float)(0.5 * asym_motor);
				for (int a : act_right) g_d->ctrl[a] = std::max(-1.0f, std::min(1.0f, (float)g_d->ctrl[a] + d));
				for (int a : act_left)  g_d->ctrl[a] = std::max(-1.0f, std::min(1.0f, (float)g_d->ctrl[a] - d));
				asym_inj_on += asym_motor; ++asym_inj_n;
			}
			// ★ 解码器闭环（--decoder-gain K）：读视叶状态 → 左右不对称。**只在后半段施加**，
			// 权重是前半段估的 ⇒ 估与评分开（反乐观偏置）。dec 减掉估计段共模 dc_mA，只留
			// 与刺激相关的分量。限幅 0.25（与正对照同量级，便于比较灵敏度）。
			if (decoder_gain > 0.0 && dc_ready)
			{
				double dec = 0.0;
				for (std::size_t k = 0; k < dc; ++k) dec += dc_w[k] * P.s[dec_cells[k]];
				const double d = std::max(-0.25, std::min(0.25, decoder_gain * (dec - dc_mA)));
				for (int a : act_right) g_d->ctrl[a] = std::max(-1.0f, std::min(1.0f, (float)g_d->ctrl[a] + (float)d));
				for (int a : act_left)  g_d->ctrl[a] = std::max(-1.0f, std::min(1.0f, (float)g_d->ctrl[a] - (float)d));
				dec_apply_sum += d; ++dec_apply_n;
				// 命令自身的信噪比自检：施加量 d 在两相之间的均值差 / 其标准误。
				// 若这一步就很小，说明解码器读出来的主要是噪声 —— 那行为测不出来是"解码器不行"，
				// 不能归因于"身体不响应"。这里用【真相位】（不套 --label-random 的分析标签）。
				{
					const bool neu = (g_loom_phase > 0.25 && g_loom_phase < 0.75);
					const bool c_on = predator && !neu && (g_loom_phase < 0.25);
					const bool c_off = predator && !neu && !(g_loom_phase < 0.25);
					if (c_on)      { dd_on += d; dd_onq += d * d; ++dd_n_on; }
					else if (c_off) { dd_off += d; dd_offq += d * d; ++dd_n_off; }
				}
			}
			(void)alpha;
		}

		// 5d. 物理
		// 分段计时：先量准再优化。实测症状是「窗口一卡一卡、身体姿态不连贯」，
		// 而渲染只跑到 4.9 fps（视口要求 28 fps）—— 因为仿真与渲染共用一条 Qt 线程，
		// 每个仿真步要 200ms，事件循环被占满。这里把这段时间拆开看是谁吃的。
		acc_act += clk() - tprev; tprev = clk();   // 5c 结束
		for (int s = 0; s < phys_per_neural; ++s) mj_step(g_m, g_d);
		acc_phys += clk() - tprev;
		++nsteps;

		// 5e. 逼近物体：周期性从远处逼近再复位（刺激由世界产生，果蝇用眼睛采样）
		if (predator && g_predator_geom >= 0)
		{
			t_pred += dt;
			// loom 周期可调（--loom-period T，默认 4.0）。
			// 动机：子智能体在 mcns_net 上查出**刺激段长是独立于细胞模型的一个开关**：
			// rev-block 0.8s → ol_intrinsic 62 显著；2.0s → 7206，且深层四层全部点亮。
			// 而本程序的两相窗口是 0.375×T = 1.5s（T=4），正好夹在"失败"与"成功"之间。
			// 约束：0.375×T 必须是步态周期 0.25s 的整数倍 ⇒ T = 2m/3（m=6→4.0s=1.5s 窗口；
			// m=8→5.333s=2.0s；m=12→8.0s=3.0s）。
			const double T = g_loom_period, ph = std::fmod(t_pred, T) / T;
			// 距离从 0.30m 一路缩到 0.03m：半径 0.06m 的球在 0.03m 处张角约 126°，
			// 覆盖视野的绝大部分，才是真正强烈的逼近刺激（原来只到 0.09m，覆盖不到 3%）。
			const double D = 0.30 * (1.0 - 0.9 * ph);          // 0.30m → 0.03m
			// 半径随逼近一起长大，逼近末段【填满整个视野】。
			// 这一步是必须的：实测网络主要响应「共模亮度变化」而不是空间图样
			// （移动光栅驱动的视叶神经元远少于全局闪光）。若捕食者只挡住 36% 视野，
			// 其余天空/地面在两相之间不变，就只是个差模刺激，运动层测不到任何反应
			// —— 实测确实如此（暗物覆盖差 33.7 个百分点，左右驱动差仅 +0.0002）。
			if (g_predator_geom >= 0)
			{
				const double rr = 0.05 + 0.65 * ph;
				for (int k = 0; k < 3; ++k) g_m->geom_size[3 * g_predator_geom + k] = rr;
				// 遮挡造成的整体照度衰减：用 (r/D)² 近似「挡住了多大一块天空」，上限 0.85。
				const double blk = std::min(1.0, (rr / std::max(D, 1e-6)) * (rr / std::max(D, 1e-6)));
				g_occlusion = (float)(1.0 - 0.85 * blk);
			}
			const double* hp = g_d->xpos + 3 * g_head_body;
			const double* R = g_d->xmat + 9 * g_head_body;
			const double fx = R[3], fy = R[4], fz = R[5];       // 头部前向
			// 相位标签：只把「很近」与「很远」两端当两个相位，中间过渡段丢弃。
			// 关键：两个窗口的长度必须都是步态周期的整数倍（步频 4Hz → 0.25s），
			// 否则窗口内步态正弦的均值不为零、两相位的步态相位分布系统性不同，
			// 会造出一个与视觉无关的假「方向性反应」（实测：关掉视觉注入后此差值一模一样）。
			// 这里取 ph∈[0, 0.25) 与 [0.75, 1.0)，各 1.0s = 恰好 4 个步态周期。
			// 相位窗口三条约束（每一条都是踩过坑才加上的）：
			//   ① 必须【包含逼近的过渡过程】—— 取两个准静态端点做对比时下游完全测不到；
			//      网络响应的是 looming 的「变化」，不是「那里有个暗东西」。
			//   ② 两个窗口必须【等长】—— 80/20 这种不平衡会让网络自身的慢漂移在两个字相位上
			//      产生系统性差别，交叉验证 d' 虚高（实测把注入幅度改成 0 后数字一模一样）。
			//   ③ 窗口长度必须是【步态周期的整数倍】—— 否则步态相位在两相间分布不同，
			//      混叠出与视觉无关的假方向性（实测关掉视觉后那个差值一模一样）。
			// 步态 0.25s、loom 周期 4s = 16 个步态周期 ⇒ 取两侧各 1.5s = 6 个步态周期。
			g_loom_phase = label_mode_net ? ((ph < 0.8) ? 0.0 : 1.0)
			                              : ((ph >= 0.625) ? 0.0
			                                 : ((ph < 0.375) ? 1.0 : 0.5));
			g_m->geom_pos[3 * g_predator_geom + 0] = hp[0] + fx * D;
			g_m->geom_pos[3 * g_predator_geom + 1] = hp[1] + fy * D;
			g_m->geom_pos[3 * g_predator_geom + 2] = hp[2] + fz * D;
			mj_forward(g_m, g_d);
		}
		else g_loom_phase = 1.0;                                // 无刺激时全部算「间歇相」

		// 5e2. 系留：把自由关节的位置与姿态钉回初始值（腿仍能动）
		if (tether)
		{
			for (int k = 0; k < 7; ++k) g_d->qpos[k] = qpos_rest[k];
			for (int k = 0; k < 6; ++k) g_d->qvel[k] = 0.0;
		}

		// 5f. 渲染同步
		if (frame % 2 == 0)
		{
			for (int b = 0; b < g_m->nbody; ++b)
				if (g_body_models[b]) g_body_models[b]->system::coord(body_world_matrix(b));
			const double* tp = g_d->qpos;
			viewport.camera_ptr()->focus(poit<value>((value)((tp[0] - g_center[0]) * g_scale),
				(value)((tp[1] - g_center[1]) * g_scale), (value)((tp[2] - g_center[2]) * g_scale)));
		}

		// 5g. 行为统计（无条件执行，否则不带 --log-csv 的对照运行什么都不累计）
		long long ms = 0, ds = 0;
		float mrate = 0.0f; std::size_t c = 0;
		for (xhqm::size i : g_groups["motor_all"]) if (P.s[i] > 0.5f) ++ms;
		for (xhqm::size i : g_groups["descending_all"]) if (P.s[i] > 0.5f) ++ds;
		{ for (auto& g : g_cd_motor) for (xhqm::size i : g) { mrate += P.s[i]; ++c; }
		  if (c) mrate /= (float)c; }
		{
			long long sb = 0; for (xhqm::size i = 0; i < n; ++i) sb += (P.s[i] > 0.5f) ? 1 : 0;
			const double* R2 = g_d->xmat + 9 * g_body_up_body;
			const double yaw = std::atan2((double)R2[3], (double)R2[4]);
			// 左右驱动差（身体真正收到的转向信号）
			double cl = 0.0, cr = 0.0;
			for (int a : act_left) cl += g_d->ctrl[a];
			for (int a : act_right) cr += g_d->ctrl[a];
			if (!act_left.empty()) cl /= (double)act_left.size();
			if (!act_right.empty()) cr /= (double)act_right.size();
			const double asym = cr - cl;
			const bool neutral2 = predator && (g_loom_phase > 0.25 && g_loom_phase < 0.75);
			const bool dph = rnd2 ? !(g_loom_phase < 0.25) : (g_loom_phase < 0.25);
			const bool is_on = predator && !neutral2 && dph;
			const bool is_off = predator && !neutral2 && !dph;
			// 解码器：只在前半段累计（估权重用），后半段才施加
			if (!dc_ready && frame < half_frames)
			{
				if (is_on)
				{
					for (std::size_t k = 0; k < dc; ++k)
					{ const double v = P.s[dec_cells[k]]; dc_on[k] += v; dc_onq[k] += v * v; }
					++dc_n_on;
				}
				else if (is_off)
				{
					for (std::size_t k = 0; k < dc; ++k)
					{ const double v = P.s[dec_cells[k]]; dc_off[k] += v; dc_offq[k] += v * v; }
					++dc_n_off;
				}
			}
			// 前半段结束 ⇒ 估出权重（Fisher 判别方向），此后只读、不再改权重
			if (!dc_ready && frame >= half_frames && dc_n_on > 2 && dc_n_off > 2)
			{
				// ★ 方差必须正则化：w = (m1−m2)/(v1+v2) 会被"几乎不动的细胞"主导
				//（v≈0 ⇒ 权重爆掉），实测 |w| 归一化前 52.95、读出来主要是噪声：
				// 注入量够大（平均 −0.091）但行为 z 只有 −0.46。加岭 λ = 方差中位数。
				std::vector<double> vv(dc, 0.0);
				double n2 = 0.0, lam = 0.0;
				for (std::size_t k = 0; k < dc; ++k)
				{
					const double m1 = dc_on[k] / (double)dc_n_on, m2 = dc_off[k] / (double)dc_n_off;
					const double v1 = std::max(dc_onq[k] / (double)dc_n_on - m1 * m1, 0.0);
					const double v2 = std::max(dc_offq[k] / (double)dc_n_off - m2 * m2, 0.0);
					vv[k] = v1 + v2;
				}
				{ std::vector<double> s = vv; std::nth_element(s.begin(), s.begin() + s.size() / 2, s.end());
				  lam = std::max(s[s.size() / 2], 1e-12); }
				for (std::size_t k = 0; k < dc; ++k)
				{
					const double m1 = dc_on[k] / (double)dc_n_on, m2 = dc_off[k] / (double)dc_n_off;
					dc_w[k] = (m1 - m2) / (vv[k] + lam);
					dc_mA += dc_w[k] * 0.5 * (m1 + m2);
					n2 += dc_w[k] * dc_w[k];
				}
				// ★ 只保留判别力最强的 N 个细胞（其余置零）。
				// 动机：实测 2800 个抽样细胞里绝大多数是噪声，|w| 归一化前 29、命令自身 z 只有 −2.52。
				// 判别力用 |m1−m2|/sqrt(v1+v2+λ) —— 这正是"该细胞到底是信号还是噪声"的度量。
				{
					const std::size_t KEEP = 256;
					if (dc > KEEP)
					{
						std::vector<std::pair<double, std::size_t>> sc(dc);
						for (std::size_t k = 0; k < dc; ++k)
						{
							const double m1 = dc_on[k] / (double)dc_n_on, m2 = dc_off[k] / (double)dc_n_off;
							sc[k] = { std::fabs(m1 - m2) / std::sqrt(vv[k] + lam), k };
						}
						std::nth_element(sc.begin(), sc.begin() + KEEP, sc.end(),
							[](const std::pair<double, std::size_t>& a, const std::pair<double, std::size_t>& b)
							{ return a.first > b.first; });
						std::vector<char> keep(dc, 0);
						for (std::size_t j = 0; j < KEEP; ++j) keep[sc[j].second] = 1;
						double n2b = 0.0; dc_mA = 0.0;
						for (std::size_t k = 0; k < dc; ++k)
						{
							if (!keep[k]) { dc_w[k] = 0.0; continue; }
							const double m1 = dc_on[k] / (double)dc_n_on, m2 = dc_off[k] / (double)dc_n_off;
							dc_mA += dc_w[k] * 0.5 * (m1 + m2);
							n2b += dc_w[k] * dc_w[k];
						}
						n2 = n2b;
						dc_keep = KEEP;
					}
				}
				dc_norm = std::sqrt(n2);
				if (dc_norm > 1e-12)
				{
					for (auto& x : dc_w) x /= dc_norm;
					dc_mA /= dc_norm;
				}
				if (decoder_permute)   // 对照：权重随机置换（同样量级、但与细胞错配）
				{
					std::mt19937 rp(20260919u);
					std::shuffle(dc_w.begin(), dc_w.end(), rp);
					std::printf("★ 解码器对照：权重已随机置换\n");
				}
				dc_ready = true;
				std::printf("★ 解码器就绪：保留 %zu/%zu 个细胞，|w|归一化前=%.4g（岭 λ=%.4g）共模 dc_mA=%.4g\n",
					dc_keep, dc, dc_norm, lam, dc_mA);
			}
			// ★ 统计单位是【步态周期块】，不是神经步。
			// 这是正对照实测逼出来的：左右驱动差被 4Hz 步态正弦主导，逐步样本强自相关，
			// AR(1) 折算后 n_eff 只剩 ~30 —— 注入 Δ=0.092 的真实转向只得到 z=+1.02，
			// 检验几乎没有功效（"窗口取步态周期整数倍"只保证均值无偏，不解决方差）。
			// 正确做法：先算块（2 个步态周期）内的均值，块间近似独立，再在【块均值】上做两相检验。
			// 只收「整块都属于同一相位」的块（跨相位边界的块丢弃），避免相位归属含混。
			blk_sum += asym; ++blk_n;
			if (is_on) ++blk_on; else if (is_off) ++blk_off;
			if (blk_n >= gait_block)
			{
				if (blk_on == blk_n || blk_off == blk_n)
				{
					const bool first = (frame < half_frames);
					const double bm = blk_sum / (double)blk_n;
					if (blk_on == blk_n)
					{
						double& s = first ? as_on : as2_on; double& q = first ? as_on_sq : as2_on_sq;
						double& x = first ? as_on_xy : as2_on_xy;
						s += bm; q += bm * bm; x += bm * as_prev_blk; as_prev_blk = bm;
						(first ? asn_on : asn2_on)++;
					}
					else
					{
						double& s = first ? as_off : as2_off; double& q = first ? as_off_sq : as2_off_sq;
						double& x = first ? as_off_xy : as2_off_xy;
						s += bm; q += bm * bm; x += bm * as_prev_blk; as_prev_blk = bm;
						(first ? asn_off : asn2_off)++;
					}
				}
				blk_sum = 0.0; blk_n = 0; blk_on = 0; blk_off = 0;
			}
			// 刺激到位诊断：两相各自的暗物覆盖比例与平均注入电流
			if (is_on)
			{
				dk_on += g_dark_frac; ++dk_n_on;                 // 注意：dk_on 在两半段都累加，
				                                                 // 除数必须用同一个计数器，
				                                                 // 不能用只统计前半段的 asn_on
				                                                 // （否则会打出 183% 这种不可能的值）
				if (!pr_cur.empty() && n > 0)
				{
					double s = 0.0;
					for (xhqm::size i = 0; i < n; i += 16) s += pr_cur[i];
					pr_i_on += s / (double)(n / 16 + 1);
				}
			}
			else if (is_off)
			{
				dk_off += g_dark_frac; ++dk_n_off;
				if (!pr_cur.empty() && n > 0)
				{
					double s = 0.0;
					for (xhqm::size i = 0; i < n; i += 16) s += pr_cur[i];
					pr_i_off += s / (double)(n / 16 + 1);
				}
			}
			if (csv.is_open())
				csv << t_sim << ',' << g_loom_phase << ',' << g_dark_frac << ',' << sb << ','
					<< ms << ',' << ds << ',' << mrate << ','
					<< g_d->qpos[0] << ',' << g_d->qpos[1] << ',' << g_d->qpos[2] << ',' << yaw
					<< ',' << cl << ',' << cr << ',' << asym << std::endl;
		}
		if (t_sim - last_log >= 2.0)
		{
			last_log = t_sim;
			long long sb = 0; for (xhqm::size i = 0; i < n; ++i) sb += (P.s[i] > 0.5f) ? 1 : 0;
			float mrate = 0.0f;
			{ std::size_t c = 0; for (auto& g : g_cd_motor) for (xhqm::size i : g) { mrate += P.s[i]; ++c; }
			  if (c) mrate /= (float)c; }
			std::printf("[t=%6.2fs] 逼近相位 %.2f 暗物覆盖 %5.1f%% | 本步发放 %lld/%zu | 肌肉均发放 %.4f | pos=(%+.3f %+.3f %+.3f) | %.1fx\n",
				t_sim, g_loom_phase, 100.0 * g_dark_frac, sb, n, mrate, g_d->qpos[0], g_d->qpos[1], g_d->qpos[2],
				t_sim / std::max(wall.elapsed() / 1000.0, 1e-9));
			// 分段耗时（每步平均毫秒）：定位「一卡一卡」是谁吃的
			if (nsteps > 0)
			{
				const double inv = 1000.0 / (double)nsteps;
				const double other = (wall.elapsed() / 1000.0) - (acc_ray + acc_net + acc_act + acc_phys);
				std::printf("    每步耗时(ms): 视觉raycast %.1f | 网络 %.1f | 肌肉映射 %.1f | MuJoCo %.1f | 其余(含渲染/事件) %.1f\n",
					acc_ray * inv, acc_net * inv, acc_act * inv, acc_phys * inv, std::max(other, 0.0) * inv);
			}
		}
		if (sec > 0.0 && t_sim >= sec)
		{
			anim.stop();
			// 汇总：逐神经元 + 交叉验证（与 mcns_net 同一套方法）
			std::printf("\n══ 汇总（仿真 %.1fs，%.0f 帧）══\n", t_sim, (double)frame);
			std::printf("逼近相样本 %lld / 间歇相 %lld\n", on_n, off_n);
			if (on_n > 20 && off_n > 20)
			{
				std::printf("%-22s %8s %10s %10s %12s %10s %10s\n", "superclass", "有发放", "显著(>3σ)", "群体 d'", "CV d'", "均值活动", "范围[min,max]");
				// 每类的【均值活动与范围】：这是"每类各自稳定在一定范围"这件事的唯一度量。
				// 没有它就无法判断某一类是不是被摆在了响应区之外（深层几类长期为 0 就是症状）。
				std::unordered_map<std::string, std::array<double, 4>> agg;   // 个数 / |Δ|和 / 显著个数 / 活动均值*个数
				std::unordered_map<std::string, std::array<double, 2>> agg_rng;   // min / max
				std::vector<xhqm::size> idx;
				long long tot_cells = 0, tot_sig = 0;   // 全网合计：用来对照假阳性期望
				for (xhqm::size i = 0; i < n; ++i)
				{
					const double a = on_sum[i] / (double)on_n, b = off_sum[i] / (double)off_n;
					if (a == 0.0 && b == 0.0) continue;
					// 标准误：经验方差 + AR(1)（原来这里是伯努利 p(1−p)，见上面声明处的说明）
					const double se = std::sqrt(std::max(
						var_of_mean(on_sum, on_sq, on_xy, (double)on_n, i)
						+ var_of_mean(off_sum, off_sq, off_xy, (double)off_n, i), 1e-12));
					auto& e = agg[g_superclass[i]];
					e[0] += 1; e[1] += std::fabs(a - b);
					const double mv = 0.5 * (a + b);
					e[3] += mv;
					auto& rg = agg_rng[g_superclass[i]];
					if (e[0] == 1.0) { rg[0] = mv; rg[1] = mv; }
					else { rg[0] = std::min(rg[0], mv); rg[1] = std::max(rg[1], mv); }
					++tot_cells;
					if (std::fabs(a - b) > 3 * se) { e[2] += 1; ++tot_sig; }
					g_dp[g_superclass[i]] += ((a - b) / se) * ((a - b) / se);
				}
				// 交叉验证：前半估权重、后半评估（再反向一次），避免乐观偏置
				// 两半各自的方差也用同一套修正量（经验方差 + AR(1)），否则权重与方差口径不一致。
				auto cvd = [&](bool A_first, bool B_first, const std::string& sc) -> double
				{
					double delta = 0.0, var = 0.0;
					const double nA = A_first ? (double)on_n : (double)on_n2;
					const double nB = B_first ? (double)on_n : (double)on_n2;
					for (xhqm::size i = 0; i < n; ++i)
					{
						if (!sc.empty() && g_superclass[i] != sc) continue;
						const double mA = (A_first ? on_sum[i] : on_sum2[i]) / nA;
						const double fA = (A_first ? off_sum[i] : off_sum2[i]) / nA;
						const double sA2 = var_of_one(true, A_first, i) + var_of_one(false, A_first, i);
						if (sA2 <= 1e-12) continue;
						const double w = (mA - fA) / sA2;
						const double mB = (B_first ? on_sum[i] : on_sum2[i]) / nB;
						const double fB = (B_first ? off_sum[i] : off_sum2[i]) / nB;
						const double sB2 = var_of_one(true, B_first, i) + var_of_one(false, B_first, i);
						delta += w * (mB - fB); var += w * w * sB2;
					}
					return (var > 1e-12) ? delta / std::sqrt(var) : 0.0;
				};
				for (auto& kv : agg)
				{
					if (kv.second[2] <= 0 && kv.second[0] < 200) continue;
					double cv1 = 0.0, cv2 = 0.0;
					if (on_n2 > 20 && off_n2 > 20)
					{
						cv1 = cvd(true, false, kv.first);
						cv2 = cvd(false, true, kv.first);
					}
					std::printf("%-22s %8.0f %10.0f %10.1f %12s %10.4f %6.3f~%.3f\n", kv.first.c_str(),
						kv.second[0], kv.second[2], std::sqrt(g_dp[kv.first]),
						(on_n2 > 20 ? (std::to_string((int)cv1) + "/" + std::to_string((int)cv2)).c_str() : "—"),
						kv.second[3] / std::max(kv.second[0], 1.0),
						agg_rng[kv.first][0], agg_rng[kv.first][1]);
				}
				// 假阳性期望：>3σ 在纯噪声下按正态尾部 0.27% 计。没有这一行，任何"显著数"都不可解释
				// （这正是本项目栽过的坑：群体 d′ 用 √(Σz²) 时噪声底随细胞数增长，89,275 个细胞
				//  无论刺激是什么都会给出 ≈300）。
				std::printf("全网 %lld 个有发放细胞；|Δ|>3σ 的 %lld 个；假阳性期望 %lld×0.27%% ≈ %.0f\n",
					tot_cells, tot_sig, tot_cells, (double)tot_cells * 0.0027);
			}
			std::printf("末位置 (%.4f %.4f %.4f)\n", g_d->qpos[0], g_d->qpos[1], g_d->qpos[2]);
			// 行为方向性：左右驱动差的两相统计
			// ★ 不只报均值差：这里给出标准误（经验方差 + AR(1) 折算 n_eff）与 z 值。
			//   没有这一步就无法区分"真差"与"噪声里的差"（本项目栽过：只报均值差时
			//   关掉视觉注入，差值一模一样仍被读成"有方向性"）。
			auto var_of_series = [](double sum, double sq, double xy, long long cnt) -> double
			{
				if (cnt <= 2) return 0.0;
				const double m = sum / (double)cnt;
				const double v = std::max(sq / (double)cnt - m * m, 0.0);
				if (v <= 1e-18) return 0.0;
				const double rho = std::min(std::max((xy / (double)cnt - m * m) / v, 0.0), 0.98);
				return v * ((1.0 + rho) / (1.0 - rho)) / (double)cnt;   // 均值的方差
			};
			auto rep = [&](const char* nm, double a, long long na, double aq, double axy,
			               double b, long long nb, double bq, double bxy)
			{
				if (na < 5 || nb < 5) { std::printf("%s 样本不足 (%lld/%lld)\n", nm, na, nb); return; }
				const double ma = a / (double)na, mb = b / (double)nb;
				const double se = std::sqrt(var_of_series(a, aq, axy, na) + var_of_series(b, bq, bxy, nb));
				const double z = (se > 1e-12) ? (ma - mb) / se : 0.0;
				std::printf("%-12s 很近 %+.6f (n=%lld)  很远 %+.6f (n=%lld)  差 %+.6f  z=%+.2f%s\n",
					nm, ma, na, mb, nb, ma - mb, z,
					(std::fabs(z) > 3.0) ? "  ★ |z|>3" : "");
			};
			std::printf("\n── 左右驱动差（正 = 右腿更强 = 右转）──\n");
			rep("前半", as_on, asn_on, as_on_sq, as_on_xy, as_off, asn_off, as_off_sq, as_off_xy);
			rep("后半", as2_on, asn2_on, as2_on_sq, as2_on_xy, as2_off, asn2_off, as2_off_sq, as2_off_xy);
			rep("全部", as_on + as2_on, asn_on + asn2_on, as_on_sq + as2_on_sq, as_on_xy + as2_on_xy,
				as_off + as2_off, asn_off + asn2_off, as_off_sq + as2_off_sq, as_off_xy + as2_off_xy);
			// 正对照自检：确认注入真的发生了（否则"测出来"可能只是别的原因）
			if (asym_motor > 0.0)
				std::printf("★ 正对照 --asym-motor %.4f：实际注入 %lld 步（仅「很近」相位），"
					"若上面的 z 没到 3，说明这套读出的灵敏度下限高于这个幅度\n",
					asym_motor, asym_inj_n);
			else if (predator)
				std::printf("（未开正对照：本次的 z 只能说明「这个幅度下测不出」，不能说明「没有反应」）\n");
			if (decoder_gain > 0.0)
			{
				std::printf("★ 解码器闭环 gain=%.3f%s：后半段施加 %lld 步，平均施加量 %+.6f\n",
					decoder_gain, decoder_permute ? "（权重已随机置换·对照）" : "",
					dec_apply_n, (dec_apply_n > 0) ? dec_apply_sum / (double)dec_apply_n : 0.0);
				// 命令自身的两相信噪比：这一条不显著 ⇒ 是解码器在噪声里读，不能怪身体
				if (dd_n_on > 2 && dd_n_off > 2)
				{
					const double m1 = dd_on / (double)dd_n_on, m2 = dd_off / (double)dd_n_off;
					const double v1 = std::max(dd_onq / (double)dd_n_on - m1 * m1, 0.0) / dd_n_on;
					const double v2 = std::max(dd_offq / (double)dd_n_off - m2 * m2, 0.0) / dd_n_off;
					std::printf("   命令自身：很近 %+.5f / 很远 %+.5f，差 %+.5f，z=%+.2f%s\n",
						m1, m2, m1 - m2, (m1 - m2) / std::sqrt(std::max(v1 + v2, 1e-18)),
						(std::fabs((m1 - m2) / std::sqrt(std::max(v1 + v2, 1e-18))) > 3.0)
							? "  ★ 命令本身有相位信息" : "  ← 命令本身就是噪声，行为测不出不能怪身体");
				}
			}
			// 关键诊断：两个相位的视觉输入到底有没有差别。
			// 没有这一步就无法区分「网络不响应」与「刺激压根没送到」。
			std::printf("── 视觉输入（视野被暗物覆盖比例）──\n");
			if (asn_on > 0 && asn_off > 0 && dk_n_on > 0 && dk_n_off > 0)
				std::printf("很近相均 %.2f%% (n=%lld)  很远相均 %.2f%% (n=%lld)  差 %+.2f 个百分点\n",
					100.0 * dk_on / (double)dk_n_on, dk_n_on,
					100.0 * dk_off / (double)dk_n_off, dk_n_off,
					100.0 * (dk_on / (double)dk_n_on - dk_off / (double)dk_n_off));
			std::printf("光感受器注入电流均 很近 %.4f / 很远 %.4f（vis_gain=%.0f，全部神经元上的抽样均值）\n",
				pr_i_on / std::max((double)dk_n_on, 1.0), pr_i_off / std::max((double)dk_n_off, 1.0), vis_gain);
			// ★ 渲染自检：起视口帧循环之后这个数必须明显大于 0，且 ≈ 运行秒数 × 28（定时器 35ms）。
			//   之前漏调 viewport.start() 时它是 0 —— 窗口全白、点击才刷新，而控制台输出一切正常。
			const unsigned long long vf = viewport.frame_count();
			const double el = wall.elapsed() / 1000.0;
			std::printf("视口渲染: 已渲染 %llu 帧 / 墙钟 %.1fs = %.1f fps %s\n", vf, el,
				(el > 0.01) ? (double)vf / el : 0.0,
				(vf == 0) ? "  ★★ 为 0 ⇒ 窗口没有在渲染（检查 viewport.start()）" : "");
			if (vf > 0)
				std::printf("    帧内拆分: 帧图执行(shadow/scene/tonemap) %.1f ms（其中本线程 CPU %.1f ms；首帧 %.1f ms，稳态单帧最大 %.1f ms，>20ms 的帧 %llu）| 起止帧+呈现 %.1f ms\n",
					viewport.frame_ms_fg() / (double)vf, viewport.frame_ms_cpu_fg() / (double)vf,
					viewport.frame_ms_fg_first(), viewport.frame_ms_fg_max(), viewport.frames_slow(),
					viewport.frame_ms_other() / (double)vf);
				if (perf_pass)
				{
					for (const auto& pn : viewport.pass_names())
						std::printf("      逐 pass: %-12s 墙钟 %7.2f ms | CPU %7.2f ms\n", pn.c_str(),
							viewport.pass_ms(pn.c_str()) / (double)vf, viewport.pass_cpu_ms(pn.c_str()) / (double)vf);
				}
			app.quit();
		}
	});
	anim.start();
	if (!snap.empty())
	{
		// 快照路径兼作【离屏渲染基准】：连续渲 5 帧取平均并打印。
	// 用途：区分「渲染本身慢」与「呈现（FIFO 垂直同步 / 虚拟显示器）被节流」——
	// 实测活动窗口每帧约 104ms 而 3090 Ti 上正常应 ~5ms；若离屏只要几 ms，
	// 那 104ms 就不是渲染工作，而是 present 在等显示器。
	QTimer::singleShot(1500, [&viewport, snap]()
	{
		const int N = 5;
		auto t0 = std::chrono::high_resolution_clock::now();
		for (int k = 0; k < N; ++k) viewport.snapshot_to(snap);
		auto t1 = std::chrono::high_resolution_clock::now();
		const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / N;
		std::printf("离屏渲染基准: %d 帧平均 %.1f ms/帧（不含呈现）\n", N, ms);
	});
	}
	return app.exec();
}
