#include "colors.h"

namespace xhqm
{
	bool color::is_good(const std::array<xhqm::real, 4>& input)
	{
		if (input[0] < 0.f || input[0] > 1.f) return false;
		if (input[1] < 0.f || input[1] > 1.f) return false;
		if (input[2] < 0.f || input[2] > 1.f) return false;
		if (input[3] < 0.f || input[3] > 1.f) return false;
		return true;
	}

	std::array<xhqm::real, 4> color::rgba2array(const xhqm::rgba& input)
	{
		std::array<xhqm::real, 4> output = { 0,0,0,0 };
		output[0] = xhqm_rgba_r(input) / 255.f;
		output[1] = xhqm_rgba_g(input) / 255.f;
		output[2] = xhqm_rgba_b(input) / 255.f;
		output[3] = xhqm_rgba_a(input) / 255.f;
		return output;
	}

	xhqm::rgba color::array2rgba(const std::array<xhqm::real, 4>& input)
	{
		xhqm::rgba output = 0;
		if (is_good(input))
		{
			xhqm_rgba_r(output) = static_cast<xhqm::byte>(255 * input[0]);
			xhqm_rgba_g(output) = static_cast<xhqm::byte>(255 * input[1]);
			xhqm_rgba_b(output) = static_cast<xhqm::byte>(255 * input[2]);
			xhqm_rgba_a(output) = static_cast<xhqm::byte>(255 * input[3]);
		}
		return output;
	}

	xhqm::cmyk color::rgba2cmyk(const xhqm::rgba& input)
	{
		xhqm::cmyk output = { 0,0,0,1 };
		auto rgba = rgba2array(input);
		output[3] = 1.f - std::max(rgba[0], std::max(rgba[1], rgba[2]));
		const xhqm::real denom = 1.f - output[3];
		if (denom <= 1e-6f) return output;   // K==1 纯黑，分母为 0，C=M=Y=0
		output[2] = 1.f - (rgba[2] / denom);
		output[1] = 1.f - (rgba[1] / denom);
		output[0] = 1.f - (rgba[0] / denom);
		return output;
	}

	xhqm::rgba color::cmyk2rgba(const xhqm::cmyk& input)
	{
		xhqm::rgba output = 0;
		if (is_good(input))
		{
			xhqm_rgba_r(output) = static_cast<xhqm::byte>(255 * (1.f - input[0]) * (1.f - input[3]));
			xhqm_rgba_g(output) = static_cast<xhqm::byte>(255 * (1.f - input[1]) * (1.f - input[3]));
			xhqm_rgba_b(output) = static_cast<xhqm::byte>(255 * (1.f - input[2]) * (1.f - input[3]));
			xhqm_rgba_a(output) = 255;
		}
		return output;
	}

	xhqm::hlsa color::rgba2hlsa(const xhqm::rgba& input)
	{
		xhqm::hlsa output = { 0, 0, 0, 1 };
		auto rgba = rgba2array(input);

		xhqm::real max = std::max({ rgba[0], rgba[1], rgba[2] });
		xhqm::real min = std::min({ rgba[0], rgba[1], rgba[2] });
		xhqm::real delta = max - min;

		// 计算亮度 (0-100)
		output[1] = (max + min) / 2 * 100;

		// 处理灰度情况
		if (delta < 1e-6) {
			output[0] = 0; // H 设为0（无意义）
			output[2] = 0; // S 设为0
			return output;
		}

		// 计算色相 (0-360)
		if (max == rgba[0]) {
			output[0] = 60 * (std::fmod(((rgba[1] - rgba[2]) / delta), 6.f));
		}
		else if (max == rgba[1]) {
			output[0] = 60 * ((rgba[2] - rgba[0]) / delta + 2);
		}
		else if (max == rgba[2]) {
			output[0] = 60 * ((rgba[0] - rgba[1]) / delta + 4);
		}

		if (output[0] < 0) output[0] += 360;

		// 计算饱和度 (0-100)；分母近 0（L≈0/1 纯黑纯白）时饱和度为 0，并钳上界防浮点爆大
		const xhqm::real denom = 1.f - std::abs(2 * (output[1] / 100) - 1);
		output[2] = (denom < 1e-6f) ? 0.f : std::min(100.f, (delta / denom) * 100);

		// 透明度保存不变
		output[3] = rgba[3];
		return output;
	}

