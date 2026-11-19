#pragma once
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
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
struct NormalizedXY {double x{},y{};friend bool operator==(const NormalizedXY&,const NormalizedXY&)=default;};
using ApplicationValue=std::variant<std::monostate,bool,std::int64_t,double,std::string,NormalizedXY>;
enum class ValueType:std::uint8_t{none,boolean,integer,real,text,xy};
struct ArgumentSpec {std::string name;ValueType type{ValueType::none};bool required{true};};
struct ActionDescriptor {
  std::string id,name,description,category;
  std::vector<ArgumentSpec> arguments;
  bool available{};
  std::string context,unavailable_reason;
};
class ActionRegistry {
public:
  ActionRegistry();
  bool register_action(ActionDescriptor,std::string& error);
  [[nodiscard]] const ActionDescriptor* find(std::string_view) const noexcept;
  [[nodiscard]] const std::vector<ActionDescriptor>& actions() const noexcept{return actions_;}
  bool validate(std::string_view,const std::map<std::string,ApplicationValue>&,std::string& error) const;
private:std::vector<ActionDescriptor> actions_;
};

enum class ParameterType:std::uint8_t{boolean,integer,real,enumeration};
struct ParameterDescriptor {
  std::string id,name,unit;
  ParameterType type{ParameterType::real};
  ApplicationValue minimum,maximum,default_value,current_value;
  double step{1};bool continuous{},automatable{},modulatable{};
  std::vector<std::string> choices;
};
class ParameterRegistry {
public:
  bool register_parameter(ParameterDescriptor,std::string& error);
  bool set(std::string_view,const ApplicationValue&,std::string& error);
  bool set_normalized(std::string_view,double,std::string& error);
  bool adjust(std::string_view,double steps,std::string& error);
  bool reset(std::string_view,std::string& error);
  [[nodiscard]] std::optional<ParameterDescriptor> get(std::string_view) const;
  [[nodiscard]] std::vector<ParameterDescriptor> parameters() const;
private:bool valid_value(const ParameterDescriptor&,const ApplicationValue&,std::string&) const;mutable std::mutex mutex_;std::map<std::string,ParameterDescriptor,std::less<>> values_;
};

enum class PadMode:std::uint8_t{step,pad,levels,notes};
enum class Modifier:std::uint8_t{shift,select};
struct ModifierState {bool shift{},select{},shift_latched{},select_latched{};};
struct TrackSummary {TrackId id{};std::string name;bool muted{};MusicalTick pattern_length{};std::vector<MusicalTick> event_ticks;};
struct ApplicationState {
  std::uint64_t revision{};
  bool playing{};std::uint64_t transport_frame{};
  double tempo_bpm{120};std::uint16_t time_signature_numerator{4},time_signature_denominator{4};
  TrackId selected_track{};SampleSlotId selected_sample_slot{};
  std::uint64_t selected_pattern{};std::int64_t selected_step{};std::uint64_t step_page{};
  PadMode pad_mode{PadMode::step};std::string parameter_page{"main"},focused_element{"transport"};
  ModifierState modifiers;std::vector<std::string> available_actions;
  std::vector<ParameterDescriptor> parameters;
  std::vector<TrackSummary> tracks;
  std::vector<SampleSlotStatus> sample_slots;std::uint64_t timeline_revision{};
};
struct ApplicationCommand {std::string action_id;std::map<std::string,ApplicationValue> arguments;std::string source_id;std::uint64_t source_sequence{};};
struct CommandResult {bool accepted{};std::uint64_t sequence{},state_revision{};std::string error;};

class ApplicationService {
public:
  static constexpr std::size_t default_queue_capacity=256;
  ApplicationService(AudioEngine&,TimelineService&,SampleManager&,std::size_t queue_capacity=default_queue_capacity);
  [[nodiscard]] const ActionRegistry& actions() const noexcept{return actions_;}
  [[nodiscard]] ParameterRegistry& parameters() noexcept{return parameters_;}
  [[nodiscard]] bool submit(ApplicationCommand) noexcept;
  [[nodiscard]] std::vector<CommandResult> dispatch_pending();
  [[nodiscard]] CommandResult execute(const ApplicationCommand&);
  [[nodiscard]] ApplicationState snapshot() const;
  [[nodiscard]] std::uint64_t rejected_commands() const noexcept;
private:
  CommandResult execute_locked(const ApplicationCommand&,std::uint64_t);
  void synchronize_locked() const;void changed_locked();
  AudioEngine& engine_;TimelineService& timeline_;SampleManager& samples_;ActionRegistry actions_;ParameterRegistry parameters_;const std::size_t capacity_;
  mutable std::mutex mutex_;mutable ApplicationState state_;std::deque<std::pair<std::uint64_t,ApplicationCommand>> queue_;std::uint64_t next_sequence_{1},rejected_{};
};

struct TextEntryState {std::string text;std::size_t cursor{};char selected{'A'};bool active{},committed{},discarded{};};
class TextEntryModel {
public:
  void begin(std::string initial={});void direct(std::string_view);void select_relative(int);void insert();void erase_or_cancel();void move_cursor(int);void commit();void discard();
  [[nodiscard]] const TextEntryState& state() const noexcept{return state_;}
private:TextEntryState state_;
};

struct XYBehavior {bool spring_to_center{},latching{},relative{},invert_x{},invert_y{};double scale_x{1},scale_y{1};};
class XYControl {
public:
  explicit XYControl(XYBehavior behavior={}):behavior_(behavior){}
  [[nodiscard]] NormalizedXY apply(double x,double y);void release();[[nodiscard]] NormalizedXY value() const noexcept{return value_;}
private:XYBehavior behavior_;NormalizedXY value_{0.5,0.5};
};
[[nodiscard]] const char* pad_mode_name(PadMode) noexcept;
}
