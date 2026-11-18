#pragma once
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/core/spsc_queue.hpp"
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
  bool queue_pattern(const Pattern& pattern) noexcept;
  void process(std::span<float> left, std::span<float> right) noexcept;
  void render_wav(const std::filesystem::path&, std::uint64_t frames);
  [[nodiscard]] std::uint64_t frame_position() const noexcept { return published_frame_.load(std::memory_order_acquire); }
  [[nodiscard]] std::uint64_t dropped_triggers() const noexcept { return dropped_triggers_.load(); }
  [[nodiscard]] std::uint64_t dropped_patterns() const noexcept { return dropped_patterns_.load(); }
private:
  struct Trigger { std::uint8_t note{}; float velocity{}; };
  std::uint32_t rate_; Sample sample_; Scheduler scheduler_; PurestGain effect_;
  SpscQueue<Trigger,65> triggers_;
  SpscQueue<Pattern,9> patterns_;
  std::atomic<std::uint64_t> dropped_triggers_{},dropped_patterns_{},published_frame_{};
  std::size_t cursor_{}; float voice_gain_{};
};
}
