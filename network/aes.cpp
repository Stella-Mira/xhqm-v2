#include "aes.h"
#include <stdexcept>

namespace xhqm
{
	namespace cipher
	{
		// S-box
		const xhqm::byte aes::ary_s_box[256] = {
			0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
			0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
			0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
			0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
			0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
			0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
			0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
			0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
			0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
			0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
			0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
			0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
			0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
			0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
			0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
			0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
		};

		// 逆S-box
		const xhqm::byte aes::inv_s_box[256] = {
			0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38, 0xBF, 0x40, 0xA3, 0x9E, 0x81, 0xF3, 0xD7, 0xFB,
			0x7C, 0xE3, 0x39, 0x82, 0x9B, 0x2F, 0xFF, 0x87, 0x34, 0x8E, 0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB,
			0x54, 0x7B, 0x94, 0x32, 0xA6, 0xC2, 0x23, 0x3D, 0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E,
			0x08, 0x2E, 0xA1, 0x66, 0x28, 0xD9, 0x24, 0xB2, 0x76, 0x5B, 0xA2, 0x49, 0x6D, 0x8B, 0xD1, 0x25,
			0x72, 0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xD4, 0xA4, 0x5C, 0xCC, 0x5D, 0x65, 0xB6, 0x92,
			0x6C, 0x70, 0x48, 0x50, 0xFD, 0xED, 0xB9, 0xDA, 0x5E, 0x15, 0x46, 0x57, 0xA7, 0x8D, 0x9D, 0x84,
			0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A, 0xF7, 0xE4, 0x58, 0x05, 0xB8, 0xB3, 0x45, 0x06,
			0xD0, 0x2C, 0x1E, 0x8F, 0xCA, 0x3F, 0x0F, 0x02, 0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B,
			0x3A, 0x91, 0x11, 0x41, 0x4F, 0x67, 0xDC, 0xEA, 0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6, 0x73,
			0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85, 0xE2, 0xF9, 0x37, 0xE8, 0x1C, 0x75, 0xDF, 0x6E,
			0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89, 0x6F, 0xB7, 0x62, 0x0E, 0xAA, 0x18, 0xBE, 0x1B,
			0xFC, 0x56, 0x3E, 0x4B, 0xC6, 0xD2, 0x79, 0x20, 0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD, 0x5A, 0xF4,
			0x1F, 0xDD, 0xA8, 0x33, 0x88, 0x07, 0xC7, 0x31, 0xB1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xEC, 0x5F,
			0x60, 0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D, 0x2D, 0xE5, 0x7A, 0x9F, 0x93, 0xC9, 0x9C, 0xEF,
			0xA0, 0xE0, 0x3B, 0x4D, 0xAE, 0x2A, 0xF5, 0xB0, 0xC8, 0xEB, 0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61,
			0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6, 0x26, 0xE1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0C, 0x7D
		};

		// 轮常数
		const xhqm::byte aes::rcon[11] = {
			0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
		};

		aes::aes(cipher::aes::type _type)
			: m_type(_type)
		{

		}

