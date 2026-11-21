#pragma once
#include "neurogenic/audio/sample.hpp"
#include <cstdint>
#include <span>

namespace neurogenic {
enum class StretchMode : std::uint8_t { off, realtime };
enum class StretchQuality : std::uint8_t { economy, balanced, smooth };

struct TimeStretchConfiguration {
    StretchMode mode{StretchMode::off};
    StretchQuality quality{StretchQuality::balanced};
    float time_ratio{1.0F};
    float pitch_ratio{1.0F};
    std::uint32_t sample_rate{48000};
    bool loop_enabled{};
    bool reverse{};
    std::uint64_t loop_start_frame{};
    std::uint64_t loop_end_frame{};
    std::uint32_t loop_crossfade_frames{};
};

struct TimeStretchState {
    double output_frame{};
    double source_origin{};
    double output_origin{};
    std::uint32_t window_frames{512};
};

class RealtimeTimeStretcher {
  public:
    [[nodiscard]] static bool valid(const TimeStretchConfiguration &) noexcept;
    [[nodiscard]] static std::uint32_t window_frames(StretchQuality) noexcept;
    [[nodiscard]] static std::uint32_t
    latency_frames(const TimeStretchConfiguration &) noexcept;
    [[nodiscard]] static float
    resolve_time_ratio(float explicit_ratio, float source_beats,
                       float source_bpm, std::uint64_t source_frames,
                       std::uint32_t sample_rate, float project_bpm) noexcept;
    static void reset(TimeStretchState &,
                      const TimeStretchConfiguration &,
                      std::uint64_t source_start = 0,
                      std::uint64_t source_end = 0,
                      std::uint64_t output_offset = 0) noexcept;
    static void set_time_ratio(TimeStretchState &, TimeStretchConfiguration &,
                               float) noexcept;
    [[nodiscard]] static double
    source_position(const TimeStretchState &,
                    const TimeStretchConfiguration &) noexcept;
    [[nodiscard]] static double
    playback_position(const TimeStretchState &,
                      const TimeStretchConfiguration &, std::uint64_t start,
                      std::uint64_t end) noexcept;
    [[nodiscard]] static float
    process_channel(const Sample &, std::uint64_t start, std::uint64_t end,
                    const TimeStretchConfiguration &, const TimeStretchState &,
                    std::uint16_t channel) noexcept;
    static void advance(TimeStretchState &) noexcept;
    static std::size_t
    process_block(const Sample &, std::uint64_t start, std::uint64_t end,
                  const TimeStretchConfiguration &, TimeStretchState &,
                  std::span<float> left, std::span<float> right,
                  std::uint64_t maximum_output_frames) noexcept;
};
} // namespace neurogenic
