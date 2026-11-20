#include "neurogenic/timeline/io.hpp"
#include "neurogenic/timeline/playback.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace neurogenic {
namespace {
Track &find_track(Timeline &timeline, TrackId id) {
    for (auto &track : timeline.tracks)
        if (track.id == id)
            return track;
    throw std::runtime_error("event references unknown track");
}
PatternDefinition &find_pattern(Timeline &timeline, PatternId id) {
    for (auto &pattern : timeline.arrangement.patterns)
        if (pattern.id == id)
            return pattern;
    throw std::runtime_error("record references unknown pattern");
}
PatternTrack &find_pattern_track(Timeline &timeline, PatternId pattern,
                                 TrackId track) {
    for (auto &value : find_pattern(timeline, pattern).tracks)
        if (value.track == track)
            return value;
    throw std::runtime_error("record references unknown pattern track");
}
void require_end(std::istringstream &line) {
    std::string extra;
    if (line >> extra)
        throw std::runtime_error("unexpected trailing timeline data");
}
} // namespace
Timeline load_timeline(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open timeline: " + path.string());
    std::string magic;
    unsigned version{};
    if (!(input >> magic >> version) || magic != "NEUROGENIC_TIMELINE" ||
        (version == 0 || version > timeline_file_version))
        throw std::runtime_error("unsupported timeline format");
    std::string ignored;
    std::getline(input, ignored);
    Timeline timeline;
    std::string text;
    std::size_t line_number = 1;
    while (std::getline(input, text)) {
        ++line_number;
        std::istringstream line(text);
        std::string kind;
        if (!(line >> kind) || kind.starts_with('#'))
            continue;
        try {
            if (kind == "transport") {
                if (!(line >> timeline.transport.microseconds_per_quarter >>
                      timeline.transport.time_signature_numerator >>
                      timeline.transport.time_signature_denominator))
                    throw std::runtime_error("invalid transport");
                require_end(line);
            } else if (kind == "seed") {
                if (!(line >> timeline.generation_seed))
                    throw std::runtime_error("invalid generation seed");
                require_end(line);
            } else if (kind == "track") {
                Track track;
                unsigned muted{};
                if (!(line >> track.id >> track.pattern.length >> muted >>
                      std::quoted(track.name)) ||
                    muted > 1)
                    throw std::runtime_error("invalid track");
                track.muted = muted != 0;
                if (line >> std::quoted(track.instrument_id)) {
                } else
                    line.clear();
                if (version >= 3 && !(line >> track.instrument_instance_id))
                    throw std::runtime_error("invalid instrument instance");
                if (version >= 4) {
                    unsigned channel{};
                    if (!(line >> track.midi_output_port >> channel) ||
                        channel > 15)
                        throw std::runtime_error("invalid MIDI track routing");
                    track.midi_channel = static_cast<std::uint8_t>(channel);
                }
                require_end(line);
                timeline.tracks.push_back(std::move(track));
            } else if (kind == "arrangement") {
                unsigned mode{}, quantization{}, loop{};
                auto &a = timeline.arrangement;
                if (!(line >> mode >> a.active_pattern >> a.queued_pattern >>
                      quantization >> a.song_start >> a.song_end >> loop >>
                      a.loop_start >> a.loop_end) ||
                    mode > static_cast<unsigned>(TimelinePlaybackMode::song) ||
                    quantization > static_cast<unsigned>(
                                       LaunchQuantization::pattern_end) ||
                    loop > 1)
                    throw std::runtime_error("invalid arrangement");
                a.playback_mode = static_cast<TimelinePlaybackMode>(mode);
                a.launch_quantization =
                    static_cast<LaunchQuantization>(quantization);
                a.loop_enabled = loop != 0;
                if (version >= 6) {
                    unsigned chain_loop{}, chain_running{}, voice_policy{},
                        song_end{};
                    if (!(line >> a.pattern_chain_id >> chain_loop >>
                          chain_running >> voice_policy >> song_end >>
                          a.return_live_pattern) ||
                        chain_loop > 1 || chain_running > 1 ||
                        voice_policy >
                            static_cast<unsigned>(
                                VoiceTransitionPolicy::stop_outgoing) ||
                        song_end > static_cast<unsigned>(
                                       SongEndBehavior::return_to_live))
                        throw std::runtime_error(
                            "invalid arrangement transition settings");
                    a.chain_loop = chain_loop != 0;
                    a.chain_running = chain_running != 0;
                    a.voice_transition =
                        static_cast<VoiceTransitionPolicy>(voice_policy);
                    a.song_end_behavior =
                        static_cast<SongEndBehavior>(song_end);
                }
                require_end(line);
            } else if (kind == "pattern") {
                PatternDefinition pattern;
                if (!(line >> pattern.id >> pattern.length >>
                      pattern.time_signature_numerator >>
                      pattern.time_signature_denominator >>
                      std::quoted(pattern.name)))
                    throw std::runtime_error("invalid pattern");
                if (version >= 6) {
                    unsigned follow{};
                    if (!(line >> follow >> pattern.follow.after_repetitions >>
                          pattern.follow.probability >>
                          pattern.follow.target) ||
                        follow > static_cast<unsigned>(FollowActionType::stop))
                        throw std::runtime_error(
                            "invalid pattern follow action");
                    pattern.follow.type = static_cast<FollowActionType>(follow);
                }
                require_end(line);
                timeline.arrangement.patterns.push_back(std::move(pattern));
            } else if (kind == "pattern_track") {
                PatternId pattern{};
                PatternTrack track;
                if (!(line >> pattern >> track.track >> track.pattern.length))
                    throw std::runtime_error("invalid pattern track");
                require_end(line);
                find_pattern(timeline, pattern)
                    .tracks.push_back(std::move(track));
            } else if (kind == "pattern_event") {
                PatternId pattern{};
                TrackId track{};
                MusicalEvent event;
                unsigned type{}, pitch{}, condition{}, ratchets{};
                if (!(line >> pattern >> track >> event.id >> event.tick >>
                      type >> pitch >> event.velocity >> event.duration >>
                      event.parameter_id >> event.value >>
                      event.target_event_id >>
                      std::quoted(event.parameter_destination) >>
                      event.trigger_probability >> condition >>
                      event.condition_n >> event.condition_m >> ratchets) ||
                    type >
                        static_cast<unsigned>(MusicalEventType::pitch_bend) ||
                    pitch > 127 ||
                    condition > static_cast<unsigned>(
                                    TriggerCondition::fill_inactive) ||
                    ratchets > 255)
                    throw std::runtime_error("invalid pattern event");
                event.type = static_cast<MusicalEventType>(type);
                event.pitch = static_cast<std::uint8_t>(pitch);
                event.trigger_condition =
                    static_cast<TriggerCondition>(condition);
                event.ratchet_count = static_cast<std::uint8_t>(ratchets);
                require_end(line);
                find_pattern_track(timeline, pattern, track)
                    .pattern.events.push_back(std::move(event));
            } else if (kind == "clip") {
                ArrangementClip clip;
                unsigned loop{}, enabled{};
                int transpose{};
                if (!(line >> clip.id >> clip.pattern >> clip.start >>
                      clip.duration >> loop >> transpose >>
                      clip.velocity_scale >> enabled) ||
                    loop > 1 || enabled > 1 || transpose < -128 ||
                    transpose > 127)
                    throw std::runtime_error("invalid clip");
                clip.loop = loop != 0;
                clip.transpose = static_cast<std::int8_t>(transpose);
                clip.enabled = enabled != 0;
                require_end(line);
                timeline.arrangement.clips.push_back(clip);
            } else if (kind == "pattern_chain") {
                PatternId id{};
                if (!(line >> timeline.arrangement.pattern_chain_position))
                    throw std::runtime_error("invalid pattern chain");
                while (line >> id)
                    timeline.arrangement.pattern_chain.push_back(id);
            } else if (kind == "chain_entry") {
                PatternChainEntry entry;
                if (!(line >> entry.pattern >> entry.repeats))
                    throw std::runtime_error("invalid pattern chain entry");
                require_end(line);
                timeline.arrangement.chain_entries.push_back(entry);
            } else if (kind == "arrangement_automation") {
                MusicalEvent event;
                if (version < 7 ||
                    !(line >> event.id >> event.tick >> event.value >>
                      std::quoted(event.parameter_destination)))
                    throw std::runtime_error("invalid arrangement automation");
                event.type = MusicalEventType::parameter;
                require_end(line);
                timeline.arrangement.automation.push_back(std::move(event));
            } else if (kind == "note") {
                TrackId track{};
                unsigned pitch{};
                MusicalEvent event;
                event.type = MusicalEventType::note;
                if (!(line >> track >> event.id >> event.tick >> pitch >>
                      event.velocity >> event.duration) ||
                    pitch > 127)
                    throw std::runtime_error("invalid note");
                event.pitch = static_cast<std::uint8_t>(pitch);
                if (version >= 2) {
                    unsigned condition{}, ratchets{};
                    if (!(line >> event.trigger_probability >> condition >>
                          event.condition_n >> event.condition_m >> ratchets) ||
                        condition > static_cast<unsigned>(
                                        TriggerCondition::fill_inactive) ||
                        ratchets > 255)
                        throw std::runtime_error(
                            "invalid note trigger properties");
                    event.trigger_condition =
                        static_cast<TriggerCondition>(condition);
                    event.ratchet_count = static_cast<std::uint8_t>(ratchets);
                }
                require_end(line);
                find_track(timeline, track).pattern.events.push_back(event);
            } else if (kind == "note_on" || kind == "note_off") {
                TrackId track{};
                unsigned pitch{};
                MusicalEvent event;
                event.type = kind == "note_on" ? MusicalEventType::note_on
                                               : MusicalEventType::note_off;
                if (!(line >> track >> event.id >> event.tick >> pitch >>
                      event.velocity) ||
                    pitch > 127)
                    throw std::runtime_error("invalid note edge");
                if (event.type == MusicalEventType::note_off &&
                    !(line >> event.target_event_id))
                    line.clear();
                event.pitch = static_cast<std::uint8_t>(pitch);
                require_end(line);
                find_track(timeline, track).pattern.events.push_back(event);
            } else if (kind == "parameter") {
                TrackId track{};
                MusicalEvent event;
                event.type = MusicalEventType::parameter;
                if (!(line >> track >> event.id >> event.tick >>
                      event.parameter_id >> event.value))
                    throw std::runtime_error("invalid parameter");
                if (line >> std::quoted(event.parameter_destination)) {
                } else
                    line.clear();
                require_end(line);
                find_track(timeline, track).pattern.events.push_back(event);
            } else if (kind == "cc" || kind == "program" || kind == "bend") {
                TrackId track{};
                MusicalEvent event;
                event.type = kind == "cc" ? MusicalEventType::control_change
                             : kind == "program"
                                 ? MusicalEventType::program_change
                                 : MusicalEventType::pitch_bend;
                if (!(line >> track >> event.id >> event.tick >>
                      event.parameter_id >> event.value))
                    throw std::runtime_error("invalid external MIDI event");
                require_end(line);
                find_track(timeline, track).pattern.events.push_back(event);
            } else
                throw std::runtime_error("unknown record: " + kind);
        } catch (const std::exception &error) {
            throw std::runtime_error("timeline line " +
                                     std::to_string(line_number) + ": " +
                                     error.what());
        }
    }
    for (auto &track : timeline.tracks) {
        std::vector<EventId> targeted;
        for (auto &event : track.pattern.events)
            if (event.type == MusicalEventType::note_off) {
                if (event.target_event_id == 0) {
                    EventId match{};
                    for (const auto &candidate : track.pattern.events)
                        if (candidate.type == MusicalEventType::note_on &&
                            candidate.pitch == event.pitch &&
                            candidate.tick < event.tick &&
                            std::find(targeted.begin(), targeted.end(),
                                      candidate.id) == targeted.end()) {
                            if (match != 0)
                                throw std::runtime_error(
                                    "ambiguous legacy note_off target");
                            match = candidate.id;
                        }
                    if (match == 0)
                        throw std::runtime_error(
                            "legacy note_off has no matching note_on");
                    event.target_event_id = match;
                }
                targeted.push_back(event.target_event_id);
            }
    }
    PlaybackPlan validation;
    std::string error;
    if (!compile_timeline(timeline, validation, error))
        throw std::runtime_error("invalid timeline: " + error);
    return timeline;
}

