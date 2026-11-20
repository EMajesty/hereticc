#pragma once
#include "neurogenic/core/spsc_queue.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace neurogenic {
using MidiPortId = std::uint32_t;
inline constexpr std::size_t maximum_midi_ports = 8;
inline constexpr std::size_t maximum_midi_routes = 32;

struct MidiPacket {
    MidiPortId port{};
    std::array<std::uint8_t, 3> bytes{};
    std::uint8_t size{};
    std::uint64_t timestamp_ns{}, frame{};
};
enum class MidiClockSource : std::uint8_t { internal, external };

struct MidiRoute {
    std::uint32_t id{};
    MidiPortId input_port{}, output_port{};
    std::int8_t input_channel{-1}, output_channel{-1};
    bool notes{true}, control_change{true}, program_change{true},
        pitch_bend{true};
    bool clock{}, transport{}, enabled{true};
};

[[nodiscard]] bool validate_midi_routes(std::span<const MidiRoute>,
                                        std::string &error) noexcept;

class MidiOutputScheduler {
  public:
    using Sender = std::function<bool(const MidiPacket &)>;
    bool enqueue(const MidiPacket &) noexcept;
    std::size_t dispatch(Sender);
    void panic(MidiPortId port = 0) noexcept;
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load();
    }
    [[nodiscard]] std::uint64_t sent() const noexcept { return sent_.load(); }
    [[nodiscard]] std::size_t queued() const noexcept { return queued_.load(); }

  private:
    SpscQueue<MidiPacket, 1025> queue_;
    std::atomic<std::uint64_t> dropped_{}, sent_{};
    std::atomic<std::size_t> queued_{};
};

class MidiRouter {
  public:
    bool configure(std::span<const MidiRoute>, std::string &error) noexcept;
    void configure_realtime(std::span<const MidiRoute>) noexcept;
    bool route(const MidiPacket &, MidiOutputScheduler &) noexcept;
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load();
    }

  private:
    std::array<MidiRoute, maximum_midi_routes> routes_{};
    std::size_t route_count_{};
    std::atomic<std::uint64_t> dropped_{};
};

class MidiClockGenerator {
  public:
    MidiClockGenerator(
        std::uint32_t sample_rate = 48000,
        std::uint32_t microseconds_per_quarter = 500000) noexcept;
    void set_tempo(std::uint32_t microseconds_per_quarter) noexcept;
    void start(MidiOutputScheduler &, std::span<const MidiPortId>) noexcept;
    void stop(MidiOutputScheduler &, std::span<const MidiPortId>) noexcept;
    void continue_playback(MidiOutputScheduler &,
                           std::span<const MidiPortId>) noexcept;
    void process(std::uint32_t frames, std::uint64_t absolute_frame,
                 MidiOutputScheduler &, std::span<const MidiPortId>) noexcept;
    [[nodiscard]] std::uint64_t pulses() const noexcept { return pulses_; }

  private:
    void transport(std::uint8_t, MidiOutputScheduler &,
                   std::span<const MidiPortId>) noexcept;
    std::uint32_t sample_rate_{}, microseconds_per_quarter_{};
    std::uint64_t phase_{}, denominator_{}, pulses_{};
    bool running_{};
};

struct ExternalClockState {
    bool running{}, available{}, dropout{};
    double tempo_bpm{};
    std::uint64_t pulses{}, last_timestamp_ns{};
};
class MidiClockReceiver {
  public:
    void receive(const MidiPacket &) noexcept;
    void update(std::uint64_t now_ns) noexcept;
    [[nodiscard]] ExternalClockState state() const noexcept { return state_; }

  private:
    ExternalClockState state_{};
    std::array<std::uint64_t, 5> intervals_{};
    std::size_t interval_count_{}, interval_cursor_{};
};
struct MidiProjectConfiguration {
    MidiClockSource clock_source{MidiClockSource::internal};
    std::array<MidiRoute, maximum_midi_routes> routes{};
    std::size_t route_count{};
    std::array<MidiPortId, maximum_midi_ports> clock_outputs{};
    std::size_t clock_output_count{};
    std::array<std::int32_t, maximum_midi_ports> output_latency_frames{};
    std::array<std::array<char, 64>, maximum_midi_ports> input_selectors{},
        output_selectors{};
};
struct MidiClockOutputConfiguration {
    bool enabled{};
    std::array<MidiPortId, maximum_midi_ports> ports{};
    std::size_t port_count{};
};
struct MidiRoutingConfiguration {
    std::array<MidiRoute, maximum_midi_routes> routes{};
    std::size_t route_count{};
    bool accept_external_clock{};
};
} // namespace neurogenic
