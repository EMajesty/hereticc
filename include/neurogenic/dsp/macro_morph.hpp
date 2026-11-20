#pragma once
#include "neurogenic/dsp/modulation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace neurogenic {
enum class MacroMorphCurve : std::uint8_t {
    linear,
    exponential,
    logarithmic,
    s_curve
};
enum class MacroMorphDurationUnit : std::uint8_t { seconds, beats };
enum class MacroMorphCommandType : std::uint8_t { start, stop, stop_all };

struct MacroMorphCommand {
    MacroMorphCommandType type{MacroMorphCommandType::start};
    std::uint64_t morph_id{};
    ModulatorInstanceId instance{};
    std::uint64_t start_frame{};
    float start_value{}, target_value{};
    double duration{};
    MacroMorphDurationUnit duration_unit{MacroMorphDurationUnit::seconds};
    MacroMorphCurve curve{MacroMorphCurve::linear};
};

struct MacroMorphRuntime {
    std::uint64_t morph_id{};
    ModulatorInstanceId instance{};
    std::uint64_t start_frame{}, end_frame{};
    float start_value{}, target_value{}, effective_value{}, progress{};
    MacroMorphDurationUnit duration_unit{MacroMorphDurationUnit::seconds};
    MacroMorphCurve curve{MacroMorphCurve::linear};
    bool active{}, scheduled{}, interrupted{}, completed{};
};

class MacroMorphProcessor {
  public:
    static constexpr std::size_t capacity = 32;
    static constexpr std::uint32_t control_interval = 32;

    bool command(const MacroMorphCommand &value, std::uint32_t sample_rate,
                 double tempo_bpm) noexcept {
        if (value.type == MacroMorphCommandType::stop_all) {
            for (auto &morph : morphs_)
                if (morph.active || morph.scheduled) {
                    morph.active = morph.scheduled = false;
                    morph.interrupted = true;
                }
            return true;
        }
        if (!value.instance)
            return false;
        if (value.type == MacroMorphCommandType::stop) {
            stop(value.instance);
            return true;
        }
        if (!value.morph_id || !sample_rate || !std::isfinite(value.start_value) ||
            !std::isfinite(value.target_value) || !std::isfinite(value.duration) ||
            value.duration < 0 || value.duration > 3600 ||
            !std::isfinite(tempo_bpm) || tempo_bpm <= 0)
            return false;
        auto *slot = find(value.instance);
        if (!slot) {
            const auto available =
                std::find_if(morphs_.begin(), morphs_.end(),
                             [](const auto &morph) {
                                 return !morph.active && !morph.scheduled;
                             });
            if (available == morphs_.end())
                return false;
            slot = &*available;
        }
        if (slot->active || slot->scheduled)
            slot->interrupted = true;
        *slot = {};
        slot->morph_id = value.morph_id;
        slot->instance = value.instance;
        slot->start_frame = value.start_frame;
        slot->start_value = std::clamp(value.start_value, 0.0F, 1.0F);
        slot->target_value = std::clamp(value.target_value, 0.0F, 1.0F);
        slot->effective_value = slot->start_value;
        slot->duration_unit = value.duration_unit;
        slot->curve = value.curve;
        slot->duration_amount = value.duration;
        slot->duration_frames = static_cast<std::uint64_t>(std::llround(
            value.duration * sample_rate *
            (value.duration_unit == MacroMorphDurationUnit::beats
                 ? 60.0 / tempo_bpm
                 : 1.0)));
        slot->end_frame = slot->duration_frames >
                                  std::numeric_limits<std::uint64_t>::max() -
                                      slot->start_frame
                              ? std::numeric_limits<std::uint64_t>::max()
                              : slot->start_frame + slot->duration_frames;
        slot->scheduled = true;
        return true;
    }

    void stop(ModulatorInstanceId instance) noexcept {
        if (auto *slot = find(instance); slot &&
                                         (slot->active || slot->scheduled)) {
            slot->active = slot->scheduled = false;
            slot->interrupted = true;
        }
    }
    void override_value(ModulatorInstanceId instance, float value) noexcept {
        if (auto *slot = find(instance)) {
            slot->active = slot->scheduled = false;
            slot->interrupted = true;
            slot->effective_value = std::clamp(value, 0.0F, 1.0F);
            slot->target_value = slot->effective_value;
        }
    }

