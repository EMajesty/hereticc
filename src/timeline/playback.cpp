#include "neurogenic/timeline/playback.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
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
           : type == MusicalEventType::note_on   ? 2
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
    return event.id != 0 && event.tick >= 0 && event.pitch <= 127 &&
           std::isfinite(event.velocity) && event.velocity >= 0 &&
           event.velocity <= 1 && std::isfinite(event.value) &&
           (event.type != MusicalEventType::note ||
            (event.duration > 0 &&
             event.duration <=
                 std::numeric_limits<MusicalTick>::max() - event.tick));
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
    plan.tracks.reserve(timeline.tracks.size());
    std::size_t compiled_events{};
    for (const auto &track : timeline.tracks)
        for (const auto &event : track.pattern.events) {
            const auto addition =
                event.type == MusicalEventType::note ? 2U : 1U;
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
                          std::uint32_t parameter_target, float value) {
            const auto length = track.pattern.length;
            plan.events.push_back(
                {raw_tick % length, id, target, type, pitch, parameter,
                 parameter_target, value,
                 static_cast<std::uint64_t>(raw_tick / length)});
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
                append(event.tick, event.id, event.id,
                       MusicalEventType::note_on, event.pitch, 0, 0,
                       event.velocity);
                append(event.tick + event.duration, event.id, event.id,
                       MusicalEventType::note_off, event.pitch, 0, 0, 0);
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
            track.instrument_id != subtractive_instrument_id) {
            error = "unavailable instrument target: " + track.instrument_id;
            return false;
        }
        plan.tracks.push_back({track.id, track.pattern.length, begin,
                               plan.events.size() - begin, 0, 0, track.muted,
                               stable_id(track.instrument_id)});
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
            const auto offset =
                absolute_frame <= start
                    ? 0
                    : static_cast<std::uint32_t>(absolute_frame - start);
            output[count++] = {offset,
                               track.id,
                               event.id,
                               event.target_event_id,
                               track.cursor_cycle - event.first_cycle,
                               event.type,
                               event.pitch,
                               event.parameter_id,
                               event.parameter_target,
                               event.value,
                               track.instrument};
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
