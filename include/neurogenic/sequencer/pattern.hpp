#pragma once
#include <array>
#include <cstdint>

namespace neurogenic {
constexpr std::size_t kSteps = 16;
struct Step { bool enabled{}; std::uint8_t note{60}; float velocity{1.0F}; };
struct Pattern { std::array<Step, kSteps> steps{}; std::uint64_t revision{}; };
constexpr bool valid_pattern(const Pattern& pattern) noexcept { for(const auto& step:pattern.steps) if(step.note>127||!(step.velocity>=0.0F&&step.velocity<=1.0F))return false; return true; }
}
