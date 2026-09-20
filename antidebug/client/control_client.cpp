/* SPDX-License-Identifier: MIT */
/* control_client.cpp —— 见同头文件。 */
#include "control_client.hpp"

#include <cstdio>

namespace antidebug {

bool ClientDeviceRunning() {
    HANDLE device = ClientOpenDevice();
    if (device == INVALID_HANDLE_VALUE)
        return false;
    ClientCloseDevice(device);
    return true;
}

HANDLE ClientOpenDevice() {
    return CreateFileA(ADBG_USER_DEVICE_PATH, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void ClientCloseDevice(HANDLE device) {
    if (device != nullptr && device != INVALID_HANDLE_VALUE)
        CloseHandle(device);
}

/* ---------------- 密钥的注册表交接 ---------------- */

bool ClientSaveSecretToRegistry(const uint8_t secret[16]) {
    HKEY key = nullptr;
    LSTATUS rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, ADBG_SECRET_REG_KEY, 0,
                                 nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                 nullptr);
    if (rc != ERROR_SUCCESS)
        return false;
    rc = RegSetValueExW(key, ADBG_SECRET_REG_VALUE, 0, REG_BINARY, secret, 16);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

bool ClientLoadSecretFromRegistry(uint8_t secret[16]) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ADBG_SECRET_REG_KEY, 0, KEY_QUERY_VALUE,
                      &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    DWORD size = 16;
    LSTATUS rc = RegQueryValueExW(key, ADBG_SECRET_REG_VALUE, nullptr, &type,
                                  secret, &size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && type == REG_BINARY && size == 16;
}

void ClientDeleteRegistrySecret() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ADBG_SECRET_REG_KEY, 0, KEY_SET_VALUE,
                      &key) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(key, ADBG_SECRET_REG_VALUE);
    RegCloseKey(key);
}

/* ---------------- IOCTL 薄封装 ---------------- */

namespace {

/* 通用收发：in 可为空（查询类），out 为接收缓冲。
 * 成功 = DeviceIoControl 返回 TRUE 且 NTSTATUS 成功。 */
bool ClientIoctl(HANDLE device, uint32_t code, void *in, uint32_t in_size,
                 void *out, uint32_t out_size, DWORD *bytes_returned,
                 NTSTATUS *status) {
    DWORD returned = 0;
    /* METHOD_BUFFERED：同一缓冲区既进又出——查询类的 in 就是 out 本体 */
    void *system_buffer = (in_size > 0) ? in : out;
    if (!DeviceIoControl(device, code, system_buffer,
                         (in_size > 0) ? in_size : out_size, out, out_size,
                         &returned, nullptr)) {
        if (status != nullptr)
            *status = (NTSTATUS)GetLastError();  /* 仅作错误提示用 */
        return false;
    }
    if (bytes_returned != nullptr)
        *bytes_returned = returned;
    return true;
}

void FillHeader(ADBG_IOCTL_HEADER *header, uint32_t size) {
    header->Size = size;
    header->Version = ADBG_ABI_VERSION;
}

}  // namespace

bool ClientQueryVersion(HANDLE device, ADBG_VERSION_INFO *out,
                        NTSTATUS *status) {
    if (out == nullptr)
        return false;
    FillHeader(&out->Header, sizeof(*out));
    DWORD bytes = 0;
    if (!ClientIoctl(device, ADBG_IOCTL_QUERY_VERSION, nullptr, 0, out,
                     sizeof(*out), &bytes, status))
        return false;
    return bytes == sizeof(*out);
}

bool ClientQueryStatus(HANDLE device, ADBG_STATUS_INFO *out, NTSTATUS *status) {
    if (out == nullptr)
        return false;
    FillHeader(&out->Header, sizeof(*out));
    DWORD bytes = 0;
    if (!ClientIoctl(device, ADBG_IOCTL_QUERY_STATUS, nullptr, 0, out,
                     sizeof(*out), &bytes, status))
        return false;
    return bytes == sizeof(*out);
}

bool ClientQueryTechniques(HANDLE device, ADBG_TECHNIQUES_INFO *out,
                           NTSTATUS *status) {
    if (out == nullptr)
        return false;
    FillHeader(&out->Header, sizeof(*out));
    DWORD bytes = 0;
    if (!ClientIoctl(device, ADBG_IOCTL_QUERY_TECHNIQUES, nullptr, 0, out,
                     sizeof(*out), &bytes, status))
        return false;
    return bytes == sizeof(*out);
}

bool ClientSessionInit(HANDLE device, const uint8_t secret[16],
                       NTSTATUS *status) {
    ADBG_SESSION_INIT request = {};
    FillHeader(&request.Header, sizeof(request));
    memcpy(request.Secret, secret, 16);
    return ClientIoctl(device, ADBG_IOCTL_SESSION_INIT, &request,
                       sizeof(request), &request, sizeof(request), nullptr,
                       status);
}

bool ClientSetTarget(HANDLE device, const uint8_t secret[16], uint32_t action,
                     uint32_t pid, NTSTATUS *status) {
    ADBG_SET_TARGET request = {};
    FillHeader(&request.Header, sizeof(request));
    memcpy(request.Secret, secret, 16);
    request.Action = action;
    request.Pid = pid;
    return ClientIoctl(device, ADBG_IOCTL_SET_TARGET, &request,
                       sizeof(request), &request, sizeof(request), nullptr,
                       status);
}

bool ClientSetTechniques(HANDLE device, const uint8_t secret[16],
                         uint32_t mask, NTSTATUS *status) {
    ADBG_SET_TECHNIQUES request = {};
    FillHeader(&request.Header, sizeof(request));
    memcpy(request.Secret, secret, 16);
    request.TechMask = mask;
    return ClientIoctl(device, ADBG_IOCTL_SET_TECHNIQUES, &request,
                       sizeof(request), &request, sizeof(request), nullptr,
                       status);
}

bool ClientDriverStop(HANDLE device, const uint8_t secret[16],
                      NTSTATUS *status) {
    ADBG_DRIVER_STOP request = {};
    FillHeader(&request.Header, sizeof(request));
    memcpy(request.Secret, secret, 16);
    return ClientIoctl(device, ADBG_IOCTL_DRIVER_STOP, &request,
                       sizeof(request), &request, sizeof(request), nullptr,
                       status);
}

bool ClientStopFromRegistry(std::string *error) {
    uint8_t secret[16];
    if (!ClientLoadSecretFromRegistry(secret)) {
        if (error != nullptr)
            *error = "注册表里没有会话密钥（驱动未加载或已停止）";
        return false;
    }
    HANDLE device = ClientOpenDevice();
    if (device == INVALID_HANDLE_VALUE) {
        if (error != nullptr)
            *error = "打不开 \\\\.\\AntidbgCtrl（驱动未运行）";
        return false;
    }
    NTSTATUS status = 0;
    const bool ok = ClientDriverStop(device, secret, &status);
    ClientCloseDevice(device);
    ClientDeleteRegistrySecret();
    if (!ok && error != nullptr) {
        char buf[64];
        snprintf(buf, sizeof(buf), "DRIVER_STOP 失败: 0x%08lX",
                 (unsigned long)status);
        *error = buf;
    }
    return ok;
}

/* ---------------- 可读化 ---------------- */

std::string ClientHex(uint32_t value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%03X", value);
    return buf;
}

std::string ClientDescribeStatus(const ADBG_STATUS_INFO &status) {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "engine=%s hooked=%u tech=%s targets=%u(",
             status.EngineRunning ? "running" : "stopped",
             status.HookedSyscallCount, ClientHex(status.TechMask).c_str(),
             status.TargetCount);
    std::string text = buf;
    for (uint32_t i = 0; i < status.TargetCount && i < ADBG_MAX_TARGETS; ++i) {
        char pid[16];
        snprintf(pid, sizeof(pid), "%s%u", i ? "," : "", status.TargetPids[i]);
        text += pid;
    }
    text += ")";
    return text;
}

}  // namespace antidebug
