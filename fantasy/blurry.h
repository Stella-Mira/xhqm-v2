#ifndef XHQM_FANTASY_BLURRY
#define XHQM_FANTASY_BLURRY

#include "fantasy.h"
#include "drawer.h"
#include <thread>
#include <atomic>
#include <mutex>

namespace xhqm::fantasy
{
	//难名其形，难言其灵
	class xhqm_io blurry
	{
	protected:
		explicit blurry() {};
		virtual ~blurry() {};
		blurry(blurry&&) = delete;
		blurry(const blurry&) = delete;
	};

	class xhqm_io object : public fantasy::blurry
		, public xhqm::derive_tree<xhqm::fantasy::object>
	{
	public:
		inline static std::mutex mutex;
	private:
		fantasy::id m_object = 0;
		xhqm::string m_name = "";
		xhqm::string m_info = "";
		bool m_used = true;
	protected:
		object(object&&) = delete;
		object(const object&) = delete;
	public:
		explicit object()
		{
			m_object = fantasy::render_id();
			m_name = xhqm::cipher::uuid::to_text(xhqm::cipher::uuid::creat_uuid_v4());
			m_info = xhqm::cipher::uuid::to_text(xhqm::cipher::uuid::creat_uuid_v4());
		}
		virtual ~object()
		{
			m_object = 0;
		};

		inline bool used(void) const { return m_used; }
		inline void used(bool value) { m_used = value; }
		inline const fantasy::id& object_id() const { return m_object; }
		inline const std::string& name(void) const { return m_name; }
		inline const std::string& info(void) const { return m_info; }
		inline void name(const std::string& value) { m_name = value; }
		inline void info(const std::string& value) { m_info = value; }

		// 存活：每帧主动行为（规则/场景/模型/相机等一切树节点）
		virtual void life() = 0;
		// 触发：被动事件（模型被触发的能力/回调）
		virtual void emit() = 0;

		static void alive(fantasy::object*);
	};
}
#endif
