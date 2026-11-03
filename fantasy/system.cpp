#include "system.h"
#include "camera.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		struct system::_packet
		{
			fantasy::matrix system = fantasy::matrix(4, 4);
		};

		struct models::_packet
		{
			bool show = true;
			bool card = true;
			fantasy::aabb aabb;
			std::vector<fantasy::drawer*> drawers;
		};

		struct scenes::_packet
		{
			fantasy::object* m_theory = nullptr;//运行法则
			fantasy::system* m_target = nullptr;//模型目标
			fantasy::camera* m_camera = nullptr;//相机道具
			fantasy::system* m_cameras = nullptr;//多相机子树（附加相机，单位 coord，与默认相机不串联）
		};


		theory::theory()
		{
		}
		theory::~theory()
		{
		}

		system::system()
		{
			system::m_packet = std::make_shared<system::_packet>();
		}
		system::~system()
		{

		}

		models::models()
		{
			m_packet = std::make_shared<models::_packet>();
		}
		models::~models()
		{
			delete_drawers();
		}

		scenes::scenes()
		{
			m_packet = std::make_shared<scenes::_packet>();

			{
				m_packet->m_theory = new fantasy::models();
				m_packet->m_target = new fantasy::system();
				m_packet->m_camera = new fantasy::camera();
				m_packet->m_cameras = new fantasy::system();
				this->add_child_ptr(m_packet->m_theory);
				this->add_child_ptr(m_packet->m_target);
				this->add_child_ptr(m_packet->m_camera);
				this->add_child_ptr(m_packet->m_cameras);
			}
		}
		scenes::~scenes()
		{
			auto delete_fun = [](fantasy::object* object_ptr) { delete object_ptr; };
			m_packet->m_theory->iterate_dfs<xhqm::npos>(delete_fun);
			m_packet->m_target->iterate_dfs<xhqm::npos>(delete_fun);
			m_packet->m_camera->iterate_dfs<xhqm::npos>(delete_fun);
			m_packet->m_cameras->iterate_dfs<xhqm::npos>(delete_fun);
		}


		fantasy::matrix system::transform(fantasy::object const* system_old, fantasy::object const* system_new)
		{
			fantasy::matrix mat_old(4, 4);
			fantasy::matrix mat_new(4, 4);
			fantasy::object const* root = nullptr;
			fantasy::object const* curr = nullptr;
			fantasy::system const* system_ptr = nullptr;

			curr = system_old;
			while (curr)
			{
				root = curr;
				curr = curr->parent_ptr();
				system_ptr = dynamic_cast<fantasy::system const*>(root);
				if (system_ptr) { mat_old = system_ptr->coord() * mat_old; }
			}
			curr = system_new;
			while (curr)
			{
				root = curr;
				curr = curr->parent_ptr();
				system_ptr = dynamic_cast<fantasy::system const*>(root);
				if (system_ptr) { mat_new = system_ptr->coord() * mat_new; }
			}

			return (mat_new.invmat() * mat_old);
		}

		void system::traverse(const fantasy::matrix& window_mat, fantasy::object* object_ptr, fantasy::matrix system_mat)
		{
			fantasy::system* system_ptr = dynamic_cast<fantasy::system*>(object_ptr);
			if (system_ptr)
			{
				system_mat = system_mat * system_ptr->coord();
				fantasy::models* models_ptr = dynamic_cast<fantasy::models*>(object_ptr);
				if (models_ptr)
				{
					models_ptr->drawer_system(window_mat * system_mat);
					models_ptr->drawer_world(system_mat);
				}
			}
			if (object_ptr)
			{
				auto childs = object_ptr->childs();
				for (auto& child_ptr : childs)
				{
					system::traverse(window_mat, child_ptr, system_mat);
				}
			}
		}
	
		void models::drawer(fantasy::object* object_ptr)
		{
			if (object_ptr)
			{
				for (auto* _object_ptr : (*object_ptr)) //广度遍历
				{
					fantasy::models* models_ptr = dynamic_cast<fantasy::models*>(_object_ptr);
					if (models_ptr) { models_ptr->render(); }
				}
			}
		}

		void models::idcard(fantasy::object* object_ptr)
		{
			if (object_ptr)
			{
				render_finish();
				for (auto* _object_ptr : (*object_ptr)) //广度遍历
				{
					fantasy::models* models_ptr = dynamic_cast<fantasy::models*>(_object_ptr);
					if (models_ptr) { models_ptr->idcard(); }
				}
				render_finish();
			}
		}
	}

	namespace fantasy
	{ 
		const fantasy::matrix& system::coord(void) const
		{
			return system::m_packet->system;
		}

		void system::coord(const fantasy::matrix& value)
		{
			system::m_packet->system = value;
		}

		void system::mul_coord(const fantasy::matrix& coord)
		{
			system::m_packet->system = system::m_packet->system * coord;
		}

		void system::coord_mul(const fantasy::matrix& coord)
		{
			system::m_packet->system = coord * system::m_packet->system;
		}

		void system::apply_world(const fantasy::matrix& M)
		{
			// 对象世界 = parent.coord * coord，世界变换 M 作用于对象：
			//   new_coord = parent⁻¹ · M · parent · coord
			fantasy::matrix parent(4, 4);
			fantasy::object* p = this->parent_ptr();
			if (p)
			{
				if (fantasy::system* ps = dynamic_cast<fantasy::system*>(p)) parent = ps->coord();
			}
			this->coord_mul(parent.invmat() * M * parent);
		}


		xhqm::vect<value> models::coord_dir_x(fantasy::system const* coord_ptr) const
		{
			const auto& sys = system::transform(this, coord_ptr);
			return vect<value>(sys(0, 0), sys(1, 0), sys(2, 0)).normal();
		}

		xhqm::vect<value> models::coord_dir_y(fantasy::system const* coord_ptr) const
		{
			const auto& sys = system::transform(this, coord_ptr);
			return vect<value>(sys(0, 1), sys(1, 1), sys(2, 1)).normal();
		}

		xhqm::vect<value> models::coord_dir_z(fantasy::system const* coord_ptr) const
		{
			const auto& sys = system::transform(this, coord_ptr);
			return vect<value>(sys(0, 2), sys(1, 2), sys(2, 2)).normal();
		}

		xhqm::vect<value> models::coord_vec_o(fantasy::system const* coord_ptr) const
		{
			const auto& sys = system::transform(this, coord_ptr);
			return vect<value>(sys(0, 3), sys(1, 3), sys(2, 3));
		}

		void models::coord(fantasy::system const* coord_ptr, const xhqm::poit<value>& pos, const xhqm::vect<value>& ypr)
		{


		}

		xhqm::vect<value> models::micro_pos() const
		{
			auto coord = this->system::coord();
			return xhqm::vect<value>(coord(0, 3), coord(1, 3), coord(2, 3));
		}

		xhqm::vect<value> models::micro_rpy() const
		{
			const auto& sys = this->system::coord();

			// 提取旋转部分（左上角3x3矩阵）
			fantasy::matrix rotation = sys.kidmat(2, 2);
			fantasy::value yaw = 0, pitch = 0, roll = 0;
			const fantasy::value pi = static_cast<fantasy::value>(xhqm::pi);

			// 从旋转矩阵中提取欧拉角（ZYX顺序）
			if (std::abs(rotation(2, 0)) < (1 - xhqm::preci<fantasy::value>))
			{
				// 非奇异情况
				yaw = std::atan2(rotation(1, 0), rotation(0, 0));
				pitch = std::asin(-rotation(2, 0));
				roll = std::atan2(rotation(2, 1), rotation(2, 2));
			}
			else
			{
				// 奇异情况（万向节锁）
				yaw = std::atan2(-rotation(0, 1), rotation(1, 1));
				pitch = ((rotation(2, 0) > 0) ? -pi : +pi) / 2;
				roll = 0;
			}

			return xhqm::vect<value>(yaw, pitch, roll);
		}

		void models::micro(const xhqm::poit<value>& pos, const xhqm::vect<value>& ypr)
		{
			fantasy::matrix move = xhqm::translate(pos.x(), pos.y(), pos.z());
			fantasy::matrix rota = xhqm::rotate_z(ypr[0]) * xhqm::rotate_y(ypr[1]) * xhqm::rotate_x(ypr[2]);
			this->system::coord(move * rota);
		}

		void models::macro(const xhqm::poit<value>& pos, const xhqm::vect<value>& ypr)
		{
			fantasy::matrix move = xhqm::translate(pos.x(), pos.y(), pos.z());
			fantasy::matrix rota = xhqm::rotate_z(ypr[0]) * xhqm::rotate_y(ypr[1]) * xhqm::rotate_x(ypr[2]);
			fantasy::matrix matrix = system::transform(this->root_ptr(), this->parent_ptr());
			this->system::coord(matrix * move * rota);
		}

		void models::show(bool value)
		{
			models::m_packet->show = value;
		}

		bool models::show(void) const
		{
			return models::m_packet->show;
		}

		void models::inter(bool value)
		{
			models::m_packet->card = value;
		}

		bool models::inter(void) const
		{
			return models::m_packet->card;
		}

		void models::drawer_select(const bool& value)
		{
			for (auto drawer : models::m_packet->drawers)
			{
				drawer->set_select(value);
			}
		}

		void models::drawer_system(const fantasy::matrix& value)
		{
			for (auto drawer : models::m_packet->drawers)
			{
				drawer->system(value);
			}
		}

		void models::drawer_world(const fantasy::matrix& value)
		{
			for (auto drawer : models::m_packet->drawers)
			{
				drawer->world(value);
			}
		}

		fantasy::aabb models::updata_aabb()
		{
			fantasy::aabb model_aabb;
			if (!models::m_packet->drawers.empty())
			{
				model_aabb = models::m_packet->drawers.front()->aabb_size();
				for (auto drawer : models::m_packet->drawers)
				{
					model_aabb.attach(drawer->aabb_size());
				}
			}
			return model_aabb;// .access(system::m_packet->system);
		}

		fantasy::point models::world_center() const
		{
			fantasy::aabb box;
			bool has = false;
			for (auto* o : (*this))   // 广度遍历（含自身）
			{
				fantasy::models* mm = dynamic_cast<fantasy::models*>(o);
				if (!mm || !mm->show()) continue;
				for (fantasy::drawer* d : mm->handle_drawers())
				{
					fantasy::aabb local = d->aabb_size();
					if (!local.good()) continue;
					fantasy::matrix w = d->world();
					fantasy::point mn = local.min, mx = local.max;
					fantasy::point corners[8] = {
						fantasy::point(mn.x(), mn.y(), mn.z()), fantasy::point(mn.x(), mn.y(), mx.z()),
						fantasy::point(mn.x(), mx.y(), mn.z()), fantasy::point(mn.x(), mx.y(), mx.z()),
						fantasy::point(mx.x(), mn.y(), mn.z()), fantasy::point(mx.x(), mn.y(), mx.z()),
						fantasy::point(mx.x(), mx.y(), mn.z()), fantasy::point(mx.x(), mx.y(), mx.z()),
					};
					for (auto& c : corners)
					{
						fantasy::point wc = c * w.tramat();   // 行向量 * 转置矩阵 = 列向量变换（平移生效）
						if (!has) { box.min = wc; box.max = wc; has = true; }
						else box.attach(wc);
					}
				}
			}
			if (!has) return fantasy::point(0, 0, 0);
			return fantasy::point((box.min.x() + box.max.x()) * value(0.5),
				(box.min.y() + box.max.y()) * value(0.5),
				(box.min.z() + box.max.z()) * value(0.5));
		}

		void models::delete_drawers()
		{
			for (auto drawer : models::m_packet->drawers)
			{
				delete drawer;
				drawer = nullptr;
			}
			models::m_packet->drawers.clear();
		}

		std::vector<fantasy::drawer*> models::handle_drawers()
		{
			return models::m_packet->drawers;
		}

		xhqm::index models::insert_drawer(fantasy::drawer* obj)
		{
			xhqm::index index = obj ? models::m_packet->drawers.size() : xhqm::npos;
			if (index != xhqm::npos)
			{
				models::m_packet->drawers.push_back(obj);
				obj->updata();
			}
			return index;
		}

		xhqm::count models::remove_drawer(fantasy::drawer* obj)
		{
			auto iter = std::find(models::m_packet->drawers.begin(), models::m_packet->drawers.end(), obj);
			if (iter != models::m_packet->drawers.end()) { models::m_packet->drawers.erase(iter); }
			return models::m_packet->drawers.size();
		}

		fantasy::drawer* models::remove_drawer(xhqm::index pos)
		{
			fantasy::drawer* drawer = nullptr;
			if (pos < models::m_packet->drawers.size() && pos != xhqm::npos)
			{
				drawer = models::m_packet->drawers[pos];
				models::m_packet->drawers.erase(models::m_packet->drawers.begin() + pos);
			}
			return drawer;
		}

		fantasy::drawer* models::handle_drawer(xhqm::index pos)
		{
			fantasy::drawer* drawer = nullptr;
			if (pos < models::m_packet->drawers.size() && pos != xhqm::npos)
			{
				drawer = models::m_packet->drawers[pos];
			}
			return drawer;
		}

		void models::render()
		{
			if (models::m_packet->show)
			{
				for (fantasy::drawer* drawer : this->models::m_packet->drawers)
				{
					drawer->render();
				}
			}
		}

		void models::idcard()
		{
			if (models::m_packet->show && models::m_packet->card)
			{
				for (fantasy::drawer* drawer : this->models::m_packet->drawers)
				{
					drawer->render_id(this->object_id());
				}
			}
		}


		fantasy::object* scenes::theory() const
		{
			return m_packet->m_theory;
		}

		fantasy::system* scenes::target() const
		{
			return m_packet->m_target;
		}

		fantasy::camera* scenes::camera() const
		{
			return m_packet->m_camera;
		}

		fantasy::system* scenes::cameras() const
		{
			return m_packet->m_cameras;
		}

		bool scenes::add_theory(fantasy::object* object_ptr)
		{
			return m_packet->m_theory->add_child_ptr(object_ptr);
		}

		bool scenes::add_target(fantasy::models* object_ptr)
		{
			return m_packet->m_target->add_child_ptr(object_ptr);
		}

		bool scenes::add_fixeds(fantasy::models* object_ptr)
		{
			return m_packet->m_camera->add_child_ptr(object_ptr);
		}

		bool scenes::add_camera(fantasy::models* object_ptr)
		{
			return m_packet->m_cameras->add_child_ptr(object_ptr);
		}

		bool scenes::sub_theory(fantasy::object* object_ptr)
		{
			return m_packet->m_theory->sub_child_ptr(object_ptr);
		}

		bool scenes::sub_target(fantasy::models* object_ptr)
		{
			return m_packet->m_target->sub_child_ptr(object_ptr);
		}

		bool scenes::sub_fixeds(fantasy::models* object_ptr)
		{
			return m_packet->m_camera->sub_child_ptr(object_ptr);
		}

		bool scenes::sub_camera(fantasy::models* object_ptr)
		{
			return m_packet->m_cameras->sub_child_ptr(object_ptr);
		}
	}
}
