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
#include <gdiplus.h>
#include <commctrl.h>

#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <mutex>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

#include "resource.h"

#pragma comment(lib, "gdiplus.lib")

#ifndef NIF_SHOWTIP
#    define NIF_SHOWTIP 0x00'00'00'80
#endif
#ifndef NOTIFYICON_VERSION_4
#    define NOTIFYICON_VERSION_4 4
#endif

// Window corner preference
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
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (auto p = (Fn)GetProcAddress(hDwm, "DwmSetWindowAttribute")) {
        DWORD pref = DWMWCP_ROUND;
        p(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    }
    FreeLibrary(hDwm);
}

// Defaults
static const int      DEFAULT_FIRST_DELAY_SEC    = 600;
static const int      DEFAULT_CHECK_INTERVAL_SEC = 600;

// Tray PNGs
static const wchar_t* ICON_IDLE                  = L"idle.png";
static const wchar_t* ICON_CHECK                 = L"check.png";
static const wchar_t* ICON_UPDATE                = L"update.png";
static const wchar_t* ICON_ERROR                 = L"error.png";
static const wchar_t* ICON_CLEAN                 = L"clean.png";

static const wchar_t* APP_NAME                   = L"Scoop Auto Updater";

// Tray + menu IDs
static const UINT     WM_TRAYICON                = WM_APP + 1;
static const UINT     ID_TRAY                    = 100;
static const UINT     IDM_CHECK_NOW              = 1001;
static const UINT     IDM_CLEANUP                = 1002;
static const UINT     IDM_SETTINGS               = 1005;
static const UINT     IDM_EXIT                   = 1004;

// Page save message
static const UINT     UM_PAGE_SAVE               = WM_APP + 100;

// Helpers
static std::wstring   ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return (wchar_t)::towlower((wint_t)c);
    });
    return s;
}

static std::wstring NowStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    std::swprintf(
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

static std::filesystem::path ExePath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

static std::filesystem::path ExeDir() {
    return ExePath().parent_path();
}

static std::filesystem::path IconsDir() {
    return ExeDir() / L"icons";
}

static std::wstring GetEnvW(const wchar_t* name) {
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

// UTF-8 conversion
static std::string W2U8(const std::wstring& ws) {
    if (ws.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s;
    s.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring U82W(const std::string& s) {
    if (s.empty())
        return {};
    int          n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws;
    ws.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), n);
    return ws;
}

// GDI+ RAII
struct GdiplusRAII {
        ULONG_PTR token = 0;

        GdiplusRAII() {
            Gdiplus::GdiplusStartupInput in;
            Gdiplus::GdiplusStartup(&token, &in, nullptr);
        }

        ~GdiplusRAII() {
            if (token)
                Gdiplus::GdiplusShutdown(token);
        }
};

static HICON LoadPngIcon(const std::filesystem::path& p) {
    Gdiplus::Bitmap bmp(p.wstring().c_str());
    HICON           h = nullptr;
    if (bmp.GetLastStatus() == Gdiplus::Ok)
        bmp.GetHICON(&h);
    return h;
}

// Autostart
static const wchar_t*        AUTOSTART_SHORTCUT = L"Scoop Auto Updater.lnk";

static std::filesystem::path StartupFolder() {
    PWSTR psz = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &psz))) {
        std::filesystem::path r = psz;
        CoTaskMemFree(psz);
        return r;
    }
    auto appdata = GetEnvW(L"APPDATA");
    return appdata.empty() ? std::filesystem::current_path()
                           : std::filesystem::path(appdata)
                                     / L"Microsoft\\Windows\\Start Menu\\Programs\\Startup";
}

static std::filesystem::path ShortcutPath() {
    return StartupFolder() / AUTOSTART_SHORTCUT;
}

static bool IsAutostartEnabled() {
    return std::filesystem::exists(ShortcutPath());
}

static bool EnableAutostart() {
    HRESULT hr         = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool    needUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;
    bool         ok  = false;
    IShellLinkW* psl = nullptr;
    if (SUCCEEDED(CoCreateInstance(
                CLSID_ShellLink,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_IShellLinkW,
                (void**)&psl
        ))
        && psl) {
        auto exe = ExePath();
        psl->SetPath(exe.wstring().c_str());
        psl->SetWorkingDirectory(ExeDir().wstring().c_str());
        psl->SetArguments(L"");
        psl->SetIconLocation(exe.wstring().c_str(), 0);
        IPersistFile* ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)) && ppf) {
            if (SUCCEEDED(ppf->Save(ShortcutPath().wstring().c_str(), TRUE)))
                ok = true;
            ppf->Release();
        }
        psl->Release();
    }
    if (needUninit)
        CoUninitialize();
    return ok;
}

