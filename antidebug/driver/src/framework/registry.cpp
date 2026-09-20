/* SPDX-License-Identifier: MIT */
/* framework/registry.cpp —— 见同头文件。 */
#include "framework/registry.hpp"

#include "common/bss.hpp"
#include "common/nt_extra.hpp"
#include "framework/policy.hpp"
#include "framework/technique.hpp"

/* 技术层的 10 个处理函数（techniques/*.cpp） */
extern "C" {
NTSTATUS NTAPI AdbgHkNtQueryInformationProcess(HANDLE, ULONG, PVOID, ULONG, PULONG);
NTSTATUS NTAPI AdbgHkNtSetInformationThread(HANDLE, ULONG, PVOID, ULONG);
NTSTATUS NTAPI AdbgHkNtClose(HANDLE);
NTSTATUS NTAPI AdbgHkNtQuerySystemInformation(ULONG, PVOID, ULONG, PULONG);
NTSTATUS NTAPI AdbgHkNtQueryInformationThread(HANDLE, ULONG, PVOID, ULONG, PULONG);
NTSTATUS NTAPI AdbgHkNtGetContextThread(HANDLE, PCONTEXT);
NTSTATUS NTAPI AdbgHkNtSetContextThread(HANDLE, PCONTEXT);
NTSTATUS NTAPI AdbgHkNtSystemDebugControl(ULONG, PVOID, ULONG, PVOID, ULONG, PULONG);
NTSTATUS NTAPI AdbgHkNtDuplicateObject(HANDLE, HANDLE, HANDLE, PHANDLE, ACCESS_MASK, ULONG, ULONG);
NTSTATUS NTAPI AdbgHkNtCreateThreadEx(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, HANDLE,
                                      PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T,
                                      PVOID);
}  // extern "C"

/* ---------------- 声明式注册表本体（★C，docs/02 §3） ---------------- */

const TechniqueRow kTechniqueRows[kHookCount] = {
    {"NtQueryInformationProcess", (PVOID)AdbgHkNtQueryInformationProcess,
     ADBG_TECH_DEBUG_OBJECT | ADBG_TECH_DEBUG_PORT | ADBG_TECH_DEBUG_FLAGS},
    {"NtSetInformationThread", (PVOID)AdbgHkNtSetInformationThread,
     ADBG_TECH_HIDE_SET | ADBG_TECH_WOW64_DR_REGISTERS},
    {"NtClose", (PVOID)AdbgHkNtClose,
     ADBG_TECH_PROTECTED_CLOSE},
    {"NtQuerySystemInformation", (PVOID)AdbgHkNtQuerySystemInformation,
     ADBG_TECH_KERNEL_DEBUGGER},
    {"NtQueryInformationThread", (PVOID)AdbgHkNtQueryInformationThread,
     ADBG_TECH_HIDE_QUERY | ADBG_TECH_WOW64_DR_REGISTERS},
    {"NtGetContextThread", (PVOID)AdbgHkNtGetContextThread,
     ADBG_TECH_DR_REGISTERS},
    {"NtSetContextThread", (PVOID)AdbgHkNtSetContextThread,
     ADBG_TECH_DR_REGISTERS},
    {"NtSystemDebugControl", (PVOID)AdbgHkNtSystemDebugControl,
     ADBG_TECH_SYSTEM_DEBUG_CONTROL},
    {"NtDuplicateObject", (PVOID)AdbgHkNtDuplicateObject,
     ADBG_TECH_PROTECTED_CLOSE},
    {"NtCreateThreadEx", (PVOID)AdbgHkNtCreateThreadEx,
     ADBG_TECH_THREAD_CREATE_HIDE},
};

ADBG_BSS_BEGIN
static PVOID g_originals[kHookCount] = {nullptr};
ADBG_BSS_END

extern "C" {

NTSTATUS AdbgRegistryInit(void) {
    for (ULONG i = 0; i < kHookCount; ++i) {
        UNICODE_STRING name;
        RtlInitUnicodeString(&name, nullptr);
        /* 名字是 ASCII 的 NtXxx，得先转到宽字符（栈上小缓冲即可） */
        WCHAR wide[64] = {};
        const char *src = kTechniqueRows[i].NtName;
        ULONG j = 0;
        for (; src[j] != '\0' && j < 62; ++j)
            wide[j] = (WCHAR)src[j];
        wide[j] = L'\0';
        RtlInitUnicodeString(&name, wide);

        g_originals[i] = MmGetSystemRoutineAddress(&name);
        if (g_originals[i] == nullptr) {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                       "[antidebug] resolve failed: %s\n", src);
            return STATUS_PROCEDURE_NOT_FOUND;
        }
    }
    return STATUS_SUCCESS;
}

void __fastcall AdbgRegistryDispatch(unsigned long index,
                                      void **system_call_function) {
    UNREFERENCED_PARAMETER(index);

    if (system_call_function == nullptr)
        return;
    /* 引擎只在用户态调用时回调，这里再兜一层（零成本） */
    if (ExGetPreviousMode() != UserMode)
        return;

    /* 总闸门：名单外进程零改写（R5，docs/02 §4.2 热路径第一跳） */
    if (!AdbgPolicyIsTargetPid(PsGetCurrentProcessId()))
        return;

    /* 地址查表：命中即换写目标指针（派发不依赖系统调用号，docs/02 §6） */
    const void *target = *system_call_function;
    for (ULONG i = 0; i < kHookCount; ++i) {
        if (target == g_originals[i]) {
            *system_call_function = kTechniqueRows[i].Handler;
            return;
        }
    }
}

PVOID AdbgRegistryOriginal(ULONG idx) {
    if (idx >= kHookCount)
        return nullptr;
    return g_originals[idx];
}

uint32_t AdbgRegistryHookedCount(void) {
    return kHookCount;
}

}  // extern "C"
