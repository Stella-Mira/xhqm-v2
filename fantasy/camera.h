#pragma once
#ifndef XHQM_FANTASY_RENDER_CAMERA
#define XHQM_FANTASY_RENDER_CAMERA

// 棱镜相机（重新实现的相机类，旧 camera 保留）。
// 设计要点（相比旧 camera 的改进）：
// 1. 视图矩阵内聚：view() 返回「NDC 缩放 × 自身坐标的逆」，theory 无需再手动 transform(root, camera)。
// 2. 默认透视（与后处理 SSAO/SSR/depth 的透视假设一致；旧 camera 默认正交）。
// 3. 继承 models 仅为复用「drawer 集合 + 跟随相机」的机制——UI 按钮（按钮/sprite 等）挂到本相机，
//    其局部坐标被视图的逆抵消，屏幕位置恒定不变（跟随相机）。语义化接口 add_ui()。
// 4. orbit 单一状态（focus/yaw/pitch/dist 是唯一来源，life() 应用到 coord）。
#include "system.h"
#include "drawer.h"

namespace xhqm
{
	namespace fantasy
	{
		class xhqm_io camera : public models
		{
			// ---- 投影参数 ----
			bool m_ortho = false;                          // 默认透视
			value m_fov = xhqm::radians(value(45));
			value m_near = value(0.01f), m_far = value(10000.0f);
			uint m_view_w = 720, m_view_h = 720, m_view_d = 100;
			matrix m_proj = matrix(4, 4);
			matrix m_view_scale = matrix(4, 4);            // NDC 缩放（正交/透视各自的视口缩放）
			// ---- 拾取 ----
			uint m_pick_target = 0, m_pick_w = 0, m_pick_h = 0;
			// ---- orbit ----
			bool m_orbit_on = false;
			point m_focus = point(0, 0, 0);
			value m_yaw = 0, m_pitch = value(0.61), m_dist = 25;
			matrix m_orient = matrix(4, 4);   // 累积朝向旋转矩阵（trackball，无万向锁）
			models* m_operand = nullptr;      // 操作对象（nullptr = 未选中，作用于视口）
			value m_pan_dist = value(25);     // 平移参考距离（恒定，未选中平移焦点用）
			// 键盘移动输入状态（life 每帧应用）：上下左右=相机，WASD=模型
			bool m_in_fwd = false, m_in_back = false, m_in_left = false, m_in_right = false;
			bool m_in_mfwd = false, m_in_mback = false, m_in_mleft = false, m_in_mright = false;
			value m_move_speed = value(0.5f);
			sprite m_background;   // 背景（全屏 sprite，跟随相机）
		public:
			explicit camera();
			virtual ~camera();

			// ---- 投影 / 视图 ----
			void resize(uint w, uint h, uint d);
			void ortho(bool v);
			bool ortho(void) const { return m_ortho; }
			void fov(value v) { m_fov = v; resize(m_view_w, m_view_h, m_view_d); }
			value fov(void) const { return m_fov; }
			void near(value v) { m_near = v; resize(m_view_w, m_view_h, m_view_d); }
			void far(value v) { m_far = v; resize(m_view_w, m_view_h, m_view_d); }
			const matrix& proj(void) const { return m_proj; }
			// 完整视图矩阵（内聚：NDC 缩放 × 自身坐标的逆）
			matrix view(void) const { return m_view_scale * system::coord().invmat(); }
			// 完整 window（投影 × 视图）
			matrix window(void) const;
			std::array<uint, 3> view_size(void) const { return { m_view_w, m_view_h, m_view_d }; }
			// 清屏 + 渲染背景（渐变天空等）
			void clear(const rgba& c) { render_clearn(c); m_background.render(); }
			// 背景贴图（全屏渐变天空）
			void ground(const std::vector<std::vector<rgba>>& value) { m_background.texture(value); }

			// ---- UI 元素（按钮等，跟随相机，屏幕位置不变） ----
			void add_ui(fantasy::drawer* d) { insert_drawer(d); }
			void add_ui(models* m) { add_child_ptr(m); }   // 挂子树（跟随相机，屏幕位置恒定，保留自身 coord 变换）
			void clear_ui(void) { delete_drawers(); }

			// ---- 拾取（左下角原点像素坐标） ----
			object* slected(uint view_x, uint view_y);

			// ---- 键盘移动输入（RhiViewport 转发按键，life 每帧应用） ----
			void key_forward(bool down) { m_in_fwd = down; }
			void key_back(bool down) { m_in_back = down; }
			void key_left(bool down) { m_in_left = down; }
			void key_right(bool down) { m_in_right = down; }
			void key_model_forward(bool down) { m_in_mfwd = down; }
			void key_model_back(bool down) { m_in_mback = down; }
			void key_model_left(bool down) { m_in_mleft = down; }
			void key_model_right(bool down) { m_in_mright = down; }
			void move_speed(value v) { m_move_speed = v; }

			// ---- orbit 控制器（单一状态：focus/yaw/pitch/dist） ----
			void orbit(bool enable) { m_orbit_on = enable; }
			bool orbit(void) const { return m_orbit_on; }
			void focus(const point& p) { m_focus = p; }
			point focus(void) const { return m_focus; }
			void focus_shift(const vect<value>& delta) { m_focus = m_focus + delta; }
			void focus_move(value forward, value right);   // 沿视线前后 + 屏幕右左右移动焦点
			void move_operand(value forward, value right); // 沿视线前后 + 屏幕右左右平移选中对象
			void orbit(value yaw, value pitch) { m_yaw = yaw; m_pitch = pitch; m_orient = xhqm::rotate_z(yaw) * xhqm::rotate_x(xhqm::radians(value(90)) - pitch); }
			void orbit_add(value dyaw, value dpitch);
			void dolly(value dist) { m_dist = dist; }
			void dolly_ratio(value factor);
			value yaw(void) const { return m_yaw; }
			value pitch(void) const { return m_pitch; }
			value distance(void) const { return m_dist; }
			value world_per_pixel(void) const;

			// ---- 对象操控（内化：选中作用对象，未选中作用相机） ----
			void operand(models* m) { m_operand = m; }
			models* operand(void) const { return m_operand; }
			system* active_operand() const;           // 当前操作对象（nullptr = 未选中）
			void apply_world(const matrix& M);        // 世界变换作用到选中对象（未选中空操作）
			void pan(value dx_px, value dy_px);       // 屏幕像素增量：选中→平移对象，未选中→平移焦点(grab)
			void zoom(value factor);                  // 缩放：选中→缩放对象，未选中→调整相机距离(dolly)
			point operand_center() const;             // 操作对象包围盒中心（未选中=焦点）

			// 每帧把 orbit 姿态应用到 coord
			virtual void life() override;
		};
	}
}

#endif
