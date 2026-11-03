#include "uuid.h"
#include "md5.h"

#include <chrono>
#include <random>

namespace xhqm
{
	namespace cipher
	{
		bool uuid::uuid_equal(const cipher::uuid& left, const cipher::uuid& right)
		{
			for (xhqm::size i = 0; i < 16; ++i)
			{
				if (left(i) != right(i)) return false;
			}
			return true;
		}

		cipher::text uuid::to_text(const cipher::uuid& uuid_byte)
		{
			// 十六进制转换表
			static constexpr char hex_digits[] = "0123456789abcdef";

			cipher::text uuid_text;
			uuid_text.reserve(36);  // 预先分配空间

			for (xhqm::size i = 0; i < uuid_byte.m_data.size(); ++i)
			{
				xhqm::byte byte = uuid_byte.m_data[i];
				// 按格式插入连字符
				if (i == 4 || i == 6 || i == 8 || i == 10)
				{
					uuid_text += '-';
				}
				// 将字节转为两个十六进制字符
				uuid_text += hex_digits[byte >> 4];
				uuid_text += hex_digits[byte & 0x0F];
			}

			return uuid_text;
		}

		cipher::uuid uuid::to_uuid(const cipher::text& uuid_text)
		{
			// 定义十六进制字符到数值的映射
			auto hex_char_to_value = [](const cipher::text::value_type& c) -> xhqm::byte
			{
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
				if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
				return 0; // 非十六进制字符返回0
			};

			cipher::uuid uuid_byte;

			// 标准 UUID 连字符位于 8/13/18/23（to_text 在字节 4/6/8/10 前插入）
			if (uuid_text.length() != 36 ||
				uuid_text[8] != '-' || uuid_text[13] != '-' ||
				uuid_text[18] != '-' || uuid_text[23] != '-')
			{
				return uuid_byte; // 返回空UUID对象
			}

			// 处理UUID字符串中的每个字节
			for (xhqm::size i = 0, pos = 0; i < 16; ++i)
			{
				if (i == 4 || i == 6 || i == 8 || i == 10) { pos++; }
				if (pos + 1 < uuid_text.length())
				{
					xhqm::text::value_type char_h = uuid_text[pos++];
					xhqm::text::value_type char_l = uuid_text[pos++];
					uuid_byte.m_data[i] = (hex_char_to_value(char_h) << 4) | hex_char_to_value(char_l);
				}
			}

			return uuid_byte;
		}

		cipher::uuid uuid::creat_uuid_v1(std::string addr)
		{
			// 时间戳
			std::chrono::duration point = std::chrono::system_clock::now().time_since_epoch();
			std::chrono::milliseconds millisecs = std::chrono::duration_cast<std::chrono::milliseconds>(point);
			std::time_t timestamp = millisecs.count();
			// 随机数
			std::mt19937 gen(std::random_device{}());
			std::uniform_int_distribution<uint16_t> dis(0, 0x3FFF);
			std::uint16_t clock_seq = (dis(gen) | 0x8000);
			// 哈希地址
			std::size_t machash = std::hash<std::string>{}(addr);

			// 存储16字节UUID数据
			cipher::uuid uuid_byte;

			// 时间戳拆分（60位）
			// 低32位
			uuid_byte.m_data[3] = static_cast<xhqm::byte>(timestamp & 0xFF);
			uuid_byte.m_data[2] = static_cast<xhqm::byte>((timestamp >> 8) & 0xFF);
			uuid_byte.m_data[1] = static_cast<xhqm::byte>((timestamp >> 16) & 0xFF);
			uuid_byte.m_data[0] = static_cast<xhqm::byte>((timestamp >> 24) & 0xFF);
			uint16_t time_m = static_cast<uint16_t>((timestamp >> 32) & 0xFFFF);
			uint16_t time_h = static_cast<uint16_t>((timestamp >> 48) & 0x0FFF);
			time_h |= (0x1 << 12);  // 设置版本号（0001）
			// 中16位
			uuid_byte.m_data[5] = static_cast<xhqm::byte>((time_m >> 0) & 0xFF);
			uuid_byte.m_data[4] = static_cast<xhqm::byte>((time_m >> 8) & 0xFF);
			// 高12位
			uuid_byte.m_data[7] = static_cast<xhqm::byte>((time_h >> 0) & 0xFF);
			uuid_byte.m_data[6] = static_cast<xhqm::byte>((time_h >> 8) & 0xFF);

			// 设置变体 (10xxxxxx)
			uuid_byte.m_data[9] = static_cast<xhqm::byte>((clock_seq >> 0) & 0xFF);
			uuid_byte.m_data[8] = static_cast<xhqm::byte>((clock_seq >> 8) & 0xFF);

			// 节点标识符（48位）
			for (xhqm::size i = 0; i < 6; ++i)
			{
				uuid_byte.m_data[10 + i] = static_cast<xhqm::byte>((machash >> (i * 8)) & 0xFF);
			}

			return uuid_byte;
		}

