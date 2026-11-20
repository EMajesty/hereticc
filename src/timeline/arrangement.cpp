#include "neurogenic/timeline/arrangement.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include "neurogenic/instrument/instrument.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace neurogenic {
namespace {
const PatternDefinition *pattern(const SongArrangement &a, PatternId id) {
    const auto it = std::find_if(a.patterns.begin(), a.patterns.end(),
                                 [=](const auto &p) { return p.id == id; });
    return it == a.patterns.end() ? nullptr : &*it;
}
const Track *track(const Timeline &t, TrackId id) {
    const auto it = std::find_if(t.tracks.begin(), t.tracks.end(),
                                 [=](const auto &v) { return v.id == id; });
    return it == t.tracks.end() ? nullptr : &*it;
}
EventId placed_id(ClipId clip, EventId source, std::uint64_t repetition) {
    auto value = clip * 0x9e3779b97f4a7c15ULL;
    value ^= source + 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
    value ^= repetition + 0x517cc1b727220a95ULL + (value << 6U) + (value >> 2U);
    return value == 0 ? 1 : value;
}
std::uint64_t random_word(std::uint64_t seed, PatternId pattern,
                          std::uint64_t step) noexcept {
    auto value = seed ^ (pattern * 0x9e3779b97f4a7c15ULL) ^
                 (step * 0xbf58476d1ce4e5b9ULL);
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}
} // namespace

