/* SPDX-License-Identifier: MIT */
/* platform/utils.cpp —— 见同头文件。来源：InfinityHookPro utils.hpp 移植。 */
#include "platform/utils.hpp"

#include <ntstrsafe.h>

#include "platform/hde/hde64.h"

namespace adbg_plat {

namespace {

constexpr ULONG kPoolTag = 'Adbg';
constexpr ULONG kSystemModuleInformation = 11;

/* wdm.h 里 ZwQuerySystemInformation 的声明可能带废弃标记或缺失，
 * 自己按 ULONG 原型声明（x64 下与真实签名二进制等价） */
extern "C" NTSTATUS NTAPI ZwQuerySystemInformation(
    ULONG SystemInformationClass, PVOID SystemInformation,
    ULONG SystemInformationLength, PULONG ReturnLength);

bool PatternCheck(const char *data, const char *pattern, const char *mask) {
    for (size_t i = 0; mask[i] != '\0'; ++i) {
        if (mask[i] == 'x' && data[i] != pattern[i])
            return false;
    }
    return true;
}

ULONG64 FindSectionAddress(ULONG64 image_base, const char *section_name,
                           ULONG *out_size) {
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image_base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    const IMAGE_NT_HEADERS64 *nt =
        (const IMAGE_NT_HEADERS64 *)(image_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (USHORT i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER *cur = &sec[i];
        if (strstr((const char *)cur->Name, section_name) != nullptr) {
            if (out_size != nullptr)
                *out_size = cur->Misc.VirtualSize;
            return image_base + cur->VirtualAddress;
        }
    }
    return 0;
}

}  // namespace

ULONG GetSystemBuildNumber() {
    RTL_OSVERSIONINFOEXW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (!NT_SUCCESS(RtlGetVersion((PRTL_OSVERSIONINFOW)&info)))
        return 0;
    return info.dwBuildNumber;
}

ULONG64 GetModuleAddress(const char *name, ULONG *out_size) {
    ULONG length = 0;
    ZwQuerySystemInformation(kSystemModuleInformation, nullptr, 0, &length);
    if (length == 0)
        return 0;

    PSYS_MODULES modules =
        (PSYS_MODULES)ExAllocatePoolWithTag(NonPagedPool, length, kPoolTag);
    if (modules == nullptr)
        return 0;

    ULONG64 result = 0;
    if (NT_SUCCESS(ZwQuerySystemInformation(kSystemModuleInformation, modules,
                                            length, nullptr))) {
        for (ULONG_PTR i = 0; i < modules->Count; ++i) {
            const SYS_MODULE_ENTRY *mod = &modules->Modules[i];
            if (strstr(mod->ImageName, name) != nullptr) {
                result = (ULONG64)mod->Base;
                if (out_size != nullptr)
                    *out_size = mod->Size;
                break;
            }
        }
    }

    ExFreePoolWithTag(modules, kPoolTag);
    return result;
}

ULONG64 FindPattern(ULONG64 address, ULONG size,
                    const char *pattern, const char *mask) {
    const ULONG mask_len = (ULONG)strlen(mask);
    if (size <= mask_len)
        return 0;
    const ULONG limit = size - mask_len;
    for (ULONG i = 0; i < limit; ++i) {
        if (PatternCheck((const char *)(address + i), pattern, mask))
            return address + i;
    }
    return 0;
}

ULONG64 FindPatternImage(ULONG64 image_base,
                         const char *pattern, const char *mask,
                         const char *section_name) {
    if (section_name == nullptr)
        section_name = ".text";

    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image_base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    const IMAGE_NT_HEADERS64 *nt =
        (const IMAGE_NT_HEADERS64 *)(image_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;

    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (USHORT i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER *cur = &sec[i];
        if (strstr((const char *)cur->Name, section_name) == nullptr)
            continue;
        ULONG64 result = FindPattern(image_base + cur->VirtualAddress,
                                     cur->Misc.VirtualSize, pattern, mask);
        if (result != 0)
            return result;
    }
    return 0;
}

PVOID GetSyscallEntry(ULONG64 ntoskrnl_base) {
    if (ntoskrnl_base == 0)
        return nullptr;

    /* IA32_LSTAR MSR 存着系统调用入口；2018 年的内核页表隔离补丁
     * (KPTI) 把它指向 KVASCODE 节里的"影子"入口，真正入口要顺着
     * 影子代码里第一条跳出该节的 jmp 找（docs/04 §6）。 */
    constexpr ULONG kIa32LstarMsr = 0xC0000082;
    PVOID entry = (PVOID)__readmsr(kIa32LstarMsr);

    ULONG kvas_size = 0;
    ULONG64 kvas = FindSectionAddress(ntoskrnl_base, "KVASCODE", &kvas_size);
    if (kvas == 0)
        return entry;  /* 没打补丁：入口本来就是真的 */

    if (!((ULONG64)entry >= kvas && (ULONG64)entry < kvas + kvas_size))
        return entry;  /* 入口不在影子节里：也是真的 */

    hde64s hde{};
    for (char *p = (char *)entry;; p += hde.len) {
        if (hde64_disasm(p, &hde) == 0)
            break;
        if (hde.opcode != 0xE9)  /* 只关心 jmp rel32 */
            continue;
        PVOID possible = (PVOID)(p + (int)hde.len + (int)hde.imm.imm32);
        if ((ULONG64)possible >= kvas && (ULONG64)possible < kvas + kvas_size)
            continue;  /* 影子节内部的跳转不算 */
        entry = possible;
        break;
    }
    return entry;
}

void Sleep(ULONG msec) {
    LARGE_INTEGER delay{};
    delay.QuadPart = -10000;  /* 负数=相对时间，单位 100ns；这里换算成毫秒 */
    delay.QuadPart *= msec;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
}

}  // namespace adbg_plat
