    target.reset();
    if (!std::filesystem::exists(path)) return;
    auto image = std::make_unique<Gdiplus::Bitmap>(path.c_str(), FALSE);
    if (image->GetLastStatus() == Gdiplus::Ok) target = std::move(image);
}

void loadUiAssets(App& app) {
    const auto ui = executableDirectory() / L"assets" / L"ui";
    loadBitmap(app.unreadBar, ui / L"unread_bar.png");
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

int glyphAdvance(int height) {
    const int scale = std::max(1, (height + pixelfont::kGlyphHeight - 1) / pixelfont::kGlyphHeight);
    return (pixelfont::kGlyphWidth + 1) * scale;
}

int pixelTextWidth(const std::wstring& text, int height) {
    const int adv = glyphAdvance(height);
    int width = 0;
    for (wchar_t ch : text) width += (ch == L' ') ? std::max(3, adv/2) : adv;
    return width;
}

std::wstring fitPixelText(std::wstring text, int maxWidth, int height) {
    if (pixelTextWidth(text, height) <= maxWidth) return text;
    while (!text.empty() && pixelTextWidth(text + L"...", height) > maxWidth) text.pop_back();
    return text + L"...";
}

void drawPixelText(App&, HDC dc, const std::wstring& source, int x, int y, int height, COLORREF color) {
    const int scale = std::max(1, (height + pixelfont::kGlyphHeight - 1) / pixelfont::kGlyphHeight);
    const int adv = (pixelfont::kGlyphWidth + 1) * scale;
    int cursor = x;
    HBRUSH brush = CreateSolidBrush(color);
    for (wchar_t ch : source) {
        if (ch == L' ') { cursor += std::max(3, adv/2); continue; }
        const auto& glyph = pixelfont::glyph(ch);
        for (int row=0; row<pixelfont::kGlyphHeight; ++row) {
            const std::uint8_t bits = glyph[static_cast<std::size_t>(row)];
            for (int col=0; col<pixelfont::kGlyphWidth; ++col) {
                if ((bits & (1u << (pixelfont::kGlyphWidth-1-col))) == 0) continue;
                RECT px{cursor + col*scale, y + row*scale, cursor + (col+1)*scale, y + (row+1)*scale};
                FillRect(dc,&px,brush);
            }
        }
        cursor += adv;
    }
    DeleteObject(brush);
}

void drawPixelCentered(App& app, HDC dc, const std::wstring& text, const RECT& r, int height, COLORREF color) {
    const int w = pixelTextWidth(text,height);
    const int actualHeight = pixelfont::kGlyphHeight * std::max(1,(height+pixelfont::kGlyphHeight-1)/pixelfont::kGlyphHeight);
    const int x = r.left + std::max(0,((r.right-r.left)-w)/2);
    const int y = r.top + std::max(0,((r.bottom-r.top)-actualHeight)/2);
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