		void aes::set_key(const std::vector<xhqm::byte>& value)
		{
			int nk = 4, nr = 10;
			xhqm::size key_len = 16;  // 128 bits
			std::vector<xhqm::byte> key = value;

			switch (m_type) {
			case cipher::aes::type::aes_128:  // AES-128
				nk = 4;
				nr = 10;
				key_len = 16;
				m_rounds = 10;
				break;
			case cipher::aes::type::aes_192:  // AES-192
				nk = 6;
				nr = 12;
				key_len = 24;
				m_rounds = 12;
				break;
			case cipher::aes::type::aes_256:  // AES-256
				nk = 8;
				nr = 14;
				key_len = 32;
				m_rounds = 14;
				break;
			default:
				throw std::runtime_error("Invalid key size");
			}

			key.resize(key_len, 0);

			// 轮密钥大小 = 块大小(16) * (轮数+1)
			int key_schedule_size = 16 * (nr + 1);
			m_round_key.resize(key_schedule_size);

			// 复制原始密钥
			for (int i = 0; i < nk * 4; i++) {
				m_round_key[i] = key[i];
			}

			// 扩展密钥
			xhqm::byte temp[4];
			for (int i = nk; i < 4 * (nr + 1); i++) {
				// 获取前一个4字节
				for (int j = 0; j < 4; j++) {
					temp[j] = m_round_key[(i - 1) * 4 + j];
				}

				// 如果是每nk个字的开始
				if (i % nk == 0) {
					// 循环左移
					xhqm::byte t = temp[0];
					temp[0] = temp[1];
					temp[1] = temp[2];
					temp[2] = temp[3];
					temp[3] = t;

					// S盒替换
					temp[0] = ary_s_box[temp[0]];
					temp[1] = ary_s_box[temp[1]];
					temp[2] = ary_s_box[temp[2]];
					temp[3] = ary_s_box[temp[3]];

					// 与轮常数异或
					temp[0] ^= rcon[i / nk];
				}
				else if (nk > 6 && i % nk == 4) {
					// AES-256的特殊处理
					temp[0] = ary_s_box[temp[0]];
					temp[1] = ary_s_box[temp[1]];
					temp[2] = ary_s_box[temp[2]];
					temp[3] = ary_s_box[temp[3]];
				}

				// 生成新的轮密钥
				m_round_key[i * 4 + 0] = m_round_key[(i - nk) * 4 + 0] ^ temp[0];
				m_round_key[i * 4 + 1] = m_round_key[(i - nk) * 4 + 1] ^ temp[1];
				m_round_key[i * 4 + 2] = m_round_key[(i - nk) * 4 + 2] ^ temp[2];
				m_round_key[i * 4 + 3] = m_round_key[(i - nk) * 4 + 3] ^ temp[3];
			}
		}

		// 加密
		std::vector<byte> aes::encrypt(const std::vector<byte>& data)
		{
			if (m_round_key.empty()) {
				throw std::runtime_error("Key not set");
			}

			if (m_mode == mode::CBC) {
				return encrypt_cbc(data);
			}

			// ECB模式
			std::vector<byte> padded = pkcs7_pad(data);
			std::vector<byte> encrypted(padded.size());

			for (xhqm::size i = 0; i < padded.size(); i += 16) {
				xhqm::byte out[16];
				cipher(reinterpret_cast<const xhqm::byte*>(&padded[i]), out);
				memcpy(&encrypted[i], out, 16);
			}

			return encrypted;
		}

		// 解密
		std::vector<byte> aes::decrypt(const std::vector<byte>& data)
		{
			if (m_round_key.empty()) {
				throw std::runtime_error("Key not set");
			}

			if (data.size() % 16 != 0) {
				throw std::runtime_error("Ciphertext must be multiple of 16 bytes");
			}

			if (m_mode == mode::CBC) {
				return decrypt_cbc(data);
			}

			// ECB模式
			std::vector<byte> decrypted(data.size());

			for (xhqm::size i = 0; i < data.size(); i += 16) {
				xhqm::byte out[16];
				inv_cipher(reinterpret_cast<const xhqm::byte*>(&data[i]), out);
				memcpy(&decrypted[i], out, 16);
			}

			// 去除填充
			return pkcs7_unpad(decrypted);
		}

		// CBC模式加密
		std::vector<byte> aes::encrypt_cbc(const std::vector<byte>& data)
		{
			std::vector<byte> padded = pkcs7_pad(data);
			std::vector<byte> encrypted(padded.size());

			std::array<xhqm::byte, 16> block = {};
			std::array<xhqm::byte, 16> temp = {};

			for (xhqm::size i = 0; i < padded.size(); i += 16) {
				// XOR with previous ciphertext (or IV for first block)
				for (int j = 0; j < 16; j++) {
					temp[j] = padded[i + j] ^ block[j];
				}

				// AES加密
				xhqm::byte out[16];
				cipher(temp.data(), out);

				// 保存输出
				memcpy(&encrypted[i], out, 16);
				memcpy(block.data(), out, 16);
			}

			return encrypted;
		}

