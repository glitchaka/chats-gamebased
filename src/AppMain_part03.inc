    app.stickerButtonRect = {640,522,742,570};
    app.audioButtonRect = {750,522,818,570};
    app.sendRect = {826,522,892,570};
    if (app.edit) SetWindowPos(app.edit,nullptr,40,531,584,29,SWP_NOZORDER|SWP_NOACTIVATE);
    if (app.friendEdit) SetWindowPos(app.friendEdit,nullptr,638,194,160,24,SWP_NOZORDER|SWP_NOACTIVATE);
    if (app.groupEdit) SetWindowPos(app.groupEdit,nullptr,638,390,160,24,SWP_NOZORDER|SWP_NOACTIVATE);
    for (int i=0;i<7;++i) app.friendRows[i] = {638,226+i*24,884,248+i*24};
    for (int i=0;i<4;++i) app.groupRows[i] = {638,422+i*20,884,440+i*20};
}

void paintStickerPopup(App& app, HDC dc) {
    app.stickerMenuRects.clear();
    if (!app.stickerMenuOpen) return;
    const Theme& t = theme(app);
    RECT popup{300,290,598,494};
    drawChamferedBox(dc,popup,t.panel2,t.accentBright,8);
    drawPixelText(app,dc,L"MY STICKERS",316,302,15,t.text);
    app.stickerImportRect = {478,298,582,330};
    drawChamferedBox(dc,app.stickerImportRect,t.accent,t.accentBright,6);
    drawPixelCentered(app,dc,L"ADD",app.stickerImportRect,13,t.bg);

    const std::size_t visible = std::min<std::size_t>(app.stickers.size(),8);
    for (std::size_t i=0;i<visible;++i) {
        const int col = static_cast<int>(i%4), row = static_cast<int>(i/4);
        RECT r{316+col*64,342+row*62,368+col*64,394+row*62};
        drawChamferedBox(dc,r,t.panel,t.accent,6);
        drawImageFit(dc,app.stickers[i].image.get(),RECT{r.left+4,r.top+4,r.right-4,r.bottom-4});
        app.stickerMenuRects.push_back(r);
    }
    if (app.stickers.empty()) drawPixelText(app,dc,L"CHOOSE ADD TO LOAD",326,382,12,t.muted);
}

