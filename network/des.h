#pragma once
#ifndef XHQM_CIPHER_DES_H
#define XHQM_CIPHER_DES_H

#include "../head.h"

namespace xhqm
{
	namespace cipher
	{
		class xhqm_io des
		{
		public:
			inline des() = default;
			void set_key(const std::vector<xhqm::byte>& value); // 密钥必须是8字节（64位，实际使用56位）

			std::vector<byte> encrypt(const std::vector<byte>& data);
			std::vector<byte> decrypt(const std::vector<byte>& data);

		private:
			// 密钥相关
			std::vector<uint32_t> m_sub_keys; // 存储16轮子密钥

			// 核心处理函数
			void generate_sub_keys(const xhqm::byte key[8]);
			uint64_t process_block(uint64_t block, bool is_encrypt);

			// Feistel轮函数
			uint32_t feistel(uint32_t r, uint32_t round_key);

			// 工具函数
			uint64_t permute(const uint8_t* table, int table_len, uint64_t input, int input_bits);
			//uint32_t permute_pc1(uint64_t key);
			//uint64_t permute_pc2(uint32_t c, uint32_t d);
			uint32_t s_box_substitution(uint64_t input);

			// 模式与填充 (仿照AES结构，但DES标准模式为ECB)
			std::vector<byte> pkcs7_pad(const std::vector<byte>& data);
			std::vector<byte> pkcs7_unpad(const std::vector<byte>& data);

			// 置换表与S盒 (声明为静态常量，将在.cpp文件中定义)
			static const uint8_t IP[64];      // 初始置换表
			static const uint8_t FP[64];      // 最终置换表
			static const uint8_t E[48];       // 扩展置换表
			static const uint8_t P[32];       // P置换表
			static const uint8_t PC1[56];     // 密钥置换表1
			static const uint8_t PC2[48];     // 密钥置换表2
			static const uint8_t SHIFT[16];   // 密钥左移位数表
			static const uint8_t S_BOX[8][4][16]; // 8个S盒
		};
	}
}

#endif // XHQM_CIPHER_DES_H