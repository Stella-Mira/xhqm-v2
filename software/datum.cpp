#include "datum.h"
#include <stack>
#include <array>
#include <vector>
#include <string>
#include <cctype>
#include <fstream>
#include <iostream>
#include <algorithm>

namespace xhqm
{
	std::vector<std::string> _split_json_object_(const std::string& jsonStr)
	{
		auto trim = [](const std::string& text) ->std::string
		{
			size_t pos_s = text.find_first_not_of(" \t\n\r\f\v");
			size_t pos_e = text.find_last_not_of(" \t\n\r\f\v");
			if (pos_s == std::string::npos || pos_e == std::string::npos)
			{
				return "";
			}
			return text.substr(pos_s, 1 + pos_e - pos_s);
		};

		std::vector<std::string> result;
		std::string trimmedStr = trim(jsonStr);

		if (trimmedStr.empty())
		{
			return result;
		}

		if (trimmedStr.front() == '{' && trimmedStr.back() == '}') {
			trimmedStr = trimmedStr.substr(1, trimmedStr.size() - 2);
			trimmedStr = trim(trimmedStr);
		}

		if (trimmedStr.empty())
		{
			return result;
		}

		std::stack<char> bracketStack;
		bool inString = false;
		bool escapeNext = false;
		size_t start = 0;
		size_t n = trimmedStr.size();

		for (size_t i = 0; i < n; ++i) {
			char c = trimmedStr[i];
			if (inString) {
				if (escapeNext) {
					escapeNext = false;
				}
				else {
					if (c == '\\') {
						escapeNext = true;
					}
					else if (c == '"') {
						inString = false;
					}
				}
			}
			else {
				if (c == '"') {
					inString = true;
					escapeNext = false;
				}
				else {
					if (c == '{' || c == '[') {
						bracketStack.push(c);
					}
					else if (c == '}' || c == ']') {
						if (!bracketStack.empty()) {
							bracketStack.pop();
						}
					}
					else if (c == ',' && bracketStack.empty()) {
						std::string field = trimmedStr.substr(start, i - start);
						field = trim(field);
						if (!field.empty()) {
							result.push_back(field);
						}
						start = i + 1;
					}
				}
			}
		}

		std::string lastField = trimmedStr.substr(start);
		lastField = trim(lastField);
		if (!lastField.empty())
		{
			result.push_back(lastField);
		}

		return result;
	}
	std::vector<std::string> _split_json_array_(const std::string& jsonStr)
	{
		std::vector<std::string> result;
		std::string trimmed = jsonStr;

		// 去除首尾空格
		auto start = trimmed.find_first_not_of(" \t\n\r\f\v");
		auto end = trimmed.find_last_not_of(" \t\n\r\f\v");
		if (start != std::string::npos && end != std::string::npos) {
			trimmed = trimmed.substr(start, end - start + 1);
		}

		// 检查并去除外层的方括号
		if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
			trimmed = trimmed.substr(1, trimmed.size() - 2);
			// 再次去除空格
			start = trimmed.find_first_not_of(" \t\n\r\f\v");
			end = trimmed.find_last_not_of(" \t\n\r\f\v");
			if (start != std::string::npos && end != std::string::npos) {
				trimmed = trimmed.substr(start, end - start + 1);
			}
		}

		// 如果数组为空，直接返回空结果
		if (trimmed.empty()) {
			return result;
		}

		// 解析数组元素
		bool in_quotes = false;
		bool escape_next = false;
		int brace_count = 0;
		int bracket_count = 0;
		size_t element_start = 0;

		for (size_t i = 0; i < trimmed.size(); ++i) {
			char c = trimmed[i];

			if (escape_next) {
				escape_next = false;
				continue;
			}

			if (c == '\\') {
				escape_next = true;
				continue;
			}

			if (c == '"' && !in_quotes) {
				in_quotes = true;
			}
			else if (c == '"' && in_quotes) {
				in_quotes = false;
			}

			if (!in_quotes) {
				if (c == '{') brace_count++;
				else if (c == '}') brace_count--;
				else if (c == '[') bracket_count++;
				else if (c == ']') bracket_count--;

				// 当不在任何嵌套结构中且遇到逗号时，分割元素
				if (c == ',' && brace_count == 0 && bracket_count == 0) {
					std::string element = trimmed.substr(element_start, i - element_start);

					// 去除元素首尾空格
					auto elem_start = element.find_first_not_of(" \t\n\r\f\v");
					auto elem_end = element.find_last_not_of(" \t\n\r\f\v");
					if (elem_start != std::string::npos && elem_end != std::string::npos) {
						element = element.substr(elem_start, elem_end - elem_start + 1);
						result.push_back(element);
					}

					element_start = i + 1;
				}
			}
		}

