#include "shader.h"
#include <fstream>

namespace xhqm
{
	namespace fantasy
	{
		namespace
		{
			constexpr char g_magic[8] = { 'X', 'H', 'Q', 'M', 'S', 'H', 'D', 'R' };
			constexpr std::uint32_t g_version = 1;

			bool write_u32(std::ostream& stream, std::uint32_t value)
			{
				stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
				return stream.good();
			}

			bool write_str(std::ostream& stream, const std::string& value)
			{
				std::uint32_t size = static_cast<std::uint32_t>(value.size());
				if (!write_u32(stream, size)) return false;
				if (size) stream.write(value.data(), static_cast<std::streamsize>(size));
				return stream.good();
			}

			bool read_u32(std::istream& stream, std::uint32_t& value)
			{
				stream.read(reinterpret_cast<char*>(&value), sizeof(value));
				return stream.good();
			}

			bool read_str(std::istream& stream, std::string& value)
			{
				std::uint32_t size = 0;
				if (!read_u32(stream, size)) return false;
				value.resize(size);
				if (size) stream.read(&value[0], static_cast<std::streamsize>(size));
				return stream.good();
			}
		}

		std::uint64_t shader_hash(const std::string& text)
		{
			std::uint64_t hash = 1469598103934665603ULL; // FNV-1a offset basis
			for (unsigned char ch : text)
			{
				hash ^= static_cast<std::uint64_t>(ch);
				hash *= 1099511628211ULL; // FNV-1a prime
			}
			return hash;
		}

		std::string shader_key(const shader_source& source)
		{
			std::uint64_t a = shader_hash(source.vertex);
			std::uint64_t b = shader_hash(source.fragment);
			std::uint64_t h = a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));

			static const char* hex = "0123456789abcdef";
			std::string key(16, '0');
			for (int i = 0; i < 16; ++i)
			{
				key[static_cast<size_t>(i)] = hex[(h >> (4 * (15 - i))) & 0xf];
			}
			return key;
		}

		bool default_shader_factory::compile(const shader_source& source, fantasy::shader& out)
		{
			out.share_ptr->reset(source.vertex, source.fragment);
			return out.share_ptr->good();
		}

		bool default_shader_factory::load(const shader_binary& binary, fantasy::shader& out)
		{
			return out.share_ptr->reset(binary);
		}

		bool default_shader_factory::extract(const fantasy::shader& program, shader_binary& out)
		{
			return program.share_ptr->binary(out);
		}

		bool default_shader_factory::supports_binary(void) const
		{
			// 当前仅 OpenGL 支持 program binary 缓存；Vulkan 落地后为 SPIR-V 缓存
			return (active_backend() == backend_kind::opengl);
		}

		bool shader_cache::save_file(const shader_binary& binary, const std::string& path)
		{
			std::ofstream file(path, std::ios::binary);
			if (!file) return false;

			file.write(g_magic, sizeof(g_magic));
			if (!write_u32(file, g_version)) return false;
			if (!write_str(file, binary.backend)) return false;
			if (!write_str(file, binary.key)) return false;
			if (!write_u32(file, binary.format)) return false;
			if (!write_u32(file, static_cast<std::uint32_t>(binary.blob.size()))) return false;
			if (!binary.blob.empty())
			{
				file.write(reinterpret_cast<const char*>(binary.blob.data()), static_cast<std::streamsize>(binary.blob.size()));
			}
			return file.good();
		}

		bool shader_cache::load_file(shader_binary& binary, const std::string& path)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file) return false;

			char magic[8] = {};
			file.read(magic, sizeof(magic));
			for (int i = 0; i < 8; ++i) { if (magic[i] != g_magic[i]) return false; }

			std::uint32_t version = 0;
			if (!read_u32(file, version)) return false;
			if (version != g_version) return false;

			if (!read_str(file, binary.backend)) return false;
			if (!read_str(file, binary.key)) return false;
			if (!read_u32(file, binary.format)) return false;

			std::uint32_t blob_size = 0;
			if (!read_u32(file, blob_size)) return false;
			binary.blob.resize(blob_size);
			if (blob_size)
			{
				file.read(reinterpret_cast<char*>(binary.blob.data()), static_cast<std::streamsize>(blob_size));
			}
			return file.good();
		}

		bool shader_cache::acquire(const std::string& key, const shader_source& source, fantasy::shader& out, const std::string& cache_dir)
		{
			default_shader_factory factory;
			shader_binary binary;

			std::string use_key = key.empty() ? shader_key(source) : key;
			// cache_dir 为空时不落盘（路径不可控，会写当前工作目录）；走内存编译
			const bool cache_enabled = !cache_dir.empty();
			std::string path = cache_enabled ? (cache_dir + "/" + use_key) : use_key;

			// 1. 尝试直接加载预编译缓存（仅启用持久化缓存时）
			if (cache_enabled && load_file(binary, path)
				&& binary.backend == backend_name(active_backend())
				&& binary.key == use_key)
			{
				if (factory.load(binary, out)) return true;
			}

			// 2. 编译源码
			if (!factory.compile(source, out)) return false;

			// 3. 提取二进制并落盘（仅启用持久化缓存时）
			if (cache_enabled && factory.supports_binary() && factory.extract(out, binary))
			{
				binary.backend = backend_name(active_backend());
				binary.key = use_key;
				save_file(binary, path);
			}
			return true;
		}
	}
}
