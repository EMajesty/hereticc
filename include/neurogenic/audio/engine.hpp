#pragma once
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/core/spsc_queue.hpp"
#include "neurogenic/dsp/purest_gain.hpp"
#include "neurogenic/sequencer/scheduler.hpp"
#include "neurogenic/timeline/playback.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace neurogenic {
class AudioEngine {
public:
  static constexpr std::size_t default_voice_capacity=32;
  static constexpr std::size_t default_note_bank_size=16;
  static constexpr std::size_t default_sample_version_capacity=64;
  explicit AudioEngine(std::uint32_t rate=48000,std::size_t voice_capacity=default_voice_capacity,std::size_t event_staging_capacity=512,std::size_t sample_version_capacity=default_sample_version_capacity);
  ~AudioEngine();
  void set_sample(Sample sample); // compatibility: slot 0, mapped from every note
  void set_sample_slot(SampleSlotId id,Sample sample);
  [[nodiscard]] bool publish_sample_slot(SampleSlotId id,Sample sample) noexcept;
  [[nodiscard]] bool unload_sample_slot(SampleSlotId id) noexcept;
  void reclaim_sample_versions() noexcept;
  [[nodiscard]] bool map_note_to_slot(std::uint8_t note,SampleSlotId id) noexcept;
  [[nodiscard]] bool has_sample_slot(SampleSlotId id) const noexcept;
  [[nodiscard]] std::size_t sample_slot_count() const noexcept;
  [[nodiscard]] std::uint32_t sample_rate() const noexcept{return rate_;}
  void trigger(std::uint8_t note, float velocity,std::uint32_t frame_offset=0) noexcept;
  bool queue_pattern(const Pattern& pattern) noexcept;
  bool queue_timeline(const Timeline&,std::string& error);
  void play() noexcept;
  void stop() noexcept;
  void restart() noexcept;
  void process(std::span<float> left, std::span<float> right) noexcept;
  void render_wav(const std::filesystem::path&, std::uint64_t frames);
  [[nodiscard]] std::uint64_t frame_position() const noexcept { return published_frame_.load(std::memory_order_acquire); }
  [[nodiscard]] bool playing() const noexcept{return published_playing_.load(std::memory_order_acquire);}
  [[nodiscard]] std::uint64_t dropped_triggers() const noexcept { return dropped_triggers_.load(); }
  [[nodiscard]] std::uint64_t dropped_patterns() const noexcept { return dropped_patterns_.load(); }
  [[nodiscard]] std::uint64_t stolen_voices() const noexcept { return stolen_voices_.load(); }
  [[nodiscard]] std::uint64_t missing_slot_triggers() const noexcept{return missing_slot_triggers_.load();}
  [[nodiscard]] std::uint64_t dropped_sample_updates() const noexcept{return dropped_sample_updates_.load();}
  [[nodiscard]] std::uint64_t reclaimed_sample_versions() const noexcept{return reclaimed_sample_versions_.load();}
  [[nodiscard]] std::uint64_t dropped_timelines() const noexcept{return dropped_timelines_.load();}
  [[nodiscard]] std::uint64_t dropped_timeline_events() const noexcept{return dropped_timeline_events_.load();}
  [[nodiscard]] std::uint64_t timeline_event_overruns() const noexcept{return dropped_timeline_events_.load();}
  [[nodiscard]] std::uint64_t dropped_legacy_events() const noexcept{return dropped_legacy_events_.load();}
  [[nodiscard]] std::uint64_t dropped_transport_commands() const noexcept{return dropped_transport_commands_.load();}
  [[nodiscard]] std::uint64_t reclaimed_timeline_plans() const noexcept{return reclaimed_timeline_plans_.load();}
  [[nodiscard]] std::size_t voice_capacity() const noexcept { return voices_.size(); }
private:
  struct Trigger { std::uint8_t note{}; float velocity{}; std::uint32_t frame_offset{}; std::uint64_t order{}; };
  struct Voice { const Sample* sample{};std::size_t sample_version{},cursor{};float gain{};std::uint64_t age{};TrackId track{};EventId event{};std::uint64_t occurrence{};std::uint8_t pitch{};bool timeline_owned{},active{}; };
  enum class SampleVersionState:std::uint8_t{free,queued,active,retiring,retired};
  struct SampleVersion {std::atomic<SampleVersionState> state{SampleVersionState::free};std::atomic<std::uint32_t> id{invalid_sample_slot.value};std::unique_ptr<const Sample> sample;};
  struct SampleUpdate {enum class Type:std::uint8_t{install,unload};Type type{};SampleSlotId id{};std::size_t version{};};
  class SampleUpdateQueue {
  public:
    explicit SampleUpdateQueue(std::size_t capacity):items_(std::make_unique<SampleUpdate[]>(capacity)),capacity_(capacity){}
    bool push(const SampleUpdate& item) noexcept{const auto head=head_.load(std::memory_order_relaxed);const auto next=(head+1)%capacity_;if(next==tail_.load(std::memory_order_acquire))return false;items_[head]=item;head_.store(next,std::memory_order_release);return true;}
    bool pop(SampleUpdate& item) noexcept{const auto tail=tail_.load(std::memory_order_relaxed);if(tail==head_.load(std::memory_order_acquire))return false;item=items_[tail];tail_.store((tail+1)%capacity_,std::memory_order_release);return true;}
  private:std::unique_ptr<SampleUpdate[]> items_;std::size_t capacity_{};std::atomic<std::size_t> head_{},tail_{};
  };
  enum class PlanState:std::uint8_t{free,queued,active,retired};
  struct PlanSlot {std::atomic<PlanState> state{PlanState::free};std::unique_ptr<PlaybackPlan> plan;};
  enum class TransportCommand:std::uint8_t{play,stop,restart};
  [[nodiscard]] std::size_t find_active_sample(SampleSlotId id) const noexcept;
  void start_voice(std::uint8_t note,float velocity,TrackId track=0,EventId event=0,std::uint64_t occurrence=0,bool timeline_owned=false) noexcept;
  void stop_timeline_voice(TrackId,EventId,std::uint64_t occurrence) noexcept;
  void stop_voices(bool timeline_only=false) noexcept;
  void reclaim_retired_plans() noexcept;
  std::uint32_t rate_; Scheduler scheduler_;TimelineScheduler timeline_scheduler_; PurestGain effect_;
  SpscQueue<Trigger,65> triggers_;
  SpscQueue<Pattern,9> patterns_;
  static constexpr std::size_t timeline_plan_slot_count=4;
  SpscQueue<std::uint8_t,4> timelines_;
  std::array<PlanSlot,timeline_plan_slot_count> timeline_plans_{};
  SpscQueue<TransportCommand,16> transport_commands_;
  std::atomic<std::uint64_t> dropped_triggers_{},dropped_patterns_{},dropped_timelines_{},dropped_timeline_events_{},dropped_legacy_events_{},dropped_transport_commands_{},reclaimed_timeline_plans_{},published_frame_{},next_order_{},stolen_voices_{},missing_slot_triggers_{},dropped_sample_updates_{},reclaimed_sample_versions_{};
  SampleUpdateQueue sample_updates_;
  std::unique_ptr<SampleVersion[]> sample_versions_;
  std::size_t sample_version_capacity_{};
  std::array<SampleSlotId,128> note_slots_{};
  std::vector<Voice> voices_;
  std::vector<ScheduledEvent> legacy_events_;
  std::vector<TimelineScheduledEvent> timeline_events_;
  int active_timeline_slot_{-1};
  std::array<Trigger,64> pending_triggers_{};
  std::size_t pending_trigger_count_{};
  std::uint64_t next_voice_age_{};
  std::uint64_t transport_frame_{};
  bool transport_playing_{true};
  std::atomic<bool> processing_started_{};
  std::atomic<bool> published_playing_{true};
};
}
