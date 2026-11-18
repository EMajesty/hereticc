#pragma once
#include "neurogenic/sequencer/pattern.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace neurogenic {
struct ScheduledEvent { std::uint32_t frame{}; std::uint8_t note{}; float velocity{}; };
class Scheduler {
public:
  Scheduler(std::uint32_t sample_rate, double bpm) noexcept;
  void request_pattern(Pattern pattern) noexcept;
  std::size_t schedule(std::uint32_t frames, std::span<ScheduledEvent> output) noexcept;
  [[nodiscard]] std::uint64_t frame_position() const noexcept { return frame_position_; }
  [[nodiscard]] const Pattern& active_pattern() const noexcept { return active_; }
private:
  [[nodiscard]] std::uint64_t step_frame(std::uint64_t step) const noexcept;
  std::uint64_t step_numerator_{};
  std::uint64_t step_denominator_{};
  std::uint64_t frame_position_{};
  std::uint64_t next_step_{};
  Pattern active_{};
  Pattern pending_{};
  bool has_pending_{};
};
}
