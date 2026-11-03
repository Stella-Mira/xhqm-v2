#include "sha.h"
#include <sstream>
#include <iomanip>

namespace xhqm
{
	namespace cipher
	{
		// 消息预处理
		std::vector<xhqm::byte> sha::preprocess(const std::string& message)
		{
			std::vector<xhqm::byte> result(message.begin(), message.end());

			auto function_sha1 = [](std::vector<xhqm::byte>& result)
				-> std::vector<xhqm::byte>&
			{
				uint64_t bitLen = result.size() * 8;
				result.push_back(0x80);
				while ((result.size() % 64) != 56)
					result.push_back(0x00);
				for (int i = 7; i >= 0; --i)
					result.push_back(((bitLen >> (i * 8)) & 0xFF));
				return result;
			};
			auto function_sha2_224 = function_sha1;
			auto function_sha2_256 = function_sha1;
			auto function_sha2_384 = [](std::vector<xhqm::byte>& result)
				-> std::vector<xhqm::byte>&
			{
				const uint64_t orig_size = result.size();
				const uint64_t totalBits_h = orig_size >> 61;   // 高3位
				const uint64_t totalBits_l = orig_size << 3;     // 低61位*8
				result.push_back(0x80);
				result.insert(result.end(), ((128 - (result.size() % 128 + 16)) % 128), 0x00);
				for (int i = 7; i >= 0; --i)
					result.push_back(static_cast<xhqm::byte>((totalBits_h >> (i * 8)) & 0xFF));
				for (int i = 7; i >= 0; --i)
					result.push_back(static_cast<xhqm::byte>((totalBits_l >> (i * 8)) & 0xFF));
				return result;
			};
			auto function_sha2_512 = function_sha2_384;

			switch (type)
			{
			case sha_type::sha1:        return function_sha1(result);
			case sha_type::sha2_224:    return function_sha2_224(result);
			case sha_type::sha2_256:    return function_sha2_256(result);
			case sha_type::sha2_384:    return function_sha2_384(result);
			case sha_type::sha2_512:    return function_sha2_512(result);
			case sha_type::sha3_224:
			case sha_type::sha3_256:
			case sha_type::sha3_384:
			case sha_type::sha3_512:
				break;
			}

			return result;
		}

		std::vector<xhqm::byte> sha::preprocess(const std::vector<xhqm::byte>& binary)
		{
			std::vector<xhqm::byte> result(binary.begin(), binary.end());

			auto function_sha1 = [](std::vector<xhqm::byte>& result)
				-> std::vector<xhqm::byte>&
			{
				uint64_t bitLen = result.size() * 8;
				result.push_back(0x80);
				while ((result.size() % 64) != 56)
					result.push_back(0x00);
				for (int i = 7; i >= 0; --i)
					result.push_back(((bitLen >> (i * 8)) & 0xFF));
				return result;
			};
			auto function_sha2_224 = function_sha1;
			auto function_sha2_256 = function_sha1;
			auto function_sha2_384 = [](std::vector<xhqm::byte>& result)
				-> std::vector<xhqm::byte>&
			{
				const uint64_t orig_size = result.size();
				const uint64_t totalBits_h = orig_size >> 61;   // 高3位
				const uint64_t totalBits_l = orig_size << 3;     // 低61位*8
				result.push_back(0x80);
				result.insert(result.end(), ((128 - (result.size() % 128 + 16)) % 128), 0x00);
				for (int i = 7; i >= 0; --i)
					result.push_back(static_cast<xhqm::byte>((totalBits_h >> (i * 8)) & 0xFF));
				for (int i = 7; i >= 0; --i)
					result.push_back(static_cast<xhqm::byte>((totalBits_l >> (i * 8)) & 0xFF));
				return result;
			};
			auto function_sha2_512 = function_sha2_384;

			switch (type)
			{
			case sha_type::sha1:        return function_sha1(result);
			case sha_type::sha2_224:    return function_sha2_224(result);
			case sha_type::sha2_256:    return function_sha2_256(result);
			case sha_type::sha2_384:    return function_sha2_384(result);
			case sha_type::sha2_512:    return function_sha2_512(result);
			case sha_type::sha3_224:
			case sha_type::sha3_256:
			case sha_type::sha3_384:
			case sha_type::sha3_512:
				break;
			}

			return result;
		}


