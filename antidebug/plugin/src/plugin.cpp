/* SPDX-License-Identifier: MIT */
/* plugin.cpp —— 菜单/命令/事件自动化（docs/03 §8）。
 *
 * 事件模型是 SDK v1 的"按导出名自动绑定"：CBCREATEPROCESS 等
 * PLUG_EXPORT 函数由 x64dbg 在加载时按名字取走（TitanHide 同款）。
 * 自动隐藏流程：CREATEPROCESS/ATTACH 记 PID → SYSTEMBREAKPOINT 入名单 →
 * STOPDEBUG 出名单。
 */
#include "plugin.h"

#include <windows.h>

#include <cstdio>
#include <string>

#include "adbg_abi.h"
#include "control_client.hpp"

#ifdef ADBG_HAS_LOADER
#include "driver_loader.hpp"
#endif

uint32_t g_debuggee_pid = 0;
static bool g_hidden = false;

/* ---------------- 配置持久化（插件目录 ini） ---------------- */

static std::wstring SettingsIniPath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);  /* x64dbg 进程目录 */
    std::wstring dir(path);
    const size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        dir = dir.substr(0, pos + 1);
    /* 插件自身目录更合适：从本 DLL 找 */
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&SettingsIniPath, &self);
    if (self != nullptr) {
        wchar_t self_path[MAX_PATH] = {};
        if (GetModuleFileNameW(self, self_path, MAX_PATH) != 0) {
            std::wstring s(self_path);
            const size_t p = s.find_last_of(L"\\/");
            if (p != std::wstring::npos)
                return s.substr(0, p + 1) + L"antidebug_settings.ini";
        }
    }
    return dir + L"antidebug_settings.ini";
}

bool AntiDebugGetAutoHide() {
    return GetPrivateProfileIntW(L"antidebug", L"auto_hide", 1,
                                 SettingsIniPath().c_str()) != 0;
}

void AntiDebugSetAutoHide(bool enabled) {
    WritePrivateProfileStringW(L"antidebug", L"auto_hide",
                               enabled ? L"1" : L"0",
                               SettingsIniPath().c_str());
}

uint32_t AntiDebugGetSavedTechMask() {
    wchar_t text[32] = {};
    GetPrivateProfileStringW(L"antidebug", L"tech_mask", L"7FF", text, 32,
                             SettingsIniPath().c_str());
    return (uint32_t)wcstoul(text, nullptr, 16);
}

void AntiDebugSaveTechMask(uint32_t mask) {
    wchar_t text[32] = {};
    swprintf(text, 32, L"%X", mask);
    WritePrivateProfileStringW(L"antidebug", L"tech_mask", text,
                               SettingsIniPath().c_str());
}

/* ---------------- 动作 ---------------- */

static void Log(const char *text) {
    _plugin_logprintf("[%s] %s\n", PLUGIN_NAME, text);
}

bool AntiDebugActionLoad() {
#ifdef ADBG_HAS_LOADER
    Log("加载驱动中（kdmapper 手动映射，需要管理员运行的 x64dbg）...");
    antidebug::DriverLoadResult result = antidebug::DriverLoad();
    if (!result.ok) {
        _plugin_logprintf("[%s] 加载失败: %s\n", PLUGIN_NAME,
                          result.error.c_str());
        return false;
    }
    if (result.already_running) {
        Log("驱动已在运行");
    } else {
        _plugin_logprintf("[%s] 加载成功 base=0x%llX build=%u\n", PLUGIN_NAME,
                          (unsigned long long)result.image_base,
                          result.os_build);
    }
    return true;
#else
    Log("32 位插件不带加载器：请先用 x64dbg(64 位) 的本插件或 antictl.exe "
        "加载驱动（docs/02 §6）");
    return false;
#endif
}

bool AntiDebugActionStop() {
    std::string error;
    if (!antidebug::ClientStopFromRegistry(&error)) {
        _plugin_logprintf("[%s] %s\n", PLUGIN_NAME, error.c_str());
        return false;
    }
    Log("驱动已停止（镜像常驻内核直到重启）");
    g_hidden = false;
    return true;
}

