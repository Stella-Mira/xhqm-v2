#ifndef SOUL_EXP_WEIGHT_LOADER
#define SOUL_EXP_WEIGHT_LOADER

// wav2vec2 权重加载器：读 export/export_wav2vec2.py 生成的 manifest.json + weights.bin，
// 按层名取 shape / 原始 f32 数据 / 重建 tensor。依赖 xhqm 的 tensor 与 json。
#include "../../../../algebra/tensor/tensor.h"
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace soul_exp {

	// 权重表：层名 → {shape, offset, nbytes}，blob 为整个 weights.bin 的 f32 视图
	struct weight_table
	{
		struct entry
		{
			std::vector<xhqm::size> shape;
			xhqm::size offset = 0;   // 字节偏移
			xhqm::size nbytes = 0;   // 字节数
		};
		std::map<std::string, entry> entries;
		std::vector<xhqm::f32> blob;

		bool has(const std::string& name) const { return entries.count(name) != 0; }

		std::vector<xhqm::size> shape(const std::string& name) const { return entries.at(name).shape; }

		xhqm::size count(const std::string& name) const
		{
			return entries.at(name).nbytes / sizeof(xhqm::f32);
		}

		const xhqm::f32* data(const std::string& name) const
		{
			return blob.data() + entries.at(name).offset / sizeof(xhqm::f32);
		}
	};

	// 从目录读 manifest.json + weights.bin
	inline weight_table load_weights(const std::string& dir)
	{
		std::ifstream mf(dir + "/manifest.json");
		if (!mf) throw xhqm::error("无法打开 manifest.json: " + dir);
		nlohmann::json manifest;
		mf >> manifest;

		weight_table wt;
		for (const auto& p : manifest.at("params"))
		{
			weight_table::entry e;
			e.shape = p.at("shape").get<std::vector<xhqm::size>>();
			e.offset = p.at("offset").get<xhqm::size>();
			e.nbytes = p.at("nbytes").get<xhqm::size>();
			wt.entries[p.at("name").get<std::string>()] = std::move(e);
		}

		std::ifstream bf(dir + "/weights.bin", std::ios::binary | std::ios::ate);
		if (!bf) throw xhqm::error("无法打开 weights.bin: " + dir);
		const std::streamsize sz = bf.tellg();
		if (sz % sizeof(xhqm::f32) != 0) throw xhqm::error("weights.bin 大小不是 f32 对齐");
		bf.seekg(0, std::ios::beg);
		wt.blob.resize(sz / sizeof(xhqm::f32));
		bf.read(reinterpret_cast<char*>(wt.blob.data()), sz);
		return wt;
	}

	// 按名重建 tensor<dim, type>（dim 由调用方给定，shape 从 manifest 校验）
	template<xhqm::size dim, class type = xhqm::f32>
	xhqm::tensor<dim, type> load_tensor(const weight_table& wt, const std::string& name)
	{
		const std::vector<xhqm::size> shp = wt.shape(name);
		if (shp.size() != dim) throw xhqm::error("weight 维度不匹配: " + name);
		std::array<xhqm::size, dim> dims{};
		for (xhqm::size i = 0; i < dim; ++i) dims[i] = shp[i];
		xhqm::tensor<dim, type> t(dims, type(0));
		const xhqm::f32* src = wt.data(name);
		for (xhqm::size i = 0; i < t.size(); ++i) t[i] = type(src[i]);
		return t;
	}

}

#endif
