cmake_minimum_required(VERSION 3.17)
# 单配置生成器(Ninja)不指定会退回 Debug(/MDd)，与 xhqm.dll 的 Release(/MD) 不匹配，
# std::string/map/vector 跨 DLL 边界会 ABI 错乱（崩溃/网络失败）；须在 project() 之前固定为 Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
project(xhqm_algebra_test CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()
add_definitions(-DXHQM_PROGRAM)
include_directories("..")

add_executable(algebra_test
    algebra_test.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/geometry/geometry.cpp
    ../algebra/tensor/backend/tensor_backend.cpp
    ../algebra/tensor/backend/tensor_backend_no.cpp
    ../network/uuid.cpp
    ../network/md5.cpp
)

add_executable(tensor_serial_test
    tensor_serial_test.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/tensor/backend/tensor_backend.cpp
    ../algebra/tensor/backend/tensor_backend_no.cpp
)

# CPU 后端（含 SIMD）测试：单独定义 TENSOR_WITH_CPU/SIMD 并开 /arch:AVX2，
# 验证 blas→simd→多线程→串行 的调度链在 CPU 上真实命中 SIMD 层。
add_executable(tensor_cpu_test
    tensor_cpu_test.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/tensor/backend/tensor_backend.cpp
    ../algebra/tensor/backend/tensor_backend_no.cpp
    ../algebra/tensor/backend/tensor_backend_cpu.cpp
)
if(MSVC)
    target_compile_definitions(tensor_cpu_test PRIVATE XHQM_DEVICE_CPU TENSOR_WITH_CPU TENSOR_WITH_SIMD TENSOR_WITH_BLAS)
    # BLAS（scipy-openblas32）：与主库同一套依赖，让 conv/trsv/chol 的 im2col+GEMM/BLAS 路径在运行时真正命中
    set(OPENBLAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../depend/openblas")
    target_include_directories(tensor_cpu_test PRIVATE "${OPENBLAS_ROOT}/include")
    target_link_libraries(tensor_cpu_test "${OPENBLAS_ROOT}/lib/libscipy_openblas.lib")
    target_compile_options(tensor_cpu_test PRIVATE /arch:AVX2)
else()
    # Linux/mac：无 BLAS/SVML（scipy-openblas 与 _mm256_atan2_ps 等内建均不可用），
    # 走标量/多线程路径验证（BLAS/SIMD 路径由 Windows 侧覆盖）
    target_compile_definitions(tensor_cpu_test PRIVATE XHQM_DEVICE_CPU TENSOR_WITH_CPU)
endif()

# GPU 卷积冒烟测试：链接预编译的 xhqm.dll（含 CUDA 后端），运行时选 GPU 后端验证卷积。
# 前置：先以 XHQM_BUILD_TENSOR=ON + XHQM_DEVICE=AUTO 构建 xhqm 主库产出 xhqm.dll。
# 本 target 只编译头文件模板 + 链接 DLL，须用 dllimport（XHQM_DYNAMIC 且去掉全局的 XHQM_PROGRAM）。
# （Windows/MSVC 专属：/U 选项与 xhqm.lib 在 Linux 无意义，gcc 会把 /UXHQM_PROGRAM 当链接输入报错）
if(MSVC)
    add_executable(gpu_smoke
        gpu_smoke.cpp
    )
    target_compile_definitions(gpu_smoke PRIVATE XHQM_DYNAMIC)
    target_compile_options(gpu_smoke PRIVATE /UXHQM_PROGRAM)
    target_link_libraries(gpu_smoke "${CMAKE_CURRENT_SOURCE_DIR}/../../binary/xhqm.lib")
endif()

add_executable(algebra_extended_test
    algebra_extended_test.cpp
    ../algebra/analysis/analysis.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/tensor/backend/tensor_backend.cpp
    ../algebra/tensor/backend/tensor_backend_no.cpp
)

add_executable(soul_test
    soul_test.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/tensor/backend/tensor_backend.cpp
    ../algebra/tensor/backend/tensor_backend_no.cpp
)

add_executable(soul_graph_test
    soul_graph_test.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/tensor/backend/tensor_backend.cpp
    ../algebra/tensor/backend/tensor_backend_no.cpp
)

add_executable(soul_ad_test
    soul_ad_test.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/tensor/backend/tensor_backend.cpp
    ../algebra/tensor/backend/tensor_backend_no.cpp
    ../network/uuid.cpp
    ../network/md5.cpp
)

add_executable(soul_diffusion_test
    soul_diffusion_test.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/tensor/backend/tensor_backend.cpp
    ../algebra/tensor/backend/tensor_backend_no.cpp
    ../network/uuid.cpp
    ../network/md5.cpp
)

add_executable(soul_diffusion_image_test
    soul_diffusion_image_test.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/tensor/backend/tensor_backend.cpp
    ../algebra/tensor/backend/tensor_backend_no.cpp
    ../algebra/tensor/backend/tensor_backend_cpu.cpp
    ../network/uuid.cpp
    ../network/md5.cpp
)
if(MSVC)
    target_compile_definitions(soul_diffusion_image_test PRIVATE XHQM_DEVICE_CPU TENSOR_WITH_CPU TENSOR_WITH_SIMD TENSOR_WITH_BLAS)
    target_include_directories(soul_diffusion_image_test PRIVATE "${OPENBLAS_ROOT}/include")
    target_link_libraries(soul_diffusion_image_test "${OPENBLAS_ROOT}/lib/libscipy_openblas.lib")
    target_compile_options(soul_diffusion_image_test PRIVATE /arch:AVX2)
else()
    # Linux/mac：SVML 内建（_mm256_atan2_ps 等）与 scipy-openblas 均不可用，走标量/线程路径
    target_compile_definitions(soul_diffusion_image_test PRIVATE XHQM_DEVICE_CPU TENSOR_WITH_CPU)
endif()

add_executable(structure_test
    structure_test.cpp
    ../algebra/ftype.cpp
)

add_executable(structure_extended_test
    structure_extended_test.cpp
)

add_executable(network_test
    network_test.cpp
    ../network/md5.cpp
    ../network/base64.cpp
    ../network/uuid.cpp
)

# 多态释放测试：基类指针 delete 派生类时派生析构必须执行（addr_b / geometry::shape 的回归夹具）。
# test/ 是独立工程（自带 project()，根工程不 add_subdirectory 它），主库靶子 xhqm 在此不可见；
# ../../binary/xhqm.lib 又只在 Windows 存在。故按本文件既有惯例显式列源文件。
# 下列源文件集是从「夹具实际引用到的符号」反推的最小闭包（addr.cpp 的依赖链含 socket/thread）。
add_executable(polydtor_test
    polydtor_test.cpp
    ../network/nebula/addr.cpp
    ../network/uuid.cpp
    ../network/md5.cpp
    ../network/network.cpp
    ../network/socket.cpp
    ../network/tcp.cpp
    ../network/udp.cpp
    ../software/system/timesys.cpp
    ../software/system/execsys.cpp
    ../software/system/process/process_base.cpp
    ../software/system/threads/threads_pool.cpp
    ../algebra/geometry/geometry.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
)

add_executable(metaclass_test
    metaclass_test.cpp
)

add_executable(pattern_test
    pattern_test.cpp
)

add_executable(pattern_extended_test
    pattern_extended_test.cpp
)

add_executable(hardware_test
    hardware_test.cpp
)

add_executable(system_test
    system_test.cpp
    ../software/system/codesys.cpp
    ../software/system/timesys.cpp
    ../software/system/filesys.cpp
    ../software/system/libmange.cpp
    ../software/system/execsys.cpp
    ../software/system/process/process_base.cpp
    ../software/system/threads/threads_pool.cpp
)

# 链主库：Windows 静态库名 xhqm.lib；Linux/mac 为 libxhqm.a
if(MSVC)
    set(XHQM_TEST_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../binary/xhqm.lib")
else()
    set(XHQM_TEST_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../binary/libxhqm.a")
endif()

add_executable(astral_test
    astral_test.cpp
)
target_link_libraries(astral_test "${XHQM_TEST_LIB}")

add_executable(astral_behavior_test
    astral_behavior_test.cpp
)
target_link_libraries(astral_behavior_test "${XHQM_TEST_LIB}")

add_executable(astral_stress_test
    astral_stress_test.cpp
)
target_link_libraries(astral_stress_test "${XHQM_TEST_LIB}")

add_executable(cs_test
    cs_test.cpp
)
target_link_libraries(cs_test "${XHQM_TEST_LIB}")

# ECS 测试：tuple_values 组件存储 + model_ecs 挂载闭环 + 多组件 join + theory_instancing 帧图接入 + 批量变换 benchmark（无 GL/VK 依赖，none 后端桩）
add_executable(ecs_test
    ecs_test.cpp
    ../fantasy/model_ecs.cpp
    ../fantasy/system.cpp
    ../fantasy/drawer.cpp
    ../fantasy/blurry.cpp
    ../fantasy/colors.cpp
    ../fantasy/camera.cpp
    ../fantasy/property.cpp
    ../fantasy/element.cpp
    ../fantasy/theory_pass.cpp
    ../fantasy/theory_render.cpp
    ../fantasy/backend/rhi_no.cpp
    ../fantasy/backend/shader.cpp
    ../fantasy/backend/shader_preprocess.cpp
    ../fantasy/lighting/shadow.cpp
    ../fantasy/lighting/csm.cpp
    ../fantasy/lighting/point_shadow.cpp
    ../fantasy/postfx/bloom.cpp
    ../fantasy/postfx/fxaa.cpp
    ../fantasy/postfx/tonemap.cpp
    ../fantasy/postfx/ssao.cpp
    ../fantasy/postfx/taa.cpp
    ../fantasy/postfx/ssr.cpp
    ../fantasy/postfx/compose.cpp
    ../fantasy/runtime/frame_graph.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/geometry/geometry.cpp
    ../network/uuid.cpp
    ../network/md5.cpp
    ../software/pattern/instance.cpp
)
target_include_directories(ecs_test PRIVATE
    "../fantasy"
    "../fantasy/backend"
    "../fantasy/lighting"
    "../fantasy/spatial"
    "../fantasy/postfx"
    "../fantasy/animation"
    "../fantasy/runtime"
)

# Toon 测试：UBO 布局偏移 + toon shader 预处理产物（GL/Vulkan/AppleGL）+ toon_prefab 接线（none 后端桩）
add_executable(toon_test
    toon_test.cpp
    ../fantasy/system.cpp
    ../fantasy/drawer.cpp
    ../fantasy/blurry.cpp
    ../fantasy/colors.cpp
    ../fantasy/camera.cpp
    ../fantasy/property.cpp
    ../fantasy/element.cpp
    ../fantasy/theory_pass.cpp
    ../fantasy/theory_render.cpp
    ../fantasy/model_ecs.cpp
    ../fantasy/backend/rhi_no.cpp
    ../fantasy/backend/shader.cpp
    ../fantasy/backend/shader_preprocess.cpp
    ../fantasy/lighting/shadow.cpp
    ../fantasy/lighting/csm.cpp
    ../fantasy/lighting/point_shadow.cpp
    ../fantasy/postfx/bloom.cpp
    ../fantasy/postfx/fxaa.cpp
    ../fantasy/postfx/tonemap.cpp
    ../fantasy/postfx/ssao.cpp
    ../fantasy/postfx/taa.cpp
    ../fantasy/postfx/ssr.cpp
    ../fantasy/postfx/compose.cpp
    ../fantasy/runtime/frame_graph.cpp
    ../algebra/ftype.cpp
    ../algebra/ctype.cpp
    ../algebra/geometry/geometry.cpp
    ../network/uuid.cpp
    ../network/md5.cpp
    ../software/pattern/instance.cpp
)
target_include_directories(toon_test PRIVATE
    "../fantasy"
    "../fantasy/backend"
    "../fantasy/lighting"
    "../fantasy/spatial"
    "../fantasy/postfx"
    "../fantasy/animation"
    "../fantasy/runtime"
)
