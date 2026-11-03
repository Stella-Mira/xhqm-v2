#include "ctype.h"

namespace xhqm
{
	std::vector<std::string> ctype::type_complex(std::string string)
	{
		std::vector<std::string> result;

		//预处理
		xhqm::index index = 0;
		while ((index = string.find(' ', index)) != string::npos)
		{
			string.erase(index, 1);
		}
		//类型判定
		std::size_t pos = string.find('<');
		if (pos != string::npos)
		{
			std::string type_name = string.substr(0, pos);
			if (type_name == "array" || type_name == "tuple" || type_name == "shape")
			{
				int closing_depth = 1;
				std::size_t closing_pos = std::string::npos;
				for (std::size_t i = pos + 1; i < string.size(); ++i)
				{
					if ('<' != string[i] && string[i] != '>') {}
					else if (string[i] == '<') { closing_depth++; }
					else if (string[i] == '>') { closing_depth--; }

					if (closing_depth == 0) { closing_pos = i; }
				}
				if (closing_pos != std::string::npos && closing_pos > pos)
				{
					string = string.substr(pos + 1, closing_pos - pos - 1);
					result.push_back(type_name);
				}
			}
		}
		//类型拆分
		if (result.size() == 1)
		{
			int depth = 0;
			std::string current;
			for (const char& c : string)
			{
				if ('<' != c && c != '>') {}
				else if (c == '<') { depth++; }
				else if (c == '>') { depth--; }

				if (c == ',' && depth == 0)
				{
					result.push_back(current);
					current.clear();
				}
				else { current += c; }
			}
			if (!current.empty())
			{
				result.push_back(current);
			}
		}

		return result;
	}
	xhqm::ctype::type ctype::get_type(const xhqm::text& type)
	{
		if (type == "null") return xhqm::ctype::null;
		else if (type == "text") return xhqm::ctype::text;
		else if (type == "bool") return xhqm::ctype::bool_1;
		else if (type == "writ_8") return xhqm::ctype::writ_8;
		else if (type == "writ_16") return xhqm::ctype::writ_16;

		else if (type == "uint_8") return xhqm::ctype::uint_8;
		else if (type == "uint_16") return xhqm::ctype::uint_16;
		else if (type == "uint_32") return xhqm::ctype::uint_32;
		else if (type == "uint_64") return xhqm::ctype::uint_64;

		else if (type == "sint_8") return xhqm::ctype::sint_8;
		else if (type == "sint_16") return xhqm::ctype::sint_16;
		else if (type == "sint_32") return xhqm::ctype::sint_32;
		else if (type == "sint_64") return xhqm::ctype::sint_64;

		else if (type == "real_16") return xhqm::ctype::real_16;
		else if (type == "real_32") return xhqm::ctype::real_32;
		else if (type == "real_64") return xhqm::ctype::real_64;
		else if (type == "real_128") return xhqm::ctype::real_128;

		//else if (type == "array") return xhqm::ctype::array;
		//else if (type == "tuple") return xhqm::ctype::tuple;
		else if (type.find("array") == 0) return xhqm::ctype::array;
		else if (type.find("tuple") == 0) return xhqm::ctype::tuple;

		return xhqm::ctype::null;
	}
	xhqm::text ctype::get_text(const xhqm::ctype::type& type)
	{
		if (type == xhqm::ctype::null) return "null";
		else if (type == xhqm::ctype::text) return "text";
		else if (type == xhqm::ctype::bool_1) return "bool";
		else if (type == xhqm::ctype::writ_8) return "writ_8";
		else if (type == xhqm::ctype::writ_16) return "writ_16";

		else if (type == xhqm::ctype::uint_8) return "uint_8";
		else if (type == xhqm::ctype::uint_16) return "uint_16";
		else if (type == xhqm::ctype::uint_32) return "uint_32";
		else if (type == xhqm::ctype::uint_64) return "uint_64";

		else if (type == xhqm::ctype::sint_8) return "sint_8";
		else if (type == xhqm::ctype::sint_16) return "sint_16";
		else if (type == xhqm::ctype::sint_32) return "sint_32";
		else if (type == xhqm::ctype::sint_64) return "sint_64";

		else if (type == xhqm::ctype::real_16) return "real_16";
		else if (type == xhqm::ctype::real_32) return "real_32";
		else if (type == xhqm::ctype::real_64) return "real_64";
		else if (type == xhqm::ctype::real_128) return "real_128";

		else if (type == xhqm::ctype::array) return "array";
		else if (type == xhqm::ctype::tuple) return "tuple";

		return "null";
	}
	xhqm::byte ctype::get_size(const xhqm::ctype::type& type)
	{
		if (type == xhqm::ctype::null) return 0;
		else if (type == xhqm::ctype::bool_1) return 1;
		else if (type == xhqm::ctype::writ_8) return 1;
		else if (type == xhqm::ctype::writ_16) return 2;

		else if (type == xhqm::ctype::uint_8) return 1;
		else if (type == xhqm::ctype::uint_16) return 2;
		else if (type == xhqm::ctype::uint_32) return 4;
		else if (type == xhqm::ctype::uint_64) return 8;

		else if (type == xhqm::ctype::sint_8) return 1;
		else if (type == xhqm::ctype::sint_16) return 2;
		else if (type == xhqm::ctype::sint_32) return 4;
		else if (type == xhqm::ctype::sint_64) return 8;

		else if (type == xhqm::ctype::real_16) return 2;
		else if (type == xhqm::ctype::real_32) return 4;
		else if (type == xhqm::ctype::real_64) return 8;
	  //else if (type == xhqm::ctype::real_128) return 16;
		else if (type == xhqm::ctype::real_128) return sizeof(xhqm::fld);

		else if (type == xhqm::ctype::array) return sizeof(xhqm::arr);
		else if (type == xhqm::ctype::tuple) return sizeof(xhqm::tup);

		return 0;
	}
	
	
	arr::arr(const std::string& array_string)
	{
		std::vector<std::string> type_list = ctype::type_complex(array_string);
		xhqm::count count = type_list.size();
		if (count < 3)							throw; //类型错误
		else if ("tuple" == type_list.front())  throw; //类型错误
		else if ("array" == type_list.front())//throw; //类型错误
		{
			for (xhqm::index i = 1; i < count; ++i)
			{
				if (i + 1 == count)
				{
					m_type = ctype::get_type(type_list[i]);
				}
				else
				{
					m_array.push_back(std::stol(type_list[i]));
				}
			}

			count = this->size();//对象个数
			xhqm::size size = ctype::get_size(m_type);//对象大小
			m_block.init(count * size); //初始化大小
			if (m_block.good())
			{
				m_string = array_string;
				if (m_type == ctype::array)
				{
					for (xhqm::index i = 0; i < count; ++i)
					{
						new(m_block.value_ptr(i * size)) xhqm::arr(type_list.back());//构造
					}
				}
				if (m_type == ctype::tuple)
				{
					for (xhqm::index i = 0; i < count; ++i)
					{
						new(m_block.value_ptr(i * size)) xhqm::tup(type_list.back());//构造
					}
				}
			}
		}
	}

