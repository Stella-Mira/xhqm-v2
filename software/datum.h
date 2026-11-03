#ifndef XHQM_SOFTWARE_DATUM
#define XHQM_SOFTWARE_DATUM

#include <sstream>
#include <fstream>
#include <functional>
#include "../algebra/value.h"
#include "../algebra/itype.h"
#include "../algebra/ctype.h"

namespace xhqm
{
	std::vector<std::string> xhqm_io _split_json_object_(const std::string& jsonStr);
	std::vector<std::string> xhqm_io _split_json_array_(const std::string& jsonStr);
	std::array<std::string, 2> xhqm_io _split_json_value_(const std::string& jsonStr);

	class xhqm_io datum;
	struct xhqm_io datum_data_
	{
		enum dtyp
		{
			null_t = 0,
			bool_t,
			text_t,
			file_t,

			uint_08,
			uint_16,
			uint_32,
			uint_64,

			sint_08,
			sint_16,
			sint_32,
			sint_64,

			real_16,
			real_32,
			real_64,
			real_ld,

			datum_t,
		};
		using dptr = void*;
		using text = std::string;
		using sptr = std::function<bool(const dptr, text&)>;
		using rptr = std::function<bool(const text&, dptr)>;
		datum_data_::text m_name = "";
		datum_data_::dtyp m_dtyp = null_t;
		datum_data_::dptr m_dptr = nullptr;
		datum_data_::sptr m_sptr = nullptr;
		datum_data_::rptr m_rptr = nullptr;

		void operator =(const datum_data_& copy) noexcept
		{
			m_name = copy.m_name;
			m_dtyp = copy.m_dtyp;
			m_dptr = copy.m_dptr;
			m_sptr = copy.m_sptr;
			m_rptr = copy.m_rptr;
		}

		bool operator ==(const datum_data_::text& name) noexcept
		{
			return (m_name == name);
		}
	};

