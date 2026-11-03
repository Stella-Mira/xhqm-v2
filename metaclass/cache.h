#pragma once
#ifndef CACHE
#define CACHE

#include "../head.h"

#include <list>
#include <array>
#include <vector>

namespace xhqm
{
	template<typename type, xhqm::size size_v = 0, xhqm::device::type device = xhqm::device::type::device_no>
	class cache : public std::array<type, size_v>
	{
	public:
		inline cache(const std::array<type, size_v>& _cache = {})
		{
			this->std::array<type, size_v>::operator=(_cache);
		}
		inline cache(const std::initializer_list<type>& _cache)
		{
			xhqm::size count = std::min(size_v, _cache.size());
			for (xhqm::index i = 0; i < count; ++i)
				this->operator[](i) = *(_cache.begin() + i);
		}
		template<class function, typename ... args>
		inline void execfun(function _function, args ..._args)
		{
			for (auto& value : (*this)) _function(value, std::forward<args>(_args)...);
		}
	};
	template<typename type>
	class cache<type, 0> : public std::vector<type>
	{
	public:
		inline cache(const xhqm::size& _size = 0)
			: std::vector<type>(_size)
		{

		}
		inline cache(const std::vector<type>& _cache)
			: std::vector<type>(_cache)
		{

		}
		inline cache(const std::initializer_list<type>& _cache)
			: std::vector<type>(_cache)
		{

		}
		template<xhqm::size _size = 0>
		inline cache(const xhqm::cache<type, _size>& _cache)
			: std::vector<type>(_cache.begin(), _cache.end())
		{

		}
		template<class function, typename ... args>
		inline void execfun(function _function, args ..._args)
		{
			for (auto& value : (*this)) _function(value, std::forward<args>(_args)...);
		}
	};

	//推导指引（注意：推导指引里**不能**写 `xhqm::cache(`，那会被解析成函数声明 →
	//   clang 报 `expected unqualified-id` / `use of undeclared identifier 'type'`；MSVC 宽容）
	template<typename type> cache(const std::initializer_list<type>&)->cache<type, 0>;
	// 原第 2 条 `template<typename type> cache(const xhqm::size&)->cache<type, 0>;` 已注释：
	//   `type` 无法从 `xhqm::size` 推导（clang：deduction guide template contains a template
	//   parameter that cannot be deduced），补模板头也编不过；它是**语义问题**不是语法问题。
	//   若确实需要「按长度构造」，应写成显式模板实参形式：template<typename type> cache<type>(const xhqm::size&)->cache<type, 0>;
	//   全仓当前无 `cache(实参)` 用法（仅 `cache<int,3>` / `cache<int>`），故注释掉不影响行为。
	template<xhqm::size size_v> using indexs = xhqm::cache<xhqm::index, size_v>;
	template<xhqm::size size_v> using counts = xhqm::cache<xhqm::count, size_v>;



}

#endif // BUFFER
