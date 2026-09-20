/* SPDX-License-Identifier: MIT */
/* framework/policy.hpp —— 策略层：技术开关位 + 隐藏名单（docs/02 §2 框架层）。
 *
 * 这是 vmp 缺失的"总闸门"：只有名单内的进程会被改写结果（R5）。
 * 并发模型：热路径（syscall 回调/处理函数）只做 volatile 读；控制层
 * （IOCTL 分发线程）做 Interlocked 写。这些数据只在人操作 UI 时变化，
 * 读写最多差一拍（刚关的开关多拦一次），无害——换来热路径零等待
 * （docs/02 §4.2）。
 */
#ifndef ADBG_POLICY_H_
#define ADBG_POLICY_H_

#include <ntddk.h>

#include "adbg_abi.h"

extern "C" {

/* 启动时调一次：默认技术全开、名单为空 */
void AdbgPolicyInit(void);

/* 热路径快查①：当前进程在隐藏名单里吗（内核模式调用者恒为否） */
BOOLEAN AdbgPolicyIsTargetPid(HANDLE pid);

/* 热路径快查②：技术开关位与目标进程的双条件探测 */
BOOLEAN AdbgPolicyProbe(uint32_t tech_mask);

/* 控制层写接口（IOCTL 线程调用） */
void AdbgPolicySetMask(uint32_t mask);
uint32_t AdbgPolicyGetMask(void);
NTSTATUS AdbgPolicySetTarget(uint32_t action, uint32_t pid);  /* ADBG_TARGET_* */
uint32_t AdbgPolicyGetTargets(uint32_t out_pids[ADBG_MAX_TARGETS]);

}  // extern "C"

#endif /* ADBG_POLICY_H_ */
