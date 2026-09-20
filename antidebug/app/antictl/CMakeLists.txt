# antictl —— 命令行控制/冒烟工具（x64 only）
#
# 子命令：load / unload / status / hide / unhide / tech / smoke / version
# 协议见 docs/03 §8.5。加载类命令需要管理员，由用户手动执行。

add_executable(antictl main.cpp)

target_include_directories(antictl PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include")

target_compile_features(antictl PRIVATE cxx_std_17)

target_link_libraries(antictl PRIVATE antidebug_loader advapi32)

set_target_properties(antictl PROPERTIES OUTPUT_NAME "antictl")

if(MSVC)
  target_compile_options(antictl PRIVATE /W4 /utf-8)
  # 静态链接 CRT：工具可以单独拷走用
  set_property(TARGET antictl PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
