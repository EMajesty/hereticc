#pragma once
#include "neurogenic/timeline/edit.hpp"
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
struct ArrangementRecordingStatus {
    ArrangementRecordPhase phase{ArrangementRecordPhase::idle};
    ArrangementRecordMode mode{ArrangementRecordMode::replace};
    bool armed{}, automation_armed{};
    MusicalTick start_tick{}, stop_tick{}, count_in_ticks{};
    std::size_t captured_patterns{}, captured_automation{};
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

  private:
    bool reserve_event() noexcept;
    std::size_t capacity_;
    ArrangementRecordingStatus status_;
    std::vector<CapturedPatternLaunch> patterns_;
    std::vector<CapturedAutomationChange> automation_;
};
} // namespace neurogenic
