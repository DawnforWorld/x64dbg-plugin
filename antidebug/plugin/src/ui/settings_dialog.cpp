/* SPDX-License-Identifier: MIT */
/* settings_dialog.cpp —— 控制面板（docs/01 R7：启动/停止、隐藏、技术开关、
 * 状态显示、自动隐藏选项）。
 *
 * 控件全部运行时创建：技术清单从驱动的 QUERY_TECHNIQUES 动态枚举
 * （前端不写死，docs/02 §3 ★C），也顺带避开 .rc 的中文字符串编码问题。
 * 慢操作（驱动加载要几秒）放 worker 线程，完成后 PostMessage 回 UI 线程
 * 刷新——不卡 x64dbg 界面。
 *
 * 布局规则（历史上写死像素导致高 DPI 错位，别再退回去）：
 *   - 全部位置由 ComputePlacements() 按 DPI 统一换算，任何一行坐标都不写死；
 *   - 字体用系统消息字体（SPI_GETNONCLIENTMETRICS），不用控件缺省的点阵字体；
 *   - 窗口可拖拽缩放：WM_SIZE 重排，WM_DPICHANGED 换算后重排。
 */
#include "ui/settings_dialog.hpp"

#include <windows.h>

#include <string>
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
    HWND grp_drv = nullptr;
    HWND grp_tgt = nullptr;
    HWND grp_tech = nullptr;
    HWND drv_status = nullptr;
    HWND target_text = nullptr;
    HWND chk_autohide = nullptr;
    HWND btn_load = nullptr;
    HWND btn_stop = nullptr;
    HWND btn_hide = nullptr;
    HWND btn_unhide = nullptr;
    HWND btn_apply = nullptr;
    HWND tech_checks[ADBG_TECH_MAX_ENTRIES] = {};
    ADBG_TECHNIQUE_ENTRY tech_entries[ADBG_TECH_MAX_ENTRIES] = {};
    uint32_t tech_count = 0;
    bool busy = false;  /* worker 线程在跑时禁用按钮 */
};

DialogState g_state;
int g_dpi = 96;
HFONT g_font = nullptr;

/* ---------------- DPI 与字体 ---------------- */

int QueryDpi(HWND wnd) {
    /* GetDpiForWindow 要 Win10 1607+；拿不到就退回系统 DPI */
    using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
    static GetDpiForWindowFn fn = []() -> GetDpiForWindowFn {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        return user32 ? reinterpret_cast<GetDpiForWindowFn>(
                            reinterpret_cast<void *>(GetProcAddress(
                                user32, "GetDpiForWindow")))
                      : nullptr;
    }();
    UINT dpi = fn != nullptr ? fn(wnd) : 0;
    if (dpi == 0) {
        HDC dc = GetDC(wnd);
        dpi = dc != nullptr ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc != nullptr)
            ReleaseDC(wnd, dc);
    }
    return dpi >= 96 ? (int)dpi : 96;
}

void EnsureFont() {
    if (g_font != nullptr)
        DeleteObject(g_font);
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfHeight = -MulDiv(9, g_dpi, 72);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
}

void ApplyFontToChildren(HWND wnd) {
    if (g_font == nullptr)
        return;
    EnumChildWindows(
        wnd,
        (WNDENUMPROC)[](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, (WPARAM)lp, TRUE);
            return TRUE;
        },
        (LPARAM)g_font);
}

/* ---------------- 布局 ---------------- */

/* 一份完整的摆位。坐标全是"96 DPI 像素 × dpi/96"，禁止直接写死。 */
struct Placements {
    RECT grp_drv, grp_tgt, grp_tech;
    RECT lbl_drv, lbl_tgt, chk_auto;
    RECT btn_load, btn_stop, btn_hide, btn_unhide, btn_apply;
    RECT tech[ADBG_TECH_COUNT];
    int client_h = 0;
    int min_client_w = 0;
};