void paint(App& app, HDC target) {
    RECT client{};
    GetClientRect(app.hwnd,&client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target,client.right,client.bottom);
    HGDIOBJ oldBitmap = SelectObject(dc,bitmap);
    const Theme& t = theme(app);
    fillSolid(dc,client,t.bg);

    HPEN frame = CreatePen(PS_SOLID,3,t.frame);
    HGDIOBJ oldPen = SelectObject(dc,frame);
    HGDIOBJ oldBrush = SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,6,6,client.right-6,client.bottom-6,22,22);
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(frame);

    drawPixelText(app,dc,L"GAME CHAT",24,22,27,t.accent);
    drawPixelText(app,dc,L"DIRECT INTERNET  //  FRIENDS  //  GROUPS",24,60,12,t.frame);

    drawChamferedBox(dc,app.closeRect,RGB(48,18,18),RGB(232,94,94),6);
    drawPixelCentered(app,dc,L"X",app.closeRect,15,RGB(255,235,235));

    RECT share{24,84,900,126};
    drawChamferedBox(dc,share,t.panel,t.accent,8);
    drawPixelText(app,dc,L"MY ADDRESS",38,96,13,t.text);
    const std::string endpoint = app.net.publicEndpoint().empty()?app.net.localEndpoint():app.net.publicEndpoint();
    drawPixelText(app,dc,fitPixelText(utf8ToWide(endpoint),470,14),156,94,14,t.accentBright);

    drawChamferedBox(dc,app.themeRect,t.panel2,t.accentBright,6);
    drawPixelCentered(app,dc,kThemes[app.themeIndex].name,app.themeRect,12,t.text);
    drawChamferedBox(dc,app.copyRect,t.panel2,t.accentBright,6);
    drawPixelCentered(app,dc,L"COPY",app.copyRect,13,t.text);

    RECT messages{24,140,610,506}, side{624,140,900,506};
    drawChamferedBox(dc,messages,t.panel,t.accent,10);
    drawChamferedBox(dc,side,t.panel,t.accent,10);
    RECT mt{40,130,188,158}, st{640,130,782,158};
    drawChamferedBox(dc,mt,t.accent,t.accentBright,7);
    drawChamferedBox(dc,st,t.accent,t.accentBright,7);
    drawPixelCentered(app,dc,L"MESSAGES",mt,13,t.bg);
    drawPixelCentered(app,dc,L"CONTACTS",st,13,t.bg);

    drawChamferedBox(dc,app.allRect,app.targetMode==TargetMode::All?t.accent:t.panel2,t.accentBright,6);
    drawPixelCentered(app,dc,L"ALL FRIENDS",app.allRect,12,app.targetMode==TargetMode::All?t.bg:t.text);
    drawPixelText(app,dc,L"ADD IP/HOST:PORT",638,177,11,t.frame);
    drawChamferedBox(dc,app.addFriendRect,t.panel2,t.accentBright,6);
    drawPixelCentered(app,dc,L"ADD",app.addFriendRect,12,t.text);

    for (int i=0;i<7 && i<static_cast<int>(app.friends.size());++i) {
        const bool selected = app.targetMode==TargetMode::Friend && app.selectedFriend==i;
        drawChamferedBox(dc,app.friendRows[i],selected?t.accent:t.panel2,selected?t.accentBright:t.muted,5);
        const auto label = fitPixelText(utf8ToWide(app.friends[static_cast<std::size_t>(i)].label),220,11);
        drawPixelText(app,dc,label,app.friendRows[i].left+8,app.friendRows[i].top+5,11,selected?t.bg:t.accentBright);
    }

    drawPixelText(app,dc,L"NEW GROUP (ALL FRIENDS)",638,370,10,t.frame);
    drawChamferedBox(dc,app.addGroupRect,t.panel2,t.accentBright,6);
    drawPixelCentered(app,dc,L"NEW",app.addGroupRect,12,t.text);
    for (int i=0;i<4 && i<static_cast<int>(app.groups.size());++i) {
        const bool selected = app.targetMode==TargetMode::Group && app.selectedGroup==i;
        drawChamferedBox(dc,app.groupRows[i],selected?t.accent:t.panel2,selected?t.accentBright:t.muted,5);
        const auto label = fitPixelText(L"# "+utf8ToWide(app.groups[static_cast<std::size_t>(i)].name),215,10);
        drawPixelText(app,dc,label,app.groupRows[i].left+8,app.groupRows[i].top+4,10,selected?t.bg:t.text);
    }

    app.audioHitRects.clear();
    HRGN clip = CreateRectRgn(messages.left+12,messages.top+16,messages.right-12,messages.bottom-12);
    SelectClipRgn(dc,clip);
    int y = messages.top+22;
    const int start = std::max(0,static_cast<int>(app.lines.size())-10);
    for (int i=start;i<static_cast<int>(app.lines.size());++i) {
        const auto& line = app.lines[static_cast<std::size_t>(i)];
        const COLORREF nameColor = line.local?t.accentBright:RGB(71,197,157);
        drawPixelText(app,dc,fitPixelText(line.name+L":",130,11),messages.left+18,y+2,11,nameColor);
        if (line.kind==ChatLine::Kind::Text) {
            drawPixelText(app,dc,fitPixelText(line.text,390,11),messages.left+155,y+2,11,t.text);
            y += 27;
        } else if (line.kind==ChatLine::Kind::Sticker) {
            auto img = std::make_unique<Gdiplus::Bitmap>(line.stickerPath.c_str(),FALSE);
            RECT sr{messages.left+155,y-3,messages.left+235,y+71};
            drawChamferedBox(dc,sr,t.panel2,t.accent,6);
            if (img->GetLastStatus()==Gdiplus::Ok) drawImageFit(dc,img.get(),RECT{sr.left+4,sr.top+4,sr.right-4,sr.bottom-4});
            y += 80;
        } else {
            RECT ar{messages.left+155,y-2,messages.left+470,y+30};
            drawChamferedBox(dc,ar,t.panel2,t.accentBright,6);
            drawPixelText(app,dc,L"> AUDIO",ar.left+10,ar.top+8,11,t.accentBright);
            drawPixelText(app,dc,fitPixelText(line.stickerPath.filename().wstring(),220,10),ar.left+82,ar.top+9,10,t.text);
            app.audioHitRects.emplace_back(ar,line.stickerPath);
            y += 40;
        }
    }
    SelectClipRgn(dc,nullptr); DeleteObject(clip);

    RECT input{24,518,628,578};
    drawChamferedBox(dc,input,t.panel2,t.accent,9);
    RECT tab{40,508,234,536};
    drawChamferedBox(dc,tab,t.accent,t.accentBright,6);
    drawPixelCentered(app,dc,fitPixelText(targetLabel(app),180,11),tab,11,t.bg);
    drawChamferedBox(dc,app.stickerButtonRect,t.panel2,t.accentBright,8);
    drawPixelCentered(app,dc,L"STICKERS",app.stickerButtonRect,10,t.text);
    drawChamferedBox(dc,app.audioButtonRect,t.panel2,t.accentBright,8);
    drawPixelCentered(app,dc,L"AUDIO",app.audioButtonRect,10,t.text);
    drawChamferedBox(dc,app.sendRect,t.panel2,t.accentBright,8);
    drawPixelCentered(app,dc,L"SEND",app.sendRect,10,t.text);
    drawPixelText(app,dc,fitPixelText(app.status,820,10),24,591,10,t.muted);

    paintStickerPopup(app,dc);

    BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc,oldBitmap); DeleteObject(bitmap); DeleteDC(dc);
}

