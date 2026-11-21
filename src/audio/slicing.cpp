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
    for (std::size_t pad = 0; pad < region_pads_.size(); ++pad)
        if (region_pads_[pad] &&
            std::any_of(slot.regions.begin(), slot.regions.end(),
                        [&](const auto &region) {
                            return region.id == *region_pads_[pad];
                        })) {
            (void)engine_.clear_note_slice(static_cast<std::uint8_t>(36 + pad));
            region_pads_[pad].reset();
        }
    slot.slices.clear();
    slot.regions.clear();
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
    SliceSnapshot result;
    result.revision = slot.revision;
    result.selected_slot = id;
    result.sample_revision = slot.sample_revision;
    result.sample_publication = slot.sample_publication;
    result.sample_frames = slot.frames;
    result.sample_rate = slot.rate;
    result.channels = slot.channels;
    result.slices = slot.slices;
    result.regions = slot.regions;
    result.pad_assignments = pads_;
    result.region_pad_assignments = region_pads_;
    result.pad_settings = pad_settings_;
    result.waveform = slot.waveform;
    return result;
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
SampleRegion *SliceService::find_region_locked(RegionId id) {
    for (auto &[slot_id, slot] : slots_) {
        (void)slot_id;
        const auto found =
            std::find_if(slot.regions.begin(), slot.regions.end(),
                         [&](const auto &value) { return value.id == id; });
        if (found != slot.regions.end()) return &*found;
    }
    return nullptr;
}
const SampleRegion *SliceService::find_region_locked(RegionId id) const {
    return const_cast<SliceService *>(this)->find_region_locked(id);
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
bool SliceService::apply_boundaries(
    SampleSlotId id, std::uint64_t expected,
    std::span<const std::uint64_t> supplied, bool create_regions, bool replace,
    bool assign_pads, std::size_t starting_pad, std::string &error) {
    std::lock_guard lock(mutex_);
    synchronize_locked(id);
    auto &slot = slots_[id.value];
    if (!valid_revision_locked(id, expected, error))
        return false;
    std::vector<std::uint64_t> boundaries(supplied.begin(), supplied.end());
    if (boundaries.size() < 2) {
        error = "at least two analysis boundaries are required";
        return false;
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    if (boundaries.size() < 2 || boundaries.front() >= boundaries.back() ||
        boundaries.back() > slot.frames) {
        error = "analysis boundaries are outside the sample";
        return false;
    }
    for (std::size_t i = 1; i < boundaries.size(); ++i)
        if (boundaries[i - 1] >= boundaries[i]) {
            error = "analysis produced an empty interval";
            return false;
        }
    const auto count = boundaries.size() - 1;
    if (assign_pads && (starting_pad >= pads_.size() ||
                        count > pads_.size() - starting_pad)) {
        error = "analysis result exceeds available pad capacity";
        return false;
    }

    auto staged_slices = slot.slices;
    auto staged_regions = slot.regions;
    auto staged_pads = pads_;
    auto staged_region_pads = region_pads_;
    if (replace) {
        if (create_regions) {
            for (auto &pad : staged_region_pads)
                if (pad && std::any_of(staged_regions.begin(), staged_regions.end(),
                    [&](const auto &region) { return region.id == *pad; }))
                    pad.reset();
            staged_regions.clear();
        } else {
            for (auto &pad : staged_pads)
                if (pad && std::any_of(staged_slices.begin(), staged_slices.end(),
                    [&](const auto &slice) { return slice.id == *pad; }))
                    pad.reset();
            staged_slices.clear();
        }
    }
    std::vector<SliceId> new_slices;
    std::vector<RegionId> new_regions;
    for (std::size_t i = 0; i < count; ++i) {
        if (create_regions) {
            const auto region_id = next_region_id_++;
            new_regions.push_back(region_id);
            staged_regions.push_back({region_id, id, slot.sample_revision,
                slot.sample_publication, boundaries[i], boundaries[i + 1],
                "Analysis " + std::to_string(i + 1),
                RegionDirection::forward, RegionFadeCurve::linear, 0, 0,
                1.0F, false, boundaries[i], boundaries[i + 1], 0});
        } else {
            const auto slice_id = next_id_++;
            new_slices.push_back(slice_id);
            staged_slices.push_back({slice_id, id, slot.sample_revision,
                slot.sample_publication, boundaries[i], boundaries[i + 1],
                "Analysis " + std::to_string(i + 1), {}, 1.0F});
        }
    }
    if (assign_pads)
        for (std::size_t i = 0; i < count; ++i) {
            const auto pad = starting_pad + i;
            staged_pads[pad] = create_regions
                                   ? std::optional<SliceId>{}
                                   : std::optional<SliceId>{new_slices[i]};
            staged_region_pads[pad] =
                create_regions ? std::optional<RegionId>{new_regions[i]}
                               : std::optional<RegionId>{};
        }

    // Queue changed mappings only after every boundary, identity and capacity
    // check has succeeded. Deleted assignments must also be removed from the
    // audio-active note map. The operation is bounded by the fixed 16 pads.
    std::array<PadMappingActivation, 16> mappings{};
    std::uint16_t changed_mask{};
    for (std::size_t pad = 0; pad < pads_.size(); ++pad) {
        if (staged_pads[pad] == pads_[pad] &&
            staged_region_pads[pad] == region_pads_[pad])
            continue;
        changed_mask |= std::uint16_t{1} << pad;
        auto &mapping = mappings[pad];
        if (staged_pads[pad]) {
            const auto found = std::find_if(
                staged_slices.begin(), staged_slices.end(), [&](const auto &v) {
                    return v.id == *staged_pads[pad];
                });
            if (found == staged_slices.end()) {
                error = "staged pad target does not exist";
                return false;
            }
            mapping = {found->slot, found->sample_publication,
                       found->start_frame, found->end_frame, found->gain,
                       pad_settings_[pad], {}, true};
        } else if (staged_region_pads[pad]) {
            const auto found = std::find_if(
                staged_regions.begin(), staged_regions.end(), [&](const auto &v) {
                    return v.id == *staged_region_pads[pad];
                });
            if (found == staged_regions.end()) {
                error = "staged region target does not exist";
                return false;
            }
            auto settings = pad_settings_[pad];
            settings.reverse = found->direction == RegionDirection::reverse;
            mapping = PadMappingActivation{
                found->slot, found->sample_publication, found->start_frame,
                found->end_frame, found->gain, settings,
                SampleRegionPlayback{found->fade_in_frames,
                                     found->fade_out_frames,
                                     found->loop_start_frame,
                                     found->loop_end_frame,
                                     found->loop_crossfade_frames,
                                     found->fade_curve,
                                     found->loop_enabled},
                true};
        }
    }
    if (!engine_.publish_pad_mappings(mappings, changed_mask)) {
        error = "atomic pad mapping queue is full";
        return false;
    }
    slot.slices = std::move(staged_slices);
    slot.regions = std::move(staged_regions);
    pads_ = staged_pads;
    region_pads_ = staged_region_pads;
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
    region_pads_[pad].reset();
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::clear_pad(std::size_t pad, std::uint64_t expected,
                             std::string &error) {
    std::lock_guard lock(mutex_);
    if (pad >= pads_.size() || (!pads_[pad] && !region_pads_[pad])) {
        error = "pad is not assigned";
        return false;
    }
    if (region_pads_[pad]) {
        auto *region = find_region_locked(*region_pads_[pad]);
        if (!region) { error = "assigned region does not exist"; return false; }
        const auto slot_id = region->slot;
        synchronize_locked(slot_id);
        if (!valid_revision_locked(slot_id, expected, error) ||
            !publish_region_pad_locked(pad, nullptr, error)) return false;
        region_pads_[pad].reset();
        ++slots_[slot_id.value].revision;
        error.clear();
        return true;
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
    const auto *region = region_pads_[pad]
                             ? find_region_locked(*region_pads_[pad])
                             : nullptr;
    const auto published =
        region ? publish_region_pad_locked(pad, region, error)
               : slice ? publish_pad_locked(pad, slice, error)
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
namespace {
bool valid_region(const SampleRegion &region, std::uint64_t frames,
                  std::string &error) {
    if (!region.id || region.start_frame >= region.end_frame ||
        region.end_frame > frames || !std::isfinite(region.gain) ||
        region.gain < 0) {
        error = "region boundaries, fades, or gain are invalid";
        return false;
    }
    const auto length = region.end_frame - region.start_frame;
    if (region.fade_in_frames > length || region.fade_out_frames > length) {
        error = "region boundaries, fades, or gain are invalid";
        return false;
    }
    if (region.loop_enabled &&
        (region.loop_start_frame < region.start_frame ||
         region.loop_start_frame >= region.loop_end_frame ||
         region.loop_end_frame > region.end_frame ||
         region.loop_crossfade_frames >
             region.loop_end_frame - region.loop_start_frame)) {
        error = "region loop is outside its playback boundaries";
        return false;
    }
    return true;
}
SampleRegionPlayback playback_for(const SampleRegion &region) {
    return {region.fade_in_frames,
            region.fade_out_frames,
            region.loop_start_frame,
            region.loop_end_frame,
            region.loop_crossfade_frames,
            region.fade_curve,
            region.loop_enabled};
}
} // namespace

bool SliceService::create_region(SampleSlotId id, std::uint64_t expected,
                                 std::uint64_t start, std::uint64_t end,
                                 std::string name, RegionId &created,
                                 std::string &error) {
    std::lock_guard lock(mutex_);
    synchronize_locked(id);
    auto &slot = slots_[id.value];
    if (!valid_revision_locked(id, expected, error)) return false;
    SampleRegion region{next_region_id_, id, slot.sample_revision,
                        slot.sample_publication, start, end, std::move(name)};
    if (!valid_region(region, slot.frames, error)) return false;
    created = next_region_id_++;
    slot.regions.push_back(std::move(region));
    ++slot.revision;
    error.clear();
    return true;
}
bool SliceService::create_region_from_slice(SliceId id, std::uint64_t expected,
                                            RegionId &created,
                                            std::string &error) {
    std::lock_guard lock(mutex_);
    auto *slice = find_locked(id);
    if (!slice) { error = "slice does not exist"; return false; }
    const auto slot_id = slice->slot;
    synchronize_locked(slot_id);
    slice = find_locked(id);
    if (!slice || !valid_revision_locked(slot_id, expected, error)) return false;
    auto &slot = slots_[slot_id.value];
    SampleRegion region{next_region_id_, slot_id, slot.sample_revision,
                        slot.sample_publication, slice->start_frame,
                        slice->end_frame, slice->name, RegionDirection::forward,
                        RegionFadeCurve::linear, 0, 0, slice->gain};
    created = next_region_id_++;
    slot.regions.push_back(std::move(region));
    ++slot.revision;
    error.clear();
    return true;
}
std::size_t SliceService::region_usage(RegionId id) const {
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::count(region_pads_.begin(),
                                               region_pads_.end(), id));
}
bool SliceService::erase_region(RegionId id, std::uint64_t expected,
                                std::string &error) {
    std::lock_guard lock(mutex_);
    auto *region = find_region_locked(id);
    if (!region) { error = "region does not exist"; return false; }
    const auto slot_id = region->slot;
    synchronize_locked(slot_id);
    region = find_region_locked(id);
    if (!region || !valid_revision_locked(slot_id, expected, error)) return false;
    if (std::find(region_pads_.begin(), region_pads_.end(), id) !=
        region_pads_.end()) {
        error = "region is assigned to a pad";
        return false;
    }
    auto &regions = slots_[slot_id.value].regions;
    std::erase_if(regions, [&](const auto &value) { return value.id == id; });
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::duplicate_region(RegionId id, std::uint64_t expected,
                                    RegionId &created, std::string &error) {
    std::lock_guard lock(mutex_);
    auto *region = find_region_locked(id);
    if (!region) { error = "region does not exist"; return false; }
    const auto slot_id = region->slot;
    synchronize_locked(slot_id);
    region = find_region_locked(id);
    if (!region || !valid_revision_locked(slot_id, expected, error)) return false;
    auto copy = *region;
    copy.id = created = next_region_id_++;
    copy.name += " copy";
    slots_[slot_id.value].regions.push_back(std::move(copy));
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::rename_region(RegionId id, std::uint64_t expected,
                                 std::string name, std::string &error) {
    std::lock_guard lock(mutex_);
    auto *region = find_region_locked(id);
    if (!region) { error = "region does not exist"; return false; }
    const auto slot_id = region->slot;
    synchronize_locked(slot_id);
    region = find_region_locked(id);
    if (!region || !valid_revision_locked(slot_id, expected, error)) return false;
    region->name = std::move(name);
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::publish_region_pad_locked(std::size_t pad,
                                             const SampleRegion *region,
                                             std::string &error) {
    if (pad >= region_pads_.size()) { error = "pad index must be 0..15"; return false; }
    if (!region) return engine_.clear_note_slice(static_cast<std::uint8_t>(36 + pad));
    auto settings = pad_settings_[pad];
    settings.reverse = region->direction == RegionDirection::reverse;
    if (!engine_.map_note_to_region(static_cast<std::uint8_t>(36 + pad),
                                    region->slot, region->sample_publication,
                                    region->start_frame, region->end_frame,
                                    region->gain, settings,
                                    playback_for(*region))) {
        error = "region mapping queue is full";
        return false;
    }
    return true;
}
bool SliceService::assign_region_pad(std::size_t pad, RegionId id,
                                     std::uint64_t expected,
                                     std::string &error) {
    std::lock_guard lock(mutex_);
    auto *region = find_region_locked(id);
    if (!region) { error = "region does not exist"; return false; }
    const auto slot_id = region->slot;
    synchronize_locked(slot_id);
    region = find_region_locked(id);
    if (!region || !valid_revision_locked(slot_id, expected, error) ||
        !publish_region_pad_locked(pad, region, error)) return false;
    region_pads_[pad] = id;
    pads_[pad].reset();
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::set_region_bounds(RegionId id, std::uint64_t expected,
                                     std::uint64_t start, std::uint64_t end,
                                     std::string &error) {
    std::lock_guard lock(mutex_);
    auto *region = find_region_locked(id);
    if (!region) { error = "region does not exist"; return false; }
    const auto slot_id = region->slot;
    synchronize_locked(slot_id);
    region = find_region_locked(id);
    if (!region || !valid_revision_locked(slot_id, expected, error)) return false;
    auto candidate = *region;
    candidate.start_frame = start;
    candidate.end_frame = end;
    if (candidate.loop_enabled &&
        (candidate.loop_start_frame < start || candidate.loop_end_frame > end)) {
        error = "region boundaries would exclude its loop";
        return false;
    }
    if (!valid_region(candidate, slots_[slot_id.value].frames, error)) return false;
    for (std::size_t pad = 0; pad < region_pads_.size(); ++pad)
        if (region_pads_[pad] == id &&
            !publish_region_pad_locked(pad, &candidate, error)) return false;
    *region = std::move(candidate);
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::set_region_fades(RegionId id, std::uint64_t expected,
                                    std::uint64_t fade_in,
                                    std::uint64_t fade_out,
                                    RegionFadeCurve curve,
                                    std::string &error) {
    std::lock_guard lock(mutex_);
    auto *region = find_region_locked(id);
    if (!region) { error = "region does not exist"; return false; }
    const auto slot_id = region->slot;
    synchronize_locked(slot_id);
    region = find_region_locked(id);
    if (!region || !valid_revision_locked(slot_id, expected, error)) return false;
    auto candidate = *region;
    candidate.fade_in_frames = fade_in;
    candidate.fade_out_frames = fade_out;
    candidate.fade_curve = curve;
    if (!valid_region(candidate, slots_[slot_id.value].frames, error)) return false;
    for (std::size_t pad = 0; pad < region_pads_.size(); ++pad)
        if (region_pads_[pad] == id &&
            !publish_region_pad_locked(pad, &candidate, error)) return false;
    *region = std::move(candidate);
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::set_region_gain(RegionId id, std::uint64_t expected,
                                   float gain, std::string &error) {
    std::lock_guard lock(mutex_);
    auto *region = find_region_locked(id);
    if (!region) { error = "region does not exist"; return false; }
    const auto slot_id = region->slot;
    synchronize_locked(slot_id);
    region = find_region_locked(id);
    if (!region || !valid_revision_locked(slot_id, expected, error)) return false;
    auto candidate = *region;
    candidate.gain = gain;
    if (!valid_region(candidate, slots_[slot_id.value].frames, error)) return false;
    for (std::size_t pad = 0; pad < region_pads_.size(); ++pad)
        if (region_pads_[pad] == id &&
            !publish_region_pad_locked(pad, &candidate, error)) return false;
    *region = std::move(candidate);
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::set_region_direction(RegionId id, std::uint64_t expected,
                                        RegionDirection direction,
                                        std::string &error) {
    std::lock_guard lock(mutex_);
    auto *region = find_region_locked(id);
    if (!region) { error = "region does not exist"; return false; }
    const auto slot_id = region->slot;
    synchronize_locked(slot_id);
    region = find_region_locked(id);
    if (!region || !valid_revision_locked(slot_id, expected, error)) return false;
    auto candidate = *region;
    candidate.direction = direction;
    for (std::size_t pad = 0; pad < region_pads_.size(); ++pad)
        if (region_pads_[pad] == id &&
            !publish_region_pad_locked(pad, &candidate, error)) return false;
    *region = std::move(candidate);
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::set_region_loop(RegionId id, std::uint64_t expected,
                                   bool enabled, std::uint64_t loop_start,
                                   std::uint64_t loop_end,
                                   std::uint32_t crossfade,
                                   std::string &error) {
    std::lock_guard lock(mutex_);
    auto *region = find_region_locked(id);
    if (!region) { error = "region does not exist"; return false; }
    const auto slot_id = region->slot;
    synchronize_locked(slot_id);
    region = find_region_locked(id);
    if (!region || !valid_revision_locked(slot_id, expected, error)) return false;
    auto candidate = *region;
    candidate.loop_enabled = enabled;
    candidate.loop_start_frame = loop_start;
    candidate.loop_end_frame = loop_end;
    candidate.loop_crossfade_frames = crossfade;
    if (!valid_region(candidate, slots_[slot_id.value].frames, error)) return false;
    for (std::size_t pad = 0; pad < region_pads_.size(); ++pad)
        if (region_pads_[pad] == id &&
            !publish_region_pad_locked(pad, &candidate, error)) return false;
    *region = std::move(candidate);
    ++slots_[slot_id.value].revision;
    error.clear();
    return true;
}
bool SliceService::preview_region(RegionId id, float velocity,
                                  std::string &error) {
    std::lock_guard lock(mutex_);
    auto *region = find_region_locked(id);
    if (!region) { error = "region does not exist"; return false; }
    const auto slot_id = region->slot;
    synchronize_locked(slot_id);
    region = find_region_locked(id);
    if (!region) { error = "region belongs to an inactive sample version"; return false; }
    SamplePlaybackSettings settings;
    settings.reverse = region->direction == RegionDirection::reverse;
    if (!engine_.trigger_region(region->slot, region->sample_publication,
                                region->start_frame, region->end_frame,
                                region->gain, settings, playback_for(*region),
                                velocity)) {
        error = "region preview queue is full or region is stale";
        return false;
    }
    error.clear();
    return true;
}
void SliceService::stop_preview() noexcept {
    (void)engine_.stop_slice_previews();
}

SliceProject SliceService::project_snapshot(
    std::map<std::uint32_t, std::filesystem::path> paths) const {
    std::lock_guard lock(mutex_);
    SliceProject project;
    project.sample_paths = std::move(paths);
    project.pads = pads_;
    project.region_pads = region_pads_;
    project.pad_settings = pad_settings_;
    for (const auto &[id, slot] : slots_)
        project.slices.insert(project.slices.end(), slot.slices.begin(),
                              slot.slices.end());
    for (const auto &[id, slot] : slots_)
        project.regions.insert(project.regions.end(), slot.regions.begin(),
                               slot.regions.end());
    return project;
}

bool SliceService::restore_project(const SliceProject &project,
                                   std::string &error) {
    std::lock_guard lock(mutex_);
    std::map<std::uint32_t, SlotState> restored;
    SliceId next = 1;
    for (auto slice : project.slices) {
        next = std::max(next, slice.id + 1);
        const auto status = samples_.status(slice.slot);
        if (!status.has_sample || status.active_publication == 0)
            continue;
        if (slice.start_frame >= slice.end_frame ||
            slice.end_frame > status.frames) {
            error = "project slice is outside its restored sample";
            return false;
        }
        slice.sample_revision = status.active_revision;
        slice.sample_publication = status.active_publication;
        auto &slot = restored[slice.slot.value];
        slot.revision = 1;
        slot.sample_revision = status.active_revision;
        slot.sample_publication = status.active_publication;
        slot.frames = status.frames;
        slot.rate = status.engine_sample_rate;
        slot.channels = status.channels;
        slot.slices.push_back(std::move(slice));
    }
    RegionId next_region = 1;
    for (auto region : project.regions) {
        next_region = std::max(next_region, region.id + 1);
        const auto status = samples_.status(region.slot);
        if (!status.has_sample || status.active_publication == 0) continue;
        if (!valid_region(region, status.frames, error)) return false;
        region.sample_revision = status.active_revision;
        region.sample_publication = status.active_publication;
        auto &slot = restored[region.slot.value];
        slot.revision = 1;
        slot.sample_revision = status.active_revision;
        slot.sample_publication = status.active_publication;
        slot.frames = status.frames;
        slot.rate = status.engine_sample_rate;
        slot.channels = status.channels;
        slot.regions.push_back(std::move(region));
    }
    std::array<std::optional<SliceId>, 16> pads{};
    std::array<std::optional<RegionId>, 16> region_pads{};
    for (std::size_t pad = 0; pad < pads.size(); ++pad) {
        const auto wanted = project.pads[pad];
        const SampleSlice *slice{};
        if (wanted)
            for (const auto &[id, slot] : restored)
                if (const auto found = std::find_if(
                        slot.slices.begin(), slot.slices.end(),
                        [&](const auto &item) { return item.id == *wanted; });
                    found != slot.slices.end()) {
                    slice = &*found;
                    break;
                }
        const auto settings = project.pad_settings[pad];
        if (!valid_playback_settings(settings)) {
            error = "project contains invalid pad playback settings";
            return false;
        }
        const SampleRegion *region{};
        if (project.region_pads[pad])
            for (const auto &[id, slot] : restored) {
                (void)id;
                const auto found = std::find_if(
                    slot.regions.begin(), slot.regions.end(),
                    [&](const auto &item) {
                        return item.id == *project.region_pads[pad];
                    });
                if (found != slot.regions.end()) { region = &*found; break; }
            }
        auto region_settings = settings;
        if (region)
            region_settings.reverse =
                region->direction == RegionDirection::reverse;
        if (!(region ? engine_.map_note_to_region(
                           static_cast<std::uint8_t>(36 + pad), region->slot,
                           region->sample_publication, region->start_frame,
                           region->end_frame, region->gain, region_settings,
                           playback_for(*region))
                     : slice ? engine_.map_note_to_slice(
                          static_cast<std::uint8_t>(36 + pad), slice->slot,
                          slice->sample_publication, slice->start_frame,
                          slice->end_frame, slice->gain, settings)
                    : engine_.clear_note_slice(
                          static_cast<std::uint8_t>(36 + pad))) ||
            (!slice && !engine_.configure_note_playback(
                           static_cast<std::uint8_t>(36 + pad), settings))) {
            error = "sample mapping publication queue is full";
            return false;
        }
        if (slice)
            pads[pad] = slice->id;
        if (region) {
            region_pads[pad] = region->id;
            pads[pad].reset();
        }
    }
    for (auto &[id, slot] : restored)
        if (const auto sample = samples_.active_sample(SampleSlotId{id}))
            slot.waveform = build_waveform_peaks(*sample);
    slots_ = std::move(restored);
    pads_ = pads;
    region_pads_ = region_pads;
    pad_settings_ = project.pad_settings;
    next_id_ = next;
    next_region_id_ = next_region;
    error.clear();
    return true;
}

bool SliceService::adopt_activated_project(const SliceProject &project,
                                           std::string &error) {
    std::lock_guard lock(mutex_);
    std::map<std::uint32_t, SlotState> restored;
    SliceId next = 1;
    for (auto slice : project.slices) {
        next = std::max(next, slice.id + 1);
        const auto status = samples_.status(slice.slot);
        if (!status.has_sample || !status.active_publication) continue;
        if (slice.start_frame >= slice.end_frame ||
            slice.end_frame > status.frames) {
            error = "preset slice is outside its activated sample";
            return false;
        }
        slice.sample_revision = status.active_revision;
        slice.sample_publication = status.active_publication;
        auto &slot = restored[slice.slot.value];
        slot.revision = 1;
        slot.sample_revision = status.active_revision;
        slot.sample_publication = status.active_publication;
        slot.frames = status.frames;
        slot.rate = status.engine_sample_rate;
        slot.channels = status.channels;
        slot.slices.push_back(std::move(slice));
    }
    RegionId next_region = 1;
    for (auto region : project.regions) {
        next_region = std::max(next_region, region.id + 1);
        const auto status = samples_.status(region.slot);
        if (!status.has_sample || !status.active_publication) continue;
        if (!valid_region(region, status.frames, error)) return false;
        region.sample_revision = status.active_revision;
        region.sample_publication = status.active_publication;
        auto &slot = restored[region.slot.value];
        slot.revision = 1;
        slot.sample_revision = status.active_revision;
        slot.sample_publication = status.active_publication;
        slot.frames = status.frames;
        slot.rate = status.engine_sample_rate;
        slot.channels = status.channels;
        slot.regions.push_back(std::move(region));
    }
    std::array<std::optional<SliceId>, 16> pads{};
    std::array<std::optional<RegionId>, 16> region_pads{};
    for (std::size_t pad = 0; pad < pads.size(); ++pad) {
        if (!valid_playback_settings(project.pad_settings[pad])) {
            error = "preset contains invalid pad playback settings";
            return false;
        }
        if (project.pads[pad])
            for (const auto &[id, slot] : restored) {
                (void)id;
                if (std::any_of(slot.slices.begin(), slot.slices.end(),
                                [&](const auto &slice) {
                                    return slice.id == *project.pads[pad];
                                })) {
                    pads[pad] = project.pads[pad];
                    break;
                }
            }
        if (project.region_pads[pad])
            for (const auto &[id, slot] : restored) {
                (void)id;
                if (std::any_of(slot.regions.begin(), slot.regions.end(),
                                [&](const auto &region) {
                                    return region.id ==
                                           *project.region_pads[pad];
                                })) {
                    region_pads[pad] = project.region_pads[pad];
                    pads[pad].reset();
                    break;
                }
            }
    }
    for (auto &[id, slot] : restored)
        if (const auto sample = samples_.active_sample(SampleSlotId{id}))
            slot.waveform = build_waveform_peaks(*sample);
    slots_ = std::move(restored);
    pads_ = pads;
    region_pads_ = region_pads;
    pad_settings_ = project.pad_settings;
    next_id_ = next;
    next_region_id_ = next_region;
    error.clear();
    return true;
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
    for (const auto &r : project.regions)
        out << "region " << r.id << ' ' << r.slot.value << ' '
            << r.sample_revision << ' ' << r.start_frame << ' ' << r.end_frame
            << ' ' << r.gain << ' ' << static_cast<unsigned>(r.direction)
            << ' ' << static_cast<unsigned>(r.fade_curve) << ' '
            << r.fade_in_frames << ' ' << r.fade_out_frames << ' '
            << (r.loop_enabled ? 1 : 0) << ' ' << r.loop_start_frame << ' '
            << r.loop_end_frame << ' ' << r.loop_crossfade_frames << ' '
            << std::quoted(r.name) << '\n';
    for (std::size_t pad = 0; pad < project.pads.size(); ++pad)
        if (project.pads[pad])
            out << "pad " << pad << ' ' << *project.pads[pad] << '\n';
    for (std::size_t pad = 0; pad < project.region_pads.size(); ++pad)
        if (project.region_pads[pad])
            out << "region-pad " << pad << ' ' << *project.region_pads[pad]
                << '\n';
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
SliceProject load_slice_project(const std::filesystem::path &path,
                                bool require_samples) {
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
            if (require_samples && !std::filesystem::exists(file))
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
        } else if (kind == "region" && version >= 4) {
            SampleRegion region;
            unsigned direction{}, curve{}, loop{};
            if (!(in >> region.id >> region.slot.value >>
                  region.sample_revision >> region.start_frame >>
                  region.end_frame >> region.gain >> direction >> curve >>
                  region.fade_in_frames >> region.fade_out_frames >> loop >>
                  region.loop_start_frame >> region.loop_end_frame >>
                  region.loop_crossfade_frames >> std::quoted(region.name)) ||
                direction > 1 || curve > 1 || loop > 1)
                throw std::runtime_error("invalid region record");
            region.direction = static_cast<RegionDirection>(direction);
            region.fade_curve = static_cast<RegionFadeCurve>(curve);
            region.loop_enabled = loop != 0;
            std::string error;
            if (!valid_region(region, std::numeric_limits<std::uint64_t>::max(),
                              error))
                throw std::runtime_error("invalid region record: " + error);
            result.regions.push_back(std::move(region));
        } else if (kind == "region-pad" && version >= 4) {
            std::size_t pad{};
            RegionId id{};
            if (!(in >> pad >> id) || pad >= result.region_pads.size())
                throw std::runtime_error("invalid region pad record");
            result.region_pads[pad] = id;
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
    std::vector<RegionId> region_ids;
    for (const auto &region : result.regions) {
        if (!region.id || std::find(region_ids.begin(), region_ids.end(),
                                    region.id) != region_ids.end())
            throw std::runtime_error("duplicate or zero region identifier");
        region_ids.push_back(region.id);
    }
    for (std::size_t pad = 0; pad < result.region_pads.size(); ++pad) {
        if (result.region_pads[pad] && result.pads[pad])
            throw std::runtime_error("pad has both slice and region assignments");
        if (result.region_pads[pad] &&
            std::find(region_ids.begin(), region_ids.end(),
                      *result.region_pads[pad]) == region_ids.end())
            throw std::runtime_error("pad references an unknown region");
    }
    return result;
}
} // namespace neurogenic
