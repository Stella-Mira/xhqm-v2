// base64.cpp
#include "base64.h"
#include <sstream>

namespace xhqm
{
	namespace cipher
	{
		const std::string base64_chars =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz"
			"0123456789+/";

		//检查字符是否为有效的Base64字符
		inline bool is_base64(xhqm::byte c)
		{
			return (std::isalnum(c) || (c == '+') || (c == '/'));
		}

		xhqm::text base64::encode(const std::vector<xhqm::byte>& data)
		{
			xhqm::text result;
			xhqm::size length = data.size();
			if (data.empty()) { return result; }
			result.reserve(((length + 2) / 3) * 4);

			for (xhqm::size i = 0; i < length; i += 3)
			{
				// 取出3个字节（24位）
				uint32_t triple = 0;
				xhqm::index index = 0;

				for (xhqm::index j = 0; j < 3; ++j)
				{
					if (i + j < length)
					{
						triple |= static_cast<uint32_t>(data[i + j]) << (16 - j * 8);
						index++;
					}
				}

				// 将24位分成4个6位的组
				for (xhqm::index j = 0; j < 4; ++j)
				{
					if (j <= index)
					{
						result.push_back(base64_chars[static_cast<xhqm::byte>((triple >> (18 - j * 6)) & 0x3F)]);
					}
					else
					{
						result.push_back('='); // 填充
					}
				}
			}
			return result;
		}

		std::vector<xhqm::byte> base64::decode(const xhqm::text& base)
		{
			std::vector<xhqm::byte> result;
			xhqm::size length = base.length();
			if (length % 4 != 0) { return result; }
			result.reserve((length * 3) / 4);

			// 创建反向映射表，加速字符查找
			xhqm::byte char_to_value[256] = { 0 };
			for (xhqm::index i = 0; i < base64_chars.length(); ++i)
			{
				char_to_value[static_cast<xhqm::byte>(base64_chars[i])] = static_cast<xhqm::byte>(i);
			}

			xhqm::size i = 0;
			while (i < length)
			{
				uint32_t quad = 0;
				xhqm::index chars_processed = 0;

				// 处理4个字符
				for (xhqm::index j = 0; j < 4; ++j)
				{
					xhqm::byte c = base[i + j];

					// 跳过非Base64字符（如空格、换行符等）
					if (c == '=')
					{
						// 填充字符，对应0值
						chars_processed++;
					}
					else
					{
						auto value = char_to_value[c];
						if (value == 0 && c != 'A') {  // 'A' 对应0，需要特殊处理
							// 检查字符是否在Base64字符集中
							if (!is_base64(c))
							{
								return result;
							}
						}
						quad |= static_cast<uint32_t>(value) << (18 - j * 6);
						chars_processed++;
					}
				}

				// 将24位分成3个字节
				for (xhqm::index j = 0; j < 3; ++j)
				{
					if (j < chars_processed - 1)
					{
						result.push_back(static_cast<xhqm::byte>((quad >> (16 - j * 8)) & 0xFF));
					}
				}

				i += 4;
			}

			// 移除末尾的填充字节
			// 检查末尾有多少个'='字符
			xhqm::size padding_count = 0;
			for (xhqm::index j = 1; j <= 2 && j <= length; ++j)
			{
				if (base[length - j] == '=')
				{
					padding_count++;
				}
			}

			// 移除相应的字节
			if (padding_count > 0)
			{
				result.resize(result.size() - padding_count);
			}

			return result;
		}
	}
}