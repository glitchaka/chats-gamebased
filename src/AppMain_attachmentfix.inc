struct AudioChunkAssembly {
    std::string senderEndpoint;
    std::wstring senderName;
    std::wstring channel;
    std::string transferId;
    std::wstring filename;
    std::vector<std::vector<std::uint8_t>> chunks;
    std::vector<bool> received;
    std::size_t receivedCount{};
    std::size_t totalBytes{};
    std::chrono::steady_clock::time_point updated{};
};

struct RecentSticker {
    std::uint64_t hash{};
    std::chrono::steady_clock::time_point seen{};
};

std::vector<AudioChunkAssembly> gAudioAssemblies;
std::vector<RecentSticker> gRecentStickers;

std::uint64_t stickerEventHash(const chatnet::Event& event) {
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&](std::uint8_t byte) {
        hash ^= byte;
        hash *= 1099511628211ull;
    };
    for (unsigned char ch : event.endpoint) mix(ch);
    for (unsigned char ch : event.stickerFilename) mix(ch);
    for (std::uint8_t byte : event.stickerData) mix(byte);
    return hash;
}

bool isDuplicateStickerEvent(const chatnet::Event& event) {
    const auto now = std::chrono::steady_clock::now();
    gRecentStickers.erase(
        std::remove_if(gRecentStickers.begin(), gRecentStickers.end(), [&](const RecentSticker& item) {
            return now - item.seen > std::chrono::seconds(4);
        }),
        gRecentStickers.end());

    const std::uint64_t hash = stickerEventHash(event);
    for (const auto& item : gRecentStickers) {
        if (item.hash == hash) return true;
    }
    gRecentStickers.push_back(RecentSticker{hash, now});
    return false;
}

std::vector<std::string> attachmentTargetEndpoints(const App& app) {
    std::vector<std::string> result;
    auto add = [&](const std::string& endpoint) {
        if (!endpoint.empty() && std::find(result.begin(), result.end(), endpoint) == result.end()) result.push_back(endpoint);
    };

    if (app.targetMode == TargetMode::Friend && app.selectedFriend >= 0 &&
        app.selectedFriend < static_cast<int>(app.friends.size())) {
        add(app.friends[static_cast<std::size_t>(app.selectedFriend)].endpoint);
        return result;
    }
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 &&
        app.selectedGroup < static_cast<int>(app.groups.size())) {
        for (const auto& endpoint : app.groups[static_cast<std::size_t>(app.selectedGroup)].members) add(endpoint);
        return result;
    }

    for (const auto& friendItem : app.friends) add(friendItem.endpoint);
    for (const auto& peer : app.net.peers()) add(peer.endpoint());
    for (const auto& endpoint : app.net.savedAddresses()) add(endpoint);
    return result;
}

bool endpointLooksPrivate(const std::string& endpoint) {
    std::string host = endpoint;
    const std::size_t colon = host.rfind(':');
    if (colon != std::string::npos) host.resize(colon);
    if (host.rfind("10.", 0) == 0 || host.rfind("192.168.", 0) == 0 || host.rfind("127.", 0) == 0) return true;
    if (host.rfind("172.", 0) == 0) {
        const std::size_t dot = host.find('.', 4);
        if (dot != std::string::npos) {
            try {
                const int second = std::stoi(host.substr(4, dot - 4));
                return second >= 16 && second <= 31;
            } catch (...) {
            }
        }
    }
    return false;
}

std::string sanitizedAttachmentName(const std::filesystem::path& path) {
    std::string name = wideToUtf8(path.filename().wstring());
    if (name.empty()) name = "audio.bin";
    for (char& ch : name) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) ch = '_';
    }
    if (name.size() > 28) {
        std::string ext = wideToUtf8(path.extension().wstring());
        if (ext.size() > 8) ext.clear();
        const std::size_t stemSize = 28 > ext.size() ? 28 - ext.size() : 20;
        name = name.substr(0, stemSize) + ext;
    }
    return name;
}

std::string makeAudioChunkFilename(const std::string& transferId,
                                   std::size_t index,
                                   std::size_t count,
                                   const std::filesystem::path& source) {
    return "AUD-" + transferId + "-" + std::to_string(index) + "-" +
           std::to_string(count) + "-" + sanitizedAttachmentName(source);
}