bool validate_arrangement(const Timeline &timeline, std::string &error,
                          ArrangementLimits limits) {
    const auto &a = timeline.arrangement;
    if (a.patterns.size() > limits.patterns || a.clips.size() > limits.clips) {
        error = "arrangement capacity exceeded";
        return false;
    }
    std::set<PatternId> pattern_ids;
    std::set<ClipId> clip_ids;
    std::size_t pattern_tracks{};
    for (const auto &p : a.patterns) {
        if (!p.id || p.name.empty() || p.length <= 0 ||
            !p.time_signature_numerator || !p.time_signature_denominator ||
            !pattern_ids.insert(p.id).second) {
            error = "invalid or duplicate pattern";
            return false;
        }
        pattern_tracks += p.tracks.size();
        if (pattern_tracks > limits.pattern_tracks) {
            error = "pattern-track capacity exceeded";
            return false;
        }
        std::set<TrackId> tracks;
        for (const auto &pt : p.tracks) {
            if (!track(timeline, pt.track) || pt.pattern.length <= 0 ||
                !tracks.insert(pt.track).second) {
                error = "pattern references an invalid or duplicate track";
                return false;
            }
            std::set<EventId> events;
            for (const auto &event : pt.pattern.events)
                if (!event.id || !events.insert(event.id).second ||
                    event.tick < 0 || event.tick >= pt.pattern.length ||
                    !std::isfinite(event.velocity) || event.velocity < 0 ||
                    event.velocity > 1 ||
                    !std::isfinite(event.trigger_probability) ||
                    event.trigger_probability < 0 ||
                    event.trigger_probability > 1 || !event.ratchet_count ||
                    event.ratchet_count > 8) {
                    error = "pattern contains an invalid event position";
                    return false;
                }
        }
    }
    if ((a.active_pattern && !pattern(a, a.active_pattern)) ||
        (a.queued_pattern && !pattern(a, a.queued_pattern))) {
        error = "active or queued pattern does not exist";
        return false;
    }
    for (const auto id : a.pattern_chain)
        if (!pattern(a, id)) {
            error = "pattern chain references a missing pattern";
            return false;
        }
    if (!a.pattern_chain.empty() &&
        a.pattern_chain_position >= a.pattern_chain.size()) {
        error = "pattern chain position is out of range";
        return false;
    }
    if (a.chain_entries.size() > 4096) {
        error = "pattern chain capacity exceeded";
        return false;
    }
    for (const auto &entry : a.chain_entries)
        if (!pattern(a, entry.pattern) || entry.repeats == 0) {
            error = "pattern chain entry is invalid";
            return false;
        }
    for (const auto &definition : a.patterns) {
        const auto &follow = definition.follow;
        if (!follow.after_repetitions || !std::isfinite(follow.probability) ||
            follow.probability < 0 || follow.probability > 1 ||
            (follow.type == FollowActionType::specific &&
             !pattern(a, follow.target))) {
            error = "pattern follow action is invalid";
            return false;
        }
    }
    if (a.automation.size() > limits.expanded_events) {
        error = "arrangement automation capacity exceeded";
        return false;
    }
    std::set<EventId> automation_ids;
    for (const auto &event : a.automation)
        if (!event.id || !automation_ids.insert(event.id).second ||
            event.type != MusicalEventType::parameter || event.tick < 0 ||
            event.parameter_destination.empty() ||
            !std::isfinite(event.value)) {
            error = "arrangement automation event is invalid";
            return false;
        }
    if (a.return_live_pattern && !pattern(a, a.return_live_pattern)) {
        error = "song return pattern does not exist";
        return false;
    }
    for (const auto &clip : a.clips) {
        if (!clip.id || !clip_ids.insert(clip.id).second ||
            !pattern(a, clip.pattern) || clip.start < 0 || clip.duration <= 0 ||
            clip.start >
                std::numeric_limits<MusicalTick>::max() - clip.duration ||
            clip.transpose < -127 || !std::isfinite(clip.velocity_scale) ||
            clip.velocity_scale < 0 || clip.velocity_scale > 4) {
            error = "invalid arrangement clip";
            return false;
        }
    }
    if (a.playback_mode == TimelinePlaybackMode::song) {
        if (a.song_start < 0 || a.song_end <= a.song_start) {
            error = "song range is invalid";
            return false;
        }
        if (a.loop_enabled &&
            (a.loop_start < a.song_start || a.loop_end <= a.loop_start ||
             a.loop_end > a.song_end)) {
            error = "arrangement loop range is invalid";
            return false;
        }
        // A pattern can address several tracks. Any temporal overlap on one of
        // those tracks is rejected because note/automation precedence would be
        // ambiguous in this initial serial-lane model.
        for (std::size_t i = 0; i < a.clips.size(); ++i) {
            if (!a.clips[i].enabled)
                continue;
            const auto *left = pattern(a, a.clips[i].pattern);
            for (std::size_t j = i + 1; j < a.clips.size(); ++j) {
                if (!a.clips[j].enabled ||
                    a.clips[i].start + a.clips[i].duration <=
                        a.clips[j].start ||
                    a.clips[j].start + a.clips[j].duration <= a.clips[i].start)
                    continue;
                const auto *right = pattern(a, a.clips[j].pattern);
                for (const auto &lt : left->tracks)
                    if (std::any_of(right->tracks.begin(), right->tracks.end(),
                                    [&](const auto &rt) {
                                        return rt.track == lt.track;
                                    })) {
                        error = "clips overlap on track " +
                                std::to_string(lt.track);
                        return false;
                    }
            }
        }
        std::size_t expanded{};
        for (const auto &clip : a.clips) {
            if (!clip.enabled)
                continue;
            const auto *definition = pattern(a, clip.pattern);
            for (const auto &pt : definition->tracks) {
                const auto repetitions =
                    clip.loop ? static_cast<std::uint64_t>(
                                    1 + (clip.duration - 1) / pt.pattern.length)
                              : 1ULL;
                const auto events =
                    static_cast<std::uint64_t>(pt.pattern.events.size());
                if (events &&
                    repetitions >
                        (limits.expanded_events - expanded) / events) {
                    error = "expanded arrangement event capacity exceeded";
                    return false;
                }
                expanded += static_cast<std::size_t>(repetitions * events);
            }
        }
    }
    error.clear();
    return true;
}

