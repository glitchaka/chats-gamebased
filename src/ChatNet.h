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
    bool sendChat(const std::string& text);
    bool sendSticker(std::uint8_t stickerId);

    const std::string& localName() const noexcept;
    std::uint16_t port() const noexcept;
    std::vector<Peer> peers() const;
    const std::string& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chatnet
