/* SPDX-License-Identifier: MIT */
/* engine/infinity_hook_pro.hpp —— 引擎内部状态与未公开结构。
 *
 * 外部只应 include hook_engine.hpp；本头仅供引擎实现（infinity_hook_pro.cpp）
 * 和启动层诊断使用。
 */
#ifndef ADBG_INFINITY_HOOK_PRO_H_
#define ADBG_INFINITY_HOOK_PRO_H_

#include <ntddk.h>
#include <evntrace.h>
#include <wmistr.h>

#include "engine/hook_engine.hpp"

namespace adbg_inf {

/* ---- 未公开结构/类型（逆向所得，InfinityHookPro 同源） ---- */

/* CKCL（Circular Kernel Context Logger）属性结构：EVENT_TRACE_PROPERTIES
 * 的尾部多一段；系统 evntrace.h 只给了前半。 */
typedef struct _CKCL_TRACE_PROPERTIES {
    EVENT_TRACE_PROPERTIES Props;      /* 系统定义的标准属性 */
    ULONG64 Unknown[3];
    UNICODE_STRING ProviderName;
} CKCL_TRACE_PROPERTIES;

/* NtTraceControl 的操作码 */
enum ETWP_TRACE_TYPE {
    EtwpStartTrace = 1,
    EtwpStopTrace = 2,
    EtwpQueryTrace = 3,
    EtwpUpdateTrace = 4,
    EtwpFlushTrace = 5
};

typedef __int64 (*HvlGetQpcBiasFn)();
typedef LONG_PTR(FASTCALL *ObfDereferenceObjectFn)(PVOID);
typedef LONG_PTR(NTAPI *ObDereferenceObjectFn)(PVOID);

/* ntoskrnl 导出但头文件没有 */
extern "C" NTSTATUS NTAPI NtTraceControl(
    ULONG FunctionCode, PVOID InBuffer, ULONG InBufferLen,
    PVOID OutBuffer, ULONG OutBufferLen, PULONG ReturnLength);

}  // namespace adbg_inf

#endif /* ADBG_INFINITY_HOOK_PRO_H_ */
