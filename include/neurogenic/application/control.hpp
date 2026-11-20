#pragma once
#include "neurogenic/audio/recording.hpp"
#include "neurogenic/audio/slicing.hpp"
#include "neurogenic/performance/monitor.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace neurogenic {
struct NormalizedXY {
    double x{}, y{};
    friend bool operator==(const NormalizedXY &,
                           const NormalizedXY &) = default;
};
using ApplicationValue = std::variant<std::monostate, bool, std::int64_t,
                                      double, std::string, NormalizedXY>;
enum class ValueType : std::uint8_t { none, boolean, integer, real, text, xy };
struct ArgumentSpec {
    std::string name;
    ValueType type{ValueType::none};
    bool required{true};
};
struct ActionDescriptor {
    std::string id, name, description, category;
    std::vector<ArgumentSpec> arguments;
    bool available{};
    std::string context, unavailable_reason;
};
class ActionRegistry {
  public:
    ActionRegistry();
    bool register_action(ActionDescriptor, std::string &error);
    [[nodiscard]] const ActionDescriptor *find(std::string_view) const noexcept;
    [[nodiscard]] const std::vector<ActionDescriptor> &
    actions() const noexcept {
        return actions_;
    }
    bool validate(std::string_view,
                  const std::map<std::string, ApplicationValue> &,
                  std::string &error) const;

  private:
    std::vector<ActionDescriptor> actions_;
};

enum class ParameterType : std::uint8_t { boolean, integer, real, enumeration };
struct ParameterDescriptor {
    std::string id, name, unit;
    ParameterType type{ParameterType::real};
    ApplicationValue minimum, maximum, default_value, current_value;
    double step{1};
    bool continuous{}, automatable{}, modulatable{};
    std::vector<std::string> choices;
    ApplicationValue effective_value{};
};
class ParameterRegistry {
  public:
    bool register_parameter(ParameterDescriptor, std::string &error);
    bool set(std::string_view, const ApplicationValue &, std::string &error);
    bool set_normalized(std::string_view, double, std::string &error);
    bool adjust(std::string_view, double steps, std::string &error);
    bool reset(std::string_view, std::string &error);
    [[nodiscard]] std::optional<ParameterDescriptor>
        get(std::string_view) const;
    [[nodiscard]] std::vector<ParameterDescriptor> parameters() const;

  private:
    bool valid_value(const ParameterDescriptor &, const ApplicationValue &,
                     std::string &) const;
    mutable std::mutex mutex_;
    std::map<std::string, ParameterDescriptor, std::less<>> values_;
};

