#pragma once

#include "neurogenic/audio/slicing.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace neurogenic {
using SampleAnalysisSessionId = std::uint64_t;
using SampleAnalysisCandidateId = std::uint64_t;

enum class SampleAnalysisAlgorithm : std::uint8_t {
    transients,
    quiet_regions,
    equal_divisions,
    tempo_grid
};
enum class SampleAnalysisStatus : std::uint8_t {
    queued,
    running,
    complete,
    cancelled,
    failed,
    stale
};
enum class SampleAnalysisApplyTarget : std::uint8_t { slices, regions };
enum class SampleAnalysisApplyMode : std::uint8_t { append, replace };

struct SampleAnalysisSettings {
    SampleAnalysisAlgorithm algorithm{SampleAnalysisAlgorithm::transients};
    std::uint64_t start_frame{};
    std::uint64_t end_frame{}; // zero means the end of the source
    float sensitivity{0.5F};
    std::uint64_t minimum_separation_frames{2400};
    float quiet_threshold_db{-48.0F};
    float quiet_hysteresis_db{6.0F};
    std::uint64_t minimum_quiet_frames{2400};
    std::uint64_t boundary_padding_frames{};
    std::uint32_t divisions{16};
    double source_bpm{120.0};
    double beats_per_slice{1.0};
    std::int64_t grid_offset_frames{};
};

struct SampleAnalysisCandidate {
    SampleAnalysisCandidateId id{};
    std::uint64_t frame{};
    float strength{1.0F};
    bool enabled{true};
};
struct SuggestedSampleRegion {
    std::uint64_t start_frame{}, end_frame{};
    float confidence{1.0F};
};
struct SampleAnalysisResult {
    SampleSlotId source_slot{invalid_sample_slot};
    std::uint64_t source_revision{}, source_publication{};
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
    std::uint64_t source_frames{}, range_start{}, range_end{};
    SampleAnalysisSettings settings;
    std::vector<SampleAnalysisCandidate> candidates;
    std::vector<SuggestedSampleRegion> suggested_regions;
    std::size_t peak_temporary_bytes{};
    std::string diagnostics;
};
struct SampleAnalysisSnapshot {
    SampleAnalysisSessionId id{};
    SampleAnalysisStatus status{SampleAnalysisStatus::queued};
    float progress{};
    bool source_stale{};
    SampleAnalysisResult result;
    std::string error;
};
struct SampleAnalysisApplyOptions {
    SampleAnalysisApplyTarget target{SampleAnalysisApplyTarget::slices};
    SampleAnalysisApplyMode mode{SampleAnalysisApplyMode::replace};
    bool assign_pads{};
    std::size_t starting_pad{};
};

[[nodiscard]] const char *
sample_analysis_algorithm_name(SampleAnalysisAlgorithm) noexcept;
[[nodiscard]] const char *
sample_analysis_status_name(SampleAnalysisStatus) noexcept;
[[nodiscard]] SampleAnalysisResult analyze_sample(
    const Sample &, SampleSlotId, std::uint64_t revision,
    std::uint64_t publication, const SampleAnalysisSettings &,
    std::stop_token = {}, const std::function<void(float)> &progress = {});

class SampleAnalysisService {
  public:
    static constexpr std::size_t maximum_sessions = 8;
    SampleAnalysisService(SampleManager &, SliceService &);
    ~SampleAnalysisService();
    SampleAnalysisService(const SampleAnalysisService &) = delete;
    SampleAnalysisService &operator=(const SampleAnalysisService &) = delete;
    bool start(SampleSlotId, SampleAnalysisSettings,
               SampleAnalysisSessionId &, std::string &);
    bool cancel(SampleAnalysisSessionId, std::string &);
    bool discard(SampleAnalysisSessionId, std::string &);
    [[nodiscard]] std::optional<SampleAnalysisSnapshot>
    snapshot(SampleAnalysisSessionId) const;
    [[nodiscard]] std::vector<SampleAnalysisSnapshot> snapshots() const;
    bool add_candidate(SampleAnalysisSessionId, std::uint64_t frame,
                       SampleAnalysisCandidateId &, std::string &);
    bool move_candidate(SampleAnalysisSessionId, SampleAnalysisCandidateId,
                        std::uint64_t frame, std::string &);
    bool remove_candidate(SampleAnalysisSessionId, SampleAnalysisCandidateId,
                          std::string &);
    bool set_candidate_enabled(SampleAnalysisSessionId,
                               SampleAnalysisCandidateId, bool,
                               std::string &);
    bool apply(SampleAnalysisSessionId, SampleAnalysisApplyOptions,
               std::string &);

  private:
    struct Session {
        SampleAnalysisSnapshot state;
        std::shared_ptr<const Sample> source;
        std::jthread worker;
    };
    [[nodiscard]] bool editable_locked(Session &, std::string &) const;
    void refresh_stale_locked(Session &) const;
    SampleManager &samples_;
    SliceService &slices_;
    mutable std::mutex mutex_;
    std::map<SampleAnalysisSessionId, std::unique_ptr<Session>> sessions_;
    SampleAnalysisSessionId next_session_id_{1};
    SampleAnalysisCandidateId next_candidate_id_{1};
};
} // namespace neurogenic
