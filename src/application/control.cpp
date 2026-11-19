#include "neurogenic/application/control.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace neurogenic {
namespace {
ActionDescriptor action(std::string id, std::string name, std::string category,
                        std::vector<ArgumentSpec> arguments = {},
                        bool available = true, std::string reason = {}) {
    auto description = name + " application action";
    return {std::move(id),
            std::move(name),
            std::move(description),
            std::move(category),
            std::move(arguments),
            available,
            {},
            std::move(reason)};
}
template <class T>
const T *argument(const ApplicationCommand &command, std::string_view name) {
    const auto found = command.arguments.find(std::string(name));
    return found == command.arguments.end() ? nullptr
                                            : std::get_if<T>(&found->second);
}
bool is_numeric(const ApplicationValue &value) {
    return std::holds_alternative<std::int64_t>(value) ||
           std::holds_alternative<double>(value);
}
double number(const ApplicationValue &value) {
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return static_cast<double>(*integer);
    return std::get<double>(value);
}
bool publish_dsp_parameter(AudioEngine &engine, ParameterRegistry &parameters,
                           const std::string &id) {
    const auto descriptor = parameters.get(id);
    if (!descriptor ||
        !std::holds_alternative<double>(descriptor->current_value))
        return true;
    const auto value =
        static_cast<float>(std::get<double>(descriptor->current_value));
    if (id == "synth.waveform")
        return engine.set_synth_parameter(SynthParameter::waveform, value);
    if (id == "synth.attack")
        return engine.set_synth_parameter(SynthParameter::attack, value);
    if (id == "synth.decay")
        return engine.set_synth_parameter(SynthParameter::decay, value);
    if (id == "synth.sustain")
        return engine.set_synth_parameter(SynthParameter::sustain, value);
    if (id == "synth.release")
        return engine.set_synth_parameter(SynthParameter::release, value);
    if (id == "synth.cutoff")
        return engine.set_synth_parameter(SynthParameter::cutoff, value);
    if (id == "synth.resonance")
        return engine.set_synth_parameter(SynthParameter::resonance, value);
    if (id == "synth.pulse-width")
        return engine.set_synth_parameter(SynthParameter::pulse_width, value);
    if (id == "synth.gain")
        return engine.set_synth_parameter(SynthParameter::gain, value);
    if (id == "effect.delay.time")
        return engine.set_delay_parameter(DelayParameter::time, value);
    if (id == "effect.delay.feedback")
        return engine.set_delay_parameter(DelayParameter::feedback, value);
    if (id == "effect.delay.mix")
        return engine.set_delay_parameter(DelayParameter::mix, value);
    return true;
}
std::optional<TriggerCondition>
parse_trigger_condition(std::string_view value) {
    if (value == "always")
        return TriggerCondition::always;
    if (value == "first")
        return TriggerCondition::first_loop;
    if (value == "not-first")
        return TriggerCondition::not_first_loop;
    if (value == "every-n")
        return TriggerCondition::every_nth_loop;
    if (value == "n-of-m")
        return TriggerCondition::n_of_m;
    if (value == "previous-fired")
        return TriggerCondition::previous_fired;
    if (value == "previous-not-fired")
        return TriggerCondition::previous_not_fired;
    if (value == "fill")
        return TriggerCondition::fill_active;
    if (value == "not-fill")
        return TriggerCondition::fill_inactive;
    return std::nullopt;
}
std::string_view trigger_condition_name(TriggerCondition value) {
    static constexpr std::string_view names[]{
        "always",  "first",          "not-first",          "every-n",
        "n-of-m",  "previous-fired", "previous-not-fired", "fill",
        "not-fill"};
    return names[static_cast<std::size_t>(value)];
}
} // namespace

ActionRegistry::ActionRegistry() {
    std::string error;
    const auto add = [&](ActionDescriptor value) {
        if (!register_action(std::move(value), error))
            throw std::logic_error(error);
    };
    add(action("transport.play.v1", "Play", "Transport"));
    add(action("transport.stop.v1", "Stop", "Transport"));
    add(action("transport.restart.v1", "Restart", "Transport"));
    add(action("track.select.v1", "Select track", "Tracks",
               {{"track_id", ValueType::integer}}));
    add(action("track.next.v1", "Next track", "Tracks"));
    add(action("track.previous.v1", "Previous track", "Tracks"));
    add(action("track.mute-toggle.v1", "Toggle track mute", "Tracks"));
    add(action("track.instrument-set.v1", "Set track instrument", "Tracks",
               {{"instrument_id", ValueType::text}}));
    add(action("pattern.select.v1", "Select pattern", "Sequencer",
               {{"pattern_id", ValueType::integer}}));
    add(action("step.select.v1", "Select step", "Sequencer",
               {{"step", ValueType::integer}}));
    add(action("step.toggle.v1", "Toggle step", "Sequencer",
               {{"step", ValueType::integer, false},
                {"velocity", ValueType::real, false},
                {"duration_ticks", ValueType::integer, false}}));
    add(action("step.event-select.v1", "Select event at step", "Sequencer",
               {{"step", ValueType::integer, false},
                {"event_id", ValueType::integer, false}}));
    add(action("step.property-select.v1", "Select step property", "Sequencer",
               {{"property", ValueType::text}}));
    add(action("step.velocity-set.v1", "Set step velocity", "Sequencer",
               {{"velocity", ValueType::real}}));
    add(action("step.duration-set.v1", "Set step duration", "Sequencer",
               {{"duration_ticks", ValueType::integer}}));
    add(action("step.pitch-set.v1", "Set step pitch", "Sequencer",
               {{"pitch", ValueType::integer}}));
    add(action("step.timing-offset-set.v1", "Set step timing offset",
               "Sequencer", {{"offset_ticks", ValueType::integer}}));
    add(action("step.property-reset.v1", "Reset step property", "Sequencer",
               {{"property", ValueType::text}}));
    add(action("step.lock-select.v1", "Select parameter lock", "Sequencer",
               {{"parameter_id", ValueType::text}}));
    add(action(
        "step.lock-set.v1", "Set parameter lock", "Sequencer",
        {{"parameter_id", ValueType::text}, {"value", ValueType::real}}));
    add(action("step.lock-remove.v1", "Remove parameter lock", "Sequencer",
               {{"parameter_id", ValueType::text}}));
    add(action("step.locks-clear.v1", "Clear step locks", "Sequencer"));
    add(action("step.probability-set.v1", "Set trigger probability",
               "Sequencer", {{"probability", ValueType::real}}));
    add(action("step.condition-set.v1", "Set trigger condition", "Sequencer",
               {{"condition", ValueType::text},
                {"n", ValueType::integer, false},
                {"m", ValueType::integer, false}}));
    add(action("step.ratchet-set.v1", "Set ratchet count", "Sequencer",
               {{"count", ValueType::integer}}));
    add(action("step.trigger-reset.v1", "Reset trigger properties",
               "Sequencer"));
    add(action("performance.fill-set.v1", "Set momentary Fill", "Performance",
               {{"active", ValueType::boolean}}));
    add(action("performance.fill-latch.v1", "Toggle latched Fill",
               "Performance"));
    add(action("timeline.seed-set.v1", "Set deterministic seed", "Generation",
               {{"seed", ValueType::integer}}));
    add(action("step.page-next.v1", "Next step page", "Sequencer"));
    add(action("step.page-previous.v1", "Previous step page", "Sequencer"));
    add(action("step.resolution-set.v1", "Set step resolution", "Sequencer",
               {{"resolution", ValueType::text}}));
    add(action("track.note-target-set.v1", "Set track note target", "Tracks",
               {{"note", ValueType::integer}}));
    add(action("sample.slot-select.v1", "Select sample slot", "Samples",
               {{"slot_id", ValueType::integer}}));
    add(action("sample.load.v1", "Load sample", "Samples",
               {{"slot_id", ValueType::integer}, {"path", ValueType::text}}));
    add(action("sample.replace.v1", "Replace sample", "Samples",
               {{"slot_id", ValueType::integer}, {"path", ValueType::text}}));
    add(action("sample.unload.v1", "Unload sample", "Samples",
               {{"slot_id", ValueType::integer}}));
    add(action("slice.select.v1", "Select slice", "Samples",
               {{"slice_id", ValueType::integer}}));
    add(action("slice.create.v1", "Create slice", "Samples",
               {{"start_frame", ValueType::integer},
                {"end_frame", ValueType::integer},
                {"name", ValueType::text, false}}));
    add(action("slice.delete.v1", "Delete slice", "Samples"));
    add(action("slice.bounds-set.v1", "Set slice boundaries", "Samples",
               {{"start_frame", ValueType::integer},
                {"end_frame", ValueType::integer}}));
    add(action("slice.rename.v1", "Rename slice", "Samples",
               {{"name", ValueType::text}}));
    add(action("slice.duplicate.v1", "Duplicate slice", "Samples"));
    add(action("slice.clear.v1", "Clear slices", "Samples"));
    add(action("slice.equal-divisions.v1", "Slice equally", "Samples",
               {{"divisions", ValueType::integer}}));
    add(action("slice.detect-transients.v1", "Detect transients", "Samples",
               {{"sensitivity", ValueType::real},
                {"minimum_frames", ValueType::integer}}));
    add(action("slice.pad-assign.v1", "Assign slice to pad", "Samples",
               {{"pad", ValueType::integer}}));
    add(action("slice.pad-clear.v1", "Clear pad slice", "Samples",
               {{"pad", ValueType::integer}}));
    add(action("slice.preview.v1", "Preview slice", "Samples"));
    add(action("sample.preview.v1", "Preview sample", "Samples"));
    add(action("slice.preview-stop.v1", "Stop preview", "Samples"));
    add(action("pad.trigger.v1", "Trigger performance pad", "Performance",
               {{"pad", ValueType::integer},
                {"velocity", ValueType::real, false},
                {"pitch_offset", ValueType::real, false}}));
    add(action("pad.release.v1", "Release performance pad", "Performance",
               {{"pad", ValueType::integer}}));
    add(action("pad.select.v1", "Select performance pad", "Performance",
               {{"pad", ValueType::integer}}));
    add(action("pad.property-select.v1", "Select pad property", "Samples",
               {{"property", ValueType::text}}));
    add(action("pad.playback-mode-set.v1", "Set pad playback mode", "Samples",
               {{"mode", ValueType::text}}));
    add(action("pad.choke-group-set.v1", "Set pad choke group", "Samples",
               {{"group", ValueType::integer}}));
    add(action("pad.choke-release-set.v1", "Set pad choke release", "Samples",
               {{"release", ValueType::boolean}}));
    add(action("pad.pitch-set.v1", "Set pad pitch", "Samples",
               {{"semitones", ValueType::real}}));
    add(action("pad.fine-set.v1", "Set pad fine tuning", "Samples",
               {{"cents", ValueType::real}}));
    add(action("pad.gain-set.v1", "Set pad gain", "Samples",
               {{"gain", ValueType::real}}));
    add(action("pad.reverse-set.v1", "Set pad reverse", "Samples",
               {{"reverse", ValueType::boolean}}));
    add(action("pad.envelope-set.v1", "Set pad amplitude envelope", "Samples",
               {{"attack", ValueType::real},
                {"decay", ValueType::real},
                {"sustain", ValueType::real},
                {"release", ValueType::real}}));
    add(action("pad.stretch-mode-set.v1", "Set pad stretch mode", "Samples",
               {{"mode", ValueType::text}}));
    add(action("pad.stretch-ratio-set.v1", "Set pad time ratio", "Samples",
               {{"ratio", ValueType::real}}));
    add(action("pad.source-beats-set.v1", "Set source beat length", "Samples",
               {{"beats", ValueType::real}}));
    add(action("pad.source-bpm-set.v1", "Set source tempo", "Samples",
               {{"bpm", ValueType::real}}));
    add(action("pad.stretch-quality-set.v1", "Set stretch quality", "Samples",
               {{"quality", ValueType::text}}));
    add(action("pad.stretch-reset.v1", "Reset stretch settings", "Samples"));
    add(action("edit.undo.v1", "Undo", "Editing"));
    add(action("edit.redo.v1", "Redo", "Editing"));
    add(action("edit.group-begin.v1", "Begin grouped edit", "Editing"));
    add(action("edit.group-commit.v1", "Commit grouped edit", "Editing"));
    add(action("edit.group-cancel.v1", "Cancel grouped edit", "Editing"));
    add(action("timeline.generate.v1", "Generate timeline", "Generation", {},
               false,
               "a generator choice and transaction payload are required"));
    add(action(
        "timeline.mutate-track.v1", "Mutate track", "Generation", {}, false,
        "mutation settings are not represented by this initial command"));
    add(action("pad.mode-select.v1", "Select pad mode", "Performance",
               {{"mode", ValueType::text}}));
    add(action("navigation.focus-next.v1", "Focus next", "Navigation"));
    add(action("navigation.focus-previous.v1", "Focus previous", "Navigation"));
    add(action("navigation.adjust.v1", "Adjust focused item", "Navigation",
               {{"steps", ValueType::real}}));
    add(action("navigation.confirm.v1", "Confirm", "Navigation"));
    add(action("navigation.cancel.v1", "Cancel", "Navigation"));
    add(action("text.input.v1", "Text input", "Text",
               {{"text", ValueType::text}}));
    add(action("modulation.xy.v1", "X/Y control", "Modulation",
               {{"position", ValueType::xy}}));
    add(action(
        "parameter.set-normalized.v1", "Set parameter", "Parameters",
        {{"parameter_id", ValueType::text}, {"value", ValueType::real}}));
    add(action(
        "parameter.adjust.v1", "Adjust parameter", "Parameters",
        {{"parameter_id", ValueType::text}, {"steps", ValueType::real}}));
    add(action("parameter.reset.v1", "Reset parameter", "Parameters",
               {{"parameter_id", ValueType::text}}));
    add(action("modifier.shift.v1", "Shift", "Modifiers",
               {{"active", ValueType::boolean},
                {"latched", ValueType::boolean, false}}));
    add(action("modifier.select.v1", "Select modifier", "Modifiers",
               {{"active", ValueType::boolean},
                {"latched", ValueType::boolean, false}}));
}
bool ActionRegistry::register_action(ActionDescriptor value,
                                     std::string &error) {
    if (value.id.empty() || value.name.empty()) {
        error = "action ID and name are required";
        return false;
    }
    if (find(value.id)) {
        error = "duplicate action ID: " + value.id;
        return false;
    }
    actions_.push_back(std::move(value));
    error.clear();
    return true;
}
const ActionDescriptor *
ActionRegistry::find(std::string_view id) const noexcept {
    for (const auto &value : actions_)
        if (value.id == id)
            return &value;
    return nullptr;
}
bool ActionRegistry::validate(
    std::string_view id, const std::map<std::string, ApplicationValue> &values,
    std::string &error) const {
    const auto *descriptor = find(id);
    if (!descriptor) {
        error = "unknown action: " + std::string(id);
        return false;
    }
    if (!descriptor->available) {
        error = descriptor->unavailable_reason.empty()
                    ? "action is unavailable"
                    : descriptor->unavailable_reason;
        return false;
    }
    for (const auto &spec : descriptor->arguments) {
        const auto found = values.find(spec.name);
        if (found == values.end()) {
            if (spec.required) {
                error = "missing argument: " + spec.name;
                return false;
            }
            continue;
        }
        const auto type = found->second.index();
        const auto expected = static_cast<std::size_t>(spec.type);
        if (type != expected) {
            error = "invalid type for argument: " + spec.name;
            return false;
        }
    }
    for (const auto &[name, value] : values) {
        (void)value;
        if (std::none_of(descriptor->arguments.begin(),
                         descriptor->arguments.end(),
                         [&](const auto &spec) { return spec.name == name; })) {
            error = "unexpected argument: " + name;
            return false;
        }
    }
    error.clear();
    return true;
}

