constexpr int kMinBarWidth = 150;
constexpr int kMinBarHeight = 50;
constexpr COLORREF kMinBarTransparent = RGB(1, 2, 3);
constexpr int kMinBarFillLeft = 44;
constexpr int kMinBarFillTop = 22;
constexpr int kMinBarFillRight = 142;
constexpr int kMinBarFillBottom = 32;

COLORREF blendBarColor(COLORREF a, COLORREF b, int amount, int total) {
    if (total <= 0) return b;
    amount = std::clamp(amount, 0, total);
    const int inv = total - amount;
    return RGB((GetRValue(a) * inv + GetRValue(b) * amount) / total,
               (GetGValue(a) * inv + GetGValue(b) * amount) / total,
               (GetBValue(a) * inv + GetBValue(b) * amount) / total);
}

std::unique_ptr<Gdiplus::Bitmap> themedUnreadBar(const App& app) {
    if (!app.unreadBar || app.unreadBar->GetLastStatus() != Gdiplus::Ok) return {};
    const UINT width = app.unreadBar->GetWidth();
    const UINT height = app.unreadBar->GetHeight();
    if (!width || !height) return {};

    auto output = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppARGB);
    if (output->GetLastStatus() != Gdiplus::Ok) return {};
    const Theme& t = theme(app);

    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            Gdiplus::Color src{};
            if (app.unreadBar->GetPixel(x, y, &src) != Gdiplus::Ok || src.GetAlpha() == 0) {
                output->SetPixel(x, y, Gdiplus::Color(0, 0, 0, 0));
                continue;
            }

            const int intensity = std::max({static_cast<int>(src.GetRed()),
                                            static_cast<int>(src.GetGreen()),
                                            static_cast<int>(src.GetBlue())});
            COLORREF mapped{};
            if (intensity < 70) {
                mapped = blendBarColor(t.bg, t.panel2, intensity, 70);
            } else if (intensity < 185) {
                mapped = blendBarColor(t.panel2, t.accent, intensity - 70, 115);
            } else {
                mapped = blendBarColor(t.accent, t.accentBright, intensity - 185, 70);
            }
            output->SetPixel(x, y, Gdiplus::Color(src.GetAlpha(),
                                                   GetRValue(mapped),
                                                   GetGValue(mapped),
                                                   GetBValue(mapped)));
        }
    }
    return output;
}

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

        auto bar = themedUnreadBar(*app);
        if (bar && bar->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(dc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
            g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

            // Exact 1:1 toolkit sprite, recolored from the current chat theme.
            g.DrawImage(bar.get(), 0, 0);

            const Theme& t = theme(*app);
            const Gdiplus::Rect fillArea(kMinBarFillLeft, kMinBarFillTop,
                                        kMinBarFillRight - kMinBarFillLeft,
                                        kMinBarFillBottom - kMinBarFillTop);
            Gdiplus::SolidBrush emptyBrush(Gdiplus::Color(255,
                                                           GetRValue(t.panel2),
                                                           GetGValue(t.panel2),
                                                           GetBValue(t.panel2)));
            g.FillRectangle(&emptyBrush, fillArea);

            const int unread = std::clamp(app->unreadCount, 0, kUnreadMax);
            const int fillWidth = ((kMinBarFillRight - kMinBarFillLeft) * unread) / kUnreadMax;
            if (fillWidth > 0) {
                const Gdiplus::GraphicsState state = g.Save();
                Gdiplus::Rect clipRect(kMinBarFillLeft, kMinBarFillTop,
                                       fillWidth,
                                       kMinBarFillBottom - kMinBarFillTop);
                g.SetClip(clipRect, Gdiplus::CombineModeReplace);
                g.DrawImage(bar.get(), 0, 0);
                g.Restore(state);
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
