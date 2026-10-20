#include "ChatNet.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <random>
#include <unordered_map>
#include <utility>

namespace chatnet {
namespace {

constexpr std::uint32_t kMagic = 0x54484347; // GCHT
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kHello = 1;
constexpr std::uint8_t kChat = 2;
constexpr std::chrono::milliseconds kHelloInterval{1500};
constexpr std::chrono::milliseconds kPeerTimeout{6500};

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic{};
    std::uint8_t version{};
    std::uint8_t type{};
    std::uint16_t reserved{};
    std::uint32_t senderId{};
    char name[32]{};
    char text[384]{};
};
#pragma pack(pop)

std::string trimField(const char* data, std::size_t capacity)
{
    const auto* end = static_cast<const char*>(std::memchr(data, '\0', capacity));
    return std::string(data, end ? static_cast<std::size_t>(end - data) : capacity);
}

std::uint32_t makeId()
{
    std::uint32_t seed = static_cast<std::uint32_t>(GetTickCount64());
    seed ^= static_cast<std::uint32_t>(GetCurrentProcessId() * 2654435761u);
    try {
        std::random_device rd;
        seed ^= rd();
    } catch (...) {
    }
    std::mt19937 rng(seed);
    std::uint32_t id = rng();
    return id == 0 ? 1u : id;
}

std::string socketError(const char* what)
{
    return std::string(what) + " (WSA " + std::to_string(WSAGetLastError()) + ")";
}

} // namespace

struct ChatNet::Impl {
    struct PeerState {
        Peer peer;
        std::chrono::steady_clock::time_point lastSeen{};
    };

    SOCKET socket{INVALID_SOCKET};
    bool winsockStarted{false};
    std::uint16_t listenPort{0};
    std::uint32_t localId{0};
    std::string name;
    std::string error;
    sockaddr_in broadcast{};
    std::unordered_map<std::uint32_t, PeerState> peerMap;
    std::chrono::steady_clock::time_point nextHello{};

    bool sendPacket(std::uint8_t type, const std::string& text)
    {
        if (socket == INVALID_SOCKET) return false;

        Packet packet{};
        packet.magic = kMagic;
        packet.version = kVersion;
        packet.type = type;
        packet.senderId = localId;
        std::strncpy(packet.name, name.c_str(), sizeof(packet.name) - 1);
        std::strncpy(packet.text, text.c_str(), sizeof(packet.text) - 1);

        const int sent = sendto(
            socket,
            reinterpret_cast<const char*>(&packet),
            static_cast<int>(sizeof(packet)),
            0,
            reinterpret_cast<const sockaddr*>(&broadcast),
            sizeof(broadcast));

        if (sent == SOCKET_ERROR) {
            error = socketError("sendto failed");
            return false;
        }
        return sent == static_cast<int>(sizeof(packet));
    }
};

ChatNet::ChatNet() : impl_(std::make_unique<Impl>()) {}
ChatNet::~ChatNet() { stop(); }
ChatNet::ChatNet(ChatNet&&) noexcept = default;
ChatNet& ChatNet::operator=(ChatNet&&) noexcept = default;

