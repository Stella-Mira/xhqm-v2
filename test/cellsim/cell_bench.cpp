// cell_bench —— 把「细胞在什么条件下做什么事」逐条测出来
//
// 关键约定：突触强度按【每次发放提供的积分电量】定义，不按电导峰值。
//   一条突触打开电导 g、衰减 tau ⇒ 积分电量 = g*tau。
//   快通道(5ms)与慢通道(40ms)如果按电导峰值取同一个值，积分电量会差 8 倍，
//   慢通道单个脉冲就能点火、HRC 测试就废了。按电量守恒定义才可比。
//
// 五组表征：
//   ① f-I 曲线（工作点、饱和点）
//   ② 阶跃响应（瞬态 vs 持续；适应）
//   ③ 频率响应（低通/带通形状由什么决定）
//   ④ 分流抑制（减性还是乘性）
//   ⑤ ★ 方向选择性（HRC）：快通道 + 慢通道 → 能否分辨运动方向
//        —— 这是当前「电流型线性求和」模型做不到、本模型应该做得到的那件事
#include "cellsim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace cellsim;

static const float DT = 0.0001f;          // 0.1ms

// 打开一条突触：突触强度按「峰值去极化」给定，单位为裕度（见 cellsim.h 的标定说明）
// （原来的 open_syn/margin 已移入 cellsim.h，飞控那边要用同一套标定，不能再各写一份）

