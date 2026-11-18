#pragma once
#include "neurogenic/sequencer/pattern.hpp"
#include "neurogenic/timeline/model.hpp"

namespace neurogenic {
Timeline adapt_step_pattern(const Pattern&,TrackId track_id=1,MusicalTick step_length=ticks_per_quarter/4);
}
