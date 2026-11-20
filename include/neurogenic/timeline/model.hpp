#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neurogenic {
using MusicalTick = std::int64_t;
using EventId = std::uint64_t;
using TrackId = std::uint64_t;
using PatternId = std::uint64_t;
using ClipId = std::uint64_t;
using PatternChainId = std::uint64_t;
using InstrumentInstanceId = std::uint32_t;
inline constexpr InstrumentInstanceId default_synth_instance_id = 1;
inline constexpr std::size_t maximum_instrument_instances = 16;
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
inline constexpr std::string_view external_midi_instrument_id =
    "midi.external.v1";
inline constexpr auto sample_instrument_key = stable_id(sample_instrument_id),
                      subtractive_instrument_key =
                          stable_id(subtractive_instrument_id),
                      external_midi_instrument_key =
                          stable_id(external_midi_instrument_id);

enum class MusicalEventType : std::uint8_t {
    note,
    note_on,
    note_off,
    parameter,
    control_change,
    program_change,
    pitch_bend
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
    InstrumentInstanceId instrument_instance_id{default_synth_instance_id};
    std::uint32_t midi_output_port{1};
    std::uint8_t midi_channel{};
};
struct TransportMetadata {
    std::uint32_t microseconds_per_quarter{500000};
    std::uint16_t time_signature_numerator{4};
    std::uint16_t time_signature_denominator{4};
};
enum class TimelinePlaybackMode : std::uint8_t { legacy, live_pattern, song };
enum class LaunchQuantization : std::uint8_t {
    immediate,
    quarter,
    bar,
    two_bars,
    pattern_end
};
enum class FollowActionType : std::uint8_t {
    continue_current,
    next,
    previous,
    specific,
    random_eligible,
    stop
};
enum class VoiceTransitionPolicy : std::uint8_t {
    preserve,
    release_outgoing,
    stop_outgoing
};
enum class SongEndBehavior : std::uint8_t {
    loop,
    stop,
    return_to_live
};
struct PatternFollowAction {
    FollowActionType type{FollowActionType::next};
    std::uint16_t after_repetitions{1};
    float probability{1.0F};
    PatternId target{};
};
struct PatternTrack {
    TrackId track{};
    MusicalPattern pattern;
};
struct PatternDefinition {
    PatternId id{};
    std::string name;
    MusicalTick length{ticks_per_quarter * 4};
    std::uint16_t time_signature_numerator{4};
    std::uint16_t time_signature_denominator{4};
    std::vector<PatternTrack> tracks;
    PatternFollowAction follow;
};
struct PatternChainEntry {
    PatternId pattern{};
    std::uint16_t repeats{1};
};
struct ArrangementClip {
    ClipId id{};
    PatternId pattern{};
    MusicalTick start{};
    MusicalTick duration{ticks_per_quarter * 4};
    bool loop{true};
    std::int8_t transpose{};
    float velocity_scale{1.0F};
    bool enabled{true};
};
struct SongArrangement {
    TimelinePlaybackMode playback_mode{TimelinePlaybackMode::legacy};
    std::vector<PatternDefinition> patterns;
    std::vector<ArrangementClip> clips;
    PatternId active_pattern{}, queued_pattern{};
    LaunchQuantization launch_quantization{LaunchQuantization::bar};
    std::vector<PatternId> pattern_chain;
    std::size_t pattern_chain_position{};
    PatternChainId pattern_chain_id{1};
    std::vector<PatternChainEntry> chain_entries;
    bool chain_loop{}, chain_running{};
    VoiceTransitionPolicy voice_transition{VoiceTransitionPolicy::preserve};
    SongEndBehavior song_end_behavior{SongEndBehavior::loop};
    PatternId return_live_pattern{};
    MusicalTick song_start{}, song_end{};
    bool loop_enabled{};
    MusicalTick loop_start{}, loop_end{};
};
struct Timeline {
    TransportMetadata transport;
    std::vector<Track> tracks;
    std::uint64_t generation_seed{};
    SongArrangement arrangement;
};
} // namespace neurogenic