void addTrayIcon(App& app) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = app.hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE|NIF_ICON|NIF_TIP;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = static_cast<HICON>(LoadImageW(app.instance,MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,16,16,LR_DEFAULTCOLOR));
    if (!nid.hIcon) nid.hIcon = LoadIconW(app.instance,MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr,IDI_APPLICATION);
    wcscpy_s(nid.szTip,L"Game Chat");
    app.trayAdded = Shell_NotifyIconW(NIM_ADD,&nid)!=FALSE;
    if (app.trayAdded) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION,&nid);
    }
}

void removeTrayIcon(App& app) {
    if (!app.trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid); nid.hWnd = app.hwnd; nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE,&nid);
    app.trayAdded = false;
}

LRESULT CALLBACK unreadProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(hwnd,msg,wParam,lParam);

    switch (msg) {
    case WM_LBUTTONUP:
        restoreMain(*app);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd,&ps);
        RECT r{}; GetClientRect(hwnd,&r);
        const Theme& t = theme(*app);
        fillSolid(dc,r,RGB(0,0,0));
        if (app->unreadBar) {
            drawImageFit(dc,app->unreadBar.get(),RECT{8,8,76,34});
            const int filled = std::min(app->unreadCount,kUnreadMax);
            const int barLeft = 27;
            const int barRight = 69;
            RECT fill{barLeft,14,barLeft + ((barRight-barLeft)*filled)/kUnreadMax,24};
            fillSolid(dc,fill,t.accentBright);
        } else {
            drawChamferedBox(dc,RECT{8,9,76,34},t.panel2,t.accent,5);
            RECT fill{18,16,18+(48*std::min(app->unreadCount,kUnreadMax))/kUnreadMax,27};
            fillSolid(dc,fill,t.accentBright);
        }
        drawPixelText(*app,dc,L"MESSAGES",84,10,11,t.text);
        drawPixelText(*app,dc,std::to_wstring(app->unreadCount),84,28,10,t.accentBright);
        EndPaint(hwnd,&ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}