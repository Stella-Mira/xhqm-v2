#ifndef XHQM_FANTASY_SYSTEM
#define XHQM_FANTASY_SYSTEM

#include "fantasy.h"
#include "blurry.h"

namespace xhqm
{
	namespace fantasy
	{
		class xhqm_io theory : public fantasy::object
		{
		public:
			inline static std::mutex mutex;
		public:
			explicit theory();
			virtual ~theory();

			virtual void life() override {};
			virtual void emit() override {};
		};

		class xhqm_io system
			: public fantasy::object
		{
			struct _packet;
			std::shared_ptr<system::_packet> m_packet;
		public:
			inline static std::mutex mutex;
		public:
			explicit system();
			virtual ~system();

			virtual void life() override {};
			virtual void emit() override {};

			void mul_coord(const fantasy::matrix&);
			void coord_mul(const fantasy::matrix&);
			void apply_world(const fantasy::matrix&);   // 世界变换作用到自身（相对父系共轭）

			const fantasy::matrix& coord(void) const;
			void coord(const fantasy::matrix& value);

		public:
			static fantasy::matrix transform(fantasy::object const*, fantasy::object const*);
			static void traverse(const fantasy::matrix&, fantasy::object*, fantasy::matrix);
		};

		class xhqm_io models
			: public fantasy::system
		{
			struct _packet;
			std::shared_ptr<models::_packet> m_packet;
		public:
			inline static std::mutex mutex;
		public:
			explicit models();
			virtual ~models();


			// 欧拉角ypr为: yaw（偏航）, pitch（俯仰）, roll（翻滚） //系统以x为前后方向，y为左右方向，z为上下方向
			xhqm::vect<value> micro_pos() const;
			xhqm::vect<value> micro_rpy() const;
			void micro(const xhqm::poit<value>& pos, const xhqm::vect<value>& ypr);
			void macro(const xhqm::poit<value>& pos, const xhqm::vect<value>& ypr);
			void coord(fantasy::system const* coord_ptr, const xhqm::poit<value>& pos, const xhqm::vect<value>& ypr);
			
			xhqm::vect<value> coord_dir_x(fantasy::system const* coord_ptr) const;
			xhqm::vect<value> coord_dir_y(fantasy::system const* coord_ptr) const;
			xhqm::vect<value> coord_dir_z(fantasy::system const* coord_ptr) const;
			xhqm::vect<value> coord_vec_o(fantasy::system const* coord_ptr) const;
			inline xhqm::vect<value> micro_dir_x() const { return coord_dir_x(this); };
			inline xhqm::vect<value> micro_dir_y() const { return coord_dir_y(this); };
			inline xhqm::vect<value> micro_dir_z() const { return coord_dir_z(this); };
			inline xhqm::vect<value> micro_pos_o() const { return coord_vec_o(this); };
			inline xhqm::vect<value> macro_dir_x() const { return coord_dir_x(dynamic_cast<fantasy::system*>(this->root_ptr())); };
			inline xhqm::vect<value> macro_dir_y() const { return coord_dir_y(dynamic_cast<fantasy::system*>(this->root_ptr())); };
			inline xhqm::vect<value> macro_dir_z() const { return coord_dir_z(dynamic_cast<fantasy::system*>(this->root_ptr())); };
			inline xhqm::vect<value> macro_pos_o() const { return coord_vec_o(dynamic_cast<fantasy::system*>(this->root_ptr())); };
	

			void show(bool value);
			bool show(void) const;
			void inter(bool value);
			bool inter(void) const;
			void drawer_select(const bool& value);
			void drawer_system(const fantasy::matrix& value);
			void drawer_world(const fantasy::matrix& value);
			fantasy::aabb updata_aabb();
			fantasy::point world_center() const;   // 自身子树的世界包围盒中心（含子节点）

			void delete_drawers();
			std::vector<fantasy::drawer*> handle_drawers();
			xhqm::index insert_drawer(fantasy::drawer* obj);
			xhqm::count remove_drawer(fantasy::drawer* obj);
			fantasy::drawer* remove_drawer(xhqm::index pos);
			fantasy::drawer* handle_drawer(xhqm::index pos);

			virtual void render();
			virtual void idcard();
		public:
			static void drawer(fantasy::object*);
			static void idcard(fantasy::object*);
		};


		class camera;   // 相机（见 fantasy/camera.h，前向声明供 scenes 使用）

		class xhqm_io scenes : public fantasy::system
		{
			struct _packet;
			std::shared_ptr<scenes::_packet> m_packet;
		public:
			inline static std::mutex mutex;
		public:
			explicit scenes();
			virtual ~scenes();

			fantasy::object* theory() const;
			fantasy::system* target() const;
			fantasy::camera* camera() const;
			// 多相机：附加相机子树（默认相机之外的独立相机；coord 为单位阵，不与默认相机串联）
			fantasy::system* cameras() const;

			bool add_theory(fantasy::object* object_ptr);
			bool add_target(fantasy::models* object_ptr);
			bool add_fixeds(fantasy::models* object_ptr);
			bool add_camera(fantasy::models* object_ptr);   // 相机/视图节点（camera，models 子类）

			bool sub_theory(fantasy::object* object_ptr);
			bool sub_target(fantasy::models* object_ptr);
			bool sub_fixeds(fantasy::models* object_ptr);
			bool sub_camera(fantasy::models* object_ptr);
		};

		using world = fantasy::scenes;//（世界）场景
		using dream = fantasy::scenes;//（梦境）场景
		using entity = fantasy::models;//（实体）模型
		using idolum = fantasy::models;//（幻象）模型
		using prisma = fantasy::camera;//（棱镜）相机
		using nekome = fantasy::camera;//（猫目）相机

		//lycoris（碧霊华）
		//papilio（雪翠蝶）
	}
}

#endif
