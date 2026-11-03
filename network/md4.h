#pragma once
#ifndef XHQM_CIPHER_MD4_H
#define XHQM_CIPHER_MD4_H

#include "../head.h"

namespace xhqm
{
	namespace cipher
	{
		struct xhqm_io md4
		{
			uint32_t a0 = 0x67452301;
			uint32_t b0 = 0xEFCDAB89;
			uint32_t c0 = 0x98BADCFE;
			uint32_t d0 = 0x10325476;
		public:
			// 字节预处理
			static std::vector<xhqm::byte> preprocess(const std::vector<xhqm::byte>& binary);
			// 消息预处理
			static std::vector<xhqm::byte> preprocess(const std::string& message);

			inline xhqm::cipher::md4 operator()(const std::vector<xhqm::byte>& binary)
			{
				return hash(preprocess(binary));
			}
			inline xhqm::cipher::md4 operator()(const std::string& message)
			{
				return hash(preprocess(message));
			}
			inline operator std::string() const
			{
				return formathex(a0) + formathex(b0) + formathex(c0) + formathex(d0);
			}
		private:
			// MD4基本函数
			static uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
			static uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (x & z) | (y & z); }
			static uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
			static uint32_t left(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
			static std::string formathex(uint32_t num);

			xhqm::cipher::md4 hash(const std::vector<xhqm::byte>& data);
		};
	}
}

#endif // XHQM_CIPHER_MD4_H
