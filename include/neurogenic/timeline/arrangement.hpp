#pragma once
#include "neurogenic/timeline/model.hpp"
#include <string>

namespace neurogenic {
struct ArrangementLimits {
    std::size_t patterns{256};
    std::size_t clips{4096};
    std::size_t pattern_tracks{4096};
    std::size_t expanded_events{65'536};
};

[[nodiscard]] bool validate_arrangement(const Timeline &, std::string &,
                                        ArrangementLimits = {});
// Produces the finite song or selected live pattern consumed by the existing
// timeline compiler. Legacy timelines are returned unchanged.
[[nodiscard]] bool materialize_arrangement(const Timeline &, Timeline &,
                                           std::string &);
[[nodiscard]] MusicalTick launch_boundary(MusicalTick now,
                                          LaunchQuantization,
                                          MusicalTick pattern_length,
                                          const TransportMetadata &) noexcept;
} // namespace neurogenic
