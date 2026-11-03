// cell_backend —— 可切换的细胞模型后端
//
// 把 test/cellsim 的电导型细胞模型接进网络步进，与既有 LIF 做 A/B。
//
// 设计原则是【一次只改一件事】，所以三级台阶：
//   CM_LIF        原封不动的既有代码路径（回归基准：换后端前后必须复现同一批数字）
//   CM_BIO        同样发放、同样注入，只把「突触电流脉冲」换成「按突触前符号打开电导通道」
//   CM_BIO + 分级  再把光感受器/LMC 改成分级输出（cell_param.graded = 1）
// 只有 lif 与 bio 的数字对得上，后面加分级才有意义。
//
// ── 与 LIF 的等效性怎么对齐（两个都要对齐，否则 A/B 的差异归因不了）──
// 1) 静息：LIF 的漏电拉向 0，靠 I_bg 把稳态顶到 v_ss = I_bg·tau = 0.86。
//    bio 的漏电直接拉向 v_rest = 0.86。两边零输入稳态严格相同。
//    ⇒ 所以 bio 模式下外部电流 i_ext 只放「视觉驱动 + 噪声」，【不能】再放 I_bg，否则重复计一次。
// 2) 单条突触效力：LIF 里一个脉冲把 v 抬高 w·dt（只作用一步）。
//    bio 的强度单位是「裕度」，1.0 裕度 = 峰值去极化正好等于 v_th−v_rest。
//    ⇒ dpk = gain · w · dt / 裕度，gain=1 时单条突触的峰值效力与 LIF 完全相同。
//    差别只剩「时间过程」：LIF 的脉冲是一次性电荷、之后按 tau_m 衰减；
//    bio 的电导按 tau_syn（5ms）衰减 ⇒ 同样数量的输入顶不到阈值，
//    所以另给一个全局 gain 把基线发放率校回 LIF 水平（校完再比，差异才可归因）。
//
// ── 通道怎么定（不需要额外数据）──
// CSR 里的 val 已经带了突触符号（GABA/谷氨酸/组胺 → −1），所以正负号直接映射到
// 兴奋通道 / 抑制通道，不用再查一遍突触前类型。
#pragma once
#ifndef XHQM_TEST_FFBO_CELL_BACKEND_H
#define XHQM_TEST_FFBO_CELL_BACKEND_H

