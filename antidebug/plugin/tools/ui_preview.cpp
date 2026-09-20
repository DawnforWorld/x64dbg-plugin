/* SPDX-License-Identifier: MIT */
/* ui_preview.cpp —— 控制面板布局预览夹具（开发期自检用，不进插件产物）。
 *
 * 直接 #include 真实的 settings_dialog.cpp，桩掉 plugin 层与控制面客户端，
 * 渲染出两种窗口尺寸并各存一张 PNG，用来看布局有没有错位/截断。
 *
 * 构建（x64 开发者命令行，仓库根目录执行）：
 *   cl /nologo /EHsc /std:c++17 /MT /utf-8 /W3 ^
 *     /I antidebug\plugin\src /I antidebug\include /I antidebug\client ^
 *     /I third_party\x64dbg-pluginsdk ^
 *     antidebug\plugin\tools\ui_preview.cpp ^
 *     /Fo:build\preview\ /Fe:build\preview\ui_preview.exe ^
 *     user32.lib gdi32.lib gdiplus.lib
 *
 * 运行：build\preview\ui_preview.exe build
 */
#include "../src/ui/settings_dialog.cpp"

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

/* ---------------- 桩：plugin 层 ---------------- */

uint32_t g_debuggee_pid = 4321;

bool AntiDebugActionLoad() { return true; }
bool AntiDebugActionStop() { return true; }
bool AntiDebugActionHide() { OutputDebugStringW(L"hide\n"); return true; }
bool AntiDebugActionUnhide() { return true; }
bool AntiDebugActionHideToggle() { return true; }
void AntiDebugActionStatus() {}
bool AntiDebugIsHidden() { return false; }
bool AntiDebugGetAutoHide() { return true; }
void AntiDebugSetAutoHide(bool) {}
uint32_t AntiDebugGetSavedTechMask() { return 0x0555; }
void AntiDebugSaveTechMask(uint32_t) {}

/* ---------------- 桩：控制面客户端（模拟"驱动运行中"） ---------------- */

static const char *kStubTechNames[ADBG_TECH_COUNT] = {
    "ProcessDebugObjectHandle",      "ProcessDebugPort",
    "ProcessDebugFlags",             "ThreadHideFromDebugger(set)",
    "ThreadHideFromDebugger(query)", "ProtectedHandle(NtClose/Dup)",
    "KernelDebuggerInformation",     "NtSystemDebugControl",
    "DrRegisters(x64)",              "DrRegisters(WOW64)",
    "ThreadCreateHideFlag",
};

namespace antidebug {

bool ClientDeviceRunning() { return true; }
HANDLE ClientOpenDevice() { return (HANDLE)(INT_PTR)0x00010001; }
void ClientCloseDevice(HANDLE) {}

bool ClientQueryStatus(HANDLE, ADBG_STATUS_INFO *out, NTSTATUS *) {
    out->EngineRunning = 1;
    out->TargetCount = 2;
    out->TechMask = 0x0555;
    out->TargetPids[0] = 4321;
    out->TargetPids[1] = 8642;
    return true;
}

bool ClientQueryTechniques(HANDLE, ADBG_TECHNIQUES_INFO *out, NTSTATUS *) {
    out->Count = ADBG_TECH_COUNT;
    for (UINT i = 0; i < ADBG_TECH_COUNT; ++i) {
        out->Entries[i].TechBit = 1u << i;
        strncpy_s(out->Entries[i].Name, kStubTechNames[i], _TRUNCATE);
    }
    return true;
}

bool ClientSessionInit(HANDLE, const uint8_t[16], NTSTATUS *st) {
    if (st != nullptr) *st = 0;
    return true;
}
bool ClientSetTechniques(HANDLE, const uint8_t[16], uint32_t, NTSTATUS *st) {
    if (st != nullptr) *st = 0;
    return true;
}
bool ClientLoadSecretFromRegistry(uint8_t secret[16]) {
    memset(secret, 0xAB, 16);
    return true;
}
bool ClientStopFromRegistry(std::string *) { return true; }

}  // namespace antidebug

/* ---------------- 截图 ---------------- */

static int GetEncoderClsid(const WCHAR *mime, CLSID *clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
        return -1;
    std::vector<char> buf(size);
    auto *eps = (Gdiplus::ImageCodecInfo *)buf.data();
    Gdiplus::GetImageEncoders(num, size, eps);
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(eps[i].MimeType, mime) == 0) {
            *clsid = eps[i].Clsid;
            return (int)i;
        }
    }
    return -1;
}

static bool SaveWindowPng(HWND wnd, const wchar_t *path) {
    RECT rc = {};
    GetWindowRect(wnd, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC wdc = GetWindowDC(wnd);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, w, h);
    HDC mem = CreateCompatibleDC(wdc);
    HGDIOBJ old = SelectObject(mem, bmp);
    PrintWindow(wnd, mem, PW_RENDERFULLCONTENT);

    Gdiplus::GdiplusStartupInput si;
    ULONG_PTR tok = 0;
    Gdiplus::GdiplusStartup(&tok, &si, nullptr);
    {
        Gdiplus::Bitmap bm(bmp, nullptr);
        CLSID clsid;
        if (GetEncoderClsid(L"image/png", &clsid) >= 0)
            bm.Save(path, &clsid, nullptr);
    }
    Gdiplus::GdiplusShutdown(tok);

    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(wnd, wdc);
    return true;
}

static void Pump(DWORD ms) {
    MSG m = {};
    DWORD deadline = GetTickCount() + ms;
    for (;;) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (GetTickCount() >= deadline)
            break;
        Sleep(10);
    }
}

int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    auto Path = [&](const wchar_t *name) -> const wchar_t * {
        static wchar_t buf[MAX_PATH];
        wchar_t wdir[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, dir.c_str(), -1, wdir, MAX_PATH);
        swprintf(buf, MAX_PATH, L"%s\\%s", wdir, name);
        return buf;
    };

    AntiDebugShowSettingsDialog();
    Pump(300);
    HWND wnd = FindWindowW(L"AntidebugSettingsWnd", nullptr);
    if (wnd == nullptr) {
        wprintf(L"panel window not found\n");
        return 1;
    }
    EnumChildWindows(
        wnd,
        (WNDENUMPROC)[](HWND c, LPARAM) -> BOOL {
            wchar_t cls[64] = L"?";
            RECT r = {};
            GetClassNameW(c, cls, 64);
            GetWindowRect(c, &r);
            wprintf(L"child %s rect=(%ld,%ld %ldx%ld)\n", cls,
                    (long)r.left, (long)r.top, (long)(r.right - r.left),
                    (long)(r.bottom - r.top));
            return TRUE;
        },
        0);
    fflush(stdout);
    SaveWindowPng(wnd, Path(L"antidebug_panel_default.png"));

    /* 用户拖窄：列宽自适应、状态文字省略号 */
    SetWindowPos(wnd, nullptr, 0, 0, 460, 640, SWP_NOMOVE | SWP_NOZORDER);
    Pump(300);
    SaveWindowPng(wnd, Path(L"antidebug_panel_narrow.png"));

    DestroyWindow(wnd);
    Pump(100);
    return 0;
}
