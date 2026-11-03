/*
 * xhqm 引擎聚合头
 *
 * 用法：
 *   #include "xhqm.h"                 → 仅基础（head.h：类型/宏/json）
 *   #define USING_XHQM 再 include     → 全量模块
 *   或 #define XHQM_USE_XXX 再 include → 按需模块
 *
 * 模块开关（对应 CMake 的 XHQM_BUILD_*）：
 *   XHQM_USE_ALGEBRA    代数（基础类型/张量/几何/分析）
 *   XHQM_USE_LATTICE    图结构（链/树/网）
 *   XHQM_USE_METACLASS  元类（block/buffer/cache/函数反射）
 *   XHQM_USE_NETWORK    网络（socket/加密/记忆 astral/感知 nebula）
 *   XHQM_USE_SOFTWARE   软件（信号槽/事件/数据/消息/命令/系统/设计模式）
 *   XHQM_USE_FANTASY    渲染引擎
 *   XHQM_USE_HARDWARE   硬件（设备选择）
 *
 * 注：宏名带 USE_ 前缀，避免与各头文件 include guard（如 XHQM_FANTASY / XHQM_HARDWARE）冲突。
 */

#ifdef USING_XHQM
#   ifndef XHQM_USE_ALGEBRA
#       define XHQM_USE_ALGEBRA
#       define XHQM_USE_LATTICE
#       define XHQM_USE_METACLASS
#       define XHQM_USE_NETWORK
#       define XHQM_USE_SOFTWARE
#       define XHQM_USE_FANTASY
#       define XHQM_USE_HARDWARE
#   endif
#endif

#include "head.h"

// ---- 硬件：设备选择（no/cpu/gpu/net/auto），tensor 后端依赖，置于最前 ----
#ifdef XHQM_USE_HARDWARE
#   include "hardware/hardware.h"
#endif

// ---- 代数：基础类型 + 张量 + 几何 + 分析 ----
#ifdef XHQM_USE_ALGEBRA
#   include "xhqm/algebra"
#   include "xhqm/tensor"
#   include "xhqm/geometry"
#   include "xhqm/analysis"
#endif

// ---- 图结构：链 / 树 / 网 ----
#ifdef XHQM_USE_LATTICE
#   include "xhqm/lattice"
#endif

// ---- 元类：block / buffer / cache / 函数反射 / 组件存储 ----
#ifdef XHQM_USE_METACLASS
#   include "xhqm/block"
#   include "xhqm/buffer"
#   include "xhqm/cache"
#   include "xhqm/function"
#   include "xhqm/tuple_values"
#endif

// ---- 网络：socket / 加密 / 记忆 astral / 感知 nebula ----
#ifdef XHQM_USE_NETWORK
#   include "xhqm/network"
#   include "xhqm/cipher"
#   include "xhqm/astral"
#   include "xhqm/nebula"
#endif

// ---- 软件：信号槽 / 事件 / 数据 / 消息 / 命令 / 系统 / 设计模式 ----
#ifdef XHQM_USE_SOFTWARE
#   include "xhqm/software"
#   include "xhqm/signal"
#   include "xhqm/threads"
#   include "xhqm/toolset"
#endif

// ---- 渲染引擎 ----
#ifdef XHQM_USE_FANTASY
#   include "xhqm/fantasy"
#endif