bool ChatNet::start(const std::string& name, std::uint16_t port)
{
    stop();
    impl_->error.clear();

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        impl_->error = socketError("WSAStartup failed");
        return false;
    }
    impl_->winsockStarted = true;

    impl_->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket == INVALID_SOCKET) {
        impl_->error = socketError("socket failed");
        stop();
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(impl_->socket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    BOOL broadcast = TRUE;
    if (setsockopt(impl_->socket, SOL_SOCKET, SO_BROADCAST,
                   reinterpret_cast<const char*>(&broadcast), sizeof(broadcast)) != 0) {
        impl_->error = socketError("SO_BROADCAST failed");
        stop();
        return false;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(impl_->socket, FIONBIO, &nonBlocking) != 0) {
        impl_->error = socketError("FIONBIO failed");
        stop();
        return false;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (bind(impl_->socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        impl_->error = socketError("bind failed");
        stop();
        return false;
    }

    impl_->listenPort = port;
    impl_->localId = makeId();
    impl_->name = name.empty() ? "PLAYER" : name.substr(0, 31);
    impl_->peerMap.clear();

    impl_->broadcast = {};
    impl_->broadcast.sin_family = AF_INET;
    impl_->broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    impl_->broadcast.sin_port = htons(port);

    impl_->nextHello = std::chrono::steady_clock::now();
    impl_->sendPacket(kHello, {});
    return true;
}

void ChatNet::stop()
{
    if (!impl_) return;

    if (impl_->socket != INVALID_SOCKET) {
        closesocket(impl_->socket);
        impl_->socket = INVALID_SOCKET;
    }
    impl_->peerMap.clear();
    impl_->listenPort = 0;
    impl_->localId = 0;

    if (impl_->winsockStarted) {
        WSACleanup();
        impl_->winsockStarted = false;
    }
}

bool ChatNet::isRunning() const noexcept
{
    return impl_ && impl_->socket != INVALID_SOCKET;
}

std::vector<Event> ChatNet::update()
{
    std::vector<Event> events;
    if (!isRunning()) return events;

    const auto now = std::chrono::steady_clock::now();
    if (now >= impl_->nextHello) {
        impl_->sendPacket(kHello, {});
        impl_->nextHello = now + kHelloInterval;
    }

    for (;;) {
        Packet packet{};
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int received = recvfrom(
            impl_->socket,
            reinterpret_cast<char*>(&packet),
            static_cast<int>(sizeof(packet)),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromLen);

        if (received == SOCKET_ERROR) {
            const int code = WSAGetLastError();
            if (code == WSAEWOULDBLOCK) break;
            impl_->error = "recvfrom failed (WSA " + std::to_string(code) + ")";
            break;
        }
        if (received < static_cast<int>(offsetof(Packet, text))) continue;
        if (packet.magic != kMagic || packet.version != kVersion || packet.senderId == impl_->localId) continue;

        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &from.sin_addr, ip, static_cast<DWORD>(sizeof(ip)));

        const std::string peerName = trimField(packet.name, sizeof(packet.name));
        auto it = impl_->peerMap.find(packet.senderId);
        if (it == impl_->peerMap.end()) {
            Peer peer;
            peer.id = packet.senderId;
            peer.name = peerName.empty() ? "PLAYER" : peerName;
            peer.address = ip;
            peer.port = ntohs(from.sin_port);
            it = impl_->peerMap.emplace(packet.senderId, Impl::PeerState{peer, now}).first;
            events.push_back(Event{EventType::PeerJoined, peer.id, peer.name, {}});
        } else {
            it->second.lastSeen = now;
            if (!peerName.empty()) it->second.peer.name = peerName;
            it->second.peer.address = ip;
            it->second.peer.port = ntohs(from.sin_port);
        }

        if (packet.type == kChat) {
            const std::string text = trimField(packet.text, sizeof(packet.text));
            if (!text.empty()) {
                events.push_back(Event{EventType::Message, packet.senderId, it->second.peer.name, text});
            }
        }
    }

    for (auto it = impl_->peerMap.begin(); it != impl_->peerMap.end();) {
        if (now - it->second.lastSeen > kPeerTimeout) {
            events.push_back(Event{EventType::PeerLeft, it->second.peer.id, it->second.peer.name, {}});
            it = impl_->peerMap.erase(it);
        } else {
            ++it;
        }
    }

    return events;
}

bool ChatNet::sendChat(const std::string& text)
{
    if (!isRunning() || text.empty()) return false;
    return impl_->sendPacket(kChat, text.substr(0, sizeof(Packet::text) - 1));
}

const std::string& ChatNet::localName() const noexcept
{
    return impl_->name;
}

std::uint16_t ChatNet::port() const noexcept
{
    return impl_->listenPort;
}

std::vector<Peer> ChatNet::peers() const
{
    std::vector<Peer> result;
    result.reserve(impl_->peerMap.size());
    for (const auto& entry : impl_->peerMap) result.push_back(entry.second.peer);
    std::sort(result.begin(), result.end(), [](const Peer& a, const Peer& b) {
        return a.name < b.name;
    });
    return result;
}

const std::string& ChatNet::lastError() const noexcept
{
    return impl_->error;
}

} // namespace chatnet
