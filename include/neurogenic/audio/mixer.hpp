#pragma once
#include "neurogenic/core/spsc_queue.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include "neurogenic/dsp/processor.hpp"
#include "neurogenic/timeline/model.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace neurogenic {

using MixerBusId = std::uint32_t;
inline constexpr std::size_t maximum_mixer_tracks = 32;
inline constexpr std::size_t maximum_mixer_inputs = 6;
inline constexpr std::size_t maximum_mixer_outputs = 6;
inline constexpr std::size_t mixer_aux_count = 2;
inline constexpr std::size_t mixer_external_fx_count = 2;
inline constexpr std::size_t maximum_external_fx_latency_frames = 16384;
inline constexpr std::size_t maximum_mixer_block = 1024;
using ExternalFxLoopId = std::uint32_t;

enum class ExternalFxSource : std::uint8_t { track, auxiliary, master };

struct MixerChannelSettings {
    TrackId track{};
    float gain{1.0F}, pan{};
    std::array<float, mixer_aux_count> aux_send{};
    std::array<bool, mixer_aux_count> aux_pre_fader{};
    std::int8_t output_pair{-1}; // -1 routes to master only.
    bool mute{}, solo{};
};
struct MixerInputSettings {
    std::uint8_t physical_channel{};
    float gain{1.0F}, pan{};
    std::array<float, mixer_aux_count> aux_send{};
    bool mute{}, route_to_master{true};
};
struct MixerAuxSettings {
    float gain{1.0F}, return_gain{1.0F};
    std::int8_t output_pair{-1};
    bool mute{}, effect_enabled{};
};
struct MixerMasterSettings {
    float gain{1.0F};
    std::uint8_t output_left{}, output_right{1};
    bool mute{};
};
struct ExternalFxLoopSettings {
    ExternalFxLoopId id{};
    std::array<char, 32> name{};
    ExternalFxSource source{ExternalFxSource::master};
    std::uint64_t source_id{}; // TrackId for tracks, zero-based index for aux.
    std::uint8_t send_left{}, send_right{1};
    std::uint8_t return_left{}, return_right{1};
    float send_gain{0.5F}, return_gain{1.0F}, wet{0.5F};
    std::uint32_t latency_frames{};
    bool enabled{}, bypass{}, return_mute{}, mono_send{}, mono_return{};
};
struct MixerConfiguration {
    std::uint64_t revision{};
    std::array<MixerChannelSettings, maximum_mixer_tracks> tracks{};
    std::size_t track_count{};
    std::array<MixerInputSettings, maximum_mixer_inputs> inputs{};
    std::size_t input_count{};
    std::array<MixerAuxSettings, mixer_aux_count> aux{};
    std::array<ExternalFxLoopSettings, mixer_external_fx_count> external_fx{};
    MixerMasterSettings master;
};
struct RoutingScene {
    std::uint64_t id{};
    std::string name;
    MixerConfiguration mixer;
};
struct StereoMeter {
    float left{}, right{};
    bool clip{};
};
struct MixerState {
    std::uint64_t revision{}, rejected_publications{};
    MixerConfiguration configuration;
    std::array<StereoMeter, maximum_mixer_tracks> tracks{};
    std::array<StereoMeter, maximum_mixer_inputs> inputs{};
    std::array<StereoMeter, mixer_aux_count> aux{};
    std::array<StereoMeter, mixer_external_fx_count> external_returns{};
    std::array<bool, mixer_external_fx_count> external_send_available{},
        external_return_available{};
    StereoMeter master;
};

[[nodiscard]] bool validate_mixer_configuration(const MixerConfiguration &,
                                                std::uint8_t input_channels,
                                                std::uint8_t output_channels,
                                                std::string &error) noexcept;

class Mixer {
  public:
    explicit Mixer(std::uint32_t sample_rate = 48000);
    [[nodiscard]] bool publish(const MixerConfiguration &,
                               std::string &error) noexcept;
    void clear_modulation() noexcept;
    [[nodiscard]] bool set_modulation(ParameterDestination, float) noexcept;
    void capture_modulation_frame(std::size_t frame) noexcept;
    void begin_block(std::size_t frames) noexcept;
    [[nodiscard]] bool add_track(TrackId, std::size_t frame, float left,
                                 float right) noexcept;
    void add_input(std::uint8_t physical_channel, std::size_t frame,
                   float value) noexcept;
    void process(std::span<float> master_left, std::span<float> master_right,
                 std::span<float> physical_interleaved = {},
                 std::uint8_t physical_channels = 0) noexcept;
    void apply_master_gain(std::span<float>, std::span<float>) noexcept;
    void route_master_output(std::span<const float>, std::span<const float>,
                             std::span<float> physical_interleaved = {},
                             std::uint8_t physical_channels = 0) noexcept;
    [[nodiscard]] MixerState state() const noexcept;
    [[nodiscard]] std::size_t active_track_count() const noexcept {
        return active_.track_count;
    }
    [[nodiscard]] TrackId active_track_id(std::size_t index) const noexcept {
        return index < active_.track_count ? active_.tracks[index].track : 0;
    }
    [[nodiscard]] std::uint8_t active_master_left() const noexcept {
        return active_.master.output_left;
    }
    [[nodiscard]] std::uint8_t active_master_right() const noexcept {
        return active_.master.output_right;
    }
    [[nodiscard]] std::span<const float> track_left(std::size_t) const noexcept;
    [[nodiscard]] std::span<const float>
    track_right(std::size_t) const noexcept;
    [[nodiscard]] std::span<const float> aux_left(std::size_t) const noexcept;
    [[nodiscard]] std::span<const float> aux_right(std::size_t) const noexcept;
    [[nodiscard]] std::span<const float>
    external_return_left(std::size_t) const noexcept;
    [[nodiscard]] std::span<const float>
    external_return_right(std::size_t) const noexcept;
    [[nodiscard]] std::span<const float>
    external_mix_left(std::size_t) const noexcept;
    [[nodiscard]] std::span<const float>
    external_mix_right(std::size_t) const noexcept;

