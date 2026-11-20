#include "neurogenic/dsp/modulation.hpp"
#include "neurogenic/instrument/subtractive_synth.hpp"
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
constexpr std::uint32_t owner_bit(ModulatorOwnerKind owner) noexcept {
    return 1U << static_cast<unsigned>(owner);
}
constexpr std::array common_parameters{
    ModulatorParameterDescriptor{"rate", "Rate", "Hz", 0, 100, 1},
    ModulatorParameterDescriptor{"beats", "Beats/cycle", "beats", 0.03125F,
                                 64, 1},
    ModulatorParameterDescriptor{"seed", "Seed", "", 1, 4294967295.0F, 1,
                                 true}};
constexpr std::array envelope_parameters{
    ModulatorParameterDescriptor{"attack", "Attack", "s", 0, 60, 0.01F},
    ModulatorParameterDescriptor{"decay", "Decay", "s", 0, 60, 0.1F},
    ModulatorParameterDescriptor{"sustain", "Sustain", "", 0, 1, 1},
    ModulatorParameterDescriptor{"release", "Release", "s", 0, 60, 0.2F}};
constexpr std::array follower_parameters{
    ModulatorParameterDescriptor{"attack", "Attack", "s", 0, 10, 0.01F},
    ModulatorParameterDescriptor{"release", "Release", "s", 0, 10, 0.1F},
    ModulatorParameterDescriptor{"gain", "Sensitivity", "x", 0, 100, 1},
    ModulatorParameterDescriptor{"threshold", "Threshold", "", 0, 1, 0}};
constexpr std::array<ModulatorParameterDescriptor, 0> no_parameters{};
constexpr std::array<ModulatorPortDescriptor, 0> no_inputs{};
constexpr std::array output_bipolar{
    ModulatorPortDescriptor{1, "value", "Value", ModulationPolarity::bipolar}};
constexpr std::array output_unipolar{
    ModulatorPortDescriptor{1, "value", "Value", ModulationPolarity::unipolar}};
constexpr auto all_owners = owner_bit(ModulatorOwnerKind::track) |
                            owner_bit(ModulatorOwnerKind::auxiliary) |
                            owner_bit(ModulatorOwnerKind::master) |
                            owner_bit(ModulatorOwnerKind::group);
const std::array registry{
    ModulatorTypeDescriptor{"native.lfo.v1", "LFO", "Oscillators",
                            ModulatorType::lfo, common_parameters, no_inputs,
                            output_bipolar, all_owners, true, true,
                            ModulatorUpdateRate::control, 32},
    ModulatorTypeDescriptor{"native.adsr.v1", "ADSR", "Envelopes",
                            ModulatorType::adsr, envelope_parameters, no_inputs,
                            output_bipolar,
                            all_owners | owner_bit(ModulatorOwnerKind::voice_definition),
                            true, true, ModulatorUpdateRate::control, 32},
    ModulatorTypeDescriptor{"native.sample-hold.v1", "Sample and hold", "Random",
                            ModulatorType::sample_hold, common_parameters,
                            no_inputs, output_bipolar, all_owners, false, false,
                            ModulatorUpdateRate::control, 32},
    ModulatorTypeDescriptor{"native.smooth-random.v1", "Smooth random", "Random",
                            ModulatorType::smooth_random, common_parameters,
                            no_inputs, output_bipolar, all_owners, false, false,
                            ModulatorUpdateRate::control, 32},
    ModulatorTypeDescriptor{"native.envelope-follower.v1", "Envelope follower",
                            "Audio", ModulatorType::envelope_follower,
                            follower_parameters, no_inputs, output_unipolar,
                            all_owners, false, false, ModulatorUpdateRate::control,
                            32},
    ModulatorTypeDescriptor{"native.xy-manual.v1", "XY/manual", "Control",
                            ModulatorType::xy_manual, no_parameters, no_inputs,
                            output_bipolar, all_owners, false, false,
                            ModulatorUpdateRate::control, 16},
    ModulatorTypeDescriptor{"native.midi-cc.v1", "MIDI CC", "Control",
                            ModulatorType::midi_cc, no_parameters, no_inputs,
                            output_bipolar, all_owners, false, false,
                            ModulatorUpdateRate::control, 16}};
} // namespace

std::span<const ModulatorTypeDescriptor> modulator_type_registry() noexcept {
    return registry;
}
const ModulatorTypeDescriptor *find_modulator_type(ModulatorType type) noexcept {
    const auto found = std::find_if(registry.begin(), registry.end(),
                                    [&](const auto &entry) {
                                        return entry.type == type;
                                    });
    return found == registry.end() ? nullptr : &*found;
}

