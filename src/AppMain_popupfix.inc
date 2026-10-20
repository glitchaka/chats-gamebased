void paintStickerPopupFixed(App& app, HDC dc) {
    app.stickerMenuRects.clear();
    if (!app.stickerMenuOpen) return;
    const Theme& t = theme(app);

    // Keep the popup completely inside the message panel. It must never cover
    // CONTACTS, the group controls, or the bottom input bar.
    RECT popup{300,290,598,494};
    drawChamferedBox(dc,popup,t.panel2,t.accentBright,8);
    drawPixelText(app,dc,L"MY STICKERS",316,302,15,t.text);
    app.stickerImportRect = {478,298,582,330};
    drawChamferedBox(dc,app.stickerImportRect,t.accent,t.accentBright,6);
    drawPixelCentered(app,dc,L"ADD",app.stickerImportRect,13,t.bg);

    const std::size_t visible = std::min<std::size_t>(app.stickers.size(),8);
    for (std::size_t i=0;i<visible;++i) {
        const int col = static_cast<int>(i%4);
        const int row = static_cast<int>(i/4);
        RECT r{316+col*64,342+row*62,368+col*64,394+row*62};
        drawChamferedBox(dc,r,t.panel,t.accent,6);
        drawImageFit(dc,app.stickers[i].image.get(),RECT{r.left+4,r.top+4,r.right-4,r.bottom-4});
        app.stickerMenuRects.push_back(r);
    }
    if (app.stickers.empty()) drawPixelText(app,dc,L"CHOOSE ADD TO SEND",326,382,12,t.muted);
}
