bool importSticker(App& app) {
    if (!importStickerLibraryOnly(app)) return false;
    if (app.stickers.empty()) {
        app.status = L"STICKER IMPORT FAILED";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return false;
    }

    // The file picker is the send action: import it into the user's library,
    // then immediately send the newly imported image to the current target.
    sendUserSticker(app, 0);
    return app.status == L"STICKER SENT";
}
