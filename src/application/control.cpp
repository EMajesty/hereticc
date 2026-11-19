#include "neurogenic/application/control.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace neurogenic {
namespace {
ActionDescriptor action(std::string id,std::string name,std::string category,std::vector<ArgumentSpec> arguments={},bool available=true,std::string reason={}){auto description=name+" application action";return{std::move(id),std::move(name),std::move(description),std::move(category),std::move(arguments),available,{},std::move(reason)};}
template<class T>const T* argument(const ApplicationCommand& command,std::string_view name){const auto found=command.arguments.find(std::string(name));return found==command.arguments.end()?nullptr:std::get_if<T>(&found->second);}
bool is_numeric(const ApplicationValue& value){return std::holds_alternative<std::int64_t>(value)||std::holds_alternative<double>(value);}
double number(const ApplicationValue& value){if(const auto* integer=std::get_if<std::int64_t>(&value))return static_cast<double>(*integer);return std::get<double>(value);}
}

ActionRegistry::ActionRegistry(){std::string error;
  const auto add=[&](ActionDescriptor value){if(!register_action(std::move(value),error))throw std::logic_error(error);};
  add(action("transport.play.v1","Play","Transport"));add(action("transport.stop.v1","Stop","Transport"));add(action("transport.restart.v1","Restart","Transport"));
  add(action("track.select.v1","Select track","Tracks",{{"track_id",ValueType::integer}}));add(action("track.next.v1","Next track","Tracks"));add(action("track.previous.v1","Previous track","Tracks"));add(action("track.mute-toggle.v1","Toggle track mute","Tracks"));
  add(action("pattern.select.v1","Select pattern","Sequencer",{{"pattern_id",ValueType::integer}}));add(action("step.select.v1","Select step","Sequencer",{{"step",ValueType::integer}}));add(action("step.toggle.v1","Toggle step","Sequencer",{{"step",ValueType::integer,false},{"velocity",ValueType::real,false},{"duration_ticks",ValueType::integer,false}}));
  add(action("step.page-next.v1","Next step page","Sequencer"));add(action("step.page-previous.v1","Previous step page","Sequencer"));add(action("step.resolution-set.v1","Set step resolution","Sequencer",{{"resolution",ValueType::text}}));add(action("track.note-target-set.v1","Set track note target","Tracks",{{"note",ValueType::integer}}));
  add(action("sample.slot-select.v1","Select sample slot","Samples",{{"slot_id",ValueType::integer}}));add(action("sample.load.v1","Load sample","Samples",{{"slot_id",ValueType::integer},{"path",ValueType::text}}));add(action("sample.replace.v1","Replace sample","Samples",{{"slot_id",ValueType::integer},{"path",ValueType::text}}));add(action("sample.unload.v1","Unload sample","Samples",{{"slot_id",ValueType::integer}}));
  add(action("edit.undo.v1","Undo","Editing",{},false,"undo history is not implemented"));add(action("edit.redo.v1","Redo","Editing",{},false,"redo history is not implemented"));
  add(action("timeline.generate.v1","Generate timeline","Generation",{},false,"a generator choice and transaction payload are required"));add(action("timeline.mutate-track.v1","Mutate track","Generation",{},false,"mutation settings are not represented by this initial command"));
  add(action("pad.mode-select.v1","Select pad mode","Performance",{{"mode",ValueType::text}}));add(action("navigation.focus-next.v1","Focus next","Navigation"));add(action("navigation.focus-previous.v1","Focus previous","Navigation"));add(action("navigation.adjust.v1","Adjust focused item","Navigation",{{"steps",ValueType::real}}));add(action("navigation.confirm.v1","Confirm","Navigation"));add(action("navigation.cancel.v1","Cancel","Navigation"));
  add(action("text.input.v1","Text input","Text",{{"text",ValueType::text}}));add(action("modulation.xy.v1","X/Y control","Modulation",{{"position",ValueType::xy}}));
  add(action("parameter.set-normalized.v1","Set parameter","Parameters",{{"parameter_id",ValueType::text},{"value",ValueType::real}}));add(action("parameter.adjust.v1","Adjust parameter","Parameters",{{"parameter_id",ValueType::text},{"steps",ValueType::real}}));add(action("parameter.reset.v1","Reset parameter","Parameters",{{"parameter_id",ValueType::text}}));
  add(action("modifier.shift.v1","Shift","Modifiers",{{"active",ValueType::boolean},{"latched",ValueType::boolean,false}}));add(action("modifier.select.v1","Select modifier","Modifiers",{{"active",ValueType::boolean},{"latched",ValueType::boolean,false}}));
}
bool ActionRegistry::register_action(ActionDescriptor value,std::string& error){if(value.id.empty()||value.name.empty()){error="action ID and name are required";return false;}if(find(value.id)){error="duplicate action ID: "+value.id;return false;}actions_.push_back(std::move(value));error.clear();return true;}
const ActionDescriptor* ActionRegistry::find(std::string_view id) const noexcept{for(const auto& value:actions_)if(value.id==id)return&value;return nullptr;}
bool ActionRegistry::validate(std::string_view id,const std::map<std::string,ApplicationValue>& values,std::string& error) const{const auto* descriptor=find(id);if(!descriptor){error="unknown action: "+std::string(id);return false;}if(!descriptor->available){error=descriptor->unavailable_reason.empty()?"action is unavailable":descriptor->unavailable_reason;return false;}for(const auto& spec:descriptor->arguments){const auto found=values.find(spec.name);if(found==values.end()){if(spec.required){error="missing argument: "+spec.name;return false;}continue;}const auto type=found->second.index();const auto expected=static_cast<std::size_t>(spec.type);if(type!=expected){error="invalid type for argument: "+spec.name;return false;}}for(const auto& [name,value]:values){(void)value;if(std::none_of(descriptor->arguments.begin(),descriptor->arguments.end(),[&](const auto& spec){return spec.name==name;})){error="unexpected argument: "+name;return false;}}error.clear();return true;}