static void DisableAutostart() {
    std::error_code ec;
    std::filesystem::remove(ShortcutPath(), ec);
}

// Run PowerShell + capture output
struct PSResult {
        DWORD        exit_code = 1;
        std::wstring output;
};

static PSResult RunPowerShell(const std::wstring& command) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE              r = nullptr, w = nullptr;
    CreatePipe(&r, &w, &sa, 0);
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = si.hStdError = w;
    PROCESS_INFORMATION pi{};
    std::wstring        cmd =
            L"powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command \""
            + command
            + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    BOOL ok = CreateProcessW(
            nullptr,
            buf.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi
    );
    CloseHandle(w);
    PSResult res{};
    if (!ok) {
        CloseHandle(r);
        return res;
    }
    std::wstring out;
    char         tmp[4096];
    DWORD        n = 0;
    while (ReadFile(r, tmp, sizeof(tmp), &n, nullptr) && n) {
        int    wlen = MultiByteToWideChar(CP_ACP, 0, tmp, (int)n, nullptr, 0);
        size_t old  = out.size();
        out.resize(old + wlen);
        MultiByteToWideChar(CP_ACP, 0, tmp, (int)n, out.data() + old, wlen);
    }
    CloseHandle(r);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &res.exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    res.output = out;
    return res;
}

static std::wstring OutputText(const PSResult& r) {
    return ToLower(r.output);
}

static bool RunOK(const std::wstring& cmd) {
    auto r = RunPowerShell(cmd);
    auto t = OutputText(r);
    return r.exit_code == 0 && t.find(L"error") == std::wstring::npos;
}

// Config
static std::filesystem::path LocalConfigDir() {
    PWSTR psz = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &psz))) {
        std::filesystem::path base = psz;
        CoTaskMemFree(psz);
        std::error_code ec;
        std::filesystem::create_directories(base / L"ScoopAutoUpdater", ec);
        return base / L"ScoopAutoUpdater";
    }
    auto            la  = GetEnvW(L"LOCALAPPDATA");
    auto            dir = (la.empty() ? ExeDir() : std::filesystem::path(la)) / L"ScoopAutoUpdater";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static std::filesystem::path ConfigPath() {
    return LocalConfigDir() / L"config.ini";
}

enum class UpdateStrategy {
    All,
    Custom
};

struct App {
        HINSTANCE        hInst{};
        HWND             hwnd{};
        NOTIFYICONDATA   nid{};
        HICON            icoIdle{}, icoCheck{}, icoUpdate{}, icoError{}, icoClean{};
        std::atomic_bool isBusy{false}, shouldStop{false}, wakeNow{false};
        std::atomic_int  firstDelaySec{DEFAULT_FIRST_DELAY_SEC},
                checkIntervalSec{DEFAULT_CHECK_INTERVAL_SEC};
        bool                             autostart = false;
        UpdateStrategy                   strategy  = UpdateStrategy::All;
        std::unordered_set<std::wstring> selectedApps;  // lowercase names
        std::mutex                       tipMutex;
        std::wstring                     lastChecked = L"Never";

        void                             SetTrayIcon(HICON h) {
            nid.hIcon  = h ? h : LoadIcon(nullptr, IDI_APPLICATION);
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
            Shell_NotifyIcon(NIM_MODIFY, &nid);
        }

        void SetTip(const std::wstring& tip) {
            std::lock_guard<std::mutex> _l(tipMutex);
            nid.uFlags = NIF_TIP | NIF_ICON | NIF_MESSAGE | NIF_SHOWTIP;
            wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
            Shell_NotifyIcon(NIM_MODIFY, &nid);
        }

        void UpdateTipLast() { SetTip(L"Last check: " + lastChecked); }

        bool HasUpdates() {
            auto r = RunPowerShell(L"scoop status");
            auto t = OutputText(r);
            return t.find(L"error")
                == std::wstring::npos
                && t.find(L"everything is ok")
                == std::wstring::npos;
        }

