constexpr int kMinBarWidth = 490;
constexpr int kMinBarHeight = 55;

void showCompactTrayBar(App& app) {
    if (!app.unreadHwnd) return;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    SetWindowPos(app.unreadHwnd, HWND_TOPMOST,
                 work.right - kMinBarWidth - 18,
                 work.top + 18,
                 kMinBarWidth, kMinBarHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    POINT shape[5] = {
        {0, 0},
        {kMinBarWidth - 1, 0},
        {kMinBarWidth - 1, kMinBarHeight - 10},
        {kMinBarWidth - 11, kMinBarHeight - 1},
        {0, kMinBarHeight - 1}
    };
    HRGN region = CreatePolygonRgn(shape, 5, WINDING);
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
        fillSolid(dc, r, t.panel2);

        if (app->unreadBar && app->unreadBar->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(dc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
            const INT w = static_cast<INT>(app->unreadBar->GetWidth());
            const INT h = static_cast<INT>(app->unreadBar->GetHeight());
            g.DrawImage(app->unreadBar.get(), 0, 0, w, h);
        } else {
            HPEN pen = CreatePen(PS_SOLID, 1, t.accentBright);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            POINT outline[6] = {
                {0,0}, {r.right-1,0}, {r.right-1,r.bottom-10},
                {r.right-11,r.bottom-1}, {0,r.bottom-1}, {0,0}
            };
            Polyline(dc, outline, 6);
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
