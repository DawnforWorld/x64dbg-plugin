/* SPDX-License-Identifier: MIT */
/* engine/infinity_hook_pro.cpp —— ★A 默认引擎实现。
 *
 * 来源：antidebug/InfinityHookPro 参考克隆 hook.cpp 移植（MIT，作者
 * "禁锢在时空之中的灵魂" / FiYHer 原作）。移植时保留全部技术决策与
 * 版本分支（docs/04），只做：命名规范化、池标签换 'Adbg'、接口包装。
 *
 * 原理一句话（docs/00 §3）：系统调用路径上有个"取时间戳"的函数指针
 * 一定会被调用（ETW 的 CKCL 会话开启系统调用记录后），把它换成我们的
 * 函数，就能在每次系统调用时拿到回调机会并改写调用目标。
 */
#include "engine/infinity_hook_pro.hpp"

#include <intrin.h>

#include "../common/bss.hpp"
#include "../platform/utils.hpp"

namespace adbg_inf {

/* 本文件是引擎的唯一实现文件，内部符号直接放 adbg_inf 命名空间 */
constexpr ULONG kPoolTag = 'Adbg';

/* 系统调用栈特征（PerfInfoLogSysCallEntry 的函数序言撒在栈上的魔数，
 * docs/04 §2：Win11 23606 前后各一个值） */
constexpr ULONG kMagic501802 = 0x501802;
constexpr ULONG kMagic601802 = 0x601802;
constexpr unsigned short kMagicF33 = 0xF33;

/* HalpPerformanceCounter 结构里的字段偏移（docs/04 §5） */
constexpr ULONG kHalCounterTypeOffset = 0xE4;
constexpr ULONG kHalCounterBaseRateOffset = 0xC0;
constexpr ULONG kHalCounterTypePhysical = 0x5;
constexpr ULONG64 kHalCounterBaseRate = 10000000ull;

/* CKCL 日志会话的 GUID */
const GUID kCkclSessionGuid = {0x54dea73a, 0xed1f, 0x42a4,
                               {0xaf, 0x71, 0x3e, 0x63, 0xd0, 0x56, 0xf1, 0x74}};

/* ---------------- 引擎状态（全部零初始化，落在 .adbss$M，docs/02 §6） --- */

ADBG_BSS_BEGIN
adbg_engine::SyscallHookCallback g_callback = nullptr;
ULONG g_build_number = 0;
PVOID g_syscall_table = nullptr;
volatile LONG g_running = 0;
volatile LONG g_detect_thread_status = 1;  /* 1=检测线程继续跑 */
PVOID g_etwp_debugger_data = nullptr;
PVOID g_ckcl_logger_context = nullptr;
PVOID *g_etwp_debugger_data_silo = nullptr;
PVOID *g_get_cpu_clock = nullptr;
PETHREAD g_detect_thread_object = nullptr;
PLONGLONG g_qpc_pointer = nullptr;
PMDL g_qpc_mdl = nullptr;
ULONG64 g_original_get_cpu_clock = 0;
ULONG64 g_hvlp_reference_tsc_page = 0;
ULONG64 g_hvl_get_qpc_bias = 0;
ULONG64 g_hvlp_get_reference_time_using_tsc_page = 0;
ULONG64 g_halp_performance_counter = 0;
ULONG64 g_halp_original_performance_counter = 0;
ULONG64 g_halp_original_performance_counter_copy = 0;
ULONG *g_halp_performance_counter_type = nullptr;
UCHAR g_vm_halp_performance_counter_type = 0;
ULONG g_original_halp_performance_counter_type = 0;
ULONG64 g_original_hvlp_get_reference_time_using_tsc_page = 0;
HvlGetQpcBiasFn g_original_hvl_get_qpc_bias = nullptr;
CLIENT_ID g_client_id = {};
bool g_thread_created = false;
/* 细分失败阶段：true = 解析(特征码)阶段失败(FailStep=6)，否则视为挂钩失败(7) */
bool g_init_failed = false;
ADBG_BSS_END

/* ---------------- CKCL 会话控制 ---------------- */

NTSTATUS NtTraceControlOp(ETWP_TRACE_TYPE type) {
    ADBG_CKCL_TRACE_PROPERTIES *prop =
        (ADBG_CKCL_TRACE_PROPERTIES *)ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE,
                                                       kPoolTag);
    if (prop == nullptr)
        return STATUS_MEMORY_NOT_ALLOCATED;

