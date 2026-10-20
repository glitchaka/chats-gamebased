#include "AudioNet.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace audionet {
namespace {

constexpr std::uint32_t kMagic = 0x41484347u; // GCHA
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kMaxAudioBytes = 16u * 1024u * 1024u;
constexpr int kConnectTimeoutMs = 3000;
constexpr int kIoTimeoutMs = 15000;

#pragma pack(push, 1)
struct Header {
    std::uint32_t magic{};
    std::uint16_t version{};
    std::uint16_t nameBytes{};
    std::uint16_t channelBytes{};
    std::uint16_t filenameBytes{};
    std::uint32_t totalSize{};
};
#pragma pack(pop)

std::string safeFilename(std::string name) {
    if (name.empty()) return "audio.bin";
    for (char& ch : name) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) ch = '_';
    }
    if (name.size() > 120) name.resize(120);
    return name;
}

bool sendAll(SOCKET socket, const void* data, std::size_t size) {
    const char* ptr = static_cast<const char*>(data);
    while (size > 0) {
        const int chunk = static_cast<int>(std::min<std::size_t>(size, 64u * 1024u));
        const int sent = send(socket, ptr, chunk, 0);
        if (sent <= 0) return false;
        ptr += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool recvAll(SOCKET socket, void* data, std::size_t size) {
    char* ptr = static_cast<char*>(data);
    while (size > 0) {
        const int chunk = static_cast<int>(std::min<std::size_t>(size, 64u * 1024u));
        const int got = recv(socket, ptr, chunk, 0);
        if (got <= 0) return false;
        ptr += got;
        size -= static_cast<std::size_t>(got);
    }
    return true;
}

bool parseEndpoint(const std::string& endpoint, sockaddr_in& address) {
    if (endpoint.empty()) return false;
    std::string host = endpoint;
    std::string port = "34197";
    const std::size_t colon = endpoint.rfind(':');
    if (colon != std::string::npos && colon > 0 && colon + 1 < endpoint.size()) {
        host = endpoint.substr(0, colon);
        port = endpoint.substr(colon + 1);
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0 || !result) {
        if (result) freeaddrinfo(result);
        return false;
    }
    address = *reinterpret_cast<sockaddr_in*>(result->ai_addr);
    freeaddrinfo(result);
    return true;
}

bool connectWithTimeout(SOCKET socket, const sockaddr_in& address) {
    u_long nonBlocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonBlocking) != 0) return false;

    const int rc = connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (rc == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEINVAL) return false;

        fd_set writeSet{};
        FD_ZERO(&writeSet);
        FD_SET(socket, &writeSet);
        timeval timeout{};
        timeout.tv_sec = kConnectTimeoutMs / 1000;
        timeout.tv_usec = (kConnectTimeoutMs % 1000) * 1000;
        if (select(0, nullptr, &writeSet, nullptr, &timeout) <= 0) return false;

        int socketError = 0;
        int len = sizeof(socketError);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &len) != 0 || socketError != 0) return false;
    }

    nonBlocking = 0;
    return ioctlsocket(socket, FIONBIO, &nonBlocking) == 0;
}

std::string sourceEndpoint(const sockaddr_in& address, std::uint16_t port) {
    char ip[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &address.sin_addr, ip, static_cast<DWORD>(sizeof(ip)))) return {};
    return std::string(ip) + ":" + std::to_string(port);
}

} // namespace

struct AudioNet::Impl {
    SOCKET listenSocket{INVALID_SOCKET};
    bool winsockStarted{false};
    std::uint16_t port{0};
    std::string name;
    std::string error;
    std::atomic<bool> running{false};
    std::thread listener;
    std::mutex queueMutex;
    std::vector<Event> pending;

    void listenLoop() {
        while (running.load()) {
            sockaddr_in from{};
            int fromLen = sizeof(from);
            SOCKET client = accept(listenSocket, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (client == INVALID_SOCKET) {
                if (!running.load()) break;
                Sleep(10);
                continue;
            }

            DWORD timeout = kIoTimeoutMs;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

            Header header{};
            bool ok = recvAll(client, &header, sizeof(header));
            if (!ok || header.magic != kMagic || header.version != kVersion ||
                header.totalSize == 0 || header.totalSize > kMaxAudioBytes ||
                header.nameBytes > 64 || header.channelBytes > 64 || header.filenameBytes > 128) {
                closesocket(client);
                continue;
            }

            Event event;
            event.name.resize(header.nameBytes);
            event.channel.resize(header.channelBytes);
            event.filename.resize(header.filenameBytes);
            event.data.resize(header.totalSize);

            ok = (event.name.empty() || recvAll(client, event.name.data(), event.name.size())) &&
                 (event.channel.empty() || recvAll(client, event.channel.data(), event.channel.size())) &&
                 (event.filename.empty() || recvAll(client, event.filename.data(), event.filename.size())) &&
                 recvAll(client, event.data.data(), event.data.size());
            closesocket(client);
            if (!ok) continue;

            event.filename = safeFilename(event.filename);
            event.endpoint = sourceEndpoint(from, port);
            std::lock_guard<std::mutex> lock(queueMutex);
            pending.push_back(std::move(event));
        }
    }
};

AudioNet::AudioNet() : impl_(std::make_unique<Impl>()) {}
AudioNet::~AudioNet() { stop(); }
AudioNet::AudioNet(AudioNet&&) noexcept = default;
AudioNet& AudioNet::operator=(AudioNet&&) noexcept = default;

bool AudioNet::start(const std::string& name, std::uint16_t port) {
    stop();
    impl_->error.clear();

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        impl_->error = "audio WSAStartup failed";
        return false;
    }
    impl_->winsockStarted = true;

