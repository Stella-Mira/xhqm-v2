// 视觉子系统实现：光感受器聚类成小眼 → raycast 采样亮度 → ON/OFF 时序差分 → 注入 LIF
#include "vision_system.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

// ── 小工具 ──
static Vec3v vsub(const Vec3v& a, const Vec3v& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static Vec3v vnorm(const Vec3v& a)
{
	const double L = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
	return L > 0 ? Vec3v{ a.x / L, a.y / L, a.z / L } : Vec3v{};
}

static int col_index(const std::string& header, const char* name)
{
	std::size_t p = 0;
	int idx = 0;
	while (p <= header.size())
	{
		std::size_t q = header.find(',', p);
		if (q == std::string::npos) q = header.size();
		std::string cell = header.substr(p, q - p);
		if (cell.size() >= 2 && cell.front() == '"') cell = cell.substr(1, cell.size() - 2);
		if (cell == name) return idx;
		++idx;
		p = q + 1;
	}
	return -1;
}

static void split_csv(const std::string& line, std::vector<std::string>& out)
{
	out.clear();
	std::size_t p = 0;
	bool q = false;
	std::string cur;
	while (p <= line.size() && out.size() < 16)
	{
		const char c = (p < line.size()) ? line[p] : ',';
		if (c == '"') { q = !q; ++p; continue; }
		if (c == ',' && !q) { out.push_back(cur); cur.clear(); ++p; continue; }
		cur += c;
		++p;
	}
	out.push_back(cur);
}

// ══════════════════ 导入 ══════════════════
bool vision_import(vision_system& vs, const char* vis_csv, const char* coord_csv)
{
	{
		std::ifstream in(vis_csv);
		if (!in.is_open()) { std::fprintf(stderr, "vision: cannot open %s\n", vis_csv); return false; }
		std::string line;
		std::getline(in, line);
		const int c_id = col_index(line, "root_id");
		const int c_type = col_index(line, "type");
		const int c_fam = col_index(line, "family");
		const int c_sub = col_index(line, "subsystem");
		const int c_side = col_index(line, "side");
		if (c_id < 0 || c_type < 0 || c_side < 0)
		{
			std::fprintf(stderr, "vision: missing columns in %s\n", vis_csv);
			return false;
		}
		std::vector<std::string> f;
		int need = std::max(std::max(c_id, c_type), std::max(c_side, std::max(c_fam, c_sub)));
		while (std::getline(in, line))
		{
			split_csv(line, f);
			if ((int)f.size() <= need) continue;
			vis_cell c;
			c.type = f[c_type];
			c.family = (c_fam >= 0) ? f[c_fam] : std::string();
			c.subsystem = (c_sub >= 0) ? f[c_sub] : std::string();
			c.side = f[c_side];
			vs.cell.emplace(f[c_id], std::move(c));
		}
	}
	std::printf("vision: 视觉类型表 %zu 个神经元\n", vs.cell.size());

	{
		std::ifstream in(coord_csv);
		if (!in.is_open()) { std::fprintf(stderr, "vision: cannot open %s\n", coord_csv); return false; }
		std::string line;
		std::getline(in, line);
		std::size_t n = 0;
		while (std::getline(in, line))
		{
			const std::size_t p1 = line.find(',');
			if (p1 == std::string::npos) continue;
			const std::size_t p2 = line.find(',', p1 + 1);
			std::string pos = line.substr(p1 + 1, (p2 == std::string::npos ? line.size() : p2) - p1 - 1);
			for (char& ch : pos) if (ch == '[' || ch == ']' || ch == ',') ch = ' ';
			std::istringstream ss(pos);
			Vec3v v;
			ss >> v.x >> v.y >> v.z;
			vs.coord.emplace(line.substr(0, p1), v);
			++n;
		}
		std::printf("vision: 坐标表 %zu 个神经元\n", n);
	}
	return true;
}

// ══════════════════ 建小眼 + 柱状配对 ══════════════════
// 复眼中心用「两眼中心差」定左右位置；前后/上下用实测的相对偏移（相对头部原点）
void vision_build(vision_system& vs, const std::unordered_map<std::uint64_t, xhqm::size>& fafb_id2idx)
{
	vs.eye[0].side = "left";
	vs.eye[1].side = "right";

	// ── 收集每侧 R1-6 及其坐标 ──
	std::vector<std::pair<std::string, Vec3v>> pts[2];
	for (auto& kv : vs.cell)
	{
		if (kv.second.type != "R1-6") continue;
		auto it = vs.coord.find(kv.first);
		if (it == vs.coord.end()) continue;
		if (kv.second.side == "left") pts[0].push_back({ kv.first, it->second });
		else if (kv.second.side == "right") pts[1].push_back({ kv.first, it->second });
	}

	// ── 每侧：复眼中心 + 10um 体素聚类成小眼 ──
	for (int e = 0; e < 2; ++e)
	{
		eye_model& em = vs.eye[e];
		auto& P = pts[e];
		if (P.empty()) { std::printf("vision: %s 无 R1-6 坐标\n", em.side.c_str()); continue; }

		Vec3v c{ 0, 0, 0 };
		for (auto& p : P) { c.x += p.second.x; c.y += p.second.y; c.z += p.second.z; }
		const double inv = 1.0 / (double)P.size();
		c.x *= inv; c.y *= inv; c.z *= inv;
		em.center_anat = c;

		// 体素键（10um）
		std::unordered_map<long long, std::size_t> bkey;
		std::vector<std::vector<std::size_t>> bucket;
		for (std::size_t i = 0; i < P.size(); ++i)
		{
			const long long ix = (long long)std::floor(P[i].second.x / 10000.0);
			const long long iy = (long long)std::floor(P[i].second.y / 10000.0);
			const long long iz = (long long)std::floor(P[i].second.z / 10000.0);
			const long long k = ((ix + 512) << 40) ^ ((iy + 512) << 20) ^ (iz + 512);
			auto it = bkey.find(k);
			if (it == bkey.end())
			{
				bkey.emplace(k, bucket.size());
				bucket.emplace_back();
				bucket.back().push_back(i);
			}
			else bucket[it->second].push_back(i);
		}

		em.om.reserve(bucket.size());
		for (auto& b : bucket)
		{
			Vec3v sum{ 0, 0, 0 };
			for (std::size_t i : b) { sum.x += P[i].second.x; sum.y += P[i].second.y; sum.z += P[i].second.z; }
			const double bi = 1.0 / (double)b.size();
			sum.x *= bi; sum.y *= bi; sum.z *= bi;

			ommatidium om;
			const Vec3v d = vsub(sum, c);
			// 解剖系 → MuJoCo 头部局部系：L.x=-F.y、L.y=-F.x、L.z=F.z
			om.dir_local = vnorm({ -d.y, -d.x, d.z });
			for (std::size_t i : b)
			{
				auto it = fafb_id2idx.find(std::strtoull(P[i].first.c_str(), nullptr, 10));
				if (it != fafb_id2idx.end()) om.pr.push_back(it->second);
			}
			em.om.push_back(std::move(om));
		}
		const std::size_t nom = em.om.size();
		std::printf("vision: %s 复眼 %zu 个小眼（R1-6=%zu）中心解剖系=(%.0f %.0f %.0f) nm\n",
			em.side.c_str(), nom, P.size(), c.x, c.y, c.z);
	}

	// ── 柱状配对：lamina 细胞按坐标找最近的小眼 ──
	// 用解剖系坐标做最近邻（小眼中心 = 该小眼 R1-6 簇心，需要重建）
	// 为避免重复存簇心，这里按 lamina 坐标就近搜索小眼：用格网加速
	for (int e = 0; e < 2; ++e)
	{
		eye_model& em = vs.eye[e];
		if (em.om.empty()) continue;
		// 收集 ommatidia 的解剖系簇心（与上面同样的聚类顺序）
		std::vector<Vec3v> omc;
		{
			std::unordered_map<long long, std::size_t> bkey;
			std::vector<Vec3v> acc;
			std::vector<int> cnt;
			for (auto& p : pts[e])
			{
				const long long ix = (long long)std::floor(p.second.x / 10000.0);
				const long long iy = (long long)std::floor(p.second.y / 10000.0);
				const long long iz = (long long)std::floor(p.second.z / 10000.0);
				const long long k = ((ix + 512) << 40) ^ ((iy + 512) << 20) ^ (iz + 512);
				auto it = bkey.find(k);
				if (it == bkey.end())
				{
					bkey.emplace(k, acc.size());
					acc.push_back(p.second);
					cnt.push_back(1);
				}
				else
				{
					Vec3v& s = acc[it->second];
					s.x += p.second.x; s.y += p.second.y; s.z += p.second.z;
					++cnt[it->second];
				}
			}
			omc.resize(acc.size());
			for (std::size_t i = 0; i < acc.size(); ++i)
				omc[i] = { acc[i].x / cnt[i], acc[i].y / cnt[i], acc[i].z / cnt[i] };
		}
		if (omc.size() != em.om.size())
		{
			std::printf("vision: %s 簇心数(%zu)与小眼数(%zu)不一致，跳过柱状配对\n",
				em.side.c_str(), omc.size(), em.om.size());
			continue;
		}
		// 3D 格网（20um）加速最近邻
		const double cell = 20000.0;
		std::unordered_map<long long, std::vector<std::size_t>> grid;
		auto gkey = [&](double x, double y, double z)
		{
			const long long ix = (long long)std::floor(x / cell);
			const long long iy = (long long)std::floor(y / cell);
			const long long iz = (long long)std::floor(z / cell);
			return ((ix + 512) << 40) ^ ((iy + 512) << 20) ^ (iz + 512);
		};
		for (std::size_t i = 0; i < omc.size(); ++i)
			grid[gkey(omc[i].x, omc[i].y, omc[i].z)].push_back(i);

		std::size_t paired = 0;
		for (auto& kv : vs.cell)
		{
			if (kv.second.family != "Lamina Monopolar") continue;
			if (kv.second.side != em.side) continue;
			auto ci = vs.coord.find(kv.first);
			if (ci == vs.coord.end()) continue;
			auto fi = fafb_id2idx.find(std::strtoull(kv.first.c_str(), nullptr, 10));
			if (fi == fafb_id2idx.end()) continue;
			const Vec3v& p = ci->second;

			// 在 3x3x3 格内找最近小眼；找不到再放大到 5x5x5
			std::size_t best = omc.size();
			double bd = 1e18;
			for (int rad = 1; rad <= 2 && best == omc.size(); ++rad)
			{
				const long long bx = (long long)std::floor(p.x / cell);
				const long long by = (long long)std::floor(p.y / cell);
				const long long bz = (long long)std::floor(p.z / cell);
				for (long long dx = -rad; dx <= rad; ++dx)
					for (long long dy = -rad; dy <= rad; ++dy)
						for (long long dz = -rad; dz <= rad; ++dz)
						{
							const long long k = ((bx + dx + 512) << 40) ^ ((by + dy + 512) << 20) ^ (bz + dz + 512);
							auto it = grid.find(k);
							if (it == grid.end()) continue;
							for (std::size_t oi : it->second)
							{
								const Vec3v dd = vsub(p, omc[oi]);
								const double d2 = dd.x * dd.x + dd.y * dd.y + dd.z * dd.z;
								if (d2 < bd) { bd = d2; best = oi; }
							}
						}
			}
			if (best >= omc.size()) continue;
			++paired;
			if (em.cart.size() <= best) em.cart.resize(em.om.size());
			em.cart[best].om = best;
			em.cart[best].lam.push_back(fi->second);
			const std::string& t = kv.second.type;
			em.cart[best].kind = (t == "L1" || t == "L5") ? 1 : (t == "L4") ? 2 : (t == "L3") ? 3 : 0;
		}
		std::printf("vision: %s 柱状配对 %zu 个 lamina 细胞 → %zu 个小眼\n",
			em.side.c_str(), paired, em.cart.size());
	}

	// ── R7/R8 颜色 + lamina 分组索引（备用注入路径）──
	for (auto& kv : vs.cell)
	{
		if (kv.second.type != "R7" && kv.second.type != "R8") continue;
		auto it = fafb_id2idx.find(std::strtoull(kv.first.c_str(), nullptr, 10));
		if (it != fafb_id2idx.end()) vs.color_pr.push_back(it->second);
	}
	// R1-6 按 side 分左右（side 是数据直接标注的，不依赖坐标推算）
	for (auto& kv : vs.cell)
	{
		if (kv.second.type != "R1-6") continue;
		auto it = fafb_id2idx.find(std::strtoull(kv.first.c_str(), nullptr, 10));
		if (it == fafb_id2idx.end()) continue;
		if (kv.second.side == "left") vs.pr_left.push_back(it->second);
		else if (kv.second.side == "right") vs.pr_right.push_back(it->second);
	}
	std::printf("vision: R1-6 左右分组 left=%zu right=%zu（用于嗅觉侧向编码）\n",
		vs.pr_left.size(), vs.pr_right.size());
	std::printf("vision: [dbg1] color_pr=%zu，接下来统计 lamina\n", vs.color_pr.size());
	std::fflush(stdout);
	{
		std::size_t i = 0;
		for (auto& kv : vs.cell)
		{
			++i;
			if (i % 20000 == 0) { std::printf("vision: [dbg1] lamina 扫描 %zu / %zu\n", i, vs.cell.size()); std::fflush(stdout); }
			if (kv.second.family != "Lamina Monopolar") continue;
			auto it = fafb_id2idx.find(std::strtoull(kv.first.c_str(), nullptr, 10));
			if (it == fafb_id2idx.end()) continue;
			const std::string& t = kv.second.type;
			if (t == "L1" || t == "L5") vs.lam_on.push_back(it->second);
			else if (t == "L4") vs.lam_off.push_back(it->second);
			else if (t == "L3") vs.lam_lum.push_back(it->second);
			else vs.lam_l2.push_back(it->second);
		}
	}
	std::printf("vision: [dbg2] lamina 统计完成 ON=%zu OFF=%zu\n", vs.lam_on.size(), vs.lam_off.size());
	std::fflush(stdout);
	vs.n_ommatidia = (int)(vs.eye[0].om.size() + vs.eye[1].om.size());
	std::printf("vision: 合计 %d 个小眼 | R7/R8=%zu | lamina ON=%zu OFF=%zu L3=%zu L2=%zu\n",
		vs.n_ommatidia, vs.color_pr.size(), vs.lam_on.size(), vs.lam_off.size(),
		vs.lam_lum.size(), vs.lam_l2.size());
	vs.ready = vs.n_ommatidia > 0;
}

// ══════════════════ 每帧：采样 + 注入 ══════════════════
// 亮度模型：raycast 命中点 → 世界坐标条纹函数 + 朗伯项 → 亮度
// 条纹随 time_s 平移，产生真实的光流（T4/T5 需要的时序差分由此而来）
void vision_step(vision_system& vs, mjModel* m, mjData* d, int head_body, std::vector<float>& cur)
{
	if (!vs.ready || head_body < 0) return;
	// 降频：非采样帧直接返回。注意不要清空 last_on/off——
	// 统计是「上次采样帧」的结果，清零会让日志正好采样到 0（曾因此误判 ON/OFF 恒为 0）。
	if (vs.vis_every > 1 && (vs.frame_no++ % vs.vis_every) != 0) return;

	auto clk = []()
	{
		return std::chrono::duration<double>(
			std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	};
	double t_a = clk();

	const double* hx = d->xpos + 3 * head_body;
	const double* R = d->xmat + 9 * head_body;

	auto to_world = [&](const Vec3v& v)
	{
		return Vec3v{
			R[0] * v.x + R[3] * v.y + R[6] * v.z,
			R[1] * v.x + R[4] * v.y + R[7] * v.z,
			R[2] * v.x + R[5] * v.y + R[8] * v.z };
	};

	// 复眼在头部局部系的偏移：左右 = 两眼解剖间距的一半；前后 = 头前 4mm；上下 = 0
	const double half_sep_mm = 0.5 * std::fabs(vs.eye[0].center_anat.y - vs.eye[1].center_anat.y) * 1e-6;
	const double fwd_mm = 0.004;

	// 条纹：空间周期 3mm、随时间快速平移 → 每个小眼亮度持续变化，
	// 这样时序差分（ON/OFF）才有信号。原设 8mm/1.5 变化太慢，实测 ON/OFF ≈ 0。
	const double stripe_k = 2.0 * 3.14159265358979323846 / 0.003;
	const double t_now = d->time;
	const double stripe_phase = -t_now * 12.0;

	float sum_on = 0.0f, sum_off = 0.0f, sum_lum = 0.0f;
	std::size_t n_cart_total = 0;
	double t_ray = 0, t_inj = 0, t_cart = 0;

	for (int e = 0; e < 2; ++e)
	{
		eye_model& em = vs.eye[e];
		const std::size_t n = em.om.size();
		if (n == 0) continue;

		Vec3v ol;
		ol.x = (e == 0 ? -1.0 : 1.0) * half_sep_mm;   // 局部 x = 左右
		ol.y = fwd_mm;                                // 局部 y = 前后
		ol.z = 0.0;                                   // 局部 z = 上下
		const Vec3v ow = to_world(ol);
		const double origin[3] = { hx[0] + ow.x, hx[1] + ow.y, hx[2] + ow.z };

		// 组装方向（一次 multiRay：批量投射全部小眼）
		if (em.dirs.size() != 3 * n)
		{
			em.dirs.resize(3 * n);
			em.geom.assign(n, -1);
			em.dist.assign(n, 0.0);
			em.nrm.assign(3 * n, 0.0);
			em.lum.assign(n, 0.0f);
			em.lum_prev.assign(n, 0.0f);
		}
		for (std::size_t i = 0; i < n; ++i)
		{
			const Vec3v w = to_world(em.om[i].dir_local);
			em.dirs[3 * i + 0] = w.x;
			em.dirs[3 * i + 1] = w.y;
			em.dirs[3 * i + 2] = w.z;
		}
		const double t_r0 = clk();
		mj_multiRay(m, d, origin, em.dirs.data(), nullptr, 1, head_body,
			em.geom.data(), em.dist.data(), em.nrm.data(), (int)n, 5.0);
		t_ray += clk() - t_r0;

		std::swap(em.lum, em.lum_prev);
		const double t_c0 = clk();
		for (std::size_t i = 0; i < n; ++i)
		{
			float L;
			if (em.geom[i] >= 0)
			{
				const double px = origin[0] + em.dirs[3 * i + 0] * em.dist[i];
				const double s = 0.5 + 0.5 * std::sin(stripe_k * px + stripe_phase);
				const double* nv = &em.nrm[3 * i];
				const double nd = -(nv[0] * em.dirs[3 * i] + nv[1] * em.dirs[3 * i + 1] + nv[2] * em.dirs[3 * i + 2]);
				const double lam = 0.25 + 0.75 * std::max(0.0, nd);
				L = (float)(0.15 + 0.85 * s * lam);
			}
			else L = 0.35f;   // 未命中 = 天空
			em.lum[i] = L;
		}
		t_cart += clk() - t_c0;

		// 注入光感受器（该小眼的每个 R1-6 都收到同一亮度）
		const double t_i0 = clk();
		for (std::size_t i = 0; i < n; ++i)
		{
			const float L = em.lum[i];
			for (xhqm::size idx : em.om[i].pr)
				if (idx < cur.size()) cur[idx] += vs.gain_pr * L;
		}

		// 柱状单元：ON = 正亮度变化率，OFF = 负变化率（时序差分 → 运动信号）
		for (auto& ct : em.cart)
		{
			if (ct.om >= n || ct.lam.empty()) continue;
			++n_cart_total;
			const float dL = em.lum[ct.om] - em.lum_prev[ct.om];
			const float g = (ct.kind == 3) ? vs.gain_lum : (ct.kind == 2 ? vs.gain_on * 0.8f : vs.gain_on);
			const float I = (ct.kind == 2) ? std::max(0.0f, -dL) * g : std::max(0.0f, dL) * g;
			for (xhqm::size idx : ct.lam)
			{
				if (idx >= cur.size()) continue;
				cur[idx] += (ct.kind == 3) ? (vs.gain_lum * std::max(0.0f, em.lum[ct.om])) : I;
			}
			if (ct.kind == 1) sum_on += std::max(0.0f, dL);
			if (ct.kind == 2) sum_off += std::max(0.0f, -dL);
		}
		t_inj += clk() - t_i0;
	}

	// ── 嗅觉侧向编码 → R1-6 左右不对称活动 ──
	// 为什么用光感受器做侧向接收器：ALPN/ORN 的坐标无法分左右（实测 29/656），
	// 而 R1-6 带 side 标注、可靠。气味偏左时左眼光感受器活动更强，
	// 这个不对称随后由连接组传到运动侧，成为转向指令的来源。
	// 注意：这里注入的是「外界信息本身」（气味在左/右），反应仍由神经活动决定。
	if (vs.odor_side_in != 0.0f)
	{
		const float sL = std::max(0.0f, vs.odor_side_in);    // 气味在左 → 左眼活动↑
		const float sR = std::max(0.0f, -vs.odor_side_in);   // 气味在右 → 右眼活动↑
		for (xhqm::size i : vs.pr_left) if (i < cur.size()) cur[i] += vs.odor_side_gain * sL;
		for (xhqm::size i : vs.pr_right) if (i < cur.size()) cur[i] += vs.odor_side_gain * sR;
		// 记录两侧的实际注入量（供主程序算神经侧向）
		vs.neur_side_left = vs.odor_side_gain * sL;
		vs.neur_side_right = vs.odor_side_gain * sR;
	}

	vs.t_total += clk() - t_a;
	// 记录本帧视觉活动强度（供日志显示是否真的看到变化）
	vs.t_ray += t_ray;
	vs.t_inj += t_inj;
	vs.t_cart += t_cart;
	vs.sum_lum = sum_lum;
	// 按柱数归一化：表示「平均每个柱的亮度变化幅度」，便于判断时序差分是否真的存在
	const float inv_n = (n_cart_total > 0) ? (1.0f / (float)n_cart_total) : 0.0f;
	vs.last_on = sum_on * inv_n;
	vs.last_off = sum_off * inv_n;
	vs.n_cart = n_cart_total;
}
