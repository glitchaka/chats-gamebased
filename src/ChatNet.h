#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chatnet {

struct Peer {
    std::uint32_t id{};
    std::string name;
    std::string address;
    std::uint16_t port{};

    std::string endpoint() const;
};

enum class EventType {
    PeerJoined,
    PeerLeft,
    Message,
    Sticker
};

struct Event {
    EventType type{};
    std::uint32_t peerId{};
    std::string name;
    std::string text;
    std::uint8_t stickerId{};
    std::string channel;
    std::string endpoint;
};

class ChatNet {
public:
    ChatNet();
    ~ChatNet();

    ChatNet(const ChatNet&) = delete;
    ChatNet& operator=(const ChatNet&) = delete;
    ChatNet(ChatNet&&) noexcept;
    ChatNet& operator=(ChatNet&&) noexcept;

    bool start(const std::string& name, std::uint16_t port = 34197);
    void stop();
    bool isRunning() const noexcept;

    std::vector<Event> update();

    // Adds a direct Internet/LAN target such as "203.0.113.7:34197" or "host.example:34197".
    // The target is pinged immediately and then kept alive with HELLO packets.
    bool addFriendAddress(const std::string& endpoint);

    // Sends to all currently known/saved targets. LAN broadcast is also kept as a discovery path.
    bool sendChat(const std::string& text);
    bool sendSticker(std::uint8_t stickerId);

    // Direct one-to-one delivery.
    bool sendChatTo(const std::string& endpoint, const std::string& text);
    bool sendStickerTo(const std::string& endpoint, std::uint8_t stickerId);

    // Group delivery is fan-out to the supplied friend endpoints, with a channel name in the packet.
    bool sendGroup(const std::string& channel,
                   const std::vector<std::string>& endpoints,
                   const std::string& text);
    bool sendGroupSticker(const std::string& channel,
                          const std::vector<std::string>& endpoints,
                          std::uint8_t stickerId);

    const std::string& localName() const noexcept;
    std::uint16_t port() const noexcept;
    std::vector<Peer> peers() const;
    std::vector<std::string> savedAddresses() const;

    // publicEndpoint() is discovered with STUN from the same UDP socket.
    // It can be copied/shared as a friend address. Some restrictive NATs can still require a relay.
    const std::string& publicEndpoint() const noexcept;
    const std::string& localEndpoint() const noexcept;
    const std::string& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chatnet