  private:
    struct AtomicMeter {
        std::atomic<float> left{}, right{};
        std::atomic<bool> clip{};
    };
    [[nodiscard]] std::size_t find_track(TrackId) const noexcept;
    void update_meter(AtomicMeter &, float, float) noexcept;
    std::uint32_t sample_rate_{};
    SpscQueue<MixerConfiguration, 5> publications_;
    MixerConfiguration active_;
    mutable std::mutex control_mutex_;
    MixerConfiguration control_;
    std::atomic<std::uint64_t> revision_{}, rejected_{};
    std::size_t frames_{};
    std::array<std::array<float, maximum_mixer_block>, maximum_mixer_tracks>
        track_left_{}, track_right_{};
    std::array<std::array<float, maximum_mixer_block>, maximum_mixer_inputs>
        physical_input_{};
    std::array<std::array<float, maximum_mixer_block>, mixer_aux_count>
        aux_left_{}, aux_right_{};
    std::array<std::array<float, maximum_mixer_block>, mixer_external_fx_count>
        external_return_left_{}, external_return_right_{}, external_mix_left_{},
        external_mix_right_{};
    std::array<std::array<float, maximum_external_fx_latency_frames + 1>,
               mixer_external_fx_count>
        external_delay_left_{}, external_delay_right_{};
    std::array<std::size_t, mixer_external_fx_count> external_delay_cursor_{};
    std::array<std::size_t, mixer_external_fx_count> external_delay_valid_{};
    std::uint8_t physical_input_channels_{};
    std::array<AtomicMeter, maximum_mixer_tracks> track_meters_{};
    std::array<AtomicMeter, maximum_mixer_inputs> input_meters_{};
    std::array<AtomicMeter, mixer_aux_count> aux_meters_{};
    std::array<AtomicMeter, mixer_external_fx_count> external_meters_{};
    std::array<std::atomic<bool>, mixer_external_fx_count>
        external_send_available_{}, external_return_available_{};
    AtomicMeter master_meter_;
    std::array<StereoDelay, mixer_aux_count> aux_effects_;
    struct NumericModulation {
        std::array<float, maximum_mixer_tracks> track_gain{}, track_pan{};
        std::array<std::array<float, mixer_aux_count>, maximum_mixer_tracks>
            track_send{};
        std::array<float, mixer_aux_count> aux_return{};
        std::array<float, mixer_external_fx_count> fx_wet{}, fx_send{},
            fx_return{};
        float master_gain{};
        std::array<bool, maximum_mixer_tracks> has_track_gain{},
            has_track_pan{};
        std::array<std::array<bool, mixer_aux_count>, maximum_mixer_tracks>
            has_track_send{};
        std::array<bool, mixer_aux_count> has_aux_return{};
        std::array<bool, mixer_external_fx_count> has_fx_wet{}, has_fx_send{},
            has_fx_return{};
        bool has_master_gain{};
    } modulation_;
    struct ModulationLanes {
        std::array<std::array<float, maximum_mixer_block>, maximum_mixer_tracks>
            track_gain{}, track_pan{};
        std::array<
            std::array<std::array<float, maximum_mixer_block>, mixer_aux_count>,
            maximum_mixer_tracks>
            track_send{};
        std::array<std::array<float, maximum_mixer_block>, mixer_aux_count>
            aux_return{};
        std::array<std::array<float, maximum_mixer_block>,
                   mixer_external_fx_count>
            fx_wet{}, fx_send{}, fx_return{};
        std::array<float, maximum_mixer_block> master_gain{};
    };
    std::unique_ptr<ModulationLanes> modulation_lanes_;
    std::array<float, mixer_external_fx_count> smoothed_fx_wet_{},
        smoothed_fx_send_{}, smoothed_fx_return_{};
};
} // namespace neurogenic
