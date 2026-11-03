#pragma once
#ifndef BLOCK
#define BLOCK

#include "../head.h"
#include <cstring>

namespace xhqm
{
	class xhqm_io block
	{
		xhqm::byte* m_data = nullptr;
		xhqm::size m_size = 0;
	public:
		block(const block& _copy) noexcept
		{
			this->init(_copy.m_size);
			std::memcpy(m_data, _copy.m_data, m_size);
		}
		block(block&& _move) noexcept
		{
			m_data = _move.m_data;
			m_size = _move.m_size;
			_move.m_data = nullptr;
			_move.m_size = 0;
		}

		inline block(xhqm::size size = 0)
		{
			init(size);
		}
		inline ~block()
		{
			free();
		}
		inline void init(xhqm::size size)
		{
			if (m_data)
			{
				std::free(m_data);
				m_data = nullptr;
				m_size = 0;
			}
			if (size > 0)
			{
				m_size = size;
				m_data = (xhqm::byte*)std::malloc(size);
			}
		}
		inline void free()
		{
			if (m_data)
			{
				std::free(m_data);
				m_data = nullptr;
				m_size = 0;
			}
		}
		inline bool good() const
		{
			return m_data;
		}
		inline xhqm::size size() const
		{
			return m_size;
		}

		void operator =(const block& _copy)
		{
			this->init(_copy.m_size);
			std::memcpy(m_data, _copy.m_data, m_size);
		}
		void operator =(block&& _move) noexcept
		{
			this->free();
			m_data = _move.m_data;
			m_size = _move.m_size;
			_move.m_data = nullptr;
			_move.m_size = 0;
		}

		template<typename type = xhqm::byte>
		inline type* value_ptr(const xhqm::size& offset)
		{
			return (type*)(m_data + offset);
		}
		template<typename type = xhqm::byte>
		inline const type* value_ptr(const xhqm::size& offset) const
		{
			return (const type*)(m_data + offset);
		}
	};
}

#endif // BLOCK
