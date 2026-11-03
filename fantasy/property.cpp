#include "property.h"
#include "theory_render.h"
#include "camera.h"

namespace xhqm
{
	namespace fantasy
	{
		theory_camera_render::theory_camera_render(fantasy::camera* camera_ptr)
		{
			m_camera_ptr = camera_ptr;
		}

		theory_camera_render::~theory_camera_render()
		{
			for (auto* p : m_passes) delete p;
			m_passes.clear();
			if (m_fg) { delete m_fg; m_fg = nullptr; }
		}

		void theory_camera_render::life()
		{
			if (!m_camera_ptr) return;

			// 懒创建内部管线：shadow + scene + tonemap（等价旧默认管线，改为 theory_pass 编排）
			if (!m_fg)
			{
				m_fg = new theory_frame_graph();
				m_passes.push_back(new theory_shadow(m_camera_ptr));
				m_passes.push_back(new theory_scene(m_camera_ptr));
				m_passes.push_back(new theory_tonemap(m_camera_ptr));
				for (auto* p : m_passes) p->fg(m_fg);
			}

			// 手动驱动各 pass（等价 alive 遍历，因不挂 world 子树）
			for (auto* p : m_passes) p->life();
			m_fg->life();
		}

		theory_system_pose::theory_system_pose(fantasy::models* models_ptr)
		{
			m_models_ptr = models_ptr;
		}

		void theory_system_pose::life()
		{
			if (m_system_ptr && m_models_ptr)
			{
				if (nullptr == m_camera_ptr)
				{
					m_camera_ptr = dynamic_cast<fantasy::camera*>(m_models_ptr->parent_ptr());
				}

				if (m_camera_ptr)
				{
					fantasy::matrix window = (m_camera_ptr->proj() * m_camera_ptr->view()).invmat();
					fantasy::matrix trans = xhqm::translate(m_window_post.operator xhqm::vect<fantasy::value>());
					fantasy::matrix scale = xhqm::scale(0.2f, 0.2f, 0.2f) * m_camera_ptr->view();
					fantasy::matrix coord = xhqm::extract_rote(fantasy::system::transform(m_system_ptr, m_camera_ptr));
					m_models_ptr->system::coord(window * trans * scale * coord);
				}
			}
		}

	}
	namespace fantasy
	{
		struct coord::_packet
		{
			fantasy::poits point_o;
			fantasy::arrow arrow_x;
			fantasy::arrow arrow_y;
			fantasy::arrow arrow_z;
		};

		coord::coord()
		{
			coord::m_packet = std::make_shared<coord::_packet>();
			coord::m_packet->point_o.poits_data({ xhqm::geometry::poit(0,0,0) });
			coord::m_packet->arrow_x.pose(geometry::vect(1, 0, 0));
			coord::m_packet->arrow_y.pose(geometry::vect(0, 1, 0));
			coord::m_packet->arrow_z.pose(geometry::vect(0, 0, 1));
			coord::m_packet->point_o.texture_colour(0xffffffff);
			coord::m_packet->arrow_x.texture_colour(0xff0000ff);
			coord::m_packet->arrow_y.texture_colour(0xff00ff00);
			coord::m_packet->arrow_z.texture_colour(0xffff0000);
			coord::m_packet->point_o.poit_size(11);
			insert_drawer(&coord::m_packet->point_o);
			insert_drawer(&coord::m_packet->arrow_x);
			insert_drawer(&coord::m_packet->arrow_y);
			insert_drawer(&coord::m_packet->arrow_z);
		}
		coord::~coord()
		{
			remove_drawer(&coord::m_packet->point_o);
			remove_drawer(&coord::m_packet->arrow_x);
			remove_drawer(&coord::m_packet->arrow_y);
			remove_drawer(&coord::m_packet->arrow_z);
		}

		void coord::render()
		{
			coord::m_packet->point_o.set_select(false);
			coord::m_packet->arrow_x.set_select(false);
			coord::m_packet->arrow_y.set_select(false);
			coord::m_packet->arrow_z.set_select(false);
			this->fantasy::models::render();
		}
	}
}

