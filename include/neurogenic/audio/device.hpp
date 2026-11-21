#pragma once
#include <cstdint>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
namespace neurogenic {
class AudioEngine;
struct AudioPortCapabilities {
    std::string name;
    std::uint16_t maximum_channels{};
    struct NativeFormat {
        std::uint32_t sample_rate{};
        std::uint16_t channels{};
        friend bool operator==(const NativeFormat &, const NativeFormat &) = default;
    };
    std::vector<NativeFormat> native_formats;
};
struct AudioDeviceStatus {
    bool running{};
    bool backend_xruns_available{};
    std::uint64_t backend_xruns{};
    std::uint64_t starts{}, stops{}, reroutes{}, interruptions{};
};
class AudioDevice {
  public:
    explicit AudioDevice(AudioEngine &, bool force_null = false);
    ~AudioDevice();
    AudioDevice(const AudioDevice &) = delete;
    AudioDevice &operator=(const AudioDevice &) = delete;
    std::string backend() const;
    std::vector<std::string> outputs() const;
    std::vector<std::string> inputs() const;
    std::vector<AudioPortCapabilities> output_capabilities() const;
    std::vector<AudioPortCapabilities> input_capabilities() const;
    bool start(const std::string &output, std::string &error,
               std::uint32_t preferred_buffer_frames = 0);
    bool start_duplex(const std::string &output, const std::string &input,
                      std::uint16_t input_channels, std::string &error,
                      std::uint16_t output_channels = 2,
                      std::uint32_t preferred_buffer_frames = 0);
    void stop() noexcept;
    [[nodiscard]] std::uint32_t selected_sample_rate() const noexcept;
    [[nodiscard]] std::uint32_t selected_buffer_frames() const noexcept;
    [[nodiscard]] std::uint16_t selected_input_channels() const noexcept;
    [[nodiscard]] std::uint16_t selected_output_channels() const noexcept;
    [[nodiscard]] AudioDeviceStatus status() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace neurogenic
