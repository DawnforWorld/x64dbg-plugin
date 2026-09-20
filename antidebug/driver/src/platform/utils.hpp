/* SPDX-License-Identifier: MIT */
/* platform/utils.hpp —— 平台层：吸收 Windows 版本差异的基础工具。
 *
 * 来源：InfinityHookPro 参考克隆 utils.hpp 移植（逻辑等价，命名规范化）。
 * 提供：系统版本号、内核模块枚举、特征码扫描、系统调用入口定位（KPTI
 * 影子解析）、线程休眠。特征码清单见 docs/04 §4。
 */
#ifndef ADBG_PLATFORM_UTILS_H_
#define ADBG_PLATFORM_UTILS_H_

#include <ntddk.h>
#include <ntstatus.h>

namespace adbg_plat {

/* 系统内部模块枚举（ZwQuerySystemInformation class 11 的返回布局） */
typedef struct _SYS_MODULE_ENTRY {
    HANDLE Section;
    PVOID MappedBase;
    PVOID Base;
    ULONG Size;
    ULONG Flags;
    USHORT Index;
    USHORT Unknown;
    USHORT LoadCount;
    USHORT ModuleNameOffset;
    CHAR ImageName[256];
} SYS_MODULE_ENTRY, *PSYS_MODULE_ENTRY;

typedef struct _SYS_MODULES {
    ULONG_PTR Count;
    SYS_MODULE_ENTRY Modules[1];
} SYS_MODULES, *PSYS_MODULES;

/* 取系统版本号（如 19045、22000、26100） */
ULONG GetSystemBuildNumber();

/* 按文件名找内核模块基址（如 "ntoskrnl.exe"），可选带出大小 */
ULONG64 GetModuleAddress(const char *name, ULONG *out_size);

/* 在内存里做带掩码的字节匹配（szMask: 'x'=必须相等，'?'=任意） */
ULONG64 FindPattern(ULONG64 address, ULONG size,
                    const char *pattern, const char *mask);

/* 在内核模块映像的指定节里扫特征码（默认 .text），返回虚拟地址 */
ULONG64 FindPatternImage(ULONG64 image_base,
                         const char *pattern, const char *mask,
                         const char *section_name);

/* 取系统调用真正的入口地址（处理 KPTI 影子入口，docs/04 §6） */
PVOID GetSyscallEntry(ULONG64 ntoskrnl_base);

/* 毫秒级休眠（引擎检测线程用） */
void Sleep(ULONG msec);

}  // namespace adbg_plat

#endif /* ADBG_PLATFORM_UTILS_H_ */
