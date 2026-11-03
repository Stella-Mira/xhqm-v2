#pragma once
#ifndef XHQM_CIPHER_UUID_H
#define XHQM_CIPHER_UUID_H

#include "../head.h"

#include <random>
#include <vector>
#include <string>
#include <array>


namespace xhqm
{
	namespace cipher
	{
		using text = std::string;

		class xhqm_io uuid
		{
			std::array<xhqm::byte, 16> m_data = {};
		public:
			uuid() = default;
			uuid(uuid&&) = default;
			uuid(const uuid&) = default;

			xhqm::byte& operator()(const xhqm::size& pos)
			{
				return m_data[pos];
			}

			const xhqm::byte& operator()(const xhqm::size& pos) const
			{
				return m_data[pos];
			}

			const std::array<xhqm::byte, 16>& data() const
			{
				return m_data;
			}
			const cipher::uuid& operator=(cipher::uuid&& value) noexcept
			{
				m_data = std::move(value.m_data);
				return (*this);
			}
			const cipher::uuid& operator=(const cipher::uuid& value) noexcept
			{
				m_data = value.m_data;
				return (*this);
			}
			const cipher::uuid& operator=(const cipher::text& value)
			{
				cipher::uuid data = cipher::uuid::to_uuid(value);
				if (!uuid_equal(cipher::uuid(), data))
				{
					m_data = data.m_data;
				}
				return (*this);
			}
			bool operator==(const cipher::uuid& value) const
			{
				return uuid_equal(*this, value);
			}

			bool operator!=(const cipher::uuid& value) const
			{
				return !uuid_equal(*this, value);
			}

			operator bool() const
			{
				return !uuid_equal(cipher::uuid(), *this);
			}

			cipher::text to_text() const
			{
				return cipher::uuid::to_text(*this);
			}

			static bool uuid_equal(const cipher::uuid& left, const cipher::uuid& right);


			static cipher::text to_text(const cipher::uuid& uuid_byte);

			static cipher::uuid to_uuid(const cipher::text& uuid_text);


			static cipher::uuid creat_uuid_v1(std::string addr);

			static cipher::uuid creat_uuid_v2(std::string addr, xhqm::size local_id, xhqm::byte domain);

			static cipher::uuid creat_uuid_v3(const cipher::uuid& namespace_uuid, const xhqm::text& name);

			static cipher::uuid creat_uuid_v4(std::random_device seed = std::random_device{});
		};

		// uuid 字节字典序比较（uuid 自身无 operator<，std::map 键需比较器）
		struct uuid_less
		{
			inline bool operator()(const uuid& left, const uuid& right) const
			{
				for (xhqm::size i = 0; i < 16; ++i)
				{
					if (left(i) != right(i)) return left(i) < right(i);
				}
				return false;
			}
		};
	}
}

#endif // XHQM_UUID_H