	template<typename type> struct datum_data_v { constexpr static datum_data_::dtyp value = datum_data_::dtyp::file_t; };
	template<> struct datum_data_v<xhqm::datum> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::datum_t; };
	template<> struct datum_data_v<xhqm::u8 > { constexpr static datum_data_::dtyp value = datum_data_::dtyp::uint_08; };
	template<> struct datum_data_v<xhqm::u16> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::uint_16; };
	template<> struct datum_data_v<xhqm::u32> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::uint_32; };
	template<> struct datum_data_v<xhqm::u64> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::uint_64; };
	template<> struct datum_data_v<xhqm::i8 > { constexpr static datum_data_::dtyp value = datum_data_::dtyp::sint_08; };
	template<> struct datum_data_v<xhqm::i16> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::sint_16; };
	template<> struct datum_data_v<xhqm::i32> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::sint_32; };
	template<> struct datum_data_v<xhqm::i64> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::sint_64; };
	template<> struct datum_data_v<xhqm::f16> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::real_16; };
	template<> struct datum_data_v<xhqm::f32> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::real_32; };
	template<> struct datum_data_v<xhqm::f64> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::real_64; };
	template<> struct datum_data_v<xhqm::fld> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::real_ld; };
	template<> struct datum_data_v<xhqm::text> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::text_t; };
	template<> struct datum_data_v<bool> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::bool_t; };
	template<> struct datum_data_v<void> { constexpr static datum_data_::dtyp value = datum_data_::dtyp::null_t; };

	class xhqm_io datum
	{
		std::vector<datum_data_> _datum_;
	public:
		static bool read_file(const std::string& path, xhqm::datum& data);

		static bool save_file(const std::string& path, xhqm::datum& data);

		static bool save_json(const xhqm::datum*& data_ptr, std::string& jspn_str);

		static bool read_json(const std::string& jspn_str, xhqm::datum*& data_ptr);

		template<typename type> static bool save_data(const type*& data_ptr, std::string& data_str)
		{
			if (data_ptr == nullptr)
			{
				data_str = "\"save nullptr error\"";
				return false;
			}
			else
			{
				if constexpr (datum_data_v<type>::value == datum_data_::dtyp::null_t)
				{
					data_str = "\"save void error\"";
					return false;
				}
				else if constexpr (datum_data_v<type>::value == datum_data_::dtyp::bool_t)
				{
					data_str = ((*data_ptr) ? "true" : "false");
					return true;
				}
				else if constexpr (datum_data_v<type>::value == datum_data_::dtyp::text_t)
				{
					data_str = ("\"" + std::string(*data_ptr) + "\"");
					return true;
				}
				else if constexpr (datum_data_v<type>::value == datum_data_::dtyp::file_t)
				{
					data_str = "\"save file error\"";
					return false;
				}
				else if constexpr (datum_data_v<type>::value == datum_data_::dtyp::datum_t)
				{
					data_str = "\"save datum error\"";
					return save_json(data_ptr, data_str);
				}
				else if constexpr (true)
				{
					data_str = std::to_string(*data_ptr);
					return true;
				}
			}
			return false;
		}

		template<typename type> static bool read_data(const std::string& data_str, type*& data_ptr)
		{
			if (data_ptr == nullptr || data_str == "" || data_str == "\"save nullptr error\"")
			{
				data_ptr = nullptr;
				return false;
			}
			else
			{
				if constexpr (datum_data_v<type>::value == datum_data_::dtyp::null_t)
				{
					return false;
				}
				else if constexpr (datum_data_v<type>::value == datum_data_::dtyp::bool_t)
				{
					(*data_ptr) = (data_str == "true");
					return true;
				}
				else if constexpr (datum_data_v<type>::value == datum_data_::dtyp::text_t)
				{
					(*data_ptr) = data_str;
					data_ptr->erase(data_ptr->begin());
					data_ptr->erase(data_ptr->end() - 1);
					return true;
				}
				else if constexpr (datum_data_v<type>::value == datum_data_::dtyp::datum_t)
				{
					type* temp_ptr = (type*)data_ptr;
					return read_json(data_str, temp_ptr);
				}
				else if constexpr (
					datum_data_v<type>::value == datum_data_::dtyp::uint_08 ||
					datum_data_v<type>::value == datum_data_::dtyp::uint_16 ||
					datum_data_v<type>::value == datum_data_::dtyp::uint_32 ||
					datum_data_v<type>::value == datum_data_::dtyp::uint_64)
				{
					(*data_ptr) = static_cast<type>(std::stoull(data_str));
					return true;
				}
				else if constexpr (
					datum_data_v<type>::value == datum_data_::dtyp::sint_08 ||
					datum_data_v<type>::value == datum_data_::dtyp::sint_16 ||
					datum_data_v<type>::value == datum_data_::dtyp::sint_32 ||
					datum_data_v<type>::value == datum_data_::dtyp::sint_64)
				{
					(*data_ptr) = static_cast<type>(std::stoll(data_str));
					return true;
				}
				else if constexpr (
					datum_data_v<type>::value == datum_data_::dtyp::real_16 ||
					datum_data_v<type>::value == datum_data_::dtyp::real_32)
				{
					(*data_ptr) = std::stof(data_str);
					return true;
				}
				else if constexpr (datum_data_v<type>::value == datum_data_::dtyp::real_64)
				{
					(*data_ptr) = std::stod(data_str);
					return true;
				}
				else if constexpr (datum_data_v<type>::value == datum_data_::dtyp::real_ld)
				{
					(*data_ptr) = std::stold(data_str);
					return true;
				}
				else if constexpr (true)
				{
					return false;
				}
			}
			return false;
		}

		template<typename type> static bool save_list(const std::vector<type>*& data_ptr, std::string& data_str)
		{
			if (data_ptr == nullptr)
			{
				data_str = "\"save array error\"";
				return false;
			}
			else
			{
				bool save_res = true;
				std::string values = "[";
				for (const type& data_ref : (*data_ptr))
				{
					std::string value = "";
					const type* temp_ptr = (const type*)&data_ref;
					save_res &= datum::save_data<type>(temp_ptr, value);
					values += (value + ",");
				}
				values.back() = ']';
				data_str = values;
				return save_res;
			}
			return false;
		}

		template<typename type> static bool read_list(const std::string& data_str, std::vector<type>*& data_ptr)
		{
			if (data_ptr == nullptr || data_str == "" || data_str == "\"save array error\"")
			{
				data_ptr = nullptr;
				return false;
			}
			else
			{
				bool save_res = true;
				std::vector<std::string> elems = _split_json_array_(data_str);
				if (elems.size() == data_ptr->size())
				{
					xhqm::count count = elems.size();
					for (xhqm::index index = 0; index < count; ++index)
					{
						type* temp_ptr = &((*data_ptr)[index]);
						save_res &= datum::read_data<type>(elems[index], temp_ptr);
					}
					return save_res;
				}
				return false;
			}
			return false;
		}

	public:
		explicit datum() {};
		virtual ~datum() {};
		datum(datum&&) = delete;
		datum(const datum&) = delete;

		template<typename type> void resigt(const std::string& name, type* data, 
			std::function<bool(const type*&, xhqm::text&)> sptr = nullptr,
			std::function<bool(const xhqm::text&, type*&)> rptr = nullptr)
		{
			if (!sptr) { sptr = datum::save_data<type>; }
			if (!rptr) { rptr = datum::read_data<type>; }

			auto fun_sptr = [sptr](const void* V1, xhqm::text& V2) ->bool 
			{ auto type_ptr = (const type*)V1; return sptr(type_ptr, V2); };
			auto fun_rptr = [rptr](const xhqm::text& V1, void* V2) ->bool
			{ auto type_ptr = (type*)V2; return rptr(V1, type_ptr); };

			datum_data_ packet;
			packet.m_name = name;
			packet.m_dtyp = datum_data_v<type>::value;
			packet.m_dptr = data;
			packet.m_sptr = fun_sptr;
			packet.m_rptr = fun_rptr;

			auto iter = std::find(_datum_.begin(), _datum_.end(), name);
			if (iter == _datum_.end()) 
			{ 
				_datum_.push_back(packet);
			}
			else 
			{
				(*iter) = packet;
			}
		};

		void resigt(const std::string& name, xhqm::datum* data,
			std::function<bool(const xhqm::datum*&, xhqm::text&)> sptr = nullptr,
			std::function<bool(const xhqm::text&, xhqm::datum*&)> rptr = nullptr)
		{
			if (!sptr) { sptr = datum::save_json; }
			if (!rptr) { rptr = datum::read_json; }

			auto fun_sptr = [sptr](const void* V1, xhqm::text& V2) ->bool
			{ auto type_ptr = (const xhqm::datum*)V1; return sptr(type_ptr, V2); };
			auto fun_rptr = [rptr](const xhqm::text& V1, void* V2) ->bool
			{ auto type_ptr = (xhqm::datum*)V2; return rptr(V1, type_ptr); };

			datum_data_ packet;
			packet.m_name = name;
			packet.m_dtyp = datum_data_::datum_t;
			packet.m_dptr = data;
			packet.m_sptr = fun_sptr;
			packet.m_rptr = fun_rptr;

			auto iter = std::find(_datum_.begin(), _datum_.end(), name);
			if (iter == _datum_.end())
			{
				_datum_.push_back(packet);
			}
			else
			{
				(*iter) = packet;
			}
		}

		template<typename type> void resigt(const std::string& name, std::vector<type>* list, 
			std::function<bool(const std::vector<type>*&, xhqm::text&)> sptr = nullptr,
			std::function<bool(const xhqm::text&, std::vector<type>*&)> rptr = nullptr)
		{
			if (!sptr) { sptr = datum::save_list<type>; }
			if (!rptr) { rptr = datum::read_list<type>; }

			auto fun_sptr = [sptr](const void* V1, xhqm::text& V2) ->bool
			{ auto type_ptr = (const std::vector<type>*)V1; return sptr(type_ptr, V2); };
			auto fun_rptr = [rptr](const xhqm::text& V1, void* V2) ->bool
			{ auto type_ptr = (std::vector<type>*)V2; return rptr(V1, type_ptr); };

			datum_data_ packet;
			packet.m_name = name;
			packet.m_dtyp = datum_data_v<type>::value;
			packet.m_dptr = list;
			packet.m_sptr = fun_sptr;
			packet.m_rptr = fun_rptr;

			auto iter = std::find(_datum_.begin(), _datum_.end(), name);
			if (iter == _datum_.end())
			{
				_datum_.push_back(packet);
			}
			else
			{
				(*iter) = packet;
			}
		}

		template<typename type> type* value_ptr(const std::string& name)
		{
			return datum::value_ptr<type>(std::find(_datum_.begin(), _datum_.end(), name) - _datum_.begin());
		}

		template<typename type> type* value_ptr(const xhqm::index& index)
		{
			if (datum_data_v<type>::value != _datum_[index].m_dtyp) { throw std::string("error type"); }
			return (type*)(_datum_[index].m_dptr);
		}
	};

}

#endif
