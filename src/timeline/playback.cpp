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
bool valid_event(const MusicalEvent& event) noexcept{
  return event.id!=0&&event.tick>=0&&event.pitch<=127&&std::isfinite(event.velocity)&&
    event.velocity>=0&&event.velocity<=1&&std::isfinite(event.value)&&
    (event.type!=MusicalEventType::note||(event.duration>0&&event.duration<=std::numeric_limits<MusicalTick>::max()-event.tick));
}
}
bool compile_timeline(const Timeline& timeline,PlaybackPlan& output,std::string& error){
  if(timeline.transport.microseconds_per_quarter==0||timeline.transport.time_signature_numerator==0||timeline.transport.time_signature_denominator==0){error="invalid transport metadata";return false;}
  if(timeline.tracks.size()>max_realtime_tracks){error="timeline exceeds real-time track capacity";return false;}
  PlaybackPlan plan;plan.transport=timeline.transport;
  for(const auto& track:timeline.tracks){
    if(track.id==0||track.pattern.length<=0){error="track identifier and pattern length must be positive";return false;}
    for(std::size_t ti=0;ti<plan.track_count;++ti)if(plan.tracks[ti].id==track.id){error="duplicate track identifier";return false;}
    const auto begin=plan.event_count;
    for(std::size_t index=0;index<track.pattern.events.size();++index){
      const auto& event=track.pattern.events[index];
      if(!valid_event(event)||event.tick>=track.pattern.length){error="invalid musical event";return false;}
      for(std::size_t previous=0;previous<index;++previous)if(track.pattern.events[previous].id==event.id){error="duplicate event identifier";return false;}
      const auto required=event.type==MusicalEventType::note?2U:1U;
      if(plan.event_count+required>plan.events.size()){error="timeline exceeds real-time event capacity";return false;}
      if(event.type==MusicalEventType::note){
        plan.events[plan.event_count++]={event.tick,event.id,MusicalEventType::note_on,event.pitch,0,event.velocity};
        plan.events[plan.event_count++]={event.tick+event.duration,event.id,MusicalEventType::note_off,event.pitch,0,0};
      }else plan.events[plan.event_count++]={event.tick,event.id,event.type,event.pitch,event.parameter_id,event.type==MusicalEventType::parameter?event.value:event.velocity};
    }
    std::sort(plan.events.begin()+static_cast<std::ptrdiff_t>(begin),plan.events.begin()+static_cast<std::ptrdiff_t>(plan.event_count),[](const auto& a,const auto& b){
      if(a.tick!=b.tick)return a.tick<b.tick;
      if(type_order(a.type)!=type_order(b.type))return type_order(a.type)<type_order(b.type);
      return a.id<b.id;
    });
    const auto count=plan.event_count-begin;
    plan.tracks[plan.track_count++]={track.id,track.pattern.length,static_cast<std::uint16_t>(begin),static_cast<std::uint16_t>(count),track.muted};
  }
  output=plan;error.clear();return true;
}

std::uint64_t TimelineScheduler::frame_at_tick(MusicalTick tick) const noexcept{
  if(tick<=0)return 0;
  const auto numerator=static_cast<WideInteger>(tick)*sample_rate_*plan_.transport.microseconds_per_quarter;
  const auto denominator=static_cast<WideInteger>(ticks_per_quarter)*1'000'000;
  return static_cast<std::uint64_t>((numerator+denominator/2)/denominator);
}

std::size_t TimelineScheduler::schedule(std::uint32_t frames,std::span<TimelineScheduledEvent> output) noexcept{
  const auto end=frame_position_+frames;std::size_t count{};
  if(has_plan_)for(std::size_t ti=0;ti<plan_.track_count;++ti){
    const auto& track=plan_.tracks[ti];if(track.muted)continue;
    for(std::size_t ei=0;ei<track.event_count;++ei){
      const auto& event=plan_.events[track.event_begin+ei];
      std::int64_t cycle{};const auto base_frame=frame_at_tick(event.tick);
      if(base_frame<frame_position_){
        const auto approximate_tick=static_cast<MusicalTick>((static_cast<WideInteger>(frame_position_)*ticks_per_quarter*1'000'000)/(static_cast<WideInteger>(sample_rate_)*plan_.transport.microseconds_per_quarter));
        if(approximate_tick>event.tick)cycle=(approximate_tick-event.tick)/track.length;
      }
      for(;;++cycle){
        const auto absolute_tick=event.tick+cycle*track.length;const auto frame=frame_at_tick(absolute_tick);
        if(frame<frame_position_)continue;
        if(frame>=end)break;
        TimelineScheduledEvent scheduled{static_cast<std::uint32_t>(frame-frame_position_),track.id,event.id,event.type,event.pitch,event.parameter_id,event.value};
        if(count<output.size())output[count++]=scheduled;else ++dropped_events_;
      }
    }
  }
  std::sort(output.begin(),output.begin()+static_cast<std::ptrdiff_t>(count),[](const auto& a,const auto& b){
    if(a.frame!=b.frame)return a.frame<b.frame;
    if(a.track!=b.track)return a.track<b.track;
    if(type_order(a.type)!=type_order(b.type))return type_order(a.type)<type_order(b.type);
    return a.id<b.id;
  });
  frame_position_=end;return count;
}
}
