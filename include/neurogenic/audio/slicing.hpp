#pragma once
#include "neurogenic/audio/sample_manager.hpp"
#include <array>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neurogenic {
using SliceId = std::uint64_t;
using RegionId = std::uint64_t;
enum class RegionDirection : std::uint8_t { forward, reverse };
struct SliceProject;
[[nodiscard]] const char *
    sample_playback_mode_name(SamplePlaybackMode) noexcept;
[[nodiscard]] std::optional<SamplePlaybackMode>
    parse_sample_playback_mode(std::string_view) noexcept;
[[nodiscard]] const char *stretch_mode_name(StretchMode) noexcept;
[[nodiscard]] std::optional<StretchMode>
    parse_stretch_mode(std::string_view) noexcept;
[[nodiscard]] const char *stretch_quality_name(StretchQuality) noexcept;
[[nodiscard]] std::optional<StretchQuality>
    parse_stretch_quality(std::string_view) noexcept;

struct SampleSlice {
    SliceId id{};
    SampleSlotId slot{};
    std::uint64_t sample_revision{}, sample_publication{};
    std::uint64_t start_frame{}, end_frame{};
    std::string name;
    std::optional<std::uint8_t> root_note;
    float gain{1.0F};
};

struct SampleRegion {
    RegionId id{};
    SampleSlotId slot{};
    std::uint64_t sample_revision{}, sample_publication{};
    std::uint64_t start_frame{}, end_frame{};
    std::string name;
    RegionDirection direction{RegionDirection::forward};
    RegionFadeCurve fade_curve{RegionFadeCurve::linear};
    std::uint64_t fade_in_frames{}, fade_out_frames{};
    float gain{1.0F};
    bool loop_enabled{};
    std::uint64_t loop_start_frame{}, loop_end_frame{};
    std::uint32_t loop_crossfade_frames{};
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
    std::vector<SampleRegion> regions;
    std::array<std::optional<SliceId>, 16> pad_assignments;
    std::array<std::optional<RegionId>, 16> region_pad_assignments;
    std::array<SamplePlaybackSettings, 16> pad_settings;
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
    bool set_pad_settings(SampleSlotId context_slot, std::size_t pad,
                          std::uint64_t expected_revision,
                          SamplePlaybackSettings, std::string &error);
    bool preview(SliceId, float velocity, std::string &error);
    bool preview_full(SampleSlotId, float velocity, std::string &error);
    bool create_region(SampleSlotId, std::uint64_t expected_revision,
                       std::uint64_t start, std::uint64_t end,
                       std::string name, RegionId &created,
                       std::string &error);
    bool create_region_from_slice(SliceId, std::uint64_t expected_revision,
                                  RegionId &created, std::string &error);
    bool erase_region(RegionId, std::uint64_t expected_revision,
                      std::string &error);
    bool duplicate_region(RegionId, std::uint64_t expected_revision,
                          RegionId &created, std::string &error);
    bool rename_region(RegionId, std::uint64_t expected_revision, std::string,
                       std::string &error);
    bool set_region_bounds(RegionId, std::uint64_t expected_revision,
                           std::uint64_t start, std::uint64_t end,
                           std::string &error);
    bool set_region_fades(RegionId, std::uint64_t expected_revision,
                          std::uint64_t fade_in, std::uint64_t fade_out,
                          RegionFadeCurve, std::string &error);
    bool set_region_gain(RegionId, std::uint64_t expected_revision, float,
                         std::string &error);
    bool set_region_direction(RegionId, std::uint64_t expected_revision,
                              RegionDirection, std::string &error);
    bool set_region_loop(RegionId, std::uint64_t expected_revision, bool,
                         std::uint64_t loop_start, std::uint64_t loop_end,
                         std::uint32_t crossfade, std::string &error);
    bool assign_region_pad(std::size_t pad, RegionId,
                           std::uint64_t expected_revision,
                           std::string &error);
    [[nodiscard]] std::size_t region_usage(RegionId) const;
    bool preview_region(RegionId, float velocity, std::string &error);
    void stop_preview() noexcept;
    [[nodiscard]] SliceProject
        project_snapshot(std::map<std::uint32_t, std::filesystem::path>) const;
    bool restore_project(const SliceProject &, std::string &error);
    bool adopt_activated_project(const SliceProject &, std::string &error);

  private:
    struct SlotState {
        std::uint64_t revision{1}, sample_revision{}, sample_publication{};
        std::uint64_t frames{};
        std::uint32_t rate{};
        std::uint16_t channels{};
        std::vector<SampleSlice> slices;
        std::vector<SampleRegion> regions;
        std::vector<WaveformPeak> waveform;
    };
    void synchronize_locked(SampleSlotId);
    [[nodiscard]] SampleSlice *find_locked(SliceId);
    [[nodiscard]] const SampleSlice *find_locked(SliceId) const;
    [[nodiscard]] SampleRegion *find_region_locked(RegionId);
    [[nodiscard]] const SampleRegion *find_region_locked(RegionId) const;
    bool valid_revision_locked(SampleSlotId, std::uint64_t,
                               std::string &) const;
    bool publish_pad_locked(std::size_t, const SampleSlice *, std::string &);
    bool publish_region_pad_locked(std::size_t, const SampleRegion *,
                                   std::string &);
    AudioEngine &engine_;
    SampleManager &samples_;
    mutable std::mutex mutex_;
    std::map<std::uint32_t, SlotState> slots_;
    std::array<std::optional<SliceId>, 16> pads_{};
    std::array<std::optional<RegionId>, 16> region_pads_{};
    std::array<SamplePlaybackSettings, 16> pad_settings_{};
    SliceId next_id_{1};
    RegionId next_region_id_{1};
};

struct SliceProject {
    static constexpr unsigned version = 4;
    std::vector<SampleSlice> slices;
    std::vector<SampleRegion> regions;
    std::array<std::optional<SliceId>, 16> pads{};
    std::array<std::optional<RegionId>, 16> region_pads{};
    std::array<SamplePlaybackSettings, 16> pad_settings{};
    std::map<std::uint32_t, std::filesystem::path> sample_paths;
};
void save_slice_project(const SliceProject &, const std::filesystem::path &);
SliceProject load_slice_project(const std::filesystem::path &,
                                bool require_samples = true);
} // namespace neurogenic
