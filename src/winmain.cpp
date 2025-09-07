#include "ui/pages.h"

// helper: Load HICON from resource id
static HICON LoadResIcon(HINSTANCE h, int id, int cx = 0, int cy = 0) {
    return (HICON)
            LoadImageW(h, MAKEINTRESOURCEW(id), IMAGE_ICON, cx, cy, cx || cy ? 0 : LR_DEFAULTSIZE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App *app = (App *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
            app              = (App *)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)app);

            LoadConfig(*app);
            if (app->autostart && !IsAutostartEnabled())
                EnableAutostart();
            if (!app->autostart && IsAutostartEnabled())
                DisableAutostart();

            // tray init
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
        case WM_COMMAND:
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
        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &app->nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    INITCOMMONCONTROLSEX icc{
        sizeof(icc),
        (DWORD)(ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES)
    };
    InitCommonControlsEx(&icc);

    App app{};
    app.hInst             = hInstance;

    // load all tray icons from resources (ICO), no GDI+
    app.icoIdle           = LoadResIcon(hInstance, IDI_TRAY_IDLE);
    app.icoCheck          = LoadResIcon(hInstance, IDI_TRAY_CHECK);
    app.icoUpdate         = LoadResIcon(hInstance, IDI_TRAY_UPDATE);
    app.icoError          = LoadResIcon(hInstance, IDI_TRAY_ERROR);
    app.icoClean          = LoadResIcon(hInstance, IDI_TRAY_CLEAN);

    const wchar_t *kClass = L"ScoopAutoUpdaterWnd";
    WNDCLASSEXW    wc{sizeof(wc)};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kClass;
    wc.hIcon         = (HICON)
            LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, 0);
    if (!RegisterClassExW(&wc))
        return 0;

    HWND hwnd = CreateWindowExW(
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
    if (!hwnd)
        return 0;
    app.hwnd = hwnd;
    PreferRoundedCorners(hwnd);

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
