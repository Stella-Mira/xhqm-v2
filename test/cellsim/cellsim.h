// cellsim —— 单细胞模拟器
//
// 目的：把「一个细胞在什么条件下会做什么事」写成【显式规格 + 可测行为】，再插进果蝇系统里。
//
// 为什么需要它（都是实测出来的缺口）：
//   1. 现在的神经元是「电流型线性求和 + 硬阈值」：I = Σ w·s，v += dt·I，v≥θ 就发放。
//      实测后果——背景递归活动（≈1.2 的驱动）和阈值裕度（1.4）同量级，刺激只是在
//      「改变发放概率」；每跨一级信号被稀释 7~10 倍。
//   2. 没有突触时间常数 ⇒ 没有相位滞后 ⇒ T4/T5 做不了 HRC 相关器。
//      实测：全场闪光下 T4c = 17.5，而运动光栅下只有 1.2（与对照 -0.3 无法区分），
//      也就是说模型里的 T4 是亮度检测器不是运动检测器。
//   3. 没有分流抑制 ⇒ 没有乘性运算 ⇒ 没有对比度归一化、没有巧合检测。
//
// 本模型加了什么：
//   · 电导型突触：输入是「按突触前类型打开某个电导通道」，不是直接加电流
//   · 四类电导通道（快/慢 × 兴奋/抑制），通道由【突触前细胞类型】决定
//     ⇒ 同一条通路可以是「直通快 + 延迟慢」，这正是 HRC 需要的两条腿
//   · 分流抑制：g_inh·(E_inh − v) 随 v 变 ⇒ 乘性
//   · 发放适应 / 不应期 / 分级（非脉冲）输出（光感受器与 LMC 是分级的）
//
// 单位约定：膜电位归一化到「阈值 ≈ 1.0」。静息由 v_rest 直接给出（程序里等价于背景电流）。
#pragma once
#ifndef XHQM_TEST_CELLSIM_H
#define XHQM_TEST_CELLSIM_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cellsim {

// ── 电导通道：哪个通道、时间常数多少、反转电位在哪 ──
// E 的单位与 v 相同（阈值 1.0）。E > 阈值 ⇒ 兴奋性；E < 静息 ⇒ 抑制性。
struct channel_param
{
	float tau = 0.010f;     // 电导衰减时间常数（秒）
	float E = 4.0f;         // 反转电位
	int inhibitory = 0;
	// NMDA 式电压依赖去阻遏：电导乘一个随 v 上升而解锁的因子。
	// unblock(v) = 1 / (1 + mg * exp(-beta * (v - v_half)))
	// 这是【乘性/巧合检测】的关键：另一条通路先把 v 顶上去，这条通道的电流就被放大，
	// 于是「两条输入的和」不再等于「分别作用之和」——纯电导 LIF 没有它就只能相加。
	float mg = 0.0f;        // 0 = 无电压依赖
	float beta = 12.0f;
	float v_half = 0.95f;
};

// 电压依赖因子（mg=0 时恒为 1）
inline float unblock(const channel_param& c, float v)
{
	if (c.mg <= 0.0f) return 1.0f;
	const float e = c.mg * std::exp(-c.beta * (v - c.v_half));
	return 1.0f / (1.0f + e);
}

// ── 突触强度怎么标定（这里踩过坑，记下来）──
//
// 一度按「积分电量相等」标定通道：open_syn 里加的是 charge/tau，保证每条突触送进膜的电荷一样。
// 对 τ_syn 与 τ_m 相近的通道没问题，但对慢通道是错的——实测 τ_m=20ms 时，
// 等电量的 NMDA(τ=80ms) 峰值去极化只有快通道(τ=5ms) 的 1/4，再乘去阻遏 0.274 就完全不点火，
// 整条 HRC 那条腿等于不存在。
//
// 正确做法：按【峰值去极化】标定——「这根突触单独到，能把膜电位顶多高」。
// 峰电导 g0 的脉冲输入（dv/dt = −(v−v_rest)/τ_m + g0·e^{−t/τ_s}·(E−v)，设 E−v 近似不变）解为
//   Δv(t) = g0(E−v_rest)·τ_mτ_s/(τ_m−τ_s)·(e^{−t/τ_m} − e^{−t/τ_s})
// 峰值在 t* = τ_mτ_s/(τ_m−τ_s)·ln(τ_m/τ_s)，故
//   Δv_peak = g0(E−v_rest)·K(τ_m,τ_s)
//   K = τ_mτ_s/(τ_m−τ_s)·[ (τ_s/τ_m)^{τ_s/(τ_m−τ_s)} − (τ_s/τ_m)^{τ_m/(τ_m−τ_s)} ]
// 校验：τ_m=20ms 时 K(20,5)=3.15ms、K(20,80)=12.6ms ⇒ 同样的 g0，慢通道强 4 倍（它持续久）。
inline float efficacy_k(float tau_m, float tau_s)
{
	if (std::fabs(tau_m - tau_s) < 1e-9f) return tau_m * std::exp(-1.0f);   // τ_s→τ_m 的极限 τ·e^{-1}
	const float d = tau_m - tau_s;
	const float r = tau_s / tau_m;
	return (tau_m * tau_s / d) * (std::pow(r, tau_s / d) - std::pow(r, tau_m / d));
}

