#ifndef XHQM_GEOMETRY
#define XHQM_GEOMETRY

#include "../value.h"
#include "linear.h"
#include <queue>
#undef hyper

namespace xhqm
{
	namespace geometry
	{
		using index = xhqm::index;
		using count = xhqm::count;
		using value = xhqm::real;
		using coord = std::vector<xhqm::index>;
		using topol = std::vector<xhqm::index>;
		using values = std::vector<geometry::value>;

		using real = geometry::value;
		using poit = xhqm::poit<geometry::value>;
		using vect = xhqm::vect<geometry::value>;
		using matx = xhqm::matx<geometry::value>;

		using points = std::vector<geometry::poit>;								//顶点集合
		using topols = std::vector<geometry::topol>;							//拓扑集合
		using bounds = std::vector<geometry::topols>;							//边界集合
		using object = std::pair<geometry::points, geometry::bounds>;			//网格实体
		using objects = std::vector<geometry::object>;							//网格实体集合

		namespace hyper //超几何
		{
			using point = std::vector<geometry::value>;								//超几何顶点
			using topol = std::vector<geometry::index>;								//超几何拓扑
			using points = std::vector<hyper::point>;								//超几何顶点集合
			using topols = std::vector<hyper::topol>;								//超几何拓扑集合
			using bounds = std::vector<hyper::topols>;								//超几何边界集合
			using object = std::pair<hyper::points, hyper::bounds>;					//超几何对象

			//—— 第 0 层：高维结构生成（3D 算法的 N 维对偶）——
			xhqm::index dimension(const point& p);									//顶点维度
			object hypercube(xhqm::count dim, value half = 1);						//超立方体：2^dim 顶点 + 2*dim 个 (dim-1) 维面
			object simplex(xhqm::count dim);										//标准 dim-单纯形：dim+1 顶点
			points hypersphere(xhqm::count dim, xhqm::count samples, value radius = 1);//N 维球面均匀采样
		}
	}

	namespace geometry
	{
		struct poit_hash
		{
			std::size_t operator()(const xhqm::poit<geometry::value>& p) const
			{
				std::size_t seed = 0;
				auto combine = [&seed](const geometry::value& v)
				{
					seed ^= std::hash<geometry::value>()(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
				};
				combine(p.x());
				combine(p.y());
				combine(p.z());
				return seed;
			}
		};

		struct xhqm_io shape 
		{
			virtual xhqm::string type() const = 0;
			virtual ~shape() = default;			// 虚析构：20+ 派生类，防止经 shape* 释放时跳过派生析构
		};


		class xhqm_io line : public geometry::shape
		{
		protected:
			geometry::poit m_pA;
			geometry::poit m_pB;
		public:
			line()
				: line(geometry::poit(), geometry::poit())
			{

			}
			line(const geometry::poit& A, const geometry::poit& B)
				: m_pA(A), m_pB(B)
			{

			}
			geometry::poit& point_A(void) { return m_pA; };
			geometry::poit& point_B(void) { return m_pB; };
			const geometry::poit& point_A(void) const { return m_pA; };
			const geometry::poit& point_B(void) const { return m_pB; };
			void point_A(const geometry::poit& point) { m_pA = point; };
			void point_B(const geometry::poit& point) { m_pB = point; };

			virtual xhqm::string type() const override { return "line"; };
		};
		class xhqm_io face : public geometry::shape
		{
		protected:
			geometry::poit m_pA;
			geometry::poit m_pB;
			geometry::poit m_pC;
		public:
			face()
				: face(geometry::poit(), geometry::poit(), geometry::poit())
			{

			}
			face(const geometry::poit& A, const geometry::poit& B, const geometry::poit& C)
				: m_pA(A), m_pB(B), m_pC(C)
			{

			}
			geometry::poit& point_A(void) { return m_pA; };
			geometry::poit& point_B(void) { return m_pB; };
			geometry::poit& point_C(void) { return m_pC; };
			const geometry::poit& point_A(void) const { return m_pA; };
			const geometry::poit& point_B(void) const { return m_pB; };
			const geometry::poit& point_C(void) const { return m_pC; };
			void point_A(const geometry::poit& point) { m_pA = point; };
			void point_B(const geometry::poit& point) { m_pB = point; };
			void point_C(const geometry::poit& point) { m_pC = point; };

