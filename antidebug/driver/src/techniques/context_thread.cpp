/* SPDX-License-Identifier: MIT */
/* techniques/context_thread.cpp —— NtGet/NtSetContextThread。
 *
 * 来源：vmp Main.cpp HkNtGetContextThread / HkNtSetContextThread 移植
 * （docs/08 §2）。要点：把 ContextFlags 里的 CONTEXT_DEBUG_REGISTERS
 * (0x10) 位剥掉再调原函数、调完还原；读取时若对方原本要了调试寄存器，
 * Dr0-3/Dr6/Dr7 与 LBR 四兄弟全清零——硬件断点对目标隐形。
 */
#include "common/nt_extra.hpp"
#include "framework/policy.hpp"
#include "framework/registry.hpp"
#include "framework/technique.hpp"

extern "C" NTSTATUS NTAPI AdbgHkNtGetContextThread(HANDLE ThreadHandle,
                                                   PCONTEXT Context) {
    auto original = (ADBG_FN_NtGetContextThread)AdbgRegistryOriginal(
        kHookNtGetContextThread);
    KPROCESSOR_MODE previous_mode = ExGetPreviousMode();

    if (previous_mode == KernelMode)
        return original(ThreadHandle, Context);

    if (!AdbgPolicyProbe(ADBG_TECH_DR_REGISTERS))
        return original(ThreadHandle, Context);

    ULONG original_flags = 0;
    BOOLEAN debug_registers_requested = FALSE;
    BOOLEAN hidden = FALSE;

    __try {
        ProbeForWrite(&Context->ContextFlags, sizeof(ULONG), 1);
        original_flags = Context->ContextFlags;
        Context->ContextFlags = original_flags & ~(ULONG)0x10;
        debug_registers_requested =
            (Context->ContextFlags != original_flags) ? TRUE : FALSE;
        hidden = TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hidden = FALSE;
    }

    NTSTATUS ret = original(ThreadHandle, Context);

    if (hidden) {
        __try {
            ProbeForWrite(&Context->ContextFlags, sizeof(ULONG), 1);
            Context->ContextFlags = original_flags;
            if (debug_registers_requested) {
                Context->Dr0 = 0;
                Context->Dr1 = 0;
                Context->Dr2 = 0;
                Context->Dr3 = 0;
                Context->Dr6 = 0;
                Context->Dr7 = 0;
                /* LBR（最后分支记录）四个 RIP 一并清零，防止侧路泄露 */
                Context->LastBranchToRip = 0;
                Context->LastBranchFromRip = 0;
                Context->LastExceptionToRip = 0;
                Context->LastExceptionFromRip = 0;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    return ret;
}

extern "C" NTSTATUS NTAPI AdbgHkNtSetContextThread(HANDLE ThreadHandle,
                                                   PCONTEXT Context) {
    auto original = (ADBG_FN_NtSetContextThread)AdbgRegistryOriginal(
        kHookNtSetContextThread);
    KPROCESSOR_MODE previous_mode = ExGetPreviousMode();

    if (previous_mode == KernelMode)
        return original(ThreadHandle, Context);

    if (!AdbgPolicyProbe(ADBG_TECH_DR_REGISTERS))
        return original(ThreadHandle, Context);

    /* 目标不能写调试寄存器（清不掉调试器的硬件断点，也做不了自检） */
    ULONG original_flags = 0;
    BOOLEAN hidden = FALSE;

    __try {
        ProbeForWrite(&Context->ContextFlags, sizeof(ULONG), 1);
        original_flags = Context->ContextFlags;
        Context->ContextFlags = original_flags & ~(ULONG)0x10;
        hidden = TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hidden = FALSE;
    }

    NTSTATUS ret = original(ThreadHandle, Context);

    if (hidden) {
        __try {
            ProbeForWrite(&Context->ContextFlags, sizeof(ULONG), 1);
            Context->ContextFlags = original_flags;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    return ret;
}
