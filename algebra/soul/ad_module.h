#ifndef XHQM_SOUL_AD_MODULE
#define XHQM_SOUL_AD_MODULE

// 可训练模块 + 优化器 + GPT 训练闭环。
// module 持有命名参数（shared payload：value 跨 epoch 持久，grad 由 backward 填充）；
// forward 时把参数 payload 注册进 tape（共享不拷贝），backward 后优化器按名更新 value。
#include "ad_io.h"
#include "../tensor/tensor.h"
#include "random.h"
#include <map>
#include <string>
#include <vector>

namespace xhqm {
namespace soul {
namespace ad {

	// —— 可训练模块：命名参数容器 ——
	template<class type = f32>
	struct module
	{
		struct param
		{
			std::string name;
			std::shared_ptr<tensor_holder> payload;  // value + grad（跨 epoch 持久）
			value<type>* node = nullptr;             // 本轮在 tape 里注册的节点（forward 后有效）
		};
		std::vector<param> params;

		// 新增命名参数（值初始化）
		template<xhqm::size dim>
		param& add_param(const std::string& name, const tensor<dim, type>& init)
		{
			params.push_back(param{ name, std::make_shared<tensor_payload<dim, type>>(init), nullptr });
			return params.back();
		}

		// 前向开始时把全部参数注册进 tape（共享 payload，不拷贝）
		void register_params(tape<type>& t)
		{
			for (auto& p : params)
				p.node = make_value<type>(t, p.payload, "leaf", p.name);
		}

		value<type>* node(const std::string& name) const
		{
			for (auto& p : params) if (p.name == name) return p.node;
			return nullptr;
		}
		tensor_holder* payload(const std::string& name) const
		{
			for (auto& p : params) if (p.name == name) return p.payload.get();
			return nullptr;
		}

		// 参数快照（state_dict）：名字 + payload 值（梯度不持久化）
		nlohmann::json save_json() const
		{
			nlohmann::json arr = nlohmann::json::array();
			for (auto& p : params)
			{
				nlohmann::json o;
				o["name"] = p.name;
				o["payload"] = p.payload->save_json();
				arr.push_back(std::move(o));
			}
			return arr;
		}
		void read_json(const nlohmann::json& j)
		{
			params.clear();
			for (const auto& o : j)
			{
				param p;
				p.name = o.at("name").get<std::string>();
				p.payload = make_payload<type>(o.at("payload"));
				params.push_back(std::move(p));
			}
		}
	};

	// —— 优化器（作用于 module，按名更新参数 payload 的 value）——
	template<class type = f32>
	struct sgd_optimizer
	{
		type lr;
		explicit sgd_optimizer(type lr_ = type(1e-2)) : lr(lr_) {}
		void step(module<type>& mod);
	};

	template<class type = f32>
	struct adam_optimizer
	{
		type lr, b1, b2, eps;
		xhqm::size t = 0;
		struct slot { std::shared_ptr<tensor_holder> m, v; };
		std::map<std::string, slot> slots;

		adam_optimizer(type lr_ = type(1e-3), type b1_ = type(0.9), type b2_ = type(0.999), type eps_ = type(1e-8))
			: lr(lr_), b1(b1_), b2(b2_), eps(eps_) {}
		void step(module<type>& mod);
	};