	xhqm::rgba color::hlsa2rgba(const xhqm::hlsa& input)
	{
		xhqm::rgba output = 0;

		if (input[0] < 0.f || input[0] > 360.f) return output;
		if (input[1] < 0.f || input[1] > 100.f) return output;
		if (input[2] < 0.f || input[2] > 100.f) return output;
		if (input[3] < 0.f || input[3] > 1.f) return output;

		xhqm::real h = input[0];
		xhqm::real l = input[1] / 100;
		xhqm::real s = input[2] / 100;
		xhqm::real a = input[3];

		// 如果饱和度为0，直接返回灰色
		if (s < 1e-6)
		{
			xhqm_rgba_r(output) = static_cast<xhqm::byte>(255 * l);
			xhqm_rgba_g(output) = static_cast<xhqm::byte>(255 * l);
			xhqm_rgba_b(output) = static_cast<xhqm::byte>(255 * l);
			xhqm_rgba_a(output) = static_cast<xhqm::byte>(255 * a);
			return output;
		}

		xhqm::real c = (1 - std::abs(2 * l - 1)) * s;
		xhqm::real x = c * (1 - std::abs(std::fmod(h / 60, 2.f) - 1));
		xhqm::real m = l - c / 2;

		xhqm::real r = 0, g = 0, b = 0;
		if (h < 60) { r = c; g = x; b = 0; }
		else if (h < 120) { r = x; g = c; b = 0; }
		else if (h < 180) { r = 0; g = c; b = x; }
		else if (h < 240) { r = 0; g = x; b = c; }
		else if (h < 300) { r = x; g = 0; b = c; }
		else if (h < 360) { r = c; g = 0; b = x; }
		else { r = c; g = 0; b = x; }

		xhqm_rgba_r(output) = static_cast<xhqm::byte>(255 * (r + m));
		xhqm_rgba_g(output) = static_cast<xhqm::byte>(255 * (g + m));
		xhqm_rgba_b(output) = static_cast<xhqm::byte>(255 * (b + m));
		xhqm_rgba_a(output) = static_cast<xhqm::byte>(255 * a);
		return output;
	}

	xhqm::hvsa color::rgba2hvsa(const xhqm::rgba& input)
	{
		xhqm::hlsa output = { 0, 0, 0, 1 };
		auto rgba = rgba2array(input);

		xhqm::real max = std::max({ rgba[0], rgba[1], rgba[2] });
		xhqm::real min = std::min({ rgba[0], rgba[1], rgba[2] });
		xhqm::real delta = max - min;

		// 计算明度 (0-100)
		output[1] = max * 100;

		// 处理灰度情况
		if (delta < 1e-6) {
			output[0] = 0; // H 设为0（无意义）
			output[2] = 0; // S 设为0
			return output;
		}

		// 计算色相 (0-360)
		if (max == rgba[0]) {
			output[0] = 60 * (std::fmod(((rgba[1] - rgba[2]) / delta), 6.f));
		}
		else if (max == rgba[1]) {
			output[0] = 60 * ((rgba[2] - rgba[0]) / delta + 2);
		}
		else if (max == rgba[2]) {
			output[0] = 60 * ((rgba[0] - rgba[1]) / delta + 4);
		}

		if (output[0] < 0) output[0] += 360;

		// 计算饱和度 (0-100)
		output[2] = (delta / max) * 100;

		// 透明度保存不变
		output[3] = rgba[3];
		return output;
	}