bool ParameterRegistry::valid_value(const ParameterDescriptor &parameter,
                                    const ApplicationValue &value,
                                    std::string &error) const {
    switch (parameter.type) {
    case ParameterType::boolean:
        if (!std::holds_alternative<bool>(value)) {
            error = "parameter requires a boolean";
            return false;
        }
        break;
    case ParameterType::integer:
    case ParameterType::enumeration:
        if (!std::holds_alternative<std::int64_t>(value)) {
            error = "parameter requires an integer";
            return false;
        }
        break;
    case ParameterType::real:
        if (!is_numeric(value)) {
            error = "parameter requires a number";
            return false;
        }
        break;
    }
    if (parameter.type != ParameterType::boolean) {
        const auto current = number(value), minimum = number(parameter.minimum),
                   maximum = number(parameter.maximum);
        if (!std::isfinite(current) || current < minimum || current > maximum) {
            error = "parameter value is outside its range";
            return false;
        }
        if (parameter.type == ParameterType::enumeration &&
            static_cast<std::size_t>(std::get<std::int64_t>(value)) >=
                parameter.choices.size()) {
            error = "enumeration value has no choice";
            return false;
        }
    }
    error.clear();
    return true;
}
bool ParameterRegistry::register_parameter(ParameterDescriptor value,
                                           std::string &error) {
    std::lock_guard lock(mutex_);
    if (value.id.empty() || value.name.empty() || values_.contains(value.id)) {
        error = "parameter ID/name is empty or duplicated";
        return false;
    }
    if (value.step <= 0 || !std::isfinite(value.step)) {
        error = "parameter step must be positive";
        return false;
    }
    if (!valid_value(value, value.default_value, error) ||
        !valid_value(value, value.current_value, error))
        return false;
    values_.emplace(value.id, std::move(value));
    error.clear();
    return true;
}
bool ParameterRegistry::set(std::string_view id, const ApplicationValue &value,
                            std::string &error) {
    std::lock_guard lock(mutex_);
    auto found = values_.find(id);
    if (found == values_.end()) {
        error = "unknown parameter";
        return false;
    }
    if (!valid_value(found->second, value, error))
        return false;
    found->second.current_value = value;
    return true;
}
bool ParameterRegistry::set_normalized(std::string_view id, double normalized,
                                       std::string &error) {
    std::lock_guard lock(mutex_);
    auto found = values_.find(id);
    if (found == values_.end()) {
        error = "unknown parameter";
        return false;
    }
    if (!std::isfinite(normalized) || normalized < 0 || normalized > 1) {
        error = "normalized parameter value must be 0..1";
        return false;
    }
    auto &parameter = found->second;
    if (parameter.type == ParameterType::boolean)
        parameter.current_value = normalized >= 0.5;
    else {
        const auto scaled =
            number(parameter.minimum) +
            (number(parameter.maximum) - number(parameter.minimum)) *
                normalized;
        if (parameter.type == ParameterType::real)
            parameter.current_value = scaled;
        else
            parameter.current_value =
                static_cast<std::int64_t>(std::llround(scaled));
    }
    error.clear();
    return true;
}
bool ParameterRegistry::adjust(std::string_view id, double steps,
                               std::string &error) {
    std::lock_guard lock(mutex_);
    auto found = values_.find(id);
    if (found == values_.end()) {
        error = "unknown parameter";
        return false;
    }
    auto &parameter = found->second;
    if (parameter.type == ParameterType::boolean) {
        if (steps != 0)
            parameter.current_value = !std::get<bool>(parameter.current_value);
        error.clear();
        return true;
    }
    if (!std::isfinite(steps)) {
        error = "relative adjustment must be finite";
        return false;
    }
    const auto minimum = number(parameter.minimum),
               maximum = number(parameter.maximum);
    auto updated =
        std::clamp(number(parameter.current_value) + steps * parameter.step,
                   minimum, maximum);
    if (parameter.type != ParameterType::real)
        parameter.current_value =
            static_cast<std::int64_t>(std::llround(updated));
    else
        parameter.current_value = updated;
    error.clear();
    return true;
}
bool ParameterRegistry::reset(std::string_view id, std::string &error) {
    std::lock_guard lock(mutex_);
    auto found = values_.find(id);
    if (found == values_.end()) {
        error = "unknown parameter";
        return false;
    }
    found->second.current_value = found->second.default_value;
    error.clear();
    return true;
}
std::optional<ParameterDescriptor>
ParameterRegistry::get(std::string_view id) const {
    std::lock_guard lock(mutex_);
    const auto found = values_.find(id);
    return found == values_.end()
               ? std::nullopt
               : std::optional<ParameterDescriptor>(found->second);
}
std::vector<ParameterDescriptor> ParameterRegistry::parameters() const {
    std::lock_guard lock(mutex_);
    std::vector<ParameterDescriptor> result;
    result.reserve(values_.size());
    for (const auto &[id, value] : values_) {
        (void)id;
        result.push_back(value);
    }
    return result;
}