bool ParameterRegistry::valid_value(const ParameterDescriptor& parameter,const ApplicationValue& value,std::string& error) const{switch(parameter.type){case ParameterType::boolean:if(!std::holds_alternative<bool>(value)){error="parameter requires a boolean";return false;}break;case ParameterType::integer:case ParameterType::enumeration:if(!std::holds_alternative<std::int64_t>(value)){error="parameter requires an integer";return false;}break;case ParameterType::real:if(!is_numeric(value)){error="parameter requires a number";return false;}break;}if(parameter.type!=ParameterType::boolean){const auto current=number(value),minimum=number(parameter.minimum),maximum=number(parameter.maximum);if(!std::isfinite(current)||current<minimum||current>maximum){error="parameter value is outside its range";return false;}if(parameter.type==ParameterType::enumeration&&static_cast<std::size_t>(std::get<std::int64_t>(value))>=parameter.choices.size()){error="enumeration value has no choice";return false;}}error.clear();return true;}
bool ParameterRegistry::register_parameter(ParameterDescriptor value,std::string& error){std::lock_guard lock(mutex_);if(value.id.empty()||value.name.empty()||values_.contains(value.id)){error="parameter ID/name is empty or duplicated";return false;}if(value.step<=0||!std::isfinite(value.step)){error="parameter step must be positive";return false;}if(!valid_value(value,value.default_value,error)||!valid_value(value,value.current_value,error))return false;values_.emplace(value.id,std::move(value));error.clear();return true;}
bool ParameterRegistry::set(std::string_view id,const ApplicationValue& value,std::string& error){std::lock_guard lock(mutex_);auto found=values_.find(id);if(found==values_.end()){error="unknown parameter";return false;}if(!valid_value(found->second,value,error))return false;found->second.current_value=value;return true;}
bool ParameterRegistry::set_normalized(std::string_view id,double normalized,std::string& error){std::lock_guard lock(mutex_);auto found=values_.find(id);if(found==values_.end()){error="unknown parameter";return false;}if(!std::isfinite(normalized)||normalized<0||normalized>1){error="normalized parameter value must be 0..1";return false;}auto& parameter=found->second;if(parameter.type==ParameterType::boolean)parameter.current_value=normalized>=0.5;else{const auto scaled=number(parameter.minimum)+(number(parameter.maximum)-number(parameter.minimum))*normalized;if(parameter.type==ParameterType::real)parameter.current_value=scaled;else parameter.current_value=static_cast<std::int64_t>(std::llround(scaled));}error.clear();return true;}
bool ParameterRegistry::adjust(std::string_view id,double steps,std::string& error){std::lock_guard lock(mutex_);auto found=values_.find(id);if(found==values_.end()){error="unknown parameter";return false;}auto& parameter=found->second;if(parameter.type==ParameterType::boolean){if(steps!=0)parameter.current_value=!std::get<bool>(parameter.current_value);error.clear();return true;}if(!std::isfinite(steps)){error="relative adjustment must be finite";return false;}const auto minimum=number(parameter.minimum),maximum=number(parameter.maximum);auto updated=std::clamp(number(parameter.current_value)+steps*parameter.step,minimum,maximum);if(parameter.type!=ParameterType::real)parameter.current_value=static_cast<std::int64_t>(std::llround(updated));else parameter.current_value=updated;error.clear();return true;}
bool ParameterRegistry::reset(std::string_view id,std::string& error){std::lock_guard lock(mutex_);auto found=values_.find(id);if(found==values_.end()){error="unknown parameter";return false;}found->second.current_value=found->second.default_value;error.clear();return true;}
std::optional<ParameterDescriptor> ParameterRegistry::get(std::string_view id) const{std::lock_guard lock(mutex_);const auto found=values_.find(id);return found==values_.end()?std::nullopt:std::optional<ParameterDescriptor>(found->second);}
std::vector<ParameterDescriptor> ParameterRegistry::parameters() const{std::lock_guard lock(mutex_);std::vector<ParameterDescriptor> result;result.reserve(values_.size());for(const auto& [id,value]:values_){(void)id;result.push_back(value);}return result;}