    WCHAR *provider_name = (WCHAR *)ExAllocatePoolWithTag(
        NonPagedPool, 256 * sizeof(WCHAR), kPoolTag);
    if (provider_name == nullptr) {
        ExFreePoolWithTag(prop, kPoolTag);
        return STATUS_MEMORY_NOT_ALLOCATED;
    }

    RtlZeroMemory(prop, PAGE_SIZE);
    RtlZeroMemory(provider_name, 256 * sizeof(WCHAR));
    RtlCopyMemory(provider_name, L"Circular Kernel Context Logger",
                  sizeof(L"Circular Kernel Context Logger"));
    RtlInitUnicodeString(&prop->ProviderName, provider_name);

    prop->Props.Wnode.BufferSize = PAGE_SIZE;
    prop->Props.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    prop->Props.Wnode.Guid = kCkclSessionGuid;
    prop->Props.Wnode.ClientContext = 3;
    prop->Props.BufferSize = sizeof(ULONG);
    prop->Props.MinimumBuffers = 2;
    prop->Props.MaximumBuffers = 2;
    prop->Props.LogFileMode = ADBG_EVENT_TRACE_BUFFERING_MODE;
    if (type == EtwpUpdateTrace)
        prop->Props.EnableFlags = ADBG_EVENT_TRACE_FLAG_SYSTEMCALL;

    ULONG length = 0;
    NTSTATUS status = NtTraceControl(type, prop, PAGE_SIZE, prop, PAGE_SIZE,
                                      &length);

    ExFreePoolWithTag(provider_name, kPoolTag);
    ExFreePoolWithTag(prop, kPoolTag);
    return status;
}

/* ---------------- 替换函数：每次系统调用都会进来 ---------------- */

ULONG64 OnCpuClock() {
    if (ExGetPreviousMode() == KernelMode)
        return __rdtsc();

    /* 当前线程对象（KTHREAD，GS 段固定偏移读取） */
    PKTHREAD thread = (PKTHREAD)__readgsqword(0x188);

    /* 系统调用号在 KTHREAD 里的偏移按版本分档（docs/04 §3） */
    ULONG call_index = 0;
    if (g_build_number <= 7601)
        call_index = *(ULONG *)((ULONG64)thread + 0x1F8);
    else
        call_index = *(ULONG *)((ULONG64)thread + 0x80);

    void **stack_max = (void **)__readgsqword(0x1A8);
    void **stack_frame = (void **)_AddressOfReturnAddress();

    /* 顺着栈找系统调用记录函数撒下的魔数，再反向找落在 SSDT
     * 表范围内的函数指针——那就是本次系统调用的目标 */
    for (void **p = stack_max; p > stack_frame; --p) {
        ULONG *v1 = (ULONG *)p;
        if (*v1 != kMagic501802 && *v1 != kMagic601802)
            continue;
        --p;
        unsigned short *v2 = (unsigned short *)p;
        if (*v2 != kMagicF33)
            continue;

        for (; p < stack_max; ++p) {
            ULONG64 value = *(ULONG64 *)p;
            if (!((ULONG64)PAGE_ALIGN((PVOID)value) >= (ULONG64)g_syscall_table &&
                  (ULONG64)PAGE_ALIGN((PVOID)value) <
                      (ULONG64)g_syscall_table + PAGE_SIZE * 2))
                continue;

            /* 命中：栈上第 9 个槽位即 KiSystemServiceExit 读取的
             * "目标函数地址"位置，交给回调改写 */
            void **system_call_function = &p[9];
            if (g_callback != nullptr)
                g_callback(call_index, system_call_function);
            break;
        }
        break;
    }

    return __rdtsc();
}

/* >1909 路径的替换函数：先做同样的栈扫描，然后返回真实的时间偏差 */
__int64 FakeHvlGetQpcBias() {
    OnCpuClock();
    if (*(ULONG64 *)g_hvlp_reference_tsc_page != 0) {
        return *(ULONG64 *)(*(ULONG64 *)g_hvlp_reference_tsc_page + 3);
    }
    return 0;
}

ULONG64 FakeGetReferenceTimeUsingTscPage() {
    return __rdtsc();
}

/* ---------------- 检测线程：处理指针被系统改回去的情况 ---------------- */

void DetectThreadRoutine(PVOID);
bool ResolveAndAttach(adbg_engine::SyscallHookCallback callback);