			geometry::vect normal(void) const;							//单位法线（右手定则 A→B→C）
			geometry::value area(void) const;							//三角形面积
			geometry::poit centroid(void) const;						//重心

			virtual xhqm::string type() const override { return "face"; };
		};
		class xhqm_io cube : public geometry::shape
		{
		protected:
			geometry::poit m_pA;
			geometry::poit m_pB;
			geometry::poit m_pC;
			geometry::poit m_pD;
		public:
			cube()
				: cube(geometry::poit(), geometry::poit(), geometry::poit(), geometry::poit())
			{

			}
			cube(const geometry::poit& A, const geometry::poit& B, const geometry::poit& C, const geometry::poit& D)
				: m_pA(A), m_pB(B), m_pC(C), m_pD(D)
			{

			}
			geometry::poit& point_A(void) { return m_pA; };
			geometry::poit& point_B(void) { return m_pB; };
			geometry::poit& point_C(void) { return m_pC; };
			geometry::poit& point_D(void) { return m_pD; };
			const geometry::poit& point_A(void) const { return m_pA; };
			const geometry::poit& point_B(void) const { return m_pB; };
			const geometry::poit& point_C(void) const { return m_pC; };
			const geometry::poit& point_D(void) const { return m_pD; };
			void point_A(const geometry::poit& point) { m_pA = point; };
			void point_B(const geometry::poit& point) { m_pB = point; };
			void point_C(const geometry::poit& point) { m_pC = point; };
			void point_D(const geometry::poit& point) { m_pD = point; };

			virtual xhqm::string type() const override { return "cube"; };
		};

		class xhqm_io grid : public geometry::shape//面网格
		{
		protected:
			geometry::points m_poits;							//顶点集合
			geometry::topols m_faces;							//网格集合
		public:
			geometry::points& poits(void) { return m_poits; };
			geometry::topols& faces(void) { return m_faces; };
			const geometry::points& poits(void) const { return m_poits; };
			const geometry::topols& faces(void) const { return m_faces; };
			void poits(const geometry::points& value) { m_poits = value; };
			void faces(const geometry::topols& value) { m_faces = value; };

			friend grid operator + (const grid& a, const grid& b);
			virtual xhqm::string type() const override { return "grid"; };
		};
		class xhqm_io mesh : public geometry::shape//体网格
		{
		protected:
			geometry::points m_poits;							//顶点集合
			geometry::topols m_faces;							//网格集合
			geometry::topols m_cubes;							//胞体集合
		public:
			geometry::points& poits(void) { return m_poits; };
			geometry::topols& faces(void) { return m_faces; };
			geometry::topols& cubes(void) { return m_cubes; };
			const geometry::points& poits(void) const { return m_poits; };
			const geometry::topols& faces(void) const { return m_faces; };
			const geometry::topols& cubes(void) const { return m_cubes; };
			void poits(const geometry::points& value) { m_poits = value; };
			void faces(const geometry::topols& value) { m_faces = value; };
			void cubes(const geometry::topols& value) { m_cubes = value; };

			friend mesh operator + (const mesh& a, const mesh& b);
			virtual xhqm::string type() const override { return "mesh"; };
		};
		class xhqm_io circle : public geometry::shape
		{
		protected:
			geometry::value m_radius = 1;
			geometry::poit m_poit = geometry::poit(0, 0, 0);
			geometry::vect m_pose = geometry::vect(0, 0, 1);
		public:
			circle(const geometry::value& radius = 1)
				: m_radius(radius)
			{

			}
			const geometry::poit& poit(void) const { return m_poit; };
			const geometry::vect& pose(void) const { return m_pose; };
			const geometry::value& radius(void) const { return m_radius; };
			void poit(const geometry::poit& value) { m_poit = value; };
			void pose(const geometry::vect& value) { m_pose = value; };
			void radius(const geometry::value& value) { m_radius = value; };

			geometry::grid circle_grid(const geometry::count& count) const;
			geometry::poit circle_point(const geometry::value& angle) const;
			std::vector<geometry::poit> circle_points(const geometry::count& count) const;