ApplicationService::ApplicationService(AudioEngine& engine,TimelineService& timeline,SampleManager& samples,std::size_t capacity):engine_(engine),timeline_(timeline),samples_(samples),capacity_(capacity){if(capacity_==0)throw std::invalid_argument("application command capacity must be positive");std::string error;parameters_.register_parameter({"transport.tempo","Tempo","BPM",ParameterType::real,20.0,300.0,120.0,120.0,0.1,true,false,false,{}},error);parameters_.register_parameter({"performance.xy.x","X axis","",ParameterType::real,0.0,1.0,0.5,0.5,0.01,true,false,true,{}},error);parameters_.register_parameter({"performance.xy.y","Y axis","",ParameterType::real,0.0,1.0,0.5,0.5,0.01,true,false,true,{}},error);state_.step_ticks=step_resolution_ticks(state_.step_resolution);state_.step_duration=state_.step_ticks;}
bool ApplicationService::submit(ApplicationCommand command) noexcept{try{std::lock_guard lock(mutex_);if(queue_.size()>=capacity_){++rejected_;return false;}queue_.emplace_back(next_sequence_++,std::move(command));return true;}catch(...){return false;}}
std::vector<CommandResult> ApplicationService::dispatch_pending(){std::vector<CommandResult> result;for(;;){std::pair<std::uint64_t,ApplicationCommand> item;{std::lock_guard lock(mutex_);if(queue_.empty())break;item=std::move(queue_.front());queue_.pop_front();}result.push_back(execute_locked(item.second,item.first));}return result;}
CommandResult ApplicationService::execute(const ApplicationCommand& command){std::uint64_t sequence;{std::lock_guard lock(mutex_);sequence=next_sequence_++;}return execute_locked(command,sequence);}
CommandResult ApplicationService::execute_locked(const ApplicationCommand& command,std::uint64_t sequence){std::string error;if(!actions_.validate(command.action_id,command.arguments,error))return{false,sequence,snapshot().revision,std::move(error)};if(command.action_id=="navigation.confirm.v1"&&snapshot().focused_element=="steps"){ApplicationCommand toggle;toggle.action_id="step.toggle.v1";toggle.source_id=command.source_id;toggle.source_sequence=command.source_sequence;return execute_locked(toggle,sequence);}bool changed=false;
  if(command.action_id=="transport.play.v1"){engine_.play();changed=true;}else if(command.action_id=="transport.stop.v1"){engine_.stop();changed=true;}else if(command.action_id=="transport.restart.v1"){engine_.restart();changed=true;}
  else if(command.action_id=="sample.load.v1"||command.action_id=="sample.replace.v1"){const auto slot=*argument<std::int64_t>(command,"slot_id");if(slot<0||slot>std::numeric_limits<std::uint32_t>::max())error="sample slot is outside its range";else if(!samples_.load(SampleSlotId{static_cast<std::uint32_t>(slot)},*argument<std::string>(command,"path")))error="sample command queue rejected the request";else changed=true;}
  else if(command.action_id=="sample.unload.v1"){const auto slot=*argument<std::int64_t>(command,"slot_id");if(slot<0||slot>std::numeric_limits<std::uint32_t>::max())error="sample slot is outside its range";else if(!samples_.unload(SampleSlotId{static_cast<std::uint32_t>(slot)}))error="sample command queue rejected the request";else changed=true;}
  else {std::lock_guard lock(mutex_);synchronize_locked();
    if(command.action_id=="track.select.v1"){const auto id=*argument<std::int64_t>(command,"track_id");const auto timeline=timeline_.snapshot();if(id<=0||std::none_of(timeline.timeline.tracks.begin(),timeline.timeline.tracks.end(),[&](const Track& track){return track.id==static_cast<TrackId>(id);})){error="track does not exist";}else{state_.selected_track=static_cast<TrackId>(id);changed_locked();}}
    else if(command.action_id=="track.next.v1"||command.action_id=="track.previous.v1"){const auto timeline=timeline_.snapshot();if(timeline.timeline.tracks.empty())error="no tracks are available";else{auto found=std::find_if(timeline.timeline.tracks.begin(),timeline.timeline.tracks.end(),[&](const Track& track){return track.id==state_.selected_track;});auto index=found==timeline.timeline.tracks.end()?0:static_cast<std::size_t>(found-timeline.timeline.tracks.begin());if(command.action_id=="track.next.v1")index=(index+1)%timeline.timeline.tracks.size();else index=(index+timeline.timeline.tracks.size()-1)%timeline.timeline.tracks.size();state_.selected_track=timeline.timeline.tracks[index].id;changed_locked();}}
    else if(command.action_id=="track.mute-toggle.v1"){auto transaction=timeline_.edit();auto* track=transaction.find_track(state_.selected_track);if(!track)error="selected track does not exist";else if(!transaction.set_track_muted(track->id,!track->muted,error)||!timeline_.commit(std::move(transaction),TimelineCommitPolicy::next_audio_callback,error)){}else{changed_locked();}}
    else if(command.action_id=="pattern.select.v1"){const auto id=*argument<std::int64_t>(command,"pattern_id");if(id<0)error="pattern ID must be nonnegative";else{state_.selected_pattern=static_cast<std::uint64_t>(id);changed_locked();}}
    else if(command.action_id=="step.select.v1"){const auto step=*argument<std::int64_t>(command,"step");if(step<0)error="step must be nonnegative";else{state_.selected_step=step;state_.step_page=static_cast<std::uint64_t>(step)/16;changed_locked();}
    } else if (command.action_id == "step.page-next.v1" ||
               command.action_id == "step.page-previous.v1") {
        const auto snapshot = timeline_.snapshot();
        const auto found = std::find_if(
            snapshot.timeline.tracks.begin(), snapshot.timeline.tracks.end(),
            [&](const Track &track) {
                return track.id == state_.selected_track;
            });
        const auto steps =
            found == snapshot.timeline.tracks.end()
                ? 0
                : static_cast<std::uint64_t>(
                      (found->pattern.length + state_.step_ticks - 1) /
                      state_.step_ticks);
        if (steps == 0)
            error = "selected track has no editable steps";
        else {
            const auto pages = (steps + 15) / 16;
            if (command.action_id == "step.page-next.v1")
                state_.step_page = (state_.step_page + 1) % pages;
            else
                state_.step_page = (state_.step_page + pages - 1) % pages;
            state_.selected_step = static_cast<std::int64_t>(
                std::min<std::uint64_t>(state_.step_page * 16, steps - 1));
            changed_locked();
        }
    } else if (command.action_id == "step.resolution-set.v1") {
        const auto resolution = parse_step_resolution(
            *argument<std::string>(command, "resolution"));
        if (!resolution)
            error = "unknown step resolution";
        else {
            state_.step_resolution = *resolution;
            state_.step_ticks = step_resolution_ticks(*resolution);
            state_.step_duration = state_.step_ticks;
            state_.step_page = 0;
            state_.selected_step = 0;
            changed_locked();
        }
    } else if (command.action_id == "track.note-target-set.v1") {
        const auto note = *argument<std::int64_t>(command, "note");
        const auto snapshot = timeline_.snapshot();
        if (note < 0 || note > 127)
            error = "track note target must be 0..127";
        else if (std::none_of(snapshot.timeline.tracks.begin(),
                              snapshot.timeline.tracks.end(),
                              [&](const Track &track) {
                                  return track.id == state_.selected_track;
                              }))
            error = "selected track does not exist";
        else {
            track_targets_[state_.selected_track] =
                static_cast<std::uint8_t>(note);
            changed_locked();
        }
    } else if (command.action_id == "step.toggle.v1") {
        auto transaction = timeline_.edit();
        auto *track = transaction.find_track(state_.selected_track);
        const auto requested = argument<std::int64_t>(command, "step");
        const auto step = requested ? *requested : state_.selected_step;
        const auto velocity = argument<double>(command, "velocity");
        const auto duration = argument<std::int64_t>(command, "duration_ticks");
        if (!track)
            error = "selected track does not exist";
        else if (step < 0 || step > std::numeric_limits<MusicalTick>::max() /
                                        state_.step_ticks)
            error = "step is outside its range";
        else {
            const auto tick = step * state_.step_ticks;
            const auto count = (track->pattern.length + state_.step_ticks - 1) /
                               state_.step_ticks;
            if (step >= count || tick >= track->pattern.length)
                error = "step is outside the selected track";
            else if (velocity && (!std::isfinite(*velocity) || *velocity < 0 ||
                                  *velocity > 1))
                error = "step velocity must be 0..1";
            else if (duration && *duration <= 0)
                error = "step duration must be positive";
            else {
                const auto track_index = static_cast<std::size_t>(
                    track - transaction.timeline().tracks.data());
                const auto target =
                    track_targets_.contains(track->id)
                        ? track_targets_[track->id]
                        : static_cast<std::uint8_t>(
                              std::min<std::size_t>(127, 36 + track_index));
                std::vector<EventId> matching;
                for(const auto& event:track->pattern.events)if(event.type==MusicalEventType::note&&event.tick==tick&&event.pitch==target)matching.push_back(event.id);
                bool edited = false;
                if(!matching.empty()){
                    edited=true;for(const auto id:matching)if(!transaction.remove_event(track->id,id,error)){edited=false;break;}
                } else
                    edited = transaction.add_note(
                        track->id, transaction.next_event_id(), tick, target,
                        static_cast<float>(velocity ? *velocity
                                                    : state_.step_velocity),
                        duration ? *duration : state_.step_duration, error);
                if (edited &&
                    timeline_.commit(std::move(transaction),
                                     TimelineCommitPolicy::next_audio_callback,
                                     error)) {
                    state_.selected_step = step;
                    state_.step_page = static_cast<std::uint64_t>(step) / 16;
                    changed_locked();
                }
            }
        }
    } else if (command.action_id == "sample.slot-select.v1") {
        const auto id = *argument<std::int64_t>(command, "slot_id");
        if (id < 0 || id > std::numeric_limits<std::uint32_t>::max())
            error = "sample slot is outside its range";
        else {
            state_.selected_sample_slot =
                SampleSlotId{static_cast<std::uint32_t>(id)};
            changed_locked();
        }
    } else if (command.action_id == "pad.mode-select.v1") {
        const auto &mode = *argument<std::string>(command, "mode");
        if (mode == "step")
            state_.pad_mode = PadMode::step;
        else if (mode == "pad")
            state_.pad_mode = PadMode::pad;
        else if (mode == "levels")
            state_.pad_mode = PadMode::levels;
        else if (mode == "notes")
            state_.pad_mode = PadMode::notes;
        else
            error = "unknown pad mode";
        if (error.empty())
            changed_locked();
    } else if (command.action_id == "navigation.focus-next.v1" ||
               command.action_id == "navigation.focus-previous.v1") {
        static constexpr std::string_view focus[]{"transport", "tracks",
                                                  "samples", "parameters"};
        auto found = std::find(focus, focus + 4, state_.focused_element);
        std::size_t index =
            found == focus + 4 ? 0 : static_cast<std::size_t>(found - focus);
        index = command.action_id == "navigation.focus-next.v1"
                    ? (index + 1) % 4
                    : (index + 3) % 4;
        state_.focused_element = focus[index];
        changed_locked();
    } else if (command.action_id == "navigation.confirm.v1") {
        if (state_.focused_element == "transport")
            state_.focused_element = "tracks";
        else if (state_.focused_element == "tracks")
            state_.focused_element = "steps";
        else
            state_.focused_element += ".confirmed";
        changed_locked();
    } else if (command.action_id == "navigation.cancel.v1") {
        state_.focused_element = state_.focused_element == "steps" ? "tracks" : "transport";
        changed_locked();
    } else if (command.action_id == "navigation.adjust.v1") {
        const auto delta=*argument<double>(command,"steps");
        const auto direction=delta<0?-1:delta>0?1:0;
        if(!std::isfinite(delta))error="navigation adjustment must be finite";
        else if(direction==0){}
        else if(state_.focused_element=="tracks"){
            const auto timeline=timeline_.snapshot();if(timeline.timeline.tracks.empty())error="no tracks are available";else{auto found=std::find_if(timeline.timeline.tracks.begin(),timeline.timeline.tracks.end(),[&](const Track& track){return track.id==state_.selected_track;});auto index=found==timeline.timeline.tracks.end()?0:static_cast<std::size_t>(found-timeline.timeline.tracks.begin());index=direction>0?(index+1)%timeline.timeline.tracks.size():(index+timeline.timeline.tracks.size()-1)%timeline.timeline.tracks.size();state_.selected_track=timeline.timeline.tracks[index].id;state_.selected_step=0;state_.step_page=0;changed_locked();}
        }else if(state_.focused_element=="steps"){
            const auto track=std::find_if(state_.tracks.begin(),state_.tracks.end(),[&](const TrackSummary& value){return value.id==state_.selected_track;});if(track==state_.tracks.end())error="selected track does not exist";else{const auto steps=(track->pattern_length+state_.step_ticks-1)/state_.step_ticks;auto step=state_.selected_step+direction;if(step<0)step=steps-1;if(step>=steps)step=0;state_.selected_step=step;state_.step_page=static_cast<std::uint64_t>(step)/16;changed_locked();}
        }else if(state_.focused_element=="transport"){if(parameters_.adjust("transport.tempo",delta,error))changed_locked();}
    } else if (command.action_id == "text.input.v1") {
        state_.focused_element =
            "text:" + *argument<std::string>(command, "text");
        changed_locked();
    } else if (command.action_id == "parameter.set-normalized.v1") {
        if (parameters_.set_normalized(
                *argument<std::string>(command, "parameter_id"),
                *argument<double>(command, "value"), error))
            changed_locked();
    } else if (command.action_id == "parameter.adjust.v1") {
        if (parameters_.adjust(*argument<std::string>(command, "parameter_id"),
                               *argument<double>(command, "steps"), error))
            changed_locked();
    } else if (command.action_id == "parameter.reset.v1") {
        if (parameters_.reset(*argument<std::string>(command, "parameter_id"),
                              error))
            changed_locked();
    } else if (command.action_id == "modifier.shift.v1") {
        state_.modifiers.shift = *argument<bool>(command, "active");
        if (const auto *latched = argument<bool>(command, "latched"))
            state_.modifiers.shift_latched = *latched;
        changed_locked();
    } else if (command.action_id == "modifier.select.v1") {
        state_.modifiers.select = *argument<bool>(command, "active");
        if (const auto *latched = argument<bool>(command, "latched"))
            state_.modifiers.select_latched = *latched;
        changed_locked();
    } else if (command.action_id == "modulation.xy.v1") {
        const auto position = *argument<NormalizedXY>(command, "position");
        if (position.x < 0 || position.x > 1 || position.y < 0 ||
            position.y > 1)
            error = "X/Y position must be normalized";
        else {
            parameters_.set("performance.xy.x", position.x, error);
            if (error.empty())
                parameters_.set("performance.xy.y", position.y, error);
            if (error.empty())
                changed_locked();
        }
    }
  }
  if(changed&&(command.action_id.rfind("sample.",0)==0||command.action_id.rfind("transport.",0)==0)){std::lock_guard lock(mutex_);changed_locked();}
  return{error.empty(),sequence,snapshot().revision,std::move(error)};
}
void ApplicationService::synchronize_locked() const {
    const auto timeline = timeline_.snapshot();
    const auto playing = engine_.playing();
    const auto frame = engine_.frame_position();
    auto slots = samples_.states();
    auto parameters = parameters_.parameters();
    bool external = state_.playing != playing ||
                    state_.transport_frame != frame ||
                    state_.timeline_revision != timeline.revision ||
                    state_.sample_slots.size() != slots.size() ||
                    state_.parameters.size() != parameters.size();
    if (!external)
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (state_.sample_slots[i].revision != slots[i].revision ||
                state_.sample_slots[i].state != slots[i].state) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < parameters.size(); ++i)
            if (state_.parameters[i].id != parameters[i].id ||
                state_.parameters[i].current_value !=
                    parameters[i].current_value) {
                external = true;
                break;
            }
    state_.playing = playing;
    state_.transport_frame = frame;
    const auto transport_tick=(static_cast<long double>(frame)*ticks_per_quarter*1'000'000.0L)/(static_cast<long double>(engine_.sample_rate())*timeline.timeline.transport.microseconds_per_quarter);
    state_.transport_tick=transport_tick>=std::numeric_limits<MusicalTick>::max()?std::numeric_limits<MusicalTick>::max():static_cast<MusicalTick>(transport_tick);
    state_.timeline_revision = timeline.revision;
    state_.tempo_bpm =
        60'000'000.0 / timeline.timeline.transport.microseconds_per_quarter;
    state_.time_signature_numerator =
        timeline.timeline.transport.time_signature_numerator;
    state_.time_signature_denominator =
        timeline.timeline.transport.time_signature_denominator;
    if (state_.selected_track == 0 && !timeline.timeline.tracks.empty())
        state_.selected_track = timeline.timeline.tracks.front().id;
    state_.tracks.clear();
    std::size_t index{};
    for (const auto &track : timeline.timeline.tracks) {
        const auto fallback =
            static_cast<std::uint8_t>(std::min<std::size_t>(127, 36 + index++));
        const auto target = track_targets_.contains(track.id)
                                ? track_targets_.at(track.id)
                                : fallback;
        state_.tracks.push_back({track.id, track.name, track.muted,
                                 track.pattern.length, target,
                                 track.pattern.events});
    }
    state_.sample_slots = std::move(slots);
    state_.parameters = std::move(parameters);
    state_.available_actions.clear();
    for (const auto &value : actions_.actions())
        if (value.available)
            state_.available_actions.push_back(value.id);
    if (external)
        ++state_.revision;
}
void ApplicationService::changed_locked(){++state_.revision;}
ApplicationState ApplicationService::snapshot() const{std::lock_guard lock(mutex_);synchronize_locked();return state_;}
std::uint64_t ApplicationService::rejected_commands() const noexcept{std::lock_guard lock(mutex_);return rejected_;}