	arr::~arr()
	{
		if (m_block.good())
		{
			xhqm::count count = this->size();//对象个数
			xhqm::size size = ctype::get_size(m_type);//对象大小

			if (m_type == ctype::array)
			{
				for (xhqm::index i = 0; i < count; ++i)
				{
					m_block.value_ptr<xhqm::arr>(i * size)->~arr();//析构
				}
			}
			if (m_type == ctype::tuple)
			{
				for (xhqm::index i = 0; i < count; ++i)
				{
					m_block.value_ptr<xhqm::tup>(i * size)->~tup();//析构
				}
			}
		}
		m_block.free();
	}

	tup::tup(const std::string& tuple_string)
	{
		std::vector<std::string> type_list = ctype::type_complex(tuple_string);
		xhqm::count count = type_list.size();
		if (count < 2)							throw; //类型错误
		else if ("array" == type_list.front())  throw; //类型错误
		else if ("tuple" == type_list.front())//throw; //类型错误
		{
			xhqm::size size = 0;//对象大小
			for (xhqm::index i = 1; i < count; ++i)
			{
				auto temp_type = ctype::get_type(type_list[i]);
				size += ctype::get_size(temp_type);
				m_tuple.push_back(temp_type);
			}

			m_block.init(size); //初始化大小
			if (m_block.good())
			{
				xhqm::size pos = 0;
				m_string = tuple_string;
				for (xhqm::index i = 1; i < count; ++i)
				{
					auto temp_type = m_tuple[i - 1];
					if (temp_type == ctype::array)
					{
						new(m_block.value_ptr(pos)) xhqm::arr(type_list[i]);//构造
					}
					if (temp_type == ctype::tuple)
					{
						new(m_block.value_ptr(pos)) xhqm::tup(type_list[i]);//构造
					}
					pos += ctype::get_size(temp_type);
				}
			}
		}
	}

	tup::~tup()
	{
		if (m_block.good())
		{
			xhqm::size pos = 0;
			xhqm::count count = this->size();//对象个数
			for (xhqm::index i = 0; i < count; ++i)
			{
				auto temp_type = m_tuple[i];
				if (temp_type == ctype::array)
				{
					m_block.value_ptr<xhqm::arr>(pos)->~arr();//析构
				}
				if (temp_type == ctype::tuple)
				{
					m_block.value_ptr<xhqm::tup>(pos)->~tup();//析构
				}
				pos += ctype::get_size(temp_type);
			}
		}
		m_block.free();
	}

}