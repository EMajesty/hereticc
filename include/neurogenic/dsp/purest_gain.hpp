#pragma once
#include "neurogenic/dsp/effect.hpp"
namespace neurogenic {
// Integration shape for Airwindows PurestGain. This independently written,
// click-free gain stage preserves the upstream effect's stable public identity.
class PurestGain final : public Effect { public: static constexpr auto id="airwindows.purest_gain.v1"; void set_gain(float value) noexcept; void process(std::span<float>,std::span<float>) noexcept override; private: float target_{0.8F}, current_{0.8F}; };
}