			virtual xhqm::string type() const override { return "circle"; };
		};
		class xhqm_io sphere : public geometry::shape
		{
		protected:
			geometry::value m_radius = 1;
			geometry::poit m_poit = geometry::poit(0, 0, 0);
		public:
			sphere(const geometry::value& radius = 1)
				: m_radius(radius)
			{

			}
			const geometry::poit& poit(void) const { return m_poit; };
			void poit(const geometry::poit& value) { m_poit = value; };
			const geometry::value& radius(void) const { return m_radius; };
			void radius(const geometry::value& value) { m_radius = value; };

			geometry::grid sphere_grid(geometry::value& resolution) const;
			geometry::poit sphere_point(const geometry::vect& pose) const;

			virtual xhqm::string type() const override { return "sphere"; };
		};
		class xhqm_io frustum : public geometry::shape
		{
		protected:
			geometry::count m_count = 3;
			geometry::value m_angle = 0;
			geometry::value m_radiusT = 1;
			geometry::value m_radiusD = 1;
			geometry::poit m_centerT = geometry::poit();//顶部中点
			geometry::poit m_centerD = geometry::poit();//底部中点
			geometry::vect m_vectorA = geometry::vect();//平面方向
		public:
			frustum(const geometry::poit& centerD, const geometry::poit& centerT, const geometry::vect& vectorA)
				: m_centerD(centerD), m_centerT(centerT), m_vectorA(vectorA)
			{

			}
			const geometry::count& count(void) const { return m_count; };
			const geometry::value& angle(void) const { return m_angle; };
			const geometry::value& radius_T(void) const { return m_radiusT; };
			const geometry::value& radius_D(void) const { return m_radiusD; };
			const geometry::poit& center_T(void) const { return m_centerT; };
			const geometry::poit& center_D(void) const { return m_centerD; };
			const geometry::vect& vector_A(void) const { return m_vectorA; };
			void count(const geometry::count& value) { m_count = value; };
			void angle(const geometry::value& value) { m_angle = value; };
			void radius_T(const geometry::value& value) { m_radiusT = value; };
			void radius_D(const geometry::value& value) { m_radiusD = value; };
			void center_T(const geometry::poit& value) { m_centerT = value; };
			void center_D(const geometry::poit& value) { m_centerD = value; };
			void vector_A(const geometry::vect& value) { m_vectorA = value; };

			geometry::grid frustum_grid() const;
			virtual xhqm::string type() const override { return "frustum"; };
		};
		class xhqm_io plane : public geometry::shape
		{
		protected:
			geometry::poit m_poit = geometry::poit(0, 0, 0);					//平面上一点
			geometry::vect m_norm = geometry::vect(0, 0, 1);					//法线
		public:
			plane() {}
			plane(const geometry::poit& poit, const geometry::vect& norm)
				: m_poit(poit), m_norm(norm)
			{

			}
			const geometry::poit& poit(void) const { return m_poit; };
			void poit(const geometry::poit& value) { m_poit = value; };
			const geometry::vect& norm(void) const { return m_norm; };
			void norm(const geometry::vect& value) { m_norm = value; };

			geometry::grid plane_grid(const geometry::value& half = 1, const geometry::count& div = 1) const;
			virtual xhqm::string type() const override { return "plane"; };
		};
		class xhqm_io box : public geometry::shape
		{
		protected:
			geometry::poit m_pMin = geometry::poit(0, 0, 0);					//最小角点
			geometry::poit m_pMax = geometry::poit(1, 1, 1);					//最大角点
		public:
			box() {}
			box(const geometry::poit& pMin, const geometry::poit& pMax)
				: m_pMin(pMin), m_pMax(pMax)
			{

			}
			const geometry::poit& pMin(void) const { return m_pMin; };
			void pMin(const geometry::poit& value) { m_pMin = value; };
			const geometry::poit& pMax(void) const { return m_pMax; };
			void pMax(const geometry::poit& value) { m_pMax = value; };

