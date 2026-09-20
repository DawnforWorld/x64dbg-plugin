/* SPDX-License-Identifier: MIT */
/* engine/infinity_hook_pro.hpp —— 引擎内部状态与未公开结构。
 *
 * 外部只应 include hook_engine.hpp；本头仅供引擎实现（infinity_hook_pro.cpp）
 * 使用。
 *
 * 注意：shared/evntrace.h 的 EVENT_TRACE_PROPERTIES 在内核态被
 * _EVNTRACE_KERNEL_MODE 排除，所以这里按微软文档布局自定义
 * （InfinityHookPro 原工程同理）；WNODE_HEADER 来自 wmistr.h（KM 可用）。
 */
#ifndef ADBG_INFINITY_HOOK_PRO_H_
#define ADBG_INFINITY_HOOK_PRO_H_

#include <ntddk.h>
#include <wmistr.h>

#include "engine/hook_engine.hpp"

namespace adbg_inf {

/* ---- 未公开结构/类型（逆向所得，InfinityHookPro 同源） ---- */

/* ETW 跟踪属性（内核态版，字段按微软文档排列） */
typedef struct _ADBG_EVENT_TRACE_PROPERTIES {
    WNODE_HEADER Wnode;
    ULONG BufferSize;
    ULONG MinimumBuffers;
    ULONG MaximumBuffers;
    ULONG MaximumFileSize;
    ULONG LogFileMode;
    ULONG FlushTimer;
    ULONG EnableFlags;
    union {
        LONG AgeLimit;
        LONG FlushThreshold;
    };
    ULONG NumberOfBuffers;
    ULONG FreeBuffers;
    ULONG EventsLost;
    ULONG BuffersWritten;
    ULONG LogBuffersLost;
    ULONG RealTimeBuffersLost;
    HANDLE LoggerThreadId;
    ULONG LogFileNameOffset;
    ULONG LoggerNameOffset;
} ADBG_EVENT_TRACE_PROPERTIES;

/* CKCL（Circular Kernel Context Logger）属性结构：上面结构的尾部
 * 多一段（逆向所得的未公开扩展）。 */
typedef struct _ADBG_CKCL_TRACE_PROPERTIES {
    ADBG_EVENT_TRACE_PROPERTIES Props;
    ULONG64 Unknown[3];
    UNICODE_STRING ProviderName;
} ADBG_CKCL_TRACE_PROPERTIES;

/* LogFileMode / EnableFlags 用到的值（evntrace.h 的宏在 KM 分支外） */
#define ADBG_EVENT_TRACE_BUFFERING_MODE 0x00000400u
#define ADBG_EVENT_TRACE_FLAG_SYSTEMCALL 0x00000080u

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
