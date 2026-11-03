// sport（motion-gen）前向对照：加载权重 + stats，跑同一段音频，与 PyTorch 参考逐值比较。
// 用法：sport_test <motion_root> <w2v_dir> <ref_dir>
#include "soul_exp/sport.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace xhqm;

static std::vector<f32> read_bin(const std::string& path)
{
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f.good()) { std::printf("无法打开 %s\n", path.c_str()); std::exit(2); }
	const std::streamsize sz = f.tellg();
	f.seekg(0, std::ios::beg);
	std::vector<f32> v(sz / sizeof(f32));
	f.read(reinterpret_cast<char*>(v.data()), sz);
	return v;
}

static std::vector<int> read_bin_i32(const std::string& path)
{
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f.good()) { std::printf("无法打开 %s\n", path.c_str()); std::exit(2); }
	const std::streamsize sz = f.tellg();
	f.seekg(0, std::ios::beg);
	std::vector<int> v(sz / sizeof(int));
	f.read(reinterpret_cast<char*>(v.data()), sz);
	return v;
}

static std::vector<f32> json_vec(const nlohmann::json& j)
{
	return j.get<std::vector<f32>>();
}

template<class T>
static double rel_to_max(const T& a, const std::vector<f32>& ref)
{
	float max_abs = 0, max_ref = 0;
	for (xhqm::size i = 0; i < ref.size(); ++i)
	{
		max_abs = std::max(max_abs, std::fabs(a[i] - ref[i]));
		max_ref = std::max(max_ref, std::fabs(ref[i]));
	}
	return double(max_abs) / double(std::max(max_ref, 1e-30f));
}

int main(int argc, char** argv)
{
	const std::string root = (argc > 1) ? argv[1] : "out/motion";
	const std::string w2v_dir = (argc > 2) ? argv[2] : "out/wav2vec2-base";
	const std::string refdir = (argc > 3) ? argv[3] : "out/motion/ref";

	// stats
	nlohmann::json stats;
	{ std::ifstream f(root + "/stats.json"); f >> stats; }

	soul_exp::sport<f32> sp;
	sp.pose_mean = json_vec(stats["pose_mean"]); sp.pose_std = json_vec(stats["pose_std"]);
	sp.tv_mean = json_vec(stats["tv_mean"]);     sp.tv_std = json_vec(stats["tv_std"]);
	sp.exp_mean = json_vec(stats["exp_mean"]);   sp.exp_std = json_vec(stats["exp_std"]);

	std::printf("loading weights ...\n");
	sp.load(root, w2v_dir);

	std::printf("loading reference ...\n");
	const auto input = read_bin(refdir + "/input.bin");
	const auto ref_tokens = read_bin_i32(refdir + "/tokens.bin");
	const auto ref_poses = read_bin(refdir + "/poses.bin");
	const auto ref_exps = read_bin(refdir + "/exps.bin");
	const auto ref_trans = read_bin(refdir + "/trans.bin");
	const auto ref_emo = read_bin(refdir + "/emo_probs.bin");
	const auto ref_emo_ids = read_bin_i32(refdir + "/emo_ids.bin");

	std::vector<f32> audio(input.begin(), input.end());

	// debug: 验证流式特征器
	{
		const auto ref_feats = read_bin(refdir + "/audio_feats.bin");
		const auto ref_centers = read_bin(refdir + "/audio_centers.bin");
		soul_exp::stream_featurizer<f32> feat; feat.w2v = &sp.w2v; feat.reset();
		std::vector<f32> feats_flat, centers_flat;
		for (xhqm::size i = 0; i < audio.size(); i += 8000) {
			std::vector<f32> ck(audio.begin() + i, audio.begin() + std::min(i + 8000, (xhqm::size)audio.size()));
			auto newf = feat.push(ck);
			if (!newf.empty()) {
				f32 c0 = feat.emitted_upto - (newf.size() - 1) * 0.02f;
				for (xhqm::size f = 0; f < newf.size(); ++f) {
					for (int c = 0; c < 768; ++c) feats_flat.push_back(newf[f][c]);
					centers_flat.push_back(c0 + f * 0.02f);
				}
			}
		}
		float max_abs = 0;
		for (xhqm::size i = 0; i < feats_flat.size(); ++i) max_abs = std::max(max_abs, std::fabs(feats_flat[i] - ref_feats[i]));
		std::printf("featurizer: n=%llu (ref %llu)  max_abs=%.3e\n",
			(unsigned long long)feats_flat.size(), (unsigned long long)ref_feats.size(), max_abs);
		float max_c = 0;
		for (xhqm::size i = 0; i < centers_flat.size(); ++i) max_c = std::max(max_c, std::fabs(centers_flat[i] - ref_centers[i]));
		std::printf("centers: n=%llu (ref %llu)  max_abs=%.3e\n",
			(unsigned long long)centers_flat.size(), (unsigned long long)ref_centers.size(), max_c);
	}

	std::printf("generate ...\n");
	auto out = sp.generate(audio, 0);   // neutral

	// tokens 精确比较
	xhqm::size tok_mismatch = 0;
	for (xhqm::size i = 0; i < ref_tokens.size(); ++i)
		if (out.tokens[i] != ref_tokens[i]) ++tok_mismatch;
	std::printf("tokens: mismatch=%llu / %llu\n", (unsigned long long)tok_mismatch, (unsigned long long)ref_tokens.size());

	std::printf("rel_to_max: poses=%.3e  exps=%.3e  trans=%.3e\n",
		rel_to_max(out.poses, ref_poses), rel_to_max(out.exps, ref_exps), rel_to_max(out.trans, ref_trans));

	// 情绪对照（emo_ids 精确 + emo_probs 逐值）。
	// 平票边界容忍：C++ 与 Python 的 float32 归约顺序不同，probs 在 ~1e-3 量级差异
	// 可能翻转边距 < 5e-3 的近平票帧的 argmax（非移植缺陷，tokens 仍须位级一致）。
	xhqm::size emo_mismatch = 0, emo_tie_tolerated = 0;
	for (xhqm::size i = 0; i < ref_emo_ids.size(); ++i)
	{
		if (out.emo_ids[i] == ref_emo_ids[i]) continue;
		const int k = ref_emo_ids[i];
		double top1 = 0, top2 = 0;
		for (int j = 0; j < soul_exp::N_EMO; ++j)
		{
			const double v = ref_emo[i * soul_exp::N_EMO + j];
			if (j == k) top1 = v;
			else if (v > top2) top2 = v;
		}
		if (top1 - top2 < 5e-3) ++emo_tie_tolerated;
		else ++emo_mismatch;
	}
	std::printf("emo_ids: mismatch=%llu / %llu (near-tie tolerated=%llu)\n",
		(unsigned long long)emo_mismatch, (unsigned long long)ref_emo_ids.size(),
		(unsigned long long)emo_tie_tolerated);
	std::printf("emo_probs: rel_to_max=%.3e\n", rel_to_max(out.emo, ref_emo));

	const bool ok = tok_mismatch == 0 && emo_mismatch == 0;
	std::printf(ok ? "sport_test OK\n" : "sport_test FAIL\n");
	return ok ? 0 : 1;
}
