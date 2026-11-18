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
enum class SampleSlotLoadState:std::uint8_t{empty,loading,loaded,error};
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
};

class SampleManager {
public:
  static constexpr std::size_t default_command_capacity=64;
  explicit SampleManager(AudioEngine&,std::size_t command_capacity=default_command_capacity);
  ~SampleManager();
  SampleManager(const SampleManager&)=delete;
  SampleManager& operator=(const SampleManager&)=delete;
  [[nodiscard]] bool load(SampleSlotId,const std::filesystem::path&);
  [[nodiscard]] bool replace(SampleSlotId id,const std::filesystem::path& path){return load(id,path);}
  [[nodiscard]] bool unload(SampleSlotId);
  [[nodiscard]] SampleSlotStatus status(SampleSlotId) const;
  [[nodiscard]] std::vector<SampleSlotStatus> states() const;
  void wait_idle();
  void shutdown() noexcept;
  [[nodiscard]] std::uint64_t rejected_commands() const noexcept;
private:
  enum class Operation:std::uint8_t{load,unload};
  struct Command{Operation operation{};SampleSlotId id{};std::filesystem::path path;std::uint64_t revision{};};
  bool submit(Command);
  void run() noexcept;
  AudioEngine& engine_;const std::size_t capacity_;
  mutable std::mutex mutex_;std::condition_variable wake_,idle_;std::deque<Command> commands_;std::map<std::uint32_t,SampleSlotStatus> states_;
  std::thread worker_;bool stopping_{},busy_{};std::uint64_t next_revision_{1},rejected_{};
};
[[nodiscard]] const char* sample_slot_state_name(SampleSlotLoadState) noexcept;
}