void save_timeline(const Timeline &timeline,
                   const std::filesystem::path &path) {
    PlaybackPlan validation;
    std::string error;
    if (!compile_timeline(timeline, validation, error))
        throw std::invalid_argument("invalid timeline: " + error);
    std::ofstream output(path);
    if (!output)
        throw std::runtime_error("cannot write timeline: " + path.string());
    output << "NEUROGENIC_TIMELINE " << timeline_file_version << "\ntransport "
           << timeline.transport.microseconds_per_quarter << ' '
           << timeline.transport.time_signature_numerator << ' '
           << timeline.transport.time_signature_denominator << "\nseed "
           << timeline.generation_seed << '\n';
    const auto &a = timeline.arrangement;
    output << "arrangement " << static_cast<unsigned>(a.playback_mode) << ' '
           << a.active_pattern << ' ' << a.queued_pattern << ' '
           << static_cast<unsigned>(a.launch_quantization) << ' '
           << a.song_start << ' ' << a.song_end << ' '
           << (a.loop_enabled ? 1 : 0) << ' ' << a.loop_start << ' '
           << a.loop_end;
    output << ' ' << a.pattern_chain_id << ' ' << (a.chain_loop ? 1 : 0) << ' '
           << (a.chain_running ? 1 : 0) << ' '
           << static_cast<unsigned>(a.voice_transition) << ' '
           << static_cast<unsigned>(a.song_end_behavior) << ' '
           << a.return_live_pattern << '\n';
    output << "pattern_chain " << a.pattern_chain_position;
    for (const auto id : a.pattern_chain)
        output << ' ' << id;
    output << '\n';
    for (const auto &entry : a.chain_entries)
        output << "chain_entry " << entry.pattern << ' ' << entry.repeats
               << '\n';
    for (const auto &event : a.automation)
        output << "arrangement_automation " << event.id << ' ' << event.tick
               << ' ' << event.value << ' '
               << std::quoted(event.parameter_destination) << '\n';
    for (const auto &track : timeline.tracks) {
        output << "track " << track.id << ' ' << track.pattern.length << ' '
               << (track.muted ? 1 : 0) << ' ' << std::quoted(track.name) << ' '
               << std::quoted(track.instrument_id) << ' '
               << track.instrument_instance_id << ' ' << track.midi_output_port
               << ' ' << unsigned(track.midi_channel) << '\n';
        for (const auto &event : track.pattern.events) {
            if (event.type == MusicalEventType::note)
                output << "note " << track.id << ' ' << event.id << ' '
                       << event.tick << ' ' << unsigned(event.pitch) << ' '
                       << event.velocity << ' ' << event.duration << ' '
                       << event.trigger_probability << ' '
                       << static_cast<unsigned>(event.trigger_condition) << ' '
                       << event.condition_n << ' ' << event.condition_m << ' '
                       << unsigned(event.ratchet_count) << '\n';
            else if (event.type == MusicalEventType::note_on ||
                     event.type == MusicalEventType::note_off) {
                output << (event.type == MusicalEventType::note_on
                               ? "note_on "
                               : "note_off ")
                       << track.id << ' ' << event.id << ' ' << event.tick
                       << ' ' << unsigned(event.pitch) << ' ' << event.velocity;
                if (event.type == MusicalEventType::note_off)
                    output << ' ' << event.target_event_id;
                output << '\n';
            } else if (event.type == MusicalEventType::parameter) {
                output << "parameter " << track.id << ' ' << event.id << ' '
                       << event.tick << ' ' << event.parameter_id << ' '
                       << event.value;
                if (!event.parameter_destination.empty())
                    output << ' ' << std::quoted(event.parameter_destination);
                output << '\n';
            } else {
                const auto kind =
                    event.type == MusicalEventType::control_change   ? "cc"
                    : event.type == MusicalEventType::program_change ? "program"
                                                                     : "bend";
                output << kind << ' ' << track.id << ' ' << event.id << ' '
                       << event.tick << ' ' << event.parameter_id << ' '
                       << event.value << '\n';
            }
        }
    }
    for (const auto &pattern : a.patterns) {
        output << "pattern " << pattern.id << ' ' << pattern.length << ' '
               << pattern.time_signature_numerator << ' '
               << pattern.time_signature_denominator << ' '
               << std::quoted(pattern.name) << ' '
               << static_cast<unsigned>(pattern.follow.type) << ' '
               << pattern.follow.after_repetitions << ' '
               << pattern.follow.probability << ' ' << pattern.follow.target
               << '\n';
        for (const auto &track : pattern.tracks) {
            output << "pattern_track " << pattern.id << ' ' << track.track
                   << ' ' << track.pattern.length << '\n';
            for (const auto &event : track.pattern.events)
                output << "pattern_event " << pattern.id << ' ' << track.track
                       << ' ' << event.id << ' ' << event.tick << ' '
                       << static_cast<unsigned>(event.type) << ' '
                       << unsigned(event.pitch) << ' ' << event.velocity << ' '
                       << event.duration << ' ' << event.parameter_id << ' '
                       << event.value << ' ' << event.target_event_id << ' '
                       << std::quoted(event.parameter_destination) << ' '
                       << event.trigger_probability << ' '
                       << static_cast<unsigned>(event.trigger_condition) << ' '
                       << event.condition_n << ' ' << event.condition_m << ' '
                       << unsigned(event.ratchet_count) << '\n';
        }
    }
    for (const auto &clip : a.clips)
        output << "clip " << clip.id << ' ' << clip.pattern << ' ' << clip.start
               << ' ' << clip.duration << ' ' << (clip.loop ? 1 : 0) << ' '
               << static_cast<int>(clip.transpose) << ' ' << clip.velocity_scale
               << ' ' << (clip.enabled ? 1 : 0) << '\n';
}
} // namespace neurogenic
