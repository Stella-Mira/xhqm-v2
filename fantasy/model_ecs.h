#ifndef XHQM_FANTASY_MODEL_ECS
#define XHQM_FANTASY_MODEL_ECS

#include "system.h"
#include "drawer.h"
#include "../metaclass/tuple_values.h"

namespace xhqm
{
	namespace fantasy
	{
		//ecs_tuple：组件存储类型擦除基类
		//一类 tuple 只管自己的数据、不在意自己类型；model_ecs 经此接口统一管理。
		class xhqm_io ecs_tuple
		{
		protected:
			explicit ecs_tuple() {};
		public:
			virtual ~ecs_tuple() {};
			ecs_tuple(ecs_tuple&&) = delete;
			ecs_tuple(const ecs_tuple&) = delete;

			virtual xhqm::size size() const = 0;
			virtual bool empty() const = 0;
			virtual bool erase(xhqm::index pos) = 0;   // 交换删除实体（O(1)，不保持顺序）
			virtual void clear() = 0;
		};

		//ecs_tuple_of：具体组件存储（持 xhqm::tuple_values<Columns...>）
		//Columns 为一类组件的各列类型：多列 SoA（如 value,value,value）或单列 AoS（如 point）。
		template<class... Columns>
		class ecs_tuple_of : public ecs_tuple
		{
			xhqm::tuple_values<Columns...> m_values;
		public:
			explicit ecs_tuple_of() {};
			virtual ~ecs_tuple_of() {};

			xhqm::tuple_values<Columns...>& values(void) { return m_values; }
			const xhqm::tuple_values<Columns...>& values(void) const { return m_values; }

			virtual xhqm::size size() const override { return m_values.size(); }
			virtual bool empty() const override { return m_values.empty(); }
			virtual bool erase(xhqm::index pos) override { return m_values.erase(pos); }
			virtual void clear() override { m_values.clear(); }
		};

		//model_ecs：树上的 ECS 节点（设计见 README「数据规则（ECS 化，设计文档）」）
		//一切皆节点：object 树保持不变，model_ecs 是树上负责草地/粒子/实例等
		//海量同质数据批量渲染的特殊节点；内部持 tuple SoA，普通节点照旧虚函数。
		//数据归属：model_ecs.attach(tuple)/detach(tuple)——model_ecs 知道自己的
		//数据该给谁管理（不是 tuple.attach(model_ecs)）。
		class xhqm_io model_ecs : public fantasy::models
		{
			std::vector<ecs_tuple*> m_tuples;	// 挂载的组件存储（model_ecs 持有所有权，语义仿 drawer 管理）
			xhqm::size m_sync_version = xhqm::npos;	// 上次同步的 position 组件数据版本（npos = 未同步）
		public:
			inline static std::mutex mutex;
		public:
			explicit model_ecs();
			virtual ~model_ecs();

			//组件存储管理（语义仿 models 的 drawer 管理：attach 取所有权，detach 归还所有权）
			void delete_tuples();
			std::vector<ecs_tuple*> handle_tuples();
			xhqm::index attach(ecs_tuple* tuple);
			xhqm::count detach(ecs_tuple* tuple);
			ecs_tuple* detach(xhqm::index pos);
			ecs_tuple* handle_tuple(xhqm::index pos);
			//按类型取回组件存储（不存在返回 nullptr）
			template<class... Columns>
			ecs_tuple_of<Columns...>* handle_tuple()
			{
				return handle_tuple_of<ecs_tuple_of<Columns...>>();
			}
			//按存储类型取回组件存储（如 ecs_tuple_of<fantasy::point>，不存在返回 nullptr）
			template<class Tuple>
			Tuple* handle_tuple_of()
			{
				for (ecs_tuple* tuple : m_tuples)
				{
					auto* find = dynamic_cast<Tuple*>(tuple);
					if (find) return find;
				}
				return nullptr;
			}
			//新建并挂载组件存储（已存在同类型存储则直接返回）
			template<class... Columns>
			ecs_tuple_of<Columns...>* create_tuple()
			{
				auto* find = handle_tuple<Columns...>();
				if (find) return find;
				auto* made = new ecs_tuple_of<Columns...>();
				attach(made);
				return made;
			}

			//多组件 join：按存储类型取回两/三类组件存储并联合遍历（任一缺失返回 false，不执行回调）
			//func(pos, cols_a..., cols_b...)：实体下标对齐，零构造读取（见 xhqm::join_each）
			//组件身份用类型：同类列签名的存储在一个 model_ecs 内唯一（position 用 point、velocity 用 vector 等语义类型区分）
			template<class TupleA, class TupleB, class Func>
			bool join_each(Func func)
			{
				auto* tuple_a = handle_tuple_of<TupleA>();
				auto* tuple_b = handle_tuple_of<TupleB>();
				if (!tuple_a || !tuple_b) return false;
				xhqm::join_each(tuple_a->values(), tuple_b->values(), func);
				return true;
			}
			template<class TupleA, class TupleB, class TupleC, class Func>
			bool join_each(Func func)
			{
				auto* tuple_a = handle_tuple_of<TupleA>();
				auto* tuple_b = handle_tuple_of<TupleB>();
				auto* tuple_c = handle_tuple_of<TupleC>();
				if (!tuple_a || !tuple_b || !tuple_c) return false;
				xhqm::join_each(tuple_a->values(), tuple_b->values(), tuple_c->values(), func);
				return true;
			}

			//实体数量（以第一个组件存储为准；无存储为 0）
			xhqm::size entity_size() const;

			//实例同步：从 position 组件生成平移矩阵写入各 prefab 的实例列表（走 M3 instancing 路径）
			//兼容两类存储：tuple_values<value,value,value>（SoA 三列）/ tuple_values<point>（AoS 单列）
			//版本守卫：position 组件数据版本未变时跳过（theory_instancing 与 render 同帧双调用去重）；
			//组件数据经 push/set_value/erase 等修改自动升版本；value_ref 引用直写后须 touch() 显式标脏
			bool sync_instances();
			void touch() { m_sync_version = xhqm::npos; }

			virtual void render() override;
		};
	}
}

#endif
