#include "neurogenic/dsp/processor.hpp"
#include <algorithm>
#include <cmath>
namespace neurogenic {
StereoDelay::StereoDelay(std::uint32_t rate, float maximum)
    : rate_(rate), max_seconds_(maximum),
      left_(static_cast<std::size_t>(rate * maximum) + 1),
      right_(left_.size()) {}
void StereoDelay::set_sample_rate(std::uint32_t rate) {
    rate_ = std::max(1U, rate);
    left_.assign(static_cast<std::size_t>(rate_ * max_seconds_) + 1, 0);
    right_.assign(left_.size(), 0);
    cursor_ = 0;
}
bool StereoDelay::set_parameter(std::uint32_t raw, float value) noexcept {
    if (!std::isfinite(value))
        return false;
    switch (static_cast<DelayParameter>(raw)) {
    case DelayParameter::time:
        time_.store(std::clamp(value, 0.001F, max_seconds_));
        break;
    case DelayParameter::feedback:
        feedback_.store(std::clamp(value, 0.0F, 0.95F));
        break;
    case DelayParameter::mix:
        mix_.store(std::clamp(value, 0.0F, 1.0F));
        break;
    default:
        return false;
    }
    return true;
}
void StereoDelay::reset() noexcept {
    std::fill(left_.begin(), left_.end(), 0);
    std::fill(right_.begin(), right_.end(), 0);
    cursor_ = 0;
}
void StereoDelay::process(std::span<float> left,
                          std::span<float> right) noexcept {
    if (bypass_.load())
        return;
    const auto count = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i) {
        smooth_time_ += (time_.load() - smooth_time_) * 0.001F;
        smooth_feedback_ += (feedback_.load() - smooth_feedback_) * 0.001F;
        smooth_mix_ += (mix_.load() - smooth_mix_) * 0.001F;
        const auto delay = std::clamp<std::size_t>(
            static_cast<std::size_t>(smooth_time_ * rate_), 1,
            left_.size() - 1);
        const auto read = (cursor_ + left_.size() - delay) % left_.size();
        const auto dl = left_[read], dr = right_[read];
        left_[cursor_] = left[i] + dr * smooth_feedback_;
        right_[cursor_] = right[i] + dl * smooth_feedback_;
        left[i] = left[i] * (1 - smooth_mix_) + dl * smooth_mix_;
        right[i] = right[i] * (1 - smooth_mix_) + dr * smooth_mix_;
        cursor_ = (cursor_ + 1) % left_.size();
    }
}
} // namespace neurogenic
