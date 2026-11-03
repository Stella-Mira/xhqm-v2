#ifndef XHQM_ITYPE
#define XHQM_ITYPE

#include "mathf.h"
#include <cstdint>
#include <limits>

namespace xhqm
{
	using u8 = std::uint8_t;
	using u16 = std::uint16_t;
	using u32 = std::uint32_t;
	using u64 = std::uint64_t;

	using i8 = std::int8_t;
	using i16 = std::int16_t;
	using i32 = std::int32_t;
	using i64 = std::int64_t;

	static constexpr u8 u8min = 0x0u;
	static constexpr u16 u16min = 0x0u;
	static constexpr u32 u32min = 0x0u;
	static constexpr u64 u64min = 0x0u;

	static constexpr u8 u8max = 0xffu;
	static constexpr u16 u16max = 0xffffu;
	static constexpr u32 u32max = 0xffffffffu;
	static constexpr u64 u64max = 0xffffffffffffffffu;


	static constexpr u8 u8zero = 0x0u;
	static constexpr u16 u16zero = 0x0u;
	static constexpr u32 u32zero = 0x0u;
	static constexpr u64 u64zero = 0x0u;

	static constexpr u8 u8npos = 0xffu;
	static constexpr u16 u16npos = 0xffffu;
	static constexpr u32 u32npos = 0xffffffffu;
	static constexpr u64 u64npos = 0xffffffffffffffffu;


	static constexpr i8 i8min = (-127 - 1);
	static constexpr i16 i16min = (-32767 - 1);
	static constexpr i32 i32min = (-2147483647 - 1);
	static constexpr i64 i64min = (-9223372036854775807 - 1);

	static constexpr i8 i8max = 127;
	static constexpr i16 i16max = 32767;
	static constexpr i32 i32max = 2147483647;
	static constexpr i64 i64max = 9223372036854775807;

	//template<typename type> inline type min(type& value) { value = type(); return value; }
	//template<typename type> inline type max(type& value) { value = type(); return value; }
	//template<typename type> inline type nan(type& value) { value = type(); return value; }
	//template<typename type> inline type inf(type& value) { value = type(); return value; }

	template<> inline u8 min(u8& value) { value = xhqm::u8min; return value; }
	template<> inline u8 max(u8& value) { value = xhqm::u8max; return value; }
	template<> inline u8 nan(u8& value) { value = xhqm::u8max; return value; }
	template<> inline u8 inf(u8& value) { value = xhqm::u8max; return value; }
	template<> inline bool ismin(const u8& value) { return (value == xhqm::u8min); }
	template<> inline bool ismax(const u8& value) { return (value == xhqm::u8max); }
	template<> inline u8 read(const std::string& value) { return static_cast<u8>(std::stoul(value)); }
	template<> inline std::string save(const u8& value) { return std::to_string(value); }

	template<> inline u16 min(u16& value) { value = xhqm::u16min; return value; }
	template<> inline u16 max(u16& value) { value = xhqm::u16max; return value; }
	template<> inline u16 nan(u16& value) { value = xhqm::u16max; return value; }
	template<> inline u16 inf(u16& value) { value = xhqm::u16max; return value; }
	template<> inline bool ismin(const u16& value) { return (value == xhqm::u16min); }
	template<> inline bool ismax(const u16& value) { return (value == xhqm::u16max); }
	template<> inline u16 read(const std::string& value) { return static_cast<u16>(std::stoul(value)); }
	template<> inline std::string save(const u16& value) { return std::to_string(value); }

	template<> inline u32 min(u32& value) { value = xhqm::u32min; return value; }
	template<> inline u32 max(u32& value) { value = xhqm::u32max; return value; }
	template<> inline u32 nan(u32& value) { value = xhqm::u32max; return value; }
	template<> inline u32 inf(u32& value) { value = xhqm::u32max; return value; }
	template<> inline bool ismin(const u32& value) { return (value == xhqm::u32min); }
	template<> inline bool ismax(const u32& value) { return (value == xhqm::u32max); }
	template<> inline u32 read(const std::string& value) { return static_cast<u32>(std::stoul(value)); }
	template<> inline std::string save(const u32& value) { return std::to_string(value); }

	template<> inline u64 min(u64& value) { value = xhqm::u64min; return value; }
	template<> inline u64 max(u64& value) { value = xhqm::u64max; return value; }
	template<> inline u64 nan(u64& value) { value = xhqm::u64max; return value; }
	template<> inline u64 inf(u64& value) { value = xhqm::u64max; return value; }
	template<> inline bool ismin(const u64& value) { return (value == xhqm::u64min); }
	template<> inline bool ismax(const u64& value) { return (value == xhqm::u64max); }
	template<> inline u64 read(const std::string& value) { return static_cast<u64>(std::stoull(value)); }
	template<> inline std::string save(const u64& value) { return std::to_string(value); }

	template<> inline i8 min(i8& value) { value = xhqm::i8min; return value; }
	template<> inline i8 max(i8& value) { value = xhqm::i8max; return value; }
	template<> inline i8 nan(i8& value) { value = xhqm::i8max; return value; }
	template<> inline i8 inf(i8& value) { value = xhqm::i8max; return value; }
	template<> inline bool ismin(const i8& value) { return (value == xhqm::i8min); }
	template<> inline bool ismax(const i8& value) { return (value == xhqm::i8max); }
	template<> inline i8 read(const std::string& value) { return static_cast<i8>(std::stoi(value)); }
	template<> inline std::string save(const i8& value) { return std::to_string(value); }

	template<> inline i16 min(i16& value) { value = xhqm::i16min; return value; }
	template<> inline i16 max(i16& value) { value = xhqm::i16max; return value; }
	template<> inline i16 nan(i16& value) { value = xhqm::i16max; return value; }
	template<> inline i16 inf(i16& value) { value = xhqm::i16max; return value; }
	template<> inline bool ismin(const i16& value) { return (value == xhqm::i16min); }
	template<> inline bool ismax(const i16& value) { return (value == xhqm::i16max); }
	template<> inline i16 read(const std::string& value) { return static_cast<i16>(std::stoi(value)); }
	template<> inline std::string save(const i16& value) { return std::to_string(value); }

	template<> inline i32 min(i32& value) { value = xhqm::i32min; return value; }
	template<> inline i32 max(i32& value) { value = xhqm::i32max; return value; }
	template<> inline i32 nan(i32& value) { value = xhqm::i32max; return value; }
	template<> inline i32 inf(i32& value) { value = xhqm::i32max; return value; }
	template<> inline bool ismin(const i32& value) { return (value == xhqm::i32min); }
	template<> inline bool ismax(const i32& value) { return (value == xhqm::i32max); }
	template<> inline i32 read(const std::string& value) { return static_cast<i32>(std::stoi(value)); }
	template<> inline std::string save(const i32& value) { return std::to_string(value); }

	template<> inline i64 min(i64& value) { value = xhqm::i64min; return value; }
	template<> inline i64 max(i64& value) { value = xhqm::i64max; return value; }
	template<> inline i64 nan(i64& value) { value = xhqm::i64max; return value; }
	template<> inline i64 inf(i64& value) { value = xhqm::i64max; return value; }
	template<> inline bool ismin(const i64& value) { return (value == xhqm::i64min); }
	template<> inline bool ismax(const i64& value) { return (value == xhqm::i64max); }
	template<> inline i64 read(const std::string& value) { return static_cast<i64>(std::stoll(value)); }
	template<> inline std::string save(const i64& value) { return std::to_string(value); }
}
#endif