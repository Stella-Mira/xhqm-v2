#pragma once
#ifndef XHQM_CIPHER_AES_H
#define XHQM_CIPHER_AES_H

#include "../head.h"

namespace xhqm
{
	namespace cipher
	{
		class xhqm_io aes
		{
		public:
			enum type
			{
				aes_128 = 0,
				aes_192,
				aes_256,
			};
		private:
			cipher::aes::type m_type;

			// 内部状态
			xhqm::byte m_rounds = 0;
			std::vector<xhqm::byte> m_round_key;
			enum class mode { ECB, CBC, CFB, OFB, CTR } m_mode = mode::ECB;

		public:
			aes(cipher::aes::type _type = cipher::aes::aes_128);
			const cipher::aes::type& get_type(void) const { return m_type; }

			void set_mode_ecb() { m_mode = mode::ECB; }
			void set_mode_cbc() { m_mode = mode::CBC; }
			void set_key(const std::vector<xhqm::byte>& value);

			std::vector<byte> encrypt(const std::vector<byte>& data);
			std::vector<byte> decrypt(const std::vector<byte>& data);
		private:
			// 内部计算函数
			void sub_bytes(xhqm::byte state[4][4]);
			void inv_sub_bytes(xhqm::byte state[4][4]);
			void shift_rows(xhqm::byte state[4][4]);
			void inv_shift_rows(xhqm::byte state[4][4]);
			xhqm::byte gf_mult(xhqm::byte a, xhqm::byte b);
			void mix_columns(xhqm::byte state[4][4]);
			void inv_mix_columns(xhqm::byte state[4][4]);
			void add_round_key(xhqm::byte state[4][4], int round);
			void cipher(const xhqm::byte in[16], xhqm::byte out[16]);
			void inv_cipher(const xhqm::byte in[16], xhqm::byte out[16]);

			// CBC模式辅助函数
			std::vector<byte> encrypt_cbc(const std::vector<byte>& data);
			std::vector<byte> decrypt_cbc(const std::vector<byte>& data);

			// PKCS7填充
			std::vector<byte> pkcs7_pad(const std::vector<byte>& data);
			std::vector<byte> pkcs7_unpad(const std::vector<byte>& data);

			// 常量表
			static const xhqm::byte ary_s_box[256];
			static const xhqm::byte inv_s_box[256];
			static const xhqm::byte rcon[11];
		};
	}
}

#endif // XHQM_CIPHER_AES_H