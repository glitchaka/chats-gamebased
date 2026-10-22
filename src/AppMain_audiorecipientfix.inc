std::wstring audioRecipientLabel(const App& app) {
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

void sendAudioFromPicker(App& app) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.hwnd;
    ofn.lpstrFilter = L"Audio\0*.mp3;*.wav;*.ogg;*.m4a;*.aac;*.flac;*.wma\0MP3\0*.mp3\0WAV\0*.wav\0All files\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    const std::filesystem::path source(file);
    if (!isAudioExtension(source)) {
        app.status = L"INVALID AUDIO";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return;
    }

    const auto bytes = readBytes(source);
    if (bytes.empty()) {
        app.status = L"AUDIO INVALID OR OVER 16 MB";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return;
    }

    // Capture the visible destination before sending so the local history keeps
    // exactly who/group this audio was addressed to.
    const std::wstring recipient = audioRecipientLabel(app);

    if (sendAudioToTarget(app, source, bytes)) {
        app.lines.push_back(ChatLine{ChatLine::Kind::Audio,
                                     L"ME",
                                     source.filename().wstring(),
                                     recipient,
                                     source,
                                     true});
        trimHistory(app);
        app.status = L"AUDIO SENT TO " + recipient;
        app.sounds.play(sfx::Cue::Click);
    } else {
        const std::wstring detail = utf8ToWide(app.net.lastError().empty() ? app.audioNet.lastError() : app.net.lastError());
        app.status = detail.empty() ? L"AUDIO SEND FAILED" : L"AUDIO SEND FAILED: " + detail;
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}