	xhqm::rgba color::hvsa2rgba(const xhqm::hvsa& input)
	{
		xhqm::rgba output = 0;

		if (input[0] < 0.f || input[0] > 360.f) return output;
		if (input[1] < 0.f || input[1] > 100.f) return output;
		if (input[2] < 0.f || input[2] > 100.f) return output;
		if (input[3] < 0.f || input[3] > 1.f) return output;

		xhqm::real h = input[0];
		xhqm::real v = input[1] / 100;
		xhqm::real s = input[2] / 100;
		xhqm::real a = input[3];

		// 如果饱和度为0，直接返回灰色
		if (s < 1e-6)
		{
			xhqm_rgba_r(output) = static_cast<xhqm::byte>(255 * v);
			xhqm_rgba_g(output) = static_cast<xhqm::byte>(255 * v);
			xhqm_rgba_b(output) = static_cast<xhqm::byte>(255 * v);
			xhqm_rgba_a(output) = static_cast<xhqm::byte>(255 * a);
			return output;
		}

		xhqm::real c = v * s;
		xhqm::real x = c * (1 - std::abs(std::fmod(h / 60, 2.f) - 1));
		xhqm::real m = v - c;

		xhqm::real r = 0, g = 0, b = 0;
		if (h < 60) { r = c; g = x; b = 0; }
		else if (h < 120) { r = x; g = c; b = 0; }
		else if (h < 180) { r = 0; g = c; b = x; }
		else if (h < 240) { r = 0; g = x; b = c; }
		else if (h < 300) { r = x; g = 0; b = c; }
		else if (h < 360) { r = c; g = 0; b = x; }
		else { r = c; g = 0; b = x; }

		xhqm_rgba_r(output) = static_cast<xhqm::byte>(255 * (r + m));
		xhqm_rgba_g(output) = static_cast<xhqm::byte>(255 * (g + m));
		xhqm_rgba_b(output) = static_cast<xhqm::byte>(255 * (b + m));
		xhqm_rgba_a(output) = static_cast<xhqm::byte>(255 * a);
		return output;
	}


	bool color::assignment(xhqm::real value, const std::vector<xhqm::color>& colors, xhqm::color& color)
	{
		if (colors.empty())
			return false;
		else if (colors.size() == 1)
			color = colors[0];
		else if (colors.size() >= 1)
		{
			//value 0.f--1.f
			value = std::max(0.f, value);
			value = std::min(value, 1.f);
			value = value * colors.size();
			xhqm::size ind1 = static_cast<xhqm::size>(value);
			if (ind1 >= colors.size()) ind1 = colors.size() - 1;   // value==1.0 钳到末元素，防越界
			xhqm::size ind2 = ind1 + 1 < colors.size() ? ind1 + 1 : ind1;

			value = value - ind1;
			color.m_color_value[0] = (1.f - value) * colors[ind1].m_color_value[0] + value * colors[ind2].m_color_value[0];
			color.m_color_value[1] = (1.f - value) * colors[ind1].m_color_value[1] + value * colors[ind2].m_color_value[1];
			color.m_color_value[2] = (1.f - value) * colors[ind1].m_color_value[2] + value * colors[ind2].m_color_value[2];
			color.m_color_value[3] = (1.f - value) * colors[ind1].m_color_value[3] + value * colors[ind2].m_color_value[3];
		}
		return true;
	}

	std::vector<xhqm::color> color::assignment(xhqm::size size, std::vector<xhqm::color> colors)
	{
		std::vector<xhqm::color> asscolors;
		if (colors.size())
		{
			xhqm::real inte = 1.f / size;
			asscolors.resize(size, xhqm::color(xhqm::rgba_color::transparent));
			for (xhqm::size i = 0; i < size; ++i) { assignment(inte * i, colors, asscolors[i]); }
		}
		return asscolors;
	}

	xhqm::rgba color::to_rgba() const
	{
		xhqm::rgba value = 0;
		switch (m_color_type)
		{
		case xhqm::color::color_hlsa: value = xhqm::color::hlsa2rgba(m_color_value); break;
		case xhqm::color::color_hvsa: value = xhqm::color::hvsa2rgba(m_color_value); break;
		case xhqm::color::color_cmyk: value = xhqm::color::cmyk2rgba(m_color_value); break;
		default: value = xhqm::color::array2rgba(m_color_value); break;
		}
		return value;
	}

	void color::from_rgba(const xhqm::rgba& value)
	{
		switch (m_color_type)
		{
		case xhqm::color::color_hlsa: m_color_value = xhqm::color::rgba2hlsa(value); break;
		case xhqm::color::color_hvsa: m_color_value = xhqm::color::rgba2hvsa(value); break;
		case xhqm::color::color_cmyk: m_color_value = xhqm::color::rgba2cmyk(value); break;
		default: m_color_value = xhqm::color::rgba2array(value); break;
		}
	}

	void color::invert_type(const xhqm::color::type& type)
	{
		xhqm::rgba value = to_rgba();
		m_color_type = type;
		from_rgba(value);
	}

}
