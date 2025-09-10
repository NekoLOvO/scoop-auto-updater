#include "app.h"

// === Config ===
static std::filesystem::path LocalConfigDir() {
    PWSTR psz = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &psz))) {
        std::filesystem::path base = psz;
        CoTaskMemFree(psz);
        std::error_code ec;
        std::filesystem::create_directories(base / L"ScoopAutoUpdater", ec);
        return base / L"ScoopAutoUpdater";
    }
    auto la  = GetEnvW(L"LOCALAPPDATA");
    auto dir = (la.empty() ? std::filesystem::current_path() : std::filesystem::path(la))
             / L"ScoopAutoUpdater";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path ConfigPath() {
    return LocalConfigDir() / L"config.ini";
}

// === Autostart ===
static const wchar_t        *AUTOSTART_SHORTCUT = L"Scoop Auto Updater.lnk";

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

bool IsAutostartEnabled() {
    return std::filesystem::exists(ShortcutPath());
}

bool EnableAutostart() {
    HRESULT hr         = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool    needUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;

    bool         ok  = false;
    IShellLinkW *psl = nullptr;
    if (SUCCEEDED(CoCreateInstance(
                CLSID_ShellLink,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_IShellLinkW,
                (void **)&psl
        ))
        && psl) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::filesystem::path exep(exe);
        psl->SetPath(exe);
        psl->SetWorkingDirectory(exep.parent_path().wstring().c_str());
        psl->SetArguments(L"");
        psl->SetIconLocation(exe, 0);
        IPersistFile *ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void **)&ppf)) && ppf) {
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

void DisableAutostart() {
    std::error_code ec;
    std::filesystem::remove(ShortcutPath(), ec);
}

// === PowerShell ===
PSResult RunPowerShell(const std::wstring &command) {
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
    res.output = ToLower(out);
    return res;
}

bool RunOK(const std::wstring &cmd) {
    auto r = RunPowerShell(cmd);
    return r.exit_code == 0 && r.output.find(L"error") == std::wstring::npos;
}

// === Scoop scanning ===
static std::filesystem::path ScoopRoot() {
    auto s = GetEnvW(L"SCOOP");
    if (!s.empty())
        return s;
    auto h = GetEnvW(L"USERPROFILE");
    if (!h.empty())
        return std::filesystem::path(h) / L"scoop";
    return std::filesystem::path(L"C:\\Users\\Public\\scoop");
}

std::vector<std::wstring> ScanScoopApps() {
    std::vector<std::wstring> v;
    auto                      apps = ScoopRoot() / L"apps";
    std::error_code           ec;
    if (std::filesystem::exists(apps, ec) && std::filesystem::is_directory(apps, ec)) {
        for (auto &e: std::filesystem::directory_iterator(apps, ec)) {
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

// === Config I/O ===
void LoadConfig(App &app) {
    std::ifstream in(ConfigPath());
    if (!in) {
        app.autostart = IsAutostartEnabled();
        return;
    }
    int                              fd = app.firstDelaySec, ci = app.checkIntervalSec;
    bool                             au = IsAutostartEnabled();
    UpdateStrategy                   st = UpdateStrategy::All;
    std::unordered_set<std::wstring> sel;
    std::string                      line;
    auto                             trim = [](std::string &s) {
        while (!s.empty() && isspace((unsigned char)s.front()))
            s.erase(s.begin());
        while (!s.empty() && isspace((unsigned char)s.back()))
            s.pop_back();
    };
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        auto p = line.find('=');
        if (p == std::string::npos)
            continue;
        std::string k = line.substr(0, p), v = line.substr(p + 1);
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
                st = (t == "custom") ? UpdateStrategy::Custom : UpdateStrategy::All;
            } else if (k == "selected_apps") {
                std::stringstream ss(v);
                std::string       item;
                while (std::getline(ss, item, ',')) {
                    trim(item);
                    if (item.empty())
                        continue;
                    sel.insert(ToLower(U82W(item)));
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

void SaveConfig(const App &app) {
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

// === App methods ===
void App::SetTrayIcon(HICON h) {
    nid.hIcon  = h ? h : LoadIcon(nullptr, IDI_APPLICATION);
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void App::SetTip(const std::wstring &tip) {
    std::lock_guard<std::mutex> _l(tipMutex);
    nid.uFlags = NIF_TIP | NIF_ICON | NIF_MESSAGE | NIF_SHOWTIP;
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void App::UpdateTipLast() {
    SetTip(L"Last check: " + lastChecked);
}

bool App::HasUpdates() {
    auto r = RunPowerShell(L"scoop status");
    return r.exit_code
        == 0
        && r.output.find(L"everything is ok")
        == std::wstring::npos
        && r.output.find(L"error")
        == std::wstring::npos;
}

std::wstring App::BuildUpdateCmd() {
    if (strategy == UpdateStrategy::All || selectedApps.empty())
        return L"scoop update *";
    std::wstring cmd   = L"scoop update";
    size_t       len   = cmd.size();
    int          count = 0;
    for (const auto &n: selectedApps) {
        cmd += L" " + n;
        len += 1 + n.size();
        if (++count > 300 || len > 24000)
            return L"scoop update *";
    }
    return cmd;
}

void App::Cycle() {
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