// ── 细胞类型的参数：这就是「什么条件下做什么事」的规格 ──
struct cell_param
{
	float v_rest = 0.86f;   // 静息电位（由背景电流决定；阈值 1.0）
	float v_th = 1.0f;
	float v_reset = 0.0f;
	float tau_m = 0.100f;   // 膜时间常数
	float adapt_inc = 0.0f; // 每发放一次阈值上抬多少（0 = 不适应）
	float tau_adapt = 0.30f;
	float refractory = 0.0f;
	int graded = 0;         // 1 = 连续输出（分级电位细胞，不发放脉冲）
	float graded_v0 = 0.86f;  // 分级输出的参考点
	float graded_v1 = 1.6f;   // 到这个电位时输出饱和为 1
};

enum : int { CH_FAST_EXC = 0, CH_SLOW_EXC = 1, CH_FAST_INH = 2, CH_SLOW_INH = 3, CH_NMDA = 4, N_CH = 5 };

// ── 细胞状态 ──
struct cell
{
	float v = 0.0f;
	float a = 0.0f;          // 适应变量（叠加在阈值上）
	float ref = 0.0f;        // 不应期剩余
	float g[N_CH] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	float out = 0.0f;        // 本步输出：脉冲 0/1，或分级 0..1
};

// 阈值裕度（从静息推到阈值需要的膜电位增量）
inline float margin(const cell_param& p) { return p.v_th - p.v_rest; }

// ── 单位峰电导在【实际积分器】下产生的峰值去极化 ──
//
// 为什么不能直接用上面连续解的 K：网络跑的是 dt=20ms 的欧拉，而 τ_syn=5ms 时
// exp(-dt/τ)=0.018 ⇒ 电导【一步就衰减 98%】。欧拉第一步用初始电导乘满 20ms，
// 得到的峰值比连续解高 K/dt = 4.27/20 = 4.7 倍 ⇒ 每条突触都强 4.7 倍，整网 85% 饱和。
// 实测就是这么炸的（--cell-mode bio 一开：85.2% 每步发放、均 42Hz）。
// 结论：【标定必须对着你真正跑的那个积分器做】，不是对着理想 ODE。
// 本函数用与 cellsim::step 完全同形的离散更新跑一条单位脉冲，返回峰值【偏转的绝对值】。
// ★ 必须取绝对值：抑制通道的 E(−1.0) 低于静息，偏转是负的。第一版只跟踪 vmax，
//   于是抑制通道算出 0 并被 1e-9 底噪托住 ⇒ 标定系数 1e9 ⇒ 抑制电导天文数字
//   ⇒ 全网等频自激（实测每个 superclass 都恰好 24.8Hz，那就是人造振荡的指纹）。
//   方向由通道自己的 E 决定，标定只管幅度。
inline float efficacy_peak_discrete(float tau_m, float tau_s, float dt, float v_rest, float E)
{
	float v = v_rest, g = 1.0f, vmax = v_rest, vmin = v_rest;
	const float r = std::exp(-dt / tau_s);
	const int n = (int)(6.0f * tau_s / dt) + 6;          // 覆盖电导衰减到可忽略所需步数
	for (int k = 0; k < n; ++k)
	{
		const float dv = (-(v - v_rest) / tau_m + g * (E - v)) * dt;   // 与 step() 同序：先算 dv 再衰减 g
		v += dv;
		g *= r;
		if (v > vmax) vmax = v;
		if (v < vmin) vmin = v;
	}
	return std::max(std::max(vmax - v_rest, v_rest - vmin), 1e-9f);
}

// 标定系数：scale = 1 / (单位峰电导的峰值去极化)。
// 必须在【实际 dt】下标定，且是热路径 ⇒ init 时按通道算一次，步进里只做乘法。
inline float discrete_scale(float tau_m, float tau_s, float dt, float v_rest, float E)
{
	return 1.0f / efficacy_peak_discrete(tau_m, tau_s, dt, v_rest, E);
}

// 打开一条突触（热路径版）：dpk_abs 是「单条突触单独作用能把膜电位顶高的幅度」，单位与 v 相同。
// 注意用 dpk_abs（绝对值）而不是裕度倍数 —— 阈值异质时「裕度」会随细胞变号，
// 那会让兴奋通道拿到负电导（踩过这个坑）。
inline void add_syn(cell& c, const channel_param ch[N_CH], int chan, float scale, float dpk_abs)
{
	const channel_param& k = ch[chan];
	c.g[chan] += dpk_abs * scale / std::max(unblock(k, c.v), 1e-6f);
}

// 便利版（测试用，非热路径）：强度按裕度给，标定当场算。
inline void open_syn(cell& c, const cell_param& p, const channel_param ch[N_CH], int chan, float dpk_margin, float dt)
{
	const channel_param& k = ch[chan];
	const float sc = discrete_scale(p.tau_m, k.tau, dt, p.v_rest, k.E);
	add_syn(c, ch, chan, sc, dpk_margin * margin(p));
}

