// bench_speed：测 sport 流式推理速度（featurizer / 生成器 step / decode 分阶段计时，RTF）。
// 用法：bench_speed <motion_root> <w2v_dir> <秒数>
#include "soul_exp/sport.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace xhqm;
using Clk = std::chrono::steady_clock;
static double ms(Clk::time_point a, Clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

static std::vector<f32> json_vec(const nlohmann::json& j) { return j.get<std::vector<f32>>(); }

int main(int argc, char** argv)
{
	const std::string root = (argc > 1) ? argv[1] : "out/motion";
	const std::string w2v_dir = (argc > 2) ? argv[2] : "out/wav2vec2-base";
	const double sec = (argc > 3) ? std::atof(argv[3]) : 2.0;

	auto t0 = Clk::now();
	nlohmann::json stats; { std::ifstream f(root + "/stats.json"); f >> stats; }
	soul_exp::sport<f32> sp;
	sp.pose_mean = json_vec(stats["pose_mean"]); sp.pose_std = json_vec(stats["pose_std"]);
	sp.tv_mean = json_vec(stats["tv_mean"]);     sp.tv_std = json_vec(stats["tv_std"]);
	sp.exp_mean = json_vec(stats["exp_mean"]);   sp.exp_std = json_vec(stats["exp_std"]);
	sp.load(root, w2v_dir);
	auto t1 = Clk::now();

	// 合成 sec 秒伪语音（16k 单声道正弦混合，计算量与真实语音一致）
	std::vector<f32> audio((xhqm::size)(sec * 16000));
	for (xhqm::size i = 0; i < audio.size(); ++i)
		audio[i] = 0.1f * std::sin(2 * 3.1415926f * 220.0f * i / 16000.0f)
		         + 0.1f * std::sin(2 * 3.1415926f * 440.0f * i / 16000.0f);

	const xhqm::size n = std::max<xhqm::size>(1, xhqm::size(std::round(audio.size() * 30.0 / 16000.0)));
	soul_exp::stream_featurizer<f32> feat; feat.w2v = &sp.w2v; feat.reset();
	typename soul_exp::motion_generator<f32>::session sess(&sp.gen, 0);

	const xhqm::size chunk = xhqm::size(0.5 * 16000.0);
	f32 received = 0;
	double feat_ms = 0, push_ms = 0, step_ms = 0;
	int n_steps = 0, n_feat_chunks = 0;
	auto t_gen0 = Clk::now();

	auto drain = [&]() {
		while (sess.hist.size() < n && f32(sess.hist.size()) / 30.0f + f32(soul_exp::AUDIO_LA) <= received + 1e-6f)
		{
			auto a = Clk::now(); sess.step(); auto b = Clk::now();
			step_ms += ms(a, b); ++n_steps;
		}
	};

	for (xhqm::size i = 0; i < audio.size(); i += chunk)
	{
		std::vector<f32> ck(audio.begin() + i, audio.begin() + std::min(i + chunk, (xhqm::size)audio.size()));
		auto a = Clk::now();
		auto newf = feat.push(ck);
		auto b = Clk::now();
		feat_ms += ms(a, b); ++n_feat_chunks;
		if (!newf.empty())
		{
			const f32 c0 = feat.emitted_upto - f32(newf.size() - 1) * 0.02f;
			auto c = Clk::now(); sess.push_audio(newf, c0); auto d = Clk::now();
			push_ms += ms(c, d);
		}
		received = f32(std::min(audio.size(), i + chunk)) / 16000.0f;
		drain();
	}
	received = f32(audio.size()) / 16000.0f;
	while (sess.hist.size() < n) { auto a = Clk::now(); sess.step(); auto b = Clk::now(); step_ms += ms(a, b); ++n_steps; }

	// decode
	auto t_d0 = Clk::now();
	xhqm::tensor<2, int> tokens(std::array<xhqm::size, 2>{ n, soul_exp::N_PART }, 0);
	for (xhqm::size t = 0; t < n; ++t) for (int i = 0; i < soul_exp::N_PART; ++i) tokens(t, i) = sess.hist[t][i];
	auto [bo, fo] = sp.tok.decode(tokens);
	auto t_d1 = Clk::now();
	auto t_gen1 = Clk::now();

	std::printf("audio: %.1fs  frames: %llu\n", sec, (unsigned long long)n);
	std::printf("load:      %8.0f ms\n", ms(t0, t1));
	std::printf("featurizer:%8.0f ms  (%d chunks, 每chunk %.1f ms)\n", feat_ms, n_feat_chunks, n_feat_chunks ? feat_ms / n_feat_chunks : 0.0);
	std::printf("push_audio:%8.0f ms\n", push_ms);
	std::printf("generator: %8.0f ms  (%d steps, 每帧 %.2f ms)\n", step_ms, n_steps, n_steps ? step_ms / n_steps : 0.0);
	std::printf("decode:    %8.0f ms\n", ms(t_d0, t_d1));
	std::printf("generate total: %8.0f ms\n", ms(t_gen0, t_gen1));
	std::printf("RTF = %.3f  (%s)\n", ms(t_gen0, t_gen1) / (sec * 1000.0),
		(ms(t_gen0, t_gen1) / (sec * 1000.0) < 1.0) ? "实时" : "超实时");
	return 0;
}