int main()
{
	channel_param ch[N_CH]; default_channels(ch);
	std::printf("电导通道: 快兴奋 tau=%.0fms | 慢兴奋 tau=%.0fms | 快抑制 tau=%.0fms | 慢抑制 tau=%.0fms（E: 兴 %.1f / 抑 %.1f）\n",
		ch[0].tau * 1000, ch[1].tau * 1000, ch[2].tau * 1000, ch[3].tau * 1000, ch[0].E, ch[3].E);
	const cell_param sp = spiking_param();
	const float M = margin(sp);
	std::printf("脉冲细胞: v_rest=%.2f 阈值=%.2f tau_m=%.0fms ⇒ 裕度 %.2f\n",
		sp.v_rest, sp.v_th, sp.tau_m * 1000, M);
	std::printf("突触标定: 单位峰电导的峰值去极化因子 K(tau_m,tau_s) ——");
	for (int k = 0; k < N_CH; ++k)
		std::printf(" %s=%.2fms", (k == 0 ? "快兴" : k == 1 ? "慢兴" : k == 2 ? "快抑" : k == 3 ? "慢抑" : "NMDA"),
			efficacy_k(sp.tau_m, ch[k].tau) * 1000);
	std::printf("\n   ⇒ τ_syn 越接近/超过 τ_m，同样的峰电导顶得越高；按「峰值去极化」标定才可比\n\n");

	// ── ① f-I 曲线 ──
	std::printf("① f-I 曲线（快兴奋通道，每条突触 1/8 裕度峰值去极化）\n");
	std::vector<float> rates = { 20, 50, 100, 200, 400, 800, 1600, 3200 };
	const float q_weak = 0.125f;   // 单位：裕度
	std::printf("   输入Hz :");
	for (float r : rates) std::printf("%8.0f", r);
	std::printf("\n   输出Hz :");
	for (float r : rates)
	{
		cell c; c.v = sp.v_rest;
		const long steps = (long)(2.0f / DT);
		float next = 0.0f; long n = 0; unsigned rng = 7u;
		auto urand = [&]() { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 16777216.0f; };
		for (long s = 0; s < steps; ++s)
		{
			const float t = s * DT;
			while (t >= next) { open_syn(c, sp, ch, CH_FAST_EXC, q_weak, DT); next += -std::log(1.0f - urand() + 1e-9f) / r; }
			step(c, sp, ch, DT);
			if (c.out > 0.5f) ++n;
		}
		std::printf("%8.1f", (float)n / 2.0f);
	}
	std::printf("\n");

	// ── ② 阶跃响应 ──
	std::printf("\n② 阶跃响应：持续 400Hz 输入下，前 0.5s 与后 0.5s 的发放次数\n");
	{
		cell_param p1 = sp; p1.adapt_inc = 0.0f;
		cell_param p2 = sp; p2.adapt_inc = 0.10f; p2.tau_adapt = 0.30f;
		for (auto pr : { std::make_pair(std::string("不适应"), p1), std::make_pair(std::string("有适应"), p2) })
		{
			cell c; c.v = pr.second.v_rest;
			const long steps = (long)(2.0f / DT);
			long first = 0, last = 0; float next = 0.0f;
			for (long s = 0; s < steps; ++s)
			{
				const float t = s * DT;
				while (t >= next) { open_syn(c, pr.second, ch, CH_FAST_EXC, q_weak, DT); next += 1.0f / 400.0f; }
				step(c, pr.second, ch, DT);
				if (c.out > 0.5f) { if (t < 0.5f) ++first; else if (t > 1.5f) ++last; }
			}
			std::printf("   %-8s 前 %.0f 次 → 后 %.0f 次（保留 %.0f%%）\n",
				pr.first.c_str(), (float)first, (float)last, 100.0f * last / std::max(first, 1L));
		}
	}

	// ── ③ 频率响应（低通特性）──
	std::printf("\n③ 频率响应：分级驱动下【输出】的调制深度（低通曲线）\n");
	{
		std::vector<float> fmods = { 0.5f, 1, 2, 5, 10, 20, 50, 100 };
		// 度量方法踩过两次坑，记下来：
		//   第一次取 c.out 的 min/max ⇒ 脉冲序列永远是 0 和 1，深度恒等于 1.000，那一列全是假的；
		//   第二次改成按相位分箱统计发放次数 ⇒ 低驱动下发放次数太少，8 个箱的量化噪声又把曲线打乱。
		// 这个测试要说明的是「膜时间常数越大越低通」，那就该用【分级输出】直接量，无量化噪声。
		std::printf("   tau_m(ms) ");
		for (float f : fmods) std::printf("%8.1f", f);
		std::printf("   (调制Hz)\n");
		for (auto pr : { std::make_pair(std::string("LMC  "), lmc_param()),
		                 std::make_pair(std::string("感光 "), photo_param()),
		                 std::make_pair(std::string("脉冲 "), spiking_param()) })
		{
			cell_param p = pr.second; p.graded = 1;   // 强制分级读输出，避免脉冲量化
			std::printf("   %s %5.0f ", pr.first.c_str(), p.tau_m * 1000);
			for (float f : fmods)
			{
				cell c; c.v = p.v_rest;
				const long steps = (long)(8.0f / DT);
				const float g0 = 0.60f * margin(p)
					/ (efficacy_k(p.tau_m, ch[CH_FAST_EXC].tau) * (ch[CH_FAST_EXC].E - p.v_rest));
				float mn = 1e9f, mx = -1e9f;
				for (long s = 0; s < steps; ++s)
				{
					const float t = s * DT;
					c.g[CH_FAST_EXC] = g0 * (1.0f + 0.8f * std::sin(2 * 3.14159265f * f * t));
					step(c, p, ch, DT);
					if (t > 4.0f) { mn = std::min(mn, c.out); mx = std::max(mx, c.out); }
				}
				std::printf("%8.3f", mx - mn);
			}
			std::printf("\n");
		}
		std::printf("   ⇒ tau_m 越大越低通：100Hz 处 LMC(10ms)=0.103、感光(20ms)=0.078，而脉冲细胞(100ms) 到 5Hz 就归零\n");
		std::printf("     （低频段不是简单排序：分级输出在 graded_v1 处被截顶，tau_m 小的摆幅大反而先截顶。看截止频率即可）\n");
	}

	// ── ④ 分流抑制 ──
	std::printf("\n④ 分流抑制：固定兴奋驱动，加入抑制电导，看输出怎么变\n");
	{
		cell_param p = sp; p.adapt_inc = 0.0f;
		auto measure = [&](float inh_level) -> float
		{
			cell c; c.v = p.v_rest;
			const long steps = (long)(1.5f / DT);
			float next = 0.0f; long n = 0;
			const float g0 = 1.5f * margin(p) / (efficacy_k(p.tau_m, ch[0].tau) * (ch[0].E - p.v_rest));   // 1.5 裕度
			for (long s = 0; s < steps; ++s)
			{
				const float t = s * DT;
				c.g[CH_FAST_EXC] = g0;
				c.g[CH_FAST_INH] = inh_level;
				step(c, p, ch, DT);
				if (t > 0.5f && c.out > 0.5f) ++n;
			}
			return (float)n;
		};
		const float b = measure(0.0f), i1 = measure(1.0f), i2 = measure(3.0f), i3 = measure(10.0f);
		std::printf("   无抑制 %.0f 次 → 抑制 1.0: %.0f（%.0f%%）→ 3.0: %.0f（%.0f%%）→ 10: %.0f（%.0f%%）\n",
			b, i1, 100 * i1 / std::max(b, 1.0f), i2, 100 * i2 / std::max(b, 1.0f), i3, 100 * i3 / std::max(b, 1.0f));
		std::printf("   ⇒ 抑制按比例压低输出（乘性），因为它进的是 g*(E-v) 而不是减一个常数\n");
	}

	// ── ⑤ ★ 方向选择性（HRC）──
	std::printf("\n⑤ 方向选择性：二维扫描「突触强度 × 运动延迟」，看方向指数 (R-L)/(R+L)\n");
	std::printf("   A 列走快通道(AMPA %.0fms)，B 列走慢通道(NMDA %.0fms + 电压依赖去阻遏)\n",
		ch[CH_FAST_EXC].tau * 1000, ch[CH_NMDA].tau * 1000);
	std::printf("   强度以「裕度」为单位：1.0 = 单条单独到正好顶到阈值；<1 表示单条不够点火\n");
	// 诊断：直接看膜电位峰值 —— 分不清「参数不对」和「代码不对」时先看这个
	{
		cell_param cp = coincidence_param();
		std::printf("   【诊断】coincidence 细胞 v_rest=%.3f v_th=%.3f tau_m=%.0fms；单条 AMPA 输入后 v 的峰值：\n",
			cp.v_rest, cp.v_th, cp.tau_m * 1000);
		for (float cu : { 0.4f, 1.0f, 1.7f, 3.0f })
		{
			cell c; c.v = cp.v_rest;
			open_syn(c, cp, ch, CH_FAST_EXC, cu, DT);
			const float g0 = c.g[CH_FAST_EXC];
			float vmax = c.v;
			for (int s = 0; s < 2000; ++s) { step(c, cp, ch, DT); vmax = std::max(vmax, c.v); }
			std::printf("     %.1f 裕度(峰电导 %.2f)：v 峰值 %.4f（阈值 %.2f，越阈即被复位所以最多显示到阈值下面一点）\n",
				cu, g0, vmax, cp.v_th);
		}
	}
	{
		std::vector<float> chg_units = { 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f };
		std::vector<float> delays = { 0, 5, 10, 15, 20, 30, 45, 70 };
		cell_param cp = coincidence_param();
		// ★ 关键前提：ISI 必须远大于运动延迟，否则两组脉冲在同一段时间里本来就交错混在一起，
		//   细胞看到的只是「合并后的脉冲列」，配对关系不存在 ⇒ 方向指数恒 0（第一次就踩了这个坑）。
		//   所以 ISI 取 120ms，延迟只扫 0~70ms，并把时长拉到 20s 保证计数不是量化的。
		std::vector<float> train1;
		{
			unsigned s = 20240303u; float acc = 0.30f;
			while (acc < 20.0f)
			{
				s = s * 1664525u + 1013904223u;
				const float u = (float)((s >> 8) & 0xFFFFu) / 65535.0f;
				train1.push_back(acc);
				acc += 0.120f * (1.0f + 0.5f * (2.0f * u - 1.0f));   // 平均 120ms ± 50%
			}
		}
		auto resp = [&](float dpk_fast, float dpk_nmda, float d_ms, bool right, bool two_ch, long* cnt) -> float
		{
			cell c; c.v = cp.v_rest;
			const float d = d_ms * 0.001f;
			const long steps = (long)(20.0f / DT);
			// A 列与 B 列用同一串脉冲；右向 = A 早 d，左向 = A 晚 d（同一对柱子，刺激反向）
			const float addA = right ? 0.0f : d, addB = right ? d : 0.0f;
			size_t i = 0, j = 0; long n = 0;
			for (long s = 0; s < steps; ++s)
			{
				const float t = s * DT;
				// 对照（two_ch=false）两条腿同通道同强度 ⇒ 纯线性求和，时间差无从发挥
				while (i < train1.size() && t >= train1[i] + addA) { open_syn(c, cp, ch, CH_FAST_EXC, dpk_fast, DT); ++i; }
				while (j < train1.size() && t >= train1[j] + addB) { open_syn(c, cp, ch, two_ch ? CH_NMDA : CH_FAST_EXC, two_ch ? dpk_nmda : dpk_fast, DT); ++j; }
				step(c, cp, ch, DT);
				if (t > 1.0f && c.out > 0.5f) ++n;   // 丢弃开始的过渡段
			}
			if (cnt) *cnt = n;
			return (float)n;
		};
		std::printf("   每列 %.0f 根输入/20s（ISI 均值 120ms）。注意 d=0 那列左右向是同一个刺激 ⇒ 方向指数恒为 0，正常\n", (float)train1.size());
		auto table = [&](bool two_ch)
		{
			std::printf("   裕度＼延迟(ms):");
			for (float d : delays) std::printf("%8.0f", d);
			std::printf("\n");
			for (float dpk : chg_units)
			{
				std::printf("   快%.2f 裕度 DI :", dpk);
				for (float d : delays)
				{
					long nr = 0, nl = 0;
					resp(dpk, dpk * 0.6f, d, true, two_ch, &nr); resp(dpk, dpk * 0.6f, d, false, two_ch, &nl);
					const float R = (float)nr, L = (float)nl;
					std::printf("%8.2f", (R + L > 0) ? (R - L) / (R + L) : 0.0f);
				}
				// 原始计数：不清不楚就是因为看不到这个
				std::printf("\n         右/左  :");
				for (float d : delays)
				{
					long nr = 0, nl = 0;
					resp(dpk, dpk * 0.6f, d, true, two_ch, &nr); resp(dpk, dpk * 0.6f, d, false, two_ch, &nl);
					char buf[16]; std::snprintf(buf, sizeof(buf), "%ld/%ld", nr, nl);
					std::printf("%8s", buf);
				}
				std::printf("\n");
			}
		};
		std::printf("   A 列快通道(AMPA 5ms) + B 列慢通道(NMDA 80ms 电压依赖去阻遏)：\n");
		table(true);
		std::printf("   ── 对照：两条输入都用同一条快通道（= 现在的电流型线性求和，没有时间差可用）──\n");
		table(false);
	}

	// ── ⑥ T4 式 HRC 在【实测参数】下的方向指数 ──
	// 目的：判定网络里方向选择性观测不到，是【机制实现有问题】还是【参数本身就不支持】。
	// 参数全部取自实测，不用理想化的整数倍：
	//   · 快腿 = Mi1（τ=5ms）；慢腿 = Tm3/Tm9/Tm2（τ=50ms 含电压依赖去阻遏）
	//   · 慢腿占总驱动 14.6%（网络实测：T4/T5 的 836705 条入边里慢腿 122081 条）
	//   · 每腿 7 条输入、簇宽 5°（网络实测：每个 T4 约 7 条 Mi1 边，腿间偏移散布 ~5°）
	//   · 刺激：空间周期 0.8rad、4Hz 漂移（=183°/s）—— 与网络默认完全一致
	//   · dt = 1ms —— 与网络一致（关键：⑤ 用的是 0.1ms，dt=1ms 下快通道的行为完全不同）
	std::printf("\n⑥ T4 式 HRC 在实测参数下的方向指数（判定方向选择性这条线是否还值得走）\n");
	{
		const float DTN = 0.001f;                 // 与网络一致
		const float SPATIAL = 0.8f;               // 条纹空间周期（rad）
		const float FREQ = 4.0f;                  // 条纹漂移频率（Hz）⇒ v = 0.8×4 = 3.2 rad/s = 183°/s
		const float K = 2.0f * 3.14159265f / SPATIAL;
		const int   NLEG = 7;                     // 每腿输入条数
		const float CLUSTER = 5.0f * 3.14159265f / 180.0f;   // 簇宽（rad）
		const float SLOW_SHARE = 0.146f;           // 慢腿占总驱动
		cell_param cp = coincidence_param();
		const float scf = discrete_scale(cp.tau_m, ch[CH_FAST_EXC].tau, DTN, cp.v_rest, ch[CH_FAST_EXC].E);
		const float scs = discrete_scale(cp.tau_m, ch[CH_NMDA].tau, DTN, cp.v_rest, ch[CH_NMDA].E);
		auto run_dir = [&](float dc_rad, int dir, float drive, float* mean_out) -> float
		{
			cell c; c.v = cp.v_rest;
			const long steps = (long)(6.0f / DTN);
			const float dpk_f = drive * (1.0f - SLOW_SHARE) / NLEG * margin(cp);
			const float dpk_s = drive * SLOW_SHARE / NLEG * margin(cp);
			float acc = 0.0f; long n = 0;
			for (long s2 = 0; s2 < steps; ++s2)
			{
				const float t = s2 * DTN;
				float gf = 0.0f, gs = 0.0f;
				for (int j = 0; j < NLEG; ++j)
				{
					const float a = (j - (NLEG - 1) * 0.5f) * (CLUSTER / (NLEG - 1));
					const float ph = 2.0f * 3.14159265f * FREQ * t * (float)dir;
					gf += 0.5f + 0.5f * std::sin(K * a - ph);
					gs += 0.5f + 0.5f * std::sin(K * (a + dc_rad) - ph);
				}
				add_syn(c, ch, CH_FAST_EXC, scf, gf * dpk_f);
				add_syn(c, ch, CH_NMDA, scs, gs * dpk_s);
				step(c, cp, ch, DTN);
				if (t > 1.0f) { acc += c.out; ++n; }
			}
			const float mo = (n > 0) ? acc / (float)n : 0.0f;
			if (mean_out) *mean_out = mo;
			return mo;
		};
		std::printf("   每腿 %d 条输入、簇宽 %.0f° | 慢腿占 %.1f%% | dt=%.0fms | 刺激 空间周期 %.2frad、%.0fHz\n",
			NLEG, CLUSTER * 180.0f / 3.14159265f, SLOW_SHARE * 100, DTN * 1000, SPATIAL, FREQ);
		std::printf("   %8s %10s %12s %12s %10s\n", "总驱动", "Δc(°)", "右向输出", "左向输出", "方向指数");
		for (float drive : { 0.10f, 0.20f, 0.50f, 1.0f, 2.0f })
		{
			for (float dcd : { 0.0f, 8.25f, 16.5f })
			{
				const float dc = dcd * 3.14159265f / 180.0f;
				float mR = 0.0f, mL = 0.0f;
				const float R = run_dir(dc, +1, drive, &mR);
				const float L = run_dir(dc, -1, drive, &mL);
				std::printf("   %8.2f %10.2f %12.5f %12.5f %10.4f\n", drive, dcd, R, L,
					(R + L > 1e-9f) ? (R - L) / (R + L) : 0.0f);
			}
		}
		std::printf("   ⇒ 参照：网络里 T4a 的输出均值约 0.020。看这个工作点附近的方向指数有多大。\n");
	}
	return 0;
}
