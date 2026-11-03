cmake_minimum_required(VERSION 3.15)

# 统一查看器工程：RenderWidget（QOpenGLWidget/GL）与 RhiViewport（QWindow/Vulkan）已合并为
# 单一 QWindow 对称壳，运行期由 --backend vulkan|opengl|auto 决定渲染后端：
#   vulkan → 引擎自建 surface；opengl → QOpenGLContext 自管上下文；auto → 探测 Vulkan 失败回退 GL。
# 交互/选中/相机 orbit/场景构建/诊断参数全部共享（RenderWidget 目录为历史版本，不再构建）。

# Release 构建（正式配置；Debug 链已打通：引擎 Debug 时同步切 Debug + debug 依赖）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

project(RhiViewport)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---- 依赖路径（默认按 Windows 本机布局；Linux/macOS 用 -D 覆盖为系统/本地路径） ----
set(QT_ROOT      "QT_PATH/" CACHE STRING "Qt install prefix")
if(WIN32)
    set(XHQM_ROOT    "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    set(DEPEND_ROOT  "${CMAKE_CURRENT_SOURCE_DIR}/../../../depend")
    set(XHQM_LIB     "${CMAKE_CURRENT_SOURCE_DIR}/../../../binary/xhqm.lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../binary")
else()
    set(XHQM_ROOT    "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    set(DEPEND_ROOT  "${CMAKE_CURRENT_SOURCE_DIR}/../../../depend")
    set(XHQM_LIB     "${CMAKE_CURRENT_SOURCE_DIR}/../../../binary/libxhqm.a")
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../binary")
endif()
set(XHQM_ROOT ${XHQM_ROOT} CACHE STRING "xhqm engine source root" FORCE)
set(DEPEND_ROOT ${DEPEND_ROOT} CACHE STRING "depend root" FORCE)
set(XHQM_LIB ${XHQM_LIB} CACHE STRING "xhqm library" FORCE)
set(ASSIMP_ROOT  "${DEPEND_ROOT}/assimp")
set(VK_ROOT      "${DEPEND_ROOT}/vulkan")
set(GL_ROOT    "${DEPEND_ROOT}/glew")

# ---- 渲染后端判断参数（与引擎 CMakeLists 同语义；构建引擎与查看器须传相同 -D） ----
set(XHQM_DEVICE "CPU" CACHE STRING "Device: NO, CPU, GPU, NET or AUTO")
set_property(CACHE XHQM_DEVICE PROPERTY STRINGS NO CPU GPU NET AUTO)
set(XHQM_RENDER_BACKEND "AUTO" CACHE STRING "Render API (device=gpu 时生效): AUTO, VULKAN or OPENGL")
set_property(CACHE XHQM_RENDER_BACKEND PROPERTY STRINGS AUTO VULKAN OPENGL)

set(CMAKE_PREFIX_PATH "${QT_ROOT}")
find_package(Qt5 REQUIRED COMPONENTS Gui)

# macOS：brew 动态库搜索路径（须在 add_executable 之前；glslang keg-only 需指向 opt 目录）
if(APPLE)
    link_directories(/opt/homebrew/lib /opt/homebrew/opt/glslang/lib)
endif()

add_executable(RhiViewport
    main.cpp
    RhiViewport.cpp
    RhiViewport.h
    ModelLoader.h
)

if(WIN32)
    target_include_directories(RhiViewport PRIVATE
        ${XHQM_ROOT}
        ${XHQM_ROOT}/fantasy
        ${XHQM_ROOT}/fantasy/backend
        ${XHQM_ROOT}/fantasy/spatial
        ${XHQM_ROOT}/fantasy/lighting
        ${XHQM_ROOT}/fantasy/postfx
        ${XHQM_ROOT}/fantasy/animation
        ${XHQM_ROOT}/fantasy/runtime
        ${GL_ROOT}/include
        ${VK_ROOT}/include
        ${VK_ROOT}/glslang/include
        ${ASSIMP_ROOT}/include
    )
else()
    target_include_directories(RhiViewport PRIVATE
        ${XHQM_ROOT}
        ${XHQM_ROOT}/fantasy
        ${XHQM_ROOT}/fantasy/backend
        ${XHQM_ROOT}/fantasy/spatial
        ${XHQM_ROOT}/fantasy/lighting
        ${XHQM_ROOT}/fantasy/postfx
        ${XHQM_ROOT}/fantasy/animation
        ${XHQM_ROOT}/fantasy/runtime
        ${XHQM_ROOT}/network
        /usr/include
    )
    find_package(assimp REQUIRED)
    # OpenGL/GLEW/Vulkan 依赖不在此处固定查找：按下方与引擎一致的渲染后端判断（_view_gl/_view_vk）
endif()

# Windows：dllimport 语义；Linux 下宏为空，无副作用，保留以对齐代码路径
target_compile_definitions(RhiViewport PRIVATE XHQM_PROGRAM)

if(WIN32)
    # GL-only 构建：链接静态 xhqm.lib（OpenGL 后端），无需 Vulkan
    target_compile_definitions(RhiViewport PRIVATE XHQM_VIEW_HAS_GL=1)
    target_link_libraries(RhiViewport PRIVATE
        Qt5::Gui
        ${XHQM_LIB}
        ${ASSIMP_ROOT}/lib/Release/assimp.lib
        ${GL_ROOT}/lib/glew32.lib
        opengl32
    )
else()
    # 渲染后端判断（与引擎 CMakeLists 的 _render_gl/_render_vk 规定一致）：
    #   device=NO/CPU/NET → 无渲染后端；device=AUTO → 双后端；device=GPU → 按 XHQM_RENDER_BACKEND
    set(_view_gl OFF)
    set(_view_vk OFF)
    if(XHQM_DEVICE STREQUAL "AUTO")
        set(_view_gl ON)
        set(_view_vk ON)
    elseif(XHQM_DEVICE STREQUAL "GPU")
        if(XHQM_RENDER_BACKEND STREQUAL "VULKAN")
            set(_view_vk ON)
        elseif(XHQM_RENDER_BACKEND STREQUAL "OPENGL")
            set(_view_gl ON)
        else()
            set(_view_gl ON)
            set(_view_vk ON)
        endif()
    endif()

    # 把"本构建实际编入了哪些后端"传给源码：--backend auto 才能选对默认后端
    # （此前源码无从得知构建形态，"auto" 硬编码 OpenGL，VK-only 构建下无参启动必然初始化失败直接退出）
    if(_view_vk)
        target_compile_definitions(RhiViewport PRIVATE XHQM_VIEW_HAS_VK=1)
    endif()
    if(_view_gl)
        target_compile_definitions(RhiViewport PRIVATE XHQM_VIEW_HAS_GL=1)
    endif()

    # 公共依赖
    target_link_libraries(RhiViewport PRIVATE
        Qt5::Gui
        ${XHQM_LIB}
        assimp::assimp
        pthread dl
    )

    # GL 后端依赖（与引擎规定一致：OpenGL::GL + GLEW；macOS 无 GLEWConfig，用 find_library）
    if(_view_gl)
        find_package(OpenGL REQUIRED)
        if(APPLE)
            find_library(GLEW_LIBRARY NAMES GLEW PATHS /opt/homebrew/opt/glew/lib REQUIRED)
            target_link_libraries(RhiViewport PRIVATE OpenGL::GL ${GLEW_LIBRARY})
        else()
            find_package(GLEW REQUIRED)
            target_link_libraries(RhiViewport PRIVATE OpenGL::GL GLEW::GLEW)
        endif()
    endif()

    # VK 后端依赖（与引擎规定一致：Vulkan + glslang/SPIRV-Tools 依赖序；macOS 无 xcb）
    if(_view_vk)
        find_package(Vulkan REQUIRED)
        if(APPLE)
            # brew 动态库：SPIRV-Tools 已链接进 /opt/homebrew/lib；glslang 是 keg-only 需指向 opt 目录
            # 引擎静态库按路径链接不传播依赖：Metal 表面辅助（.mm）所需的 ObjC 运行时/框架在此显式链接
            target_link_libraries(RhiViewport PRIVATE Vulkan::Vulkan
                glslang-default-resource-limits SPIRV-Tools-opt SPIRV-Tools SPIRV glslang
                "-framework Cocoa" "-framework QuartzCore" "-lobjc")
        else()
            target_link_libraries(RhiViewport PRIVATE Vulkan::Vulkan xcb
                glslang-default-resource-limits SPIRV-Tools-opt SPIRV-Tools SPIRV glslang)
        endif()
    endif()
endif()
