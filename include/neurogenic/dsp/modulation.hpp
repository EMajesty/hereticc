#pragma once
#include "neurogenic/dsp/parameter_destination.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace neurogenic {
using ModulationRouteId = std::uint32_t;
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
    count
};
enum class LfoWaveform : std::uint8_t { sine, triangle, saw, square, random };
enum class ModulationPolarity : std::uint8_t { bipolar, unipolar };
enum class ModulationScope : std::uint8_t { global, track, voice };

struct ModulationSourceSettings {
    LfoWaveform waveform{LfoWaveform::sine};
    float rate_hz{1.0F};
    bool tempo_sync{};
    float beats_per_cycle{1.0F};
    bool retrigger{};
    ModulationScope scope{ModulationScope::global};
    float attack_seconds{0.01F}, decay_seconds{0.1F}, sustain{1.0F},
        release_seconds{0.2F};
    std::uint32_t seed{1};
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
};
struct ModulationConfiguration {
    static constexpr std::size_t maximum_routes = 32;
    static constexpr std::size_t maximum_tracks = 32;
    static constexpr std::size_t maximum_instances = 16;
    static constexpr std::size_t source_count =
        static_cast<std::size_t>(ModulationSourceId::count);
    std::array<ModulationSourceSettings, source_count> sources{};
    std::array<ModulationRoute, maximum_routes> routes{};
    std::size_t route_count{};
    std::uint32_t update_interval{32};
};
struct ModulationSnapshot {
    std::array<float, ModulationConfiguration::source_count> source_values{};
    std::array<float, ModulationConfiguration::maximum_routes>
        effective_values{};
    std::array<float, ModulationConfiguration::maximum_routes>
        route_source_values{};
    std::uint64_t generation{};
};

class ModulationEngine {
  public:
    explicit ModulationEngine(std::uint32_t sample_rate = 48000) noexcept;
    [[nodiscard]] bool configure(const ModulationConfiguration &) noexcept;
    void set_sample_rate(std::uint32_t) noexcept;
    void set_tempo(float bpm) noexcept;
    void set_input(ModulationSourceId, float) noexcept;
    void note_on(std::uint64_t track_id, float velocity) noexcept;
    void note_off(std::uint64_t track_id) noexcept;
    void restart() noexcept;
    void clear_automation() noexcept;
    void set_automation(ParameterDestination, float) noexcept;
    template <class Apply>
    void process_frame(Apply &&apply) noexcept {
        if (++frames_until_update_ < configuration_.update_interval)
            return;
        const auto elapsed = frames_until_update_;
        frames_until_update_ = 0;
        update_sources(elapsed);
        apply_routes(static_cast<Apply &&>(apply));
    }
    [[nodiscard]] ModulationSnapshot snapshot() const noexcept;
    [[nodiscard]] const ModulationConfiguration &configuration() const noexcept {
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
    void update_sources(std::uint32_t) noexcept;
    float waveform(std::size_t, std::size_t, double, bool) noexcept;
    float random(std::size_t, std::size_t) noexcept;
    [[nodiscard]] std::size_t track_slot(std::uint64_t) const noexcept;
    template <class Apply> void apply_routes(Apply &&apply) noexcept {
        for (std::size_t route_index = 0;
             route_index < configuration_.route_count; ++route_index) {
            const auto &route = configuration_.routes[route_index];
            if (!route.enabled || route.scope == ModulationScope::voice)
                continue;
            bool already_processed{};
            for (std::size_t previous = 0; previous < route_index; ++previous)
                already_processed |=
                    configuration_.routes[previous].enabled &&
                    configuration_.routes[previous].scope !=
                        ModulationScope::voice &&
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
                    candidate.destination.target != route.destination.target ||
                    candidate.destination.parameter !=
                        route.destination.parameter ||
                    candidate.destination.instance != route.destination.instance)
                    continue;
                const auto source_index =
                    static_cast<std::size_t>(candidate.source);
                const auto state_slot =
                    configuration_.sources[source_index].scope ==
                            ModulationScope::track
                        ? track_slot(candidate.track_id)
                        : 0;
                if (state_slot >= track_count_)
                    continue;
                const auto source =
                    source_states_[state_slot][source_index].value;
                published_route_sources_[contribution].store(
                    source, std::memory_order_relaxed);
                const auto shaped =
                    candidate.polarity == ModulationPolarity::bipolar
                        ? source
                        : (source + 1.0F) * 0.5F;
                normalized += shaped * candidate.depth;
            }
            const auto effective = route.minimum +
                                   std::clamp(normalized, 0.0F, 1.0F) * range;
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
        published_generation_.fetch_add(1, std::memory_order_release);
    }
    ModulationConfiguration configuration_;
    std::array<std::array<SourceState, ModulationConfiguration::source_count>,
               ModulationConfiguration::maximum_tracks>
        source_states_{};
    std::array<std::uint64_t, ModulationConfiguration::maximum_tracks>
        track_ids_{};
    std::size_t track_count_{1};
    std::array<AutomationValue, ModulationConfiguration::maximum_routes>
        automation_{};
    std::array<float, ModulationConfiguration::maximum_routes> effective_{};
    std::array<std::atomic<float>, ModulationConfiguration::source_count>
        published_sources_{};
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
