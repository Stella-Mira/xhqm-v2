
#include <regex>
#include <locale>
#include <codecvt>

#include "codesys.h"

#ifdef XHQM_WINDOWS
#include <Windows.h>
#pragma warning(disable : 4996)
#endif


namespace xhqm
{
	void codesys::tosl(xhqm::byte data, xhqm::byte* txtdata) {
		txtdata[0] = (data & 0xf0) >> 4;
		txtdata[1] = (data & 0x0f) >> 0;
		txtdata[0] = (txtdata[0] < 10 ? txtdata[0] + 0x30 : txtdata[0] + 0x41 - 10);
		txtdata[1] = (txtdata[1] < 10 ? txtdata[1] + 0x30 : txtdata[1] + 0x41 - 10);
		txtdata[2] = 0x00;
	}

	void codesys::tonu(xhqm::byte* txtdata, xhqm::byte& data) {
		data = 0x00;
		data |= (txtdata[1] < 0x41 ? (txtdata[1] - 0x30) << 0 : (txtdata[1] - 0x41 + 10) << 0);
		data |= (txtdata[0] < 0x41 ? (txtdata[0] - 0x30) << 4 : (txtdata[0] - 0x41 + 10) << 4);
	}

	std::string codesys::string2hexa(const std::string str) {
		std::string asistr = "";
		xhqm::byte asi[3] = { 0x30,0x30,0x00 };
		xhqm::size len = str.length();
		for (xhqm::size j = 0; j < len; j++) {
			tosl(str.c_str()[j], asi);
			asistr = asistr + std::string((char*)asi);
		}return asistr;
	}

	std::string codesys::hexa2string(const std::string asistr) {
		std::string str = "";
		xhqm::byte txtdata[4] = { 0x30,0x30,0x00,0x00 };
		xhqm::size len = asistr.length();
		for (xhqm::size j = 0; j < len; j++) {
			txtdata[0] = asistr[j++], txtdata[1] = asistr[j], txtdata[2] = 0x00;
			tonu(txtdata, txtdata[2]);
			str = str + (char)txtdata[2];
		}return str;
	}

	bool codesys::FILE_BINtoBXT(const std::string BINpath, const std::string BXTpath) {
		FILE* fil;
		FILE* bxt;
		xhqm::byte txtdata[3] = { ' ',' ','\0' };
		if ((fil = fopen(BINpath.c_str(), "rb")) != NULL) {
			bxt = fopen(BXTpath.c_str(), "w");
			do {
				tosl(fgetc(fil), txtdata);
				txtdata[2] = ' ';
				fwrite(txtdata, 2, 1, bxt);
				fgetc(fil);
				if (feof(fil) != 0)
					break;
				fseek(fil, -1, SEEK_CUR);
			} while (!feof(fil));//(true);//
			fclose(fil);
			fclose(bxt);
			return true;
		}return false;
	}

	bool codesys::FILE_BXTtoBIN(const std::string BXTpath, const std::string BINpath) {
		FILE* bxt;
		FILE* fil;
		xhqm::byte txtdata[4] = { ' ',' ','0' };
		if ((bxt = fopen(BXTpath.c_str(), "rb")) != NULL) {
			fil = fopen(BINpath.c_str(), "wb");
			do {
				txtdata[0] = fgetc(bxt);
				txtdata[1] = fgetc(bxt);
				txtdata[2] = fgetc(bxt);
				if (txtdata[0] == -1)
					break;
				tonu(txtdata, txtdata[2]);
				fputc(txtdata[2], fil);
			} while (!feof(bxt));
			fclose(fil);
			fclose(bxt);
			return true;
		}return false;
	}


	std::string codesys::unicode_to_string(const std::wstring& str)
	{
		return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(str);
	}

	std::wstring codesys::string_to_unicode(const std::string& str)
	{
		return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(str);
	}

