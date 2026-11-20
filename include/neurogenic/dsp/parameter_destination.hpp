#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
namespace neurogenic {
enum class ParameterTarget : std::uint32_t {
    none,
    subtractive_synth,
    stereo_delay
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