const char* step_resolution_name(StepResolution value) noexcept{switch(value){case StepResolution::quarter:return"1/4";case StepResolution::eighth:return"1/8";case StepResolution::sixteenth:return"1/16";case StepResolution::thirty_second:return"1/32";case StepResolution::quarter_triplet:return"1/4T";case StepResolution::eighth_triplet:return"1/8T";case StepResolution::sixteenth_triplet:return"1/16T";case StepResolution::thirty_second_triplet:return"1/32T";}return"1/16";}
MusicalTick step_resolution_ticks(StepResolution value) noexcept{switch(value){case StepResolution::quarter:return 960;case StepResolution::eighth:return 480;case StepResolution::sixteenth:return 240;case StepResolution::thirty_second:return 120;case StepResolution::quarter_triplet:return 640;case StepResolution::eighth_triplet:return 320;case StepResolution::sixteenth_triplet:return 160;case StepResolution::thirty_second_triplet:return 80;}return 240;}
std::optional<StepResolution> parse_step_resolution(std::string_view value) noexcept{for(const auto resolution:{StepResolution::quarter,StepResolution::eighth,StepResolution::sixteenth,StepResolution::thirty_second,StepResolution::quarter_triplet,StepResolution::eighth_triplet,StepResolution::sixteenth_triplet,StepResolution::thirty_second_triplet})if(value==step_resolution_name(resolution))return resolution;return std::nullopt;}