        std::wstring BuildUpdateCmd() {
            if (strategy == UpdateStrategy::All || selectedApps.empty())
                return L"scoop update *";
            std::wstring cmd   = L"scoop update";
            size_t       len   = cmd.size();
            int          count = 0;
            for (const auto& n: selectedApps) {
                cmd += L" " + n;
                len += 1 + n.size();
                if (++count > 300 || len > 24000)
                    return L"scoop update *";
            }
            return cmd;
        }

        void Cycle() {
            if (isBusy.exchange(true))
                return;
            SetTrayIcon(icoCheck);
            SetTip(L"Checking…");
            bool ok = RunPowerShell(L"scoop update").exit_code == 0;
            if (ok && HasUpdates()) {
                SetTrayIcon(icoUpdate);
                SetTip(L"Updating…");
                ok = RunOK(BuildUpdateCmd());
            }
            SetTrayIcon(ok ? icoIdle : icoError);
            lastChecked = NowStr();
            UpdateTipLast();
            isBusy = false;
        }
};

// Scan %SCOOP%\apps
static std::filesystem::path ScoopRoot() {
    auto s = GetEnvW(L"SCOOP");
    if (!s.empty())
        return s;
    auto h = GetEnvW(L"USERPROFILE");
    if (!h.empty())
        return std::filesystem::path(h) / L"scoop";
    return std::filesystem::path(L"C:\\Users\\Public\\scoop");
}

static std::vector<std::wstring> ScanScoopApps() {
    std::vector<std::wstring> v;
    auto                      apps = ScoopRoot() / L"apps";
    std::error_code           ec;
    if (std::filesystem::exists(apps, ec) && std::filesystem::is_directory(apps, ec)) {
        for (auto& e: std::filesystem::directory_iterator(apps, ec)) {
            if (!e.is_directory())
                continue;
            auto name = ToLower(e.path().filename().wstring());
            if (name.empty() || name[0] == L'.')
                continue;
            v.push_back(name);
        }
    }
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

// Config I/O
static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void LoadConfig(App& app) {
    std::ifstream in(ConfigPath());
    if (!in) {
        app.autostart = IsAutostartEnabled();
        return;
    }
    std::string                      line;
    int                              fd = app.firstDelaySec, ci = app.checkIntervalSec;
    bool                             au = IsAutostartEnabled();
    UpdateStrategy                   st = UpdateStrategy::All;
    std::unordered_set<std::wstring> sel;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        auto p = line.find('=');
        if (p == std::string::npos)
            continue;
        std::string k = line.substr(0, p), v = line.substr(p + 1);
        auto        trim = [](std::string& s) {
            while (!s.empty() && isspace((unsigned char)s.front()))
                s.erase(s.begin());
            while (!s.empty() && isspace((unsigned char)s.back()))
                s.pop_back();
        };
        trim(k);
        trim(v);
        try {
            if (k == "first_delay_seconds")
                fd = std::stoi(v);
            else if (k == "check_interval_seconds")
                ci = std::stoi(v);
            else if (k == "autostart")
                au = (std::stoi(v) != 0);
            else if (k == "update_strategy") {
                std::string t = v;
                std::transform(t.begin(), t.end(), t.begin(), ::tolower);
                st = (t == "custom" ? UpdateStrategy::Custom : UpdateStrategy::All);
            } else if (k == "selected_apps") {
                std::stringstream ss(v);
                std::string       item;
                while (std::getline(ss, item, ',')) {
                    trim(item);
                    if (item.empty())
                        continue;
                    std::wstring w = ToLower(U82W(item));
                    sel.insert(w);
                }
            }
        } catch (...) { }
    }
    app.firstDelaySec    = clampi(fd, 0, 24 * 60 * 60);
    app.checkIntervalSec = clampi(ci, 60, 24 * 60 * 60);
    app.autostart        = au;
    app.strategy         = st;
    app.selectedApps     = std::move(sel);
}

static void SaveConfig(const App& app) {
    std::ofstream out(ConfigPath(), std::ios::trunc);
    if (!out)
        return;
    out << "first_delay_seconds=" << app.firstDelaySec.load() << "\n";
    out << "check_interval_seconds=" << app.checkIntervalSec.load() << "\n";
    out << "autostart=" << (app.autostart ? 1 : 0) << "\n";
    out << "update_strategy=" << (app.strategy == UpdateStrategy::All ? "all" : "custom") << "\n";
    std::vector<std::wstring> v(app.selectedApps.begin(), app.selectedApps.end());
    std::sort(v.begin(), v.end());
    out << "selected_apps=";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i)
            out << ",";
        out << W2U8(v[i]);
    }
    out << "\n";
}

