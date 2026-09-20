# antidebug_loader —— 用户态加载器静态库（x64 only）
#
# 三层结构（docs/02 §3）：
#   spi/            三个可换接口（原语提供者 / 镜像映射器 / 入口调用器）
#   kdm/            kdmapper 移植（intel_driver + service + kdmapper + utils）
#   driver_loader   高层编排：加载窗口协议 + 密钥交接
#
# 两个"生成头"依赖（docs/05 §6 字节内嵌链）：
#   antidebug_driver_resource.hpp  由 antidebug.sys 构建产物转换而来
#   intel_driver_resource.hpp      由 ADBG_IQVW64E_HPP/PATH 提供，缺省用空桩
#                                  （空桩在 kdm/ 里，gen 目录排在前面把它遮住）

set(_src_root "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(antidebug_loader STATIC
  "${_src_root}/driver_loader.cpp"
  "${_src_root}/kdm/intel_driver.cpp"
  "${_src_root}/kdm/service.cpp"
  "${_src_root}/kdm/kdmapper.cpp"
  "${_src_root}/kdm/utils.cpp"
  "${_src_root}/kdm/portable_executable.cpp")

target_include_directories(antidebug_loader PUBLIC
  "${_src_root}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
  "${ADBG_GEN_DIR}")  # 生成头优先：遮住 kdm/ 里的 iqvw64e 空桩

target_compile_features(antidebug_loader PUBLIC cxx_std_17)

# 池标签改成自己的指纹（kdmapper 上游写死 'BwtE'，是已知的映射器特征）
target_compile_definitions(antidebug_loader PRIVATE ADBG_POOL_TAG=0x67626441)  # 'Adbg'
# 输出静音开关：插件里不想让 kdmapper 往 stdout 打日志
target_compile_definitions(antidebug_loader PUBLIC ADBG_KDM_QUIET)

# ---- 生成头 1：iqvw64e 字节（两种来源二选一，都没有就跳过，用空桩） ----
set(_iqvw64e_gen "${ADBG_GEN_DIR}/intel_driver_resource.hpp")
if(ADBG_IQVW64E_HPP)
  add_custom_command(
    OUTPUT "${_iqvw64e_gen}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ADBG_IQVW64E_HPP}" "${_iqvw64e_gen}"
    DEPENDS "${ADBG_IQVW64E_HPP}"
    COMMENT "复制 iqvw64e 资源头")
elseif(ADBG_IQVW64E_PATH)
  add_custom_command(
    OUTPUT "${_iqvw64e_gen}"
    COMMAND ${CMAKE_COMMAND}
      -DINPUT="${ADBG_IQVW64E_PATH}"
      -DOUTPUT="${_iqvw64e_gen}"
      -DNAMESPACE=intel_driver_resource
      -DARRAY=driver
      -DSIZEVAR=driver_size
      -P "${CMAKE_SOURCE_DIR}/cmake/BinToHeader.cmake"
    DEPENDS "${ADBG_IQVW64E_PATH}"
    COMMENT "转换 iqvw64e.sys -> 资源头")
endif()

if(ADBG_IQVW64E_HPP OR ADBG_IQVW64E_PATH)
  add_custom_target(adbg_gen_iqvw64e DEPENDS "${_iqvw64e_gen}")
  add_dependencies(antidebug_loader adbg_gen_iqvw64e)
else()
  message(STATUS "antidebug: 未提供 iqvw64e 字节（-DADBG_IQVW64E_HPP / _PATH），使用空桩；"
                 "加载器会在运行时报 \"resource not generated\"（docs/05 §3）")
endif()

# ---- 生成头 2：antidebug.sys 字节（依赖驱动目标，构建时自动转换） ----
set(_driver_gen "${ADBG_GEN_DIR}/antidebug_driver_resource.hpp")
add_custom_command(
  OUTPUT "${_driver_gen}"
  COMMAND ${CMAKE_COMMAND}
    -DINPUT="$<TARGET_FILE:antidebug_driver>"
    -DOUTPUT="${_driver_gen}"
    -DNAMESPACE=antidebug_driver_resource
    -DARRAY=driver
    -DSIZEVAR=driver_size
    -P "${CMAKE_SOURCE_DIR}/cmake/BinToHeader.cmake"
  DEPENDS antidebug_driver
  COMMENT "转换 antidebug.sys -> 资源头（内嵌进加载器）")
add_custom_target(adbg_gen_driver_header DEPENDS "${_driver_gen}")
add_dependencies(antidebug_loader adbg_gen_driver_header)

target_link_libraries(antidebug_loader PUBLIC advapi32)
