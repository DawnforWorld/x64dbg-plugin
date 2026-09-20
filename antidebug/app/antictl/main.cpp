/* SPDX-License-Identifier: MIT */
/* antictl —— antidebug 的命令行控制/冒烟工具（docs/03 §8.5）。
 *
 * 子命令：
 *   load             加载并启动驱动（管理员）
 *   unload           停止驱动（管理员）
 *   status           查询状态（只读，不需要管理员）
 *   hide <pid>       加入隐藏名单（管理员；密钥来自注册表）
 *   unhide <pid>     移出隐藏名单
 *   tech [mask]      查询/设置技术开关位（十六进制，如 0x2FF）
 *   smoke            完整循环：加载→查询→停止（管理员，docs/07 T3）
 *   version          工具与协议版本（离线自测，不碰驱动）
 */
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "adbg_abi.h"
#include "control_client.hpp"
#include "driver_loader.hpp"

namespace {

int CmdLoad() {
    printf("[antictl] 加载驱动...\n");
    antidebug::DriverLoadResult result = antidebug::DriverLoad();
    if (!result.ok) {
        printf("[-] %s\n", result.error.c_str());
        return 1;
    }
    if (result.already_running) {
        printf("[+] 驱动已在运行\n");
    } else {
        printf("[+] 加载成功：base=0x%llX build=%u\n",
               (unsigned long long)result.image_base, result.os_build);
        printf("    kd 加载符号: .reload /f antidebug.sys=0x%llX\n",
               (unsigned long long)result.image_base);
    }
    return 0;
}

int CmdUnload() {
    printf("[antictl] 停止驱动...\n");
    std::string error;
    if (!antidebug::ClientStopFromRegistry(&error)) {
        printf("[-] %s\n", error.c_str());
        return 1;
    }
    printf("[+] 已停止（注意：镜像常驻内核直到重启，docs/02 §4.3）\n");
    return 0;
}

int CmdStatus() {
    if (!antidebug::ClientDeviceRunning()) {
        printf("[.] 驱动未运行（\\\\.\\AntidbgCtrl 打不开）\n");
        return 2;
    }
    HANDLE device = antidebug::ClientOpenDevice();
    ADBG_VERSION_INFO version = {};
    ADBG_STATUS_INFO status = {};
    antidebug::ClientQueryVersion(device, &version, nullptr);
    antidebug::ClientQueryStatus(device, &status, nullptr);
    antidebug::ClientCloseDevice(device);
    printf("[+] abi=%u driver=%u os_build=%u flags=0x%X\n",
           version.AbiVersion, version.DriverVersion, version.OsBuildNumber,
           version.Flags);
    printf("[+] %s\n", antidebug::ClientDescribeStatus(status).c_str());
    return 0;
}

int CmdTarget(uint32_t action, uint32_t pid) {
    uint8_t secret[16];
    if (!antidebug::ClientLoadSecretFromRegistry(secret)) {
        printf("[-] 注册表里没有会话密钥（驱动未加载？先 antictl load）\n");
        return 1;
    }
    HANDLE device = antidebug::ClientOpenDevice();
    if (device == INVALID_HANDLE_VALUE) {
        printf("[-] 打不开控制设备\n");
        return 1;
    }
    NTSTATUS status = 0;
    const bool ok =
        antidebug::ClientSetTarget(device, secret, action, pid, &status);
    antidebug::ClientCloseDevice(device);
    if (!ok) {
        printf("[-] SET_TARGET 失败: 0x%08lX（密钥不对？）\n",
               (unsigned long)status);
        return 1;
    }
    printf("[+] 完成\n");
    return 0;
}

int CmdTech(const char *mask_text) {
    HANDLE device = antidebug::ClientOpenDevice();
    if (device == INVALID_HANDLE_VALUE) {
        printf("[-] 驱动未运行\n");
        return 1;
    }

    if (mask_text == nullptr) {
        ADBG_TECHNIQUES_INFO tech = {};
        ADBG_STATUS_INFO status = {};
        antidebug::ClientQueryTechniques(device, &tech, nullptr);
        antidebug::ClientQueryStatus(device, &status, nullptr);
        printf("当前开关: %s\n", antidebug::ClientHex(status.TechMask).c_str());
        for (uint32_t i = 0; i < tech.Count && i < ADBG_TECH_MAX_ENTRIES; ++i) {
            printf("  %s %-30s\n",
                   (status.TechMask & tech.Entries[i].TechBit) ? "[x]" : "[ ]",
                   tech.Entries[i].Name);
        }
        antidebug::ClientCloseDevice(device);
        return 0;
    }

    uint8_t secret[16];
    if (!antidebug::ClientLoadSecretFromRegistry(secret)) {
        antidebug::ClientCloseDevice(device);
        printf("[-] 注册表里没有会话密钥（驱动未加载？）\n");
        return 1;
    }
    const uint32_t mask = (uint32_t)strtoul(mask_text, nullptr, 0);
    NTSTATUS status = 0;
    const bool ok =
        antidebug::ClientSetTechniques(device, secret, mask, &status);
    antidebug::ClientCloseDevice(device);
    if (!ok) {
        printf("[-] SET_TECHNIQUES 失败: 0x%08lX\n", (unsigned long)status);
        return 1;
    }
    printf("[+] 技术开关已设为 0x%X\n", mask);
    return 0;
}

int CmdSmoke() {
    printf("[antictl] smoke：加载 -> 查询 -> 停止\n");
    int rc = CmdLoad();
    if (rc != 0)
        return rc;
    rc = CmdStatus();
    if (rc != 0)
        return rc;
    rc = CmdTech(nullptr);
    if (rc != 0)
        return rc;
    return CmdUnload();
}

void PrintUsage() {
    printf("用法: antictl <命令> [参数]\n"
           "  load          加载并启动驱动（管理员）\n"
           "  unload        停止驱动（管理员）\n"
           "  status        查询状态（只读）\n"
           "  hide <pid>    加入隐藏名单（管理员）\n"
           "  unhide <pid>  移出隐藏名单（管理员）\n"
           "  tech [mask]   查询/设置技术开关（如 tech 0x2FF）\n"
           "  smoke         完整循环：加载→查询→停止（管理员）\n"
           "  version       工具与协议版本（离线）\n");
}

}  // namespace

int wmain(int argc, wchar_t **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        PrintUsage();
        return 1;
    }
    const std::wstring cmd = argv[1];
    if (cmd == L"load")
        return CmdLoad();
    if (cmd == L"unload")
        return CmdUnload();
    if (cmd == L"status")
        return CmdStatus();
    if (cmd == L"hide" || cmd == L"unhide") {
        if (argc < 3) {
            printf("用法: antictl %ls <pid>\n", cmd.c_str());
            return 1;
        }
        const uint32_t pid = (uint32_t)wcstoul(argv[2], nullptr, 0);
        return CmdTarget(cmd == L"hide" ? ADBG_TARGET_ADD : ADBG_TARGET_REMOVE,
                         pid);
    }
    if (cmd == L"tech") {
        if (argc >= 3) {
            char mask_text[64] = {};
            WideCharToMultiByte(CP_ACP, 0, argv[2], -1, mask_text,
                                sizeof(mask_text), nullptr, nullptr);
            return CmdTech(mask_text);
        }
        return CmdTech(nullptr);
    }
    if (cmd == L"smoke")
        return CmdSmoke();
    if (cmd == L"version") {
        printf("antictl 1.0, abi=%u, driver=%u\n", ADBG_ABI_VERSION,
               ADBG_DRIVER_VERSION);
        return 0;
    }
    PrintUsage();
    return 1;
}
