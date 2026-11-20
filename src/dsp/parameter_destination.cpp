#include "neurogenic/dsp/parameter_destination.hpp"
#include "neurogenic/dsp/effect_chain.hpp"
#include "neurogenic/dsp/processor.hpp"
#include "neurogenic/instrument/subtractive_synth.hpp"
#include <charconv>
namespace neurogenic {
std::optional<ParameterDestination>
resolve_parameter_destination(std::string_view id) noexcept {
    using S = SynthParameter;
    using D = DelayParameter;
    if (id.starts_with("fx.")) {
        id.remove_prefix(3);
        const auto separator = id.find('.');
        std::uint32_t instance{}, parameter{};
        if (separator != std::string_view::npos) {
            const auto instance_text = id.substr(0, separator);
            const auto parameter_text = id.substr(separator + 1);
            const auto a = std::from_chars(
                instance_text.data(),
                instance_text.data() + instance_text.size(), instance);
            const auto b = std::from_chars(
                parameter_text.data(),
                parameter_text.data() + parameter_text.size(), parameter);
            if (a.ec == std::errc{} && a.ptr == instance_text.end() &&
                b.ec == std::errc{} && b.ptr == parameter_text.end() &&
                instance && parameter >= 1 &&
                parameter <= maximum_effect_parameters)
                return {{ParameterTarget::native_effect, parameter, instance}};
        }
        return std::nullopt;
    }
    if (id == "synth.waveform")
        return {{ParameterTarget::subtractive_synth,
                 static_cast<std::uint32_t>(S::waveform)}};
    if (id == "synth.attack")
        return {{ParameterTarget::subtractive_synth,
                 static_cast<std::uint32_t>(S::attack)}};
    if (id == "synth.decay")
        return {{ParameterTarget::subtractive_synth,
                 static_cast<std::uint32_t>(S::decay)}};
    if (id == "synth.sustain")
        return {{ParameterTarget::subtractive_synth,
                 static_cast<std::uint32_t>(S::sustain)}};
    if (id == "synth.release")
        return {{ParameterTarget::subtractive_synth,
                 static_cast<std::uint32_t>(S::release)}};
    if (id == "synth.cutoff")
        return {{ParameterTarget::subtractive_synth,
                 static_cast<std::uint32_t>(S::cutoff)}};
    if (id == "synth.resonance")
        return {{ParameterTarget::subtractive_synth,
                 static_cast<std::uint32_t>(S::resonance)}};
    if (id == "synth.pulse-width")
        return {{ParameterTarget::subtractive_synth,
                 static_cast<std::uint32_t>(S::pulse_width)}};
    if (id == "synth.gain")
        return {{ParameterTarget::subtractive_synth,
                 static_cast<std::uint32_t>(S::gain)}};
    if (id == "synth.pitch")
        return {{ParameterTarget::subtractive_synth,
                 static_cast<std::uint32_t>(S::pitch)}};
    if (id == "effect.delay.time")
        return {{ParameterTarget::stereo_delay,
                 static_cast<std::uint32_t>(D::time)}};
    if (id == "effect.delay.feedback")
        return {{ParameterTarget::stereo_delay,
                 static_cast<std::uint32_t>(D::feedback)}};
    if (id == "effect.delay.mix")
        return {{ParameterTarget::stereo_delay,
                 static_cast<std::uint32_t>(D::mix)}};
    if (id == "sample.pitch")
        return {{ParameterTarget::sample_voice,
                 static_cast<std::uint32_t>(SampleModulationParameter::pitch)}};
    if (id == "sample.gain")
        return {{ParameterTarget::sample_voice,
                 static_cast<std::uint32_t>(SampleModulationParameter::gain)}};
    if (id == "sample.pan")
        return {{ParameterTarget::sample_voice,
                 static_cast<std::uint32_t>(SampleModulationParameter::pan)}};
    if (id == "mixer.track.gain")
        return {
            {ParameterTarget::mixer_track,
             static_cast<std::uint32_t>(MixerTrackModulationParameter::gain)}};
    if (id == "mixer.track.pan")
        return {
            {ParameterTarget::mixer_track,
             static_cast<std::uint32_t>(MixerTrackModulationParameter::pan)}};
    if (id == "mixer.track.aux-1")
        return {{ParameterTarget::mixer_track,
                 static_cast<std::uint32_t>(
                     MixerTrackModulationParameter::aux_send_1)}};
    if (id == "mixer.track.aux-2")
        return {{ParameterTarget::mixer_track,
                 static_cast<std::uint32_t>(
                     MixerTrackModulationParameter::aux_send_2)}};
    if (id == "mixer.aux.return")
        return {{ParameterTarget::mixer_aux, 1}};
    if (id == "mixer.master.gain")
        return {{ParameterTarget::mixer_master, 1}};
    if (id == "external-fx.wet")
        return {
            {ParameterTarget::external_fx,
             static_cast<std::uint32_t>(ExternalFxModulationParameter::wet)}};
    if (id == "external-fx.send-gain")
        return {{ParameterTarget::external_fx,
                 static_cast<std::uint32_t>(
                     ExternalFxModulationParameter::send_gain)}};
    if (id == "external-fx.return-gain")
        return {{ParameterTarget::external_fx,
                 static_cast<std::uint32_t>(
                     ExternalFxModulationParameter::return_gain)}};
    return std::nullopt;
}
bool valid_legacy_parameter(ParameterTarget target,
                            std::uint32_t parameter) noexcept {
    if (target == ParameterTarget::subtractive_synth)
        return parameter >= 1 &&
               parameter <=
                   static_cast<std::uint32_t>(SynthParameter::filter_enabled);
    if (target == ParameterTarget::stereo_delay)
        return parameter >= 1 &&
               parameter <= static_cast<std::uint32_t>(DelayParameter::mix);
    return false;
}
} // namespace neurogenic
