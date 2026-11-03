# cellsim —— 单细胞模拟器子项目
#
# 目的：把「一个细胞在什么条件下会做什么事」写成显式规格 + 可测行为，
#       再作为可替换的细胞模型插进果蝇系统（test/ffbo）。
#
# 只依赖标准库 —— 不引入任何第三方依赖（与 xhqm 的约定一致）。
cmake_minimum_required(VERSION 3.15)
project(cellsim)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_executable(cellsim_bench cell_bench.cpp cellsim.h)