		//初始化数据
		void sha::init()
		{
			switch (type)
			{
			case sha_type::sha1:
				h0 = 0x67452301; h1 = 0xEFCDAB89; h2 = 0x98BADCFE; h3 = 0x10325476;
				h4 = 0xC3D2E1F0; h5 = 0x00000000; h6 = 0x00000000; h7 = 0x00000000;
				break;
			case sha_type::sha2_224:
				h0 = 0xc1059ed8; h1 = 0x367cd507; h2 = 0x3070dd17; h3 = 0xf70e5939;
				h4 = 0xffc00b31; h5 = 0x68581511; h6 = 0x64f98fa7; h7 = 0xbefa4fa4;
				break;
			case sha_type::sha2_256:
				h0 = 0x6a09e667; h1 = 0xbb67ae85; h2 = 0x3c6ef372; h3 = 0xa54ff53a;
				h4 = 0x510e527f; h5 = 0x9b05688c; h6 = 0x1f83d9ab; h7 = 0x5be0cd19;
				break;
			case sha_type::sha2_384:
				h0 = 0xcbbb9d5dc1059ed8; h1 = 0x629a292a367cd507;
				h2 = 0x9159015a3070dd17; h3 = 0x152fecd8f70e5939;
				h4 = 0x67332667ffc00b31; h5 = 0x8eb44a8768581511;
				h6 = 0xdb0c2e0d64f98fa7; h7 = 0x47b5481dbefa4fa4;
				break;
			case sha_type::sha2_512:
				h0 = 0x6a09e667f3bcc908; h1 = 0xbb67ae8584caa73b;
				h2 = 0x3c6ef372fe94f82b; h3 = 0xa54ff53a5f1d36f1;
				h4 = 0x510e527fade682d1; h5 = 0x9b05688c2b3e6c1f;
				h6 = 0x1f83d9abfb41bd6b; h7 = 0x5be0cd19137e2179;
				break;
			case sha_type::sha3_224:
			case sha_type::sha3_256:
			case sha_type::sha3_384:
			case sha_type::sha3_512:
			default:
				h0 = 0; h1 = 0; h2 = 0; h3 = 0;
				h4 = 0; h5 = 0; h6 = 0; h7 = 0;
				break;
			}
		}

		//输出结果数据
		std::string sha::exit() const
		{
			switch (type)
			{
			case sha_type::sha1:
				return hex32(h0) + hex32(h1) + hex32(h2) + hex32(h3) + hex32(h4);
			case sha_type::sha2_224:
				return hex32(h0) + hex32(h1) + hex32(h2) + hex32(h3) + hex32(h4) + hex32(h5) + hex32(h6);
			case sha_type::sha2_256:
				return hex32(h0) + hex32(h1) + hex32(h2) + hex32(h3) + hex32(h4) + hex32(h5) + hex32(h6) + hex32(h7);
			case sha_type::sha2_384:
				return hex64(h0) + hex64(h1) + hex64(h2) + hex64(h3) + hex64(h4) + hex64(h5);
			case sha_type::sha2_512:
				return hex64(h0) + hex64(h1) + hex64(h2) + hex64(h3) + hex64(h4) + hex64(h5) + hex64(h6) + hex64(h7);
			case sha_type::sha3_224:
			case sha_type::sha3_256:
			case sha_type::sha3_384:
			case sha_type::sha3_512:
			{
				// 根据类型确定输出字节长度
				xhqm::size len = 0;
				switch (type) {
				case sha_type::sha3_224: len = 28; break; // 224-bit = 28字节
				case sha_type::sha3_256: len = 32; break; // 256-bit = 32字节
				case sha_type::sha3_384: len = 48; break; // 384-bit = 48字节
				case sha_type::sha3_512: len = 64; break; // 512-bit = 64字节
				default: return "";
				}

				// 大端序提取h0-h7的字节
				const uint64_t hs[8] = { h0, h1, h2, h3, h4, h5, h6, h7 };
				std::vector<xhqm::byte> bytes;
				bytes.reserve(64);
				for (uint64_t h : hs)
				{
					for (int shift = 56; shift >= 0; shift -= 8)
					{
						bytes.push_back(static_cast<xhqm::byte>((h >> shift) & 0xFF));
					}
				}

				// 十六进制编码指定长度的字节
				std::string hexStr;
				hexStr.reserve(len * 2);
				static const char hex_chars[] = "0123456789abcdef";
				for (xhqm::size i = 0; i < len; ++i)
				{
					xhqm::byte b = bytes[i];
					hexStr.push_back(hex_chars[b >> 4]);
					hexStr.push_back(hex_chars[b & 0x0F]);
				}
				return hexStr;
			}
			default:
				return "";
			}
		}

