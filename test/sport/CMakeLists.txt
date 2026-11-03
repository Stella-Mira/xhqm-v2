cmake_minimum_required(VERSION 3.17)
project(soul_exp CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
    # 张量 CPU SIMD（AVX2 + FMA）
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/arch:AVX2>)
endif()

# 依赖 xhqm 头文件
include_directories("include")
# 禁用 head.h 里自动 link ../binary/xhqm.lib 的 pragma
add_definitions(-DXHQM_PROGRAM)

# ===== 张量后端：CPU 并行 + SIMD + OpenBLAS（默认）；可选 CUDA GPU =====
# 实测：流式小矩阵增量推理下 GPU 传输开销无收益（w2v gpu≈cpu、生成器 gpu 慢 2x），
# 默认走 CPU 后端；需要 GPU 时 -DSOUL_EXP_CUDA=ON。
option(SOUL_EXP_CUDA "Build with CUDA GPU backend" OFF)
if(SOUL_EXP_CUDA)
    add_definitions(-DXHQM_DEVICE_AUTO -DTENSOR_WITH_GPU)
    message(STATUS "Tensor backend: AUTO (CPU + GPU)")
else()
    add_definitions(-DXHQM_DEVICE_CPU)
    message(STATUS "Tensor backend: CPU only")
endif()
# SIMD 内核（tensor_backend_simd.h）使用 MSVC 专属 intrinsics（_mm256_atan2_ps 等），
# GCC/Clang 不存在这些内置函数；与引擎本体策略一致：SIMD 仅 Windows/MSVC 开启，
# 其他平台走标量/多线程 CPU 路径。
add_definitions(-DTENSOR_WITH_CPU)
if(MSVC)
    add_definitions(-DTENSOR_WITH_SIMD)
endif()

# OpenBLAS 检测：
# - Windows：depend/openblas（scipy-openblas32，符号带 scipy_ 前缀）
# - Linux/其他：系统 OpenBLAS（标准 cblas_ 前缀，需 -DOPENBLAS_STANDARD）
# - 都没有：回退纯 SIMD（不定义 TENSOR_WITH_BLAS，避免 cblas.h 缺失导致编译失败）
set(OPENBLAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../depend/openblas")
if(EXISTS "${OPENBLAS_ROOT}/include/cblas.h" AND EXISTS "${OPENBLAS_ROOT}/lib/libscipy_openblas.lib")
    include_directories("${OPENBLAS_ROOT}/include")
    set(OPENBLAS_LIB "${OPENBLAS_ROOT}/lib/libscipy_openblas.lib")
    message(STATUS "Tensor CPU BLAS: OpenBLAS (scipy-openblas32)")
elseif(NOT MSVC)
    find_path(OPENBLAS_INCLUDE_DIR cblas.h)
    find_library(OPENBLAS_LIBRARY NAMES openblas libopenblas)
    if(OPENBLAS_INCLUDE_DIR AND OPENBLAS_LIBRARY)
        include_directories("${OPENBLAS_INCLUDE_DIR}")
        set(OPENBLAS_LIB "${OPENBLAS_LIBRARY}")
        add_definitions(-DOPENBLAS_STANDARD)
        message(STATUS "Tensor CPU BLAS: OpenBLAS (system, standard prefix)")
    else()
        message(WARNING "OpenBLAS not found; fallback SIMD (no BLAS)")
    endif()
else()
    message(WARNING "OpenBLAS not found at ${OPENBLAS_ROOT}; fallback SIMD (no BLAS)")
endif()
if(OPENBLAS_LIB)
    add_definitions(-DTENSOR_WITH_BLAS)
endif()

# xhqm 核心源文件（CPU 后端恒编入）
set(XHQM_CORE_SOURCES
    ../../algebra/ftype.cpp
    ../../algebra/ctype.cpp
    ../../algebra/tensor/backend/tensor_backend.cpp
    ../../algebra/tensor/backend/tensor_backend_no.cpp
    ../../algebra/tensor/backend/tensor_backend_cpu.cpp
)

# CUDA GPU 后端
if(SOUL_EXP_CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD 17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        # nvcc 的 host 编译器按 UTF-8 解析（xhqm 的 .cu 含中文注释，否则被 GBK 误解析吞掉下一行）
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Xcompiler=/utf-8")
        add_definitions(-DXHQM_TENSOR_CUDA)
        list(APPEND XHQM_CORE_SOURCES
            ../../algebra/tensor/backend/tensor_backend_gpu.cpp
            ../../algebra/tensor/backend/tensor_backend_gpu.cu)
        message(STATUS "Tensor GPU backend: CUDA ${CUDAToolkit_VERSION}")
    else()
        message(WARNING "CUDA not found; falling back to CPU only")
    endif()
endif()

function(soul_exp_executable name)
    add_executable(${name} ${ARGN} ${XHQM_CORE_SOURCES})
    if(SOUL_EXP_CUDA AND CUDAToolkit_FOUND)
        set_property(TARGET ${name} PROPERTY CUDA_ARCHITECTURES 86)   # RTX 3090 Ti = SM86
        target_link_libraries(${name} CUDA::cudart CUDA::cublas CUDA::cusolver)
    endif()
    if(OPENBLAS_LIB)
        target_link_libraries(${name} "${OPENBLAS_LIB}")
    endif()
endfunction()

soul_exp_executable(loader_smoke test/loader_smoke.cpp)
soul_exp_executable(wav2vec2_smoke test/wav2vec2_smoke.cpp)
soul_exp_executable(loader_test test/loader_test.cpp)
soul_exp_executable(dinov2_test test/dinov2_test.cpp)
soul_exp_executable(sport_test test/sport_test.cpp)
soul_exp_executable(bench_speed test/bench_speed.cpp)
