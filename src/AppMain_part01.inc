    target.reset();
    if (!std::filesystem::exists(path)) return;
    auto image = std::make_unique<Gdiplus::Bitmap>(path.c_str(), FALSE);
    if (image->GetLastStatus() == Gdiplus::Ok) target = std::move(image);
}

void loadUiAssets(App& app) {
    const auto ui = executableDirectory() / L"assets" / L"ui";
    loadBitmap(app.unreadBar, ui / L"unread_bar.png");
    loadBitmap(app.pixelFont, ui / L"pixel_font.png");
}

void loadUserStickers(App& app) {
    app.stickers.clear();
    std::error_code ec;
    std::filesystem::create_directories(stickerDirectory(), ec);
    for (const auto& entry : std::filesystem::directory_iterator(stickerDirectory(), ec)) {
        if (ec || !entry.is_regular_file() || !isImageExtension(entry.path())) continue;
        auto image = std::make_unique<Gdiplus::Bitmap>(entry.path().c_str(), FALSE);
        if (image->GetLastStatus() != Gdiplus::Ok) continue;
        app.stickers.push_back(StickerItem{entry.path(), std::move(image)});
    }
    std::sort(app.stickers.begin(), app.stickers.end(), [](const StickerItem& a, const StickerItem& b) {
        std::error_code ea, eb;
        return std::filesystem::last_write_time(a.path, ea) > std::filesystem::last_write_time(b.path, eb);
    });
}

