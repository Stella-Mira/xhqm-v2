#include "bc1.h"
#include "colors.h"
#include <cstring>

namespace xhqm
{
	namespace fantasy
	{
		static std::uint16_t to565(std::uint8_t r, std::uint8_t g, std::uint8_t b)
		{
			return (std::uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
		}
		static void from565(std::uint16_t c, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b)
		{
			r = (std::uint8_t)((c >> 11) & 0x1F); r = (std::uint8_t)((r << 3) | (r >> 2));
			g = (std::uint8_t)((c >> 5) & 0x3F);  g = (std::uint8_t)((g << 2) | (g >> 4));
			b = (std::uint8_t)(c & 0x1F);         b = (std::uint8_t)((b << 3) | (b >> 2));
		}

		std::vector<std::uint8_t> bc1_encode(const std::vector<rgba>& flat, uint w, uint h)
		{
			std::vector<std::uint8_t> out;
			if (w == 0 || h == 0 || flat.size() < (size_t)w * h) return out;
			uint bw = (w + 3) / 4, bh = (h + 3) / 4;
			out.resize((size_t)bw * bh * 8, 0);

			std::uint8_t r[16], g[16], b[16];
			for (uint by = 0; by < bh; ++by)
			{
				for (uint bx = 0; bx < bw; ++bx)
				{
					// 收集 16 像素（越界按边缘重复）
					for (int i = 0; i < 16; ++i)
					{
						int x = (int)bx * 4 + (i & 3);
						int y = (int)by * 4 + (i >> 2);
						if (x >= (int)w) x = (int)w - 1;
						if (y >= (int)h) y = (int)h - 1;
						rgba px = flat[(size_t)y * w + (size_t)x];
						r[i] = xhqm_rgba_r(px);
						g[i] = xhqm_rgba_g(px);
						b[i] = xhqm_rgba_b(px);
					}

					std::uint8_t minr = 255, ming = 255, minb = 255, maxr = 0, maxg = 0, maxb = 0;
					for (int i = 0; i < 16; ++i)
					{
						if (r[i] < minr) minr = r[i]; if (r[i] > maxr) maxr = r[i];
						if (g[i] < ming) ming = g[i]; if (g[i] > maxg) maxg = g[i];
						if (b[i] < minb) minb = b[i]; if (b[i] > maxb) maxb = b[i];
					}

					std::uint16_t c0 = to565(maxr, maxg, maxb);
					std::uint16_t c1 = to565(minr, ming, minb);

					std::uint8_t c0r, c0g, c0b, c1r, c1g, c1b;
					from565(c0, c0r, c0g, c0b);
					from565(c1, c1r, c1g, c1b);

					int pal_r[4], pal_g[4], pal_b[4];
					bool four = (c0 > c1);
					pal_r[0] = c0r; pal_g[0] = c0g; pal_b[0] = c0b;
					pal_r[1] = c1r; pal_g[1] = c1g; pal_b[1] = c1b;
					if (four)
					{
						pal_r[2] = (2 * c0r + c1r) / 3; pal_g[2] = (2 * c0g + c1g) / 3; pal_b[2] = (2 * c0b + c1b) / 3;
						pal_r[3] = (c0r + 2 * c1r) / 3; pal_g[3] = (c0g + 2 * c1g) / 3; pal_b[3] = (c0b + 2 * c1b) / 3;
					}
					else
					{
						pal_r[2] = (c0r + c1r) / 2; pal_g[2] = (c0g + c1g) / 2; pal_b[2] = (c0b + c1b) / 2;
						pal_r[3] = 0; pal_g[3] = 0; pal_b[3] = 0;   // 透明（未用）
					}

					std::uint32_t indices = 0;
					for (int i = 0; i < 16; ++i)
					{
						int best = 0, bestd = 0x7fffffff;
						int ncolors = four ? 4 : 3;
						for (int k = 0; k < ncolors; ++k)
						{
							int dr = (int)r[i] - pal_r[k], dg = (int)g[i] - pal_g[k], db = (int)b[i] - pal_b[k];
							int d = dr * dr + dg * dg + db * db;
							if (d < bestd) { bestd = d; best = k; }
						}
						indices |= (std::uint32_t)best << (i * 2);
					}

					std::uint8_t* block = out.data() + ((size_t)by * bw + bx) * 8;
					block[0] = (std::uint8_t)(c0 & 0xFF);
					block[1] = (std::uint8_t)(c0 >> 8);
					block[2] = (std::uint8_t)(c1 & 0xFF);
					block[3] = (std::uint8_t)(c1 >> 8);
					block[4] = (std::uint8_t)(indices & 0xFF);
					block[5] = (std::uint8_t)((indices >> 8) & 0xFF);
					block[6] = (std::uint8_t)((indices >> 16) & 0xFF);
					block[7] = (std::uint8_t)((indices >> 24) & 0xFF);
				}
			}
			return out;
		}

		std::vector<rgba> bc1_decode(const std::vector<std::uint8_t>& bc1, uint w, uint h)
		{
			std::vector<rgba> out;
			if (w == 0 || h == 0) return out;
			uint bw = (w + 3) / 4, bh = (h + 3) / 4;
			if (bc1.size() < (size_t)bw * bh * 8) return out;
			out.resize((size_t)w * h, 0xff000000);

			for (uint by = 0; by < bh; ++by)
			{
				for (uint bx = 0; bx < bw; ++bx)
				{
					const std::uint8_t* block = bc1.data() + ((size_t)by * bw + bx) * 8;
					std::uint16_t c0 = (std::uint16_t)(block[0] | (block[1] << 8));
					std::uint16_t c1 = (std::uint16_t)(block[2] | (block[3] << 8));
					std::uint8_t c0r, c0g, c0b, c1r, c1g, c1b;
					from565(c0, c0r, c0g, c0b);
					from565(c1, c1r, c1g, c1b);
					int pal_r[4], pal_g[4], pal_b[4];
					bool four = (c0 > c1);
					pal_r[0] = c0r; pal_g[0] = c0g; pal_b[0] = c0b;
					pal_r[1] = c1r; pal_g[1] = c1g; pal_b[1] = c1b;
					if (four)
					{
						pal_r[2] = (2 * c0r + c1r) / 3; pal_g[2] = (2 * c0g + c1g) / 3; pal_b[2] = (2 * c0b + c1b) / 3;
						pal_r[3] = (c0r + 2 * c1r) / 3; pal_g[3] = (c0g + 2 * c1g) / 3; pal_b[3] = (c0b + 2 * c1b) / 3;
					}
					else
					{
						pal_r[2] = (c0r + c1r) / 2; pal_g[2] = (c0g + c1g) / 2; pal_b[2] = (c0b + c1b) / 2;
						pal_r[3] = 0; pal_g[3] = 0; pal_b[3] = 0;
					}

					std::uint32_t indices = (std::uint32_t)(block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24));
					for (int i = 0; i < 16; ++i)
					{
						int x = (int)bx * 4 + (i & 3);
						int y = (int)by * 4 + (i >> 2);
						if (x >= (int)w || y >= (int)h) continue;
						int idx = (int)((indices >> (i * 2)) & 3);
						rgba px = xhqm_rgba(pal_r[idx], pal_g[idx], pal_b[idx], 255);
						out[(size_t)y * w + (size_t)x] = px;
					}
				}
			}
			return out;
		}
	}
}
