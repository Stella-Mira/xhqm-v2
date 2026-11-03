#ifndef XHQM_TUPLE_VALUES
#define XHQM_TUPLE_VALUES
#include "../head.h"
#include "../algebra/value.h"
#include <tuple>
#include <vector>
#include <utility>

namespace xhqm
{
	//tuple_values：组件存储（ECS 数据层，设计见 fantasy/README.md「数据规则（ECS 化）」）
	//主模板：std::tuple<std::vector<Columns>...> 多列 SoA 连续存储；
	//单列偏特化：退化为 AoS 直接存储（不包 tuple，可引用读写）。
	//四接口语义：
	//  value<type,ind>(pos)            读（值返回；ind=npos 聚合还原语义类型，否则取单列）有构造开销
	//  get_value(pos, Columns&...)     读（out 参数，零构造，批量热路径）
	//  set_value(pos, const Columns&...) 写（多列整体写，const 引用无拷贝）
	//  value_ref(pos)                  读/写（引用，仅单列直接存储成立）
	//返回类型规则：
	//  引用返回（value_ref）只在「直接存储」（单列 AoS）成立；聚合（SoA 多列）只能值返回（临时对象）。
	//  get_value/set_value 的参数类型锁死为 Columns（非模板方法），类型安全、无需 static_assert 兜底。
	template<class... Columns>
	class tuple_values
	{
		std::tuple<std::vector<Columns>...> values;
		xhqm::size m_version = 0;	// 数据版本：每次变更自增（model_ecs 同步去重等增量检测用）
	public:
		//读（值返回，聚合还原语义类型；ind=npos 聚合，否则取单列）
		template<class type, xhqm::index ind = xhqm::npos>
		type value(xhqm::index pos) const
		{
			if constexpr (ind == xhqm::npos)
				return make<type>(pos, std::index_sequence_for<Columns...>{});
			else
				return std::get<ind>(values).at(pos);
		}
		//读（out 参数，零构造）
		bool get_value(xhqm::index pos, Columns&... args) const
		{
			if (pos >= size()) return false;
			get_impl(pos, std::index_sequence_for<Columns...>{}, args...);
			return true;
		}
		//写（整体写每列，const 引用无拷贝）
		bool set_value(xhqm::index pos, const Columns&... args)
		{
			if (pos >= size()) return false;
			set_impl(pos, std::index_sequence_for<Columns...>{}, args...);
			++m_version;
			return true;
		}
		//追加实体（每列尾插同一组组件值）
		void push(const Columns&... args)
		{
			push_impl(std::index_sequence_for<Columns...>{}, args...);
			++m_version;
		}
		//交换删除实体（与末尾交换后弹出，O(1)，不保持顺序）
		bool erase(xhqm::index pos)
		{
			if (pos >= size()) return false;
			erase_impl(pos, std::index_sequence_for<Columns...>{});
			++m_version;
			return true;
		}
		void resize(xhqm::size n) { resize_impl(n, std::index_sequence_for<Columns...>{}); ++m_version; }
		void clear() { clear_impl(std::index_sequence_for<Columns...>{}); ++m_version; }
		bool empty() const { return size() == 0; }
		xhqm::size size() const { return std::get<0>(values).size(); }
		//数据版本（每次变更自增；注意 value_ref 引用直写不经此计数，引用写后须自行同步）
		xhqm::size version() const { return m_version; }
	private:
		template<class type, std::size_t... I>
		type make(xhqm::index pos, std::index_sequence<I...>) const
		{ return type{ std::get<I>(values).at(pos)... }; }
		template<std::size_t... I>
		void get_impl(xhqm::index pos, std::index_sequence<I...>, Columns&... args) const
		{ ((args = std::get<I>(values).at(pos)), ...); }
		template<std::size_t... I>
		void set_impl(xhqm::index pos, std::index_sequence<I...>, const Columns&... args)
		{ ((std::get<I>(values).at(pos) = args), ...); }
		template<std::size_t... I>
		void push_impl(std::index_sequence<I...>, const Columns&... args)
		{ (std::get<I>(values).push_back(args), ...); }
		template<std::size_t... I>
		void erase_impl(xhqm::index pos, std::index_sequence<I...>)
		{ (swap_pop(std::get<I>(values), pos), ...); }
		template<std::size_t... I>
		void resize_impl(xhqm::size n, std::index_sequence<I...>)
		{ (std::get<I>(values).resize(n), ...); }
		template<std::size_t... I>
		void clear_impl(std::index_sequence<I...>)
		{ (std::get<I>(values).clear(), ...); }
		template<class column>
		static void swap_pop(std::vector<column>& cols, xhqm::index pos)
		{
			cols.at(pos) = std::move(cols.back());
			cols.pop_back();
		}
	};

