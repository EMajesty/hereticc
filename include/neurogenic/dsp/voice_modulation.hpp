#pragma once
#include "neurogenic/dsp/modulation.hpp"
#include <array>
#include <cstdint>

namespace neurogenic {

struct SampleVoiceModulationValues {
    float pitch_semitones{};
    float gain{1.0F};
    float pan{};
};

// Transient, preallocated state for one sample voice. Configuration and graph
// topology remain shared in ModulationConfiguration; only phase, envelope and
// deterministic random state are replicated per voice.
struct SampleVoiceModulationState {
    struct Instance {
        double phase{};
        float value{}, held{}, target{};
        std::array<float, 3> outputs{}, inputs{};
        std::uint32_t random{1}, step{}, direction{1};
        std::uint8_t envelope_stage{};
        bool gate{}, trigger_latched{};
    };
    std::array<Instance, ModulationConfiguration::maximum_modulators> instances{};
    std::uint32_t frames_until_update{};
    std::uint64_t track_id{};
    float velocity{};
    SampleVoiceModulationValues current{};
    bool gate{}, active{};
};

[[nodiscard]] bool voice_modulator_supported(ModulatorType) noexcept;
void reset_sample_voice_modulation(SampleVoiceModulationState &,
                                   const ModulationConfiguration &,
                                   std::uint64_t track_id, float velocity,
                                   std::uint64_t occurrence) noexcept;
void release_sample_voice_modulation(SampleVoiceModulationState &) noexcept;
[[nodiscard]] SampleVoiceModulationValues process_sample_voice_modulation(
    SampleVoiceModulationState &, const ModulationConfiguration &,
    std::uint32_t sample_rate, float tempo_bpm) noexcept;

} // namespace neurogenic
