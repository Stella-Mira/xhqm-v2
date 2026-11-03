// wav2vec2 全前向对照：加载导出权重跑前向，与 PyTorch 参考输出逐值比较。
// 相对误差 = max|diff| / max|ref|（峰值归一，避免近零元素放大相对误差），阈值 1e-4。
// 用法：loader_test <model_dir> <ref_dir>   （model_dir 含 manifest.json+weights.bin，ref_dir 含参考 .bin）
#include "soul_exp/wav2vec2.h"
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

int main(int argc, char** argv)
{
	const std::string dir = (argc > 1) ? argv[1] : "out/wav2vec2-base";
	const std::string refdir = (argc > 2) ? argv[2] : "out/ref";

	std::printf("loading weights: %s\n", dir.c_str());
	soul_exp::wav2vec2_base<f32> model;
	model.load(dir);

	std::printf("loading reference: %s\n", refdir.c_str());
	const auto input = read_bin(refdir + "/input.bin");
	const auto ref_hidden = read_bin(refdir + "/last_hidden.bin");
	const auto ref_extract = read_bin(refdir + "/extract.bin");

	tensor<1, f32> x(std::array<xhqm::size, 1>{ input.size() }, 0.0f);
	for (xhqm::size i = 0; i < input.size(); ++i) x[i] = input[i];

	std::printf("forward ...\n");
	auto out = model.forward(x);

	assert(out.last_hidden_state.size() == ref_hidden.size());
	assert(out.extract_features.size() == ref_extract.size());

	// 峰值归一相对误差
	auto rel_to_max = [](const auto& a, const std::vector<f32>& ref) {
		float max_abs = 0, max_ref = 0;
		for (xhqm::size i = 0; i < ref.size(); ++i)
		{
			max_abs = std::max(max_abs, std::fabs(a[i] - ref[i]));
			max_ref = std::max(max_ref, std::fabs(ref[i]));
		}
		return double(max_abs) / double(std::max(max_ref, 1e-30f));
	};

	const double r_hidden = rel_to_max(out.last_hidden_state, ref_hidden);
	const double r_extract = rel_to_max(out.extract_features, ref_extract);
	std::printf("rel_to_max: last_hidden=%.3e  extract=%.3e\n", r_hidden, r_extract);

	const bool ok = r_hidden < 1e-4 && r_extract < 1e-4;
	std::printf(ok ? "loader_test OK\n" : "loader_test FAIL\n");
	return ok ? 0 : 1;
}
