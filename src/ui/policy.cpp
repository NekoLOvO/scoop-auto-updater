#include "pages.h"

static void Policy_UpdateSummary(HWND hDlg, const PolicyState &st) {
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

static void Policy_RebuildList(HWND hDlg, PolicyState &st) {
    HWND hList = GetDlgItem(hDlg, IDC_LIST_APPS);
    ListView_DeleteAllItems(hList);
    auto pass = [&](const std::wstring &n) {
        return st.filter.empty() || (ToLower(n).find(st.filter) != std::wstring::npos);
    };
    int idx = 0;
    for (auto &name: st.all) {
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
    PolicyState *pst = (PolicyState *)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    switch (msg) {
        case WM_INITDIALOG: {
            pst      = new PolicyState();
            pst->app = (App *)lParam;
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
                auto *lv = (NMLISTVIEW *)lParam;
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
        case UM_PAGE_SAVE:
            pst->app->strategy     = (IsDlgButtonChecked(hDlg, IDC_RAD_CUSTOM) == BST_CHECKED)
                                           ? UpdateStrategy::Custom
                                           : UpdateStrategy::All;
            pst->app->selectedApps = pst->chosen;
            return TRUE;
        case WM_DESTROY:
            delete pst;
            return TRUE;
    }
    return FALSE;
}
