#include "neurogenic/dsp/modulation.hpp"
#include "neurogenic/dsp/voice_modulation.hpp"
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
    ModulatorParameterDescriptor{"rate", "Rate", "Hz", 0, 100, 1, false,
                                 ModulatorParameterId::rate},
    ModulatorParameterDescriptor{"beats", "Beats/cycle", "beats", 0.03125F,
                                 64, 1, false, ModulatorParameterId::beats},
    ModulatorParameterDescriptor{"seed", "Seed", "", 1, 4294967295.0F, 1,
                                 true, {}, false}};
constexpr std::array envelope_parameters{
    ModulatorParameterDescriptor{"attack", "Attack", "s", 0, 60, 0.01F,
                                 false, ModulatorParameterId::attack},
    ModulatorParameterDescriptor{"decay", "Decay", "s", 0, 60, 0.1F,
                                 false, ModulatorParameterId::decay},
    ModulatorParameterDescriptor{"sustain", "Sustain", "", 0, 1, 1, false,
                                 ModulatorParameterId::sustain},
    ModulatorParameterDescriptor{"release", "Release", "s", 0, 60, 0.2F,
                                 false, ModulatorParameterId::release}};
constexpr std::array follower_parameters{
    ModulatorParameterDescriptor{"attack", "Attack", "s", 0, 10, 0.01F,
                                 false, ModulatorParameterId::attack},
    ModulatorParameterDescriptor{"release", "Release", "s", 0, 10, 0.1F,
                                 false, ModulatorParameterId::release},
    ModulatorParameterDescriptor{"gain", "Sensitivity", "x", 0, 100, 1,
                                 false, ModulatorParameterId::follower_gain},
    ModulatorParameterDescriptor{"threshold", "Threshold", "", 0, 1, 0,
                                 false,
                                 ModulatorParameterId::follower_threshold}};
constexpr std::array step_parameters{
    ModulatorParameterDescriptor{"steps", "Steps", "", 1, 16, 8, true,
                                 ModulatorParameterId::step_count, false},
    ModulatorParameterDescriptor{"order", "Order", "", 0, 3, 0, true,
                                 ModulatorParameterId::order, false},
    ModulatorParameterDescriptor{"duration", "Step duration", "beats", 0.03125F,
                                 16, 0.25F, false,
                                 ModulatorParameterId::step_duration},
    ModulatorParameterDescriptor{"swing", "Swing", "%", 0, 0.75F, 0, false,
                                 ModulatorParameterId::swing}};
constexpr std::array euclidean_parameters{
    ModulatorParameterDescriptor{"steps", "Steps", "", 1, 16, 8, true,
                                 ModulatorParameterId::step_count, false},
    ModulatorParameterDescriptor{"pulses", "Pulses", "", 0, 16, 4, true,
                                 ModulatorParameterId::pulses, false},
    ModulatorParameterDescriptor{"rotation", "Rotation", "", 0, 15, 0, true,
                                 ModulatorParameterId::rotation, false},
    ModulatorParameterDescriptor{"duration", "Step duration", "beats", 0.03125F,
                                 16, 0.25F, false,
                                 ModulatorParameterId::step_duration},
    ModulatorParameterDescriptor{"gate", "Gate length", "%", 0.01F, 1, 0.5F,
                                 false, ModulatorParameterId::gate_length}};
constexpr std::array random_parameters{
    ModulatorParameterDescriptor{"distribution", "Distribution", "", 0, 2, 0,
                                 true, ModulatorParameterId::distribution,
                                 false},
    ModulatorParameterDescriptor{"duration", "Clock duration", "beats", 0.03125F,
                                 16, 0.25F, false,
                                 ModulatorParameterId::step_duration},
    ModulatorParameterDescriptor{"correlation", "Correlation", "%", 0, 1, 0,
                                 false, ModulatorParameterId::correlation},
    ModulatorParameterDescriptor{"quantization", "Quantization", "steps", 0,
                                 32, 0, false,
                                 ModulatorParameterId::quantization},
    ModulatorParameterDescriptor{"slew", "Slew", "s", 0, 10, 0, false,
                                 ModulatorParameterId::slew}};
constexpr std::array function_parameters{
    ModulatorParameterDescriptor{"rise", "Rise", "s", 0.001F, 60, 0.1F, false,
                                 ModulatorParameterId::rise},
    ModulatorParameterDescriptor{"fall", "Fall", "s", 0.001F, 60, 0.1F, false,
                                 ModulatorParameterId::fall},
    ModulatorParameterDescriptor{"curve", "Curve", "", -1, 1, 0, false,
                                 ModulatorParameterId::curve}};
constexpr std::array scale_parameters{
    ModulatorParameterDescriptor{"scale", "Scale", "x", -4, 4, 1, false,
                                 ModulatorParameterId::scale},
    ModulatorParameterDescriptor{"offset", "Offset", "", -1, 1, 0, false,
                                 ModulatorParameterId::offset}};
constexpr std::array slew_parameters{
    ModulatorParameterDescriptor{"slew", "Lag", "s", 0, 10, 0.1F, false,
                                 ModulatorParameterId::slew}};
constexpr std::array quantize_parameters{
    ModulatorParameterDescriptor{"quantization", "Levels", "", 1, 32, 8,
                                 false, ModulatorParameterId::quantization}};