ModulationEngine::ModulationEngine(std::uint32_t sample_rate) noexcept
    : sample_rate_(std::max(1U, sample_rate)) {
    track_ids_[0] = 0;
}

bool ModulationEngine::configure(
    const ModulationConfiguration &configuration) noexcept {
    if (configuration.modulator_count > configuration.modulators.size() ||
        configuration.route_count > configuration.routes.size() ||
        configuration.update_interval < 1 ||
        configuration.update_interval > 1024)
        return false;
    const auto valid_settings = [](const ModulationSourceSettings &source) {
        return std::isfinite(source.rate_hz) && source.rate_hz >= 0 &&
               source.rate_hz <= 100 &&
               std::isfinite(source.beats_per_cycle) &&
               source.beats_per_cycle > 0 && source.beats_per_cycle <= 64 &&
               std::isfinite(source.attack_seconds) &&
               source.attack_seconds >= 0 && source.attack_seconds <= 60 &&
               std::isfinite(source.decay_seconds) &&
               source.decay_seconds >= 0 && source.decay_seconds <= 60 &&
               std::isfinite(source.sustain) && source.sustain >= 0 &&
               source.sustain <= 1 && std::isfinite(source.release_seconds) &&
               source.release_seconds >= 0 && source.release_seconds <= 60 &&
               std::isfinite(source.follower_attack_seconds) &&
               source.follower_attack_seconds >= 0 &&
               source.follower_attack_seconds <= 10 &&
               std::isfinite(source.follower_release_seconds) &&
               source.follower_release_seconds >= 0 &&
               source.follower_release_seconds <= 10 &&
               std::isfinite(source.follower_gain) &&
               source.follower_gain >= 0 && source.follower_gain <= 100 &&
               std::isfinite(source.follower_threshold) &&
               source.follower_threshold >= 0 &&
               source.follower_threshold <= 1;
    };
    for (std::size_t i = 0; i < configuration.modulator_count; ++i) {
        const auto &instance = configuration.modulators[i];
        const auto *type = find_modulator_type(instance.type);
        if (!instance.id || !type ||
            !(type->owner_mask & owner_bit(instance.owner_kind)) ||
            ((instance.owner_kind == ModulatorOwnerKind::track ||
              instance.owner_kind == ModulatorOwnerKind::group ||
              instance.owner_kind == ModulatorOwnerKind::voice_definition) &&
             instance.owner_id == 0) ||
            (instance.owner_kind == ModulatorOwnerKind::auxiliary &&
             instance.owner_id >= 2) ||
            (instance.owner_kind == ModulatorOwnerKind::master &&
             instance.owner_id != 0) ||
            !valid_settings(instance.settings))
            return false;
        for (std::size_t other = 0; other < i; ++other)
            if (configuration.modulators[other].id == instance.id)
                return false;
    }
    for (const auto &source : configuration.sources)
        if (!valid_settings(source))
            return false;
    for (std::size_t source = 0; source < configuration.sources.size();
         ++source)
        if (configuration.sources[source].scope == ModulationScope::voice &&
            source >= 4 &&
            source != static_cast<std::size_t>(ModulationSourceId::envelope) &&
            source != static_cast<std::size_t>(ModulationSourceId::velocity))
            return false;
    std::array<std::uint64_t, ModulationConfiguration::maximum_tracks> tracks{};
    std::size_t count = 1;
    for (std::size_t i = 0; i < configuration.route_count; ++i) {
        const auto &route = configuration.routes[i];
        if (route.id == 0 || route.source >= ModulationSourceId::count ||
            !std::isfinite(route.depth) || route.depth < -1.0F ||
            route.depth > 1.0F || !std::isfinite(route.minimum) ||
            !std::isfinite(route.maximum) || route.minimum >= route.maximum ||
            (!route.source_instance &&
             configuration.sources[static_cast<std::size_t>(route.source)]
                     .scope != route.scope))
            return false;
        if (route.source_instance) {
            const auto source = std::find_if(
                configuration.modulators.begin(),
                configuration.modulators.begin() + configuration.modulator_count,
                [&](const auto &instance) {
                    return instance.id == route.source_instance;
                });
            if (source == configuration.modulators.begin() +
                              configuration.modulator_count ||
                route.source_output != 1)
                return false;
            const auto expected_scope =
                source->owner_kind == ModulatorOwnerKind::track
                    ? ModulationScope::track
                : source->owner_kind == ModulatorOwnerKind::voice_definition
                    ? ModulationScope::voice
                    : ModulationScope::global;
            if (route.scope != expected_scope ||
                (source->owner_kind == ModulatorOwnerKind::track &&
                 route.track_id != source->owner_id) ||
                (source->owner_kind == ModulatorOwnerKind::auxiliary &&
                 (route.destination.target != ParameterTarget::mixer_aux ||
                  route.destination.instance != source->owner_id + 1)))
                return false;
        }
        const auto target = route.destination.target;
        const auto instance = route.destination.instance;
        const auto parameter = route.destination.parameter;
        const auto valid_numeric_destination =
            (target == ParameterTarget::subtractive_synth &&
             instance <= configuration.maximum_instances) ||
            (target == ParameterTarget::stereo_delay && instance == 0) ||
            (target == ParameterTarget::sample_voice && parameter >= 1 &&
             parameter <= 3) ||
            (target == ParameterTarget::mixer_track && instance >= 1 &&
             instance <= 32 && parameter >= 1 && parameter <= 4) ||
            (target == ParameterTarget::mixer_aux && instance >= 1 &&
             instance <= 2 && parameter == 1) ||
            (target == ParameterTarget::mixer_master && instance == 0 &&
             parameter == 1) ||
            (target == ParameterTarget::external_fx && instance >= 1 &&
             instance <= 2 && parameter >= 1 && parameter <= 3) ||
            (target == ParameterTarget::native_effect && instance >= 1 &&
             parameter >= 1 && parameter <= maximum_effect_parameters);
        if (!valid_numeric_destination ||
            (route.scope == ModulationScope::track &&
             route.destination.instance == 0))
            return false;
        if (route.scope == ModulationScope::voice &&
            (route.destination.target != ParameterTarget::subtractive_synth ||
             route.destination.instance == 0 ||
             (route.destination.parameter !=
                  static_cast<std::uint32_t>(SynthParameter::pitch) &&
              route.destination.parameter !=
                  static_cast<std::uint32_t>(SynthParameter::cutoff) &&
              route.destination.parameter !=
                  static_cast<std::uint32_t>(SynthParameter::gain))))
            return false;
        for (std::size_t other = 0; other < i; ++other)
            if (configuration.routes[other].id == route.id)
                return false;
        if ((!route.source_instance &&
             configuration.sources[static_cast<std::size_t>(route.source)]
                     .scope == ModulationScope::track) ||
            (route.source_instance && route.scope == ModulationScope::track)) {
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
    for (std::size_t instance = 0; instance < configuration_.modulator_count;
         ++instance) {
        modulator_states_[instance] = {};
        auto seed = configuration_.modulators[instance].settings.seed ^
                    configuration_.modulators[instance].id;
        modulator_states_[instance].random = seed ? seed : 1;
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
    if (id == ModulationSourceId::envelope_follower)
        source_states_[0][source].target = std::clamp(value, 0.0F, 1.0F);
    else
        source_states_[0][source].value = clamp_source(value);
    const auto dynamic_type =
        id == ModulationSourceId::envelope_follower
            ? ModulatorType::envelope_follower
        : id == ModulationSourceId::midi_cc ? ModulatorType::midi_cc
        : (id == ModulationSourceId::xy_x || id == ModulationSourceId::xy_y)
            ? ModulatorType::xy_manual
            : ModulatorType::lfo;
    for (std::size_t instance = 0; instance < configuration_.modulator_count;
         ++instance)
        if (configuration_.modulators[instance].enabled &&
            configuration_.modulators[instance].type == dynamic_type) {
            if (dynamic_type == ModulatorType::envelope_follower)
                modulator_states_[instance].target =
                    std::clamp(value, 0.0F, 1.0F);
            else
                modulator_states_[instance].value = clamp_source(value);
        }
}
void ModulationEngine::note_on(std::uint64_t track_id,
                               float velocity) noexcept {
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
        const auto slot =
            configuration_.sources[source].scope == ModulationScope::global
                ? 0
                : local_slot;
        if (slot < track_count_ && configuration_.sources[source].retrigger)
            source_states_[slot][source].phase = 0;
    }
    for (std::size_t instance = 0; instance < configuration_.modulator_count;
         ++instance) {
        const auto &definition = configuration_.modulators[instance];
        const auto owned = definition.owner_kind == ModulatorOwnerKind::master ||
                           (definition.owner_kind == ModulatorOwnerKind::track &&
                            definition.owner_id == track_id) ||
                           (definition.owner_kind ==
                                ModulatorOwnerKind::voice_definition &&
                            definition.owner_id == track_id);
        if (!definition.enabled || !owned)
            continue;
        if (definition.type == ModulatorType::adsr) {
            modulator_states_[instance].gate = true;
            modulator_states_[instance].envelope_stage =
                SourceState::EnvelopeStage::attack;
        }
        if (definition.type == ModulatorType::lfo &&
            definition.settings.retrigger)
            modulator_states_[instance].phase = 0;
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
    for (std::size_t instance = 0; instance < configuration_.modulator_count;
         ++instance) {
        const auto &definition = configuration_.modulators[instance];
        if (definition.type == ModulatorType::adsr &&
            (definition.owner_kind == ModulatorOwnerKind::master ||
             ((definition.owner_kind == ModulatorOwnerKind::track ||
               definition.owner_kind == ModulatorOwnerKind::voice_definition) &&
              definition.owner_id == track_id))) {
            modulator_states_[instance].gate = false;
            modulator_states_[instance].envelope_stage =
                SourceState::EnvelopeStage::release;
        }
    }
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
    for (std::size_t instance = 0; instance < configuration_.modulator_count;
         ++instance) {
        modulator_states_[instance].phase = 0;
        modulator_states_[instance].gate = false;
        if (configuration_.modulators[instance].settings.retrigger)
            modulator_states_[instance].value = 0;
    }
}
void ModulationEngine::clear_automation() noexcept {
    for (auto &value : automation_)
        value.active = false;
}
void ModulationEngine::set_automation(ParameterDestination destination,
                                      float value) noexcept {
    for (auto &entry : automation_)
        if (entry.active && entry.destination.target == destination.target &&
            entry.destination.parameter == destination.parameter &&
            entry.destination.instance == destination.instance) {
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
float ModulationEngine::instance_random(std::size_t instance) noexcept {
    auto &state = modulator_states_[instance].random;
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
            if (settings.scope == ModulationScope::voice)
                continue;
            if ((settings.scope == ModulationScope::global && track != 0) ||
                (settings.scope == ModulationScope::track && track == 0))
                continue;
            const auto rate =
                settings.tempo_sync
                    ? tempo_bpm_ /
                          (60.0F * std::max(0.03125F, settings.beats_per_cycle))
                    : std::max(0.0F, settings.rate_hz);
            auto &state = source_states_[track][source];
            const auto previous = state.phase;
            state.phase = std::fmod(state.phase + seconds * rate, 1.0);
            state.value =
                waveform(track, source, state.phase, state.phase < previous);
        }
        for (const auto source_id : {ModulationSourceId::sample_hold,
                                     ModulationSourceId::smooth_random}) {
            const auto source = static_cast<std::size_t>(source_id);
            const auto &settings = configuration_.sources[source];
            if (settings.scope == ModulationScope::voice)
                continue;
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
            state.value =
                source_id == ModulationSourceId::sample_hold
                    ? state.target
                    : state.held + (state.target - state.held) *
                                       static_cast<float>(state.phase);
        }
        const auto follower_index =
            static_cast<std::size_t>(ModulationSourceId::envelope_follower);
        const auto &follower_settings = configuration_.sources[follower_index];
        if (track == 0 && follower_settings.scope == ModulationScope::global) {
            auto &follower = source_states_[0][follower_index];
            const auto target = std::clamp(
                (follower.target - follower_settings.follower_threshold) *
                    follower_settings.follower_gain,
                0.0F, 1.0F);
            const auto duration =
                target > (follower.value + 1.0F) * 0.5F
                    ? follower_settings.follower_attack_seconds
                    : follower_settings.follower_release_seconds;
            const auto coefficient =
                duration <= 0
                    ? 1.0F
                    : 1.0F - std::exp(-static_cast<float>(seconds) / duration);
            const auto current = (follower.value + 1.0F) * 0.5F;
            follower.value =
                (current + (target - current) * coefficient) * 2.0F - 1.0F;
        }
        auto &envelope = source_states_[track][envelope_source];
        const auto &settings = configuration_.sources[envelope_source];
        if (settings.scope == ModulationScope::voice)
            continue;
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

void ModulationEngine::update_modulators(std::uint32_t elapsed) noexcept {
    const auto seconds = static_cast<double>(elapsed) / sample_rate_;
    for (std::size_t index = 0; index < configuration_.modulator_count;
         ++index) {
        const auto &instance = configuration_.modulators[index];
        auto &state = modulator_states_[index];
        if (!instance.enabled) {
            state.value = 0;
            continue;
        }
        const auto &settings = instance.settings;
        const auto rate =
            settings.tempo_sync
                ? tempo_bpm_ /
                      (60.0F * std::max(0.03125F, settings.beats_per_cycle))
                : std::max(0.0F, settings.rate_hz);
        if (instance.type == ModulatorType::lfo) {
            const auto previous = state.phase;
            state.phase = std::fmod(state.phase + seconds * rate, 1.0);
            const auto wrapped = state.phase < previous;
            switch (settings.waveform) {
            case LfoWaveform::sine:
                state.value = static_cast<float>(std::sin(
                    state.phase * 2.0 * std::numbers::pi_v<double>));
                break;
            case LfoWaveform::triangle:
                state.value = static_cast<float>(
                    1.0 - 4.0 * std::abs(state.phase - 0.5));
                break;
            case LfoWaveform::saw:
                state.value = static_cast<float>(state.phase * 2.0 - 1.0);
                break;
            case LfoWaveform::square:
                state.value = state.phase < 0.5 ? 1.0F : -1.0F;
                break;
            case LfoWaveform::random:
                if (wrapped)
                    state.held = instance_random(index);
                state.value = state.held;
                break;
            }
        } else if (instance.type == ModulatorType::sample_hold ||
                   instance.type == ModulatorType::smooth_random) {
            const auto previous = state.phase;
            state.phase = std::fmod(state.phase + seconds *
                                                      std::max(0.01F, rate),
                                    1.0);
            if (state.phase < previous) {
                state.held = state.target;
                state.target = instance_random(index);
            }
            state.value = instance.type == ModulatorType::sample_hold
                              ? state.target
                              : state.held + (state.target - state.held) *
                                                 static_cast<float>(state.phase);
        } else if (instance.type == ModulatorType::envelope_follower) {
            const auto target = std::clamp(
                (state.target - settings.follower_threshold) *
                    settings.follower_gain,
                0.0F, 1.0F);
            const auto current = (state.value + 1.0F) * 0.5F;
            const auto duration = target > current
                                      ? settings.follower_attack_seconds
                                      : settings.follower_release_seconds;
            const auto coefficient =
                duration <= 0
                    ? 1.0F
                    : 1.0F - std::exp(-static_cast<float>(seconds) / duration);
            state.value =
                (current + (target - current) * coefficient) * 2.0F - 1.0F;
        } else if (instance.type == ModulatorType::adsr) {
            const auto frames = static_cast<float>(elapsed);
            switch (state.envelope_stage) {
            case SourceState::EnvelopeStage::idle:
                state.value = -1.0F;
                break;
            case SourceState::EnvelopeStage::attack:
                state.value += 2.0F * frames /
                               std::max(1.0F, settings.attack_seconds *
                                                   sample_rate_);
                if (state.value >= 1.0F) {
                    state.value = 1.0F;
                    state.envelope_stage = SourceState::EnvelopeStage::decay;
                }
                break;
            case SourceState::EnvelopeStage::decay: {
                const auto sustain = settings.sustain * 2.0F - 1.0F;
                state.value -= 2.0F * frames /
                               std::max(1.0F, settings.decay_seconds *
                                                   sample_rate_);
                if (state.value <= sustain) {
                    state.value = sustain;
                    state.envelope_stage = SourceState::EnvelopeStage::sustain;
                }
                break;
            }
            case SourceState::EnvelopeStage::sustain:
                state.value = settings.sustain * 2.0F - 1.0F;
                if (!state.gate)
                    state.envelope_stage = SourceState::EnvelopeStage::release;
                break;
            case SourceState::EnvelopeStage::release:
                state.value -= 2.0F * frames /
                               std::max(1.0F, settings.release_seconds *
                                                   sample_rate_);
                if (state.value <= -1.0F) {
                    state.value = -1.0F;
                    state.envelope_stage = SourceState::EnvelopeStage::idle;
                }
                break;
            }
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
    result.modulator_count = configuration_.modulator_count;
    for (std::size_t instance = 0; instance < result.modulator_count; ++instance)
        result.modulator_values[instance] =
            published_modulators_[instance].load(std::memory_order_relaxed);
    result.generation = published_generation_.load(std::memory_order_acquire);
    return result;
}
} // namespace neurogenic