// ListView checkbox helpers
#ifndef INDEXTOSTATEIMAGEMASK
#    define INDEXTOSTATEIMAGEMASK(i) ((i) << 12)
#endif
static inline void LV_SetCheck(HWND h, int i, BOOL ck) {
    LVITEMW it{};
    it.stateMask = LVIS_STATEIMAGEMASK;
    it.state     = INDEXTOSTATEIMAGEMASK(ck ? 2 : 1);
    SendMessageW(h, LVM_SETITEMSTATE, i, (LPARAM)&it);
}

static inline BOOL LV_GetCheck(HWND h, int i) {
    UINT st = (UINT)SendMessageW(h, LVM_GETITEMSTATE, i, LVIS_STATEIMAGEMASK);
    return ((st & LVIS_STATEIMAGEMASK) >> 12) == 2;
}

// General page
INT_PTR CALLBACK GeneralPageProc(HWND hDlg, UINT msg, WPARAM, LPARAM lParam) {
    App* app = (App*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    switch (msg) {
        case WM_INITDIALOG:
            app = (App*)lParam;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)app);
            PreferRoundedCorners(hDlg);
            SetDlgItemInt(hDlg, IDC_EDIT_FIRST_MIN, (UINT)(app->firstDelaySec.load() / 60), FALSE);
            SetDlgItemInt(
                    hDlg,
                    IDC_EDIT_INTERVAL_MIN,
                    (UINT)(app->checkIntervalSec.load() / 60),
                    FALSE
            );
            CheckDlgButton(
                    hDlg,
                    IDC_CHECK_AUTOSTART,
                    IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED
            );
            return TRUE;
        case UM_PAGE_SAVE: {
            BOOL ok1 = FALSE, ok2 = FALSE;
            UINT firstMin = GetDlgItemInt(hDlg, IDC_EDIT_FIRST_MIN, &ok1, FALSE);
            UINT interMin = GetDlgItemInt(hDlg, IDC_EDIT_INTERVAL_MIN, &ok2, FALSE);
            if (!ok1)
                firstMin = DEFAULT_FIRST_DELAY_SEC / 60;
            if (!ok2)
                interMin = DEFAULT_CHECK_INTERVAL_SEC / 60;
            firstMin              = (UINT)clampi((int)firstMin, 0, 1440);
            interMin              = (UINT)clampi((int)interMin, 1, 1440);
            app->firstDelaySec    = (int)firstMin * 60;
            app->checkIntervalSec = (int)interMin * 60;
            app->autostart        = (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOSTART) == BST_CHECKED);
            return TRUE;
        }
    }
    return FALSE;
}

// Policy page
struct PolicyState {
        App*                             app{};
        std::vector<std::wstring>        all;
        std::unordered_set<std::wstring> chosen;
        std::wstring                     filter;
};

static void Policy_UpdateSummary(HWND hDlg, const PolicyState& st) {
    std::wstring s = L"Selected: " + std::to_wstring(st.chosen.size());
    SetDlgItemTextW(hDlg, IDC_TXT_SELECTED, s.c_str());
}

static void Policy_EnableCustom(HWND hDlg, BOOL on) {
    EnableWindow(GetDlgItem(hDlg, IDC_EDIT_SEARCH), on);
    EnableWindow(GetDlgItem(hDlg, IDC_LIST_APPS), on);
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_SELALL), on);
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_SELNONE), on);
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_SELINV), on);
}

static void Policy_RebuildList(HWND hDlg, PolicyState& st) {
    HWND hList = GetDlgItem(hDlg, IDC_LIST_APPS);
    ListView_DeleteAllItems(hList);
    auto pass = [&](const std::wstring& n) {
        return st.filter.empty() || (ToLower(n).find(st.filter) != std::wstring::npos);
    };
    int idx = 0;
    for (auto& name: st.all) {
        if (!pass(name))
            continue;
        LVITEMW it{};
        it.mask    = LVIF_TEXT;
        it.iItem   = idx;
        it.pszText = (LPWSTR)name.c_str();
        int pos    = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&it);
        LV_SetCheck(hList, pos, st.chosen.count(name) ? TRUE : FALSE);
        ++idx;
    }
    Policy_UpdateSummary(hDlg, st);
}

