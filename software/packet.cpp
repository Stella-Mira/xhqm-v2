#include "packet.h"
#include "system/filesys.h"

namespace xhqm
{
	object::object()
	{
		object::uuid(xhqm::cipher::uuid::to_text(xhqm::cipher::uuid::creat_uuid_v4()));
		m_attribute.resigt("uuid", &m_uuid);
		m_attribute.resigt("name", &m_name);
		m_attribute.resigt("icon", &m_icon);
		m_attribute.resigt("info", &m_info);
		m_attribute.resigt("type", &m_type);
		object::type("object");
	}

	object::~object()
	{

	}

	namespace software
	{
		packet::packet()
		{
			m_attribute.resigt("select", &m_select);
			m_attribute.resigt("active", &m_active);
			object::type("packet");
		}

		packet::~packet()
		{

		}

		void packet::save(void)
		{
			std::string path = xhqm::filesys::current_path() + "/packet/" + uuid();
			xhqm::datum::save_file(path, this->m_attribute);
		}

		void packet::read(void)
		{
			std::string path = xhqm::filesys::current_path() + "/packet/" + uuid();
			xhqm::datum::read_file(path, this->m_attribute);
		}
	}
}

xhqm_regist(packet, xhqm::software, );