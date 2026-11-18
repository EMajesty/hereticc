#include "neurogenic/sequencer/scheduler.hpp"
#include <cmath>

namespace neurogenic {
Scheduler::Scheduler(std::uint32_t rate, double bpm) noexcept
    : step_numerator_(static_cast<std::uint64_t>(rate)*60'000ULL), step_denominator_(static_cast<std::uint64_t>(std::llround(bpm*1000.0))*4ULL) { if(step_denominator_==0)step_denominator_=1; }
std::uint64_t Scheduler::step_frame(std::uint64_t step) const noexcept { const auto whole=step/step_denominator_;const auto remainder=step%step_denominator_;return whole*step_numerator_+(remainder*step_numerator_+step_denominator_/2)/step_denominator_; }
void Scheduler::request_pattern(Pattern pattern) noexcept { if(valid_pattern(pattern)){pending_=pattern;has_pending_=true;} }
std::size_t Scheduler::schedule(std::uint32_t frames, std::span<ScheduledEvent> out) noexcept {
  std::size_t count = 0;
  const auto end = frame_position_ + frames;
  for (auto boundary=step_frame(next_step_); boundary<end; boundary=step_frame(next_step_)) {
    if(boundary<frame_position_){++next_step_;continue;}
    if ((next_step_ % kSteps) == 0 && has_pending_) { active_ = pending_; has_pending_ = false; }
    const auto& step = active_.steps[next_step_ % kSteps];
    if (step.enabled && count < out.size()) out[count++] = {static_cast<std::uint32_t>(boundary - frame_position_), step.note, step.velocity};
    ++next_step_;
  }
  frame_position_ = end;
  return count;
}
}
