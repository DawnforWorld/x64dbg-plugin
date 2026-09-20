/* SPDX-License-Identifier: MIT */
/* framework/policy.cpp —— 见同头文件。 */
#include "framework/policy.hpp"

#include "common/bss.hpp"

extern "C" {

ADBG_BSS_BEGIN
static volatile LONG g_tech_mask = 0;
static volatile ULONG g_target_pids[ADBG_MAX_TARGETS] = {0};
ADBG_BSS_END

void AdbgPolicyInit(void) {
    InterlockedExchange(&g_tech_mask, (LONG)ADBG_TECH_ALL);
    for (ULONG i = 0; i < ADBG_MAX_TARGETS; ++i)
        g_target_pids[i] = 0;
}

BOOLEAN AdbgPolicyIsTargetPid(HANDLE pid) {
    const ULONG value = (ULONG)(ULONG_PTR)pid;
    if (value == 0)
        return FALSE;
    for (ULONG i = 0; i < ADBG_MAX_TARGETS; ++i) {
        if (g_target_pids[i] == value)
            return TRUE;
    }
    return FALSE;
}

BOOLEAN AdbgPolicyProbe(uint32_t tech_mask) {
    if (((uint32_t)g_tech_mask & tech_mask) == 0)
        return FALSE;
    return AdbgPolicyIsTargetPid(PsGetCurrentProcessId());
}

void AdbgPolicySetMask(uint32_t mask) {
    InterlockedExchange(&g_tech_mask, (LONG)mask);
}

uint32_t AdbgPolicyGetMask(void) {
    return (uint32_t)g_tech_mask;
}

NTSTATUS AdbgPolicySetTarget(uint32_t action, uint32_t pid) {
    switch (action) {
    case ADBG_TARGET_CLEAR:
        for (ULONG i = 0; i < ADBG_MAX_TARGETS; ++i)
            g_target_pids[i] = 0;
        return STATUS_SUCCESS;

    case ADBG_TARGET_ADD: {
        if (pid == 0)
            return STATUS_INVALID_PARAMETER;
        ULONG free_slot = ADBG_MAX_TARGETS;  /* 未找到空位 */
        for (ULONG i = 0; i < ADBG_MAX_TARGETS; ++i) {
            if (g_target_pids[i] == pid)
                return STATUS_SUCCESS;  /* 已在名单里，幂等 */
            if (g_target_pids[i] == 0 && free_slot == ADBG_MAX_TARGETS)
                free_slot = i;
        }
        if (free_slot == ADBG_MAX_TARGETS)
            return STATUS_INSUFFICIENT_RESOURCES;  /* 名单满了（8 个） */
        g_target_pids[free_slot] = pid;
        return STATUS_SUCCESS;
    }

    case ADBG_TARGET_REMOVE: {
        for (ULONG i = 0; i < ADBG_MAX_TARGETS; ++i) {
            if (g_target_pids[i] == pid) {
                g_target_pids[i] = 0;
                return STATUS_SUCCESS;
            }
        }
        return STATUS_SUCCESS;  /* 不在名单里也算成功，幂等 */
    }

    default:
        return STATUS_INVALID_PARAMETER;
    }
}

uint32_t AdbgPolicyGetTargets(uint32_t out_pids[ADBG_MAX_TARGETS]) {
    uint32_t count = 0;
    for (ULONG i = 0; i < ADBG_MAX_TARGETS; ++i) {
        uint32_t pid = g_target_pids[i];
        if (pid != 0) {
            out_pids[count] = pid;
            ++count;
        }
    }
    return count;
}

}  // extern "C"
