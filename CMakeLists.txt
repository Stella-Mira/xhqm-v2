cmake_minimum_required(VERSION 3.9...3.11)
project(xhqm)

##编译建议
#$cmakeArgs = @(
#  '-DXHQM_DYNAMIC=1'
#  '-DXHQM_DEVICE=CPU'
#  '-DXHQM_RENDER_BACKEND=OPENGL'
#  '-DXHQM_BUILD_SYSTEM=ON'
#  '-DXHQM_BUILD_TENSOR=ON'
#  '-DXHQM_BUILD_NETWORK=ON'
#  '-DXHQM_BUILD_FANTASY=ON'
#  '-DCMAKE_BUILD_TYPE=Debug'
#)
#cmake .. @cmakeArgs


# 设置c++版本为17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源码含 UTF-8 中文注释，显式指定 /utf-8 防止 MSVC 按 GBK 误读
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:/utf-8>)
    add_compile_options($<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>)
endif()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ============================================================
# 模块开关（默认全关）：控制哪些模块编入 xhqm 库
#   XHQM_BUILD_TENSOR   — algebra/tensor + algebra/analysis
#   XHQM_BUILD_NETWORK  — network（crypto/socket/mmap/cs/astral）
#   XHQM_BUILD_FANTASY  — fantasy 渲染引擎
#   XHQM_BUILD_SYSTEM   — software（datum/event/system/pattern/threads）
# ============================================================
option(XHQM_BUILD_TENSOR  "Build tensor/analysis module"   OFF)
option(XHQM_BUILD_NETWORK "Build network/astral module"     OFF)
option(XHQM_BUILD_FANTASY "Build fantasy rendering module"  OFF)
option(XHQM_BUILD_SYSTEM  "Build software/system module"    OFF)

if(XHQM_BUILD_FANTASY AND (NOT XHQM_BUILD_NETWORK OR NOT XHQM_BUILD_SYSTEM))
    message(WARNING "XHQM_BUILD_FANTASY 依赖 NETWORK(uuid) 与 SYSTEM(pattern/threads)，建议同时开启")
endif()