	std::vector<std::string> codesys::split_string(const std::string& str, char tag)
	{
		std::string item;
		std::stringstream ss(str);
		std::vector<std::string> elems;
		while (std::getline(ss, item, tag))
			if (!item.empty()) elems.push_back(item);
		return elems;
	}

	void codesys::split_string(const std::string& str, std::string tag, std::vector<std::string>& elements)
	{
		std::regex reg(tag);
		std::sregex_token_iterator iter(str.begin(), str.end(), reg, -1);
		elements = std::vector<std::string>(iter, std::sregex_token_iterator());
	}

	std::string codesys::utf8_to_system(const std::string& utf8_str)
	{
		if (utf8_str.empty()) {
			return "";
		}

#ifdef XHQM_WINDOWS
		// Windows: UTF-8 -> UTF-16 -> ANSI (系统代码页)

		// 获取字符串长度
		int utf16_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, nullptr, 0);
		if (utf16_len == 0) {
			return "";
		}

		// 转换为UTF-16
		std::wstring utf16_str(utf16_len, 0);
		MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, &utf16_str[0], utf16_len);

		// 获取ANSI编码的长度
		int ansi_len = WideCharToMultiByte(CP_ACP, 0, utf16_str.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (ansi_len == 0) {
			return "";
		}

		// 转换为ANSI
		std::string ansi_str(ansi_len, 0);
		WideCharToMultiByte(CP_ACP, 0, utf16_str.c_str(), -1, &ansi_str[0], ansi_len, nullptr, nullptr);

		// 去除末尾的null字符
		ansi_str.pop_back();

		return ansi_str;

#elif defined(__APPLE__) || defined(__MACH__)
		// macOS: 通常是UTF-8，直接返回
		return utf8_str;

#elif defined(__linux__) || defined(__unix__)
		// Linux/Unix: 检查当前locale编码
		const char* locale = setlocale(LC_CTYPE, nullptr);

		if (locale != nullptr) {
			std::string locale_str(locale);

			// 如果locale是UTF-8，直接返回
			if (locale_str.find("UTF-8") != std::string::npos ||
				locale_str.find("utf-8") != std::string::npos ||
				locale_str.find("utf8") != std::string::npos) {
				return utf8_str;
			}

			// 如果locale是C或POSIX，通常是ASCII。
			// Linux 下内核/系统调用按原始字节处理路径与参数，C/POSIX locale
			// 下 pass-through 即正确语义；MSVC 的 codecvt<wchar_t,char> 构造
			// 在 libstdc++ 下不可移植（构造器 protected），故 Linux 直接原样返回。
			if (locale_str == "C" || locale_str == "POSIX") {
				return utf8_str;
			}
		}

		// 其他情况，返回原始字符串
		return utf8_str;

#else
		// 其他平台，直接返回
		return utf8_str;
#endif
	}
	std::string codesys::system_to_utf8(const std::string& system_str)
	{
		if (system_str.empty()) {
			return "";
		}

#ifdef XHQM_WINDOWS
		// Windows: ANSI -> UTF-16 -> UTF-8

		// 获取UTF-16长度
		int utf16_len = MultiByteToWideChar(CP_ACP, 0, system_str.c_str(), -1, nullptr, 0);
		if (utf16_len == 0) {
			return "";
		}

		// 转换为UTF-16
		std::wstring utf16_str(utf16_len, 0);
		MultiByteToWideChar(CP_ACP, 0, system_str.c_str(), -1, &utf16_str[0], utf16_len);

		// 获取UTF-8长度
		int utf8_len = WideCharToMultiByte(CP_UTF8, 0, utf16_str.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (utf8_len == 0) {
			return "";
		}

		// 转换为UTF-8
		std::string utf8_str(utf8_len, 0);
		WideCharToMultiByte(CP_UTF8, 0, utf16_str.c_str(), -1, &utf8_str[0], utf8_len, nullptr, nullptr);

		// 去除末尾的null字符
		utf8_str.pop_back();

		return utf8_str;

#else
		// 非Windows平台，假设系统编码是UTF-8
		return system_str;
#endif
	}
}
