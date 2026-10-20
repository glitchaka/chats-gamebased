#include "ChatNet.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chatnet {
namespace {

constexpr std::uint32_t kMagic = 0x54484347; // GCHT
constexpr std::uint8_t kVersion = 3;
constexpr std::uint8_t kHello = 1;
constexpr std::uint8_t kChat = 2;
constexpr std::uint8_t kSticker = 3;
constexpr std::chrono::milliseconds kHelloInterval{1500};
constexpr std::chrono::milliseconds kPeerTimeout{9000};
constexpr std::chrono::seconds kStunRetry{5};
constexpr std::uint32_t kStunCookie = 0x2112A442u;
constexpr const char* kStunHost = "stun.cloudflare.com";
constexpr std::uint16_t kStunPort = 3478;

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic{};
    std::uint8_t version{};
    std::uint8_t type{};
    std::uint8_t stickerId{};
    std::uint8_t reserved{};
    std::uint32_t senderId{};
    char name[32]{};
    char channel[32]{};
    char text[384]{};
};
#pragma pack(pop)

struct Target {
    std::string source;
    sockaddr_in address{};
    std::chrono::steady_clock::time_point nextHello{};
};

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

std::string endpointFromSockaddr(const sockaddr_in& address)
{
    char ip[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &address.sin_addr, ip, static_cast<DWORD>(sizeof(ip)))) return {};
    return std::string(ip) + ":" + std::to_string(ntohs(address.sin_port));
}

bool parseEndpoint(const std::string& endpoint,
                   std::uint16_t defaultPort,
                   sockaddr_in& output,
                   std::string& normalized,
                   std::string& error)
{
    if (endpoint.empty()) {
        error = "empty endpoint";
        return false;
    }

    std::string host = endpoint;
    std::string portText = std::to_string(defaultPort);

    const std::size_t colon = endpoint.rfind(':');
    if (colon != std::string::npos && colon > 0 && colon + 1 < endpoint.size()) {
        host = endpoint.substr(0, colon);
        portText = endpoint.substr(colon + 1);
    }

    char* end = nullptr;
    const long parsedPort = std::strtol(portText.c_str(), &end, 10);
    if (!end || *end != '\0' || parsedPort <= 0 || parsedPort > 65535) {
        error = "invalid endpoint port: " + endpoint;
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* results = nullptr;
    const int rc = getaddrinfo(host.c_str(), portText.c_str(), &hints, &results);
    if (rc != 0 || !results) {
        error = "cannot resolve endpoint: " + endpoint;
        if (results) freeaddrinfo(results);
        return false;
    }

    output = *reinterpret_cast<sockaddr_in*>(results->ai_addr);
    normalized = endpointFromSockaddr(output);
    freeaddrinfo(results);
    return !normalized.empty();
}

std::string firstLocalIPv4(std::uint16_t port)
{
    char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname)) != 0) return "0.0.0.0:" + std::to_string(port);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* results = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &results) != 0 || !results) {
        if (results) freeaddrinfo(results);
        return "0.0.0.0:" + std::to_string(port);
    }

    std::string fallback;
    for (addrinfo* it = results; it; it = it->ai_next) {
        const auto* address = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
        char ip[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &address->sin_addr, ip, static_cast<DWORD>(sizeof(ip)))) continue;
        const std::string value = ip;
        if (fallback.empty()) fallback = value;
        if (value.rfind("127.", 0) != 0) {
            freeaddrinfo(results);
            return value + ":" + std::to_string(port);
        }
    }

    freeaddrinfo(results);
    if (fallback.empty()) fallback = "0.0.0.0";
    return fallback + ":" + std::to_string(port);
}

std::uint16_t readU16(const unsigned char* bytes)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) |
                                      static_cast<std::uint16_t>(bytes[1]));
}

std::uint32_t readU32(const unsigned char* bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

void writeU16(unsigned char* bytes, std::uint16_t value)
{
    bytes[0] = static_cast<unsigned char>((value >> 8) & 0xff);
    bytes[1] = static_cast<unsigned char>(value & 0xff);
}

void writeU32(unsigned char* bytes, std::uint32_t value)
{
    bytes[0] = static_cast<unsigned char>((value >> 24) & 0xff);
    bytes[1] = static_cast<unsigned char>((value >> 16) & 0xff);
    bytes[2] = static_cast<unsigned char>((value >> 8) & 0xff);
    bytes[3] = static_cast<unsigned char>(value & 0xff);
}

} // namespace

