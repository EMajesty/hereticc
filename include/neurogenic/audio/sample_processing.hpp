#pragma once

#include "neurogenic/audio/slicing.hpp"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace neurogenic {
using SampleProcessingJobId = std::uint64_t;
using SampleProcessingOperationId = std::uint64_t;

enum class SampleProcessingSourceKind : std::uint8_t { sample, region, slice };
enum class SampleProcessingOperationType : std::uint8_t {
    trim,
    normalize_peak,
    normalize_rms,
    reverse,
    gain,
    fade,
    insert_silence,
    resample,
    channel_convert
};
enum class SampleProcessingStatus : std::uint8_t {
    draft,
    running,
    complete,
    cancelled,
    failed,
    publication_pending,
    audio_active
};
enum class SampleWavFormat : std::uint8_t { pcm16, pcm24, float32 };
enum class SilencePosition : std::uint8_t { beginning, end };

struct SampleProcessingSource {
    SampleProcessingSourceKind kind{SampleProcessingSourceKind::sample};
    SampleSlotId slot{invalid_sample_slot};
    RegionId region{};
    SliceId slice{};
    std::uint64_t loop_output_frames{};
    bool apply_region_gain_and_fades{true};
};
struct SampleProcessingOperation {
    SampleProcessingOperationId id{};
    SampleProcessingOperationType type{SampleProcessingOperationType::gain};
    std::uint64_t start_frame{}, end_frame{};
    double value{};
    std::uint64_t frames{};
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
    SilencePosition silence_position{SilencePosition::end};
};
struct SampleProcessingResultMetadata {
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
    std::uint64_t frames{}, memory_bytes{}, processing_microseconds{};
    float peak{}, rms{};
    std::filesystem::path saved_path;
    SampleSlotId destination{invalid_sample_slot};
};
struct SampleProcessingSnapshot {
    SampleProcessingJobId id{};
    SampleProcessingStatus status{SampleProcessingStatus::draft};
    SampleProcessingSource source;
    std::uint64_t source_revision{}, source_publication{};
    std::vector<SampleProcessingOperation> operations;
    float progress{};
    SampleProcessingResultMetadata result;
    std::string error;
};

[[nodiscard]] const char *
sample_processing_operation_name(SampleProcessingOperationType) noexcept;
[[nodiscard]] const char *
sample_processing_status_name(SampleProcessingStatus) noexcept;
[[nodiscard]] Sample process_sample_offline(
    Sample, std::span<const SampleProcessingOperation>, std::stop_token = {},
    const std::function<void(float)> &progress = {});
void save_sample_wav_atomic(const Sample &, const std::filesystem::path &,
                            SampleWavFormat, bool overwrite = false);

class SampleProcessingService {
  public:
    static constexpr std::size_t maximum_jobs = 8;
    static constexpr std::size_t maximum_operations = 16;
    SampleProcessingService(SampleManager &, SliceService &);
    ~SampleProcessingService();
    bool create(SampleProcessingSource, SampleProcessingJobId &,
                std::string &);
    bool add_operation(SampleProcessingJobId, SampleProcessingOperation,
                       SampleProcessingOperationId &, std::string &);
    bool remove_operation(SampleProcessingJobId, SampleProcessingOperationId,
                          std::string &);
    bool move_operation(SampleProcessingJobId, SampleProcessingOperationId,
                        std::size_t, std::string &);
    bool validate(SampleProcessingJobId, std::string &);
    bool start(SampleProcessingJobId, std::string &);
    bool cancel(SampleProcessingJobId, std::string &);
    bool discard(SampleProcessingJobId, std::string &);
    bool save(SampleProcessingJobId, const std::filesystem::path &,
              SampleWavFormat, bool overwrite, std::string &);
    bool publish_to_slot(SampleProcessingJobId, SampleSlotId, std::string &);
    [[nodiscard]] std::optional<SampleProcessingSnapshot>
    snapshot(SampleProcessingJobId) const;
    [[nodiscard]] std::vector<SampleProcessingSnapshot> snapshots() const;

  private:
    struct Job {
        SampleProcessingSnapshot state;
        std::shared_ptr<const Sample> source_sample;
        std::shared_ptr<const Sample> result_sample;
        std::optional<SampleRegion> source_region;
        std::optional<SampleSlice> source_slice;
        std::uint64_t pending_destination_revision{};
        std::jthread worker;
    };
    bool prepare_source_locked(Job &, std::string &);
    void refresh_publication_locked(Job &) const;
    SampleManager &samples_;
    SliceService &slices_;
    mutable std::mutex mutex_;
    std::map<SampleProcessingJobId, std::unique_ptr<Job>> jobs_;
    SampleProcessingJobId next_job_id_{1};
    SampleProcessingOperationId next_operation_id_{1};
};
} // namespace neurogenic
