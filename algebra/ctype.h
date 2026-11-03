#ifndef XHQM_CTYPE
#define XHQM_CTYPE

#include "mathf.h"
#include "itype.h"
#include "ftype.h"
#include "tensor/complex.hpp"
#include "tensor/dualnum.hpp"
#include "../xhqm/block"
namespace xhqm
{
	struct xhqm_io ctype
	{
		enum type
		{
			null = 0,
			text = 1,
			bool_1,
			writ_8,
			writ_16,

			uint_8,
			uint_16,
			uint_32,
			uint_64,

			sint_8,
			sint_16,
			sint_32,
			sint_64,

			real_16,
			real_32,
			real_64,
			real_128,

			array,
			tuple,
		};

		static std::vector<std::string> type_complex(std::string string);
		static xhqm::ctype::type get_type(const xhqm::text& type);//类型编号
		static xhqm::text get_text(const xhqm::ctype::type& type);//类型名称
		static xhqm::byte get_size(const xhqm::ctype::type& type);//字节大小
	};

	class arr;
	class tup;

	class xhqm_io arr : public xhqm::ctype
	{
		xhqm::block m_block;
		std::string m_string;
		std::vector<xhqm::size> m_array;
		arr::type m_type = arr::type::null;
	public:
		std::string name() const
		{
			return m_string;
		}
		arr::type type() const
		{
			return m_type;
		}
		std::vector<xhqm::size> dim() const
		{
			return m_array;
		}
		xhqm::count size() const
		{
			xhqm::count count = 1;
			for (const auto& size : m_array)
				count *= size;
			return count;
		}
		bool good() const
		{
			return m_block.good();
		}

		arr(const std::string& array_string);

		~arr();

		arr(const arr& _copy) noexcept
		{
			m_block = _copy.m_block;
			m_array = _copy.m_array;
			m_type = _copy.m_type;
		}
		arr(arr&& _move) noexcept
		{
			m_block = std::move(_move.m_block);
			m_array = std::move(_move.m_array);
			m_type = std::move(_move.m_type);
		}

		void operator =(const arr& _copy)
		{
			m_block = _copy.m_block;
			m_array = _copy.m_array;
			m_type = _copy.m_type;
		}
		void operator =(arr&& _move) noexcept
		{
			m_block = std::move(_move.m_block);
			m_array = std::move(_move.m_array);
			m_type = std::move(_move.m_type);
		}

		void* value()
		{
			return m_block.value_ptr<void>(0);
		}
		const void* value() const
		{
			return m_block.value_ptr<void>(0);
		}
		template<typename type>
		type& value(const xhqm::index& index)
		{
			return *(m_block.value_ptr<type>(index * sizeof(type)));
		}
		template<typename type>
		const type& value(const xhqm::index& index) const
		{
			return *(m_block.value_ptr<type>(index * sizeof(type)));
		}

		template<typename type>
		type value_as(const xhqm::index& index) const
		{
			type value;
			if (m_type == ctype::type::null) { value = type(); }
			else if (m_type == ctype::type::bool_1) { value = this->value<bool>(index); }
			else if (m_type == ctype::type::writ_8) { value = this->value<char>(index); }
			else if (m_type == ctype::type::writ_16) { value = this->value<wchar_t>(index); }
			else if (m_type == ctype::type::uint_8) { value = this->value<unsigned char>(index); }
			else if (m_type == ctype::type::uint_16) { value = this->value<unsigned short>(index); }
			else if (m_type == ctype::type::uint_32) { value = this->value<unsigned int>(index); }
			else if (m_type == ctype::type::uint_64) { value = this->value<unsigned long long>(index); }
			else if (m_type == ctype::type::sint_8) { value = this->value<signed char>(index); }
			else if (m_type == ctype::type::sint_16) { value = this->value<signed short>(index); }
			else if (m_type == ctype::type::sint_32) { value = this->value<signed int>(index); }
			else if (m_type == ctype::type::sint_64) { value = this->value<signed long long>(index); }
			else if (m_type == ctype::type::real_16) { value = this->value<xhqm::float_16>(index); }
			else if (m_type == ctype::type::real_32) { value = this->value<xhqm::float_32>(index); }
			else if (m_type == ctype::type::real_64) { value = this->value<xhqm::float_64>(index); }
			else if (m_type == ctype::type::real_128) { value = this->value<xhqm::float_ld>(index); }
			else if (m_type == ctype::type::array) { value = this->value<xhqm::arr>(index); }
			else if (m_type == ctype::type::tuple) { value = this->value<xhqm::tup>(index); }
			return value;
		}
	};

