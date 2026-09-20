/* SPDX-License-Identifier: MIT */
/* techniques/process_info.cpp —— NtQueryInformationProcess 的 3 项技术。
 *
 * 来源：vmp Main.cpp HkNtQueryInformationProcess 移植，语义保留清单见
 * docs/08 §2：0x1E 早退返回 STATUS_PORT_NOT_SET；DebugFlags→TRUE；
 * DebugPort→0；BasicInformation 不动；ReturnLength 先读后写回。
 */
#include "common/nt_extra.hpp"
#include "framework/policy.hpp"
#include "framework/registry.hpp"
#include "framework/technique.hpp"

extern "C" NTSTATUS NTAPI AdbgHkNtQueryInformationProcess(
    HANDLE ProcessHandle, ULONG InformationClass, PVOID ProcessInformation,
    ULONG ProcessInformationLength, PULONG ReturnLength) {
    auto original = (ADBG_FN_NtQueryInformationProcess)AdbgRegistryOriginal(
        kHookNtQueryInformationProcess);
    KPROCESSOR_MODE previous_mode = ExGetPreviousMode();

    if (previous_mode == KernelMode)
        return original(ProcessHandle, InformationClass, ProcessInformation,
                        ProcessInformationLength, ReturnLength);

    /* ProcessDebugObjectHandle(0x1E)：未调试的进程就是
     * "端口未设置"——直接演全套，不调原函数 */
    if (InformationClass == ADBG_PROCESS_DEBUG_OBJECT_CLASS &&
        ProcessInformation != nullptr &&
        ProcessInformationLength == sizeof(HANDLE)) {
        if (!AdbgPolicyProbe(ADBG_TECH_DEBUG_OBJECT))
            return original(ProcessHandle, InformationClass,
                            ProcessInformation, ProcessInformationLength,
                            ReturnLength);

        PEPROCESS process = nullptr;
        NTSTATUS status = ObReferenceObjectByHandle(
            ProcessHandle, PROCESS_QUERY_INFORMATION, *PsProcessType,
            previous_mode, (PVOID *)&process, nullptr);
        if (!NT_SUCCESS(status))
            return status;
        ObDereferenceObject(process);

        __try {
            ProbeForWrite(ProcessInformation, sizeof(HANDLE), 4);
            if (ReturnLength != nullptr)
                ProbeForWrite(ReturnLength, sizeof(ULONG), 1);
            *(PHANDLE)ProcessInformation = nullptr;
            if (ReturnLength != nullptr)
                *ReturnLength = sizeof(HANDLE);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return GetExceptionCode();
        }
        return STATUS_PORT_NOT_SET;
    }

    NTSTATUS ret = original(ProcessHandle, InformationClass, ProcessInformation,
                            ProcessInformationLength, ReturnLength);

    if (NT_SUCCESS(ret) && ProcessInformation != nullptr &&
        InformationClass != ADBG_PROCESS_BASIC_INFO_CLASS) {
        if (InformationClass == ADBG_PROCESS_DEBUG_FLAGS_CLASS &&
            AdbgPolicyProbe(ADBG_TECH_DEBUG_FLAGS)) {
            __try {
                ULONG temp_return_length = 0;
                if (ReturnLength != nullptr) {
                    ProbeForWrite(ReturnLength, sizeof(ULONG), 1);
                    temp_return_length = *ReturnLength;
                }
                *(ULONG *)ProcessInformation = TRUE;  /* NoDebugInherit */
                if (ReturnLength != nullptr)
                    *ReturnLength = temp_return_length;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ret = GetExceptionCode();
            }
        } else if (InformationClass == ADBG_PROCESS_DEBUG_PORT_CLASS &&
                   AdbgPolicyProbe(ADBG_TECH_DEBUG_PORT)) {
            __try {
                ULONG temp_return_length = 0;
                if (ReturnLength != nullptr) {
                    ProbeForWrite(ReturnLength, sizeof(ULONG), 1);
                    temp_return_length = *ReturnLength;
                }
                *(ULONG_PTR *)ProcessInformation = 0;  /* 没有调试端口 */
                if (ReturnLength != nullptr)
                    *ReturnLength = temp_return_length;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ret = GetExceptionCode();
            }
        }
    }

    return ret;
}
