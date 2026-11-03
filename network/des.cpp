#include "des.h"
#include <stdexcept>

namespace xhqm
{
	namespace cipher
	{
		// 初始置换表 (IP)
		const uint8_t des::IP[64] = {
			58, 50, 42, 34, 26, 18, 10, 2,
			60, 52, 44, 36, 28, 20, 12, 4,
			62, 54, 46, 38, 30, 22, 14, 6,
			64, 56, 48, 40, 32, 24, 16, 8,
			57, 49, 41, 33, 25, 17, 9,  1,
			59, 51, 43, 35, 27, 19, 11, 3,
			61, 53, 45, 37, 29, 21, 13, 5,
			63, 55, 47, 39, 31, 23, 15, 7
		};

		// 最终置换表 (FP)
		const uint8_t des::FP[64] = {
			40, 8, 48, 16, 56, 24, 64, 32,
			39, 7, 47, 15, 55, 23, 63, 31,
			38, 6, 46, 14, 54, 22, 62, 30,
			37, 5, 45, 13, 53, 21, 61, 29,
			36, 4, 44, 12, 52, 20, 60, 28,
			35, 3, 43, 11, 51, 19, 59, 27,
			34, 2, 42, 10, 50, 18, 58, 26,
			33, 1, 41, 9,  49, 17, 57, 25
		};

		// 扩展置换表 (E)
		const uint8_t des::E[48] = {
			32, 1,  2,  3,  4,  5,
			4,  5,  6,  7,  8,  9,
			8,  9,  10, 11, 12, 13,
			12, 13, 14, 15, 16, 17,
			16, 17, 18, 19, 20, 21,
			20, 21, 22, 23, 24, 25,
			24, 25, 26, 27, 28, 29,
			28, 29, 30, 31, 32, 1
		};

		// P置换表
		const uint8_t des::P[32] = {
			16, 7,  20, 21,
			29, 12, 28, 17,
			1,  15, 23, 26,
			5,  18, 31, 10,
			2,  8,  24, 14,
			32, 27, 3,  9,
			19, 13, 30, 6,
			22, 11, 4,  25
		};

		// 密钥置换表1 (PC-1)
		const uint8_t des::PC1[56] = {
			57, 49, 41, 33, 25, 17, 9,
			1,  58, 50, 42, 34, 26, 18,
			10, 2,  59, 51, 43, 35, 27,
			19, 11, 3,  60, 52, 44, 36,
			63, 55, 47, 39, 31, 23, 15,
			7,  62, 54, 46, 38, 30, 22,
			14, 6,  61, 53, 45, 37, 29,
			21, 13, 5,  28, 20, 12, 4
		};

		// 密钥置换表2 (PC-2)
		const uint8_t des::PC2[48] = {
			14, 17, 11, 24, 1,  5,
			3,  28, 15, 6,  21, 10,
			23, 19, 12, 4,  26, 8,
			16, 7,  27, 20, 13, 2,
			41, 52, 31, 37, 47, 55,
			30, 40, 51, 45, 33, 48,
			44, 49, 39, 56, 34, 53,
			46, 42, 50, 36, 29, 32
		};

		// 密钥左移位数表
		const uint8_t des::SHIFT[16] = {
			1, 1, 2, 2, 2, 2, 2, 2,
			1, 2, 2, 2, 2, 2, 2, 1
		};

		// S盒 (8个)
		const uint8_t des::S_BOX[8][4][16] = {
			{{14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7},
			 {0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8},
			 {4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0},
			 {15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13}},// S1
			{{15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10},
			 {3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5},
			 {0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15},
			 {13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9}},// S2
			{{10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8},
			 {13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1},
			 {13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7},
			 {1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12}},// S3
			{{7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15},
			 {13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9},
			 {10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4},
			 {3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14}},// S4
			{{2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9},
			 {14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6},
			 {4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14},
			 {11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3}},// S5
			{{12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11},
			 {10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8},
			 {9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6},
			 {4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13}},// S6
			{{4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1},
			 {13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6},
			 {1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2},
			 {6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12}},// S7
			{{13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7},
			 {1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2},
			 {7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8},
			 {2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11}} // S8
		};

		void des::set_key(const std::vector<xhqm::byte>& value)
		{
			std::vector<xhqm::byte> key = value;
			key.resize(8, 0);
			m_sub_keys.resize(16);
			generate_sub_keys(key.data());
		}

		std::vector<byte> des::encrypt(const std::vector<byte>& data)
		{
			if (m_sub_keys.empty()) {
				throw std::runtime_error("Key not set");
			}

			std::vector<byte> padded = pkcs7_pad(data);
			std::vector<byte> encrypted(padded.size());

			for (size_t i = 0; i < padded.size(); i += 8) {
				// 组装64位块
				uint64_t block = 0;
				for (int j = 0; j < 8; ++j) {
					block = (block << 8) | padded[i + j];
				}

				// 加密处理
				uint64_t encrypted_block = process_block(block, true);

				// 存储结果
				for (int j = 7; j >= 0; --j) {
					encrypted[i + (7 - j)] = (encrypted_block >> (8 * j)) & 0xFF;
				}
			}

			return encrypted;
		}

