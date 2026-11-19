#include "neurogenic/instrument/subtractive_synth.hpp"
#include <algorithm>
#include <cmath>
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
}
void SubtractiveSynth::note_off(std::uint8_t note,
                                NoteIdentity owner) noexcept {
    for (auto &voice : voices_)
        if (voice.stage != EnvelopeStage::idle && voice.note == note &&
            (owner.event == 0 || voice.owner == owner)) {
            voice.stage = EnvelopeStage::release;
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
    const float frequency = midi_frequency(voice.note),
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
    const auto cutoff = std::clamp(smooth(cutoff_, cutoff_current_), 20.0F,
                                   rate_ * 0.45F),
               resonance = smooth(resonance_, resonance_current_),
               velocity = smooth(velocity_, velocity_current_),
               gain = smooth(gain_, gain_current_);
    const auto coefficient =
        2 * std::sin(std::numbers::pi_v<float> * cutoff / rate_);
    for (auto &voice : voices_) {
        if (voice.stage == EnvelopeStage::idle)
            continue;
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
        output += value;
    }
    return std::clamp(output * gain, -2.0F, 2.0F);
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
} // namespace neurogenic
