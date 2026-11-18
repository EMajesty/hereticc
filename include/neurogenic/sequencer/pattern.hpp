#pragma once
#include <array>
#include <cstdint>

namespace neurogenic {
constexpr std::size_t kSteps = 16;
struct Step { bool enabled{}; std::uint8_t note{60}; float velocity{1.0F}; };
struct Pattern { std::array<Step, kSteps> steps{}; std::uint64_t revision{}; };
}
