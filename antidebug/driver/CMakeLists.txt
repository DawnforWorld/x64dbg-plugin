# antidebug.sys —— 手动映射的内核驱动镜像
#
# 不走 SCM/WDK 工程系统加载：它由 loader/ 里的 kdmapper 移植代码手动映射
# （docs/02 §4.1）。链接仍然用 WDK 的导入库，导入表在映射时由映射器解析。
# 配方继承自 vtdbg 已验证的做法（docs/05 §6）：
#   - /kernel 内核方言；/GS- 无栈保护（映射器不支持）；/GR- 无 RTTI；
#     /EHs-c- 无 C++ 异常（__try/__except 的 SEH 不受影响）
#   - /NODEFAULTLIB 无 CRT：不用 new/delete/std::，全局变量必须零初始化
#   - 必须保留 .reloc（映射器按随机池基址重定位，固定链接直接致命）

find_package(WDK REQUIRED)

add_library(antidebug_driver MODULE
  src/entry.cpp
  # 平台层：OS 差异吸收（模块枚举/特征码/KVAS 影子解析）
  src/platform/utils.cpp
  # 引擎层：InfinityHookPro 移植 + HDE64 反汇编引擎
  src/engine/infinity_hook_pro.cpp
  src/platform/hde/hde64.cpp
  # 框架层：技术注册表 + 策略
  src/framework/registry.cpp
  src/framework/policy.cpp
  # 技术层：vmp 移植的 10 个处理函数（docs/08 §2）
  src/techniques/process_info.cpp        # NtQueryInformationProcess 3 项
  src/techniques/thread_info.cpp         # NtSet/NtQueryInformationThread 4 项
  src/techniques/close_and_duplicate.cpp # NtClose + NtDuplicateObject
  src/techniques/system_info.cpp         # NtQuerySystemInformation + NtSystemDebugControl
  src/techniques/context_thread.cpp      # NtGet/NtSetContextThread
  src/techniques/create_thread.cpp       # NtCreateThreadEx
  # 控制层：设备对象 + IOCTL 分发
  src/control/device.cpp
)

set_target_properties(antidebug_driver PROPERTIES
  OUTPUT_NAME "antidebug"
  SUFFIX ".sys")

target_include_directories(antidebug_driver PRIVATE
  "${WDK_INCLUDE_DIR}/km"
  "${WDK_INCLUDE_DIR}/shared"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include")

target_compile_definitions(antidebug_driver PRIVATE
  _AMD64_ _WIN64
  NTDDI_VERSION=0x0A00000B _WIN32_WINNT=0x0A00 WINVER=0x0A00)

target_compile_features(antidebug_driver PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(antidebug_driver PRIVATE /kernel /GS- /GR- /EHs-c- /W4 /utf-8)
  target_link_options(antidebug_driver PRIVATE
    /SUBSYSTEM:NATIVE
    /ENTRY:CustomDriverEntry
    /NODEFAULTLIB
    /MACHINE:X64
    /DRIVER
    /MANIFEST:NO
    /INCREMENTAL:NO
    /DYNAMICBASE /FIXED:NO)  # 保留 .reloc：映射器要随机基址重定位
endif()

target_link_libraries(antidebug_driver PRIVATE
  "${WDK_LIB_DIR}/ntoskrnl.lib"
  "${WDK_LIB_DIR}/hal.lib"
  "${WDK_LIB_DIR}/wdmsec.lib")  # IoCreateDeviceSecure：给控制设备上管理员 DACL