bool AntiDebugActionHide() {
    if (g_debuggee_pid == 0) {
        Log("没有正在调试的进程");
        return false;
    }
    uint8_t secret[16];
    if (!antidebug::ClientLoadSecretFromRegistry(secret)) {
        Log("注册表里没有会话密钥（驱动未加载？菜单里先\"加载并启动\"）");
        return false;
    }
    HANDLE device = antidebug::ClientOpenDevice();
    if (device == INVALID_HANDLE_VALUE) {
        Log("打不开控制设备（驱动未运行）");
        return false;
    }
    NTSTATUS status = 0;
    const bool ok = antidebug::ClientSetTarget(device, secret, ADBG_TARGET_ADD,
                                               g_debuggee_pid, &status);
    antidebug::ClientCloseDevice(device);
    if (!ok) {
        _plugin_logprintf("[%s] 隐藏失败: 0x%08lX\n", PLUGIN_NAME,
                          (unsigned long)status);
        return false;
    }
    _plugin_logprintf("[%s] 已隐藏 PID %u\n", PLUGIN_NAME, g_debuggee_pid);
    g_hidden = true;
    return true;
}

bool AntiDebugActionUnhide() {
    if (g_debuggee_pid == 0)
        return false;
    uint8_t secret[16];
    if (!antidebug::ClientLoadSecretFromRegistry(secret))
        return false;
    HANDLE device = antidebug::ClientOpenDevice();
    if (device == INVALID_HANDLE_VALUE)
        return false;
    NTSTATUS status = 0;
    const bool ok = antidebug::ClientSetTarget(device, secret,
                                               ADBG_TARGET_REMOVE,
                                               g_debuggee_pid, &status);
    antidebug::ClientCloseDevice(device);
    if (ok)
        _plugin_logprintf("[%s] 已取消隐藏 PID %u\n", PLUGIN_NAME,
                          g_debuggee_pid);
    g_hidden = false;
    return ok;
}

bool AntiDebugActionHideToggle() {
    return g_hidden ? AntiDebugActionUnhide() : AntiDebugActionHide();
}

bool AntiDebugIsHidden() {
    return g_hidden;
}

void AntiDebugActionStatus() {
    if (!antidebug::ClientDeviceRunning()) {
        Log("驱动未运行");
        return;
    }
    HANDLE device = antidebug::ClientOpenDevice();
    ADBG_VERSION_INFO version = {};
    ADBG_STATUS_INFO status = {};
    antidebug::ClientQueryVersion(device, &version, nullptr);
    antidebug::ClientQueryStatus(device, &status, nullptr);
    antidebug::ClientCloseDevice(device);
    _plugin_logprintf("[%s] abi=%u driver=%u build=%u | %s\n", PLUGIN_NAME,
                      version.AbiVersion, version.DriverVersion,
                      version.OsBuildNumber,
                      antidebug::ClientDescribeStatus(status).c_str());
}

/* ---------------- 命令回调 ---------------- */

static bool cbLoad(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return AntiDebugActionLoad();
}

static bool cbUnload(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return AntiDebugActionStop();
}

static bool cbHide(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return AntiDebugActionHide();
}

static bool cbUnhide(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return AntiDebugActionUnhide();
}

static bool cbStatus(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    AntiDebugActionStatus();
    return true;
}

static bool cbOptions(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    AntiDebugShowSettingsDialog();
    return true;
}

/* ---------------- 事件（_plugin_registercallback 显式注册） ---------------- */

static void OnCreateProcess(CBTYPE cbType, void *info) {
    (void)cbType;
    g_debuggee_pid =
        ((PLUG_CB_CREATEPROCESS *)info)->fdProcessInfo->dwProcessId;
}

static void OnAttach(CBTYPE cbType, void *info) {
    (void)cbType;
    g_debuggee_pid = ((PLUG_CB_ATTACH *)info)->dwProcessId;
}

