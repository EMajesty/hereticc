#pragma once
#include "neurogenic/audio/engine.hpp"
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace neurogenic {
enum class SampleSlotLoadState : std::uint8_t {
    empty,
    queued,
    preparing,
    prepared,
    pending_activation,
    loaded,
    error
};
struct SampleSlotStatus {
    SampleSlotId id{};
    SampleSlotLoadState state{SampleSlotLoadState::empty};
    bool has_sample{};
    std::filesystem::path source_path;
    std::uint32_t source_sample_rate{};
    std::uint32_t engine_sample_rate{};
    std::uint16_t channels{};
    std::uint64_t frames{};
    double duration_seconds{};
    float progress{};
    std::string error;
    std::uint64_t revision{};
    std::uint64_t active_revision{}, active_publication{};
    std::filesystem::path pending_source_path;
};

class SampleManager {
  public:
    static constexpr std::size_t default_command_capacity = 64;
    explicit SampleManager(
        AudioEngine &, std::size_t command_capacity = default_command_capacity);
    ~SampleManager();
    SampleManager(const SampleManager &) = delete;
    SampleManager &operator=(const SampleManager &) = delete;
    [[nodiscard]] bool load(SampleSlotId, const std::filesystem::path &);
    [[nodiscard]] bool replace(SampleSlotId id,
                               const std::filesystem::path &path) {
        return load(id, path);
    }
    [[nodiscard]] bool unload(SampleSlotId);
    [[nodiscard]] bool
        publish_prepared(SampleSlotId, std::shared_ptr<const Sample>,
                         const std::filesystem::path &source_path);
    [[nodiscard]] bool set_source_path(SampleSlotId,
                                       const std::filesystem::path &);
    void adopt_preset_activation(SampleSlotId,
                                 std::shared_ptr<const Sample>,
                                 const std::filesystem::path &source_path,
                                 std::uint64_t publication);
    [[nodiscard]] SampleSlotStatus status(SampleSlotId) const;
    [[nodiscard]] std::vector<SampleSlotStatus> states() const;
    [[nodiscard]] std::shared_ptr<const Sample>
        active_sample(SampleSlotId) const;
    void wait_idle();
    void shutdown() noexcept;
    [[nodiscard]] std::uint64_t rejected_commands() const noexcept;

  private:
    enum class Operation : std::uint8_t { load, unload };
    struct Command {
        Operation operation{};
        SampleSlotId id{};
        std::filesystem::path path;
        std::uint64_t revision{};
    };
    struct Publication {
        Operation operation{};
        std::uint64_t revision{}, sequence{};
        std::filesystem::path path;
        std::uint32_t source_rate{};
        std::uint16_t channels{};
        std::uint64_t frames{};
        double duration_seconds{};
        std::shared_ptr<const Sample> sample;
    };
    struct SlotRecord {
        SampleSlotStatus status;
        std::deque<Publication> publications;
        std::shared_ptr<const Sample> active_sample;
    };
    bool submit(Command);
    void run() noexcept;
    void refresh_locked(SlotRecord &) const;
    AudioEngine &engine_;
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::deque<Command> commands_;
    mutable std::map<std::uint32_t, SlotRecord> states_;
    std::thread worker_;
    bool stopping_{}, busy_{};
    std::uint64_t next_revision_{1}, rejected_{};
};
[[nodiscard]] const char *sample_slot_state_name(SampleSlotLoadState) noexcept;
} // namespace neurogenic
