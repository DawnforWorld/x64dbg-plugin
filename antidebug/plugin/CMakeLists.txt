# antidebug 插件（dp64 / dp32）
#
# 两个架构各编一次（根 preset 控制）：x64 带完整加载器，x86 只带控制面
# （docs/02 §6 决策：iqvw64e 手法仅 x64）。
# 插件 SDK 用合集级共享副本：third_party/x64dbg-pluginsdk
# （源自 x64dbg 项目，经 TitanHide 拷贝取得；所有插件共用，不再各自拷贝，
# 许可注意事项见 docs/06 §6）

set(_src_root "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(_sdk "${CMAKE_SOURCE_DIR}/third_party/x64dbg-pluginsdk")

set(_sources
  "${_src_root}/pluginmain.cpp"
  "${_src_root}/plugin.cpp"
  "${_src_root}/ui/settings_dialog.cpp")

add_library(antidebug_plugin SHARED ${_sources})

set_target_properties(antidebug_plugin PROPERTIES
  OUTPUT_NAME "antidebug")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set_target_properties(antidebug_plugin PROPERTIES SUFFIX ".dp64")
else()
  set_target_properties(antidebug_plugin PROPERTIES SUFFIX ".dp32")
endif()

target_include_directories(antidebug_plugin PRIVATE
  "${_src_root}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
  "${_sdk}")

target_compile_definitions(antidebug_plugin PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS)

target_link_libraries(antidebug_plugin PRIVATE antidebug_client)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_compile_definitions(antidebug_plugin PRIVATE ADBG_HAS_LOADER=1)
  target_link_libraries(antidebug_plugin PRIVATE antidebug_loader)
endif()

target_compile_features(antidebug_plugin PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(antidebug_plugin PRIVATE /W4 /utf-8)
  # 静态 CRT（TitanHide 插件同款做法，避免目标机器缺运行库）
  set_property(TARGET antidebug_plugin PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 插件按 x64dbg 的约定链接 SDK 导入库（桥接 x64dbg.exe / x32dbg.exe 的导出）
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_link_libraries(antidebug_plugin PRIVATE
    "${_sdk}/x64dbg.lib" "${_sdk}/x64bridge.lib")
else()
  target_link_libraries(antidebug_plugin PRIVATE
    "${_sdk}/x32dbg.lib" "${_sdk}/x32bridge.lib")
endif()

target_link_libraries(antidebug_plugin PRIVATE user32 gdi32 comctl32 advapi32 shell32)
