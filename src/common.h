#pragma once

#ifndef UNICODE
#    define UNICODE
#endif
#ifndef _UNICODE
#    define _UNICODE
#endif
#define _WIN32_WINNT 0x06'01

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <commctrl.h>

#include <string>
#include <vector>
#include <unordered_set>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <system_error>

#include "resource.h"

// Rounded corners helper
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#    define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#    define DWMWCP_DEFAULT    0
#    define DWMWCP_DONOTROUND 1
#    define DWMWCP_ROUND      2
#    define DWMWCP_ROUNDSMALL 3
#endif
static inline void PreferRoundedCorners(HWND hwnd) {
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (!hDwm)
        return;
    using Fn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    if (auto p = (Fn)GetProcAddress(hDwm, "DwmSetWindowAttribute")) {
        DWORD pref = DWMWCP_ROUND;
        p(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    }
    FreeLibrary(hDwm);
}

// App constants
static const wchar_t      *APP_NAME      = L"Scoop Auto Updater";

// Tray + menu IDs
static const UINT          WM_TRAYICON   = WM_APP + 1;
static const UINT          ID_TRAY       = 100;
static const UINT          IDM_CHECK_NOW = 1001;
static const UINT          IDM_CLEANUP   = 1002;
static const UINT          IDM_SETTINGS  = 1005;
static const UINT          IDM_EXIT      = 1004;

// Page save message
static const UINT          UM_PAGE_SAVE  = WM_APP + 100;

// Helpers
static inline std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return (wchar_t)::towlower((wint_t)c);
    });
    return s;
}

static inline std::wstring NowStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf(
            buf,
            64,
            L"%04u-%02u-%02u %02u:%02u:%02u",
            st.wYear,
            st.wMonth,
            st.wDay,
            st.wHour,
            st.wMinute,
            st.wSecond
    );
    return buf;
}

static inline std::wstring U82W(const std::string &s) {
    if (s.empty())
        return {};
    int          n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws;
    ws.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), n);
    return ws;
}

static inline std::string W2U8(const std::wstring &ws) {
    if (ws.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s;
    s.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline std::wstring GetEnvW(const wchar_t *name) {
    DWORD need = GetEnvironmentVariableW(name, nullptr, 0);
    if (!need)
        return L"";
    std::wstring s;
    s.resize(need);
    DWORD n = GetEnvironmentVariableW(name, s.data(), need);
    if (n && s.back() == L'\0')
        s.pop_back();
    if (n)
        s.resize(n);
    return s;
}
