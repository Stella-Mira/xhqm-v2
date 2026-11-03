// 蘑菇体多巴胺调制可塑性实现
#include "mushroom_body.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

static int col_idx(const std::string& header, const char* name)
{
	std::size_t p = 0;
	int idx = 0;
	while (p <= header.size())
	{
		std::size_t q = header.find(',', p);
		if (q == std::string::npos) q = header.size();
		std::string c = header.substr(p, q - p);
		if (c.size() >= 2 && c.front() == '"') c = c.substr(1, c.size() - 2);
		if (c == name) return idx;
		++idx;
		p = q + 1;
	}
	return -1;
}

static void split_csv(const std::string& line, std::vector<std::string>& out, std::size_t cap = 16)
{
	out.clear();
	std::size_t p = 0;
	bool q = false;
	std::string cur;
	while (p <= line.size() && out.size() < cap)
	{
		const char c = (p < line.size()) ? line[p] : ',';
		if (c == '"') { q = !q; ++p; continue; }
		if (c == ',' && !q) { out.push_back(cur); cur.clear(); ++p; continue; }
		cur += c;
		++p;
	}
	out.push_back(cur);
}

bool mb_import(mushroom_body& mb, const char* cell_types_csv, const char* classification_csv,
	const std::unordered_map<std::uint64_t, xhqm::size>& fafb_id2idx)
{
	// 1. primary_type（Kenyon / PAM / PPL / MBON 等）
	std::unordered_map<std::string, std::string> primary;
	{
		std::ifstream in(cell_types_csv);
		if (!in.is_open()) { std::fprintf(stderr, "mb: cannot open %s\n", cell_types_csv); return false; }
		std::string line;
		std::getline(in, line);
		const int c_id = col_idx(line, "root_id");
		const int c_t = col_idx(line, "primary_type");
		if (c_id < 0 || c_t < 0) { std::fprintf(stderr, "mb: missing columns\n"); return false; }
		std::vector<std::string> f;
		while (std::getline(in, line))
		{
			split_csv(line, f, 8);
			if ((int)f.size() <= std::max(c_id, c_t)) continue;
			primary.emplace(f[c_id], f[c_t]);
		}
	}

	// 2. classification 的 class（Kenyon_Cell / MBON / DAN / ALPN / olfactory）
	{
		std::ifstream in(classification_csv);
		if (!in.is_open()) { std::fprintf(stderr, "mb: cannot open %s\n", classification_csv); return false; }
		std::string line;
		std::getline(in, line);
		const int c_id = col_idx(line, "root_id");
		const int c_cl = col_idx(line, "class");
		if (c_id < 0 || c_cl < 0) { std::fprintf(stderr, "mb: missing class column\n"); return false; }
		std::vector<std::string> f;
		while (std::getline(in, line))
		{
			split_csv(line, f, 10);
			if ((int)f.size() <= std::max(c_id, c_cl)) continue;
			auto it = fafb_id2idx.find(std::strtoull(f[c_id].c_str(), nullptr, 10));
			if (it == fafb_id2idx.end()) continue;
			const xhqm::size idx = it->second;
			const std::string& cl = f[c_cl];
			auto pt = primary.find(f[c_id]);
			const std::string t = (pt != primary.end()) ? pt->second : std::string();

			if (cl == "Kenyon_Cell") mb.kc.push_back(idx);
			else if (cl == "MBON") mb.mbon.push_back(idx);
			else if (cl == "DAN")
			{
				if (t.rfind("PAM", 0) == 0) mb.pam.push_back(idx);       // 奖励型
				else if (t.rfind("PPL", 0) == 0) mb.ppl.push_back(idx);  // 惩罚型
			}
			else if (cl == "ALPN") mb.alpn.push_back(idx);
			else if (cl == "olfactory")
			{
				mb.orn.push_back(idx);
				// 嗅小球通道 = primary_type 去掉 ORN_ 前缀
				std::string g = (t.rfind("ORN_", 0) == 0) ? t.substr(4) : std::string("other");
				auto gi = mb.glom_id.find(g);
				int gid;
				if (gi == mb.glom_id.end())
				{
					gid = (int)mb.glomeruli.size();
					mb.glom_id.emplace(g, gid);
					mb.glomeruli.push_back(g);
					mb.glom_orn.emplace_back();
				}
				else gid = gi->second;
				mb.glom_orn[gid].push_back(idx);
			}
		}
	}

	std::printf("蘑菇体: KC=%zu MBON=%zu PAM(奖励)=%zu PPL(惩罚)=%zu ALPN=%zu ORN=%zu（%zu 个嗅小球）\n",
		mb.kc.size(), mb.mbon.size(), mb.pam.size(), mb.ppl.size(), mb.alpn.size(),
		mb.orn.size(), mb.glomeruli.size());
	char buf[256];
	std::snprintf(buf, sizeof(buf), "KC=%zu MBON=%zu PAM=%zu PPL=%zu ORN=%zu glomeruli=%zu",
		mb.kc.size(), mb.mbon.size(), mb.pam.size(), mb.ppl.size(), mb.orn.size(), mb.glomeruli.size());
	mb.info = buf;
	mb.ready = !mb.kc.empty() && !mb.mbon.empty();
	return mb.ready;
}

