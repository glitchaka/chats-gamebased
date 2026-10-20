bool importSticker(App& app) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.hwnd;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp\0PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0Bitmap\0*.bmp\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return false;

    const std::filesystem::path source(file);
    if (!isImageExtension(source)) {
        app.status = L"INVALID STICKER";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return false;
    }

    const auto bytes = readBytes(source);
    if (bytes.empty()) {
        app.status = L"STICKER TOO LARGE OR INVALID";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return false;
    }

    // Choosing a file sends that file. It is not copied into a persistent
    // sticker library and it will not reappear the next time the app starts.
    if (!sendStickerToTarget(app, source, bytes)) {
        app.status = L"STICKER SEND FAILED";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return false;
    }

    app.lines.push_back(ChatLine{ChatLine::Kind::Sticker, L"ME", {}, {}, source, true});
    trimHistory(app);
    app.sounds.play(sfx::Cue::Sticker);
    app.status = L"STICKER SENT";
    app.stickerMenuOpen = false;
    InvalidateRect(app.hwnd, nullptr, FALSE);
    return true;
}
