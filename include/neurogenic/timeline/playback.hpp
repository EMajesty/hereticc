#pragma once
#include "neurogenic/timeline/model.hpp"
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace neurogenic {
struct PlaybackEvent { MusicalTick tick{};EventId id{};MusicalEventType type{};std::uint8_t pitch{};std::uint32_t parameter_id{};float value{}; };
struct PlaybackTrack { TrackId id{};MusicalTick length{};std::size_t event_begin{},event_count{};bool muted{}; };
struct PlaybackPlan {
  TransportMetadata transport{};
  std::vector<PlaybackTrack> tracks;
  std::vector<PlaybackEvent> events;
};
bool compile_timeline(const Timeline&,PlaybackPlan&,std::string& error);

struct TimelineScheduledEvent { std::uint32_t frame{};TrackId track{};EventId id{};MusicalEventType type{};std::uint8_t pitch{};std::uint32_t parameter_id{};float value{}; };
class TimelineScheduler {
public:
  explicit TimelineScheduler(std::uint32_t sample_rate):sample_rate_(sample_rate){}
  void set_plan(const PlaybackPlan* plan) noexcept{plan_=plan;}
  std::size_t schedule(std::uint64_t start_frame,std::uint32_t frames,std::span<TimelineScheduledEvent>) noexcept;
  [[nodiscard]] std::uint64_t dropped_events() const noexcept{return dropped_events_;}
  [[nodiscard]] std::uint64_t frame_at_tick(MusicalTick tick) const noexcept;
private:
  std::uint32_t sample_rate_{};const PlaybackPlan* plan_{};std::uint64_t dropped_events_{};
};
}
