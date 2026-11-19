#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace neurogenic {
class AudioEngine;
class AudioDevice {
  public:
    explicit AudioDevice(AudioEngine &, bool force_null = false);
    ~AudioDevice();
    AudioDevice(const AudioDevice &) = delete;
    AudioDevice &operator=(const AudioDevice &) = delete;
    std::string backend() const;
    std::vector<std::string> outputs() const;
    std::vector<std::string> inputs() const;
    bool start(const std::string &output, std::string &error);
    bool start_duplex(const std::string &output, const std::string &input,
                      std::uint16_t input_channels, std::string &error);
    void stop() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace neurogenic
