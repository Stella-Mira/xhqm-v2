#pragma once
#ifndef XHQM_FANTASY_SHADER
#define XHQM_FANTASY_SHADER

#include "backend.h"
#include "drawer.h"
#include <cstdint>
#include <string>

namespace xhqm
{
	namespace fantasy
	{
		// 着色器源码稳定哈希（FNV-1a 64），跨运行/跨平台稳定
		std::uint64_t xhqm_io shader_hash(const std::string& text);

		// 生成缓存键（对 vertex + fragment 的稳定内容哈希）
		std::string xhqm_io shader_key(const shader_source& source);

		// 预编译着色器抽象接口
		// 任一后端的着色器编译/加载都经由此接口，屏蔽底层 API 差异：
		//   compile —— 从源码编译
		//   load    —— 从预编译二进制直接加载（不重新编译）
		//   extract —— 提取已编译二进制（用于落盘缓存）
		class xhqm_io shader_factory
		{
		public:
			virtual ~shader_factory() = default;
			virtual bool compile(const shader_source& source, fantasy::shader& out) = 0;
			virtual bool load(const shader_binary& binary, fantasy::shader& out) = 0;
			virtual bool extract(const fantasy::shader& program, shader_binary& out) = 0;
			virtual bool supports_binary(void) const = 0;
		};

		// 默认工厂：直接走当前后端的 _shader 实现
		class xhqm_io default_shader_factory : public shader_factory
		{
		public:
			virtual bool compile(const shader_source& source, fantasy::shader& out) override;
			virtual bool load(const shader_binary& binary, fantasy::shader& out) override;
			virtual bool extract(const fantasy::shader& program, shader_binary& out) override;
			virtual bool supports_binary(void) const override;
		};

		// 预编译缓存：首次编译源码并落盘二进制，之后直接加载二进制
		class xhqm_io shader_cache
		{
		public:
			// key 为空时按源码内容自动生成
			static bool acquire(const std::string& key, const shader_source& source, fantasy::shader& out, const std::string& cache_dir = "");
			static bool save_file(const shader_binary& binary, const std::string& path);
			static bool load_file(shader_binary& binary, const std::string& path);
		};
	}
}

#endif
