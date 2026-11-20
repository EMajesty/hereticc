#include "neurogenic/dsp/voice_modulation.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace neurogenic {
namespace {
float next_random(std::uint32_t &state) noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state) /
               static_cast<float>(std::numeric_limits<std::uint32_t>::max()) *
               2.0F -
           1.0F;
}
float wave(LfoWaveform waveform, double phase, std::uint32_t &random,
           float &held, double previous) noexcept {
    switch (waveform) {
    case LfoWaveform::sine:
        return std::sin(static_cast<float>(phase * 2.0 * std::numbers::pi));
    case LfoWaveform::triangle:
        return 1.0F - 4.0F * std::fabs(static_cast<float>(phase) - 0.5F);
    case LfoWaveform::saw:
        return static_cast<float>(phase * 2.0 - 1.0);
    case LfoWaveform::square:
        return phase < 0.5 ? 1.0F : -1.0F;
    case LfoWaveform::random:
        if (phase < previous)
            held = next_random(random);
        return held;
    }
    return 0;
}
std::size_t find_instance(const ModulationConfiguration &configuration,
                          ModulatorInstanceId id) noexcept {
    for (std::size_t i = 0; i < configuration.modulator_count; ++i)
        if (configuration.modulators[i].id == id)
            return i;
    return configuration.modulator_count;
}
float output(const SampleVoiceModulationState::Instance &state,
             std::uint32_t id) noexcept {
    return id >= 1 && id <= state.outputs.size() ? state.outputs[id - 1] : 0;
}
} // namespace

bool voice_modulator_supported(ModulatorType type) noexcept {
    switch (type) {
    case ModulatorType::lfo:
    case ModulatorType::adsr:
    case ModulatorType::sample_hold:
    case ModulatorType::smooth_random:
    case ModulatorType::step_sequencer:
    case ModulatorType::clocked_random:
    case ModulatorType::function_generator:
    case ModulatorType::scale_offset:
    case ModulatorType::invert:
    case ModulatorType::rectify:
    case ModulatorType::slew:
    case ModulatorType::quantize:
    case ModulatorType::minimum:
    case ModulatorType::maximum:
    case ModulatorType::sum:
    case ModulatorType::multiply:
    case ModulatorType::velocity:
    case ModulatorType::gate:
        return true;
    default:
        return false;
    }
}

void reset_sample_voice_modulation(
    SampleVoiceModulationState &state,
    const ModulationConfiguration &configuration, std::uint64_t track_id,
    float velocity, std::uint64_t occurrence) noexcept {
    state = {};
    state.track_id = track_id;
    state.velocity = std::clamp(velocity, 0.0F, 1.0F);
    state.gate = true;
    state.active = true;
    for (std::size_t i = 0; i < configuration.modulator_count; ++i) {
        const auto &definition = configuration.modulators[i];
        if (definition.owner_kind != ModulatorOwnerKind::voice_definition ||
            definition.owner_id != track_id)
            continue;
        auto &runtime = state.instances[i];
        runtime.random = definition.settings.seed ^
                         static_cast<std::uint32_t>(occurrence) ^
                         (definition.id * 0x9e3779b9U);
        if (!runtime.random)
            runtime.random = 1;
        runtime.held = next_random(runtime.random);
        runtime.target = next_random(runtime.random);
        if (!definition.settings.retrigger)
            runtime.phase = static_cast<double>(runtime.random) /
                            std::numeric_limits<std::uint32_t>::max();
        runtime.gate = true;
        runtime.envelope_stage = 1; // attack
        runtime.direction = 1;
    }
}

void release_sample_voice_modulation(SampleVoiceModulationState &state) noexcept {
    state.gate = false;
    for (auto &runtime : state.instances) {
        runtime.gate = false;
        if (runtime.envelope_stage != 0)
            runtime.envelope_stage = 4; // release
    }
}