# ------------------------------------------------------------
# MSVC：software/ 位于 -I 搜索路径内，其中的 signal.h 会遮蔽系统 <signal.h>
#（MSVC 的 <csignal> 内部就是 #include <signal.h>，且 MSVC 没有 include_next），
# 会让 network/network.cpp、software/system/process/process_base.cpp 等 TU 报
# C2039/C2873（sig_atomic_t / raise / signal 不是全局命名空间成员）而编译失败。
# 把 UCRT 头目录排到最前，让真正的系统头先命中；非 MSVC 平台无需处理。
# ------------------------------------------------------------
if(MSVC)
    set(XHQM_UCRT_CANDIDATES "")
    if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
        list(APPEND XHQM_UCRT_CANDIDATES "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}ucrt")
    endif()
    # 兜底：只有 VS 生成器（当前 shell 未跑 vcvars）时，用 SDK 版本号拼标准安装路径
    # 注意：ProgramFiles(x86) 的括号必须转义，$ENV{...} 与 if(DEFINED ...) 都如此
    if(DEFINED ENV{ProgramFiles\(x86\)} AND CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        file(TO_CMAKE_PATH "$ENV{ProgramFiles\(x86\)}" XHQM_PF86)
        list(APPEND XHQM_UCRT_CANDIDATES
            "${XHQM_PF86}/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/ucrt")
    endif()
    foreach(XHQM_UCRT IN LISTS XHQM_UCRT_CANDIDATES)
        if(EXISTS "${XHQM_UCRT}/signal.h")
            include_directories(BEFORE "${XHQM_UCRT}")
            message(STATUS "MSVC: UCRT include dir prepended (system signal.h wins): ${XHQM_UCRT}")
            break()
        endif()
    endforeach()
endif()

# 头文件位置
include_directories("./")
# 注意顺序：fantasy 子目录须在 hardware/software 之前，避免 "system.h" 等
# 相对包含被 software/system.h 抢先命中（fantasy/system.h 才是 fantasy 侧的头）
if(XHQM_BUILD_FANTASY)
    include_directories("./fantasy/")
    include_directories("./fantasy/backend/")
    include_directories("./fantasy/spatial/")
    include_directories("./fantasy/lighting/")
    include_directories("./fantasy/postfx/")
    include_directories("./fantasy/animation/")
    include_directories("./fantasy/runtime/")
    include_directories("../depend/glew/include/")
    include_directories("../depend/vulkan/include/")
    include_directories("../depend/vulkan/glslang/include/")
endif()
include_directories("./hardware/")
include_directories("./software/")
include_directories("../depend/nlohmann/")

# 硬件设备开关（全局）：NO / CPU / GPU / NET / AUTO，渲染与张量后端均从该设备派生
set(XHQM_DEVICE "CPU" CACHE STRING "Device: NO, CPU, GPU, NET or AUTO")
set_property(CACHE XHQM_DEVICE PROPERTY STRINGS NO CPU GPU NET AUTO)
if(XHQM_DEVICE STREQUAL "NO")
    add_definitions(-DXHQM_DEVICE_NO)
    message(STATUS "Device: NO (serial/stub)")
elseif(XHQM_DEVICE STREQUAL "CPU")
    add_definitions(-DXHQM_DEVICE_CPU)
    message(STATUS "Device: CPU")
elseif(XHQM_DEVICE STREQUAL "GPU")
    add_definitions(-DXHQM_DEVICE_GPU)
    message(STATUS "Device: GPU")
elseif(XHQM_DEVICE STREQUAL "NET")
    add_definitions(-DXHQM_DEVICE_NET)
    message(STATUS "Device: NET")
else()
    add_definitions(-DXHQM_DEVICE_AUTO)
    message(STATUS "Device: AUTO")
endif()

# 张量后端开关：NO=串行桩 / OFF=并行桩（无 SIMD/BLAS）/ ON=自动检测（SIMD/BLAS/CUDA/Metal）
set(XHQM_TENSOR_BACKEND "ON" CACHE STRING "Tensor backend: NO (serial), OFF (parallel), ON (auto-detect)")
set_property(CACHE XHQM_TENSOR_BACKEND PROPERTY STRINGS NO OFF ON)

# ------------------------------------------------------------
# 后端选择总约定：编译期和运行期一样，都是先检测、检测不到回退。
#
# XHQM_TENSOR_BACKEND（张量后端）：
#   device=NO    且 TENSOR_BACKEND 无效 → 统一走串行桩
#   device=CPU   且 TENSOR_BACKEND=OFF  → 走并行桩，并行→串行
#   device=CPU   且 TENSOR_BACKEND=ON   → 编译期自动检测 BLAS 和 SIMD，blas→simd→并行→串行
#   device=GPU   且 TENSOR_BACKEND=OFF  → 走并行桩，并行→串行
#   device=GPU   且 TENSOR_BACKEND=ON   → 编译期自动检测 metal/cuda/blas/simd，metal/cuda→blas→simd→并行→串行
#   device=AUTO  且 TENSOR_BACKEND 无效 → 编译期自动检测 metal/cuda/blas/simd，
#                                         运行期优先顺序 metal/cuda→blas→simd→并行→串行
#
# 有效张量模式（XHQM_DEVICE 与 XHQM_TENSOR_BACKEND 的交互）：
#   device=NO   → 恒 NO（串行桩，TENSOR_BACKEND 无效）
#   device=AUTO → 恒 ON（全自动检测，TENSOR_BACKEND 无效）
#   device=CPU/GPU/NET → 按 XHQM_TENSOR_BACKEND
if(XHQM_DEVICE STREQUAL "NO")
    set(_tensor_mode "NO")
elseif(XHQM_DEVICE STREQUAL "AUTO")
    set(_tensor_mode "ON")
else()
    set(_tensor_mode "${XHQM_TENSOR_BACKEND}")
endif()
message(STATUS "Tensor backend mode: ${_tensor_mode}")

# CPU 后端（并行桩）：mode OFF/ON（no 后端恒编入，见 XHQM_BODY_CORE）
if(XHQM_BUILD_TENSOR AND (_tensor_mode STREQUAL "OFF" OR _tensor_mode STREQUAL "ON"))
    add_definitions(-DTENSOR_WITH_CPU)
endif()

# GPU 后端宏：mode ON 且 device GPU/AUTO
if(XHQM_BUILD_TENSOR AND _tensor_mode STREQUAL "ON" AND (XHQM_DEVICE STREQUAL "GPU" OR XHQM_DEVICE STREQUAL "AUTO"))
    add_definitions(-DTENSOR_WITH_GPU)
endif()

# SIMD（mode ON，MSVC AVX2 内建）
if(_tensor_mode STREQUAL "ON" AND MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/arch:AVX2>)
    add_definitions(-DTENSOR_WITH_SIMD)
    message(STATUS "Tensor CPU SIMD: AVX2")
endif()

# BLAS（mode ON，检测 depend/openblas，scipy-openblas32）
set(OPENBLAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../depend/openblas")
if(_tensor_mode STREQUAL "ON" AND XHQM_BUILD_TENSOR AND EXISTS "${OPENBLAS_ROOT}/include/cblas.h" AND EXISTS "${OPENBLAS_ROOT}/lib/libscipy_openblas.lib")
    add_definitions(-DTENSOR_WITH_BLAS)
    include_directories("${OPENBLAS_ROOT}/include")
    set(OPENBLAS_LIBRARY "${OPENBLAS_ROOT}/lib/libscipy_openblas.lib")
    message(STATUS "Tensor CPU BLAS: OpenBLAS (scipy-openblas32)")
else()
    message(STATUS "Tensor CPU BLAS: not found (fallback SIMD)")
endif()

# ------------------------------------------------------------
# XHQM_RENDER_BACKEND（渲染后端）：
#   device=NO/CPU/NET                  → RENDER_BACKEND 无效，只使用 none 桩
#   device=GPU 且 RENDER_BACKEND=AUTO   → 编译期自动检测 vk 和 gl，vk→gl→none
#   device=GPU 且 RENDER_BACKEND=VULKAN → vk→none
#   device=GPU 且 RENDER_BACKEND=OPENGL → gl→none
#   device=AUTO                        → RENDER_BACKEND 无效，vk→gl→none
# RENDER_BACKEND 选项：AUTO（双后端编译，运行时回退）/ VULKAN / OPENGL
set(XHQM_RENDER_BACKEND "AUTO" CACHE STRING "Render API (device=gpu 时生效): AUTO, VULKAN or OPENGL")
set_property(CACHE XHQM_RENDER_BACKEND PROPERTY STRINGS AUTO VULKAN OPENGL)
if(XHQM_DEVICE STREQUAL "NO" OR XHQM_DEVICE STREQUAL "CPU" OR XHQM_DEVICE STREQUAL "NET")
    # no/cpu/net：不编译任何渲染后端，RENDER_BACKEND 无效
    message(STATUS "Render backend: NONE (no/cpu/net device, RENDER_BACKEND ignored)")
elseif(XHQM_DEVICE STREQUAL "AUTO")
    # auto：双后端编译，运行时 vk→gl→none 回退，RENDER_BACKEND 无效
    add_definitions(-DBUILD_WITH_VULKAN -DBUILD_WITH_OPENGL)
    message(STATUS "Render backend: AUTO (Vulkan → OpenGL → None, RENDER_BACKEND ignored)")
else()
    # gpu：按 RENDER_BACKEND 编译单/双后端，运行时探测回退
    if(XHQM_RENDER_BACKEND STREQUAL "VULKAN")
        add_definitions(-DBUILD_WITH_VULKAN)
        message(STATUS "Render backend: VULKAN (Vulkan → None)")
    elseif(XHQM_RENDER_BACKEND STREQUAL "OPENGL")
        add_definitions(-DBUILD_WITH_OPENGL)
        message(STATUS "Render backend: OPENGL (OpenGL → None)")
    else()
        add_definitions(-DBUILD_WITH_VULKAN -DBUILD_WITH_OPENGL)
        message(STATUS "Render backend: AUTO (Vulkan → OpenGL → None)")
    endif()
endif()

# ============================================================
# 核心（恒编译）：head/hardware/algebra 基础/geometry/metaclass/structure
# ============================================================
set(XHQM_HEAD_CORE
    "head.h"
    "hardware/hardware.h"
    "hardware/media.h"

    "algebra/value.h"
    "algebra/mathf.h"
    "algebra/itype.h"
    "algebra/ftype.h"
    "algebra/ctype.h"
    "algebra/tensor/complex.hpp"
    "algebra/tensor/dualnum.hpp"

    "algebra/geometry/geometry.h"
    "algebra/geometry/linear.h"

    "metaclass/cache.h"
    "metaclass/block.h"
    "metaclass/buffer.h"
    "metaclass/compute.h"
    "metaclass/template.h"
    "metaclass/tuple_values.h"

    "lattice/stacks.h"
    "lattice/queues.h"
    "lattice/list.hpp"
    "lattice/tree.hpp"
    "lattice/mesh.hpp"
    "lattice/directed_mesh.hpp"

    "software/system/process/process_base.h"
    "software/system/threads/threads_pool.h"
    "software/system/codesys.h"
    "software/system/filesys.h"
    "software/system/timesys.h"
    "software/system/execsys.h"
    # signal（信号槽）是 nebula 的公共依赖：addr_b 的 frame_signal/broad_signal 需要
    # signals::pool()；NETWORK=ON 而 SYSTEM=OFF 时缺该实现会 LNK2019（CMake 构建实测）
    "software/signal.h"
)

set(XHQM_BODY_CORE
    "algebra/itype.cpp"
    "algebra/ftype.cpp"
    "algebra/ctype.cpp"
    "algebra/geometry/geometry.cpp"

    "software/system/process/process_base.cpp"
    "software/system/threads/threads_pool.cpp"
    "software/system/codesys.cpp"
    "software/system/filesys.cpp"
    "software/system/timesys.cpp"
    "software/system/execsys.cpp"
    # signal.cpp 与上面几个同为多模块公用的底座（SYSTEM 模块也自带一份，重复列入会自动去重）：
    # nebula 的 addr_b（frame_signal/broad_signal）用到 signals::pool()，只开 NETWORK 时必须编入
    "software/signal.cpp"

    # 张量 no 后端恒编入：关闭 TENSOR 模块时 tensor_backend_active 仍可用
    # （等价于「TENSOR 开 + DEVICE=NO」的串行桩），matrix/tensor 运算可链接运行
    "algebra/tensor/backend/tensor_backend.cpp"
    "algebra/tensor/backend/tensor_backend_no.cpp"
)

# ============================================================
# tensor：algebra/tensor + algebra/analysis
# ============================================================
set(XHQM_HEAD_TENSOR
    "algebra/analysis/analysis.h"
	
    "algebra/tensor/store.h"
    "algebra/tensor/figure.h"
    "algebra/tensor/vector.h"
    "algebra/tensor/matrix.h"
    "algebra/tensor/tensor.h"
	"algebra/tensor/linalg.h"
    "algebra/tensor/sparse.h"
    "algebra/tensor/tensor_base.h"
    "algebra/tensor/tensor_view.h"
    "algebra/tensor/conv.h"
    "algebra/tensor/stats.h"
    "algebra/tensor/poly.h"
    "algebra/tensor/signal.h"
    "algebra/tensor/quaternion.hpp"
    "algebra/tensor/dual_quaternion.hpp"
    "algebra/tensor/backend/backend.h"
    "algebra/tensor/backend/tensor_backend.h"

    "algebra/soul/random.h"
    "algebra/soul/activation.h"
    "algebra/soul/normalize.h"
    "algebra/soul/linear.h"
    "algebra/soul/attention.h"
    "algebra/soul/embedding.h"
    "algebra/soul/transformer.h"
    "algebra/soul/serialize.h"
    "algebra/soul/ad_node.h"
    "algebra/soul/ad_ops.h"
    "algebra/soul/ad.h"
    "algebra/soul/soul.h"
)

set(XHQM_BODY_TENSOR
    "algebra/analysis/analysis.cpp"
)

# ============================================================
# network：crypto / socket / mmap / cs / astral
# ============================================================
set(XHQM_HEAD_NETWORK
    "network/md2.h"
    "network/md4.h"
    "network/md5.h"
    "network/sha.h"
    "network/uuid.h"
    "network/socket.h"
    "network/network.h"
    "network/license.h"
    "network/base64.h"
    "network/mmap.h"
    "network/aes.h"
    "network/des.h"
    "network/cs.h"
    "network/tcp.h"
    "network/udp.h"
    "network/astral/astral.h"
    "network/astral/kernel.h"
	"network/astral/evoke.h"
    "network/astral/mmap_index.h"
    "network/astral/thing.h"
    # nebula：B 世代分层（下→上）node 基类 → addr（地址/邻居表）→ data（通道/记忆）
    #        → life（事件池）→ link（L1 邻居图）→ find（L2 搜索）→ post（L3 订阅推送）
    #        → tool（工具封装）→ zero/star（组装）
    "network/nebula/nebula.h"
    "network/nebula/addr.h"
    "network/nebula/addr/addr_b.h"
    "network/nebula/addr/addr_uuid.h"
    "network/nebula/addr/addr_ipv4.h"
    "network/nebula/addr/addr_serial.h"
    "network/nebula/addr/addr_ipv6.h"
    "network/nebula/data.h"
    "network/nebula/life.h"
    "network/nebula/link.h"
    "network/nebula/find.h"
    "network/nebula/post.h"
    "network/nebula/zero.h"
    "network/nebula/star.h"
    # A 世代的 notify/sse_server/sse_client/store/query/astral_store/server/sensor 已从
    # network/nebula/ 移除；门面头 xhqm/nebula 与 network/README.md 仍引用旧名，待同步
)

set(XHQM_BODY_NETWORK
    "network/md2.cpp"
    "network/md4.cpp"
    "network/md5.cpp"
    "network/sha.cpp"
    "network/uuid.cpp"
    "network/socket.cpp"
    "network/network.cpp"
    "network/license.cpp"
    "network/base64.cpp"
    "network/mmap.cpp"
    "network/aes.cpp"
    "network/des.cpp"
    "network/tcp.cpp"
    "network/udp.cpp"
    "network/astral/kernel.cpp"
	"network/astral/evoke.cpp"
    "network/astral/mmap_index.cpp"
    "network/astral/thing.cpp"
    "network/nebula/nebula.cpp"
    "network/nebula/addr.cpp"
    "network/nebula/addr/addr_b.cpp"
    "network/nebula/addr/addr_uuid.cpp"
    "network/nebula/addr/addr_ipv4.cpp"
    "network/nebula/addr/addr_serial.cpp"
    "network/nebula/addr/addr_ipv6.cpp"
    "network/nebula/data.cpp"
    "network/nebula/life.cpp"
    "network/nebula/link.cpp"
    "network/nebula/find.cpp"
    "network/nebula/post.cpp"
    "network/nebula/zero.cpp"
)

# nebula：B 世代 13 个 TU 全部纳入，文件集与 VS 工程 xhqm.vcxproj 一致。
# data.cpp 早前因上层未适配 link 新形态被临时剔除，现 find/post/zero 已适配 link/addr/life。

# ============================================================
# fantasy：渲染引擎（rhi_gl/rhi_vk/spirv 按设备/后端条件加入）
# ============================================================
set(XHQM_HEAD_FANTASY
    "fantasy/fantasy.h"
    "fantasy/colors.h"
    "fantasy/drawer.h"
    "fantasy/blurry.h"
    "fantasy/system.h"
    "fantasy/element.h"
    "fantasy/property.h"
    "fantasy/camera.h"
    "fantasy/theory_pass.h"
    "fantasy/theory_render.h"
    "fantasy/model_ecs.h"
    "fantasy/backend/backend.h"
    "fantasy/backend/shader.h"
    "fantasy/backend/rhi_no.h"
    "fantasy/backend/rhi_gl.h"
    "fantasy/backend/rhi_vk.h"
    "fantasy/backend/spirv.h"
    "fantasy/backend/shader_preprocess.h"
    "fantasy/backend/shader_sources.h"
    "fantasy/spatial/spatial.h"
    "fantasy/spatial/culler.h"
    "fantasy/spatial/render_queue.h"
    "fantasy/spatial/lod.h"
    "fantasy/spatial/occlusion.h"
    "fantasy/spatial/gpu_culler.h"
    "fantasy/lighting/light.h"
    "fantasy/lighting/shadow.h"
    "fantasy/lighting/csm.h"
    "fantasy/lighting/point_shadow.h"
    "fantasy/postfx/bloom.h"
    "fantasy/postfx/fxaa.h"
    "fantasy/postfx/tonemap.h"
    "fantasy/postfx/compose.h"
    "fantasy/animation/skeleton.h"
    "fantasy/animation/animation.h"
    "fantasy/animation/ik.h"
    "fantasy/runtime/performance.h"
    "fantasy/runtime/async.h"
    "fantasy/runtime/frame_graph.h"
    "fantasy/runtime/command_recorder.h"
    "fantasy/runtime/asset_manager.h"
    "fantasy/runtime/gltf_loader.h"
    "fantasy/runtime/bc1.h"
    "fantasy/runtime/scene_io.h"
)

set(XHQM_BODY_FANTASY
    "fantasy/colors.cpp"
    "fantasy/drawer.cpp"
    "fantasy/blurry.cpp"
    "fantasy/system.cpp"
    "fantasy/element.cpp"
    "fantasy/property.cpp"
    "fantasy/camera.cpp"
    "fantasy/theory_pass.cpp"
    "fantasy/theory_render.cpp"
    "fantasy/model_ecs.cpp"
    "fantasy/backend/shader.cpp"
    "fantasy/backend/rhi_no.cpp"
    "fantasy/backend/shader_preprocess.cpp"
    "fantasy/spatial/spatial.cpp"
    "fantasy/animation/skeleton.cpp"
    "fantasy/animation/animation.cpp"
    "fantasy/spatial/culler.cpp"
    "fantasy/spatial/render_queue.cpp"
    "fantasy/animation/ik.cpp"
    "fantasy/animation/gpu_skinning.cpp"
    "fantasy/runtime/performance.cpp"
    "fantasy/spatial/lod.cpp"
    "fantasy/spatial/occlusion.cpp"
    "fantasy/spatial/gpu_culler.cpp"
    "fantasy/spatial/gpu_hiz.cpp"
    "fantasy/spatial/gpu_hiz_culler.cpp"
    "fantasy/lighting/shadow.cpp"
    "fantasy/lighting/csm.cpp"
    "fantasy/lighting/point_shadow.cpp"
    "fantasy/postfx/bloom.cpp"
    "fantasy/postfx/fxaa.cpp"
    "fantasy/postfx/tonemap.cpp"
    "fantasy/postfx/compose.cpp"
    "fantasy/postfx/ssao.cpp"
    "fantasy/postfx/taa.cpp"
    "fantasy/postfx/ssr.cpp"
    "fantasy/runtime/async.cpp"
    "fantasy/runtime/frame_graph.cpp"
    "fantasy/runtime/command_recorder.cpp"
    "fantasy/runtime/gltf_loader.cpp"
    "fantasy/runtime/bc1.cpp"
    "fantasy/runtime/scene_io.cpp"
)

# ============================================================
# system：software（datum/event/system/pattern/threads）
# ============================================================
set(XHQM_HEAD_SYSTEM
    "software/system/threads.h"
	"software/system/threads/exist.h"
    "software/system/threads/lock_mutex.h"
    "software/system/threads/threads_task.h"
    "software/system/threads/threads_pool.h"
    "software/system/process/process_base.h"

    "software/datum.h"
    "software/event.h"
    "software/packet.h"
    "software/utility.h"
    "software/message.h"
    "software/command.h"
    "software/project.h"
    "software/system/filesys.h"
    "software/system/timesys.h"
    "software/system/libmange.h"
    "software/system/process.h"
    "software/system/execsys.h"
    "software/signal.h"
    "software/system.h"
    "software/pattern/notifier.h"
    "software/pattern/observer.h"
    "software/pattern/instance.h"
)

set(XHQM_BODY_SYSTEM
    "software/system/threads/threads_pool.cpp"
    "software/system/process/process_base.cpp"

    "software/datum.cpp"
    "software/packet.cpp"
    "software/utility.cpp"
    "software/message.cpp"
    "software/command.cpp"
    "software/project.cpp"
    "software/system/timesys.cpp"
    "software/system/filesys.cpp"
    "software/system/libmange.cpp"
    "software/system/execsys.cpp"
    "software/signal.cpp"
    "software/system.cpp"
    "software/pattern/instance.cpp"
)

# ============================================================
# 按开关组合源文件
# ============================================================
set(XHQM_HEAD ${XHQM_HEAD_CORE})
set(XHQM_BODY ${XHQM_BODY_CORE})

# uuid/md5 是多个模块（fantasy/system 等）的公共依赖，与 NETWORK 开关解耦无条件编译；
# 否则 NETWORK=OFF 时链接缺 uuid_equal（nebula 等重模块保持按开关编译）
list(APPEND XHQM_HEAD "network/uuid.h" "network/md5.h")
list(APPEND XHQM_BODY "network/uuid.cpp" "network/md5.cpp")

if(XHQM_BUILD_TENSOR)
    add_definitions(-DXHQM_BUILD_TENSOR)
    list(APPEND XHQM_HEAD ${XHQM_HEAD_TENSOR})
    list(APPEND XHQM_BODY ${XHQM_BODY_TENSOR})
    message(STATUS "Module: TENSOR ON")
endif()
if(XHQM_BUILD_NETWORK)
    add_definitions(-DXHQM_BUILD_NETWORK)
    list(APPEND XHQM_HEAD ${XHQM_HEAD_NETWORK})
    list(APPEND XHQM_BODY ${XHQM_BODY_NETWORK})
    message(STATUS "Module: NETWORK ON")
endif()
if(XHQM_BUILD_FANTASY)
    add_definitions(-DXHQM_BUILD_FANTASY)
    list(APPEND XHQM_HEAD ${XHQM_HEAD_FANTASY})
    list(APPEND XHQM_BODY ${XHQM_BODY_FANTASY})
    message(STATUS "Module: FANTASY ON")
endif()
if(XHQM_BUILD_SYSTEM)
    add_definitions(-DXHQM_BUILD_SYSTEM)
    list(APPEND XHQM_HEAD ${XHQM_HEAD_SYSTEM})
    list(APPEND XHQM_BODY ${XHQM_BODY_SYSTEM})
    message(STATUS "Module: SYSTEM ON")
endif()

# 生成位置（xhqm 在 xhqm_v2/xhqm，上溯一级到 D:\WorkSpace\xhqm_v2\binary；轻量构建可覆盖到独立目录）
set(XHQM_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../binary/" CACHE STRING "xhqm 产物输出目录")
set(PROJECT_BINARY_DIR "${XHQM_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/")#表示静态库的默认输出路径
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/")#表示linux上动态库（*.so）的输出路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/")#表示windows上动态库（*.dll）的输出路径

# 根据XHQM_DYNAMIC宏定义选择库类型
if(DEFINED XHQM_DYNAMIC)
	# 设置预定义宏
	add_definitions(-DXHQM_DYNAMIC)
	add_definitions(-DXHQM_PROGRAM)
    add_library(xhqm SHARED ${XHQM_HEAD} ${XHQM_BODY}) # 动态库
    message(STATUS "Building xhqm as SHARED library")
else()
	# 设置预定义宏
	add_definitions(-DXHQM_PROGRAM)
    add_library(xhqm STATIC ${XHQM_HEAD} ${XHQM_BODY}) # 静态库
    message(STATUS "Building xhqm as STATIC library")
endif()

# 静态库需可被共享库（如 astral_api.so）链接：非 Windows 编译位置无关代码
if(NOT WIN32)
    set_target_properties(xhqm PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# 渲染后端实现（fantasy 开启时按 device/backend 条件编译）
# 与后端宏定义保持一致：no/cpu/net→无，auto→双后端，gpu→按 RENDER_BACKEND
set(_render_gl OFF)
set(_render_vk OFF)
if(XHQM_BUILD_FANTASY)
    if(XHQM_DEVICE STREQUAL "NO" OR XHQM_DEVICE STREQUAL "CPU" OR XHQM_DEVICE STREQUAL "NET")
        # no/cpu/net：不编译渲染后端
    elseif(XHQM_DEVICE STREQUAL "AUTO")
        set(_render_gl ON)
        set(_render_vk ON)
    else()
        # gpu：按 RENDER_BACKEND 编译
        if(XHQM_RENDER_BACKEND STREQUAL "VULKAN")
            set(_render_vk ON)
        elseif(XHQM_RENDER_BACKEND STREQUAL "OPENGL")
            set(_render_gl ON)
        else()
            set(_render_gl ON)
            set(_render_vk ON)
        endif()
    endif()
endif()
if(_render_gl)
    target_sources(xhqm PRIVATE "fantasy/backend/rhi_gl.cpp")
endif()
if(_render_vk)
    target_sources(xhqm PRIVATE "fantasy/backend/rhi_vk.cpp" "fantasy/backend/spirv.cpp")
endif()

# 张量后端实现（按有效模式 _tensor_mode 编译）
if(XHQM_BUILD_TENSOR AND (_tensor_mode STREQUAL "OFF" OR _tensor_mode STREQUAL "ON"))
    target_sources(xhqm PRIVATE "algebra/tensor/backend/tensor_backend_cpu.cpp")
endif()
# 张量 GPU 后端：mode ON 且 device GPU/AUTO；Apple Silicon 用 Metal/MPS，其余用 CUDA
if(XHQM_BUILD_TENSOR AND _tensor_mode STREQUAL "ON" AND (XHQM_DEVICE STREQUAL "GPU" OR XHQM_DEVICE STREQUAL "AUTO"))
    if(APPLE)
        enable_language(OBJCXX)
        add_definitions(-DXHQM_TENSOR_METAL)
        target_sources(xhqm PRIVATE "algebra/tensor/backend/tensor_backend_metal.mm")
        target_link_libraries(xhqm "-framework Metal" "-framework MetalPerformanceShaders" "-framework Foundation")
        set_source_files_properties("algebra/tensor/backend/tensor_backend_metal.mm"
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        message(STATUS "Tensor GPU backend: Metal/MPS (Apple Silicon)")
    else()
        find_package(CUDAToolkit QUIET)
        if(CUDAToolkit_FOUND)
            enable_language(CUDA)
            set(CMAKE_CUDA_STANDARD 17)
            set(CMAKE_CUDA_STANDARD_REQUIRED ON)
            set(XHQM_CUDA_ARCH "86" CACHE STRING "CUDA compute capability (e.g. 86/89/90)")
            set_property(TARGET xhqm PROPERTY CUDA_ARCHITECTURES ${XHQM_CUDA_ARCH})
            add_definitions(-DXHQM_TENSOR_CUDA)
            target_sources(xhqm PRIVATE
                "algebra/tensor/backend/tensor_backend_gpu.cpp"
                "algebra/tensor/backend/tensor_backend_gpu.cu")
            target_link_libraries(xhqm CUDA::cudart CUDA::cublas CUDA::cusolver)
            message(STATUS "Tensor GPU backend: CUDA ${CUDAToolkit_VERSION}")
        else()
            target_sources(xhqm PRIVATE "algebra/tensor/backend/tensor_backend_gpu.cpp")
            message(STATUS "Tensor GPU backend: CUDA not found, stub fallback")
        endif()
    endif()
endif()

# 张量 CPU BLAS 链接依赖
if(OPENBLAS_LIBRARY)
    target_link_libraries(xhqm "${OPENBLAS_LIBRARY}")
endif()

# 渲染后端链接依赖
if(WIN32)
    # Release 链接也生成 PDB（崩溃定位需要符号；不影响运行时行为）
    target_link_options(xhqm PRIVATE "/DEBUG" "/PDB:D:/WorkSpace/xhqm_v2/binary/xhqm.pdb")
endif()
if(WIN32 AND _render_gl)
    target_link_libraries(xhqm opengl32)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_libraries(xhqm "${CMAKE_CURRENT_SOURCE_DIR}/../depend/glew/lib/glew32d.lib")
    else()
        target_link_libraries(xhqm "${CMAKE_CURRENT_SOURCE_DIR}/../depend/glew/lib/glew32.lib")
    endif()
endif()
if(WIN32 AND _render_vk)
    target_link_libraries(xhqm "${CMAKE_CURRENT_SOURCE_DIR}/../depend/vulkan/lib/vulkan-1.lib")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Debug 版 glslang/SPIRV-Tools（源码 /MDd 构建，见 depend/vulkan/glslang_src）
        target_link_libraries(xhqm
            "${CMAKE_CURRENT_SOURCE_DIR}/../depend/vulkan/glslang/lib/Debug/glslang.lib"
            "${CMAKE_CURRENT_SOURCE_DIR}/../depend/vulkan/glslang/lib/Debug/SPIRV-Tools.lib"
            "${CMAKE_CURRENT_SOURCE_DIR}/../depend/vulkan/glslang/lib/Debug/SPIRV-Tools-opt.lib")
    else()
        target_link_libraries(xhqm
            "${CMAKE_CURRENT_SOURCE_DIR}/../depend/vulkan/glslang/lib/glslang.lib"
            "${CMAKE_CURRENT_SOURCE_DIR}/../depend/vulkan/glslang/lib/SPIRV-Tools.lib"
            "${CMAKE_CURRENT_SOURCE_DIR}/../depend/vulkan/glslang/lib/SPIRV-Tools-opt.lib")
    endif()
endif()
# Linux/macOS 渲染后端依赖：系统 glew/OpenGL/Vulkan/glslang（apt: libglew-dev libvulkan-dev glslang-dev）
if(NOT WIN32 AND _render_gl)
    find_package(OpenGL REQUIRED)
    find_package(GLEW REQUIRED)
    target_link_libraries(xhqm OpenGL::GL GLEW::GLEW)
endif()
if(NOT WIN32 AND _render_vk)
    find_package(Vulkan REQUIRED)
    if(APPLE)
        # macOS：MoltenVK（无 xcb；Metal 表面辅助文件 + Cocoa/QuartzCore 框架）
        enable_language(OBJCXX)
        target_sources(xhqm PRIVATE "fantasy/backend/rhi_vk_macos.mm")
        set_source_files_properties("fantasy/backend/rhi_vk_macos.mm" PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(xhqm Vulkan::Vulkan
            glslang-default-resource-limits SPIRV-Tools-opt SPIRV-Tools SPIRV glslang
            "-framework Cocoa" "-framework QuartzCore")
    else()
        # glslang-dev（Ubuntu 提供静态库 libglslang.a/libSPIRV.a/...）按依赖序链接
        target_link_libraries(xhqm Vulkan::Vulkan xcb
            glslang-default-resource-limits SPIRV-Tools-opt SPIRV-Tools SPIRV glslang)
    endif()
    target_link_libraries(xhqm pthread)
endif()
