void updateUnreadHudOnClose(App& app) {
    if (!app.unreadHwnd) return;

    // Closing the main window leaves the chat running in the tray, so the
    // compact top-right bar must become visible immediately even with 0 unread.
    if (IsWindowVisible(app.hwnd)) {
        ShowWindow(app.unreadHwnd, SW_HIDE);
        return;
    }

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    SetWindowPos(app.unreadHwnd, HWND_TOPMOST,
                 work.right - kUnreadWidth - 18,
                 work.top + 18,
                 kUnreadWidth,
                 kUnreadHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(app.unreadHwnd, nullptr, TRUE);
}