	class xhqm_io tup : public xhqm::ctype
	{
		xhqm::block m_block;
		std::string m_string;
		std::vector<tup::type> m_tuple;
	public:
		std::string name() const
		{
			return m_string;
		}
		std::vector<tup::type> get_type() const
		{
			return m_tuple;
		}
		xhqm::size size() const
		{
			return m_tuple.size();
		}
		bool good() const
		{
			return m_block.good();
		}

		tup(const std::string& tuple_string);

		~tup();

		tup(const tup& _copy) noexcept
		{
			m_block = _copy.m_block;
			m_tuple = _copy.m_tuple;
		}
		tup(tup&& _move) noexcept
		{
			m_block = std::move(_move.m_block);
			m_tuple = std::move(_move.m_tuple);
		}

		void operator =(const tup& _copy)
		{
			m_block = _copy.m_block;
			m_tuple = _copy.m_tuple;
		}
		void operator =(tup&& _move) noexcept
		{
			m_block = std::move(_move.m_block);
			m_tuple = std::move(_move.m_tuple);
		}

		template<typename type>
		type& value(const xhqm::index& index)
		{
			xhqm::size offset = 0;
			for (xhqm::index i = 0; i < index; ++i)
				offset += ctype::get_size(m_tuple[i]);
			return *(m_block.value_ptr<type>(offset));
		}
		template<typename type>
		const type& value(const xhqm::index& index) const
		{
			xhqm::size offset = 0;
			for (xhqm::index i = 0; i < index; ++i)
				offset += ctype::get_size(m_tuple[i]);
			return *(m_block.value_ptr<type>(offset));
		}

		template<typename type>
		type value_as(const xhqm::index& index) const
		{
			type value;
			if (m_tuple[index] == ctype::type::null) { value = type(); }
			else if (m_tuple[index] == ctype::type::bool_1) { value = this->value<bool>(index); }
			else if (m_tuple[index] == ctype::type::writ_8) { value = this->value<char>(index); }
			else if (m_tuple[index] == ctype::type::writ_16) { value = this->value<wchar_t>(index); }
			else if (m_tuple[index] == ctype::type::uint_8) { value = this->value<unsigned char>(index); }
			else if (m_tuple[index] == ctype::type::uint_16) { value = this->value<unsigned short>(index); }
			else if (m_tuple[index] == ctype::type::uint_32) { value = this->value<unsigned int>(index); }
			else if (m_tuple[index] == ctype::type::uint_64) { value = this->value<unsigned long long>(index); }
			else if (m_tuple[index] == ctype::type::sint_8) { value = this->value<signed char>(index); }
			else if (m_tuple[index] == ctype::type::sint_16) { value = this->value<signed short>(index); }
			else if (m_tuple[index] == ctype::type::sint_32) { value = this->value<signed int>(index); }
			else if (m_tuple[index] == ctype::type::sint_64) { value = this->value<signed long long>(index); }
			else if (m_tuple[index] == ctype::type::real_16) { value = this->value<xhqm::float_16>(index); }
			else if (m_tuple[index] == ctype::type::real_32) { value = this->value<xhqm::float_32>(index); }
			else if (m_tuple[index] == ctype::type::real_64) { value = this->value<xhqm::float_64>(index); }
			else if (m_tuple[index] == ctype::type::real_128) { value = this->value<xhqm::float_ld>(index); }
			else if (m_tuple[index] == ctype::type::array) { value = this->value<xhqm::arr>(index); }
			else if (m_tuple[index] == ctype::type::tuple) { value = this->value<xhqm::tup>(index); }
			return value;
		}
	};