		// SHA-1 实现
		xhqm::cipher::sha sha::_sha1(const std::vector<xhqm::byte>& message)
		{
			// 初始化哈希值
			h0 = 0x67452301;
			h1 = 0xEFCDAB89;
			h2 = 0x98BADCFE;
			h3 = 0x10325476;
			h4 = 0xC3D2E1F0;

			// 处理512位（64字节）数据块
			for (size_t i = 0; i < message.size(); i += 64) {
				// 消息调度数组
				uint32_t w[80] = { 0 };

				// 初始化前16个字（大端转小端）
				for (int t = 0; t < 16; ++t)
				{
					w[t] =
						(message[i + t * 4 + 0] << 24) |
						(message[i + t * 4 + 1] << 16) |
						(message[i + t * 4 + 2] << 8) |
						(message[i + t * 4 + 3]);
				}

				// 扩展消息
				for (int t = 16; t < 80; ++t)
				{
					w[t] = left32(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
				}

				// 初始化哈希值
				uint32_t a = static_cast<uint32_t>(h0);
				uint32_t b = static_cast<uint32_t>(h1);
				uint32_t c = static_cast<uint32_t>(h2);
				uint32_t d = static_cast<uint32_t>(h3);
				uint32_t e = static_cast<uint32_t>(h4);

				// 主循环
				for (int t = 0; t < 80; ++t)
				{
					uint32_t f, k;
					if (t < 20) {
						f = (b & c) | ((~b) & d);
						k = 0x5A827999;
					}
					else if (t < 40) {
						f = b ^ c ^ d;
						k = 0x6ED9EBA1;
					}
					else if (t < 60) {
						f = (b & c) | (b & d) | (c & d);
						k = 0x8F1BBCDC;
					}
					else {
						f = b ^ c ^ d;
						k = 0xCA62C1D6;
					}

					uint32_t temp = left32(a, 5) + f + e + k + w[t];
					e = d;
					d = c;
					c = left32(b, 30);
					b = a;
					a = temp;
				}

				// 更新哈希值
				h0 += a;
				h1 += b;
				h2 += c;
				h3 += d;
				h4 += e;
			};

			return (*this);
		}

		// SHA-224 实现
		xhqm::cipher::sha sha::_sha2_224(const std::vector<xhqm::byte>& input)
		{
			// 从成员变量获取 SHA-224 初始值
			uint32_t H[8] = {
				static_cast<uint32_t>(h0),
				static_cast<uint32_t>(h1),
				static_cast<uint32_t>(h2),
				static_cast<uint32_t>(h3),
				static_cast<uint32_t>(h4),
				static_cast<uint32_t>(h5),
				static_cast<uint32_t>(h6),
				static_cast<uint32_t>(h7)
			};

			// 处理每个 512 位（64 字节）区块
			for (size_t i = 0; i < input.size(); i += 64) {
				uint32_t W[64] = { 0 };
				uint32_t a, b, c, d, e, f, g, h;

				// 准备消息调度表
				for (int t = 0; t < 16; ++t)
					W[t] = (input[i + t * 4] << 24) | (input[i + t * 4 + 1] << 16) |
					(input[i + t * 4 + 2] << 8) | input[i + t * 4 + 3];

				for (int t = 16; t < 64; ++t) {
					uint32_t s0 = (right32(W[t - 15], 7)) ^ (right32(W[t - 15], 18)) ^ (W[t - 15] >> 3);
					uint32_t s1 = (right32(W[t - 2], 17)) ^ (right32(W[t - 2], 19)) ^ (W[t - 2] >> 10);
					W[t] = W[t - 16] + s0 + W[t - 7] + s1;
				}

				// 初始化工作变量
				a = H[0]; b = H[1]; c = H[2]; d = H[3];
				e = H[4]; f = H[5]; g = H[6]; h = H[7];

				// 主循环
				for (int t = 0; t < 64; ++t) {
					uint32_t S1 = (right32(e, 6)) ^ (right32(e, 11)) ^ (right32(e, 25));
					uint32_t ch = (e & f) ^ ((~e) & g);
					uint32_t temp1 = h + S1 + ch + k256[t] + W[t];
					uint32_t S0 = (right32(a, 2)) ^ (right32(a, 13)) ^ (right32(a, 22));
					uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
					uint32_t temp2 = S0 + maj;

					h = g; g = f; f = e; e = d + temp1;
					d = c; c = b; b = a; a = temp1 + temp2;
				}

				// 更新哈希值
				H[0] += a; H[1] += b; H[2] += c; H[3] += d;
				H[4] += e; H[5] += f; H[6] += g; H[7] += h;
			}

			// 保存结果到成员变量
			h0 = H[0]; h1 = H[1]; h2 = H[2]; h3 = H[3];
			h4 = H[4]; h5 = H[5]; h6 = H[6]; h7 = H[7];
			return (*this);
		}

		// SHA-256 实现
		xhqm::cipher::sha sha::_sha2_256(const std::vector<xhqm::byte>& input)
		{
			// SHA-256 初始值
			uint32_t H[8] = {
				0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
				0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
			};

			// 处理每个 512 位块
			for (size_t i = 0; i < input.size(); i += 64) {
				uint32_t W[64] = { 0 };
				uint32_t a, b, c, d, e, f, g, h;

				// 准备消息调度
				for (int t = 0; t < 16; ++t)
					W[t] = (input[i + t * 4] << 24) | (input[i + t * 4 + 1] << 16) |
					(input[i + t * 4 + 2] << 8) | input[i + t * 4 + 3];

				for (int t = 16; t < 64; ++t) {
					uint32_t s0 = (right32(W[t - 15], 7)) ^ (right32(W[t - 15], 18)) ^ (W[t - 15] >> 3);
					uint32_t s1 = (right32(W[t - 2], 17)) ^ (right32(W[t - 2], 19)) ^ (W[t - 2] >> 10);
					W[t] = W[t - 16] + s0 + W[t - 7] + s1;
				}

				// 初始化工作变量
				a = H[0]; b = H[1]; c = H[2]; d = H[3];
				e = H[4]; f = H[5]; g = H[6]; h = H[7];

				// 主循环
				for (int t = 0; t < 64; ++t) {
					uint32_t S1 = (right32(e, 6)) ^ (right32(e, 11)) ^ (right32(e, 25));
					uint32_t ch = (e & f) ^ ((~e) & g);
					uint32_t temp1 = h + S1 + ch + k256[t] + W[t];
					uint32_t S0 = (right32(a, 2)) ^ (right32(a, 13)) ^ (right32(a, 22));
					uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
					uint32_t temp2 = S0 + maj;

					h = g; g = f; f = e; e = d + temp1;
					d = c; c = b; b = a; a = temp1 + temp2;
				}

				// 更新哈希值
				H[0] += a; H[1] += b; H[2] += c; H[3] += d;
				H[4] += e; H[5] += f; H[6] += g; H[7] += h;
			}

			// 保存结果
			h0 = H[0]; h1 = H[1]; h2 = H[2]; h3 = H[3];
			h4 = H[4]; h5 = H[5]; h6 = H[6]; h7 = H[7];
			return (*this);
		}

		// SHA-384 实现
		xhqm::cipher::sha sha::_sha2_384(const std::vector<xhqm::byte>& input)
		{
			uint64_t H[8] = { h0, h1, h2, h3, h4, h5, h6, h7 };

			for (size_t i = 0; i < input.size(); i += 128) {
				uint64_t W[80] = { 0 };
				uint64_t a, b, c, d, e, f, g, h;

				// 大端序解析输入
				for (int t = 0; t < 16; ++t) {
					W[t] =
						(uint64_t)input[i + t * 8 + 0] << 56 |
						(uint64_t)input[i + t * 8 + 1] << 48 |
						(uint64_t)input[i + t * 8 + 2] << 40 |
						(uint64_t)input[i + t * 8 + 3] << 32 |
						(uint64_t)input[i + t * 8 + 4] << 24 |
						(uint64_t)input[i + t * 8 + 5] << 16 |
						(uint64_t)input[i + t * 8 + 6] << 8 |
						(uint64_t)input[i + t * 8 + 7];
				}

				// 消息调度
				for (int t = 16; t < 80; ++t) {
					uint64_t s0 = right64(W[t - 15], 1) ^ right64(W[t - 15], 8) ^ (W[t - 15] >> 7);
					uint64_t s1 = right64(W[t - 2], 19) ^ right64(W[t - 2], 61) ^ (W[t - 2] >> 6);
					W[t] = W[t - 16] + s0 + W[t - 7] + s1;
				}

				// 初始化工作变量
				a = H[0]; b = H[1]; c = H[2]; d = H[3];
				e = H[4]; f = H[5]; g = H[6]; h = H[7];

				// 主循环
				for (int t = 0; t < 80; ++t) {
					uint64_t S1 = right64(e, 14) ^ right64(e, 18) ^ right64(e, 41);
					uint64_t ch = (e & f) ^ (~e & g);
					uint64_t temp1 = h + S1 + ch + k512[t] + W[t];
					uint64_t S0 = right64(a, 28) ^ right64(a, 34) ^ right64(a, 39);
					uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
					uint64_t temp2 = S0 + maj;

					// 更新工作变量
					h = g;
					g = f;
					f = e;
					e = d + temp1;
					d = c;
					c = b;
					b = a;
					a = temp1 + temp2;
				}

				// 更新哈希值
				H[0] += a; H[1] += b; H[2] += c; H[3] += d;
				H[4] += e; H[5] += f; H[6] += g; H[7] += h;
			}

			// 保存最终状态（不要手动截断）
			h0 = H[0]; h1 = H[1]; h2 = H[2]; h3 = H[3];
			h4 = H[4]; h5 = H[5]; h6 = H[6]; h7 = H[7];

			return *this;
		}

		// SHA-512 实现
		xhqm::cipher::sha sha::_sha2_512(const std::vector<xhqm::byte>& input)
		{
			// SHA-512 初始值
			uint64_t H[8] = { h0, h1, h2, h3, h4, h5, h6, h7 };

			// 处理每个 1024 位块
			for (size_t i = 0; i < input.size(); i += 128) {
				uint64_t W[80] = { 0 };
				uint64_t a, b, c, d, e, f, g, h;

				for (int t = 0; t < 16; ++t) {
					W[t] =
						(uint64_t)input[i + t * 8 + 0] << 56 |
						(uint64_t)input[i + t * 8 + 1] << 48 |
						(uint64_t)input[i + t * 8 + 2] << 40 |
						(uint64_t)input[i + t * 8 + 3] << 32 |
						(uint64_t)input[i + t * 8 + 4] << 24 |
						(uint64_t)input[i + t * 8 + 5] << 16 |
						(uint64_t)input[i + t * 8 + 6] << 8 |
						(uint64_t)input[i + t * 8 + 7];
				}

				for (int t = 16; t < 80; ++t) {
					uint64_t s0 = (right64(W[t - 15], 1) ^ right64(W[t - 15], 8) ^ (W[t - 15] >> 7));
					uint64_t s1 = (right64(W[t - 2], 19) ^ right64(W[t - 2], 61) ^ (W[t - 2] >> 6));
					W[t] = W[t - 16] + s0 + W[t - 7] + s1;
				}

				// 初始化工作变量
				a = H[0]; b = H[1]; c = H[2]; d = H[3];
				e = H[4]; f = H[5]; g = H[6]; h = H[7];

				// 主循环
				for (int t = 0; t < 80; ++t) {
					uint64_t S1 = right64(e, 14) ^ right64(e, 18) ^ right64(e, 41);
					uint64_t ch = (e & f) ^ ((~e) & g);
					uint64_t temp1 = h + S1 + ch + k512[t] + W[t];
					uint64_t S0 = right64(a, 28) ^ right64(a, 34) ^ right64(a, 39);
					uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
					uint64_t temp2 = S0 + maj;

					h = g; g = f; f = e; e = d + temp1;
					d = c; c = b; b = a; a = temp1 + temp2;
				}

				// 更新哈希值
				H[0] += a; H[1] += b; H[2] += c; H[3] += d;
				H[4] += e; H[5] += f; H[6] += g; H[7] += h;
			}

			// 保存结果
			h0 = H[0]; h1 = H[1]; h2 = H[2]; h3 = H[3];
			h4 = H[4]; h5 = H[5]; h6 = H[6]; h7 = H[7];
			return (*this);
		}


		// 其他SHA版本暂留空
		xhqm::cipher::sha sha::_sha3_224(const std::vector<xhqm::byte>&) { return (*this); }
		xhqm::cipher::sha sha::_sha3_256(const std::vector<xhqm::byte>&) { return (*this); }
		xhqm::cipher::sha sha::_sha3_384(const std::vector<xhqm::byte>&) { return (*this); }
		xhqm::cipher::sha sha::_sha3_512(const std::vector<xhqm::byte>&) { return (*this); }
	}
}