INT_PTR CALLBACK PolicyPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    PolicyState* pst = (PolicyState*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    switch (msg) {
        case WM_INITDIALOG: {
            pst      = new PolicyState();
            pst->app = (App*)lParam;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pst);
            PreferRoundedCorners(hDlg);
            if (pst->app->strategy == UpdateStrategy::All)
                CheckRadioButton(hDlg, IDC_RAD_ALL, IDC_RAD_CUSTOM, IDC_RAD_ALL);
            else
                CheckRadioButton(hDlg, IDC_RAD_ALL, IDC_RAD_CUSTOM, IDC_RAD_CUSTOM);
            HWND hList = GetDlgItem(hDlg, IDC_LIST_APPS);
            ListView_SetExtendedListViewStyle(
                    hList,
                    LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER
            );
            LVCOLUMNW col{};
            col.mask = LVCF_WIDTH;
            col.cx   = 220;
            SendMessageW(hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
            pst->all    = ScanScoopApps();
            pst->chosen = pst->app->selectedApps;
            pst->filter.clear();
            Policy_RebuildList(hDlg, *pst);
            Policy_EnableCustom(hDlg, (IsDlgButtonChecked(hDlg, IDC_RAD_CUSTOM) == BST_CHECKED));
            return TRUE;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_RAD_ALL:
                    Policy_EnableCustom(hDlg, FALSE);
                    return TRUE;
                case IDC_RAD_CUSTOM:
                    Policy_EnableCustom(hDlg, TRUE);
                    return TRUE;
                case IDC_EDIT_SEARCH:
                    if (HIWORD(wParam) == EN_CHANGE) {
                        wchar_t b[128];
                        GetDlgItemTextW(hDlg, IDC_EDIT_SEARCH, b, 128);
                        pst->filter = ToLower(b);
                        Policy_RebuildList(hDlg, *pst);
                    }
                    return TRUE;
                case IDC_BTN_SELALL:
                case IDC_BTN_SELNONE:
                case IDC_BTN_SELINV: {
                    HWND hList = GetDlgItem(hDlg, IDC_LIST_APPS);
                    int  n     = (int)SendMessageW(hList, LVM_GETITEMCOUNT, 0, 0);
                    for (int i = 0; i < n; ++i) {
                        wchar_t t[260];
                        LVITEMW it{};
                        it.mask       = LVIF_TEXT;
                        it.iItem      = i;
                        it.pszText    = t;
                        it.cchTextMax = 260;
                        SendMessageW(hList, LVM_GETITEMW, 0, (LPARAM)&it);
                        std::wstring name  = t;
                        bool         state = LV_GetCheck(hList, i);
                        if (LOWORD(wParam) == IDC_BTN_SELALL)
                            state = true;
                        else if (LOWORD(wParam) == IDC_BTN_SELNONE)
                            state = false;
                        else
                            state = !state;
                        LV_SetCheck(hList, i, state ? TRUE : FALSE);
                        if (state)
                            pst->chosen.insert(name);
                        else
                            pst->chosen.erase(name);
                    }
                    Policy_UpdateSummary(hDlg, *pst);
                    return TRUE;
                }
            }
            break;
        }
        case WM_NOTIFY: {
            LPNMHDR nh = (LPNMHDR)lParam;
            if (nh->idFrom == IDC_LIST_APPS && nh->code == LVN_ITEMCHANGED) {
                auto* lv = (NMLISTVIEW*)lParam;
                if ((lv->uChanged & LVIF_STATE)
                    && ((lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK)) {
                    wchar_t t[260];
                    LVITEMW it{};
                    it.mask       = LVIF_TEXT;
                    it.iItem      = lv->iItem;
                    it.pszText    = t;
                    it.cchTextMax = 260;
                    SendMessageW(nh->hwndFrom, LVM_GETITEMW, 0, (LPARAM)&it);
                    if (LV_GetCheck(nh->hwndFrom, lv->iItem))
                        pst->chosen.insert(t);
                    else
                        pst->chosen.erase(t);
                    Policy_UpdateSummary(hDlg, *pst);
                }
            }
            break;
        }
        case UM_PAGE_SAVE: {
            pst->app->strategy     = (IsDlgButtonChecked(hDlg, IDC_RAD_CUSTOM) == BST_CHECKED)
                                           ? UpdateStrategy::Custom
                                           : UpdateStrategy::All;
            pst->app->selectedApps = pst->chosen;
            return TRUE;
        }
        case WM_DESTROY:
            delete pst;
            return TRUE;
    }
    return FALSE;
}