bool parseAudioChunkFilename(const std::string& encoded,
                             std::string& transferId,
                             std::size_t& index,
                             std::size_t& count,
                             std::wstring& filename) {
    if (encoded.rfind("AUD-", 0) != 0) return false;
    const std::size_t p1 = encoded.find('-', 4);
    const std::size_t p2 = p1 == std::string::npos ? p1 : encoded.find('-', p1 + 1);
    const std::size_t p3 = p2 == std::string::npos ? p2 : encoded.find('-', p2 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) return false;

    try {
        transferId = encoded.substr(4, p1 - 4);
        index = static_cast<std::size_t>(std::stoul(encoded.substr(p1 + 1, p2 - p1 - 1)));
        count = static_cast<std::size_t>(std::stoul(encoded.substr(p2 + 1, p3 - p2 - 1)));
    } catch (...) {
        return false;
    }
    if (transferId.empty() || count == 0 || count > 1024 || index >= count) return false;
    filename = utf8ToWide(encoded.substr(p3 + 1));
    if (filename.empty()) filename = L"audio.bin";
    return true;
}

bool sendAudioChunksThroughChat(App& app,
                                const std::filesystem::path& source,
                                const std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t kChunkBytes = 96u * 1024u;
    const std::size_t count = (bytes.size() + kChunkBytes - 1) / kChunkBytes;
    if (count == 0 || count > 1024) return false;

    const std::uint64_t seed = GetTickCount64() ^
        (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32) ^
        static_cast<std::uint64_t>(bytes.size());
    const std::string transferId = std::to_string(seed);

    bool sentAny = false;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t offset = index * kChunkBytes;
        const std::size_t size = std::min(kChunkBytes, bytes.size() - offset);
        std::vector<std::uint8_t> piece(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
        const std::filesystem::path wireName = utf8ToWide(makeAudioChunkFilename(transferId, index, count, source));

        bool pieceSent = false;
        // Three independent copies make the existing UDP chunk transport much less
        // sensitive to a single dropped datagram. Receivers de-duplicate by chunk id.
        for (int attempt = 0; attempt < 3; ++attempt) {
            pieceSent = sendStickerToTarget(app, wireName, piece) || pieceSent;
        }
        if (!pieceSent) return false;
        sentAny = true;
    }
    return sentAny;
}

bool sendAudioToTarget(App& app, const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    const auto endpoints = attachmentTargetEndpoints(app);
    const std::string filename = wideToUtf8(path.filename().wstring());

    // TCP is reliable and efficient on LAN. Internet peers use the same UDP/NAT
    // path as chat/stickers so an unrelated TCP mapping is not required.
    if (!endpoints.empty() &&
        std::all_of(endpoints.begin(), endpoints.end(), [](const std::string& endpoint) { return endpointLooksPrivate(endpoint); })) {
        bool allSent = true;
        const std::string channel = (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 &&
                                     app.selectedGroup < static_cast<int>(app.groups.size()))
                                        ? app.groups[static_cast<std::size_t>(app.selectedGroup)].name
                                        : std::string{};
        for (const auto& endpoint : endpoints) {
            if (!app.audioNet.sendTo(endpoint, channel, filename, bytes)) allSent = false;
        }
        if (allSent) return true;
    }

    return sendAudioChunksThroughChat(app, path, bytes);
}

bool ingestAudioChunk(App& app, const chatnet::Event& event) {
    std::string transferId;
    std::size_t index = 0;
    std::size_t count = 0;
    std::wstring filename;
    if (!parseAudioChunkFilename(event.stickerFilename, transferId, index, count, filename)) return false;

    const auto now = std::chrono::steady_clock::now();
    gAudioAssemblies.erase(
        std::remove_if(gAudioAssemblies.begin(), gAudioAssemblies.end(), [&](const AudioChunkAssembly& item) {
            return now - item.updated > std::chrono::seconds(45);
        }),
        gAudioAssemblies.end());

    AudioChunkAssembly* assembly = nullptr;
    for (auto& item : gAudioAssemblies) {
        if (item.transferId == transferId && item.senderEndpoint == event.endpoint) {
            assembly = &item;
            break;
        }
    }
    if (!assembly) {
        AudioChunkAssembly created;
        created.senderEndpoint = event.endpoint;
        created.senderName = utf8ToWide(event.name);
        created.channel = utf8ToWide(event.channel);
        created.transferId = transferId;
        created.filename = filename;
        created.chunks.resize(count);
        created.received.assign(count, false);
        created.updated = now;
        gAudioAssemblies.push_back(std::move(created));
        assembly = &gAudioAssemblies.back();
    }

    if (assembly->chunks.size() != count || index >= assembly->chunks.size()) return true;
    assembly->updated = now;
    if (!assembly->received[index]) {
        assembly->chunks[index] = event.stickerData;
        assembly->received[index] = true;
        ++assembly->receivedCount;
        assembly->totalBytes += event.stickerData.size();
    }

    if (assembly->totalBytes > kMaxLocalAttachmentBytes) {
        assembly->updated = std::chrono::steady_clock::time_point{};
        return true;
    }
    if (assembly->receivedCount != assembly->chunks.size()) return true;

    std::error_code ec;
    std::filesystem::create_directories(receivedAudioDirectory(), ec);
    std::filesystem::path safeName = assembly->filename;
    if (!isAudioExtension(safeName)) safeName = L"audio.bin";
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto output = receivedAudioDirectory() /
        (std::to_wstring(stamp) + L"_" + safeName.filename().wstring());

    std::ofstream file(output, std::ios::binary);
    if (file) {
        for (const auto& chunk : assembly->chunks) {
            file.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        }
    }
    if (file) {
        app.lines.push_back(ChatLine{ChatLine::Kind::Audio, assembly->senderName, assembly->filename,
                                     assembly->channel, output, false});
        trimHistory(app);
        app.sounds.play(sfx::Cue::Incoming);
        noteIncoming(app);
    }

    assembly->updated = std::chrono::steady_clock::time_point{};
    return true;
}

