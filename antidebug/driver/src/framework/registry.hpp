/* SPDX-License-Identifier: MIT */
/* framework/registry.hpp —— 注册表初始化与派发。 */
#ifndef ADBG_REGISTRY_H_
#define ADBG_REGISTRY_H_

#include <ntddk.h>

#include "engine/hook_engine.hpp"

extern "C" {

/* 启动时解析 10 个内核函数地址。任何一个失败都返回错误
 * （邮箱 FailStep=5，日志有具体名字）——vmp 只查 5/10 个的缺陷
 * 在这里修掉（docs/08 §4）。 */
NTSTATUS AdbgRegistryInit(void);

/* 引擎回调的落点：总闸门（名单）→ 地址查表 → 换写目标指针。
 * 挂到引擎上的就是它（entry.cpp 接线）。 */
void __fastcall AdbgRegistryDispatch(unsigned long index,
                                      void **system_call_function);

/* 技术层用：取第 idx 个钩子的原函数地址（调回真实实现） */
PVOID AdbgRegistryOriginal(ULONG idx);

uint32_t AdbgRegistryHookedCount(void);

}  // extern "C"

#endif /* ADBG_REGISTRY_H_ */
