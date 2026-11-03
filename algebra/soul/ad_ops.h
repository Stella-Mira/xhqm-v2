#ifndef XHQM_SOUL_AD_OPS
#define XHQM_SOUL_AD_OPS

// reverse-mode AD 算子：前向建节点 + 反向闭包（把本节点梯度传播给输入）。
// 标量类型编译期确定（type），维度运行时擦除（0/1/2，逐元素算子按 dim 分派）。
#include "ad_node.h"
#include "../tensor/tensor.h"
#include "../tensor/conv_grad.h"
#include "activation.h"
#include <array>
#include <string>

namespace xhqm {
namespace soul {
namespace ad {

	// 创建叶子/参数节点（无反向，梯度在 backward 时由下游累加）
	template<xhqm::size dim, class type>
	value<type>* make_value(tape<type>& t, const tensor<dim, type>& data, std::string op = "leaf", std::string name = "")
	{
		value_meta m;
		m.op = std::move(op);
		m.name = std::move(name);
		m.id = xhqm::cipher::uuid::creat_uuid_v4();
		m.payload = std::make_shared<tensor_payload<dim, type>>(data);
		return t.new_value(std::move(m));
	}

	// 共享载荷创建节点（不拷贝 value，供 module 把参数 payload 注册进 tape，跨 epoch 持久）
	template<class type>
	value<type>* make_value(tape<type>& t, std::shared_ptr<tensor_holder> payload, std::string op = "leaf", std::string name = "")
	{
		value_meta m;
		m.op = std::move(op);
		m.name = std::move(name);
		m.id = xhqm::cipher::uuid::creat_uuid_v4();
		m.payload = std::move(payload);
		return t.new_value(std::move(m));
	}

	// —— 矩阵乘（仅 2D）：C = A @ B ——
	template<class type>
	value<type>* matmul(tape<type>& t, value<type>* a, value<type>* b)
	{
		auto* pa = as_payload<2, type>(a->holder());
		auto* pb = as_payload<2, type>(b->holder());
		value<type>* out = make_value<2, type>(t, tensor_base::matmul(pa->value, pb->value), "matmul");
		out->add_input(a);
		out->add_input(b);
		out->backward_fn() = [a, b, out]() {
			auto* pa = as_payload<2, type>(a->holder());
			auto* pb = as_payload<2, type>(b->holder());
			auto* po = as_payload<2, type>(out->holder());
			// dA = dC @ B^T，dB = A^T @ dC
			pa->grad = pa->grad + tensor_base::matmul(po->grad, tensor_base::transpose(pb->value, 0, 1));
			pb->grad = pb->grad + tensor_base::matmul(tensor_base::transpose(pa->value, 0, 1), po->grad);
		};
		return out;
	}

	// —— 逐元素 add（同 shape）——
	template<xhqm::size dim, class type>
	value<type>* add_impl(tape<type>& t, value<type>* a, value<type>* b)
	{
		auto* pa = as_payload<dim, type>(a->holder());
		auto* pb = as_payload<dim, type>(b->holder());
		value<type>* out = make_value<dim, type>(t, pa->value + pb->value, "add");
		out->add_input(a);
		out->add_input(b);
		out->backward_fn() = [a, b, out]() {
			auto* pa = as_payload<dim, type>(a->holder());
			auto* pb = as_payload<dim, type>(b->holder());
			auto* po = as_payload<dim, type>(out->holder());
			pa->grad = pa->grad + po->grad;
			pb->grad = pb->grad + po->grad;
		};
		return out;
	}
	// 标量 + 张量广播（bias 等）：x(dim) + s(标量)
	template<xhqm::size dim, class type>
	value<type>* add_scalar_impl(tape<type>& t, value<type>* x, value<type>* s)
	{
		auto* px = as_payload<dim, type>(x->holder());
		type sc = static_cast<const type*>(s->holder()->data())[0];
		value<type>* out = make_value<dim, type>(t, px->value + sc, "add_scalar");
		out->add_input(x);
		out->add_input(s);
		out->backward_fn() = [x, s, out]() {
			auto* px = as_payload<dim, type>(x->holder());
			auto* po = as_payload<dim, type>(out->holder());
			px->grad = px->grad + po->grad;
			type sum = type(0);
			for (xhqm::size i = 0; i < po->grad.size(); ++i) sum += po->grad[i];
			static_cast<type*>(s->holder()->gradient())[0] += sum;
		};
		return out;
	}
	template<class type>
	value<type>* add_scalar(tape<type>& t, value<type>* x, value<type>* s)
	{
		switch (x->dim())
		{
		case 2: return add_scalar_impl<2, type>(t, x, s);
		case 1: return add_scalar_impl<1, type>(t, x, s);
		default: throw xhqm::error("ad add_scalar 不支持该维度");
		}
	}
	template<class type>
	value<type>* add(tape<type>& t, value<type>* a, value<type>* b)
	{
		if (b->dim() == 0) return add_scalar(t, a, b);
		if (a->dim() == 0) return add_scalar(t, b, a);
		switch (a->dim())
		{
		case 2: return add_impl<2, type>(t, a, b);
		case 1: return add_impl<1, type>(t, a, b);
		case 0: return add_impl<0, type>(t, a, b);
		default: throw xhqm::error("ad add 不支持该维度");
		}
	}

	// —— 逐元素 mul（同 shape）——
	template<xhqm::size dim, class type>
	value<type>* mul_impl(tape<type>& t, value<type>* a, value<type>* b)
	{
		auto* pa = as_payload<dim, type>(a->holder());
		auto* pb = as_payload<dim, type>(b->holder());
		value<type>* out = make_value<dim, type>(t, pa->value * pb->value, "mul");
		out->add_input(a);
		out->add_input(b);
		out->backward_fn() = [a, b, out]() {
			auto* pa = as_payload<dim, type>(a->holder());
			auto* pb = as_payload<dim, type>(b->holder());
			auto* po = as_payload<dim, type>(out->holder());
			pa->grad = pa->grad + po->grad * pb->value;  // dA = dC * B
			pb->grad = pb->grad + po->grad * pa->value;  // dB = dC * A
		};
		return out;
	}
	template<class type>
	value<type>* mul(tape<type>& t, value<type>* a, value<type>* b)
	{
		switch (a->dim())
		{
		case 2: return mul_impl<2, type>(t, a, b);
		case 1: return mul_impl<1, type>(t, a, b);
		case 0: return mul_impl<0, type>(t, a, b);
		default: throw xhqm::error("ad mul 不支持该维度");
		}
	}

	// —— relu（逐元素，前向复用 soul::relu）——
	template<xhqm::size dim, class type>
	value<type>* relu_impl(tape<type>& t, value<type>* x)
	{
		auto* px = as_payload<dim, type>(x->holder());
		value<type>* out = make_value<dim, type>(t, soul::relu(px->value), "relu");
		out->add_input(x);
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
		return out;
	}
	template<class type>
	value<type>* relu(tape<type>& t, value<type>* x)
	{
		switch (x->dim())
		{
		case 2: return relu_impl<2, type>(t, x);
		case 1: return relu_impl<1, type>(t, x);
		case 0: return relu_impl<0, type>(t, x);
		default: throw xhqm::error("ad relu 不支持该维度");
		}
	}

	// —— leaky_relu（逐元素，负侧斜率 slope，避免死神经元）——
	template<xhqm::size dim, class type>
	value<type>* leaky_relu_impl(tape<type>& t, value<type>* x, type slope)
	{
		auto* px = as_payload<dim, type>(x->holder());
		tensor<dim, type> c = px->value;
		tensor_base::tensor_backend_active().parallel_for(0, c.size(), [&](xhqm::index i) {
			c[i] = c[i] > type(0) ? c[i] : slope * c[i];
		});
		value<type>* out = make_value<dim, type>(t, c, "leaky_relu");
		out->add_input(x);
		out->params()["slope"] = double(slope);
		out->backward_fn() = [x, out, slope]() {
			auto* px = as_payload<dim, type>(x->holder());
			auto* po = as_payload<dim, type>(out->holder());
			tensor_base::tensor_backend_active().parallel_for(0, px->value.size(), [&](xhqm::index i) {
				px->grad[i] += po->grad[i] * (px->value[i] > type(0) ? type(1) : slope);
			});
		};
		return out;
	}
	template<class type>
	value<type>* leaky_relu(tape<type>& t, value<type>* x, type slope = type(0.01))
	{
		switch (x->dim())
		{
		case 2: return leaky_relu_impl<2, type>(t, x, slope);
		case 1: return leaky_relu_impl<1, type>(t, x, slope);
		case 0: return leaky_relu_impl<0, type>(t, x, slope);
		default: throw xhqm::error("ad leaky_relu 不支持该维度");
		}
	}

	// —— sigmoid（逐元素，前向复用 soul::sigmoid）——
	template<xhqm::size dim, class type>
	value<type>* sigmoid_impl(tape<type>& t, value<type>* x)
	{
		auto* px = as_payload<dim, type>(x->holder());
		value<type>* out = make_value<dim, type>(t, soul::sigmoid(px->value), "sigmoid");
		out->add_input(x);
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
		return out;
	}
	template<class type>
	value<type>* sigmoid(tape<type>& t, value<type>* x)
	{
		switch (x->dim())
		{
		case 2: return sigmoid_impl<2, type>(t, x);
		case 1: return sigmoid_impl<1, type>(t, x);
		case 0: return sigmoid_impl<0, type>(t, x);
		default: throw xhqm::error("ad sigmoid 不支持该维度");
		}
	}