SampleVoiceModulationValues process_sample_voice_modulation(
    SampleVoiceModulationState &voice,
    const ModulationConfiguration &configuration, std::uint32_t sample_rate,
    float tempo_bpm) noexcept {
    if (!voice.active || ++voice.frames_until_update < configuration.update_interval)
        return voice.current;
    SampleVoiceModulationValues result;
    const auto elapsed = std::exchange(voice.frames_until_update, 0U);
    const auto seconds = static_cast<float>(elapsed) /
                         static_cast<float>(std::max(1U, sample_rate));
    for (std::size_t order = 0; order < configuration.prepared_count; ++order) {
        const auto index = find_instance(configuration,
                                         configuration.prepared_order[order]);
        if (index == configuration.modulator_count)
            continue;
        const auto &definition = configuration.modulators[index];
        if (!definition.enabled ||
            definition.owner_kind != ModulatorOwnerKind::voice_definition ||
            definition.owner_id != voice.track_id)
            continue;
        auto &runtime = voice.instances[index];
        runtime.inputs = {};
        for (std::size_t r = 0; r < configuration.route_count; ++r) {
            const auto &route = configuration.routes[r];
            if (!route.enabled || route.scope != ModulationScope::voice ||
                route.destination_kind != ModulationDestinationKind::modulator_input ||
                route.destination_modulator != definition.id)
                continue;
            const auto source = find_instance(configuration, route.source_instance);
            if (source < configuration.modulator_count && route.destination_input >= 1 &&
                route.destination_input <= runtime.inputs.size())
                runtime.inputs[route.destination_input - 1] +=
                    output(voice.instances[source], route.source_output) * route.depth;
        }
        auto settings = definition.settings;
        for (std::size_t r = 0; r < configuration.route_count; ++r) {
            const auto &route = configuration.routes[r];
            if (!route.enabled || route.scope != ModulationScope::voice ||
                route.destination_kind !=
                    ModulationDestinationKind::modulator_parameter ||
                route.destination_modulator != definition.id)
                continue;
            const auto source = find_instance(configuration, route.source_instance);
            if (source == configuration.modulator_count)
                continue;
            const auto contribution =
                output(voice.instances[source], route.source_output) * route.depth;
            switch (route.destination_modulator_parameter) {
            case ModulatorParameterId::rate:
                settings.rate_hz = std::clamp(settings.rate_hz + contribution * 100.0F,
                                              0.0F, 100.0F);
                break;
            case ModulatorParameterId::beats:
                settings.beats_per_cycle = std::clamp(
                    settings.beats_per_cycle + contribution * 64.0F,
                    0.03125F, 64.0F);
                break;
            case ModulatorParameterId::attack:
                settings.attack_seconds = std::clamp(
                    settings.attack_seconds + contribution * 60.0F, 0.0F, 60.0F);
                break;
            case ModulatorParameterId::decay:
                settings.decay_seconds = std::clamp(
                    settings.decay_seconds + contribution * 60.0F, 0.0F, 60.0F);
                break;
            case ModulatorParameterId::sustain:
                settings.sustain = std::clamp(settings.sustain + contribution,
                                              0.0F, 1.0F);
                break;
            case ModulatorParameterId::release:
                settings.release_seconds = std::clamp(
                    settings.release_seconds + contribution * 60.0F, 0.0F, 60.0F);
                break;
            case ModulatorParameterId::rise:
                settings.rise_seconds = std::clamp(
                    settings.rise_seconds + contribution * 60.0F, 0.001F, 60.0F);
                break;
            case ModulatorParameterId::fall:
                settings.fall_seconds = std::clamp(
                    settings.fall_seconds + contribution * 60.0F, 0.001F, 60.0F);
                break;
            case ModulatorParameterId::scale:
                settings.scale = std::clamp(settings.scale + contribution * 4.0F,
                                            -4.0F, 4.0F);
                break;
            case ModulatorParameterId::offset:
                settings.offset = std::clamp(settings.offset + contribution,
                                             -1.0F, 1.0F);
                break;
            case ModulatorParameterId::slew:
                settings.slew_seconds = std::clamp(
                    settings.slew_seconds + contribution * 10.0F, 0.0F, 10.0F);
                break;
            default:
                break;
            }
        }
        const auto frequency = settings.tempo_sync
                                   ? tempo_bpm / 60.0F /
                                         std::max(0.03125F, settings.beats_per_cycle)
                                   : settings.rate_hz;
        const auto prior_phase = runtime.phase;
        runtime.phase = std::fmod(runtime.phase + frequency * seconds, 1.0);
        switch (definition.type) {
        case ModulatorType::velocity:
            runtime.outputs[0] = voice.velocity;
            break;
        case ModulatorType::gate:
            runtime.outputs[0] = voice.gate ? 1.0F : 0.0F;
            runtime.outputs[1] = runtime.trigger_latched ? 0.0F : 1.0F;
            runtime.trigger_latched = true;
            break;
        case ModulatorType::lfo:
            runtime.outputs[0] = wave(settings.waveform, runtime.phase,
                                      runtime.random, runtime.held, prior_phase);
            break;
        case ModulatorType::adsr: {
            auto &value = runtime.value;
            if (runtime.envelope_stage == 1) {
                value += seconds / std::max(settings.attack_seconds, 0.000001F);
                if (value >= 1) runtime.envelope_stage = 2;
            } else if (runtime.envelope_stage == 2) {
                value -= seconds * (1.0F - settings.sustain) /
                         std::max(settings.decay_seconds, 0.000001F);
                if (value <= settings.sustain) runtime.envelope_stage = 3;
            } else if (runtime.envelope_stage == 3) value = settings.sustain;
            else if (runtime.envelope_stage == 4) {
                value -= seconds / std::max(settings.release_seconds, 0.000001F);
                if (value <= 0) { value = 0; runtime.envelope_stage = 0; }
            }
            runtime.outputs[0] = std::clamp(value, 0.0F, 1.0F);
            break;
        }
        case ModulatorType::sample_hold:
        case ModulatorType::clocked_random:
            if (runtime.phase < prior_phase) runtime.held = next_random(runtime.random);
            runtime.outputs[0] = runtime.held;
            runtime.outputs[1] = runtime.phase < prior_phase ? 1.0F : 0.0F;
            break;
        case ModulatorType::smooth_random:
            if (runtime.phase < prior_phase) {
                runtime.held = runtime.target;
                runtime.target = next_random(runtime.random);
            }
            runtime.outputs[0] = std::lerp(runtime.held, runtime.target,
                                           static_cast<float>(runtime.phase));
            break;
        case ModulatorType::step_sequencer: {
            const auto step_frames = std::max(
                1.0, settings.step_duration_beats * 60.0 /
                         std::max(1.0F, tempo_bpm) * sample_rate);
            const auto old = runtime.phase;
            runtime.phase += elapsed / step_frames;
            if (runtime.phase >= 1.0) {
                runtime.phase -= std::floor(runtime.phase);
                if (settings.order == 1)
                    runtime.step = (runtime.step + settings.step_count - 1) %
                                   settings.step_count;
                else if (settings.order == 2 && settings.step_count > 1) {
                    if (runtime.direction &&
                        runtime.step + 1 >= settings.step_count)
                        runtime.direction = 0;
                    else if (!runtime.direction && runtime.step == 0)
                        runtime.direction = 1;
                    runtime.step = runtime.direction ? runtime.step + 1
                                                     : runtime.step - 1;
                } else if (settings.order == 3) {
                    const auto random = (next_random(runtime.random) + 1.0F) *
                                        0.5F;
                    runtime.step = std::min<std::uint32_t>(
                        static_cast<std::uint32_t>(random * settings.step_count),
                        settings.step_count - 1);
                } else
                    runtime.step = (runtime.step + 1) % settings.step_count;
            }
            runtime.outputs[0] = settings.step_values[runtime.step];
            runtime.outputs[1] = runtime.phase < old ? 1.0F : 0.0F;
            break;
        }
        case ModulatorType::function_generator: {
            if (settings.looping) {
                runtime.outputs[1] = 0;
                if (runtime.envelope_stage == 2) {
                    runtime.value -=
                        seconds / std::max(settings.fall_seconds, 0.001F);
                    if (runtime.value <= 0) {
                        runtime.value = 0;
                        runtime.envelope_stage = 1;
                        runtime.outputs[1] = 1;
                    }
                } else {
                    runtime.value +=
                        seconds / std::max(settings.rise_seconds, 0.001F);
                    if (runtime.value >= 1) {
                        runtime.value = 1;
                        runtime.envelope_stage = 2;
                    }
                }
            } else if (!voice.gate && runtime.value > 0)
                runtime.value -= seconds / std::max(settings.fall_seconds, 0.001F);
            else if (voice.gate && runtime.value < 1)
                runtime.value += seconds / std::max(settings.rise_seconds, 0.001F);
            runtime.outputs[0] = std::clamp(runtime.value, 0.0F, 1.0F);
            if (!settings.looping)
                runtime.outputs[1] = runtime.value <= 0 ? 1.0F : 0.0F;
            break;
        }
        case ModulatorType::scale_offset:
            runtime.outputs[0] = std::clamp(runtime.inputs[0] * settings.scale +
                                                settings.offset,
                                            -1.0F, 1.0F);
            break;
        case ModulatorType::invert: runtime.outputs[0] = -runtime.inputs[0]; break;
        case ModulatorType::rectify: runtime.outputs[0] = std::fabs(runtime.inputs[0]); break;
        case ModulatorType::slew: {
            const auto coefficient = settings.slew_seconds <= 0 ? 1.0F :
                std::min(1.0F, seconds / settings.slew_seconds);
            runtime.value += (runtime.inputs[0] - runtime.value) * coefficient;
            runtime.outputs[0] = runtime.value;
            break;
        }
        case ModulatorType::quantize: {
            const auto levels = std::max(1.0F, settings.quantization);
            runtime.outputs[0] = std::round(runtime.inputs[0] * levels) / levels;
            break;
        }
        case ModulatorType::minimum: runtime.outputs[0] = std::min(runtime.inputs[0], runtime.inputs[1]); break;
        case ModulatorType::maximum: runtime.outputs[0] = std::max(runtime.inputs[0], runtime.inputs[1]); break;
        case ModulatorType::sum: runtime.outputs[0] = std::clamp(runtime.inputs[0] + runtime.inputs[1], -1.0F, 1.0F); break;
        case ModulatorType::multiply: runtime.outputs[0] = runtime.inputs[0] * runtime.inputs[1]; break;
        default: break;
        }
    }
    for (const auto parameter : {SampleModulationParameter::pitch,
                                 SampleModulationParameter::gain,
                                 SampleModulationParameter::pan}) {
        const ModulationRoute *first{};
        float normalized{};
        for (std::size_t r = 0; r < configuration.route_count; ++r) {
            const auto &route = configuration.routes[r];
            if (!route.enabled || route.scope != ModulationScope::voice ||
                route.destination_kind != ModulationDestinationKind::parameter ||
                route.destination.target != ParameterTarget::sample_voice ||
                route.destination.parameter != static_cast<std::uint32_t>(parameter) ||
                route.track_id != voice.track_id)
                continue;
            if (!first) {
                first = &route;
                normalized = (route.base_value - route.minimum) /
                             (route.maximum - route.minimum);
            }
            const auto source = find_instance(configuration, route.source_instance);
            if (source == configuration.modulator_count)
                continue;
            auto value = output(voice.instances[source], route.source_output);
            if (route.polarity == ModulationPolarity::unipolar)
                value = (value + 1.0F) * 0.5F;
            normalized += value * route.depth;
        }
        if (!first)
            continue;
        const auto effective = first->minimum +
            std::clamp(normalized, 0.0F, 1.0F) *
                (first->maximum - first->minimum);
        switch (parameter) {
        case SampleModulationParameter::pitch: result.pitch_semitones = effective; break;
        case SampleModulationParameter::gain: result.gain = effective; break;
        case SampleModulationParameter::pan: result.pan = effective; break;
        }
    }
    result.pan = std::clamp(result.pan, -1.0F, 1.0F);
    voice.current = result;
    return voice.current;
}
} // namespace neurogenic
