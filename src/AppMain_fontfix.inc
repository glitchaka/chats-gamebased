struct GlyphMetrics {
    int left;
    int top;
    int width;
    int height;
};

static constexpr GlyphMetrics kGlyphMetrics[36] = {
    {8,4,31,47}, {8,4,31,47}, {12,4,23,47}, {8,4,31,47}, {12,4,23,47}, {12,4,23,47},
    {8,4,31,47}, {8,4,31,47}, {20,4,8,47}, {18,4,11,48}, {8,4,31,47}, {12,4,23,47},
    {4,11,40,34}, {8,4,31,47}, {8,4,31,47}, {8,4,31,47}, {8,4,31,47}, {8,4,31,47},
    {8,4,31,47}, {12,4,23,47}, {8,4,31,47}, {4,4,39,47}, {4,11,40,34}, {8,4,31,47},
    {4,4,39,47}, {8,4,31,47}, {8,4,31,47}, {12,4,23,47}, {8,4,31,47}, {8,4,31,47},
    {8,4,31,47}, {8,4,31,47}, {8,4,31,47}, {8,4,31,47}, {8,4,31,47}, {8,4,31,47}
};

int sourceGlyphIndex(wchar_t ch) {
    if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
    if (ch >= L'A' && ch <= L'Z') return static_cast<int>(ch - L'A');
    if (ch >= L'0' && ch <= L'9') return 26 + static_cast<int>(ch - L'0');
    return -1;
}

int glyphRenderedWidth(int index, int height) {
    if (index < 0 || index >= 36) return std::max(4, height / 2 + 2);
    const GlyphMetrics& m = kGlyphMetrics[index];
    return std::max(1, (m.width * height + m.height / 2) / m.height);
}

int glyphAdvanceFor(wchar_t ch, int height) {
    if (ch == L' ') return std::max(4, height / 2);
    const int index = sourceGlyphIndex(ch);
    if (index >= 0) return glyphRenderedWidth(index, height) + std::max(1, height / 6);
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
    drawPixelMarkLegacy(dc, ch, x, y, height, color);
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
            const GlyphMetrics& m = kGlyphMetrics[index];
            const int col = index % 10;
            const int row = index / 10;
            const int destWidth = glyphRenderedWidth(index, height);
            const Gdiplus::Rect dest(cursor, y, destWidth, height);
            graphics.DrawImage(app.pixelFont.get(), dest,
                               col * kFontCellWidth + m.left,
                               row * kFontCellHeight + m.top,
                               m.width,
                               m.height,
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
