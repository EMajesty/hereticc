#pragma once
#include "neurogenic/timeline/model.hpp"
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace neurogenic {
struct PlaybackEvent {
    MusicalTick tick{};
    EventId id{};
    EventId target_event_id{};
    MusicalEventType type{};
    std::uint8_t pitch{};
    std::uint32_t parameter_id{};
    std::uint32_t parameter_target{};
    float value{};
    std::uint64_t first_cycle{};
};
struct PlaybackTrack {
    TrackId id{};
    MusicalTick length{};
    std::size_t event_begin{}, event_count{}, cursor_index{};
    std::uint64_t cursor_cycle{};
    bool muted{};
    std::uint32_t instrument{};
};
struct PlaybackPlan {
    TransportMetadata transport{};
    std::vector<PlaybackTrack> tracks;
    std::vector<PlaybackEvent> events;
    std::vector<std::size_t> scheduler_heap;
};
bool compile_timeline(const Timeline &, PlaybackPlan &, std::string &error);

struct TimelineScheduledEvent {
    std::uint32_t frame{};
    TrackId track{};
    EventId id{};
    EventId target_event_id{};
    std::uint64_t occurrence{};
    MusicalEventType type{};
    std::uint8_t pitch{};
    std::uint32_t parameter_id{};
    std::uint32_t parameter_target{};
    float value{};
    std::uint32_t instrument{};
};
class TimelineScheduler {
  public:
    explicit TimelineScheduler(std::uint32_t sample_rate)
        : sample_rate_(sample_rate) {}
    void set_plan(PlaybackPlan *plan, std::uint64_t start_frame) noexcept;
    std::size_t schedule(std::uint64_t start_frame, std::uint32_t frames,
                         std::span<TimelineScheduledEvent>) noexcept;
    [[nodiscard]] std::uint64_t dropped_events() const noexcept {
        return dropped_events_;
    }
    [[nodiscard]] std::uint64_t overrun_count() const noexcept {
        return dropped_events_;
    }
    [[nodiscard]] std::uint64_t frame_at_tick(MusicalTick tick) const noexcept;

  private:
    void seek(std::uint64_t start_frame) noexcept;
    [[nodiscard]] std::uint64_t frame_for(const PlaybackTrack &,
                                          const PlaybackEvent &,
                                          std::uint64_t cycle) const noexcept;
    void advance(PlaybackTrack &) noexcept;
    [[nodiscard]] bool track_after(std::size_t, std::size_t) const noexcept;
    std::uint32_t sample_rate_{};
    PlaybackPlan *plan_{};
    std::uint64_t expected_frame_{}, dropped_events_{};
};
} // namespace neurogenic