ApplicationService::ApplicationService(AudioEngine &engine,
                                       TimelineService &timeline,
                                       SampleManager &samples,
                                       std::size_t capacity)
    : engine_(engine), timeline_(timeline), samples_(samples),
      slices_(engine, samples), capacity_(capacity) {
    if (capacity_ == 0)
        throw std::invalid_argument(
            "application command capacity must be positive");
    std::string error;
    parameters_.register_parameter({"transport.tempo",
                                    "Tempo",
                                    "BPM",
                                    ParameterType::real,
                                    20.0,
                                    300.0,
                                    120.0,
                                    120.0,
                                    0.1,
                                    true,
                                    false,
                                    false,
                                    {}},
                                   error);
    parameters_.register_parameter({"performance.xy.x",
                                    "X axis",
                                    "",
                                    ParameterType::real,
                                    0.0,
                                    1.0,
                                    0.5,
                                    0.5,
                                    0.01,
                                    true,
                                    false,
                                    true,
                                    {}},
                                   error);
    parameters_.register_parameter({"performance.xy.y",
                                    "Y axis",
                                    "",
                                    ParameterType::real,
                                    0.0,
                                    1.0,
                                    0.5,
                                    0.5,
                                    0.01,
                                    true,
                                    false,
                                    true,
                                    {}},
                                   error);
    const auto synth_parameter =
        [&](std::string id, std::string name, std::string unit, double minimum,
            double maximum, double initial, double step) {
            parameters_.register_parameter({std::move(id),
                                            std::move(name),
                                            std::move(unit),
                                            ParameterType::real,
                                            minimum,
                                            maximum,
                                            initial,
                                            initial,
                                            step,
                                            true,
                                            true,
                                            false,
                                            {}},
                                           error);
        };
    synth_parameter("synth.waveform", "Waveform", "", 0, 3, 2, 1);
    synth_parameter("synth.attack", "Attack", "s", 0.001, 5, 0.005, 0.01);
    synth_parameter("synth.decay", "Decay", "s", 0.001, 5, 0.12, 0.01);
    synth_parameter("synth.sustain", "Sustain", "", 0, 1, 0.75, 0.01);
    synth_parameter("synth.release", "Release", "s", 0.001, 10, 0.25, 0.01);
    synth_parameter("synth.cutoff", "Cutoff", "Hz", 20, 20000, 8000, 100);
    synth_parameter("synth.resonance", "Resonance", "", 0, 0.95, 0.15, 0.01);
    synth_parameter("synth.pulse-width", "Pulse width", "", 0.05, 0.95, 0.5,
                    0.01);
    synth_parameter("synth.gain", "Synth gain", "", 0, 1, 0.25, 0.01);
    synth_parameter("effect.delay.time", "Delay time", "s", 0.001, 2, 0.25,
                    0.01);
    synth_parameter("effect.delay.feedback", "Delay feedback", "", 0, 0.95, 0.3,
                    0.01);
    synth_parameter("effect.delay.mix", "Delay mix", "", 0, 1, 0, 0.01);
    state_.step_ticks = step_resolution_ticks(state_.step_resolution);
    state_.step_duration = state_.step_ticks;
}
bool ApplicationService::submit(ApplicationCommand command) noexcept {
    try {
        std::lock_guard lock(mutex_);
        if (queue_.size() >= capacity_) {
            ++rejected_;
            return false;
        }
        queue_.emplace_back(next_sequence_++, std::move(command));
        return true;
    } catch (...) {
        return false;
    }
}
std::vector<CommandResult> ApplicationService::dispatch_pending() {
    std::vector<CommandResult> result;
    for (;;) {
        std::pair<std::uint64_t, ApplicationCommand> item;
        {
            std::lock_guard lock(mutex_);
            if (queue_.empty())
                break;
            item = std::move(queue_.front());
            queue_.pop_front();
        }
        result.push_back(execute_locked(item.second, item.first));
    }
    return result;
}
CommandResult ApplicationService::execute(const ApplicationCommand &command) {
    std::uint64_t sequence;
    {
        std::lock_guard lock(mutex_);
        sequence = next_sequence_++;
    }
    return execute_locked(command, sequence);
}
CommandResult
ApplicationService::execute_locked(const ApplicationCommand &command,
                                   std::uint64_t sequence) {
    std::string error;
    if (!actions_.validate(command.action_id, command.arguments, error))
        return {false, sequence, snapshot().revision, std::move(error)};
    const auto before = snapshot();
    if (command.action_id == "navigation.confirm.v1" &&
        before.focused_element == "steps") {
        ApplicationCommand select;
        select.action_id = "step.event-select.v1";
        auto result = execute_locked(select, sequence);
        if (!result.accepted) {
            ApplicationCommand toggle;
            toggle.action_id = "step.toggle.v1";
            result = execute_locked(toggle, sequence);
            if (result.accepted)
                result = execute_locked(select, sequence);
        }
        if (result.accepted) {
            std::lock_guard lock(mutex_);
            state_.focused_element = "step-properties";
            changed_locked();
            result.state_revision = state_.revision;
        }
        return result;
    }
    if (command.action_id == "navigation.adjust.v1" &&
        before.focused_element == "step-properties" && before.selected_event) {
        const auto delta = *argument<double>(command, "steps");
        ApplicationCommand edit;
        const auto &event = *before.selected_event;
        if (before.selected_step_property == "velocity") {
            edit.action_id = "step.velocity-set.v1";
            edit.arguments = {
                {"velocity",
                 std::clamp(static_cast<double>(event.velocity) + delta * 0.05,
                            0.0, 1.0)}};
        } else if (before.selected_step_property == "duration") {
            edit.action_id = "step.duration-set.v1";
            edit.arguments = {
                {"duration_ticks",
                 static_cast<std::int64_t>(std::max<MusicalTick>(
                     1, event.duration + static_cast<MusicalTick>(delta) *
                                             before.step_ticks / 4))}};
        } else if (before.selected_step_property == "pitch") {
            edit.action_id = "step.pitch-set.v1";
            edit.arguments = {
                {"pitch", static_cast<std::int64_t>(std::clamp<int>(
                              event.pitch + (delta < 0 ? -1 : 1), 0, 127))}};
        } else if (before.selected_step_property == "timing") {
            edit.action_id = "step.timing-offset-set.v1";
            edit.arguments = {
                {"offset_ticks",
                 static_cast<std::int64_t>(std::clamp<MusicalTick>(
                     event.timing_offset + static_cast<MusicalTick>(delta) * 10,
                     -before.step_ticks + 1, before.step_ticks - 1))}};
        } else if (before.selected_step_property == "probability") {
            edit.action_id = "step.probability-set.v1";
            edit.arguments = {
                {"probability",
                 std::clamp(static_cast<double>(event.trigger_probability) +
                                delta * 0.05,
                            0.0, 1.0)}};
        } else if (before.selected_step_property == "ratchet") {
            edit.action_id = "step.ratchet-set.v1";
            edit.arguments = {
                {"count",
                 static_cast<std::int64_t>(std::clamp<int>(
                     event.ratchet_count + (delta < 0 ? -1 : 1), 1, 8))}};
        } else {
            const auto current = static_cast<int>(event.trigger_condition);
            const auto next = (current + (delta < 0 ? 8 : 1)) % 9;
            edit.action_id = "step.condition-set.v1";
            edit.arguments = {
                {"condition", std::string(trigger_condition_name(
                                  static_cast<TriggerCondition>(next)))},
                {"n", static_cast<std::int64_t>(event.condition_n)},
                {"m", static_cast<std::int64_t>(event.condition_m)}};
        }
        return execute_locked(edit, sequence);
    }
    if (command.action_id == "navigation.adjust.v1" &&
        before.focused_element == "pad-playback") {
        const auto delta = *argument<double>(command, "steps");
        const auto &p = before.selected_pad_settings;
        ApplicationCommand edit;
        if (before.selected_pad_property == "mode") {
            const auto next = static_cast<SamplePlaybackMode>(
                (static_cast<int>(p.mode) + (delta < 0 ? 2 : 1)) % 3);
            edit.action_id = "pad.playback-mode-set.v1";
            edit.arguments = {
                {"mode", std::string(sample_playback_mode_name(next))}};
        } else if (before.selected_pad_property == "choke") {
            edit.action_id = "pad.choke-group-set.v1";
            edit.arguments = {
                {"group", static_cast<std::int64_t>(std::clamp<int>(
                              p.choke_group + (delta < 0 ? -1 : 1), 0, 16))}};
        } else if (before.selected_pad_property == "pitch") {
            edit.action_id = "pad.pitch-set.v1";
            edit.arguments = {
                {"semitones", static_cast<double>(p.semitones + delta)}};
        } else if (before.selected_pad_property == "fine") {
            edit.action_id = "pad.fine-set.v1";
            edit.arguments = {
                {"cents", static_cast<double>(p.fine_cents + delta)}};
        } else if (before.selected_pad_property == "gain") {
            edit.action_id = "pad.gain-set.v1";
            edit.arguments = {
                {"gain",
                 static_cast<double>(std::clamp(
                     p.gain + static_cast<float>(delta * 0.05), 0.0F, 4.0F))}};
        } else if (before.selected_pad_property == "reverse") {
            edit.action_id = "pad.reverse-set.v1";
            edit.arguments = {{"reverse", !p.reverse}};
        } else if (before.selected_pad_property == "stretch-mode") {
            edit.action_id = "pad.stretch-mode-set.v1";
            edit.arguments = {
                {"mode",
                 std::string(p.stretch_mode == StretchMode::off ? "realtime"
                                                                : "off")}};
        } else if (before.selected_pad_property == "stretch-ratio") {
            edit.action_id = "pad.stretch-ratio-set.v1";
            edit.arguments = {
                {"ratio", static_cast<double>(std::clamp(
                              p.stretch_ratio + delta * .05, .25, 4.0))}};
        } else if (before.selected_pad_property == "beats") {
            edit.action_id = "pad.source-beats-set.v1";
            edit.arguments = {
                {"beats", static_cast<double>(std::clamp(
                              p.source_beats + delta * .25, 0.0, 1024.0))}};
        } else if (before.selected_pad_property == "source-bpm") {
            edit.action_id = "pad.source-bpm-set.v1";
            edit.arguments = {{"bpm", static_cast<double>(std::clamp(
                                          p.source_bpm + delta, 0.0, 1000.0))}};
        } else if (before.selected_pad_property == "stretch-quality") {
            const auto current = static_cast<int>(p.stretch_quality);
            const auto next = (current + (delta < 0 ? 2 : 1)) % 3;
            edit.action_id = "pad.stretch-quality-set.v1";
            edit.arguments = {
                {"quality", std::string(stretch_quality_name(
                                static_cast<StretchQuality>(next)))}};
        } else {
            auto attack = p.attack_seconds;
            auto decay = p.decay_seconds;
            auto sustain = p.sustain;
            auto release = p.release_seconds;
            if (before.selected_pad_property == "attack")
                attack =
                    std::max(0.0F, attack + static_cast<float>(delta * .01));
            else if (before.selected_pad_property == "decay")
                decay = std::max(0.0F, decay + static_cast<float>(delta * .01));
            else if (before.selected_pad_property == "sustain")
                sustain = std::clamp(sustain + static_cast<float>(delta * .01),
                                     0.0F, 1.0F);
            else
                release =
                    std::max(0.0F, release + static_cast<float>(delta * .01));
            edit.action_id = "pad.envelope-set.v1";
            edit.arguments = {{"attack", static_cast<double>(attack)},
                              {"decay", static_cast<double>(decay)},
                              {"sustain", static_cast<double>(sustain)},
                              {"release", static_cast<double>(release)}};
        }
        return execute_locked(edit, sequence);
    }
    if (command.action_id == "navigation.adjust.v1" &&
        before.focused_element == "step-locks") {
        const auto descriptor = parameters_.get(before.selected_lock_parameter);
        if (!descriptor)
            return {false, sequence, before.revision,
                    "selected lock parameter is unavailable"};
        auto value = number(descriptor->current_value);
        const auto selected_lock = std::find_if(
            before.selected_step_locks.begin(),
            before.selected_step_locks.end(), [&](const auto &item) {
                return item.parameter_id == before.selected_lock_parameter;
            });
        if (selected_lock != before.selected_step_locks.end())
            value = selected_lock->value;
        value = std::clamp(
            value + *argument<double>(command, "steps") * descriptor->step,
            number(descriptor->minimum), number(descriptor->maximum));
        ApplicationCommand lock;
        lock.action_id = "step.lock-set.v1";
        lock.arguments = {{"parameter_id", before.selected_lock_parameter},
                          {"value", value}};
        return execute_locked(lock, sequence);
    }
    bool changed = false;
    if (command.action_id == "transport.play.v1") {
        engine_.play();
        changed = true;
    } else if (command.action_id == "transport.stop.v1") {
        engine_.stop();
        changed = true;
    } else if (command.action_id == "transport.restart.v1") {
        engine_.restart();
        changed = true;
    } else if (command.action_id == "sample.load.v1" ||
               command.action_id == "sample.replace.v1") {
        const auto slot = *argument<std::int64_t>(command, "slot_id");
        if (slot < 0 || slot > std::numeric_limits<std::uint32_t>::max())
            error = "sample slot is outside its range";
        else if (!samples_.load(SampleSlotId{static_cast<std::uint32_t>(slot)},
                                *argument<std::string>(command, "path")))
            error = "sample command queue rejected the request";
        else
            changed = true;
    } else if (command.action_id == "sample.unload.v1") {
        const auto slot = *argument<std::int64_t>(command, "slot_id");
        if (slot < 0 || slot > std::numeric_limits<std::uint32_t>::max())
            error = "sample slot is outside its range";
        else if (!samples_.unload(
                     SampleSlotId{static_cast<std::uint32_t>(slot)}))
            error = "sample command queue rejected the request";
        else
            changed = true;
    } else {
        std::lock_guard lock(mutex_);
        synchronize_locked();
        if (command.action_id == "track.select.v1") {
            const auto id = *argument<std::int64_t>(command, "track_id");
            const auto timeline = timeline_.snapshot();
            if (id <= 0 || std::none_of(timeline.timeline.tracks.begin(),
                                        timeline.timeline.tracks.end(),
                                        [&](const Track &track) {
                                            return track.id ==
                                                   static_cast<TrackId>(id);
                                        })) {
                error = "track does not exist";
            } else {
                state_.selected_track = static_cast<TrackId>(id);
                changed_locked();
            }
        } else if (command.action_id == "track.next.v1" ||
                   command.action_id == "track.previous.v1") {
            const auto timeline = timeline_.snapshot();
            if (timeline.timeline.tracks.empty())
                error = "no tracks are available";
            else {
                auto found = std::find_if(
                    timeline.timeline.tracks.begin(),
                    timeline.timeline.tracks.end(), [&](const Track &track) {
                        return track.id == state_.selected_track;
                    });
                auto index =
                    found == timeline.timeline.tracks.end()
                        ? 0
                        : static_cast<std::size_t>(
                              found - timeline.timeline.tracks.begin());
                if (command.action_id == "track.next.v1")
                    index = (index + 1) % timeline.timeline.tracks.size();
                else
                    index = (index + timeline.timeline.tracks.size() - 1) %
                            timeline.timeline.tracks.size();
                state_.selected_track = timeline.timeline.tracks[index].id;
                changed_locked();
            }
        } else if (command.action_id == "track.mute-toggle.v1") {
            auto transaction = timeline_.edit();
            auto *track = transaction.find_track(state_.selected_track);
            if (!track)
                error = "selected track does not exist";
            else if (!transaction.set_track_muted(track->id, !track->muted,
                                                  error) ||
                     !timeline_.commit(
                         std::move(transaction),
                         TimelineCommitPolicy::next_audio_callback, error)) {
            } else {
                changed_locked();
            }
        } else if (command.action_id == "track.instrument-set.v1") {
            auto transaction = timeline_.edit();
            if (!transaction.set_track_instrument(
                    state_.selected_track,
                    *argument<std::string>(command, "instrument_id"), error) ||
                !timeline_.commit(std::move(transaction),
                                  TimelineCommitPolicy::next_audio_callback,
                                  error)) {
            } else
                changed_locked();
        } else if (command.action_id == "pattern.select.v1") {
            const auto id = *argument<std::int64_t>(command, "pattern_id");
            if (id < 0)
                error = "pattern ID must be nonnegative";
            else {
                state_.selected_pattern = static_cast<std::uint64_t>(id);
                changed_locked();
            }
        } else if (command.action_id == "step.select.v1") {
            const auto step = *argument<std::int64_t>(command, "step");
            if (step < 0)
                error = "step must be nonnegative";
            else {
                state_.selected_step = step;
                state_.step_page = static_cast<std::uint64_t>(step) / 16;
                selected_event_id_.reset();
                changed_locked();
            }
        } else if (command.action_id == "step.event-select.v1") {
            const auto step_arg = argument<std::int64_t>(command, "step");
            const auto event_arg = argument<std::int64_t>(command, "event_id");
            const auto step = step_arg ? *step_arg : state_.selected_step;
            const auto snapshot = timeline_.snapshot();
            const auto track = std::find_if(
                snapshot.timeline.tracks.begin(),
                snapshot.timeline.tracks.end(), [&](const Track &value) {
                    return value.id == state_.selected_track;
                });
            if (step < 0 || track == snapshot.timeline.tracks.end())
                error = step < 0 ? "step must be nonnegative"
                                 : "selected track does not exist";
            else {
                const auto begin = step * state_.step_ticks;
                const auto end = std::min<MusicalTick>(
                    begin + state_.step_ticks, track->pattern.length);
                const MusicalEvent *selected = nullptr;
                for (const auto &event : track->pattern.events)
                    if (event.type == MusicalEventType::note &&
                        event.tick >= begin && event.tick < end &&
                        (!event_arg ||
                         (*event_arg > 0 &&
                          event.id == static_cast<EventId>(*event_arg)))) {
                        if (!selected || event.id < selected->id)
                            selected = &event;
                    }
                if (!selected)
                    error = "no selectable note event at step";
                else {
                    state_.selected_step = step;
                    state_.step_page = static_cast<std::uint64_t>(step) / 16;
                    selected_event_id_ = selected->id;
                    changed_locked();
                }
            }
        } else if (command.action_id == "performance.fill-set.v1" ||
                   command.action_id == "performance.fill-latch.v1") {
            const auto previous_latched = state_.fill_latched;
            const auto previous_momentary = fill_momentary_;
            if (command.action_id == "performance.fill-latch.v1") {
                state_.fill_latched = !state_.fill_latched;
            } else {
                fill_momentary_ = *argument<bool>(command, "active");
            }
            state_.fill_active = fill_momentary_ || state_.fill_latched;
            if (!engine_.set_fill(state_.fill_active)) {
                state_.fill_latched = previous_latched;
                fill_momentary_ = previous_momentary;
                state_.fill_active = fill_momentary_ || state_.fill_latched;
                error = "transport command queue is full";
            } else
                changed_locked();
        } else if (command.action_id == "timeline.seed-set.v1") {
            const auto value = *argument<std::int64_t>(command, "seed");
            if (value < 0)
                error = "generation seed must be nonnegative";
            else {
                auto transaction =
                    edit_group_ ? *edit_group_ : timeline_.edit();
                transaction.set_generation_seed(
                    static_cast<std::uint64_t>(value));
                if (edit_group_)
                    edit_group_ = std::move(transaction);
                else if (!timeline_.commit(
                             std::move(transaction),
                             TimelineCommitPolicy::next_audio_callback,
                             error)) {
                }
                if (error.empty())
                    changed_locked();
            }
        } else if (command.action_id == "step.probability-set.v1" ||
                   command.action_id == "step.condition-set.v1" ||
                   command.action_id == "step.ratchet-set.v1" ||
                   command.action_id == "step.trigger-reset.v1") {
            if (!selected_event_id_)
                error = "no note event is selected";
            else {
                auto transaction =
                    edit_group_ ? *edit_group_ : timeline_.edit();
                auto *track = transaction.find_track(state_.selected_track);
                MusicalEvent *event = nullptr;
                if (track)
                    for (auto &candidate : track->pattern.events)
                        if (candidate.id == *selected_event_id_ &&
                            candidate.type == MusicalEventType::note) {
                            event = &candidate;
                            break;
                        }
                if (!event)
                    error = "selected note event no longer exists";
                else if (command.action_id == "step.probability-set.v1") {
                    const auto value =
                        *argument<double>(command, "probability");
                    if (!std::isfinite(value) || value < 0 || value > 1)
                        error = "trigger probability must be 0..1";
                    else
                        event->trigger_probability = static_cast<float>(value);
                } else if (command.action_id == "step.ratchet-set.v1") {
                    const auto value =
                        *argument<std::int64_t>(command, "count");
                    if (value < 1 || value > 8)
                        error = "ratchet count must be 1..8";
                    else
                        event->ratchet_count = static_cast<std::uint8_t>(value);
                } else if (command.action_id == "step.condition-set.v1") {
                    const auto condition = parse_trigger_condition(
                        *argument<std::string>(command, "condition"));
                    const auto n_arg = argument<std::int64_t>(command, "n");
                    const auto m_arg = argument<std::int64_t>(command, "m");
                    const auto n = n_arg ? *n_arg : 1;
                    const auto m = m_arg ? *m_arg : n;
                    if (!condition)
                        error = "unknown trigger condition";
                    else if (n < 1 || m < 1 || n > m || m > 65535)
                        error = "condition cycles require 1 <= n <= m <= 65535";
                    else {
                        event->trigger_condition = *condition;
                        event->condition_n = static_cast<std::uint16_t>(n);
                        event->condition_m = static_cast<std::uint16_t>(m);
                    }
                } else {
                    event->trigger_probability = 1.0F;
                    event->trigger_condition = TriggerCondition::always;
                    event->condition_n = event->condition_m = 1;
                    event->ratchet_count = 1;
                }
                if (error.empty()) {
                    if (edit_group_)
                        edit_group_ = std::move(transaction);
                    else
                        timeline_.commit(
                            std::move(transaction),
                            TimelineCommitPolicy::next_audio_callback, error);
                    if (error.empty())
                        changed_locked();
                }
            }
        } else if (command.action_id == "step.property-select.v1") {
            const auto &property = *argument<std::string>(command, "property");
            if (property != "velocity" && property != "duration" &&
                property != "pitch" && property != "timing" &&
                property != "probability" && property != "condition" &&
                property != "ratchet")
                error = "unknown step property";
            else {
                state_.selected_step_property = property;
                state_.focused_element = "step-properties";
                changed_locked();
            }
        } else if (command.action_id == "step.lock-select.v1") {
            const auto &id = *argument<std::string>(command, "parameter_id");
            if (!resolve_parameter_destination(id) || !parameters_.get(id))
                error = "unknown parameter lock destination";
            else {
                state_.selected_lock_parameter = id;
                state_.focused_element = "step-locks";
                changed_locked();
            }
        } else if (command.action_id == "step.lock-set.v1" ||
                   command.action_id == "step.lock-remove.v1" ||
                   command.action_id == "step.locks-clear.v1") {
            auto transaction = edit_group_ ? *edit_group_ : timeline_.edit();
            auto *track = transaction.find_track(state_.selected_track);
            const auto tick = state_.selected_step * state_.step_ticks;
            if (!track || tick < 0 || tick >= track->pattern.length)
                error = "selected step is outside the track";
            else {
                std::vector<EventId> remove;
                std::string id;
                if (command.action_id != "step.locks-clear.v1")
                    id = *argument<std::string>(command, "parameter_id");
                for (const auto &event : track->pattern.events)
                    if (event.type == MusicalEventType::parameter &&
                        event.tick == tick &&
                        (id.empty() || event.parameter_destination == id))
                        remove.push_back(event.id);
                bool edited = true;
                if (command.action_id == "step.lock-set.v1") {
                    const auto destination = resolve_parameter_destination(id);
                    const auto descriptor = parameters_.get(id);
                    const auto value = *argument<double>(command, "value");
                    if (!destination || !descriptor)
                        error = "unknown parameter lock destination";
                    else if (!std::isfinite(value) ||
                             value < number(descriptor->minimum) ||
                             value > number(descriptor->maximum))
                        error = "parameter lock value is outside its range";
                    else if (destination->target ==
                                 ParameterTarget::subtractive_synth &&
                             track->instrument_id != subtractive_instrument_id)
                        error = "synth parameter lock requires a synth track";
                    else if (!remove.empty())
                        edited = transaction.modify_parameter_destination(
                            track->id, remove.front(), tick, id,
                            static_cast<float>(value), error);
                    else
                        edited = transaction.add_parameter_destination(
                            track->id, transaction.next_event_id(), tick, id,
                            static_cast<float>(value), error);
                } else {
                    if (command.action_id == "step.lock-remove.v1" &&
                        remove.empty())
                        error = "parameter lock does not exist";
                    for (const auto event_id : remove)
                        if (!transaction.remove_event(track->id, event_id,
                                                      error)) {
                            edited = false;
                            break;
                        }
                }
                if (error.empty() && edited &&
                    (edit_group_ ||
                     timeline_.commit(transaction,
                                      TimelineCommitPolicy::next_audio_callback,
                                      error))) {
                    if (edit_group_)
                        edit_group_ = std::move(transaction);
                    if (!id.empty())
                        state_.selected_lock_parameter = id;
                    changed_locked();
                }
            }
        } else if (command.action_id == "edit.undo.v1" ||
                   command.action_id == "edit.redo.v1") {
            if (edit_group_)
                error = "finish or cancel the grouped edit first";
            else {
                const bool ok =
                    command.action_id == "edit.undo.v1"
                        ? timeline_.undo(
                              TimelineCommitPolicy::next_audio_callback, error)
                        : timeline_.redo(
                              TimelineCommitPolicy::next_audio_callback, error);
                if (ok) {
                    selected_event_id_.reset();
                    changed_locked();
                }
            }
        } else if (command.action_id == "edit.group-begin.v1") {
            if (edit_group_)
                error = "a grouped edit is already active";
            else {
                edit_group_ = timeline_.edit();
                state_.edit_group_active = true;
                changed_locked();
            }
        } else if (command.action_id == "edit.group-cancel.v1") {
            if (!edit_group_)
                error = "no grouped edit is active";
            else {
                edit_group_.reset();
                state_.edit_group_active = false;
                changed_locked();
            }
        } else if (command.action_id == "edit.group-commit.v1") {
            if (!edit_group_)
                error = "no grouped edit is active";
            else if (timeline_.commit(*edit_group_,
                                      TimelineCommitPolicy::next_audio_callback,
                                      error)) {
                edit_group_.reset();
                state_.edit_group_active = false;
                changed_locked();
            }
        } else if (command.action_id == "step.velocity-set.v1" ||
                   command.action_id == "step.duration-set.v1" ||
                   command.action_id == "step.pitch-set.v1" ||
                   command.action_id == "step.timing-offset-set.v1" ||
                   command.action_id == "step.property-reset.v1") {
            if (!selected_event_id_)
                error = "no note event is selected";
            else {
                auto transaction =
                    edit_group_ ? *edit_group_ : timeline_.edit();
                auto *track = transaction.find_track(state_.selected_track);
                MusicalEvent *event = nullptr;
                if (track)
                    for (auto &candidate : track->pattern.events)
                        if (candidate.id == *selected_event_id_ &&
                            candidate.type == MusicalEventType::note) {
                            event = &candidate;
                            break;
                        }
                if (!event)
                    error = "selected note event no longer exists";
                else {
                    auto tick = event->tick;
                    auto pitch = event->pitch;
                    auto velocity = event->velocity;
                    auto duration = event->duration;
                    const auto base = state_.selected_step * state_.step_ticks;
                    if (command.action_id == "step.velocity-set.v1") {
                        const auto value =
                            *argument<double>(command, "velocity");
                        if (!std::isfinite(value) || value < 0 || value > 1)
                            error = "step velocity must be 0..1";
                        else
                            velocity = static_cast<float>(value);
                    } else if (command.action_id == "step.duration-set.v1") {
                        const auto value =
                            *argument<std::int64_t>(command, "duration_ticks");
                        if (value <= 0)
                            error = "step duration must be positive";
                        else
                            duration = value;
                    } else if (command.action_id == "step.pitch-set.v1") {
                        const auto value =
                            *argument<std::int64_t>(command, "pitch");
                        if (value < 0 || value > 127)
                            error = "step pitch must be 0..127";
                        else
                            pitch = static_cast<std::uint8_t>(value);
                    } else if (command.action_id ==
                               "step.timing-offset-set.v1") {
                        const auto value =
                            *argument<std::int64_t>(command, "offset_ticks");
                        if (value <= -state_.step_ticks ||
                            value >= state_.step_ticks || base + value < 0 ||
                            base + value >= track->pattern.length)
                            error = "timing offset must remain within one step "
                                    "and the pattern";
                        else
                            tick = base + value;
                    } else {
                        const auto &property =
                            *argument<std::string>(command, "property");
                        if (property == "velocity")
                            velocity = state_.step_velocity;
                        else if (property == "duration")
                            duration = state_.step_duration;
                        else if (property == "pitch")
                            pitch = track_targets_.contains(track->id)
                                        ? track_targets_[track->id]
                                        : 36;
                        else if (property == "timing")
                            tick = base;
                        else
                            error = "unknown step property";
                    }
                    if (error.empty() && transaction.modify_note(
                                             track->id, event->id, tick, pitch,
                                             velocity, duration, error)) {
                        if (edit_group_) {
                            edit_group_ = std::move(transaction);
                            changed_locked();
                        } else if (timeline_.commit(std::move(transaction),
                                                    TimelineCommitPolicy::
                                                        next_audio_callback,
                                                    error))
                            changed_locked();
                    }
                }
            }
        } else if (command.action_id == "step.page-next.v1" ||
                   command.action_id == "step.page-previous.v1") {
            const auto snapshot = timeline_.snapshot();
            const auto found = std::find_if(
                snapshot.timeline.tracks.begin(),
                snapshot.timeline.tracks.end(), [&](const Track &track) {
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
            auto transaction = edit_group_ ? *edit_group_ : timeline_.edit();
            auto *track = transaction.find_track(state_.selected_track);
            const auto requested = argument<std::int64_t>(command, "step");
            const auto step = requested ? *requested : state_.selected_step;
            const auto velocity = argument<double>(command, "velocity");
            const auto duration =
                argument<std::int64_t>(command, "duration_ticks");
            if (!track)
                error = "selected track does not exist";
            else if (step < 0 ||
                     step > std::numeric_limits<MusicalTick>::max() /
                                state_.step_ticks)
                error = "step is outside its range";
            else {
                const auto tick = step * state_.step_ticks;
                const auto count =
                    (track->pattern.length + state_.step_ticks - 1) /
                    state_.step_ticks;
                if (step >= count || tick >= track->pattern.length)
                    error = "step is outside the selected track";
                else if (velocity && (!std::isfinite(*velocity) ||
                                      *velocity < 0 || *velocity > 1))
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
                    for (const auto &event : track->pattern.events)
                        if (event.type == MusicalEventType::note &&
                            event.tick == tick && event.pitch == target)
                            matching.push_back(event.id);
                    bool edited = false;
                    if (!matching.empty()) {
                        edited = true;
                        for (const auto id : matching)
                            if (!transaction.remove_event(track->id, id,
                                                          error)) {
                                edited = false;
                                break;
                            }
                    } else
                        edited = transaction.add_note(
                            track->id, transaction.next_event_id(), tick,
                            target,
                            static_cast<float>(velocity ? *velocity
                                                        : state_.step_velocity),
                            duration ? *duration : state_.step_duration, error);
                    if (edited &&
                        (edit_group_ ||
                         timeline_.commit(
                             transaction,
                             TimelineCommitPolicy::next_audio_callback,
                             error))) {
                        if (edit_group_)
                            edit_group_ = std::move(transaction);
                        state_.selected_step = step;
                        state_.step_page =
                            static_cast<std::uint64_t>(step) / 16;
                        selected_event_id_.reset();
                        changed_locked();
                    }
                }
            }
        } else if (command.action_id.rfind("slice.", 0) == 0 ||
                   command.action_id == "sample.preview.v1" ||
                   (command.action_id.rfind("pad.", 0) == 0 &&
                    command.action_id != "pad.mode-select.v1")) {
            auto refresh_slices = [&] {
                const auto value =
                    slices_.snapshot(state_.selected_sample_slot);
                state_.slice_revision = value.revision;
                state_.sample_frames = value.sample_frames;
                state_.sample_rate = value.sample_rate;
                state_.sample_channels = value.channels;
                state_.slice_pad_assignments = value.pad_assignments;
                state_.pad_settings = value.pad_settings;
                state_.selected_pad_settings =
                    value.pad_settings[state_.selected_pad];
                state_.stretch_latency_frames =
                    RealtimeTimeStretcher::latency_frames(
                        {state_.selected_pad_settings.stretch_mode,
                         state_.selected_pad_settings.stretch_quality,
                         state_.selected_pad_settings.stretch_ratio, 1.0F,
                         engine_.sample_rate()});
                state_.waveform_peaks = value.waveform;
                state_.slices.clear();
                for (const auto &slice : value.slices)
                    state_.slices.push_back({slice.id, slice.name,
                                             slice.start_frame, slice.end_frame,
                                             slice.gain, slice.root_note});
                if (state_.selected_slice != 0 &&
                    std::none_of(state_.slices.begin(), state_.slices.end(),
                                 [&](const auto &slice) {
                                     return slice.id == state_.selected_slice;
                                 }))
                    state_.selected_slice = 0;
            };
            if (command.action_id == "slice.select.v1") {
                const auto id = *argument<std::int64_t>(command, "slice_id");
                if (id <= 0 ||
                    std::none_of(state_.slices.begin(), state_.slices.end(),
                                 [&](const auto &slice) {
                                     return slice.id ==
                                            static_cast<SliceId>(id);
                                 }))
                    error = "slice does not exist";
                else
                    state_.selected_slice = static_cast<SliceId>(id);
            } else if (command.action_id == "slice.create.v1") {
                const auto start =
                    *argument<std::int64_t>(command, "start_frame");
                const auto end = *argument<std::int64_t>(command, "end_frame");
                std::string name = "Slice";
                if (const auto found = command.arguments.find("name");
                    found != command.arguments.end())
                    name = std::get<std::string>(found->second);
                SliceId created{};
                if (start < 0 || end < 0 ||
                    !slices_.create(state_.selected_sample_slot,
                                    state_.slice_revision,
                                    static_cast<std::uint64_t>(start),
                                    static_cast<std::uint64_t>(end),
                                    std::move(name), created, error)) {
                    if (error.empty())
                        error = "slice boundaries are invalid";
                } else
                    state_.selected_slice = created;
            } else if (command.action_id == "slice.delete.v1") {
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else if (slices_.erase(state_.selected_slice,
                                       state_.slice_revision, error))
                    state_.selected_slice = 0;
            } else if (command.action_id == "slice.bounds-set.v1") {
                const auto start =
                    *argument<std::int64_t>(command, "start_frame");
                const auto end = *argument<std::int64_t>(command, "end_frame");
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else if (start < 0 || end < 0)
                    error = "slice boundaries cannot be negative";
                else
                    (void)slices_.set_bounds(
                        state_.selected_slice, state_.slice_revision,
                        static_cast<std::uint64_t>(start),
                        static_cast<std::uint64_t>(end), error);
            } else if (command.action_id == "slice.rename.v1") {
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else
                    (void)slices_.rename(
                        state_.selected_slice, state_.slice_revision,
                        *argument<std::string>(command, "name"), error);
            } else if (command.action_id == "slice.duplicate.v1") {
                SliceId created{};
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else if (slices_.duplicate(state_.selected_slice,
                                           state_.slice_revision, created,
                                           error))
                    state_.selected_slice = created;
            } else if (command.action_id == "slice.clear.v1") {
                if (slices_.clear(state_.selected_sample_slot,
                                  state_.slice_revision, error))
                    state_.selected_slice = 0;
            } else if (command.action_id == "slice.equal-divisions.v1") {
                const auto divisions =
                    *argument<std::int64_t>(command, "divisions");
                if (divisions <= 0)
                    error = "division count must be positive";
                else if (slices_.equal_divisions(
                             state_.selected_sample_slot, state_.slice_revision,
                             static_cast<std::size_t>(divisions), error))
                    state_.selected_slice = 0;
            } else if (command.action_id == "slice.detect-transients.v1") {
                const auto separation =
                    *argument<std::int64_t>(command, "minimum_frames");
                if (separation <= 0)
                    error = "minimum separation must be positive";
                else if (slices_.detect_transients(
                             state_.selected_sample_slot, state_.slice_revision,
                             static_cast<float>(
                                 *argument<double>(command, "sensitivity")),
                             static_cast<std::uint64_t>(separation), error))
                    state_.selected_slice = 0;
            } else if (command.action_id == "slice.pad-assign.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else if (pad < 0 ||
                         !slices_.assign_pad(static_cast<std::size_t>(pad),
                                             state_.selected_slice,
                                             state_.slice_revision, error)) {
                    if (error.empty())
                        error = "pad index is invalid";
                }
            } else if (command.action_id == "slice.pad-clear.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                if (pad < 0 ||
                    !slices_.clear_pad(static_cast<std::size_t>(pad),
                                       state_.slice_revision, error)) {
                    if (error.empty())
                        error = "pad index is invalid";
                }
            } else if (command.action_id == "slice.preview.v1") {
                if (state_.selected_slice == 0)
                    error = "no slice is selected";
                else
                    (void)slices_.preview(state_.selected_slice, 1.0F, error);
            } else if (command.action_id == "sample.preview.v1") {
                (void)slices_.preview_full(state_.selected_sample_slot, 1.0F,
                                           error);
            } else if (command.action_id == "slice.preview-stop.v1") {
                slices_.stop_preview();
            } else if (command.action_id == "pad.select.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                if (pad < 0 || pad >= 16)
                    error = "pad index must be 0..15";
                else
                    state_.selected_pad = static_cast<std::size_t>(pad);
            } else if (command.action_id == "pad.property-select.v1") {
                static constexpr std::string_view properties[]{
                    "mode",    "choke",      "pitch",          "fine",
                    "gain",    "reverse",    "attack",         "decay",
                    "sustain", "release",    "stretch-mode",   "stretch-ratio",
                    "beats",   "source-bpm", "stretch-quality"};
                const auto &property =
                    *argument<std::string>(command, "property");
                if (std::find(properties, properties + 15, property) ==
                    properties + 15)
                    error = "unknown pad playback property";
                else {
                    state_.selected_pad_property = property;
                    state_.focused_element = "pad-playback";
                }
            } else if (command.action_id == "pad.trigger.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                double velocity = 1.0;
                double pitch_offset = 0.0;
                if (const auto found = command.arguments.find("velocity");
                    found != command.arguments.end())
                    velocity = std::get<double>(found->second);
                if (const auto found = command.arguments.find("pitch_offset");
                    found != command.arguments.end())
                    pitch_offset = std::get<double>(found->second);
                if (pad < 0 || pad >= 16 || velocity < 0 || velocity > 1 ||
                    pitch_offset < -96 || pitch_offset > 96)
                    error = "pad or velocity is outside its range";
                else if (!engine_.trigger_pad(
                             static_cast<std::uint8_t>(36 + pad),
                             static_cast<float>(velocity), true,
                             static_cast<float>(pitch_offset)))
                    error = "pad trigger queue is full";
            } else if (command.action_id == "pad.release.v1") {
                const auto pad = *argument<std::int64_t>(command, "pad");
                if (pad < 0 || pad >= 16)
                    error = "pad index must be 0..15";
                else if (!engine_.trigger_pad(
                             static_cast<std::uint8_t>(36 + pad), 0, false))
                    error = "pad release queue is full";
            } else {
                auto settings = state_.pad_settings[state_.selected_pad];
                if (command.action_id == "pad.playback-mode-set.v1") {
                    const auto mode = parse_sample_playback_mode(
                        *argument<std::string>(command, "mode"));
                    if (!mode)
                        error = "unknown sample playback mode";
                    else
                        settings.mode = *mode;
                } else if (command.action_id == "pad.choke-group-set.v1") {
                    const auto group =
                        *argument<std::int64_t>(command, "group");
                    if (group < 0 || group > 16)
                        error = "choke group must be 0..16";
                    else
                        settings.choke_group = static_cast<std::uint8_t>(group);
                } else if (command.action_id == "pad.choke-release-set.v1")
                    settings.choke_release =
                        *argument<bool>(command, "release");
                else if (command.action_id == "pad.pitch-set.v1")
                    settings.semitones = static_cast<float>(
                        *argument<double>(command, "semitones"));
                else if (command.action_id == "pad.fine-set.v1")
                    settings.fine_cents =
                        static_cast<float>(*argument<double>(command, "cents"));
                else if (command.action_id == "pad.gain-set.v1")
                    settings.gain =
                        static_cast<float>(*argument<double>(command, "gain"));
                else if (command.action_id == "pad.reverse-set.v1")
                    settings.reverse = *argument<bool>(command, "reverse");
                else if (command.action_id == "pad.envelope-set.v1") {
                    settings.attack_seconds = static_cast<float>(
                        *argument<double>(command, "attack"));
                    settings.decay_seconds =
                        static_cast<float>(*argument<double>(command, "decay"));
                    settings.sustain = static_cast<float>(
                        *argument<double>(command, "sustain"));
                    settings.release_seconds = static_cast<float>(
                        *argument<double>(command, "release"));
                } else if (command.action_id == "pad.stretch-mode-set.v1") {
                    const auto mode = parse_stretch_mode(
                        *argument<std::string>(command, "mode"));
                    if (!mode)
                        error = "unknown stretch mode";
                    else
                        settings.stretch_mode = *mode;
                } else if (command.action_id == "pad.stretch-ratio-set.v1")
                    settings.stretch_ratio =
                        static_cast<float>(*argument<double>(command, "ratio"));
                else if (command.action_id == "pad.source-beats-set.v1")
                    settings.source_beats =
                        static_cast<float>(*argument<double>(command, "beats"));
                else if (command.action_id == "pad.source-bpm-set.v1")
                    settings.source_bpm =
                        static_cast<float>(*argument<double>(command, "bpm"));
                else if (command.action_id == "pad.stretch-quality-set.v1") {
                    const auto quality = parse_stretch_quality(
                        *argument<std::string>(command, "quality"));
                    if (!quality)
                        error = "unknown stretch quality";
                    else
                        settings.stretch_quality = *quality;
                } else if (command.action_id == "pad.stretch-reset.v1") {
                    settings.stretch_mode = StretchMode::off;
                    settings.stretch_quality = StretchQuality::balanced;
                    settings.stretch_ratio = 1;
                    settings.source_beats = 0;
                    settings.source_bpm = 0;
                } else
                    error = "unsupported pad action";
                if (error.empty())
                    (void)slices_.set_pad_settings(
                        state_.selected_sample_slot, state_.selected_pad,
                        state_.slice_revision, settings, error);
            }
            if (error.empty()) {
                refresh_slices();
                changed_locked();
            }
        } else if (command.action_id == "sample.slot-select.v1") {
            const auto id = *argument<std::int64_t>(command, "slot_id");
            if (id < 0 || id > std::numeric_limits<std::uint32_t>::max())
                error = "sample slot is outside its range";
            else {
                state_.selected_sample_slot =
                    SampleSlotId{static_cast<std::uint32_t>(id)};
                state_.selected_slice = 0;
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
            static constexpr std::string_view focus[]{
                "transport", "tracks", "samples", "pad-playback", "parameters"};
            auto found = std::find(focus, focus + 5, state_.focused_element);
            std::size_t index = found == focus + 5
                                    ? 0
                                    : static_cast<std::size_t>(found - focus);
            index = command.action_id == "navigation.focus-next.v1"
                        ? (index + 1) % 5
                        : (index + 4) % 5;
            state_.focused_element = focus[index];
            changed_locked();
        } else if (command.action_id == "navigation.confirm.v1") {
            if (state_.focused_element == "transport")
                state_.focused_element = "tracks";
            else if (state_.focused_element == "tracks")
                state_.focused_element = "steps";
            else if (state_.focused_element == "step-properties") {
                static constexpr std::string_view properties[]{
                    "velocity",    "duration",  "pitch",  "timing",
                    "probability", "condition", "ratchet"};
                const auto found = std::find(properties, properties + 7,
                                             state_.selected_step_property);
                const auto index =
                    found == properties + 7
                        ? 0
                        : (static_cast<std::size_t>(found - properties) + 1) %
                              7;
                if (state_.selected_step_property == "ratchet")
                    state_.focused_element = "step-locks";
                else
                    state_.selected_step_property = properties[index];
            } else if (state_.focused_element == "step-locks") {
                std::vector<std::string> destinations;
                for (const auto &parameter : state_.parameters)
                    if (resolve_parameter_destination(parameter.id))
                        destinations.push_back(parameter.id);
                const auto found =
                    std::find(destinations.begin(), destinations.end(),
                              state_.selected_lock_parameter);
                if (!destinations.empty())
                    state_.selected_lock_parameter =
                        destinations[(found == destinations.end()
                                          ? 0
                                          : static_cast<std::size_t>(
                                                found - destinations.begin() +
                                                1)) %
                                     destinations.size()];
            } else if (state_.focused_element == "pad-playback") {
                static constexpr std::string_view properties[]{
                    "mode",    "choke",      "pitch",          "fine",
                    "gain",    "reverse",    "attack",         "decay",
                    "sustain", "release",    "stretch-mode",   "stretch-ratio",
                    "beats",   "source-bpm", "stretch-quality"};
                const auto found = std::find(properties, properties + 15,
                                             state_.selected_pad_property);
                const auto index =
                    found == properties + 15
                        ? 0
                        : (static_cast<std::size_t>(found - properties) + 1) %
                              15;
                state_.selected_pad_property = properties[index];
            } else
                state_.focused_element += ".confirmed";
            changed_locked();
        } else if (command.action_id == "navigation.cancel.v1") {
            state_.focused_element =
                state_.focused_element == "pad-playback" ? "samples"
                : state_.focused_element == "step-locks" ? "step-properties"
                : state_.focused_element == "step-properties" ? "steps"
                : state_.focused_element == "steps"           ? "tracks"
                                                              : "transport";
            changed_locked();
        } else if (command.action_id == "navigation.adjust.v1") {
            const auto delta = *argument<double>(command, "steps");
            const auto direction = delta < 0 ? -1 : delta > 0 ? 1 : 0;
            if (!std::isfinite(delta))
                error = "navigation adjustment must be finite";
            else if (direction == 0) {
            } else if (state_.focused_element == "tracks") {
                const auto timeline = timeline_.snapshot();
                if (timeline.timeline.tracks.empty())
                    error = "no tracks are available";
                else {
                    auto found = std::find_if(timeline.timeline.tracks.begin(),
                                              timeline.timeline.tracks.end(),
                                              [&](const Track &track) {
                                                  return track.id ==
                                                         state_.selected_track;
                                              });
                    auto index =
                        found == timeline.timeline.tracks.end()
                            ? 0
                            : static_cast<std::size_t>(
                                  found - timeline.timeline.tracks.begin());
                    index =
                        direction > 0
                            ? (index + 1) % timeline.timeline.tracks.size()
                            : (index + timeline.timeline.tracks.size() - 1) %
                                  timeline.timeline.tracks.size();
                    state_.selected_track = timeline.timeline.tracks[index].id;
                    state_.selected_step = 0;
                    state_.step_page = 0;
                    changed_locked();
                }
            } else if (state_.focused_element == "steps") {
                const auto track =
                    std::find_if(state_.tracks.begin(), state_.tracks.end(),
                                 [&](const TrackSummary &value) {
                                     return value.id == state_.selected_track;
                                 });
                if (track == state_.tracks.end())
                    error = "selected track does not exist";
                else {
                    const auto steps =
                        (track->pattern_length + state_.step_ticks - 1) /
                        state_.step_ticks;
                    auto step = state_.selected_step + direction;
                    if (step < 0)
                        step = steps - 1;
                    if (step >= steps)
                        step = 0;
                    state_.selected_step = step;
                    state_.step_page = static_cast<std::uint64_t>(step) / 16;
                    changed_locked();
                }
            } else if (state_.focused_element == "transport") {
                if (parameters_.adjust("transport.tempo", delta, error))
                    changed_locked();
            }
        } else if (command.action_id == "text.input.v1") {
            state_.focused_element =
                "text:" + *argument<std::string>(command, "text");
            changed_locked();
        } else if (command.action_id == "parameter.set-normalized.v1") {
            const auto id = *argument<std::string>(command, "parameter_id");
            const auto previous = parameters_.get(id);
            if (parameters_.set_normalized(
                    id, *argument<double>(command, "value"), error)) {
                if (!publish_dsp_parameter(engine_, parameters_, id)) {
                    std::string rollback_error;
                    if (previous)
                        parameters_.set(id, previous->current_value,
                                        rollback_error);
                    error = "DSP parameter queue is full";
                } else
                    changed_locked();
            }
        } else if (command.action_id == "parameter.adjust.v1") {
            const auto id = *argument<std::string>(command, "parameter_id");
            const auto previous = parameters_.get(id);
            if (parameters_.adjust(id, *argument<double>(command, "steps"),
                                   error)) {
                if (!publish_dsp_parameter(engine_, parameters_, id)) {
                    std::string rollback_error;
                    if (previous)
                        parameters_.set(id, previous->current_value,
                                        rollback_error);
                    error = "DSP parameter queue is full";
                } else
                    changed_locked();
            }
        } else if (command.action_id == "parameter.reset.v1") {
            const auto id = *argument<std::string>(command, "parameter_id");
            const auto previous = parameters_.get(id);
            if (parameters_.reset(id, error)) {
                if (!publish_dsp_parameter(engine_, parameters_, id)) {
                    std::string rollback_error;
                    if (previous)
                        parameters_.set(id, previous->current_value,
                                        rollback_error);
                    error = "DSP parameter queue is full";
                } else
                    changed_locked();
            }
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
    if (changed && (command.action_id.rfind("sample.", 0) == 0 ||
                    command.action_id.rfind("transport.", 0) == 0)) {
        std::lock_guard lock(mutex_);
        changed_locked();
    }
    return {error.empty(), sequence, snapshot().revision, std::move(error)};
}
void ApplicationService::synchronize_locked() const {
    const auto timeline = timeline_.snapshot();
    const auto history = timeline_.history_status();
    const auto playing = engine_.playing();
    const auto frame = engine_.frame_position();
    auto slots = samples_.states();
    auto slice_state = slices_.snapshot(state_.selected_sample_slot);
    auto parameters = parameters_.parameters();
    bool external = state_.playing != playing ||
                    state_.transport_frame != frame ||
                    state_.timeline_revision != timeline.revision ||
                    state_.can_undo != history.can_undo ||
                    state_.can_redo != history.can_redo ||
                    state_.undo_depth != history.undo_depth ||
                    state_.redo_depth != history.redo_depth ||
                    state_.sample_slots.size() != slots.size() ||
                    state_.slice_revision != slice_state.revision ||
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
    const auto transport_tick =
        (static_cast<long double>(frame) * ticks_per_quarter * 1'000'000.0L) /
        (static_cast<long double>(engine_.sample_rate()) *
         timeline.timeline.transport.microseconds_per_quarter);
    state_.transport_tick =
        transport_tick >= std::numeric_limits<MusicalTick>::max()
            ? std::numeric_limits<MusicalTick>::max()
            : static_cast<MusicalTick>(transport_tick);
    state_.timeline_revision = timeline.revision;
    state_.can_undo = history.can_undo;
    state_.can_redo = history.can_redo;
    state_.undo_depth = history.undo_depth;
    state_.redo_depth = history.redo_depth;
    state_.history_capacity = history.capacity;
    state_.slice_revision = slice_state.revision;
    state_.sample_frames = slice_state.sample_frames;
    state_.sample_rate = slice_state.sample_rate;
    state_.sample_channels = slice_state.channels;
    state_.slice_pad_assignments = slice_state.pad_assignments;
    state_.pad_settings = slice_state.pad_settings;
    state_.selected_pad_settings =
        slice_state.pad_settings[state_.selected_pad];
    state_.effective_stretch_ratio = state_.selected_pad_settings.stretch_ratio;
    const auto current_tempo =
        60'000'000.0 / timeline.timeline.transport.microseconds_per_quarter;
    auto stretch_source_frames = slice_state.sample_frames;
    if (const auto assigned = slice_state.pad_assignments[state_.selected_pad])
        if (const auto found = std::find_if(
                slice_state.slices.begin(), slice_state.slices.end(),
                [&](const auto &slice) { return slice.id == *assigned; });
            found != slice_state.slices.end())
            stretch_source_frames = found->end_frame - found->start_frame;
    const auto source_seconds =
        slice_state.sample_rate > 0
            ? static_cast<double>(stretch_source_frames) /
                  slice_state.sample_rate
            : 0.0;
    if (state_.selected_pad_settings.source_beats > 0 && source_seconds > 0)
        state_.effective_stretch_ratio =
            static_cast<float>(state_.selected_pad_settings.source_beats *
                               60.0 / current_tempo / source_seconds);
    else if (state_.selected_pad_settings.source_bpm > 0)
        state_.effective_stretch_ratio =
            state_.selected_pad_settings.source_bpm / current_tempo;
    state_.effective_stretch_ratio =
        std::clamp(state_.effective_stretch_ratio, 0.25F, 4.0F);
    state_.stretch_latency_frames = RealtimeTimeStretcher::latency_frames(
        {state_.selected_pad_settings.stretch_mode,
         state_.selected_pad_settings.stretch_quality,
         state_.effective_stretch_ratio, 1.0F, engine_.sample_rate()});
    state_.waveform_peaks = std::move(slice_state.waveform);
    state_.slices.clear();
    state_.slices.reserve(slice_state.slices.size());
    for (const auto &slice : slice_state.slices)
        state_.slices.push_back({slice.id, slice.name, slice.start_frame,
                                 slice.end_frame, slice.gain, slice.root_note});
    if (state_.selected_slice != 0 &&
        std::none_of(state_.slices.begin(), state_.slices.end(),
                     [&](const auto &slice) {
                         return slice.id == state_.selected_slice;
                     }))
        state_.selected_slice = 0;
    state_.tempo_bpm =
        60'000'000.0 / timeline.timeline.transport.microseconds_per_quarter;
    state_.time_signature_numerator =
        timeline.timeline.transport.time_signature_numerator;
    state_.time_signature_denominator =
        timeline.timeline.transport.time_signature_denominator;
    if (state_.selected_track == 0 && !timeline.timeline.tracks.empty())
        state_.selected_track = timeline.timeline.tracks.front().id;
    state_.tracks.clear();
    state_.generation_seed = timeline.generation_seed;
    std::size_t index{};
    for (const auto &track : timeline.timeline.tracks) {
        const auto fallback =
            static_cast<std::uint8_t>(std::min<std::size_t>(127, 36 + index++));
        const auto target = track_targets_.contains(track.id)
                                ? track_targets_.at(track.id)
                                : fallback;
        state_.tracks.push_back({track.id, track.name, track.muted,
                                 track.pattern.length, target,
                                 track.instrument_id, track.pattern.events});
    }
    state_.selected_event.reset();
    state_.selected_step_locks.clear();
    const auto selected_track = std::find_if(
        timeline.timeline.tracks.begin(), timeline.timeline.tracks.end(),
        [&](const Track &value) { return value.id == state_.selected_track; });
    if (selected_track != timeline.timeline.tracks.end()) {
        const auto lock_tick = state_.selected_step * state_.step_ticks;
        for (const auto &event : selected_track->pattern.events)
            if (event.type == MusicalEventType::parameter &&
                event.tick == lock_tick && !event.parameter_destination.empty())
                state_.selected_step_locks.push_back(
                    {event.id, event.parameter_destination, event.value});
        std::sort(state_.selected_step_locks.begin(),
                  state_.selected_step_locks.end(),
                  [](const auto &a, const auto &b) {
                      return a.parameter_id < b.parameter_id;
                  });
    }
    if (selected_event_id_) {
        const auto track = selected_track;
        if (track != timeline.timeline.tracks.end()) {
            const auto selected = std::find_if(
                track->pattern.events.begin(), track->pattern.events.end(),
                [&](const MusicalEvent &event) {
                    return event.id == *selected_event_id_ &&
                           event.type == MusicalEventType::note;
                });
            if (selected != track->pattern.events.end()) {
                const auto base = state_.selected_step * state_.step_ticks;
                std::size_t index_at_step{}, events_at_step{};
                for (const auto &event : track->pattern.events)
                    if (event.type == MusicalEventType::note &&
                        event.tick >= base &&
                        event.tick <
                            std::min<MusicalTick>(base + state_.step_ticks,
                                                  track->pattern.length)) {
                        if (event.id == selected->id)
                            index_at_step = events_at_step;
                        ++events_at_step;
                    }
                state_.selected_event =
                    SelectedEventState{track->id,
                                       selected->id,
                                       state_.selected_step,
                                       selected->tick,
                                       selected->tick - base,
                                       selected->pitch,
                                       selected->velocity,
                                       selected->duration,
                                       index_at_step,
                                       events_at_step,
                                       selected->trigger_probability,
                                       selected->trigger_condition,
                                       selected->condition_n,
                                       selected->condition_m,
                                       selected->ratchet_count};
            } else
                selected_event_id_.reset();
        }
    }
    state_.sample_slots = std::move(slots);
    state_.parameters = std::move(parameters);
    state_.performance.generation = system_monitor_.generation();
    state_.performance.monotonic_milliseconds =
        system_monitor_.monotonic_milliseconds();
    state_.performance.audio = engine_.callback_statistics();
    state_.performance.engine = {engine_.sample_rate(),
                                 engine_.buffer_frames(),
                                 engine_.active_voice_count(),
                                 engine_.voice_capacity(),
                                 engine_.playing(),
                                 engine_.dropped_triggers(),
                                 engine_.dropped_patterns(),
                                 engine_.dropped_timelines(),
                                 engine_.timeline_event_overruns(),
                                 engine_.dropped_legacy_events(),
                                 engine_.dropped_transport_commands(),
                                 engine_.dropped_sample_updates(),
                                 engine_.missing_slot_triggers(),
                                 engine_.stolen_voices(),
                                 rejected_,
                                 engine_.dropped_dsp_commands(),
                                 engine_.synth_state().stolen_voices,
                                 engine_.instrument_nanoseconds(),
                                 engine_.effect_nanoseconds(),
                                 engine_.synth_state().active_voices,
                                 engine_.synth_state().maximum_voices};
    state_.performance.system = system_monitor_.snapshot();
    state_.available_actions.clear();
    for (const auto &value : actions_.actions())
        if (value.available &&
            (value.id != "edit.undo.v1" || history.can_undo) &&
            (value.id != "edit.redo.v1" || history.can_redo))
            state_.available_actions.push_back(value.id);
    if (external)
        ++state_.revision;
}
void ApplicationService::changed_locked() { ++state_.revision; }
ApplicationState ApplicationService::snapshot() const {
    std::lock_guard lock(mutex_);
    synchronize_locked();
    return state_;
}
std::uint64_t ApplicationService::rejected_commands() const noexcept {
    std::lock_guard lock(mutex_);
    return rejected_;
}

const char *step_resolution_name(StepResolution value) noexcept {
    switch (value) {
    case StepResolution::quarter:
        return "1/4";
    case StepResolution::eighth:
        return "1/8";
    case StepResolution::sixteenth:
        return "1/16";
    case StepResolution::thirty_second:
        return "1/32";
    case StepResolution::quarter_triplet:
        return "1/4T";
    case StepResolution::eighth_triplet:
        return "1/8T";
    case StepResolution::sixteenth_triplet:
        return "1/16T";
    case StepResolution::thirty_second_triplet:
        return "1/32T";
    }
    return "1/16";
}
MusicalTick step_resolution_ticks(StepResolution value) noexcept {
    switch (value) {
    case StepResolution::quarter:
        return 960;
    case StepResolution::eighth:
        return 480;
    case StepResolution::sixteenth:
        return 240;
    case StepResolution::thirty_second:
        return 120;
    case StepResolution::quarter_triplet:
        return 640;
    case StepResolution::eighth_triplet:
        return 320;
    case StepResolution::sixteenth_triplet:
        return 160;
    case StepResolution::thirty_second_triplet:
        return 80;
    }
    return 240;
}
std::optional<StepResolution>
parse_step_resolution(std::string_view value) noexcept {
    for (const auto resolution :
         {StepResolution::quarter, StepResolution::eighth,
          StepResolution::sixteenth, StepResolution::thirty_second,
          StepResolution::quarter_triplet, StepResolution::eighth_triplet,
          StepResolution::sixteenth_triplet,
          StepResolution::thirty_second_triplet})
        if (value == step_resolution_name(resolution))
            return resolution;
    return std::nullopt;
}

void TextEntryModel::begin(std::string initial) {
    state_ = {std::move(initial), 0, 'A', true, false, false};
    state_.cursor = state_.text.size();
}
void TextEntryModel::direct(std::string_view text) {
    if (!state_.active)
        return;
    state_.text.insert(state_.cursor, text);
    state_.cursor += text.size();
}
void TextEntryModel::select_relative(int delta) {
    if (!state_.active)
        return;
    constexpr std::string_view characters =
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";
    auto found = characters.find(state_.selected);
    auto index = found == std::string_view::npos ? 0 : static_cast<int>(found);
    index = (index + delta % static_cast<int>(characters.size()) +
             static_cast<int>(characters.size())) %
            static_cast<int>(characters.size());
    state_.selected = characters[static_cast<std::size_t>(index)];
}
void TextEntryModel::insert() {
    if (state_.active) {
        state_.text.insert(state_.cursor, 1, state_.selected);
        ++state_.cursor;
    }
}
void TextEntryModel::erase_or_cancel() {
    if (!state_.active)
        return;
    if (state_.cursor > 0) {
        state_.text.erase(--state_.cursor, 1);
    } else
        discard();
}
void TextEntryModel::move_cursor(int delta) {
    if (!state_.active)
        return;
    const auto position = static_cast<std::int64_t>(state_.cursor) + delta;
    state_.cursor = static_cast<std::size_t>(std::clamp<std::int64_t>(
        position, 0, static_cast<std::int64_t>(state_.text.size())));
}
void TextEntryModel::commit() {
    if (state_.active) {
        state_.active = false;
        state_.committed = true;
    }
}
void TextEntryModel::discard() {
    if (state_.active) {
        state_.active = false;
        state_.discarded = true;
    }
}

NormalizedXY XYControl::apply(double x, double y) {
    if (behavior_.relative) {
        x += value_.x;
        y += value_.y;
    }
    if (behavior_.invert_x)
        x = 1 - x;
    if (behavior_.invert_y)
        y = 1 - y;
    value_.x = std::clamp(0.5 + (x - 0.5) * behavior_.scale_x, 0.0, 1.0);
    value_.y = std::clamp(0.5 + (y - 0.5) * behavior_.scale_y, 0.0, 1.0);
    return value_;
}
void XYControl::release() {
    if (behavior_.spring_to_center && !behavior_.latching)
        value_ = {0.5, 0.5};
}
const char *pad_mode_name(PadMode mode) noexcept {
    switch (mode) {
    case PadMode::step:
        return "step";
    case PadMode::pad:
        return "pad";
    case PadMode::levels:
        return "levels";
    case PadMode::notes:
        return "notes";
    }
    return "unknown";
}
} // namespace neurogenic