enum class PadMode : std::uint8_t { step, pad, levels, notes };
enum class StepResolution : std::uint8_t {
    quarter,
    eighth,
    sixteenth,
    thirty_second,
    quarter_triplet,
    eighth_triplet,
    sixteenth_triplet,
    thirty_second_triplet
};
enum class Modifier : std::uint8_t { shift, select };
struct ModifierState {
    bool shift{}, select{}, shift_latched{}, select_latched{};
};
struct TrackSummary {
    TrackId id{};
    std::string name;
    bool muted{};
    MusicalTick pattern_length{};
    std::uint8_t target_note{36};
    std::string instrument_id{sample_instrument_id};
    std::vector<MusicalEvent> events;
    InstrumentInstanceId instrument_instance_id{default_synth_instance_id};
};
struct SelectedEventState {
    TrackId track_id{};
    EventId event_id{};
    std::int64_t step{};
    MusicalTick tick{}, timing_offset{};
    std::uint8_t pitch{};
    float velocity{};
    MusicalTick duration{};
    std::size_t index_at_step{}, events_at_step{};
    float trigger_probability{1.0F};
    TriggerCondition trigger_condition{TriggerCondition::always};
    std::uint16_t condition_n{1}, condition_m{1};
    std::uint8_t ratchet_count{1};
};
struct StepParameterLockState {
    EventId event_id{};
    std::string parameter_id;
    float value{};
};
struct SliceState {
    SliceId id{};
    std::string name;
    std::uint64_t start_frame{}, end_frame{};
    float gain{1.0F};
    std::optional<std::uint8_t> root_note;
};
struct ApplicationState {
    std::uint64_t revision{};
    bool playing{};
    std::uint64_t transport_frame{};
    MusicalTick transport_tick{};
    double tempo_bpm{120};
    std::uint16_t time_signature_numerator{4}, time_signature_denominator{4};
    TrackId selected_track{};
    SampleSlotId selected_sample_slot{};
    SliceId selected_slice{};
    std::size_t selected_pad{};
    SamplePlaybackSettings selected_pad_settings;
    float effective_stretch_ratio{1.0F};
    std::uint32_t stretch_latency_frames{};
    std::string selected_pad_property{"gain"};
    std::array<SamplePlaybackSettings, 16> pad_settings{};
    std::uint64_t slice_revision{}, sample_frames{};
    std::uint32_t sample_rate{};
    std::uint16_t sample_channels{};
    std::vector<SliceState> slices;
    std::array<std::optional<SliceId>, 16> slice_pad_assignments{};
    std::vector<WaveformPeak> waveform_peaks;
    std::uint64_t selected_pattern{};
    std::int64_t selected_step{};
    std::uint64_t step_page{};
    StepResolution step_resolution{StepResolution::sixteenth};
    MusicalTick step_ticks{ticks_per_quarter / 4};
    float step_velocity{1.0F};
    MusicalTick step_duration{ticks_per_quarter / 4};
    std::optional<SelectedEventState> selected_event;
    std::string selected_step_property{"velocity"};
    std::string selected_lock_parameter{"synth.cutoff"};
    std::vector<StepParameterLockState> selected_step_locks;
    bool edit_group_active{}, can_undo{}, can_redo{};
    bool fill_active{}, fill_latched{};
    std::uint64_t generation_seed{};
    std::size_t undo_depth{}, redo_depth{}, history_capacity{};
    PadMode pad_mode{PadMode::step};
    std::string controller_context{"sequencer"};
    std::string parameter_page{"main"}, focused_element{"transport"};
    ModifierState modifiers;
    std::vector<std::string> available_actions;
    std::vector<ParameterDescriptor> parameters;
    std::vector<TrackSummary> tracks;
    std::vector<SampleSlotStatus> sample_slots;
    std::uint64_t timeline_revision{};
    PerformanceSnapshot performance;
    std::string project_name{"Untitled"};
    std::filesystem::path project_path;
    bool project_dirty{}, project_loading{}, project_saving{};
    std::string project_error;
    std::vector<std::filesystem::path> missing_assets;
    RecordingStatus recording;
    MixerState mixer;
    std::size_t selected_mixer_channel{};
    std::size_t selected_external_fx_loop{};
    std::vector<RoutingScene> routing_scenes;
    std::uint64_t active_routing_scene{};
    ModulationConfiguration modulation;
    ModulationSnapshot modulation_runtime;
    ModulationRouteId selected_modulation_route{};
    MidiProjectConfiguration midi;
    std::uint64_t midi_dropped{}, midi_sent{};
    ExternalClockState external_midi_clock;
    std::array<bool, maximum_midi_ports> midi_output_connected{};
    std::array<bool, maximum_midi_ports> midi_input_connected{};
};
struct ApplicationCommand {
    std::string action_id;
    std::map<std::string, ApplicationValue> arguments;
    std::string source_id;
    std::uint64_t source_sequence{};
};
struct CommandResult {
    bool accepted{};
    std::uint64_t sequence{}, state_revision{};
    std::string error;
};

