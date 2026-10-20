constexpr std::size_t kNetworkStickerBytes = 512u * 1024u;

bool findPngEncoder(CLSID& clsid) {
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) return false;
    std::vector<BYTE> buffer(bytes);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (codecs[i].MimeType && wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

bool bitmapToPngBytes(Gdiplus::Bitmap& bitmap, std::vector<std::uint8_t>& bytes) {
    CLSID png{};
    if (!findPngEncoder(png)) return false;

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK || !stream) return false;
    const auto status = bitmap.Save(stream, &png, nullptr);
    if (status != Gdiplus::Ok) {
        stream->Release();
        return false;
    }

    HGLOBAL memory = nullptr;
    if (GetHGlobalFromStream(stream, &memory) != S_OK || !memory) {
        stream->Release();
        return false;
    }
    const SIZE_T size = GlobalSize(memory);
    void* data = GlobalLock(memory);
    if (!data || size == 0) {
        if (data) GlobalUnlock(memory);
        stream->Release();
        return false;
    }
    const auto* first = static_cast<const std::uint8_t*>(data);
    bytes.assign(first, first + size);
    GlobalUnlock(memory);
    stream->Release();
    return true;
}

bool prepareStickerBytes(const std::filesystem::path& source,
                         std::vector<std::uint8_t>& bytes,
                         std::filesystem::path& networkName) {
    bytes = readBytes(source);
    if (bytes.empty()) return false;
    networkName = source;
    if (bytes.size() <= kNetworkStickerBytes) return true;

    Gdiplus::Bitmap original(source.c_str(), FALSE);
    if (original.GetLastStatus() != Gdiplus::Ok || original.GetWidth() == 0 || original.GetHeight() == 0) return false;

    CLSID png{};
    if (!findPngEncoder(png)) return false;
    const int limits[] = {640, 512, 384, 320, 256, 192};
    for (int limit : limits) {
        const double scale = std::min(1.0, std::min(static_cast<double>(limit) / original.GetWidth(),
                                                    static_cast<double>(limit) / original.GetHeight()));
        const int width = std::max(1, static_cast<int>(original.GetWidth() * scale));
        const int height = std::max(1, static_cast<int>(original.GetHeight() * scale));
        Gdiplus::Bitmap reduced(width, height, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(&reduced);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.DrawImage(&original, Gdiplus::Rect(0, 0, width, height));

        std::vector<std::uint8_t> encoded;
        if (bitmapToPngBytes(reduced, encoded) && !encoded.empty() && encoded.size() <= kNetworkStickerBytes) {
            bytes = std::move(encoded);
            networkName = source.stem().wstring() + L".png";
            return true;
        }
    }
    return false;
}

bool importSticker(App& app) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.hwnd;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp\0PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0Bitmap\0*.bmp\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    const BOOL chosen = GetOpenFileNameW(&ofn);
    app.stickerMenuOpen = false;
    if (!chosen) {
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return false;
    }

    const std::filesystem::path source(file);
    if (!isImageExtension(source)) {
        app.status = L"INVALID STICKER";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return false;
    }

    auto image = std::make_unique<Gdiplus::Bitmap>(source.c_str(), FALSE);
    if (!image || image->GetLastStatus() != Gdiplus::Ok || image->GetWidth() == 0 || image->GetHeight() == 0) {
        app.status = L"INVALID STICKER";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return false;
    }

    app.stickers.insert(app.stickers.begin(), StickerItem{source, std::move(image)});
    if (app.stickers.size() > 32) app.stickers.resize(32);
    app.status = L"STICKER LOADED";
    app.sounds.play(sfx::Cue::Click);
    InvalidateRect(app.hwnd, nullptr, FALSE);
    return true;
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

    if (sendStickerToTarget(app, networkName, bytes)) {
        app.lines.push_back(ChatLine{ChatLine::Kind::Sticker, L"ME", {}, {}, item.path, true});
        trimHistory(app);
        app.sounds.play(sfx::Cue::Sticker);
        app.status = L"STICKER SENT";
    } else {
        app.status = L"STICKER SEND FAILED";
    }
    app.stickerMenuOpen = false;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}
