#ifndef XHQM_FACTORY
#define XHQM_FACTORY

// 工厂（注册表）：按 id 类型安全地创建派生类，返回 shared_ptr<base_type>
// 单线程：无锁 map；多线程：加锁，create 拷贝创建器快照后调用
#include <map>
#include <mutex>
#include <memory>
#include <functional>
#include "thread_mode.h"
#include "../../head.h"

namespace xhqm
{
	// 主模板：只声明，实现见单/多线程特化
	template<class base_type, class id_type = xhqm::id, class thread = xhqm::single_thread>
	class factory;

	// 单线程工厂（默认）
	template<class base_type, class id_type>
	class factory<base_type, id_type, xhqm::single_thread>
	{
		std::map<id_type, std::function<std::shared_ptr<base_type>()>> m_creators;
	public:
		// 注册派生类：factory.regist<derived>(id)
		template<class derived_type>
		void regist(id_type id)
		{
			m_creators[id] = []() -> std::shared_ptr<base_type> { return std::make_shared<derived_type>(); };
		}

		// 按 id 创建；未注册返回 nullptr
		std::shared_ptr<base_type> create(id_type id) const
		{
			auto it = m_creators.find(id);
			if (it == m_creators.end()) return nullptr;
			return it->second();
		}

		bool has(id_type id) const { return m_creators.count(id) != 0; }
	};

	// 多线程工厂
	template<class base_type, class id_type>
	class factory<base_type, id_type, xhqm::multi_thread>
	{
		mutable std::mutex m_mutex;
		std::map<id_type, std::function<std::shared_ptr<base_type>()>> m_creators;
	public:
		template<class derived_type>
		void regist(id_type id)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_creators[id] = []() -> std::shared_ptr<base_type> { return std::make_shared<derived_type>(); };
		}

		std::shared_ptr<base_type> create(id_type id) const
		{
			std::function<std::shared_ptr<base_type>()> creator;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				auto it = m_creators.find(id);
				if (it == m_creators.end()) return nullptr;
				creator = it->second;
			}
			return creator();
		}

		bool has(id_type id) const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_creators.count(id) != 0;
		}
	};
}

#endif
