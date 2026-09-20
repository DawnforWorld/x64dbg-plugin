# antidebug_client —— 控制面客户端库（双架构）
#
# 封装与 \\.\AntidbgCtrl 的全部对话（docs/03 §4-§6）：查询/会话/名单/开关/停止，
# 以及会话密钥的注册表交接。dp32（x32dbg 插件）只有这一个内核交互通道，
# 所以它独立成库、两个架构都编，不依赖加载器。

add_library(antidebug_client STATIC
  control_client.cpp)

target_include_directories(antidebug_client PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include")

target_compile_features(antidebug_client PUBLIC cxx_std_17)

target_link_libraries(antidebug_client PUBLIC advapi32)

if(MSVC)
  target_compile_options(antidebug_client PRIVATE /W4 /utf-8)
endif()

# 静态 CRT：与 antictl/插件一致，产物不依赖目标机的运行库
set_property(TARGET antidebug_client PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

