/* SPDX-License-Identifier: MIT */
/* control/device.hpp —— 控制层：设备对象 + IOCTL 分发 + 密钥校验。
 *
 * 手动映射的驱动没有 DRIVER_OBJECT，控制设备用 IoCreateDriver 现造一个
 * （这是映射驱动做命名设备的通行做法），再用 IoCreateDeviceSecure 上
 * 管理员 DACL。控制层是唯一能写策略的层（docs/02 §2）。
 */
#ifndef ADBG_CONTROL_DEVICE_H_
#define ADBG_CONTROL_DEVICE_H_

#include <ntddk.h>

#include <stdint.h>

extern "C" {

/* 启动线程调：创建驱动对象/设备/符号链接。secret 是启动邮箱里那份
 * 16 字节会话密钥的拷贝。 */
NTSTATUS AdbgControlCreate(const uint8_t *secret);

/* 已停止标记（DRIVER_STOP 后置位；QUERY_STATUS 会带出） */
BOOLEAN AdbgControlIsStopped(void);

}  // extern "C"

#endif /* ADBG_CONTROL_DEVICE_H_ */
