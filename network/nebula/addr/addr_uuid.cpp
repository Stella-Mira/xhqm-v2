#include "addr_uuid.h"

namespace xhqm
{
	namespace nebula
	{
		static std::mutex uuid_mutex;
		static xhqm::software::signal<const json&> uuid_broad;
		static std::map<nebula::uuid, nebula::addr_uuid*, uuid_less> uuid_registry;


		void addr_uuid::_frame_signal_(const nebula::json& value)
		{
			frame_signal.emit(value);
		}

		void addr_uuid::_broad_signal_(const nebula::json& value)
		{
			broad_signal.emit(value);
		}

		addr_uuid::addr_uuid(const nebula::uuid& _uuid) : addr_b(xhqm::nebula::kind::uuid, _uuid)
		{
			std::lock_guard<std::mutex> lock(uuid_mutex);
			auto it = uuid_registry.find(uuid());
			if (it == uuid_registry.end())
			{
				uuid_registry[uuid()] = this;
			}
			else
			{
				frame_signal.connect(it->second, &addr_uuid::_frame_signal_);
			}
			uuid_broad.connect(this, &addr_uuid::_broad_signal_);
		}

		addr_uuid::~addr_uuid()
		{
			{
				// 退注册表：否则表里留着悬垂指针，下一个同 uuid 的克隆会把槽连到已释放对象上
				std::lock_guard<std::mutex> lock(uuid_mutex);
				auto it = uuid_registry.find(uuid());
				if (it != uuid_registry.end() && it->second == this) { uuid_registry.erase(it); }
			}
			frame_signal.disconnect(this);
			uuid_broad.disconnect(this);
		}


		bool addr_uuid::opened()
		{
			return (m_opened = true);
		}

		void addr_uuid::closed()
		{
			m_opened = false;
		}

		bool addr_uuid::send_broad(const json& value)
		{
			uuid_broad.emit(value); return true;
		}

		bool addr_uuid::send_frame(const json& value)
		{
			// 不按 m_opened 拦截：克隆句柄（addr_from 造、挂进邻居表的那种）本来就不会 opened，
			// 它的 frame_signal 在构造时已连到实体端点的转发槽 —— 一拦，邻居方向的帧就全丢。
			frame_signal.emit(value);
			return true;
		}

		bool addr_uuid::send_frame(const json& value, sint)
		{
			return send_frame(value);
		}
	}
}
