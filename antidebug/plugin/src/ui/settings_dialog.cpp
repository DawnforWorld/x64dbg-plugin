/* SPDX-License-Identifier: MIT */
/* settings_dialog.cpp —— 控制面板（docs/01 R7：启动/停止、隐藏、技术开关、
 * 状态显示、自动隐藏选项）。
 *
 * 控件全部运行时创建：技术清单从驱动的 QUERY_TECHNIQUES 动态枚举
 * （前端不写死，docs/02 §3 ★C），也顺带避开 .rc 的中文字符串编码问题。
 * 慢操作（驱动加载要几秒）放 worker 线程，完成后 PostMessage 回 UI 线程
 * 刷新——不卡 x64dbg 界面。
 */
#include "ui/settings_dialog.hpp"

#include <windows.h>

#include <thread>

#include "adbg_abi.h"
#include "control_client.hpp"
#include "plugin.h"

#ifdef ADBG_HAS_LOADER
#include "driver_loader.hpp"
#endif

namespace {

constexpr wchar_t kClassName[] = L"AntidebugSettingsWnd";
constexpr UINT WM_APP_REFRESH = WM_APP + 1;  /* worker 完成后请求刷新 */

/* 控件 ID */
enum {
    IDC_DRV_STATUS = 100,
    IDC_BTN_LOAD,
    IDC_BTN_STOP,
    IDC_TARGET_TEXT,
    IDC_BTN_HIDE,
    IDC_BTN_UNHIDE,
    IDC_CHK_AUTOHIDE,
    IDC_CHK_TECH_FIRST = 200,  /* 200 + 技术序号，最多 11 个 */
    IDC_BTN_APPLY,
};

struct DialogState {
    HWND wnd = nullptr;
    HWND drv_status = nullptr;
    HWND target_text = nullptr;
    HWND chk_autohide = nullptr;
    HWND tech_checks[ADBG_TECH_MAX_ENTRIES] = {};
    ADBG_TECHNIQUE_ENTRY tech_entries[ADBG_TECH_MAX_ENTRIES] = {};
    uint32_t tech_count = 0;
    bool busy = false;  /* worker 线程在跑时禁用按钮 */
};

DialogState g_state;

/* ---------------- 工具 ---------------- */

HWND MakeControl(HWND parent, const wchar_t *klass, const wchar_t *text,
                 DWORD style, int x, int y, int w, int h, int id) {
    return CreateWindowW(klass, text, WS_CHILD | WS_VISIBLE | style, x, y, w,
                         h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
}

HWND MakeButton(HWND parent, const wchar_t *text, int x, int y, int w, int h,
                int id) {
    return MakeControl(parent, L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, x,
                       y, w, h, id);
}

HWND MakeCheck(HWND parent, const wchar_t *text, int x, int y, int w, int h,
               int id) {
    return MakeControl(parent, L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX,
                       x, y, w, h, id);
}

HWND MakeGroup(HWND parent, const wchar_t *text, int x, int y, int w, int h) {
    return MakeControl(parent, L"BUTTON", text, BS_GROUPBOX, x, y, w, h, -1);
}

HWND MakeLabel(HWND parent, const wchar_t *text, int x, int y, int w, int h,
               int id = -1) {
    return MakeControl(parent, L"STATIC", text, SS_LEFT, x, y, w, h, id);
}

void SetEnabledAll(bool enabled) {
    EnableWindow(GetDlgItem(g_state.wnd, IDC_BTN_LOAD), enabled);
    EnableWindow(GetDlgItem(g_state.wnd, IDC_BTN_STOP), enabled);
    EnableWindow(GetDlgItem(g_state.wnd, IDC_BTN_HIDE), enabled);
    EnableWindow(GetDlgItem(g_state.wnd, IDC_BTN_UNHIDE), enabled);
    EnableWindow(GetDlgItem(g_state.wnd, IDC_BTN_APPLY), enabled);
}

/* ---------------- 状态刷新（UI 线程） ---------------- */

void RefreshStatus() {
    if (!antidebug::ClientDeviceRunning()) {
        SetWindowTextW(g_state.drv_status, L"驱动未运行");
        SetWindowTextW(g_state.target_text, L"当前调试进程: -");
        return;
    }

    HANDLE device = antidebug::ClientOpenDevice();
    if (device == INVALID_HANDLE_VALUE) {
        SetWindowTextW(g_state.drv_status, L"驱动在运行但打不开设备");
        return;
    }

    ADBG_STATUS_INFO status = {};
    ADBG_TECHNIQUES_INFO tech = {};
    antidebug::ClientQueryStatus(device, &status, nullptr);
    const bool queried = antidebug::ClientQueryTechniques(device, &tech, nullptr);
    antidebug::ClientCloseDevice(device);

    wchar_t text[128];
    swprintf(text, 128, L"驱动运行中 | 引擎:%s | 名单:%u 个进程",
             status.EngineRunning ? L"开" : L"停", status.TargetCount);
    SetWindowTextW(g_state.drv_status, text);
    swprintf(text, 128, L"当前调试进程: %s",
             g_debuggee_pid != 0 ? std::to_wstring(g_debuggee_pid).c_str()
                                 : L"-");
    SetWindowTextW(g_state.target_text, text);

    /* 技术复选框：文本 + 勾选态跟随驱动 */
    if (queried) {
        g_state.tech_count = tech.Count;
        for (uint32_t i = 0; i < tech.Count &&
                            i < ADBG_TECH_MAX_ENTRIES; ++i) {
            g_state.tech_entries[i] = tech.Entries[i];
            wchar_t wide[64] = {};
            MultiByteToWideChar(CP_UTF8, 0, tech.Entries[i].Name, -1, wide, 64);
            SetWindowTextW(g_state.tech_checks[i], wide);
            SendMessageW(g_state.tech_checks[i], BM_SETCHECK,
                         (status.TechMask & tech.Entries[i].TechBit)
                             ? BST_CHECKED
                             : BST_UNCHECKED,
                         0);
        }
    }
}

/* ---------------- 慢操作（worker 线程） ---------------- */

void RunInWorker(void (*work)()) {
    g_state.busy = true;
    SetEnabledAll(false);
    std::thread([work]() {
        work();
        if (g_state.wnd != nullptr)
            PostMessageW(g_state.wnd, WM_APP_REFRESH, 0, 0);
    }).detach();
}

void WorkLoad() {
#ifdef ADBG_HAS_LOADER
    antidebug::DriverLoad();
#endif
}

void WorkStop() {
    std::string error;
    antidebug::ClientStopFromRegistry(&error);
}

/* ---------------- 窗口过程 ---------------- */

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_state.wnd = wnd;

            int y = 12;
            MakeGroup(wnd, L"驱动", 8, y, 456, 64);
            g_state.drv_status = MakeLabel(wnd, L"查询中...", 20, y + 20, 300,
                                           18, IDC_DRV_STATUS);
            MakeButton(wnd, L"加载并启动", 336, y + 16, 112, 26, IDC_BTN_LOAD);
            MakeButton(wnd, L"停止", 336, y + 46 - 14, 112, 26, IDC_BTN_STOP);
            y += 68;

            MakeGroup(wnd, L"目标", 8, y, 456, 62);
            g_state.target_text =
                MakeLabel(wnd, L"当前调试进程: -", 20, y + 20, 300, 18,
                          IDC_TARGET_TEXT);
            MakeButton(wnd, L"隐藏", 336, y + 14, 112, 26, IDC_BTN_HIDE);
            MakeButton(wnd, L"取消隐藏", 336, y + 44 - 12, 112, 26,
                       IDC_BTN_UNHIDE);
            g_state.chk_autohide = MakeCheck(
                wnd, L"调试开始时自动隐藏（系统断点触发）", 20, y + 40, 300,
                18, IDC_CHK_AUTOHIDE);
            SendMessageW(g_state.chk_autohide, BM_SETCHECK,
                         AntiDebugGetAutoHide() ? BST_CHECKED : BST_UNCHECKED,
                         0);
            y += 66;

            MakeGroup(wnd, L"技术开关（来自驱动注册表）", 8, y, 456, 168);
            for (int i = 0; i < (int)ADBG_TECH_COUNT; ++i) {
                g_state.tech_checks[i] = MakeCheck(
                    wnd, L"-", 20 + (i % 2) * 216, y + 20 + (i / 2) * 24, 210,
                    20, IDC_CHK_TECH_FIRST + i);
            }
            MakeButton(wnd, L"应用", 336, y + 132, 112, 26, IDC_BTN_APPLY);

#ifndef ADBG_HAS_LOADER
            /* 32 位插件：没有加载器，禁用加载按钮并说明 */
            EnableWindow(GetDlgItem(wnd, IDC_BTN_LOAD), FALSE);
            SetWindowTextW(g_state.drv_status,
                           L"32 位插件仅控制面：请先用 x64dbg 或 antictl 加载驱动");
#endif
            RefreshStatus();
            return 0;
        }

