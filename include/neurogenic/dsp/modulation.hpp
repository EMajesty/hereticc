#pragma once
#include "neurogenic/dsp/effect_chain.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace neurogenic {
using ModulationRouteId = std::uint32_t;
using ModulatorInstanceId = std::uint32_t;
enum class ModulationSourceId : std::uint8_t {
    lfo_1,
    lfo_2,
    lfo_3,
    lfo_4,
    envelope,
    sample_hold,
    smooth_random,
    xy_x,
    xy_y,
    velocity,
    midi_cc,
    envelope_follower,
    count
};
enum class LfoWaveform : std::uint8_t { sine, triangle, saw, square, random };
enum class ModulationPolarity : std::uint8_t { bipolar, unipolar };
enum class ModulationScope : std::uint8_t { global, track, voice };
enum class ModulatorType : std::uint8_t {
    lfo = 1, adsr, sample_hold, smooth_random, envelope_follower, xy_manual,
    midi_cc, step_sequencer, euclidean, clocked_random, function_generator,
    scale_offset, invert, rectify, slew, quantize, minimum, maximum, sum,
    multiply, logistic, velocity, gate
};
enum class ModulatorOwnerKind : std::uint8_t {
    track = 1, auxiliary, master, group, voice_definition
};
enum class ModulatorUpdateRate : std::uint8_t { control, per_voice_control };
enum class ModulationDestinationKind : std::uint8_t {
    parameter,
    modulator_parameter,
    modulator_input
};
enum class ModulatorParameterId : std::uint32_t {
    rate = 1,
    beats,
    attack,
    decay,
    sustain,
    release,
    follower_gain,
    follower_threshold,
    step_duration,
    swing,
    pulses,
    rotation,
    gate_length,
    step_count,
    order,
    distribution,
    correlation,
    quantization,
    slew,
    rise,
    fall,
    curve,
    scale,
    offset,
    chaos
};
enum class FollowerTapKind : std::uint8_t {
    master,
    track,
    auxiliary,
    physical_input,
    group
};
struct ModulatorParameterDescriptor {
    std::string_view id, name, unit;
    float minimum{}, maximum{}, default_value{};
    bool discrete{};
    ModulatorParameterId numeric_id{};
    bool modulatable{true};
};
struct ModulatorPortDescriptor {
    std::uint32_t id{};
    std::string_view stable_id, name;
    ModulationPolarity polarity{ModulationPolarity::bipolar};
};
struct ModulatorTypeDescriptor {
    std::string_view id, name, category;
    ModulatorType type{};
    std::span<const ModulatorParameterDescriptor> parameters;
    std::span<const ModulatorPortDescriptor> inputs, outputs;
    std::uint32_t owner_mask{};
    bool triggerable{}, retriggerable{};
    ModulatorUpdateRate update_rate{ModulatorUpdateRate::control};
    std::uint32_t estimated_state_bytes{};
    std::uint32_t version{1};
};
[[nodiscard]] std::span<const ModulatorTypeDescriptor>
modulator_type_registry() noexcept;
[[nodiscard]] const ModulatorTypeDescriptor *
find_modulator_type(ModulatorType) noexcept;

