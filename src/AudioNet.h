#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace audionet {

struct Event {
    std::string name;
    std::string channel;
    std::string endpoint;
    std::string filename;
    std::vector<std::uint8_t> data;
};

class AudioNet {
public:
    AudioNet();
    ~AudioNet();

    AudioNet(const AudioNet&) = delete;
    AudioNet& operator=(const AudioNet&) = delete;
    AudioNet(AudioNet&&) noexcept;
    AudioNet& operator=(AudioNet&&) noexcept;

    bool start(const std::string& name, std::uint16_t port = 34197);
    void stop();
    bool isRunning() const noexcept;

    std::vector<Event> update();

    bool sendTo(const std::string& endpoint,
                const std::string& channel,
                const std::string& filename,
                const std::vector<std::uint8_t>& bytes);
    bool sendMany(const std::vector<std::string>& endpoints,
                  const std::string& channel,
                  const std::string& filename,
                  const std::vector<std::uint8_t>& bytes);

    const std::string& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace audionet