// Settings dialog
static void Tab_AdjustChildRect(HWND hTab, RECT& rc) {
    TabCtrl_AdjustRect(hTab, FALSE, &rc);
}

struct Pages {
        HWND hTab{}, hGen{}, hPol{};
};

INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    App*   app   = (App*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    Pages* pages = (Pages*)GetPropW(hDlg, L"_pages");
    switch (msg) {
        case WM_INITDIALOG: {
            app = (App*)lParam;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)app);
            PreferRoundedCorners(hDlg);
            HWND    hTab = GetDlgItem(hDlg, IDC_TAB);
            TCITEMW it{};
            it.mask    = TCIF_TEXT;
            it.pszText = (LPWSTR)L"General";
            TabCtrl_InsertItem(hTab, 0, &it);
            it.pszText = (LPWSTR)L"Update Policy";
            TabCtrl_InsertItem(hTab, 1, &it);
            RECT rc;
            GetWindowRect(hTab, &rc);
            MapWindowPoints(nullptr, hDlg, (LPPOINT)&rc, 2);
            Tab_AdjustChildRect(hTab, rc);
            pages       = new Pages();
            pages->hTab = hTab;
            pages->hGen = CreateDialogParamW(
                    app->hInst,
                    MAKEINTRESOURCE(IDD_PAGE_GENERAL),
                    hDlg,
                    GeneralPageProc,
                    (LPARAM)app
            );
            pages->hPol = CreateDialogParamW(
                    app->hInst,
                    MAKEINTRESOURCE(IDD_PAGE_POLICY),
                    hDlg,
                    PolicyPageProc,
                    (LPARAM)app
            );
            SetPropW(hDlg, L"_pages", pages);
            SetWindowPos(
                    pages->hGen,
                    nullptr,
                    rc.left,
                    rc.top,
                    rc.right - rc.left,
                    rc.bottom - rc.top,
                    SWP_SHOWWINDOW
            );
            SetWindowPos(
                    pages->hPol,
                    nullptr,
                    rc.left,
                    rc.top,
                    rc.right - rc.left,
                    rc.bottom - rc.top,
                    SWP_HIDEWINDOW
            );
            return TRUE;
        }
        case WM_NOTIFY: {
            LPNMHDR nh = (LPNMHDR)lParam;
            if (nh->idFrom == IDC_TAB && nh->code == TCN_SELCHANGE) {
                int sel = TabCtrl_GetCurSel(pages->hTab);
                ShowWindow(pages->hGen, sel == 0 ? SW_SHOW : SW_HIDE);
                ShowWindow(pages->hPol, sel == 1 ? SW_SHOW : SW_HIDE);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDOK:
                    SendMessageW(pages->hGen, UM_PAGE_SAVE, 0, 0);
                    SendMessageW(pages->hPol, UM_PAGE_SAVE, 0, 0);
                    if (app->autostart && !IsAutostartEnabled())
                        EnableAutostart();
                    if (!app->autostart && IsAutostartEnabled())
                        DisableAutostart();
                    SaveConfig(*app);
                    app->wakeNow = true;
                    EndDialog(hDlg, IDOK);
                    return TRUE;
                case IDCANCEL:
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            if (pages) {
                RemovePropW(hDlg, L"_pages");
                delete pages;
            }
            return TRUE;
    }
    return FALSE;
}

