#include "neurogenic/sequencer/scheduler.hpp"
#include <algorithm>
#include <cmath>

namespace neurogenic {
Scheduler::Scheduler(std::uint32_t rate, double bpm) noexcept
    : frames_per_step_(std::max<std::uint64_t>(1, std::llround(rate * 60.0 / bpm / 4.0))) {}
void Scheduler::request_pattern(Pattern pattern) noexcept { pending_ = pattern; has_pending_ = true; }
std::size_t Scheduler::schedule(std::uint32_t frames, std::span<ScheduledEvent> out) noexcept {
  std::size_t count = 0;
  const auto end = frame_position_ + frames;
  auto boundary = ((frame_position_ + frames_per_step_ - 1) / frames_per_step_) * frames_per_step_;
  for (; boundary < end; boundary += frames_per_step_) {
    const auto absolute_step = boundary / frames_per_step_;
    if ((absolute_step % kSteps) == 0 && has_pending_) { active_ = pending_; has_pending_ = false; }
    const auto& step = active_.steps[absolute_step % kSteps];
    if (step.enabled && count < out.size()) out[count++] = {static_cast<std::uint32_t>(boundary - frame_position_), step.note, step.velocity};
  }
  frame_position_ = end;
  return count;
}
}
