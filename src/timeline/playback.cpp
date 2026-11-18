#include "neurogenic/timeline/playback.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace neurogenic {
namespace {
#if defined(__SIZEOF_INT128__)
__extension__ using WideInteger=__int128;
#else
#error "NEUROGENIC timeline scheduling requires a compiler with 128-bit integer support"
#endif
int type_order(MusicalEventType type) noexcept{return type==MusicalEventType::note_off?0:type==MusicalEventType::note_on?1:2;}
bool scheduled_before(const TimelineScheduledEvent& a,const TimelineScheduledEvent& b) noexcept{
  if(a.frame!=b.frame)return a.frame<b.frame;
  if(a.track!=b.track)return a.track<b.track;
  if(type_order(a.type)!=type_order(b.type))return type_order(a.type)<type_order(b.type);
  return a.id<b.id;
}
bool valid_event(const MusicalEvent& event) noexcept{
  return event.id!=0&&event.tick>=0&&event.pitch<=127&&std::isfinite(event.velocity)&&
    event.velocity>=0&&event.velocity<=1&&std::isfinite(event.value)&&
    (event.type!=MusicalEventType::note||(event.duration>0&&event.duration<=std::numeric_limits<MusicalTick>::max()-event.tick));
}
}
bool compile_timeline(const Timeline& timeline,PlaybackPlan& output,std::string& error){
  if(timeline.transport.microseconds_per_quarter==0||timeline.transport.time_signature_numerator==0||timeline.transport.time_signature_denominator==0){error="invalid transport metadata";return false;}
  PlaybackPlan plan;plan.transport=timeline.transport;plan.tracks.reserve(timeline.tracks.size());
  std::size_t compiled_events{};for(const auto& track:timeline.tracks)for(const auto& event:track.pattern.events){const auto addition=event.type==MusicalEventType::note?2U:1U;if(addition>std::numeric_limits<std::size_t>::max()-compiled_events){error="timeline is too large to compile";return false;}compiled_events+=addition;}
  plan.events.reserve(compiled_events);
  for(const auto& track:timeline.tracks){
    if(track.id==0||track.pattern.length<=0){error="track identifier and pattern length must be positive";return false;}
    for(const auto& compiled:plan.tracks)if(compiled.id==track.id){error="duplicate track identifier";return false;}
    const auto begin=plan.events.size();
    for(std::size_t index=0;index<track.pattern.events.size();++index){
      const auto& event=track.pattern.events[index];
      if(!valid_event(event)||event.tick>=track.pattern.length){error="invalid musical event";return false;}
      for(std::size_t previous=0;previous<index;++previous)if(track.pattern.events[previous].id==event.id){error="duplicate event identifier";return false;}
      if(event.type==MusicalEventType::note){
        plan.events.push_back({event.tick,event.id,MusicalEventType::note_on,event.pitch,0,event.velocity});
        plan.events.push_back({event.tick+event.duration,event.id,MusicalEventType::note_off,event.pitch,0,0});
      }else plan.events.push_back({event.tick,event.id,event.type,event.pitch,event.parameter_id,event.type==MusicalEventType::parameter?event.value:event.velocity});
    }
    std::sort(plan.events.begin()+static_cast<std::ptrdiff_t>(begin),plan.events.end(),[](const auto& a,const auto& b){
      if(a.tick!=b.tick)return a.tick<b.tick;
      if(type_order(a.type)!=type_order(b.type))return type_order(a.type)<type_order(b.type);
      return a.id<b.id;
    });
    plan.tracks.push_back({track.id,track.pattern.length,begin,plan.events.size()-begin,track.muted});
  }
  output=plan;error.clear();return true;
}

std::uint64_t TimelineScheduler::frame_at_tick(MusicalTick tick) const noexcept{
  if(tick<=0||plan_==nullptr)return 0;
  const auto numerator=static_cast<WideInteger>(tick)*sample_rate_*plan_->transport.microseconds_per_quarter;
  const auto denominator=static_cast<WideInteger>(ticks_per_quarter)*1'000'000;
  const auto frame=(numerator+denominator/2)/denominator;
  return frame>std::numeric_limits<std::uint64_t>::max()?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(frame);
}

std::size_t TimelineScheduler::schedule(std::uint64_t start,std::uint32_t frames,std::span<TimelineScheduledEvent> output) noexcept{
  const auto end=frames>std::numeric_limits<std::uint64_t>::max()-start?std::numeric_limits<std::uint64_t>::max():start+frames;std::size_t count{};
  if(plan_!=nullptr)for(const auto& track:plan_->tracks){
    if(track.muted)continue;
    for(std::size_t ei=0;ei<track.event_count;++ei){
      const auto& event=plan_->events[track.event_begin+ei];
      WideInteger cycle{};const auto base_frame=frame_at_tick(event.tick);
      if(base_frame<start){
        const auto approximate_tick=(static_cast<WideInteger>(start)*ticks_per_quarter*1'000'000)/(static_cast<WideInteger>(sample_rate_)*plan_->transport.microseconds_per_quarter);
        if(approximate_tick>event.tick)cycle=(approximate_tick-event.tick)/track.length;
      }
      for(;;++cycle){
        const auto absolute_tick=static_cast<WideInteger>(event.tick)+cycle*track.length;
        const auto numerator=absolute_tick*sample_rate_*plan_->transport.microseconds_per_quarter;
        const auto denominator=static_cast<WideInteger>(ticks_per_quarter)*1'000'000;
        const auto wide_frame=(numerator+denominator/2)/denominator;
        const auto frame=wide_frame>std::numeric_limits<std::uint64_t>::max()?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(wide_frame);
        if(frame<start)continue;
        if(frame>=end)break;
        TimelineScheduledEvent scheduled{static_cast<std::uint32_t>(frame-start),track.id,event.id,event.type,event.pitch,event.parameter_id,event.value};
        if(count<output.size())output[count++]=scheduled;
        else {
          ++dropped_events_;auto worst=std::max_element(output.begin(),output.end(),scheduled_before);
          if(worst!=output.end()&&scheduled_before(scheduled,*worst))*worst=scheduled;
        }
      }
    }
  }
  std::sort(output.begin(),output.begin()+static_cast<std::ptrdiff_t>(count),scheduled_before);
  return count;
}
}
