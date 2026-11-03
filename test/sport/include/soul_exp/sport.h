#ifndef SOUL_EXP_SPORT
#define SOUL_EXP_SPORT

// motion-gen（sport）C++ 移植：音频 -> SMPL-X 动作 + 逐帧情感脉冲。
//   音频 -> wav2vec2 滑窗特征 -> AR Transformer 生成器（帧级 5 token，流式 KV 缓存）
//   -> VQ-VAE 分词器 decode -> 反归一化。
// 依赖 wav2vec2.h（音频特征）、nn.h（gelu/layer_norm/linear）。
// 生成器的线性投影与注意力均走 tensor_base::matmul（BLAS/SIMD/GPU 后端）。

#include "wav2vec2.h"
#include "nn.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace soul_exp {

	// ================= 部位拆分常量（对齐 sport/vqvae.py） =================
	inline std::vector<int> _j3(const std::vector<int>& joints)
	{
		std::vector<int> out; out.reserve(joints.size() * 3);
		for (int j : joints) { out.push_back(3 * j); out.push_back(3 * j + 1); out.push_back(3 * j + 2); }
		return out;
	}
	inline const std::vector<int>& mouth_b()  { static auto v = _j3({ 22 }); return v; }
	inline const std::vector<int>& face_b()   { static auto v = _j3({ 23, 24 }); return v; }
	inline const std::vector<int>& hands_b()  { static auto v = _j3([] { std::vector<int> j; for (int i = 25; i < 55; ++i) j.push_back(i); return j; }()); return v; }
	inline const std::vector<int>& neckarms_b() { static auto v = _j3({ 12,15,13,14,16,17,18,19,20,21 }); return v; }
	inline const std::vector<int>& bodylegs_b() { static auto v = _j3({ 0,1,2,3,6,9,4,5,7,8,10,11 }); return v; }
	inline const std::vector<int>& mouth_expr() { static auto v = std::vector<int>{ 0,1,2,3,5,12,13,68,95 }; return v; }
	inline const std::vector<int>& face_expr()  { static auto v = [] { std::vector<int> r; auto me = mouth_expr(); for (int i = 0; i < 100; ++i) if (std::find(me.begin(), me.end(), i) == me.end()) r.push_back(i); return r; }(); return v; }
	inline const std::vector<int>& tv_idx()     { static auto v = std::vector<int>{ 165,166,167 }; return v; }

	constexpr int N_PART = 5, CODE = 512, HID = 512, BODY_IN = 168, FACE_IN = 100;
	constexpr int N_EMO = 5;
	constexpr int D = 512, N_LAYER = 6, N_HEAD = 8, HD = 64, FFN = 2048;
	constexpr int MAX_POS = 8192, MAX_AUDIO_POS = 4096;
	constexpr double AUDIO_LA = 0.2, SR = 16000.0, EPS = 1e-5;

	template<class type> inline type swish(type x) { return x / (type(1) + type(std::exp(-x))); }

	// GroupNorm(groups, C)：按组归一化（每组 C/groups 通道 × T），biased var，逐通道仿射。
	template<class type>
	inline xhqm::tensor<3, type> group_norm(const xhqm::tensor<3, type>& x,
		const xhqm::tensor<1, type>& weight, const xhqm::tensor<1, type>& bias, int groups, type eps)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), L = x.dimension(2);
		const xhqm::size cpg = C / groups;
		xhqm::tensor<3, type> y(std::array<xhqm::size, 3>{ N, C, L }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
			for (xhqm::size g = 0; g < (xhqm::size)groups; ++g)
			{
				const xhqm::size c0 = g * cpg;
				type mean = 0;
				for (xhqm::size c = 0; c < cpg; ++c) for (xhqm::size l = 0; l < L; ++l) mean += x(n, c0 + c, l);
				mean /= type(cpg * L);
				type var = 0;
				for (xhqm::size c = 0; c < cpg; ++c) for (xhqm::size l = 0; l < L; ++l) { type d = x(n, c0 + c, l) - mean; var += d * d; }
				var /= type(cpg * L);
				const type rstd = type(1) / type(std::sqrt(var + eps));
				for (xhqm::size c = 0; c < cpg; ++c)
					for (xhqm::size l = 0; l < L; ++l)
						y(n, c0 + c, l) = (x(n, c0 + c, l) - mean) * rstd * weight[c0 + c] + bias[c0 + c];
			}
		return y;
	}

	// ================= VQ-VAE 分词器（decode） =================
	template<class type = xhqm::f32>
	struct motion_tokenizer
	{
		std::array<xhqm::tensor<2, type>, N_PART> codebooks;   // (512, part_dim)
		xhqm::tensor<3, type> dec_in_w, dec_out_w;             // (512,268,1)/(268,512,1)
		xhqm::tensor<1, type> dec_in_b, dec_out_b;
		struct resblock { xhqm::tensor<3, type> c1_w, c2_w; xhqm::tensor<1, type> c1_b, c2_b, gn_w, gn_b; xhqm::size dilation = 1; };
		std::vector<resblock> dec_blocks;

		void load(const std::string& dir)
		{
			const weight_table wt = load_weights(dir);
			for (int i = 0; i < N_PART; ++i) codebooks[i] = load_tensor<2, type>(wt, "vqs." + std::to_string(i) + ".emb");
			dec_in_w = load_tensor<3, type>(wt, "dec_in.weight");  dec_in_b = load_tensor<1, type>(wt, "dec_in.bias");
			dec_out_w = load_tensor<3, type>(wt, "dec_out.weight"); dec_out_b = load_tensor<1, type>(wt, "dec_out.bias");
			const int dils[5] = { 16, 8, 4, 2, 1 };
			dec_blocks.resize(5);
			for (int i = 0; i < 5; ++i)
			{
				const std::string p = "dec_blocks." + std::to_string(i);
				resblock& b = dec_blocks[i];
				b.c1_w = load_tensor<3, type>(wt, p + ".c1.weight"); b.c1_b = load_tensor<1, type>(wt, p + ".c1.bias");
				b.c2_w = load_tensor<3, type>(wt, p + ".c2.weight"); b.c2_b = load_tensor<1, type>(wt, p + ".c2.bias");
				b.gn_w = load_tensor<1, type>(wt, p + ".norm.weight"); b.gn_b = load_tensor<1, type>(wt, p + ".norm.bias");
				b.dilation = dils[i];
			}
		}

		// tokens (T,5) -> (body (T,168), face (T,100))
		std::pair<xhqm::tensor<2, type>, xhqm::tensor<2, type>> decode(const xhqm::tensor<2, int>& tokens) const
		{
			const xhqm::size T = tokens.dimension(0);
			std::array<xhqm::tensor<3, type>, N_PART> parts;
			for (int i = 0; i < N_PART; ++i)
			{
				const xhqm::size pd = codebooks[i].dimension(1);
				xhqm::tensor<3, type> p(std::array<xhqm::size, 3>{ 1, pd, T }, type(0));
				for (xhqm::size t = 0; t < T; ++t) for (xhqm::size d = 0; d < pd; ++d) p(0, d, t) = codebooks[i](tokens(t, i), d);
				parts[i] = p;
			}
			xhqm::tensor<3, type> body(std::array<xhqm::size, 3>{ 1, BODY_IN, T }, type(0));
			xhqm::tensor<3, type> face(std::array<xhqm::size, 3>{ 1, FACE_IN, T }, type(0));
			auto place = [&](const xhqm::tensor<3, type>& part, const std::vector<int>& idx, xhqm::tensor<3, type>& dst, int off)
			{ for (xhqm::size i = 0; i < idx.size(); ++i) for (xhqm::size t = 0; t < T; ++t) dst(0, idx[i], t) = part(0, off + i, t); };
			place(parts[0], mouth_b(), body, 0);  place(parts[0], mouth_expr(), face, 3);   // mouth: 关节(0:3) + 表情(3:12)
			place(parts[1], face_b(), body, 0);   place(parts[1], face_expr(), face, 6);   // face:  关节(0:6) + 表情(6:97)
			place(parts[2], hands_b(), body, 0);
			place(parts[3], neckarms_b(), body, 0);
			place(parts[4], bodylegs_b(), body, 0); place(parts[4], tv_idx(), body, 36);

			xhqm::tensor<3, type> cat(std::array<xhqm::size, 3>{ 1, BODY_IN + FACE_IN, T }, type(0));
			for (xhqm::size c = 0; c < BODY_IN; ++c) for (xhqm::size t = 0; t < T; ++t) cat(0, c, t) = body(0, c, t);
			for (xhqm::size c = 0; c < FACE_IN; ++c) for (xhqm::size t = 0; t < T; ++t) cat(0, BODY_IN + c, t) = face(0, c, t);

			xhqm::tensor<3, type> h = xhqm::tensor_base::conv1d(cat, dec_in_w, dec_in_b, 1, 0, 1, 1);
			for (xhqm::size i = 0; i < h.size(); ++i) h[i] = swish(h[i]);
			for (const resblock& b : dec_blocks)
			{
				xhqm::tensor<3, type> r = xhqm::tensor_base::conv1d(h, b.c1_w, b.c1_b, 1, b.dilation, b.dilation, 1);
				r = group_norm(r, b.gn_w, b.gn_b, 8, type(EPS));
				for (xhqm::size i = 0; i < r.size(); ++i) r[i] = swish(r[i]);
				r = xhqm::tensor_base::conv1d(r, b.c2_w, b.c2_b, 1, 0, 1, 1);
				h = h + r;
			}
			xhqm::tensor<3, type> out = xhqm::tensor_base::conv1d(h, dec_out_w, dec_out_b, 1, 0, 1, 1);

			xhqm::tensor<2, type> bo(std::array<xhqm::size, 2>{ T, BODY_IN }, type(0));
			xhqm::tensor<2, type> fo(std::array<xhqm::size, 2>{ T, FACE_IN }, type(0));
			for (xhqm::size t = 0; t < T; ++t)
			{
				for (xhqm::size c = 0; c < BODY_IN; ++c) bo(t, c) = out(0, c, t);
				for (xhqm::size c = 0; c < FACE_IN; ++c) fo(t, c) = out(0, BODY_IN + c, t);
			}
			return { bo, fo };
		}
	};

	// ================= AR Transformer 生成器 + 流式会话 =================
	template<class type = xhqm::f32>
	struct motion_generator
	{
		xhqm::tensor<1, type> bos;
		std::array<xhqm::tensor<2, type>, N_PART> emb_parts;
		xhqm::tensor<2, type> emo_emb, pos_emb, audio_proj_t, audio_pos;
		xhqm::tensor<1, type> audio_proj_b;
		struct layer
		{
			xhqm::tensor<2, type> sa_in_t, sa_out_t, xa_q_t, xa_kv_t, xa_out_t, ff1_t, ff2_t;
			xhqm::tensor<1, type> sa_in_b, sa_out_b, xa_q_b, xa_kv_b, xa_out_b, ff1_b, ff2_b;
			xhqm::tensor<1, type> n1_w, n1_b, n2_w, n2_b, n3_w, n3_b;
		};
		std::vector<layer> layers;
		std::array<xhqm::tensor<2, type>, N_PART> head_t;
		std::array<xhqm::tensor<1, type>, N_PART> head_b;
		xhqm::tensor<1, type> n_out_w, n_out_b;
		xhqm::tensor<2, type> emo_head_t;  // (D, N_EMO) 转置后的情感输出头
		xhqm::tensor<1, type> emo_head_b;  // (N_EMO,)

		void load(const std::string& dir)
		{
			const weight_table wt = load_weights(dir);
			bos = load_tensor<1, type>(wt, "bos");
			for (int i = 0; i < N_PART; ++i) emb_parts[i] = load_tensor<2, type>(wt, "emb_parts." + std::to_string(i) + ".weight");
			emo_emb = load_tensor<2, type>(wt, "emo_emb.weight");
			pos_emb = load_tensor<2, type>(wt, "pos_emb.weight");
			audio_proj_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, "audio_proj.weight"), 0, 1);
			audio_proj_b = load_tensor<1, type>(wt, "audio_proj.bias");
			audio_pos = load_tensor<2, type>(wt, "audio_pos.weight");

			layers.resize(N_LAYER);
			for (int i = 0; i < N_LAYER; ++i)
			{
				const std::string p = "layers." + std::to_string(i);
				layer& L = layers[i];
				auto T2 = [&](const std::string& n) { return xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + n), 0, 1); };
				L.sa_in_t = T2(".sa_in.weight");  L.sa_in_b = load_tensor<1, type>(wt, p + ".sa_in.bias");
				L.sa_out_t = T2(".sa_out.weight"); L.sa_out_b = load_tensor<1, type>(wt, p + ".sa_out.bias");
				L.xa_q_t = T2(".xa_q.weight");     L.xa_q_b = load_tensor<1, type>(wt, p + ".xa_q.bias");
				L.xa_kv_t = T2(".xa_kv.weight");   L.xa_kv_b = load_tensor<1, type>(wt, p + ".xa_kv.bias");
				L.xa_out_t = T2(".xa_out.weight"); L.xa_out_b = load_tensor<1, type>(wt, p + ".xa_out.bias");
				L.ff1_t = T2(".ff.0.weight");      L.ff1_b = load_tensor<1, type>(wt, p + ".ff.0.bias");
				L.ff2_t = T2(".ff.3.weight");      L.ff2_b = load_tensor<1, type>(wt, p + ".ff.3.bias");
				L.n1_w = load_tensor<1, type>(wt, p + ".n1.weight"); L.n1_b = load_tensor<1, type>(wt, p + ".n1.bias");
				L.n2_w = load_tensor<1, type>(wt, p + ".n2.weight"); L.n2_b = load_tensor<1, type>(wt, p + ".n2.bias");
				L.n3_w = load_tensor<1, type>(wt, p + ".n3.weight"); L.n3_b = load_tensor<1, type>(wt, p + ".n3.bias");
			}
			for (int i = 0; i < N_PART; ++i)
			{
				head_t[i] = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, "heads." + std::to_string(i) + ".weight"), 0, 1);
				head_b[i] = load_tensor<1, type>(wt, "heads." + std::to_string(i) + ".bias");
			}
			n_out_w = load_tensor<1, type>(wt, "n_out.weight");
			n_out_b = load_tensor<1, type>(wt, "n_out.bias");
			emo_head_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, "emo_head.weight"), 0, 1);
			emo_head_b = load_tensor<1, type>(wt, "emo_head.bias");
		}

		// —— 流式会话（对齐 GenSession：KV 缓存增量解码，线性投影/注意力走 matmul 后端） ——
		struct session
		{
			const motion_generator* gen;
			int emo_id;
			// 每层缓存：self K/V（(T,D)）、audio K/V（(F,D)），tensor 连续内存供 matmul/BLAS
			std::vector<xhqm::tensor<2, type>> sk, sv, ak, av;
			std::vector<type> centers;      // 音频中心时刻
			std::vector<std::array<int, N_PART>> hist;   // token 历史
			std::vector<type> emo_vec;
			std::vector<std::vector<type>> emo_hist;    // 逐帧情绪 softmax 分布 (N_EMO,)

			session(const motion_generator* g, int e) : gen(g), emo_id(e)
			{
				sk.resize(N_LAYER); sv.resize(N_LAYER); ak.resize(N_LAYER); av.resize(N_LAYER);
				emo_vec.assign(D, type(0));
				for (int c = 0; c < D; ++c) emo_vec[c] = gen->emo_emb(emo_id, c);
			}

			// 线性投影 y = x·Wᵀ + b（Wᵀ 已转置 (in,out)），matmul 走 BLAS/SIMD/GPU 后端
			static xhqm::tensor<2, type> lin(const xhqm::tensor<2, type>& x,
				const xhqm::tensor<2, type>& wt, const xhqm::tensor<1, type>& b)
			{
				xhqm::tensor<2, type> y = xhqm::tensor_base::matmul(x, wt);
				const xhqm::size rows = y.dimension(0), cols = y.dimension(1);
				for (xhqm::size r = 0; r < rows; ++r)
					for (xhqm::size c = 0; c < cols; ++c) y(r, c) += b[c];
				return y;
			}

			void push_audio(const std::vector<std::vector<type>>& feats, type c0)
			{
				const xhqm::size F = feats.size();
				if (F == 0) return;
				xhqm::tensor<2, type> ft(std::array<xhqm::size, 2>{ F, 768 }, type(0));
				for (xhqm::size f = 0; f < F; ++f)
					for (int c = 0; c < 768; ++c) ft(f, c) = feats[f][c];
				xhqm::tensor<2, type> mem = lin(ft, gen->audio_proj_t, gen->audio_proj_b);   // (F,D)
				for (xhqm::size f = 0; f < F; ++f)
					for (int c = 0; c < D; ++c) mem(f, c) += gen->audio_pos((centers.size() + f) % MAX_AUDIO_POS, c);
				for (int li = 0; li < N_LAYER; ++li)
				{
					const layer& L = gen->layers[li];
					xhqm::tensor<2, type> kv = lin(mem, L.xa_kv_t, L.xa_kv_b);   // (F, 2D)
					const xhqm::size a0 = ak[li].dimension(0);
					ak[li].redimension(std::array<xhqm::size, 2>{ a0 + F, D }, type(0));
					av[li].redimension(std::array<xhqm::size, 2>{ a0 + F, D }, type(0));
					for (xhqm::size f = 0; f < F; ++f)
						for (int c = 0; c < D; ++c) { ak[li](a0 + f, c) = kv(f, c); av[li](a0 + f, c) = kv(f, D + c); }
				}
				for (xhqm::size f = 0; f < F; ++f) centers.push_back(c0 + type(f) * type(0.02));
			}

			// 单头缩放点积注意力：q (1,HD) 对 K/V (S,HD)，allowed=false 屏蔽为 -inf
			static std::vector<type> head_attn(const xhqm::tensor<2, type>& qh,
				const xhqm::tensor<2, type>& Kh, const xhqm::tensor<2, type>& Vh,
				const std::vector<char>& allowed)
			{
				const xhqm::size S = Kh.dimension(0), HD = qh.dimension(1);
				const type scale = type(1) / type(std::sqrt(type(HD)));
				xhqm::tensor<2, type> scores = xhqm::tensor_base::matmul(qh, xhqm::tensor_base::transpose(Kh, 0, 1));   // (1,S)
				bool any = false;
				for (xhqm::size j = 0; j < S; ++j)
				{
					scores(0, j) *= scale;
					if (allowed[j]) any = true; else scores(0, j) = -1e30f;
				}
				if (!any)
				{
					scores = xhqm::tensor_base::matmul(qh, xhqm::tensor_base::transpose(Kh, 0, 1));
					for (xhqm::size j = 0; j < S; ++j) scores(0, j) *= scale;
				}
				type mx = -1e30f; for (xhqm::size j = 0; j < S; ++j) mx = std::max(mx, scores(0, j));
				std::vector<type> pr(S); type s = 0;
				for (xhqm::size j = 0; j < S; ++j) { pr[j] = type(std::exp(scores(0, j) - mx)); s += pr[j]; }
				for (xhqm::size j = 0; j < S; ++j) pr[j] /= s;
				xhqm::tensor<2, type> probs(std::array<xhqm::size, 2>{ 1, S }, type(0));
				for (xhqm::size j = 0; j < S; ++j) probs(0, j) = pr[j];
				xhqm::tensor<2, type> oh = xhqm::tensor_base::matmul(probs, Vh);   // (1,HD)
				std::vector<type> out(HD, type(0));
				for (xhqm::size c = 0; c < HD; ++c) out[c] = oh(0, c);
				return out;
			}

			// 多头注意力：q (1,D) 对缓存 K/V (S,D) → (D,)
			static std::vector<type> multi_head(const xhqm::tensor<2, type>& q,
				const xhqm::tensor<2, type>& K, const xhqm::tensor<2, type>& V,
				const std::vector<char>& allowed)
			{
				const xhqm::size S = K.dimension(0);
				std::vector<type> out(D, type(0));
				for (int hh = 0; hh < N_HEAD; ++hh)
				{
					xhqm::tensor<2, type> qh = q.slice(std::array<xhqm::index, 2>{0, xhqm::index(hh * HD)}, std::array<xhqm::index, 2>{1, xhqm::index((hh + 1) * HD)});
					xhqm::tensor<2, type> Kh = K.slice(std::array<xhqm::index, 2>{0, xhqm::index(hh * HD)}, std::array<xhqm::index, 2>{xhqm::index(S), xhqm::index((hh + 1) * HD)});
					xhqm::tensor<2, type> Vh = V.slice(std::array<xhqm::index, 2>{0, xhqm::index(hh * HD)}, std::array<xhqm::index, 2>{xhqm::index(S), xhqm::index((hh + 1) * HD)});
					std::vector<type> oh = head_attn(qh, Kh, Vh, allowed);
					for (int c = 0; c < HD; ++c) out[hh * HD + c] = oh[c];
				}
				return out;
			}

			// (D,) vector → (1,D) tensor
			static xhqm::tensor<2, type> to_row(const std::vector<type>& v)
			{
				xhqm::tensor<2, type> t(std::array<xhqm::size, 2>{ 1, D }, type(0));
				for (int c = 0; c < D; ++c) t(0, c) = v[c];
				return t;
			}

			std::array<int, N_PART> step()
			{
				const xhqm::size p = hist.size();
				const type eps = type(EPS);

				// build x (1,D)：p=0 = BOS + 种子情绪；p>0 = 上一帧 token 嵌入 + 情绪 + 位置
				xhqm::tensor<2, type> x(std::array<xhqm::size, 2>{ 1, D }, type(0));
				if (p == 0) { for (int c = 0; c < D; ++c) x(0, c) = gen->bos[c] + emo_vec[c]; }
				else
				{
					for (int i = 0; i < N_PART; ++i)
						for (int c = 0; c < D; ++c) x(0, c) += gen->emb_parts[i](hist[p - 1][i], c);
					for (int c = 0; c < D; ++c) x(0, c) += emo_vec[c] + gen->pos_emb(p % MAX_POS, c);
				}

				const xhqm::size audio_count = centers.size();
				const type t_tok = type(p) / type(30);

				for (int li = 0; li < N_LAYER; ++li)
				{
					const layer& L = gen->layers[li];
					// self-attn：sa_in → q,k,v（matmul），缓存 k/v，因果注意力（缓存即因果）
					xhqm::tensor<2, type> qkv = lin(x, L.sa_in_t, L.sa_in_b);   // (1,3D)
					xhqm::tensor<2, type> q = qkv.slice(std::array<xhqm::index, 2>{0, 0}, std::array<xhqm::index, 2>{1, D});
					xhqm::tensor<2, type> k = qkv.slice(std::array<xhqm::index, 2>{0, D}, std::array<xhqm::index, 2>{1, 2 * D});
					xhqm::tensor<2, type> v = qkv.slice(std::array<xhqm::index, 2>{0, 2 * D}, std::array<xhqm::index, 2>{1, 3 * D});
					sk[li].redimension(std::array<xhqm::size, 2>{ p + 1, D }, type(0));
					sv[li].redimension(std::array<xhqm::size, 2>{ p + 1, D }, type(0));
					for (int c = 0; c < D; ++c) { sk[li](p, c) = k(0, c); sv[li](p, c) = v(0, c); }
					std::vector<char> allowed_self(sk[li].dimension(0), 1);
					std::vector<type> attn = multi_head(q, sk[li], sv[li], allowed_self);
					x = x + lin(to_row(attn), L.sa_out_t, L.sa_out_b);
					x = layer_norm(x, L.n1_w, L.n1_b, eps);

					// cross-attn：q=xa_q(x)，K/V=音频缓存，掩码 centers<=t_tok+LA
					xhqm::tensor<2, type> qq = lin(x, L.xa_q_t, L.xa_q_b);
					std::vector<char> allowed(audio_count, 0);
					for (xhqm::size r = 0; r < audio_count; ++r) if (centers[r] <= t_tok + type(AUDIO_LA)) allowed[r] = 1;
					std::vector<type> xa = multi_head(qq, ak[li], av[li], allowed);
					x = x + lin(to_row(xa), L.xa_out_t, L.xa_out_b);
					x = layer_norm(x, L.n2_w, L.n2_b, eps);

					// FFN
					xhqm::tensor<2, type> f = lin(x, L.ff1_t, L.ff1_b);
					for (xhqm::size i = 0; i < f.size(); ++i) f[i] = gelu_exact(f[i]);
					x = x + lin(f, L.ff2_t, L.ff2_b);
					x = layer_norm(x, L.n3_w, L.n3_b, eps);
				}

				// n_out（LayerNorm）
				x = layer_norm(x, gen->n_out_w, gen->n_out_b, eps);

				// emo_head（逐帧情感脉冲）：softmax + 软调制回馈到下一帧输入
				std::vector<type> emo_logits(N_EMO, type(0));
				for (int k = 0; k < N_EMO; ++k)
				{
					type l = gen->emo_head_b[k];
					for (int d = 0; d < D; ++d) l += x(0, d) * gen->emo_head_t(d, k);
					emo_logits[k] = l;
				}
				type emx = -1e30f; for (int k = 0; k < N_EMO; ++k) emx = std::max(emx, emo_logits[k]);
				std::vector<type> p_emo(N_EMO); type es = 0;
				for (int k = 0; k < N_EMO; ++k) { p_emo[k] = type(std::exp(emo_logits[k] - emx)); es += p_emo[k]; }
				for (int k = 0; k < N_EMO; ++k) p_emo[k] /= es;
				for (int c = 0; c < D; ++c)
				{
					type acc = 0;
					for (int k = 0; k < N_EMO; ++k) acc += p_emo[k] * gen->emo_emb(k, c);
					emo_vec[c] = acc;
				}
				emo_hist.push_back(p_emo);

				std::array<int, N_PART> toks;
				for (int i = 0; i < N_PART; ++i)
				{
					type best_logit = -1e30f; int best = 0;
					for (int c = 0; c < CODE; ++c)
					{
						type l = gen->head_b[i][c];
						for (int d = 0; d < D; ++d) l += x(0, d) * gen->head_t[i](d, c);
						if (l > best_logit) { best_logit = l; best = c; }
					}
					toks[i] = best;
				}
				hist.push_back(toks);
				return toks;
			}
		};
	};

	// ================= 流式音频特征器（对齐 sport/audio.py） =================
	template<class type = xhqm::f32>
	struct stream_featurizer
	{
		const wav2vec2_base<type>* w2v = nullptr;
		std::vector<type> buf;
		type received_t = type(0), emitted_upto = type(-1e9);
		static constexpr double LB = 1.0, LA = 0.2; static constexpr int STRIDE = 320, CENTER_OFF = 200;
		static type frame_center(int k) { return type(k * STRIDE + CENTER_OFF) / type(SR); }
		void reset() { buf.clear(); received_t = type(0); emitted_upto = type(-1e9); }

		std::vector<std::vector<type>> push(const std::vector<type>& chunk)
		{
			buf.insert(buf.end(), chunk.begin(), chunk.end());
			received_t = type(buf.size()) / type(SR);
			const type emit_upto = received_t - type(LA);
			if (emit_upto <= emitted_upto) return {};
			const type t0 = std::max(type(0), emitted_upto - type(LB));
			const type t1 = emit_upto + type(LA);
			const xhqm::size s0 = xhqm::size(t0 * type(SR)), s1 = xhqm::size(std::ceil(t1 * type(SR)));
			xhqm::tensor<1, type> seg(std::array<xhqm::size, 1>{ s1 - s0 }, type(0));
			for (xhqm::size i = 0; i < s1 - s0; ++i) seg[i] = buf[s0 + i];
			auto feats = w2v->forward(seg).last_hidden_state;
			const xhqm::size F = feats.dimension(0);
			std::vector<xhqm::size> sel;
			for (xhqm::size i = 0; i < F; ++i) { type c = t0 + frame_center(int(i)); if (c > emitted_upto && c <= emit_upto) sel.push_back(i); }
			std::vector<std::vector<type>> out(sel.size(), std::vector<type>(768, type(0)));
			for (xhqm::size a = 0; a < sel.size(); ++a) for (int c = 0; c < 768; ++c) out[a][c] = feats(sel[a], c);
			if (!sel.empty()) emitted_upto = t0 + frame_center(int(sel.back()));
			return out;
		}
	};

	// ================= sport 编排 =================
	template<class type = xhqm::f32>
	struct sport
	{
		wav2vec2_base<type> w2v;
		motion_tokenizer<type> tok;
		motion_generator<type> gen;
		std::vector<type> pose_std, pose_mean, tv_std, tv_mean, exp_std, exp_mean;

		void load(const std::string& root, const std::string& w2v_dir)
		{
			w2v.load(w2v_dir);
			tok.load(root + "/tokenizer");
			gen.load(root + "/generator");
		}

		struct motion_out
		{
			xhqm::tensor<2, type> poses, exps, trans;
			xhqm::tensor<2, int> tokens;
			xhqm::tensor<2, type> emo;      // (T, N_EMO) 逐帧情绪 softmax
			xhqm::tensor<1, int> emo_ids;   // (T,) argmax 标签
		};

		motion_out generate(const std::vector<type>& audio, int emo_id, type chunk_s = type(0.5))
		{
			const xhqm::size n = std::max<xhqm::size>(1, xhqm::size(std::round(audio.size() * 30.0 / 16000.0)));
			stream_featurizer<type> feat; feat.w2v = &w2v; feat.reset();
			typename motion_generator<type>::session sess(&gen, emo_id);

			const xhqm::size chunk = xhqm::size(chunk_s * 16000.0);
			type received = type(0);
			// 当前模型为流式小矩阵增量推理：CPU 后端（SIMD+BLAS）最优，GPU 传输开销无收益。
			// 显式选 CPU，避免 XHQM_DEVICE_AUTO 下默认落到 GPU。
			xhqm::tensor_base::tensor_backend_select(xhqm::hardware::device::cpu);
			auto drain = [&]() {
				while (sess.hist.size() < n && type(sess.hist.size()) / type(30) + type(AUDIO_LA) <= received + type(1e-6))
					sess.step();
			};
			for (xhqm::size i = 0; i < audio.size(); i += chunk)
			{
				std::vector<type> ck(audio.begin() + i, audio.begin() + std::min(i + chunk, (xhqm::size)audio.size()));
				auto newf = feat.push(ck);
				if (!newf.empty())
				{
					const type c0 = feat.emitted_upto - (type(newf.size() - 1) * type(0.02));
					sess.push_audio(newf, c0);
				}
				received = type(std::min(audio.size(), i + chunk)) / type(16000);
				drain();
			}
			received = type(audio.size()) / type(16000);
			while (sess.hist.size() < n) sess.step();

			xhqm::tensor<2, int> tokens(std::array<xhqm::size, 2>{ n, N_PART }, 0);
			for (xhqm::size t = 0; t < n; ++t) for (int i = 0; i < N_PART; ++i) tokens(t, i) = sess.hist[t][i];

			auto [bo, fo] = tok.decode(tokens);
			xhqm::tensor<2, type> poses(std::array<xhqm::size, 2>{ n, 165 }, type(0));
			xhqm::tensor<2, type> exps(std::array<xhqm::size, 2>{ n, 100 }, type(0));
			xhqm::tensor<2, type> trans(std::array<xhqm::size, 2>{ n, 3 }, type(0));
			for (xhqm::size t = 0; t < n; ++t)
			{
				for (int c = 0; c < 165; ++c) poses(t, c) = bo(t, c) * pose_std[c] + pose_mean[c];
				for (int c = 0; c < 100; ++c) exps(t, c) = fo(t, c) * exp_std[c] + exp_mean[c];
				for (int c = 0; c < 3; ++c)
				{
					const type tv = bo(t, 165 + c) * tv_std[c] + tv_mean[c];
					trans(t, c) = (t == 0 ? type(0) : trans(t - 1, c)) + tv;
				}
			}

			xhqm::tensor<2, type> emo(std::array<xhqm::size, 2>{ n, N_EMO }, type(0));
			xhqm::tensor<1, int> emo_ids(std::array<xhqm::size, 1>{ n }, 0);
			for (xhqm::size t = 0; t < n; ++t)
			{
				type best = -1e30f; int bid = 0;
				for (int k = 0; k < N_EMO; ++k)
				{
					emo(t, k) = sess.emo_hist[t][k];
					if (emo(t, k) > best) { best = emo(t, k); bid = k; }
				}
				emo_ids[t] = bid;
			}
			return { poses, exps, trans, tokens, emo, emo_ids };
		}
	};

}

#endif
