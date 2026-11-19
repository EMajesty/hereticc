#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace neurogenic {
struct AudioCallbackStatistics {
    bool available{};
    std::uint64_t current_nanoseconds{}, average_nanoseconds{},
        peak_nanoseconds{}, deadline_nanoseconds{}, callback_count{},
        deadline_overruns{};
    double current_load_percent{}, average_load_percent{}, peak_load_percent{};
};

struct EngineStatistics {
    std::uint32_t sample_rate{}, buffer_frames{};
    std::size_t active_voices{}, maximum_voices{};
    bool playing{};
    std::uint64_t dropped_triggers{}, dropped_patterns{}, dropped_timelines{},
        timeline_event_overruns{}, dropped_legacy_events{},
        dropped_transport_commands{}, dropped_sample_updates{},
        missing_slot_triggers{}, stolen_voices{}, rejected_commands{},
        dropped_dsp_commands{}, synth_stolen_voices{}, instrument_nanoseconds{},
        effect_nanoseconds{};
    std::size_t active_synth_voices{}, maximum_synth_voices{};
    std::uint64_t recording_captured_frames{}, recording_capacity_frames{},
        recording_dropped_frames{},
        recording_overflows{}, recording_finalization_microseconds{},
        recording_memory_bytes{};
};

struct SystemStatistics {
    bool process_cpu_available{}, resident_memory_available{},
        total_memory_available{}, system_cpu_available{};
    double process_cpu_percent{}, system_cpu_percent{};
    std::uint64_t resident_memory_bytes{}, total_memory_bytes{};
};

struct PerformanceSnapshot {
    std::uint64_t generation{}, monotonic_milliseconds{};
    AudioCallbackStatistics audio;
    EngineStatistics engine;
    SystemStatistics system;
};
[[nodiscard]] std::string performance_json(const PerformanceSnapshot &);
[[nodiscard]] SystemStatistics sample_system_now() noexcept;

[[nodiscard]] double cpu_percent(std::uint64_t work_delta_nanoseconds,
                                 std::uint64_t wall_delta_nanoseconds) noexcept;
void saturating_increment(std::atomic<std::uint64_t> &value) noexcept;

class CallbackMonitor {
  public:
    explicit CallbackMonitor(bool enabled = true) noexcept
        : enabled_(enabled) {}
    void record(std::uint64_t elapsed_nanoseconds, std::uint32_t frames,
                std::uint32_t sample_rate, std::size_t active_voices) noexcept;
    [[nodiscard]] AudioCallbackStatistics snapshot() const noexcept;
    [[nodiscard]] std::size_t active_voices() const noexcept {
        return active_voices_.load(std::memory_order_relaxed);
    }
    void set_enabled(bool value) noexcept { enabled_.store(value); }
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(); }

  private:
    std::atomic<bool> enabled_;
    std::atomic<std::uint64_t> current_{}, average_{}, peak_{}, deadline_{},
        count_{}, overruns_{};
    std::atomic<std::size_t> active_voices_{};
};

class SystemMonitor {
  public:
    explicit SystemMonitor(
        std::chrono::milliseconds interval = std::chrono::milliseconds(500));
    ~SystemMonitor();
    SystemMonitor(const SystemMonitor &) = delete;
    SystemMonitor &operator=(const SystemMonitor &) = delete;
    [[nodiscard]] SystemStatistics snapshot() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t monotonic_milliseconds() const noexcept {
        return timestamp_ms_.load(std::memory_order_acquire);
    }

  private:
    void run() noexcept;
    std::chrono::milliseconds interval_;
    std::atomic<bool> stopping_{};
    std::thread worker_;
    std::atomic<std::uint64_t> generation_{}, timestamp_ms_{}, resident_{},
        total_{}, process_cpu_milli_{}, system_cpu_milli_{};
    std::atomic<std::uint8_t> availability_{};
    std::mutex wait_mutex_;
    std::condition_variable wake_;
};
} // namespace neurogenic
