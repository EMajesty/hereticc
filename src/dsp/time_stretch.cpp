#include "neurogenic/dsp/time_stretch.hpp"
#include <algorithm>
#include <cmath>

namespace neurogenic {
namespace {
float read_linear(const Sample &sample, double frame, std::uint64_t start,
                  std::uint64_t end, std::uint16_t channel) noexcept {
    if (frame < static_cast<double>(start) || frame >= static_cast<double>(end))
        return 0;
    const auto base = static_cast<std::uint64_t>(frame);
    const auto next = std::min(base + 1, end - 1);
    const auto fraction = static_cast<float>(frame - base);
    const auto actual = sample.channels > 1 ? channel : 0;
    const auto a = sample.interleaved[base * sample.channels + actual];
    const auto b = sample.interleaved[next * sample.channels + actual];
    return a + (b - a) * fraction;
}
float window(double local, double size, StretchQuality quality) noexcept {
    if (local < 0 || local >= size)
        return 0;
    const auto phase = static_cast<float>(local / size);
    const auto triangle = 1.0F - std::abs(phase * 2.0F - 1.0F);
    if (quality == StretchQuality::economy)
        return triangle;
    if (quality == StretchQuality::balanced)
        return triangle * triangle * (3.0F - 2.0F * triangle);
    constexpr double pi = 3.14159265358979323846;
    const auto sine = std::sin(pi * local / size);
    return static_cast<float>(sine * sine);
}
} // namespace

bool RealtimeTimeStretcher::valid(
    const TimeStretchConfiguration &value) noexcept {
    return value.sample_rate > 0 && std::isfinite(value.time_ratio) &&
           value.time_ratio >= 0.25F && value.time_ratio <= 4.0F &&
           std::isfinite(value.pitch_ratio) && value.pitch_ratio >= 0.25F &&
           value.pitch_ratio <= 4.0F;
}
std::uint32_t
RealtimeTimeStretcher::window_frames(StretchQuality quality) noexcept {
    switch (quality) {
    case StretchQuality::economy:
        return 256;
    case StretchQuality::smooth:
        return 1024;
    default:
        return 512;
    }
}
std::uint32_t RealtimeTimeStretcher::latency_frames(
    const TimeStretchConfiguration &value) noexcept {
    return value.mode == StretchMode::realtime
               ? window_frames(value.quality) / 2
               : 0;
}
float RealtimeTimeStretcher::resolve_time_ratio(float explicit_ratio,
                                                float source_beats,
                                                float source_bpm,
                                                std::uint64_t source_frames,
                                                std::uint32_t sample_rate,
                                                float project_bpm) noexcept {
    if (sample_rate == 0 || project_bpm <= 0)
        return explicit_ratio;
    const auto source_seconds =
        static_cast<double>(source_frames) / static_cast<double>(sample_rate);
    if (source_beats > 0 && source_seconds > 0)
        return static_cast<float>(source_beats * 60.0 / project_bpm /
                                  source_seconds);
    if (source_bpm > 0)
        return source_bpm / project_bpm;
    return explicit_ratio;
}
void RealtimeTimeStretcher::reset(
    TimeStretchState &state,
    const TimeStretchConfiguration &configuration) noexcept {
    state = {};
    state.window_frames = window_frames(configuration.quality);
}
float RealtimeTimeStretcher::process_channel(
    const Sample &sample, std::uint64_t start, std::uint64_t end,
    const TimeStretchConfiguration &configuration,
    const TimeStretchState &state, std::uint16_t channel) noexcept {
    const auto size = static_cast<double>(state.window_frames);
    const auto hop = size * 0.5;
    const auto grain = static_cast<std::int64_t>(state.output_frame / hop);
    float sum{}, weights{};
    for (std::int64_t offset = -1; offset <= 0; ++offset) {
        const auto index = grain + offset;
        if (index < 0)
            continue;
        const auto output_start = static_cast<double>(index) * hop;
        const auto local = state.output_frame - output_start;
        const auto weight = window(local, size, configuration.quality);
        if (weight == 0)
            continue;
        const auto source_anchor = static_cast<double>(start) +
                                   output_start / configuration.time_ratio;
        const auto source = source_anchor + local * configuration.pitch_ratio;
        if (source < static_cast<double>(start) ||
            source >= static_cast<double>(end))
            continue;
        sum += read_linear(sample, source, start, end, channel) * weight;
        weights += weight;
    }
    if (weights > 1.0e-9F)
        return sum / weights;
    const auto fallback = static_cast<double>(start) +
                          state.output_frame / configuration.time_ratio;
    return read_linear(sample, fallback, start, end, channel);
}
void RealtimeTimeStretcher::advance(TimeStretchState &state) noexcept {
    state.output_frame += 1.0;
}
std::size_t RealtimeTimeStretcher::process_block(
    const Sample &sample, std::uint64_t start, std::uint64_t end,
    const TimeStretchConfiguration &configuration, TimeStretchState &state,
    std::span<float> left, std::span<float> right,
    std::uint64_t maximum_output_frames) noexcept {
    const auto count = std::min(left.size(), right.size());
    std::size_t produced{};
    while (produced < count &&
           state.output_frame < static_cast<double>(maximum_output_frames)) {
        left[produced] =
            process_channel(sample, start, end, configuration, state, 0);
        right[produced] =
            process_channel(sample, start, end, configuration, state, 1);
        advance(state);
        ++produced;
    }
    return produced;
}
} // namespace neurogenic