void DetectThreadRoutine(PVOID) {
    while (g_detect_thread_status != 0) {
        adbg_plat::Sleep(1000);

        /* Win7 上 GetCpuClock 的值会被系统改几次，需要盯着修回来 */
        if (g_build_number <= 18363 && g_detect_thread_status != 0) {
            if (MmIsAddressValid(g_get_cpu_clock) &&
                MmIsAddressValid(*g_get_cpu_clock)) {
                if ((ULONG64)(ULONG_PTR)OnCpuClock != (ULONG64)*g_get_cpu_clock) {
                    ResolveAndAttach(nullptr);
                }
            } else {
                ResolveAndAttach(nullptr);
            }
        }
        /* 现代路径需要持续调用 KeQueryPerformanceCounter 保持
         * 我们占用的取时钟路径是"热的" */
        KeQueryPerformanceCounter(nullptr);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* ---------------- 初始化：解析全部内核内部地址 ---------------- */

bool ResolveAll(adbg_engine::SyscallHookCallback callback) {
    g_init_failed = true;  /* 每个失败出口都算解析失败，成功末尾翻回 */
    if (g_detect_thread_status == 0)
        return false;

    if (callback == nullptr || !MmIsAddressValid((PVOID)(ULONG_PTR)callback))
        return false;
    g_callback = callback;

    /* 先把 CKCL 会话开起来（一次更新不行就先启动再更新） */
    if (!NT_SUCCESS(NtTraceControlOp(EtwpUpdateTrace))) {
        if (!NT_SUCCESS(NtTraceControlOp(EtwpStartTrace)))
            return false;
        if (!NT_SUCCESS(NtTraceControlOp(EtwpUpdateTrace)))
            return false;
    }

    g_build_number = adbg_plat::GetSystemBuildNumber();
    if (g_build_number == 0)
        return false;

    ULONG64 ntoskrnl = adbg_plat::GetModuleAddress("ntoskrnl.exe", nullptr);
    if (ntoskrnl == 0)
        return false;

    /* EtwpDebuggerData：唯一一处"按内容特征"定位的结构（docs/04 §4） */
    ULONG64 etwp_debugger_data = adbg_plat::FindPatternImage(
        ntoskrnl, "\x00\x00\x2c\x08\x04\x38\x0c", "??xxxxx", ".text");
    if (etwp_debugger_data == 0)
        etwp_debugger_data = adbg_plat::FindPatternImage(
            ntoskrnl, "\x00\x00\x2c\x08\x04\x38\x0c", "??xxxxx", ".data");
    if (etwp_debugger_data == 0)
        etwp_debugger_data = adbg_plat::FindPatternImage(
            ntoskrnl, "\x00\x00\x2c\x08\x04\x38\x0c", "??xxxxx", ".rdata");
    if (etwp_debugger_data == 0)
        return false;

    /* +0x10 是 silo 指针，silo[2] 是 CKCL 的日志器上下文
     * （InfinityHookPro 实测：这两个小偏移全版本一致） */
    g_etwp_debugger_data_silo = *(PVOID **)(etwp_debugger_data + 0x10);
    if (g_etwp_debugger_data_silo == nullptr)
        return false;
    g_ckcl_logger_context = g_etwp_debugger_data_silo[2];
    if (g_ckcl_logger_context == nullptr)
        return false;

    /* GetCpuClock 字段在日志器上下文里的偏移按版本分档：
     * Win7 和 Win11 都是 0x18，中间版本 0x28（docs/04 §3） */
    if (g_build_number <= 7601 || g_build_number >= 22000)
        g_get_cpu_clock = (PVOID *)((ULONG64)g_ckcl_logger_context + 0x18);
    else
        g_get_cpu_clock = (PVOID *)((ULONG64)g_ckcl_logger_context + 0x28);
    if (!MmIsAddressValid(g_get_cpu_clock))
        return false;

    g_syscall_table = PAGE_ALIGN(adbg_plat::GetSyscallEntry(ntoskrnl));
    if (g_syscall_table == nullptr)
        return false;

    if (g_build_number > 18363) {
        /* 现代路径（docs/04 §2）：下面 5 个地址全靠特征码 */

        g_hvlp_reference_tsc_page = adbg_plat::FindPatternImage(
            ntoskrnl,
            "\x48\x8b\x05\x00\x00\x00\x00\x48\x8b\x40\x00\x48\x8b\x0d\x00\x00"
            "\x00\x00\x48\xf7\xe2",
            "xxx????xxx?xxx????xxx", ".text");
        if (g_hvlp_reference_tsc_page == 0)
            return false;
        g_hvlp_reference_tsc_page += 7 +
            *(int *)(g_hvlp_reference_tsc_page + 3);  /* rip 相对寻址解引用 */

        /* HvlGetQpcBias 指针：22H2 前后两段特征码 */
        ULONG64 p = adbg_plat::FindPatternImage(
            ntoskrnl,
            "\x48\x8b\x05\x00\x00\x00\x00\x48\x85\xc0\x74\x00\x48\x83\x3d\x00"
            "\x00\x00\x00\x00\x74",
            "xxx????xxxx?xxx?????x", ".text");
        if (p == 0)
            p = adbg_plat::FindPatternImage(
                ntoskrnl,
                "\x48\x8b\x05\x00\x00\x00\x00\xe8\x00\x00\x00\x00\x48\x03\xd8"
                "\x48\x89\x1f",
                "xxx????x????xxxxxx", ".text");
        if (p == 0)
            return false;
        g_hvl_get_qpc_bias = p + 7 + *(int *)(p + 3);

        /* HvlpGetReferenceTimeUsingTscPage 指针：同样两段 */
        p = adbg_plat::FindPatternImage(
            ntoskrnl,
            "\x48\x8b\x05\x00\x00\x00\x00\x48\x85\xc0\x74\x00\x33\xc9\xe8\x00"
            "\x00\x00\x00\x48\x8b\xd8",
            "xxx????xxxx?xxx????xxx", ".text");
        if (p == 0)
            p = adbg_plat::FindPatternImage(
                ntoskrnl,
                "\x48\x8b\x05\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x03\xd8",
                "xxx????x????xxx", ".text");
        if (p == 0)
            return false;
        g_hvlp_get_reference_time_using_tsc_page = p + 7 + *(int *)(p + 3);

        /* HalpPerformanceCounter（全版本一致的特征码）。
         * 注意保留这份特征码地址：物理机分支要从它的 cmp 立即数里
         * 抠出虚拟机类型值（+21），下面 p 会被复用 */
        ULONG64 halp_counter_pattern = adbg_plat::FindPatternImage(
            ntoskrnl,
            "\x48\x8b\x05\x00\x00\x00\x00\x48\x8b\xf9\x48\x85\xc0\x74\x00\x83"
            "\xb8",
            "xxx????xxxxxxx?xx", ".text");
        if (halp_counter_pattern == 0)
            return false;
        g_halp_performance_counter =
            halp_counter_pattern + 7 + *(int *)(halp_counter_pattern + 3);

        /* HalpOriginalPerformanceCounter：Win11 23606 后换到
         * KeQueryPerformanceCounter 里找 */
        p = adbg_plat::FindPatternImage(
            ntoskrnl,
            "\x48\x8b\x05\x00\x00\x00\x00\x48\x3b\x00\x0f\x85\x00\x00\x00\x00"
            "\xA0",
            "xxx????xx?xx????x", ".text");
        if (p == 0)
            p = adbg_plat::FindPatternImage(
                ntoskrnl,
                "\x48\x8b\x0d\x00\x00\x00\x00\x4c\x00\x00\x00\x00\x48\x3b\xf1",
                "xxx????x????xxx", ".text");
        if (p == 0)
            return false;
        g_halp_original_performance_counter = p + 7 + *(int *)(p + 3);

        g_halp_performance_counter_type =
            (ULONG *)(*(ULONG64 *)g_halp_performance_counter +
                      kHalCounterTypeOffset);

        /* 物理机专属准备（docs/04 §5）：造假计数器结构 + 映射 QpcBias */
        if (*g_halp_performance_counter_type == kHalCounterTypePhysical) {
            /* 虚拟机形态的类型值直接从比对指令的立即数里抠出来 */
            g_vm_halp_performance_counter_type =
                *(UCHAR *)(halp_counter_pattern + 21);

            g_halp_original_performance_counter_copy = (ULONG64)
                ExAllocatePoolWithTag(NonPagedPool, 0xFF, kPoolTag);
            if (g_halp_original_performance_counter_copy == 0)
                return false;
            RtlZeroMemory((PVOID)g_halp_original_performance_counter_copy, 0xFF);
            *(ULONG64 *)(g_halp_original_performance_counter_copy +
                         kHalCounterBaseRateOffset) = kHalCounterBaseRate;
            *(ULONG *)(g_halp_original_performance_counter_copy +
                       kHalCounterTypeOffset) = kHalCounterTypePhysical;

            /* KUSER_SHARED_DATA 的 QpcBias：停止时修正睡眠唤醒的时钟漂移 */
            PLONGLONG qpc = (PLONGLONG)0xFFFFF780000003B8ull;
            g_qpc_mdl = IoAllocateMdl(qpc, 8, FALSE, FALSE, nullptr);
            if (g_qpc_mdl == nullptr)
                return false;
            MmBuildMdlForNonPagedPool(g_qpc_mdl);
            g_qpc_pointer = (PLONGLONG)MmMapLockedPagesSpecifyCache(
                g_qpc_mdl, KernelMode, MmWriteCombined, nullptr, FALSE,
                NormalPagePriority);
            if (g_qpc_pointer == nullptr)
                return false;
        }
    }

    g_init_failed = false;
    return true;
}

/* ---------------- 挂钩 / 脱钩 ---------------- */

bool AttachHooks() {
    if (g_callback == nullptr)
        return false;
    if (!MmIsAddressValid(g_get_cpu_clock))
        return false;

    g_original_get_cpu_clock = (ULONG64)*g_get_cpu_clock;

    if (g_build_number <= 18363) {
        /* 老路径：GetCpuClock 本身就是函数指针，直接换 */
        *g_get_cpu_clock = (PVOID)(ULONG_PTR)OnCpuClock;
    } else {
        /* 现代路径：GetCpuClock 是选择值。设成 2 选中
         * HalpTimerQueryHostPerformanceCounter，再换它内部的
         * HvlGetQpcBias 指针 */
        *g_get_cpu_clock = (PVOID)2;

        g_original_hvl_get_qpc_bias =
            (HvlGetQpcBiasFn)(*(ULONG64 *)g_hvl_get_qpc_bias);

        if (g_hvlp_get_reference_time_using_tsc_page != 0) {
            g_original_hvlp_get_reference_time_using_tsc_page =
                *(ULONG64 *)g_hvlp_get_reference_time_using_tsc_page;
            /* 只在原值为空时替换：原函数在数据结构未初始化时会蓝屏 */
            if (g_original_hvlp_get_reference_time_using_tsc_page == 0) {
                *(ULONG64 *)g_hvlp_get_reference_time_using_tsc_page =
                    (ULONG64)(ULONG_PTR)FakeGetReferenceTimeUsingTscPage;
            }
        }

        g_original_halp_performance_counter_type = *g_halp_performance_counter_type;
        if (g_original_halp_performance_counter_type == kHalCounterTypePhysical) {
            /* 物理机三件套（docs/04 §5） */
            *(ULONG64 *)g_halp_original_performance_counter =
                g_halp_original_performance_counter_copy;
            *g_halp_performance_counter_type = g_vm_halp_performance_counter_type;
        }

        *(ULONG64 *)g_hvl_get_qpc_bias = (ULONG64)(ULONG_PTR)FakeHvlGetQpcBias;
    }

    if (!g_thread_created) {
        g_thread_created = true;
        OBJECT_ATTRIBUTES att{};
        HANDLE thread = nullptr;
        InitializeObjectAttributes(&att, nullptr, OBJ_KERNEL_HANDLE, nullptr,
                                   nullptr);
        NTSTATUS status = PsCreateSystemThread(
            &thread, THREAD_ALL_ACCESS, &att, nullptr, &g_client_id,
            DetectThreadRoutine, nullptr);
        if (NT_SUCCESS(status)) {
            ObReferenceObjectByHandle(thread, THREAD_ALL_ACCESS, nullptr,
                                      KernelMode, (PVOID *)&g_detect_thread_object,
                                      nullptr);
            ZwClose(thread);
        }
    }

    return true;
}

NTSTATUS DetachHooks() {
    /* 先停检测线程，否则刚还原的指针可能又把它改回去 */
    g_detect_thread_status = 0;

    NTSTATUS result =
        NT_SUCCESS(NtTraceControlOp(EtwpStopTrace)) &&
        NT_SUCCESS(NtTraceControlOp(EtwpStartTrace))
            ? STATUS_SUCCESS
            : STATUS_UNSUCCESSFUL;

    if (g_detect_thread_object != nullptr) {
        KeWaitForSingleObject(g_detect_thread_object, Executive, KernelMode,
                              FALSE, nullptr);
        /* Win7 7600 只有 ObDereferenceObject，7601+ 是 ObfDereferenceObject，
         * 动态解析以兼容两者 */
        UNICODE_STRING name = RTL_CONSTANT_STRING(L"ObfDereferenceObject");
        ObfDereferenceObjectFn fn =
            (ObfDereferenceObjectFn)MmGetSystemRoutineAddress(&name);
        if (fn != nullptr) {
            fn(g_detect_thread_object);
        } else {
            UNICODE_STRING name2 = RTL_CONSTANT_STRING(L"ObDereferenceObject");
            ObDereferenceObjectFn fn2 =
                (ObDereferenceObjectFn)MmGetSystemRoutineAddress(&name2);
            if (fn2 != nullptr)
                fn2(g_detect_thread_object);
        }
        g_detect_thread_object = nullptr;
    }

    /* 还原 GetCpuClock（两条路径都要：vmp 老引擎漏了老路径的还原，
     * 这是本移植修掉的缺陷，docs/08 §4） */
    if (MmIsAddressValid(g_get_cpu_clock))
        *g_get_cpu_clock = (PVOID)g_original_get_cpu_clock;

    if (g_build_number > 18363) {
        if (g_hvlp_get_reference_time_using_tsc_page != 0 &&
            g_original_hvlp_get_reference_time_using_tsc_page == 0) {
            *(ULONG64 *)g_hvlp_get_reference_time_using_tsc_page = 0;
        }

        if (g_original_halp_performance_counter_type == kHalCounterTypePhysical) {
            LARGE_INTEGER before = KeQueryPerformanceCounter(nullptr);
            /* 顺序固定：先还原类型；HalpOriginalPerformanceCounter 保留指向
             * 假结构（还原回真值会让时间倒退导致死锁） */
            *g_halp_performance_counter_type =
                g_original_halp_performance_counter_type;

            /* 修正停止瞬间的时钟跳变，防止系统假死（docs/04 §5） */
            LARGE_INTEGER after = KeQueryPerformanceCounter(nullptr);
            if (after.QuadPart - before.QuadPart > (LONGLONG)kHalCounterBaseRate &&
                g_qpc_pointer != nullptr) {
                LONGLONG qpc_value = *g_qpc_pointer;
                qpc_value -= after.QuadPart - before.QuadPart;
                *g_qpc_pointer = qpc_value;
            }

            if (g_qpc_mdl != nullptr) {
                IoFreeMdl(g_qpc_mdl);
                g_qpc_mdl = nullptr;
                g_qpc_pointer = nullptr;
            }
        }

        if (g_hvl_get_qpc_bias != 0)
            *(ULONG64 *)g_hvl_get_qpc_bias = (ULONG64)g_original_hvl_get_qpc_bias;
    }

    g_thread_created = false;
    return result;
}

/* ---------------- 接口包装（AD_HOOK_ENGINE 的实现体） ---------------- */

bool ResolveAndAttach(adbg_engine::SyscallHookCallback callback) {
    if (callback != nullptr)
        g_callback = callback;
    if (!ResolveAll(g_callback))
        return false;
    return AttachHooks();
}

}  // namespace adbg_inf

namespace adbg_engine {

/* 启动失败时的细分：true = 解析阶段失败（FailStep=6），false = 挂钩失败（7） */
bool LastStartWasInitFailure() {
    return adbg_inf::g_init_failed;
}

/* 接口实现（hook_engine.hpp 的声明；经 adbg_inf 的桥接函数落地） */

NTSTATUS EngineStart(const SyscallHookCallback callback) {
    if (InterlockedCompareExchange(&adbg_inf::g_running, 1, 0) != 0)
        return STATUS_ALREADY_REGISTERED;
    if (!adbg_inf::ResolveAndAttach(callback)) {
        InterlockedExchange(&adbg_inf::g_running, 0);
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}

NTSTATUS EngineStop() {
    if (InterlockedCompareExchange(&adbg_inf::g_running, 0, 1) != 1)
        return STATUS_UNSUCCESSFUL;
    return adbg_inf::DetachHooks();
}

BOOLEAN EngineIsRunning() {
    return adbg_inf::g_running != 0 ? TRUE : FALSE;
}

const AD_HOOK_ENGINE InfinityHookProEngine = {
    &EngineStart,
    &EngineStop,
    &EngineIsRunning,
};

}  // namespace adbg_engine
