#pragma once
#ifndef XHQM_FANTASY_ELEMENT
#define XHQM_FANTASY_ELEMENT

#include "fantasy.h"
#include "drawer.h"

namespace xhqm
{
	namespace fantasy
	{
		class xhqm_io poits : public drawer
		{
			inline static fantasy::shader _shader;  //默认shader
		protected:
			fantasy::texture m_texture;
			std::vector<std::tuple<geometry::poit, fantasy::value>> m_poits;
		public:
			virtual void shader() override;
			virtual void updata() override;
			virtual void render() override;
		protected:
			virtual void _render_id_() override;
		public:
			inline fantasy::id get_texture() const { return m_texture; };
			void set_texture(const std::vector<fantasy::rgba>& scale) { m_texture.reset(scale); };
			inline const std::vector<std::tuple<geometry::poit, fantasy::value>>& poits_data() const { return m_poits; };		//顶点数据
			inline void poits_data(const std::vector<std::tuple<geometry::poit, fantasy::value>>& value) { m_poits = value; };	//顶点数据
			inline void poits_data(const std::vector<geometry::poit>& value) 
			{ 
				m_poits.resize(value.size());
				for (xhqm::index index = 0; index < value.size(); ++index)
				{
					std::get<0>(m_poits[index]) = value[index];
					std::get<1>(m_poits[index]) = fantasy::value(0);
				}
			};	
		};

		class xhqm_io lines : public drawer
		{
			inline static fantasy::shader _shader;  //默认shader
		protected:
			fantasy::texture m_texture;
			std::vector<std::tuple<geometry::line, fantasy::value>> m_lines;
		public:
			virtual void shader() override;
			virtual void updata() override;
			virtual void render() override;
		protected:
			virtual void _render_id_() override;
		public:
			inline fantasy::id get_texture() const { return m_texture; };
			void set_texture(const std::vector<fantasy::rgba>& scale) { m_texture.reset(scale); };
			inline const std::vector<std::tuple<geometry::line, fantasy::value>>& lines_data() const { return m_lines; };		//顶点数据
			inline void lines_data(const std::vector<std::tuple<geometry::line, fantasy::value>>& value) { m_lines = value; };	//顶点数据
			inline void lines_data(const std::vector<geometry::line>& value) 
			{ 
				m_lines.resize(value.size());
				for (xhqm::index index = 0; index < value.size(); ++index)
				{
					std::get<0>(m_lines[index]) = value[index];
					std::get<1>(m_lines[index]) = fantasy::value(0);
				}
			};	//顶点数据
		};

		class xhqm_io ploys : public drawer
		{
			inline static fantasy::shader _shader;  //默认shader
		protected:
			std::vector<topol> m_topols;		//拓扑数据
			std::vector<point> m_points;		//顶点数据
			std::vector<value> m_values;		//顶点属性
			fantasy::texture m_texture;
		public:
			virtual void shader() override;
			virtual void updata() override;
			virtual void render() override;
		protected:
			virtual void _render_id_() override;
			// 把连续同顶点数的 topol 合并成一次 draw（render 与 render_id 共用）
			void _draw_runs_();
		public:
			inline const std::vector<topol>& topols() const { return m_topols; };		//拓扑数据
			inline void topols(const std::vector<topol>& value) { m_topols = value; };	//拓扑数据
			inline const std::vector<point>& points() const { return m_points; };		//顶点数据
			inline void points(const std::vector<point>& value) { m_points = value; };	//顶点数据
			inline const std::vector<value>& values() const { return m_values; };		//顶点属性
			inline void values(const std::vector<value>& value) { m_values = value; };	//顶点属性

			inline void show_grid(const geometry::grid& value)
			{
				m_points = value.poits();
				m_topols = value.faces();
				m_values.resize(m_points.size(), 0);
			}
			inline void show_mesh(const geometry::mesh& value)
			{
				m_points = value.poits();
				m_topols = value.faces();
				m_values.resize(m_points.size(), 0);
			}

			inline fantasy::id get_texture() const { return m_texture; };
			void set_texture(const std::vector<fantasy::rgba>& scale) { m_texture.reset(scale); };
		};

		class xhqm_io curve : public drawer
		{
			inline static fantasy::shader _shader;  //默认shader
		protected:
			fantasy::value m_ratio = 1.0;
			fantasy::count m_shape = 0;
			fantasy::count m_count = 0;
			fantasy::texture m_texture;
			geometry::curve m_nurbs;
		public:
			virtual void shader() override;
			virtual void updata() override;
			virtual void render() override;
		protected:
			virtual void _render_id_() override;
		public:
			const value& ratio(void) const { return m_ratio; };
			void ratio(const value& value) { m_ratio = value; };

			const geometry::curve& curve_data(void) const { return m_nurbs; };
			void curve_data(const geometry::curve& value) { m_nurbs = value; };

			inline fantasy::id get_texture() const { return m_texture; };
			void set_texture(const std::vector<fantasy::rgba>& scale) { m_texture.reset(scale); };
		};

		class xhqm_io nurbs : public drawer
		{
			inline static fantasy::shader _shader;  //默认shader
		protected:
			fantasy::value m_ratio = 1.0;
			fantasy::count m_shape = 0;
			fantasy::count m_count = 0;
			fantasy::texture m_texture;
			geometry::surface m_nurbs;
		public:
			virtual void shader() override;
			virtual void updata() override;
			virtual void render() override;
		protected:
			virtual void _render_id_() override;
		public:
			const value& ratio(void) const { return m_ratio; };
			void ratio(const value& value) { m_ratio = value; };

			const geometry::surface& surface(void) const { return m_nurbs; };
			void surface(const geometry::surface& value) { m_nurbs = value; };

			inline fantasy::id get_texture() const { return m_texture; };
			void set_texture(const std::vector<std::vector<fantasy::rgba>>& scale) { m_texture.reset(scale); };
		};
	}

	namespace fantasy
	{
		class xhqm_io arrow
			: public fantasy::ploys
		{
			xhqm::vect<fantasy::value> m_arrow;
			fantasy::value m_first = fantasy::value(0.75);
			fantasy::value m_r1 = fantasy::value(0.05);
			fantasy::value m_r2 = fantasy::value(0.10);
		public:
			explicit arrow()
			{
				m_arrow = xhqm::vect<fantasy::value>(0, 0, 1);
			}

			virtual void updata() override;

			xhqm::vect<fantasy::value> pose(void) const { return m_arrow; };
			fantasy::value proportion(void) const { return m_first; };
			fantasy::value hand_radius(void) const { return m_r1; };
			fantasy::value head_radius(void) const { return m_r2; };

			void pose(const xhqm::vect<fantasy::value>& value) { m_arrow = value; };
			void proportion(const fantasy::value& value) { m_first = value; };
			void hand_radius(const fantasy::value& value) { m_r1 = value; };
			void head_radius(const fantasy::value& value) { m_r2 = value; };

			static geometry::grid make(fantasy::value first, fantasy::value r1, fantasy::value r2);
		};
	}
}

#endif