constexpr std::array chaos_parameters{
    ModulatorParameterDescriptor{"chaos", "Chaos", "", 3.5F, 4, 3.8F, false,
                                 ModulatorParameterId::chaos},
    ModulatorParameterDescriptor{"rate", "Rate", "Hz", 0.01F, 100, 4, false,
                                 ModulatorParameterId::rate}};
constexpr std::array<ModulatorParameterDescriptor, 0> no_parameters{};
constexpr std::array<ModulatorPortDescriptor, 0> no_inputs{};
constexpr std::array input_one{
    ModulatorPortDescriptor{1, "input", "Input", ModulationPolarity::bipolar}};
constexpr std::array input_two{
    ModulatorPortDescriptor{1, "a", "A", ModulationPolarity::bipolar},
    ModulatorPortDescriptor{2, "b", "B", ModulationPolarity::bipolar}};
constexpr std::array output_bipolar{
    ModulatorPortDescriptor{1, "value", "Value", ModulationPolarity::bipolar}};
constexpr std::array output_unipolar{
    ModulatorPortDescriptor{1, "value", "Value", ModulationPolarity::unipolar}};
constexpr std::array output_value_trigger{
    ModulatorPortDescriptor{1, "value", "Value", ModulationPolarity::bipolar},
    ModulatorPortDescriptor{2, "trigger", "Trigger", ModulationPolarity::unipolar}};
constexpr std::array output_gate_accent{
    ModulatorPortDescriptor{1, "gate", "Gate", ModulationPolarity::unipolar},
    ModulatorPortDescriptor{2, "accent", "Accent", ModulationPolarity::unipolar}};
constexpr std::array output_function{
    ModulatorPortDescriptor{1, "value", "Value", ModulationPolarity::bipolar},
    ModulatorPortDescriptor{2, "eoc", "End of cycle", ModulationPolarity::unipolar}};
constexpr std::array output_gate_trigger{
    ModulatorPortDescriptor{1, "gate", "Gate", ModulationPolarity::unipolar},
    ModulatorPortDescriptor{2, "trigger", "Trigger", ModulationPolarity::unipolar}};
constexpr auto all_owners = owner_bit(ModulatorOwnerKind::track) |
                            owner_bit(ModulatorOwnerKind::auxiliary) |
                            owner_bit(ModulatorOwnerKind::master) |
                            owner_bit(ModulatorOwnerKind::group);
constexpr auto voice_owner = owner_bit(ModulatorOwnerKind::voice_definition);
const std::array registry{
    ModulatorTypeDescriptor{"native.lfo.v1", "LFO", "Oscillators",
                            ModulatorType::lfo, common_parameters, no_inputs,
                            output_bipolar, all_owners | voice_owner, true, true,
                            ModulatorUpdateRate::per_voice_control, 32},
    ModulatorTypeDescriptor{"native.adsr.v1", "ADSR", "Envelopes",
                            ModulatorType::adsr, envelope_parameters, no_inputs,
                            output_bipolar,
                            all_owners | voice_owner, true, true,
                            ModulatorUpdateRate::per_voice_control, 32},
    ModulatorTypeDescriptor{"native.sample-hold.v1", "Sample and hold", "Random",
                            ModulatorType::sample_hold, common_parameters,
                            no_inputs, output_bipolar, all_owners | voice_owner,
                            false, false, ModulatorUpdateRate::per_voice_control, 32},
    ModulatorTypeDescriptor{"native.smooth-random.v1", "Smooth random", "Random",
                            ModulatorType::smooth_random, common_parameters,
                            no_inputs, output_bipolar, all_owners | voice_owner,
                            false, false, ModulatorUpdateRate::per_voice_control, 32},
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
                            ModulatorUpdateRate::control, 16},
    ModulatorTypeDescriptor{"native.step.v1", "Step modulator", "Sequencers",
                            ModulatorType::step_sequencer, step_parameters,
                            no_inputs, output_value_trigger,
                            all_owners | voice_owner, true, true,
                            ModulatorUpdateRate::per_voice_control, 192},
    ModulatorTypeDescriptor{"native.euclidean.v1", "Euclidean", "Sequencers",
                            ModulatorType::euclidean, euclidean_parameters,
                            no_inputs, output_gate_accent, all_owners, true, true,
                            ModulatorUpdateRate::control, 48},
    ModulatorTypeDescriptor{"native.clocked-random.v1", "Clocked random",
                            "Random", ModulatorType::clocked_random,
                            random_parameters, no_inputs, output_value_trigger,
                            all_owners | voice_owner, true, true,
                            ModulatorUpdateRate::per_voice_control, 48},
    ModulatorTypeDescriptor{"native.function.v1", "Function generator",
                            "Envelopes", ModulatorType::function_generator,
                            function_parameters, input_one, output_function,
                            all_owners | voice_owner, true, true,
                            ModulatorUpdateRate::per_voice_control, 48},
    ModulatorTypeDescriptor{"native.scale-offset.v1", "Scale / offset", "Math",
                            ModulatorType::scale_offset, scale_parameters,
                            input_one, output_bipolar, all_owners | voice_owner, false, false,
                            ModulatorUpdateRate::control, 24},
    ModulatorTypeDescriptor{"native.invert.v1", "Invert", "Math",
                            ModulatorType::invert, no_parameters, input_one,
                            output_bipolar, all_owners | voice_owner, false, false,
                            ModulatorUpdateRate::control, 16},
    ModulatorTypeDescriptor{"native.rectify.v1", "Rectify", "Math",
                            ModulatorType::rectify, no_parameters, input_one,
                            output_unipolar, all_owners | voice_owner, false, false,
                            ModulatorUpdateRate::control, 16},
    ModulatorTypeDescriptor{"native.slew.v1", "Slew / lag", "Math",
                            ModulatorType::slew, slew_parameters, input_one,
                            output_bipolar, all_owners | voice_owner, false, false,
                            ModulatorUpdateRate::control, 24},
    ModulatorTypeDescriptor{"native.quantize.v1", "Quantize", "Math",
                            ModulatorType::quantize, quantize_parameters,
                            input_one, output_bipolar, all_owners | voice_owner, false, false,
                            ModulatorUpdateRate::control, 16},
    ModulatorTypeDescriptor{"native.minimum.v1", "Minimum", "Math",
                            ModulatorType::minimum, no_parameters, input_two,
                            output_bipolar, all_owners | voice_owner, false, false,
                            ModulatorUpdateRate::control, 16},
    ModulatorTypeDescriptor{"native.maximum.v1", "Maximum", "Math",
                            ModulatorType::maximum, no_parameters, input_two,
                            output_bipolar, all_owners | voice_owner, false, false,
                            ModulatorUpdateRate::control, 16},
    ModulatorTypeDescriptor{"native.sum.v1", "Sum", "Math",
                            ModulatorType::sum, no_parameters, input_two,
                            output_bipolar, all_owners | voice_owner, false, false,
                            ModulatorUpdateRate::control, 16},
    ModulatorTypeDescriptor{"native.multiply.v1", "Multiply", "Math",
                            ModulatorType::multiply, no_parameters, input_two,
                            output_bipolar, all_owners | voice_owner, false, false,
                            ModulatorUpdateRate::control, 16},
    ModulatorTypeDescriptor{"native.logistic.v1", "Logistic chaos", "Experimental",
                            ModulatorType::logistic, chaos_parameters, no_inputs,
                            output_bipolar, all_owners, true, true,
                            ModulatorUpdateRate::control, 32},
    ModulatorTypeDescriptor{"native.velocity.v1", "Note velocity", "Voice",
                            ModulatorType::velocity, no_parameters, no_inputs,
                            output_unipolar, voice_owner, true, true,
                            ModulatorUpdateRate::per_voice_control, 16},
    ModulatorTypeDescriptor{"native.gate.v1", "Note gate", "Voice",
                            ModulatorType::gate, no_parameters, no_inputs,
                            output_gate_trigger, voice_owner, true, true,
                            ModulatorUpdateRate::per_voice_control, 16}};
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

