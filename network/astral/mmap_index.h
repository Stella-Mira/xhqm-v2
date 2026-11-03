#ifndef ASTRAL_MEMORY_MMAP_INDEX
#define ASTRAL_MEMORY_MMAP_INDEX

#include "astral.h"

namespace xhqm
{
	namespace astral
	{
#pragma pack(push, 1)
		// node 主索引记录（64 字节，按 uuid 排序；data 与 link 同构，link 的 data_off/size=0）
		struct index_record
		{
			uuid id;              // 16
			xhqm::u64 info_off;   // 24
			xhqm::u64 info_size;  // 32
			xhqm::u64 data_off;   // 40
			xhqm::u64 data_size;  // 48
			xhqm::u64 links_off;  // 56
			xhqm::u64 link_count; // 64
		};

		// info 二级索引记录（32 字节，按字符串排序）
		struct str_index_record
		{
			xhqm::u64 str_off;    // 变长池偏移
			xhqm::u64 str_size;   // 字符串长度
			uuid id;              // 16
		};
#pragma pack(pop)

		static_assert(sizeof(index_record) == 64, "index_record must be 64 bytes");
		static_assert(sizeof(str_index_record) == 32, "str_index_record must be 32 bytes");

		// mmap 只读索引：uuid/info 二级索引，物理内存只占访问页
		class xhqm_io mmap_index
		{
		public:
			~mmap_index() { close(); }

			bool open(const xhqm::text& path);
			void close();

			xhqm::u64 count() const { return count_; }

			// uuid 二分查找，找到返回 true，填 info 与 data 本体
			bool get(const uuid& id, xhqm::text& info, nlohmann::json& data) const;

			// info 二级索引：返回所有同 info 节点的 uuid
			std::vector<uuid> find_info(const xhqm::text& info) const;

		private:
			xhqm::mmap map_;
			xhqm::u64 count_ = 0;
			const xhqm::byte* data_idx_ = nullptr;
			const xhqm::byte* info_idx_ = nullptr;
			const xhqm::byte* pool_ = nullptr;
			const xhqm::byte* blob_ = nullptr;
		};
	}
}

#endif
