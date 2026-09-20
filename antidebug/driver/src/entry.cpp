/* SPDX-License-Identifier: MIT */
/* entry.cpp —— 启动层：CustomDriverEntry + 启动邮箱握手（docs/02 §4.1）。
 *
 * 映射器通过 NtAddAtom 劫持调用这里：param1 = 启动邮箱（内核地址），
 * param2 = ADBG_ENTRY_MAGIC。劫持窗口必须保持在微秒级——入口只做
 * 校验和清零，慢活全部丢给系统线程（vtdbg 验证过的模式）。
 *
 * 启动线程的步骤与失败码（ADBG_FAIL_*，docs/03 §2）：
 *   解析 10 个内核函数地址(5) → 策略默认值 → 引擎启动(6/7) →
 *   控制设备(8) → 邮箱回写 SUCCESS。
 */
#include <ntddk.h>
#include <stdint.h>

#include "adbg_abi.h"
#include "common/bss.hpp"
#include "control/device.hpp"
#include "engine/hook_engine.hpp"
#include "framework/policy.hpp"
#include "framework/registry.hpp"

/* ---------------- .adbss 锚点（docs/common/bss.hpp） ---------------- */

#pragma bss_seg(".adbss$A")
static ULONG64 s_adbss_begin = 0;  /* 排最前 */
#pragma bss_seg(".adbss$M")
static ADBG_BOOT_SCRATCH *s_scratch = nullptr;
static uint8_t s_secret_copy[16] = {0};
#pragma bss_seg(".adbss$Z")
static ULONG64 s_adbss_end = 0;  /* 排最后 */
#pragma bss_seg()

/* 清零范围的上限保险：远超现有全部全局变量；超出说明锚点不是我们
 * 认识的样子，拒绝清零（宁可带着垃圾状态失败，也不能乱清） */
#define ADBG_BSS_ZERO_MAX_BYTES 0x100000u

static VOID BringupThread(PVOID context) {
    UNREFERENCED_PARAMETER(context);

    ADBG_BOOT_SCRATCH *s = s_scratch;
    if (s == nullptr) {
        PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
        return;
    }

    /* 步骤 5：解析 10 个内核函数地址（全量校验，缺一个都不启动） */
    NTSTATUS status = AdbgRegistryInit();
    if (!NT_SUCCESS(status)) {
        KeMemoryBarrier();
        s->FailStep = ADBG_FAIL_RESOLVE_EXPORTS;
        s->Result = ADBG_BOOT_FAILED;
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    /* 策略默认值：技术全开、名单为空（总闸门关闭状态，安全默认） */
    AdbgPolicyInit();

    /* 步骤 6/7：启动 hook 引擎（回调挂在注册表的派发函数上） */
    status = adbg_engine::InfinityHookProEngine.Start(&AdbgRegistryDispatch);
    if (!NT_SUCCESS(status)) {
        KeMemoryBarrier();
        /* 细分失败阶段：解析（特征码）失败 = 6，挂钩失败 = 7 */
        s->FailStep = adbg_engine::LastStartWasInitFailure()
                          ? ADBG_FAIL_ENGINE_INIT
                          : ADBG_FAIL_ENGINE_START;
        s->Result = ADBG_BOOT_FAILED;
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    /* 步骤 8：控制设备（带走会话密钥的拷贝） */
    RtlCopyMemory(s_secret_copy, s->AuthSecret, sizeof(s_secret_copy));
    status = AdbgControlCreate(s_secret_copy);
    if (!NT_SUCCESS(status)) {
        adbg_engine::InfinityHookProEngine.Stop();
        KeMemoryBarrier();
        s->FailStep = ADBG_FAIL_CONTROL_DEVICE;
        s->Result = ADBG_BOOT_FAILED;
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    /* 先写载荷字段，内存屏障，再发布终态（加载器读快照，x86-TSO
     * 保证这个顺序）——vtdbg 同款 */
    KeMemoryBarrier();
    s->OsBuildNumber = 0;  /* 版本号走 IOCTL 实时查，这里留 0 */
    s->Result = ADBG_BOOT_SUCCESS;

    PsTerminateSystemThread(STATUS_SUCCESS);
}

extern "C" NTSTATUS CustomDriverEntry(PVOID param1, PVOID param2) {
    if ((ULONG64)param2 != ADBG_ENTRY_MAGIC)
        return STATUS_INVALID_PARAMETER;

    ADBG_BOOT_SCRATCH *s = (ADBG_BOOT_SCRATCH *)param1;
    if (s == nullptr || s->Size < sizeof(ADBG_BOOT_SCRATCH) ||
        s->Version != ADBG_BOOT_SCRATCH_VERSION || s->Magic != ADBG_ENTRY_MAGIC)
        return STATUS_INVALID_PARAMETER_1;

    /* 加载窗口的 ABI 检查：版本不一致 = 拒绝加载 */
    if (s->ExpectedAbiVersion != ADBG_ABI_VERSION) {
        s->FailStep = ADBG_FAIL_ABI_VERSION;
        KeMemoryBarrier();
        s->Result = ADBG_BOOT_FAILED;
        return STATUS_INVALID_PARAMETER_2;
    }

    /* 防御性清零全局区（映射器不拷贝未初始化数据节，见 common/bss.hpp） */
    const ULONG64 bss_len = (ULONG64)&s_adbss_end - (ULONG64)&s_adbss_begin;
    if (bss_len == 0 || bss_len > ADBG_BSS_ZERO_MAX_BYTES) {
        s->FailStep = ADBG_FAIL_BSS_ZERO;
        KeMemoryBarrier();
        s->Result = ADBG_BOOT_FAILED;
        return STATUS_UNSUCCESSFUL;
    }
    RtlZeroMemory(&s_adbss_begin, (SIZE_T)bss_len);

    s_scratch = s;

    HANDLE thread = nullptr;
    NTSTATUS status = PsCreateSystemThread(&thread, 0, nullptr, nullptr,
                                           nullptr, BringupThread, nullptr);
    if (!NT_SUCCESS(status)) {
        /* 还没有任何并发，直接写终态是安全的 */
        KeMemoryBarrier();
        s->FailStep = ADBG_FAIL_THREAD_CREATE;
        s->Result = ADBG_BOOT_FAILED;
        return status;
    }
    ZwClose(thread);

    /* 线程可能已经写出终态——只在还是 PENDING 时标记"线程已创建"，
     * 绝不覆盖终态（覆盖会制造假超时） */
    KeMemoryBarrier();
    s->ThreadSpawned = 1;
    InterlockedCompareExchange64((volatile LONG64 *)&s->Result,
                                 ADBG_BOOT_THREAD_ACK, ADBG_BOOT_PENDING);
    return STATUS_SUCCESS;
}
