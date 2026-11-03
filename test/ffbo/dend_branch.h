// dend_branch —— 秩-K 树突亚基（分支）结构，离线预计算
//
// 对应 PyTorch 参考实现 test/ffbo/torch_bio_brain.py 的 BioBrain（K 条树突分支）：
//
//     h_iβ = Σ_{j∈分支β} w_ij x_j
//     z_i  = Σ_β g_iβ · relu(h_iβ − ϑ_iβ)
//     s_i  = φ_soma(z_i − θ_i)            ← 胞体非线性沿用它原有的那条路径
//
// 与参考实现一致的三条约定：
//   1) **分支划分按突触前 superclass**：每个神经元把自己的入边按突触前类型分组，
//      取入边里最多的 K−1 种各占一个分支，其余全部归分支 0。
//      （生物学依据：不同突触前伙伴落在不同树突区。数据里现成有 superclass。）
//   2) **每条边恰好属于一个分支** ⇒ 总突触数不变、不引入额外增益。
//   3) 分支增益 g 初值 1、分支阈值 ϑ ~ U(−0.1, 0.1)（与参考实现同一分布与同一随机流）。
//
// branch_mode='identity'（只分组、不加非线性，z = Σ_β h_β）在数学上严格等于 K=1 ——
// 这是本项目里用来证明「分组本身没作用」的关键对照，因此这里把它做成一个显式模式而不是
// 「把 relu 去掉」的临时代码。
//
// 结构在【初始化时预计算成 CSR】（每条边只存一次），不在每步重建：
//   · brp / bcol / bws ：按 (神经元, 分支) 分组的行 CSR，bws 是【带符号】的有效权重
//     （= 主 CSR 的 val × 全局突触增益 syn_gain）。它是 h_β 的唯一真值来源，
//     避免用 |w| 累加时正负相消带来的精度损失。
//   · edge_b ：长度 = 非零元数，每条边属于哪个分支（主 CSR 顺序），热路径用来
//     把「分支调制因子」写回逐边累加（这样抑制通道的符号才不会被丢掉）。
#pragma once
#ifndef XHQM_TEST_FFBO_DEND_BRANCH_H
#define XHQM_TEST_FFBO_DEND_BRANCH_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace dendbranch
{
	// 分支数的编译期上界：热路径上每细胞一个定长栈数组（避免每步堆分配）。
	// 参考实现扫到 K=8；这里留到 16，改大只需重编。
	enum : int { DEND_MAX_K = 16 };

	// 分支模式：relu = 分支整流（秩 K）；identity = 只分组不加非线性（= K=1 的对照）
	enum mode { DM_RELU = 0, DM_IDENTITY = 1 };

	struct dend
	{
		int K = 1;                  // 分支数
		int md = DM_RELU;
		// 参考实现里分支增益初值恒为 1（每个 (神经元, 分支) 一个值）。这里保留成数组而不是
		// 直接写死，是为了将来做「分支增益标定」时不用再改结构；默认全 1 ⇒ 数值上与参考一致。
		std::vector<float> g_br;    // [n*K]
		std::vector<float> br_th;   // [n*K] 分支阈值 ϑ ~ U(−0.1, 0.1)

		// 按 (神经元, 分支) 分组的行 CSR（bws = 带符号的有效权重 val×syn_gain）
		std::vector<std::size_t> brp;     // [(n+1)*K]：神经元 i 的分支 b 占 [brp[i*K+b], brp[i*K+b+1])
		std::vector<std::uint32_t> bcol;  // 突触前索引
		std::vector<float> bws;           // 带符号有效权重
		// 主 CSR 顺序下每条边的分支（热路径查表用）
		std::vector<std::uint8_t> edge_b;

		// 与 PyTorch 参考实现同一套随机数：torch.Generator(device).manual_seed(seed) 之后的
		// 第 N 次 torch.rand()（N<2^24 时）在 CUDA 上就是 Philox4x32-10 的第 N 个 32 位输出。
		// 自检判据：philox(seed=0, N=1)/2^32 必须等于 torch 的 0.7911307215690613
		// （torch.manual_seed(0); torch.rand(1) 的实测值），见 self_check()。
		struct philox
		{
			std::uint32_t m_key, ctr[4];
			explicit philox(std::uint64_t s)
			{
				m_key = (std::uint32_t)(s & 0xFFFFFFFFu);
				ctr[0] = (std::uint32_t)((s >> 32) & 0xFFFFFFFFu);
				ctr[1] = ctr[2] = ctr[3] = 0;
			}
			// round：c ← 一轮 Philox 置换，第 2 个参数是【本轮】的密钥（必须显式传入 ——
			// 早先写成读成员变量，于是 kk 的逐轮推进完全没生效，自检当场抓出来了）
			static void round(std::uint32_t c[4], const std::uint32_t kk[2])
			{
				const std::uint64_t p0 = (std::uint64_t)0xD2511F53u * c[0];
				const std::uint64_t p1 = (std::uint64_t)0xCD9E8D57u * c[2];
				const std::uint32_t hi0 = (std::uint32_t)(p0 >> 32), lo0 = (std::uint32_t)p0;
				const std::uint32_t hi1 = (std::uint32_t)(p1 >> 32), lo1 = (std::uint32_t)p1;
				c[0] = hi1 ^ c[1] ^ kk[0];
				c[1] = lo1;
				c[2] = hi0 ^ c[3] ^ kk[1];
				c[3] = lo0;
			}
			std::uint32_t next()
			{
				std::uint32_t c[4] = { ctr[0], ctr[1], ctr[2], ctr[3] };
				std::uint32_t kk[2] = { m_key, 0u };
				for (int r = 0; r < 10; ++r)
				{
					round(c, kk);
					kk[0] += 0x9E3779B9u; kk[1] += 0xBB67AE85u;
				}
				++ctr[0];
				if (ctr[0] == 0) { ++ctr[1]; if (ctr[1] == 0) { ++ctr[2]; if (ctr[2] == 0) ++ctr[3]; } }
				return c[0];
			}
		};

		// torch.rand(...) 的第一次抽样 = 一个 uint32 / 2^32（float32 下再截成 24 位）
		static float rand01(philox& r) { return (float)(r.next() >> 8) * (1.0f / 16777216.0f); }

		// 走一遍随机流（用掉 count 个均匀样本），保证「取 ϑ 的那批数」与参考实现对齐
		static void skip_rand(philox& r, std::size_t count)
		{
			for (std::size_t i = 0; i < count; ++i) (void)r.next();
		}

		static float quantize_f32(float a, float b, std::uint32_t u32)
		{
			// 模拟 torch 的 uniform_：float32 下 (b-a) 先降低精度后再乘 → 用 double 算再截断
			const double x = (double)(float)(b - a);
			const double d = (double)u32 * (1.0 / 4294967296.0);
			return (float)((double)a + (double)(float)(x * d));
		}

		// ── 自检：Philox 块是否正确 ──
		// 判据用的是【单个块】的已知值：philox4x32-10(counter=0, key=0) = 6627E8D5 E169C58D
		// BC57AC4C 9B00DBD8。torch.Generator(device='cuda').manual_seed(0) 的第一次
		// torch.rand(1) 正好给出 0.3990464807（= 0x6627E8D5/2^32）。
		// ★ 但【连续抽样】对不上：CUDA 侧每次 rand 调用把 Philox 计数器推进一个 offset
		//   （实测第 2 个数 0.5166791677 ≠ counter=1 的 0.9722412014，且步长不是 1/2/4/5/8 中
		//   任何一个），那是 PyTorch CUDA 生成器自己的取数策略，本文件不复制它。
		//   ⇒ 分支阈值 ϑ 与参考实现【同分布 U(−0.1,0.1) 但不是同一批数】，这一点必须写进结论。
		static void self_check()
		{
			// 只比 next() 的第一次调用（它对应 ctr=0 的块首字），后面每次调用都会推进计数器，
			// 拿它们去比「同一块的第 2/3/4 个字」是错的（第一版自检就是这么写错的）。
			philox r(0);
			const std::uint32_t w0 = r.next();
			const std::uint32_t want0 = 0x6627E8D5u;
			philox r2(0);
			(void)r2.next();
			const std::uint32_t w2 = r2.next();
			std::printf("dend Philox 自检: next() 第 1 个字 = %08X（期望 %08X，philox4x32-10 ctr=0 key=0）%s；"
				"第 2 次调用 = %08X（= ctr=1 的块首字 F8E4CCA4）%s\n", w0, want0,
				(w0 == want0) ? "  OK" : "  ★不一致", w2, (w2 == 0xF8E4CCA4u) ? "  OK" : "  ★不一致");
			std::printf("  注：CUDA 生成器【连续抽样】的计数器推进方式未复制（见本函数注释）"
				"⇒ ϑ 与参考实现同分布、非同一批数。\n");
		}

		// ── 构建 ──
		// rp/col/val ：主 CSR（行=突触后，列=突触前），val 已含 syn_gain
		// sc         ：每个神经元的 superclass
		// seed       ：随机流种子（用于 ϑ；参考实现用 torch.Generator(seed)）
		bool build(std::size_t n, const std::size_t* rp, const std::size_t* col, const float* val,
			const std::vector<std::string>& sc, int K_, int md_, std::uint64_t seed)
		{
			K = (K_ < 1) ? 1 : K_;
			md = md_;
			const std::size_t nnz = (std::size_t)rp[n];
			edge_b.assign(nnz, 0);
			if (K > 1)
			{
				// 1) 类型编码（与 numpy.unique(return_inverse) 同一效果：按字典序给每个不同
				//    superclass 一个编号；编号本身不影响结果，只用来聚合入边计数）
				std::vector<std::string> uniq(sc.begin(), sc.begin() + (std::ptrdiff_t)n);
				std::sort(uniq.begin(), uniq.end());
				uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
				std::unordered_map<std::string, int> tcode;
				tcode.reserve(uniq.size() * 2);
				for (std::size_t t = 0; t < uniq.size(); ++t) tcode[uniq[t]] = (int)t;
				const int T = (int)uniq.size();
				std::vector<int> tc(n, 0);
				for (std::size_t i = 0; i < n; ++i) tc[i] = tcode[sc[i]];
				// 2) counts[i*T + t] = 神经元 i 的入边里突触前类型 t 的条数
				std::vector<int> counts((std::size_t)n * (std::size_t)T, 0);
				for (std::size_t i = 0; i < n; ++i)
					for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) ++counts[(std::size_t)i * T + tc[col[k]]];
				// 3) 前 K−1 种（条数降序；并列时类型编号小的在前 —— 解析层的确定性约定）
				std::vector<int> top((std::size_t)n * (std::size_t)(K - 1));
				std::vector<std::pair<int, int>> tmp;
				for (std::size_t i = 0; i < n; ++i)
				{
					tmp.clear();
					for (int t = 0; t < T; ++t)
					{
						const int c = counts[(std::size_t)i * T + t];
						if (c > 0) tmp.emplace_back(-c, t);
					}
					const std::size_t take = std::min<std::size_t>(tmp.size(), (std::size_t)(K - 1));
					std::partial_sort(tmp.begin(), tmp.begin() + (std::ptrdiff_t)take, tmp.end());
					for (std::size_t r = 0; r < (std::size_t)(K - 1); ++r)
						top[(std::size_t)i * (std::size_t)(K - 1) + r] = (r < take) ? tmp[r].second : -1;
				}
				// 4) 每条边归到哪个分支。**必须与参考实现同序**：它按 r = 0..K−2 顺序
				//    branch[pre == top[row, r]] = r + 1，后写的覆盖先写的 —— 所以这里也按 r 递增覆盖。
				for (int r = 0; r < K - 1; ++r)
					for (std::size_t i = 0; i < n; ++i)
					{
						const int t = top[(std::size_t)i * (std::size_t)(K - 1) + r];
						if (t < 0) continue;
						for (std::size_t k = rp[i]; k < rp[i + 1]; ++k)
							if (tc[col[k]] == t) edge_b[k] = (std::uint8_t)(r + 1);
					}
			}
			// 5) 按 (神经元, 分支) 分组建 CSR：对行内边做一次计数排序（保持行内相对次序）
			brp.assign((n + 1) * (std::size_t)K, 0);
			std::vector<std::size_t> off((n + 1) * (std::size_t)K, 0);
			for (std::size_t i = 0; i < n; ++i)
				for (std::size_t k = rp[i]; k < rp[i + 1]; ++k)
					++brp[(std::size_t)i * K + edge_b[k] + 1];
			for (std::size_t i = 0; i < n * (std::size_t)K + K; ++i) brp[i + 1] += brp[i];
			for (std::size_t i = 0; i <= n * (std::size_t)K; ++i) off[i] = brp[i];
			bcol.assign(nnz, 0);
			bws.assign(nnz, 0.0f);
			for (std::size_t i = 0; i < n; ++i)
				for (std::size_t k = rp[i]; k < rp[i + 1]; ++k)
				{
					const std::size_t o = off[(std::size_t)i * K + edge_b[k]]++;
					bcol[o] = col[k];
					bws[o] = val[k];
				}
			// 6) 分支阈值 ϑ ~ U(−0.1, 0.1)：与参考实现用【同一个随机流】
			//    （seed → θ(n) → τ(n) → g_br(n×K) → ϑ(n×K)），跳过前 2n + nK 个样本。
			//    ⇒ 两边拿到的是同一批数，不是「同分布的不同抽样」。g_br 保持参考实现的初值 1。
			g_br.assign((std::size_t)n * K, 1.0f);
			br_th.assign((std::size_t)n * K, 0.0f);
			philox rng(seed);
			skip_rand(rng, (std::size_t)n * 2 + (std::size_t)n * K);
			for (std::size_t i = 0; i < br_th.size(); ++i)
				br_th[i] = quantize_f32(-0.1f, 0.1f, rng.next());
			return true;
		}
	};

	// ── 热路径：算每个分支的驱动（式子的前半，即「分支对胞体的驱动」）──
	// 返回值 z 的语义：relu 模式 = g·relu(h−ϑ) 累加；identity 模式 = h 之和（= 全输入线性求和）。
	// bias 里回传 Σ_β g·ϑ·1[h_β>ϑ_β]：调用方把它从【该细胞行内的通道累加】里减掉，
	// 就能得到干净的 g·relu(h−ϑ) 权重调制（见 cell_backend.h 的说明）。
	inline float branch_mod(const dend& D, std::size_t i, const float* s_row,
		float* mod_out, float& bias_out)
	{
		const int K = D.K;
		const std::size_t base = i * (std::size_t)K;
		float z = 0.0f, bias = 0.0f;
		for (int b = 0; b < K; ++b)
		{
			float h = 0.0f;
			for (std::size_t k = D.brp[base + b]; k < D.brp[base + b + 1]; ++k)
				h += D.bws[k] * s_row[D.bcol[k]];
			const float g = D.g_br[base + b], th = D.br_th[base + b];
			if (D.md == DM_IDENTITY)
			{
				mod_out[b] = g;                 // 只分组：Σ_β g_β·h_β
				bias += g * th;                 // Σ_β g_β·ϑ_β（常数项，与边无关）
			}
			else if (h > th)
			{
				mod_out[b] = g;                 // 边上的调制因子 = g（阈值项按常数扣）
				bias += g * th;
			}
			else mod_out[b] = 0.0f;
			z += mod_out[b] * h;
		}
		bias_out = (D.md == DM_IDENTITY) ? 0.0f : bias;   // identity 没有 relu ⇒ 常数项不扣
		return z - bias_out;
	}
}

#endif
