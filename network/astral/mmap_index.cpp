#include "mmap_index.h"
#include <cstring>
#include <vector>

namespace xhqm
{
	namespace astral
	{
		static constexpr xhqm::u64 HEADER_SIZE = 96;
		static constexpr xhqm::u64 REC_SIZE = 64;
		static constexpr xhqm::u64 STR_REC_SIZE = 32;

		static xhqm::u64 read_u64(const xhqm::byte* p)
		{
			xhqm::u64 v = 0;
			for (int i = 0; i < 8; ++i)
			{
				v |= static_cast<xhqm::u64>(p[i]) << (8 * i);
			}
			return v;
		}

		static int uuid_compare(const uuid& a, const uuid& b)
		{
			for (int i = 0; i < 16; ++i)
			{
				if (a(i) < b(i)) return -1;
				if (a(i) > b(i)) return +1;
			}
			return 0;
		}

		static int str_compare(const xhqm::byte* pool, const str_index_record* rec, const xhqm::text& query)
		{
			const char* a = reinterpret_cast<const char*>(pool + rec->str_off);
			xhqm::u64 n = rec->str_size < query.size() ? rec->str_size : query.size();
			int c = std::memcmp(a, query.data(), n);
			if (c != 0) return c;
			if (rec->str_size < query.size()) return -1;
			if (rec->str_size > query.size()) return 1;
			return 0;
		}

		bool mmap_index::open(const xhqm::text& path)
		{
			close();
			if (!map_.open(path)) return false;
			const xhqm::byte* base = map_.data();
			if (map_.size() < HEADER_SIZE) { close(); return false; }
			if (std::memcmp(base, "ASTRAL", 6) != 0) { close(); return false; }
			if (base[6] != 8) { close(); return false; }   // version 8（统一 node）

			count_ = read_u64(base + 8);
			xhqm::u64 info_index_off = read_u64(base + 24);
			xhqm::u64 pool_off = read_u64(base + 32);
			xhqm::u64 blob_off = read_u64(base + 40);

			data_idx_ = base + HEADER_SIZE;
			info_idx_ = base + info_index_off;
			pool_ = base + pool_off;
			blob_ = base + blob_off;
			return true;
		}

		void mmap_index::close()
		{
			map_.close();
			data_idx_ = info_idx_ = pool_ = blob_ = nullptr;
			count_ = 0;
		}

		bool mmap_index::get(const uuid& id, xhqm::text& info, nlohmann::json& data) const
		{
			if (!data_idx_ || count_ == 0) return false;
			xhqm::u64 lo = 0, hi = count_;
			while (lo < hi)
			{
				xhqm::u64 mid = (lo + hi) / 2;
				const index_record* rec = reinterpret_cast<const index_record*>(data_idx_ + mid * REC_SIZE);
				int cmp = uuid_compare(rec->id, id);
				if (cmp < 0) lo = mid + 1;
				else if (cmp > 0) hi = mid;
				else
				{
					info.assign(reinterpret_cast<const char*>(pool_ + rec->info_off), rec->info_size);
					const xhqm::byte* d = blob_ + rec->data_off;
					std::vector<std::uint8_t> cb(d, d + rec->data_size);
					data = nlohmann::json::from_cbor(cb);
					return true;
				}
			}
			return false;
		}

		std::vector<uuid> mmap_index::find_info(const xhqm::text& info) const
		{
			std::vector<uuid> out;
			if (!info_idx_ || count_ == 0) return out;
			xhqm::u64 lo = 0, hi = count_;
			while (lo < hi)
			{
				xhqm::u64 mid = (lo + hi) / 2;
				const str_index_record* rec = reinterpret_cast<const str_index_record*>(info_idx_ + mid * STR_REC_SIZE);
				if (str_compare(pool_, rec, info) < 0) lo = mid + 1;
				else hi = mid;
			}
			for (xhqm::u64 i = lo; i < count_; ++i)
			{
				const str_index_record* rec = reinterpret_cast<const str_index_record*>(info_idx_ + i * STR_REC_SIZE);
				if (str_compare(pool_, rec, info) != 0) break;
				out.push_back(rec->id);
			}
			return out;
		}
	}
}