static void OnSystemBreakpoint(CBTYPE cbType, void *info) {
    (void)cbType;
    (void)info;
    if (AntiDebugGetAutoHide())
        AntiDebugActionHide();
}

static void OnStopDebug(CBTYPE cbType, void *info) {
    (void)cbType;
    (void)info;
    if (AntiDebugIsHidden())
        AntiDebugActionUnhide();
    g_debuggee_pid = 0;
}

static void OnMenuEntry(CBTYPE cbType, void *info) {
    (void)cbType;
    PLUG_CB_MENUENTRY *entry = (PLUG_CB_MENUENTRY *)info;
    switch (entry->hEntry) {
        case kMenuPanel:
            AntiDebugShowSettingsDialog();
            break;
        case kMenuLoad:
            AntiDebugActionLoad();
            break;
        case kMenuStop:
            AntiDebugActionStop();
            break;
        case kMenuHideToggle:
            AntiDebugActionHideToggle();
            break;
        case kMenuAbout:
            _plugin_logprintf(
                "[%s] antidebug v%d —— 反反调试研究插件；"
                "文档见 antidebug/docs\n",
                PLUGIN_NAME, PLUGIN_VERSION);
            break;
        default:
            break;
    }
}

/* ---------------- 初始化 / 清理 ---------------- */

void AntiDebugInit(PLUG_INITSTRUCT *initStruct) {
    (void)initStruct;
    _plugin_registercallback(pluginHandle, CB_CREATEPROCESS, OnCreateProcess);
    _plugin_registercallback(pluginHandle, CB_ATTACH, OnAttach);
    _plugin_registercallback(pluginHandle, CB_SYSTEMBREAKPOINT,
                             OnSystemBreakpoint);
    _plugin_registercallback(pluginHandle, CB_STOPDEBUG, OnStopDebug);
    _plugin_registercallback(pluginHandle, CB_MENUENTRY, OnMenuEntry);
    _plugin_registercommand(pluginHandle, "AntiDbgLoad", cbLoad, true);
    _plugin_registercommand(pluginHandle, "AntiDbgUnload", cbUnload, true);
    _plugin_registercommand(pluginHandle, "AntiDbgHide", cbHide, true);
    _plugin_registercommand(pluginHandle, "AntiDbgUnhide", cbUnhide, true);
    _plugin_registercommand(pluginHandle, "AntiDbgStatus", cbStatus, false);
    _plugin_registercommand(pluginHandle, "AntiDbgOptions", cbOptions, false);
}

void AntiDebugSetup(PLUG_SETUPSTRUCT *setupStruct) {
    (void)setupStruct;
    _plugin_menuaddentry(hMenu, kMenuPanel, "控制面板...");
    _plugin_menuaddentry(hMenu, kMenuLoad, "加载并启动");
    _plugin_menuaddentry(hMenu, kMenuStop, "停止");
    _plugin_menuaddseparator(hMenu);
    _plugin_menuaddentry(hMenu, kMenuHideToggle, "隐藏当前调试进程");
    _plugin_menuaddseparator(hMenu);
    _plugin_menuaddentry(hMenu, kMenuAbout, "关于");
}

void AntiDebugStop() {
    _plugin_unregistercallback(pluginHandle, CB_MENUENTRY);
    _plugin_unregistercallback(pluginHandle, CB_STOPDEBUG);
    _plugin_unregistercallback(pluginHandle, CB_SYSTEMBREAKPOINT);
    _plugin_unregistercallback(pluginHandle, CB_ATTACH);
    _plugin_unregistercallback(pluginHandle, CB_CREATEPROCESS);
    _plugin_unregistercommand(pluginHandle, "AntiDbgOptions");
    _plugin_unregistercommand(pluginHandle, "AntiDbgStatus");
    _plugin_unregistercommand(pluginHandle, "AntiDbgUnhide");
    _plugin_unregistercommand(pluginHandle, "AntiDbgHide");
    _plugin_unregistercommand(pluginHandle, "AntiDbgUnload");
    _plugin_unregistercommand(pluginHandle, "AntiDbgLoad");
    _plugin_menuclear(hMenu);
}
