#pragma once
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/timeline/performance_launching.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neurogenic {
enum class ArrangementRecordMode : std::uint8_t { replace, overdub };
enum class ArrangementRecordPhase : std::uint8_t {
    idle,
    armed,
    recording,
    pending,
    committed,
    discarded,
    overflow,
    conflict
};
struct CapturedPatternLaunch {
    PatternId pattern{};
    MusicalTick tick{};
};
struct CapturedAutomationChange {
    std::string parameter_id;
    float value{};
    MusicalTick tick{};
};
struct CapturedPerformanceLaunch {
    LaunchSlotId slot{};
    TrackId track{};
    PreparedLaunchOperation operation{PreparedLaunchOperation::start};
    LaunchSourceType source_type{LaunchSourceType::pattern};
    std::uint64_t source_id{};
    MusicalTick tick{};
    std::uint64_t frame{};
    PerformanceSceneId scene{};
    bool follow_transition{};
    float velocity{1.0F};
};
struct ArrangementRecordingStatus {
    ArrangementRecordPhase phase{ArrangementRecordPhase::idle};
    ArrangementRecordMode mode{ArrangementRecordMode::replace};
    bool armed{}, automation_armed{};
    MusicalTick start_tick{}, stop_tick{}, count_in_ticks{};
    std::size_t captured_patterns{}, captured_automation{};
    std::size_t captured_performance_launches{};
    std::uint64_t overflow_count{};
    std::string error;
};

class ArrangementRecordingService {
  public:
    static constexpr std::size_t default_capture_capacity = 4096;
    explicit ArrangementRecordingService(
        std::size_t capacity = default_capture_capacity)
        : capacity_(capacity) {}
    bool arm(bool, std::string &);
    bool arm_automation(bool, std::string &);
    bool start(MusicalTick, ArrangementRecordMode, MusicalTick count_in,
               bool automation, std::string &);
    bool capture_pattern(PatternId, MusicalTick) noexcept;
    bool capture_chain(const Timeline &, MusicalTick) noexcept;
    bool capture_automation(std::string, float, MusicalTick,
                            MusicalTick thinning) noexcept;
    bool capture_performance(const PreparedLaunch &, std::uint64_t actual_frame,
                             PerformanceSceneId scene = 0,
                             bool follow_transition = false,
                             float velocity = 1.0F) noexcept;
    bool stop(MusicalTick, std::string &);
    void report_overflow(std::uint64_t count = 1) noexcept;
    bool commit(TimelineService &, std::string &);
    void discard() noexcept;
    [[nodiscard]] ArrangementRecordingStatus status() const;
    [[nodiscard]] const std::vector<CapturedPatternLaunch> &patterns() const {
        return patterns_;
    }
    [[nodiscard]] const std::vector<CapturedAutomationChange> &
    automation() const {
        return automation_;
    }
    [[nodiscard]] const std::vector<CapturedPerformanceLaunch> &
    performance_launches() const noexcept { return performance_launches_; }

  private:
    bool reserve_event() noexcept;
    std::size_t capacity_;
    ArrangementRecordingStatus status_;
    std::vector<CapturedPatternLaunch> patterns_;
    std::vector<CapturedAutomationChange> automation_;
    std::vector<CapturedPerformanceLaunch> performance_launches_;
};
} // namespace neurogenic
