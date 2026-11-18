#include "neurogenic/sequencer/pattern_adapter.hpp"
#include <stdexcept>

namespace neurogenic {
Timeline adapt_step_pattern(const Pattern& pattern,TrackId track_id,MusicalTick step_length){
  if(!valid_pattern(pattern)||step_length<=0)throw std::invalid_argument("invalid compatibility pattern");
  Timeline timeline;Track track;track.id=track_id;track.name="16-step compatibility";
  track.pattern.length=static_cast<MusicalTick>(pattern.steps.size())*step_length;
  for(std::size_t i=0;i<pattern.steps.size();++i){
    const auto& step=pattern.steps[i];if(!step.enabled)continue;
    track.pattern.events.push_back({static_cast<EventId>(i+1),static_cast<MusicalTick>(i)*step_length,
      MusicalEventType::note,step.note,step.velocity,step_length,0,0});
  }
  timeline.tracks.push_back(std::move(track));return timeline;
}
}