void ComputePlacements(int client_w, Placements *out) {
    /* RECT 是 {left,top,right,bottom}，不是 {x,y,w,h}——统一走这个构造器，
     * 免得再犯混写的错 */
    auto RC = [](int x, int y, int w, int h) {
        RECT r = {x, y, x + w, y + h};
        return r;
    };
    auto S = [](int v) { return MulDiv(v, g_dpi, 96); };
    const int m = S(10);       /* 窗口外边距 */
    const int p = S(12);       /* 组内边距 */
    const int cap = S(22);     /* 组框标题条高度 */
    const int btnW = S(112);
    const int btnH = S(28);
    const int lblH = S(20);
    const int chkH = S(18);
    const int pitch = S(26);   /* 技术复选框行距 */
    const int gap = S(10);     /* 组间距 */
    const int btnGapV = S(6);  /* 按钮纵向间距 */

    const int btnX = client_w - m - btnW;
    const int lblW = btnX - S(8) - (m + p);

    /* 驱动组：左边状态文字（垂直居中），右边两枚按钮竖排 */
    const int drvH = cap + btnH * 2 + btnGapV;
    out->grp_drv = RC(m, m, client_w - 2 * m, drvH);
    out->btn_load = RC(btnX, m + cap, btnW, btnH);
    out->btn_stop = RC(btnX, m + cap + btnH + btnGapV, btnW, btnH);
    out->lbl_drv = RC(m + p, m + cap + (btnH * 2 + btnGapV - lblH) / 2, lblW,
                      lblH);

    /* 目标组：左边"当前进程 + 自动隐藏"两行，右边两枚按钮竖排 */
    const int y2 = m + drvH + gap;
    const int tgtH = cap + btnH * 2 + btnGapV;
    out->grp_tgt = RC(m, y2, client_w - 2 * m, tgtH);
    out->btn_hide = RC(btnX, y2 + cap, btnW, btnH);
    out->btn_unhide = RC(btnX, y2 + cap + btnH + btnGapV, btnW, btnH);
    const int leftH = lblH + S(6) + chkH;
    const int ly = y2 + cap + ((btnH * 2 + btnGapV) - leftH) / 2;
    out->lbl_tgt = RC(m + p, ly, lblW, lblH);
    out->chk_auto = RC(m + p, ly + lblH + S(6), lblW, chkH);

    /* 技术组：两列网格（按 11 个固定算行数，窗口高度稳定），应用在右下 */
    const int y3 = y2 + tgtH + gap;
    const int rows = (ADBG_TECH_COUNT + 1) / 2;
    const int colW = (client_w - 2 * m - 2 * p - S(10)) / 2;
    const int gridTop = y3 + cap + S(2);
    for (UINT i = 0; i < ADBG_TECH_COUNT; ++i) {
        const int col = i % 2, row = i / 2;
        out->tech[i] = RC(m + p + col * (colW + S(10)), gridTop + row * pitch,
                          colW, chkH);
    }
    const int gridH = rows * pitch;
    out->btn_apply = RC(btnX, gridTop + gridH + S(4), btnW, btnH);
    const int techH = cap + S(2) + gridH + S(4) + btnH + S(6);
    out->grp_tech = RC(m, y3, client_w - 2 * m, techH);

    out->client_h = y3 + techH + m;
    out->min_client_w = 2 * (m + p) + 2 * S(180) + S(10);
}

void MoveTo(HWND ctl, const RECT &r) {
    if (ctl != nullptr)
        MoveWindow(ctl, r.left, r.top, r.right - r.left, r.bottom - r.top,
                   TRUE);
}

void Relayout(HWND wnd) {
    RECT rc = {};
    if (!GetClientRect(wnd, &rc) || rc.right <= 0)
        return;
    Placements pl;
    ComputePlacements(rc.right, &pl);
    MoveTo(g_state.grp_drv, pl.grp_drv);
    MoveTo(g_state.grp_tgt, pl.grp_tgt);
    MoveTo(g_state.grp_tech, pl.grp_tech);
    MoveTo(g_state.drv_status, pl.lbl_drv);
    MoveTo(g_state.target_text, pl.lbl_tgt);
    MoveTo(g_state.chk_autohide, pl.chk_auto);
    MoveTo(g_state.btn_load, pl.btn_load);
    MoveTo(g_state.btn_stop, pl.btn_stop);
    MoveTo(g_state.btn_hide, pl.btn_hide);
    MoveTo(g_state.btn_unhide, pl.btn_unhide);
    MoveTo(g_state.btn_apply, pl.btn_apply);
    for (UINT i = 0; i < ADBG_TECH_COUNT; ++i)
        MoveTo(g_state.tech_checks[i], pl.tech[i]);
}

