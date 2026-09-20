/* SPDX-License-Identifier: MIT */
/* common/kernel_compat.cpp —— 无 CRT 镜像链接 wdmsec.lib 的兼容层。
 *
 * wdmsec.lib（IoCreateDeviceSecure 所在）的目标文件是带 /GS 栈保护编译
 * 的，引用 CRT 提供的三个符号；我们的镜像 /NODEFAULTLIB，链接不过。
 * 这里提供等价物：
 *   - __security_cookie：静态非常量初值（没有 CRT 初始化可依赖）；
 *   - __security_check_cookie：空实现——退化的只是 wdmsec 内部目标文件
 *     的栈帧校验，本镜像自身 /GS- 本来就没有栈保护（研究工具取舍，
 *     docs/06 §4 有记录）；
 *   - __report_rangecheckfailure：直接蓝屏——能走到这里说明内存已经
 *     越界写，静默继续比蓝屏危险。
 */
#include <ntddk.h>

extern "C" UINT_PTR __security_cookie = 0x9E3779B97F4A7C15ull;

extern "C" void __security_check_cookie(UINT_PTR) {}

extern "C" void __report_rangecheckfailure() {
    KeBugCheckEx(MANUALLY_INITIATED_CRASH, 0, 0, 0, 0);
}
