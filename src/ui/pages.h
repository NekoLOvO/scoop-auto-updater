#pragma once
#include "app.h"

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

INT_PTR CALLBACK GeneralPageProc(HWND hDlg, UINT msg, WPARAM, LPARAM lParam);
INT_PTR CALLBACK PolicyPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

struct PolicyState {
        App                             *app{};
        std::vector<std::wstring>        all;
        std::unordered_set<std::wstring> chosen;
        std::wstring                     filter;
};
