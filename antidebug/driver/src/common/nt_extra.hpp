/* SPDX-License-Identifier: MIT */
/* nt_extra.hpp —— 内核侧的 NT 函数原型与未公开结构定义。
 *
 * 来源：D:\project\vmp\vmp\Main.h 移植（docs/08 §2 语义保留清单）。
 * 约定：信息类参数一律用 ULONG 数值传递（KM 头文件对 SYSTEM_INFORMATION_CLASS
 * 等类型的定义不稳定，数值常量 + 强制转换与 vmp 行为一致，x64 调用约定下
 * 与真实签名的二进制布局相同）。
 */
#ifndef ADBG_NT_EXTRA_H_
#define ADBG_NT_EXTRA_H_

#include <ntifs.h>
#include <ntddk.h>
#include <wdm.h>
#include <ntstatus.h>
#include <ntimage.h>
#include <intrin.h>

/* ---------------- vmp Main.h 移植的常量与结构 ---------------- */

#define OBJ_PROTECT_CLOSE 0x00000001L
#define THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER 0x00000004UL

#ifndef PROCESS_QUERY_INFORMATION
#define PROCESS_QUERY_INFORMATION 0x0400
#endif

/* NtQueryInformationProcess 的信息类（用到的三个 + BasicInformation 排除项） */
#define ADBG_PROCESS_DEBUG_PORT_CLASS 7u
#define ADBG_PROCESS_DEBUG_FLAGS_CLASS 0x1Fu
#define ADBG_PROCESS_DEBUG_OBJECT_CLASS 0x1Eu
#define ADBG_PROCESS_BASIC_INFO_CLASS 0u

/* NtQuerySystemInformation 的信息类 */
#define ADBG_SYSINFO_KERNEL_DEBUGGER 0x23u
#define ADBG_SYSINFO_KERNEL_DEBUGGER_EX 0x95u

/* NtSystemDebugControl 的命令白名单 */
typedef enum _ADBG_SYSDBG_COMMAND {
    AdbgSysDbgGetTriageDump = 29,
    AdbgSysDbgGetLiveKernelDump = 37
} ADBG_SYSDBG_COMMAND;

/* WOW64（32 位程序跑在 64 位系统上）的线程上下文结构，内核头文件没有 */
typedef struct _ADBG_WOW64_FLOATING_SAVE_AREA {
    ULONG ControlWord;
    ULONG StatusWord;
    ULONG TagWord;
    ULONG ErrorOffset;
    ULONG ErrorSelector;
    ULONG DataOffset;
    ULONG DataSelector;
    UCHAR RegisterArea[80];
    ULONG Cr0NpxState;
} ADBG_WOW64_FLOATING_SAVE_AREA, *PADBG_WOW64_FLOATING_SAVE_AREA;

typedef struct _ADBG_WOW64_CONTEXT {
    ULONG ContextFlags;
    ULONG Dr0;
    ULONG Dr1;
    ULONG Dr2;
    ULONG Dr3;
    ULONG Dr6;
    ULONG Dr7;
    ADBG_WOW64_FLOATING_SAVE_AREA FloatSave;
    ULONG SegGs;
    ULONG SegFs;
    ULONG SegEs;
    ULONG SegDs;
    ULONG Edi;
    ULONG Esi;
    ULONG Ebx;
    ULONG Edx;
    ULONG Ecx;
    ULONG Eax;
    ULONG Ebp;
    ULONG Eip;
    ULONG SegCs;
    ULONG EFlags;
    ULONG Esp;
    ULONG SegSs;
    UCHAR ExtendedRegisters[512];
} ADBG_WOW64_CONTEXT;
typedef ADBG_WOW64_CONTEXT *PADBG_WOW64_CONTEXT;

typedef struct _ADBG_PS_ATTRIBUTE {
    ULONG_PTR Attribute;
    SIZE_T Size;
    union {
        ULONG_PTR Value;
        PVOID ValuePtr;
    };
    PSIZE_T ReturnLength;
} ADBG_PS_ATTRIBUTE, *PADBG_PS_ATTRIBUTE;

