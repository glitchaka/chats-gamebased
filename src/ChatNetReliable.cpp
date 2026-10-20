#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::uint32_t kChatMagic = 0x54484347; // GCHT
constexpr std::uint8_t kChatVersion = 4;
constexpr std::uint8_t kStickerChunkType = 3;
constexpr std::uint32_t kAckMagic = 0x4B434147; // GACK
constexpr std::uint8_t kAckVersion = 1;
constexpr std::size_t kTransferIdOffset = 12;
constexpr std::size_t kChunkIndexOffset = 16;
constexpr std::size_t kFilenameOffset = 90;
constexpr std::size_t kFilenameBytes = 64;
constexpr std::size_t kStickerHeaderBytes = 154;
constexpr std::size_t kInitialBurst = 80;
constexpr std::size_t kRetryBurst = 32;
constexpr auto kRetryDelay = std::chrono::milliseconds(240);
constexpr auto kPendingLifetime = std::chrono::seconds(24);
constexpr auto kAudioDuplicateWindow = std::chrono::seconds(12);
constexpr unsigned kMaxAttempts = 18;

#pragma pack(push, 1)
struct AckPacket {
    std::uint32_t magic{kAckMagic};
    std::uint8_t version{kAckVersion};
    std::uint8_t reserved0{};
    std::uint16_t reserved1{};
    std::uint32_t transferId{};
    std::uint16_t chunkIndex{};
    std::uint16_t reserved2{};
};
#pragma pack(pop)

static_assert(sizeof(AckPacket) == 16, "unexpected ACK packet size");

struct StickerHeader {
    std::uint32_t transferId{};
    std::uint16_t chunkIndex{};
    std::string filename;
};

struct PendingChunk {
    SOCKET socket{INVALID_SOCKET};
    sockaddr_in destination{};
    int flags{};
    std::vector<char> packet;
    std::chrono::steady_clock::time_point created{};
    std::chrono::steady_clock::time_point lastSent{};
    unsigned attempts{};
};

struct AudioPrimary {
    std::uint32_t transferId{};
    std::chrono::steady_clock::time_point seen{};
};

std::unordered_map<std::string, PendingChunk> gPending;
std::unordered_map<std::string, AudioPrimary> gAudioPrimary;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> gSuppressedAudioTransfers;
std::chrono::steady_clock::time_point gLastPump{};

