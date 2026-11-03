#ifndef XHQM_SOUL_AD_IO
#define XHQM_SOUL_AD_IO

// 整图序列化：把 tape 的计算图（节点 + 连接 + 权重 + 算子超参）落盘 / 回读。
// 节点按 {id, op, name, params, inputs, payload} 存 json；payload 复用 tensor_payload
// （dtype + shape + 原始字节序），梯度不持久化（回读后重建为零）。
// 反向闭包 std::function 不可序列化，故回读后按 op 名 + 超参重新安装（reinstall_backward）。
#include "ad_ops.h"
#include <unordered_map>

namespace xhqm {
namespace soul {
namespace ad {

	// —— 反向重建：按 op 名 + 超参把 backward_fn 重新装回节点（与 ad_ops.h 前向安装等价）——
	// 元素级算子按 dim 分派；下述 install_* 与 ad_ops.h 中对应 backward 闭包逐字等价。
	namespace detail
	{
		template<xhqm::size dim, class type>
		void install_add(value<type>* a, value<type>* b, value<type>* out)
		{
			out->backward_fn() = [a, b, out]() {
				auto* pa = as_payload<dim, type>(a->holder());
				auto* pb = as_payload<dim, type>(b->holder());
				auto* po = as_payload<dim, type>(out->holder());
				pa->grad = pa->grad + po->grad;
				pb->grad = pb->grad + po->grad;
			};
		}
		template<xhqm::size dim, class type>
		void install_mul(value<type>* a, value<type>* b, value<type>* out)
		{
			out->backward_fn() = [a, b, out]() {
				auto* pa = as_payload<dim, type>(a->holder());
				auto* pb = as_payload<dim, type>(b->holder());
				auto* po = as_payload<dim, type>(out->holder());
				pa->grad = pa->grad + po->grad * pb->value;
				pb->grad = pb->grad + po->grad * pa->value;
			};
		}
		template<xhqm::size dim, class type>
		void install_relu(value<type>* x, value<type>* out)
		{
			out->backward_fn() = [x, out]() {
				auto* px = as_payload<dim, type>(x->holder());
				auto* po = as_payload<dim, type>(out->holder());
				auto& be = tensor_base::tensor_backend_active();
				if (be.grad_accum(px->grad.data(), po->grad.data(), px->value.data(),
					px->value.size(), tensor_base::dtype_of<type>::value, tensor_base::grad_relu))
					return;
				be.parallel_for(0, px->value.size(), [&](xhqm::index i) {
					px->grad[i] += (px->value[i] > type(0)) ? po->grad[i] : type(0);
				});
			};
		}
		template<xhqm::size dim, class type>
		void install_leaky_relu(value<type>* x, value<type>* out, type slope)
		{
			out->backward_fn() = [x, out, slope]() {
				auto* px = as_payload<dim, type>(x->holder());
				auto* po = as_payload<dim, type>(out->holder());
				tensor_base::tensor_backend_active().parallel_for(0, px->value.size(), [&](xhqm::index i) {
					px->grad[i] += po->grad[i] * (px->value[i] > type(0) ? type(1) : slope);
				});
			};
		}
		template<xhqm::size dim, class type>
		void install_sigmoid(value<type>* x, value<type>* out)
		{
			out->backward_fn() = [x, out]() {
				auto* px = as_payload<dim, type>(x->holder());
				auto* po = as_payload<dim, type>(out->holder());
				auto& be = tensor_base::tensor_backend_active();
				if (be.grad_accum(px->grad.data(), po->grad.data(), po->value.data(),
					px->value.size(), tensor_base::dtype_of<type>::value, tensor_base::grad_sigmoid))
					return;
				be.parallel_for(0, px->value.size(), [&](xhqm::index i) {
					px->grad[i] += po->grad[i] * po->value[i] * (type(1) - po->value[i]);
				});
			};
		}
		template<xhqm::size dim, class type>
		void install_add_scalar(value<type>* x, value<type>* s, value<type>* out)
		{
			out->backward_fn() = [x, s, out]() {
				auto* px = as_payload<dim, type>(x->holder());
				auto* po = as_payload<dim, type>(out->holder());
				px->grad = px->grad + po->grad;
				type sum = type(0);
				for (xhqm::size i = 0; i < po->grad.size(); ++i) sum += po->grad[i];
				static_cast<type*>(s->holder()->gradient())[0] += sum;
			};
		}
		template<xhqm::size dim, class type>
		void install_mul_scalar(value<type>* x, value<type>* s, value<type>* out)
		{
			out->backward_fn() = [x, s, out]() {
				auto* px = as_payload<dim, type>(x->holder());
				auto* po = as_payload<dim, type>(out->holder());
				type sc = static_cast<const type*>(s->holder()->data())[0];
				px->grad = px->grad + po->grad * sc;
				type sum = type(0);
				for (xhqm::size i = 0; i < po->grad.size(); ++i) sum += po->grad[i] * px->value[i];
				static_cast<type*>(s->holder()->gradient())[0] += sum;
			};
		}
	}

