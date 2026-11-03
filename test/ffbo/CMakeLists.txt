cmake_minimum_required(VERSION 3.15)
project(digital_fruit_fly)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()

set(XHQM_ROOT    "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(DEPEND_ROOT  "${CMAKE_CURRENT_SOURCE_DIR}/../../../depend")
set(XHQM_LIB     "${CMAKE_CURRENT_SOURCE_DIR}/../../../binary/xhqm.lib")
set(QT_ROOT      "C:/Qt/5.15.2/msvc2019_64")
set(MUJOCO_ROOT  "D:/WorkSpace/_research/digital-fruit-fly/depend/mujoco")

set(CMAKE_PREFIX_PATH "${QT_ROOT}")
find_package(Qt5 REQUIRED COMPONENTS Gui)

set(FLY_INCLUDES
    ${XHQM_ROOT}
    ${XHQM_ROOT}/fantasy
    ${XHQM_ROOT}/fantasy/backend
    ${XHQM_ROOT}/fantasy/spatial
    ${XHQM_ROOT}/fantasy/lighting
    ${XHQM_ROOT}/fantasy/postfx
    ${XHQM_ROOT}/fantasy/animation
    ${XHQM_ROOT}/fantasy/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/../cellsim
    ${DEPEND_ROOT}/glew/include
    ${MUJOCO_ROOT}/include
)

set(FLY_LIBS
    Qt5::Gui
    ${XHQM_LIB}
    ${DEPEND_ROOT}/glew/lib/glew32.lib
    opengl32
    ${MUJOCO_ROOT}/mujoco.lib
)

# ══════════════════ 当前构建目标 ══════════════════
#
# 【已归档】旧的全通路程序 fly_brain_manc_view 已移到 archive/，不再构建。
#   归档原因（两条都是结构性的，不是调参问题）：
#     1. 它用 FAFB 大脑 + MANC 神经索靠「类型名」拼桥接表，而 MaleCNS v1.0 是
#        单一整合数据集（脑+腹神经索一体），桥接整层可以去掉。
#     2. 它的 LIF 是 I += w*s 且权重全正 —— 等于把 44.8% 的抑制性突触当兴奋用。
#        实测在 MaleCNS 上会让 99.8% 的神经元以 48Hz 饱和，信号退化成直流。
#   归档目录里同时保留了它的启动脚本，仅供溯源，不要直接跑。

# MaleCNS v1.0 网络验证程序（只有神经网络，不含身体与渲染）。
# 先确认「突触符号 + LIF 工作点」做对了，再往上接躯体。
add_executable(mcns_net mcns_net.cpp)
target_include_directories(mcns_net PRIVATE ${FLY_INCLUDES})
target_compile_definitions(mcns_net PRIVATE XHQM_PROGRAM)
target_link_libraries(mcns_net PRIVATE ${FLY_LIBS})

# 说明：import_connections.cpp / fly_drive_diag.cpp / mujoco_diag.cpp 是命令行
# 手动编译的一次性调查工具（见 README），故意不进构建。

# MaleCNS 全通路（网络 + FlyBody 身体 + fantasy 渲染 + 复眼 raycast）
add_executable(fly_mcns_view fly_mcns_view.cpp RhiViewport.cpp RhiViewport.h)
target_include_directories(fly_mcns_view PRIVATE ${FLY_INCLUDES})
target_compile_definitions(fly_mcns_view PRIVATE XHQM_PROGRAM XHQM_VIEW_HAS_GL=1)
target_link_libraries(fly_mcns_view PRIVATE ${FLY_LIBS})