// Tray window
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = (App*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            app              = (App*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)app);
            LoadConfig(*app);
            if (app->autostart && !IsAutostartEnabled())
                EnableAutostart();
            if (!app->autostart && IsAutostartEnabled())
                DisableAutostart();
            app->nid                  = {};
            app->nid.cbSize           = sizeof(app->nid);
            app->nid.hWnd             = hwnd;
            app->nid.uID              = ID_TRAY;
            app->nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
            app->nid.uCallbackMessage = WM_TRAYICON;
            wcsncpy_s(app->nid.szTip, L"Last check: Never", _TRUNCATE);
            app->nid.hIcon = app->icoIdle ? app->icoIdle : LoadIcon(nullptr, IDI_APPLICATION);
            Shell_NotifyIcon(NIM_ADD, &app->nid);
            app->nid.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIcon(NIM_SETVERSION, &app->nid);
            Shell_NotifyIcon(NIM_MODIFY, &app->nid);
            return 0;
        }
        case WM_TRAYICON: {
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
                SetForegroundWindow(hwnd);
                HMENU m    = CreatePopupMenu();
                UINT  busy = (app->isBusy ? MF_GRAYED : MF_ENABLED);
                AppendMenuW(m, MF_STRING | busy, IDM_CHECK_NOW, L"Check now");
                AppendMenuW(m, MF_STRING | busy, IDM_CLEANUP, L"Clean");
                AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"Settings");
                AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(m, MF_STRING, IDM_EXIT, L"Exit");
                POINT pt;
                GetCursorPos(&pt);
                UINT cmd = TrackPopupMenuEx(
                        m,
                        TPM_RETURNCMD | TPM_RIGHTBUTTON,
                        pt.x,
                        pt.y,
                        hwnd,
                        nullptr
                );
                DestroyMenu(m);
                if (cmd)
                    PostMessageW(hwnd, WM_COMMAND, cmd, 0);
                return 0;
            } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                if (!app->isBusy)
                    std::thread([app] {
                        app->Cycle();
                    }).detach();
                return 0;
            }
            break;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDM_CHECK_NOW:
                    if (!app->isBusy)
                        std::thread([app] {
                            app->Cycle();
                        }).detach();
                    return 0;
                case IDM_CLEANUP:
                    if (!app->isBusy)
                        std::thread([app] {
                            app->SetTrayIcon(app->icoClean);
                            app->SetTip(L"Cleaning…");
                            bool ok = RunOK(L"scoop cache rm *") && RunOK(L"scoop cleanup *");
                            app->SetTrayIcon(ok ? app->icoIdle : app->icoError);
                            app->UpdateTipLast();
                            app->isBusy = false;
                        }).detach();
                    return 0;
                case IDM_SETTINGS:
                    DialogBoxParamW(
                            app->hInst,
                            MAKEINTRESOURCE(IDD_SETTINGS),
                            hwnd,
                            SettingsDlgProc,
                            (LPARAM)app
                    );
                    return 0;
                case IDM_EXIT:
                    app->shouldStop = true;
                    Shell_NotifyIcon(NIM_DELETE, &app->nid);
                    PostQuitMessage(0);
                    return 0;
            }
            break;
        }
        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &app->nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Entry
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    INITCOMMONCONTROLSEX icc{
        sizeof(icc),
        (DWORD)(ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES)
    };
    InitCommonControlsEx(&icc);
    GdiplusRAII _gdi;

    App         app{};
    app.hInst             = hInstance;

    auto dir              = IconsDir();
    app.icoIdle           = LoadPngIcon(dir / ICON_IDLE);
    app.icoCheck          = LoadPngIcon(dir / ICON_CHECK);
    app.icoUpdate         = LoadPngIcon(dir / ICON_UPDATE);
    app.icoError          = LoadPngIcon(dir / ICON_ERROR);
    app.icoClean          = LoadPngIcon(dir / ICON_CLEAN);

    const wchar_t* kClass = L"ScoopAutoUpdaterWnd";
    WNDCLASSEXW    wc{sizeof(wc)};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kClass;
    wc.hIcon         = (HICON)
            LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(
            hInstance,
            MAKEINTRESOURCEW(IDI_APPICON),
            IMAGE_ICON,
            16,
            16,
            LR_DEFAULTCOLOR
    );
    if (!RegisterClassExW(&wc))
        return 0;

    app.hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kClass,
            APP_NAME,
            WS_OVERLAPPED,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            0,
            0,
            nullptr,
            nullptr,
            hInstance,
            &app
    );
    if (!app.hwnd)
        return 0;
    PreferRoundedCorners(app.hwnd);

    std::thread([&app] {
        for (int i = 0; i < app.firstDelaySec.load() && !app.shouldStop; ++i) {
            if (app.wakeNow.exchange(false))
                break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        while (!app.shouldStop) {
            app.Cycle();
            for (int i = 0, n = app.checkIntervalSec.load(); i < n && !app.shouldStop; ++i) {
                if (app.wakeNow.exchange(false))
                    break;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }).detach();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    auto destroy = [&](HICON h) {
        if (h)
            DestroyIcon(h);
    };
    destroy(app.icoIdle);
    destroy(app.icoCheck);
    destroy(app.icoUpdate);
    destroy(app.icoError);
    destroy(app.icoClean);
    return 0;
}
