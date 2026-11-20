#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
namespace neurogenic {
enum class ParameterTarget : std::uint32_t {
    none,
    subtractive_synth,
    stereo_delay,
    sample_voice,
    mixer_track,
    mixer_aux,
    mixer_master,
    external_fx,
    native_effect,
    mixer_group
};
enum class SampleModulationParameter : std::uint32_t { pitch = 1, gain, pan };
enum class MixerTrackModulationParameter : std::uint32_t {
    gain = 1,
    pan,
    aux_send_1,
    aux_send_2
};
enum class MixerAuxModulationParameter : std::uint32_t { return_gain = 1 };
enum class MixerMasterModulationParameter : std::uint32_t { gain = 1 };
enum class MixerGroupModulationParameter : std::uint32_t {
    gain = 1,
    pan,
    aux_send_1,
    aux_send_2
};
enum class ExternalFxModulationParameter : std::uint32_t {
    wet = 1,
    send_gain,
    return_gain
};
struct ParameterDestination {
    ParameterTarget target{};
    std::uint32_t parameter{};
    std::uint32_t instance{};
};
[[nodiscard]] std::optional<ParameterDestination>
    resolve_parameter_destination(std::string_view) noexcept;
[[nodiscard]] bool valid_legacy_parameter(ParameterTarget,
                                          std::uint32_t) noexcept;
} // namespace neurogenic
