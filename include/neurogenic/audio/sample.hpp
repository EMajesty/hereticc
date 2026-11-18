#pragma once
#include <cstdint>
#include <filesystem>
#include <limits>
#include <vector>
namespace neurogenic {
struct SampleSlotId {
  std::uint32_t value{};
  friend constexpr bool operator==(SampleSlotId,SampleSlotId)=default;
};
inline constexpr auto invalid_sample_slot=SampleSlotId{std::numeric_limits<std::uint32_t>::max()};
struct Sample { std::uint32_t sample_rate{}; std::uint16_t channels{}; std::vector<float> interleaved; };
Sample load_wav(const std::filesystem::path& path);
}
