#include "tensor_backend_no.h"

namespace xhqm
{
	namespace tensor_base
	{
		namespace
		{
			// no 后端：极简串行桩，只负责 parallel_for 串行执行 + 内存 no-op。
			// 全部算子（elemwise/matmul/reduce/unary/transpose/grad_accum/spmv）一律继承基类默认 false，
			// 串行标量语义统一走上层模板的 xhqm:: 泛型回退（matrix/vector/tensor_base/linalg/analysis/activation），
			// 只有一套实现，避免与 no 后端重复。
			class no_backend : public tensor_backend
			{
			public:
				const char* name() const noexcept override { return "no"; }
				device kind() const noexcept override { return device::no; }
				bool supports_memory_transfer() const noexcept override { return false; }

				void parallel_for(std::size_t begin, std::size_t end,
					const std::function<void(std::size_t)>& body) override
				{
					for (std::size_t i = begin; i < end; ++i)
						body(i);
				}

				void upload(void*, std::size_t) override {}
				void download(void*, std::size_t) override {}
				void synchronize() override {}
			};
		}

		tensor_backend* create_no_backend()
		{
			static no_backend instance;
			return &instance;
		}
	}
}
