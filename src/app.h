#pragma once
#include "common.h"

enum class UpdateStrategy {
    All,
    Custom
};

struct PSResult {
        DWORD        exit_code = 1;
        std::wstring output;
};

struct App {
        HINSTANCE                        hInst{};
        HWND                             hwnd{};
        NOTIFYICONDATA                   nid{};
        HICON                            icoIdle{}, icoCheck{}, icoUpdate{}, icoError{}, icoClean{};
        std::atomic_bool                 isBusy{false}, shouldStop{false}, wakeNow{false};
        std::atomic_int                  firstDelaySec{600}, checkIntervalSec{600};
        bool                             autostart = false;
        UpdateStrategy                   strategy  = UpdateStrategy::All;
        std::unordered_set<std::wstring> selectedApps;  // lowercase names
        std::mutex                       tipMutex;
        std::wstring                     lastChecked = L"Never";

        // tray helpers
        void                             SetTrayIcon(HICON h);
        void                             SetTip(const std::wstring &tip);
        void                             UpdateTipLast();

        // update flow
        bool                             HasUpdates();
        std::wstring                     BuildUpdateCmd();
        void                             Cycle();
};

// utilities used by App & UI
std::filesystem::path     ConfigPath();
void                      LoadConfig(App &app);
void                      SaveConfig(const App &app);
bool                      IsAutostartEnabled();
bool                      EnableAutostart();
void                      DisableAutostart();
std::vector<std::wstring> ScanScoopApps();

// PowerShell runners
PSResult                  RunPowerShell(const std::wstring &command);
bool                      RunOK(const std::wstring &cmd);