typedef struct _ADBG_PS_ATTRIBUTE_LIST {
    SIZE_T TotalLength;
    ADBG_PS_ATTRIBUTE Attributes[1];
} ADBG_PS_ATTRIBUTE_LIST, *PADBG_PS_ATTRIBUTE_LIST;

typedef PVOID ADBG_USER_THREAD_START_ROUTINE;

/* ---------------- 10 个被拦函数的原型（信息类参数统一 ULONG） --------- */

typedef NTSTATUS(NTAPI *ADBG_FN_NtQueryInformationProcess)(
    HANDLE ProcessHandle, ULONG InformationClass, PVOID ProcessInformation,
    ULONG ProcessInformationLength, PULONG ReturnLength);

typedef NTSTATUS(NTAPI *ADBG_FN_NtSetInformationThread)(
    HANDLE ThreadHandle, ULONG ThreadInformationClass, PVOID ThreadInformation,
    ULONG ThreadInformationLength);

typedef NTSTATUS(NTAPI *ADBG_FN_NtClose)(HANDLE Handle);

typedef NTSTATUS(NTAPI *ADBG_FN_NtQuerySystemInformation)(
    ULONG SystemInformationClass, PVOID SystemInformation,
    ULONG SystemInformationLength, PULONG ReturnLength);

typedef NTSTATUS(NTAPI *ADBG_FN_NtQueryInformationThread)(
    HANDLE ThreadHandle, ULONG ThreadInformationClass, PVOID ThreadInformation,
    ULONG ThreadInformationLength, PULONG ReturnLength);

typedef NTSTATUS(NTAPI *ADBG_FN_NtGetContextThread)(
    HANDLE ThreadHandle, PCONTEXT Context);

typedef NTSTATUS(NTAPI *ADBG_FN_NtSetContextThread)(
    HANDLE ThreadHandle, PCONTEXT Context);

typedef NTSTATUS(NTAPI *ADBG_FN_NtSystemDebugControl)(
    ULONG Command, PVOID InputBuffer, ULONG InputBufferLength,
    PVOID OutputBuffer, ULONG OutputBufferLength, PULONG ReturnLength);

typedef NTSTATUS(NTAPI *ADBG_FN_NtDuplicateObject)(
    HANDLE SourceProcessHandle, HANDLE SourceHandle, HANDLE TargetProcessHandle,
    PHANDLE TargetHandle, ACCESS_MASK DesiredAccess, ULONG HandleAttributes,
    ULONG Options);

typedef NTSTATUS(NTAPI *ADBG_FN_NtCreateThreadEx)(
    PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, HANDLE ProcessHandle,
    ADBG_USER_THREAD_START_ROUTINE StartRoutine, PVOID Argument,
    ULONG CreateFlags, SIZE_T ZeroBits, SIZE_T StackSize,
    SIZE_T MaximumStackSize, PADBG_PS_ATTRIBUTE_LIST AttributeList);

/* THREADINFOCLASS 里用到的值（数值，避免依赖头文件枚举完整度） */
#define ADBG_THREAD_HIDE_FROM_DEBUGGER 0x11u
#define ADBG_THREAD_WOW64_CONTEXT 0x2Au

/* ---------------- 头文件里没有、ntoskrnl 导出的函数 ------------------ */

/* 未公开但已导出：取进程的调试端口（vmp 同款声明） */
extern "C" NTKERNELAPI PVOID NTAPI PsGetProcessDebugPort(PEPROCESS Process);

/* 未公开但已导出：查句柄的审计标志（NtClose 重实现用） */
extern "C" NTKERNELAPI NTSTATUS NTAPI ObQueryObjectAuditingByHandle(
    HANDLE Handle, PBOOLEAN GenerateOnClose);

#endif /* ADBG_NT_EXTRA_H_ */