	template<typename type = void> struct ctype_v { constexpr static ctype::type value = ctype::type::null; };
	template<> struct ctype_v<xhqm::text> { constexpr static ctype::type value = ctype::type::text; };
	template<> struct ctype_v<bool> { constexpr static ctype::type value = ctype::type::bool_1; };
	template<> struct ctype_v<char> { constexpr static ctype::type value = ctype::type::writ_8; };
	template<> struct ctype_v<wchar_t> { constexpr static ctype::type value = ctype::type::writ_16; };
	template<> struct ctype_v<xhqm::u8> { constexpr static ctype::type value = xhqm::ctype::type::uint_8; };
	template<> struct ctype_v<xhqm::u16> { constexpr static ctype::type value = xhqm::ctype::type::uint_16; };
	template<> struct ctype_v<xhqm::u32> { constexpr static ctype::type value = xhqm::ctype::type::uint_32; };
	template<> struct ctype_v<xhqm::u64> { constexpr static ctype::type value = xhqm::ctype::type::uint_64; };
	template<> struct ctype_v<xhqm::i8> { constexpr static ctype::type value = xhqm::ctype::type::sint_8; };
	template<> struct ctype_v<xhqm::i16> { constexpr static ctype::type value = xhqm::ctype::type::sint_16; };
	template<> struct ctype_v<xhqm::i32> { constexpr static ctype::type value = xhqm::ctype::type::sint_32; };
	template<> struct ctype_v<xhqm::i64> { constexpr static ctype::type value = xhqm::ctype::type::sint_64; };
	template<> struct ctype_v<xhqm::f16> { constexpr static ctype::type value = xhqm::ctype::type::real_16; };
	template<> struct ctype_v<xhqm::f32> { constexpr static ctype::type value = xhqm::ctype::type::real_32; };
	template<> struct ctype_v<xhqm::f64> { constexpr static ctype::type value = xhqm::ctype::type::real_64; };
	template<> struct ctype_v<xhqm::fld> { constexpr static ctype::type value = xhqm::ctype::type::real_128; };
	template<> struct ctype_v<xhqm::arr> { constexpr static ctype::type value = xhqm::ctype::type::array; };
	template<> struct ctype_v<xhqm::tup> { constexpr static ctype::type value = xhqm::ctype::type::tuple; };
	template<ctype::type type_v = xhqm::ctype::type::null> struct ctype_s { using type = void; };
	template<> struct ctype_s<xhqm::ctype::type::text> { using type = xhqm::text; };
	template<> struct ctype_s<xhqm::ctype::type::bool_1> { using type = bool; };
	template<> struct ctype_s<xhqm::ctype::type::writ_8> { using type = char; };
	template<> struct ctype_s<xhqm::ctype::type::writ_16> { using type = wchar_t; };
	template<> struct ctype_s<xhqm::ctype::type::uint_8> { using type = xhqm::u8; };
	template<> struct ctype_s<xhqm::ctype::type::uint_16> { using type = xhqm::u16; };
	template<> struct ctype_s<xhqm::ctype::type::uint_32> { using type = xhqm::u32; };
	template<> struct ctype_s<xhqm::ctype::type::uint_64> { using type = xhqm::u64; };
	template<> struct ctype_s<xhqm::ctype::type::sint_8> { using type = xhqm::i8; };
	template<> struct ctype_s<xhqm::ctype::type::sint_16> { using type = xhqm::i16; };
	template<> struct ctype_s<xhqm::ctype::type::sint_32> { using type = xhqm::i32; };
	template<> struct ctype_s<xhqm::ctype::type::sint_64> { using type = xhqm::i64; };
	template<> struct ctype_s<xhqm::ctype::type::real_16> { using type = xhqm::f16; };
	template<> struct ctype_s<xhqm::ctype::type::real_32> { using type = xhqm::f32; };
	template<> struct ctype_s<xhqm::ctype::type::real_64> { using type = xhqm::f64; };
	template<> struct ctype_s<xhqm::ctype::type::real_128> { using type = xhqm::fld; };
	template<> struct ctype_s<xhqm::ctype::type::array> { using type = xhqm::arr; };
	template<> struct ctype_s<xhqm::ctype::type::tuple> { using type = xhqm::tup; };
}										
#endif									
										
										

										
										
										
										

										
										
										
										

										
										
										