			geometry::grid box_grid(void) const;							//6 面 12 三角形
			bool contain(const geometry::poit& p) const;					//点是否在盒内
			virtual xhqm::string type() const override { return "box"; };
		};
		class xhqm_io cylinder : public geometry::shape
		{
		protected:
			geometry::value m_radius = 1;
			geometry::value m_height = 2;
			geometry::poit m_poit = geometry::poit(0, 0, 0);				//底面中心
			geometry::count m_count = 16;									//圆周分段
		public:
			cylinder(const geometry::value& radius = 1, const geometry::value& height = 2)
				: m_radius(radius), m_height(height)
			{

			}
			const geometry::value& radius(void) const { return m_radius; };
			void radius(const geometry::value& value) { m_radius = value; };
			const geometry::value& height(void) const { return m_height; };
			void height(const geometry::value& value) { m_height = value; };
			const geometry::poit& poit(void) const { return m_poit; };
			void poit(const geometry::poit& value) { m_poit = value; };
			const geometry::count& count(void) const { return m_count; };
			void count(const geometry::count& value) { m_count = value; };

			geometry::grid cylinder_grid(void) const;						//侧面 + 上下盖
			virtual xhqm::string type() const override { return "cylinder"; };
		};
		class xhqm_io cone : public geometry::shape
		{
		protected:
			geometry::value m_radius = 1;
			geometry::value m_height = 2;
			geometry::poit m_poit = geometry::poit(0, 0, 0);				//底面中心
			geometry::count m_count = 16;									//圆周分段
		public:
			cone(const geometry::value& radius = 1, const geometry::value& height = 2)
				: m_radius(radius), m_height(height)
			{

			}
			const geometry::value& radius(void) const { return m_radius; };
			void radius(const geometry::value& value) { m_radius = value; };
			const geometry::value& height(void) const { return m_height; };
			void height(const geometry::value& value) { m_height = value; };
			const geometry::poit& poit(void) const { return m_poit; };
			void poit(const geometry::poit& value) { m_poit = value; };
			const geometry::count& count(void) const { return m_count; };
			void count(const geometry::count& value) { m_count = value; };

			geometry::grid cone_grid(void) const;							//底面 + 侧面（顶点在上）
			virtual xhqm::string type() const override { return "cone"; };
		};
		class xhqm_io torus : public geometry::shape
		{
		protected:
			geometry::value m_radiusM = 2;									//大半径（环心到管心）
			geometry::value m_radiusm = 1;									//小半径（管半径）
			geometry::poit m_poit = geometry::poit(0, 0, 0);				//环心
			geometry::count m_countU = 16;									//大环分段
			geometry::count m_countV = 12;									//管截面分段
		public:
			torus(const geometry::value& radiusM = 2, const geometry::value& radiusm = 1)
				: m_radiusM(radiusM), m_radiusm(radiusm)
			{

			}
			const geometry::value& radius_M(void) const { return m_radiusM; };
			void radius_M(const geometry::value& value) { m_radiusM = value; };
			const geometry::value& radius_m(void) const { return m_radiusm; };
			void radius_m(const geometry::value& value) { m_radiusm = value; };
			const geometry::poit& poit(void) const { return m_poit; };
			void poit(const geometry::poit& value) { m_poit = value; };
			const geometry::count& count_U(void) const { return m_countU; };
			void count_U(const geometry::count& value) { m_countU = value; };
			const geometry::count& count_V(void) const { return m_countV; };
			void count_V(const geometry::count& value) { m_countV = value; };

			geometry::grid torus_grid(void) const;
			virtual xhqm::string type() const override { return "torus"; };
		};
		class xhqm_io capsule : public geometry::shape
		{
		protected:
			geometry::value m_radius = 1;
			geometry::value m_height = 2;									//圆柱段长度
			geometry::poit m_poit = geometry::poit(0, 0, 0);				//中心
			geometry::count m_count = 16;									//圆周分段
		public:
			capsule(const geometry::value& radius = 1, const geometry::value& height = 2)
				: m_radius(radius), m_height(height)
			{

			}
			const geometry::value& radius(void) const { return m_radius; };
			void radius(const geometry::value& value) { m_radius = value; };
			const geometry::value& height(void) const { return m_height; };
			void height(const geometry::value& value) { m_height = value; };
			const geometry::poit& poit(void) const { return m_poit; };
			void poit(const geometry::poit& value) { m_poit = value; };
			const geometry::count& count(void) const { return m_count; };
			void count(const geometry::count& value) { m_count = value; };

