#pragma once
#ifndef XHQM_CIPHER_MD5_H
#define XHQM_CIPHER_MD5_H

#include "../head.h"

namespace xhqm
{
	namespace cipher
	{
		struct xhqm_io md5
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

			inline xhqm::cipher::md5 operator()(const  std::vector<xhqm::byte>& binary)
			{
				return hash(preprocess(binary));
			}
			inline xhqm::cipher::md5 operator()(const std::string& message)
			{
				return hash(preprocess(message));
			}
			inline operator std::string() const
			{
				return formathex(a0) + formathex(b0) + formathex(c0) + formathex(d0);
			}
		private:
			// 循环左移函数
			// 基础操作函数
			template<typename function, typename rotate>
			static void OPFunction(function func, rotate left, uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t M, uint32_t s, uint32_t t)
			{
				a += func(b, c, d) + M + t;
				a = left(a, s) + b;
			}
			static uint32_t left(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
			static uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
			static uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
			static uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
			static uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }
			static void FF(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t M, uint32_t s, uint32_t t) { OPFunction(F, left, a, b, c, d, M, s, t); }
			static void GG(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t M, uint32_t s, uint32_t t) { OPFunction(G, left, a, b, c, d, M, s, t); }
			static void HH(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t M, uint32_t s, uint32_t t) { OPFunction(H, left, a, b, c, d, M, s, t); }
			static void II(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t M, uint32_t s, uint32_t t) { OPFunction(I, left, a, b, c, d, M, s, t); }
			// 格式化十六进制输出
			static std::string formathex(uint32_t num);

			xhqm::cipher::md5 hash(const std::vector<xhqm::byte>& data);
		};
	}
}

#endif // XHQM_UUID_H
