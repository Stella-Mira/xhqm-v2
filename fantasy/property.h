#ifndef XHQM_FANTASY_PROPERTY
#define XHQM_FANTASY_PROPERTY

#include "fantasy.h"
#include "element.h"
#include "system.h"
#include "tonemap.h"
#include "shadow.h"

namespace xhqm
{
	namespace fantasy
	{
		class xhqm_io coord
			: public fantasy::models
		{
			struct _packet;
			std::shared_ptr<coord::_packet> m_packet;
			friend class theory_coord_arrow;
		public:
			explicit coord();
			virtual ~coord();

			virtual void render() override;
		};
	}

	namespace fantasy
	{
		class theory_frame_graph;
		class theory_pass;

		class xhqm_io theory_camera_render
			: public fantasy::theory
		{
			fantasy::camera* m_camera_ptr = nullptr;
			// 内部管线：fg + passes 由本 theory 手动驱动（不挂 world 子树）
			theory_frame_graph* m_fg = nullptr;
			std::vector<theory_pass*> m_passes;
		public:
			explicit theory_camera_render() : theory_camera_render(nullptr) { };
			explicit theory_camera_render(fantasy::camera* camera_ptr);
			virtual ~theory_camera_render();

			inline fantasy::camera* camera_ptr(void) const { return m_camera_ptr; };
			inline void camera_ptr(fantasy::camera* value) { m_camera_ptr = value; };

			virtual void life() override;
		};

		class xhqm_io theory_system_pose
			: public fantasy::theory
		{
			fantasy::point m_window_post;
			fantasy::models* m_models_ptr = nullptr;
			fantasy::system* m_system_ptr = nullptr;
			fantasy::camera* m_camera_ptr = nullptr;
		public:
			explicit theory_system_pose() : theory_system_pose(nullptr) { };
			explicit theory_system_pose(fantasy::models* models_ptr);

			inline fantasy::point window_post(void) const { return m_window_post; };
			inline void window_post(fantasy::point value) { m_camera_ptr = nullptr; m_window_post = value; };

			inline fantasy::models* models_ptr(void) const { return m_models_ptr; };
			inline void models_ptr(fantasy::models* value) { m_camera_ptr = nullptr; m_models_ptr = value; };

			inline fantasy::system* system_ptr(void) const { return m_system_ptr; };
			inline void system_ptr(fantasy::system* value) { m_camera_ptr = nullptr; m_system_ptr = value; };

			virtual void life() override;
		};
	}
}
#endif