/* 首次打开 / DPI 变化：让窗口外框正好包住内容高度 */
void FitWindowToContent(HWND wnd) {
    RECT rc = {};
    if (!GetClientRect(wnd, &rc))
        return;
    Placements pl;
    ComputePlacements(rc.right > 0 ? rc.right : 480, &pl);
    const int clientW = rc.right > pl.min_client_w ? rc.right
                                                   : pl.min_client_w;
    RECT want = {0, 0, clientW, pl.client_h};
    AdjustWindowRect(&want, (DWORD)GetWindowLongPtrW(wnd, GWL_STYLE), FALSE);
    SetWindowPos(wnd, nullptr, 0, 0, want.right - want.left,
                 want.bottom - want.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ---------------- 工具 ---------------- */

HWND MakeControl(HWND parent, const wchar_t *klass, const wchar_t *text,
                 DWORD style, int id) {
    return CreateWindowW(klass, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0,
                         0, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
}

void SetEnabledAll(bool enabled) {
    EnableWindow(g_state.btn_load, enabled);
    EnableWindow(g_state.btn_stop, enabled);
    EnableWindow(g_state.btn_hide, enabled);
    EnableWindow(g_state.btn_unhide, enabled);
    EnableWindow(g_state.btn_apply, enabled);
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

void CreateControls(HWND wnd) {
    g_state.grp_drv = MakeControl(wnd, L"BUTTON", L"驱动", BS_GROUPBOX, -1);
    g_state.drv_status = MakeControl(wnd, L"STATIC", L"查询中...",
                                     SS_LEFT | SS_ENDELLIPSIS, IDC_DRV_STATUS);
    g_state.btn_load =
        MakeControl(wnd, L"BUTTON", L"加载并启动", WS_TABSTOP | BS_PUSHBUTTON,
                    IDC_BTN_LOAD);
    g_state.btn_stop = MakeControl(wnd, L"BUTTON", L"停止",
                                   WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_STOP);

    g_state.grp_tgt = MakeControl(wnd, L"BUTTON", L"目标", BS_GROUPBOX, -1);
    g_state.target_text =
        MakeControl(wnd, L"STATIC", L"当前调试进程: -",
                    SS_LEFT | SS_ENDELLIPSIS, IDC_TARGET_TEXT);
    g_state.chk_autohide =
        MakeControl(wnd, L"BUTTON", L"调试开始时自动隐藏（系统断点触发）",
                    WS_TABSTOP | BS_AUTOCHECKBOX, IDC_CHK_AUTOHIDE);
    SendMessageW(g_state.chk_autohide, BM_SETCHECK,
                 AntiDebugGetAutoHide() ? BST_CHECKED : BST_UNCHECKED, 0);
    g_state.btn_hide = MakeControl(wnd, L"BUTTON", L"隐藏",
                                   WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_HIDE);
    g_state.btn_unhide = MakeControl(wnd, L"BUTTON", L"取消隐藏",
                                     WS_TABSTOP | BS_PUSHBUTTON,
                                     IDC_BTN_UNHIDE);

    g_state.grp_tech =
        MakeControl(wnd, L"BUTTON", L"技术开关（来自驱动注册表）", BS_GROUPBOX,
                    -1);
    for (UINT i = 0; i < ADBG_TECH_COUNT; ++i) {
        g_state.tech_checks[i] =
            MakeControl(wnd, L"BUTTON", L"-", WS_TABSTOP | BS_AUTOCHECKBOX,
                        IDC_CHK_TECH_FIRST + (int)i);
    }
    g_state.btn_apply = MakeControl(wnd, L"BUTTON", L"应用",
                                    WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_APPLY);

#ifndef ADBG_HAS_LOADER
    /* 32 位插件：没有加载器，禁用加载按钮并说明 */
    EnableWindow(g_state.btn_load, FALSE);
    SetWindowTextW(g_state.drv_status,
                   L"32 位插件仅控制面：请先用 x64dbg 或 antictl 加载驱动");
#endif
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_state.wnd = wnd;
            g_dpi = QueryDpi(wnd);
            EnsureFont();
            CreateControls(wnd);
            ApplyFontToChildren(wnd);
            FitWindowToContent(wnd);
            Relayout(wnd);
            RefreshStatus();
            return 0;
        }

        case WM_SIZE:
            Relayout(wnd);
            return 0;

        case WM_GETMINMAXINFO: {
            auto *mmi = (MINMAXINFO *)lp;
            RECT need = {0, 0, MulDiv(380, g_dpi, 96), MulDiv(240, g_dpi, 96)};
            AdjustWindowRect(&need, (DWORD)GetWindowLongPtrW(wnd, GWL_STYLE),
                             FALSE);
            mmi->ptMinTrackSize.x = need.right - need.left;
            mmi->ptMinTrackSize.y = need.bottom - need.top;
            return 0;
        }

        case WM_DPICHANGED: {
            g_dpi = QueryDpi(wnd);
            EnsureFont();
            ApplyFontToChildren(wnd);
            const auto *sug = (const RECT *)lp;
            SetWindowPos(wnd, nullptr, sug->left, sug->top,
                         sug->right - sug->left, sug->bottom - sug->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Relayout(wnd);
            return 0;
        }

        case WM_APP_REFRESH: {
            g_state.busy = false;
            SetEnabledAll(true);
#ifndef ADBG_HAS_LOADER
            EnableWindow(g_state.btn_load, FALSE);
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
        L"AntiDebug 控制面板", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 560, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    ShowWindow(wnd, SW_SHOWNORMAL);
    /* 无模态窗口：不跑自己的消息循环——x64dbg 的消息泵在同一线程，
     * 会替我们分发（自己跑循环会把调试器主界面卡住）。 */
}
