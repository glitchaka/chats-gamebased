enum class AttachmentAudioMode { Stopped, Playing, Paused, Error };

constexpr wchar_t kAttachmentAudioAlias[] = L"chat_attachment";
std::filesystem::path gAttachmentAudioPath;
std::wstring gAttachmentAudioLastMode;

void closeInternalAudio() {
    mciSendStringW(L"close chat_attachment", nullptr, 0, nullptr);
    gAttachmentAudioPath.clear();
    gAttachmentAudioLastMode.clear();
}

std::wstring attachmentAudioStatus(const wchar_t* item) {
    if (gAttachmentAudioPath.empty()) return {};
    wchar_t buffer[64]{};
    std::wstring command = L"status ";
    command += kAttachmentAudioAlias;
    command += L" ";
    command += item;
    if (mciSendStringW(command.c_str(), buffer, static_cast<UINT>(std::size(buffer)), nullptr) != 0) return {};
    return buffer;
}

AttachmentAudioMode attachmentAudioMode() {
    const std::wstring mode = attachmentAudioStatus(L"mode");
    if (mode == L"playing") return AttachmentAudioMode::Playing;
    if (mode == L"paused") return AttachmentAudioMode::Paused;
    if (mode.empty() && !gAttachmentAudioPath.empty()) return AttachmentAudioMode::Error;
    return AttachmentAudioMode::Stopped;
}

std::uint64_t attachmentAudioNumber(const wchar_t* item) {
    const std::wstring value = attachmentAudioStatus(item);
    if (value.empty()) return 0;
    wchar_t* end = nullptr;
    const unsigned long long parsed = _wcstoui64(value.c_str(), &end, 10);
    return (end && *end == L'\0') ? static_cast<std::uint64_t>(parsed) : 0;
}

bool openInternalAudio(const std::filesystem::path& path) {
    closeInternalAudio();
    if (path.empty() || !std::filesystem::exists(path)) return false;

    const std::wstring quoted = L"\"" + path.wstring() + L"\"";
    std::wstring command = L"open " + quoted + L" alias " + kAttachmentAudioAlias;
    MCIERROR error = mciSendStringW(command.c_str(), nullptr, 0, nullptr);

    // Some Windows installations need the legacy MPEG/video MCI driver named
    // explicitly for MP3/AAC-style containers. Keep this as a fallback while
    // still letting MCI auto-detect WAV/WMA and other registered codecs first.
    if (error != 0) {
        command = L"open " + quoted + L" type mpegvideo alias " + kAttachmentAudioAlias;
        error = mciSendStringW(command.c_str(), nullptr, 0, nullptr);
    }
    if (error != 0) {
        mciSendStringW(L"close chat_attachment", nullptr, 0, nullptr);
        return false;
    }

    mciSendStringW(L"set chat_attachment time format milliseconds", nullptr, 0, nullptr);
    gAttachmentAudioPath = path;
    gAttachmentAudioLastMode.clear();
    return true;
}

bool playInternalAudioFromStart() {
    if (gAttachmentAudioPath.empty()) return false;
    mciSendStringW(L"seek chat_attachment to start", nullptr, 0, nullptr);
    return mciSendStringW(L"play chat_attachment", nullptr, 0, nullptr) == 0;
}

bool toggleInternalAudio(App& app, const std::filesystem::path& path) {
    if (path.empty()) return false;

    if (gAttachmentAudioPath.empty() || gAttachmentAudioPath != path) {
        if (!openInternalAudio(path) || !playInternalAudioFromStart()) {
            closeInternalAudio();
            app.status = L"AUDIO FORMAT NOT SUPPORTED BY WINDOWS";
            return false;
        }
        app.status = L"AUDIO PLAYING";
        return true;
    }

    switch (attachmentAudioMode()) {
    case AttachmentAudioMode::Playing:
        if (mciSendStringW(L"pause chat_attachment", nullptr, 0, nullptr) == 0) {
            app.status = L"AUDIO PAUSED";
            return true;
        }
        break;
    case AttachmentAudioMode::Paused:
        if (mciSendStringW(L"resume chat_attachment", nullptr, 0, nullptr) == 0) {
            app.status = L"AUDIO PLAYING";
            return true;
        }
        break;
    case AttachmentAudioMode::Stopped:
        if (playInternalAudioFromStart()) {
            app.status = L"AUDIO PLAYING";
            return true;
        }
        break;
    case AttachmentAudioMode::Error:
        break;
    }

    app.status = L"AUDIO PLAYBACK FAILED";
    return false;
}

HINSTANCE internalAudioShellExecuteW(HWND hwnd, LPCWSTR operation, LPCWSTR file,
                                    LPCWSTR, LPCWSTR, INT) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!app || !file || (operation && _wcsicmp(operation, L"open") != 0)) {
        return reinterpret_cast<HINSTANCE>(static_cast<INT_PTR>(SE_ERR_NOASSOC));
    }

    const bool ok = toggleInternalAudio(*app, std::filesystem::path(file));
    InvalidateRect(hwnd, nullptr, FALSE);
    return reinterpret_cast<HINSTANCE>(static_cast<INT_PTR>(ok ? 33 : SE_ERR_NOASSOC));
}

bool audioPlayerNeedsRepaint() {
    if (gAttachmentAudioPath.empty()) return false;
    const std::wstring mode = attachmentAudioStatus(L"mode");
    const bool changed = mode != gAttachmentAudioLastMode;
    gAttachmentAudioLastMode = mode;
    return changed || mode == L"playing";
}

void updateNetworkWithAudioPlayer(App& app) {
    updateNetwork(app);
    if (audioPlayerNeedsRepaint()) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void removeTrayIconWithAudioShutdown(App& app) {
    closeInternalAudio();
    removeTrayIcon(app);
}

void paintInternalAudioPlayer(App& app, HDC dc) {
    if (app.audioHitRects.empty()) return;
    const Theme& t = theme(app);
    const AttachmentAudioMode mode = attachmentAudioMode();
    const std::uint64_t length = attachmentAudioNumber(L"length");
    const std::uint64_t position = attachmentAudioNumber(L"position");

    for (const auto& item : app.audioHitRects) {
        const RECT& ar = item.first;
        const bool current = !gAttachmentAudioPath.empty() && gAttachmentAudioPath == item.second;

        RECT button{ar.left + 5, ar.top + 4, ar.left + 86, ar.bottom - 5};
        fillSolid(dc, button, t.panel2);

        std::wstring label = L"> PLAY";
        if (current && mode == AttachmentAudioMode::Playing) label = L"II PAUSE";
        else if (current && mode == AttachmentAudioMode::Paused) label = L"> RESUME";
        drawPixelText(app, dc, label, button.left + 5, button.top + 5, 9,
                      current ? t.accentBright : t.text);

        RECT track{ar.left + 6, ar.bottom - 5, ar.right - 6, ar.bottom - 2};
        fillSolid(dc, track, t.muted);
        if (current && length > 0) {
            const std::uint64_t capped = std::min(position, length);
            const int width = track.right - track.left;
            RECT progress{track.left, track.top,
                          track.left + static_cast<int>((static_cast<std::uint64_t>(width) * capped) / length),
                          track.bottom};
            fillSolid(dc, progress, t.accentBright);
        }
    }
}

void paint(App& app, HDC target) {
    paintWithSenderIdentities(app, target);
    paintInternalAudioPlayer(app, target);
}
