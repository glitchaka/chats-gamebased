std::wstring outgoingRecipientLabel(const App& app) {
    if (app.targetMode == TargetMode::Friend && app.selectedFriend >= 0 &&
        app.selectedFriend < static_cast<int>(app.friends.size())) {
        const auto& friendItem = app.friends[static_cast<std::size_t>(app.selectedFriend)];
        const std::string label = friendItem.label.empty() ? friendItem.endpoint : friendItem.label;
        return utf8ToWide(label);
    }
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 &&
        app.selectedGroup < static_cast<int>(app.groups.size())) {
        return L"#" + utf8ToWide(app.groups[static_cast<std::size_t>(app.selectedGroup)].name);
    }
    return L"ALL FRIENDS";
}

void sendCurrentMessage(App& app) {
    std::wstring text = readEdit(app.edit);
    if (text.empty()) return;

    const std::wstring recipient = outgoingRecipientLabel(app);
    if (sendTextToTarget(app, wideToUtf8(text))) {
        app.lines.push_back(ChatLine{ChatLine::Kind::Text,
                                     L"ME",
                                     text,
                                     recipient,
                                     {},
                                     true});
        trimHistory(app);
        SetWindowTextW(app.edit, L"");
        app.sounds.play(sfx::Cue::Click);
        app.status = L"SENT TO " + recipient;
        InvalidateRect(app.hwnd, nullptr, FALSE);
    } else {
        app.status = L"SEND FAILED";
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
}

void sendUserSticker(App& app, std::size_t index) {
    if (index >= app.stickers.size()) return;
    const auto& item = app.stickers[index];
    std::vector<std::uint8_t> bytes;
    std::filesystem::path networkName;
    if (!prepareStickerBytes(item.path, bytes, networkName)) {
        app.status = L"STICKER INVALID OR TOO LARGE";
        app.stickerMenuOpen = false;
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return;
    }

    const std::wstring recipient = outgoingRecipientLabel(app);
    bool sent = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        sent = sendStickerToTarget(app, networkName, bytes) || sent;
    }

    if (sent) {
        app.lines.push_back(ChatLine{ChatLine::Kind::Sticker,
                                     L"ME",
                                     {},
                                     recipient,
                                     item.path,
                                     true});
        trimHistory(app);
        app.sounds.play(sfx::Cue::Sticker);
        app.status = L"STICKER SENT TO " + recipient;
    } else {
        const std::wstring detail = utf8ToWide(app.net.lastError());
        app.status = detail.empty() ? L"STICKER SEND FAILED" : L"STICKER SEND FAILED: " + detail;
    }
    app.stickerMenuOpen = false;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}