	// 按 op 名重建单节点反向
	template<class type>
	void reinstall_backward(value<type>* node)
	{
		const std::string& op = node->op();
		auto ins = node->inputs();
		if (op == "leaf") { node->backward_fn() = nullptr; return; }

		if (op == "matmul")
		{
			value<type>* a = ins[0]; value<type>* b = ins[1]; value<type>* out = node;
			node->backward_fn() = [a, b, out]() {
				auto* pa = as_payload<2, type>(a->holder());
				auto* pb = as_payload<2, type>(b->holder());
				auto* po = as_payload<2, type>(out->holder());
				pa->grad = pa->grad + tensor_base::matmul(po->grad, tensor_base::transpose(pb->value, 0, 1));
				pb->grad = pb->grad + tensor_base::matmul(tensor_base::transpose(pa->value, 0, 1), po->grad);
			};
			return;
		}
		if (op == "add")
		{
			value<type>* a = ins[0]; value<type>* b = ins[1]; value<type>* out = node;
			switch (node->dim())
			{
			case 2: detail::install_add<2, type>(a, b, out); return;
			case 1: detail::install_add<1, type>(a, b, out); return;
			default: detail::install_add<0, type>(a, b, out); return;
			}
		}
		if (op == "add_scalar")
		{
			value<type>* x = ins[0]; value<type>* s = ins[1]; value<type>* out = node;
			switch (node->dim())
			{
			case 2: detail::install_add_scalar<2, type>(x, s, out); return;
			case 1: detail::install_add_scalar<1, type>(x, s, out); return;
			default: throw xhqm::error("reinstall add_scalar 维度不支持");
			}
		}
		if (op == "mul")
		{
			value<type>* a = ins[0]; value<type>* b = ins[1]; value<type>* out = node;
			switch (node->dim())
			{
			case 2: detail::install_mul<2, type>(a, b, out); return;
			case 1: detail::install_mul<1, type>(a, b, out); return;
			default: detail::install_mul<0, type>(a, b, out); return;
			}
		}
		if (op == "relu")
		{
			value<type>* x = ins[0]; value<type>* out = node;
			switch (node->dim())
			{
			case 2: detail::install_relu<2, type>(x, out); return;
			case 1: detail::install_relu<1, type>(x, out); return;
			default: detail::install_relu<0, type>(x, out); return;
			}
		}
		if (op == "leaky_relu")
		{
			value<type>* x = ins[0]; value<type>* out = node;
			type slope = type(node->params().value("slope", 0.01));
			switch (node->dim())
			{
			case 2: detail::install_leaky_relu<2, type>(x, out, slope); return;
			case 1: detail::install_leaky_relu<1, type>(x, out, slope); return;
			default: detail::install_leaky_relu<0, type>(x, out, slope); return;
			}
		}
		if (op == "sigmoid")
		{
			value<type>* x = ins[0]; value<type>* out = node;
			switch (node->dim())
			{
			case 2: detail::install_sigmoid<2, type>(x, out); return;
			case 1: detail::install_sigmoid<1, type>(x, out); return;
			default: detail::install_sigmoid<0, type>(x, out); return;
			}
		}
		if (op == "sum")
		{
			value<type>* x = ins[0]; value<type>* out = node;
			node->backward_fn() = [x, out]() {
				const xhqm::size n = x->holder()->size();
				type g = static_cast<const type*>(out->holder()->gradient())[0];
				type* xg = static_cast<type*>(x->holder()->gradient());
				tensor_base::tensor_backend_active().parallel_for(0, n, [&](xhqm::index i) {
					xg[i] += g;
				});
			};
			return;
		}
		if (op == "mse")
		{
			value<type>* pred = ins[0]; value<type>* target = ins[1]; value<type>* out = node;
			node->backward_fn() = [pred, target, out]() {
				const xhqm::size n = pred->holder()->size();
				type g = static_cast<const type*>(out->holder()->gradient())[0];
				type* pg = static_cast<type*>(pred->holder()->gradient());
				type* tg = static_cast<type*>(target->holder()->gradient());
				const type* pd = static_cast<const type*>(pred->holder()->data());
				const type* td = static_cast<const type*>(target->holder()->data());
				const type c = type(2) * g / type(n);
				tensor_base::tensor_backend_active().parallel_for(0, n, [&](xhqm::index i) {
					type d = pd[i] - td[i]; pg[i] += c * d; tg[i] -= c * d;
				});
			};
			return;
		}
		if (op == "softmax")
		{
			value<type>* x = ins[0]; value<type>* out = node;
			node->backward_fn() = [x, out]() {
				auto* px = as_payload<2, type>(x->holder());
				auto* po = as_payload<2, type>(out->holder());
				const xhqm::size rows = po->value.dimension(0);
				const xhqm::size cols = po->value.dimension(1);
				tensor_base::tensor_backend_active().parallel_for(0, rows, [&](xhqm::size i) {
					type dot = type(0);
					for (xhqm::size j = 0; j < cols; ++j) dot += po->grad(i, j) * po->value(i, j);
					for (xhqm::size j = 0; j < cols; ++j)
						px->grad(i, j) += po->value(i, j) * (po->grad(i, j) - dot);
				});
			};
			return;
		}
		if (op == "softmax_ce")
		{
			value<type>* logits = ins[0]; value<type>* out = node;
			std::vector<xhqm::size> target = node->params().at("target").template get<std::vector<xhqm::size>>();
			node->backward_fn() = [logits, out, target]() {
				auto* px = as_payload<2, type>(logits->holder());
				auto* po = as_payload<0, type>(out->holder());
				const xhqm::size batch = px->value.dimension(0);
				const xhqm::size vocab = px->value.dimension(1);
				const type g = po->grad[0] / type(batch);
				for (xhqm::size i = 0; i < batch; ++i)
				{
					type m = px->value(i, 0);
					for (xhqm::size j = 1; j < vocab; ++j) if (px->value(i, j) > m) m = px->value(i, j);
					type sum = type(0);
					for (xhqm::size j = 0; j < vocab; ++j) sum += type(std::exp(px->value(i, j) - m));
					for (xhqm::size j = 0; j < vocab; ++j)
					{
						type p = type(std::exp(px->value(i, j) - m)) / sum;
						px->grad(i, j) += g * (p - (j == target[i] ? type(1) : type(0)));
					}
				}
			};
			return;
		}
		if (op == "layer_norm")
		{
			value<type>* x = ins[0]; value<type>* gamma = ins[1]; value<type>* beta = ins[2]; value<type>* out = node;
			type eps = type(node->params().value("eps", 1e-5));
			node->backward_fn() = [x, gamma, beta, out, eps]() {
				auto* px = as_payload<2, type>(x->holder());
				auto* pg = as_payload<1, type>(gamma->holder());
				auto* pb = as_payload<1, type>(beta->holder());
				auto* po = as_payload<2, type>(out->holder());
				const xhqm::size rows = px->value.dimension(0);
				const xhqm::size cols = px->value.dimension(1);
				const type n = type(cols);
				for (xhqm::size i = 0; i < rows; ++i)
				{
					type mean = type(0);
					for (xhqm::size j = 0; j < cols; ++j) mean += px->value(i, j);
					mean /= n;
					type var = type(0);
					for (xhqm::size j = 0; j < cols; ++j) { type d = px->value(i, j) - mean; var += d * d; }
					var /= n;
					type rstd = type(1) / type(std::sqrt(var + eps));
					type dvar_sum = type(0), dmean_sum = type(0);
					for (xhqm::size j = 0; j < cols; ++j)
					{
						type xh = (px->value(i, j) - mean) * rstd;
						type dxh = po->grad(i, j) * pg->value[j];
						dvar_sum += dxh * xh;
						dmean_sum += dxh;
					}
					type dvar = dvar_sum * type(-0.5) * rstd * rstd;
					type dmean = dmean_sum * (-rstd);
					for (xhqm::size j = 0; j < cols; ++j)
					{
						type d = px->value(i, j) - mean;
						px->grad(i, j) += po->grad(i, j) * pg->value[j] * rstd + dvar * type(2) * d / n + dmean / n;
						pg->grad[j] += po->grad(i, j) * d * rstd;
						pb->grad[j] += po->grad(i, j);
					}
				}
			};
			return;
		}
		if (op == "embedding")
		{
			value<type>* weight = ins[0]; value<type>* out = node;
			std::vector<xhqm::size> ids = node->params().at("ids").template get<std::vector<xhqm::size>>();
			node->backward_fn() = [weight, out, ids]() {
				auto* pw = as_payload<2, type>(weight->holder());
				auto* po = as_payload<2, type>(out->holder());
				const xhqm::size hidden = pw->value.dimension(1);
				for (xhqm::size i = 0; i < ids.size(); ++i)
					for (xhqm::size j = 0; j < hidden; ++j)
						pw->grad(ids[i], j) += po->grad(i, j);
			};
			return;
		}
		if (op == "transpose")
		{
			value<type>* x = ins[0]; value<type>* out = node;
			node->backward_fn() = [x, out]() {
				auto* px = as_payload<2, type>(x->holder());
				auto* po = as_payload<2, type>(out->holder());
				px->grad = px->grad + tensor_base::transpose(po->grad, 0, 1);
			};
			return;
		}
		if (op == "mul_scalar")
		{
			value<type>* x = ins[0]; value<type>* s = ins[1]; value<type>* out = node;
			switch (node->dim())
			{
			case 2: detail::install_mul_scalar<2, type>(x, s, out); return;
			case 1: detail::install_mul_scalar<1, type>(x, s, out); return;
			default: throw xhqm::error("reinstall mul_scalar 维度不支持");
			}
		}
		if (op == "slice_columns")
		{
			value<type>* x = ins[0]; value<type>* out = node;
			xhqm::size c0 = node->params().at("c0").template get<xhqm::size>();
			node->backward_fn() = [x, out, c0]() {
				auto* px = as_payload<2, type>(x->holder());
				auto* po = as_payload<2, type>(out->holder());
				const xhqm::size rows = po->value.dimension(0);
				const xhqm::size n = po->value.dimension(1);
				for (xhqm::size r = 0; r < rows; ++r)
					for (xhqm::size c = 0; c < n; ++c)
						px->grad(r, c0 + c) += po->grad(r, c);
			};
			return;
		}
		if (op == "concat_columns")
		{
			std::vector<value<type>*> xs = ins; value<type>* out = node;
			node->backward_fn() = [xs, out]() {
				auto* po = as_payload<2, type>(out->holder());
				const xhqm::size rows = po->value.dimension(0);
				xhqm::size off = 0;
				for (auto* x : xs)
				{
					auto* px = as_payload<2, type>(x->holder());
					const xhqm::size n = px->value.dimension(1);
					for (xhqm::size r = 0; r < rows; ++r)
						for (xhqm::size c = 0; c < n; ++c)
							px->grad(r, c) += po->grad(r, off + c);
					off += n;
				}
			};
			return;
		}
		throw xhqm::error("reinstall_backward 未知算子: " + op);
	}

