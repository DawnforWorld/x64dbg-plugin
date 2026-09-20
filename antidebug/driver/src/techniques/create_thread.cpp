/* SPDX-License-Identifier: MIT */
/* techniques/create_thread.cpp —— NtCreateThreadEx。
 *
 * 来源：vmp Main.cpp HkNtCreateThreadEx 移植（docs/08 §2）。要点：清掉
 * CreateFlags 里的 THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER(0x4)——
 * VMProtect 的"对调试器隐身线程"失效。
 */
#include "common/nt_extra.hpp"
#include "framework/policy.hpp"
#include "framework/registry.hpp"
#include "framework/technique.hpp"

extern "C" NTSTATUS NTAPI AdbgHkNtCreateThreadEx(
    PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, HANDLE ProcessHandle,
    ADBG_USER_THREAD_START_ROUTINE StartRoutine, PVOID Argument, ULONG CreateFlags,
    SIZE_T ZeroBits, SIZE_T StackSize, SIZE_T MaximumStackSize,
    PADBG_PS_ATTRIBUTE_LIST AttributeList) {
    auto original = (ADBG_FN_NtCreateThreadEx)AdbgRegistryOriginal(
        kHookNtCreateThreadEx);
    KPROCESSOR_MODE previous_mode = ExGetPreviousMode();

    if (previous_mode == KernelMode)
        return original(ThreadHandle, DesiredAccess, ObjectAttributes,
                        ProcessHandle, StartRoutine, Argument, CreateFlags,
                        ZeroBits, StackSize, MaximumStackSize, AttributeList);

    if (AdbgPolicyProbe(ADBG_TECH_THREAD_CREATE_HIDE) &&
        (CreateFlags & THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER) != 0) {
        CreateFlags &= ~(ULONG)THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER;
    }

    return original(ThreadHandle, DesiredAccess, ObjectAttributes,
                    ProcessHandle, StartRoutine, Argument, CreateFlags,
                    ZeroBits, StackSize, MaximumStackSize, AttributeList);
}