	//偏特化：单列（退化，不包 tuple，可引用读写）
	template<class Column>
	class tuple_values<Column>
	{
		std::vector<Column> values;
		xhqm::size m_version = 0;	// 数据版本：每次变更自增（引用直写不经此计数，引用写后须自行同步）
	public:
		Column&       value_ref(xhqm::index pos)       { return values.at(pos); }
		const Column& value_ref(xhqm::index pos) const { return values.at(pos); }
		Column        value(xhqm::index pos)     const { return values.at(pos); }
		//读（out 参数，零构造）
		bool get_value(xhqm::index pos, Column& arg) const
		{
			if (pos >= size()) return false;
			arg = values.at(pos);
			return true;
		}
		//写（const 引用无拷贝）
		bool set_value(xhqm::index pos, const Column& arg)
		{
			if (pos >= size()) return false;
			values.at(pos) = arg;
			++m_version;
			return true;
		}
		void push(const Column& arg) { values.push_back(arg); ++m_version; }
		//交换删除实体（与末尾交换后弹出，O(1)，不保持顺序）
		bool erase(xhqm::index pos)
		{
			if (pos >= size()) return false;
			values.at(pos) = std::move(values.back());
			values.pop_back();
			++m_version;
			return true;
		}
		void resize(xhqm::size n) { values.resize(n); ++m_version; }
		void clear() { values.clear(); ++m_version; }
		bool empty() const { return values.empty(); }
		xhqm::size size() const { return values.size(); }
		xhqm::size version() const { return m_version; }
	};

	//join_size：联合容量（各组件存储尺寸的最小值）
	//实体下标对齐：同一 pos 即同一实体；某实体缺某组件（该存储更短）则不进入联合遍历
	template<class First, class... Rest>
	inline xhqm::size join_size(const First& first, const Rest&... rest)
	{
		xhqm::size n = first.size();
		((n = (rest.size() < n ? rest.size() : n)), ...);
		return n;
	}

	//join_each：多组件联合遍历（热路径：out 参数零构造读取，回调外无逐实体临时对象）
	//func(pos, cols_a..., cols_b...)：各存储组件列按顺序展开；写回由 func 内经 set_value 完成
	template<class... ColsA, class... ColsB, class Func>
	void join_each(tuple_values<ColsA...>& tuple_a, tuple_values<ColsB...>& tuple_b, Func func)
	{
		std::tuple<ColsA...> args_a;
		std::tuple<ColsB...> args_b;
		xhqm::size n = join_size(tuple_a, tuple_b);
		for (xhqm::index pos = 0; pos < n; ++pos)
		{
			std::apply([&](ColsA&... cols_a) { tuple_a.get_value(pos, cols_a...); }, args_a);
			std::apply([&](ColsB&... cols_b) { tuple_b.get_value(pos, cols_b...); }, args_b);
			std::apply([&](ColsA&... cols_a) {
				std::apply([&](ColsB&... cols_b) { func(pos, cols_a..., cols_b...); }, args_b);
			}, args_a);
		}
	}

	//join_each：三组件联合遍历（同上）
	template<class... ColsA, class... ColsB, class... ColsC, class Func>
	void join_each(tuple_values<ColsA...>& tuple_a, tuple_values<ColsB...>& tuple_b, tuple_values<ColsC...>& tuple_c, Func func)
	{
		std::tuple<ColsA...> args_a;
		std::tuple<ColsB...> args_b;
		std::tuple<ColsC...> args_c;
		xhqm::size n = join_size(tuple_a, tuple_b, tuple_c);
		for (xhqm::index pos = 0; pos < n; ++pos)
		{
			std::apply([&](ColsA&... cols_a) { tuple_a.get_value(pos, cols_a...); }, args_a);
			std::apply([&](ColsB&... cols_b) { tuple_b.get_value(pos, cols_b...); }, args_b);
			std::apply([&](ColsC&... cols_c) { tuple_c.get_value(pos, cols_c...); }, args_c);
			std::apply([&](ColsA&... cols_a) {
				std::apply([&](ColsB&... cols_b) {
					std::apply([&](ColsC&... cols_c) { func(pos, cols_a..., cols_b..., cols_c...); }, args_c);
				}, args_b);
			}, args_a);
		}
	}
}
#endif
