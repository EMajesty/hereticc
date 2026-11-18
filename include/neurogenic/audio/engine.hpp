#pragma once
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/dsp/purest_gain.hpp"
#include "neurogenic/sequencer/scheduler.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <span>
namespace neurogenic {
class AudioEngine {
public:
  explicit AudioEngine(std::uint32_t rate=48000);
  void set_sample(Sample sample); // control thread only
  void trigger(std::uint8_t note, float velocity) noexcept;
  void process(std::span<float> left, std::span<float> right) noexcept;
  void render_wav(const std::filesystem::path&, std::uint64_t frames);
  Scheduler& scheduler() noexcept { return scheduler_; }
private:
  struct Trigger { std::uint8_t note{}; float velocity{}; };
  std::uint32_t rate_; Sample sample_; Scheduler scheduler_; PurestGain effect_;
  std::atomic<bool> trigger_pending_{}; Trigger trigger_{};
  std::size_t cursor_{}; float voice_gain_{};
};
}
