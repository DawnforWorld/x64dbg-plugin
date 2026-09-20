# x64dbg-plugin 插件合集 —— 根构建脚本
#
# 根脚本只做三件事：定最低版本、把 cmake/ 下的辅助模块加进搜索路径、
# 把每个插件目录加进来。插件的构建逻辑各自写在自己目录里。
#
# 新增第二个插件：建目录 + 在下面加一行 add_subdirectory。

cmake_minimum_required(VERSION 3.24)

project(x64dbg-plugins LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_subdirectory(antidebug)
