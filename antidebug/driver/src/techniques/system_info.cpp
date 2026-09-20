/* SPDX-License-Identifier: MIT */
/* techniques/system_info.cpp —— NtQuerySystemInformation /
 * NtSystemDebugControl。
 *
 * 来源：vmp Main.cpp 同名函数移植（docs/08 §2）。要点：
 * KernelDebuggerInformation(0x23)→{FALSE,TRUE}；Ex(0x95)→三 FALSE；
 * SystemDebugControl 只放行 SysDbgGetTriageDump(29)/GetLiveKernelDump(37)。
 */
#include "common/nt_extra.hpp"
#include "framework/policy.hpp"
#include "framework/registry.hpp"
#include "framework/technique.hpp"

extern "C" NTSTATUS NTAPI AdbgHkNtQuerySystemInformation(
    ULONG SystemInformationClass, PVOID SystemInformation,
    ULONG SystemInformationLength, PULONG ReturnLength) {
    auto original = (ADBG_FN_NtQuerySystemInformation)AdbgRegistryOriginal(
        kHookNtQuerySystemInformation);
    KPROCESSOR_MODE previous_mode = ExGetPreviousMode();

    if (previous_mode == KernelMode)
        return original(SystemInformationClass, SystemInformation,
                        SystemInformationLength, ReturnLength);

    NTSTATUS ret = original(SystemInformationClass, SystemInformation,
                            SystemInformationLength, ReturnLength);
    if (!NT_SUCCESS(ret) || SystemInformation == nullptr)
        return ret;

    if (!AdbgPolicyProbe(ADBG_TECH_KERNEL_DEBUGGER))
        return ret;

    if (SystemInformationClass == ADBG_SYSINFO_KERNEL_DEBUGGER) {
        typedef struct _SYS_KD_INFO {
            BOOLEAN DebuggerEnabled;
            BOOLEAN DebuggerNotPresent;
        } SYS_KD_INFO;

        __try {
            ULONG temp_return_length = 0;
            if (ReturnLength != nullptr) {
                ProbeForWrite(ReturnLength, sizeof(ULONG), 1);
                temp_return_length = *ReturnLength;
            }
            SYS_KD_INFO *info = (SYS_KD_INFO *)SystemInformation;
            info->DebuggerEnabled = FALSE;
            info->DebuggerNotPresent = TRUE;
            if (ReturnLength != nullptr)
                *ReturnLength = temp_return_length;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ret = GetExceptionCode();
        }
    } else if (SystemInformationClass == ADBG_SYSINFO_KERNEL_DEBUGGER_EX) {
        typedef struct _SYS_KD_INFO_EX {
            BOOLEAN DebuggerAllowed;
            BOOLEAN DebuggerEnabled;
            BOOLEAN DebuggerPresent;
        } SYS_KD_INFO_EX;

        __try {
            ULONG temp_return_length = 0;
            if (ReturnLength != nullptr) {
                ProbeForWrite(ReturnLength, sizeof(ULONG), 1);
                temp_return_length = *ReturnLength;
            }
            SYS_KD_INFO_EX *info = (SYS_KD_INFO_EX *)SystemInformation;
            info->DebuggerAllowed = FALSE;
            info->DebuggerEnabled = FALSE;
            info->DebuggerPresent = FALSE;
            if (ReturnLength != nullptr)
                *ReturnLength = temp_return_length;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ret = GetExceptionCode();
        }
    }

    return ret;
}

extern "C" NTSTATUS NTAPI AdbgHkNtSystemDebugControl(
    ULONG Command, PVOID InputBuffer, ULONG InputBufferLength,
    PVOID OutputBuffer, ULONG OutputBufferLength, PULONG ReturnLength) {
    auto original = (ADBG_FN_NtSystemDebugControl)AdbgRegistryOriginal(
        kHookNtSystemDebugControl);
    KPROCESSOR_MODE previous_mode = ExGetPreviousMode();

    if (previous_mode == KernelMode)
        return original(Command, InputBuffer, InputBufferLength, OutputBuffer,
                        OutputBufferLength, ReturnLength);

    if (!AdbgPolicyProbe(ADBG_TECH_SYSTEM_DEBUG_CONTROL))
        return original(Command, InputBuffer, InputBufferLength, OutputBuffer,
                        OutputBufferLength, ReturnLength);

    /* 只放行转储类命令（29/37），其余报告"没有内核调试器" */
    if (Command != (ULONG)AdbgSysDbgGetTriageDump &&
        Command != (ULONG)AdbgSysDbgGetLiveKernelDump) {
        return STATUS_DEBUGGER_INACTIVE;
    }

    return original(Command, InputBuffer, InputBufferLength, OutputBuffer,
                    OutputBufferLength, ReturnLength);
}
