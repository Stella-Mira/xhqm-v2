// hardware 测试：设备选择（header-only）
#include "../hardware/hardware.h"
#include "test.h"
#include <cstring>

using namespace xhqm;

TEST_CASE("test_device")
{
	// 设备名
	CHECK(std::strcmp(hardware::device_name(hardware::device::no), "no") == 0);
	CHECK(std::strcmp(hardware::device_name(hardware::device::cpu), "cpu") == 0);
	CHECK(std::strcmp(hardware::device_name(hardware::device::gpu), "gpu") == 0);
	CHECK(std::strcmp(hardware::device_name(hardware::device::auto_), "auto") == 0);

	// no 设备始终已编译
	CHECK(hardware::device_compiled(hardware::device::no));

	// 编译期设备与运行时激活默认一致
	CHECK(hardware::active_device() == hardware::compiled_device());

	// 设置后生效
	hardware::active_device(hardware::device::cpu);
	CHECK(hardware::active_device() == hardware::device::cpu);
	hardware::active_device(hardware::compiled_device());  // 恢复
}
