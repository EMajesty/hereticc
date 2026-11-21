#include "neurogenic/timeline/arrangement_recording.hpp"
#include "neurogenic/timeline/arrangement.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace neurogenic {
namespace {
const PatternDefinition *find_pattern(const Timeline &timeline, PatternId id) {
    const auto found =
        std::find_if(timeline.arrangement.patterns.begin(),
                     timeline.arrangement.patterns.end(),
                     [=](const auto &pattern) { return pattern.id == id; });
    return found == timeline.arrangement.patterns.end() ? nullptr : &*found;
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

bool ArrangementRecordingService::arm(bool armed, std::string &error) {
    if (status_.phase == ArrangementRecordPhase::recording) {
        error = "cannot change arm state while recording";
        return false;
    }
    status_.armed = armed;
    status_.phase =
        armed ? ArrangementRecordPhase::armed : ArrangementRecordPhase::idle;
    status_.error.clear();
    error.clear();
    return true;
}
bool ArrangementRecordingService::arm_automation(bool armed,
                                                 std::string &error) {
    if (status_.phase == ArrangementRecordPhase::recording) {
        status_.automation_armed = armed;
        error.clear();
        return true;
    }
    status_.automation_armed = armed;
    error.clear();
    return true;
}
bool ArrangementRecordingService::start(MusicalTick tick,
                                        ArrangementRecordMode mode,
                                        MusicalTick count_in, bool automation,
                                        std::string &error) {
    if (!status_.armed || tick < 0 || count_in < 0) {
        error = !status_.armed ? "arrangement recording is not armed"
                               : "invalid recording position or count-in";
        return false;
    }
    patterns_.clear();
    automation_.clear();
    performance_launches_.clear();
    status_ = {ArrangementRecordPhase::recording,
               mode,
               true,
               automation,
               tick + count_in,
               0,
               count_in,
               0,
               0,
               0,
               0,
               {}};
    error.clear();
    return true;
}
bool ArrangementRecordingService::reserve_event() noexcept {
    if (patterns_.size() + automation_.size() + performance_launches_.size() < capacity_)
        return true;
    ++status_.overflow_count;
    status_.phase = ArrangementRecordPhase::overflow;
    status_.error = "arrangement capture capacity exceeded";
    return false;
}
bool ArrangementRecordingService::capture_pattern(PatternId pattern,
                                                  MusicalTick tick) noexcept {
    if (status_.phase != ArrangementRecordPhase::recording || !pattern ||
        tick < status_.start_tick)
        return false;
    if (!patterns_.empty() && patterns_.back().pattern == pattern)
        return true;
    if (!reserve_event())
        return false;
    patterns_.push_back({pattern, tick});
    status_.captured_patterns = patterns_.size();
    return true;
}
bool ArrangementRecordingService::capture_chain(const Timeline &timeline,
                                                MusicalTick start) noexcept {
    const auto &a = timeline.arrangement;
    if (!a.chain_running || a.chain_entries.empty())
        return false;
    auto index = std::min(a.pattern_chain_position, a.chain_entries.size() - 1);
    std::vector<bool> visited(a.chain_entries.size());
    auto tick = start;
    for (std::size_t step = 0; step < a.chain_entries.size(); ++step) {
        if (visited[index])
            break;
        visited[index] = true;
        const auto &entry = a.chain_entries[index];
        const auto *pattern = find_pattern(timeline, entry.pattern);
        if (!pattern)
            return false;
        const auto repeats =
            std::max(entry.repeats, pattern->follow.after_repetitions);
        for (std::uint16_t repeat = 0; repeat < repeats; ++repeat) {
            if (!capture_pattern(pattern->id, tick))
                return false;
            // Repeated identical launches are intentionally represented by one
            // clip; its duration is closed by the next distinct launch/stop.
            tick += pattern->length;
        }
        auto next = index + 1;
        const auto random =
            random_word(timeline.generation_seed, pattern->id, step);
        const auto fires = static_cast<double>(random >> 11U) /
                               static_cast<double>(1ULL << 53U) <
                           pattern->follow.probability;
        if (fires && pattern->follow.type == FollowActionType::stop)
            break;
        if (fires)
            switch (pattern->follow.type) {
            case FollowActionType::previous:
                next = index ? index - 1 : a.chain_entries.size() - 1;
                break;
            case FollowActionType::specific: {
                const auto found = std::find_if(
                    a.chain_entries.begin(), a.chain_entries.end(),
                    [&](const auto &candidate) {
                        return candidate.pattern == pattern->follow.target;
                    });
                if (found != a.chain_entries.end())
                    next = static_cast<std::size_t>(found -
                                                    a.chain_entries.begin());
                break;
            }
            case FollowActionType::random_eligible:
                next = random % a.chain_entries.size();
                break;
            case FollowActionType::continue_current:
                next = index;
                break;
            case FollowActionType::next:
            case FollowActionType::stop:
                break;
            }
        if (next >= a.chain_entries.size()) {
            if (!a.chain_loop)
                break;
            next = 0;
        }
        index = next;
    }
    return true;
}
bool ArrangementRecordingService::capture_automation(
    std::string id, float value, MusicalTick tick,
    MusicalTick thinning) noexcept {
    if (status_.phase != ArrangementRecordPhase::recording ||
        !status_.automation_armed || id.empty() || !std::isfinite(value) ||
        tick < status_.start_tick)
        return false;
    const auto previous = std::find_if(
        automation_.rbegin(), automation_.rend(),
        [&](const auto &event) { return event.parameter_id == id; });
    if (previous != automation_.rend() && tick - previous->tick < thinning) {
        previous->value = value;
        return true;
    }
    if (!reserve_event())
        return false;
    automation_.push_back({std::move(id), value, tick});
    status_.captured_automation = automation_.size();
    return true;
}
bool ArrangementRecordingService::capture_performance(
    const PreparedLaunch &launch, std::uint64_t actual_frame,
    PerformanceSceneId scene, bool follow_transition, float velocity) noexcept {
    if (status_.phase != ArrangementRecordPhase::recording ||
        launch.activation_tick < status_.start_tick || !launch.slot ||
        !launch.track || !std::isfinite(velocity))
        return false;
    if (!reserve_event()) return false;
    performance_launches_.push_back(
        {launch.slot, launch.track, launch.operation, launch.source_type,
         launch.source_id, launch.activation_tick, actual_frame, scene,
         follow_transition, std::clamp(velocity, 0.0F, 1.0F)});
    status_.captured_performance_launches = performance_launches_.size();
    return true;
}
bool ArrangementRecordingService::stop(MusicalTick tick, std::string &error) {
    if (status_.phase != ArrangementRecordPhase::recording ||
        tick < status_.start_tick) {
        error = "arrangement recording is not active";
        return false;
    }
    status_.stop_tick = tick;
    status_.phase = ArrangementRecordPhase::pending;
    error.clear();
    return true;
}
void ArrangementRecordingService::report_overflow(
    std::uint64_t count) noexcept {
    if (status_.phase != ArrangementRecordPhase::recording)
        return;
    status_.overflow_count += count;
    status_.phase = ArrangementRecordPhase::overflow;
    status_.error = "arrangement capture queue overflow";
}
bool ArrangementRecordingService::commit(TimelineService &service,
                                         std::string &error) {
    if (status_.phase != ArrangementRecordPhase::pending) {
        error = status_.phase == ArrangementRecordPhase::overflow
                    ? status_.error
                    : "no completed arrangement recording is pending";
        return false;
    }
    auto transaction = service.edit();
    auto &arrangement = transaction.timeline().arrangement;
    const auto source_audio_clips = arrangement.audio_clips;
    arrangement.song_start = std::min(arrangement.song_start, status_.start_tick);
    arrangement.song_end = std::max(arrangement.song_end, status_.stop_tick);
    if (arrangement.loop_enabled) {
        arrangement.loop_start =
            std::min(arrangement.loop_start, status_.start_tick);
        arrangement.loop_end =
            std::max(arrangement.loop_end, status_.stop_tick);
    }
    if (status_.mode == ArrangementRecordMode::replace) {
        std::erase_if(arrangement.clips, [&](const auto &clip) {
            return clip.start < status_.stop_tick &&
                   clip.start + clip.duration > status_.start_tick;
        });
        std::erase_if(arrangement.audio_clips, [&](const auto &clip) {
            return clip.start < status_.stop_tick &&
                   clip.start + clip.duration > status_.start_tick;
        });
        std::erase_if(arrangement.audio_crossfades, [&](const auto &crossfade) {
            const auto exists = [&](AudioClipId id) {
                return std::any_of(arrangement.audio_clips.begin(),
                                   arrangement.audio_clips.end(),
                                   [=](const auto &clip) { return clip.id == id; });
            };
            return !exists(crossfade.outgoing_clip) ||
                   !exists(crossfade.incoming_clip);
        });
    }
    ClipId next_clip = 1;
    for (const auto &clip : arrangement.clips)
        next_clip = std::max(next_clip, clip.id + 1);
    for (std::size_t i = 0; i < patterns_.size(); ++i) {
        const auto end = i + 1 < patterns_.size() ? patterns_[i + 1].tick
                                                  : status_.stop_tick;
        if (end <= patterns_[i].tick ||
            !find_pattern(transaction.timeline(), patterns_[i].pattern))
            continue;
        arrangement.clips.push_back(
            {next_clip++, patterns_[i].pattern, patterns_[i].tick,
             end - patterns_[i].tick, true, 0, 1.0F, true});
    }
    AudioClipId next_audio = 1;
    for (const auto &clip : arrangement.audio_clips)
        next_audio = std::max(next_audio, clip.id + 1);
    for (const auto &clip : source_audio_clips)
        next_audio = std::max(next_audio, clip.id + 1);
    std::set<AudioClipId> reused_audio_sources;
    for (std::size_t i = 0; i < performance_launches_.size(); ++i) {
        const auto &captured = performance_launches_[i];
        if (captured.operation == PreparedLaunchOperation::stop) continue;
        auto end = status_.stop_tick;
        for (std::size_t j = i + 1; j < performance_launches_.size(); ++j)
            if (performance_launches_[j].track == captured.track) {
                end = performance_launches_[j].tick;
                break;
            }
        if (end <= captured.tick) continue;
        if (captured.source_type == LaunchSourceType::pattern) {
            if (!find_pattern(transaction.timeline(), captured.source_id)) continue;
            arrangement.clips.push_back(
                {next_clip++, captured.source_id, captured.tick,
                 end - captured.tick, true, 0, captured.velocity, true});
        } else {
            const auto source = std::find_if(
                source_audio_clips.begin(), source_audio_clips.end(),
                [&](const auto &clip) { return clip.id == captured.source_id; });
            if (source == source_audio_clips.end()) continue;
            auto clip = *source;
            const auto source_survives = std::any_of(
                arrangement.audio_clips.begin(), arrangement.audio_clips.end(),
                [&](const auto &existing) { return existing.id == source->id; });
            if (!source_survives && reused_audio_sources.insert(source->id).second)
                clip.id = source->id;
            else
                clip.id = next_audio++;
            clip.track = captured.track;
            clip.start = captured.tick;
            clip.duration = end - captured.tick;
            clip.enabled = true;
            arrangement.audio_clips.push_back(std::move(clip));
        }
    }
    if (status_.mode == ArrangementRecordMode::replace)
        std::erase_if(arrangement.automation, [&](const auto &event) {
            return event.tick >= status_.start_tick &&
                   event.tick < status_.stop_tick;
        });
    EventId next_event = 1;
    for (const auto &event : arrangement.automation)
        next_event = std::max(next_event, event.id + 1);
    for (const auto &captured : automation_) {
        MusicalEvent event;
        event.id = next_event++;
        event.tick = captured.tick;
        event.type = MusicalEventType::parameter;
        event.parameter_destination = captured.parameter_id;
        event.value = captured.value;
        arrangement.automation.push_back(std::move(event));
    }
    arrangement.playback_mode = TimelinePlaybackMode::song;
    if (!service.commit(std::move(transaction),
                        TimelineCommitPolicy::next_audio_callback, error)) {
        status_.phase = ArrangementRecordPhase::conflict;
        status_.error = error;
        return false;
    }
    status_.phase = ArrangementRecordPhase::committed;
    status_.armed = false;
    error.clear();
    return true;
}
void ArrangementRecordingService::discard() noexcept {
    patterns_.clear();
    automation_.clear();
    performance_launches_.clear();
    status_ = {};
    status_.phase = ArrangementRecordPhase::discarded;
}
ArrangementRecordingStatus ArrangementRecordingService::status() const {
    return status_;
}
} // namespace neurogenic
