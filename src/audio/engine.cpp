#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/recording.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace neurogenic {
AudioEngine::AudioEngine(std::uint32_t rate, std::size_t voice_capacity,
                         std::size_t event_staging_capacity,
                         std::size_t sample_version_capacity)
    : rate_(rate), scheduler_(rate, 120.0), timeline_scheduler_(rate),
      sample_updates_(sample_version_capacity + 1),
      sample_versions_(
          std::make_unique<SampleVersion[]>(sample_version_capacity)),
      sample_version_capacity_(sample_version_capacity),
      voices_(voice_capacity), legacy_events_(event_staging_capacity),
      timeline_events_(event_staging_capacity), delay_(rate), modulation_(rate),
      mixer_(rate) {
    if (rate == 0 || voice_capacity == 0 || event_staging_capacity == 0 ||
        sample_version_capacity == 0)
        throw std::invalid_argument(
            "sample rate and all bounded capacities must be positive");
    for (auto &synth : synths_)
        synth = std::make_unique<SubtractiveSynth>(rate, voice_capacity);
    note_slots_.fill(invalid_sample_slot);
    for (auto &target : note_slices_) {
        target.playback.mode = SamplePlaybackMode::gate;
        target.playback.release_seconds = 0;
    }
    for (std::size_t i = 0; i < default_note_bank_size; ++i)
        note_slots_[36 + i] = SampleSlotId{static_cast<std::uint32_t>(i)};
}

AudioEngine::~AudioEngine() = default;

void AudioEngine::set_sample(Sample sample) {
    set_sample_slot(SampleSlotId{0}, std::move(sample));
    note_slots_.fill(SampleSlotId{0});
}

std::size_t AudioEngine::find_active_sample(SampleSlotId id) const noexcept {
    for (std::size_t i = 0; i < sample_version_capacity_; ++i)
        if (sample_versions_[i].id.load(std::memory_order_acquire) ==
                id.value &&
            sample_versions_[i].state.load(std::memory_order_acquire) ==
                SampleVersionState::active)
            return i;
    return std::numeric_limits<std::size_t>::max();
}

bool AudioEngine::has_sample_slot(SampleSlotId id) const noexcept {
    for (std::size_t i = 0; i < sample_version_capacity_; ++i) {
        const auto state =
            sample_versions_[i].state.load(std::memory_order_acquire);
        if (sample_versions_[i].id.load(std::memory_order_acquire) ==
                id.value &&
            (state == SampleVersionState::queued ||
             state == SampleVersionState::active))
            return true;
    }
    return false;
}

std::size_t AudioEngine::sample_slot_count() const noexcept {
    std::size_t count{};
    for (std::size_t i = 0; i < sample_version_capacity_; ++i) {
        const auto state =
            sample_versions_[i].state.load(std::memory_order_acquire);
        if (state != SampleVersionState::queued &&
            state != SampleVersionState::active)
            continue;
        const auto id = sample_versions_[i].id.load(std::memory_order_acquire);
        bool seen = false;
        for (std::size_t j = 0; j < i; ++j) {
            const auto prior =
                sample_versions_[j].state.load(std::memory_order_acquire);
            if (sample_versions_[j].id.load(std::memory_order_acquire) == id &&
                (prior == SampleVersionState::queued ||
                 prior == SampleVersionState::active)) {
                seen = true;
                break;
            }
        }
        if (!seen)
            ++count;
    }
    return count;
}

void AudioEngine::set_sample_slot(SampleSlotId id, Sample sample) {
    if (id == invalid_sample_slot)
        throw std::invalid_argument("invalid sample slot identifier");
    if (sample.sample_rate != rate_)
        throw std::invalid_argument(
            "prepared sample rate does not match engine rate; use "
            "load_wav(path, engine.sample_rate()) or SampleManager");
    if ((sample.channels != 1 && sample.channels != 2) ||
        sample.interleaved.empty() ||
        sample.interleaved.size() % sample.channels != 0)
        throw std::invalid_argument(
            "sample must contain non-empty mono or stereo interleaved audio");
    if (!publish_sample_slot(id, std::move(sample)))
        throw std::runtime_error("sample update capacity is exhausted");
}

void AudioEngine::reclaim_sample_versions_unlocked() noexcept {
    for (std::size_t i = 0; i < sample_version_capacity_; ++i)
        if (sample_versions_[i].state.load(std::memory_order_acquire) ==
            SampleVersionState::retired) {
            sample_versions_[i].sample.reset();
            sample_versions_[i].id.store(invalid_sample_slot.value,
                                         std::memory_order_release);
            sample_versions_[i].publication.store(0, std::memory_order_release);
            sample_versions_[i].state.store(SampleVersionState::free,
                                            std::memory_order_release);
            reclaimed_sample_versions_.fetch_add(1, std::memory_order_relaxed);
        }
}
void AudioEngine::reclaim_sample_versions() noexcept {
    std::lock_guard lock(sample_update_producer_mutex_);
    reclaim_sample_versions_unlocked();
}

bool AudioEngine::publish_sample_slot(SampleSlotId id, Sample sample,
                                      std::uint64_t *publication) noexcept {
    try {
        return publish_sample_slot(
            id, std::make_shared<const Sample>(std::move(sample)), publication);
    } catch (...) {
        return false;
    }
}