struct ModulationSourceSettings {
    static constexpr std::size_t maximum_steps = 16;
    LfoWaveform waveform{LfoWaveform::sine};
    float rate_hz{1.0F};
    bool tempo_sync{};
    float beats_per_cycle{1.0F};
    bool retrigger{};
    ModulationScope scope{ModulationScope::global};
    float attack_seconds{0.01F}, decay_seconds{0.1F}, sustain{1.0F},
        release_seconds{0.2F};
    std::uint32_t seed{1};
    float follower_attack_seconds{0.01F}, follower_release_seconds{0.1F},
        follower_gain{1.0F}, follower_threshold{};
    FollowerTapKind follower_tap{FollowerTapKind::master};
    std::uint64_t follower_tap_id{};
    std::array<float, maximum_steps> step_values{};
    std::uint8_t step_count{8}, pulses{4}, rotation{}, order{}, distribution{};
    float step_duration_beats{0.25F}, swing{}, gate_length{0.5F};
    float correlation{}, quantization{}, slew_seconds{};
    float rise_seconds{0.1F}, fall_seconds{0.1F}, curve{};
    float scale{1.0F}, offset{}, chaos{3.8F};
    bool unipolar{}, looping{};
};
struct ModulationRoute {
    ModulationRouteId id{};
    ModulationSourceId source{ModulationSourceId::lfo_1};
    ParameterDestination destination;
    float minimum{}, maximum{1.0F}, base_value{};
    float depth{};
    bool enabled{true};
    ModulationPolarity polarity{ModulationPolarity::bipolar};
    ModulationScope scope{ModulationScope::global};
    std::uint64_t track_id{};
    ModulatorInstanceId source_instance{};
    std::uint32_t source_output{1};
    ModulationDestinationKind destination_kind{
        ModulationDestinationKind::parameter};
    ModulatorInstanceId destination_modulator{};
    ModulatorParameterId destination_modulator_parameter{};
    std::uint32_t destination_input{};
    bool destination_owner_explicit{};
    ModulatorOwnerKind destination_owner_kind{ModulatorOwnerKind::master};
    std::uint64_t destination_owner_id{};
};
struct ModulatorInstanceConfiguration {
    ModulatorInstanceId id{};
    ModulatorType type{ModulatorType::lfo};
    ModulatorOwnerKind owner_kind{ModulatorOwnerKind::master};
    std::uint64_t owner_id{};
    ModulationSourceSettings settings;
    std::array<char, 32> display_name{};
    bool enabled{true};
    bool shared{};
};
struct ModulationConfiguration {
    static constexpr std::size_t maximum_modulators = 64;
    static constexpr std::size_t maximum_routes = 128;
    static constexpr std::size_t maximum_tracks = 32;
    static constexpr std::size_t maximum_instances = 16;
    static constexpr std::size_t source_count =
        static_cast<std::size_t>(ModulationSourceId::count);
    std::array<ModulationSourceSettings, source_count> sources{};
    std::array<ModulatorInstanceConfiguration, maximum_modulators> modulators{};
    std::size_t modulator_count{};
    std::array<ModulationRoute, maximum_routes> routes{};
    std::size_t route_count{};
    std::uint32_t update_interval{32};
    // Control-thread compiled graph order. This is runtime publication data,
    // not persisted project state.
    std::array<ModulatorInstanceId, maximum_modulators> prepared_order{};
    std::size_t prepared_count{};
    bool graph_prepared{};
};
struct ModulationGraphDiagnostics {
    bool valid{};
    ModulationRouteId connection{};
    std::string message;
    std::array<ModulatorInstanceId, ModulationConfiguration::maximum_modulators>
        evaluation_order{};
    std::size_t evaluation_count{};
    std::size_t instance_capacity{ModulationConfiguration::maximum_modulators};
    std::size_t connection_capacity{ModulationConfiguration::maximum_routes};
};
[[nodiscard]] bool validate_modulation_graph(const ModulationConfiguration &,
                                             ModulationGraphDiagnostics &);
[[nodiscard]] bool prepare_modulation_graph(ModulationConfiguration &,
                                            ModulationGraphDiagnostics &);
struct ModulationSnapshot {
    std::array<float, ModulationConfiguration::source_count> source_values{};
    std::array<float, ModulationConfiguration::maximum_routes>
        effective_values{};
    std::array<float, ModulationConfiguration::maximum_routes>
        route_source_values{};
    std::array<float, ModulationConfiguration::maximum_modulators>
        modulator_values{};
    std::array<std::array<float, 3>,
               ModulationConfiguration::maximum_modulators>
        modulator_outputs{};
    std::size_t modulator_count{};
    std::uint64_t generation{};
    std::size_t voice_definition_count{};
};

