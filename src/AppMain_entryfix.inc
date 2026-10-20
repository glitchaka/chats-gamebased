int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    ULONG_PTR gdiplusToken{};
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) return 1;

    App app;
    app.instance = instance;
    loadUiAssets(app);
    const std::string playerName = defaultPlayerName();
    if (!app.net.start(playerName, 34197)) {
        MessageBoxW(nullptr, utf8ToWide(app.net.lastError()).c_str(), L"Game Chat - network error", MB_ICONERROR | MB_OK);
    }
    if (!app.audioNet.start(playerName, 34197)) {
        app.status = L"AUDIO TRANSPORT OFFLINE";
    }

    HICON icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    if (!icon) icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));

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
        app.audioNet.stop();
        app.net.stop();
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }

    app.unreadHwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                                     kUnreadClass, L"", WS_POPUP,
                                     0, 0, kMinBarWidth, kMinBarHeight,
                                     hwnd, nullptr, instance, &app);
    if (app.unreadHwnd) {
        SetLayeredWindowAttributes(app.unreadHwnd, kMinBarTransparent, 0, LWA_COLORKEY);
    }

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
    app.audioNet.stop();
    app.net.stop();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
