#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neurogenic {
using MusicalTick = std::int64_t;
using EventId = std::uint64_t;
using TrackId = std::uint64_t;
inline constexpr std::uint32_t ticks_per_quarter = 960;
constexpr std::uint32_t stable_id(std::string_view value) noexcept {
    std::uint32_t hash = 2166136261U;
    for (const auto byte : value) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 16777619U;
    }
    return hash;
}
inline constexpr std::string_view sample_instrument_id = "sample.main";
inline constexpr std::string_view subtractive_instrument_id =
    "synth.subtractive.v1";
inline constexpr auto sample_instrument_key = stable_id(sample_instrument_id),
                      subtractive_instrument_key =
                          stable_id(subtractive_instrument_id);

enum class MusicalEventType : std::uint8_t {
    note,
    note_on,
    note_off,
    parameter
};
enum class TriggerCondition : std::uint8_t {
    always,
    first_loop,
    not_first_loop,
    every_nth_loop,
    n_of_m,
    previous_fired,
    previous_not_fired,
    fill_active,
    fill_inactive
};
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
    std::string parameter_destination;
    float trigger_probability{1.0F};
    TriggerCondition trigger_condition{TriggerCondition::always};
    std::uint16_t condition_n{1}, condition_m{1};
    std::uint8_t ratchet_count{1};
};
struct MusicalPattern {
    MusicalTick length{ticks_per_quarter * 4};
    std::vector<MusicalEvent> events;
};
struct Track {
    TrackId id{};
    std::string name;
    bool muted{};
    MusicalPattern pattern;
    std::string instrument_id{sample_instrument_id};
};
struct TransportMetadata {
    std::uint32_t microseconds_per_quarter{500000};
    std::uint16_t time_signature_numerator{4};
    std::uint16_t time_signature_denominator{4};
};
struct Timeline {
    TransportMetadata transport;
    std::vector<Track> tracks;
    std::uint64_t generation_seed{};
};
} // namespace neurogenic