// 一步更新。caller 负责：
//   1) 先把本步到达的突触「打开电导」：open_syn(c, p, ch, chan, dpk)   （每条突触各自的 chan）
//   2) 再调用本函数做膜方程 + 发放判定
// i_ext：外部注入电流（视觉驱动、噪声等）。果蝇主程序用它把两级模型对齐——
//   LIF 那边是 I = I_bg + pr_cur + noise 而漏电拉向 0，稳态 v_ss = I_bg·tau；
//   这边是漏电拉向 v_rest（=0.86），所以 i_ext 只放 pr_cur + noise，I_bg 由 v_rest 承担，
//   零输入时两边稳态严格相同 ⇒ A/B 的差异只剩「突触时间过程」。
inline void step(cell& c, const cell_param& p, const channel_param ch[N_CH], float dt, float i_ext = 0.0f)
{
		float Isyn = 0.0f;
	for (int k = 0; k < N_CH; ++k)
		Isyn += c.g[k] * unblock(ch[k], c.v) * (ch[k].E - c.v);   // 分流 + 电压依赖
	const float dv = (-(c.v - p.v_rest) / p.tau_m + Isyn + i_ext) * dt;
	c.v += dv;
	for (int k = 0; k < N_CH; ++k) c.g[k] *= std::exp(-dt / ch[k].tau);
	c.a *= std::exp(-dt / p.tau_adapt);
	if (c.ref > 0.0f) { c.ref -= dt; c.out = 0.0f; return; }
	if (p.graded)
	{
		const float u = (c.v - p.graded_v0) / std::max(p.graded_v1 - p.graded_v0, 1e-6f);
		c.out = (u < 0.0f) ? 0.0f : (u > 1.0f ? 1.0f : u);
		return;
	}
	if (c.v >= p.v_th + c.a) { c.v = p.v_reset; c.a += p.adapt_inc; c.ref = p.refractory; c.out = 1.0f; }
	else c.out = 0.0f;
}

// ── 预置的细胞类型（参数集中在这里，便于对照与标定）──
// 光感受器：分级（不发放脉冲）、有光适应（用适应变量实现「响应变化而不是绝对亮度」）
inline cell_param photo_param()
{
	cell_param p;
	p.v_rest = 0.86f; p.tau_m = 0.020f; p.graded = 1;
	p.adapt_inc = 0.0f; p.tau_adapt = 0.20f;
	return p;
}
// lamina LMC：快、分级、符号反转由突触通道承担（光感受器是组胺能=抑制性）
inline cell_param lmc_param()
{
	cell_param p;
	p.v_rest = 0.86f; p.tau_m = 0.010f; p.graded = 1;
	return p;
}
// 普通脉冲神经元（medulla intrinsic / T4 / DN / motor 等先用同一套基线）
inline cell_param spiking_param()
{
	cell_param p;
	p.v_rest = 0.86f; p.tau_m = 0.100f; p.refractory = 0.002f;
	return p;
}
// T4/T5 式巧合检测细胞：膜更快、不应期更短（要能分辨毫秒级的先后）
inline cell_param coincidence_param()
{
	cell_param p;
	p.v_rest = 0.86f; p.tau_m = 0.020f; p.refractory = 0.001f;
	return p;
}

// 四/五个电导通道的默认参数：
//   快兴奋 5ms / 慢兴奋 40ms（延迟线）/ 快抑制 8ms / 慢抑制 50ms / NMDA 50ms（电压依赖）
//
// NMDA 去阻遏参数是踩坑调出来的，记下来：初版 mg=0.9, beta=12, v_half=0.95，
// 而静息 0.86、裕度只有 0.14 ⇒ 去阻遏因子在【一个裕度之内】就从 0.274 冲到 1.0（3.65 倍）。
// 实测后果：细胞自持发放，20s 内发 1000~2400 次而输入只有 324 根，方向指数在 ±0.9 之间乱跳
// ——那不是巧合检测，是双稳触发器。
//  现在的取值让去阻遏在工作区间内【平滑】变化：u(0.86)=0.35 → u(1.2)=0.75，即 2.1 倍乘性调制，
//  足以把「两条腿的和」变成非线性，又不至于自激。
inline void default_channels(channel_param ch[N_CH])
{
	ch[CH_FAST_EXC] = { 0.005f, 4.0f, 0 };
	ch[CH_SLOW_EXC] = { 0.040f, 4.0f, 0 };
	ch[CH_FAST_INH] = { 0.008f, -1.0f, 1 };
	ch[CH_SLOW_INH] = { 0.050f, -1.0f, 1 };
	ch[CH_NMDA] = { 0.050f, 4.0f, 0 };
	ch[CH_NMDA].mg = 0.073f; ch[CH_NMDA].beta = 5.05f; ch[CH_NMDA].v_half = 1.5f;
}

} // namespace cellsim
#endif