bool AudioEngine::publish_sample_slot(SampleSlotId id,
                                      std::shared_ptr<const Sample> sample,
                                      std::uint64_t *publication) noexcept {
    std::lock_guard lock(sample_update_producer_mutex_);
    if (publication)
        *publication = 0;
    if (id == invalid_sample_slot || !sample || sample->sample_rate != rate_ ||
        (sample->channels != 1 && sample->channels != 2) ||
        sample->interleaved.empty() ||
        sample->interleaved.size() % sample->channels != 0)
        return false;
    reclaim_sample_versions_unlocked();
    std::size_t version = sample_version_capacity_;
    for (std::size_t i = 0; i < sample_version_capacity_; ++i)
        if (sample_versions_[i].state.load(std::memory_order_acquire) ==
            SampleVersionState::free) {
            version = i;
            break;
        }
    if (version == sample_version_capacity_) {
        dropped_sample_updates_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    sample_versions_[version].sample = std::move(sample);
    sample_versions_[version].id.store(id.value, std::memory_order_release);
    sample_versions_[version].state.store(SampleVersionState::queued,
                                          std::memory_order_release);
    if (next_sample_update_sequence_ ==
            std::numeric_limits<std::uint64_t>::max() ||
        !sample_updates_.push({SampleUpdate::Type::install, id, version,
                               next_sample_update_sequence_ + 1})) {
        sample_versions_[version].state.store(SampleVersionState::free,
                                              std::memory_order_release);
        sample_versions_[version].sample.reset();
        sample_versions_[version].id.store(invalid_sample_slot.value,
                                           std::memory_order_release);
        dropped_sample_updates_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ++next_sample_update_sequence_;
    sample_versions_[version].publication.store(next_sample_update_sequence_,
                                                std::memory_order_release);
    if (publication)
        *publication = next_sample_update_sequence_;
    return true;
}

bool AudioEngine::unload_sample_slot(SampleSlotId id,
                                     std::uint64_t *publication) noexcept {
    std::lock_guard lock(sample_update_producer_mutex_);
    if (publication)
        *publication = 0;
    if (id == invalid_sample_slot ||
        next_sample_update_sequence_ ==
            std::numeric_limits<std::uint64_t>::max() ||
        !sample_updates_.push({SampleUpdate::Type::unload, id, 0,
                               next_sample_update_sequence_ + 1})) {
        dropped_sample_updates_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ++next_sample_update_sequence_;
    if (publication)
        *publication = next_sample_update_sequence_;
    return true;
}

bool AudioEngine::map_note_to_slot(std::uint8_t note,
                                   SampleSlotId id) noexcept {
    if (processing_started_.load(std::memory_order_acquire) ||
        !has_sample_slot(id))
        return false;
    note_slots_[note] = id;
    return true;
}

void AudioEngine::trigger(std::uint8_t note, float velocity,
                          std::uint32_t frame_offset) noexcept {
    Trigger event{};
    event.note = note;
    event.velocity = velocity;
    event.frame_offset = frame_offset;
    event.order = next_order_.fetch_add(1, std::memory_order_relaxed);
    if (!triggers_.push(event))
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
}
bool AudioEngine::trigger_pad(std::uint8_t note, float velocity, bool note_on,
                              float pitch_offset,
                              std::uint32_t frame_offset) noexcept {
    if (!std::isfinite(velocity) || velocity < 0 || velocity > 1 ||
        !std::isfinite(pitch_offset) || pitch_offset < -96 || pitch_offset > 96)
        return false;
    Trigger event{};
    event.note = note;
    event.velocity = velocity;
    event.frame_offset = frame_offset;
    event.order = next_order_.fetch_add(1, std::memory_order_relaxed);
    event.note_on = note_on;
    event.pad_event = true;
    event.pitch_offset = pitch_offset;
    if (!triggers_.push(event)) {
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::trigger_slice(SampleSlotId slot, std::uint64_t publication,
                                std::uint64_t start_frame,
                                std::uint64_t end_frame, float slice_gain,
                                float velocity,
                                std::uint32_t frame_offset) noexcept {
    if (slot == invalid_sample_slot || publication == 0 ||
        start_frame >= end_frame || !std::isfinite(slice_gain) ||
        slice_gain < 0 || !std::isfinite(velocity) || velocity < 0)
        return false;
    Trigger event{};
    event.velocity = velocity;
    event.frame_offset = frame_offset;
    event.order = next_order_.fetch_add(1, std::memory_order_relaxed);
    event.slot = slot;
    event.publication = publication;
    event.start_frame = start_frame;
    event.end_frame = end_frame;
    event.slice_gain = slice_gain;
    event.sliced = true;
    if (!triggers_.push(event)) {
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::stop_slice_previews() noexcept {
    Trigger event{};
    event.order = next_order_.fetch_add(1, std::memory_order_relaxed);
    event.stop_previews = true;
    if (!triggers_.push(event)) {
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::map_note_to_slice(std::uint8_t note, SampleSlotId slot,
                                    std::uint64_t publication,
                                    std::uint64_t start_frame,
                                    std::uint64_t end_frame, float gain,
                                    SamplePlaybackSettings playback) noexcept {
    if (slot == invalid_sample_slot || publication == 0 ||
        start_frame >= end_frame || !std::isfinite(gain) || gain < 0)
        return false;
    playback.gain *= gain;
    return slice_map_commands_.push(
        {SliceMapCommand::Type::map,
         note,
         {slot, publication, start_frame, end_frame, 1.0F, playback, true}});
}
bool AudioEngine::clear_note_slice(std::uint8_t note) noexcept {
    return slice_map_commands_.push({SliceMapCommand::Type::clear, note, {}});
}
bool AudioEngine::configure_note_playback(
    std::uint8_t note, SamplePlaybackSettings settings) noexcept {
    SliceTarget target{};
    target.playback = settings;
    return slice_map_commands_.push(
        {SliceMapCommand::Type::settings, note, target});
}
void AudioEngine::trigger_synth(std::uint8_t note, float velocity,
                                bool note_on,
                                InstrumentInstanceId instance) noexcept {
    if (instance == 0 || instance > synths_.size() ||
        !synth_triggers_.push({note, velocity, note_on, instance}))
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
}
bool AudioEngine::set_synth_parameter(SynthParameter parameter,
                                      float value,
                                      InstrumentInstanceId instance) noexcept {
    if (instance == 0 || instance > synths_.size())
        return false;
    if (!dsp_commands_.push({DspCommand::Target::synth,
                             static_cast<std::uint32_t>(parameter), value,
                             instance})) {
        dropped_dsp_commands_.fetch_add(1);
        return false;
    }
    return true;
}
bool AudioEngine::set_delay_parameter(DelayParameter parameter,
                                      float value) noexcept {
    if (!dsp_commands_.push({DspCommand::Target::delay,
                             static_cast<std::uint32_t>(parameter), value})) {
        dropped_dsp_commands_.fetch_add(1);
        return false;
    }
    return true;
}
bool AudioEngine::set_delay_bypass(bool value) noexcept {
    if (!dsp_commands_.push(
            {DspCommand::Target::delay_bypass, 0, value ? 1.0F : 0.0F})) {
        dropped_dsp_commands_.fetch_add(1);
        return false;
    }
    return true;
}
bool AudioEngine::publish_modulation(
    const ModulationConfiguration &configuration) noexcept {
    if (!modulation_configurations_.push(configuration)) {
        dropped_dsp_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}
bool AudioEngine::set_modulation_input(ModulationSourceId source,
                                       float value) noexcept {
    if (!dsp_commands_.push({DspCommand::Target::modulation_input,
                             static_cast<std::uint32_t>(source), value})) {
        dropped_dsp_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool AudioEngine::queue_pattern(const Pattern &pattern) noexcept {
    if (!valid_pattern(pattern))
        return false;
    if (!patterns_.push(pattern)) {
        dropped_patterns_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool AudioEngine::queue_timeline(const Timeline &timeline, std::string &error) {
    reclaim_retired_plans();
    std::size_t slot = timeline_plans_.size();
    for (std::size_t i = 0; i < timeline_plans_.size(); ++i)
        if (timeline_plans_[i].state.load(std::memory_order_acquire) ==
            PlanState::free) {
            slot = i;
            break;
        }
    if (slot == timeline_plans_.size()) {
        dropped_timelines_.fetch_add(1, std::memory_order_relaxed);
        error = "timeline plan slots are full";
        return false;
    }
    auto plan = std::make_unique<PlaybackPlan>();
    if (!compile_timeline(timeline, *plan, error))
        return false;
    timeline_plans_[slot].plan = std::move(plan);
    timeline_plans_[slot].state.store(PlanState::queued,
                                      std::memory_order_release);
    if (!timelines_.push(static_cast<std::uint8_t>(slot))) {
        timeline_plans_[slot].state.store(PlanState::free,
                                          std::memory_order_release);
        timeline_plans_[slot].plan.reset();
        dropped_timelines_.fetch_add(1, std::memory_order_relaxed);
        error = "timeline queue is full";
        return false;
    }
    error.clear();
    return true;
}

void AudioEngine::reclaim_retired_plans() noexcept {
    for (auto &slot : timeline_plans_)
        if (slot.state.load(std::memory_order_acquire) == PlanState::retired) {
            slot.plan.reset();
            slot.state.store(PlanState::free, std::memory_order_release);
            reclaimed_timeline_plans_.fetch_add(1, std::memory_order_relaxed);
        }
}
void AudioEngine::play() noexcept {
    if (!transport_commands_.push(TransportCommand::play))
        dropped_transport_commands_.fetch_add(1, std::memory_order_relaxed);
}
void AudioEngine::stop() noexcept {
    if (!transport_commands_.push(TransportCommand::stop))
        dropped_transport_commands_.fetch_add(1, std::memory_order_relaxed);
}
void AudioEngine::restart() noexcept {
    if (!transport_commands_.push(TransportCommand::restart))
        dropped_transport_commands_.fetch_add(1, std::memory_order_relaxed);
}
bool AudioEngine::set_fill(bool active) noexcept {
    if (!transport_commands_.push(active ? TransportCommand::fill_on
                                         : TransportCommand::fill_off)) {
        dropped_transport_commands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void AudioEngine::start_voice(std::uint8_t note, float velocity, TrackId track,
                              EventId event, std::uint64_t occurrence,
                              bool timeline_owned, const SliceTarget *slice,
                              bool preview, float pitch_offset) noexcept {
    const auto slot = slice && slice->enabled ? slice->slot : note_slots_[note];
    const auto sample_index = find_active_sample(slot);
    if (sample_index == std::numeric_limits<std::size_t>::max()) {
        missing_slot_triggers_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto *sample = sample_versions_[sample_index].sample.get();
    const auto sample_frames = sample->frames();
    if (slice && slice->enabled &&
        (sample_versions_[sample_index].publication.load(
             std::memory_order_acquire) != slice->publication ||
         slice->start_frame >= slice->end_frame ||
         slice->end_frame > sample_frames)) {
        missing_slot_triggers_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto settings = slice ? slice->playback : SamplePlaybackSettings{};
    if (settings.mode == SamplePlaybackMode::toggle)
        for (auto &voice : voices_)
            if (voice.active && voice.trigger_note == note &&
                voice.mode == SamplePlaybackMode::toggle) {
                if (voice.envelope_stage == Voice::EnvelopeStage::release) {
                    voice.active = false;
                    continue;
                }
                release_voice(voice);
                return;
            }
    if (settings.choke_group != 0)
        for (auto &voice : voices_)
            if (voice.active && voice.choke_group == settings.choke_group)
                release_voice(voice, !settings.choke_release);
    auto selected = voices_.size();
    for (std::size_t i = 0; i < voices_.size(); ++i)
        if (!voices_[i].active) {
            selected = i;
            break;
        }
    if (selected == voices_.size()) {
        selected = 0;
        for (std::size_t i = 1; i < voices_.size(); ++i)
            if (voices_[i].age < voices_[selected].age)
                selected = i;
        stolen_voices_.fetch_add(1, std::memory_order_relaxed);
    }
    const auto start = slice && slice->enabled ? slice->start_frame : 0;
    const auto end = slice && slice->enabled ? slice->end_frame : sample_frames;
    auto &voice = voices_[selected];
    voice = {};
    voice.sample = sample;
    voice.sample_version = sample_index;
    voice.start_frame = start;
    voice.end_frame = end;
    voice.reverse = settings.reverse;
    voice.position = settings.reverse ? static_cast<double>(end - 1)
                                      : static_cast<double>(start);
    voice.increment = std::exp2(
        (settings.semitones + settings.fine_cents / 100.0F + pitch_offset) /
        12.0F);
    const auto time_ratio = std::clamp(
        RealtimeTimeStretcher::resolve_time_ratio(
            settings.stretch_ratio, settings.source_beats, settings.source_bpm,
            end - start, rate_, project_tempo_bpm_),
        0.25F, 4.0F);
    voice.stretch_configuration = {settings.stretch_mode,
                                   settings.stretch_quality, time_ratio,
                                   static_cast<float>(voice.increment), rate_};
    voice.stretched = !settings.reverse &&
                      settings.stretch_mode == StretchMode::realtime &&
                      RealtimeTimeStretcher::valid(voice.stretch_configuration);
    if (voice.stretched) {
        RealtimeTimeStretcher::reset(voice.stretch_state,
                                     voice.stretch_configuration);
        voice.stretch_duration = static_cast<double>(end - start) * time_ratio;
    }
    voice.gain = velocity * settings.gain * (slice ? slice->gain : 1.0F);
    voice.sustain = settings.sustain;
    if (settings.attack_seconds > 0) {
        voice.envelope_level = 0;
        voice.envelope_stage = Voice::EnvelopeStage::attack;
        voice.attack_step =
            1.0F / std::max(1.0F, settings.attack_seconds * rate_);
    } else if (settings.decay_seconds > 0) {
        voice.envelope_level = 1;
        voice.envelope_stage = Voice::EnvelopeStage::decay;
    } else {
        voice.envelope_level = settings.sustain;
        voice.envelope_stage = Voice::EnvelopeStage::sustain;
    }
    voice.decay_step = (1.0F - settings.sustain) /
                       std::max(1.0F, settings.decay_seconds * rate_);
    voice.release_samples =
        std::max(1.0F, settings.release_seconds * static_cast<float>(rate_));
    voice.release_step = 1.0F / voice.release_samples;
    voice.age = next_voice_age_++;
    voice.track = track;
    voice.event = event;
    voice.occurrence = occurrence;
    voice.pitch = note;
    voice.trigger_note = note;
    voice.choke_group = settings.choke_group;
    voice.mode = settings.mode;
    voice.choke_release = settings.choke_release;
    voice.timeline_owned = timeline_owned;
    voice.preview = preview;
    voice.active = true;
}

void AudioEngine::release_voice(Voice &voice, bool immediate) noexcept {
    if (immediate || voice.release_step >= 1.0F) {
        voice.active = false;
        return;
    }
    voice.release_step = voice.envelope_level / voice.release_samples;
    voice.envelope_stage = Voice::EnvelopeStage::release;
}

void AudioEngine::release_note(std::uint8_t note, TrackId track, EventId event,
                               std::uint64_t occurrence,
                               bool timeline_owned) noexcept {
    for (auto &voice : voices_)
        if (voice.active && voice.trigger_note == note &&
            (!timeline_owned ||
             (voice.timeline_owned && voice.track == track &&
              voice.event == event && voice.occurrence == occurrence))) {
            if (voice.mode == SamplePlaybackMode::gate)
                release_voice(voice);
            if (timeline_owned)
                return;
        }
}

void AudioEngine::stop_timeline_voice(TrackId track, EventId event,
                                      std::uint64_t occurrence) noexcept {
    for (auto &voice : voices_)
        if (voice.active && voice.timeline_owned && voice.track == track &&
            voice.event == event && voice.occurrence == occurrence) {
            release_note(voice.trigger_note, track, event, occurrence, true);
            return;
        }
}

void AudioEngine::stop_voices(bool timeline_only) noexcept {
    for (auto &voice : voices_)
        if (!timeline_only || voice.timeline_owned)
            voice.active = false;
    for (auto &synth : synths_)
        synth->reset();
    synth_active_.fill(false);
}

void AudioEngine::process(std::span<float> left,
                          std::span<float> right) noexcept {
    const auto callback_frame = callback_frame_.load(std::memory_order_relaxed);
    const auto monitoring = callback_monitor_.enabled();
    const auto callback_begin = monitoring
                                    ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{};
    processing_started_.store(true, std::memory_order_release);
    const auto frames = std::min(left.size(), right.size());
    std::fill(left.begin(), left.end(), 0);
    std::fill(right.begin(), right.end(), 0);
    mixer_.begin_block(frames);
    if (duplex_input_ && duplex_input_channels_) {
        const auto input_frames =
            std::min(frames, duplex_input_samples_ / duplex_input_channels_);
        for (std::size_t frame = 0; frame < input_frames; ++frame)
            for (std::uint16_t channel = 0; channel < duplex_input_channels_ &&
                                            channel < maximum_mixer_inputs;
                 ++channel)
                mixer_.add_input(
                    channel, frame,
                    duplex_input_[frame * duplex_input_channels_ + channel]);
    }

    SampleUpdate sample_update;
    while (sample_updates_.pop(sample_update)) {
        for (std::size_t i = 0; i < sample_version_capacity_; ++i)
            if (sample_versions_[i].id.load(std::memory_order_acquire) ==
                    sample_update.id.value &&
                sample_versions_[i].state.load(std::memory_order_acquire) ==
                    SampleVersionState::active)
                sample_versions_[i].state.store(SampleVersionState::retiring,
                                                std::memory_order_release);
        if (sample_update.type == SampleUpdate::Type::install)
            sample_versions_[sample_update.version].state.store(
                SampleVersionState::active, std::memory_order_release);
        applied_sample_update_sequence_.store(sample_update.sequence,
                                              std::memory_order_release);
    }
    SliceMapCommand slice_map;
    while (slice_map_commands_.pop(slice_map)) {
        if (slice_map.type == SliceMapCommand::Type::map)
            note_slices_[slice_map.note] = slice_map.target;
        else if (slice_map.type == SliceMapCommand::Type::clear)
            note_slices_[slice_map.note].enabled = false;
        else
            note_slices_[slice_map.note].playback = slice_map.target.playback;
    }

    Pattern pattern;
    ModulationConfiguration modulation_configuration;
    while (modulation_configurations_.pop(modulation_configuration)) {
        if (modulation_.configure(modulation_configuration))
            for (std::size_t instance = 0; instance < synths_.size(); ++instance)
                (void)synths_[instance]->configure_voice_modulation(
                    modulation_configuration,
                    static_cast<std::uint32_t>(instance + 1));
    }
    DspCommand dsp;
    while (dsp_commands_.pop(dsp)) {
        if (dsp.target == DspCommand::Target::synth)
            synths_[dsp.instance - 1]->set_parameter(dsp.parameter, dsp.value);
        else if (dsp.target == DspCommand::Target::delay)
            delay_.set_parameter(dsp.parameter, dsp.value);
        else if (dsp.target == DspCommand::Target::delay_bypass)
            delay_.set_bypass(dsp.value >= 0.5F);
        else
            modulation_.set_input(
                static_cast<ModulationSourceId>(dsp.parameter), dsp.value);
    }
    SynthTrigger synth_trigger;
    while (synth_triggers_.pop(synth_trigger)) {
        if (synth_trigger.note_on)
            synths_[synth_trigger.instance - 1]->set_modulation_frame(
                callback_frame);
        if (synth_trigger.note_on)
            synths_[synth_trigger.instance - 1]->note_on(
                synth_trigger.note, synth_trigger.velocity);
        else
            synths_[synth_trigger.instance - 1]->note_off(synth_trigger.note);
        if (synth_trigger.note_on)
            synth_active_[synth_trigger.instance - 1] = true;
        if (synth_trigger.note_on)
            modulation_.note_on(0, synth_trigger.velocity);
        else
            modulation_.note_off(0);
    }
    while (patterns_.pop(pattern))
        scheduler_.request_pattern(pattern);
    std::uint8_t timeline_slot{};
    while (timelines_.pop(timeline_slot)) {
        stop_voices(true);
        auto &next = timeline_plans_[timeline_slot];
        auto *timeline = next.plan.get();
        timeline_scheduler_.set_plan(timeline, transport_frame_);
        scheduler_.set_tempo(timeline->transport.microseconds_per_quarter);
        project_tempo_bpm_ = static_cast<float>(
            60000000.0 / timeline->transport.microseconds_per_quarter);
        modulation_.set_tempo(project_tempo_bpm_);
        for (auto &synth : synths_)
            synth->set_tempo(project_tempo_bpm_);
        modulation_.clear_automation();
        next.state.store(PlanState::active, std::memory_order_release);
        if (active_timeline_slot_ >= 0)
            timeline_plans_[static_cast<std::size_t>(active_timeline_slot_)]
                .state.store(PlanState::retired, std::memory_order_release);
        active_timeline_slot_ = timeline_slot;
    }
    TransportCommand command;
    while (transport_commands_.pop(command)) {
        if (command == TransportCommand::play)
            transport_playing_ = true;
        else if (command == TransportCommand::stop) {
            transport_playing_ = false;
            stop_voices();
            modulation_.clear_automation();
        } else if (command == TransportCommand::restart) {
            transport_frame_ = 0;
            transport_playing_ = true;
            stop_voices();
            modulation_.restart();
        } else {
            const auto fill = command == TransportCommand::fill_on;
            timeline_scheduler_.set_fill(fill);
            published_fill_.store(fill, std::memory_order_release);
        }
    }

    Trigger incoming;
    while (pending_trigger_count_ < pending_triggers_.size() &&
           triggers_.pop(incoming))
        pending_triggers_[pending_trigger_count_++] = incoming;
    for (std::size_t i = 1; i < pending_trigger_count_; ++i) {
        auto value = pending_triggers_[i];
        auto j = i;
        while (j > 0 &&
               (pending_triggers_[j - 1].frame_offset > value.frame_offset ||
                (pending_triggers_[j - 1].frame_offset == value.frame_offset &&
                 pending_triggers_[j - 1].order > value.order))) {
            pending_triggers_[j] = pending_triggers_[j - 1];
            --j;
        }
        pending_triggers_[j] = value;
    }

    const auto scheduled_count =
        transport_playing_
            ? scheduler_.schedule(transport_frame_,
                                  static_cast<std::uint32_t>(frames),
                                  legacy_events_)
            : 0;
    const auto timeline_count =
        transport_playing_
            ? timeline_scheduler_.schedule(transport_frame_,
                                           static_cast<std::uint32_t>(frames),
                                           timeline_events_)
            : 0;
    dropped_timeline_events_.store(timeline_scheduler_.dropped_events(),
                                   std::memory_order_release);
    dropped_legacy_events_.store(scheduler_.dropped_events(),
                                 std::memory_order_release);
    std::size_t external_index{}, scheduled_index{}, timeline_index{};
    const auto instrument_begin = monitoring
                                      ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
    std::array<std::uint64_t, maximum_mixer_tracks> mixer_tracks{};
    std::array<float, maximum_mixer_tracks> synth_tracks{};
    const auto mixer_track_count =
        std::min(mixer_.active_track_count(), mixer_tracks.size());
    for (std::size_t i = 0; i < mixer_track_count; ++i)
        mixer_tracks[i] = mixer_.active_track_id(i);

    for (std::size_t frame = 0; frame < frames; ++frame) {
        modulation_.process_frame([&](ParameterDestination destination,
                                      float value) noexcept {
            if (destination.target == ParameterTarget::subtractive_synth) {
                for (std::size_t instance = 0; instance < synths_.size();
                     ++instance)
                    if (destination.instance == 0 ||
                        destination.instance == instance + 1)
                        (void)synths_[instance]->set_parameter(
                            destination.parameter, value);
            } else if (destination.target == ParameterTarget::stereo_delay)
                (void)delay_.set_parameter(destination.parameter, value);
        });
        while (external_index < pending_trigger_count_ &&
               pending_triggers_[external_index].frame_offset == frame) {
            const auto &event = pending_triggers_[external_index++];
            if (event.stop_previews) {
                for (auto &voice : voices_)
                    if (voice.active && voice.preview)
                        voice.active = false;
                continue;
            }
            if (event.sliced) {
                SliceTarget target{};
                target.slot = event.slot;
                target.publication = event.publication;
                target.start_frame = event.start_frame;
                target.end_frame = event.end_frame;
                target.gain = event.slice_gain;
                target.enabled = true;
                start_voice(event.note, event.velocity, 0, 0, 0, false, &target,
                            true);
            } else {
                const auto *target = &note_slices_[event.note];
                if (event.pad_event && !event.note_on)
                    release_note(event.note);
                else
                    start_voice(event.note, event.velocity, 0, 0, 0, false,
                                target, false, event.pitch_offset);
            }
        }
        while (scheduled_index < scheduled_count &&
               legacy_events_[scheduled_index].frame == frame) {
            const auto &event = legacy_events_[scheduled_index++];
            start_voice(event.note, event.velocity, 0, 0, 0, false,
                        &note_slices_[event.note]);
        }
        while (timeline_index < timeline_count &&
               timeline_events_[timeline_index].frame == frame) {
            const auto &event = timeline_events_[timeline_index++];
            if (event.type == MusicalEventType::parameter) {
                modulation_.set_automation(
                    {static_cast<ParameterTarget>(event.parameter_target),
                     event.parameter_id,
                     event.parameter_target == static_cast<std::uint32_t>(
                                                   ParameterTarget::subtractive_synth)
                         ? event.instrument_instance
                         : 0},
                    event.value);
                if (event.parameter_target ==
                    static_cast<std::uint32_t>(
                        ParameterTarget::subtractive_synth))
                    synths_[event.instrument_instance - 1]->set_parameter(
                        event.parameter_id, event.value);
            } else if (event.instrument == subtractive_instrument_key) {
                const NoteIdentity owner{event.track, event.target_event_id,
                                         event.occurrence};
                if (event.type == MusicalEventType::note_on)
                    synths_[event.instrument_instance - 1]
                        ->set_modulation_frame(transport_frame_ + frame);
                if (event.type == MusicalEventType::note_on)
                    synths_[event.instrument_instance - 1]->note_on(
                        event.pitch, event.value, owner);
                else if (event.type == MusicalEventType::note_off)
                    synths_[event.instrument_instance - 1]->note_off(
                        event.pitch, owner);
                if (event.type == MusicalEventType::note_on)
                    synth_active_[event.instrument_instance - 1] = true;
                if (event.type == MusicalEventType::note_on)
                    modulation_.note_on(event.track, event.value);
                else if (event.type == MusicalEventType::note_off)
                    modulation_.note_off(event.track);
            } else if (event.instrument == sample_instrument_key) {
                if (event.type == MusicalEventType::note_on)
                    start_voice(event.pitch, event.value, event.track,
                                event.target_event_id, event.occurrence, true,
                                &note_slices_[event.pitch]);
                else if (event.type == MusicalEventType::note_off)
                    stop_timeline_voice(event.track, event.target_event_id,
                                        event.occurrence);
            } else
                missing_slot_triggers_.fetch_add(1, std::memory_order_relaxed);
        }
        for (auto &voice : voices_) {
            if (!voice.active)
                continue;
            const auto &sample = *voice.sample;
            if ((voice.stretched &&
                 voice.stretch_state.output_frame >= voice.stretch_duration) ||
                (!voice.stretched &&
                 (voice.position < static_cast<double>(voice.start_frame) ||
                  voice.position >= static_cast<double>(voice.end_frame)))) {
                voice.active = false;
                continue;
            }
            float sample_left{}, sample_right{};
            if (voice.stretched) {
                sample_left = RealtimeTimeStretcher::process_channel(
                    sample, voice.start_frame, voice.end_frame,
                    voice.stretch_configuration, voice.stretch_state, 0);
                sample_right = RealtimeTimeStretcher::process_channel(
                    sample, voice.start_frame, voice.end_frame,
                    voice.stretch_configuration, voice.stretch_state, 1);
            } else {
                const auto base = static_cast<std::uint64_t>(voice.position);
                const auto fraction = static_cast<float>(voice.position - base);
                const auto adjacent =
                    voice.reverse
                        ? (base > voice.start_frame ? base - 1 : base)
                        : (base + 1 < voice.end_frame ? base + 1 : base);
                const auto interpolate = [&](std::uint16_t channel) {
                    const auto actual = sample.channels > 1 ? channel : 0;
                    const auto first =
                        sample.interleaved[base * sample.channels + actual];
                    const auto second =
                        sample.interleaved[adjacent * sample.channels + actual];
                    return first + (second - first) * fraction;
                };
                sample_left = interpolate(0);
                sample_right = interpolate(1);
            }
            const auto level = voice.gain * voice.envelope_level;
            const auto routed =
                voice.timeline_owned &&
                mixer_.add_track(voice.track, frame, sample_left * level,
                                 sample_right * level);
            if (!routed) {
                left[frame] += sample_left * level;
                right[frame] += sample_right * level;
            }
            if (voice.envelope_stage == Voice::EnvelopeStage::attack) {
                voice.envelope_level += voice.attack_step;
                if (voice.envelope_level >= 1.0F) {
                    voice.envelope_level = 1.0F;
                    voice.envelope_stage = Voice::EnvelopeStage::decay;
                }
            } else if (voice.envelope_stage == Voice::EnvelopeStage::decay) {
                voice.envelope_level -= voice.decay_step;
                if (voice.envelope_level <= voice.sustain) {
                    voice.envelope_level = voice.sustain;
                    voice.envelope_stage = Voice::EnvelopeStage::sustain;
                }
            } else if (voice.envelope_stage == Voice::EnvelopeStage::release) {
                voice.envelope_level -= voice.release_step;
                if (voice.envelope_level <= 0) {
                    voice.envelope_level = 0;
                    voice.active = false;
                    continue;
                }
            }
            if (voice.stretched)
                RealtimeTimeStretcher::advance(voice.stretch_state);
            else
                voice.position +=
                    voice.reverse ? -voice.increment : voice.increment;
        }
        for (std::size_t synth_index = 0; synth_index < synths_.size();
             ++synth_index) {
            if (!synth_active_[synth_index])
                continue;
            auto &synth = synths_[synth_index];
            float synth_unrouted{};
            synth->render_sample_tracks({mixer_tracks.data(), mixer_track_count},
                                        {synth_tracks.data(), mixer_track_count},
                                        synth_unrouted);
            left[frame] += synth_unrouted;
            right[frame] += synth_unrouted;
            for (std::size_t i = 0; i < mixer_track_count; ++i)
                (void)mixer_.add_track(mixer_tracks[i], frame, synth_tracks[i],
                                       synth_tracks[i]);
            synth_active_[synth_index] = synth->active();
        }
    }

    const auto remaining = pending_trigger_count_ - external_index;
    if (monitoring)
        instrument_nanoseconds_.store(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - instrument_begin)
                    .count()),
            std::memory_order_relaxed);
    for (std::size_t i = 0; i < remaining; ++i) {
        pending_triggers_[i] = pending_triggers_[external_index + i];
        pending_triggers_[i].frame_offset -= static_cast<std::uint32_t>(frames);
    }
    pending_trigger_count_ = remaining;
    for (std::size_t version = 0; version < sample_version_capacity_; ++version)
        if (sample_versions_[version].state.load(std::memory_order_acquire) ==
            SampleVersionState::retiring) {
            bool referenced = false;
            for (const auto &voice : voices_)
                if (voice.active && voice.sample_version == version) {
                    referenced = true;
                    break;
                }
            if (!referenced)
                sample_versions_[version].state.store(
                    SampleVersionState::retired, std::memory_order_release);
        }
    if (transport_playing_) {
        if (frames >
            std::numeric_limits<std::uint64_t>::max() - transport_frame_) {
            transport_frame_ = std::numeric_limits<std::uint64_t>::max();
            transport_playing_ = false;
            stop_voices();
        } else
            transport_frame_ += frames;
    }
    published_frame_.store(transport_frame_, std::memory_order_release);
    published_playing_.store(transport_playing_, std::memory_order_release);
    mixer_.process(left.first(frames), right.first(frames),
                   physical_output_ ? std::span<float>{physical_output_,
                                                       physical_output_samples_}
                                    : std::span<float>{},
                   physical_output_channels_);
    if (auto *recorder = recording_.load(std::memory_order_acquire)) {
        for (std::size_t i = 0; i < mixer_track_count; ++i)
            recorder->capture_internal(
                RecordingSource::track_channel, mixer_.track_left(i),
                mixer_.track_right(i), callback_frame, mixer_tracks[i]);
        for (std::size_t i = 0; i < mixer_aux_count; ++i)
            recorder->capture_internal(RecordingSource::auxiliary_bus,
                                       mixer_.aux_left(i), mixer_.aux_right(i),
                                       callback_frame, i);
        for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
            recorder->capture_internal(RecordingSource::external_fx_return,
                                       mixer_.external_return_left(i),
                                       mixer_.external_return_right(i),
                                       callback_frame, i);
            recorder->capture_internal(
                RecordingSource::external_fx_mix, mixer_.external_mix_left(i),
                mixer_.external_mix_right(i), callback_frame, i);
        }
        recorder->capture_internal(RecordingSource::master_pre_effects,
                                   left.first(frames), right.first(frames),
                                   callback_frame);
    }
    mixer_.apply_master_gain(left.first(frames), right.first(frames));
    const auto effect_begin = monitoring
                                  ? std::chrono::steady_clock::now()
                                  : std::chrono::steady_clock::time_point{};
    effect_.process(left.first(frames), right.first(frames));
    std::size_t delay_frame{};
    for (std::size_t i = 0; i < timeline_count; ++i) {
        const auto &event = timeline_events_[i];
        if (event.type != MusicalEventType::parameter ||
            event.parameter_target !=
                static_cast<std::uint32_t>(ParameterTarget::stereo_delay))
            continue;
        const auto event_frame = std::min<std::size_t>(event.frame, frames);
        if (event_frame > delay_frame)
            delay_.process(
                left.subspan(delay_frame, event_frame - delay_frame),
                right.subspan(delay_frame, event_frame - delay_frame));
        delay_.set_parameter(event.parameter_id, event.value);
        delay_frame = event_frame;
    }
    if (delay_frame < frames)
        delay_.process(left.subspan(delay_frame, frames - delay_frame),
                       right.subspan(delay_frame, frames - delay_frame));
    if (auto *recorder = recording_.load(std::memory_order_acquire))
        recorder->capture_internal(RecordingSource::master_post_effects,
                                   left.first(frames), right.first(frames),
                                   callback_frame);
    mixer_.route_master_output(
        left.first(frames), right.first(frames),
        physical_output_
            ? std::span<float>{physical_output_, physical_output_samples_}
            : std::span<float>{},
        physical_output_channels_);
    if (monitoring)
        effect_nanoseconds_.store(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - effect_begin)
                    .count()),
            std::memory_order_relaxed);
    buffer_frames_.store(static_cast<std::uint32_t>(frames),
                         std::memory_order_relaxed);
    if (monitoring) {
        std::size_t active_voices{};
        for (const auto &voice : voices_)
            active_voices += voice.active ? 1U : 0U;
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - callback_begin)
                .count();
        callback_monitor_.record(
            static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)),
            static_cast<std::uint32_t>(frames), rate_, active_voices);
    }
    callback_frame_.store(callback_frame + frames, std::memory_order_release);
}

void AudioEngine::capture_external_input(std::span<const float> interleaved,
                                         std::uint16_t channels) noexcept {
    if (auto *recorder = recording_.load(std::memory_order_acquire))
        recorder->capture_external(
            interleaved, channels,
            callback_frame_.load(std::memory_order_relaxed));
}
void AudioEngine::recording_device_stopped() noexcept {
    if (auto *recorder = recording_.load(std::memory_order_acquire))
        recorder->device_stopped();
}

void AudioEngine::process_duplex(std::span<float> left, std::span<float> right,
                                 std::span<const float> input,
                                 std::uint16_t channels) noexcept {
    capture_external_input(input, channels);
    duplex_input_ = input.data();
    duplex_input_samples_ = input.size();
    duplex_input_channels_ = channels;
    process(left, right);
    duplex_input_ = nullptr;
    duplex_input_samples_ = 0;
    duplex_input_channels_ = 0;
    auto *recorder = recording_.load(std::memory_order_acquire);
    if (!recorder || !recorder->monitoring_active() || channels == 0)
        return;
    const auto frames =
        std::min({left.size(), right.size(), input.size() / channels});
    for (std::size_t i = 0; i < frames; ++i) {
        const auto l = input[i * channels];
        const auto r = channels > 1 ? input[i * channels + 1] : l;
        left[i] += l;
        right[i] += r;
    }
}

void AudioEngine::process_io(std::span<float> output,
                             std::uint16_t output_channels,
                             std::span<const float> input,
                             std::uint16_t input_channels) noexcept {
    if (output_channels == 0)
        return;
    const auto frames = output.size() / output_channels;
    std::array<float, maximum_mixer_block> left{}, right{};
    const auto count = std::min(frames, left.size());
    std::fill(output.begin(), output.end(), 0.0F);
    physical_output_ = output.data();
    physical_output_samples_ = output.size();
    physical_output_channels_ = static_cast<std::uint8_t>(
        std::min<std::uint16_t>(output_channels, maximum_mixer_outputs));
    duplex_input_ = input.data();
    duplex_input_samples_ = input.size();
    duplex_input_channels_ = input_channels;
    capture_external_input(input, input_channels);
    process({left.data(), count}, {right.data(), count});
    if (auto *recorder = recording_.load(std::memory_order_acquire);
        recorder && recorder->monitoring_active() && input_channels) {
        const auto output_left = mixer_.active_master_left();
        const auto output_right = mixer_.active_master_right();
        if (output_left < output_channels && output_right < output_channels)
            for (std::size_t frame = 0;
                 frame < std::min(count, input.size() / input_channels);
                 ++frame) {
                output[frame * output_channels + output_left] +=
                    input[frame * input_channels];
                output[frame * output_channels + output_right] +=
                    input[frame * input_channels +
                          (input_channels > 1 ? 1 : 0)];
            }
    }
    physical_output_ = nullptr;
    physical_output_samples_ = 0;
    physical_output_channels_ = 0;
    duplex_input_ = nullptr;
    duplex_input_samples_ = 0;
    duplex_input_channels_ = 0;
}

void AudioEngine::render_wav(const std::filesystem::path &path,
                             std::uint64_t frames) {
    std::ofstream output(path, std::ios::binary);
    auto write16 = [&](std::uint16_t value) {
        output.write(reinterpret_cast<char *>(&value), 2);
    };
    auto write32 = [&](std::uint32_t value) {
        output.write(reinterpret_cast<char *>(&value), 4);
    };
    output.write("RIFF", 4);
    write32(36 + frames * 4);
    output.write("WAVEfmt ", 8);
    write32(16);
    write16(1);
    write16(2);
    write32(rate_);
    write32(rate_ * 4);
    write16(4);
    write16(16);
    output.write("data", 4);
    write32(frames * 4);
    std::array<float, 256> left{}, right{};
    for (std::uint64_t done = 0; done < frames;) {
        const auto count = std::min<std::uint64_t>(left.size(), frames - done);
        process({left.data(), count}, {right.data(), count});
        for (std::size_t i = 0; i < count; ++i) {
            auto convert = [](float value) {
                return static_cast<std::int16_t>(
                    std::clamp(value, -1.0F, 1.0F) * 32767);
            };
            auto l = convert(left[i]), r = convert(right[i]);
            output.write(reinterpret_cast<char *>(&l), 2);
            output.write(reinterpret_cast<char *>(&r), 2);
        }
        done += count;
    }
}
} // namespace neurogenic
