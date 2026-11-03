#pragma once
#ifndef XHQM_SOUL
#define XHQM_SOUL
#include "../head.h"

#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace xhqm
{
		class xhqm_io mmap {
			mmap& operator=(const mmap&) = delete;
			mmap(const mmap&) = delete;
		public:
			mmap(const xhqm::text& path = "")
			{
				if (!path.empty())
				{
					open(path);
				}
				info_ = "";
				size_ = 0;
			};
			~mmap()
			{
				close();
			}

			bool open(const xhqm::text& path);
			void close();

			std::vector<xhqm::byte> buffer(xhqm::size pos = 0, xhqm::size size = 0);

			inline bool good() const { return addr_; }
			inline void* addr() const { return addr_; }
			inline size_t size() const { return size_; }
			inline xhqm::byte* data()
			{
				return static_cast<xhqm::byte*>(addr_);
			}
			inline const xhqm::byte* data() const
			{
				return static_cast<const xhqm::byte*>(addr_);
			}
			inline const xhqm::text& error() const { return info_; }
		private:
			void* addr_ = nullptr;
			xhqm::size size_;
			xhqm::text info_;
		};

}
#endif