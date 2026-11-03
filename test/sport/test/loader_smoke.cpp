// loader 冒烟测试：合成 manifest.json + weights.bin，加载后校验 shape/值。
#include "soul_exp/weight_loader.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace xhqm;

static void write_file(const std::string& path, const std::string& content)
{
	std::ofstream f(path, std::ios::binary);
	f << content;
}

int main()
{
	const std::string dir = "smoke_tmp";
	std::filesystem::create_directories(dir);

	// 合成 manifest.json：两个参数，a.weight(2x3) + b.bias(2)
	const std::string manifest =
		"{\"model\":\"synthetic\",\"dtype\":\"f32\",\"params\":["
		"{\"name\":\"a.weight\",\"shape\":[2,3],\"offset\":0,\"nbytes\":24},"
		"{\"name\":\"b.bias\",\"shape\":[2],\"offset\":24,\"nbytes\":8}"
		"]}";
	write_file(dir + "/manifest.json", manifest);

	// 合成 weights.bin：f32 [1,2,3,4,5,6, 10,20]
	const f32 raw[] = { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 10.f, 20.f };
	{
		std::ofstream bf(dir + "/weights.bin", std::ios::binary);
		bf.write(reinterpret_cast<const char*>(raw), sizeof(raw));
		bf.close();
	}

	// 加载并校验
	soul_exp::weight_table wt = soul_exp::load_weights(dir);
	assert(wt.has("a.weight") && wt.has("b.bias"));

	auto w = soul_exp::load_tensor<2, f32>(wt, "a.weight");
	assert(w.dimension(0) == 2 && w.dimension(1) == 3);
	assert(w(0, 0) == 1.f && w(0, 2) == 3.f && w(1, 0) == 4.f && w(1, 2) == 6.f);

	auto b = soul_exp::load_tensor<1, f32>(wt, "b.bias");
	assert(b.dimension(0) == 2);
	assert(b[0] == 10.f && b[1] == 20.f);

	std::printf("loader_smoke OK\n");
	return 0;
}