		std::vector<byte> des::decrypt(const std::vector<byte>& data)
		{
			if (m_sub_keys.empty()) {
				throw std::runtime_error("Key not set");
			}

			if (data.size() % 8 != 0) {
				throw std::runtime_error("Ciphertext must be multiple of 8 bytes");
			}

			std::vector<byte> decrypted(data.size());

			for (size_t i = 0; i < data.size(); i += 8) {
				// 组装64位块
				uint64_t block = 0;
				for (int j = 0; j < 8; ++j) {
					block = (block << 8) | data[i + j];
				}

				// 解密处理
				uint64_t decrypted_block = process_block(block, false);

				// 存储结果
				for (int j = 7; j >= 0; --j) {
					decrypted[i + (7 - j)] = (decrypted_block >> (8 * j)) & 0xFF;
				}
			}

			// 去除填充
			return pkcs7_unpad(decrypted);
		}

		uint64_t des::permute(const uint8_t* table, int table_len, uint64_t input, int input_bits)
		{
			uint64_t result = 0;
			for (int i = 0; i < table_len; ++i) {
				int bit_pos = table_len - i - 1;
				int src_pos = input_bits - table[i];
				if ((input >> src_pos) & 1) {
					result |= (1ULL << bit_pos);
				}
			}
			return result;
		}

		void des::generate_sub_keys(const xhqm::byte key[8])
		{
			uint64_t key64 = 0;
			for (int i = 0; i < 8; ++i) {
				key64 = (key64 << 8) | key[i];
			}

			uint64_t permuted = permute(PC1, 56, key64, 64);

			uint32_t c = (permuted >> 28) & 0x0FFFFFFF;
			uint32_t d = permuted & 0x0FFFFFFF;

			for (int i = 0; i < 16; ++i) {
				// 循环左移
				int shift = SHIFT[i];
				c = ((c << shift) | (c >> (28 - shift))) & 0x0FFFFFFF;
				d = ((d << shift) | (d >> (28 - shift))) & 0x0FFFFFFF;

				// 合并并置换选择2
				uint64_t cd = ((uint64_t)c << 28) | d;
				m_sub_keys[i] = (uint32_t)permute(PC2, 48, cd, 56);
			}
		}

		uint32_t des::feistel(uint32_t r, uint32_t round_key)
		{
			// 扩展置换 (32位 -> 48位)
			uint64_t expanded = permute(E, 48, r, 32);

			// 与轮密钥异或
			expanded ^= round_key;

			// S盒替换
			uint32_t substituted = s_box_substitution(expanded);

			// P置换
			uint32_t output = (uint32_t)permute(P, 32, substituted, 32);

			return output;
		}

		uint32_t des::s_box_substitution(uint64_t input)
		{
			uint32_t output = 0;

			for (int i = 0; i < 8; ++i) {
				int box_num = 7 - i;
				uint8_t six_bits = (input >> (6 * i)) & 0x3F;

				int row = ((six_bits >> 4) & 0x2) | (six_bits & 0x1);
				int col = (six_bits >> 1) & 0xF;

				output <<= 4;
				output |= S_BOX[box_num][row][col];
			}

			return output;
		}

		uint64_t des::process_block(uint64_t block, bool is_encrypt)
		{
			// 初始置换
			block = permute(IP, 64, block, 64);

			uint32_t l = (block >> 32) & 0xFFFFFFFF;
			uint32_t r = block & 0xFFFFFFFF;

			// 16轮Feistel网络
			if (is_encrypt) {
				for (int i = 0; i < 16; ++i) {
					uint32_t temp = r;
					r = l ^ feistel(r, m_sub_keys[i]);
					l = temp;
				}
			}
			else {
				for (int i = 15; i >= 0; --i) {
					uint32_t temp = r;
					r = l ^ feistel(r, m_sub_keys[i]);
					l = temp;
				}
			}

			// 合并左右两部分 (注意最后一轮后不交换，但我们的循环已经完成了交换)
			uint64_t combined = ((uint64_t)r << 32) | l;

			// 最终置换
			return permute(FP, 64, combined, 64);
		}

		std::vector<byte> des::pkcs7_pad(const std::vector<byte>& data)
		{
			size_t block_size = 8;
			size_t padding_len = block_size - (data.size() % block_size);
			if (padding_len == 0) padding_len = block_size;

			std::vector<byte> padded = data;
			padded.resize(data.size() + padding_len, static_cast<byte>(padding_len));
			return padded;
		}

		std::vector<byte> des::pkcs7_unpad(const std::vector<byte>& data)
		{
			if (data.empty()) return data;

			byte padding_len = data.back();
			if (padding_len > 8 || padding_len == 0) return data;

			// 验证填充
			for (size_t i = data.size() - padding_len; i < data.size(); ++i) {
				if (data[i] != padding_len) {
					return data;  // 填充无效，返回原数据
				}
			}

			std::vector<byte> unpadded(data.begin(), data.begin() + data.size() - padding_len);
			return unpadded;
		}
	}
}