#pragma once
#include "neurogenic/audio/mixer.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/core/spsc_queue.hpp"
#include "neurogenic/dsp/processor.hpp"
#include "neurogenic/dsp/purest_gain.hpp"
#include "neurogenic/dsp/time_stretch.hpp"
#include "neurogenic/instrument/subtractive_synth.hpp"
#include "neurogenic/performance/monitor.hpp"
#include "neurogenic/sequencer/scheduler.hpp"
#include "neurogenic/timeline/playback.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>
namespace neurogenic {
class RecordingService;
enum class SamplePlaybackMode : std::uint8_t { one_shot, gate, toggle };
struct SamplePlaybackSettings {
    SamplePlaybackMode mode{SamplePlaybackMode::one_shot};
    std::uint8_t choke_group{};
    bool choke_release{true};
    float semitones{}, fine_cents{}, gain{1.0F};
    std::int16_t root_note{-1};
    bool reverse{};
    StretchMode stretch_mode{StretchMode::off};
    StretchQuality stretch_quality{StretchQuality::balanced};
    float stretch_ratio{1.0F};
    float source_beats{}, source_bpm{};
    float attack_seconds{}, decay_seconds{}, sustain{1.0F};
    float release_seconds{0.005F};
    friend bool operator==(const SamplePlaybackSettings &,
                           const SamplePlaybackSettings &) = default;
};
class AudioEngine {
  public:
    static constexpr std::size_t default_voice_capacity = 32;
    static constexpr std::size_t default_note_bank_size = 16;
    static constexpr std::size_t default_sample_version_capacity = 64;
    explicit AudioEngine(
        std::uint32_t rate = 48000,
        std::size_t voice_capacity = default_voice_capacity,
        std::size_t event_staging_capacity = 512,
        std::size_t sample_version_capacity = default_sample_version_capacity);
    ~AudioEngine();
    void
    set_sample(Sample sample); // compatibility: slot 0, mapped from every note
    void set_sample_slot(SampleSlotId id, Sample sample);
    [[nodiscard]] bool
    publish_sample_slot(SampleSlotId id, Sample sample,
                        std::uint64_t *publication = nullptr) noexcept;
    [[nodiscard]] bool
    publish_sample_slot(SampleSlotId id, std::shared_ptr<const Sample> sample,
                        std::uint64_t *publication = nullptr) noexcept;
    [[nodiscard]] bool
    unload_sample_slot(SampleSlotId id,
                       std::uint64_t *publication = nullptr) noexcept;
    [[nodiscard]] bool
    sample_update_applied(std::uint64_t publication) const noexcept {
        return publication != 0 &&
               applied_sample_update_sequence_.load(
                   std::memory_order_acquire) >= publication;
    }
    void reclaim_sample_versions() noexcept;
    [[nodiscard]] bool map_note_to_slot(std::uint8_t note,
                                        SampleSlotId id) noexcept;
    [[nodiscard]] bool has_sample_slot(SampleSlotId id) const noexcept;
    [[nodiscard]] std::size_t sample_slot_count() const noexcept;
    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return rate_; }
    void trigger(std::uint8_t note, float velocity,
                 std::uint32_t frame_offset = 0) noexcept;
    [[nodiscard]] bool trigger_pad(std::uint8_t note, float velocity,
                                   bool note_on = true,
                                   float pitch_offset = 0.0F,
                                   std::uint32_t frame_offset = 0) noexcept;
    [[nodiscard]] bool trigger_slice(SampleSlotId, std::uint64_t publication,
                                     std::uint64_t start_frame,
                                     std::uint64_t end_frame, float slice_gain,
                                     float velocity,
                                     std::uint32_t frame_offset = 0) noexcept;
    [[nodiscard]] bool stop_slice_previews() noexcept;
    [[nodiscard]] bool map_note_to_slice(std::uint8_t note, SampleSlotId,
                                         std::uint64_t publication,
                                         std::uint64_t start_frame,
                                         std::uint64_t end_frame,
                                         float gain = 1.0F,
                                         SamplePlaybackSettings = {}) noexcept;
    [[nodiscard]] bool clear_note_slice(std::uint8_t note) noexcept;
    [[nodiscard]] bool configure_note_playback(std::uint8_t note,
                                               SamplePlaybackSettings) noexcept;
    void trigger_synth(std::uint8_t note, float velocity,
                       bool note_on = true) noexcept;
    [[nodiscard]] bool set_synth_parameter(SynthParameter, float) noexcept;
    [[nodiscard]] bool set_delay_parameter(DelayParameter, float) noexcept;
    [[nodiscard]] bool set_delay_bypass(bool) noexcept;
    [[nodiscard]] bool publish_mixer(const MixerConfiguration &configuration,
                                     std::string &error) noexcept {
        return mixer_.publish(configuration, error);
    }
    [[nodiscard]] MixerState mixer_state() const noexcept {
        return mixer_.state();
    }
    bool queue_pattern(const Pattern &pattern) noexcept;
    bool queue_timeline(const Timeline &, std::string &error);
    void play() noexcept;
    void stop() noexcept;
    void restart() noexcept;
    [[nodiscard]] bool set_fill(bool active) noexcept;
    void process(std::span<float> left, std::span<float> right) noexcept;
    void process_duplex(std::span<float> left, std::span<float> right,
                        std::span<const float> interleaved_input,
                        std::uint16_t input_channels) noexcept;
    void process_io(std::span<float> interleaved_output,
                    std::uint16_t output_channels,
                    std::span<const float> interleaved_input = {},
                    std::uint16_t input_channels = 0) noexcept;
    void set_recording_service(RecordingService *service) noexcept {
        recording_.store(service, std::memory_order_release);
    }
    void capture_external_input(std::span<const float> interleaved,
                                std::uint16_t channels) noexcept;
    void recording_device_stopped() noexcept;
    [[nodiscard]] std::uint64_t callback_frame_position() const noexcept {
        return callback_frame_.load(std::memory_order_acquire);
    }
    void render_wav(const std::filesystem::path &, std::uint64_t frames);
    [[nodiscard]] std::uint64_t frame_position() const noexcept {
        return published_frame_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool playing() const noexcept {
        return published_playing_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool fill_active() const noexcept {
        return published_fill_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t dropped_triggers() const noexcept {
        return dropped_triggers_.load();
    }
    [[nodiscard]] std::uint64_t dropped_patterns() const noexcept {
        return dropped_patterns_.load();
    }
    [[nodiscard]] std::uint64_t stolen_voices() const noexcept {
        return stolen_voices_.load();
    }
    [[nodiscard]] std::uint64_t missing_slot_triggers() const noexcept {
        return missing_slot_triggers_.load();
    }
    [[nodiscard]] std::uint64_t dropped_sample_updates() const noexcept {
        return dropped_sample_updates_.load();
    }
    [[nodiscard]] std::uint64_t reclaimed_sample_versions() const noexcept {
        return reclaimed_sample_versions_.load();
    }
    [[nodiscard]] std::uint64_t dropped_timelines() const noexcept {
        return dropped_timelines_.load();
    }
    [[nodiscard]] std::uint64_t dropped_timeline_events() const noexcept {
        return dropped_timeline_events_.load();
    }
    [[nodiscard]] std::uint64_t timeline_event_overruns() const noexcept {
        return dropped_timeline_events_.load();
    }
    [[nodiscard]] std::uint64_t dropped_legacy_events() const noexcept {
        return dropped_legacy_events_.load();
    }
    [[nodiscard]] std::uint64_t dropped_transport_commands() const noexcept {
        return dropped_transport_commands_.load();
    }
    [[nodiscard]] std::uint64_t reclaimed_timeline_plans() const noexcept {
        return reclaimed_timeline_plans_.load();
    }
    [[nodiscard]] std::size_t voice_capacity() const noexcept {
        return voices_.size();
    }
    [[nodiscard]] std::size_t active_voice_count() const noexcept {
        return callback_monitor_.active_voices();
    }
    [[nodiscard]] AudioCallbackStatistics callback_statistics() const noexcept {
        return callback_monitor_.snapshot();
    }
    void set_performance_monitoring(bool enabled) noexcept {
        callback_monitor_.set_enabled(enabled);
    }
    [[nodiscard]] std::uint32_t buffer_frames() const noexcept {
        return buffer_frames_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] InstrumentState synth_state() const noexcept {
        return synth_.state();
    }
    [[nodiscard]] std::uint64_t dropped_dsp_commands() const noexcept {
        return dropped_dsp_commands_.load();
    }
    [[nodiscard]] std::uint64_t instrument_nanoseconds() const noexcept {
        return instrument_nanoseconds_.load();
    }
    [[nodiscard]] std::uint64_t effect_nanoseconds() const noexcept {
        return effect_nanoseconds_.load();
    }

  private:
    struct Trigger {
        std::uint8_t note{};
        float velocity{};
        std::uint32_t frame_offset{};
        std::uint64_t order{};
        SampleSlotId slot{invalid_sample_slot};
        std::uint64_t publication{}, start_frame{}, end_frame{};
        float slice_gain{1.0F};
        float pitch_offset{};
        bool sliced{}, stop_previews{}, note_on{true}, pad_event{};
    };
    struct SynthTrigger {
        std::uint8_t note{};
        float velocity{};
        bool note_on{};
    };
    struct DspCommand {
        enum class Target : std::uint8_t { synth, delay, delay_bypass };
        Target target{};
        std::uint32_t parameter{};
        float value{};
    };
    struct Voice {
        const Sample *sample{};
        std::size_t sample_version{};
        double position{}, increment{1.0};
        TimeStretchConfiguration stretch_configuration;
        TimeStretchState stretch_state;
        double stretch_duration{};
        std::uint64_t start_frame{}, end_frame{};
        float gain{}, envelope_level{1.0F};
        enum class EnvelopeStage : std::uint8_t {
            attack,
            decay,
            sustain,
            release
        } envelope_stage{EnvelopeStage::sustain};
        float attack_step{}, decay_step{}, release_step{}, release_samples{1},
            sustain{1.0F};
        std::uint64_t age{};
        TrackId track{};
        EventId event{};
        std::uint64_t occurrence{};
        std::uint8_t pitch{};
        std::uint8_t trigger_note{}, choke_group{};
        SamplePlaybackMode mode{SamplePlaybackMode::one_shot};
        bool choke_release{}, reverse{}, stretched{}, timeline_owned{},
            preview{}, active{};
    };
    enum class SampleVersionState : std::uint8_t {
        free,
        queued,
        active,
        retiring,
        retired
    };
    struct SampleVersion {
        std::atomic<SampleVersionState> state{SampleVersionState::free};
        std::atomic<std::uint32_t> id{invalid_sample_slot.value};
        std::shared_ptr<const Sample> sample;
        std::atomic<std::uint64_t> publication{};
    };
    struct SliceTarget {
        SampleSlotId slot{invalid_sample_slot};
        std::uint64_t publication{}, start_frame{}, end_frame{};
        float gain{1.0F};
        SamplePlaybackSettings playback;
        bool enabled{};
    };
    struct SliceMapCommand {
        enum class Type : std::uint8_t { map, clear, settings };
        Type type{Type::map};
        std::uint8_t note{};
        SliceTarget target;
    };
    struct SampleUpdate {
        enum class Type : std::uint8_t { install, unload };
        Type type{};
        SampleSlotId id{};
        std::size_t version{};
        std::uint64_t sequence{};
    };
    class SampleUpdateQueue {
      public:
        explicit SampleUpdateQueue(std::size_t capacity)
            : items_(std::make_unique<SampleUpdate[]>(capacity)),
              capacity_(capacity) {}
        bool push(const SampleUpdate &item) noexcept {
            const auto head = head_.load(std::memory_order_relaxed);
            const auto next = (head + 1) % capacity_;
            if (next == tail_.load(std::memory_order_acquire))
                return false;
            items_[head] = item;
            head_.store(next, std::memory_order_release);
            return true;
        }
        bool pop(SampleUpdate &item) noexcept {
            const auto tail = tail_.load(std::memory_order_relaxed);
            if (tail == head_.load(std::memory_order_acquire))
                return false;
            item = items_[tail];
            tail_.store((tail + 1) % capacity_, std::memory_order_release);
            return true;
        }

      private:
        std::unique_ptr<SampleUpdate[]> items_;
        std::size_t capacity_{};
        std::atomic<std::size_t> head_{}, tail_{};
    };
    enum class PlanState : std::uint8_t { free, queued, active, retired };
    struct PlanSlot {
        std::atomic<PlanState> state{PlanState::free};
        std::unique_ptr<PlaybackPlan> plan;
    };
    enum class TransportCommand : std::uint8_t {
        play,
        stop,
        restart,
        fill_on,
        fill_off
    };
    [[nodiscard]] std::size_t
    find_active_sample(SampleSlotId id) const noexcept;
    void start_voice(std::uint8_t note, float velocity, TrackId track = 0,
                     EventId event = 0, std::uint64_t occurrence = 0,
                     bool timeline_owned = false,
                     const SliceTarget *slice = nullptr, bool preview = false,
                     float pitch_offset = 0.0F) noexcept;
    void stop_timeline_voice(TrackId, EventId,
                             std::uint64_t occurrence) noexcept;
    void release_note(std::uint8_t, TrackId = 0, EventId = 0,
                      std::uint64_t occurrence = 0,
                      bool timeline_owned = false) noexcept;
    void release_voice(Voice &, bool immediate = false) noexcept;
    void stop_voices(bool timeline_only = false) noexcept;
    void reclaim_retired_plans() noexcept;
    void reclaim_sample_versions_unlocked() noexcept;
    std::uint32_t rate_;
    Scheduler scheduler_;
    TimelineScheduler timeline_scheduler_;
    PurestGain effect_;
    SpscQueue<Trigger, 65> triggers_;
    SpscQueue<SynthTrigger, 65> synth_triggers_;
    SpscQueue<DspCommand, 129> dsp_commands_;
    SpscQueue<Pattern, 9> patterns_;
    static constexpr std::size_t timeline_plan_slot_count = 4;
    SpscQueue<std::uint8_t, 4> timelines_;
    std::array<PlanSlot, timeline_plan_slot_count> timeline_plans_{};
    SpscQueue<TransportCommand, 16> transport_commands_;
    SpscQueue<SliceMapCommand, 65> slice_map_commands_;
    std::atomic<std::uint64_t> dropped_triggers_{}, dropped_patterns_{},
        dropped_timelines_{}, dropped_timeline_events_{},
        dropped_legacy_events_{}, dropped_transport_commands_{},
        reclaimed_timeline_plans_{}, published_frame_{}, next_order_{},
        stolen_voices_{}, missing_slot_triggers_{}, dropped_sample_updates_{},
        reclaimed_sample_versions_{};
    std::atomic<std::uint64_t> applied_sample_update_sequence_{};
    std::atomic<std::uint64_t> dropped_dsp_commands_{};
    std::uint64_t next_sample_update_sequence_{};
    std::mutex sample_update_producer_mutex_;
    SampleUpdateQueue sample_updates_;
    std::unique_ptr<SampleVersion[]> sample_versions_;
    std::size_t sample_version_capacity_{};
    std::array<SampleSlotId, 128> note_slots_{};
    std::array<SliceTarget, 128> note_slices_{};
    std::vector<Voice> voices_;
    std::vector<ScheduledEvent> legacy_events_;
    std::vector<TimelineScheduledEvent> timeline_events_;
    int active_timeline_slot_{-1};
    std::array<Trigger, 64> pending_triggers_{};
    std::size_t pending_trigger_count_{};
    std::uint64_t next_voice_age_{};
    std::uint64_t transport_frame_{};
    float project_tempo_bpm_{120.0F};
    bool transport_playing_{true};
    std::atomic<bool> processing_started_{};
    std::atomic<bool> published_playing_{true};
    std::atomic<bool> published_fill_{};
    CallbackMonitor callback_monitor_;
    std::atomic<std::uint32_t> buffer_frames_{};
    std::atomic<std::uint64_t> instrument_nanoseconds_{}, effect_nanoseconds_{};
    std::atomic<RecordingService *> recording_{};
    std::atomic<std::uint64_t> callback_frame_{};
    SubtractiveSynth synth_;
    StereoDelay delay_;
    Mixer mixer_;
    const float *duplex_input_{};
    std::size_t duplex_input_samples_{};
    std::uint16_t duplex_input_channels_{};
    float *physical_output_{};
    std::size_t physical_output_samples_{};
    std::uint8_t physical_output_channels_{};
};
} // namespace neurogenic
