/* SPDX-License-Identifier: MIT */
/* control_client.hpp —— 与 antidebug.sys 控制设备的客户端（docs/03）。
 *
 * 同时服务 antictl / dp64 / dp32。所有收发结构来自 adbg_abi.h，
 * 本层只做"打开设备 + 发 IOCTL + 解释结果"。
 * 密钥交接：加载驱动的一方把 16 字节会话密钥写进注册表
 * （HKLM\SOFTWARE\Antidbg\SessionSecret），其他进程从这里读——
 * 这是 dp32/新实例获得控制权的通道（docs/02 §6 决策）。
 */
#ifndef ADBG_CONTROL_CLIENT_H_
#define ADBG_CONTROL_CLIENT_H_

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "adbg_abi.h"

namespace antidebug {

/* 设备是否在运行（能打开 \\.\AntidbgCtrl 即算） */
bool ClientDeviceRunning();

/* 打开/关闭控制设备。失败返回 INVALID_HANDLE_VALUE。 */
HANDLE ClientOpenDevice();
void ClientCloseDevice(HANDLE device);

/* 会话密钥的注册表交接 */
bool ClientSaveSecretToRegistry(const uint8_t secret[16]);
bool ClientLoadSecretFromRegistry(uint8_t secret[16]);
void ClientDeleteRegistrySecret();

/* 以下每个调用都要求传入已打开的设备句柄。
 * 返回 true = IOCTL 成功；NT 状态码带出给调用者打印。 */

bool ClientQueryVersion(HANDLE device, ADBG_VERSION_INFO *out,
                        NTSTATUS *status);
bool ClientQueryStatus(HANDLE device, ADBG_STATUS_INFO *out,
                       NTSTATUS *status);
bool ClientQueryTechniques(HANDLE device, ADBG_TECHNIQUES_INFO *out,
                           NTSTATUS *status);

bool ClientSessionInit(HANDLE device, const uint8_t secret[16],
                       NTSTATUS *status);
bool ClientSetTarget(HANDLE device, const uint8_t secret[16], uint32_t action,
                     uint32_t pid, NTSTATUS *status);
bool ClientSetTechniques(HANDLE device, const uint8_t secret[16],
                         uint32_t mask, NTSTATUS *status);
bool ClientDriverStop(HANDLE device, const uint8_t secret[16],
                      NTSTATUS *status);

/* 便捷封装：读注册表密钥 -> 停止驱动 -> 清掉密钥。 */
bool ClientStopFromRegistry(std::string *error);

/* 状态的可读化（日志/控制台用） */
std::string ClientDescribeStatus(const ADBG_STATUS_INFO &status);
std::string ClientHex(uint32_t value);

}  // namespace antidebug

#endif /* ADBG_CONTROL_CLIENT_H_ */
