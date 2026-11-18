#pragma once
#include "neurogenic/timeline/model.hpp"
#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace neurogenic {
inline constexpr std::size_t max_realtime_tracks=32;
inline constexpr std::size_t max_realtime_events=512;
struct PlaybackEvent { MusicalTick tick{};EventId id{};MusicalEventType type{};std::uint8_t pitch{};std::uint32_t parameter_id{};float value{}; };
struct PlaybackTrack { TrackId id{};MusicalTick length{};std::uint16_t event_begin{},event_count{};bool muted{}; };
struct PlaybackPlan {
  TransportMetadata transport{};
  std::array<PlaybackTrack,max_realtime_tracks> tracks{};
  std::array<PlaybackEvent,max_realtime_events> events{};
  std::size_t track_count{},event_count{};
};
bool compile_timeline(const Timeline&,PlaybackPlan&,std::string& error);

struct TimelineScheduledEvent { std::uint32_t frame{};TrackId track{};EventId id{};MusicalEventType type{};std::uint8_t pitch{};std::uint32_t parameter_id{};float value{}; };
class TimelineScheduler {
public:
  explicit TimelineScheduler(std::uint32_t sample_rate):sample_rate_(sample_rate){}
  void set_plan(const PlaybackPlan& plan) noexcept{plan_=plan;has_plan_=true;}
  std::size_t schedule(std::uint32_t frames,std::span<TimelineScheduledEvent>) noexcept;
  [[nodiscard]] std::uint64_t frame_position() const noexcept{return frame_position_;}
  [[nodiscard]] std::uint64_t dropped_events() const noexcept{return dropped_events_;}
  [[nodiscard]] std::uint64_t frame_at_tick(MusicalTick tick) const noexcept;
private:
  std::uint32_t sample_rate_{};PlaybackPlan plan_{};bool has_plan_{};std::uint64_t frame_position_{},dropped_events_{};
};
}
