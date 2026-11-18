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
  void set_tempo(std::uint32_t microseconds_per_quarter) noexcept;
  void request_pattern(Pattern pattern) noexcept;
  std::size_t schedule(std::uint64_t start_frame,std::uint32_t frames, std::span<ScheduledEvent> output) noexcept;
  [[nodiscard]] const Pattern& active_pattern() const noexcept { return active_; }
  [[nodiscard]] std::uint64_t dropped_events() const noexcept{return dropped_events_;}
private:
  [[nodiscard]] std::uint64_t step_frame(std::uint64_t step) const noexcept;
  std::uint32_t sample_rate_{};
  std::uint64_t step_numerator_{};
  std::uint64_t step_denominator_{};
  Pattern active_{};
  Pattern pending_{};
  bool has_pending_{};
  std::uint64_t dropped_events_{};
};
}