std::string Peer::endpoint() const
{
    return address + ":" + std::to_string(port);
}

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
    std::vector<Target> targets;
    std::chrono::steady_clock::time_point nextBroadcastHello{};

    sockaddr_in stunServer{};
    bool stunServerReady{false};
    std::array<unsigned char, 12> stunTransaction{};
    std::chrono::steady_clock::time_point nextStun{};
    std::string publicAddress;
    std::string localAddress;

    bool sendRaw(const sockaddr_in& destination, const void* data, std::size_t size)
    {
        if (socket == INVALID_SOCKET || !data || size == 0) return false;
        const int sent = sendto(socket,
                                static_cast<const char*>(data),
                                static_cast<int>(size),
                                0,
                                reinterpret_cast<const sockaddr*>(&destination),
                                sizeof(destination));
        if (sent == SOCKET_ERROR) {
            error = socketError("sendto failed");
            return false;
        }
        return sent == static_cast<int>(size);
    }

    bool sendPacketTo(const sockaddr_in& destination,
                      std::uint8_t type,
                      const std::string& text,
                      std::uint8_t stickerId,
                      const std::string& channel)
    {
        Packet packet{};
        packet.magic = kMagic;
        packet.version = kVersion;
        packet.type = type;
        packet.stickerId = stickerId;
        packet.senderId = localId;
        std::strncpy(packet.name, name.c_str(), sizeof(packet.name) - 1);
        std::strncpy(packet.channel, channel.c_str(), sizeof(packet.channel) - 1);
        std::strncpy(packet.text, text.c_str(), sizeof(packet.text) - 1);
        return sendRaw(destination, &packet, sizeof(packet));
    }

    bool resolveStunServer()
    {
        if (stunServerReady) return true;

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        addrinfo* results = nullptr;
        const std::string portText = std::to_string(kStunPort);
        if (getaddrinfo(kStunHost, portText.c_str(), &hints, &results) != 0 || !results) {
            if (results) freeaddrinfo(results);
            return false;
        }

        stunServer = *reinterpret_cast<sockaddr_in*>(results->ai_addr);
        freeaddrinfo(results);
        stunServerReady = true;
        return true;
    }

    void requestStun()
    {
        if (socket == INVALID_SOCKET || !resolveStunServer()) return;

        std::array<unsigned char, 20> request{};
        writeU16(request.data(), 0x0001); // Binding Request
        writeU16(request.data() + 2, 0);
        writeU32(request.data() + 4, kStunCookie);

        std::uint32_t seed = makeId();
        std::mt19937 rng(seed);
        for (unsigned char& byte : stunTransaction) byte = static_cast<unsigned char>(rng() & 0xff);
        std::memcpy(request.data() + 8, stunTransaction.data(), stunTransaction.size());

        sendRaw(stunServer, request.data(), request.size());
        nextStun = std::chrono::steady_clock::now() + kStunRetry;
    }

    bool parseStun(const unsigned char* data, std::size_t size)
    {
        if (size < 20 || readU16(data) != 0x0101 || readU32(data + 4) != kStunCookie) return false;
        if (std::memcmp(data + 8, stunTransaction.data(), stunTransaction.size()) != 0) return false;

        const std::size_t messageLength = readU16(data + 2);
        const std::size_t end = std::min(size, 20u + messageLength);
        std::size_t offset = 20;

        while (offset + 4 <= end) {
            const std::uint16_t type = readU16(data + offset);
            const std::uint16_t length = readU16(data + offset + 2);
            const std::size_t value = offset + 4;
            if (value + length > end) break;

            if ((type == 0x0020 || type == 0x0001) && length >= 8 && data[value + 1] == 0x01) {
                std::uint16_t port = readU16(data + value + 2);
                std::uint32_t address = readU32(data + value + 4);
                if (type == 0x0020) {
                    port ^= static_cast<std::uint16_t>(kStunCookie >> 16);
                    address ^= kStunCookie;
                }

                in_addr ipv4{};
                ipv4.s_addr = htonl(address);
                char ip[INET_ADDRSTRLEN]{};
                if (inet_ntop(AF_INET, &ipv4, ip, static_cast<DWORD>(sizeof(ip)))) {
                    publicAddress = std::string(ip) + ":" + std::to_string(port);
                    return true;
                }
            }

            offset = value + length;
            while ((offset & 3u) != 0u) ++offset;
        }
        return true;
    }

    bool addTarget(const std::string& endpoint)
    {
        sockaddr_in address{};
        std::string normalized;
        std::string parseError;
        if (!parseEndpoint(endpoint, listenPort, address, normalized, parseError)) {
            error = parseError;
            return false;
        }

        const auto existing = std::find_if(targets.begin(), targets.end(), [&](const Target& target) {
            return target.source == normalized;
        });
        if (existing != targets.end()) {
            existing->nextHello = std::chrono::steady_clock::now();
            sendPacketTo(existing->address, kHello, {}, 0, {});
            return true;
        }

        Target target;
        target.source = normalized;
        target.address = address;
        target.nextHello = std::chrono::steady_clock::now();
        targets.push_back(target);
        sendPacketTo(address, kHello, {}, 0, {});
        return true;
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
    setsockopt(impl_->socket, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

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
    impl_->targets.clear();

    impl_->broadcast = {};
    impl_->broadcast.sin_family = AF_INET;
    impl_->broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    impl_->broadcast.sin_port = htons(port);

    impl_->localAddress = firstLocalIPv4(port);
    impl_->publicAddress.clear();
    impl_->nextBroadcastHello = std::chrono::steady_clock::now();
    impl_->nextStun = std::chrono::steady_clock::now();

    impl_->sendPacketTo(impl_->broadcast, kHello, {}, 0, {});
    impl_->requestStun();
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
    impl_->targets.clear();
    impl_->listenPort = 0;
    impl_->localId = 0;
    impl_->publicAddress.clear();
    impl_->localAddress.clear();
    impl_->stunServerReady = false;

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

    if (now >= impl_->nextBroadcastHello) {
        impl_->sendPacketTo(impl_->broadcast, kHello, {}, 0, {});
        impl_->nextBroadcastHello = now + kHelloInterval;
    }

    for (Target& target : impl_->targets) {
        if (now >= target.nextHello) {
            impl_->sendPacketTo(target.address, kHello, {}, 0, {});
            target.nextHello = now + kHelloInterval;
        }
    }

    if (impl_->publicAddress.empty() && now >= impl_->nextStun) impl_->requestStun();

    for (;;) {
        std::array<unsigned char, 2048> buffer{};
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int received = recvfrom(
            impl_->socket,
            reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromLen);

        if (received == SOCKET_ERROR) {
            const int code = WSAGetLastError();
            if (code == WSAEWOULDBLOCK) break;
            impl_->error = "recvfrom failed (WSA " + std::to_string(code) + ")";
            break;
        }

        const std::size_t size = static_cast<std::size_t>(received);
        if (impl_->parseStun(buffer.data(), size)) continue;
        if (size < sizeof(Packet)) continue;

        Packet packet{};
        std::memcpy(&packet, buffer.data(), sizeof(packet));
        if (packet.magic != kMagic || packet.version != kVersion || packet.senderId == impl_->localId) continue;

        const std::string sourceEndpoint = endpointFromSockaddr(from);
        const std::string peerName = trimField(packet.name, sizeof(packet.name));
        const std::string channel = trimField(packet.channel, sizeof(packet.channel));

        auto it = impl_->peerMap.find(packet.senderId);
        const bool isNew = it == impl_->peerMap.end();
        if (isNew) {
            Peer peer;
            peer.id = packet.senderId;
            peer.name = peerName.empty() ? "PLAYER" : peerName;
            peer.address = sourceEndpoint.substr(0, sourceEndpoint.find(':'));
            peer.port = ntohs(from.sin_port);
            it = impl_->peerMap.emplace(packet.senderId, Impl::PeerState{peer, now}).first;
            events.push_back(Event{EventType::PeerJoined, peer.id, peer.name, {}, 0, {}, sourceEndpoint});
        } else {
            it->second.lastSeen = now;
            if (!peerName.empty()) it->second.peer.name = peerName;
            it->second.peer.address = sourceEndpoint.substr(0, sourceEndpoint.find(':'));
            it->second.peer.port = ntohs(from.sin_port);
        }

        if (isNew || packet.type == kHello) {
            impl_->sendPacketTo(from, kHello, {}, 0, {});
        }

        if (packet.type == kChat) {
            const std::string text = trimField(packet.text, sizeof(packet.text));
            if (!text.empty()) {
                events.push_back(Event{EventType::Message, packet.senderId, it->second.peer.name,
                                       text, 0, channel, sourceEndpoint});
            }
        } else if (packet.type == kSticker && packet.stickerId >= 1 && packet.stickerId <= 4) {
            events.push_back(Event{EventType::Sticker, packet.senderId, it->second.peer.name,
                                   {}, packet.stickerId, channel, sourceEndpoint});
        }
    }

    for (auto it = impl_->peerMap.begin(); it != impl_->peerMap.end();) {
        if (now - it->second.lastSeen > kPeerTimeout) {
            const std::string endpoint = it->second.peer.endpoint();
            events.push_back(Event{EventType::PeerLeft, it->second.peer.id, it->second.peer.name,
                                   {}, 0, {}, endpoint});
            it = impl_->peerMap.erase(it);
        } else {
            ++it;
        }
    }

    return events;
}

bool ChatNet::addFriendAddress(const std::string& endpoint)
{
    if (!isRunning()) return false;
    return impl_->addTarget(endpoint);
}

bool ChatNet::sendChat(const std::string& text)
{
    if (!isRunning() || text.empty()) return false;
    bool sent = impl_->sendPacketTo(impl_->broadcast, kChat,
                                    text.substr(0, sizeof(Packet::text) - 1), 0, {});
    for (const Target& target : impl_->targets) {
        sent = impl_->sendPacketTo(target.address, kChat,
                                   text.substr(0, sizeof(Packet::text) - 1), 0, {}) || sent;
    }
    return sent;
}

bool ChatNet::sendSticker(std::uint8_t stickerId)
{
    if (!isRunning() || stickerId < 1 || stickerId > 4) return false;
    bool sent = impl_->sendPacketTo(impl_->broadcast, kSticker, {}, stickerId, {});
    for (const Target& target : impl_->targets) {
        sent = impl_->sendPacketTo(target.address, kSticker, {}, stickerId, {}) || sent;
    }
    return sent;
}

bool ChatNet::sendChatTo(const std::string& endpoint, const std::string& text)
{
    if (!isRunning() || text.empty()) return false;
    sockaddr_in address{};
    std::string normalized;
    std::string parseError;
    if (!parseEndpoint(endpoint, impl_->listenPort, address, normalized, parseError)) {
        impl_->error = parseError;
        return false;
    }
    return impl_->sendPacketTo(address, kChat, text.substr(0, sizeof(Packet::text) - 1), 0, {});
}

bool ChatNet::sendStickerTo(const std::string& endpoint, std::uint8_t stickerId)
{
    if (!isRunning() || stickerId < 1 || stickerId > 4) return false;
    sockaddr_in address{};
    std::string normalized;
    std::string parseError;
    if (!parseEndpoint(endpoint, impl_->listenPort, address, normalized, parseError)) {
        impl_->error = parseError;
        return false;
    }
    return impl_->sendPacketTo(address, kSticker, {}, stickerId, {});
}

bool ChatNet::sendGroup(const std::string& channel,
                        const std::vector<std::string>& endpoints,
                        const std::string& text)
{
    if (!isRunning() || channel.empty() || endpoints.empty() || text.empty()) return false;
    bool sent = false;
    for (const std::string& endpoint : endpoints) {
        sockaddr_in address{};
        std::string normalized;
        std::string parseError;
        if (!parseEndpoint(endpoint, impl_->listenPort, address, normalized, parseError)) continue;
        sent = impl_->sendPacketTo(address, kChat,
                                   text.substr(0, sizeof(Packet::text) - 1), 0,
                                   channel.substr(0, sizeof(Packet::channel) - 1)) || sent;
    }
    return sent;
}

bool ChatNet::sendGroupSticker(const std::string& channel,
                               const std::vector<std::string>& endpoints,
                               std::uint8_t stickerId)
{
    if (!isRunning() || channel.empty() || endpoints.empty() || stickerId < 1 || stickerId > 4) return false;
    bool sent = false;
    for (const std::string& endpoint : endpoints) {
        sockaddr_in address{};
        std::string normalized;
        std::string parseError;
        if (!parseEndpoint(endpoint, impl_->listenPort, address, normalized, parseError)) continue;
        sent = impl_->sendPacketTo(address, kSticker, {}, stickerId,
                                   channel.substr(0, sizeof(Packet::channel) - 1)) || sent;
    }
    return sent;
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

std::vector<std::string> ChatNet::savedAddresses() const
{
    std::vector<std::string> result;
    result.reserve(impl_->targets.size());
    for (const Target& target : impl_->targets) result.push_back(target.source);
    return result;
}

const std::string& ChatNet::publicEndpoint() const noexcept
{
    return impl_->publicAddress;
}

const std::string& ChatNet::localEndpoint() const noexcept
{
    return impl_->localAddress;
}

const std::string& ChatNet::lastError() const noexcept
{
    return impl_->error;
}

} // namespace chatnet
