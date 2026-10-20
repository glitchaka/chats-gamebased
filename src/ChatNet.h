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
    StickerImage
};

struct Event {
    EventType type{};
    std::uint32_t peerId{};
    std::string name;
    std::string text;
    std::string channel;
    std::string endpoint;

    // Filled only for StickerImage events. The image is transported by the
    // chat itself; the receiver does not need to own the same sticker file.
    std::string stickerFilename;
    std::vector<std::uint8_t> stickerData;
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

    bool addFriendAddress(const std::string& endpoint);

    bool sendChat(const std::string& text);
    bool sendChatTo(const std::string& endpoint, const std::string& text);
    bool sendGroup(const std::string& channel,
                   const std::vector<std::string>& endpoints,
                   const std::string& text);

    // Sticker images are user-selected files. They are fragmented into UDP
    // packets and reconstructed by the receiver. No built-in sticker IDs or
    // shared sticker pack are required.
    bool sendStickerFile(const std::string& filename,
                         const std::vector<std::uint8_t>& bytes);
    bool sendStickerFileTo(const std::string& endpoint,
                           const std::string& filename,
                           const std::vector<std::uint8_t>& bytes);
    bool sendGroupStickerFile(const std::string& channel,
                              const std::vector<std::string>& endpoints,
                              const std::string& filename,
                              const std::vector<std::uint8_t>& bytes);

    const std::string& localName() const noexcept;
    std::uint16_t port() const noexcept;
    std::vector<Peer> peers() const;
    std::vector<std::string> savedAddresses() const;

    const std::string& publicEndpoint() const noexcept;
    const std::string& localEndpoint() const noexcept;
    const std::string& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chatnet