class ModulationEngine {
  public:
    explicit ModulationEngine(std::uint32_t sample_rate = 48000) noexcept;
    [[nodiscard]] bool configure(const ModulationConfiguration &) noexcept;
    void set_sample_rate(std::uint32_t) noexcept;
    void set_tempo(float bpm) noexcept;
    void set_input(ModulationSourceId, float) noexcept;
    void set_follower_input(FollowerTapKind, std::uint64_t, float) noexcept;
    void note_on(std::uint64_t track_id, float velocity) noexcept;
    void note_off(std::uint64_t track_id) noexcept;
    void restart() noexcept;
    void clear_automation() noexcept;
    void set_automation(ParameterDestination, float) noexcept;
    void set_modulator_automation(ModulatorInstanceId, ModulatorParameterId,
                                  float) noexcept;
    template <class Apply> void process_frame(Apply &&apply) noexcept {
        if (++frames_until_update_ < configuration_.update_interval)
            return;
        const auto elapsed = frames_until_update_;
        frames_until_update_ = 0;
        update_sources(elapsed);
        update_modulators(elapsed);
        apply_routes(static_cast<Apply &&>(apply));
    }
    [[nodiscard]] ModulationSnapshot snapshot() const noexcept;
    [[nodiscard]] const ModulationConfiguration &
    configuration() const noexcept {
        return configuration_;
    }