void mb_build_edges(mushroom_body& mb, const std::vector<xhqm::size>& csr_rp,
	const std::vector<xhqm::size>& csr_col)
{
	if (!mb.ready) return;
	// KC / MBON 的成员标记（O(N) 查表）
	std::vector<char> is_kc(csr_rp.size(), 0), is_mbon(csr_rp.size(), 0);
	for (xhqm::size i : mb.kc) if (i < is_kc.size()) is_kc[i] = 1;
	for (xhqm::size i : mb.mbon) if (i < is_mbon.size()) is_mbon[i] = 1;

	// 反向索引：网络索引 → KC 在 mb.kc 中的位置（用于痕迹查表）
	std::unordered_map<xhqm::size, xhqm::size> kc_pos;
	kc_pos.reserve(mb.kc.size() * 2);
	for (xhqm::size i = 0; i < mb.kc.size(); ++i) kc_pos.emplace(mb.kc[i], i);
	mb.kc_out.assign(mb.kc.size(), {});

	mb.edges.clear();
	mb.edges.reserve(40000);
	for (xhqm::size pre : mb.kc)
	{
		if (pre + 1 >= csr_rp.size()) continue;
		auto kp = kc_pos.find(pre);
		if (kp == kc_pos.end()) continue;
		const xhqm::size kpos = kp->second;
		for (xhqm::size k = csr_rp[pre]; k < csr_rp[pre + 1]; ++k)
		{
			const xhqm::size post = csr_col[k];
			if (post >= is_mbon.size() || !is_mbon[post]) continue;
			plastic_edge e;
			e.k = k;
			e.pre = kpos;
			mb.edges.push_back(e);
			mb.kc_out[kpos].push_back(mb.edges.size() - 1);
		}
	}
	mb.n_edges = mb.edges.size();
	std::printf("蘑菇体可塑突触: KC→MBON %zu 条（在脑网络 CSR 内）\n", mb.n_edges);
}

void mb_step(mushroom_body& mb, std::vector<float>& csr_val, const std::vector<float>& spike,
	float dt, float reward, float punish)
{
	if (!mb.ready || mb.edges.empty()) return;

	// 1) 更新 eligibility 痕迹：KC 发放时置 1，其余时刻指数衰减
	const float decay = std::exp(-dt / std::max(mb.tau_trace, 1e-3f));
	for (xhqm::size i = 0; i < mb.kc.size(); ++i)
	{
		const xhqm::size idx = mb.kc[i];
		const bool fired = (idx < spike.size() && spike[idx] > 0.5f);
		for (xhqm::size ei : mb.kc_out[i])
		{
			float& tr = mb.edges[ei].trace;
			tr *= decay;
			if (fired) tr = 1.0f;
		}
	}

	// 2) 三因子更新：痕迹 × 多巴胺 → 改权重
	//    奖励（PAM）增强 KC→MBON；惩罚（PPL）减弱
	// 乘性可塑：w = base * (1 + plastic)。这样相对变化清晰、也天然不会反号。
	const float da = reward - punish;
	for (std::size_t i = 0; i < mb.edges.size(); ++i)
	{
		plastic_edge& e = mb.edges[i];
		float& pl = mb.plastic[i];
		if (e.trace > 1e-4f && std::fabs(da) > 1e-6f)
		{
			const float before = pl;
			pl += mb.lr * da * e.trace;
			if (pl < mb.w_min) pl = mb.w_min;
			if (pl > mb.w_max) pl = mb.w_max;
			mb.w_delta_total += (double)(pl - before) * (double)e.base;
			++mb.n_updates;
		}
		// 同源性衰减：可塑量缓慢回零
		if (mb.w_decay > 0.0f) pl += (0.0f - pl) * mb.w_decay * dt;
		// 写回 CSR 权重（LIF 下一步就用它）
		if (e.k < csr_val.size()) csr_val[e.k] = e.base * (1.0f + pl);
	}

	// 4) MBON 读出（趋避指标）：MBON 的当前发放率
	float sum = 0.0f;
	for (xhqm::size i : mb.mbon) if (i < spike.size()) sum += spike[i];
	mb.mbon_readout.push_back(sum / (float)std::max<std::size_t>(mb.mbon.size(), 1));
	if (mb.mbon_readout.size() > 4096) mb.mbon_readout.erase(mb.mbon_readout.begin());
}
