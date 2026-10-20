
LRESULT CALLBACK editSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetPropW(hwnd,L"GameChatApp"));
    if (app && msg==WM_KEYDOWN && wParam==VK_RETURN) {
        PostMessageW(app->hwnd,kSendMessageCommand,0,0);
        return 0;
    }
    return app && app->oldEditProc ? CallWindowProcW(app->oldEditProc,hwnd,msg,wParam,lParam) : DefWindowProcW(hwnd,msg,wParam,lParam);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
        app->hwnd = hwnd;
    }
    if (!app) return DefWindowProcW(hwnd,msg,wParam,lParam);

    switch (msg) {
    case WM_CREATE:
        app->editFont = makeEditFont(18);
        app->smallEditFont = makeEditFont(14);
        app->editBrush = CreateSolidBrush(theme(*app).panel2);
        app->edit = createEdit(hwnd,app->editFont,900);
        SetPropW(app->edit,L"GameChatApp",app);
        app->oldEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(app->edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(editSubclass)));
        app->friendEdit = createEdit(hwnd,app->smallEditFont,120);
        app->groupEdit = createEdit(hwnd,app->smallEditFont,31);
        layout(*app);
        SetTimer(hwnd,kNetworkTimer,60,nullptr);
        addTrayIcon(*app);
        return 0;
    case WM_TIMER:
        if (wParam==kNetworkTimer) updateNetwork(*app);
        return 0;
    case kSendMessageCommand:
        sendCurrentMessage(*app);
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam),theme(*app).text);
        SetBkColor(reinterpret_cast<HDC>(wParam),theme(*app).panel2);
        return reinterpret_cast<INT_PTR>(app->editBrush);
    case WM_CLOSE:
        ShowWindow(hwnd,SW_HIDE);
        updateUnreadHud(*app);
        return 0;
    case kTrayMessage:
        if (LOWORD(lParam)==WM_LBUTTONUP || LOWORD(lParam)==WM_LBUTTONDBLCLK) {
            restoreMain(*app);
            return 0;
        }
        if (LOWORD(lParam)==WM_RBUTTONUP || LOWORD(lParam)==WM_CONTEXTMENU) {
            POINT p{}; GetCursorPos(&p);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu,MF_STRING,kTrayOpen,L"Open chat");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING,kTrayExit,L"Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu,TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);
            DestroyMenu(menu);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam)==kTrayOpen) { restoreMain(*app); return 0; }
        if (LOWORD(wParam)==kTrayExit) { app->exiting=true; DestroyWindow(hwnd); return 0; }
        break;
    case WM_LBUTTONUP: {
        POINT pt{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        if (PtInRect(&app->closeRect,pt)) { ShowWindow(hwnd,SW_HIDE); updateUnreadHud(*app); return 0; }
        if (PtInRect(&app->copyRect,pt)) {
            const std::string value = app->net.publicEndpoint().empty()?app->net.localEndpoint():app->net.publicEndpoint();
            copyToClipboard(hwnd,utf8ToWide(value)); app->status=L"ADDRESS COPIED"; app->sounds.play(sfx::Cue::Click); InvalidateRect(hwnd,nullptr,FALSE); return 0;
        }
        if (PtInRect(&app->themeRect,pt)) {
            app->themeIndex=(app->themeIndex+1)%static_cast<int>(std::size(kThemes));
            if (app->editBrush) DeleteObject(app->editBrush);
            app->editBrush=CreateSolidBrush(theme(*app).panel2);
            InvalidateRect(app->edit,nullptr,TRUE); InvalidateRect(app->friendEdit,nullptr,TRUE); InvalidateRect(app->groupEdit,nullptr,TRUE);
            InvalidateRect(hwnd,nullptr,TRUE); InvalidateRect(app->unreadHwnd,nullptr,TRUE); app->sounds.play(sfx::Cue::Click); return 0;
        }
        if (PtInRect(&app->addFriendRect,pt)) { addFriendFromEdit(*app); return 0; }
        if (PtInRect(&app->addGroupRect,pt)) { addGroupFromEdit(*app); return 0; }
        if (PtInRect(&app->allRect,pt)) { app->targetMode=TargetMode::All; app->selectedFriend=app->selectedGroup=-1; InvalidateRect(hwnd,nullptr,FALSE); return 0; }
        if (PtInRect(&app->sendRect,pt)) { sendCurrentMessage(*app); SetFocus(app->edit); return 0; }
        if (PtInRect(&app->stickerButtonRect,pt)) { app->stickerMenuOpen=!app->stickerMenuOpen; InvalidateRect(hwnd,nullptr,FALSE); return 0; }
        if (app->stickerMenuOpen && PtInRect(&app->stickerImportRect,pt)) { importSticker(*app); return 0; }
        if (app->stickerMenuOpen) {
            for (std::size_t i=0;i<app->stickerMenuRects.size();++i) if (PtInRect(&app->stickerMenuRects[i],pt)) { sendUserSticker(*app,i); return 0; }
        }
        for (int i=0;i<7 && i<static_cast<int>(app->friends.size());++i) if (PtInRect(&app->friendRows[i],pt)) {
            app->targetMode=TargetMode::Friend; app->selectedFriend=i; app->selectedGroup=-1; InvalidateRect(hwnd,nullptr,FALSE); return 0;
        }
        for (int i=0;i<4 && i<static_cast<int>(app->groups.size());++i) if (PtInRect(&app->groupRows[i],pt)) {
            app->targetMode=TargetMode::Group; app->selectedGroup=i; app->selectedFriend=-1; InvalidateRect(hwnd,nullptr,FALSE); return 0;
        }
        break;
    }
    case WM_NCHITTEST: {
        LRESULT base = DefWindowProcW(hwnd,msg,wParam,lParam);
        if (base!=HTCLIENT) return base;
        POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; ScreenToClient(hwnd,&p);
        if (p.y<76 && p.x<840) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); paint(*app,dc); EndPaint(hwnd,&ps); return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(hwnd,kNetworkTimer);
        removeTrayIcon(*app);
        app->net.stop();
        if (app->edit) RemovePropW(app->edit,L"GameChatApp");
        if (app->editBrush) DeleteObject(app->editBrush);
        if (app->editFont) DeleteObject(app->editFont);
        if (app->smallEditFont) DeleteObject(app->smallEditFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    ULONG_PTR gdiplusToken{};
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken,&gdiplusInput,nullptr)!=Gdiplus::Ok) return 1;

    App app;
    app.instance=instance;
    loadUiAssets(app);
    loadUserStickers(app);
    if (!app.net.start(defaultPlayerName(),34197)) {
        MessageBoxW(nullptr,utf8ToWide(app.net.lastError()).c_str(),L"Game Chat - network error",MB_ICONERROR|MB_OK);
    }

    HICON icon = LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP_ICON));
    WNDCLASSEXW wc{};
    wc.cbSize=sizeof(wc); wc.lpfnWndProc=wndProc; wc.hInstance=instance; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hIcon=icon; wc.hIconSm=icon; wc.lpszClassName=kWindowClass;
    RegisterClassExW(&wc);

    WNDCLASSEXW uwc{};
    uwc.cbSize=sizeof(uwc); uwc.lpfnWndProc=unreadProc; uwc.hInstance=instance; uwc.hCursor=LoadCursorW(nullptr,IDC_HAND);
    uwc.hIcon=icon; uwc.hIconSm=icon; uwc.lpszClassName=kUnreadClass;
    RegisterClassExW(&uwc);

    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int x = work.right-kWindowWidth-24;
    const int y = work.top+24;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST|WS_EX_APPWINDOW,kWindowClass,L"Game Chat",WS_POPUP,
                                x,y,kWindowWidth,kWindowHeight,nullptr,nullptr,instance,&app);
    if (!hwnd) { Gdiplus::GdiplusShutdown(gdiplusToken); return 1; }

    app.unreadHwnd = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,kUnreadClass,L"",
                                     WS_POPUP,0,0,kUnreadWidth,kUnreadHeight,hwnd,nullptr,instance,&app);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
    ShowWindow(hwnd,SW_SHOW); UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    if (app.unreadHwnd && IsWindow(app.unreadHwnd)) DestroyWindow(app.unreadHwnd);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
