#ifndef XHQM_SOUL_AD_NODE
#define XHQM_SOUL_AD_NODE

// reverse-mode AD 值节点 + tape。
// 值节点 = 图结构（directed_node，来/去连接）+ 元数据（uuid / 算子名 / 载荷 / 反向闭包）。
// 边方向 = 数据流（输入 → 输出）：z = op(x,y) 时 x→z、y→z，
// 故 z.from_list() = 输入，adjacent_list()（出边）配合拓扑排序得正向计算顺序，逆序即反向梯度顺序。
#include "../../lattice/directed_mesh.hpp"
#include "../../lattice/graph_algorithms.hpp"
#include "../../network/uuid.h"
#include "serialize.h"
#include <memory>
#include <string>
#include <functional>
#include <vector>

namespace xhqm {
namespace soul {
namespace ad {

	// 值节点元数据
	struct value_meta
	{
		xhqm::cipher::uuid id;
		std::string op;           // 算子名（序列化时据此重建反向）
		std::string name;         // 参数名（叶子节点可命名，空串=中间节点）
		nlohmann::json params;    // 算子超参（slope/eps/target/ids 等，重建反向所需）
		std::shared_ptr<tensor_holder> payload;
		std::function<void()> backward_fn;
	};

	// AD 值节点（type 为图内统一标量类型，默认 f32）
	template<class type = f32>
	struct value : public xhqm::_graph_::directed_node<value_meta>
	{
		using base = xhqm::_graph_::directed_node<value_meta>;
		using value_ptr = value*;

		value() = default;
		explicit value(value_meta&& m) : base(std::move(m)) {}
		explicit value(const value_meta& m) : base(m) {}

		// —— 载荷 / 元数据 ——
		tensor_holder* holder() const { return this->data.payload.get(); }
		const std::string& op() const { return this->data.op; }
		const std::string& name() const { return this->data.name; }
		const xhqm::cipher::uuid& id() const { return this->data.id; }
		std::function<void()>& backward_fn() { return this->data.backward_fn; }
		const nlohmann::json& params() const { return this->data.params; }
		nlohmann::json& params() { return this->data.params; }

		xhqm::size dim() const { return holder()->dim(); }
		std::vector<xhqm::size> shape() const { return holder()->shape(); }
		void* raw_data() { return holder()->data(); }
		void* grad() { return holder()->gradient(); }
		void zero_grad() { holder()->zero_grad(); }

		// —— 图连接（value* 视图） ——
		// 建边：this 是 x 的输出（x → this），即 x 是本节点输入
		void add_input(value_ptr x) { x->add_goto(this); }
		std::vector<value_ptr> inputs() const
		{
			std::vector<value_ptr> r;
			for (auto* n : this->from_list()) r.push_back(static_cast<value_ptr>(n));
			return r;
		}
		std::vector<value_ptr> consumers() const
		{
			std::vector<value_ptr> r;
			for (auto* n : this->goto_list()) r.push_back(static_cast<value_ptr>(n));
			return r;
		}
		// 供拓扑排序（返回出边，value* 视图）
		std::vector<value_ptr> adjacent_list() const { return consumers(); }

		// 标量种子（loss 反向入口）
		void seed_grad_one()
		{
			type* g = static_cast<type*>(holder()->gradient());
			g[0] = type(1);
		}
	};

	// tape：拥有所有值节点，负责拓扑排序 + 反向传播
	template<class type = f32>
	struct tape
	{
		std::vector<std::unique_ptr<value<type>>> nodes;

		// 新建节点（接管生命周期，返回裸指针供构图）
		value<type>* new_value(value_meta&& m)
		{
			auto v = std::make_unique<value<type>>(std::move(m));
			value<type>* raw = v.get();
			nodes.push_back(std::move(v));
			return raw;
		}

		std::vector<value<type>*> all() const
		{
			std::vector<value<type>*> r;
			r.reserve(nodes.size());
			for (auto& n : nodes) r.push_back(n.get());
			return r;
		}

		// 反向传播：从 loss 出发，沿拓扑序逆序调用各节点 backward_fn
		void backward(value<type>* loss)
		{
			auto order = xhqm::topological_sort(all());
			if (order.empty()) throw xhqm::error("ad 计算图存在环");
			for (auto* v : order) v->zero_grad();
			loss->seed_grad_one();
			for (auto it = order.rbegin(); it != order.rend(); ++it)
				if ((*it)->backward_fn()) (*it)->backward_fn()();
		}
	};

}
}
}

#endif
