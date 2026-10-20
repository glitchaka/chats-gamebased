// This file intentionally uses the exact glyph sheet already generated from the supplied high-resolution font image.
// The sheet is an ASCII grid: columns 0..15, rows 0..3, characters 32 (' ') through 95 ('_').

int sourceGlyphIndex(wchar_t ch) {
    if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
    if (ch >= L' ' && ch <= L'_') return static_cast<int>(ch - L' ');
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

void drawPixelMark(HDC dc, wchar_t, int x, int y, int height, COLORREF color) {
    const int width = std::max(4, height / 2 + 2);
    const int unit = std::max(1, height / 8);
    RECT r{x + unit, y + height / 2, x + width - unit, y + height / 2 + unit};
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
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
            const int col = index % 16;
            const int row = index / 16;
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
    const int w = pixelTextWidth(text, height);
    const int x = static_cast<int>(r.left) + std::max(0, (static_cast<int>(r.right-r.left)-w)/2);
    const int y = static_cast<int>(r.top) + std::max(0, (static_cast<int>(r.bottom-r.top)-height)/2);
    drawPixelText(app, dc, text, x, y, height, color);
}
