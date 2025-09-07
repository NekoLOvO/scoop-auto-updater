#include "pages.h"

struct Pages {
        HWND hTab{}, hGen{}, hPol{};
};

INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    App   *app   = (App *)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    Pages *pages = (Pages *)GetPropW(hDlg, L"_pages");
    switch (msg) {
        case WM_INITDIALOG: {
            app = (App *)lParam;
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
            TabCtrl_AdjustRect(hTab, FALSE, &rc);

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
        case WM_COMMAND:
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
        case WM_DESTROY:
            if (pages) {
                RemovePropW(hDlg, L"_pages");
                delete pages;
            }
            return TRUE;
    }
    return FALSE;
}
