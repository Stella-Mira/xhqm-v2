#ifndef XHQM_TENSOR_STORE
#define XHQM_TENSOR_STORE

// 元素存储：bool 用 figure<bool>（GF(2) 值类型）存到 std::vector —— 连续内存、data() 有效，
// 规避 std::vector<bool> 位压缩代理与 std::deque<bool> 非连续的问题；
// 其余类型用 std::vector（连续内存，供后端 elemwise/matmul 取 data()）。
// 接口对齐容器所需的最小集合：构造 / resize / assign / size / operator[] / data。
#include "figure.h"
#include <vector>
#include <cstddef>

namespace xhqm
{
	namespace tensor_base
	{
		template<class type>
		class store
		{
			std::vector<type> m_values;
		public:
			store() = default;
			store(const std::size_t& n, const type& v = type()) : m_values(n, v) {}
			store(const std::vector<type>& list) : m_values(list) {}
			store(std::initializer_list<type> list) : m_values(list) {}

			inline void resize(const std::size_t& n) { m_values.resize(n); }
			inline void resize(const std::size_t& n, const type& v) { m_values.resize(n, v); }
			inline void assign(const std::size_t& n, const type& v) { m_values.assign(n, v); }
			inline void push_back(const type& v) { m_values.push_back(v); }
			inline std::size_t size() const { return m_values.size(); }

			inline type& operator[](const std::size_t& i) { return m_values[i]; }
			inline const type& operator[](const std::size_t& i) const { return m_values[i]; }

			inline type* data() { return m_values.data(); }
			inline const type* data() const { return m_values.data(); }
		};

		template<>
		class store<bool>
		{
			// 内部用 figure<bool>（GF(2) 值类型）存连续内存，对外仍以 bool 暴露（value/reinterpret_cast）。
			std::vector<figure<bool>> m_values;
		public:
			store() = default;
			store(const std::size_t& n, const bool& v = false) : m_values(n, figure<bool>(v)) {}
			store(const std::vector<bool>& list) : m_values(list.begin(), list.end()) {}
			store(std::initializer_list<bool> list) : m_values(list.begin(), list.end()) {}

			inline void resize(const std::size_t& n) { m_values.resize(n); }
			inline void resize(const std::size_t& n, const bool& v) { m_values.resize(n, figure<bool>(v)); }
			inline void assign(const std::size_t& n, const bool& v) { m_values.assign(n, figure<bool>(v)); }
			inline void push_back(const bool& v) { m_values.push_back(figure<bool>(v)); }
			inline std::size_t size() const { return m_values.size(); }

			inline bool& operator[](const std::size_t& i) { return m_values[i].value(); }
			inline const bool& operator[](const std::size_t& i) const { return m_values[i].value(); }

			// figure<bool> 与 bool 布局兼容（1 字节，无填充）。data() 以 bool* 做类型擦除传参，
			// 后端按 dtype::boolean 经 unsigned char（字符类型别名，well-defined）做 GF(2) 访问。
			inline bool* data() { return reinterpret_cast<bool*>(m_values.data()); }
			inline const bool* data() const { return reinterpret_cast<const bool*>(m_values.data()); }
		};
	}
}

#endif
