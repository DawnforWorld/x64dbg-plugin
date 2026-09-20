/* SPDX-License-Identifier: MIT */
/* techniques/thread_info.cpp —— NtSet/NtQueryInformationThread 的 4 项技术。
 *
 * 来源：vmp Main.cpp HkNtSetInformationThread / HkNtQueryInformationThread
 * 移植（docs/08 §2）。要点：ThreadHideFromDebugger 设置=吞调用（校验
 * 句柄后直接返回成功）；查询=改 TRUE 演全套；ThreadWow64Context=剥
 * 0x10 位包住原函数 + 需要时清 WOW64 调试寄存器。
 */
#include "common/nt_extra.hpp"
#include "framework/policy.hpp"
#include "framework/registry.hpp"
#include "framework/technique.hpp"

extern "C" NTSTATUS NTAPI AdbgHkNtSetInformationThread(
    HANDLE ThreadHandle, ULONG ThreadInformationClass, PVOID ThreadInformation,
    ULONG ThreadInformationLength) {
    auto original = (ADBG_FN_NtSetInformationThread)AdbgRegistryOriginal(
        kHookNtSetInformationThread);
    KPROCESSOR_MODE previous_mode = ExGetPreviousMode();

    if (previous_mode == KernelMode)
        return original(ThreadHandle, ThreadInformationClass,
                        ThreadInformation, ThreadInformationLength);

    /* ThreadHideFromDebugger：目标想让线程对调试器隐身——校验句柄后
     * 吞掉这个调用：目标以为成功了，调试器照样收得到事件 */
    if (ThreadInformationClass == ADBG_THREAD_HIDE_FROM_DEBUGGER &&
        ThreadInformationLength == 0) {
        if (!AdbgPolicyProbe(ADBG_TECH_HIDE_SET))
            return original(ThreadHandle, ThreadInformationClass,
                            ThreadInformation, ThreadInformationLength);

        PETHREAD thread = nullptr;
        NTSTATUS status = ObReferenceObjectByHandle(
            ThreadHandle, THREAD_SET_INFORMATION, *PsThreadType, previous_mode,
            (PVOID *)&thread, nullptr);
        if (NT_SUCCESS(status))
            ObDereferenceObject(thread);
        return status;
    }

    /* ThreadWow64Context：32 位目标想设置自己的上下文（含调试寄存器）——
     * 把 CONTEXT_DEBUG_REGISTERS(0x10) 位剥掉再调，调完还原 */
    if (ThreadInformationClass == ADBG_THREAD_WOW64_CONTEXT &&
        ThreadInformation != nullptr &&
        ThreadInformationLength == sizeof(ADBG_WOW64_CONTEXT)) {
        if (!AdbgPolicyProbe(ADBG_TECH_WOW64_DR_REGISTERS))
            return original(ThreadHandle, ThreadInformationClass,
                            ThreadInformation, ThreadInformationLength);

        PADBG_WOW64_CONTEXT context = (PADBG_WOW64_CONTEXT)ThreadInformation;
        ULONG original_flags = 0;

        __try {
            ProbeForWrite(&context->ContextFlags, sizeof(ULONG), 1);
            original_flags = context->ContextFlags;
            context->ContextFlags = original_flags & ~(ULONG)0x10;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        NTSTATUS status = original(ThreadHandle, ThreadInformationClass,
                                   ThreadInformation, ThreadInformationLength);

        __try {
            ProbeForWrite(&context->ContextFlags, sizeof(ULONG), 1);
            context->ContextFlags = original_flags;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        return status;
    }

    return original(ThreadHandle, ThreadInformationClass, ThreadInformation,
                    ThreadInformationLength);
}

extern "C" NTSTATUS NTAPI AdbgHkNtQueryInformationThread(
    HANDLE ThreadHandle, ULONG ThreadInformationClass, PVOID ThreadInformation,
    ULONG ThreadInformationLength, PULONG ReturnLength) {
    auto original = (ADBG_FN_NtQueryInformationThread)AdbgRegistryOriginal(
        kHookNtQueryInformationThread);
    KPROCESSOR_MODE previous_mode = ExGetPreviousMode();

    if (previous_mode == KernelMode)
        return original(ThreadHandle, ThreadInformationClass,
                        ThreadInformation, ThreadInformationLength,
                        ReturnLength);

    /* ThreadWow64Context：32 位目标读上下文——先剥 0x10 位调原函数，
     * 若对方原本要了调试寄存器，把 WOW64 的 Dr0-3/Dr6/Dr7 清零 */
    if (ThreadInformationClass == ADBG_THREAD_WOW64_CONTEXT &&
        ThreadInformation != nullptr &&
        ThreadInformationLength == sizeof(ADBG_WOW64_CONTEXT)) {
        if (!AdbgPolicyProbe(ADBG_TECH_WOW64_DR_REGISTERS))
            return original(ThreadHandle, ThreadInformationClass,
                            ThreadInformation, ThreadInformationLength,
                            ReturnLength);

        PADBG_WOW64_CONTEXT context = (PADBG_WOW64_CONTEXT)ThreadInformation;
        ULONG original_flags = 0;
        BOOLEAN debug_registers_requested = FALSE;

        __try {
            ProbeForWrite(&context->ContextFlags, sizeof(ULONG), 1);
            original_flags = context->ContextFlags;
            context->ContextFlags = original_flags & ~(ULONG)0x10;
            debug_registers_requested =
                (context->ContextFlags != original_flags) ? TRUE : FALSE;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        NTSTATUS status = original(ThreadHandle, ThreadInformationClass,
                                   ThreadInformation, ThreadInformationLength,
                                   ReturnLength);

        __try {
            ProbeForWrite(&context->ContextFlags, sizeof(ULONG), 1);
            context->ContextFlags = original_flags;
            if (debug_registers_requested) {
                context->Dr0 = 0;
                context->Dr1 = 0;
                context->Dr2 = 0;
                context->Dr3 = 0;
                context->Dr6 = 0;
                context->Dr7 = 0;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        return status;
    }

    NTSTATUS status = original(ThreadHandle, ThreadInformationClass,
                               ThreadInformation, ThreadInformationLength,
                               ReturnLength);

    /* ThreadHideFromDebugger 查询：配合"吞设置"演全套，返回 TRUE */
    if (NT_SUCCESS(status) &&
        ThreadInformationClass == ADBG_THREAD_HIDE_FROM_DEBUGGER &&
        ThreadInformation != nullptr) {
        if (!AdbgPolicyProbe(ADBG_TECH_HIDE_QUERY))
            return status;

        __try {
            ULONG temp_return_length = 0;
            if (ReturnLength != nullptr) {
                ProbeForWrite(ReturnLength, sizeof(ULONG), 1);
                temp_return_length = *ReturnLength;
            }
            *(BOOLEAN *)ThreadInformation = TRUE;
            if (ReturnLength != nullptr)
                *ReturnLength = temp_return_length;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            status = GetExceptionCode();
        }
    }

    return status;
}
