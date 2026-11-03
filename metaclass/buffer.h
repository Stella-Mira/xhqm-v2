#pragma once
#ifndef BUFFER
#define BUFFER

#include "../head.h"
#include "../metaclass/template.h"

#include <string>
#include <numeric>
#include <algorithm>

namespace xhqm
{
	template <class type> class buffer
	{
	private:
		xhqm::size m_size = 0;
		type* m_buffer = nullptr;
	public:
		~buffer() noexcept
		{
			reset(0);
		}

		buffer(const xhqm::size& size = 0) noexcept
		{
			reset(size);
		}
		buffer(const buffer& _copy) noexcept
			: buffer(_copy.m_size)
		{
			for (xhqm::size i = 0; i < m_size; ++i)
			{
				m_buffer[i] = _copy.m_buffer[i];
			}
		}
		buffer(buffer&& _move) noexcept
			: m_size(_move.m_size)
		{
			m_buffer = _move.m_buffer;
			_move.m_buffer = nullptr;
			_move.m_size = 0;
		}

		void operator =(const buffer& _copy)
		{
			if (&_copy != this)
			{
				reset(_copy.m_size);
				for (xhqm::size i = 0; i < m_size; ++i)
				{
					m_buffer[i] = _copy.m_buffer[i];
				}
			}
		}
		void operator =(buffer&& _move) noexcept
		{
			if (&_move != this)
			{
				reset(0);
				m_buffer = _move.m_buffer;
				_move.m_buffer = nullptr;
				m_size = _move.m_size;
				_move.m_size = 0;
			}
		}

	public:
		void reset(const xhqm::size& size)
		{
			m_size = size;
			if (m_buffer) delete[] m_buffer;
			m_buffer = (size == 0 ? nullptr : new type[size]);
		}

        inline const xhqm::size& size() const
		{
			return m_size;
		}

		inline const type& value(const xhqm::size& pos) const
		{
			return m_buffer[pos];
		}

		inline type& value(const xhqm::size& pos)
		{
			return m_buffer[pos];
		}

		inline const type* data() const
		{
			return m_buffer;
		}

		inline type* data()
		{
			return m_buffer;
		}
	};

	template <class type, xhqm::size dim> class buffer_dim
	{
	private:
		std::array<xhqm::size, dim> m_dimensions;
		xhqm::buffer<type> m_buffer;
	public:
		buffer_dim(const std::array<xhqm::size, dim>& _dimensions) noexcept
			: m_dimensions(_dimensions)
		{
			xhqm::size count = 1;
			for (const auto& dimension : _dimensions)
				count *= dimension;
			m_buffer.reset(count);
		}
		buffer_dim(const buffer_dim& _copy) noexcept
			: m_dimensions(_copy.m_dimensions), m_buffer(_copy.m_buffer)
		{
		}
		buffer_dim(buffer_dim&& _move) noexcept
			: m_dimensions(_move.m_dimensions), m_buffer(std::move(_move.m_buffer))
		{
		}

		void operator =(const buffer_dim& _copy)
		{
			m_dimensions = _copy.m_dimensions;
			m_buffer = _copy.m_buffer;
		}
		void operator =(buffer_dim&& _move)
		{
			m_dimensions = _move.m_dimensions;
			m_buffer = std::move(_move.m_buffer);
		}

	public:
		constexpr std::array<xhqm::size, dim> dimensions() const noexcept
		{
			return m_dimensions;
		}

		constexpr xhqm::size dimension() const noexcept
		{
			return dim;
		}

		constexpr xhqm::size size() const noexcept
		{
			return m_buffer.size();
		}

		inline void reset(const std::array<xhqm::size, dim>& _dimensions) const noexcept
		{
			xhqm::size count = 1;
			for (const auto& dimension : _dimensions)
				count *= dimension;
			m_buffer.reset(count);
		}

		inline const type& value(std::array<xhqm::size, dim> _location) const
		{
			xhqm::index index = 0;
			for (xhqm::index i = dim; i != 0; --i)
			{
				index = i - 1;
				_location[index] = std::accumulate(m_dimensions.begin(), m_dimensions.begin() + index, _location[index], [](const xhqm::size& that, const xhqm::size& next) { return that * next; });
			}
			index = std::accumulate(_location.begin(), _location.end(), xhqm::size(0), [](const xhqm::size& that, const xhqm::size& next) { return that + next; });
			return m_buffer.value(index);
		}

		inline type& value(const std::array<xhqm::size, dim>& _location)
		{
			xhqm::index index = 0;
			for (xhqm::index i = dim; i != 0; --i)
			{
				index = i - 1;
				_location[index] = std::accumulate(m_dimensions.begin(), m_dimensions.begin() + index, _location[index], [](const xhqm::size& that, const xhqm::size& next) { return that * next; });
			}
			index = std::accumulate(_location.begin(), _location.end(), xhqm::size(0), [](const xhqm::size& that, const xhqm::size& next) { return that + next; });
			return m_buffer.value(index);
		}

		inline const type* data() const
		{
			return m_buffer.data();
		}

		inline type* data()
		{
			return m_buffer.data();
		}
	};

	//template <class type> class buffer_dim<type, 0>
	//{
	//	//static_assert(false, "The dimension cannot be zero !!!");
	//};
}

#endif // BUFFER