bool validate_modulation_graph(const ModulationConfiguration &configuration,
                               ModulationGraphDiagnostics &diagnostics) {
    diagnostics = {};
    if (configuration.modulator_count > configuration.modulators.size() ||
        configuration.route_count > configuration.routes.size()) {
        diagnostics.message = "modulation graph exceeds its prepared budget";
        return false;
    }
    const auto find_instance = [&](ModulatorInstanceId id) {
        return std::find_if(
            configuration.modulators.begin(),
            configuration.modulators.begin() + configuration.modulator_count,
            [&](const auto &instance) { return instance.id == id; });
    };
    std::array<std::uint16_t, ModulationConfiguration::maximum_modulators>
        indegree{};
    for (std::size_t route_index = 0;
         route_index < configuration.route_count; ++route_index) {
        const auto &route = configuration.routes[route_index];
        diagnostics.connection = route.id;
        if (!route.source_instance)
            continue; // Task 028 compatibility source.
        const auto source = find_instance(route.source_instance);
        if (source == configuration.modulators.begin() +
                          configuration.modulator_count) {
            diagnostics.message = "connection references missing source";
            return false;
        }
        const auto *source_type = find_modulator_type(source->type);
        if (!source_type ||
            std::none_of(source_type->outputs.begin(), source_type->outputs.end(),
                         [&](const auto &output) {
                             return output.id == route.source_output;
                         })) {
            diagnostics.message = "connection references missing output port";
            return false;
        }
        if (source->owner_kind == ModulatorOwnerKind::voice_definition) {
            if (route.scope != ModulationScope::voice) {
                diagnostics.message = "voice source requires voice scope";
                return false;
            }
            if (route.destination_kind == ModulationDestinationKind::parameter) {
                if (route.destination.target != ParameterTarget::sample_voice ||
                    route.track_id != source->owner_id) {
                    diagnostics.message =
                        "voice source must target its parent track's sample voices";
                    return false;
                }
            }
        }
        if (route.destination_owner_explicit &&
            (route.destination_owner_kind != source->owner_kind ||
             route.destination_owner_id != source->owner_id) &&
            !source->shared) {
            diagnostics.message =
                "cross-owner connection requires an explicitly shared source";
            return false;
        }
        if (route.destination_kind != ModulationDestinationKind::parameter) {
            const auto destination = find_instance(route.destination_modulator);
            if (destination == configuration.modulators.begin() +
                                   configuration.modulator_count) {
                diagnostics.message =
                    "connection references missing destination modulator";
                return false;
            }
            const auto *destination_type = find_modulator_type(destination->type);
            if (!destination_type) {
                diagnostics.message = "connection targets an unavailable modulator";
                return false;
            }
            if (route.destination_kind ==
                ModulationDestinationKind::modulator_parameter) {
                const auto parameter = std::find_if(
                    destination_type->parameters.begin(),
                    destination_type->parameters.end(), [&](const auto &candidate) {
                        return candidate.numeric_id ==
                                   route.destination_modulator_parameter &&
                               candidate.modulatable && !candidate.discrete;
                    });
                if (parameter == destination_type->parameters.end()) {
                    diagnostics.message =
                        "connection targets an unavailable modulator parameter";
                    return false;
                }
            } else if (std::none_of(
                           destination_type->inputs.begin(),
                           destination_type->inputs.end(), [&](const auto &input) {
                               return input.id == route.destination_input;
                           })) {
                diagnostics.message =
                    "connection targets an unavailable modulator input";
                return false;
            }
            const auto cross_owner =
                source->owner_kind != destination->owner_kind ||
                source->owner_id != destination->owner_id;
            if (cross_owner &&
                (!route.destination_owner_explicit ||
                 route.destination_owner_kind != destination->owner_kind ||
                 route.destination_owner_id != destination->owner_id ||
                 !source->shared)) {
                diagnostics.message =
                    "cross-owner modulator connection requires an explicitly "
                    "shared source and matching destination owner";
                return false;
            }
            if ((source->owner_kind == ModulatorOwnerKind::voice_definition ||
                 destination->owner_kind ==
                     ModulatorOwnerKind::voice_definition) &&
                (source->owner_kind != ModulatorOwnerKind::voice_definition ||
                 destination->owner_kind !=
                     ModulatorOwnerKind::voice_definition ||
                 source->owner_id != destination->owner_id)) {
                diagnostics.message =
                    "voice-local modulator connections cannot cross voices or parents";
                return false;
            }
            if (!cross_owner && route.destination_owner_explicit &&
                (route.destination_owner_kind != destination->owner_kind ||
                 route.destination_owner_id != destination->owner_id)) {
                diagnostics.message =
                    "connection destination owner does not match modulator";
                return false;
            }
            if (source->id == destination->id) {
                diagnostics.message = "direct modulation cycle";
                return false;
            }
            const auto slot = static_cast<std::size_t>(
                destination - configuration.modulators.begin());
            if (route.enabled)
                ++indegree[slot];
        }
    }
    std::array<bool, ModulationConfiguration::maximum_modulators> emitted{};
    for (std::size_t count = 0; count < configuration.modulator_count; ++count) {
        std::size_t selected = configuration.modulator_count;
        for (std::size_t candidate = 0;
             candidate < configuration.modulator_count; ++candidate)
            if (!emitted[candidate] && indegree[candidate] == 0 &&
                (selected == configuration.modulator_count ||
                 configuration.modulators[candidate].id <
                     configuration.modulators[selected].id))
                selected = candidate;
        if (selected == configuration.modulator_count) {
            diagnostics.message = "indirect modulation cycle";
            return false;
        }
        emitted[selected] = true;
        diagnostics.evaluation_order[diagnostics.evaluation_count++] =
            configuration.modulators[selected].id;
        for (std::size_t route_index = 0;
             route_index < configuration.route_count; ++route_index) {
            const auto &route = configuration.routes[route_index];
            if (!route.enabled ||
                route.destination_kind == ModulationDestinationKind::parameter ||
                route.source_instance != configuration.modulators[selected].id)
                continue;
            const auto destination = find_instance(route.destination_modulator);
            if (destination != configuration.modulators.begin() +
                                   configuration.modulator_count)
                --indegree[static_cast<std::size_t>(
                    destination - configuration.modulators.begin())];
        }
    }
    diagnostics.connection = 0;
    diagnostics.valid = true;
    return true;
}

