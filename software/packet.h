#ifndef XHQM_SOFTWARE_PACKET
#define XHQM_SOFTWARE_PACKET

#include "../lattice/tree.hpp"
#include "../software/pattern/instance.h"
#include "../software/datum.h"
#include "../network/uuid.h"
#include "../head.h"

namespace xhqm
{
	struct object_type;

	template<typename> class regist;

	class xhqm_io object : public xhqm::derive_tree<xhqm::object>
	{
	protected:
		xhqm::text m_type;
		xhqm::text m_uuid;
		xhqm::text m_name;
		xhqm::text m_icon;
		xhqm::text m_info;
		xhqm::datum m_attribute;
	protected:
		explicit object();
		virtual ~object();
		object(object&&) = delete;
		object(const object&) = delete;
		virtual void save(void) = 0;
		virtual void read(void) = 0;
	public:
		inline const xhqm::text& type(void) const { return m_type; };
		inline const xhqm::text& uuid(void) const { return m_uuid; };
		inline const xhqm::text& name(void) const { return m_name; };
		inline const xhqm::text& icon(void) const { return m_icon; };
		inline const xhqm::text& info(void) const { return m_info; };
		inline void type(const xhqm::text& value) { m_type = value; };
		inline void uuid(const xhqm::text& value) { m_uuid = value; };
		inline void name(const xhqm::text& value) { m_name = value; };
		inline void icon(const xhqm::text& value) { m_icon = value; };
		inline void info(const xhqm::text& value) { m_info = value; };

		template<typename type>
		type& attribute(const xhqm::text& name)
		{
			return (*(m_attribute.value_ptr<type>(name)));
		}

		template<typename type> 
		type* type_ptr()
		{
			return dynamic_cast<type*>(this);
		}
	};

	template<typename _object_type> class regist
	{
		friend struct xhqm::object_type;
		inline static xhqm::text regist_type = "";
	public:
		regist(const xhqm::text& type)
		{
			using function = std::function<xhqm::object* (void)>;
			using regist_map = std::map<xhqm::text, function>;

			regist_type = type;
			if (regist_type == "")
			{
				throw std::string("unknow object_type");
			}
			function create = []()
				->xhqm::object* 
			{ 
				return (new _object_type())->object_ptr();
			};

			xhqm::instance<regist_map>::get()[regist_type] = create;
		}
	};

	struct object_type
	{
		template<typename _type> static xhqm::text name()
		{
			return xhqm::regist<_type>::regist_type;
		}
	};

	namespace software
	{
		class xhqm_io packet : protected xhqm::object
		{
		protected:
			bool m_active = false;
			bool m_select = false;
		public:
			explicit packet();
			virtual ~packet();
			packet(packet&&) = delete;
			packet(const packet&) = delete;
			virtual void save(void) override;
			virtual void read(void) override;
			inline const bool& select(void) const { return m_select; };
			inline void select(const bool& value) { m_select = value; };
			inline const bool& active(void) const { return m_active; };
			inline void active(const bool& value) { m_active = value; };

			software::packet* root_ptr()
			{
				return this->xhqm::object::root_ptr()->xhqm::object::type_ptr<software::packet>();
			}

			xhqm::object* object_ptr()
			{
				return this->xhqm::object::type_ptr<xhqm::object>();
			}

			template<typename type> type* dynamic_ptr()
			{
				return this->xhqm::object::type_ptr<type>();
			}
		};
	}
}

#define xhqm_regist(type, space, _regist) namespace space { xhqm::regist<space::type> class_##type(#space "::" #type); }
//#define xhqm_regist(type, space, _regist) namespace space { xhqm::regist<space::type> XHQMCONNECTSTR(class_, type)(XHQMTOSTR(XHQMCONNECTSTR(space::,type))); }
#define xhqm_regist_create(object_type) xhqm::object* create_object() { return (new object_type())->object_ptr(); }

#endif