		cipher::uuid uuid::creat_uuid_v2(std::string addr, xhqm::size local_id, xhqm::byte domain)
		{
			// 使用v1作为基础
			cipher::uuid uuid_byte = creat_uuid_v1(addr);

			// 设置版本号为2 (0010)
			uuid_byte.m_data[6] = (uuid_byte.m_data[6] & 0x0F) | 0x20;

			// 在时钟序列位置插入域标识符
			uuid_byte.m_data[9] = domain;

			// 替换节点标识符的前4字节为本地ID (小端序)
			uuid_byte.m_data[10] = static_cast<xhqm::byte>(local_id >> 0);
			uuid_byte.m_data[11] = static_cast<xhqm::byte>(local_id >> 8);
			uuid_byte.m_data[12] = static_cast<xhqm::byte>(local_id >> 16);
			uuid_byte.m_data[13] = static_cast<xhqm::byte>(local_id >> 24);

			return uuid_byte;
		}

		cipher::uuid uuid::creat_uuid_v3(const cipher::uuid& namespace_uuid, const cipher::text& name)
		{
			// 1. 准备输入数据：命名空间UUID + 名称字符串
			std::vector<xhqm::byte> input_data;

			// 添加命名空间UUID的原始字节
			for (xhqm::size i = 0; i < 16; ++i)
			{
				input_data.push_back(namespace_uuid.m_data[i]);
			}

			// 添加名称字符串的字节（UTF-8编码）
			for (char c : name)
			{
				input_data.push_back(static_cast<xhqm::byte>(c));
			}

			// 2. 计算MD5哈希
			cipher::md5 md5_result;
			md5_result(input_data);

			// 3. 从MD5结果中提取哈希值（小端序）
			cipher::uuid result;

			// 提取 a0 (4字节)
			result.m_data[0] = static_cast<xhqm::byte>(md5_result.a0 >> 24);
			result.m_data[1] = static_cast<xhqm::byte>(md5_result.a0 >> 16);
			result.m_data[2] = static_cast<xhqm::byte>(md5_result.a0 >> 8);
			result.m_data[3] = static_cast<xhqm::byte>(md5_result.a0);

			// 提取 b0 (4字节)
			result.m_data[4] = static_cast<xhqm::byte>(md5_result.b0 >> 24);
			result.m_data[5] = static_cast<xhqm::byte>(md5_result.b0 >> 16);
			result.m_data[6] = static_cast<xhqm::byte>(md5_result.b0 >> 8);
			result.m_data[7] = static_cast<xhqm::byte>(md5_result.b0);

			// 提取 c0 (4字节)
			result.m_data[8] = static_cast<xhqm::byte>(md5_result.c0 >> 24);
			result.m_data[9] = static_cast<xhqm::byte>(md5_result.c0 >> 16);
			result.m_data[10] = static_cast<xhqm::byte>(md5_result.c0 >> 8);
			result.m_data[11] = static_cast<xhqm::byte>(md5_result.c0);

			// 提取 d0 (4字节)
			result.m_data[12] = static_cast<xhqm::byte>(md5_result.d0 >> 24);
			result.m_data[13] = static_cast<xhqm::byte>(md5_result.d0 >> 16);
			result.m_data[14] = static_cast<xhqm::byte>(md5_result.d0 >> 8);
			result.m_data[15] = static_cast<xhqm::byte>(md5_result.d0);

			// 4. 设置版本号字段 (第三字节的高位)
			result.m_data[6] = (result.m_data[6] & 0x0F) | 0x30; // 00110000 (版本3)

			// 5. 设置变体字段 (第九字节的高位)
			result.m_data[8] = (result.m_data[8] & 0x3F) | 0x80; // 10000000 (RFC 4122变体)

			return result;
		}

		cipher::uuid uuid::creat_uuid_v4(std::random_device seed)
		{
			// 优化：thread_local 引擎，仅首次调用构造，混合 4 个 random_device 值 + 时间戳作强种子。
			// 修复原实现「每次只取 32 位种子」在 10 万量级即生日碰撞的问题（2^32 空间过小）。
			static thread_local std::mt19937_64 gen = [&seed]() {
				std::seed_seq seq{
					seed(), seed(), seed(), seed(),
					static_cast<unsigned int>(
						std::chrono::high_resolution_clock::now().time_since_epoch().count()),
				};
				return std::mt19937_64(seq);
			}();

			cipher::uuid uuid_byte;
			const std::uint64_t a = gen();
			const std::uint64_t b = gen();
			for (xhqm::size i = 0; i < 8; ++i)
			{
				uuid_byte.m_data[i] = static_cast<xhqm::byte>((a >> (8 * i)) & 0xFF);
				uuid_byte.m_data[8 + i] = static_cast<xhqm::byte>((b >> (8 * i)) & 0xFF);
			}

			// 设置版本 (0100xxxx)
			uuid_byte.m_data[6] = static_cast<xhqm::byte>((uuid_byte.m_data[6] & 0x0F) | 0x40);  // 版本4
			// 设置变体 (10xxxxxx)
			uuid_byte.m_data[8] = static_cast<xhqm::byte>((uuid_byte.m_data[8] & 0x3F) | 0x80);   // RFC4122变体

			return uuid_byte;
		}
	}
}