bool prepare_modulation_graph(ModulationConfiguration &configuration,
                              ModulationGraphDiagnostics &diagnostics) {
    configuration.graph_prepared = false;
    configuration.prepared_count = 0;
    if (!validate_modulation_graph(configuration, diagnostics))
        return false;
    configuration.prepared_order = diagnostics.evaluation_order;
    configuration.prepared_count = diagnostics.evaluation_count;
    configuration.graph_prepared = true;
    return true;
}

ModulationEngine::ModulationEngine(std::uint32_t sample_rate) noexcept
    : sample_rate_(std::max(1U, sample_rate)) {
    track_ids_[0] = 0;
}

bool ModulationEngine::configure(
    const ModulationConfiguration &configuration) noexcept {
    ModulationGraphDiagnostics graph;
    if (configuration.graph_prepared) {
        if (configuration.prepared_count != configuration.modulator_count)
            return false;
        graph.valid = true;
        graph.evaluation_order = configuration.prepared_order;
        graph.evaluation_count = configuration.prepared_count;
    } else if (!validate_modulation_graph(configuration, graph))
        return false;
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
               source.follower_threshold <= 1 && source.step_count >= 1 &&
               source.step_count <= source.step_values.size() &&
               source.pulses <= source.step_count &&
               source.rotation < source.step_count && source.order <= 3 &&
               source.distribution <= 2 &&
               std::isfinite(source.step_duration_beats) &&
               source.step_duration_beats >= 0.03125F &&
               source.step_duration_beats <= 16 &&
               std::isfinite(source.swing) && source.swing >= 0 &&
               source.swing <= 0.75F && std::isfinite(source.gate_length) &&
               source.gate_length >= 0.01F && source.gate_length <= 1 &&
               std::isfinite(source.correlation) && source.correlation >= 0 &&
               source.correlation <= 1 && std::isfinite(source.quantization) &&
               source.quantization >= 0 && source.quantization <= 32 &&
               std::isfinite(source.slew_seconds) && source.slew_seconds >= 0 &&
               source.slew_seconds <= 10 && std::isfinite(source.rise_seconds) &&
               source.rise_seconds >= 0.001F && source.rise_seconds <= 60 &&
               std::isfinite(source.fall_seconds) &&
               source.fall_seconds >= 0.001F && source.fall_seconds <= 60 &&
               std::isfinite(source.curve) && source.curve >= -1 &&
               source.curve <= 1 && std::isfinite(source.scale) &&
               source.scale >= -4 && source.scale <= 4 &&
               std::isfinite(source.offset) && source.offset >= -1 &&
               source.offset <= 1 && std::isfinite(source.chaos) &&
               source.chaos >= 3.5F && source.chaos <= 4 &&
               std::all_of(source.step_values.begin(), source.step_values.end(),
                           [](float value) {
                               return std::isfinite(value) && value >= -1 &&
                                      value <= 1;
                           });
    };
    for (std::size_t i = 0; i < configuration.modulator_count; ++i) {
        const auto &instance = configuration.modulators[i];
        const auto *type = find_modulator_type(instance.type);
        if (!instance.id || !type ||
            !(type->owner_mask & owner_bit(instance.owner_kind)) ||
            (instance.owner_kind == ModulatorOwnerKind::voice_definition &&
             !voice_modulator_supported(instance.type)) ||
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
                              configuration.modulator_count)
                return false;
            const auto expected_scope =
                source->owner_kind == ModulatorOwnerKind::track
                    ? ModulationScope::track
                : source->owner_kind == ModulatorOwnerKind::voice_definition
                    ? ModulationScope::voice
                    : ModulationScope::global;
            if (route.scope != expected_scope ||
                (source->owner_kind == ModulatorOwnerKind::track &&
                 route.track_id != source->owner_id))
                return false;
        }
        if (route.destination_kind != ModulationDestinationKind::parameter)
            continue;
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
        if (route.scope == ModulationScope::voice) {
            const auto synth_voice =
                route.destination.target == ParameterTarget::subtractive_synth &&
                route.destination.instance != 0 &&
                (route.destination.parameter ==
                     static_cast<std::uint32_t>(SynthParameter::pitch) ||
                 route.destination.parameter ==
                     static_cast<std::uint32_t>(SynthParameter::cutoff) ||
                 route.destination.parameter ==
                     static_cast<std::uint32_t>(SynthParameter::gain));
            const auto sample_voice =
                route.destination.target == ParameterTarget::sample_voice &&
                route.track_id != 0 && route.destination.instance == route.track_id &&
                route.destination.parameter >=
                    static_cast<std::uint32_t>(SampleModulationParameter::pitch) &&
                route.destination.parameter <=
                    static_cast<std::uint32_t>(SampleModulationParameter::pan);
            if (!synth_voice && !sample_voice)
                return false;
        }
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
    evaluation_count_ = graph.evaluation_count;
    for (std::size_t order = 0; order < graph.evaluation_count; ++order) {
        const auto found = std::find_if(
            configuration_.modulators.begin(),
            configuration_.modulators.begin() + configuration_.modulator_count,
            [&](const auto &instance) {
                return instance.id == graph.evaluation_order[order];
            });
        evaluation_order_[order] = static_cast<std::size_t>(
            found - configuration_.modulators.begin());
    }
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
    modulator_automation_.fill({});
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
            if (dynamic_type == ModulatorType::envelope_follower &&
                configuration_.modulators[instance].settings.follower_tap ==
                    FollowerTapKind::master &&
                configuration_.modulators[instance].settings.follower_tap_id ==
                    0)
                modulator_states_[instance].target =
                    std::clamp(value, 0.0F, 1.0F);
            else if (dynamic_type != ModulatorType::envelope_follower)
                modulator_states_[instance].value = clamp_source(value);
        }
}
void ModulationEngine::set_follower_input(FollowerTapKind tap,
                                          std::uint64_t tap_id,
                                          float value) noexcept {
    if (!std::isfinite(value))
        return;
    for (std::size_t instance = 0; instance < configuration_.modulator_count;
         ++instance) {
        const auto &definition = configuration_.modulators[instance];
        if (definition.enabled &&
            definition.type == ModulatorType::envelope_follower &&
            definition.settings.follower_tap == tap &&
            definition.settings.follower_tap_id == tap_id)
            modulator_states_[instance].target = std::clamp(value, 0.0F, 1.0F);
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
        if (definition.type == ModulatorType::adsr ||
            definition.type == ModulatorType::function_generator) {
            modulator_states_[instance].gate = true;
            modulator_states_[instance].envelope_stage =
                SourceState::EnvelopeStage::attack;
        }
        if (definition.settings.retrigger)
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
        if ((definition.type == ModulatorType::adsr ||
             definition.type == ModulatorType::function_generator) &&
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
        modulator_states_[instance].step = 0;
        modulator_states_[instance].direction = 1;
        modulator_states_[instance].gate = false;
        if (configuration_.modulators[instance].settings.retrigger)
            modulator_states_[instance].value = 0;
    }
}
void ModulationEngine::clear_automation() noexcept {
    for (auto &value : automation_)
        value.active = false;
    for (auto &value : modulator_automation_)
        value.active = false;
}
void ModulationEngine::set_modulator_automation(
    ModulatorInstanceId instance, ModulatorParameterId parameter,
    float value) noexcept {
    if (!std::isfinite(value))
        return;
    for (auto &entry : modulator_automation_)
        if (entry.active && entry.instance == instance &&
            entry.parameter == parameter) {
            entry.value = value;
            return;
        }
    for (auto &entry : modulator_automation_)
        if (!entry.active) {
            entry = {instance, parameter, value, true};
            return;
        }
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

ModulationSourceSettings
ModulationEngine::effective_settings(std::size_t index) const noexcept {
    auto settings = configuration_.modulators[index].settings;
    const auto destination_id = configuration_.modulators[index].id;
    const auto *type = find_modulator_type(configuration_.modulators[index].type);
    if (!type)
        return settings;
    for (const auto &descriptor : type->parameters) {
        if (!descriptor.modulatable || descriptor.discrete)
            continue;
        float base{};
        switch (descriptor.numeric_id) {
        case ModulatorParameterId::rate: base = settings.rate_hz; break;
        case ModulatorParameterId::beats: base = settings.beats_per_cycle; break;
        case ModulatorParameterId::attack:
            base = configuration_.modulators[index].type ==
                           ModulatorType::envelope_follower
                       ? settings.follower_attack_seconds
                       : settings.attack_seconds;
            break;
        case ModulatorParameterId::decay: base = settings.decay_seconds; break;
        case ModulatorParameterId::sustain: base = settings.sustain; break;
        case ModulatorParameterId::release:
            base = configuration_.modulators[index].type ==
                           ModulatorType::envelope_follower
                       ? settings.follower_release_seconds
                       : settings.release_seconds;
            break;
        case ModulatorParameterId::follower_gain:
            base = settings.follower_gain;
            break;
        case ModulatorParameterId::follower_threshold:
            base = settings.follower_threshold;
            break;
        case ModulatorParameterId::step_duration:
            base = settings.step_duration_beats;
            break;
        case ModulatorParameterId::swing: base = settings.swing; break;
        case ModulatorParameterId::gate_length: base = settings.gate_length; break;
        case ModulatorParameterId::correlation: base = settings.correlation; break;
        case ModulatorParameterId::quantization: base = settings.quantization; break;
        case ModulatorParameterId::slew: base = settings.slew_seconds; break;
        case ModulatorParameterId::rise: base = settings.rise_seconds; break;
        case ModulatorParameterId::fall: base = settings.fall_seconds; break;
        case ModulatorParameterId::curve: base = settings.curve; break;
        case ModulatorParameterId::scale: base = settings.scale; break;
        case ModulatorParameterId::offset: base = settings.offset; break;
        case ModulatorParameterId::chaos: base = settings.chaos; break;
        default: continue;
        }
        for (const auto &automation : modulator_automation_)
            if (automation.active && automation.instance == destination_id &&
                automation.parameter == descriptor.numeric_id) {
                base = automation.value;
                break;
            }
        auto normalized = (base - descriptor.minimum) /
                          (descriptor.maximum - descriptor.minimum);
        for (std::size_t route_index = 0;
             route_index < configuration_.route_count; ++route_index) {
            const auto &route = configuration_.routes[route_index];
            if (!route.enabled ||
                route.destination_kind !=
                    ModulationDestinationKind::modulator_parameter ||
                route.destination_modulator != destination_id ||
                route.destination_modulator_parameter != descriptor.numeric_id)
                continue;
            const auto source = std::find_if(
                configuration_.modulators.begin(),
                configuration_.modulators.begin() + configuration_.modulator_count,
                [&](const auto &instance) {
                    return instance.id == route.source_instance;
                });
            if (source == configuration_.modulators.begin() +
                              configuration_.modulator_count)
                continue;
            const auto value = modulator_output(
                static_cast<std::size_t>(source - configuration_.modulators.begin()),
                route.source_output);
            normalized += (route.polarity == ModulationPolarity::bipolar
                               ? value
                               : (value + 1.0F) * 0.5F) *
                          route.depth;
        }
        const auto effective = descriptor.minimum +
                               std::clamp(normalized, 0.0F, 1.0F) *
                                   (descriptor.maximum - descriptor.minimum);
        switch (descriptor.numeric_id) {
        case ModulatorParameterId::rate: settings.rate_hz = effective; break;
        case ModulatorParameterId::beats: settings.beats_per_cycle = effective; break;
        case ModulatorParameterId::attack:
            if (configuration_.modulators[index].type ==
                ModulatorType::envelope_follower)
                settings.follower_attack_seconds = effective;
            else
                settings.attack_seconds = effective;
            break;
        case ModulatorParameterId::decay: settings.decay_seconds = effective; break;
        case ModulatorParameterId::sustain: settings.sustain = effective; break;
        case ModulatorParameterId::release:
            if (configuration_.modulators[index].type ==
                ModulatorType::envelope_follower)
                settings.follower_release_seconds = effective;
            else
                settings.release_seconds = effective;
            break;
        case ModulatorParameterId::follower_gain:
            settings.follower_gain = effective;
            break;
        case ModulatorParameterId::follower_threshold:
            settings.follower_threshold = effective;
            break;
        case ModulatorParameterId::step_duration:
            settings.step_duration_beats = effective;
            break;
        case ModulatorParameterId::swing: settings.swing = effective; break;
        case ModulatorParameterId::gate_length:
            settings.gate_length = effective;
            break;
        case ModulatorParameterId::correlation:
            settings.correlation = effective;
            break;
        case ModulatorParameterId::quantization:
            settings.quantization = effective;
            break;
        case ModulatorParameterId::slew:
            settings.slew_seconds = effective;
            break;
        case ModulatorParameterId::rise: settings.rise_seconds = effective; break;
        case ModulatorParameterId::fall: settings.fall_seconds = effective; break;
        case ModulatorParameterId::curve: settings.curve = effective; break;
        case ModulatorParameterId::scale: settings.scale = effective; break;
        case ModulatorParameterId::offset: settings.offset = effective; break;
        case ModulatorParameterId::chaos: settings.chaos = effective; break;
        default: break;
        }
    }
    return settings;
}

void ModulationEngine::update_modulators(std::uint32_t elapsed) noexcept {
    const auto seconds = static_cast<double>(elapsed) / sample_rate_;
    for (std::size_t order = 0; order < evaluation_count_; ++order) {
        const auto index = evaluation_order_[order];
        const auto &instance = configuration_.modulators[index];
        auto &state = modulator_states_[index];
        state.inputs.fill(0);
        for (std::size_t route_index = 0;
             route_index < configuration_.route_count; ++route_index) {
            const auto &route = configuration_.routes[route_index];
            if (!route.enabled || route.destination_kind !=
                                      ModulationDestinationKind::modulator_input ||
                route.destination_modulator != instance.id ||
                route.destination_input == 0 || route.destination_input > 3)
                continue;
            const auto source = std::find_if(
                configuration_.modulators.begin(),
                configuration_.modulators.begin() + configuration_.modulator_count,
                [&](const auto &candidate) {
                    return candidate.id == route.source_instance;
                });
            if (source == configuration_.modulators.begin() +
                              configuration_.modulator_count)
                continue;
            const auto source_index = static_cast<std::size_t>(
                source - configuration_.modulators.begin());
            auto value = modulator_output(source_index, route.source_output);
            if (route.polarity == ModulationPolarity::unipolar)
                value = (value + 1.0F) * 0.5F;
            state.inputs[route.destination_input - 1] = std::clamp(
                state.inputs[route.destination_input - 1] + value * route.depth,
                -1.0F, 1.0F);
        }
        if (!instance.enabled) {
            state.value = 0;
            state.outputs.fill(0);
            continue;
        }
        const auto settings = effective_settings(index);
        const auto rate =
            settings.tempo_sync
                ? tempo_bpm_ /
                      (60.0F * std::max(0.03125F, settings.beats_per_cycle))
                : std::max(0.0F, settings.rate_hz);
        state.outputs[1] = 0;
        state.outputs[2] = 0;
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
        } else if (instance.type == ModulatorType::step_sequencer) {
            const auto swing = (state.step & 1U) ? settings.swing : -settings.swing;
            const auto duration = std::max(
                1.0e-6, static_cast<double>(settings.step_duration_beats) *
                            60.0 / tempo_bpm_ * (1.0 + swing));
            const auto previous = state.phase;
            state.phase += seconds / duration;
            bool advanced{};
            while (state.phase >= 1.0 - 1.0e-7) {
                state.phase = std::max(0.0, state.phase - 1.0);
                advanced = true;
                if (settings.order == 1)
                    state.step = (state.step + settings.step_count - 1) %
                                 settings.step_count;
                else if (settings.order == 2) {
                    if (settings.step_count <= 1)
                        state.step = 0;
                    else if (state.direction &&
                             state.step + 1 >= settings.step_count)
                        state.direction = 0;
                    else if (!state.direction && state.step == 0)
                        state.direction = 1;
                    if (settings.step_count > 1)
                        state.step = state.direction ? state.step + 1
                                                     : state.step - 1;
                } else if (settings.order == 3) {
                    const auto random_value = instance_random(index);
                    state.step = std::min<std::uint32_t>(
                        settings.step_count - 1,
                        static_cast<std::uint32_t>((random_value + 1.0F) * 0.5F *
                                                   settings.step_count));
                } else
                    state.step = (state.step + 1) % settings.step_count;
            }
            (void)previous;
            state.value = settings.step_values[state.step];
            if (settings.unipolar)
                state.value = (state.value + 1.0F) * 0.5F;
            state.outputs[1] = advanced ? 1.0F : 0.0F;
        } else if (instance.type == ModulatorType::euclidean) {
            const auto duration = std::max(
                1.0e-6, static_cast<double>(settings.step_duration_beats) *
                            60.0 / tempo_bpm_);
            state.phase += seconds / duration;
            while (state.phase >= 1.0 - 1.0e-7) {
                state.phase = std::max(0.0, state.phase - 1.0);
                state.step = (state.step + 1) % settings.step_count;
            }
            const auto rotated =
                (state.step + settings.rotation) % settings.step_count;
            const auto hit = settings.pulses != 0 &&
                             (rotated * settings.pulses) % settings.step_count <
                                 settings.pulses;
            const auto gate = hit && state.phase < settings.gate_length;
            state.value = gate ? 1.0F : 0.0F;
            state.outputs[1] = hit && (rotated % 4 == 0) ? 1.0F : 0.0F;
        } else if (instance.type == ModulatorType::clocked_random) {
            const auto duration = std::max(
                1.0e-6, static_cast<double>(settings.step_duration_beats) *
                            60.0 / tempo_bpm_);
            state.phase += seconds / duration;
            if (state.phase >= 1.0 - 1.0e-7) {
                state.phase = std::max(0.0, state.phase - 1.0);
                auto next = instance_random(index);
                if (settings.distribution == 1)
                    next = (next + instance_random(index)) * 0.5F;
                else if (settings.distribution == 2)
                    next = std::copysign(std::sqrt(std::fabs(next)), next);
                next = state.value * settings.correlation +
                       next * (1.0F - settings.correlation);
                if (settings.quantization >= 2) {
                    const auto levels = std::round(settings.quantization);
                    next = std::round((next + 1.0F) * 0.5F * (levels - 1)) /
                               (levels - 1) * 2.0F -
                           1.0F;
                }
                state.target = std::clamp(next, -1.0F, 1.0F);
                state.outputs[1] = 1.0F;
            }
            const auto coefficient =
                settings.slew_seconds <= 0
                    ? 1.0F
                    : 1.0F - std::exp(-static_cast<float>(seconds) /
                                      settings.slew_seconds);
            state.value += (state.target - state.value) * coefficient;
        } else if (instance.type == ModulatorType::function_generator) {
            const auto triggered = state.gate || state.inputs[0] > 0;
            if (triggered && !state.trigger_latched &&
                state.envelope_stage ==
                                 SourceState::EnvelopeStage::idle)
                state.envelope_stage = SourceState::EnvelopeStage::attack;
            state.trigger_latched = triggered;
            const auto shape = [&](float value) {
                const auto power = std::exp2(settings.curve * 2.0F);
                return std::pow(std::clamp(value, 0.0F, 1.0F), power);
            };
            if (state.envelope_stage == SourceState::EnvelopeStage::attack) {
                state.phase += seconds / settings.rise_seconds;
                if (state.phase >= 1.0) {
                    state.phase = 1.0;
                    state.envelope_stage = SourceState::EnvelopeStage::release;
                }
                state.value = shape(static_cast<float>(state.phase));
            } else if (state.envelope_stage ==
                       SourceState::EnvelopeStage::release) {
                state.phase -= seconds / settings.fall_seconds;
                if (state.phase <= 0.0) {
                    state.phase = 0;
                    state.outputs[1] = 1.0F;
                    state.envelope_stage = settings.looping
                                               ? SourceState::EnvelopeStage::attack
                                               : SourceState::EnvelopeStage::idle;
                }
                state.value = shape(static_cast<float>(state.phase));
            } else
                state.value = 0;
            if (!settings.unipolar)
                state.value = state.value * 2.0F - 1.0F;
        } else if (instance.type == ModulatorType::scale_offset)
            state.value = std::clamp(state.inputs[0] * settings.scale +
                                         settings.offset,
                                     -1.0F, 1.0F);
        else if (instance.type == ModulatorType::invert)
            state.value = -state.inputs[0];
        else if (instance.type == ModulatorType::rectify)
            state.value = std::fabs(state.inputs[0]);
        else if (instance.type == ModulatorType::slew) {
            const auto coefficient = settings.slew_seconds <= 0
                                         ? 1.0F
                                         : 1.0F - std::exp(
                                                        -static_cast<float>(seconds) /
                                                        settings.slew_seconds);
            state.value += (state.inputs[0] - state.value) * coefficient;
        } else if (instance.type == ModulatorType::quantize) {
            const auto levels = std::max(2.0F, std::round(settings.quantization));
            state.value = std::round((state.inputs[0] + 1.0F) * 0.5F *
                                     (levels - 1)) /
                              (levels - 1) * 2.0F -
                          1.0F;
        } else if (instance.type == ModulatorType::minimum)
            state.value = std::min(state.inputs[0], state.inputs[1]);
        else if (instance.type == ModulatorType::maximum)
            state.value = std::max(state.inputs[0], state.inputs[1]);
        else if (instance.type == ModulatorType::sum)
            state.value = std::clamp(state.inputs[0] + state.inputs[1], -1.0F,
                                     1.0F);
        else if (instance.type == ModulatorType::multiply)
            state.value = state.inputs[0] * state.inputs[1];
        else if (instance.type == ModulatorType::logistic) {
            const auto previous = state.phase;
            state.phase = std::fmod(state.phase + seconds * rate, 1.0);
            if (state.phase < previous) {
                auto x = std::clamp((state.value + 1.0F) * 0.5F, 0.001F,
                                    0.999F);
                x = settings.chaos * x * (1.0F - x);
                state.value = std::isfinite(x) ? x * 2.0F - 1.0F : 0.0F;
            }
        }
        state.value = std::isfinite(state.value)
                          ? std::clamp(state.value, -1.0F, 1.0F)
                          : 0.0F;
        state.outputs[0] = state.value;
    }
}

float ModulationEngine::modulator_output(std::size_t instance,
                                         std::uint32_t output) const noexcept {
    if (instance >= configuration_.modulator_count || output == 0 || output > 3)
        return 0;
    return modulator_states_[instance].outputs[output - 1];
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
    result.voice_definition_count = static_cast<std::size_t>(std::count_if(
        configuration_.modulators.begin(),
        configuration_.modulators.begin() + configuration_.modulator_count,
        [](const auto &instance) {
            return instance.owner_kind == ModulatorOwnerKind::voice_definition;
        }));
    for (std::size_t instance = 0; instance < result.modulator_count; ++instance) {
        result.modulator_values[instance] =
            published_modulators_[instance].load(std::memory_order_relaxed);
        for (std::size_t output = 0; output < 3; ++output)
            result.modulator_outputs[instance][output] =
                published_modulator_outputs_[instance][output].load(
                    std::memory_order_relaxed);
    }
    result.generation = published_generation_.load(std::memory_order_acquire);
    return result;
}
} // namespace neurogenic
