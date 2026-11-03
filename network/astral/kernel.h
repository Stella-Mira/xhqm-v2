#ifndef ASTRAL_MEMORY_KERNEL
#define ASTRAL_MEMORY_KERNEL

#include "astral.h"

#include <mutex>
#include <shared_mutex>

namespace xhqm
{
	namespace astral
	{
		// 系统最高精度时间戳（纳秒）
		xhqm::u64 xhqm_io now_stamp();

		// 内核：节点主存 + 双向关联 + json/二进制持久化
		class xhqm_io kernel
		{
		public:
			using uuid_list = std::vector<xhqm::astral::uuid>;
			using node_list = std::vector<xhqm::astral::node>;
			using name_map = std::map<xhqm::astral::text, xhqm::astral::uuid>;
			using info_map = std::map<xhqm::astral::text, std::vector<xhqm::astral::uuid>>;  // info → uuid 列表（一对多，find-info 索引）
			using node_map = std::map<xhqm::astral::uuid, xhqm::astral::node, uuid_less>;
			using offset = std::map<xhqm::astral::uuid, std::pair<xhqm::u64, xhqm::u64>, uuid_less>;
		protected:
			mutable std::shared_mutex m_mutex;   // 读写锁：读共享、写独占
		protected:
			offset m_offset;                      // data 段(offset,size)
			node_map m_node_data;                 // 主存路由：uuid → node
			info_map m_info_index;                // info 索引（find-info 加速，info 不唯一）
			xhqm::text m_path_fetch;              // 当前文件：懒加载 fetch
		protected:
			// 不加锁的内部实现（供已加锁的公共方法重入调用）
			uuid put_impl(node& n);
			bool del_impl(const uuid& id);

			bool set_connect_impl(const uuid& a, const uuid& b);
			bool dis_connect_impl(const uuid& a, const uuid& b);
			bool load_index_impl(const xhqm::text& path);
			bool fetch_impl(node& n, const uuid& id);               // 冷节点加热（不加锁，供 update/remove 重入）
			void index_add(const node& n);                          // 节点入 info 索引（已加锁）
			void index_erase(const uuid& id, const text& info);     // 节点出 info 索引（已加锁）

		public:
			xhqm::size count() const;
			bool has(const uuid& id) const;
			const node* get(const uuid& id) const;
			json link_of(const uuid& id, bool wath_info = true) const;
			json data_of(const uuid& id, bool full_data = true) const;

			// ---- 连 ----
			bool set_connect(const uuid& a, const uuid& b);
			bool dis_connect(const uuid& a, const uuid& b);

			// ---- 增 ----
			uuid ensure(const astral::text& form, const astral::uuid& uuid, const astral::text& info, const astral::json& data);
			uuid create(const astral::text& form, const astral::text& info, const astral::json& data);

			// ---- 删 ----
			template<typename _func, typename ..._argv> //bool (const&form, const&node, &&argv...) //bool 是否删除整个节点
			bool remove(const astral::text& form, const astral::uuid& id, _func&& func_, _argv&& ...argv_)
			{
				std::unique_lock<std::shared_mutex> lock(m_mutex);
				const auto& iter = m_node_data.find(id);
				if (iter != m_node_data.end())
				{
					if (func_(form, iter->second, std::forward<_argv>(argv_)...))
					{
						return del_impl(id); //符合条件的节点删除
					}
				}
				return false;
			}

			// ---- 改 ----
			template<typename _func, typename ..._argv> //bool (const&form, &node, &&argv...) //bool 是否更新成功
			bool update(const astral::text& form, const astral::uuid& id, _func&& func_, _argv&& ...argv_)
			{
				std::unique_lock<std::shared_mutex> lock(m_mutex);
				const auto& iter = m_node_data.find(id);
				if (iter != m_node_data.end())
				{
					fetch_impl(iter->second, id);   // 冷节点先加热，避免历史数据被空 datas 覆盖丢失
					return func_(form, iter->second, std::forward<_argv>(argv_)...);//更新节点
				}
				return false;
			}

			// ---- 查 ----
			// 查询对冷节点自动加热（fetch_impl），非 const：查询可能触发按需加载
			template<typename _func, typename ..._argv> //json_array (const&form, const&node, &&argv...) //json 处理后的节点数据
			node_list select(const astral::text& form, _func&& func_, _argv&& ...argv_)
			{
				std::unique_lock<std::shared_mutex> lock(m_mutex);
				std::vector<astral::node> select_array;
				for (auto& iter : m_node_data)
				{
					fetch_impl(iter.second, iter.first);   // 冷节点加热（全表扫描类查询的固有代价）
					astral::json json_array = func_(form, iter.second, std::forward<_argv>(argv_)...);
					if (json_array.is_array() && !json_array.empty()) //存在正确的数据
					{
						select_array.emplace_back(astral::node{
							iter.second.uuid,iter.second.info,std::move(json_array),iter.second.links
							});
					}
				}
				return select_array;
			}

			//更新id处node的info
			bool update_info(const text& form, const uuid& id, const text& info);
			//追加id处node的data
			bool update_data(const text& form, const uuid& id, const json& data);
			//移触id处node在time前的data
			bool remove_time(const text& form, const uuid& id, xhqm::u64 time);
			//移触id处node
			bool remove_node(const text& form, const uuid& id);

			//搜索uuids包含的node的最新一条data
			node_list select_uuid(const std::vector<astral::uuid>& uuids);
			//搜索info包含word的node的最新一条data
			node_list select_info(const std::vector<astral::text>& words);
			//搜索最新一条data包含word的node
			node_list select_data(const std::vector<astral::text>& words);
			//搜索所有node里time之后的data
			node_list select_time(xhqm::u64 time);
			//搜索所有node里最后size条data
			node_list select_size(xhqm::u64 size);

			// ---- 持久化（json/binary） ----
			bool load_json(const xhqm::text& path);
			bool save_json(const xhqm::text& path);   // 非 const：成功后刷新 m_offset 为新布局
			bool load_binary(const xhqm::text& path);
			bool save_binary(const xhqm::text& path); // 非 const：成功后刷新 m_offset 为新布局
			// ---- 索引/数据 ----
			bool load_index(const xhqm::text& path);          // 加载索引
			bool data_fetch(const astral::uuid& id);          // 加载单体
			// ---- 惰性加载 ----
			xhqm::size loaded_size() const;                   // 已加载 data 本体数
			bool loaded_data(const astral::uuid& id) const;   // data 本体是否已加载

			// ---- mmap 只读索引（10 亿级，供 mmap_index 二分查找）----
			bool save_mmap(const xhqm::text& path) const;     // 定长索引 v8（统一 node）
		};
	}
}

#endif
