/* SPDX-License-Identifier: MIT */
/* engine/hook_engine.hpp —— ★A 可换接口：hook 引擎（docs/02 §3）。
 *
 * 引擎的职责只有一件事：在每次用户态系统调用发生时，把回调叫一次，
 * 交给它一个"指向本次调用目标内核函数地址"的指针——回调改写这个指针
 * 就完成一次拦截。
 *
 * 为什么用函数指针结构体而不是 C++ 虚类：内核镜像无 CRT（docs/02 §6），
 * 这种形式不需要构造函数，全局可零初始化，效果等价于 vtable。
 *
 * 默认实现：InfinityHookProEngine（engine/infinity_hook_pro.cpp）。
 * 换引擎 = 写一个新的 const AD_HOOK_ENGINE 实例，在 entry.cpp 换一行挂载。
 */
#ifndef ADBG_HOOK_ENGINE_H_
#define ADBG_HOOK_ENGINE_H_

#include <ntddk.h>

namespace adbg_engine {

/* 回调签名（与 InfinityHook 引擎原生回调一致）：
 *   index                 系统调用号（参考用，当前派发不依赖它）
 *   system_call_function  指向"本次系统调用要跳去的内核函数地址"的槽位；
 *                         回调里改写 *system_call_function 即拦截。
 * 调用环境：发起系统调用的用户线程上下文，禁止加锁/分配/等待。 */
typedef void(__fastcall *SyscallHookCallback)(unsigned long index,
                                              void **system_call_function);

struct AD_HOOK_ENGINE {
    NTSTATUS (*Start)(const SyscallHookCallback callback);
    NTSTATUS (*Stop)();      /* 必须完整还原所有改过的指针（docs/04 §7） */
    BOOLEAN (*IsRunning)();
};

/* 默认实现（InfinityHookPro 移植，支持 Win7~Win11 24H2） */
extern const AD_HOOK_ENGINE InfinityHookProEngine;

/* 启动失败时的细分：true = 解析（特征码）阶段失败（FailStep=6），
 * false = 挂钩失败（FailStep=7）。仅 Start 失败后有意义。 */
bool LastStartWasInitFailure();

}  // namespace adbg_engine

#endif /* ADBG_HOOK_ENGINE_H_ */
