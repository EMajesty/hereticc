#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
namespace neurogenic {
struct Sample { std::uint32_t sample_rate{}; std::uint16_t channels{}; std::vector<float> interleaved; };
Sample load_wav(const std::filesystem::path& path);
}