  private:
    struct SourceState {
        double phase{};
        float value{}, held{}, target{};
        std::uint32_t random{};
        enum class EnvelopeStage : std::uint8_t {
            idle,
            attack,
            decay,
            sustain,
            release
        } envelope_stage{EnvelopeStage::idle};
    };
    struct AutomationValue {
        ParameterDestination destination;
        float value{};
        bool active{};
    };
    struct ModulatorState {
        double phase{};
        float value{}, held{}, target{};
        std::array<float, 3> outputs{};
        std::array<float, 3> inputs{};
        std::uint32_t random{1};
        std::uint32_t step{}, direction{1};
        SourceState::EnvelopeStage envelope_stage{
            SourceState::EnvelopeStage::idle};
        bool gate{}, trigger_latched{};
    };
    struct ModulatorAutomationValue {
        ModulatorInstanceId instance{};
        ModulatorParameterId parameter{};
        float value{};
        bool active{};
    };
    void update_sources(std::uint32_t) noexcept;
    void update_modulators(std::uint32_t) noexcept;
    [[nodiscard]] ModulationSourceSettings
    effective_settings(std::size_t) const noexcept;
    float waveform(std::size_t, std::size_t, double, bool) noexcept;
    float random(std::size_t, std::size_t) noexcept;
    float instance_random(std::size_t) noexcept;
    [[nodiscard]] float modulator_output(std::size_t, std::uint32_t) const noexcept;
    [[nodiscard]] std::size_t track_slot(std::uint64_t) const noexcept;
    template <class Apply> void apply_routes(Apply &&apply) noexcept {
        for (std::size_t route_index = 0;
             route_index < configuration_.route_count; ++route_index) {
            const auto &route = configuration_.routes[route_index];
            if (!route.enabled || route.scope == ModulationScope::voice ||
                route.destination_kind != ModulationDestinationKind::parameter)
                continue;
            bool already_processed{};
            for (std::size_t previous = 0; previous < route_index; ++previous)
                already_processed |=
                    configuration_.routes[previous].enabled &&
                    configuration_.routes[previous].scope !=
                        ModulationScope::voice &&
                    configuration_.routes[previous].destination_kind ==
                        ModulationDestinationKind::parameter &&
                    configuration_.routes[previous].destination.target ==
                        route.destination.target &&
                    configuration_.routes[previous].destination.parameter ==
                        route.destination.parameter &&
                    configuration_.routes[previous].destination.instance ==
                        route.destination.instance;
            if (already_processed)
                continue;
            float base = route.base_value;
            for (const auto &automation : automation_)
                if (automation.active &&
                    automation.destination.target == route.destination.target &&
                    automation.destination.parameter ==
                        route.destination.parameter &&
                    automation.destination.instance ==
                        route.destination.instance) {
                    base = automation.value;
                    break;
                }
            const auto range = route.maximum - route.minimum;
            float normalized = range > 0 ? (base - route.minimum) / range : 0;
            for (std::size_t contribution = route_index;
                 contribution < configuration_.route_count; ++contribution) {
                const auto &candidate = configuration_.routes[contribution];
                if (!candidate.enabled ||
                    candidate.scope == ModulationScope::voice ||
                    candidate.destination_kind !=
                        ModulationDestinationKind::parameter ||
                    candidate.destination.target != route.destination.target ||
                    candidate.destination.parameter !=
                        route.destination.parameter ||
                    candidate.destination.instance !=
                        route.destination.instance)
                    continue;
                float source{};
                if (candidate.source_instance) {
                    const auto found = std::find_if(
                        configuration_.modulators.begin(),
                        configuration_.modulators.begin() +
                            configuration_.modulator_count,
                        [&](const auto &instance) {
                            return instance.id == candidate.source_instance;
                        });
                    if (found == configuration_.modulators.begin() +
                                     configuration_.modulator_count)
                        continue;
                    source = modulator_output(
                        static_cast<std::size_t>(
                            found - configuration_.modulators.begin()),
                        candidate.source_output);
                } else {
                    const auto source_index =
                        static_cast<std::size_t>(candidate.source);
                    const auto state_slot =
                        configuration_.sources[source_index].scope ==
                                ModulationScope::track
                            ? track_slot(candidate.track_id)
                            : 0;
                    if (state_slot >= track_count_)
                        continue;
                    source = source_states_[state_slot][source_index].value;
                }
                published_route_sources_[contribution].store(
                    source, std::memory_order_relaxed);
                const auto shaped =
                    candidate.polarity == ModulationPolarity::bipolar
                        ? source
                        : (source + 1.0F) * 0.5F;
                normalized += shaped * candidate.depth;
            }
            const auto effective =
                route.minimum + std::clamp(normalized, 0.0F, 1.0F) * range;
            for (std::size_t matching = route_index;
                 matching < configuration_.route_count; ++matching)
                if (configuration_.routes[matching].destination.target ==
                        route.destination.target &&
                    configuration_.routes[matching].destination.parameter ==
                        route.destination.parameter &&
                    configuration_.routes[matching].destination.instance ==
                        route.destination.instance) {
                    effective_[matching] = effective;
                    published_effective_[matching].store(
                        effective, std::memory_order_relaxed);
                }
            apply(route.destination, effective);
        }
        for (std::size_t source = 0; source < configuration_.source_count;
             ++source)
            published_sources_[source].store(source_states_[0][source].value,
                                             std::memory_order_relaxed);
        for (std::size_t instance = 0;
             instance < configuration_.modulator_count; ++instance) {
            published_modulators_[instance].store(
                modulator_states_[instance].value, std::memory_order_relaxed);
            for (std::size_t output = 0; output < 3; ++output)
                published_modulator_outputs_[instance][output].store(
                    modulator_states_[instance].outputs[output],
                    std::memory_order_relaxed);
        }
        published_generation_.fetch_add(1, std::memory_order_release);
    }
    ModulationConfiguration configuration_;
    std::array<std::array<SourceState, ModulationConfiguration::source_count>,
               ModulationConfiguration::maximum_tracks>
        source_states_{};
    std::array<ModulatorState, ModulationConfiguration::maximum_modulators>
        modulator_states_{};
    std::array<std::size_t, ModulationConfiguration::maximum_modulators>
        evaluation_order_{};
    std::size_t evaluation_count_{};
    std::array<std::uint64_t, ModulationConfiguration::maximum_tracks>
        track_ids_{};
    std::size_t track_count_{1};
    std::array<AutomationValue, ModulationConfiguration::maximum_routes>
        automation_{};
    std::array<ModulatorAutomationValue,
               ModulationConfiguration::maximum_routes>
        modulator_automation_{};
    std::array<float, ModulationConfiguration::maximum_routes> effective_{};
    std::array<std::atomic<float>, ModulationConfiguration::source_count>
        published_sources_{};
    std::array<std::atomic<float>, ModulationConfiguration::maximum_modulators>
        published_modulators_{};
    std::array<std::array<std::atomic<float>, 3>,
               ModulationConfiguration::maximum_modulators>
        published_modulator_outputs_{};
    std::array<std::atomic<float>, ModulationConfiguration::maximum_routes>
        published_effective_{};
    std::array<std::atomic<float>, ModulationConfiguration::maximum_routes>
        published_route_sources_{};
    std::uint32_t sample_rate_{48000}, frames_until_update_{};
    float tempo_bpm_{120.0F};
    std::array<bool, ModulationConfiguration::maximum_tracks> envelope_gates_{};
    std::atomic<std::uint64_t> published_generation_{};
};
} // namespace neurogenic
