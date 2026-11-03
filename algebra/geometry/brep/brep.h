#pragma once
#ifndef XHQM_GEOMETRY_BREP
#define XHQM_GEOMETRY_BREP

// BRep 边界表示拓扑层：参考 OCC TopoDS，复用 lattice 的 ring_list、geometry 的曲线曲面、network 的 uuid。
#include "../geometry.h"
#include "../../../lattice/ring_list.hpp"
#include "../../../network/uuid.h"

namespace xhqm
{
	namespace geometry
	{
		namespace brep
		{
			using node_id = xhqm::cipher::uuid;		//节点稳定身份（16 字节 UUID）

			struct node_base
			{
				node_id id = xhqm::cipher::uuid::creat_uuid_v4();
			};

			//顶点：位置 + 共享边
			struct vertex : public node_base
			{
				geometry::poit position;
				std::vector<node_id> edges;		//共享此顶点的边
			};

			//边：端点 + 曲线 + 参数区间 + 相邻面
			struct edge : public node_base
			{
				node_id from;					//起点
				node_id to;						//终点
				geometry::curve* crv = nullptr;	//几何曲线（局部坐标）
				geometry::value t0 = 0;			//参数区间起点
				geometry::value t1 = 1;			//参数区间终点
				std::vector<node_id> faces;		//相邻面（coboundary）
			};

			//方向：挂在环的边数据上（方向属于引用，不属于对象）
			struct orient
			{
				bool reversed = false;
			};

			//线框（Wire）：边的环形链表，节点=边指针，环边数据=方向
			using wire = xhqm::ring_list<edge*, orient>;

			//面：曲面 + 外环 + 内环（洞）
			struct face : public node_base
			{
				geometry::surface* geom = nullptr;
				wire outer;					//外环
				std::vector<wire> inner;	//内环
				std::vector<node_id> shells;
			};

			//壳：共享边的面集合
			struct shell : public node_base
			{
				std::vector<node_id> faces;
			};

			//实体：闭合壳集合
			struct solid : public node_base
			{
				std::vector<node_id> shells;
			};
		}
	}
}

#endif