		// 处理最后一个元素
		if (element_start < trimmed.size()) {
			std::string element = trimmed.substr(element_start);

			// 去除元素首尾空格
			auto elem_start = element.find_first_not_of(" \t\n\r\f\v");
			auto elem_end = element.find_last_not_of(" \t\n\r\f\v");
			if (elem_start != std::string::npos && elem_end != std::string::npos) {
				element = element.substr(elem_start, elem_end - elem_start + 1);
				result.push_back(element);
			}
		}

		return result;
	}
	std::array<std::string, 2> _split_json_value_(const std::string& jsonStr) {

		// 去除首尾空格
		auto trim = [](const std::string& text) ->std::string
		{
			size_t pos_s = text.find_first_not_of(" \t\n\r\f\v");
			size_t pos_e = text.find_last_not_of(" \t\n\r\f\v");
			if (pos_s == std::string::npos || pos_e == std::string::npos)
			{
				return "";
			}
			return text.substr(pos_s, 1 + pos_e - pos_s);
		};

		std::array<std::string, 2> result;
		std::string trimmed = trim(jsonStr);

		// 查找第一个冒号的位置（不在引号或嵌套结构中）
		size_t colon_pos = std::string::npos;
		bool in_quotes = false;
		int brace_count = 0;
		int bracket_count = 0;
		bool escape_next = false;

		for (size_t i = 0; i < trimmed.size(); ++i) {
			char c = trimmed[i];

			if (escape_next) {
				escape_next = false;
				continue;
			}

			if (c == '\\') {
				escape_next = true;
				continue;
			}

			if (c == '"' && !in_quotes) {
				in_quotes = true;
			}
			else if (c == '"' && in_quotes) {
				in_quotes = false;
			}

			if (!in_quotes) {
				if (c == '{') brace_count++;
				else if (c == '}') brace_count--;
				else if (c == '[') bracket_count++;
				else if (c == ']') bracket_count--;

				if (c == ':' && brace_count == 0 && bracket_count == 0) {
					colon_pos = i;
					break;
				}
			}
		}
		// 提取键和值
		if (colon_pos == std::string::npos)
		{
			return result;
		}
		result[0] = trim(trimmed.substr(0, colon_pos));
		result[1] = trim(trimmed.substr(colon_pos + 1));
		return result;
	}

	bool datum::save_json(const xhqm::datum*& data_ptr, std::string& jspn_str)
	{
		jspn_str = "";
		if (data_ptr)
		{
			jspn_str.push_back('{');
			for (auto& data_ref : data_ptr->_datum_)
			{
				std::string value = "";
				value.push_back('\"');
				value += data_ref.m_name;
				value.push_back('\"');
				value += ":";

				std::string json_value = "";
				if (data_ref.m_sptr(data_ref.m_dptr, json_value))
				{
					value += json_value;
				}

				value.push_back(',');
				jspn_str += value;
			}

			if (jspn_str.back() == ',')
			{
				jspn_str.back() = '}';
			}
			else
			{
				jspn_str.push_back('}');
			}
			return true;
		}
		return false;
	}

	bool datum::read_json(const std::string& jspn_str, xhqm::datum*& data_ptr)
	{
		if (data_ptr)
		{
			std::vector<std::string> _datum_json = _split_json_object_(jspn_str);
			if (_datum_json.size() != data_ptr->_datum_.size()) { return false; }

			xhqm::count count = _datum_json.size();
			for (xhqm::index index = 0; index < count; ++index)
			{
				auto data_key = _split_json_value_(_datum_json[index]);
				auto& data_ref = data_ptr->_datum_[index];

				if (data_key[0] == "\"" + data_ref.m_name + "\"")
				{ 
					if (!data_ref.m_rptr(data_key[1], data_ref.m_dptr))
					{
						return false;
					}
				}
			}
			return true;
		}
		return false;
	}

	bool datum::read_file(const std::string& path, xhqm::datum& data)
	{
		std::string json_str = "";
		std::ifstream file(path);
		if (file.is_open())
		{
			while (!file.eof())
			{
				std::string line = "";
				std::getline(file, line);
				json_str += line;
			}
			file.close();
		}
		xhqm::datum* data_ptr = (xhqm::datum*)&data;
		return datum::read_json(json_str, data_ptr);
	}

	bool datum::save_file(const std::string& path, xhqm::datum& data)
	{
		std::string json_str = "";
		const xhqm::datum* data_ptr = (const xhqm::datum*)&data;
		if (xhqm::datum::save_json(data_ptr, json_str))
		{
			std::ofstream file(path);
			if (file.is_open())
			{
				file << json_str << "\n\n\n";
				file.close();
				return true;
			}
		}
		return false;
	}
}