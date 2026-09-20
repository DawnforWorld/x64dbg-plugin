/* SPDX-License-Identifier: MIT */
/* driver_loader.hpp —— 加载编排：把 antidebug.sys 送进内核（docs/02 §4.1）。
 *
 * 完整加载窗口：IntelNalProvider.Load → 填启动邮箱（含 16 字节随机
 * 会话密钥）→ KdmapperMapper 映射 → NtAddAtomInvoker 调
 * CustomDriverEntry(邮箱, 魔数) → 轮询邮箱到终态 → 卸载 iqvw64e →
 * SESSION_INIT → 密钥写注册表交接。
 *
 * 失败语义（docs/02 §6"宁可泄露不回收"）：映射后任何无法确认安全的
 * 失败（超时/读不到邮箱/启动失败）都不回收内核内存——残留到重启，
 * 错误信息里说明。
 */
#ifndef ADBG_DRIVER_LOADER_H_
#define ADBG_DRIVER_LOADER_H_

#include <windows.h>

#include <cstdint>
#include <string>

namespace antidebug {

struct DriverLoadResult {
    bool ok = false;
    bool already_running = false;  /* 驱动已在运行（本次未重复加载） */
    uint64_t image_base = 0;
    uint32_t os_build = 0;         /* 驱动上报的系统版本号 */
    uint8_t secret[16] = {0};      /* 会话密钥（也已写入注册表） */
    std::string error;             /* 失败时的人可读原因 */
};

/* 幂等：已在运行时返回 already_running。 */
DriverLoadResult DriverLoad();

/* 失败步骤码的名字（加载失败时打印用） */
const char *DriverFailStepName(int32_t step);

}  // namespace antidebug

#endif /* ADBG_DRIVER_LOADER_H_ */