void TextEntryModel::begin(std::string initial){state_={std::move(initial),0,'A',true,false,false};state_.cursor=state_.text.size();}
void TextEntryModel::direct(std::string_view text){if(!state_.active)return;state_.text.insert(state_.cursor,text);state_.cursor+=text.size();}
void TextEntryModel::select_relative(int delta){if(!state_.active)return;constexpr std::string_view characters=" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";auto found=characters.find(state_.selected);auto index=found==std::string_view::npos?0:static_cast<int>(found);index=(index+delta%static_cast<int>(characters.size())+static_cast<int>(characters.size()))%static_cast<int>(characters.size());state_.selected=characters[static_cast<std::size_t>(index)];}
void TextEntryModel::insert(){if(state_.active){state_.text.insert(state_.cursor,1,state_.selected);++state_.cursor;}}
void TextEntryModel::erase_or_cancel(){if(!state_.active)return;if(state_.cursor>0){state_.text.erase(--state_.cursor,1);}else discard();}
void TextEntryModel::move_cursor(int delta){if(!state_.active)return;const auto position=static_cast<std::int64_t>(state_.cursor)+delta;state_.cursor=static_cast<std::size_t>(std::clamp<std::int64_t>(position,0,static_cast<std::int64_t>(state_.text.size())));}
void TextEntryModel::commit(){if(state_.active){state_.active=false;state_.committed=true;}}
void TextEntryModel::discard(){if(state_.active){state_.active=false;state_.discarded=true;}}

NormalizedXY XYControl::apply(double x,double y){if(behavior_.relative){x+=value_.x;y+=value_.y;}if(behavior_.invert_x)x=1-x;if(behavior_.invert_y)y=1-y;value_.x=std::clamp(0.5+(x-0.5)*behavior_.scale_x,0.0,1.0);value_.y=std::clamp(0.5+(y-0.5)*behavior_.scale_y,0.0,1.0);return value_;}
void XYControl::release(){if(behavior_.spring_to_center&&!behavior_.latching)value_={0.5,0.5};}
const char* pad_mode_name(PadMode mode) noexcept{switch(mode){case PadMode::step:return"step";case PadMode::pad:return"pad";case PadMode::levels:return"levels";case PadMode::notes:return"notes";}return"unknown";}
}