#include "cellsim.h"
#include "dend_branch.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace cellbackend
{
	using cellsim::cell;
	using cellsim::cell_param;
	using cellsim::channel_param;
	using cellsim::N_CH;

	enum mode { CM_LIF = 0, CM_BIO = 1 };

	struct bio
	{
		std::vector<cell> c;
		std::vector<cell_param> p;
		channel_param ch[N_CH];
		float scale[N_CH] = { 0, 0, 0, 0, 0 };   // 各通道「峰值去极化 → 电导」标定系数（按实际 dt 标定）
		float dt = 0.020f;      // 秒
		float margin = 0.14f;   // v_th − v_rest（仅用于打印/参考；突触强度已改用绝对值，见下方说明）
		float gain = 1.0f;      // 全局突触增益，用来把基线发放率校回 LIF 水平
		// 逐边通道指定（长度 = CSR 非零元数）：0xFF = 默认（按 val 符号走快兴奋/快抑制）。
		// 用它接 HRC 的第二条腿：T4/T5 是经典相关器，需要「一条快 + 一条慢且带电压依赖」。
		// 从连接组已知 T4a←Mi1 28.0% + Tm3 12.1% + Mi9 9.0%，所以让 Mi1→T4 走快通道、
		// Tm3/Tm9→T4 走慢电压依赖通道（CH_NMDA）。空 = 全部走快通道（等于没有 HRC）。
		std::vector<unsigned char> edge_chan;
		// 分流归一化增益（--norm-gain）：神经元自身的兴奋驱动产生【成比例】的分流电导。
		// 为什么需要它（实测逼出来的）：视觉链每一级的下游驱动都被自身递归主导 ——
		//   cb_intrinsic 的输入里 visual_projection 只占 5.95%、自身递归占 85.34%；
		//   vnc_motor 里 descending 只占 9.35%、vnc_intrinsic 占 81.22%。
		// 所以给通路加 5 倍增益也没用（6%×5 仍是零头，输出被 85% 的递归环路设定）。
		// 加一个与自身兴奋电导成正比的抑制电导后，稳态 Δv ≈ (1/β)(1 + g_vis/g_rec)
		// —— 输出变成【相对构成】的函数，视觉那一份的相对变化才能直接体现出来，
		// 与它占多少绝对份额无关。生理对应：反馈抑制 / 钙激活钾电流（适应）的集总模型。
		float norm_gain = 0.0f;
		// 分级细胞的【静息输出】与「只传偏移」模式。
		// ★ 为什么必须有它（实测逼出来的）：分级范围以静息为中心 ⇒ 每个分级细胞静息时恒发 0.5。
		//   52,873 个分级细胞每步恒定发 0.5 = 往网络灌一个巨大的【恒定】驱动。实测后果：
		//     lif / bio（无分级）：注入 80 → 504337 发放，注入 0 → 0 发放（刺激贡献 100%，静默干净）
		//     bio + 分级链：      注入 80 → 7608223，注入 0 → 7595116（刺激只贡献 0.2%）
		//   也就是说分级链把网络变成了「刺激无关的恒定饱和」——正是要避免的退化形态。
		//   修法：传递 s_k − out_rest_k（相对静息的偏移），绝对值不传。
		std::vector<float> out_rest;
		// 传递的是 s_k − dev_base·out_rest_k：dev_base=0 传绝对电平，=1 传纯偏移。
		// 实测两端都不行，中间才是窗：
		//   dev_base=0（绝对）：52,873 个分级细胞静息恒发 0.5 ⇒ 巨大恒定驱动 ⇒ 网络饱和、
		//                       刺激只贡献 0.2%（实测 7608223 对 7595116）
		//   dev_base=1（纯偏移）：网络静默干净（脉冲细胞 0.00 Hz）⇒ 但脉冲细胞离阈值太远，信号也到不了
		// 所以【紧张性增益与位相性增益必须分开标定】：要留一点紧张性把脉冲细胞抬到响应区，
		// 但不能多到饱和。这个参数就是那个旋钮。
		float dev_base = 0.0f;
		// 位相性增益：传 A·(s_k − dev_base·out_rest_k)。紧张性与位相性必须【分开】标定：
		//   dev_base 管紧张性（把脉冲细胞抬到响应区，但不能多到饱和 —— 实测最优在 0.7 附近，
		//   刺激贡献 0.07% → 8.0%）；A 管位相性（让偏移量够过阈）。
		// 实测脉冲细胞阈值 0.85~1.15 而静息 0.45，差 0.4~0.7，所以 A 必须 > 1 才可能传过去。
		float dev_amp = 1.0f;

		// ── 每类死区自稳（内在可兴奋性调节 / 稳态可塑性）──
		// 动机（实测）：十个解剖类的均值活动全部挤在 0.023~0.025、上限 0.06~0.075
		// —— 深层各类被钉在同一个极低静息值上，输入几乎不改变输出，所以谈不上被调制。
		// 机制：每细胞一个兴奋性倍率 h_i，按【类】的慢平均活动与死区 [lo,hi] 比较后缓慢增减。
		//   自校准：不手填每类目标值（手填 20 个常数正是当前问题的来源）。
		// ★ 时间常数必须 ≫ 刺激周期（两相窗口 1.5~2s ⇒ τ 取 20s 量级），否则它会把 looming
		//   本身当漂移抹掉。可证伪：短 τ 下"显著数"应掉回假阳性期望。
		// 正向副作用：它同时把总电导压在显式欧拉的稳定界内（见下面 g_stable）。
		float homeo_tau = 0.0f;                  // 0 = 关闭（默认 ⇒ 旧路径逐位不变）
		float homeo_lo = 0.05f, homeo_hi = 0.50f;
		std::vector<float> homeo_h;              // 每细胞倍率（长度不符或 tau=0 时完全不生效）
		std::vector<int> cell_class;             // 每细胞的类编号（<0 或越界 = 不参与）
		int n_class = 0;
		long long homeo_steps = 0;               // 自检：实际更新的步数
		// ── 电流型模式（--cell-model cur）：与 PyTorch 参考严格同构 ──────────────
		// 为什么需要它：bio（电导型 + 5 通道 + 44.8% 抑制性突触）与参考（电流型求和 + 分级饱和）
		// 不是同一个细胞模型，所以两边「秩-K 能不能穿深层」的差异无法归因。这个模式把 C++ 放到
		// 与 torch_bio_brain.py 的 BioBrain._drive 逐行对应的动力学上：
		//     h_i = Σ_k w_k·s_k            （权重即电流；w 已含 syn_gain —— 与参考的 val*syn_gain 同一语义）
		//     v_i ← α·v_i + dt·(h_i + i_ext_i)
		//     s   = clamp(v, 0, 1)         （分级）；脉冲细胞则 v≥θ 发放、复位到 0
		//   **无 I_bg、无噪声、不接通道/电导**（参考就是这样）。
		//   α = 1 − cur_alpha：参考写的是 `nv = v + (-v/tau)*dt + I*dt`，把 (dt/τ) 记为 cur_alpha
		//   就得到 v ← (1−cur_alpha)·v + dt·I。取 cur_alpha = 0.02 对应 τ=1s 的等效时间常数
		//   （参考的 τ 参数就是 0.08~0.12 这一档，同样的 (1−dt/τ) 形式）。
		// 取值：0 = 关闭（默认，老路径逐位不变）；1 = 线性求和；2 = 秩-K 分支（把求和换成 branch_mod）
		int cur_mode = 0;
		// 与参考逐字对齐的两个系数（**由参考的方程决定，不是调出来的**）：
		//   参考： nv = v + (−v/τ)·dt + I·dt  ⇒  nv = (1 − dt/τ)·v + dt·I
		//   本处： nv = (1 − cur_alpha)·v + cur_kI·I
		//   ⇒ cur_alpha = dt/τ = 0.02/0.1 = 0.2，cur_kI = dt = 0.02（τ 取参考 graded 细胞的均值 0.1 s）
		// ★ 第一版我把 cur_kI 设成 1.0（丢掉 dt 因子），于是 v_ss = I/(dt/τ) 比参考高 1/dt = 50 倍，
		//   整网被顶到 clamp=1 饱和 —— 那是**量纲错**，不是"两边工作点不同"。
		float cur_alpha = 0.2f;
		float cur_kI = 0.02f;
		// 数值上限守卫被触发的次数（0 = 本跑从未接近溢出；见 bio_step 里的说明）
		long long n_clip = 0;

		// ── 秩-K 树突亚基（分支）──────────────────────────────────────────────
		// 默认 nullptr ⇒ 点神经元路径，与加这个功能之前【逐位相同】。
		// 见 dend_branch.h 与 DESIGN_NOTES 第十四节：把「先求和、后一次非线性」换成
		// 「输入在到达胞体之前先各自非线性组合」，信号才第一次穿过深层。
		//
		// 数学上这里做的是：把原有的「边 e 的权重 w_e 对胞体驱动贡献 w_e·s_e」换成
		//     w_e · g_{i,β(e)} · relu( h_β − ϑ_β ) / h_β      （relu 模式，h_β = Σ_{e∈β} w_e s_e）
		//     w_e · g_{i,β(e)}                                  （identity 模式，Σ_β 后严格等于原式）
		// 即：**同一个线性求和**，只是每条边的权重被它所在分支的非线性重新标定了一次。
		// 正因如此，identity 模式与 K=1 是同一个算式（两条路径的逐边加法次序也相同），
		// 而 relu 模式才是真的多了一层「输入侧非线性」。
		// 注意：抑制性通道的符号走的是 val 的符号，所以这里必须用【带符号】的 |w|（fabs(w)）——
		// 分支整流只改幅度、不改符号，这样抑制不会因为取绝对值而被当成兴奋（本项目栽过一次）。
		dendbranch::dend* dend = nullptr;

		// th     ：逐神经元阈值（沿用 LIF 的阈值异质性——均匀驱动下确定性 LIF 会全网同步放电）
		// v_rest ：LIF 的稳态静息 I_bg·tau，两边必须取同一个值
		void init(std::size_t count, const std::vector<float>& th, float dt_, float v_rest_, float tau_m_, float gain_)
		{
			dt = dt_; gain = gain_;
			cellsim::default_channels(ch);
			// ★ 标定必须用【实际 dt】：τ_syn=5ms 配 dt=20ms 时 exp(-dt/τ)=0.018，电导一步衰减 98%，
			//   欧拉第一步拿初始电导乘满 20ms ⇒ 比连续解峰值高 K/dt = 4.7 倍。
			//   实测没按 dt 标定时整网 85% 每步发放、均 42Hz 饱和。
			for (int k = 0; k < N_CH; ++k)
				scale[k] = cellsim::discrete_scale(tau_m_, ch[k].tau, dt, v_rest_, ch[k].E);
			c.assign(count, cell());
			p.assign(count, cell_param());
			margin = 1.0f - v_rest_;
			for (std::size_t i = 0; i < count; ++i)
			{
				cell_param& q = p[i];
				q.v_rest = v_rest_;
				q.v_reset = 0.0f;              // 与 LIF 一致（复位到 0，非减阈值）
				q.v_th = (i < th.size()) ? th[i] : 1.0f;
				q.tau_m = tau_m_;
				q.refractory = 0.0f;           // LIF 没有不应期；先保持一致，别一次改两件事
				q.adapt_inc = 0.0f;
				q.graded = 0;
				c[i].v = v_rest_;              // 初值取静息，避免开场瞬态
			}
		}

		// 零输入自检：bio 的稳态必须等于 v_rest，也就是 LIF 的 I_bg·tau。
		// 这一条对不上，后面所有对比都是错的。
		void self_check() const
		{
			if (c.empty()) return;
			cell probe; probe.v = p[0].v_rest;
			for (int s = 0; s < 2000; ++s) cellsim::step(probe, p[0], ch, dt);
			std::printf("cellsim 后端自检: 零输入稳态 v=%.6f（期望 v_rest=%.6f，即 LIF 的 I_bg·tau）%s\n",
				probe.v, p[0].v_rest, (std::fabs(probe.v - p[0].v_rest) < 1e-4f) ? "  OK" : "  ★不一致");
			// 单条突触等效性：LIF 一个脉冲抬高 w·dt ⇒ 本后端也要抬高 w·dt（gain=1 时）。
			// 直接数值验证，不靠公式推导。
			std::printf("  单条突触对齐(数值验证): 单位权重单脉冲实际峰值 Δv = %.6f，期望 w·dt = %.6f（w=1, gain=%.2f）\n",
				cellsim::efficacy_peak_discrete(p[0].tau_m, ch[cellsim::CH_FAST_EXC].tau, dt, p[0].v_rest, ch[cellsim::CH_FAST_EXC].E) * scale[cellsim::CH_FAST_EXC] * gain * dt,
				dt, gain);
			std::printf("  通道标定系数 scale[快兴/慢兴/快抑/慢抑/NMDA] = %.3f / %.3f / %.3f / %.3f / %.3f\n",
				scale[0], scale[1], scale[2], scale[3], scale[4]);
			std::printf("  电导一步衰减率 exp(-dt/τ):");
			for (int k = 0; k < N_CH; ++k) std::printf(" %.4f", std::exp(-dt / ch[k].tau));
			std::printf("   ← 接近 0 表示该通道在 dt=%.0fms 下【一步就没了】，时间结构无法表示\n", dt * 1000);
		}
	};

	// 一步电导型网络步进。run(count, fn) 用来适配调用方各自的任务池（par_pool::run 同形）。
	// s 是本步突触前发放（可连续，支持分级输出），结果写 s_new；cur 是外部电流。
	template<class Run>
	inline void bio_step(bio& B, std::size_t n,
		const std::size_t* rp, const std::size_t* col, const float* val,
		const float* cur, const float* s, float* s_new,
		Run&& run, std::atomic<long long>& fired)
	{
		fired.store(0, std::memory_order_relaxed);
		// 逐边通道表只有在长度正好等于 CSR 非零元数时才启用（长度不符宁可当没给，避免越界）
		const bool has_ec = !B.edge_chan.empty() && (B.edge_chan.size() == (std::size_t)rp[n]);
		// 秩-K 树突：dend 为空（默认）时下面所有分支都不执行，逐边累加的算式与次序完全不变。
		const dendbranch::dend* D = B.dend;
		const bool dend_on = (D != nullptr && D->K > 1);
		// 权重 → 绝对峰值去极化 的换算：LIF 里一个脉冲把 v 抬高 w·dt，这里保证同样的峰值。
		// 换算系数在 init() 里按【实际 dt】标定过（见 cellsim.h 的 efficacy_peak_discrete）。
		const float k_ol = B.gain * B.dt;
		// ── 电流型模式（cur_mode）的常数（见 bio::cur_mode 的说明）──
		const float alpha = std::max(0.0f, 1.0f - B.cur_alpha);   // v ← alpha·v + dt·I
		const float cdt = B.dt;
		run(n, [&](std::size_t b, std::size_t e)
		{
			long long local = 0;
			for (std::size_t i = b; i < e; ++i)
			{
				cell& cc = B.c[i];
				const cell_param& q = B.p[i];
				// ── 电流型模式：与 torch_bio_brain.py 的 BioBrain._drive 严格同构 ──
				//     h_i = Σ_k w_k·s_k   （权重即电流；w 里已含 syn_gain）
				//     v_i ← α·v_i + dt·(h_i + i_ext_i)
				//     s   = clamp(v, 0, 1)（graded）或 v≥θ 发放并复位（spiking）
				//   注意：这里 **不加 I_bg、不加噪声、不做过电导/通道换算**，
				//   因为参考实现就是这样的。切换由 B.cur_mode 控制，默认 0 ⇒ 下面的老路径逐位不变。
				if (B.cur_mode != 0)
				{
					// 逐边线性求和 h_i = Σ_k w_k·s_k（权重即电流，w 已含 syn_gain）
					float h = 0.0f;
					for (std::size_t k = rp[i]; k < rp[i + 1]; ++k)
					{
						const float sk0 = s[col[k]];
						if (sk0 == 0.0f) continue;
						h += val[k] * sk0;
					}
					// cur_mode==2：把「线性求和」换成参考实现的秩-K 分支路径
					//   z = Σ_β g_β·relu(h_β − ϑ_β) − Σ_β g_β·ϑ_β     （= 参考的 Σ_β g_β·relu(...)）
					// branch_mod 返回的正是这个 z（identity 模式退回线性求和），所以直接替换 h。
					if (B.cur_mode == 2 && dend_on)
					{
						float m2[dendbranch::DEND_MAX_K], cst = 0.0f;
						const float z = dendbranch::branch_mod(*D, i, s, m2, cst);
						h = z;                       // branch_mod 已经把常数项扣掉了
					}
					const float drive = cur[i] + h;
					// v ← α·v + k_I·drive。两个系数就是为了把参考的
					//   参考： v ← (1−1/τ)·v + I          （I 不带 dt 因子）
					// 与本地步长对上：取 k_I 与 k_alpha 使 **(1−k_alpha) 与 k_I 分别等于参考的 α 与 1**。
					//   —— 参考的 `nv = v + (-v/tau)*dt + I*dt` 里 dt=1、tau=0.1 ⇒ α=0 而 I 系数 1；
					//   无论如何，两个系数都要在【标定】里定，而不是靠假定。
					const float nv = alpha * cc.v + B.cur_kI * drive;
					if (q.graded)
					{
						cc.v = nv;
						cc.out = (nv < 0.0f) ? 0.0f : (nv > 1.0f ? 1.0f : nv);
					}
					else if (nv >= q.v_th) { cc.v = 0.0f; cc.out = 1.0f; ++local; }
					else { cc.v = nv; cc.out = 0.0f; }
					s_new[i] = cc.out;
					continue;
				}
				float gsum[N_CH] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
				// 0) 秩-K 树突：先按分支算 h_β 与分支非线性，得到每条边的调制因子。
				//    mod_b = g_β·relu(h_β−ϑ_β)/h_β 的等价形式见 dend_branch.h；
				//    常数项部分（Σ_β g_β ϑ_β）与边无关，累在 gsum 的临时槽里最后一起扣掉。
				float mod[dendbranch::DEND_MAX_K];
				bool has_mod = false;
				float dend_const = 0.0f;
				if (dend_on)
				{
					const float z = dendbranch::branch_mod(*D, i, s, mod, dend_const);
					has_mod = true;
					(void)z;            // z 不直接用：胞体的线性部分仍走下面的逐边累加（保符号、保通道）
					gsum[cellsim::CH_SLOW_INH] += dend_const;
				}
				// 1) 收集本步的突触电导，按通道分槽累加
				//    （接上 HRC 之后同一行会有不同通道，不能再只用兴奋/抑制两个标量）
				for (std::size_t k = rp[i]; k < rp[i + 1]; ++k)
				{
					const std::size_t pk = col[k];
					// 传 A·(s_k − dev_base·out_rest_k)：分级细胞静息输出 0.5，绝对电平会把网络灌饱和
					const float sk = (B.dev_base != 0.0f && !B.out_rest.empty() && pk < B.out_rest.size())
						? (B.dev_amp * (s[pk] - B.dev_base * B.out_rest[pk])) : (B.dev_amp * s[pk]);
					if (sk == 0.0f) continue;
					const float w = val[k];
					// ★★ 这里是本项目最贵的一个 bug（r571 里就是它，写清楚免得再犯）：
					//   调制因子只该【乘】在权重上，不该把权重的符号吃掉。
					//     旧（错）：w_abs = has_mod ? (fabs(w) * mod[b]) : w;  … gsum[ci] += (w>=0 ? w_abs : -w_abs) * sk;
					//       K=1 时 w_abs = w（带符号），三元式再把负号还原一次 ⇒ 抑制边贡献 = w·sk  ✔
					//       K>1 时 w_abs = |w|·mod（符号已丢），三元式又取反 ⇒ 抑制边贡献 = +|w|·mod·sk  ✘
					//       于是同一张图、同一组参数，--dend 8 --dend-identity 的【抑制边整体反号】：
					//       第 0 步 gsum[CH_FAST_INH] 从 −0.2058 变成 +0.2058，两条轨迹当场分岔
					//       （实测：ol_intrinsic 显著数 164 对 1335、稳态 0.734 对 0.293）。
					//     而我当时只用静态驱动自检（同一个 s 下比【总和】）判等价 —— 那个和里抑制与兴奋
					//     混在一起，恰好把单通道反号抵消掉，于是我把差异误归因成「浮点加法次序敏感」。
					//   正确写法：保持逐边算式的形状（乘号左边仍带符号），把标量调制因子乘进去即可：
					//       K=1 → gsum[ci] += w · sk              （与改动前逐位相同）
					//       K>1 → gsum[ci] += w · mod_b · sk      （identity 时 mod_b ≡ 1 ⇒ 与 K=1 逐位相同）
					//   推导：Σ_b g_b·relu(h_b−ϑ_b) = Σ_e w_e·s_e·mod_{β(e)} − Σ_b g_b·ϑ_b；
					//   下面各通道累加器存的正是 Σ_e w_e·mod_{β(e)}·s_e（带符号），分支阈值常数项单独扣。
					float w_abs;
					if (has_mod) w_abs = mod[D->edge_b[k]] * w; else w_abs = w;
					int ci;
					if (w >= 0.0f)
						ci = (has_ec && B.edge_chan[k] != 0xFF) ? (int)B.edge_chan[k] : (int)cellsim::CH_FAST_EXC;
					else
						ci = (int)cellsim::CH_FAST_INH;      // 抑制通道的慢腿还没假设，先保持快抑制
					gsum[ci] += ((w >= 0.0f) ? w_abs : -w_abs) * sk;
				}
				// 1b) 扣掉分支阈值项（见步骤 0 的说明）：identity 模式下 dend_const=0，
				//     逐边累加就是 Σ w_e s_e，与本功能引入前【严格同一个算式】。
				if (dend_on) gsum[cellsim::CH_SLOW_INH] -= dend_const;
				// 偏移可正可负 ⇒ 各通道电导之和必须钳在 0 以上（释放率不能为负）
				if (B.dev_base != 0.0f)
					for (int c = 0; c < N_CH; ++c) if (gsum[c] < 0.0f) gsum[c] = 0.0f;
				// 1c) 每类自稳倍率（--homeo-tau>0 才生效；默认 h 不参与 ⇒ 与改动前逐位相同）
				if (B.homeo_tau > 0.0f && i < B.homeo_h.size())
				{
					const float hh = B.homeo_h[i];
					if (hh != 1.0f)
						for (int c = 0; c < N_CH; ++c) if (gsum[c] > 0.0f) gsum[c] *= hh;
				}
				// 2) 换成电导（同一套标定），必要时叠加【分流归一化】，再走膜方程
				float gc[N_CH] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
				for (int c = 0; c < N_CH; ++c)
					if (gsum[c] > 0.0f)
						gc[c] = gsum[c] * k_ol * B.scale[c] / std::max(cellsim::unblock(B.ch[c], cc.v), 1e-6f);
				if (B.norm_gain > 0.0f)
				{
					// 自身兴奋驱动 → 成比例的分流抑制。稳态 Δv ≈ (1/β)(1 + g_vis/g_rec)：
					// 输出变成相对构成的函数，视觉那一份的相对变化与它占多少绝对份额无关。
					const float ge = gc[cellsim::CH_FAST_EXC] + gc[cellsim::CH_SLOW_EXC] + gc[cellsim::CH_NMDA];
					gc[cellsim::CH_SLOW_INH] += B.norm_gain * ge;
				}
				for (int c = 0; c < N_CH; ++c)
					if (gc[c] > 0.0f) cc.g[c] += gc[c];
				// ── 显式欧拉的稳定上限（修高 syn_gain 下的 NaN）────────────────────
				// 实测故障（K=1 默认路径同样复现，与本功能的树突无关）：
				//   --sec 2 --syn-gain 480 --cell-model bio --bio-graded --graded-base 0.7 --graded-amp 4
				//   --inject visual_R1_8 80
				// ⇒ ol_sensory / ol_intrinsic 读数全 NaN、R1-6 里 3262/3377 是 -nan(ind)，共 23 行。
				// 首发量（临时探针逐步检查 isfinite(v) 抓到，探针已删）：
				//   NANSRC i=120302 g=0.0033 0 105.8 0 0 | gc=0 0 1177 0 0 | gsum=0 0 1707 0 0 | v=-inf
				//   —— 最先溢出的是 **v**，触发通道是 CH_FAST_INH（scale≈34.5）。
				// 机理（不是"某个数太大"，是**积分器失稳**）：cellsim::step 是显式欧拉
				//     v' = v + dt·[ −(v−v_rest)/τ_m + Σ_c g_c·(E_c − v) ]
				//   把同一通道合并，v' = (1 − dt/τ_m − dt·g)·v + dt·(v_rest/τ_m + g·E)。
				//   稳定的充要条件是 |1 − dt/τ_m − dt·g| < 1，即
				//     **g < (2 − dt/τ_m)/dt**
				// 稳定的充要条件是对**全部通道之和**成立（E 只平移不动点，不参与稳定性）：
				//     |1 − dt/τ_m − dt·Σ_c g_c| < 1   ⇒   **Σ_c g_c < (2 − dt/τ_m)/dt**
				//   代入 dt=0.02、τ_m=0.1 ⇒ Σg < 90。实测 g≈1283（gc 1177 累加进去）时
				//   系数 = 1 − 0.2 − 25.7 = −24.9 ⇒ 每步把 v 放大 25 倍并翻号 ⇒ 两三步就到 ±inf。
				// 修法：把**总电导**按比例缩到稳定上限内（这是数值稳定的必要条件，不是模型选择）。
				//   ★ 只钳**总和**是不够的（第一版只钳单通道，g=90 时系数恰好 −1.0、临界不衰减，
				//     NaN 仍有 16 行）；这里留 10% 余量 ⇒ 系数 −0.82，稳定。
				//   ★ 判据：正常量程必须碰不到它 —— syn_gain 16/60 下总电导 ≤ 数十，
				//     实测 K=1 回归逐位不变、返回码 0、守卫触发行 0。超过上限的工作点本来就没有
				//     数值意义，钳住只是把"算出 NaN"变成"算出饱和"。
				{
					const float g_stable = 0.9f * (2.0f - B.dt / std::max(q.tau_m, 1e-6f)) / B.dt;
					float gtot = 0.0f;
					for (int c = 0; c < N_CH; ++c) gtot += cc.g[c];
					if (gtot > g_stable)
					{
						const float s = g_stable / gtot;
						for (int c = 0; c < N_CH; ++c) cc.g[c] *= s;
						++B.n_clip;
						if (B.n_clip == 1)
							std::fprintf(stderr, "★ bio_step 总电导被钳到显式欧拉稳定上限 Σg<%.3g"
								"（dt=%.3g τ_m=%.3g；首次在神经元 %zu，Σg=%.4g）—— 超过它的工作点本来就会发散成 NaN\n",
								(double)g_stable, (double)B.dt, (double)q.tau_m, (std::size_t)i, (double)gtot);
					}
				}
				cellsim::step(cc, q, B.ch, B.dt, cur[i]);
				s_new[i] = cc.out;
				// ★ 只把【脉冲】细胞计入发放数：分级细胞输出是 0~1 的模拟量、静息恰好 0.5，
				//   用 out>0.5 判发放会把 5 万多个分级细胞全算成「每步都发放」。
				//   实测被这个骗过：分级链下报「总发放 800 万」，而脉冲细胞其实是 0.00 Hz。
				if (cc.out > 0.5f && !q.graded) ++local;
			}
			if (local) fired.fetch_add(local, std::memory_order_relaxed);
		});
		// ── 每类死区自稳：把各类的慢平均活动拉进 [lo,hi]（--homeo-tau>0 才生效）──
		// 放在整步之后（两趟线性扫描，相对 2560 万突触可忽略）。
		// 自校准：目标不是一个手填的数，而是"落在带内就不动"——越界的类才被推。
		if (B.homeo_tau > 0.0f && B.n_class > 0 && B.homeo_h.size() == n && B.cell_class.size() == n)
		{
			const std::size_t nc = (std::size_t)B.n_class;
			std::vector<double> sum(nc, 0.0), cnt(nc, 0.0);
			for (std::size_t i = 0; i < n; ++i)
			{
				const int cl = B.cell_class[i];
				if (cl < 0 || (std::size_t)cl >= nc) continue;
				sum[cl] += (double)s_new[i]; cnt[cl] += 1.0;
			}
			const double eta = (double)B.dt / (double)B.homeo_tau;
			for (std::size_t i = 0; i < n; ++i)
			{
				const int cl = B.cell_class[i];
				if (cl < 0 || (std::size_t)cl >= nc) continue;
				const double m = sum[cl] / std::max(cnt[cl], 1.0);
				float& hh = B.homeo_h[i];
				if (m < B.homeo_lo) hh = (float)(hh * (1.0 + eta));
				else if (m > B.homeo_hi) hh = (float)(hh * (1.0 - eta));
				if (hh < 0.02f) hh = 0.02f; else if (hh > 200.0f) hh = 200.0f;
			}
			++B.homeo_steps;
		}
	}
}

#endif
