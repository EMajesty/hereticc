#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace neurogenic {
using MusicalTick=std::int64_t;
using EventId=std::uint64_t;
using TrackId=std::uint64_t;
inline constexpr std::uint32_t ticks_per_quarter=960;

enum class MusicalEventType:std::uint8_t{note,note_on,note_off,parameter};
struct MusicalEvent {
  EventId id{};
  MusicalTick tick{};
  MusicalEventType type{MusicalEventType::note};
  std::uint8_t pitch{60};
  float velocity{1.0F};
  MusicalTick duration{};
  std::uint32_t parameter_id{};
  float value{};
  EventId target_event_id{};
};
struct MusicalPattern { MusicalTick length{ticks_per_quarter*4};std::vector<MusicalEvent> events; };
struct Track { TrackId id{};std::string name;bool muted{};MusicalPattern pattern; };
struct TransportMetadata {
  std::uint32_t microseconds_per_quarter{500000};
  std::uint16_t time_signature_numerator{4};
  std::uint16_t time_signature_denominator{4};
};
struct Timeline { TransportMetadata transport;std::vector<Track> tracks; };
}
