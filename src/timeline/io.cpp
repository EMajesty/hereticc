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
                if (version >= 3 &&
                    !(line >> track.instrument_instance_id))
                    throw std::runtime_error("invalid instrument instance");
                require_end(line);
                timeline.tracks.push_back(std::move(track));
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
    for (const auto &track : timeline.tracks) {
        output << "track " << track.id << ' ' << track.pattern.length << ' '
               << (track.muted ? 1 : 0) << ' ' << std::quoted(track.name) << ' '
               << std::quoted(track.instrument_id) << ' '
               << track.instrument_instance_id << '\n';
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
            } else {
                output << "parameter " << track.id << ' ' << event.id << ' '
                       << event.tick << ' ' << event.parameter_id << ' '
                       << event.value;
                if (!event.parameter_destination.empty())
                    output << ' ' << std::quoted(event.parameter_destination);
                output << '\n';
            }
        }
    }
}
} // namespace neurogenic
