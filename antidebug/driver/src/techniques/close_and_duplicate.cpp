/* SPDX-License-Identifier: MIT */
/* techniques/close_and_duplicate.cpp —— NtClose / NtDuplicateObject。
 *
 * 来源：vmp Main.cpp HkFnNtClose / HkNtDuplicateObject 移植（docs/08 §2）。
 * 要点：NtClose 是"重实现"而不是包一层——用 ObQueryObjectAuditingByHandle
 * 判非法句柄、ObCloseHandle 真关闭；被调试时受保护句牌(OBJ_PROTECT_CLOSE)
 * 保留 STATUS_HANDLE_NOT_CLOSABLE 语义；DuplicateObject 剥
 * DUPLICATE_CLOSE_SOURCE 堵住"复制后关闭"的绕过。
 */
#include "common/nt_extra.hpp"
#include "framework/policy.hpp"
#include "framework/registry.hpp"
#include "framework/technique.hpp"

extern "C" NTSTATUS NTAPI AdbgHkNtClose(HANDLE Handle) {
    auto original = (ADBG_FN_NtClose)AdbgRegistryOriginal(kHookNtClose);
    KPROCESSOR_MODE previous_mode = ExGetPreviousMode();

    if (previous_mode == KernelMode)
        return original(Handle);

    if (!AdbgPolicyProbe(ADBG_TECH_PROTECTED_CLOSE))
        return original(Handle);

    BOOLEAN audit_on_close = FALSE;
    NTSTATUS ob_status = ObQueryObjectAuditingByHandle(Handle, &audit_on_close);

    NTSTATUS status;
    if (ob_status != STATUS_INVALID_HANDLE) {
        /* 被调试时：VMProtect 会给句柄打 OBJ_PROTECT_CLOSE 标记再故意
         * Close——未调试的进程同样会拿到 HANDLE_NOT_CLOSABLE，这里把
         * 这个语义原样保住 */
        BOOLEAN being_debugged =
            PsGetProcessDebugPort(PsGetCurrentProcess()) != nullptr;
        OBJECT_HANDLE_INFORMATION handle_info = {};

        if (being_debugged) {
            PVOID object = nullptr;
            ob_status = ObReferenceObjectByHandle(Handle, 0, nullptr,
                                                  previous_mode, &object,
                                                  &handle_info);
            if (object != nullptr)
                ObDereferenceObject(object);
        }

        if (being_debugged && NT_SUCCESS(ob_status) &&
            (handle_info.HandleAttributes & OBJ_PROTECT_CLOSE)) {
            status = STATUS_HANDLE_NOT_CLOSABLE;
        } else {
            status = ObCloseHandle(Handle, previous_mode);
        }
    } else {
        status = STATUS_INVALID_HANDLE;  /* 非法句柄：保持原生表现 */
    }

    return status;
}

extern "C" NTSTATUS NTAPI AdbgHkNtDuplicateObject(
    HANDLE SourceProcessHandle, HANDLE SourceHandle, HANDLE TargetProcessHandle,
    PHANDLE TargetHandle, ACCESS_MASK DesiredAccess, ULONG HandleAttributes,
    ULONG Options) {
    auto original = (ADBG_FN_NtDuplicateObject)AdbgRegistryOriginal(
        kHookNtDuplicateObject);
    KPROCESSOR_MODE previous_mode = ExGetPreviousMode();

    if (previous_mode == KernelMode)
        return original(SourceProcessHandle, SourceHandle, TargetProcessHandle,
                        TargetHandle, DesiredAccess, HandleAttributes,
                        Options);

    if (!AdbgPolicyProbe(ADBG_TECH_PROTECTED_CLOSE))
        return original(SourceProcessHandle, SourceHandle, TargetProcessHandle,
                        TargetHandle, DesiredAccess, HandleAttributes,
                        Options);

    /* "复制一个新句柄再关闭源句柄"是绕过受保护句柄的经典路——
     * 被调试时若源句柄受保护，剥掉 DUPLICATE_CLOSE_SOURCE */
    BOOLEAN being_debugged =
        PsGetProcessDebugPort(PsGetCurrentProcess()) != nullptr;
    if (being_debugged && (Options & DUPLICATE_CLOSE_SOURCE)) {
        PVOID object = nullptr;
        OBJECT_HANDLE_INFORMATION handle_info = {};
        NTSTATUS status = ObReferenceObjectByHandle(SourceHandle, 0, nullptr,
                                                    previous_mode, &object,
                                                    &handle_info);
        if (NT_SUCCESS(status)) {
            if (object != nullptr)
                ObDereferenceObject(object);
            if (handle_info.HandleAttributes & OBJ_PROTECT_CLOSE)
                Options &= ~(ULONG)DUPLICATE_CLOSE_SOURCE;
        }
    }

    return original(SourceProcessHandle, SourceHandle, TargetProcessHandle,
                    TargetHandle, DesiredAccess, HandleAttributes, Options);
}