        case WM_APP_REFRESH: {
            g_state.busy = false;
            SetEnabledAll(true);
#ifndef ADBG_HAS_LOADER
            EnableWindow(GetDlgItem(wnd, IDC_BTN_LOAD), FALSE);
#endif
            RefreshStatus();
            return 0;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            switch (id) {
                case IDC_BTN_LOAD:
                    RunInWorker(&WorkLoad);
                    return 0;
                case IDC_BTN_STOP:
                    RunInWorker(&WorkStop);
                    return 0;
                case IDC_BTN_HIDE:
                    AntiDebugActionHide();
                    RefreshStatus();
                    return 0;
                case IDC_BTN_UNHIDE:
                    AntiDebugActionUnhide();
                    RefreshStatus();
                    return 0;
                case IDC_BTN_APPLY: {
                    /* 收集勾选 -> SET_TECHNIQUES（密钥来自注册表） */
                    uint8_t secret[16];
                    if (!antidebug::ClientLoadSecretFromRegistry(secret)) {
                        MessageBoxW(
                            wnd, L"注册表里没有会话密钥（驱动未加载？）",
                            L"AntiDebug", MB_ICONWARNING);
                        return 0;
                    }
                    HANDLE device = antidebug::ClientOpenDevice();
                    if (device == INVALID_HANDLE_VALUE)
                        return 0;
                    uint32_t mask = 0;
                    for (uint32_t i = 0; i < g_state.tech_count &&
                                        i < ADBG_TECH_MAX_ENTRIES; ++i) {
                        if (SendMessageW(g_state.tech_checks[i], BM_GETCHECK, 0,
                                         0) == BST_CHECKED)
                            mask |= g_state.tech_entries[i].TechBit;
                    }
                    NTSTATUS status = 0;
                    antidebug::ClientSetTechniques(device, secret, mask,
                                                   &status);
                    antidebug::ClientCloseDevice(device);
                    AntiDebugSaveTechMask(mask);  /* 记住，下次加载后可手动恢复 */
                    RefreshStatus();
                    return 0;
                }
                case IDC_CHK_AUTOHIDE:
                    AntiDebugSetAutoHide(SendMessageW(
                                             (HWND)lp, BM_GETCHECK, 0, 0) ==
                                         BST_CHECKED);
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(wnd);
            return 0;

        case WM_DESTROY:
            g_state.wnd = nullptr;
            return 0;

        default:
            break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

}  // namespace

void AntiDebugShowSettingsDialog() {
    static bool class_registered = false;
    if (!class_registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        RegisterClassW(&wc);
        class_registered = true;
    }

    if (g_state.wnd != nullptr) {  /* 已开：置前 */
        ShowWindow(g_state.wnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_state.wnd);
        RefreshStatus();
        return;
    }

    HWND wnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName,
        L"AntiDebug 控制面板", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX &
                                  ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 488, 368, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    ShowWindow(wnd, SW_SHOWNORMAL);
    /* 无模态窗口：不跑自己的消息循环——x64dbg 的消息泵在同一线程，
     * 会替我们分发（自己跑循环会把调试器主界面卡住）。 */
}
