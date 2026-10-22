std::string defaultPlayerName() {
    wchar_t user[64]{};
    const DWORD count = GetEnvironmentVariableW(L"USERNAME", user, static_cast<DWORD>(std::size(user)));
    if (count > 0 && count < std::size(user)) {
        std::string utf8 = wideToUtf8(std::wstring(user, user + count));
        if (utf8.size() > 24) utf8.resize(24);
        if (!utf8.empty()) return utf8;
    }
    return defaultPlayerNameLegacy();
}

void paintSenderIdentities(App& app, HDC dc) {
    const Theme& t = theme(app);
    const RECT messages{24,140,610,506};

    const std::wstring local = utf8ToWide(app.net.localName());
    RECT identity{38,112,360,124};
    fillSolid(dc, identity, t.panel);
    drawPixelText(app, dc, fitPixelText(L"USER " + (local.empty() ? L"PLAYER" : local), 310, 9),
                  identity.left, identity.top, 9, t.muted);

    HRGN clip = CreateRectRgn(messages.left + 12, messages.top + 16,
                              messages.right - 12, messages.bottom - 12);
    SelectClipRgn(dc, clip);

    int y = messages.top + 22;
    const int start = std::max(0, static_cast<int>(app.lines.size()) - 10);
    for (int i = start; i < static_cast<int>(app.lines.size()); ++i) {
        const auto& line = app.lines[static_cast<std::size_t>(i)];

        std::wstring sender;
        COLORREF senderColor{};
        if (line.local) {
            if (!line.channel.empty()) {
                sender = L"TO " + line.channel;
            } else {
                sender = L"YOU " + (local.empty() ? L"PLAYER" : local);
            }
            senderColor = t.accentBright;
        } else {
            const std::wstring remote = line.name.empty() ? L"UNKNOWN" : line.name;
            sender = L"FROM " + remote;
            senderColor = RGB(71,197,157);
        }

        RECT nameArea{messages.left + 14, y, messages.left + 151, y + 18};
        fillSolid(dc, nameArea, t.panel);
        drawPixelText(app, dc, fitPixelText(sender + L":", 128, 10),
                      messages.left + 18, y + 2, 10, senderColor);

        if (line.kind == ChatLine::Kind::Text) y += 27;
        else if (line.kind == ChatLine::Kind::Sticker) y += 80;
        else y += 40;
    }

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

void paint(App& app, HDC target) {
    paintLegacy(app, target);
    paintSenderIdentities(app, target);
}
