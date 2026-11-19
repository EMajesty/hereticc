#include "neurogenic/audio/slicing.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

namespace neurogenic {
const char *sample_playback_mode_name(SamplePlaybackMode mode) noexcept {
    switch (mode) {
    case SamplePlaybackMode::one_shot:
        return "one-shot";
    case SamplePlaybackMode::gate:
        return "gate";
    case SamplePlaybackMode::toggle:
        return "toggle";
    }
    return "unknown";
}
std::optional<SamplePlaybackMode>
parse_sample_playback_mode(std::string_view value) noexcept {
    if (value == "one-shot")
        return SamplePlaybackMode::one_shot;
    if (value == "gate")
        return SamplePlaybackMode::gate;
    if (value == "toggle")
        return SamplePlaybackMode::toggle;
    return std::nullopt;
}
const char *stretch_mode_name(StretchMode mode) noexcept {
    return mode == StretchMode::realtime ? "Realtime" : "Off";
}
std::optional<StretchMode> parse_stretch_mode(std::string_view value) noexcept {
    if (value == "off" || value == "Off")
        return StretchMode::off;
    if (value == "realtime" || value == "Realtime")
        return StretchMode::realtime;
    return std::nullopt;
}
const char *stretch_quality_name(StretchQuality quality) noexcept {
    switch (quality) {
    case StretchQuality::economy:
        return "Economy";
    case StretchQuality::smooth:
        return "Smooth";
    default:
        return "Balanced";
    }
}
std::optional<StretchQuality>
parse_stretch_quality(std::string_view value) noexcept {
    if (value == "economy" || value == "Economy")
        return StretchQuality::economy;
    if (value == "balanced" || value == "Balanced")
        return StretchQuality::balanced;
    if (value == "smooth" || value == "Smooth")
        return StretchQuality::smooth;
    return std::nullopt;
}
std::vector<WaveformPeak> build_waveform_peaks(const Sample &sample,
                                               std::size_t count) {
    if ((sample.channels != 1 && sample.channels != 2) ||
        sample.interleaved.size() % sample.channels != 0)
        throw std::invalid_argument("waveform sample is invalid");
    count = std::min(count, sample.frames());
    std::vector<WaveformPeak> result(count);
    if (count == 0)
        return result;
    for (std::size_t bucket = 0; bucket < count; ++bucket) {
        const auto begin = bucket * sample.frames() / count;
        const auto end =
            std::max(begin + 1, (bucket + 1) * sample.frames() / count);
        auto &peak = result[bucket];
        peak.left_min = peak.right_min = 1.0F;
        peak.left_max = peak.right_max = -1.0F;
        for (auto frame = begin; frame < end; ++frame) {
            const auto left = sample.interleaved[frame * sample.channels];
            const auto right =
                sample.interleaved[frame * sample.channels +
                                   (sample.channels > 1 ? 1 : 0)];
            peak.left_min = std::min(peak.left_min, left);
            peak.left_max = std::max(peak.left_max, left);
            peak.right_min = std::min(peak.right_min, right);
            peak.right_max = std::max(peak.right_max, right);
        }
    }
    return result;
}
std::vector<std::uint64_t> detect_transient_frames(const Sample &sample,
                                                   float sensitivity,
                                                   std::uint64_t separation) {
    if (!std::isfinite(sensitivity) || sensitivity <= 0 || sensitivity > 1 ||
        separation == 0 || sample.frames() == 0 ||
        (sample.channels != 1 && sample.channels != 2) ||
        sample.interleaved.size() % sample.channels != 0)
        throw std::invalid_argument("invalid transient detection settings");
    std::vector<float> flux(sample.frames());
    float maximum{}, previous{};
    for (std::size_t frame = 0; frame < sample.frames(); ++frame) {
        float amplitude{};
        for (std::uint16_t channel = 0; channel < sample.channels; ++channel)
            amplitude +=
                std::abs(sample.interleaved[frame * sample.channels + channel]);
        amplitude /= sample.channels;
        flux[frame] = std::max(0.0F, amplitude - previous);
        maximum = std::max(maximum, flux[frame]);
        previous = amplitude;
    }
    std::vector<std::uint64_t> starts{0};
    const auto threshold = maximum * (0.15F + 0.75F * sensitivity);
    for (std::uint64_t frame = 1; frame + 1 < sample.frames(); ++frame)
        if (flux[frame] >= threshold && flux[frame] >= flux[frame - 1] &&
            flux[frame] >= flux[frame + 1] &&
            frame - starts.back() >= separation)
            starts.push_back(frame);
    return starts;
}
namespace {
bool valid_playback_settings(const SamplePlaybackSettings &value) {
    return value.choke_group <= 16 && std::isfinite(value.semitones) &&
           value.semitones >= -48 && value.semitones <= 48 &&
           std::isfinite(value.fine_cents) && value.fine_cents >= -100 &&
           value.fine_cents <= 100 && std::isfinite(value.gain) &&
           value.gain >= 0 && value.gain <= 4 && value.root_note >= -1 &&
           value.root_note <= 127 && std::isfinite(value.attack_seconds) &&
           value.attack_seconds >= 0 && value.attack_seconds <= 30 &&
           std::isfinite(value.decay_seconds) && value.decay_seconds >= 0 &&
           value.decay_seconds <= 30 && std::isfinite(value.sustain) &&
           value.sustain >= 0 && value.sustain <= 1 &&
           std::isfinite(value.release_seconds) && value.release_seconds >= 0 &&
           value.release_seconds <= 30 && std::isfinite(value.stretch_ratio) &&
           value.stretch_ratio >= 0.25F && value.stretch_ratio <= 4.0F &&
           std::isfinite(value.source_beats) && value.source_beats >= 0 &&
           value.source_beats <= 1024 && std::isfinite(value.source_bpm) &&
           value.source_bpm >= 0 && value.source_bpm <= 1000;
}
} // namespace

SliceService::SliceService(AudioEngine &engine, SampleManager &samples)
    : engine_(engine), samples_(samples) {}

void SliceService::synchronize_locked(SampleSlotId id) {
    const auto status = samples_.status(id);
    auto &slot = slots_[id.value];
    if (status.active_revision == slot.sample_revision &&
        status.active_publication == slot.sample_publication)
        return;
    for (std::size_t pad = 0; pad < pads_.size(); ++pad)
        if (pads_[pad] && std::any_of(slot.slices.begin(), slot.slices.end(),
                                      [&](const auto &slice) {
                                          return slice.id == *pads_[pad];
                                      })) {
            (void)engine_.clear_note_slice(static_cast<std::uint8_t>(36 + pad));
            pads_[pad].reset();
        }
    slot.slices.clear();
    slot.waveform.clear();
    slot.sample_revision = status.active_revision;
    slot.sample_publication = status.active_publication;
    slot.frames = status.frames;
    slot.rate = status.engine_sample_rate;
    slot.channels = status.channels;
    if (const auto sample = samples_.active_sample(id))
        slot.waveform = build_waveform_peaks(*sample);
    ++slot.revision;
}

SliceSnapshot SliceService::snapshot(SampleSlotId id) {
    std::lock_guard lock(mutex_);
    synchronize_locked(id);
    const auto &slot = slots_[id.value];
    return {slot.revision,
            id,
            0,
            slot.sample_revision,
            slot.sample_publication,
            slot.frames,
            slot.rate,
            slot.channels,
            slot.slices,
            pads_,
            pad_settings_,
            slot.waveform};
}

SampleSlice *SliceService::find_locked(SliceId id) {
    for (auto &[slot_id, slot] : slots_) {
        (void)slot_id;
        const auto found =
            std::find_if(slot.slices.begin(), slot.slices.end(),
                         [&](const auto &value) { return value.id == id; });
        if (found != slot.slices.end())
            return &*found;
    }
    return nullptr;
}
const SampleSlice *SliceService::find_locked(SliceId id) const {
    return const_cast<SliceService *>(this)->find_locked(id);
}
bool SliceService::valid_revision_locked(SampleSlotId id,
                                         std::uint64_t expected,
                                         std::string &error) const {
    const auto found = slots_.find(id.value);
    if (found == slots_.end() || found->second.sample_publication == 0 ||
        found->second.frames == 0) {
        error = "sample slot has no active sample";
        return false;
    }
    if (expected != found->second.revision) {
        error = "slice revision conflict";
        return false;
    }
    return true;
}
bool SliceService::create(SampleSlotId id, std::uint64_t expected,
                          std::uint64_t start, std::uint64_t end,
                          std::string name, SliceId &created,
                          std::string &error) {
    std::lock_guard lock(mutex_);
    synchronize_locked(id);
    auto &slot = slots_[id.value];
    if (!valid_revision_locked(id, expected, error) || start >= end ||
        end > slot.frames) {
        if (error.empty())
            error = "slice boundaries are outside the sample";
        return false;
    }
    created = next_id_++;
    slot.slices.push_back({created,
                           id,
                           slot.sample_revision,
                           slot.sample_publication,
                           start,
                           end,
                           std::move(name),
                           {},
                           1.0F});
    ++slot.revision;
    error.clear();
    return true;
}
bool SliceService::erase(SliceId id, std::uint64_t expected,
                         std::string &error) {
    std::lock_guard lock(mutex_);
    auto *slice = find_locked(id);
    if (!slice) {
        error = "slice does not exist";
        return false;
    }
    const auto slice_slot = slice->slot;
    synchronize_locked(slice_slot);
    auto &slot = slots_[slice_slot.value];
    if (!valid_revision_locked(slice_slot, expected, error))
        return false;
    for (std::size_t pad = 0; pad < pads_.size(); ++pad)
        if (pads_[pad] == id) {
            if (!publish_pad_locked(pad, nullptr, error))
                return false;
            pads_[pad].reset();
        }
    std::erase_if(slot.slices,
                  [&](const auto &value) { return value.id == id; });
    ++slot.revision;
    error.clear();
    return true;
}
bool SliceService::set_bounds(SliceId id, std::uint64_t expected,
                              std::uint64_t start, std::uint64_t end,
                              std::string &error) {
    std::lock_guard lock(mutex_);
    auto *slice = find_locked(id);
    if (!slice) {
        error = "slice does not exist";
        return false;
    }
    const auto slice_slot = slice->slot;
    synchronize_locked(slice_slot);
    auto &slot = slots_[slice_slot.value];
    slice = find_locked(id);
    if (!slice || !valid_revision_locked(slice->slot, expected, error) ||
        start >= end || end > slot.frames) {
        if (error.empty())
            error = "slice boundaries are outside the sample";
        return false;
    }
    const auto old_start = slice->start_frame, old_end = slice->end_frame;
    slice->start_frame = start;
    slice->end_frame = end;
    for (std::size_t pad = 0; pad < pads_.size(); ++pad)
        if (pads_[pad] == id && !publish_pad_locked(pad, slice, error)) {
            slice->start_frame = old_start;
            slice->end_frame = old_end;
            return false;
        }
    ++slot.revision;
    error.clear();
    return true;
}
bool SliceService::rename(SliceId id, std::uint64_t expected, std::string name,
                          std::string &error) {
    std::lock_guard lock(mutex_);
    auto *slice = find_locked(id);
    if (!slice) {
        error = "slice does not exist";
        return false;
    }
    const auto slot_id = slice->slot;
    synchronize_locked(slot_id);
    slice = find_locked(id);
    if (!slice || !valid_revision_locked(slot_id, expected, error))
        return false;
    slice->name = std::move(name);
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::duplicate(SliceId id, std::uint64_t expected,
                             SliceId &created, std::string &error) {
    std::lock_guard lock(mutex_);
    const auto *source = find_locked(id);
    if (!source) {
        error = "slice does not exist";
        return false;
    }
    const auto slot_id = source->slot;
    synchronize_locked(slot_id);
    source = find_locked(id);
    if (!source || !valid_revision_locked(slot_id, expected, error))
        return false;
    auto copy = *source;
    copy.id = created = next_id_++;
    copy.name += " copy";
    auto &slot = slots_[slot_id.value];
    slot.slices.push_back(std::move(copy));
    ++slot.revision;
    error.clear();
    return true;
}
bool SliceService::clear(SampleSlotId id, std::uint64_t expected,
                         std::string &error) {
    std::lock_guard lock(mutex_);
    synchronize_locked(id);
    auto &slot = slots_[id.value];
    if (!valid_revision_locked(id, expected, error))
        return false;
    for (std::size_t pad = 0; pad < pads_.size(); ++pad)
        if (pads_[pad] &&
            std::any_of(slot.slices.begin(), slot.slices.end(),
                        [&](const auto &s) { return s.id == *pads_[pad]; })) {
            if (!publish_pad_locked(pad, nullptr, error))
                return false;
            pads_[pad].reset();
        }
    slot.slices.clear();
    ++slot.revision;
    error.clear();
    return true;
}
bool SliceService::equal_divisions(SampleSlotId id, std::uint64_t expected,
                                   std::size_t divisions, std::string &error) {
    if (!(divisions == 2 || divisions == 4 || divisions == 8 ||
          divisions == 16 || divisions == 32 || divisions == 64)) {
        error = "equal divisions must be 2, 4, 8, 16, 32, or 64";
        return false;
    }
    std::lock_guard lock(mutex_);
    synchronize_locked(id);
    auto &slot = slots_[id.value];
    if (!valid_revision_locked(id, expected, error) ||
        slot.frames < divisions) {
        if (error.empty())
            error = "sample is too short for requested divisions";
        return false;
    }
    for (std::size_t pad = 0; pad < pads_.size(); ++pad)
        if (pads_[pad] && std::any_of(slot.slices.begin(), slot.slices.end(),
                                      [&](const auto &slice) {
                                          return slice.id == *pads_[pad];
                                      })) {
            if (!publish_pad_locked(pad, nullptr, error))
                return false;
            pads_[pad].reset();
        }
    slot.slices.clear();
    for (std::size_t i = 0; i < divisions; ++i) {
        const auto start = slot.frames * i / divisions;
        const auto end = slot.frames * (i + 1) / divisions;
        slot.slices.push_back({next_id_++,
                               id,
                               slot.sample_revision,
                               slot.sample_publication,
                               start,
                               end,
                               "Slice " + std::to_string(i + 1),
                               {},
                               1.0F});
    }
    ++slot.revision;
    error.clear();
    return true;
}
bool SliceService::detect_transients(SampleSlotId id, std::uint64_t expected,
                                     float sensitivity,
                                     std::uint64_t separation,
                                     std::string &error) {
    if (!std::isfinite(sensitivity) || sensitivity <= 0 || sensitivity > 1 ||
        separation == 0) {
        error = "transient sensitivity must be 0..1 and separation positive";
        return false;
    }
    std::lock_guard lock(mutex_);
    synchronize_locked(id);
    auto &slot = slots_[id.value];
    if (!valid_revision_locked(id, expected, error))
        return false;
    const auto sample = samples_.active_sample(id);
    if (!sample) {
        error = "sample data is unavailable";
        return false;
    }
    auto starts = detect_transient_frames(*sample, sensitivity, separation);
    for (std::size_t pad = 0; pad < pads_.size(); ++pad)
        if (pads_[pad] && std::any_of(slot.slices.begin(), slot.slices.end(),
                                      [&](const auto &slice) {
                                          return slice.id == *pads_[pad];
                                      })) {
            if (!publish_pad_locked(pad, nullptr, error))
                return false;
            pads_[pad].reset();
        }
    slot.slices.clear();
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const auto end =
            i + 1 < starts.size() ? starts[i + 1] : sample->frames();
        if (starts[i] < end)
            slot.slices.push_back({next_id_++,
                                   id,
                                   slot.sample_revision,
                                   slot.sample_publication,
                                   starts[i],
                                   end,
                                   "Transient " + std::to_string(i + 1),
                                   {},
                                   1.0F});
    }
    ++slot.revision;
    error.clear();
    return true;
}
bool SliceService::publish_pad_locked(std::size_t pad, const SampleSlice *slice,
                                      std::string &error) {
    if (pad >= pads_.size()) {
        error = "pad index must be 0..15";
        return false;
    }
    const auto note = static_cast<std::uint8_t>(36 + pad);
    const auto ok = slice ? engine_.map_note_to_slice(
                                note, slice->slot, slice->sample_publication,
                                slice->start_frame, slice->end_frame,
                                slice->gain, pad_settings_[pad])
                          : engine_.clear_note_slice(note);
    if (!ok) {
        error = "slice mapping queue is full";
        return false;
    }
    return true;
}
bool SliceService::assign_pad(std::size_t pad, SliceId id,
                              std::uint64_t expected, std::string &error) {
    std::lock_guard lock(mutex_);
    auto *slice = find_locked(id);
    if (!slice) {
        error = "slice does not exist";
        return false;
    }
    const auto slot_id = slice->slot;
    synchronize_locked(slot_id);
    slice = find_locked(id);
    if (!slice || !valid_revision_locked(slot_id, expected, error))
        return false;
    if (!publish_pad_locked(pad, slice, error))
        return false;
    pads_[pad] = id;
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::clear_pad(std::size_t pad, std::uint64_t expected,
                             std::string &error) {
    std::lock_guard lock(mutex_);
    if (pad >= pads_.size() || !pads_[pad]) {
        error = "pad is not assigned";
        return false;
    }
    auto *slice = find_locked(*pads_[pad]);
    if (!slice) {
        error = "assigned slice does not exist";
        return false;
    }
    const auto slot_id = slice->slot;
    synchronize_locked(slot_id);
    slice = find_locked(*pads_[pad]);
    if (!slice || !valid_revision_locked(slot_id, expected, error))
        return false;
    if (!publish_pad_locked(pad, nullptr, error))
        return false;
    pads_[pad].reset();
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::set_pad_settings(SampleSlotId context_slot, std::size_t pad,
                                    std::uint64_t expected,
                                    SamplePlaybackSettings settings,
                                    std::string &error) {
    std::lock_guard lock(mutex_);
    synchronize_locked(context_slot);
    if (pad >= pad_settings_.size()) {
        error = "pad index must be 0..15";
        return false;
    }
    if (!valid_revision_locked(context_slot, expected, error))
        return false;
    if (!valid_playback_settings(settings)) {
        error = "pad playback settings are outside supported ranges";
        return false;
    }
    const auto previous = pad_settings_[pad];
    pad_settings_[pad] = settings;
    const auto *slice = pads_[pad] ? find_locked(*pads_[pad]) : nullptr;
    const auto published =
        slice ? publish_pad_locked(pad, slice, error)
              : engine_.configure_note_playback(
                    static_cast<std::uint8_t>(36 + pad), settings);
    if (!published) {
        pad_settings_[pad] = previous;
        if (error.empty())
            error = "pad settings queue is full";
        return false;
    }
    ++slots_[context_slot.value].revision;
    error.clear();
    return true;
}
bool SliceService::preview(SliceId id, float velocity, std::string &error) {
    std::lock_guard lock(mutex_);
    const auto *slice = find_locked(id);
    if (!slice) {
        error = "slice does not exist";
        return false;
    }
    const auto slot_id = slice->slot;
    synchronize_locked(slot_id);
    slice = find_locked(id);
    if (!slice) {
        error = "slice belongs to an inactive sample version";
        return false;
    }
    if (!engine_.trigger_slice(slice->slot, slice->sample_publication,
                               slice->start_frame, slice->end_frame,
                               slice->gain, velocity)) {
        error = "slice preview queue is full or slice is stale";
        return false;
    }
    error.clear();
    return true;
}
bool SliceService::preview_full(SampleSlotId id, float velocity,
                                std::string &error) {
    std::lock_guard lock(mutex_);
    synchronize_locked(id);
    const auto &slot = slots_[id.value];
    if (slot.frames == 0 ||
        !engine_.trigger_slice(id, slot.sample_publication, 0, slot.frames, 1,
                               velocity)) {
        error = "full-sample preview is unavailable";
        return false;
    }
    error.clear();
    return true;
}
void SliceService::stop_preview() noexcept {
    (void)engine_.stop_slice_previews();
}

void save_slice_project(const SliceProject &project,
                        const std::filesystem::path &path) {
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot write slice project: " +
                                 path.string());
    out << "NEUROGENIC_SLICES " << SliceProject::version << '\n';
    for (const auto &[slot, file] : project.sample_paths)
        out << "sample " << slot << ' ' << std::quoted(file.string()) << '\n';
    for (const auto &s : project.slices)
        out << "slice " << s.id << ' ' << s.slot.value << ' '
            << s.sample_revision << ' ' << s.start_frame << ' ' << s.end_frame
            << ' ' << s.gain << ' ' << (s.root_note ? int(*s.root_note) : -1)
            << ' ' << std::quoted(s.name) << '\n';
    for (std::size_t pad = 0; pad < project.pads.size(); ++pad)
        if (project.pads[pad])
            out << "pad " << pad << ' ' << *project.pads[pad] << '\n';
    for (std::size_t pad = 0; pad < project.pad_settings.size(); ++pad) {
        const auto &s = project.pad_settings[pad];
        if (s == SamplePlaybackSettings{})
            continue;
        out << "playback " << pad << ' ' << static_cast<unsigned>(s.mode) << ' '
            << static_cast<unsigned>(s.choke_group) << ' '
            << (s.choke_release ? 1 : 0) << ' ' << s.semitones << ' '
            << s.fine_cents << ' ' << s.gain << ' ' << s.root_note << ' '
            << (s.reverse ? 1 : 0) << ' ' << s.attack_seconds << ' '
            << s.decay_seconds << ' ' << s.sustain << ' ' << s.release_seconds
            << ' ' << static_cast<unsigned>(s.stretch_mode) << ' '
            << static_cast<unsigned>(s.stretch_quality) << ' '
            << s.stretch_ratio << ' ' << s.source_beats << ' ' << s.source_bpm
            << '\n';
    }
}
SliceProject load_slice_project(const std::filesystem::path &path) {
    std::ifstream in(path);
    std::string magic;
    unsigned version{};
    if (!(in >> magic >> version) || magic != "NEUROGENIC_SLICES" ||
        (version < 1 || version > SliceProject::version))
        throw std::runtime_error("unsupported slice project format");
    SliceProject result;
    std::string kind;
    while (in >> kind) {
        if (kind == "sample") {
            std::uint32_t slot{};
            std::string file;
            if (!(in >> slot >> std::quoted(file)))
                throw std::runtime_error("invalid sample reference");
            if (!std::filesystem::exists(file))
                throw std::runtime_error("referenced sample is missing: " +
                                         file);
            result.sample_paths[slot] = file;
        } else if (kind == "slice") {
            SampleSlice s;
            int root{};
            if (!(in >> s.id >> s.slot.value >> s.sample_revision >>
                  s.start_frame >> s.end_frame >> s.gain >> root >>
                  std::quoted(s.name)) ||
                s.start_frame >= s.end_frame || !std::isfinite(s.gain) ||
                s.gain < 0 || root > 127)
                throw std::runtime_error("invalid slice record");
            if (root >= 0)
                s.root_note = static_cast<std::uint8_t>(root);
            result.slices.push_back(std::move(s));
        } else if (kind == "pad") {
            std::size_t pad{};
            SliceId id{};
            if (!(in >> pad >> id) || pad >= result.pads.size())
                throw std::runtime_error("invalid pad record");
            result.pads[pad] = id;
        } else if (kind == "playback" && version >= 2) {
            std::size_t pad{};
            unsigned mode{}, group{}, choke_release{}, reverse{};
            SamplePlaybackSettings settings;
            if (!(in >> pad >> mode >> group >> choke_release >>
                  settings.semitones >> settings.fine_cents >> settings.gain >>
                  settings.root_note >> reverse >> settings.attack_seconds >>
                  settings.decay_seconds >> settings.sustain >>
                  settings.release_seconds) ||
                pad >= result.pad_settings.size() || mode > 2 || group > 16 ||
                choke_release > 1 || reverse > 1) {
                throw std::runtime_error("invalid pad playback record");
            }
            settings.mode = static_cast<SamplePlaybackMode>(mode);
            settings.choke_group = static_cast<std::uint8_t>(group);
            settings.choke_release = choke_release != 0;
            settings.reverse = reverse != 0;
            if (version >= 3) {
                unsigned stretch_mode{}, stretch_quality{};
                if (!(in >> stretch_mode >> stretch_quality >>
                      settings.stretch_ratio >> settings.source_beats >>
                      settings.source_bpm) ||
                    stretch_mode > 1 || stretch_quality > 2)
                    throw std::runtime_error("invalid stretch playback record");
                settings.stretch_mode = static_cast<StretchMode>(stretch_mode);
                settings.stretch_quality =
                    static_cast<StretchQuality>(stretch_quality);
            }
            if (!valid_playback_settings(settings))
                throw std::runtime_error("invalid pad playback settings");
            result.pad_settings[pad] = settings;
        } else
            throw std::runtime_error("unknown slice project record");
    }
    std::vector<SliceId> ids;
    ids.reserve(result.slices.size());
    for (const auto &slice : result.slices) {
        if (slice.id == 0 ||
            std::find(ids.begin(), ids.end(), slice.id) != ids.end())
            throw std::runtime_error("duplicate or zero slice identifier");
        ids.push_back(slice.id);
    }
    for (const auto pad : result.pads)
        if (pad && std::find(ids.begin(), ids.end(), *pad) == ids.end())
            throw std::runtime_error("pad references an unknown slice");
    return result;
}
} // namespace neurogenic
