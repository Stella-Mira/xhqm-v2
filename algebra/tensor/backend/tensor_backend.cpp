#include "tensor_backend.h"
#include "tensor_backend_no.h"
#if defined(TENSOR_WITH_CPU)
#include "tensor_backend_cpu.h"
#endif
#if defined(TENSOR_WITH_GPU)
#include "tensor_backend_gpu.h"
#endif
#if defined(XHQM_TENSOR_METAL)
#include "tensor_backend_metal.h"
#endif

namespace xhqm
{
	namespace tensor_base
	{
		namespace
		{
			tensor_backend*& active_ptr()
			{
				static tensor_backend* active = nullptr;
				return active;
			}
		}

		// 按设备类别创建后端：auto 解析为 gpu→cpu→no；gpu 无 CUDA 时回退 cpu；net 占位回退 cpu
		static tensor_backend* create_backend(device kind)
		{
			if (kind == device::auto_)
				kind = xhqm::hardware::resolve_auto();
			switch (kind)
			{
			case device::gpu:
#if defined(XHQM_TENSOR_METAL)
				// Apple Silicon：Metal/MPS GPU 后端；无 Metal 设备则回退下方
				if (tensor_backend* m = create_metal_backend()) return m;
#endif
#if defined(XHQM_TENSOR_CUDA)
				return create_gpu_backend();
#elif defined(TENSOR_WITH_CPU)
				return create_cpu_backend();	// GPU 桩（无 CUDA），回退 CPU
#else
				return create_no_backend();
#endif
			case device::cpu:
#if defined(TENSOR_WITH_CPU)
				return create_cpu_backend();
#else
				return create_no_backend();
#endif
			case device::net:
#if defined(TENSOR_WITH_CPU)
				return create_cpu_backend();	// 分布式占位，回退 CPU
#else
				return create_no_backend();
#endif
			case device::no:
			default:
				return create_no_backend();
			}
		}

		tensor_backend& tensor_backend_active()
		{
			if (!active_ptr())
				active_ptr() = create_backend(xhqm::hardware::active_device());
			return *active_ptr();
		}

		void tensor_backend_select(device kind)
		{
			tensor_backend* target = create_backend(kind);
			if (target)
				active_ptr() = target;
		}
	}
}
