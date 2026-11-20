#include "neurogenic/timeline/playback.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include "neurogenic/midi/routing.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace neurogenic {
namespace {
#if defined(__SIZEOF_INT128__)
__extension__ using WideInteger = __int128;
#else
#error                                                                         \
    "NEUROGENIC timeline scheduling requires a compiler with 128-bit integer support"
#endif
int type_order(MusicalEventType type) noexcept {
    return type == MusicalEventType::note_off    ? 0
           : type == MusicalEventType::parameter ? 1
           : type == MusicalEventType::control_change ||
                   type == MusicalEventType::program_change ||
                   type == MusicalEventType::pitch_bend
               ? 1
           : type == MusicalEventType::note_on ? 2
                                               : 3;
}
bool scheduled_before(const TimelineScheduledEvent &a,
                      const TimelineScheduledEvent &b) noexcept {
    if (a.frame != b.frame)
        return a.frame < b.frame;
    if (a.track != b.track)
        return a.track < b.track;
    if (type_order(a.type) != type_order(b.type))
        return type_order(a.type) < type_order(b.type);
    if (a.id != b.id)
        return a.id < b.id;
    return a.occurrence < b.occurrence;
}
bool valid_event(const MusicalEvent &event) noexcept {
    const auto midi_value_valid =
        event.type == MusicalEventType::control_change
            ? event.parameter_id <= 127 && event.value >= 0 && event.value <= 1
        : event.type == MusicalEventType::program_change
            ? event.parameter_id <= 127
        : event.type == MusicalEventType::pitch_bend
            ? event.value >= -1 && event.value <= 1
            : true;
    return midi_value_valid && event.id != 0 && event.tick >= 0 &&
           event.pitch <= 127 && std::isfinite(event.velocity) &&
           event.velocity >= 0 && event.velocity <= 1 &&
           std::isfinite(event.value) &&
           std::isfinite(event.trigger_probability) &&
           event.trigger_probability >= 0 && event.trigger_probability <= 1 &&
           event.condition_n > 0 && event.condition_m > 0 &&
           event.condition_n <= event.condition_m && event.ratchet_count > 0 &&
           event.ratchet_count <= 8 &&
           (event.type != MusicalEventType::note ||
            (event.duration >= event.ratchet_count &&
             event.duration <=
                 std::numeric_limits<MusicalTick>::max() - event.tick));
}
std::uint64_t mix(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}
bool probability_fires(std::uint64_t seed, TrackId track, EventId event,
                       std::uint64_t loop, float probability) noexcept {
    if (probability <= 0)
        return false;
    if (probability >= 1)
        return true;
    const auto random = mix(seed ^ mix(track) ^ mix(event) ^ mix(loop));
    // A float is a binary fraction, so scaling it by 2^32 produces an exact
    // integer threshold on every supported IEEE-754 host.
    const auto threshold = static_cast<std::uint64_t>(
        static_cast<double>(probability) * 4'294'967'296.0);
    return (random >> 32U) < threshold;
}
bool condition_fires(TriggerCondition condition, std::uint64_t loop,
                     std::uint16_t n, std::uint16_t m, bool previous,
                     bool fill) noexcept {
    switch (condition) {
    case TriggerCondition::always:
        return true;
    case TriggerCondition::first_loop:
        return loop == 0;
    case TriggerCondition::not_first_loop:
        return loop != 0;
    case TriggerCondition::every_nth_loop:
        return loop % n == static_cast<std::uint64_t>(n - 1U);
    case TriggerCondition::n_of_m:
        return loop % m < n;
    case TriggerCondition::previous_fired:
        return previous;
    case TriggerCondition::previous_not_fired:
        return !previous;
    case TriggerCondition::fill_active:
        return fill;
    case TriggerCondition::fill_inactive:
        return !fill;
    }
    return false;
}
} // namespace

bool compile_timeline(const Timeline &timeline, PlaybackPlan &output,
                      std::string &error) {
    if (timeline.transport.microseconds_per_quarter == 0 ||
        timeline.transport.time_signature_numerator == 0 ||
        timeline.transport.time_signature_denominator == 0) {
        error = "invalid transport metadata";
        return false;
    }
    PlaybackPlan plan;
    plan.transport = timeline.transport;
    plan.generation_seed = timeline.generation_seed;
    plan.tracks.reserve(timeline.tracks.size());
    std::size_t compiled_events{};
    for (const auto &track : timeline.tracks)
        for (const auto &event : track.pattern.events) {
            const auto addition =
                event.type == MusicalEventType::note
                    ? static_cast<unsigned>(event.ratchet_count) * 2U
                    : 1U;
            if (addition >
                std::numeric_limits<std::size_t>::max() - compiled_events) {
                error = "timeline is too large to compile";
                return false;
            }
            compiled_events += addition;
        }
    plan.events.reserve(compiled_events);
    plan.scheduler_heap.reserve(timeline.tracks.size());
    for (const auto &track : timeline.tracks) {
        if (track.id == 0 || track.pattern.length <= 0) {
            error = "track identifier and pattern length must be positive";
            return false;
        }
        for (const auto &compiled : plan.tracks)
            if (compiled.id == track.id) {
                error = "duplicate track identifier";
                return false;
            }
        const auto begin = plan.events.size();
        auto append = [&](MusicalTick raw_tick, EventId id, EventId target,
                          MusicalEventType type, std::uint8_t pitch,
                          std::uint32_t parameter,
                          std::uint32_t parameter_target, float value,
                          float probability = 1.0F,
                          TriggerCondition condition = TriggerCondition::always,
                          std::uint16_t n = 1, std::uint16_t m = 1,
                          std::uint8_t ratchet = 0) {
            const auto length = track.pattern.length;
            plan.events.push_back(
                {raw_tick % length, id, target, type, pitch, parameter,
                 parameter_target, value,
                 static_cast<std::uint64_t>(raw_tick / length), probability,
                 condition, n, m, ratchet});
        };
        for (std::size_t index = 0; index < track.pattern.events.size();
             ++index) {
            const auto &event = track.pattern.events[index];
            if (!valid_event(event) || event.tick >= track.pattern.length) {
                error = "invalid musical event";
                return false;
            }
            for (std::size_t previous = 0; previous < index; ++previous)
                if (track.pattern.events[previous].id == event.id) {
                    error = "duplicate event identifier";
                    return false;
                }
            if (event.type == MusicalEventType::note) {
                for (std::uint8_t ratchet = 0; ratchet < event.ratchet_count;
                     ++ratchet) {
                    const auto begin = event.tick + event.duration * ratchet /
                                                        event.ratchet_count;
                    const auto end = event.tick + event.duration *
                                                      (ratchet + 1) /
                                                      event.ratchet_count;
                    append(begin, event.id, event.id, MusicalEventType::note_on,
                           event.pitch, 0, 0, event.velocity,
                           event.trigger_probability, event.trigger_condition,
                           event.condition_n, event.condition_m, ratchet);
                    append(end, event.id, event.id, MusicalEventType::note_off,
                           event.pitch, 0, 0, 0, 1.0F, TriggerCondition::always,
                           1, 1, ratchet);
                }
            } else {
                if (event.type == MusicalEventType::note_off) {
                    if (event.target_event_id == 0) {
                        error = "explicit note-off requires target_event_id";
                        return false;
                    }
                    bool target_found = false;
                    for (const auto &candidate : track.pattern.events)
                        if (candidate.id == event.target_event_id &&
                            candidate.type == MusicalEventType::note_on &&
                            candidate.pitch == event.pitch &&
                            candidate.tick < event.tick) {
                            target_found = true;
                            break;
                        }
                    if (!target_found) {
                        error =
                            "explicit note-off target does not name a note-on";
                        return false;
                    }
                }
                std::uint32_t parameter_target{};
                auto parameter = event.parameter_id;
                if (event.type == MusicalEventType::parameter) {
                    if (event.parameter_destination.empty()) {
                        const auto legacy_target =
                            track.instrument_id == subtractive_instrument_id
                                ? ParameterTarget::subtractive_synth
                                : ParameterTarget::none;
                        parameter_target =
                            static_cast<std::uint32_t>(legacy_target);
                    } else {
                        const auto destination = resolve_parameter_destination(
                            event.parameter_destination);
                        if (!destination ||
                            (destination->target ==
                                 ParameterTarget::subtractive_synth &&
                             track.instrument_id !=
                                 subtractive_instrument_id)) {
                            error = "unknown or unroutable parameter "
                                    "destination: " +
                                    event.parameter_destination;
                            return false;
                        }
                        parameter = destination->parameter;
                        parameter_target =
                            static_cast<std::uint32_t>(destination->target);
                    }
                }
                append(event.tick, event.id,
                       event.type == MusicalEventType::note_off
                           ? event.target_event_id
                           : event.id,
                       event.type, event.pitch, parameter, parameter_target,
                       event.type == MusicalEventType::parameter
                           ? event.value
                           : event.velocity);
            }
        }
        std::sort(plan.events.begin() + static_cast<std::ptrdiff_t>(begin),
                  plan.events.end(), [](const auto &a, const auto &b) {
                      if (a.tick != b.tick)
                          return a.tick < b.tick;
                      if (type_order(a.type) != type_order(b.type))
                          return type_order(a.type) < type_order(b.type);
                      return a.id < b.id;
                  });
        if (track.instrument_id != sample_instrument_id &&
            track.instrument_id != subtractive_instrument_id &&
            track.instrument_id != external_midi_instrument_id) {
            error = "unavailable instrument target: " + track.instrument_id;
            return false;
        }
        if (track.instrument_id == subtractive_instrument_id &&
            (track.instrument_instance_id == 0 ||
             track.instrument_instance_id > maximum_instrument_instances)) {
            error = "synth instrument instance is out of range";
            return false;
        }
        if (track.instrument_id == external_midi_instrument_id &&
            (track.midi_output_port == 0 ||
             track.midi_output_port > maximum_midi_ports ||
             track.midi_channel > 15)) {
            error = "external MIDI track routing is out of range";
            return false;
        }
        plan.tracks.push_back({track.id, track.pattern.length, begin,
                               plan.events.size() - begin, 0, 0, track.muted,
                               stable_id(track.instrument_id),
                               track.instrument_instance_id, false, 0, 0, false,
                               track.midi_output_port, track.midi_channel});
    }
    output = std::move(plan);
    error.clear();
    return true;
}

std::uint64_t
TimelineScheduler::frame_at_tick(MusicalTick tick) const noexcept {
    if (tick <= 0 || plan_ == nullptr)
        return 0;
    const auto numerator = static_cast<WideInteger>(tick) * sample_rate_ *
                           plan_->transport.microseconds_per_quarter;
    const auto denominator =
        static_cast<WideInteger>(ticks_per_quarter) * 1'000'000;
    const auto frame = (numerator + denominator / 2) / denominator;
    return frame > std::numeric_limits<std::uint64_t>::max()
               ? std::numeric_limits<std::uint64_t>::max()
               : static_cast<std::uint64_t>(frame);
}

std::uint64_t TimelineScheduler::frame_for(const PlaybackTrack &track,
                                           const PlaybackEvent &event,
                                           std::uint64_t cycle) const noexcept {
    const auto tick =
        static_cast<WideInteger>(cycle) * track.length + event.tick;
    const auto numerator =
        tick * sample_rate_ * plan_->transport.microseconds_per_quarter;
    const auto denominator =
        static_cast<WideInteger>(ticks_per_quarter) * 1'000'000;
    const auto frame = (numerator + denominator / 2) / denominator;
    return frame > std::numeric_limits<std::uint64_t>::max()
               ? std::numeric_limits<std::uint64_t>::max()
               : static_cast<std::uint64_t>(frame);
}

void TimelineScheduler::advance(PlaybackTrack &track) noexcept {
    if (++track.cursor_index == track.event_count) {
        track.cursor_index = 0;
        if (track.cursor_cycle < std::numeric_limits<std::uint64_t>::max())
            ++track.cursor_cycle;
    }
}

bool TimelineScheduler::track_after(std::size_t left,
                                    std::size_t right) const noexcept {
    const auto &lt = plan_->tracks[left];
    const auto &rt = plan_->tracks[right];
    const auto &le = plan_->events[lt.event_begin + lt.cursor_index];
    const auto &re = plan_->events[rt.event_begin + rt.cursor_index];
    const auto lf = frame_for(lt, le, lt.cursor_cycle),
               rf = frame_for(rt, re, rt.cursor_cycle);
    if (lf != rf)
        return lf > rf;
    if (lt.id != rt.id)
        return lt.id > rt.id;
    if (type_order(le.type) != type_order(re.type))
        return type_order(le.type) > type_order(re.type);
    return le.id > re.id;
}

void TimelineScheduler::seek(std::uint64_t start) noexcept {
    if (plan_ == nullptr)
        return;
    plan_->scheduler_heap.clear();
    const auto approximate_tick =
        (static_cast<WideInteger>(start) * ticks_per_quarter * 1'000'000) /
        (static_cast<WideInteger>(sample_rate_) *
         plan_->transport.microseconds_per_quarter);
    for (std::size_t index = 0; index < plan_->tracks.size(); ++index) {
        auto &track = plan_->tracks[index];
        track.previous_trigger_fired = false;
        track.decision_event = 0;
        if (track.muted || track.event_count == 0)
            continue;
        const auto cycle = approximate_tick / track.length;
        track.cursor_cycle = cycle > std::numeric_limits<std::uint64_t>::max()
                                 ? std::numeric_limits<std::uint64_t>::max()
                                 : static_cast<std::uint64_t>(cycle);
        const auto phase =
            static_cast<MusicalTick>(approximate_tick % track.length);
        const auto first = plan_->events.begin() +
                           static_cast<std::ptrdiff_t>(track.event_begin);
        const auto last =
            first + static_cast<std::ptrdiff_t>(track.event_count);
        track.cursor_index = static_cast<std::size_t>(
            std::lower_bound(first, last, phase,
                             [](const PlaybackEvent &event, MusicalTick tick) {
                                 return event.tick < tick;
                             }) -
            first);
        if (track.cursor_index == track.event_count) {
            track.cursor_index = 0;
            if (track.cursor_cycle < std::numeric_limits<std::uint64_t>::max())
                ++track.cursor_cycle;
        }
        while (frame_for(track,
                         plan_->events[track.event_begin + track.cursor_index],
                         track.cursor_cycle) < start)
            advance(track);
        plan_->scheduler_heap.push_back(index);
    }
    auto after = [this](std::size_t a, std::size_t b) {
        return track_after(a, b);
    };
    std::make_heap(plan_->scheduler_heap.begin(), plan_->scheduler_heap.end(),
                   after);
    expected_frame_ = start;
}

void TimelineScheduler::set_plan(PlaybackPlan *plan,
                                 std::uint64_t start_frame) noexcept {
    plan_ = plan;
    seek(start_frame);
}

std::size_t
TimelineScheduler::schedule(std::uint64_t start, std::uint32_t frames,
                            std::span<TimelineScheduledEvent> output) noexcept {
    if (plan_ == nullptr)
        return 0;
    if (start != expected_frame_)
        seek(start);
    const auto end = frames > std::numeric_limits<std::uint64_t>::max() - start
                         ? std::numeric_limits<std::uint64_t>::max()
                         : start + frames;
    std::size_t count{};
    auto after = [this](std::size_t a, std::size_t b) {
        return track_after(a, b);
    };
    while (!plan_->scheduler_heap.empty()) {
        const auto track_index = plan_->scheduler_heap.front();
        auto &track = plan_->tracks[track_index];
        const auto &event =
            plan_->events[track.event_begin + track.cursor_index];
        const auto absolute_frame = frame_for(track, event, track.cursor_cycle);
        if (absolute_frame >= end)
            break;
        if (count == output.size()) {
            ++dropped_events_;
            break;
        }
        const auto valid_occurrence = track.cursor_cycle >= event.first_cycle;
        if (valid_occurrence) {
            const auto occurrence = track.cursor_cycle - event.first_cycle;
            bool emit = true;
            if (event.type == MusicalEventType::note_on) {
                if (event.ratchet_index != 0 &&
                    track.decision_event == event.id &&
                    track.decision_occurrence == occurrence)
                    emit = track.decision_fired;
                else {
                    emit = condition_fires(event.trigger_condition, occurrence,
                                           event.condition_n, event.condition_m,
                                           track.previous_trigger_fired,
                                           fill_active_) &&
                           probability_fires(plan_->generation_seed, track.id,
                                             event.id, occurrence,
                                             event.trigger_probability);
                    track.decision_event = event.id;
                    track.decision_occurrence = occurrence;
                    track.decision_fired = emit;
                    track.previous_trigger_fired = emit;
                }
            }
            const auto offset =
                absolute_frame <= start
                    ? 0
                    : static_cast<std::uint32_t>(absolute_frame - start);
            if (emit)
                output[count++] = {offset,
                                   track.id,
                                   event.id,
                                   event.target_event_id,
                                   occurrence * 8U + event.ratchet_index,
                                   event.type,
                                   event.pitch,
                                   event.parameter_id,
                                   event.parameter_target,
                                   event.value,
                                   track.instrument,
                                   track.instrument_instance,
                                   track.midi_output_port,
                                   track.midi_channel};
        }
        std::pop_heap(plan_->scheduler_heap.begin(),
                      plan_->scheduler_heap.end(), after);
        advance(track);
        std::push_heap(plan_->scheduler_heap.begin(),
                       plan_->scheduler_heap.end(), after);
    }
    std::sort(output.begin(),
              output.begin() + static_cast<std::ptrdiff_t>(count),
              scheduled_before);
    expected_frame_ = end;
    return count;
}
} // namespace neurogenic