class ApplicationService {
  public:
    static constexpr std::size_t default_queue_capacity = 256;
    ApplicationService(AudioEngine &, TimelineService &, SampleManager &,
                       std::size_t queue_capacity = default_queue_capacity);
    ~ApplicationService();
    [[nodiscard]] const ActionRegistry &actions() const noexcept {
        return actions_;
    }
    [[nodiscard]] ParameterRegistry &parameters() noexcept {
        return parameters_;
    }
    [[nodiscard]] bool submit(ApplicationCommand) noexcept;
    [[nodiscard]] std::vector<CommandResult> dispatch_pending();
    [[nodiscard]] CommandResult execute(const ApplicationCommand &);
    [[nodiscard]] ApplicationState snapshot() const;
    [[nodiscard]] std::uint64_t rejected_commands() const noexcept;
    [[nodiscard]] SliceService &slices() noexcept { return slices_; }
    [[nodiscard]] RecordingService &recording() noexcept { return recording_; }
    [[nodiscard]] MidiOutputScheduler &midi_output() noexcept {
        return midi_output_;
    }
    [[nodiscard]] bool receive_midi(const MidiPacket &packet) noexcept {
        return engine_.receive_midi(packet);
    }
    void set_midi_output_connections(
        const std::array<bool, maximum_midi_ports> &connections);
    void set_midi_input_connections(
        const std::array<bool, maximum_midi_ports> &connections);

  private:
    CommandResult execute_locked(const ApplicationCommand &, std::uint64_t);
    void synchronize_locked() const;
    void changed_locked();
    AudioEngine &engine_;
    TimelineService &timeline_;
    SampleManager &samples_;
    mutable SliceService slices_;
    mutable RecordingService recording_;
    RecordingConfiguration recording_configuration_;
    std::uint64_t recording_fixed_bars_{};
    mutable MixerConfiguration mixer_configuration_;
    ModulationConfiguration modulation_configuration_;
    MidiOutputScheduler midi_output_;
    MidiRouter midi_router_;
    ModulationRouteId next_modulation_route_id_{1};
    std::map<InstrumentInstanceId, std::map<std::string, double>>
        synth_instance_parameters_;
    ActionRegistry actions_;
    ParameterRegistry parameters_;
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    mutable ApplicationState state_;
    std::map<TrackId, std::uint8_t> track_targets_;
    std::deque<std::pair<std::uint64_t, ApplicationCommand>> queue_;
    std::uint64_t next_sequence_{1}, rejected_{};
    std::optional<TimelineTransaction> edit_group_;
    mutable std::optional<EventId> selected_event_id_;
    bool fill_momentary_{};
    Project project_;
    mutable SystemMonitor system_monitor_;
};

struct TextEntryState {
    std::string text;
    std::size_t cursor{};
    char selected{'A'};
    bool active{}, committed{}, discarded{};
};
class TextEntryModel {
  public:
    [[nodiscard]] static constexpr std::string_view characters() noexcept {
        return " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";
    }
    void begin(std::string initial = {});
    void direct(std::string_view);
    void select_relative(int);
    void select(char);
    void insert();
    void erase_or_cancel();
    void move_cursor(int);
    void commit();
    void discard();
    [[nodiscard]] const TextEntryState &state() const noexcept {
        return state_;
    }

  private:
    TextEntryState state_;
};

struct XYBehavior {
    bool spring_to_center{}, latching{}, relative{}, invert_x{}, invert_y{};
    double scale_x{1}, scale_y{1};
};
class XYControl {
  public:
    explicit XYControl(XYBehavior behavior = {}) : behavior_(behavior) {}
    [[nodiscard]] NormalizedXY apply(double x, double y);
    void release();
    [[nodiscard]] NormalizedXY value() const noexcept { return value_; }

  private:
    XYBehavior behavior_;
    NormalizedXY value_{0.5, 0.5};
};
[[nodiscard]] const char *pad_mode_name(PadMode) noexcept;
[[nodiscard]] const char *step_resolution_name(StepResolution) noexcept;
[[nodiscard]] MusicalTick step_resolution_ticks(StepResolution) noexcept;
[[nodiscard]] std::optional<StepResolution>
    parse_step_resolution(std::string_view) noexcept;
} // namespace neurogenic