			geometry::grid capsule_grid(void) const;						//圆柱段 + 两半球（圆锥近似）
			virtual xhqm::string type() const override { return "capsule"; };
		};
		class xhqm_io quad : public geometry::shape
		{
		protected:
			geometry::poit m_pA;
			geometry::poit m_pB;
			geometry::poit m_pC;
			geometry::poit m_pD;
		public:
			quad()
				: quad(geometry::poit(), geometry::poit(), geometry::poit(), geometry::poit())
			{

			}
			quad(const geometry::poit& A, const geometry::poit& B, const geometry::poit& C, const geometry::poit& D)
				: m_pA(A), m_pB(B), m_pC(C), m_pD(D)
			{

			}
			geometry::poit& point_A(void) { return m_pA; };
			geometry::poit& point_B(void) { return m_pB; };
			geometry::poit& point_C(void) { return m_pC; };
			geometry::poit& point_D(void) { return m_pD; };
			const geometry::poit& point_A(void) const { return m_pA; };
			const geometry::poit& point_B(void) const { return m_pB; };
			const geometry::poit& point_C(void) const { return m_pC; };
			const geometry::poit& point_D(void) const { return m_pD; };
			void point_A(const geometry::poit& point) { m_pA = point; };
			void point_B(const geometry::poit& point) { m_pB = point; };
			void point_C(const geometry::poit& point) { m_pC = point; };
			void point_D(const geometry::poit& point) { m_pD = point; };

			geometry::grid quad_grid(void) const;							//2 三角形
			virtual xhqm::string type() const override { return "quad"; };
		};


		struct xhqm_io nurbs
		{
			//阶  数，K 
			//关  系  M = N + K + 1
			//节向量，V 共 M + 1 个 
			//控制点，P 共 N + 1 个
			//权重值，W 共 N + 1 个
			static geometry::index find_span(const values& knots, count p, value u);
			static geometry::values base_funs(const values& knots, count p, value u, count span);
			/* 基函数 0..order 阶导数（The NURBS Book A2.3）：ders[k][j] = N_{span-p+j}^{(k)} */
			static std::vector<geometry::values> base_funs_deriv(const values& knots, count p, value u, count span, count order);
			static geometry::values linspace(const count& size, const value& star, const value& stop);
		};
		class xhqm_io curve : public geometry::shape
		{
		protected:
			xhqm::count m_nurbs_k = xhqm::npos;
			std::vector<value> m_nurbs_d;
			std::vector<real> m_nurbs_w;
			std::vector<poit> m_nurbs_p;
		public:
			count& nurbs_k(void) { return m_nurbs_k; };
			const count& nurbs_k(void) const { return m_nurbs_k; };
			void nurbs_k(const count& value) { m_nurbs_k = value; };

			std::vector<value>& nurbs_v(void) { return m_nurbs_d; };
			const std::vector<value>& nurbs_v(void) const { return m_nurbs_d; };
			void nurbs_v(const std::vector<value>& values) { m_nurbs_d = values; };

			std::vector<real>& nurbs_w(void) { return m_nurbs_w; };
			const std::vector<real>& nurbs_w(void) const { return m_nurbs_w; };
			void nurbs_w(const std::vector<real>& values) { m_nurbs_w = values; };

			std::vector<poit>& nurbs_p(void) { return m_nurbs_p; };
			const std::vector<poit>& nurbs_p(void) const { return m_nurbs_p; };
			void nurbs_p(const std::vector<poit>& values) { m_nurbs_p = values; };

			//v: 0-1
			geometry::poit creat_point(const geometry::real& v) const;
			std::vector<geometry::poit> creat_points(const std::vector<value>& v) const;
			/* 有理曲线 order 阶导数（1/2 阶），返回 vect */
			geometry::vect creat_derivative(const geometry::real& v, count order = 1) const;

			virtual xhqm::string type() const override { return "curve"; };
		};
		class xhqm_io surface : public geometry::shape
		{
		protected:
			std::array<count, 2> m_nurbs_k = { xhqm::npos, xhqm::npos };
			std::array<std::vector<value>, 2> m_nurbs_d;
			std::vector<std::vector<real>> m_nurbs_w;
			std::vector<std::vector<poit>> m_nurbs_p;
		public:
			count& nurbs_s(void) { return m_nurbs_k[0]; };
			const count& nurbs_s(void) const { return m_nurbs_k[0]; };
			void nurbs_s(const count& value) { m_nurbs_k[0] = value; };