bool materialize_arrangement(const Timeline &source, Timeline &result,
                             std::string &error) {
    if (!validate_arrangement(source, error))
        return false;
    result = source;
    result.arrangement = {};
    const auto &a = source.arrangement;
    if (a.playback_mode == TimelinePlaybackMode::legacy || a.patterns.empty()) {
        error.clear();
        return true;
    }
    for (auto &t : result.tracks)
        t.pattern.events.clear();
    if (a.playback_mode == TimelinePlaybackMode::live_pattern) {
        if (a.chain_running && !a.chain_entries.empty()) {
            Timeline chained = source;
            auto &arrangement = chained.arrangement;
            arrangement.playback_mode = TimelinePlaybackMode::song;
            arrangement.clips.clear();
            arrangement.song_start = 0;
            MusicalTick cursor{};
            ClipId clip_id = 1;
            auto entry_index =
                std::min(a.pattern_chain_position, a.chain_entries.size() - 1);
            std::set<std::size_t> visited;
            for (std::size_t step = 0; step < a.chain_entries.size(); ++step) {
                if (!visited.insert(entry_index).second)
                    break;
                const auto &entry = a.chain_entries[entry_index];
                const auto *definition = pattern(a, entry.pattern);
                const auto repeats = std::max<std::uint16_t>(
                    entry.repeats, definition->follow.after_repetitions);
                for (std::uint16_t repeat = 0; repeat < repeats; ++repeat) {
                    arrangement.clips.push_back({clip_id++, definition->id,
                                                 cursor, definition->length,
                                                 false, 0, 1, true});
                    cursor += definition->length;
                }
                auto next = entry_index + 1;
                const auto random =
                    random_word(source.generation_seed, definition->id, step);
                const auto fires = static_cast<double>(random >> 11U) /
                                       static_cast<double>(1ULL << 53U) <
                                   definition->follow.probability;
                if (fires) {
                    if (definition->follow.type == FollowActionType::stop)
                        break;
                    switch (definition->follow.type) {
                    case FollowActionType::previous:
                        next = entry_index == 0 ? a.chain_entries.size() - 1
                                                : entry_index - 1;
                        break;
                    case FollowActionType::specific: {
                        const auto found = std::find_if(
                            a.chain_entries.begin(), a.chain_entries.end(),
                            [&](const auto &candidate) {
                                return candidate.pattern ==
                                       definition->follow.target;
                            });
                        if (found != a.chain_entries.end())
                            next = static_cast<std::size_t>(
                                found - a.chain_entries.begin());
                        break;
                    }
                    case FollowActionType::random_eligible:
                        next = static_cast<std::size_t>(random %
                                                        a.chain_entries.size());
                        break;
                    case FollowActionType::continue_current:
                        next = entry_index;
                        break;
                    case FollowActionType::next:
                    case FollowActionType::stop:
                        break;
                    }
                }
                if (next >= a.chain_entries.size()) {
                    if (!a.chain_loop)
                        break;
                    next = 0;
                }
                entry_index = next;
            }
            arrangement.song_end = cursor;
            arrangement.loop_enabled = false;
            arrangement.chain_running = false;
            if (cursor <= 0) {
                error = "pattern chain produced no playable content";
                return false;
            }
            return materialize_arrangement(chained, result, error);
        }
        const auto *selected = pattern(a, a.active_pattern);
        if (!selected) {
            error = "live mode has no active pattern";
            return false;
        }
        result.transport.time_signature_numerator =
            selected->time_signature_numerator;
        result.transport.time_signature_denominator =
            selected->time_signature_denominator;
        for (auto &t : result.tracks)
            t.pattern.length = selected->length;
        for (const auto &pt : selected->tracks) {
            auto it =
                std::find_if(result.tracks.begin(), result.tracks.end(),
                             [&](const auto &t) { return t.id == pt.track; });
            it->pattern = pt.pattern;
        }
        error.clear();
        return true;
    }
    const auto range_start = a.loop_enabled ? a.loop_start : a.song_start;
    const auto range_end = a.loop_enabled ? a.loop_end : a.song_end;
    const auto song_length = range_end - range_start;
    for (auto &t : result.tracks)
        t.pattern.length = song_length;
    std::set<EventId> ids;
    for (const auto &clip : a.clips) {
        if (!clip.enabled)
            continue;
        const auto *definition = pattern(a, clip.pattern);
        const auto clip_end = clip.start + clip.duration;
        for (const auto &pt : definition->tracks) {
            auto output =
                std::find_if(result.tracks.begin(), result.tracks.end(),
                             [&](const auto &t) { return t.id == pt.track; });
            for (std::uint64_t repetition = 0;; ++repetition) {
                const auto origin =
                    clip.start +
                    static_cast<MusicalTick>(repetition) * pt.pattern.length;
                if (origin >= clip_end || (!clip.loop && repetition != 0))
                    break;
                for (const auto &source_event : pt.pattern.events) {
                    const auto absolute = origin + source_event.tick;
                    if (absolute < range_start || absolute >= clip_end ||
                        absolute >= range_end)
                        continue;
                    auto event = source_event;
                    event.id = placed_id(clip.id, source_event.id, repetition);
                    if (source_event.target_event_id)
                        event.target_event_id = placed_id(
                            clip.id, source_event.target_event_id, repetition);
                    while (!ids.insert(event.id).second)
                        ++event.id;
                    event.tick = absolute - range_start;
                    if (event.type == MusicalEventType::note ||
                        event.type == MusicalEventType::note_on ||
                        event.type == MusicalEventType::note_off) {
                        const auto pitch = static_cast<int>(event.pitch) +
                                           static_cast<int>(clip.transpose);
                        if (pitch < 0 || pitch > 127) {
                            error = "clip transpose exceeds MIDI pitch range";
                            return false;
                        }
                        event.pitch = static_cast<std::uint8_t>(pitch);
                        event.velocity = std::clamp(
                            event.velocity * clip.velocity_scale, 0.0F, 1.0F);
                    }
                    if (event.type == MusicalEventType::note)
                        event.duration =
                            std::min(event.duration, range_end - absolute);
                    output->pattern.events.push_back(std::move(event));
                }
            }
        }
    }
    if (!a.automation.empty() && result.tracks.empty()) {
        error = "arrangement automation requires a timeline track";
        return false;
    }
    if (!result.tracks.empty())
        for (const auto &source : a.automation)
            if (source.tick >= range_start && source.tick < range_end) {
                auto event = source;
                event.tick -= range_start;
                while (!ids.insert(event.id).second)
                    ++event.id;
                auto destination = result.tracks.begin();
                if (const auto resolved = resolve_parameter_destination(
                        event.parameter_destination);
                    resolved &&
                    resolved->target == ParameterTarget::subtractive_synth)
                    destination =
                        std::find_if(result.tracks.begin(), result.tracks.end(),
                                     [](const auto &candidate) {
                                         return candidate.instrument_id ==
                                                subtractive_instrument_id;
                                     });
                if (destination == result.tracks.end()) {
                    error = "arrangement automation has no compatible track";
                    return false;
                }
                destination->pattern.events.push_back(std::move(event));
            }
    error.clear();
    return true;
}

MusicalTick launch_boundary(MusicalTick now, LaunchQuantization quantization,
                            MusicalTick pattern_length,
                            const TransportMetadata &transport) noexcept {
    if (quantization == LaunchQuantization::immediate)
        return now;
    MusicalTick quantum = ticks_per_quarter;
    if (quantization == LaunchQuantization::bar ||
        quantization == LaunchQuantization::two_bars) {
        quantum = static_cast<MusicalTick>(ticks_per_quarter) *
                  transport.time_signature_numerator * 4 /
                  transport.time_signature_denominator;
        if (quantization == LaunchQuantization::two_bars)
            quantum *= 2;
    } else if (quantization == LaunchQuantization::pattern_end)
        quantum = pattern_length;
    return quantum <= 0 ? now : ((now + quantum - 1) / quantum) * quantum;
}
} // namespace neurogenic
