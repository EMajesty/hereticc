#include "neurogenic/dsp/modulation.hpp"
#include <cmath>
#include <limits>
#include <numbers>

namespace neurogenic {
namespace {
constexpr auto envelope_source =
    static_cast<std::size_t>(ModulationSourceId::envelope);
constexpr float clamp_source(float value) noexcept {
    return std::clamp(value, -1.0F, 1.0F);
}
} // namespace

ModulationEngine::ModulationEngine(std::uint32_t sample_rate) noexcept
    : sample_rate_(std::max(1U, sample_rate)) {
    track_ids_[0] = 0;
}

bool ModulationEngine::configure(
    const ModulationConfiguration &configuration) noexcept {
    if (configuration.route_count > configuration.routes.size() ||
        configuration.update_interval < 1 ||
        configuration.update_interval > 1024)
        return false;
    for (const auto &source : configuration.sources)
        if (source.scope == ModulationScope::voice ||
            !std::isfinite(source.rate_hz) || source.rate_hz < 0 ||
            source.rate_hz > 100 || !std::isfinite(source.beats_per_cycle) ||
            source.beats_per_cycle <= 0 || source.beats_per_cycle > 64 ||
            !std::isfinite(source.attack_seconds) ||
            source.attack_seconds < 0 || source.attack_seconds > 60 ||
            !std::isfinite(source.decay_seconds) || source.decay_seconds < 0 ||
            source.decay_seconds > 60 || !std::isfinite(source.sustain) ||
            source.sustain < 0 || source.sustain > 1 ||
            !std::isfinite(source.release_seconds) ||
            source.release_seconds < 0 || source.release_seconds > 60)
            return false;
    std::array<std::uint64_t, ModulationConfiguration::maximum_tracks> tracks{};
    std::size_t count = 1;
    for (std::size_t i = 0; i < configuration.route_count; ++i) {
        const auto &route = configuration.routes[i];
        if (route.id == 0 || route.source >= ModulationSourceId::count ||
            !std::isfinite(route.depth) || route.depth < -1.0F ||
            route.depth > 1.0F || !std::isfinite(route.minimum) ||
            !std::isfinite(route.maximum) ||
            route.minimum >= route.maximum ||
            route.scope == ModulationScope::voice ||
            configuration.sources[static_cast<std::size_t>(route.source)]
                    .scope == ModulationScope::voice ||
            configuration.sources[static_cast<std::size_t>(route.source)]
                    .scope != route.scope)
            return false;
        for (std::size_t other = 0; other < i; ++other)
            if (configuration.routes[other].id == route.id)
                return false;
        if (configuration.sources[static_cast<std::size_t>(route.source)]
                    .scope == ModulationScope::track) {
            if (route.track_id == 0)
                return false;
            bool found{};
            for (std::size_t track = 1; track < count; ++track)
                found |= tracks[track] == route.track_id;
            if (!found) {
                if (count == tracks.size())
                    return false;
                tracks[count++] = route.track_id;
            }
        }
    }
    configuration_ = configuration;
    track_ids_ = tracks;
    track_count_ = count;
    frames_until_update_ = 0;
    for (std::size_t track = 0; track < track_count_; ++track)
        for (std::size_t source = 0; source < configuration_.source_count;
             ++source) {
            source_states_[track][source] = {};
            auto seed = configuration_.sources[source].seed;
            seed ^= static_cast<std::uint32_t>(track_ids_[track]);
            seed ^= static_cast<std::uint32_t>(track_ids_[track] >> 32U);
            source_states_[track][source].random = seed ? seed : 1;
        }
    effective_.fill(0);
    for (auto &value : published_effective_)
        value.store(0, std::memory_order_relaxed);
    for (auto &value : published_route_sources_)
        value.store(0, std::memory_order_relaxed);
    automation_.fill({});
    envelope_gates_.fill(false);
    published_generation_.fetch_add(1, std::memory_order_release);
    return true;
}

void ModulationEngine::set_sample_rate(std::uint32_t sample_rate) noexcept {
    sample_rate_ = std::max(1U, sample_rate);
}
void ModulationEngine::set_tempo(float bpm) noexcept {
    if (std::isfinite(bpm) && bpm > 0)
        tempo_bpm_ = bpm;
}
std::size_t ModulationEngine::track_slot(std::uint64_t id) const noexcept {
    for (std::size_t i = 0; i < track_count_; ++i)
        if (track_ids_[i] == id)
            return i;
    return track_count_;
}
void ModulationEngine::set_input(ModulationSourceId id, float value) noexcept {
    const auto source = static_cast<std::size_t>(id);
    if (source >= configuration_.source_count || !std::isfinite(value))
        return;
    source_states_[0][source].value = clamp_source(value);
}
void ModulationEngine::note_on(std::uint64_t track_id, float velocity) noexcept {
    const auto local_slot = track_slot(track_id);
    const auto envelope_slot =
        configuration_.sources[envelope_source].scope == ModulationScope::global
            ? 0
            : local_slot;
    if (envelope_slot < track_count_) {
        envelope_gates_[envelope_slot] = true;
        auto &envelope = source_states_[envelope_slot][envelope_source];
        envelope.envelope_stage = SourceState::EnvelopeStage::attack;
    }
    const auto velocity_source =
        static_cast<std::size_t>(ModulationSourceId::velocity);
    const auto velocity_slot =
        configuration_.sources[velocity_source].scope == ModulationScope::global
            ? 0
            : local_slot;
    if (velocity_slot < track_count_)
        source_states_[velocity_slot][velocity_source].value =
            std::clamp(velocity, 0.0F, 1.0F) * 2.0F - 1.0F;
    for (std::size_t source = 0; source < 4; ++source) {
        const auto slot = configuration_.sources[source].scope ==
                                  ModulationScope::global
                              ? 0
                              : local_slot;
        if (slot < track_count_ && configuration_.sources[source].retrigger)
            source_states_[slot][source].phase = 0;
    }
}
void ModulationEngine::note_off(std::uint64_t track_id) noexcept {
    const auto slot =
        configuration_.sources[envelope_source].scope == ModulationScope::global
            ? 0
            : track_slot(track_id);
    if (slot >= track_count_)
        return;
    envelope_gates_[slot] = false;
    source_states_[slot][envelope_source].envelope_stage =
        SourceState::EnvelopeStage::release;
}
void ModulationEngine::restart() noexcept {
    for (std::size_t track = 0; track < track_count_; ++track)
        for (std::size_t source = 0; source < configuration_.source_count;
             ++source) {
            source_states_[track][source].phase = 0;
            if (configuration_.sources[source].retrigger)
                source_states_[track][source].value = 0;
        }
    clear_automation();
}
void ModulationEngine::clear_automation() noexcept {
    for (auto &value : automation_)
        value.active = false;
}
void ModulationEngine::set_automation(ParameterDestination destination,
                                      float value) noexcept {
    for (auto &entry : automation_)
        if (entry.active && entry.destination.target == destination.target &&
            entry.destination.parameter == destination.parameter) {
            entry.value = value;
            return;
        }
    for (auto &entry : automation_)
        if (!entry.active) {
            entry = {destination, value, true};
            return;
        }
}

float ModulationEngine::random(std::size_t track, std::size_t source) noexcept {
    auto &state = source_states_[track][source].random;
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return static_cast<float>(state) /
               static_cast<float>(std::numeric_limits<std::uint32_t>::max()) *
               2.0F -
           1.0F;
}
float ModulationEngine::waveform(std::size_t track, std::size_t source,
                                 double phase, bool wrapped) noexcept {
    switch (configuration_.sources[source].waveform) {
    case LfoWaveform::sine:
        return static_cast<float>(
            std::sin(phase * 2.0 * std::numbers::pi_v<double>));
    case LfoWaveform::triangle:
        return static_cast<float>(1.0 - 4.0 * std::abs(phase - 0.5));
    case LfoWaveform::saw:
        return static_cast<float>(phase * 2.0 - 1.0);
    case LfoWaveform::square:
        return phase < 0.5 ? 1.0F : -1.0F;
    case LfoWaveform::random:
        if (wrapped)
            source_states_[track][source].held = random(track, source);
        return source_states_[track][source].held;
    }
    return 0;
}

void ModulationEngine::update_sources(std::uint32_t elapsed) noexcept {
    const auto seconds = static_cast<double>(elapsed) / sample_rate_;
    for (std::size_t track = 0; track < track_count_; ++track) {
        for (std::size_t source = 0; source < 4; ++source) {
            const auto &settings = configuration_.sources[source];
            if ((settings.scope == ModulationScope::global && track != 0) ||
                (settings.scope == ModulationScope::track && track == 0))
                continue;
            const auto rate = settings.tempo_sync
                                  ? tempo_bpm_ /
                                        (60.0F * std::max(0.03125F,
                                                          settings.beats_per_cycle))
                                  : std::max(0.0F, settings.rate_hz);
            auto &state = source_states_[track][source];
            const auto previous = state.phase;
            state.phase = std::fmod(state.phase + seconds * rate, 1.0);
            state.value = waveform(track, source, state.phase,
                                   state.phase < previous);
        }
        for (const auto source_id : {ModulationSourceId::sample_hold,
                                     ModulationSourceId::smooth_random}) {
            const auto source = static_cast<std::size_t>(source_id);
            const auto &settings = configuration_.sources[source];
            if ((settings.scope == ModulationScope::global && track != 0) ||
                (settings.scope == ModulationScope::track && track == 0))
                continue;
            auto &state = source_states_[track][source];
            const auto rate = std::max(0.01F, settings.rate_hz);
            const auto previous = state.phase;
            state.phase = std::fmod(state.phase + seconds * rate, 1.0);
            if (state.phase < previous) {
                state.held = state.target;
                state.target = random(track, source);
            }
            state.value = source_id == ModulationSourceId::sample_hold
                              ? state.target
                              : state.held +
                                    (state.target - state.held) *
                                        static_cast<float>(state.phase);
        }
        auto &envelope = source_states_[track][envelope_source];
        const auto &settings = configuration_.sources[envelope_source];
        if ((settings.scope == ModulationScope::global && track != 0) ||
            (settings.scope == ModulationScope::track && track == 0))
            continue;
        const auto samples = static_cast<float>(elapsed);
        switch (envelope.envelope_stage) {
        case SourceState::EnvelopeStage::idle:
            envelope.value = -1.0F;
            break;
        case SourceState::EnvelopeStage::attack:
            envelope.value +=
                2.0F * samples /
                std::max(1.0F, settings.attack_seconds * sample_rate_);
            if (envelope.value >= 1.0F) {
                envelope.value = 1.0F;
                envelope.envelope_stage = SourceState::EnvelopeStage::decay;
            }
            break;
        case SourceState::EnvelopeStage::decay: {
            const auto sustain = settings.sustain * 2.0F - 1.0F;
            envelope.value -=
                2.0F * samples /
                std::max(1.0F, settings.decay_seconds * sample_rate_);
            if (envelope.value <= sustain) {
                envelope.value = sustain;
                envelope.envelope_stage = SourceState::EnvelopeStage::sustain;
            }
            break;
        }
        case SourceState::EnvelopeStage::sustain:
            envelope.value = settings.sustain * 2.0F - 1.0F;
            if (!envelope_gates_[track])
                envelope.envelope_stage = SourceState::EnvelopeStage::release;
            break;
        case SourceState::EnvelopeStage::release:
            envelope.value -=
                2.0F * samples /
                std::max(1.0F, settings.release_seconds * sample_rate_);
            if (envelope.value <= -1.0F) {
                envelope.value = -1.0F;
                envelope.envelope_stage = SourceState::EnvelopeStage::idle;
            }
            break;
        }
    }
}

ModulationSnapshot ModulationEngine::snapshot() const noexcept {
    ModulationSnapshot result;
    for (std::size_t source = 0; source < configuration_.source_count; ++source)
        result.source_values[source] =
            published_sources_[source].load(std::memory_order_relaxed);
    for (std::size_t route = 0; route < configuration_.maximum_routes; ++route)
        result.effective_values[route] =
            published_effective_[route].load(std::memory_order_relaxed);
    for (std::size_t route = 0; route < configuration_.maximum_routes; ++route)
        result.route_source_values[route] =
            published_route_sources_[route].load(std::memory_order_relaxed);
    result.generation = published_generation_.load(std::memory_order_acquire);
    return result;
}
} // namespace neurogenic
