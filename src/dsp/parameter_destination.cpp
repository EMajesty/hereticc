#include "neurogenic/dsp/parameter_destination.hpp"
#include "neurogenic/dsp/processor.hpp"
#include "neurogenic/instrument/subtractive_synth.hpp"
namespace neurogenic {
std::optional<ParameterDestination>
resolve_parameter_destination(std::string_view id) noexcept {
    using S = SynthParameter;
    using D = DelayParameter;
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
