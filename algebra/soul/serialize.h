#ifndef XHQM_SOUL_SERIALIZE
#define XHQM_SOUL_SERIALIZE

// 序列化基类 + 带 tensor 的模板载荷。
// json 存「类型(dtype) + 大小(shape) + 原始字节序(data)」，data 走 CBOR byte string，
// 不经过 float→double 转换，紧凑且保真。
// tensor_holder 是类型擦除的可计算张量（数据 + 梯度），供 AD 引擎统一访问不同维度的节点。
#include "../tensor/tensor.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>

namespace xhqm {
namespace soul {

	// dtype 名称（配合 json 存「类型」字段；未特化类型回 unknown）
	inline const char* dtype_name(tensor_base::dtype dt)
	{
		switch (dt)
		{
		case tensor_base::dtype::f16: return "f16";
		case tensor_base::dtype::f32: return "f32";
		case tensor_base::dtype::f64: return "f64";
		case tensor_base::dtype::fld: return "fld";
		default: return "unknown";
		}
	}

	// 抽象基类：可 JSON 序列化（save/read）
	struct serializable
	{
		virtual ~serializable() = default;
		virtual nlohmann::json save_json() const = 0;
		virtual void read_json(const nlohmann::json& j) = 0;
	};

	// 类型擦除的可计算张量：数据 + 梯度 + 形状。AD 引擎经此统一访问不同维度的节点。
	struct tensor_holder : serializable
	{
		virtual ~tensor_holder() = default;
		virtual xhqm::size dim() const = 0;                 // 运行时维度数
		virtual xhqm::size size() const = 0;                // 元素个数
		virtual std::vector<xhqm::size> shape() const = 0;  // 形状
		virtual void* data() = 0;                           // 前向数据（连续内存）
		virtual const void* data() const = 0;
		virtual void* gradient() = 0;                       // 梯度（同 shape）
		virtual const void* gradient() const = 0;
		virtual void zero_grad() = 0;                       // 梯度清零
	};

	// 带 tensor 的模板载荷：持有 value + grad，实现类型擦除访问与序列化。
	// 模板参数用 ndim 而非 dim，避免与成员函数 dim() 同名遮蔽。
	template<xhqm::size ndim, class type>
	struct tensor_payload : tensor_holder
	{
		tensor<ndim, type> value;
		tensor<ndim, type> grad;

		tensor_payload() = default;
		explicit tensor_payload(const tensor<ndim, type>& v) : value(v), grad(v.shape(), type(0)) {}

		// —— 类型擦除访问 ——
		xhqm::size dim() const override { return ndim; }
		xhqm::size size() const override { return value.size(); }
		std::vector<xhqm::size> shape() const override
		{
			std::vector<xhqm::size> s(ndim);
			for (xhqm::size k = 0; k < ndim; ++k) s[k] = value.dimension(k);
			return s;
		}
		void* data() override { return value.data(); }
		const void* data() const override { return value.data(); }
		void* gradient() override { return grad.data(); }
		const void* gradient() const override { return grad.data(); }
		void zero_grad() override
		{
			for (xhqm::size i = 0; i < grad.size(); ++i) grad[i] = type(0);
		}

		// —— 序列化：存 dtype + shape + 原始字节序 ——
		nlohmann::json save_json() const override
		{
			nlohmann::json j;
			j["dim"] = ndim;
			j["dtype"] = dtype_name(tensor_base::dtype_of<type>::value);
			std::vector<xhqm::size> shape(ndim);
			for (xhqm::size k = 0; k < ndim; ++k) shape[k] = value.dimension(k);
			j["shape"] = shape;
			const xhqm::size bytes = value.size() * sizeof(type);
			std::vector<uint8_t> raw(bytes);
			if (bytes) std::memcpy(raw.data(), value.data(), bytes);
			j["data"] = raw;  // nlohmann 按 binary 存 → CBOR/MessagePack byte string
			return j;
		}

		void read_json(const nlohmann::json& j) override
		{
			if (j.contains("dtype"))
			{
				const std::string dt = j.at("dtype").get<std::string>();
				const char* expected = dtype_name(tensor_base::dtype_of<type>::value);
				if (dt != expected && dt != "unknown")
					throw xhqm::error("tensor_payload dtype 不匹配");
			}
			if (j.at("dim").get<xhqm::size>() != ndim)
				throw xhqm::error("tensor_payload dim 不匹配");

			std::vector<xhqm::size> shape = j.at("shape").get<std::vector<xhqm::size>>();
			if (shape.size() != ndim)
				throw xhqm::error("tensor_payload shape 维度不匹配");
			std::array<xhqm::size, ndim> dims{};
			for (xhqm::size k = 0; k < ndim; ++k) dims[k] = shape[k];

			value = tensor<ndim, type>(dims, type(0));
			auto raw = j.at("data").get<std::vector<uint8_t>>();
			const xhqm::size bytes = value.size() * sizeof(type);
			if (raw.size() != bytes)
				throw xhqm::error("tensor_payload 字节数不匹配");
			if (bytes) std::memcpy(value.data(), raw.data(), bytes);

			grad = tensor<ndim, type>(value.shape(), type(0));  // 梯度不持久化，重建为零
		}
	};

	// 便捷：json ↔ CBOR 二进制
	inline std::vector<uint8_t> json_to_cbor(const nlohmann::json& j)
	{
		return nlohmann::json::to_cbor(j);
	}
	inline nlohmann::json cbor_to_json(const std::vector<uint8_t>& bytes)
	{
		return nlohmann::json::from_cbor(bytes);
	}

	// 类型擦除 → 具体载荷（前置校验 dim 由调用方保证）
	template<xhqm::size ndim, class type>
	inline tensor_payload<ndim, type>* as_payload(tensor_holder* h)
	{
		return static_cast<tensor_payload<ndim, type>*>(h);
	}
	template<xhqm::size ndim, class type>
	inline const tensor_payload<ndim, type>* as_payload(const tensor_holder* h)
	{
		return static_cast<const tensor_payload<ndim, type>*>(h);
	}

}
}

#endif
