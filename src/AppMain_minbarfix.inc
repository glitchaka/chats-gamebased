constexpr int kMinBarWidth = 150;
constexpr int kMinBarHeight = 50;
constexpr COLORREF kMinBarTransparent = RGB(1, 2, 3);
constexpr COLORREF kMinBarEmpty = RGB(0, 6, 25);
constexpr int kMinBarFillLeft = 44;
constexpr int kMinBarFillTop = 22;
constexpr int kMinBarFillRight = 142;
constexpr int kMinBarFillBottom = 32;

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

        fillSolid(dc, r, kMinBarTransparent);
        if (app->unreadBar && app->unreadBar->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(dc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
            g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

            // 1:1 source sprite. Never stretch the toolkit art.
            g.DrawImage(app->unreadBar.get(), 0, 0);

            // The light cavity is the unread meter. Start it empty, then reveal
            // the original light pixels from left to right as unread messages arrive.
            const Gdiplus::Rect fillArea(
                kMinBarFillLeft,
                kMinBarFillTop,
                kMinBarFillRight - kMinBarFillLeft,
                kMinBarFillBottom - kMinBarFillTop);
            Gdiplus::SolidBrush emptyBrush(Gdiplus::Color(255, 0, 6, 25));
            g.FillRectangle(&emptyBrush, fillArea);

            const int unread = std::clamp(app->unreadCount, 0, kUnreadMax);
            const int fillWidth = ((kMinBarFillRight - kMinBarFillLeft) * unread) / kUnreadMax;
            if (fillWidth > 0) {
                const Gdiplus::Region oldClip = [&g]() {
                    Gdiplus::Region region;
                    g.GetClip(&region);
                    return region;
                }();
                Gdiplus::Rect clipRect(kMinBarFillLeft, kMinBarFillTop,
                                       fillWidth,
                                       kMinBarFillBottom - kMinBarFillTop);
                g.SetClip(clipRect, Gdiplus::CombineModeReplace);
                g.DrawImage(app->unreadBar.get(), 0, 0);
                g.SetClip(&oldClip, Gdiplus::CombineModeReplace);
            }
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
