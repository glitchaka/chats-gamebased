
bool sendStickerToTarget(App& app, const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    const std::string filename = wideToUtf8(path.filename().wstring());
    if (app.targetMode == TargetMode::Friend && app.selectedFriend >= 0 && app.selectedFriend < static_cast<int>(app.friends.size()))
        return app.net.sendStickerFileTo(app.friends[static_cast<std::size_t>(app.selectedFriend)].endpoint, filename, bytes);
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 && app.selectedGroup < static_cast<int>(app.groups.size())) {
        const auto& g = app.groups[static_cast<std::size_t>(app.selectedGroup)];
        return app.net.sendGroupStickerFile(g.name, g.members, filename, bytes);
    }
    return app.net.sendStickerFile(filename, bytes);
}

bool sendAudioToTarget(App& app, const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    const std::string filename = wideToUtf8(path.filename().wstring());
    if (app.targetMode == TargetMode::Friend && app.selectedFriend >= 0 && app.selectedFriend < static_cast<int>(app.friends.size())) {
        return app.audioNet.sendTo(app.friends[static_cast<std::size_t>(app.selectedFriend)].endpoint, {}, filename, bytes);
    }
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 && app.selectedGroup < static_cast<int>(app.groups.size())) {
        const auto& g = app.groups[static_cast<std::size_t>(app.selectedGroup)];
        return app.audioNet.sendMany(g.members, g.name, filename, bytes);
    }
    return app.audioNet.sendMany(allFriendEndpoints(app), {}, filename, bytes);
}

std::filesystem::path saveReceivedSticker(const chatnet::Event& event) {
    if (event.stickerData.empty()) return {};
    std::error_code ec;
    std::filesystem::create_directories(receivedDirectory(), ec);
    std::filesystem::path name = utf8ToWide(event.stickerFilename);
    if (!isImageExtension(name)) name = L"sticker.png";
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto path = receivedDirectory() / (std::to_wstring(stamp) + L"_" + name.filename().wstring());
    std::ofstream file(path, std::ios::binary);
    if (!file) return {};
    file.write(reinterpret_cast<const char*>(event.stickerData.data()), static_cast<std::streamsize>(event.stickerData.size()));
    return file ? path : std::filesystem::path{};
}

std::filesystem::path saveReceivedAudio(const audionet::Event& event) {
    if (event.data.empty()) return {};
    std::error_code ec;
    std::filesystem::create_directories(receivedAudioDirectory(), ec);
    std::filesystem::path name = utf8ToWide(event.filename);
    if (!isAudioExtension(name)) name = L"audio.bin";
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto path = receivedAudioDirectory() / (std::to_wstring(stamp) + L"_" + name.filename().wstring());
    std::ofstream file(path, std::ios::binary);
    if (!file) return {};
    file.write(reinterpret_cast<const char*>(event.data.data()), static_cast<std::streamsize>(event.data.size()));
    return file ? path : std::filesystem::path{};
}

void updateUnreadHud(App& app) {
    if (!app.unreadHwnd) return;
    if (app.unreadCount <= 0 || IsWindowVisible(app.hwnd)) {
        ShowWindow(app.unreadHwnd, SW_HIDE);
        return;
    }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    SetWindowPos(app.unreadHwnd, HWND_TOPMOST, work.right-kUnreadWidth-18, work.top+18,
                 kUnreadWidth, kUnreadHeight, SWP_NOACTIVATE|SWP_SHOWWINDOW);
    InvalidateRect(app.unreadHwnd, nullptr, FALSE);
}

