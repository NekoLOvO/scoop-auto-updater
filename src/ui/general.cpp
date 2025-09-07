#include "pages.h"

INT_PTR CALLBACK GeneralPageProc(HWND hDlg, UINT msg, WPARAM, LPARAM lParam) {
    App *app = (App *)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    switch (msg) {
        case WM_INITDIALOG:
            app = (App *)lParam;
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
                firstMin = 600 / 60;
            if (!ok2)
                interMin = 600 / 60;
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