std::uint32_t readU32Host(const char* data, std::size_t offset)
{
    std::uint32_t value{};
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

std::uint16_t readU16Host(const char* data, std::size_t offset)
{
    std::uint16_t value{};
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

bool parseStickerChunk(const char* data, int len, StickerHeader& header)
{
    if (!data || len < static_cast<int>(kStickerHeaderBytes)) return false;
    if (readU32Host(data, 0) != kChatMagic) return false;
    if (static_cast<std::uint8_t>(data[4]) != kChatVersion ||
        static_cast<std::uint8_t>(data[5]) != kStickerChunkType) return false;

    header.transferId = readU32Host(data, kTransferIdOffset);
    header.chunkIndex = readU16Host(data, kChunkIndexOffset);

    const char* filename = data + kFilenameOffset;
    std::size_t count = 0;
    while (count < kFilenameBytes && filename[count] != '\0') ++count;
    header.filename.assign(filename, count);
    return true;
}

bool parseAck(const char* data, int len, AckPacket& ack)
{
    if (!data || len != static_cast<int>(sizeof(AckPacket))) return false;
    std::memcpy(&ack, data, sizeof(ack));
    return ack.magic == kAckMagic && ack.version == kAckVersion;
}

std::string endpointKey(const sockaddr_in& address)
{
    return std::to_string(static_cast<unsigned long>(address.sin_addr.s_addr)) + ":" +
           std::to_string(static_cast<unsigned>(address.sin_port));
}

std::string pendingKey(SOCKET socket,
                       const sockaddr_in& destination,
                       std::uint32_t transferId,
                       std::uint16_t chunkIndex)
{
    return std::to_string(static_cast<unsigned long long>(socket)) + "|" + endpointKey(destination) + "|" +
           std::to_string(transferId) + "|" + std::to_string(chunkIndex);
}

std::string transferKey(SOCKET socket,
                        const sockaddr_in& destination,
                        std::uint32_t transferId)
{
    return std::to_string(static_cast<unsigned long long>(socket)) + "|" + endpointKey(destination) + "|" +
           std::to_string(transferId);
}

std::string audioKey(SOCKET socket,
                     const sockaddr_in& destination,
                     const std::string& filename)
{
    return std::to_string(static_cast<unsigned long long>(socket)) + "|" + endpointKey(destination) + "|" + filename;
}

void pruneAudioTracking(std::chrono::steady_clock::time_point now)
{
    for (auto it = gAudioPrimary.begin(); it != gAudioPrimary.end();) {
        if (now - it->second.seen > kAudioDuplicateWindow) it = gAudioPrimary.erase(it);
        else ++it;
    }
    for (auto it = gSuppressedAudioTransfers.begin(); it != gSuppressedAudioTransfers.end();) {
        if (now - it->second > kAudioDuplicateWindow) it = gSuppressedAudioTransfers.erase(it);
        else ++it;
    }
}

bool suppressDuplicateAudioTransfer(SOCKET socket,
                                    const sockaddr_in& destination,
                                    const StickerHeader& header,
                                    std::chrono::steady_clock::time_point now)
{
    if (header.filename.rfind("AUD-", 0) != 0) return false;

    pruneAudioTracking(now);
    const std::string xferKey = transferKey(socket, destination, header.transferId);
    if (gSuppressedAudioTransfers.find(xferKey) != gSuppressedAudioTransfers.end()) return true;

    const std::string key = audioKey(socket, destination, header.filename);
    auto primary = gAudioPrimary.find(key);
    if (header.chunkIndex == 0) {
        if (primary == gAudioPrimary.end()) {
            gAudioPrimary.emplace(key, AudioPrimary{header.transferId, now});
            return false;
        }
        primary->second.seen = now;
        if (primary->second.transferId != header.transferId) {
            gSuppressedAudioTransfers[xferKey] = now;
            return true;
        }
    } else if (primary != gAudioPrimary.end() && primary->second.transferId != header.transferId) {
        gSuppressedAudioTransfers[xferKey] = now;
        return true;
    }
    return false;
}

void queueStickerChunk(SOCKET socket,
                       const sockaddr_in& destination,
                       const char* data,
                       int len,
                       int flags,
                       const StickerHeader& header,
                       std::chrono::steady_clock::time_point now)
{
    const std::string key = pendingKey(socket, destination, header.transferId, header.chunkIndex);
    PendingChunk pending;
    pending.socket = socket;
    pending.destination = destination;
    pending.flags = flags;
    pending.packet.assign(data, data + len);
    pending.created = now;
    gPending[key] = std::move(pending);
}

bool rawSendPending(PendingChunk& pending, std::chrono::steady_clock::time_point now)
{
    if (pending.packet.empty()) return false;
    const int sent = ::sendto(pending.socket,
                              pending.packet.data(),
                              static_cast<int>(pending.packet.size()),
                              pending.flags,
                              reinterpret_cast<const sockaddr*>(&pending.destination),
                              sizeof(pending.destination));
    pending.lastSent = now;
    ++pending.attempts;
    return sent == static_cast<int>(pending.packet.size());
}

void pumpPending(SOCKET socket)
{
    const auto now = std::chrono::steady_clock::now();
    if (gLastPump.time_since_epoch().count() != 0 && now - gLastPump < std::chrono::milliseconds(15)) return;
    gLastPump = now;

    for (auto it = gPending.begin(); it != gPending.end();) {
        const bool expired = now - it->second.created > kPendingLifetime || it->second.attempts >= kMaxAttempts;
        if (expired) it = gPending.erase(it);
        else ++it;
    }

    std::size_t initialBudget = kInitialBurst;
    for (auto& entry : gPending) {
        auto& pending = entry.second;
        if (initialBudget == 0) break;
        if (pending.socket != socket || pending.attempts != 0) continue;
        rawSendPending(pending, now);
        --initialBudget;
    }

    std::size_t retryBudget = kRetryBurst;
    for (auto& entry : gPending) {
        auto& pending = entry.second;
        if (retryBudget == 0) break;
        if (pending.socket != socket || pending.attempts == 0) continue;
        if (now - pending.lastSent < kRetryDelay) continue;
        rawSendPending(pending, now);
        --retryBudget;
    }
}

void acknowledgeChunk(SOCKET socket,
                      const sockaddr_in& source,
                      std::uint32_t transferId,
                      std::uint16_t chunkIndex)
{
    gPending.erase(pendingKey(socket, source, transferId, chunkIndex));
}

void sendAck(SOCKET socket,
             const sockaddr_in& destination,
             std::uint32_t transferId,
             std::uint16_t chunkIndex)
{
    AckPacket ack;
    ack.transferId = transferId;
    ack.chunkIndex = chunkIndex;
    ::sendto(socket,
             reinterpret_cast<const char*>(&ack),
             static_cast<int>(sizeof(ack)),
             0,
             reinterpret_cast<const sockaddr*>(&destination),
             sizeof(destination));
}

int WSAAPI gbc_sendto(SOCKET socket,
                      const char* data,
                      int len,
                      int flags,
                      const sockaddr* destination,
                      int destinationLen)
{
    StickerHeader header;
    if (!destination || destination->sa_family != AF_INET ||
        destinationLen < static_cast<int>(sizeof(sockaddr_in)) ||
        !parseStickerChunk(data, len, header)) {
        return ::sendto(socket, data, len, flags, destination, destinationLen);
    }

    const auto& ipv4 = *reinterpret_cast<const sockaddr_in*>(destination);
    const auto now = std::chrono::steady_clock::now();
    if (suppressDuplicateAudioTransfer(socket, ipv4, header, now)) return len;

    queueStickerChunk(socket, ipv4, data, len, flags, header, now);
    return len;
}

int WSAAPI gbc_recvfrom(SOCKET socket,
                        char* buffer,
                        int len,
                        int flags,
                        sockaddr* source,
                        int* sourceLen)
{
    pumpPending(socket);

    for (;;) {
        const int received = ::recvfrom(socket, buffer, len, flags, source, sourceLen);
        if (received == SOCKET_ERROR) return received;

        sockaddr_in ipv4{};
        const bool hasIPv4Source = source && sourceLen && *sourceLen >= static_cast<int>(sizeof(sockaddr_in)) &&
                                   source->sa_family == AF_INET;
        if (hasIPv4Source) ipv4 = *reinterpret_cast<const sockaddr_in*>(source);

        AckPacket ack;
        if (hasIPv4Source && parseAck(buffer, received, ack)) {
            acknowledgeChunk(socket, ipv4, ack.transferId, ack.chunkIndex);
            continue;
        }

        StickerHeader header;
        if (hasIPv4Source && parseStickerChunk(buffer, received, header)) {
            sendAck(socket, ipv4, header.transferId, header.chunkIndex);
        }
        return received;
    }
}

} // namespace

#define sendto gbc_sendto
#define recvfrom gbc_recvfrom
#include "ChatNet.cpp"
#undef recvfrom
#undef sendto
