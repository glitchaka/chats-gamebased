constexpr int kMinBarWidth = 150;
constexpr int kMinBarHeight = 50;
constexpr COLORREF kMinBarTransparent = RGB(1, 2, 3);

void showCompactTrayBar(App& app) {
    if (!app.unreadHwnd) return;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    SetWindowPos(app.unreadHwnd, HWND_TOPMOST,
                 work.right - kMinBarWidth - 18,
                 work.top + 18,
                 kMinBarWidth, kMinBarHeight,
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

        // The minimized control is exactly the supplied toolkit bar. Paint a
        // chroma-key background only so the pixels outside its contour vanish.
        fillSolid(dc, r, kMinBarTransparent);
        if (app->unreadBar && app->unreadBar->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(dc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
            g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            // Native 1:1 copy. Never stretch the cropped toolkit asset.
            g.DrawImage(app->unreadBar.get(), 0, 0);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
