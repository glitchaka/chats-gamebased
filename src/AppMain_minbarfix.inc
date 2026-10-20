void showCompactTrayBar(App& app) {
    if (!app.unreadHwnd) return;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    SetWindowPos(app.unreadHwnd, HWND_TOPMOST,
                 work.right - kUnreadWidth - 18,
                 work.top + 18,
                 kUnreadWidth, kUnreadHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(app.unreadHwnd, nullptr, TRUE);
}

LRESULT CALLBACK unreadProcFixed(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_LBUTTONUP:
        restoreMain(*app);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        fillSolid(dc, r, RGB(0,0,0));

        if (app->unreadBar && app->unreadBar->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(dc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            g.DrawImage(app->unreadBar.get(), Gdiplus::Rect(0, 0, r.right, r.bottom));
        }

        // The source asset already supplies the full bar and its outline.
        // Only overlay the unread counter; never draw the old black HUD frame.
        if (app->unreadCount > 0) {
            const Theme& t = theme(*app);
            const std::wstring count = std::to_wstring(app->unreadCount);
            drawPixelText(*app, dc, count, r.right - 42, 14, 13, t.bg);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
