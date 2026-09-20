/* SPDX-License-Identifier: MIT */
/* framework/technique.hpp —— ★C 声明式技术注册表（docs/02 §3）。
 *
 * 对比 vmp 的做法（if/else 连锁 + 加一个钩子改 5 处）：这里加一项
 * 新技术 = 在 kTechniqueRows 加一行 + 写一个处理函数文件；派发、策略、
 * UI 清单（QUERY_TECHNIQUES）全部自动跟上。
 */
#ifndef ADBG_TECHNIQUE_H_
#define ADBG_TECHNIQUE_H_

#include <ntddk.h>

#include "adbg_abi.h"

/* 注册表行号：技术层用它取"原函数指针"（RegistryOriginal） */
enum AdbgHookId {
    kHookNtQueryInformationProcess = 0,
    kHookNtSetInformationThread,
    kHookNtClose,
    kHookNtQuerySystemInformation,
    kHookNtQueryInformationThread,
    kHookNtGetContextThread,
    kHookNtSetContextThread,
    kHookNtSystemDebugControl,
    kHookNtDuplicateObject,
    kHookNtCreateThreadEx,
    kHookCount
};

/* 一行 = 一个被拦的内核函数。Handler 与该函数原型一致（nt_extra.hpp）。
 * TechMask：这一行服务哪些技术位（一位或多位的或）——处理函数内部
 * 会按位细分。 */
struct TechniqueRow {
    const char *NtName;
    void *Handler;
    uint32_t TechMask;
};

extern const TechniqueRow kTechniqueRows[kHookCount];

#endif /* ADBG_TECHNIQUE_H_ */
