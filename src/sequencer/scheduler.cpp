#include "neurogenic/sequencer/scheduler.hpp"
#include <cmath>
#include <limits>

namespace neurogenic {
#if defined(__SIZEOF_INT128__)
__extension__ using WideInteger=__int128;
#endif
Scheduler::Scheduler(std::uint32_t rate, double bpm) noexcept
    : sample_rate_(rate),step_numerator_(static_cast<std::uint64_t>(rate)*60'000ULL), step_denominator_(static_cast<std::uint64_t>(std::llround(bpm*1000.0))*4ULL) { if(step_denominator_==0)step_denominator_=1; }
void Scheduler::set_tempo(std::uint32_t microseconds_per_quarter) noexcept {step_numerator_=static_cast<std::uint64_t>(sample_rate_)*microseconds_per_quarter;step_denominator_=4'000'000ULL;}
std::uint64_t Scheduler::step_frame(std::uint64_t step) const noexcept {
  const auto result=(static_cast<WideInteger>(step)*step_numerator_+step_denominator_/2)/step_denominator_;
  return result>std::numeric_limits<std::uint64_t>::max()?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(result);
}
void Scheduler::request_pattern(Pattern pattern) noexcept { if(valid_pattern(pattern)){pending_=pattern;has_pending_=true;} }
std::size_t Scheduler::schedule(std::uint64_t start,std::uint32_t frames, std::span<ScheduledEvent> out) noexcept {
  std::size_t count=0;const auto end=frames>std::numeric_limits<std::uint64_t>::max()-start?std::numeric_limits<std::uint64_t>::max():start+frames;
  const auto wide_step=(static_cast<WideInteger>(start)*step_denominator_)/step_numerator_;
  auto step=wide_step>std::numeric_limits<std::uint64_t>::max()?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(wide_step);
  while(step_frame(step)<start){if(step==std::numeric_limits<std::uint64_t>::max())return 0;++step;}
  for(auto boundary=step_frame(step);boundary<end;){
    if((step%kSteps)==0&&has_pending_){active_=pending_;has_pending_=false;}
    const auto& value=active_.steps[step%kSteps];
    if(value.enabled){if(count<out.size())out[count++]={static_cast<std::uint32_t>(boundary-start),value.note,value.velocity};else ++dropped_events_;}
    if(step==std::numeric_limits<std::uint64_t>::max())break;
    boundary=step_frame(++step);
  }
  return count;
}
}