void updateNetwork(App& app) {
    bool changed = false;
    for (const auto& event : app.net.update()) {
        if (!event.endpoint.empty()) ensureFriend(app, event.endpoint, event.name);
        if (!event.channel.empty()) ensureIncomingGroup(app, event.channel, event.endpoint);

        if (event.type == chatnet::EventType::Message) {
            app.lines.push_back(ChatLine{ChatLine::Kind::Text, utf8ToWide(event.name), utf8ToWide(event.text),
                                         utf8ToWide(event.channel), {}, false});
            trimHistory(app);
            app.sounds.play(sfx::Cue::Incoming);
            noteIncoming(app);
            changed = true;
        } else if (event.type == chatnet::EventType::StickerImage) {
            if (ingestAudioChunk(app, event)) {
                changed = true;
                continue;
            }
            if (isDuplicateStickerEvent(event)) continue;
            const auto path = saveReceivedSticker(event);
            if (!path.empty()) {
                app.lines.push_back(ChatLine{ChatLine::Kind::Sticker, utf8ToWide(event.name), {},
                                             utf8ToWide(event.channel), path, false});
                trimHistory(app);
                app.sounds.play(sfx::Cue::Sticker);
                noteIncoming(app);
                changed = true;
            }
        } else if (event.type == chatnet::EventType::PeerJoined || event.type == chatnet::EventType::PeerLeft) {
            changed = true;
        }
    }

    // Keep accepting the reliable LAN TCP transport as well.
    for (const auto& event : app.audioNet.update()) {
        if (!event.endpoint.empty()) ensureFriend(app, event.endpoint, event.name);
        if (!event.channel.empty()) ensureIncomingGroup(app, event.channel, event.endpoint);
        const auto path = saveReceivedAudio(event);
        if (!path.empty()) {
            app.lines.push_back(ChatLine{ChatLine::Kind::Audio, utf8ToWide(event.name), utf8ToWide(event.filename),
                                         utf8ToWide(event.channel), path, false});
            trimHistory(app);
            app.sounds.play(sfx::Cue::Incoming);
            noteIncoming(app);
            changed = true;
        }
    }

    if (changed) InvalidateRect(app.hwnd, nullptr, FALSE);
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
        const std::wstring detail = utf8ToWide(app.net.lastError().empty() ? app.audioNet.lastError() : app.net.lastError());
        app.status = detail.empty() ? L"AUDIO SEND FAILED" : L"AUDIO SEND FAILED: " + detail;
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
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

    bool sent = false;
    // Duplicate transport is de-duplicated on receive and protects a sticker
    // against a transient dropped UDP packet.
    for (int attempt = 0; attempt < 2; ++attempt) {
        sent = sendStickerToTarget(app, networkName, bytes) || sent;
    }

    if (sent) {
        app.lines.push_back(ChatLine{ChatLine::Kind::Sticker, L"ME", {}, {}, item.path, true});
        trimHistory(app);
        app.sounds.play(sfx::Cue::Sticker);
        app.status = L"STICKER SENT";
    } else {
        const std::wstring detail = utf8ToWide(app.net.lastError());
        app.status = detail.empty() ? L"STICKER SEND FAILED" : L"STICKER SEND FAILED: " + detail;
    }
    app.stickerMenuOpen = false;
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

    // Keep it only for this running session, then send the file immediately.
    app.stickers.insert(app.stickers.begin(), StickerItem{source, std::move(image)});
    if (app.stickers.size() > 32) app.stickers.resize(32);
    sendUserSticker(app, 0);
    return true;
}