	// —— 序列化 ——
	template<class type>
	nlohmann::json save_tape(const tape<type>& t)
	{
		nlohmann::json nodes = nlohmann::json::array();
		for (auto* v : t.all())
		{
			nlohmann::json n;
			n["id"] = xhqm::cipher::uuid::to_text(v->id());
			n["op"] = v->op();
			n["name"] = v->name();
			if (!v->params().is_null()) n["params"] = v->params();
			nlohmann::json ins = nlohmann::json::array();
			for (auto* p : v->inputs()) ins.push_back(xhqm::cipher::uuid::to_text(p->id()));
			n["inputs"] = ins;
			n["payload"] = v->holder()->save_json();
			nodes.push_back(std::move(n));
		}
		nlohmann::json j;
		j["nodes"] = std::move(nodes);
		return j;
	}

	// 按 json payload 重建类型擦除载荷（dim 运行时 0/1/2 分派）
	template<class type>
	std::shared_ptr<tensor_holder> make_payload(const nlohmann::json& j)
	{
		switch (j.at("dim").get<xhqm::size>())
		{
		case 0: { auto p = std::make_shared<tensor_payload<0, type>>(); p->read_json(j); return p; }
		case 1: { auto p = std::make_shared<tensor_payload<1, type>>(); p->read_json(j); return p; }
		case 2: { auto p = std::make_shared<tensor_payload<2, type>>(); p->read_json(j); return p; }
		default: throw xhqm::error("load_tape 不支持该维度");
		}
	}

	template<class type>
	tape<type> load_tape(const nlohmann::json& j)
	{
		tape<type> t;
		std::unordered_map<std::string, value<type>*> by_id;
		by_id.reserve(j.at("nodes").size());

		// 第一遍：建节点（值 + 元数据），不连边
		for (const auto& n : j.at("nodes"))
		{
			value_meta m;
			m.id = xhqm::cipher::uuid::to_uuid(n.at("id").get<std::string>());
			m.op = n.at("op").get<std::string>();
			m.name = n.value("name", std::string());
			if (n.contains("params") && !n.at("params").is_null())
				m.params = n.at("params");
			m.payload = make_payload<type>(n.at("payload"));
			value<type>* v = t.new_value(std::move(m));
			by_id[xhqm::cipher::uuid::to_text(v->id())] = v;
		}
		// 第二遍：连边（input → this，即数据流方向）
		for (const auto& n : j.at("nodes"))
		{
			value<type>* v = by_id.at(n.at("id").get<std::string>());
			for (const auto& in : n.at("inputs"))
				v->add_input(by_id.at(in.get<std::string>()));
		}
		// 第三遍：重建反向闭包
		for (auto* v : t.all())
			reinstall_backward<type>(v);
		return t;
	}

}
}
}

#endif
