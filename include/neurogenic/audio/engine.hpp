#pragma once
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/core/spsc_queue.hpp"
#include "neurogenic/dsp/purest_gain.hpp"
#include "neurogenic/sequencer/scheduler.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <span>
#include <vector>
namespace neurogenic {
class AudioEngine {
public:
  static constexpr std::size_t default_voice_capacity=32;
  explicit AudioEngine(std::uint32_t rate=48000,std::size_t voice_capacity=default_voice_capacity);
  void set_sample(Sample sample); // control thread only
  void trigger(std::uint8_t note, float velocity,std::uint32_t frame_offset=0) noexcept;
  bool queue_pattern(const Pattern& pattern) noexcept;
  void process(std::span<float> left, std::span<float> right) noexcept;
  void render_wav(const std::filesystem::path&, std::uint64_t frames);
  [[nodiscard]] std::uint64_t frame_position() const noexcept { return published_frame_.load(std::memory_order_acquire); }
  [[nodiscard]] std::uint64_t dropped_triggers() const noexcept { return dropped_triggers_.load(); }
  [[nodiscard]] std::uint64_t dropped_patterns() const noexcept { return dropped_patterns_.load(); }
  [[nodiscard]] std::uint64_t stolen_voices() const noexcept { return stolen_voices_.load(); }
  [[nodiscard]] std::size_t voice_capacity() const noexcept { return voices_.size(); }
private:
  struct Trigger { std::uint8_t note{}; float velocity{}; std::uint32_t frame_offset{}; std::uint64_t order{}; };
  struct Voice { std::size_t cursor{};float gain{};std::uint64_t age{};bool active{}; };
  void start_voice(float velocity) noexcept;
  std::uint32_t rate_; Sample sample_; Scheduler scheduler_; PurestGain effect_;
  SpscQueue<Trigger,65> triggers_;
  SpscQueue<Pattern,9> patterns_;
  std::atomic<std::uint64_t> dropped_triggers_{},dropped_patterns_{},published_frame_{},next_order_{},stolen_voices_{};
  std::vector<Voice> voices_;
  std::array<Trigger,64> pending_triggers_{};
  std::size_t pending_trigger_count_{};
  std::uint64_t next_voice_age_{};
  std::atomic<bool> processing_started_{};
};
}
