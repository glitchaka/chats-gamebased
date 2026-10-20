int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    ULONG_PTR gdiplusToken{};
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) return 1;

    if (!gToolkitIcon) gToolkitIcon = createToolkitIcon();

    App app;
    app.instance = instance;
    loadUiAssets(app);
    // Stickers are send-on-select. Do not preload the old persistent sticker
    // directory, otherwise a previously chosen image appears "stuck" forever.
    if (!app.net.start(defaultPlayerName(), 34197)) {
        MessageBoxW(nullptr, utf8ToWide(app.net.lastError()).c_str(), L"Game Chat - network error", MB_ICONERROR | MB_OK);
    }

    HICON icon = gToolkitIcon ? gToolkitIcon : LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon;
    wc.hIconSm = icon;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    WNDCLASSEXW uwc{};
    uwc.cbSize = sizeof(uwc);
    uwc.lpfnWndProc = unreadProc;
    uwc.hInstance = instance;
    uwc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    uwc.hIcon = icon;
    uwc.hIconSm = icon;
    uwc.lpszClassName = kUnreadClass;
    RegisterClassExW(&uwc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.right - kWindowWidth - 24;
    const int y = work.top + 24;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, kWindowClass, L"Game Chat", WS_POPUP,
                                x, y, kWindowWidth, kWindowHeight, nullptr, nullptr, instance, &app);
    if (!hwnd) {
        if (gToolkitIcon) { DestroyIcon(gToolkitIcon); gToolkitIcon = nullptr; }
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }

    app.unreadHwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kUnreadClass, L"",
                                     WS_POPUP, 0, 0, kUnreadWidth, kUnreadHeight, hwnd, nullptr, instance, &app);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (app.unreadHwnd && IsWindow(app.unreadHwnd)) DestroyWindow(app.unreadHwnd);
    if (gToolkitIcon) { DestroyIcon(gToolkitIcon); gToolkitIcon = nullptr; }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
