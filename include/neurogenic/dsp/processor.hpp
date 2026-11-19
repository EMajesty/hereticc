#pragma once
#include <atomic>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
namespace neurogenic {
class AudioProcessor {
  public:
    virtual ~AudioProcessor() = default;
    virtual std::string_view id() const noexcept = 0;
    virtual void set_sample_rate(std::uint32_t) = 0;
    virtual bool set_parameter(std::uint32_t, float) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void set_bypass(bool) noexcept = 0;
    virtual void process(std::span<float>, std::span<float>) noexcept = 0;
};
enum class DelayParameter : std::uint32_t { time = 1, feedback, mix };
class StereoDelay final : public AudioProcessor {
  public:
    explicit StereoDelay(std::uint32_t = 48000, float max_seconds = 2);
    std::string_view id() const noexcept override {
        return "effect.stereo-delay.v1";
    }
    void set_sample_rate(std::uint32_t) override;
    bool set_parameter(std::uint32_t, float) noexcept override;
    void reset() noexcept override;
    void set_bypass(bool value) noexcept override { bypass_.store(value); }
    void process(std::span<float>, std::span<float>) noexcept override;

  private:
    std::uint32_t rate_{};
    float max_seconds_{}, smooth_time_{0.25F}, smooth_feedback_{0.3F},
        smooth_mix_{};
    std::atomic<float> time_{0.25F}, feedback_{0.3F}, mix_{};
    std::atomic<bool> bypass_{};
    std::vector<float> left_, right_;
    std::size_t cursor_{};
};
} // namespace neurogenic
