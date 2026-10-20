void showCompactTrayBar(App& app) {
    if (!app.unreadHwnd) return;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    SetWindowPos(app.unreadHwnd, HWND_TOPMOST,
                 work.right - kUnreadWidth - 18,
                 work.top + 18,
                 kUnreadWidth, kUnreadHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // Match the actual toolkit bar silhouette instead of showing a rectangular
    // black helper window around it.
    POINT shape[] = {
        {0, 0},
        {kUnreadWidth - 1, 0},
        {kUnreadWidth - 1, kUnreadHeight - 10},
        {kUnreadWidth - 11, kUnreadHeight - 1},
        {0, kUnreadHeight - 1}
    };
    HRGN region = CreatePolygonRgn(shape, static_cast<int>(std::size(shape)), WINDING);
    if (region) SetWindowRgn(app.unreadHwnd, region, TRUE);
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
        const Theme& t = theme(*app);

        // One bar only. No title, no counter text, no second HUD layered on top.
        fillSolid(dc, r, t.panel2);

        const int unread = std::min(app->unreadCount, kUnreadMax);
        if (unread > 0) {
            const int innerWidth = std::max(0, r.right - 10);
            RECT fill{2, 2, 2 + (innerWidth * unread) / kUnreadMax, r.bottom - 2};
            fillSolid(dc, fill, t.accent);
        }

        if (app->unreadBar && app->unreadBar->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(dc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            g.DrawImage(app->unreadBar.get(), Gdiplus::Rect(0, 0, r.right, r.bottom));
        } else {
            HPEN pen = CreatePen(PS_SOLID, 2, t.accentBright);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            POINT outline[] = {
                {0,0}, {r.right-1,0}, {r.right-1,r.bottom-10},
                {r.right-11,r.bottom-1}, {0,r.bottom-1}, {0,0}
            };
            Polyline(dc, outline, static_cast<int>(std::size(outline)));
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
