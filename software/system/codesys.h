#pragma once
#ifndef XHQM_TOOL_CODE
#define XHQM_TOOL_CODE
#include "../../head.h"

namespace xhqm
{
	//string的编码方式为utf8
	class xhqm_io codesys
	{
	public:
		static void tosl(xhqm::byte data, xhqm::byte* txtdata);
		static void tonu(xhqm::byte* txtdata, xhqm::byte& data);
		static std::string string2hexa(const std::string str);
		static std::string hexa2string(const std::string asi);
		static bool FILE_BINtoBXT(const std::string BINpath, const std::string BXTpath);
		static bool FILE_BXTtoBIN(const std::string BXTpath, const std::string BINpath);

		static std::string unicode_to_string(const std::wstring& str);
		static std::wstring string_to_unicode(const std::string& str);
		static std::vector<std::string> split_string(const std::string& str, char tag);
		static void split_string(const std::string& str, std::string tag, std::vector<std::string>& elements);

		/**
		* @brief 将UTF-8字符串转换为系统本地编码的字符串
		* @param utf8_str UTF-8编码的输入字符串
		* @return 系统本地编码的字符串
		* @note Windows: 转换到系统代码页(CP_ACP)
		* @Linux/macOS: 返回原始UTF-8（通常系统就是UTF-8）
		*/
		static std::string utf8_to_system(const std::string& utf8_str);

		/**
		 * @brief 将系统本地编码的字符串转换为UTF-8字符串
		 * @param system_str 系统本地编码的字符串
		 * @return UTF-8编码的字符串
		 */
		static std::string system_to_utf8(const std::string& system_str);
	};
}
#endif