    template <class Apply>
    void process(std::uint64_t frame, bool transport_running,
                 std::uint32_t sample_rate, double tempo_bpm,
                 Apply &&apply) noexcept {
        if (!sample_rate || !std::isfinite(tempo_bpm) || tempo_bpm <= 0)
            return;
        for (auto &morph : morphs_) {
            if (morph.scheduled && frame >= morph.start_frame) {
                morph.scheduled = false;
                morph.active = true;
                morph.last_frame = morph.start_frame;
                morph.next_update_frame = morph.start_frame;
                if (morph.duration_amount == 0) {
                    morph.progress = 1;
                    morph.effective_value = morph.target_value;
                    apply(morph.instance, morph.effective_value);
                    morph.active = false;
                    morph.completed = true;
                    continue;
                }
            }
            if (!transport_running)
                continue;
            if (morph.active && frame < morph.last_frame) {
                morph.last_frame = frame;
                morph.next_update_frame = frame;
            }
            if (!morph.active || frame < morph.next_update_frame)
                continue;
            const auto delta = frame >= morph.last_frame
                                   ? frame - morph.last_frame
                                   : std::uint64_t{};
            morph.last_frame = frame;
            morph.next_update_frame = frame + control_interval;
            if (morph.duration_unit == MacroMorphDurationUnit::seconds)
                morph.elapsed += static_cast<double>(delta) / sample_rate;
            else
                morph.elapsed += static_cast<double>(delta) * tempo_bpm /
                                 (60.0 * sample_rate);
            morph.progress = static_cast<float>(std::clamp(
                morph.elapsed / morph.duration_amount, 0.0, 1.0));
            if (morph.duration_unit == MacroMorphDurationUnit::beats) {
                const auto remaining =
                    std::max(0.0, morph.duration_amount - morph.elapsed);
                const auto frames = static_cast<std::uint64_t>(std::llround(
                    remaining * 60.0 * sample_rate / tempo_bpm));
                morph.end_frame = frames >
                                          std::numeric_limits<std::uint64_t>::max() -
                                              frame
                                      ? std::numeric_limits<std::uint64_t>::max()
                                      : frame + frames;
            }
            const auto shaped = shape(morph.curve, morph.progress);
            morph.effective_value =
                std::lerp(morph.start_value, morph.target_value, shaped);
            apply(morph.instance, morph.effective_value);
            if (morph.progress >= 1) {
                morph.active = false;
                morph.completed = true;
            }
        }
    }

    [[nodiscard]] const auto &morphs() const noexcept { return morphs_; }

  private:
    struct State : MacroMorphRuntime {
        double duration_amount{}, elapsed{};
        std::uint64_t duration_frames{}, last_frame{}, next_update_frame{};
    };
    State *find(ModulatorInstanceId instance) noexcept {
        const auto found = std::find_if(morphs_.begin(), morphs_.end(),
                                        [=](const auto &morph) {
                                            return morph.instance == instance &&
                                                   (morph.active ||
                                                    morph.scheduled);
                                        });
        return found == morphs_.end() ? nullptr : &*found;
    }
    static float shape(MacroMorphCurve curve, float value) noexcept {
        value = std::clamp(value, 0.0F, 1.0F);
        switch (curve) {
        case MacroMorphCurve::exponential: return value * value;
        case MacroMorphCurve::logarithmic: return std::sqrt(value);
        case MacroMorphCurve::s_curve:
            return value * value * (3.0F - 2.0F * value);
        case MacroMorphCurve::linear: return value;
        }
        return value;
    }
    std::array<State, capacity> morphs_{};
};

enum class ParameterSmoothingMode : std::uint8_t {
    bypass,
    linear,
    exponential
};
class ParameterSmoother {
  public:
    void reset(float value) noexcept { current_ = target_ = value; }
    void set_target(float value, float seconds, std::uint32_t sample_rate,
                    ParameterSmoothingMode mode) noexcept {
        target_ = value;
        mode_ = seconds <= 0 ? ParameterSmoothingMode::bypass : mode;
        remaining_ = mode_ == ParameterSmoothingMode::linear
                         ? std::max<std::uint32_t>(
                               1, static_cast<std::uint32_t>(std::llround(
                                      seconds * sample_rate)))
                         : 0;
        linear_step_ = remaining_ ? (target_ - current_) / remaining_ : 0;
        exponential_coefficient_ =
            mode_ == ParameterSmoothingMode::exponential && sample_rate
                ? std::exp(-1.0F /
                           std::max(1.0F, seconds * sample_rate))
                : 0;
        if (mode_ == ParameterSmoothingMode::bypass)
            current_ = target_;
    }
    float process() noexcept {
        if (mode_ == ParameterSmoothingMode::linear && remaining_) {
            current_ += linear_step_;
            if (!--remaining_)
                current_ = target_;
        } else if (mode_ == ParameterSmoothingMode::exponential) {
            current_ = target_ +
                       (current_ - target_) * exponential_coefficient_;
            if (std::fabs(current_ - target_) < 1.0e-6F)
                current_ = target_;
        }
        return current_;
    }
    [[nodiscard]] float value() const noexcept { return current_; }

  private:
    ParameterSmoothingMode mode_{ParameterSmoothingMode::bypass};
    float current_{}, target_{}, linear_step_{}, exponential_coefficient_{};
    std::uint32_t remaining_{};
};
} // namespace neurogenic
