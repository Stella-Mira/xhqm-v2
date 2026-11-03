#include "mmap.h"
#include <fstream>
#include <iostream>

// Windows
#ifdef _WIN32
#include <windows.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#else
// Linux/macOS
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#undef min
#undef max

namespace xhqm
{
		bool mmap::open(const xhqm::text& path)
		{
			if (this->good()) return true;
#ifdef _WIN32
			// Windows 实现
			HANDLE fd_ = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
			);

			if (fd_ == INVALID_HANDLE_VALUE) {
				info_ = "Failed to open file: " + path;
				return false;
			}

			LARGE_INTEGER fileSize;
			if (!GetFileSizeEx(fd_, &fileSize)) {
				info_ = "Failed to get file size: " + path;
				CloseHandle(fd_);
				return false;
			}

			HANDLE hMapping = CreateFileMappingA(fd_, nullptr, PAGE_READONLY, 0, 0, nullptr);
			CloseHandle(fd_);
			if (!hMapping) {
				info_ = "Failed to create file mapping: " + path;
				return false;
			}

			addr_ = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
			CloseHandle(hMapping);
			if (!addr_) {
				info_ = "Failed to map view of file: " + path;
				return false;
			}

			size_ = fileSize.QuadPart;
#else
			// Linux/macOS 保持原有实现
			int fd = ::open(path.c_str(), O_RDONLY);
			if (fd < 0) {
				info_ = "Failed to open file for mmap: " + path;
				return false;
			}

			struct stat st;
			if (::fstat(fd, &st) != 0) {
				info_ = "Failed to stat file: " + path;
				::close(fd);
				return false;
			}

			addr_ = ::mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
			::close(fd);

			if (addr_ == MAP_FAILED) {
				info_ = "Failed to mmap file: " + path;
				return false;
			}

			size_ = st.st_size;
#endif
			return true;
		}

		void mmap::close()
		{
#ifdef _WIN32
			if (addr_)
			{
				UnmapViewOfFile(addr_);
				addr_ = nullptr;
				info_ = "";
				size_ = 0;
			}
#else
			if (addr_)
			{
				munmap(addr_, size_);
				addr_ = nullptr;
				info_ = "";
				size_ = 0;
			}
#endif
		}

		std::vector<xhqm::byte> mmap::buffer(xhqm::size pos, xhqm::size size)
		{
			std::vector<xhqm::byte> buffer;
			if (addr_ && pos <= size_)
			{
				if ((pos + size) > size_ || size == 0)
				{
					size = size_ - pos;
				}
				if (size > 0)
				{
					buffer.resize(size);
					const xhqm::byte* src = this->data() + pos;
					std::memcpy(buffer.data(), src, size);
				}
			}
			return buffer;
		}

}
