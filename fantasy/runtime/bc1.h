#pragma once
#ifndef XHQM_FANTASY_BC1
#define XHQM_FANTASY_BC1

// BC1 (DXT1) 纹理压缩（M4）：CPU 离线压缩，4x4 块每块 8 字节（4bpp）。
// v1 只编码 RGB（忽略 alpha，输出始终不透明）；w/h 非 4 的倍数时按边缘重复补齐。
#include "fantasy.h"
#include <vector>
#include <cstdint>

namespace xhqm
{
	namespace fantasy
	{
		// RGBA8（xhqm::rgba，BGR(A) 字节序）→ BC1 压缩字节流（长度 = ceil(w/4)*ceil(h/4)*8）
		xhqm_io std::vector<std::uint8_t> bc1_encode(const std::vector<rgba>& flat, uint w, uint h);

		// BC1 字节流 → RGBA8（用于验证/软件解码回退）
		xhqm_io std::vector<rgba> bc1_decode(const std::vector<std::uint8_t>& bc1, uint w, uint h);
	}
}

#endif