HFONT makeEditFont(int height) {
    HFONT font = CreateFontW(-height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Terminal");
    if (!font) {
        font = CreateFontW(-height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    }
    return font;
}

void fillSolid(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void drawChamferedBox(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int cut = 8) {
    POINT points[8] = {
        {rect.left+cut,rect.top},{rect.right-cut,rect.top},{rect.right,rect.top+cut},{rect.right,rect.bottom-cut},
        {rect.right-cut,rect.bottom},{rect.left+cut,rect.bottom},{rect.left,rect.bottom-cut},{rect.left,rect.top+cut}
    };
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 2, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, 8);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

constexpr int kFontCellWidth = 48;
constexpr int kFontCellHeight = 56;
constexpr int kFontVisibleTop = 4;
constexpr int kFontVisibleHeight = 48;

int sourceGlyphIndex(wchar_t ch) {
    if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
    if (ch >= L'A' && ch <= L'Z') return static_cast<int>(ch - L'A');
    if (ch >= L'0' && ch <= L'9') return 26 + static_cast<int>(ch - L'0');
    return -1;
}

int glyphAdvanceFor(wchar_t ch, int height) {
    if (ch == L' ') return std::max(4, height / 2);
    if (sourceGlyphIndex(ch) >= 0) return std::max(6, height);
    return std::max(4, height / 2 + 2);
}

int glyphAdvance(int height) {
    return std::max(6, height);
}

int pixelTextWidth(const std::wstring& text, int height) {
    int width = 0;
    for (wchar_t ch : text) width += glyphAdvanceFor(ch, height);
    return width;
}

std::wstring fitPixelText(std::wstring text, int maxWidth, int height) {
    if (pixelTextWidth(text, height) <= maxWidth) return text;
    while (!text.empty() && pixelTextWidth(text + L"...", height) > maxWidth) text.pop_back();
    return text + L"...";
}

void drawPixelMark(HDC dc, wchar_t ch, int x, int y, int height, COLORREF color) {
    const int width = glyphAdvanceFor(ch, height);
    const int unit = std::max(1, height / 8);
    const int cx = x + width / 2;
    HBRUSH brush = CreateSolidBrush(color);
    auto block = [&](int left, int top, int right, int bottom) {
        RECT r{left, top, right, bottom};
        FillRect(dc, &r, brush);
    };

    switch (ch) {
    case L'.': block(cx-unit, y+height-2*unit, cx+unit, y+height); break;
    case L',': block(cx-unit, y+height-2*unit, cx+unit, y+height-unit); block(cx, y+height-unit, cx+unit, y+height); break;
    case L':': block(cx-unit, y+2*unit, cx+unit, y+4*unit); block(cx-unit, y+height-3*unit, cx+unit, y+height-unit); break;
    case L';': block(cx-unit, y+2*unit, cx+unit, y+4*unit); block(cx-unit, y+height-3*unit, cx+unit, y+height-2*unit); block(cx, y+height-2*unit, cx+unit, y+height-unit); break;
    case L'-': block(x+unit, y+height/2-unit/2, x+width-unit, y+height/2+unit); break;
    case L'_': block(x, y+height-unit, x+width, y+height); break;
    case L'+': block(cx-unit/2, y+2*unit, cx+unit, y+height-2*unit); block(x+unit, y+height/2-unit/2, x+width-unit, y+height/2+unit); break;
    case L'=': block(x+unit, y+3*unit, x+width-unit, y+4*unit); block(x+unit, y+height-3*unit, x+width-unit, y+height-2*unit); break;
    case L'!': block(cx-unit/2, y+unit, cx+unit, y+height-3*unit); block(cx-unit/2, y+height-2*unit, cx+unit, y+height-unit); break;
    case L'/':
        for (int i=0; i<6; ++i) block(x+width-(i+2)*unit, y+(i+1)*unit, x+width-i*unit, y+(i+3)*unit);
        break;
    case L'\\':
        for (int i=0; i<6; ++i) block(x+i*unit, y+(i+1)*unit, x+(i+2)*unit, y+(i+3)*unit);
        break;
    case L'(':
        block(x+width-2*unit, y+unit, x+width-unit, y+height-unit);
        block(x+width-3*unit, y+2*unit, x+width-2*unit, y+height-2*unit);
        break;
    case L')':
        block(x+unit, y+unit, x+2*unit, y+height-unit);
        block(x+2*unit, y+2*unit, x+3*unit, y+height-2*unit);
        break;
    case L'[': block(x+unit,y+unit,x+2*unit,y+height-unit); block(x+unit,y+unit,x+width-unit,y+2*unit); block(x+unit,y+height-2*unit,x+width-unit,y+height-unit); break;
    case L']': block(x+width-2*unit,y+unit,x+width-unit,y+height-unit); block(x+unit,y+unit,x+width-unit,y+2*unit); block(x+unit,y+height-2*unit,x+width-unit,y+height-unit); break;
    case L'"': block(x+unit,y+unit,x+2*unit,y+3*unit); block(x+width-2*unit,y+unit,x+width-unit,y+3*unit); break;
    case L'\'': block(cx-unit/2,y+unit,cx+unit,y+3*unit); break;
    case L'*': block(cx-unit/2,y+2*unit,cx+unit,y+height-2*unit); block(x+unit,y+height/2-unit/2,x+width-unit,y+height/2+unit); break;
    case L'?': block(x+unit,y+unit,x+width-unit,y+2*unit); block(x+width-2*unit,y+2*unit,x+width-unit,y+4*unit); block(cx-unit/2,y+4*unit,cx+unit,y+height-3*unit); block(cx-unit/2,y+height-2*unit,cx+unit,y+height-unit); break;
    default: block(x+unit, y+height/2-unit/2, x+width-unit, y+height/2+unit); break;
    }
    DeleteObject(brush);
}

void drawPixelText(App& app, HDC dc, const std::wstring& source, int x, int y, int height, COLORREF color) {
    int cursor = x;
    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const float red = static_cast<float>(GetRValue(color)) / 255.0f;
    const float green = static_cast<float>(GetGValue(color)) / 255.0f;
    const float blue = static_cast<float>(GetBValue(color)) / 255.0f;
    Gdiplus::ColorMatrix matrix = {{
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
        {red, green, blue, 0.0f, 1.0f}
    }};
    Gdiplus::ImageAttributes tint;
    tint.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

    for (wchar_t ch : source) {
        if (ch == L' ') {
            cursor += glyphAdvanceFor(ch, height);
            continue;
        }

        const int index = sourceGlyphIndex(ch);
        if (index >= 0 && app.pixelFont && app.pixelFont->GetLastStatus() == Gdiplus::Ok) {
            const int col = index % 10;
            const int row = index / 10;
            const Gdiplus::Rect dest(cursor, y, height, height);
            graphics.DrawImage(app.pixelFont.get(), dest,
                               col * kFontCellWidth,
                               row * kFontCellHeight + kFontVisibleTop,
                               kFontCellWidth,
                               kFontVisibleHeight,
                               Gdiplus::UnitPixel,
                               &tint);
        } else {
            drawPixelMark(dc, ch, cursor, y, height, color);
        }
        cursor += glyphAdvanceFor(ch, height);
    }
}

void drawPixelCentered(App& app, HDC dc, const std::wstring& text, const RECT& r, int height, COLORREF color) {
    const int w = pixelTextWidth(text,height);
    const int actualHeight = height;
    const int x = static_cast<int>(r.left) + std::max(0,(static_cast<int>(r.right-r.left)-w)/2);
    const int y = static_cast<int>(r.top) + std::max(0,(static_cast<int>(r.bottom-r.top)-actualHeight)/2);
    drawPixelText(app,dc,text,x,y,height,color);
}

void drawImageFit(HDC dc, Gdiplus::Bitmap* image, const RECT& rect) {
    if (!image || image->GetLastStatus() != Gdiplus::Ok) return;
    const UINT iw = image->GetWidth(), ih = image->GetHeight();
    if (!iw || !ih) return;
    const int bw = rect.right-rect.left, bh = rect.bottom-rect.top;
    const double scale = std::min(static_cast<double>(bw)/iw, static_cast<double>(bh)/ih);
    const int dw = std::max(1, static_cast<int>(iw*scale));
    const int dh = std::max(1, static_cast<int>(ih*scale));
    const int x = rect.left+(bw-dw)/2, y = rect.top+(bh-dh)/2;
    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.DrawImage(image, Gdiplus::Rect(x,y,dw,dh));
}

void copyToClipboard(HWND hwnd, const std::wstring& value) {
    if (value.empty() || !OpenClipboard(hwnd)) return;
    EmptyClipboard();
    const std::size_t bytes = (value.size()+1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* ptr = GlobalLock(memory);
        if (ptr) {
            std::memcpy(ptr, value.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

void trimHistory(App& app) {
    constexpr std::size_t kMaxHistory = 160;
    if (app.lines.size() > kMaxHistory) {
        app.lines.erase(app.lines.begin(), app.lines.begin() + static_cast<std::ptrdiff_t>(app.lines.size()-kMaxHistory));
    }
}

int findFriend(const App& app, const std::string& endpoint) {
    for (std::size_t i=0; i<app.friends.size(); ++i) if (app.friends[i].endpoint == endpoint) return static_cast<int>(i);
    return -1;
}

void ensureFriend(App& app, const std::string& endpoint, const std::string& label) {
    if (endpoint.empty()) return;
    const int found = findFriend(app, endpoint);
    if (found >= 0) {
        if (!label.empty()) app.friends[static_cast<std::size_t>(found)].label = label;
        return;
    }
    app.friends.push_back(FriendItem{endpoint, label.empty()?endpoint:label});
}

int findGroup(const App& app, const std::string& name) {
    for (std::size_t i=0; i<app.groups.size(); ++i) if (app.groups[i].name == name) return static_cast<int>(i);
    return -1;
}

void ensureIncomingGroup(App& app, const std::string& channel, const std::string& senderEndpoint) {
    if (channel.empty()) return;
    if (findGroup(app, channel) >= 0) return;
    GroupItem group;
    group.name = channel;
    for (const auto& f : app.friends) group.members.push_back(f.endpoint);
    if (std::find(group.members.begin(), group.members.end(), senderEndpoint) == group.members.end()) group.members.push_back(senderEndpoint);
    app.groups.push_back(std::move(group));
}

std::vector<std::string> allFriendEndpoints(const App& app) {
    std::vector<std::string> result;
    for (const auto& f : app.friends) result.push_back(f.endpoint);
    return result;
}

std::wstring targetLabel(const App& app) {
    if (app.targetMode == TargetMode::Friend && app.selectedFriend >= 0 && app.selectedFriend < static_cast<int>(app.friends.size()))
        return L"FRIEND: " + utf8ToWide(app.friends[static_cast<std::size_t>(app.selectedFriend)].label);
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 && app.selectedGroup < static_cast<int>(app.groups.size()))
        return L"GROUP: " + utf8ToWide(app.groups[static_cast<std::size_t>(app.selectedGroup)].name);
    return L"ALL FRIENDS";
}

bool sendTextToTarget(App& app, const std::string& text) {
    if (app.targetMode == TargetMode::Friend && app.selectedFriend >= 0 && app.selectedFriend < static_cast<int>(app.friends.size()))
        return app.net.sendChatTo(app.friends[static_cast<std::size_t>(app.selectedFriend)].endpoint, text);
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 && app.selectedGroup < static_cast<int>(app.groups.size())) {
        const auto& g = app.groups[static_cast<std::size_t>(app.selectedGroup)];
        return app.net.sendGroup(g.name, g.members, text);
    }
    return app.net.sendChat(text);
}