void restoreMain(App& app) {
    app.unreadCount = 0;
    updateUnreadHud(app);
    ShowWindow(app.hwnd, SW_SHOW);
    SetForegroundWindow(app.hwnd);
    SetFocus(app.edit);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void noteIncoming(App& app) {
    if (!IsWindowVisible(app.hwnd)) {
        ++app.unreadCount;
        updateUnreadHud(app);
    }
}

void updateNetwork(App& app) {
    bool changed = false;
    for (const auto& event : app.net.update()) {
        if (!event.endpoint.empty()) ensureFriend(app, event.endpoint, event.name);
        if (!event.channel.empty()) ensureIncomingGroup(app, event.channel, event.endpoint);

        if (event.type == chatnet::EventType::Message) {
            app.lines.push_back(ChatLine{ChatLine::Kind::Text, utf8ToWide(event.name), utf8ToWide(event.text), utf8ToWide(event.channel), {}, false});
            trimHistory(app);
            app.sounds.play(sfx::Cue::Incoming);
            noteIncoming(app);
            changed = true;
        } else if (event.type == chatnet::EventType::StickerImage) {
            const auto path = saveReceivedSticker(event);
            if (!path.empty()) {
                app.lines.push_back(ChatLine{ChatLine::Kind::Sticker, utf8ToWide(event.name), {}, utf8ToWide(event.channel), path, false});
                trimHistory(app);
                app.sounds.play(sfx::Cue::Sticker);
                noteIncoming(app);
                changed = true;
            }
        } else if (event.type == chatnet::EventType::PeerJoined || event.type == chatnet::EventType::PeerLeft) {
            changed = true;
        }
    }

    for (const auto& event : app.audioNet.update()) {
        if (!event.endpoint.empty()) ensureFriend(app, event.endpoint, event.name);
        if (!event.channel.empty()) ensureIncomingGroup(app, event.channel, event.endpoint);
        const auto path = saveReceivedAudio(event);
        if (!path.empty()) {
            app.lines.push_back(ChatLine{ChatLine::Kind::Audio, utf8ToWide(event.name), utf8ToWide(event.filename), utf8ToWide(event.channel), path, false});
            trimHistory(app);
            app.sounds.play(sfx::Cue::Incoming);
            noteIncoming(app);
            changed = true;
        }
    }

    if (changed) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void sendCurrentMessage(App& app) {
    std::wstring text = readEdit(app.edit);
    if (text.empty()) return;
    if (sendTextToTarget(app, wideToUtf8(text))) {
        app.lines.push_back(ChatLine{ChatLine::Kind::Text, L"ME", text, {}, {}, true});
        trimHistory(app);
        SetWindowTextW(app.edit, L"");
        app.sounds.play(sfx::Cue::Click);
        app.status = L"SENT";
        InvalidateRect(app.hwnd, nullptr, FALSE);
    } else {
        app.status = L"SEND FAILED";
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
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

    if (sendAudioToTarget(app, source, bytes)) {
        app.lines.push_back(ChatLine{ChatLine::Kind::Audio, L"ME", source.filename().wstring(), {}, source, true});
        trimHistory(app);
        app.status = L"AUDIO SENT";
        app.sounds.play(sfx::Cue::Click);
    } else {
        app.status = L"AUDIO SEND FAILED";
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void addFriendFromEdit(App& app) {
    const std::string endpoint = wideToUtf8(readEdit(app.friendEdit));
    if (endpoint.empty()) return;
    if (app.net.addFriendAddress(endpoint)) {
        ensureFriend(app, endpoint, endpoint);
        SetWindowTextW(app.friendEdit, L"");
        app.status = L"FRIEND ADDRESS ADDED";
        app.sounds.play(sfx::Cue::Click);
    } else {
        app.status = L"INVALID ADDRESS";
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void addGroupFromEdit(App& app) {
    std::string name = wideToUtf8(readEdit(app.groupEdit));
    if (name.empty()) name = "GROUP" + std::to_string(app.groups.size()+1);
    if (findGroup(app, name) < 0) app.groups.push_back(GroupItem{name, allFriendEndpoints(app)});
    SetWindowTextW(app.groupEdit, L"");
    app.status = L"GROUP CREATED";
    app.sounds.play(sfx::Cue::Click);
    InvalidateRect(app.hwnd, nullptr, FALSE);
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
    if (!GetOpenFileNameW(&ofn)) return false;

    std::filesystem::path source(file);
    if (!isImageExtension(source)) return false;
    const auto bytes = readBytes(source);
    if (bytes.empty()) {
        app.status = L"STICKER TOO LARGE OR INVALID";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(stickerDirectory(), ec);
    auto destination = stickerDirectory() / source.filename();
    if (std::filesystem::exists(destination, ec)) {
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        destination = stickerDirectory() / (std::to_wstring(stamp) + L"_" + source.filename().wstring());
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        app.status = L"COULD NOT IMPORT STICKER";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return false;
    }
    loadUserStickers(app);
    app.status = L"STICKER ADDED";
    app.sounds.play(sfx::Cue::Click);
    InvalidateRect(app.hwnd, nullptr, FALSE);
    return true;
}

void sendUserSticker(App& app, std::size_t index) {
    if (index >= app.stickers.size()) return;
    const auto& item = app.stickers[index];
    const auto bytes = readBytes(item.path);
    if (bytes.empty()) {
        app.status = L"STICKER INVALID";
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return;
    }
    if (sendStickerToTarget(app, item.path, bytes)) {
        app.lines.push_back(ChatLine{ChatLine::Kind::Sticker, L"ME", {}, {}, item.path, true});
        trimHistory(app);
        app.sounds.play(sfx::Cue::Sticker);
        app.status = L"STICKER SENT";
        app.stickerMenuOpen = false;
    } else {
        app.status = L"STICKER SEND FAILED";
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

HWND createEdit(HWND parent, HFONT font, int limit) {
    HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                                0,0,100,24,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(edit, EM_SETLIMITTEXT, limit, 0);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN|EC_RIGHTMARGIN, MAKELPARAM(5,5));
    return edit;
}

void layout(App& app) {
    app.closeRect = {866,16,900,50};
    app.copyRect = {776,88,892,120};
    app.themeRect = {650,88,766,120};
    app.allRect = {636,150,884,178};
    app.addFriendRect = {810,190,884,220};
    app.addGroupRect = {810,386,884,416};