			count& nurbs_t(void) { return m_nurbs_k[1]; };
			const count& nurbs_t(void) const { return m_nurbs_k[1]; };
			void nurbs_t(const count& value) { m_nurbs_k[1] = value; };

			std::vector<value>& nurbs_u(void) { return m_nurbs_d[0]; };
			const std::vector<value>& nurbs_u(void) const { return m_nurbs_d[0]; };
			void nurbs_u(const std::vector<value>& values) { m_nurbs_d[0] = values; };

			std::vector<value>& nurbs_v(void) { return m_nurbs_d[1]; };
			const std::vector<value>& nurbs_v(void) const { return m_nurbs_d[1]; };
			void nurbs_v(const std::vector<value>& values) { m_nurbs_d[1] = values; };

			std::vector<std::vector<real>>& nurbs_w(void) { return m_nurbs_w; };
			const std::vector<std::vector<real>>& nurbs_w(void) const { return m_nurbs_w; };
			void nurbs_w(const std::vector<std::vector<real>>& values) { m_nurbs_w = values; };

			std::vector<std::vector<poit>>& nurbs_p(void) { return m_nurbs_p; };
			const std::vector<std::vector<poit>>& nurbs_p(void) const { return m_nurbs_p; };
			void nurbs_p(const std::vector<std::vector<poit>>& values) { m_nurbs_p = values; };

			geometry::curve index_curve(const index& ind, bool is_h = true) const;
			//v: 0-1
			geometry::poit creat_point(const geometry::real& v1, index i2) const;
			geometry::poit creat_point(index i1, const geometry::real& v2) const;
			geometry::poit creat_point(const geometry::real& v1, const geometry::real& v2) const;
			std::vector<std::vector<geometry::poit>> creat_points(const std::vector<value>& v1, const std::vector<value>& v2) const;
			/* 一阶偏导（is_u=true 沿 u，否则沿 v） */
			geometry::vect creat_derivative(const geometry::real& v1, const geometry::real& v2, bool is_u = true) const;
			/* 曲面单位法线 = 偏导叉乘归一化 */
			geometry::vect creat_normal(const geometry::real& v1, const geometry::real& v2) const;
			/* 等参网格化：u_div × v_div 四边形 → 三角形 */
			geometry::grid surface_grid(count u_div, count v_div) const;

			virtual xhqm::string type() const override { return "surface"; };
		};



		/* 球体的三角型数据 Radius:半径 Resolution:分辨率 */
		std::vector<xhqm::geometry::face> xhqm_io create_sphere(geometry::value Radius, geometry::value resolution);
		/* 球体的NURBS曲面数据 Radius:半径 */
		xhqm::geometry::surface xhqm_io create_sphere(geometry::value Radius);
		xhqm::geometry::surface xhqm_io create_sphere_1(geometry::value radius);
		xhqm::geometry::surface xhqm_io create_sphere_2(geometry::value radius);
		xhqm::geometry::surface xhqm_io create_sphere_4(geometry::value radius);
		xhqm::geometry::surface xhqm_io create_sphere_8(geometry::value radius);

		/* —— 求交与度量 —— */
		/* ray = ro + t*rd 与三角形求交（Möller–Trumbore），命中返回最近参数 t */
		bool xhqm_io ray_triangle(const geometry::poit& ro, const geometry::vect& rd,
			const geometry::poit& pA, const geometry::poit& pB, const geometry::poit& pC, geometry::value& t);
		/* ray 与球求交，返回最近命中参数 t */
		bool xhqm_io ray_sphere(const geometry::poit& ro, const geometry::vect& rd,
			const geometry::poit& center, geometry::value radius, geometry::value& t);
		/* 点到平面有符号距离 */
		geometry::value xhqm_io point_plane_distance(const geometry::poit& p,
			const geometry::poit& plane_poit, const geometry::vect& plane_norm);
		/* 四面体有向体积 = (B-A)·((C-A)×(D-A))/6，符号随顶点定向 */
		geometry::value xhqm_io signed_volume(const geometry::poit& pA, const geometry::poit& pB,
			const geometry::poit& pC, const geometry::poit& pD);
	}

}
#endif