	// —— 内部：按运行时 dim 分派的单参数更新 ——
	namespace detail
	{
		template<class type>
		std::shared_ptr<tensor_holder> zero_like(const tensor_holder* src)
		{
			std::vector<xhqm::size> shp = src->shape();
			switch (src->dim())
			{
			case 0: return std::make_shared<tensor_payload<0, type>>(tensor<0, type>(std::array<xhqm::size, 0>{}, type(0)));
			case 1: return std::make_shared<tensor_payload<1, type>>(tensor<1, type>(std::array<xhqm::size, 1>{ shp[0] }, type(0)));
			case 2: return std::make_shared<tensor_payload<2, type>>(tensor<2, type>(std::array<xhqm::size, 2>{ shp[0], shp[1] }, type(0)));
			case 3: return std::make_shared<tensor_payload<3, type>>(tensor<3, type>(std::array<xhqm::size, 3>{ shp[0], shp[1], shp[2] }, type(0)));
			case 4: return std::make_shared<tensor_payload<4, type>>(tensor<4, type>(std::array<xhqm::size, 4>{ shp[0], shp[1], shp[2], shp[3] }, type(0)));
			default: throw xhqm::error("zero_like 不支持该维度");
			}
		}
		template<class type>
		void sgd_apply(tensor_holder* p, type lr)
		{
			switch (p->dim())
			{
			case 2: { auto* q = as_payload<2, type>(p); q->value = q->value - q->grad * lr; break; }
			case 1: { auto* q = as_payload<1, type>(p); q->value = q->value - q->grad * lr; break; }
			case 0: { auto* q = as_payload<0, type>(p); q->value = q->value - q->grad * lr; break; }
			case 3: { auto* q = as_payload<3, type>(p); q->value = q->value - q->grad * lr; break; }
			case 4: { auto* q = as_payload<4, type>(p); q->value = q->value - q->grad * lr; break; }
			default: throw xhqm::error("sgd_apply 不支持该维度");
			}
		}
		template<class type>
		void adam_apply(tensor_holder* p, tensor_holder* m, tensor_holder* v, type lr, xhqm::size t, type b1, type b2, type eps)
		{
			switch (p->dim())
			{
			case 2: { auto* q = as_payload<2, type>(p); auto* qm = as_payload<2, type>(m); auto* qv = as_payload<2, type>(v);
				adam_step<2, type>(q->value, q->grad, lr, t, qm->value, qv->value, b1, b2, eps); break; }
			case 1: { auto* q = as_payload<1, type>(p); auto* qm = as_payload<1, type>(m); auto* qv = as_payload<1, type>(v);
				adam_step<1, type>(q->value, q->grad, lr, t, qm->value, qv->value, b1, b2, eps); break; }
			case 0: { auto* q = as_payload<0, type>(p); auto* qm = as_payload<0, type>(m); auto* qv = as_payload<0, type>(v);
				adam_step<0, type>(q->value, q->grad, lr, t, qm->value, qv->value, b1, b2, eps); break; }
			case 3: { auto* q = as_payload<3, type>(p); auto* qm = as_payload<3, type>(m); auto* qv = as_payload<3, type>(v);
				adam_step<3, type>(q->value, q->grad, lr, t, qm->value, qv->value, b1, b2, eps); break; }
			case 4: { auto* q = as_payload<4, type>(p); auto* qm = as_payload<4, type>(m); auto* qv = as_payload<4, type>(v);
				adam_step<4, type>(q->value, q->grad, lr, t, qm->value, qv->value, b1, b2, eps); break; }
			default: throw xhqm::error("adam_apply 不支持该维度");
			}
		}
		// 解耦权重衰减：param *= (1 - coeff)，coeff = lr·wd（AdamW）
		template<class type>
		void decay_apply(tensor_holder* p, type coeff)
		{
			switch (p->dim())
			{
			case 2: { auto* q = as_payload<2, type>(p); q->value = q->value * (type(1) - coeff); break; }
			case 1: { auto* q = as_payload<1, type>(p); q->value = q->value * (type(1) - coeff); break; }
			case 0: { auto* q = as_payload<0, type>(p); q->value = q->value * (type(1) - coeff); break; }
			case 3: { auto* q = as_payload<3, type>(p); q->value = q->value * (type(1) - coeff); break; }
			case 4: { auto* q = as_payload<4, type>(p); q->value = q->value * (type(1) - coeff); break; }
			default: throw xhqm::error("decay_apply 不支持该维度");
			}
		}
	}

	template<class type>
	void sgd_optimizer<type>::step(module<type>& mod)
	{
		for (auto& p : mod.params)
			detail::sgd_apply<type>(p.payload.get(), lr);
	}

	template<class type>
	void adam_optimizer<type>::step(module<type>& mod)
	{
		++t;
		for (auto& p : mod.params)
		{
			slot& s = slots[p.name];
			if (!s.m) { s.m = detail::zero_like<type>(p.payload.get()); s.v = detail::zero_like<type>(p.payload.get()); }
			detail::adam_apply<type>(p.payload.get(), s.m.get(), s.v.get(), lr, t, b1, b2, eps);
		}
	}

	// —— AdamW（解耦权重衰减）：Adam 更新后 param *= (1 - lr·wd) ——
	template<class type = f32>
	struct adamw_optimizer
	{
		type lr, wd, b1, b2, eps;
		xhqm::size t = 0;
		struct slot { std::shared_ptr<tensor_holder> m, v; };
		std::map<std::string, slot> slots;

		adamw_optimizer(type lr_ = type(1e-3), type wd_ = type(1e-2), type b1_ = type(0.9), type b2_ = type(0.999), type eps_ = type(1e-8))
			: lr(lr_), wd(wd_), b1(b1_), b2(b2_), eps(eps_) {}
		void step(module<type>& mod);
	};
	template<class type>
	void adamw_optimizer<type>::step(module<type>& mod)
	{
		++t;
		for (auto& p : mod.params)
		{
			slot& s = slots[p.name];
			if (!s.m) { s.m = detail::zero_like<type>(p.payload.get()); s.v = detail::zero_like<type>(p.payload.get()); }
			detail::adam_apply<type>(p.payload.get(), s.m.get(), s.v.get(), lr, t, b1, b2, eps);
			detail::decay_apply<type>(p.payload.get(), lr * wd);
		}
	}

