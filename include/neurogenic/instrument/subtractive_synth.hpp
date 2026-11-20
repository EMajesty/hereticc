#pragma once
#include "neurogenic/instrument/instrument.hpp"
#include "neurogenic/dsp/modulation.hpp"
#include <array>
#include <atomic>
#include <vector>
namespace neurogenic {
enum class OscillatorWaveform : std::uint8_t { sine, triangle, saw, pulse };
enum class SynthParameter : std::uint32_t {
    waveform = 1,
    attack,
    decay,
    sustain,
    release,
    cutoff,
    resonance,
    pulse_width,
    velocity_sensitivity,
    gain,
    filter_enabled,
    pitch
};
class SubtractiveSynth final : public Instrument {
  public:
    explicit SubtractiveSynth(std::uint32_t sample_rate = 48000,
                              std::size_t voices = 16);
    std::string_view id() const noexcept override {
        return "synth.subtractive.v1";
    }
    void note_on(std::uint8_t, float, NoteIdentity = {}) noexcept override;
    void note_off(std::uint8_t, NoteIdentity = {}) noexcept override;
    bool set_parameter(std::uint32_t, float) noexcept override;
    void process(std::span<float>, std::span<float>) noexcept override;
    void reset() noexcept override;
    void set_sample_rate(std::uint32_t) noexcept override;
    InstrumentState state() const noexcept override;
    [[nodiscard]] bool active() const noexcept;
    bool configure_voice_modulation(const ModulationConfiguration &,
                                    std::uint32_t instance) noexcept;
    void set_tempo(float bpm) noexcept { tempo_bpm_ = bpm; }
    void set_modulation_frame(std::uint64_t frame) noexcept {
        modulation_frame_ = frame;
    }
    float render_sample() noexcept;
    void render_sample_tracks(std::span<const std::uint64_t> tracks,
                              std::span<float> outputs,
                              float &unrouted) noexcept;

  private:
    enum class EnvelopeStage : std::uint8_t {
        idle,
        attack,
        decay,
        sustain,
        release
    };
    struct Voice {
        double phase{};
        float envelope{}, low{}, band{}, velocity{};
        std::uint64_t age{};
        NoteIdentity owner{};
        std::uint8_t note{};
        EnvelopeStage stage{EnvelopeStage::idle};
        std::array<double, 4> modulation_phase{};
        float modulation_envelope{-1.0F};
        EnvelopeStage modulation_stage{EnvelopeStage::idle};
        std::array<std::uint32_t, 4> modulation_random{1, 1, 1, 1};
        std::uint32_t modulation_countdown{};
    };
    float oscillator(Voice &) noexcept;
    float envelope(Voice &) noexcept;
    float smooth(std::atomic<float> &target, float &current) noexcept;
    void update_voice_modulation(Voice &) noexcept;
    float voice_source(Voice &, ModulationSourceId) noexcept;
    float voice_parameter(Voice &, SynthParameter, float base, float minimum,
                          float maximum) noexcept;
    std::uint32_t rate_;
    std::vector<Voice> voices_;
    std::atomic<float> waveform_{2}, attack_{0.005F}, decay_{0.12F},
        sustain_{0.75F}, release_{0.25F}, cutoff_{8000}, resonance_{0.15F},
        pulse_width_{0.5F}, velocity_{1}, gain_{0.25F}, filter_enabled_{1},
        pitch_{};
    float attack_current_{0.005F}, decay_current_{0.12F},
        sustain_current_{0.75F}, release_current_{0.25F}, cutoff_current_{8000},
        resonance_current_{0.15F}, pulse_current_{0.5F}, velocity_current_{1},
        gain_current_{0.25F}, pitch_current_{};
    std::uint64_t age_{}, stolen_{};
    ModulationConfiguration voice_modulation_{};
    std::uint32_t instance_id_{1};
    float tempo_bpm_{120.0F};
    std::uint64_t modulation_frame_{};
};
} // namespace neurogenic
