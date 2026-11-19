#pragma once
#include "neurogenic/audio/sample_manager.hpp"
#include <array>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace neurogenic {
using SliceId = std::uint64_t;

struct SampleSlice {
    SliceId id{};
    SampleSlotId slot{};
    std::uint64_t sample_revision{}, sample_publication{};
    std::uint64_t start_frame{}, end_frame{};
    std::string name;
    std::optional<std::uint8_t> root_note;
    float gain{1.0F};
};

struct WaveformPeak {
    float left_min{}, left_max{}, right_min{}, right_max{};
};
[[nodiscard]] std::vector<WaveformPeak>
build_waveform_peaks(const Sample &, std::size_t maximum_peaks = 1024);
[[nodiscard]] std::vector<std::uint64_t>
detect_transient_frames(const Sample &, float sensitivity,
                        std::uint64_t minimum_separation);

struct SliceSnapshot {
    std::uint64_t revision{};
    SampleSlotId selected_slot{};
    SliceId selected_slice{};
    std::uint64_t sample_revision{}, sample_publication{}, sample_frames{};
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
    std::vector<SampleSlice> slices;
    std::array<std::optional<SliceId>, 16> pad_assignments;
    std::vector<WaveformPeak> waveform;
};

class SliceService {
  public:
    SliceService(AudioEngine &, SampleManager &);
    [[nodiscard]] SliceSnapshot snapshot(SampleSlotId);
    bool create(SampleSlotId, std::uint64_t expected_revision,
                std::uint64_t start, std::uint64_t end, std::string name,
                SliceId &created, std::string &error);
    bool erase(SliceId, std::uint64_t expected_revision, std::string &error);
    bool set_bounds(SliceId, std::uint64_t expected_revision,
                    std::uint64_t start, std::uint64_t end, std::string &error);
    bool rename(SliceId, std::uint64_t expected_revision, std::string,
                std::string &error);
    bool duplicate(SliceId, std::uint64_t expected_revision, SliceId &created,
                   std::string &error);
    bool clear(SampleSlotId, std::uint64_t expected_revision,
               std::string &error);
    bool equal_divisions(SampleSlotId, std::uint64_t expected_revision,
                         std::size_t divisions, std::string &error);
    bool detect_transients(SampleSlotId, std::uint64_t expected_revision,
                           float sensitivity, std::uint64_t minimum_separation,
                           std::string &error);
    bool assign_pad(std::size_t pad, SliceId, std::uint64_t expected_revision,
                    std::string &error);
    bool clear_pad(std::size_t pad, std::uint64_t expected_revision,
                   std::string &error);
    bool preview(SliceId, float velocity, std::string &error);
    bool preview_full(SampleSlotId, float velocity, std::string &error);
    void stop_preview() noexcept;

  private:
    struct SlotState {
        std::uint64_t revision{1}, sample_revision{}, sample_publication{};
        std::uint64_t frames{};
        std::uint32_t rate{};
        std::uint16_t channels{};
        std::vector<SampleSlice> slices;
        std::vector<WaveformPeak> waveform;
    };
    void synchronize_locked(SampleSlotId);
    [[nodiscard]] SampleSlice *find_locked(SliceId);
    [[nodiscard]] const SampleSlice *find_locked(SliceId) const;
    bool valid_revision_locked(SampleSlotId, std::uint64_t,
                               std::string &) const;
    bool publish_pad_locked(std::size_t, const SampleSlice *, std::string &);
    AudioEngine &engine_;
    SampleManager &samples_;
    mutable std::mutex mutex_;
    std::map<std::uint32_t, SlotState> slots_;
    std::array<std::optional<SliceId>, 16> pads_{};
    SliceId next_id_{1};
};

struct SliceProject {
    static constexpr unsigned version = 1;
    std::vector<SampleSlice> slices;
    std::array<std::optional<SliceId>, 16> pads{};
    std::map<std::uint32_t, std::filesystem::path> sample_paths;
};
void save_slice_project(const SliceProject &, const std::filesystem::path &);
SliceProject load_slice_project(const std::filesystem::path &);
} // namespace neurogenic