    impl_->listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->listenSocket == INVALID_SOCKET) {
        impl_->error = "audio socket failed";
        stop();
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(impl_->listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (bind(impl_->listenSocket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0 ||
        listen(impl_->listenSocket, 8) != 0) {
        impl_->error = "audio listen failed";
        stop();
        return false;
    }

    impl_->port = port;
    impl_->name = name.empty() ? "PLAYER" : name.substr(0, 63);
    impl_->running.store(true);
    impl_->listener = std::thread([this]() { impl_->listenLoop(); });
    return true;
}

void AudioNet::stop() {
    if (!impl_) return;
    impl_->running.store(false);
    if (impl_->listenSocket != INVALID_SOCKET) {
        shutdown(impl_->listenSocket, SD_BOTH);
        closesocket(impl_->listenSocket);
        impl_->listenSocket = INVALID_SOCKET;
    }
    if (impl_->listener.joinable()) impl_->listener.join();
    {
        std::lock_guard<std::mutex> lock(impl_->queueMutex);
        impl_->pending.clear();
    }
    impl_->port = 0;
    if (impl_->winsockStarted) {
        WSACleanup();
        impl_->winsockStarted = false;
    }
}

bool AudioNet::isRunning() const noexcept {
    return impl_ && impl_->running.load();
}

std::vector<Event> AudioNet::update() {
    std::vector<Event> result;
    if (!impl_) return result;
    std::lock_guard<std::mutex> lock(impl_->queueMutex);
    result.swap(impl_->pending);
    return result;
}

bool AudioNet::sendTo(const std::string& endpoint,
                      const std::string& channel,
                      const std::string& filename,
                      const std::vector<std::uint8_t>& bytes) {
    if (!isRunning() || bytes.empty() || bytes.size() > kMaxAudioBytes) {
        if (impl_) impl_->error = "audio must be between 1 byte and 16 MB";
        return false;
    }

    sockaddr_in address{};
    if (!parseEndpoint(endpoint, address)) {
        impl_->error = "invalid audio endpoint";
        return false;
    }

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) {
        impl_->error = "audio socket failed";
        return false;
    }
    DWORD timeout = kIoTimeoutMs;
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    if (!connectWithTimeout(client, address)) {
        impl_->error = "audio connection failed";
        closesocket(client);
        return false;
    }

    const std::string cleanName = safeFilename(filename);
    const std::string cleanChannel = channel.substr(0, 63);
    Header header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.nameBytes = static_cast<std::uint16_t>(impl_->name.size());
    header.channelBytes = static_cast<std::uint16_t>(cleanChannel.size());
    header.filenameBytes = static_cast<std::uint16_t>(cleanName.size());
    header.totalSize = static_cast<std::uint32_t>(bytes.size());

    const bool ok = sendAll(client, &header, sizeof(header)) &&
                    sendAll(client, impl_->name.data(), impl_->name.size()) &&
                    sendAll(client, cleanChannel.data(), cleanChannel.size()) &&
                    sendAll(client, cleanName.data(), cleanName.size()) &&
                    sendAll(client, bytes.data(), bytes.size());
    closesocket(client);
    if (!ok) impl_->error = "audio send failed";
    return ok;
}

bool AudioNet::sendMany(const std::vector<std::string>& endpoints,
                        const std::string& channel,
                        const std::string& filename,
                        const std::vector<std::uint8_t>& bytes) {
    bool sent = false;
    for (const auto& endpoint : endpoints) {
        sent = sendTo(endpoint, channel, filename, bytes) || sent;
    }
    if (!sent && endpoints.empty()) impl_->error = "no recipients for audio";
    return sent;
}

const std::string& AudioNet::lastError() const noexcept {
    return impl_->error;
}

} // namespace audionet