		// CBC模式解密
		std::vector<byte> aes::decrypt_cbc(const std::vector<byte>& data)
		{
			if (data.size() % 16 != 0) {
				throw std::runtime_error("CBC ciphertext must be multiple of 16 bytes");
			}

			std::vector<byte> decrypted(data.size());

			std::array<xhqm::byte, 16> block = {};
			std::array<xhqm::byte, 16> prev_block = {};

			for (xhqm::size i = 0; i < data.size(); i += 16) {
				// AES解密
				xhqm::byte out[16];
				inv_cipher(reinterpret_cast<const xhqm::byte*>(&data[i]), out);

				// XOR with previous ciphertext (or IV for first block)
				for (int j = 0; j < 16; j++) {
					decrypted[i + j] = out[j] ^ prev_block[j];
				}

				// 保存当前密文块供下一块使用
				memcpy(prev_block.data(), &data[i], 16);
			}

			// 去除填充
			return pkcs7_unpad(decrypted);
		}



		// 字节替换
		void aes::sub_bytes(xhqm::byte state[4][4])
		{
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					state[i][j] = ary_s_box[state[i][j]];
				}
			}
		}

		// 逆字节替换
		void aes::inv_sub_bytes(xhqm::byte state[4][4])
		{
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					state[i][j] = inv_s_box[state[i][j]];
				}
			}
		}

		// 行移位
		void aes::shift_rows(xhqm::byte state[4][4])
		{
			// 第0行不移位

			// 第1行循环左移1位
			xhqm::byte temp = state[1][0];
			state[1][0] = state[1][1];
			state[1][1] = state[1][2];
			state[1][2] = state[1][3];
			state[1][3] = temp;

			// 第2行循环左移2位
			temp = state[2][0];
			state[2][0] = state[2][2];
			state[2][2] = temp;
			temp = state[2][1];
			state[2][1] = state[2][3];
			state[2][3] = temp;

			// 第3行循环左移3位
			temp = state[3][3];
			state[3][3] = state[3][2];
			state[3][2] = state[3][1];
			state[3][1] = state[3][0];
			state[3][0] = temp;
		}

		// 逆行移位
		void aes::inv_shift_rows(xhqm::byte state[4][4])
		{
			// 第0行不移位

			// 第1行循环右移1位
			xhqm::byte temp = state[1][3];
			state[1][3] = state[1][2];
			state[1][2] = state[1][1];
			state[1][1] = state[1][0];
			state[1][0] = temp;

			// 第2行循环右移2位
			temp = state[2][0];
			state[2][0] = state[2][2];
			state[2][2] = temp;
			temp = state[2][1];
			state[2][1] = state[2][3];
			state[2][3] = temp;

			// 第3行循环右移3位
			temp = state[3][0];
			state[3][0] = state[3][1];
			state[3][1] = state[3][2];
			state[3][2] = state[3][3];
			state[3][3] = temp;
		}

		// GF(2^8)乘法
		xhqm::byte aes::gf_mult(xhqm::byte a, xhqm::byte b)
		{
			xhqm::byte p = 0;
			xhqm::byte carry;

			for (int i = 0; i < 8; i++) {
				if (b & 1) {
					p ^= a;
				}

				carry = a & 0x80;
				a <<= 1;
				if (carry) {
					a ^= 0x1b;  // 不可约多项式 x^8 + x^4 + x^3 + x + 1
				}
				b >>= 1;
			}

			return p;
		}

		// 列混合
		void aes::mix_columns(xhqm::byte state[4][4])
		{
			xhqm::byte temp[4][4];

			for (int c = 0; c < 4; c++) {
				temp[0][c] = gf_mult(0x02, state[0][c]) ^ gf_mult(0x03, state[1][c]) ^ state[2][c] ^ state[3][c];
				temp[1][c] = state[0][c] ^ gf_mult(0x02, state[1][c]) ^ gf_mult(0x03, state[2][c]) ^ state[3][c];
				temp[2][c] = state[0][c] ^ state[1][c] ^ gf_mult(0x02, state[2][c]) ^ gf_mult(0x03, state[3][c]);
				temp[3][c] = gf_mult(0x03, state[0][c]) ^ state[1][c] ^ state[2][c] ^ gf_mult(0x02, state[3][c]);
			}

			memcpy(state, temp, 16);
		}

		// 逆列混合
		void aes::inv_mix_columns(xhqm::byte state[4][4])
		{
			xhqm::byte temp[4][4];

			for (int c = 0; c < 4; c++) {
				temp[0][c] = gf_mult(0x0e, state[0][c]) ^ gf_mult(0x0b, state[1][c]) ^ gf_mult(0x0d, state[2][c]) ^ gf_mult(0x09, state[3][c]);
				temp[1][c] = gf_mult(0x09, state[0][c]) ^ gf_mult(0x0e, state[1][c]) ^ gf_mult(0x0b, state[2][c]) ^ gf_mult(0x0d, state[3][c]);
				temp[2][c] = gf_mult(0x0d, state[0][c]) ^ gf_mult(0x09, state[1][c]) ^ gf_mult(0x0e, state[2][c]) ^ gf_mult(0x0b, state[3][c]);
				temp[3][c] = gf_mult(0x0b, state[0][c]) ^ gf_mult(0x0d, state[1][c]) ^ gf_mult(0x09, state[2][c]) ^ gf_mult(0x0e, state[3][c]);
			}

			memcpy(state, temp, 16);
		}

		// 轮密钥加
		void aes::add_round_key(xhqm::byte state[4][4], int round)
		{
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					state[i][j] ^= m_round_key[round * 16 + i + 4 * j];
				}
			}
		}

		// AES加密一个块
		void aes::cipher(const xhqm::byte in[16], xhqm::byte out[16])
		{
			xhqm::byte state[4][4];

			// 将输入复制到state矩阵
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					state[i][j] = in[i + 4 * j];
				}
			}

			// 初始轮密钥加
			add_round_key(state, 0);

			// 主轮
			for (int round = 1; round < m_rounds; round++) {
				sub_bytes(state);
				shift_rows(state);
				mix_columns(state);
				add_round_key(state, round);
			}

			// 最后一轮（不进行列混合）
			sub_bytes(state);
			shift_rows(state);
			add_round_key(state, m_rounds);

			// 输出
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					out[i + 4 * j] = state[i][j];
				}
			}
		}

		// AES解密一个块
		void aes::inv_cipher(const xhqm::byte in[16], xhqm::byte out[16])
		{
			xhqm::byte state[4][4];

			// 将输入复制到state矩阵
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					state[i][j] = in[i + 4 * j];
				}
			}

			// 初始轮密钥加
			add_round_key(state, m_rounds);

			// 主轮
			for (int round = m_rounds - 1; round > 0; round--) {
				inv_shift_rows(state);
				inv_sub_bytes(state);
				add_round_key(state, round);
				inv_mix_columns(state);
			}

			// 最后一轮（不进行列混合）
			inv_shift_rows(state);
			inv_sub_bytes(state);
			add_round_key(state, 0);

			// 输出
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					out[i + 4 * j] = state[i][j];
				}
			}
		}

		// PKCS7填充
		std::vector<byte> aes::pkcs7_pad(const std::vector<byte>& data)
		{
			xhqm::size block_size = 16;
			xhqm::size padding_len = block_size - (data.size() % block_size);
			if (padding_len == 0) padding_len = block_size;

			std::vector<byte> padded = data;
			padded.resize(data.size() + padding_len, static_cast<byte>(padding_len));
			return padded;
		}

		// PKCS7去填充
		std::vector<byte> aes::pkcs7_unpad(const std::vector<byte>& data)
		{
			if (data.empty()) return data;

			byte padding_len = data.back();
			if (padding_len > 16) return data;  // 无效的填充长度

			// 验证填充
			for (xhqm::size i = data.size() - padding_len; i < data.size(); i++) {
				if (data[i] != padding_len) {
					return data;  // 填充无效，返回原数据
				}
			}

			std::vector<byte> unpadded(data.begin(), data.begin() + data.size() - padding_len);
			return unpadded;
		}


	}
}