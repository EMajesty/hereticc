#pragma once
#include "neurogenic/timeline/model.hpp"
#include <span>
#include <string>
#include <vector>

namespace neurogenic {
inline constexpr std::size_t maximum_launch_slots = 256;
inline constexpr std::size_t maximum_performance_scenes = 64;
inline constexpr std::size_t maximum_scene_entries = 64;
inline constexpr std::size_t maximum_pending_launches = 128;

enum class PreparedLaunchOperation : std::uint8_t { start, stop, retrigger };
struct PreparedLaunch {
    LaunchSlotId slot{};
    TrackId track{};
    PreparedLaunchOperation operation{PreparedLaunchOperation::start};
    MusicalTick activation_tick{};
    std::uint64_t activation_frame{};
    LaunchSourceType source_type{LaunchSourceType::pattern};
    std::uint64_t source_id{};
    LaunchBehavior behavior{LaunchBehavior::loop};
    bool preserve_phase{};
};
struct LaunchDiagnostics {
    std::size_t slot_capacity{maximum_launch_slots};
    std::size_t scene_capacity{maximum_performance_scenes};
    std::size_t pending_capacity{maximum_pending_launches};
    std::size_t used_slots{}, used_scenes{}, pending{};
    std::uint64_t rejected{}, late_commands{};
};

class PerformanceLauncher {
  public:
    explicit PerformanceLauncher(std::uint32_t sample_rate = 48000)
        : sample_rate_(sample_rate) {}
    bool configure(const Timeline &, std::string &, bool preserve_pending = false);
    bool prepare_slot(LaunchSlotId, PreparedLaunchOperation, MusicalTick now,
                      std::uint64_t now_frame, PreparedLaunch &, std::string &);
    bool prepare_scene(PerformanceSceneId, MusicalTick now,
                       std::uint64_t now_frame, std::vector<PreparedLaunch> &,
                       std::string &);
    bool cancel(LaunchSlotId) noexcept;
    void acknowledge(std::uint64_t activation_frame) noexcept;
    [[nodiscard]] LaunchSlotId follow(LaunchSlotId, std::uint64_t repetition,
                                      std::span<const LaunchSlotId> eligible) const noexcept;
    [[nodiscard]] float velocity(float input, bool velocity_capable,
                                 bool full_level) const noexcept;
    [[nodiscard]] const LaunchDiagnostics &diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] std::span<const PreparedLaunch> pending() const noexcept { return pending_; }

  private:
    [[nodiscard]] const LaunchSlot *slot(LaunchSlotId) const noexcept;
    [[nodiscard]] MusicalTick boundary(const LaunchSlot &, MusicalTick) const noexcept;
    [[nodiscard]] std::uint64_t frame_at(MusicalTick, MusicalTick,
                                         std::uint64_t) const noexcept;
    Timeline timeline_;
    std::uint32_t sample_rate_{};
    std::vector<PreparedLaunch> pending_;
    LaunchDiagnostics diagnostics_;
};
} // namespace neurogenic
