#include "neurogenic/instrument/subtractive_synth.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
namespace neurogenic {
namespace {
float poly_blep(float phase, float increment) noexcept {
    if (increment <= 0)
        return 0;
    if (phase < increment) {
        const auto x = phase / increment;
        return x + x - x * x - 1;
    }
    if (phase > 1 - increment) {
        const auto x = (phase - 1) / increment;
        return x * x + x + x + 1;
    }
    return 0;
}
float midi_frequency(std::uint8_t note) noexcept {
    return 440 * std::exp2((static_cast<int>(note) - 69) / 12.0F);
}
} // namespace
SubtractiveSynth::SubtractiveSynth(std::uint32_t rate, std::size_t voices)
    : rate_(std::max(1U, rate)), voices_(std::max<std::size_t>(1, voices)) {}
void SubtractiveSynth::note_on(std::uint8_t note, float velocity,
                               NoteIdentity owner) noexcept {
    std::size_t selected = voices_.size();
    for (std::size_t i = 0; i < voices_.size(); ++i)
        if (voices_[i].stage == EnvelopeStage::idle) {
            selected = i;
            break;
        }
    if (selected == voices_.size()) {
        selected = 0;
        for (std::size_t i = 1; i < voices_.size(); ++i)
            if (voices_[i].age < voices_[selected].age)
                selected = i;
        ++stolen_;
    }
    voices_[selected] = {0,
                         0,
                         0,
                         0,
                         std::clamp(velocity, 0.0F, 1.0F),
                         age_++,
                         owner,
                         note,
                         EnvelopeStage::attack};
    auto &voice = voices_[selected];
    for (std::size_t source = 0; source < 4; ++source)
        if (voice_modulation_.sources[source].retrigger)
            voice.modulation_phase[source] = 0;
        else {
            const auto &settings = voice_modulation_.sources[source];
            const auto frequency = settings.tempo_sync
                                       ? tempo_bpm_ /
                                             (60.0F * std::max(
                                                           0.03125F,
                                                           settings.beats_per_cycle))
                                       : settings.rate_hz;
            voice.modulation_phase[source] = std::fmod(
                static_cast<double>(modulation_frame_) *
                    std::max(0.0F, frequency) / rate_,
                1.0);
        }
    voice.modulation_envelope = -1.0F;
    voice.modulation_stage = EnvelopeStage::attack;
    const auto identity = owner.event != 0
                              ? owner.track ^ owner.event ^ owner.occurrence
                              : age_;
    for (std::size_t source = 0; source < 4; ++source) {
        voice.modulation_random[source] =
            voice_modulation_.sources[source].seed ^
            static_cast<std::uint32_t>(identity) ^
            static_cast<std::uint32_t>(identity >> 32U) ^
            (static_cast<std::uint32_t>(note) << 16U);
        if (voice.modulation_random[source] == 0)
            voice.modulation_random[source] = 1;
    }
}
void SubtractiveSynth::note_off(std::uint8_t note,
                                NoteIdentity owner) noexcept {
    for (auto &voice : voices_)
        if (voice.stage != EnvelopeStage::idle && voice.note == note &&
            (owner.event == 0 || voice.owner == owner)) {
            voice.stage = EnvelopeStage::release;
            voice.modulation_stage = EnvelopeStage::release;
            if (owner.event != 0)
                return;
        }
}
bool SubtractiveSynth::set_parameter(std::uint32_t raw, float value) noexcept {
    if (!std::isfinite(value))
        return false;
    switch (static_cast<SynthParameter>(raw)) {
    case SynthParameter::waveform:
        waveform_.store(std::clamp(value, 0.0F, 3.0F));
        break;
    case SynthParameter::attack:
        attack_.store(std::clamp(value, 0.001F, 5.0F));
        break;
    case SynthParameter::decay:
        decay_.store(std::clamp(value, 0.001F, 5.0F));
        break;
    case SynthParameter::sustain:
        sustain_.store(std::clamp(value, 0.0F, 1.0F));
        break;
    case SynthParameter::release:
        release_.store(std::clamp(value, 0.001F, 10.0F));
        break;
    case SynthParameter::cutoff:
        cutoff_.store(std::clamp(value, 20.0F, 20000.0F));
        break;
    case SynthParameter::resonance:
        resonance_.store(std::clamp(value, 0.0F, 0.95F));
        break;
    case SynthParameter::pulse_width:
        pulse_width_.store(std::clamp(value, 0.05F, 0.95F));
        break;
    case SynthParameter::velocity_sensitivity:
        velocity_.store(std::clamp(value, 0.0F, 1.0F));
        break;
    case SynthParameter::gain:
        gain_.store(std::clamp(value, 0.0F, 1.0F));
        break;
    case SynthParameter::filter_enabled:
        filter_enabled_.store(value >= 0.5F);
        break;
    case SynthParameter::pitch:
        pitch_.store(std::clamp(value, -24.0F, 24.0F));
        break;
    default:
        return false;
    }
    return true;
}
float SubtractiveSynth::smooth(std::atomic<float> &target,
                               float &current) noexcept {
    current += (target.load(std::memory_order_relaxed) - current) * 0.002F;
    return current;
}
float SubtractiveSynth::oscillator(Voice &voice) noexcept {
    const auto pitch = voice_parameter(voice, SynthParameter::pitch,
                                       smooth(pitch_, pitch_current_), -24, 24);
    const float frequency = midi_frequency(voice.note) * std::exp2(pitch / 12.0F),
                increment = std::min(0.49F, frequency / rate_);
    float phase = static_cast<float>(voice.phase), out{};
    const auto wave = static_cast<OscillatorWaveform>(
        std::clamp<int>(static_cast<int>(std::lround(waveform_.load())), 0, 3));
    if (wave == OscillatorWaveform::sine)
        out = std::sin(2 * std::numbers::pi_v<float> * phase);
    else if (wave == OscillatorWaveform::triangle)
        out = 4 * std::abs(phase - 0.5F) - 1;
    else if (wave == OscillatorWaveform::saw)
        out = 2 * phase - 1 - poly_blep(phase, increment);
    else {
        const auto width = smooth(pulse_width_, pulse_current_);
        out = phase < width ? 1.0F : -1.0F;
        out += poly_blep(phase, increment) -
               poly_blep(std::fmod(phase - width + 1, 1.0F), increment);
    }
    voice.phase += increment;
    if (voice.phase >= 1)
        voice.phase -= 1;
    return out;
}
bool SubtractiveSynth::configure_voice_modulation(
    const ModulationConfiguration &configuration,
    std::uint32_t instance) noexcept {
    voice_modulation_ = {};
    voice_modulation_.update_interval = configuration.update_interval;
    voice_modulation_.sources = configuration.sources;
    instance_id_ = instance;
    for (std::size_t i = 0; i < configuration.route_count; ++i) {
        const auto &route = configuration.routes[i];
        if (route.scope != ModulationScope::voice)
            continue;
        if (route.destination.target != ParameterTarget::subtractive_synth ||
            route.destination.instance != instance ||
            (route.destination.parameter !=
                 static_cast<std::uint32_t>(SynthParameter::pitch) &&
             route.destination.parameter !=
                 static_cast<std::uint32_t>(SynthParameter::cutoff) &&
             route.destination.parameter !=
                 static_cast<std::uint32_t>(SynthParameter::gain)))
            return false;
        if (voice_modulation_.route_count ==
            voice_modulation_.routes.size())
            return false;
        voice_modulation_.routes[voice_modulation_.route_count++] = route;
    }
    return true;
}
float SubtractiveSynth::voice_source(Voice &voice,
                                     ModulationSourceId source) noexcept {
    if (source == ModulationSourceId::velocity)
        return voice.velocity * 2.0F - 1.0F;
    if (source == ModulationSourceId::envelope)
        return voice.modulation_envelope;
    const auto index = static_cast<std::size_t>(source);
    if (index < 4) {
        const auto phase = voice.modulation_phase[index];
        switch (voice_modulation_.sources[index].waveform) {
        case LfoWaveform::sine:
            return std::sin(static_cast<float>(phase) * 2.0F *
                            std::numbers::pi_v<float>);
        case LfoWaveform::triangle:
            return static_cast<float>(1.0 - 4.0 * std::abs(phase - 0.5));
        case LfoWaveform::saw:
            return static_cast<float>(phase * 2.0 - 1.0);
        case LfoWaveform::square:
            return phase < 0.5 ? 1.0F : -1.0F;
        case LfoWaveform::random:
            return static_cast<float>(voice.modulation_random[index]) /
                       static_cast<float>(
                           std::numeric_limits<std::uint32_t>::max()) *
                       2.0F -
                   1.0F;
        }
    }
    return 0;
}
void SubtractiveSynth::update_voice_modulation(Voice &voice) noexcept {
    if (voice.modulation_countdown != 0) {
        --voice.modulation_countdown;
        return;
    }
    const auto elapsed = std::max(1U, voice_modulation_.update_interval);
    voice.modulation_countdown = elapsed - 1;
    for (std::size_t source = 0; source < 4; ++source) {
        const auto &settings = voice_modulation_.sources[source];
        if (settings.scope != ModulationScope::voice)
            continue;
        const auto rate = settings.tempo_sync
                              ? tempo_bpm_ /
                                    (60.0F * std::max(0.03125F,
                                                      settings.beats_per_cycle))
                              : settings.rate_hz;
        const auto previous = voice.modulation_phase[source];
        voice.modulation_phase[source] = std::fmod(
            voice.modulation_phase[source] +
                static_cast<double>(elapsed) * std::max(0.0F, rate) / rate_,
            1.0);
        if (voice_modulation_.sources[source].waveform ==
                LfoWaveform::random &&
            voice.modulation_phase[source] < previous) {
            auto &random = voice.modulation_random[source];
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
        }
    }
    const auto &settings = voice_modulation_.sources[
        static_cast<std::size_t>(ModulationSourceId::envelope)];
    if (settings.scope != ModulationScope::voice)
        return;
    const auto samples = static_cast<float>(elapsed);
    if (voice.modulation_stage == EnvelopeStage::attack) {
        voice.modulation_envelope +=
            2.0F * samples /
            std::max(1.0F, settings.attack_seconds * rate_);
        if (voice.modulation_envelope >= 1) {
            voice.modulation_envelope = 1;
            voice.modulation_stage = EnvelopeStage::decay;
        }
    } else if (voice.modulation_stage == EnvelopeStage::decay) {
        const auto sustain = settings.sustain * 2.0F - 1.0F;
        voice.modulation_envelope -=
            2.0F * samples /
            std::max(1.0F, settings.decay_seconds * rate_);
        if (voice.modulation_envelope <= sustain) {
            voice.modulation_envelope = sustain;
            voice.modulation_stage = EnvelopeStage::sustain;
        }
    } else if (voice.modulation_stage == EnvelopeStage::sustain)
        voice.modulation_envelope = settings.sustain * 2.0F - 1.0F;
    else if (voice.modulation_stage == EnvelopeStage::release) {
        voice.modulation_envelope -=
            2.0F * samples /
            std::max(1.0F, settings.release_seconds * rate_);
        if (voice.modulation_envelope <= -1) {
            voice.modulation_envelope = -1;
            voice.modulation_stage = EnvelopeStage::idle;
        }
    }
}
float SubtractiveSynth::voice_parameter(Voice &voice, SynthParameter parameter,
                                        float base, float minimum,
                                        float maximum) noexcept {
    auto normalized = (base - minimum) / (maximum - minimum);
    for (std::size_t i = 0; i < voice_modulation_.route_count; ++i) {
        const auto &route = voice_modulation_.routes[i];
        if (route.enabled && route.destination.parameter ==
                                 static_cast<std::uint32_t>(parameter)) {
            const auto source = voice_source(voice, route.source);
            const auto shaped = route.polarity == ModulationPolarity::bipolar
                                    ? source
                                    : (source + 1.0F) * 0.5F;
            normalized += shaped * route.depth;
        }
    }
    return minimum + std::clamp(normalized, 0.0F, 1.0F) *
                         (maximum - minimum);
}
float SubtractiveSynth::envelope(Voice &voice) noexcept {
    const auto attack = std::max(0.001F, smooth(attack_, attack_current_)),
               decay = std::max(0.001F, smooth(decay_, decay_current_)),
               sustain = smooth(sustain_, sustain_current_),
               release = std::max(0.001F, smooth(release_, release_current_));
    if (voice.stage == EnvelopeStage::attack) {
        voice.envelope += 1 / (attack * rate_);
        if (voice.envelope >= 1) {
            voice.envelope = 1;
            voice.stage = EnvelopeStage::decay;
        }
    } else if (voice.stage == EnvelopeStage::decay) {
        voice.envelope -= (1 - sustain) / (decay * rate_);
        if (voice.envelope <= sustain) {
            voice.envelope = sustain;
            voice.stage = EnvelopeStage::sustain;
        }
    } else if (voice.stage == EnvelopeStage::sustain)
        voice.envelope = sustain;
    else if (voice.stage == EnvelopeStage::release) {
        voice.envelope -= 1 / (release * rate_);
        if (voice.envelope <= 0) {
            voice.envelope = 0;
            voice.stage = EnvelopeStage::idle;
        }
    }
    return voice.envelope;
}
float SubtractiveSynth::render_sample() noexcept {
    float output{};
    render_sample_tracks({}, {}, output);
    return output;
}
void SubtractiveSynth::render_sample_tracks(std::span<const std::uint64_t> tracks,
                                            std::span<float> outputs,
                                            float &unrouted) noexcept {
    std::fill(outputs.begin(), outputs.end(), 0.0F);
    unrouted = 0;
    const auto cutoff_base = std::clamp(smooth(cutoff_, cutoff_current_), 20.0F,
                                        rate_ * 0.45F),
               resonance = smooth(resonance_, resonance_current_),
               velocity = smooth(velocity_, velocity_current_),
               gain_base = smooth(gain_, gain_current_);
    for (auto &voice : voices_) {
        if (voice.stage == EnvelopeStage::idle)
            continue;
        update_voice_modulation(voice);
        const auto cutoff = voice_parameter(
            voice, SynthParameter::cutoff, cutoff_base, 20.0F,
            std::min(20000.0F, rate_ * 0.45F));
        const auto gain = voice_parameter(voice, SynthParameter::gain,
                                          gain_base, 0.0F, 1.0F);
        const auto coefficient =
            2 * std::sin(std::numbers::pi_v<float> * cutoff / rate_);
        float value = oscillator(voice) * envelope(voice) *
                      (1 - velocity + velocity * voice.velocity);
        if (filter_enabled_.load(std::memory_order_relaxed) >= 0.5F) {
            voice.low += coefficient * voice.band;
            const auto high = value - voice.low - (1 - resonance) * voice.band;
            voice.band += coefficient * high;
            value = voice.low;
            if (!std::isfinite(value)) {
                voice.low = voice.band = 0;
                value = 0;
            }
        }
        auto routed = false;
        for (std::size_t i = 0; i < std::min(tracks.size(), outputs.size()); ++i)
            if (voice.owner.track != 0 && voice.owner.track == tracks[i]) {
                outputs[i] += value * gain;
                routed = true;
                break;
            }
        if (!routed)
            unrouted += value * gain;
    }
    for (auto &value : outputs)
        value = std::clamp(value, -2.0F, 2.0F);
    unrouted = std::clamp(unrouted, -2.0F, 2.0F);
}
void SubtractiveSynth::process(std::span<float> left,
                               std::span<float> right) noexcept {
    const auto count = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = render_sample();
        left[i] += value;
        right[i] += value;
    }
}
void SubtractiveSynth::reset() noexcept {
    for (auto &voice : voices_)
        voice = Voice{};
    age_ = 0;
    modulation_frame_ = 0;
}
void SubtractiveSynth::release_timeline_voices(
    bool immediate, std::span<const std::uint64_t> tracks) noexcept {
    for (auto &voice : voices_)
        if (voice.owner.track != 0 &&
            (tracks.empty() || std::find(tracks.begin(), tracks.end(),
                                         voice.owner.track) != tracks.end())) {
            if (immediate)
                voice.stage = EnvelopeStage::idle;
            else {
                voice.stage = EnvelopeStage::release;
                voice.modulation_stage = EnvelopeStage::release;
            }
        }
}
void SubtractiveSynth::set_sample_rate(std::uint32_t rate) noexcept {
    rate_ = std::max(1U, rate);
    reset();
}
InstrumentState SubtractiveSynth::state() const noexcept {
    std::size_t active{};
    for (const auto &voice : voices_)
        active += voice.stage != EnvelopeStage::idle;
    return {active, voices_.size(), stolen_, 0};
}
bool SubtractiveSynth::active() const noexcept {
    for (const auto &voice : voices_)
        if (voice.stage != EnvelopeStage::idle)
            return true;
    return false;
}
} // namespace neurogenic
