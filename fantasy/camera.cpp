#include "camera.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		camera::camera()
		{
			// 背景：全屏 quad（z=0.99999 最远），跟随相机（屏幕空间恒定）
			{
				const value face_z = value(0.99999f);
				std::vector<std::array<point, 3>> vertex(2);
				std::vector<std::array<value, 2>> flipuv(6);
				vertex[0][0] = point(-1, +1, face_z); flipuv[0] = { 0, 0 };
				vertex[0][1] = point(+1, +1, face_z); flipuv[1] = { 1, 0 };
				vertex[0][2] = point(-1, -1, face_z); flipuv[2] = { 0, 1 };
				vertex[1][0] = point(-1, -1, face_z); flipuv[3] = { 0, 1 };
				vertex[1][1] = point(+1, +1, face_z); flipuv[4] = { 1, 0 };
				vertex[1][2] = point(+1, -1, face_z); flipuv[5] = { 1, 1 };
				m_background.size(2);
				m_background.vertex(vertex);
				m_background.flipuv(flipuv);
				m_background.layer(face_z);
				m_background.updata();
			}
			resize(m_view_w, m_view_h, m_view_d);
			// 初始朝向：与 yaw/pitch 一致
			m_orient = xhqm::rotate_z(m_yaw) * xhqm::rotate_x(xhqm::radians(value(90)) - m_pitch);
		}
		camera::~camera()
		{
			if (m_pick_target)
			{
				render_backend_active().delete_offscreen(m_pick_target);
				m_pick_target = 0;
			}
		}

		void camera::resize(uint w, uint h, uint d)
		{
			if (w == 0 || h == 0 || d == 0) return;
			m_view_w = w; m_view_h = h; m_view_d = d;
			const value vw = (value)w, vh = (value)h, vd = (value)d;
			const value base_w = 540, base_h = 540;
			if (m_ortho)
			{
				m_view_scale = xhqm::scale(base_w / vw, base_h / vh, 1 / vd);
				m_proj = xhqm::ortho(-vd, +vd, -vd, +vd, (value)0, (value)2).tramat();
			}
			else
			{
				// 均匀缩放（高度基准）：透视 proj 已含 aspect 补偿，非均匀会拉伸形变
				m_view_scale = xhqm::scale(base_h / vh, base_h / vh, (value)1);
				m_proj = xhqm::perspective(m_fov, vw / vh, m_near, m_far);
			}
			render_window(0, 0, w, h);
		}

		void camera::ortho(bool v)
		{
			m_ortho = v;
			resize(m_view_w, m_view_h, m_view_d);
		}

		object* camera::slected(uint view_x, uint view_y)
		{
			object* root = this->root_ptr();
			if (!root) return nullptr;
			render_backend& rhi = render_backend_active();

			// 视图矩阵已内聚：window = proj × view()（含自身坐标逆），无需再 transform(root, this)
			matrix window = this->window();   // 含视口变换（与渲染一致）

			// 懒创建/重建拾取离屏
			if (m_pick_target == 0 || m_pick_w != m_view_w || m_pick_h != m_view_h)
			{
				if (m_pick_target) rhi.delete_offscreen(m_pick_target);
				m_pick_target = 0;
				std::string err;
				m_pick_target = rhi.create_offscreen(m_view_w, m_view_h, 1, err);
				m_pick_w = m_view_w; m_pick_h = m_view_h;
			}
			if (m_pick_target == 0) return nullptr;

			rhi.begin_offscreen(m_pick_target);
			render_clearn(xhqm_rgba(0, 0, 0, 0));
			system::traverse(window, root, matrix(4, 4));
			models::idcard(root);
			rhi.end_offscreen();

			rgba object_id = 0;
			rhi.read_offscreen(m_pick_target, (sint)view_x, (sint)view_y, &object_id);
			for (auto* obj : (*root))
				if (obj->object_id() == object_id) return obj;
			return nullptr;
		}

		void camera::orbit_add(value dyaw, value dpitch)
		{
			// trackball 旋转：绕当前屏幕轴累积（无万向锁，可越过头顶/脚底连续旋转）
			const matrix& R = m_orient;
			xhqm::vect<value> right(R(0, 0), R(1, 0), R(2, 0));   // +x 列 = 屏幕右
			xhqm::vect<value> up(R(0, 1), R(1, 1), R(2, 1));      // +y 列 = 屏幕上
			// 左右拖绕屏幕上轴（方位），上下拖绕屏幕右轴（俯仰）
			matrix delta = xhqm::rotate_v(up, -dyaw) * xhqm::rotate_v(right, dpitch);
			m_orient = delta * m_orient;   // 左乘累积
			// 同步球坐标（仅作 getter 参考）
			m_yaw += dyaw;
			m_pitch += dpitch;
		}

		void camera::focus_move(value forward, value right)
		{
			const matrix& R = m_orient;
			xhqm::vect<value> fwd(-R(0, 2), -R(1, 2), -R(2, 2));   // 视线 = -z 列
			xhqm::vect<value> rgt(R(0, 0), R(1, 0), R(2, 0));      // 屏幕右 = +x 列
			m_focus = m_focus + fwd * forward + rgt * right;
		}

		void camera::move_operand(value forward, value right)
		{
			if (!m_operand) return;
			// 模型自身坐标系（局部轴，忽略世界缩放）
			matrix R = xhqm::extract_rote(m_operand->system::coord());
			xhqm::vect<value> ax(R(0, 0), R(1, 0), R(2, 0));   // 局部 +x（右）
			xhqm::vect<value> az(R(0, 2), R(1, 2), R(2, 2));   // 局部 +z
			xhqm::vect<value> delta = az * forward - ax * right;   // W/S 沿局部前/后(+z/-z), A/D 沿局部左/右(-x/+x)
			m_operand->apply_world(xhqm::translate(delta));
		}

		void camera::dolly_ratio(value factor)
		{
			m_dist *= factor;
			if (m_dist < value(0.1)) m_dist = value(0.1);
			if (m_dist > value(5000)) m_dist = value(5000);
		}

		matrix camera::window(void) const
		{
			return m_proj * m_view_scale * system::coord().invmat();
		}

		value camera::world_per_pixel(void) const
		{
			const value bh = 540;
			if (m_ortho) return value(2) * (value)m_view_d / bh;
			const value tan_half = std::tan(m_fov * value(0.5));
			return value(2) * m_pan_dist * tan_half / bh;   // 恒定平移速度（不随缩放衰减）
		}

		void camera::life()
		{
			// 键盘移动（每帧应用，独立于 orbit 开关）：上下左右=相机，WASD=模型
			const value fwd = (m_in_fwd ? m_move_speed : value(0)) - (m_in_back ? m_move_speed : value(0));
			const value rgt = (m_in_right ? m_move_speed : value(0)) - (m_in_left ? m_move_speed : value(0));
			if (fwd != value(0) || rgt != value(0)) focus_move(fwd, rgt);
			const value mfwd = (m_in_mfwd ? m_move_speed : value(0)) - (m_in_mback ? m_move_speed : value(0));
			const value mrgt = (m_in_mright ? m_move_speed : value(0)) - (m_in_mleft ? m_move_speed : value(0));
			if ((mfwd != value(0) || mrgt != value(0)) && m_operand) move_operand(mfwd, mrgt);

			if (!m_orbit_on) return;
			// 相机世界 = translate(focus) * orient * translate(0,0,dist)
			// 位置 = focus + orient·(0,0,dist)，朝向 = orient
			matrix camcoord =
				xhqm::translate(m_focus.x(), m_focus.y(), m_focus.z())
				* m_orient
				* xhqm::translate(value(0), value(0), m_dist);
			this->system::coord(camcoord);
		}

		system* camera::active_operand() const
		{
			return m_operand;   // 未选中 = nullptr，作用于相机
		}

		void camera::apply_world(const matrix& M)
		{
			if (m_operand) m_operand->apply_world(M);
		}

		void camera::pan(value dx_px, value dy_px)
		{
			value wpp = world_per_pixel();
			matrix screen = xhqm::translate(dx_px * wpp, -dy_px * wpp, value(0));
			matrix CamWorld = this->system::coord();
			matrix W = CamWorld * screen * CamWorld.invmat();
			if (m_operand)
			{
				m_operand->apply_world(W);                          // 选中：平移对象
			}
			else
			{
				xhqm::vect<value> delta(-W(0, 3), -W(1, 3), -W(2, 3));
				focus_shift(delta);                                 // 未选中：平移焦点（grab）
			}
		}

		point camera::operand_center() const
		{
			if (m_operand) return m_operand->world_center();
			return m_focus;
		}

		void camera::zoom(value factor)
		{
			if (m_operand)
			{
				point C = operand_center();
				matrix M = xhqm::translate(C.x(), C.y(), C.z())
					* xhqm::scale(factor, factor, factor)
					* xhqm::translate(-C.x(), -C.y(), -C.z());
				m_operand->apply_world(M);                          // 选中：缩放对象
			}
			else
			{
				dolly_ratio(value(1) / factor);   // 未选中：调整相机距离
			}
		}
	}
}
