#include "model_ecs.h"
#include <algorithm>

namespace xhqm
{
	namespace fantasy
	{
		model_ecs::model_ecs()
		{
		}

		model_ecs::~model_ecs()
		{
			delete_tuples();
		}

		void model_ecs::delete_tuples()
		{
			for (auto tuple : m_tuples)
			{
				delete tuple;
				tuple = nullptr;
			}
			m_tuples.clear();
		}

		std::vector<ecs_tuple*> model_ecs::handle_tuples()
		{
			return m_tuples;
		}

		xhqm::index model_ecs::attach(ecs_tuple* tuple)
		{
			xhqm::index index = tuple ? m_tuples.size() : xhqm::npos;
			if (index != xhqm::npos)
			{
				m_tuples.push_back(tuple);
				m_sync_version = xhqm::npos;   // 组件存储变更，强制重新同步
			}
			return index;
		}

		xhqm::count model_ecs::detach(ecs_tuple* tuple)
		{
			auto iter = std::find(m_tuples.begin(), m_tuples.end(), tuple);
			if (iter != m_tuples.end()) { m_tuples.erase(iter); m_sync_version = xhqm::npos; }
			return m_tuples.size();
		}

		ecs_tuple* model_ecs::detach(xhqm::index pos)
		{
			ecs_tuple* tuple = nullptr;
			if (pos < m_tuples.size() && pos != xhqm::npos)
			{
				tuple = m_tuples[pos];
				m_tuples.erase(m_tuples.begin() + pos);
				m_sync_version = xhqm::npos;
			}
			return tuple;
		}

		ecs_tuple* model_ecs::handle_tuple(xhqm::index pos)
		{
			ecs_tuple* tuple = nullptr;
			if (pos < m_tuples.size() && pos != xhqm::npos)
			{
				tuple = m_tuples[pos];
			}
			return tuple;
		}

		xhqm::size model_ecs::entity_size() const
		{
			return m_tuples.empty() ? 0 : m_tuples[0]->size();
		}

		bool model_ecs::sync_instances()
		{
			//定位 position 组件存储（SoA 三列优先，AoS point 单列次之）
			xhqm::tuple_values<value, value, value>* soa_values = nullptr;
			xhqm::tuple_values<fantasy::point>* aos_values = nullptr;
			if (auto* soa = handle_tuple<value, value, value>()) soa_values = &soa->values();
			else if (auto* aos = handle_tuple<fantasy::point>()) aos_values = &aos->values();
			else return false;

			//版本守卫：position 组件数据未变则跳过（theory_instancing 与 render 同帧双调用去重）
			const xhqm::size data_version = soa_values ? soa_values->version() : aos_values->version();
			if (data_version == m_sync_version) return true;

			std::vector<fantasy::matrix> instances;
			if (soa_values)
			{
				//SoA 三列：out 参数零构造读取，逐实体生成平移矩阵
				instances.reserve(soa_values->size());
				for (xhqm::index i = 0; i < soa_values->size(); ++i)
				{
					value x, y, z;
					soa_values->get_value(i, x, y, z);
					instances.push_back(xhqm::translate(x, y, z));
				}
			}
			else
			{
				//AoS 单列：引用直读，逐实体生成平移矩阵
				instances.reserve(aos_values->size());
				for (xhqm::index i = 0; i < aos_values->size(); ++i)
				{
					const fantasy::point& p = aos_values->value_ref(i);
					instances.push_back(xhqm::translate(p.x(), p.y(), p.z()));
				}
			}
			for (fantasy::drawer* drawer : handle_drawers())
			{
				if (auto* prefab_ptr = dynamic_cast<fantasy::prefab*>(drawer))
				{
					prefab_ptr->instances(instances);
				}
			}
			m_sync_version = data_version;
			return true;
		}

		void model_ecs::render()
		{
			sync_instances();
			fantasy::models::render();
		}
	}
}
