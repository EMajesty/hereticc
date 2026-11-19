#include "neurogenic/audio/engine.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include <algorithm>
#include <chrono>
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
      timeline_events_(event_staging_capacity), synth_(rate, voice_capacity),
      delay_(rate) {
    if (rate == 0 || voice_capacity == 0 || event_staging_capacity == 0 ||
        sample_version_capacity == 0)
        throw std::invalid_argument(
            "sample rate and all bounded capacities must be positive");
    note_slots_.fill(invalid_sample_slot);
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
    std::lock_guard lock(sample_update_producer_mutex_);
    if (publication)
        *publication = 0;
    if (id == invalid_sample_slot || sample.sample_rate != rate_ ||
        (sample.channels != 1 && sample.channels != 2) ||
        sample.interleaved.empty() ||
        sample.interleaved.size() % sample.channels != 0)
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
    try {
        sample_versions_[version].sample =
            std::make_unique<const Sample>(std::move(sample));
    } catch (...) {
        return false;
    }
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
    const Trigger event{note, velocity, frame_offset,
                        next_order_.fetch_add(1, std::memory_order_relaxed)};
    if (!triggers_.push(event))
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
}
void AudioEngine::trigger_synth(std::uint8_t note, float velocity,
                                bool note_on) noexcept {
    if (!synth_triggers_.push({note, velocity, note_on}))
        dropped_triggers_.fetch_add(1, std::memory_order_relaxed);
}
bool AudioEngine::set_synth_parameter(SynthParameter parameter,
                                      float value) noexcept {
    if (!dsp_commands_.push({DspCommand::Target::synth,
                             static_cast<std::uint32_t>(parameter), value})) {
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
                              bool timeline_owned) noexcept {
    const auto sample_index = find_active_sample(note_slots_[note]);
    if (sample_index == std::numeric_limits<std::size_t>::max()) {
        missing_slot_triggers_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
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
    voices_[selected] = {sample_versions_[sample_index].sample.get(),
                         sample_index,
                         0,
                         velocity,
                         next_voice_age_++,
                         track,
                         event,
                         occurrence,
                         note,
                         timeline_owned,
                         true};
}

void AudioEngine::stop_timeline_voice(TrackId track, EventId event,
                                      std::uint64_t occurrence) noexcept {
    for (auto &voice : voices_)
        if (voice.active && voice.timeline_owned && voice.track == track &&
            voice.event == event && voice.occurrence == occurrence) {
            voice.active = false;
            return;
        }
}

void AudioEngine::stop_voices(bool timeline_only) noexcept {
    for (auto &voice : voices_)
        if (!timeline_only || voice.timeline_owned)
            voice.active = false;
    synth_.reset();
}

void AudioEngine::process(std::span<float> left,
                          std::span<float> right) noexcept {
    const auto monitoring = callback_monitor_.enabled();
    const auto callback_begin = monitoring
                                    ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{};
    processing_started_.store(true, std::memory_order_release);
    const auto frames = std::min(left.size(), right.size());
    std::fill(left.begin(), left.end(), 0);
    std::fill(right.begin(), right.end(), 0);

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

    Pattern pattern;
    DspCommand dsp;
    while (dsp_commands_.pop(dsp)) {
        if (dsp.target == DspCommand::Target::synth)
            synth_.set_parameter(dsp.parameter, dsp.value);
        else if (dsp.target == DspCommand::Target::delay)
            delay_.set_parameter(dsp.parameter, dsp.value);
        else
            delay_.set_bypass(dsp.value >= 0.5F);
    }
    SynthTrigger synth_trigger;
    while (synth_triggers_.pop(synth_trigger)) {
        if (synth_trigger.note_on)
            synth_.note_on(synth_trigger.note, synth_trigger.velocity);
        else
            synth_.note_off(synth_trigger.note);
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
        } else if (command == TransportCommand::restart) {
            transport_frame_ = 0;
            transport_playing_ = true;
            stop_voices();
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

    for (std::size_t frame = 0; frame < frames; ++frame) {
        while (external_index < pending_trigger_count_ &&
               pending_triggers_[external_index].frame_offset == frame) {
            const auto &event = pending_triggers_[external_index++];
            start_voice(event.note, event.velocity);
        }
        while (scheduled_index < scheduled_count &&
               legacy_events_[scheduled_index].frame == frame) {
            const auto &event = legacy_events_[scheduled_index++];
            start_voice(event.note, event.velocity);
        }
        while (timeline_index < timeline_count &&
               timeline_events_[timeline_index].frame == frame) {
            const auto &event = timeline_events_[timeline_index++];
            if (event.type == MusicalEventType::parameter) {
                if (event.parameter_target ==
                    static_cast<std::uint32_t>(
                        ParameterTarget::subtractive_synth))
                    synth_.set_parameter(event.parameter_id, event.value);
            } else if (event.instrument == subtractive_instrument_key) {
                const NoteIdentity owner{event.track, event.target_event_id,
                                         event.occurrence};
                if (event.type == MusicalEventType::note_on)
                    synth_.note_on(event.pitch, event.value, owner);
                else if (event.type == MusicalEventType::note_off)
                    synth_.note_off(event.pitch, owner);
            } else if (event.instrument == sample_instrument_key) {
                if (event.type == MusicalEventType::note_on)
                    start_voice(event.pitch, event.value, event.track,
                                event.target_event_id, event.occurrence, true);
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
            if (voice.cursor + sample.channels > sample.interleaved.size()) {
                voice.active = false;
                continue;
            }
            left[frame] += sample.interleaved[voice.cursor] * voice.gain;
            right[frame] +=
                sample
                    .interleaved[voice.cursor + (sample.channels > 1 ? 1 : 0)] *
                voice.gain;
            voice.cursor += sample.channels;
            if (voice.cursor >= sample.interleaved.size())
                voice.active = false;
        }
        const auto synth_sample = synth_.render_sample();
        left[frame] += synth_sample;
        right[frame] += synth_sample;
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