	// —— 全归约到标量：sum(x) ——
	template<class type>
	value<type>* sum_all(tape<type>& t, value<type>* x)
	{
		const xhqm::size n = x->holder()->size();
		const type* xd = static_cast<const type*>(x->holder()->data());
		type s = type(0);
		for (xhqm::size i = 0; i < n; ++i) s += xd[i];
		value<type>* out = make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, s), "sum");
		out->add_input(x);
		out->backward_fn() = [x, out, n]() {
			type g = static_cast<const type*>(out->holder()->gradient())[0];
			type* xg = static_cast<type*>(x->holder()->gradient());
			tensor_base::tensor_backend_active().parallel_for(0, n, [&](xhqm::index i) {
				xg[i] += g;
			});
		};
		return out;
	}

	// —— 均方误差：mean((pred - target)^2) → 标量 ——
	template<class type>
	value<type>* mse(tape<type>& t, value<type>* pred, value<type>* target)
	{
		const xhqm::size n = pred->holder()->size();
		const type* pd = static_cast<const type*>(pred->holder()->data());
		const type* td = static_cast<const type*>(target->holder()->data());
		type s = type(0);
		for (xhqm::size i = 0; i < n; ++i) { type d = pd[i] - td[i]; s += d * d; }
		s /= type(n);
		value<type>* out = make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, s), "mse");
		out->add_input(pred);
		out->add_input(target);
		out->backward_fn() = [pred, target, out, n]() {
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
		return out;
	}

	// —— softmax（2D，沿最后一维；前向数值稳定，反向复用概率）——
	template<class type>
	value<type>* softmax(tape<type>& t, value<type>* x)
	{
		auto* px = as_payload<2, type>(x->holder());
		value<type>* out = make_value<2, type>(t, soul::softmax(px->value), "softmax");
		out->add_input(x);
		out->backward_fn() = [x, out]() {
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
		return out;
	}

	// —— softmax + cross entropy（融合，数值稳定）：logits(2D) + target(类别索引) → 标量 ——
	template<class type>
	value<type>* softmax_cross_entropy(tape<type>& t, value<type>* logits, const std::vector<xhqm::size>& target)
	{
		auto* px = as_payload<2, type>(logits->holder());
		type loss = tensor_base::softmax_cross_entropy(px->value, target);
		value<type>* out = make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, loss), "softmax_ce");
		out->add_input(logits);
		out->params()["target"] = target;
		out->backward_fn() = [logits, out, target]() {
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
		return out;
	}

	// —— layer_norm（2D，沿最后一维；gamma/beta 为 1D features）——
	template<class type>
	value<type>* layer_norm(tape<type>& t, value<type>* x, value<type>* gamma, value<type>* beta, type eps = type(1e-5))
	{
		auto* px = as_payload<2, type>(x->holder());
		auto* pg = as_payload<1, type>(gamma->holder());
		auto* pb = as_payload<1, type>(beta->holder());
		const xhqm::size rows = px->value.dimension(0);
		const xhqm::size cols = px->value.dimension(1);
		tensor<2, type> y(std::array<xhqm::size, 2>{ rows, cols }, type(0));
		for (xhqm::size i = 0; i < rows; ++i)
		{
			type mean = type(0);
			for (xhqm::size j = 0; j < cols; ++j) mean += px->value(i, j);
			mean /= type(cols);
			type var = type(0);
			for (xhqm::size j = 0; j < cols; ++j) { type d = px->value(i, j) - mean; var += d * d; }
			var /= type(cols);
			type rstd = type(1) / type(std::sqrt(var + eps));
			for (xhqm::size j = 0; j < cols; ++j)
				y(i, j) = (px->value(i, j) - mean) * rstd * pg->value[j] + pb->value[j];
		}
		value<type>* out = make_value<2, type>(t, y, "layer_norm");
		out->add_input(x);
		out->add_input(gamma);
		out->add_input(beta);
		out->params()["eps"] = double(eps);
		out->backward_fn() = [x, gamma, beta, out, eps]() {
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
				// dxh = dy * gamma, xh = (x-mean)*rstd
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
					pg->grad[j] += po->grad(i, j) * d * rstd;  // dgamma += dy * x_hat
					pb->grad[j] += po->grad(i, j);              // dbeta += dy
				}
			}
		};
		return out;
	}

	// —— embedding 查表：weight(vocab×hidden) + ids(seq) → (seq×hidden)（前向 gather / 反向 scatter_add 下沉）——
	template<class type>
	value<type>* embedding_lookup(tape<type>& t, value<type>* weight, const std::vector<xhqm::size>& ids)
	{
		auto* pw = as_payload<2, type>(weight->holder());
		value<type>* out = make_value<2, type>(t, tensor_base::gather(pw->value, ids), "embedding");
		out->add_input(weight);
		out->params()["ids"] = ids;
		out->backward_fn() = [weight, out, ids]() {
			auto* pw = as_payload<2, type>(weight->holder());
			auto* po = as_payload<2, type>(out->holder());
			tensor_base::scatter_add(pw->grad, po->grad, ids);
		};
		return out;
	}

	// —— 转置（2D）：反向 = 转置(grad) ——
	template<class type>
	value<type>* transpose(tape<type>& t, value<type>* x)
	{
		auto* px = as_payload<2, type>(x->holder());
		value<type>* out = make_value<2, type>(t, tensor_base::transpose(px->value, 0, 1), "transpose");
		out->add_input(x);
		out->backward_fn() = [x, out]() {
			auto* px = as_payload<2, type>(x->holder());
			auto* po = as_payload<2, type>(out->holder());
			px->grad = px->grad + tensor_base::transpose(po->grad, 0, 1);
		};
		return out;
	}

	// —— 切列（2D，沿最后一维取 [c0,c1)）：反向 = 梯度散射回源列 ——
	template<class type>
	value<type>* slice_columns(tape<type>& t, value<type>* x, xhqm::size c0, xhqm::size c1)
	{
		auto* px = as_payload<2, type>(x->holder());
		const xhqm::size rows = px->value.dimension(0);
		const xhqm::size n = c1 - c0;
		tensor<2, type> y(std::array<xhqm::size, 2>{ rows, n }, type(0));
		for (xhqm::size r = 0; r < rows; ++r)
			for (xhqm::size c = 0; c < n; ++c)
				y(r, c) = px->value(r, c0 + c);
		value<type>* out = make_value<2, type>(t, y, "slice_columns");
		out->add_input(x);
		out->params()["c0"] = c0;
		out->params()["c1"] = c1;
		out->backward_fn() = [x, out, c0]() {
			auto* px = as_payload<2, type>(x->holder());
			auto* po = as_payload<2, type>(out->holder());
			const xhqm::size rows = po->value.dimension(0);
			const xhqm::size n = po->value.dimension(1);
			for (xhqm::size r = 0; r < rows; ++r)
				for (xhqm::size c = 0; c < n; ++c)
					px->grad(r, c0 + c) += po->grad(r, c);
		};
		return out;
	}

	// —— 拼列（2D，多个 rows×col_i 沿最后一维拼接）：反向 = 梯度切片回各输入 ——
	template<class type>
	value<type>* concat_columns(tape<type>& t, const std::vector<value<type>*>& xs)
	{
		if (xs.empty()) throw xhqm::error("ad concat_columns 输入为空");
		const xhqm::size rows = xs[0]->shape()[0];
		xhqm::size total = 0;
		std::vector<xhqm::size> splits;
		splits.reserve(xs.size());
		for (auto* x : xs) { xhqm::size n = x->shape()[1]; splits.push_back(n); total += n; }
		tensor<2, type> y(std::array<xhqm::size, 2>{ rows, total }, type(0));
		xhqm::size off = 0;
		for (xhqm::size i = 0; i < xs.size(); ++i)
		{
			auto* px = as_payload<2, type>(xs[i]->holder());
			const xhqm::size n = splits[i];
			for (xhqm::size r = 0; r < rows; ++r)
				for (xhqm::size c = 0; c < n; ++c)
					y(r, off + c) = px->value(r, c);
			off += n;
		}
		value<type>* out = make_value<2, type>(t, y, "concat_columns");
		for (auto* x : xs) out->add_input(x);
		out->params()["splits"] = splits;
		out->backward_fn() = [xs, out]() {
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
		return out;
	}

	// —— 标量乘广播：x * s ——
	template<xhqm::size dim, class type>
	value<type>* mul_scalar_impl(tape<type>& t, value<type>* x, value<type>* s)
	{
		auto* px = as_payload<dim, type>(x->holder());
		type sc = static_cast<const type*>(s->holder()->data())[0];
		value<type>* out = make_value<dim, type>(t, px->value * sc, "mul_scalar");
		out->add_input(x);
		out->add_input(s);
		out->backward_fn() = [x, s, out]() {
			auto* px = as_payload<dim, type>(x->holder());
			auto* po = as_payload<dim, type>(out->holder());
			type sc = static_cast<const type*>(s->holder()->data())[0];
			px->grad = px->grad + po->grad * sc;
			type sum = type(0);
			for (xhqm::size i = 0; i < po->grad.size(); ++i) sum += po->grad[i] * px->value[i];
			static_cast<type*>(s->holder()->gradient())[0] += sum;
		};
		return out;
	}
	template<class type>
	value<type>* mul_scalar(tape<type>& t, value<type>* x, value<type>* s)
	{
		switch (x->dim())
		{
		case 2: return mul_scalar_impl<2, type>(t, x, s);
		case 1: return mul_scalar_impl<1, type>(t, x, s);
		default: throw xhqm::error("ad mul_scalar 不支持该维度");
		}
	}

	// 因果掩码（下三角 0，上三角 -1e9）
	template<class type>
	tensor<2, type> causal_mask(xhqm::size seq)
	{
		tensor<2, type> m(std::array<xhqm::size, 2>{ seq, seq }, type(0));
		for (xhqm::size i = 0; i < seq; ++i)
			for (xhqm::size j = 0; j < seq; ++j)
				if (j > i) m(i, j) = type(-1e9);
		return m;
	}

	// —— 单头缩放点积注意力（q/k/v 已按头切好）：softmax(mask + q·kᵀ·scale) · v ——
	template<class type>
	value<type>* sdpa(tape<type>& t, value<type>* q, value<type>* k, value<type>* v,
		const tensor<2, type>& mask, type scale)
	{
		auto* scores = matmul(t, q, transpose(t, k));
		auto* sc_node = make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, scale));
		auto* scores_scaled = mul_scalar(t, scores, sc_node);
		auto* mask_node = make_value<2, type>(t, mask);
		auto* scores_masked = add(t, scores_scaled, mask_node);
		auto* probs = softmax(t, scores_masked);
		return matmul(t, probs, v);
	}

	// —— 单头因果自注意力：QKV 投影 + 缩放点积 + 掩码 + softmax + 输出投影 ——
	// x(seq×hidden)，权重均为 (hidden×hidden)。
	template<class type>
	value<type>* attention(tape<type>& t,
		value<type>* x,
		value<type>* wq, value<type>* wk, value<type>* wv, value<type>* wo,
		const tensor<2, type>& mask, type scale)
	{
		auto* q = matmul(t, x, wq);
		auto* k = matmul(t, x, wk);
		auto* v = matmul(t, x, wv);
		auto* oh = sdpa(t, q, k, v, mask, scale);
		return matmul(t, oh, wo);
	}

	// —— 多头因果自注意力：QKV 投影后按 head 切列 → 逐头 sdpa → 拼列 → 输出投影 ——
	// x(seq×hidden)，wq/wk/wv/wo 均为 (hidden×hidden)；要求 hidden 能被 heads 整除。
	// scale 建议 1/√head_dim（head_dim = hidden/heads），与前向 attention.h 一致。
	template<class type>
	value<type>* multi_head_attention(tape<type>& t,
		value<type>* x,
		value<type>* wq, value<type>* wk, value<type>* wv, value<type>* wo,
		xhqm::size heads, const tensor<2, type>& mask, type scale)
	{
		auto* q = matmul(t, x, wq);
		auto* k = matmul(t, x, wk);
		auto* v = matmul(t, x, wv);
		const xhqm::size hidden = q->shape()[1];
		if (hidden % heads != 0) throw xhqm::error("ad multi_head_attention hidden 不能被 heads 整除");
		const xhqm::size head_dim = hidden / heads;
		std::vector<value<type>*> head_outs;
		head_outs.reserve(heads);
		for (xhqm::size h = 0; h < heads; ++h)
		{
			auto* qh = slice_columns(t, q, h * head_dim, (h + 1) * head_dim);
			auto* kh = slice_columns(t, k, h * head_dim, (h + 1) * head_dim);
			auto* vh = slice_columns(t, v, h * head_dim, (h + 1) * head_dim);
			head_outs.push_back(sdpa(t, qh, kh, vh, mask, scale));
		}
		auto* concat = concat_columns(t, head_outs);
		return matmul(t, concat, wo);
	}

	// —— SGD 单步：param -= lr * param.grad ——
	template<class type>
	void sgd_step(value<type>* v, type lr)
	{
		switch (v->dim())
		{
		case 2: { auto* p = as_payload<2, type>(v->holder()); p->value = p->value - p->grad * lr; break; }
		case 1: { auto* p = as_payload<1, type>(v->holder()); p->value = p->value - p->grad * lr; break; }
		case 0: { auto* p = as_payload<0, type>(v->holder()); p->value = p->value - p->grad * lr; break; }
		default: throw xhqm::error("ad sgd_step 不支持该维度");
		}
	}

	// —— 动量 SGD 单步：v = mu*v - lr*g，param += v（velocity 跨 step 持久）——
	// param/velocity 由调用方跨 step 持有（与「每 epoch 新建 tape、参数留在 tape 外」的模式一致）。
	template<xhqm::size dim, class type>
	void momentum_step(tensor<dim, type>& param, const tensor<dim, type>& grad,
		tensor<dim, type>& velocity, type lr, type mu = type(0.9))
	{
		for (xhqm::size i = 0; i < param.size(); ++i)
		{
			velocity[i] = mu * velocity[i] - lr * grad[i];
			param[i] += velocity[i];
		}
	}

	// —— Adam 单步（含偏置修正）：m = b1*m+(1-b1)g，v = b2*v+(1-b2)g²，
	//    param -= lr * (m/(1-b1^t)) / (sqrt(v/(1-b2^t)) + eps) ——
	// m/v 为调用方持有的同形状一阶/二阶矩（初始化为 0），t 为步数（从 1 起）。
	template<xhqm::size dim, class type>
	void adam_step(tensor<dim, type>& param, const tensor<dim, type>& grad, type lr, xhqm::size t,
		tensor<dim, type>& m, tensor<dim, type>& vv,
		type b1 = type(0.9), type b2 = type(0.999), type eps = type(1e-8))
	{
		const type c1 = type(1) - b1;
		const type c2 = type(1) - b2;
		const type bc1 = type(1) - type(std::pow(double(b1), int(t)));
		const type bc2 = type(1) - type(std::pow(double(b2), int(t)));
		for (xhqm::size i = 0; i < param.size(); ++i)
		{
			type g = grad[i];
			m[i] = b1 * m[i] + c1 * g;
			vv[i] = b2 * vv[i] + c2 * g * g;
			type mh = m[i] / bc1;
			type vh = vv[i] / bc2;
			param[i] -= lr * mh / (type(std::sqrt(vh)) + eps);
		}
	}

	// —— where（条件选择，cond 为常量 mask，无梯度）：out = cond ? a : b ——
	template<xhqm::size dim, class type>
	value<type>* where(tape<type>& t, const tensor<dim, bool>& cond, value<type>* a, value<type>* b)
	{
		if (a->dim() != dim || b->dim() != dim)
			throw xhqm::error("ad where cond 维度与输入不一致");
		auto* pa = as_payload<dim, type>(a->holder());
		auto* pb = as_payload<dim, type>(b->holder());
		value<type>* out = make_value<dim, type>(t, tensor_base::where(cond, pa->value, pb->value), "where");
		out->add_input(a);
		out->add_input(b);
		out->backward_fn() = [a, b, out, cond]() {
			auto* pa = as_payload<dim, type>(a->holder());
			auto* pb = as_payload<dim, type>(b->holder());
			auto* po = as_payload<dim, type>(out->holder());
			for (xhqm::size i = 0; i < po->grad.size(); ++i)
			{
				if (cond[i]) pa->grad[i] = xhqm::sum(pa->grad[i], po->grad[i]);
				else         pb->grad[i] = xhqm::sum(pb->grad[i], po->grad[i]);
			}
		};
		return out;
	}

	// —— dropout（inverted，训练态）：前向 dropout + 重建 mask，反向 dx = mask · grad / (1-p) ——
	template<xhqm::size dim, class type>
	value<type>* dropout_impl(tape<type>& t, value<type>* x, type p, uint32_t seed)
	{
		auto* px = as_payload<dim, type>(x->holder());
		tensor<dim, type> d = tensor_base::dropout(px->value, p, seed);
		tensor<dim, bool> mask(px->value.shape(), false);
		const float pf = (float)p;
		for (xhqm::size i = 0; i < mask.size(); ++i)
		{
			uint32_t s = tensor_base::rand_seed_at(seed, i);
			mask[i] = (tensor_base::rand_uniform01(s) >= pf);
		}
		value<type>* out = make_value<dim, type>(t, d, "dropout");
		out->add_input(x);
		out->params()["p"] = double(p);
		out->params()["seed"] = double(seed);
		out->backward_fn() = [x, out, mask, p]() {
			auto* px = as_payload<dim, type>(x->holder());
			auto* po = as_payload<dim, type>(out->holder());
			const type scale = xhqm::div(type(1), xhqm::sub(type(1), p));
			for (xhqm::size i = 0; i < po->grad.size(); ++i)
				if (mask[i]) px->grad[i] = xhqm::sum(px->grad[i], xhqm::mul(po->grad[i], scale));
		};
		return out;
	}
	template<class type>
	value<type>* dropout(tape<type>& t, value<type>* x, type p, uint32_t seed)
	{
		switch (x->dim())
		{
		case 2: return dropout_impl<2, type>(t, x, p, seed);
		case 1: return dropout_impl<1, type>(t, x, p, seed);
		case 0: return dropout_impl<0, type>(t, x, p, seed);
		default: throw xhqm::error("ad dropout 不支持该维度");
		}
	}

	// —— max_pool2d（NCHW，反向仅把梯度传回 argmax 位置）——
	template<class type>
	value<type>* max_pool2d(tape<type>& t, value<type>* x, xhqm::size k, xhqm::size stride, xhqm::size padding)
	{
		auto* px = as_payload<4, type>(x->holder());
		const xhqm::size N = px->value.dimension(0), C = px->value.dimension(1), H = px->value.dimension(2), W = px->value.dimension(3);
		const xhqm::size Oh = tensor_base::pool_out_size(H, k, stride, padding);
		const xhqm::size Ow = tensor_base::pool_out_size(W, k, stride, padding);
		tensor<4, type> y(std::array<xhqm::size, 4>{ N, C, Oh, Ow }, type(0));
		tensor<4, xhqm::index> idx(std::array<xhqm::size, 4>{ N, C, Oh, Ow }, xhqm::index(0));
		for (xhqm::size n = 0; n < N; ++n)
			for (xhqm::size c = 0; c < C; ++c)
				for (xhqm::size oh = 0; oh < Oh; ++oh)
					for (xhqm::size ow = 0; ow < Ow; ++ow)
					{
						const std::int64_t h0 = (std::int64_t)oh * (std::int64_t)stride - (std::int64_t)padding;
						const std::int64_t w0 = (std::int64_t)ow * (std::int64_t)stride - (std::int64_t)padding;
						bool first = true;
						type acc = type(0);
						xhqm::index ai = 0;
						for (xhqm::size kh = 0; kh < k; ++kh)
							for (xhqm::size kw = 0; kw < k; ++kw)
							{
								const std::int64_t hh = h0 + (std::int64_t)kh;
								const std::int64_t ww = w0 + (std::int64_t)kw;
								if (hh < 0 || hh >= (std::int64_t)H || ww < 0 || ww >= (std::int64_t)W) continue;
								type v = px->value(n, c, (xhqm::size)hh, (xhqm::size)ww);
								if (first) { acc = v; ai = ((n * C + c) * H + (xhqm::size)hh) * W + (xhqm::size)ww; first = false; }
								else if (v > acc) { acc = v; ai = ((n * C + c) * H + (xhqm::size)hh) * W + (xhqm::size)ww; }
							}
						y(n, c, oh, ow) = acc;
						idx(n, c, oh, ow) = ai;
					}
		value<type>* out = make_value<4, type>(t, y, "max_pool2d");
		out->add_input(x);
		out->params()["k"] = double(k); out->params()["stride"] = double(stride); out->params()["padding"] = double(padding);
		out->backward_fn() = [x, out, idx]() {
			auto* px = as_payload<4, type>(x->holder());
			auto* po = as_payload<4, type>(out->holder());
			for (xhqm::size i = 0; i < po->grad.size(); ++i)
				px->grad[idx[i]] = xhqm::sum(px->grad[idx[i]], po->grad[i]);
		};
		return out;
	}

	// —— avg_pool2d（NCHW，反向均匀散射到窗口内有效元素）——
	template<class type>
	value<type>* avg_pool2d(tape<type>& t, value<type>* x, xhqm::size k, xhqm::size stride, xhqm::size padding)
	{
		auto* px = as_payload<4, type>(x->holder());
		auto y = tensor_base::avg_pool2d(px->value, k, stride, padding);
		value<type>* out = make_value<4, type>(t, y, "avg_pool2d");
		out->add_input(x);
		out->params()["k"] = double(k); out->params()["stride"] = double(stride); out->params()["padding"] = double(padding);
		out->backward_fn() = [x, out, k, stride, padding]() {
			auto* px = as_payload<4, type>(x->holder());
			auto* po = as_payload<4, type>(out->holder());
			const xhqm::size N = px->value.dimension(0), C = px->value.dimension(1), H = px->value.dimension(2), W = px->value.dimension(3);
			const xhqm::size Oh = po->grad.dimension(2), Ow = po->grad.dimension(3);
			for (xhqm::size n = 0; n < N; ++n)
				for (xhqm::size c = 0; c < C; ++c)
					for (xhqm::size oh = 0; oh < Oh; ++oh)
						for (xhqm::size ow = 0; ow < Ow; ++ow)
						{
							const std::int64_t h0 = (std::int64_t)oh * (std::int64_t)stride - (std::int64_t)padding;
							const std::int64_t w0 = (std::int64_t)ow * (std::int64_t)stride - (std::int64_t)padding;
							xhqm::size cnt = 0;
							for (xhqm::size kh = 0; kh < k; ++kh)
								for (xhqm::size kw = 0; kw < k; ++kw)
								{
									const std::int64_t hh = h0 + (std::int64_t)kh;
									const std::int64_t ww = w0 + (std::int64_t)kw;
									if (hh < 0 || hh >= (std::int64_t)H || ww < 0 || ww >= (std::int64_t)W) continue;
									++cnt;
								}
							if (cnt == 0) continue;
							const type g = xhqm::div(po->grad(n, c, oh, ow), type(cnt));
							for (xhqm::size kh = 0; kh < k; ++kh)
								for (xhqm::size kw = 0; kw < k; ++kw)
								{
									const std::int64_t hh = h0 + (std::int64_t)kh;
									const std::int64_t ww = w0 + (std::int64_t)kw;
									if (hh < 0 || hh >= (std::int64_t)H || ww < 0 || ww >= (std::int64_t)W) continue;
									px->grad(n, c, (xhqm::size)hh, (xhqm::size)ww) = xhqm::sum(
										px->grad(n, c, (xhqm::size)hh, (xhqm::size)ww), g);
								}
						}
		};
		return out;
	}

	// —— upsample_nearest2d（NCHW，反向按 1/scale 散射回源位置并累加）——
	template<class type>
	value<type>* upsample_nearest2d(tape<type>& t, value<type>* x, xhqm::size scale_h, xhqm::size scale_w)
	{
		auto* px = as_payload<4, type>(x->holder());
		auto y = tensor_base::upsample_nearest2d(px->value, scale_h, scale_w);
		value<type>* out = make_value<4, type>(t, y, "upsample_nearest2d");
		out->add_input(x);
		out->params()["scale_h"] = double(scale_h);
		out->params()["scale_w"] = double(scale_w);
		out->backward_fn() = [x, out, scale_h, scale_w]() {
			auto* px = as_payload<4, type>(x->holder());
			auto* po = as_payload<4, type>(out->holder());
			const xhqm::size N = px->value.dimension(0), C = px->value.dimension(1);
			const xhqm::size Oh = po->grad.dimension(2), Ow = po->grad.dimension(3);
			for (xhqm::size n = 0; n < N; ++n)
				for (xhqm::size c = 0; c < C; ++c)
					for (xhqm::size oh = 0; oh < Oh; ++oh)
						for (xhqm::size ow = 0; ow < Ow; ++ow)
							px->grad(n, c, oh / scale_h, ow / scale_w) = xhqm::sum(
								px->grad(n, c, oh / scale_h, ow / scale_w), po->grad(n, c, oh, ow));
		};
		return out;
	}

	// —— upsample_bilinear2d（NCHW，反向按插值权重散射）——
	template<class type>
	value<type>* upsample_bilinear2d(tape<type>& t, value<type>* x, xhqm::size out_h, xhqm::size out_w)
	{
		auto* px = as_payload<4, type>(x->holder());
		auto y = tensor_base::upsample_bilinear2d(px->value, out_h, out_w);
		value<type>* out = make_value<4, type>(t, y, "upsample_bilinear2d");
		out->add_input(x);
		out->params()["out_h"] = double(out_h);
		out->params()["out_w"] = double(out_w);
		out->backward_fn() = [x, out, out_h, out_w]() {
			auto* px = as_payload<4, type>(x->holder());
			auto* po = as_payload<4, type>(out->holder());
			const xhqm::size N = px->value.dimension(0), C = px->value.dimension(1), H = px->value.dimension(2), W = px->value.dimension(3);
			const double scale_h = (double)H / (double)out_h;
			const double scale_w = (double)W / (double)out_w;
			for (xhqm::size n = 0; n < N; ++n)
				for (xhqm::size c = 0; c < C; ++c)
					for (xhqm::size oh = 0; oh < out_h; ++oh)
						for (xhqm::size ow = 0; ow < out_w; ++ow)
						{
							double sh = ((double)oh + 0.5) * scale_h - 0.5;
							double sw = ((double)ow + 0.5) * scale_w - 0.5;
							if (sh < 0.0) sh = 0.0; if (sh > (double)(H - 1)) sh = (double)(H - 1);
							if (sw < 0.0) sw = 0.0; if (sw > (double)(W - 1)) sw = (double)(W - 1);
							const xhqm::size h0 = (xhqm::size)sh;
							const xhqm::size w0 = (xhqm::size)sw;
							const xhqm::size h1 = (h0 + 1 < H) ? h0 + 1 : h0;
							const xhqm::size w1 = (w0 + 1 < W) ? w0 + 1 : w0;
							const type hw = type(sh - (double)h0);
							const type ww = type(sw - (double)w0);
							const type one = type(1);
							const type g = po->grad(n, c, oh, ow);
							px->grad(n, c, h0, w0) = xhqm::sum(px->grad(n, c, h0, w0), xhqm::mul(g, xhqm::mul(xhqm::sub(one, hw), xhqm::sub(one, ww))));
							px->grad(n, c, h0, w1) = xhqm::sum(px->grad(n, c, h0, w1), xhqm::mul(g, xhqm::mul(xhqm::sub(one, hw), ww)));
							px->grad(n, c, h1, w0) = xhqm::sum(px->grad(n, c, h1, w0), xhqm::mul(g, xhqm::mul(hw, xhqm::sub(one, ww))));
							px->grad(n, c, h1, w1) = xhqm::sum(px->grad(n, c, h1, w1), xhqm::mul(g, xhqm::mul(hw, ww)));
						}
		};
		return out;
	}

	// —— unsqueeze（dim→dim+1，插入 size-1 维；反向 = squeeze 回原 dim）——
	template<xhqm::size dim, class type>
	value<type>* unsqueeze_impl(tape<type>& t, value<type>* x, xhqm::size axis)
	{
		auto* px = as_payload<dim, type>(x->holder());
		value<type>* out = make_value<dim + 1, type>(t, tensor_base::unsqueeze(px->value, axis), "unsqueeze");
		out->add_input(x);
		out->params()["axis"] = double(axis);
		out->backward_fn() = [x, out, axis]() {
			auto* px = as_payload<dim, type>(x->holder());
			auto* po = as_payload<dim + 1, type>(out->holder());
			px->grad = px->grad + tensor_base::squeeze(po->grad, axis);
		};
		return out;
	}
	template<class type>
	value<type>* unsqueeze(tape<type>& t, value<type>* x, xhqm::size axis)
	{
		switch (x->dim())
		{
		case 2: return unsqueeze_impl<2, type>(t, x, axis);
		case 1: return unsqueeze_impl<1, type>(t, x, axis);
		case 0: return unsqueeze_impl<0, type>(t, x, axis);
		default: throw xhqm::error("ad unsqueeze 不支持该维度");
		}
	}

	// —— squeeze（dim→dim-1，删 size-1 维；反向 = unsqueeze 回原 dim）——
	template<xhqm::size dim, class type>
	value<type>* squeeze_impl(tape<type>& t, value<type>* x, xhqm::size axis)
	{
		auto* px = as_payload<dim, type>(x->holder());
		value<type>* out = make_value<dim - 1, type>(t, tensor_base::squeeze(px->value, axis), "squeeze");
		out->add_input(x);
		out->params()["axis"] = double(axis);
		out->backward_fn() = [x, out, axis]() {
			auto* px = as_payload<dim, type>(x->holder());
			auto* po = as_payload<dim - 1, type>(out->holder());
			px->grad = px->grad + tensor_base::unsqueeze(po->grad, axis);
		};
		return out;
	}
	template<class type>
	value<type>* squeeze(tape<type>& t, value<type>* x, xhqm::size axis)
	{
		switch (x->dim())
		{
		case 2: return squeeze_impl<2, type>(t, x, axis);
		case 1: return squeeze_impl<1, type>(t, x, axis);
		default: throw xhqm::error("ad squeeze 不支持该维度");
		}
	}

	// —— flatten（2D → 1D，行优先展平；反向 = reshape 回 2D）——
	template<class type>
	value<type>* flatten(tape<type>& t, value<type>* x)
	{
		auto* px = as_payload<2, type>(x->holder());
		const xhqm::size n = px->value.size();
		tensor<1, type> y(std::array<xhqm::size, 1>{ n }, type(0));
		for (xhqm::size i = 0; i < n; ++i) y[i] = px->value[i];
		value<type>* out = make_value<1, type>(t, y, "flatten");
		out->add_input(x);
		out->backward_fn() = [x, out]() {
			auto* px = as_payload<2, type>(x->holder());
			auto* po = as_payload<1, type>(out->holder());
			for (xhqm::size i = 0; i < po->grad.size(); ++i)
				px->grad[i] = xhqm::sum(px->grad[i], po->grad[i]);
		};
		return out;
	}

	// —— unflatten（1D → 2D；反向 = flatten）——
	template<class type>
	value<type>* unflatten(tape<type>& t, value<type>* x, xhqm::size rows, xhqm::size cols)
	{
		auto* px = as_payload<1, type>(x->holder());
		if (px->value.size() != rows * cols) throw xhqm::error("ad unflatten 尺寸不匹配");
		tensor<2, type> y(std::array<xhqm::size, 2>{ rows, cols }, type(0));
		for (xhqm::size i = 0; i < px->value.size(); ++i) y[i] = px->value[i];
		value<type>* out = make_value<2, type>(t, y, "unflatten");
		out->add_input(x);
		out->params()["rows"] = double(rows);
		out->params()["cols"] = double(cols);
		out->backward_fn() = [x, out]() {
			auto* px = as_payload<1, type>(x->holder());
			auto* po = as_payload<2, type>(out->holder());
			for (xhqm::size i = 0; i < px->grad.size(); ++i)
				px->grad[i] = xhqm::sum(px->grad[i], po->grad[i]);
		};
		return out;
	}

	// —— silu（Swish）：y = x·σ(x)；反向 σ(x)·(1 + x·(1-σ(x))) ——
	template<xhqm::size dim, class type>
	value<type>* silu_impl(tape<type>& t, value<type>* x)
	{
		auto* px = as_payload<dim, type>(x->holder());
		value<type>* out = make_value<dim, type>(t, soul::silu(px->value), "silu");
		out->add_input(x);
		out->backward_fn() = [x, out]() {
			auto* px = as_payload<dim, type>(x->holder());
			auto* po = as_payload<dim, type>(out->holder());
			for (xhqm::size i = 0; i < px->value.size(); ++i)
			{
				const type sig = type(1) / (type(1) + type(std::exp(-px->value[i])));
				const type d = xhqm::mul(sig, xhqm::sum(type(1), xhqm::mul(px->value[i], xhqm::sub(type(1), sig))));
				px->grad[i] = xhqm::sum(px->grad[i], xhqm::mul(po->grad[i], d));
			}
		};
		return out;
	}
	template<class type>
	value<type>* silu(tape<type>& t, value<type>* x)
	{
		switch (x->dim())
		{
		case 4: return silu_impl<4, type>(t, x);
		case 3: return silu_impl<3, type>(t, x);
		case 2: return silu_impl<2, type>(t, x);
		case 1: return silu_impl<1, type>(t, x);
		case 0: return silu_impl<0, type>(t, x);
		default: throw xhqm::error("ad silu 不支持该维度");
		}
	}

	// —— gelu（tanh 近似）：y = 0.5·x·(1+tanh(c·(x+a·x³)))，c=√(2/π), a=0.044715 ——
	template<xhqm::size dim, class type>
	value<type>* gelu_impl(tape<type>& t, value<type>* x)
	{
		auto* px = as_payload<dim, type>(x->holder());
		value<type>* out = make_value<dim, type>(t, soul::gelu(px->value), "gelu");
		out->add_input(x);
		out->backward_fn() = [x, out]() {
			auto* px = as_payload<dim, type>(x->holder());
			auto* po = as_payload<dim, type>(out->holder());
			const type c = type(0.7978845608028654), a = type(0.044715);
			for (xhqm::size i = 0; i < px->value.size(); ++i)
			{
				const type xv = px->value[i];
				const type u = xhqm::mul(c, xhqm::sum(xv, xhqm::mul(a, xhqm::mul(xhqm::mul(xv, xv), xv))));
				const type th = type(std::tanh(u));
				const type du = xhqm::mul(c, xhqm::sum(type(1), xhqm::mul(xhqm::mul(type(3), a), xhqm::mul(xv, xv))));
				const type d = xhqm::sum(xhqm::mul(type(0.5), xhqm::sum(type(1), th)),
					xhqm::mul(xhqm::mul(type(0.5), xv), xhqm::mul(xhqm::sub(type(1), xhqm::mul(th, th)), du)));
				px->grad[i] = xhqm::sum(px->grad[i], xhqm::mul(po->grad[i], d));
			}
		};
		return out;
	}
	template<class type>
	value<type>* gelu(tape<type>& t, value<type>* x)
	{
		switch (x->dim())
		{
		case 4: return gelu_impl<4, type>(t, x);
		case 3: return gelu_impl<3, type>(t, x);
		case 2: return gelu_impl<2, type>(t, x);
		case 1: return gelu_impl<1, type>(t, x);
		case 0: return gelu_impl<0, type>(t, x);
		default: throw xhqm::error("ad gelu 不支持该维度");
		}
	}

	// —— group_norm（NCHW，逐通道 affine）：前向走 tensor_base::group_norm，反向标准 LayerNorm 组内公式 ——
	template<class type>
	value<type>* group_norm(tape<type>& t, value<type>* x, value<type>* gamma, value<type>* beta,
		xhqm::size groups, type eps = type(1e-5))
	{
		auto* px = as_payload<4, type>(x->holder());
		auto* pg = as_payload<1, type>(gamma->holder());
		auto* pb = as_payload<1, type>(beta->holder());
		value<type>* out = make_value<4, type>(t, tensor_base::group_norm(px->value, pg->value, pb->value, groups, eps), "group_norm");
		out->add_input(x); out->add_input(gamma); out->add_input(beta);
		out->params()["groups"] = double(groups);
		out->params()["eps"] = double(eps);
		out->backward_fn() = [x, gamma, beta, out, groups, eps]() {
			auto* px = as_payload<4, type>(x->holder());
			auto* pg = as_payload<1, type>(gamma->holder());
			auto* pb = as_payload<1, type>(beta->holder());
			auto* po = as_payload<4, type>(out->holder());
			const xhqm::size N = px->value.dimension(0), C = px->value.dimension(1), H = px->value.dimension(2), W = px->value.dimension(3);
			const xhqm::size cg = C / groups, sp = H * W, gsz = cg * sp;
			for (xhqm::size n = 0; n < N; ++n)
			for (xhqm::size g = 0; g < groups; ++g)
			{
				const xhqm::size c0 = g * cg;
				type sum = type(0), sum2 = type(0);
				for (xhqm::size c = c0; c < c0 + cg; ++c)
					for (xhqm::size p = 0; p < sp; ++p)
					{
						const type v = px->value(n, c, p / W, p % W);
						sum = xhqm::sum(sum, v); sum2 = xhqm::sum(sum2, xhqm::mul(v, v));
					}
				const type mean = xhqm::div(sum, type(gsz));
				const type var = xhqm::sub(xhqm::div(sum2, type(gsz)), xhqm::mul(mean, mean));
				const type rstd = xhqm::div(type(1), type(std::sqrt(xhqm::sum(var, eps))));
				type dvar = type(0), dmean = type(0);
				for (xhqm::size c = c0; c < c0 + cg; ++c)
				{
					type dg = type(0), db = type(0);
					for (xhqm::size p = 0; p < sp; ++p)
					{
						const xhqm::size h = p / W, w = p % W;
						const type v = px->value(n, c, h, w);
						const type dy = po->grad(n, c, h, w);
						const type dxhat = xhqm::mul(dy, pg->value[c]);
						dg = xhqm::sum(dg, xhqm::mul(dy, xhqm::mul(xhqm::sub(v, mean), rstd)));
						db = xhqm::sum(db, dy);
						dvar = xhqm::sum(dvar, xhqm::mul(dxhat, xhqm::sub(v, mean)));
						dmean = xhqm::sum(dmean, dxhat);
					}
					pg->grad[c] = xhqm::sum(pg->grad[c], dg);
					pb->grad[c] = xhqm::sum(pb->grad[c], db);
				}
				dvar = xhqm::mul(dvar, xhqm::mul(type(-0.5), xhqm::mul(rstd, xhqm::mul(rstd, rstd))));
				dmean = xhqm::mul(dmean, xhqm::sub(type(0), rstd));
				const type dvar_scale = xhqm::div(xhqm::mul(type(2), dvar), type(gsz));
				const type dmean_scale = xhqm::div(dmean, type(gsz));
				for (xhqm::size c = c0; c < c0 + cg; ++c)
					for (xhqm::size p = 0; p < sp; ++p)
					{
						const xhqm::size h = p / W, w = p % W;
						const type v = px->value(n, c, h, w);
						const type dy = po->grad(n, c, h, w);
						const type dxhat = xhqm::mul(dy, pg->value[c]);
						const type dx = xhqm::sum(xhqm::sum(xhqm::mul(dxhat, rstd), xhqm::mul(dvar_scale, xhqm::sub(v, mean))), dmean_scale);
						px->grad(n, c, h, w) = xhqm::sum(px->grad(n, c, h, w), dx);
					}
			}
		};
		return out;
	}

	// —— conv2d（NCHW，含 bias；groups=1）：前向走 tensor_base::conv2d，反向直连 ——
	template<class type>
	value<type>* conv2d(tape<type>& t, value<type>* x, value<type>* w, value<type>* b,
		xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		auto* px = as_payload<4, type>(x->holder());
		auto* pw = as_payload<4, type>(w->holder());
		auto* pb = as_payload<1, type>(b->holder());
		const xhqm::size H = px->value.dimension(2), W = px->value.dimension(3);
		const xhqm::size kh = pw->value.dimension(2), kw = pw->value.dimension(3);
		value<type>* out = make_value<4, type>(t, tensor_base::conv2d(px->value, pw->value, pb->value, stride, padding, dilation), "conv2d");
		out->add_input(x); out->add_input(w); out->add_input(b);
		out->params()["stride"] = double(stride);
		out->params()["padding"] = double(padding);
		out->params()["dilation"] = double(dilation);
		out->backward_fn() = [x, w, b, out, stride, padding, dilation, H, W, kh, kw]() {
			auto* px = as_payload<4, type>(x->holder());
			auto* pw = as_payload<4, type>(w->holder());
			auto* pb = as_payload<1, type>(b->holder());
			auto* po = as_payload<4, type>(out->holder());
			const auto gb = tensor_base::conv2d_grad_bias<type>(po->grad);
			const auto gw = tensor_base::conv2d_grad_weight<type>(px->value, po->grad, kh, kw, stride, padding, dilation);
			const auto gx = tensor_base::conv2d_grad_input<type>(pw->value, po->grad, H, W, stride, padding, dilation);
			for (xhqm::size i = 0; i < pb->grad.size(); ++i) pb->grad[i] = xhqm::sum(pb->grad[i], gb[i]);
			for (xhqm::size i = 0; i < pw->grad.size(); ++i) pw->grad[i] = xhqm::sum(pw->grad[i], gw[i]);
			for (xhqm::size i = 0; i < px->grad.size(); ++i) px->grad[i] = xhqm::sum(px->grad[i], gx[i]);
		};
		return out;
	}

	// —— 二元交叉熵：loss = -mean(t·log(p) + (1-t)·log(1-p))，p 为概率(0,1)、t∈{0,1} ——
	template<class type>
	value<type>* bce(tape<type>& t, value<type>* pred, value<type>* target)
	{
		const xhqm::size n = pred->holder()->size();
		const type* pd = static_cast<const type*>(pred->holder()->data());
		const type* td = static_cast<const type*>(target->holder()->data());
		type s = type(0);
		for (xhqm::size i = 0; i < n; ++i)
		{
			type p = pd[i];
			if (p < type(1e-7)) p = type(1e-7); else if (p > type(1) - type(1e-7)) p = type(1) - type(1e-7);
			s = xhqm::sum(s, xhqm::sum(xhqm::mul(td[i], type(std::log(p))), xhqm::mul(xhqm::sub(type(1), td[i]), type(std::log(type(1) - p)))));
		}
		s = xhqm::div(xhqm::sub(type(0), s), type(n));
		value<type>* out = make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, s), "bce");
		out->add_input(pred); out->add_input(target);
		out->backward_fn() = [pred, target, out, n]() {
			const type g = static_cast<const type*>(out->holder()->gradient())[0];
			type* pg = static_cast<type*>(pred->holder()->gradient());
			type* tg = static_cast<type*>(target->holder()->gradient());
			const type* pd = static_cast<const type*>(pred->holder()->data());
			const type* td = static_cast<const type*>(target->holder()->data());
			const type c = xhqm::div(xhqm::sub(type(0), g), type(n));
			for (xhqm::size i = 0; i < n; ++i)
			{
				type p = pd[i];
				if (p < type(1e-7)) p = type(1e-7); else if (p > type(1) - type(1e-7)) p = type(1) - type(1e-7);
				pg[i] = xhqm::sum(pg[i], xhqm::mul(c, xhqm::sub(xhqm::div(td[i], p), xhqm::div(xhqm::sub(type(1), td[i]), xhqm::sub(type(1), p)))));
				tg[i] = xhqm::sum(tg[i], xhqm::mul(c, xhqm::sub(type(std::log(p)), type(std::log(type(1) - p)))));
			}
		};
		return out;
	}

	// —— 负对数似然：loss = -mean(log_prob[i, target[i]])，log_probs 已为对数概率 ——
	template<class type>
	value<type>* nll(tape<type>& t, value<type>* log_probs, const std::vector<xhqm::size>& target)
	{
		auto* px = as_payload<2, type>(log_probs->holder());
		const xhqm::size batch = px->value.dimension(0);
		type s = type(0);
		for (xhqm::size i = 0; i < batch; ++i) s = xhqm::sum(s, px->value(i, target[i]));
		s = xhqm::div(xhqm::sub(type(0), s), type(batch));
		value<type>* out = make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, s), "nll");
		out->add_input(log_probs);
		out->params()["batch"] = double(batch);
		out->backward_fn() = [log_probs, out, target, batch]() {
			const type g = static_cast<const type*>(out->holder()->gradient())[0];
			auto* px = as_payload<2, type>(log_probs->holder());
			const type c = xhqm::div(xhqm::sub(type(0), g), type(batch));
			for (xhqm::size i = 0; i < batch; ++i)
				px->grad(i, target[i]) = xhqm::sum(px->grad(i, target[i]), c);
		};
		return out;
	}

	// —— VAE 高斯 KL：KL(N(μ,σ)||N(0,1)) = -0.5·mean(1 + logvar - μ² - exp(logvar)) ——
	template<class type>
	value<type>* kl_div_gaussian(tape<type>& t, value<type>* mu, value<type>* logvar)
	{
		const xhqm::size n = mu->holder()->size();
		const type* md = static_cast<const type*>(mu->holder()->data());
		const type* vd = static_cast<const type*>(logvar->holder()->data());
		type s = type(0);
		for (xhqm::size i = 0; i < n; ++i)
		{
			const type term = type(1) + vd[i] - md[i] * md[i] - type(std::exp(vd[i]));
			s += term;
		}
		s = type(-0.5) * s / type(n);
		value<type>* out = make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, s), "kl_div_gaussian");
		out->add_input(mu); out->add_input(logvar);
		out->backward_fn() = [mu, logvar, out, n]() {
			const type g = static_cast<const type*>(out->holder()->gradient())[0];
			type* mg = static_cast<type*>(mu->holder()->gradient());
			type* vg = static_cast<type*>(logvar->holder()->gradient());
			const type* md = static_cast<const type*>(mu->holder()->data());
			const type* vd = static_cast<const type*>(logvar->holder()->data());
			const type c = xhqm::div(xhqm::mul(type(-0.5), g), type(n));
			for (xhqm::size i = 0; i < n; ++i)
			{
				mg[i] = xhqm::sum(mg[i], xhqm::mul(c, xhqm::mul(type(-2), md[i])));
				vg[i] = xhqm::sum(vg[i], xhqm::mul(c, xhqm::sub(type(1), type(std::exp(vd[i])))));
			}
		};
		return out;
	}

	// —— tanh：y = tanh(x)，反向 dy/dx = 1 - y² ——
	template<xhqm::size dim, class type>
	value<type>* tanh_impl(tape<type>& t, value<type>* x)
	{
		auto* px = as_payload<dim, type>(x->holder());
		value<type>* out = make_value<dim, type>(t, soul::tanh_act(px->value), "tanh");
		out->add_input(x);
		out->backward_fn() = [x, out]() {
			auto* px = as_payload<dim, type>(x->holder());
			auto* po = as_payload<dim, type>(out->holder());
			for (xhqm::size i = 0; i < px->value.size(); ++i)
				px->grad[i] = xhqm::sum(px->grad[i], xhqm::mul(po->grad[i], xhqm::sub(type(1), xhqm::mul(po->value[i], po->value[i]))));
		};
		return out;
	}
	template<class type>
	value<type>* tanh(tape<type>& t, value<type>* x)
	{
		switch (x->dim())
		{
		case 4: return tanh_impl<4, type>(t, x);
		case 3: return tanh_impl<3, type>(t, x);
		case 2: return tanh_impl<2, type>(t, x);
		case 1: return tanh_impl<1, type>(t, x);
		case 0: return tanh_impl<0, type>(t, x);
		default: throw xhqm::error("ad tanh 不支持该维度");
		}
	}

	// 非因果掩码（全零 = 无掩码，encoder 双向注意力用）
	template<class type>
	tensor<2, type> non_causal_mask(xhqm::size seq)
	{
		return tensor<2, type>(std::array<xhqm::size, 2>{ seq, seq }, type(0));
	}

	// —— conv1d（N,C,L，含 bias；groups=1）——
	template<class type>
	value<type>* conv1d(tape<type>& t, value<type>* x, value<type>* w, value<type>* b,
		xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		auto* px = as_payload<3, type>(x->holder());
		auto* pw = as_payload<3, type>(w->holder());
		auto* pb = as_payload<1, type>(b->holder());
		const xhqm::size L = px->value.dimension(2);
		const xhqm::size k = pw->value.dimension(2);
		value<type>* out = make_value<3, type>(t, tensor_base::conv1d(px->value, pw->value, pb->value, stride, padding, dilation), "conv1d");
		out->add_input(x); out->add_input(w); out->add_input(b);
		out->params()["stride"] = double(stride); out->params()["padding"] = double(padding); out->params()["dilation"] = double(dilation);
		out->backward_fn() = [x, w, b, out, stride, padding, dilation, L, k]() {
			auto* px = as_payload<3, type>(x->holder());
			auto* pw = as_payload<3, type>(w->holder());
			auto* pb = as_payload<1, type>(b->holder());
			auto* po = as_payload<3, type>(out->holder());
			const auto gb = tensor_base::conv1d_grad_bias<type>(po->grad);
			const auto gw = tensor_base::conv1d_grad_weight<type>(px->value, po->grad, k, stride, padding, dilation);
			const auto gx = tensor_base::conv1d_grad_input<type>(pw->value, po->grad, L, stride, padding, dilation);
			for (xhqm::size i = 0; i < pb->grad.size(); ++i) pb->grad[i] = xhqm::sum(pb->grad[i], gb[i]);
			for (xhqm::size i = 0; i < pw->grad.size(); ++i) pw->grad[i] = xhqm::sum(pw->grad[i], gw[i]);
			for (xhqm::size i = 0; i < px->grad.size(); ++i) px->grad[i] = xhqm::sum(px->grad[i], gx[i]);
		};
		return out;
	}

	// —— conv3d（N,C,D,H,W，含 bias；groups=1；stride/padding/dilation 三轴一致）——
	template<class type>
	value<type>* conv3d(tape<type>& t, value<type>* x, value<type>* w, value<type>* b,
		xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		auto* px = as_payload<5, type>(x->holder());
		auto* pw = as_payload<5, type>(w->holder());
		auto* pb = as_payload<1, type>(b->holder());
		const xhqm::size D = px->value.dimension(2), H = px->value.dimension(3), W = px->value.dimension(4);
		const xhqm::size kd = pw->value.dimension(2), kh = pw->value.dimension(3), kw = pw->value.dimension(4);
		value<type>* out = make_value<5, type>(t, tensor_base::conv3d(px->value, pw->value, pb->value, stride, padding, dilation), "conv3d");
		out->add_input(x); out->add_input(w); out->add_input(b);
		out->backward_fn() = [x, w, b, out, stride, padding, dilation, D, H, W, kd, kh, kw]() {
			auto* px = as_payload<5, type>(x->holder());
			auto* pw = as_payload<5, type>(w->holder());
			auto* pb = as_payload<1, type>(b->holder());
			auto* po = as_payload<5, type>(out->holder());
			const auto gb = tensor_base::conv3d_grad_bias<type>(po->grad);
			const auto gw = tensor_base::conv3d_grad_weight<type>(px->value, po->grad, kd, kh, kw, stride, stride, stride, padding, padding, padding, dilation, dilation, dilation);
			const auto gx = tensor_base::conv3d_grad_input<type>(pw->value, po->grad, D, H, W, stride, stride, stride, padding, padding, padding, dilation, dilation, dilation);
			for (xhqm::size i = 0; i < pb->grad.size(); ++i) pb->grad[i] = xhqm::sum(pb->grad[i], gb[i]);
			for (xhqm::size i = 0; i < pw->grad.size(); ++i) pw->grad[i] = xhqm::sum(pw->grad[i], gw[i]);
			for (xhqm::size i = 0; i < px->grad.size(); ++i) px->grad[i] = xhqm::sum(px->grad[i], gx[i]);
		};
		return out;
	}

	// —— batch_norm（NCHW，训练态用 batch 统计量；逐通道 affine）——
	template<class type>
	value<type>* batch_norm(tape<type>& t, value<type>* x, value<type>* gamma, value<type>* beta, type eps = type(1e-5))
	{
		auto* px = as_payload<4, type>(x->holder());
		auto* pg = as_payload<1, type>(gamma->holder());
		auto* pb = as_payload<1, type>(beta->holder());
		value<type>* out = make_value<4, type>(t, tensor_base::batch_norm(px->value, pg->value, pb->value, eps), "batch_norm");
		out->add_input(x); out->add_input(gamma); out->add_input(beta);
		out->params()["eps"] = double(eps);
		out->backward_fn() = [x, gamma, beta, out, eps]() {
			auto* px = as_payload<4, type>(x->holder());
			auto* pg = as_payload<1, type>(gamma->holder());
			auto* pb = as_payload<1, type>(beta->holder());
			auto* po = as_payload<4, type>(out->holder());
			const xhqm::size N = px->value.dimension(0), C = px->value.dimension(1), H = px->value.dimension(2), W = px->value.dimension(3);
			const xhqm::size M = N * H * W;
			for (xhqm::size c = 0; c < C; ++c)
			{
				type sum = type(0), sum2 = type(0);
				for (xhqm::size n = 0; n < N; ++n)
					for (xhqm::size p = 0; p < H * W; ++p)
					{
						const type v = px->value(n, c, p / W, p % W);
						sum = xhqm::sum(sum, v); sum2 = xhqm::sum(sum2, xhqm::mul(v, v));
					}
				const type mean = xhqm::div(sum, type(M));
				const type var = xhqm::sub(xhqm::div(sum2, type(M)), xhqm::mul(mean, mean));
				const type rstd = xhqm::div(type(1), type(std::sqrt(xhqm::sum(var, eps))));
				type dvar = type(0), dmean = type(0), dg = type(0), db = type(0);
				for (xhqm::size n = 0; n < N; ++n)
					for (xhqm::size p = 0; p < H * W; ++p)
					{
						const xhqm::size h = p / W, w = p % W;
						const type v = px->value(n, c, h, w);
						const type dy = po->grad(n, c, h, w);
						const type dxhat = xhqm::mul(dy, pg->value[c]);
						dg = xhqm::sum(dg, xhqm::mul(dy, xhqm::mul(xhqm::sub(v, mean), rstd)));
						db = xhqm::sum(db, dy);
						dvar = xhqm::sum(dvar, xhqm::mul(dxhat, xhqm::sub(v, mean)));
						dmean = xhqm::sum(dmean, dxhat);
					}
				pg->grad[c] = xhqm::sum(pg->grad[c], dg);
				pb->grad[c] = xhqm::sum(pb->grad[c], db);
				dvar = xhqm::mul(dvar, xhqm::mul(type(-0.5), xhqm::mul(rstd, xhqm::mul(rstd, rstd))));
				dmean = xhqm::mul(dmean, xhqm::sub(type(0), rstd));
				const type dvar_scale = xhqm::div(xhqm::mul(type(2), dvar), type(M));
				const type dmean_scale = xhqm::div(dmean, type(M));
				for (xhqm::size n = 0; n < N; ++n)
					for (xhqm::size p = 0; p < H * W; ++p)
					{
						const xhqm::size h = p / W, w = p % W;
						const type v = px->value(n, c, h, w);
						const type dy = po->grad(n, c, h, w);
						const type dxhat = xhqm::mul(dy, pg->value[c]);
						const type dx = xhqm::sum(xhqm::sum(xhqm::mul(dxhat, rstd), xhqm::mul(dvar_scale, xhqm::sub(v, mean))), dmean_scale);
						px->grad(n, c, h, w) = xhqm::sum(px->grad(n, c, h, w), dx);
					}
			}
		};
		return out;
	}

	// —— cross-attention：Q 来自 x，K/V 来自 y（encoder-decoder / 图文对齐）——
	template<class type>
	value<type>* cross_attention(tape<type>& t,
		value<type>* x, value<type>* y,
		value<type>* wq, value<type>* wk, value<type>* wv, value<type>* wo,
		const tensor<2, type>& mask, type scale)
	{
		auto* q = matmul(t, x, wq);
		auto* k = matmul(t, y, wk);
		auto* v = matmul(t, y, wv);
		auto* oh = sdpa(t, q, k, v, mask, scale);
		return matmul(t, oh, wo);
	}

	// —— conv_transpose2d（转置卷积，UNet 解码器上采样）：w 布局 (Cin,Cout,KH,KW) ——
	template<class type>
	value<type>* conv_transpose2d(tape<type>& t, value<type>* x, value<type>* w, value<type>* b,
		xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		auto* px = as_payload<4, type>(x->holder());
		auto* pw = as_payload<4, type>(w->holder());
		auto* pb = as_payload<1, type>(b->holder());
		const xhqm::size H = px->value.dimension(2), W = px->value.dimension(3);
		const xhqm::size KH = pw->value.dimension(2), KW = pw->value.dimension(3);
		value<type>* out = make_value<4, type>(t, tensor_base::conv_transpose2d(px->value, pw->value, pb->value, stride, padding, dilation), "conv_transpose2d");
		out->add_input(x); out->add_input(w); out->add_input(b);
		out->backward_fn() = [x, w, b, out, stride, padding, dilation, H, W, KH, KW]() {
			auto* px = as_payload<4, type>(x->holder());
			auto* pw = as_payload<4, type>(w->holder());
			auto* pb = as_payload<1, type>(b->holder());
			auto* po = as_payload<4, type>(out->holder());
			const auto gb = tensor_base::conv_transpose2d_grad_bias<type>(po->grad);
			const auto gw = tensor_base::conv_transpose2d_grad_weight<type>(px->value, po->grad, KH, KW, stride, padding, dilation);
			const auto gx = tensor_base::conv_transpose2d_grad_input<type>(pw->value, po->grad, H, W, stride, padding, dilation);
			for (xhqm::size i = 0; i < pb->grad.size(); ++i) pb->grad[i] = xhqm::sum(pb->grad[i], gb[i]);
			for (xhqm::size i = 0; i < pw->grad.size(); ++i) pw->grad[i] = xhqm::sum(pw->grad[i], gw[i]);
			for (xhqm::size i = 0; i < px->grad.size(); ++i) px->grad[i] = xhqm::sum(px->grad[i], gx[i]);
		};
		return out;
	}

	// —— grouped/depthwise conv2d：w 布局 (Cout, Cin/groups, KH, KW) ——
	template<class type>
	value<type>* conv2d_grouped(tape<type>& t, value<type>* x, value<type>* w, value<type>* b,
		xhqm::size stride, xhqm::size padding, xhqm::size dilation, xhqm::size groups)
	{
		auto* px = as_payload<4, type>(x->holder());
		auto* pw = as_payload<4, type>(w->holder());
		auto* pb = as_payload<1, type>(b->holder());
		const xhqm::size H = px->value.dimension(2), W = px->value.dimension(3);
		const xhqm::size kh = pw->value.dimension(2), kw = pw->value.dimension(3);
		value<type>* out = make_value<4, type>(t, tensor_base::conv2d(px->value, pw->value, pb->value, stride, padding, dilation, groups), "conv2d_grouped");
		out->add_input(x); out->add_input(w); out->add_input(b);
		out->backward_fn() = [x, w, b, out, stride, padding, dilation, groups, H, W, kh, kw]() {
			auto* px = as_payload<4, type>(x->holder());
			auto* pw = as_payload<4, type>(w->holder());
			auto* pb = as_payload<1, type>(b->holder());
			auto* po = as_payload<4, type>(out->holder());
			const auto gb = tensor_base::conv2d_grad_bias<type>(po->grad);
			const auto gw = tensor_base::conv2d_grad_weight_groups<type>(px->value, po->grad, kh, kw, stride, padding, dilation, groups);
			const auto gx = tensor_base::conv2d_grad_input_groups<type>(pw->value, po->grad, H, W, stride, padding, dilation, groups);
			for (xhqm::size i = 0; i < pb->grad.size(); ++i) pb->grad[i] = xhqm::sum(pb->grad[i], gb[i]);
			for (xhqm::size i = 0; i < pw->grad.size(); ++i) pw->grad[i] = xhqm::sum(pw->grad[i], gw[i]);
			for (xhqm::size i = 0; i < px->grad.size(); ++i) px->grad[i] = xhqm::sum(px->grad[i], gx[i]);
		};
		return out;
	}

	// —— hinge 损失：mean(max(0, 1 - pred·target))，target ∈ {-1,+1}（SVM 间隔）——
	template<class type>
	value<type>* hinge_loss(tape<type>& t, value<type>* pred, value<type>* target)
	{
		const xhqm::size n = pred->holder()->size();
		const type* pd = static_cast<const type*>(pred->holder()->data());
		const type* td = static_cast<const type*>(target->holder()->data());
		tensor<1, type> loss(std::array<xhqm::size, 1>{ n }, type(0));
		tensor_base::tensor_backend_active().parallel_for(0, n, [&](xhqm::size i) {
			const type m = type(1) - pd[i] * td[i];
			loss[i] = (m > type(0)) ? m : type(0);
		});
		const type s = tensor_base::reduce_sum(loss, 0)[0] / type(n);
		value<type>* out = make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, s), "hinge");
		out->add_input(pred); out->add_input(target);
		out->backward_fn() = [pred, target, out, n]() {
			const type g = static_cast<const type*>(out->holder()->gradient())[0];
			type* pg = static_cast<type*>(pred->holder()->gradient());
			type* tg = static_cast<type*>(target->holder()->gradient());
			const type* pd = static_cast<const type*>(pred->holder()->data());
			const type* td = static_cast<const type*>(target->holder()->data());
			const type c = g / type(n);
			tensor_base::tensor_backend_active().parallel_for(0, n, [&](xhqm::size i) {
				if (type(1) - pd[i] * td[i] > type(0))
				{
					pg[i] = xhqm::sum(pg[i], xhqm::mul(c, xhqm::sub(type(0), td[i])));
					tg[i] = xhqm::sum(tg[i], xhqm::mul(c, xhqm::sub(type(0), pd[i])));
				}
			});
		};
		return out;
	}

	// —— focal 二元交叉熵：mean(-t·(1-p)^γ·log(p) - (1-t)·p^γ·log(1-p))，γ 调节难样本权重 ——
	template<class type>
	value<type>* focal_bce(tape<type>& t, value<type>* pred, value<type>* target, type gamma)
	{
		const xhqm::size n = pred->holder()->size();
		const type* pd = static_cast<const type*>(pred->holder()->data());
		const type* td = static_cast<const type*>(target->holder()->data());
		tensor<1, type> loss(std::array<xhqm::size, 1>{ n }, type(0));
		tensor_base::tensor_backend_active().parallel_for(0, n, [&](xhqm::size i) {
			type p = pd[i];
			if (p < type(1e-7)) p = type(1e-7); else if (p > type(1) - type(1e-7)) p = type(1) - type(1e-7);
			loss[i] = td[i] * type(std::pow(double(type(1) - p), double(gamma))) * type(std::log(double(p)))
				+ (type(1) - td[i]) * type(std::pow(double(p), double(gamma))) * type(std::log(double(type(1) - p)));
		});
		const type s = xhqm::div(xhqm::sub(type(0), tensor_base::reduce_sum(loss, 0)[0]), type(n));
		value<type>* out = make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, s), "focal_bce");
		out->add_input(pred); out->add_input(target);
		out->params()["gamma"] = double(gamma);
		out->backward_fn() = [pred, target, out, n, gamma]() {
			const type g = static_cast<const type*>(out->holder()->gradient())[0];
			type* pg = static_cast<type*>(pred->holder()->gradient());
			const type* pd = static_cast<const type*>(pred->holder()->data());
			const type* td = static_cast<const type*>(target->holder()->data());
			const type c = xhqm::div(xhqm::sub(type(0), g), type(n));
			tensor_base::tensor_backend_active().parallel_for(0, n, [&](xhqm::size i) {
				type p = pd[i];
				if (p < type(1e-7)) p = type(1e-7); else if (p > type(1) - type(1e-7)) p = type(1) - type(1e-7);
				const type t_ = td[i], q = type(1) - p;
				// dL/dp = -[t·((1-p)^γ/p - γ(1-p)^(γ-1)·log p) + (1-t)·(γ p^(γ-1)·log(1-p) - p^γ/(1-p))]
				const type d1 = type(std::pow(double(q), double(gamma))) / p - gamma * type(std::pow(double(q), double(gamma - type(1)))) * type(std::log(double(p)));
				const type d0 = gamma * type(std::pow(double(p), double(gamma - type(1)))) * type(std::log(double(q))) - type(std::pow(double(p), double(gamma))) / q;
				const type d = t_ * d1 + (type(1) - t_) * d0;
				pg[i] = xhqm::sum(pg[i], xhqm::mul(c, d));
			});
		};
		return out;
	}

	// —— InfoNCE 对比损失：S = q·k^T / τ，正样本为对角线，softmax 交叉熵 ——
	// 组合 matmul + transpose + mul_scalar + softmax_cross_entropy，反向经已有算子自动传播
	template<class type>
	value<type>* info_nce(tape<type>& t, value<type>* q, value<type>* k, type tau)
	{
		const xhqm::size n = q->shape()[0];
		auto* kt = transpose(t, k);
		auto* S = matmul(t, q, kt);  // (N, N) 相似度矩阵
		auto* inv_tau = make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, type(1) / tau));
		auto* Ss = mul_scalar(t, S, inv_tau);
		std::vector<xhqm::size> target(n);
		for (xhqm::size i = 0; i < n; ++i) target[i] = i;
		return softmax_cross_entropy(t, Ss, target);
	}

}
}
}

#endif