	// —— cosine + warmup 学习率调度：step < warmup 线性上升，之后 cosine 衰减 ——
	inline f32 cosine_warmup_lr(xhqm::size step, f32 base_lr, xhqm::size warmup, xhqm::size total)
	{
		if (step < warmup) return base_lr * float(step + 1) / float(warmup > 0 ? warmup : 1);
		const float progress = (total <= warmup) ? 0.0f : float(step - warmup) / float(total - warmup);
		return base_lr * 0.5f * (1.0f + std::cos(3.14159265358979323846f * progress));
	}

	// —— 全局梯度范数裁剪：||g|| > max_norm 时整体缩放 ——
	template<class type>
	void clip_grad_norm(module<type>& mod, type max_norm)
	{
		type total = type(0);
		for (auto& p : mod.params)
			for (xhqm::size i = 0; i < p.payload->size(); ++i)
			{
				const type g = static_cast<const type*>(p.payload->gradient())[i];
				total = xhqm::sum(total, xhqm::mul(g, g));
			}
		const type norm = type(std::sqrt(double(total)));
		if (norm > max_norm && norm > type(0))
		{
			const type scale = xhqm::div(max_norm, norm);
			for (auto& p : mod.params)
				for (xhqm::size i = 0; i < p.payload->size(); ++i)
					static_cast<type*>(p.payload->gradient())[i] = xhqm::mul(static_cast<type*>(p.payload->gradient())[i], scale);
		}
	}

	// —— GPT 解码器（可训练模块）：embedding + 多头注意力 + layer_norm + FFN ——
	// 参数名：wte/wq/wk/wv/wo/w1/w2/wlm/g/b；forward 构图并返回 softmax 交叉熵 loss。
	template<class type = f32>
	struct gpt : module<type>
	{
		xhqm::size vocab = 0, hidden = 0, ffn = 0, heads = 1;

		void init(xhqm::size vocab_, xhqm::size hidden_, xhqm::size ffn_, xhqm::size heads_, uint32_t& seed)
		{
			vocab = vocab_; hidden = hidden_; ffn = ffn_; heads = heads_;
			if (hidden % heads != 0) throw xhqm::error("gpt hidden 不能被 heads 整除");
			this->params.clear();

			tensor<2, type> wte(std::array<xhqm::size, 2>{ vocab, hidden }, type(0)); fill_normal(wte, type(0), type(0.02), seed); this->add_param("wte", wte);
			tensor<2, type> wq(std::array<xhqm::size, 2>{ hidden, hidden }, type(0)); xavier_uniform(wq, seed); this->add_param("wq", wq);
			tensor<2, type> wk(std::array<xhqm::size, 2>{ hidden, hidden }, type(0)); xavier_uniform(wk, seed); this->add_param("wk", wk);
			tensor<2, type> wv(std::array<xhqm::size, 2>{ hidden, hidden }, type(0)); xavier_uniform(wv, seed); this->add_param("wv", wv);
			tensor<2, type> wo(std::array<xhqm::size, 2>{ hidden, hidden }, type(0)); xavier_uniform(wo, seed); this->add_param("wo", wo);
			tensor<2, type> w1(std::array<xhqm::size, 2>{ hidden, ffn }, type(0)); xavier_uniform(w1, seed); this->add_param("w1", w1);
			tensor<2, type> w2(std::array<xhqm::size, 2>{ ffn, hidden }, type(0)); xavier_uniform(w2, seed); this->add_param("w2", w2);
			tensor<2, type> wlm(std::array<xhqm::size, 2>{ hidden, vocab }, type(0)); xavier_uniform(wlm, seed); this->add_param("wlm", wlm);
			tensor<1, type> g(std::array<xhqm::size, 1>{ hidden }, type(1)); this->add_param("g", g);
			tensor<1, type> b(std::array<xhqm::size, 1>{ hidden }, type(0)); this->add_param("b", b);
		}

		// 前向构图：把参数注册进 tape，返回 softmax 交叉熵 loss
		value<type>* forward(tape<type>& t, const std::vector<xhqm::size>& input, const std::vector<xhqm::size>& target)
		{
			this->register_params(t);
			auto* wte = this->node("wte");
			auto* wq = this->node("wq");
			auto* wk = this->node("wk");
			auto* wv = this->node("wv");
			auto* wo = this->node("wo");
			auto* w1 = this->node("w1");
			auto* w2 = this->node("w2");
			auto* wlm = this->node("wlm");
			auto* g = this->node("g");
			auto* b = this->node("b");

			auto* x = embedding_lookup(t, wte, input);
			const type scale = type(1) / type(std::sqrt(type(hidden / heads)));
			auto* a = multi_head_attention(t, x, wq, wk, wv, wo, heads, causal_mask<type>(input.size()), scale);
			auto* h = add(t, x, a);
			h = layer_norm(t, h, g, b);
			auto* f = matmul(t, relu(t, matmul(t, h, w1)), w2);
			h = add(t, h, f);
			auto* logits = matmul(t, h, wlm);
			return softmax_cross_entropy(t, logits, target);
		}
	};

}
}
}

#endif
