# antidebug —— x64dbg 反反调试插件
#
# 编排四个子目标（docs/02 §5）：
#   driver/      antidebug.sys        内核驱动（仅 x64）
#   loader/      antidebug_loader     用户态加载器静态库（仅 x64）
#   app/antictl/ antictl.exe          命令行工具（仅 x64）
#   plugin/      antidebug.dp64/.dp32 x64dbg 插件（两个架构都编）
#
# x86 预设只编插件（dp32 不带加载器，docs/02 §6 有决策说明）。

set(_platform "${CMAKE_GENERATOR_PLATFORM}")
if(NOT _platform)
  set(_platform "${CMAKE_VS_PLATFORM_NAME_DEFAULT}")  # VS 默认平台（x64 主机上是 x64）
endif()

if(_platform STREQUAL "Win32")
  set(_driver_default OFF)
else()
  set(_driver_default ON)
endif()

option(ADBG_BUILD_DRIVER "构建驱动/加载器/antictl（仅 x64 平台可用）" ${_driver_default})

# iqvw64e.sys 字节的两种提供方式（docs/05 §3）。都不给则用空桩。
set(ADBG_IQVW64E_HPP "" CACHE PATH "已生成的 iqvw64e 资源头文件（推荐复用 vtdbg 的）")
set(ADBG_IQVW64E_PATH "" CACHE PATH "iqvw64e.sys 原始二进制（构建时自动转成头文件）")

set(ADBG_GEN_DIR "${CMAKE_BINARY_DIR}/gen" CACHE INTERNAL "生成头文件目录")
set(ADBG_ABI_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/adbg_abi.h")

# 控制面客户端：双架构都要（dp32 只有它）
add_subdirectory(client)

if(ADBG_BUILD_DRIVER)
  add_subdirectory(driver)
  add_subdirectory(loader)
  add_subdirectory(app/antictl)
endif()

add_subdirectory(plugin)
