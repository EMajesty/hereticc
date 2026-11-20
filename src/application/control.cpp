#include "neurogenic/application/control.hpp"
#include "neurogenic/dsp/parameter_destination.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>

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
    if (id == "synth.pitch")
        return engine.set_synth_parameter(SynthParameter::pitch, value);
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
               {{"instrument_id", ValueType::text},
                {"instance_id", ValueType::integer, false}}));
    add(action("instrument.instance-parameter-set.v1",
               "Set instrument instance parameter", "Instruments",
               {{"instance_id", ValueType::integer},
                {"parameter_id", ValueType::text},
                {"value", ValueType::real}}));
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
    add(action("project.new.v1", "New project", "Project",
               {{"name", ValueType::text, false}}));
    add(action("project.open.v1", "Open project", "Project",
               {{"path", ValueType::text}}));
    add(action("project.save.v1", "Save project", "Project"));
    add(action("project.save-as.v1", "Save project as", "Project",
               {{"path", ValueType::text}, {"name", ValueType::text, false}}));
    add(action("project.close.v1", "Close project", "Project"));
    add(action(
        "project.relink-sample.v1", "Relink missing sample", "Project",
        {{"missing", ValueType::text}, {"replacement", ValueType::text}}));
    add(action("project.query.v1", "Query project status", "Project"));
    add(action("recording.source-select.v1", "Select recording source",
               "Recording",
               {{"source", ValueType::text},
                {"source_id", ValueType::integer, false}}));
    add(action("recording.destination-select.v1",
               "Select recording destination", "Recording",
               {{"slot_id", ValueType::integer}}));
    add(action("recording.channels-set.v1", "Set recording channels",
               "Recording", {{"channels", ValueType::integer}}));
    add(action("recording.quantization-set.v1", "Set recording quantization",
               "Recording", {{"quantization", ValueType::text}}));
    add(action("recording.fixed-bars-set.v1", "Set fixed recording length",
               "Recording", {{"bars", ValueType::integer}}));
    add(action("recording.monitoring-set.v1", "Set input monitoring",
               "Recording", {{"mode", ValueType::text}}));
    add(action("recording.arm.v1", "Arm recording", "Recording"));
    add(action("recording.start.v1", "Start recording", "Recording"));
    add(action("recording.stop.v1", "Stop recording", "Recording"));
    add(action("recording.cancel.v1", "Cancel recording", "Recording"));
    add(action(
        "recording.save.v1", "Save recording", "Recording",
        {{"path", ValueType::text}, {"format", ValueType::text, false}}));
    add(action("recording.assign-pad.v1", "Assign recording to pad",
               "Recording", {{"pad", ValueType::integer}}));
    add(action("mixer.channel-select.v1", "Select mixer channel", "Mixer",
               {{"index", ValueType::integer}}));
    add(action("mixer.gain-set.v1", "Set mixer gain", "Mixer",
               {{"value", ValueType::real}}));
    add(action("mixer.pan-set.v1", "Set mixer pan", "Mixer",
               {{"value", ValueType::real}}));
    add(action("mixer.mute-toggle.v1", "Toggle mixer mute", "Mixer"));
    add(action("mixer.solo-toggle.v1", "Toggle mixer solo", "Mixer"));
    add(action("mixer.aux-send-set.v1", "Set auxiliary send", "Mixer",
               {{"aux", ValueType::integer},
                {"value", ValueType::real},
                {"pre_fader", ValueType::boolean, false}}));
    add(action("mixer.output-pair-set.v1", "Set channel output pair", "Mixer",
               {{"pair", ValueType::integer}}));
    add(action("mixer.master-gain-set.v1", "Set master gain", "Mixer",
               {{"value", ValueType::real}}));
    add(action("mixer.master-mute-toggle.v1", "Toggle master mute", "Mixer"));
    add(action("mixer.master-output-set.v1", "Set master output", "Mixer",
               {{"left", ValueType::integer}, {"right", ValueType::integer}}));
    add(action("mixer.input-set.v1", "Configure physical input", "Mixer",
               {{"index", ValueType::integer},
                {"physical_channel", ValueType::integer},
                {"gain", ValueType::real},
                {"pan", ValueType::real},
                {"mute", ValueType::boolean, false},
                {"route_to_master", ValueType::boolean, false},
                {"aux_1", ValueType::real, false},
                {"aux_2", ValueType::real, false}}));
    add(action("mixer.aux-set.v1", "Configure auxiliary bus", "Mixer",
               {{"aux", ValueType::integer},
                {"gain", ValueType::real},
                {"return_gain", ValueType::real},
                {"effect_enabled", ValueType::boolean},
                {"mute", ValueType::boolean, false},
                {"output_pair", ValueType::integer, false}}));
    add(action("mixer.fx-loop-select.v1", "Select external FX loop", "Routing",
               {{"index", ValueType::integer}}));
    add(action("mixer.fx-loop-set.v1", "Configure external FX loop", "Routing",
               {{"index", ValueType::integer},
                {"source", ValueType::text},
                {"source_id", ValueType::integer},
                {"send_left", ValueType::integer},
                {"send_right", ValueType::integer},
                {"return_left", ValueType::integer},
                {"return_right", ValueType::integer},
                {"name", ValueType::text, false},
                {"enabled", ValueType::boolean, false},
                {"bypass", ValueType::boolean, false},
                {"mono_send", ValueType::boolean, false},
                {"mono_return", ValueType::boolean, false}}));
    add(action("mixer.fx-loop-levels.v1", "Set external FX loop levels",
               "Routing",
               {{"index", ValueType::integer},
                {"send_gain", ValueType::real},
                {"return_gain", ValueType::real},
                {"wet", ValueType::real},
                {"return_mute", ValueType::boolean, false}}));
    add(action(
        "mixer.fx-loop-latency.v1", "Set external FX loop latency", "Routing",
        {{"index", ValueType::integer}, {"frames", ValueType::integer}}));
    add(action("mixer.scene-save.v1", "Save routing scene", "Routing",
               {{"name", ValueType::text}}));
    add(action("mixer.scene-recall.v1", "Recall routing scene", "Routing",
               {{"id", ValueType::integer}}));
    add(action("mixer.scene-rename.v1", "Rename routing scene", "Routing",
               {{"id", ValueType::integer}, {"name", ValueType::text}}));
    add(action("mixer.scene-delete.v1", "Delete routing scene", "Routing",
               {{"id", ValueType::integer}}));
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
    add(action("modulation.midi-cc.v1", "MIDI CC modulation", "Modulation",
               {{"value", ValueType::real}}));
    add(action("modulation.route-create.v1", "Create modulation route",
               "Modulation",
               {{"source", ValueType::integer},
                {"destination", ValueType::text},
                {"depth", ValueType::real},
                {"scope", ValueType::integer},
                {"track_id", ValueType::integer, false},
                {"instance_id", ValueType::integer, false}}));
    add(action("modulation.route-delete.v1", "Delete modulation route",
               "Modulation", {{"route_id", ValueType::integer}}));
    add(action("modulation.route-select.v1", "Select modulation route",
               "Modulation", {{"route_id", ValueType::integer}}));
    add(action("modulation.route-depth.v1", "Set modulation depth",
               "Modulation",
               {{"route_id", ValueType::integer}, {"depth", ValueType::real}}));
    add(action("modulation.route-enable.v1", "Enable modulation route",
               "Modulation",
               {{"route_id", ValueType::integer}, {"enabled", ValueType::boolean}}));
    add(action("modulation.source-configure.v1", "Configure modulation source",
               "Modulation",
               {{"source", ValueType::integer},
                {"waveform", ValueType::integer},
                {"rate", ValueType::real},
                {"tempo_sync", ValueType::boolean},
                {"beats", ValueType::real},
                {"scope", ValueType::integer},
                {"retrigger", ValueType::boolean, false},
                {"attack", ValueType::real, false},
                {"decay", ValueType::real, false},
                {"sustain", ValueType::real, false},
                {"release", ValueType::real, false},
                {"seed", ValueType::integer, false}}));
    add(action("modulation.source-reset.v1", "Reset modulation source",
               "Modulation", {{"source", ValueType::integer}}));
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
      slices_(engine, samples), recording_(engine.sample_rate()),
      capacity_(capacity) {
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
                                            true,
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
    synth_parameter("synth.pitch", "Synth pitch", "st", -24, 24, 0, 0.1);
    synth_parameter("effect.delay.time", "Delay time", "s", 0.001, 2, 0.25,
                    0.01);
    synth_parameter("effect.delay.feedback", "Delay feedback", "", 0, 0.95, 0.3,
                    0.01);
    synth_parameter("effect.delay.mix", "Delay mix", "", 0, 1, 0, 0.01);
    const auto mixer_parameter = [&](std::string id, std::string name,
                                     double minimum, double maximum,
                                     double initial) {
        parameters_.register_parameter({std::move(id),
                                        std::move(name),
                                        "",
                                        ParameterType::real,
                                        minimum,
                                        maximum,
                                        initial,
                                        initial,
                                        0.01,
                                        true,
                                        false,
                                        false,
                                        {}},
                                       error);
    };
    mixer_parameter("mixer.master.gain", "Master gain", 0, 2, 1);
    mixer_parameter("mixer.selected.gain", "Channel gain", 0, 2, 1);
    mixer_parameter("mixer.selected.pan", "Channel pan", -1, 1, 0);
    mixer_parameter("mixer.external.selected.wet", "External FX wet/dry", 0, 1,
                    0.5);
    state_.step_ticks = step_resolution_ticks(state_.step_resolution);
    state_.step_duration = state_.step_ticks;
    project_.timeline = timeline_.snapshot().timeline;
    state_.timeline_revision = timeline_.snapshot().revision;
    recording_configuration_.destination = SampleSlotId{0};
    mixer_configuration_.revision = 1;
    mixer_configuration_.track_count =
        std::min(project_.timeline.tracks.size(), maximum_mixer_tracks);
    for (std::size_t i = 0; i < mixer_configuration_.track_count; ++i)
        mixer_configuration_.tracks[i].track = project_.timeline.tracks[i].id;
    for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
        auto &loop = mixer_configuration_.external_fx[i];
        loop.id = static_cast<ExternalFxLoopId>(i + 1);
        const auto name = "External FX " + std::to_string(i + 1);
        std::copy_n(name.begin(), std::min(name.size(), loop.name.size() - 1),
                    loop.name.begin());
        loop.send_left = static_cast<std::uint8_t>(2 + i * 2);
        loop.send_right = static_cast<std::uint8_t>(3 + i * 2);
        loop.return_left = static_cast<std::uint8_t>(i * 2);
        loop.return_right = static_cast<std::uint8_t>(1 + i * 2);
    }
    if (!engine_.publish_mixer(mixer_configuration_, error))
        throw std::runtime_error("cannot publish initial mixer: " + error);
    project_.modulation = modulation_configuration_;
    if (!engine_.publish_modulation(modulation_configuration_))
        throw std::runtime_error("cannot publish initial modulation state");
    engine_.set_recording_service(&recording_);
}
ApplicationService::~ApplicationService() {
    engine_.set_recording_service(nullptr);
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
    const auto publish_parameter = [&](const std::string &id) {
        if (id.rfind("mixer.", 0) != 0) {
            if (!publish_dsp_parameter(engine_, parameters_, id))
                return false;
            const auto destination = resolve_parameter_destination(id);
            const auto descriptor = parameters_.get(id);
            if (!destination || !descriptor ||
                !std::holds_alternative<double>(descriptor->current_value))
                return true;
            auto next = modulation_configuration_;
            bool changed{};
            for (std::size_t route = 0; route < next.route_count; ++route)
                if (next.routes[route].destination.target ==
                        destination->target &&
                    next.routes[route].destination.parameter ==
                        destination->parameter) {
                    next.routes[route].base_value = static_cast<float>(
                        std::get<double>(descriptor->current_value));
                    changed = true;
                }
            if (changed) {
                if (!engine_.publish_modulation(next))
                    return false;
                modulation_configuration_ = next;
                project_.modulation = next;
            }
            return true;
        }
        const auto descriptor = parameters_.get(id);
        if (!descriptor ||
            !std::holds_alternative<double>(descriptor->current_value))
            return false;
        const auto value =
            static_cast<float>(std::get<double>(descriptor->current_value));
        auto next = mixer_configuration_;
        if (id == "mixer.master.gain")
            next.master.gain = value;
        else if (state_.selected_mixer_channel < next.track_count &&
                 id == "mixer.selected.gain")
            next.tracks[state_.selected_mixer_channel].gain = value;
        else if (state_.selected_mixer_channel < next.track_count &&
                 id == "mixer.selected.pan")
            next.tracks[state_.selected_mixer_channel].pan = value;
        else if (id == "mixer.external.selected.wet" &&
                 state_.selected_external_fx_loop < mixer_external_fx_count)
            next.external_fx[state_.selected_external_fx_loop].wet = value;
        else
            return false;
        ++next.revision;
        std::string mixer_error;
        if (!engine_.publish_mixer(next, mixer_error))
            return false;
        mixer_configuration_ = next;
        project_.active_routing_scene = 0;
        state_.active_routing_scene = 0;
        state_.project_dirty = true;
        return true;
    };
    if (command.action_id.rfind("recording.", 0) == 0) {
        const auto previous_recording_configuration = recording_configuration_;
        const auto text = [&](std::string_view name) {
            return *argument<std::string>(command, name);
        };
        if (command.action_id == "recording.source-select.v1") {
            const auto value = text("source");
            if (value == "external-mono")
                recording_configuration_.source =
                    RecordingSource::external_mono;
            else if (value == "external-stereo")
                recording_configuration_.source =
                    RecordingSource::external_stereo;
            else if (value == "master-pre")
                recording_configuration_.source =
                    RecordingSource::master_pre_effects;
            else if (value == "master-post")
                recording_configuration_.source =
                    RecordingSource::master_post_effects;
            else if (value == "track")
                recording_configuration_.source =
                    RecordingSource::track_channel;
            else if (value == "aux")
                recording_configuration_.source =
                    RecordingSource::auxiliary_bus;
            else if (value == "physical-input")
                recording_configuration_.source =
                    RecordingSource::physical_input;
            else if (value == "external-fx-return")
                recording_configuration_.source =
                    RecordingSource::external_fx_return;
            else if (value == "external-fx-mix")
                recording_configuration_.source =
                    RecordingSource::external_fx_mix;
            else
                error = "unknown recording source";
            if (const auto *source_id =
                    argument<std::int64_t>(command, "source_id")) {
                if (*source_id < 0)
                    error = "recording source ID must be non-negative";
                else
                    recording_configuration_.source_id =
                        static_cast<std::uint64_t>(*source_id);
            }
            if (recording_configuration_.source ==
                    RecordingSource::auxiliary_bus &&
                recording_configuration_.source_id >= mixer_aux_count)
                error = "auxiliary recording source is unavailable";
            if (recording_configuration_.source ==
                    RecordingSource::physical_input &&
                recording_configuration_.source_id >= maximum_mixer_inputs)
                error = "physical recording source is unavailable";
            if ((recording_configuration_.source ==
                     RecordingSource::external_fx_return ||
                 recording_configuration_.source ==
                     RecordingSource::external_fx_mix) &&
                recording_configuration_.source_id >= mixer_external_fx_count)
                error = "external FX recording source is unavailable";
            if (recording_configuration_.source ==
                    RecordingSource::track_channel &&
                std::none_of(mixer_configuration_.tracks.begin(),
                             mixer_configuration_.tracks.begin() +
                                 mixer_configuration_.track_count,
                             [&](const auto &channel) {
                                 return channel.track ==
                                        recording_configuration_.source_id;
                             }))
                error = "track recording source is unavailable";
        } else if (command.action_id == "recording.destination-select.v1") {
            const auto value = *argument<std::int64_t>(command, "slot_id");
            if (value < 0 || value >= invalid_sample_slot.value)
                error = "invalid recording destination";
            else
                recording_configuration_.destination =
                    SampleSlotId{static_cast<std::uint32_t>(value)};
        } else if (command.action_id == "recording.channels-set.v1") {
            const auto value = *argument<std::int64_t>(command, "channels");
            if (value != 1 && value != 2)
                error = "recording channels must be 1 or 2";
            else
                recording_configuration_.channels =
                    static_cast<std::uint16_t>(value);
        } else if (command.action_id == "recording.quantization-set.v1") {
            const auto value = text("quantization");
            if (value == "immediate")
                recording_configuration_.quantization =
                    RecordingQuantization::immediate;
            else if (value == "beat")
                recording_configuration_.quantization =
                    RecordingQuantization::beat;
            else if (value == "bar")
                recording_configuration_.quantization =
                    RecordingQuantization::bar;
            else
                error = "unknown recording quantization";
        } else if (command.action_id == "recording.fixed-bars-set.v1") {
            const auto value = *argument<std::int64_t>(command, "bars");
            if (value < 0 || value > 1024)
                error = "fixed recording length must be 0..1024 bars";
            else
                recording_fixed_bars_ = static_cast<std::uint64_t>(value);
        } else if (command.action_id == "recording.monitoring-set.v1") {
            const auto value = text("mode");
            if (value == "off")
                recording_configuration_.monitoring = RecordingMonitorMode::off;
            else if (value == "on")
                recording_configuration_.monitoring = RecordingMonitorMode::on;
            else if (value == "auto")
                recording_configuration_.monitoring =
                    RecordingMonitorMode::automatic;
            else
                error = "unknown recording monitoring mode";
        } else if (command.action_id == "recording.arm.v1") {
            const auto timeline = timeline_.snapshot().timeline;
            const auto bar_ticks = static_cast<MusicalTick>(
                ticks_per_quarter * 4ULL *
                timeline.transport.time_signature_numerator /
                timeline.transport.time_signature_denominator);
            auto start_tick = before.transport_tick;
            if (recording_configuration_.quantization !=
                RecordingQuantization::immediate) {
                if (!before.playing)
                    error = "quantized recording requires a running transport";
                const auto quantum =
                    recording_configuration_.quantization ==
                            RecordingQuantization::beat
                        ? static_cast<MusicalTick>(ticks_per_quarter)
                        : bar_ticks;
                if (error.empty())
                    start_tick = ((start_tick / quantum) + 1) * quantum;
            }
            const auto frames_for_ticks = [&](MusicalTick ticks) {
                return static_cast<std::uint64_t>(std::ceil(
                    static_cast<long double>(std::max<MusicalTick>(0, ticks)) *
                    engine_.sample_rate() *
                    timeline.transport.microseconds_per_quarter /
                    (static_cast<long double>(ticks_per_quarter) *
                     1'000'000.0L)));
            };
            if (error.empty()) {
                recording_configuration_.start_frame =
                    engine_.callback_frame_position() +
                    frames_for_ticks(start_tick - before.transport_tick);
                recording_configuration_.fixed_frames =
                    recording_fixed_bars_
                        ? frames_for_ticks(bar_ticks * recording_fixed_bars_)
                        : 0;
                recording_.arm(recording_configuration_, error);
            }
        } else if (command.action_id == "recording.start.v1") {
            recording_.start(engine_.callback_frame_position(), error);
        } else if (command.action_id == "recording.stop.v1") {
            if (recording_configuration_.quantization ==
                RecordingQuantization::immediate)
                recording_.stop(error);
            else if (!before.playing)
                error = "quantized recording stop requires running transport";
            else {
                const auto timeline = timeline_.snapshot().timeline;
                const auto bar_ticks = static_cast<MusicalTick>(
                    ticks_per_quarter * 4ULL *
                    timeline.transport.time_signature_numerator /
                    timeline.transport.time_signature_denominator);
                const auto quantum =
                    recording_configuration_.quantization ==
                            RecordingQuantization::beat
                        ? static_cast<MusicalTick>(ticks_per_quarter)
                        : bar_ticks;
                const auto stop_tick =
                    ((before.transport_tick / quantum) + 1) * quantum;
                const auto delta_frames = static_cast<std::uint64_t>(
                    std::ceil(static_cast<long double>(stop_tick -
                                                       before.transport_tick) *
                              engine_.sample_rate() *
                              timeline.transport.microseconds_per_quarter /
                              (static_cast<long double>(ticks_per_quarter) *
                               1'000'000.0L)));
                recording_.stop_at(
                    engine_.callback_frame_position() + delta_frames, error);
            }
        } else if (command.action_id == "recording.cancel.v1") {
            recording_.cancel();
        } else if (command.action_id == "recording.save.v1") {
            const auto format_found = command.arguments.find("format");
            auto format = RecordingWavFormat::pcm16;
            if (format_found != command.arguments.end()) {
                const auto &value = std::get<std::string>(format_found->second);
                if (value == "float32")
                    format = RecordingWavFormat::float32;
                else if (value != "pcm16")
                    error = "recording WAV format must be pcm16 or float32";
            }
            const auto path = std::filesystem::path(text("path"));
            if (error.empty() && recording_.save_wav(path, format, error) &&
                !samples_.set_source_path(recording_configuration_.destination,
                                          path))
                error = "recorded sample is not published";
        } else if (command.action_id == "recording.assign-pad.v1") {
            const auto pad_value = *argument<std::int64_t>(command, "pad");
            if (pad_value < 0 || pad_value >= 16)
                error = "pad must be 0..15";
            else {
                const auto status =
                    samples_.status(recording_configuration_.destination);
                auto slice_state =
                    slices_.snapshot(recording_configuration_.destination);
                if (!status.has_sample || status.frames == 0)
                    error = "recorded sample is not audio-active";
                else {
                    SliceId id{};
                    const auto found = std::find_if(
                        slice_state.slices.begin(), slice_state.slices.end(),
                        [&](const auto &slice) {
                            return slice.start_frame == 0 &&
                                   slice.end_frame == status.frames;
                        });
                    if (found != slice_state.slices.end())
                        id = found->id;
                    else if (!slices_.create(
                                 recording_configuration_.destination,
                                 slice_state.revision, 0, status.frames,
                                 "Recording", id, error))
                        id = 0;
                    if (id) {
                        slice_state = slices_.snapshot(
                            recording_configuration_.destination);
                        slices_.assign_pad(static_cast<std::size_t>(pad_value),
                                           id, slice_state.revision, error);
                    }
                }
            }
        }
        {
            std::lock_guard lock(mutex_);
            if (!error.empty())
                recording_configuration_ = previous_recording_configuration;
            if (error.empty() &&
                (command.action_id == "recording.save.v1" ||
                 command.action_id == "recording.assign-pad.v1"))
                state_.project_dirty = true;
            changed_locked();
        }
        return {error.empty(), sequence, snapshot().revision, std::move(error)};
    }
    if (command.action_id.rfind("mixer.", 0) == 0) {
        if (command.action_id.rfind("mixer.scene-", 0) == 0) {
            if (command.action_id == "mixer.scene-save.v1") {
                const auto &name = *argument<std::string>(command, "name");
                if (name.empty() || name.size() > 128)
                    error = "routing scene name must contain 1..128 characters";
                else if (project_.routing_scenes.size() >= 32)
                    error = "routing scene capacity is full";
                else {
                    std::uint64_t id = 1;
                    for (const auto &scene : project_.routing_scenes)
                        id = std::max(id, scene.id + 1);
                    project_.routing_scenes.push_back(
                        {id, name, mixer_configuration_});
                    project_.active_routing_scene = id;
                }
            } else {
                const auto requested = *argument<std::int64_t>(command, "id");
                const auto found = std::find_if(
                    project_.routing_scenes.begin(),
                    project_.routing_scenes.end(), [&](const auto &scene) {
                        return requested > 0 &&
                               scene.id ==
                                   static_cast<std::uint64_t>(requested);
                    });
                if (found == project_.routing_scenes.end())
                    error = "routing scene was not found";
                else if (command.action_id == "mixer.scene-recall.v1") {
                    auto next = found->mixer;
                    next.revision = mixer_configuration_.revision + 1;
                    if (engine_.publish_mixer(next, error)) {
                        mixer_configuration_ = next;
                        project_.active_routing_scene = found->id;
                    }
                } else if (command.action_id == "mixer.scene-rename.v1") {
                    const auto &name = *argument<std::string>(command, "name");
                    if (name.empty() || name.size() > 128)
                        error =
                            "routing scene name must contain 1..128 characters";
                    else
                        found->name = name;
                } else if (command.action_id == "mixer.scene-delete.v1") {
                    const auto id = found->id;
                    project_.routing_scenes.erase(found);
                    if (project_.active_routing_scene == id)
                        project_.active_routing_scene = 0;
                }
            }
            {
                std::lock_guard lock(mutex_);
                if (error.empty())
                    state_.project_dirty = true;
                state_.routing_scenes = project_.routing_scenes;
                state_.active_routing_scene = project_.active_routing_scene;
                changed_locked();
            }
            return {error.empty(), sequence, snapshot().revision,
                    std::move(error)};
        }
        const auto previous_configuration = mixer_configuration_;
        auto changed = false;
        if (command.action_id == "mixer.fx-loop-select.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            if (index < 0 ||
                index >= static_cast<std::int64_t>(mixer_external_fx_count))
                error = "external FX loop index is unavailable";
            else
                state_.selected_external_fx_loop =
                    static_cast<std::size_t>(index);
            if (error.empty()) {
                std::string ignored;
                parameters_.set(
                    "mixer.external.selected.wet",
                    static_cast<double>(
                        mixer_configuration_
                            .external_fx[state_.selected_external_fx_loop]
                            .wet),
                    ignored);
            }
        } else if (command.action_id == "mixer.fx-loop-set.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            const auto source_id =
                *argument<std::int64_t>(command, "source_id");
            const auto send_left =
                *argument<std::int64_t>(command, "send_left");
            const auto send_right =
                *argument<std::int64_t>(command, "send_right");
            const auto return_left =
                *argument<std::int64_t>(command, "return_left");
            const auto return_right =
                *argument<std::int64_t>(command, "return_right");
            if (index < 0 || index >= 2 || source_id < 0 || send_left < 0 ||
                send_left >= 6 || send_right < 0 || send_right >= 6 ||
                return_left < 0 || return_left >= 6 || return_right < 0 ||
                return_right >= 6)
                error = "invalid external FX mapping";
            else {
                auto &loop = mixer_configuration_
                                 .external_fx[static_cast<std::size_t>(index)];
                const auto source = *argument<std::string>(command, "source");
                if (source == "track")
                    loop.source = ExternalFxSource::track;
                else if (source == "aux")
                    loop.source = ExternalFxSource::auxiliary;
                else if (source == "master")
                    loop.source = ExternalFxSource::master;
                else
                    error = "external FX source must be track, aux, or master";
                loop.source_id = static_cast<std::uint64_t>(source_id);
                loop.send_left = static_cast<std::uint8_t>(send_left);
                loop.send_right = static_cast<std::uint8_t>(send_right);
                loop.return_left = static_cast<std::uint8_t>(return_left);
                loop.return_right = static_cast<std::uint8_t>(return_right);
                if (const auto *name = argument<std::string>(command, "name")) {
                    if (name->empty() || name->size() >= loop.name.size())
                        error =
                            "external FX name must contain 1..31 characters";
                    else {
                        loop.name.fill('\0');
                        std::copy(name->begin(), name->end(),
                                  loop.name.begin());
                    }
                }
                if (const auto *value = argument<bool>(command, "enabled"))
                    loop.enabled = *value;
                if (const auto *value = argument<bool>(command, "bypass"))
                    loop.bypass = *value;
                if (const auto *value = argument<bool>(command, "mono_send"))
                    loop.mono_send = *value;
                if (const auto *value = argument<bool>(command, "mono_return"))
                    loop.mono_return = *value;
                changed = error.empty();
            }
        } else if (command.action_id == "mixer.fx-loop-levels.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            const auto send = number(command.arguments.at("send_gain"));
            const auto receive = number(command.arguments.at("return_gain"));
            const auto wet = number(command.arguments.at("wet"));
            if (index < 0 || index >= 2 || !std::isfinite(send) || send < 0 ||
                send > 2 || !std::isfinite(receive) || receive < 0 ||
                receive > 2 || !std::isfinite(wet) || wet < 0 || wet > 1)
                error = "invalid external FX levels";
            else {
                auto &loop = mixer_configuration_
                                 .external_fx[static_cast<std::size_t>(index)];
                loop.send_gain = static_cast<float>(send);
                loop.return_gain = static_cast<float>(receive);
                loop.wet = static_cast<float>(wet);
                if (const auto *mute = argument<bool>(command, "return_mute"))
                    loop.return_mute = *mute;
                changed = true;
            }
        } else if (command.action_id == "mixer.fx-loop-latency.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            const auto frames = *argument<std::int64_t>(command, "frames");
            if (index < 0 || index >= 2 || frames < 0 ||
                frames > static_cast<std::int64_t>(
                             maximum_external_fx_latency_frames))
                error = "external FX latency is out of range";
            else {
                mixer_configuration_
                    .external_fx[static_cast<std::size_t>(index)]
                    .latency_frames = static_cast<std::uint32_t>(frames);
                changed = true;
            }
        } else if (command.action_id == "mixer.channel-select.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            if (index < 0 || static_cast<std::size_t>(index) >=
                                 mixer_configuration_.track_count)
                error = "mixer channel index is unavailable";
            else
                state_.selected_mixer_channel = static_cast<std::size_t>(index);
            if (error.empty()) {
                const auto &selected =
                    mixer_configuration_.tracks[state_.selected_mixer_channel];
                std::string ignored;
                parameters_.set("mixer.selected.gain",
                                static_cast<double>(selected.gain), ignored);
                parameters_.set("mixer.selected.pan",
                                static_cast<double>(selected.pan), ignored);
            }
        } else if (command.action_id == "mixer.master-gain-set.v1") {
            const auto value = number(command.arguments.at("value"));
            if (!std::isfinite(value) || value < 0 || value > 2)
                error = "master gain must be 0..2";
            else {
                mixer_configuration_.master.gain = static_cast<float>(value);
                changed = true;
            }
        } else if (command.action_id == "mixer.master-mute-toggle.v1") {
            mixer_configuration_.master.mute =
                !mixer_configuration_.master.mute;
            changed = true;
        } else if (command.action_id == "mixer.master-output-set.v1") {
            const auto left = *argument<std::int64_t>(command, "left");
            const auto right = *argument<std::int64_t>(command, "right");
            if (left < 0 || right < 0 || left >= 6 || right >= 6 ||
                left == right)
                error = "master outputs must be distinct channels 0..5";
            else {
                mixer_configuration_.master.output_left =
                    static_cast<std::uint8_t>(left);
                mixer_configuration_.master.output_right =
                    static_cast<std::uint8_t>(right);
                changed = true;
            }
        } else if (command.action_id == "mixer.input-set.v1") {
            const auto index = *argument<std::int64_t>(command, "index");
            const auto physical =
                *argument<std::int64_t>(command, "physical_channel");
            const auto gain = number(command.arguments.at("gain"));
            const auto pan = number(command.arguments.at("pan"));
            if (index < 0 || index >= 6 || physical < 0 || physical >= 6 ||
                !std::isfinite(gain) || gain < 0 || gain > 2 ||
                !std::isfinite(pan) || pan < -1 || pan > 1)
                error = "invalid physical input configuration";
            else {
                auto &input = mixer_configuration_
                                  .inputs[static_cast<std::size_t>(index)];
                input.physical_channel = static_cast<std::uint8_t>(physical);
                input.gain = static_cast<float>(gain);
                input.pan = static_cast<float>(pan);
                if (const auto *mute = argument<bool>(command, "mute"))
                    input.mute = *mute;
                if (const auto *route =
                        argument<bool>(command, "route_to_master"))
                    input.route_to_master = *route;
                for (std::size_t aux = 0; aux < mixer_aux_count; ++aux) {
                    const auto id =
                        std::string("aux_") + std::to_string(aux + 1);
                    if (const auto found = command.arguments.find(id);
                        found != command.arguments.end()) {
                        const auto send = number(found->second);
                        if (!std::isfinite(send) || send < 0 || send > 1) {
                            error = "input aux sends must be 0..1";
                            break;
                        }
                        input.aux_send[aux] = static_cast<float>(send);
                    }
                }
                mixer_configuration_.input_count =
                    std::max(mixer_configuration_.input_count,
                             static_cast<std::size_t>(index + 1));
                changed = true;
            }
        } else if (command.action_id == "mixer.aux-set.v1") {
            const auto aux = *argument<std::int64_t>(command, "aux");
            const auto gain = number(command.arguments.at("gain"));
            const auto return_gain =
                number(command.arguments.at("return_gain"));
            if (aux < 0 || aux >= 2 || !std::isfinite(gain) || gain < 0 ||
                gain > 2 || !std::isfinite(return_gain) || return_gain < 0 ||
                return_gain > 2)
                error = "invalid auxiliary bus configuration";
            else {
                auto &bus =
                    mixer_configuration_.aux[static_cast<std::size_t>(aux)];
                bus.gain = static_cast<float>(gain);
                bus.return_gain = static_cast<float>(return_gain);
                bus.effect_enabled = *argument<bool>(command, "effect_enabled");
                if (const auto *mute = argument<bool>(command, "mute"))
                    bus.mute = *mute;
                if (const auto *output =
                        argument<std::int64_t>(command, "output_pair")) {
                    if (*output != -1 && *output != 0 && *output != 2 &&
                        *output != 4)
                        error =
                            "aux output pair must be master, 1/2, 3/4, or 5/6";
                    else
                        bus.output_pair = static_cast<std::int8_t>(*output);
                }
                changed = true;
            }
        } else if (state_.selected_mixer_channel >=
                   mixer_configuration_.track_count) {
            error = "no mixer channel is selected";
        } else {
            auto &channel =
                mixer_configuration_.tracks[state_.selected_mixer_channel];
            if (command.action_id == "mixer.gain-set.v1") {
                const auto value = number(command.arguments.at("value"));
                if (!std::isfinite(value) || value < 0 || value > 2)
                    error = "channel gain must be 0..2";
                else {
                    channel.gain = static_cast<float>(value);
                    changed = true;
                }
            } else if (command.action_id == "mixer.pan-set.v1") {
                const auto value = number(command.arguments.at("value"));
                if (!std::isfinite(value) || value < -1 || value > 1)
                    error = "channel pan must be -1..1";
                else {
                    channel.pan = static_cast<float>(value);
                    changed = true;
                }
            } else if (command.action_id == "mixer.mute-toggle.v1") {
                channel.mute = !channel.mute;
                changed = true;
            } else if (command.action_id == "mixer.solo-toggle.v1") {
                channel.solo = !channel.solo;
                changed = true;
            } else if (command.action_id == "mixer.aux-send-set.v1") {
                const auto aux = *argument<std::int64_t>(command, "aux");
                const auto value = number(command.arguments.at("value"));
                if (aux < 0 ||
                    aux >= static_cast<std::int64_t>(mixer_aux_count) ||
                    !std::isfinite(value) || value < 0 || value > 1)
                    error = "aux send requires bus 0..1 and value 0..1";
                else {
                    channel.aux_send[static_cast<std::size_t>(aux)] =
                        static_cast<float>(value);
                    if (const auto *pre = argument<bool>(command, "pre_fader"))
                        channel.aux_pre_fader[static_cast<std::size_t>(aux)] =
                            *pre;
                    changed = true;
                }
            } else if (command.action_id == "mixer.output-pair-set.v1") {
                const auto pair = *argument<std::int64_t>(command, "pair");
                if (pair != -1 && pair != 0 && pair != 2 && pair != 4)
                    error = "output pair must be master, 1/2, 3/4, or 5/6";
                else {
                    channel.output_pair = static_cast<std::int8_t>(pair);
                    changed = true;
                }
            }
        }
        if (!error.empty())
            mixer_configuration_ = previous_configuration;
        if (changed && error.empty()) {
            ++mixer_configuration_.revision;
            if (!engine_.publish_mixer(mixer_configuration_, error))
                mixer_configuration_ = previous_configuration;
            else {
                project_.active_routing_scene = 0;
                state_.active_routing_scene = 0;
                state_.project_dirty = true;
                std::string ignored;
                if (command.action_id == "mixer.master-gain-set.v1")
                    parameters_.set(
                        "mixer.master.gain",
                        static_cast<double>(mixer_configuration_.master.gain),
                        ignored);
                else if (state_.selected_mixer_channel <
                         mixer_configuration_.track_count) {
                    const auto &channel =
                        mixer_configuration_
                            .tracks[state_.selected_mixer_channel];
                    if (command.action_id == "mixer.gain-set.v1")
                        parameters_.set("mixer.selected.gain",
                                        static_cast<double>(channel.gain),
                                        ignored);
                    if (command.action_id == "mixer.pan-set.v1")
                        parameters_.set("mixer.selected.pan",
                                        static_cast<double>(channel.pan),
                                        ignored);
                }
            }
        }
        changed_locked();
        return {error.empty(), sequence, snapshot().revision, std::move(error)};
    }
    if (command.action_id.rfind("project.", 0) == 0) {
        try {
            if (command.action_id == "project.query.v1")
                return {true, sequence, before.revision, {}};
            if (command.action_id == "project.save.v1" ||
                command.action_id == "project.save-as.v1") {
                std::filesystem::path path = before.project_path;
                if (command.action_id == "project.save-as.v1")
                    path = *argument<std::string>(command, "path");
                if (path.empty())
                    return {false, sequence, before.revision,
                            "project path is not set; use Save As"};
                Project saving;
                saving.name = before.project_name;
                if (const auto found = command.arguments.find("name");
                    found != command.arguments.end())
                    saving.name = std::get<std::string>(found->second);
                saving.timeline = timeline_.snapshot().timeline;
                saving.mixer = mixer_configuration_;
                saving.routing_scenes = project_.routing_scenes;
                saving.active_routing_scene = project_.active_routing_scene;
                saving.modulation = modulation_configuration_;
                for (const auto &slot : samples_.states())
                    if (slot.has_sample && slot.source_path.empty()) {
                        const auto recording = recording_.status();
                        if (recording.state != RecordingState::audio_active ||
                            recording.configuration.destination != slot.id)
                            throw std::runtime_error(
                                "project contains an unsaved in-memory sample");
                        const auto asset =
                            path / "recordings" /
                            ("slot-" + std::to_string(slot.id.value) + ".wav");
                        std::filesystem::create_directories(
                            asset.parent_path());
                        if (!recording_.save_wav(
                                asset, RecordingWavFormat::float32, error) ||
                            !samples_.set_source_path(slot.id, asset))
                            throw std::runtime_error(
                                error.empty() ? "cannot retain recorded asset"
                                              : error);
                    }
                std::map<std::uint32_t, std::filesystem::path> paths;
                for (const auto &slot : samples_.states())
                    if (slot.has_sample)
                        paths[slot.id.value] = slot.source_path;
                for (const auto &[slot, sample] : project_.samples.sample_paths)
                    if (!paths.contains(slot))
                        paths[slot] = sample;
                saving.samples = slices_.project_snapshot(std::move(paths));
                // SliceService intentionally exposes only audio-active samples.
                // Retain metadata that belongs to unresolved assets so a save
                // never erases the user's intended edits or pad assignments.
                std::set<SliceId> retained_missing_slices;
                for (const auto &slice : project_.samples.slices) {
                    const auto found =
                        project_.samples.sample_paths.find(slice.slot.value);
                    if (found != project_.samples.sample_paths.end() &&
                        std::find(project_.missing_assets.begin(),
                                  project_.missing_assets.end(),
                                  found->second) !=
                            project_.missing_assets.end()) {
                        saving.samples.slices.push_back(slice);
                        retained_missing_slices.insert(slice.id);
                    }
                }
                for (std::size_t pad = 0; pad < project_.samples.pads.size();
                     ++pad)
                    if (project_.samples.pads[pad] &&
                        retained_missing_slices.contains(
                            *project_.samples.pads[pad]))
                        saving.samples.pads[pad] = project_.samples.pads[pad];
                saving.missing_assets = project_.missing_assets;
                for (const auto &parameter : parameters_.parameters())
                    if (is_numeric(parameter.current_value) &&
                        (parameter.id.rfind("synth.", 0) == 0 ||
                         parameter.id.rfind("effect.", 0) == 0))
                        saving.parameters[parameter.id] =
                            number(parameter.current_value);
                for (const auto &[instance, values] : synth_instance_parameters_)
                    for (const auto &[id, value] : values)
                        saving.parameters["instance." +
                                          std::to_string(instance) + "." + id] =
                            value;
                {
                    std::lock_guard lock(mutex_);
                    state_.project_saving = true;
                    changed_locked();
                }
                save_project(saving, path);
                {
                    std::lock_guard lock(mutex_);
                    project_ = std::move(saving);
                    state_.project_name = project_.name;
                    state_.project_path = path;
                    state_.project_dirty = false;
                    state_.timeline_revision = timeline_.snapshot().revision;
                    state_.project_saving = false;
                    state_.project_error.clear();
                    state_.missing_assets = project_.missing_assets;
                    changed_locked();
                }
                return {true, sequence, snapshot().revision, {}};
            }
            if (command.action_id == "project.open.v1") {
                const std::filesystem::path path =
                    *argument<std::string>(command, "path");
                {
                    std::lock_guard lock(mutex_);
                    state_.project_loading = true;
                    changed_locked();
                }
                auto loaded = load_project(path);
                for (std::size_t route = 0;
                     route < loaded.modulation.route_count; ++route) {
                    const auto &candidate = loaded.modulation.routes[route];
                    bool supported{};
                    for (const auto &parameter : parameters_.parameters()) {
                        const auto destination =
                            resolve_parameter_destination(parameter.id);
                        supported |= destination && parameter.modulatable &&
                                     destination->target ==
                                         candidate.destination.target &&
                                     destination->parameter ==
                                         candidate.destination.parameter;
                    }
                    if (!supported)
                        throw std::runtime_error(
                            "project contains an unavailable modulation destination");
                }
                for (const auto &[id, value] : loaded.parameters) {
                    if (id.rfind("instance.", 0) == 0)
                        continue;
                    const auto descriptor = parameters_.get(id);
                    if (!descriptor || value < number(descriptor->minimum) ||
                        value > number(descriptor->maximum))
                        throw std::runtime_error("invalid project parameter: " +
                                                 id);
                }
                recording_.cancel();
                engine_.stop();
                for (const auto &[slot, sample] : loaded.samples.sample_paths)
                    if (std::filesystem::exists(sample) &&
                        !samples_.load(SampleSlotId{slot}, sample))
                        throw std::runtime_error("sample load queue is full");
                samples_.wait_idle();
                const auto deadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (std::chrono::steady_clock::now() < deadline) {
                    bool pending{};
                    for (const auto &[slot, sample] :
                         loaded.samples.sample_paths)
                        if (std::filesystem::exists(sample)) {
                            const auto status =
                                samples_.status(SampleSlotId{slot});
                            pending |=
                                status.state != SampleSlotLoadState::loaded &&
                                status.state != SampleSlotLoadState::error;
                        }
                    if (!pending)
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                for (const auto &[slot, sample] : loaded.samples.sample_paths)
                    if (std::filesystem::exists(sample)) {
                        const auto status = samples_.status(SampleSlotId{slot});
                        if (status.state == SampleSlotLoadState::error)
                            throw std::runtime_error(status.error);
                        if (status.state != SampleSlotLoadState::loaded)
                            throw std::runtime_error(
                                "sample activation timed out");
                    }
                auto edit = timeline_.edit();
                edit.timeline() = loaded.timeline;
                edit.set_generation_seed(loaded.timeline.generation_seed);
                if (!timeline_.commit(std::move(edit),
                                      TimelineCommitPolicy::next_audio_callback,
                                      error))
                    throw std::runtime_error(error);
                if (!slices_.restore_project(loaded.samples, error))
                    throw std::runtime_error(error);
                auto loaded_mixer = loaded.mixer;
                if (loaded_mixer.track_count == 0) {
                    loaded_mixer.track_count = std::min(
                        loaded.timeline.tracks.size(), maximum_mixer_tracks);
                    for (std::size_t i = 0; i < loaded_mixer.track_count; ++i)
                        loaded_mixer.tracks[i].track =
                            loaded.timeline.tracks[i].id;
                }
                for (std::size_t i = 0; i < mixer_external_fx_count; ++i)
                    if (!loaded_mixer.external_fx[i].id) {
                        auto &loop = loaded_mixer.external_fx[i];
                        loop.id = static_cast<ExternalFxLoopId>(i + 1);
                        const auto name =
                            "External FX " + std::to_string(i + 1);
                        std::copy_n(name.begin(),
                                    std::min(name.size(), loop.name.size() - 1),
                                    loop.name.begin());
                        loop.send_left = static_cast<std::uint8_t>(2 + i * 2);
                        loop.send_right = static_cast<std::uint8_t>(3 + i * 2);
                        loop.return_left = static_cast<std::uint8_t>(i * 2);
                        loop.return_right =
                            static_cast<std::uint8_t>(1 + i * 2);
                    }
                loaded_mixer.revision = mixer_configuration_.revision + 1;
                if (!engine_.publish_mixer(loaded_mixer, error))
                    throw std::runtime_error(error);
                mixer_configuration_ = loaded_mixer;
                if (!engine_.publish_modulation(loaded.modulation))
                    throw std::runtime_error(
                        "modulation publication queue is full");
                modulation_configuration_ = loaded.modulation;
                synth_instance_parameters_.clear();
                next_modulation_route_id_ = 1;
                for (std::size_t route = 0;
                     route < modulation_configuration_.route_count; ++route)
                    next_modulation_route_id_ =
                        std::max(next_modulation_route_id_,
                                 modulation_configuration_.routes[route].id + 1);
                for (const auto &[id, value] : loaded.parameters) {
                    if (id.rfind("instance.", 0) == 0) {
                        const auto separator = id.find('.', 9);
                        if (separator == std::string::npos)
                            throw std::runtime_error(
                                "invalid instance parameter key: " + id);
                        const auto instance = static_cast<InstrumentInstanceId>(
                            std::stoul(id.substr(9, separator - 9)));
                        const auto parameter_id = id.substr(separator + 1);
                        const auto descriptor = parameters_.get(parameter_id);
                        const auto destination =
                            resolve_parameter_destination(parameter_id);
                        if (!descriptor || !destination ||
                            destination->target !=
                                ParameterTarget::subtractive_synth ||
                            instance == 0 ||
                            instance > maximum_instrument_instances ||
                            value < number(descriptor->minimum) ||
                            value > number(descriptor->maximum) ||
                            !engine_.set_synth_parameter(
                                static_cast<SynthParameter>(
                                    destination->parameter),
                                static_cast<float>(value), instance))
                            throw std::runtime_error(
                                "cannot restore instance parameter: " + id);
                        synth_instance_parameters_[instance][parameter_id] = value;
                        continue;
                    }
                    const auto previous = parameters_.get(id);
                    if (!parameters_.set(id, value, error) ||
                        !publish_dsp_parameter(engine_, parameters_, id)) {
                        if (previous)
                            parameters_.set(id, previous->current_value, error);
                        throw std::runtime_error("cannot restore parameter: " +
                                                 id);
                    }
                }
                {
                    std::lock_guard lock(mutex_);
                    project_ = std::move(loaded);
                    state_.project_name = project_.name;
                    state_.project_path = path;
                    state_.project_dirty = false;
                    state_.timeline_revision = timeline_.snapshot().revision;
                    state_.routing_scenes = project_.routing_scenes;
                    state_.active_routing_scene = project_.active_routing_scene;
                    state_.project_loading = false;
                    state_.project_error.clear();
                    state_.missing_assets = project_.missing_assets;
                    changed_locked();
                }
                return {true, sequence, snapshot().revision, {}};
            }
            if (command.action_id == "project.relink-sample.v1") {
                const auto missing = std::filesystem::path(
                    *argument<std::string>(command, "missing"));
                const auto replacement = std::filesystem::path(
                    *argument<std::string>(command, "replacement"));
                auto relinked = project_;
                if (!relink_project_sample(relinked, missing, replacement,
                                           error))
                    return {false, sequence, before.revision, std::move(error)};
                const auto found = std::find_if(
                    relinked.samples.sample_paths.begin(),
                    relinked.samples.sample_paths.end(), [&](const auto &item) {
                        return item.second == replacement;
                    });
                if (found == relinked.samples.sample_paths.end() ||
                    !samples_.load(SampleSlotId{found->first}, replacement))
                    return {false, sequence, before.revision,
                            "sample load queue is full"};
                samples_.wait_idle();
                const auto deadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(5);
                for (;;) {
                    const auto status =
                        samples_.status(SampleSlotId{found->first});
                    if (status.state == SampleSlotLoadState::loaded)
                        break;
                    if (status.state == SampleSlotLoadState::error)
                        return {false, sequence, before.revision, status.error};
                    if (std::chrono::steady_clock::now() >= deadline)
                        return {false, sequence, before.revision,
                                "sample activation timed out"};
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (!slices_.restore_project(relinked.samples, error))
                    return {false, sequence, before.revision, std::move(error)};
                std::lock_guard lock(mutex_);
                project_ = std::move(relinked);
                state_.missing_assets = project_.missing_assets;
                state_.project_dirty = true;
                changed_locked();
                return {true, sequence, state_.revision, {}};
            }
            if (command.action_id == "project.new.v1" ||
                command.action_id == "project.close.v1") {
                Project replacement;
                if (const auto found = command.arguments.find("name");
                    found != command.arguments.end())
                    replacement.name = std::get<std::string>(found->second);
                auto edit = timeline_.edit();
                edit.timeline() = replacement.timeline;
                if (!timeline_.commit(std::move(edit),
                                      TimelineCommitPolicy::next_audio_callback,
                                      error))
                    return {false, sequence, before.revision, std::move(error)};
                recording_.cancel();
                engine_.stop();
                if (!slices_.restore_project(replacement.samples, error))
                    return {false, sequence, before.revision, std::move(error)};
                replacement.mixer.revision = mixer_configuration_.revision + 1;
                for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
                    auto &loop = replacement.mixer.external_fx[i];
                    loop.id = static_cast<ExternalFxLoopId>(i + 1);
                    const auto name = "External FX " + std::to_string(i + 1);
                    std::copy_n(name.begin(),
                                std::min(name.size(), loop.name.size() - 1),
                                loop.name.begin());
                    loop.send_left = static_cast<std::uint8_t>(2 + i * 2);
                    loop.send_right = static_cast<std::uint8_t>(3 + i * 2);
                    loop.return_left = static_cast<std::uint8_t>(i * 2);
                    loop.return_right = static_cast<std::uint8_t>(1 + i * 2);
                }
                if (!engine_.publish_mixer(replacement.mixer, error))
                    return {false, sequence, before.revision, std::move(error)};
                mixer_configuration_ = replacement.mixer;
                if (!engine_.publish_modulation(replacement.modulation))
                    return {false, sequence, before.revision,
                            "modulation publication queue is full"};
                modulation_configuration_ = replacement.modulation;
                synth_instance_parameters_.clear();
                next_modulation_route_id_ = 1;
                for (const auto &slot : samples_.states())
                    if ((slot.has_sample ||
                         slot.state != SampleSlotLoadState::empty) &&
                        !samples_.unload(slot.id))
                        return {false, sequence, before.revision,
                                "sample unload queue is full"};
                {
                    std::lock_guard lock(mutex_);
                    project_ = std::move(replacement);
                    state_.project_name = project_.name;
                    state_.project_path.clear();
                    state_.project_dirty =
                        command.action_id == "project.new.v1";
                    state_.timeline_revision = timeline_.snapshot().revision;
                    state_.missing_assets.clear();
                    changed_locked();
                }
                return {true, sequence, snapshot().revision, {}};
            }
        } catch (const std::exception &exception) {
            std::lock_guard lock(mutex_);
            state_.project_loading = false;
            state_.project_saving = false;
            state_.project_error = exception.what();
            changed_locked();
            return {false, sequence, state_.revision, state_.project_error};
        }
    }
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
            const auto instance = argument<std::int64_t>(command, "instance_id");
            if (!transaction.set_track_instrument(
                    state_.selected_track,
                    *argument<std::string>(command, "instrument_id"), error,
                    instance ? static_cast<InstrumentInstanceId>(*instance)
                             : default_synth_instance_id) ||
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
        } else if (command.action_id ==
                   "instrument.instance-parameter-set.v1") {
            const auto instance =
                *argument<std::int64_t>(command, "instance_id");
            const auto &id = *argument<std::string>(command, "parameter_id");
            const auto value = *argument<double>(command, "value");
            const auto descriptor = parameters_.get(id);
            const auto destination = resolve_parameter_destination(id);
            if (instance <= 0 ||
                instance > static_cast<std::int64_t>(
                               maximum_instrument_instances) ||
                !descriptor || !destination ||
                destination->target != ParameterTarget::subtractive_synth ||
                !std::isfinite(value) || value < number(descriptor->minimum) ||
                value > number(descriptor->maximum))
                error = "invalid instrument instance parameter";
            else if (!engine_.set_synth_parameter(
                         static_cast<SynthParameter>(destination->parameter),
                         static_cast<float>(value),
                         static_cast<InstrumentInstanceId>(instance)))
                error = "DSP parameter queue is full";
            else {
                auto next = modulation_configuration_;
                bool modulation_changed{};
                for (std::size_t route = 0; route < next.route_count; ++route)
                    if (next.routes[route].destination.target ==
                            ParameterTarget::subtractive_synth &&
                        next.routes[route].destination.parameter ==
                            destination->parameter &&
                        next.routes[route].destination.instance == instance) {
                        next.routes[route].base_value =
                            static_cast<float>(value);
                        modulation_changed = true;
                    }
                if (modulation_changed && !engine_.publish_modulation(next))
                    error = "modulation publication queue is full";
                if (!error.empty())
                    return {false, sequence, before.revision,
                            std::move(error)};
                modulation_configuration_ = next;
                project_.modulation = next;
                synth_instance_parameters_[static_cast<InstrumentInstanceId>(
                    instance)][id] = value;
                changed_locked();
            }
        } else if (command.action_id == "parameter.set-normalized.v1") {
            const auto id = *argument<std::string>(command, "parameter_id");
            const auto previous = parameters_.get(id);
            if (parameters_.set_normalized(
                    id, *argument<double>(command, "value"), error)) {
                if (!publish_parameter(id)) {
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
                if (!publish_parameter(id)) {
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
                if (!publish_parameter(id)) {
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
        } else if (command.action_id == "modulation.route-create.v1") {
            const auto source_value = *argument<std::int64_t>(command, "source");
            const auto scope_value = *argument<std::int64_t>(command, "scope");
            const auto depth = *argument<double>(command, "depth");
            const auto destination_id =
                *argument<std::string>(command, "destination");
            if (source_value < 0 ||
                source_value >= static_cast<std::int64_t>(
                                    ModulationSourceId::count))
                error = "invalid modulation source";
            else if (scope_value < 0 ||
                     scope_value >
                         static_cast<std::int64_t>(ModulationScope::voice))
                error = "invalid modulation scope";
            else if (!std::isfinite(depth) || depth < -1 || depth > 1)
                error = "modulation depth must be -1..1";
            else if (modulation_configuration_.route_count ==
                     modulation_configuration_.routes.size())
                error = "modulation route capacity is exhausted";
            else {
                const auto destination =
                    resolve_parameter_destination(destination_id);
                const auto descriptor = parameters_.get(destination_id);
                if (!destination || !descriptor || !descriptor->modulatable ||
                    descriptor->type != ParameterType::real)
                    error = "parameter is not a real-time modulation destination";
                else {
                    const auto scope = static_cast<ModulationScope>(scope_value);
                    const auto source =
                        static_cast<ModulationSourceId>(source_value);
                    const auto source_scope = modulation_configuration_
                                                  .sources[static_cast<std::size_t>(
                                                      source)]
                                                  .scope;
                    const auto track = argument<std::int64_t>(command, "track_id");
                    const auto instance =
                        argument<std::int64_t>(command, "instance_id");
                    if (scope != source_scope)
                        error = "route scope must match the source scope";
                    else if (instance &&
                             (*instance < 0 ||
                              *instance > static_cast<std::int64_t>(
                                              maximum_instrument_instances)))
                        error = "instrument instance is out of range";
                    else if (scope == ModulationScope::track &&
                             (!track || *track <= 0))
                        error = "track modulation requires a positive track ID";
                    else if ((scope == ModulationScope::track ||
                              scope == ModulationScope::voice) &&
                             (!instance || *instance <= 0 ||
                              *instance > static_cast<std::int64_t>(
                                              maximum_instrument_instances)))
                        error = "local modulation requires a valid instrument instance";
                    else if (scope == ModulationScope::track &&
                             destination->target !=
                                 ParameterTarget::subtractive_synth)
                        error = "track modulation requires a synth instance destination";
                    else if (scope == ModulationScope::global && instance &&
                             *instance != 0 && destination->target !=
                                                   ParameterTarget::subtractive_synth)
                        error = "shared destinations do not accept an instrument instance";
                    else if (scope == ModulationScope::track) {
                        const auto timeline = timeline_.snapshot().timeline;
                        const auto routed = std::find_if(
                            timeline.tracks.begin(), timeline.tracks.end(),
                            [&](const Track &candidate) {
                                return candidate.id ==
                                           static_cast<TrackId>(*track) &&
                                       candidate.instrument_id ==
                                           subtractive_instrument_id &&
                                       candidate.instrument_instance_id ==
                                           static_cast<InstrumentInstanceId>(
                                               *instance);
                            });
                        if (routed == timeline.tracks.end())
                            error = "track is not routed to the requested synth instance";
                        else {
                            auto next = modulation_configuration_;
                            auto &route = next.routes[next.route_count++];
                            route.id = next_modulation_route_id_++;
                            route.source = source;
                            route.destination = *destination;
                            route.destination.instance =
                                static_cast<std::uint32_t>(*instance);
                            route.minimum = static_cast<float>(
                                number(descriptor->minimum));
                            route.maximum = static_cast<float>(
                                number(descriptor->maximum));
                            route.base_value = static_cast<float>(
                                number(descriptor->current_value));
                            route.depth = static_cast<float>(depth);
                            route.scope = scope;
                            route.track_id = static_cast<std::uint64_t>(*track);
                            if (!engine_.publish_modulation(next))
                                error = "modulation publication queue is full";
                            else {
                                modulation_configuration_ = next;
                                project_.modulation = next;
                                state_.selected_modulation_route = route.id;
                                changed_locked();
                            }
                        }
                    }
                    else if (scope == ModulationScope::voice &&
                             destination->target !=
                                 ParameterTarget::subtractive_synth)
                        error = "voice modulation requires a synth destination";
                    else {
                        auto next = modulation_configuration_;
                        auto &route = next.routes[next.route_count++];
                        route.id = next_modulation_route_id_++;
                        route.source = source;
                        route.destination = *destination;
                        route.destination.instance =
                            instance ? static_cast<std::uint32_t>(*instance) : 0;
                        route.minimum = static_cast<float>(
                            number(descriptor->minimum));
                        route.maximum = static_cast<float>(
                            number(descriptor->maximum));
                        route.base_value = static_cast<float>(
                            number(descriptor->current_value));
                        route.depth = static_cast<float>(depth);
                        route.scope = scope;
                        route.track_id = track ?
                            static_cast<std::uint64_t>(*track) : 0;
                        if (!engine_.publish_modulation(next))
                            error = "modulation publication queue is full";
                        else {
                            modulation_configuration_ = next;
                            project_.modulation = next;
                            state_.selected_modulation_route = route.id;
                            changed_locked();
                        }
                    }
                }
            }
        } else if (command.action_id == "modulation.route-delete.v1" ||
                   command.action_id == "modulation.route-select.v1" ||
                   command.action_id == "modulation.route-depth.v1" ||
                   command.action_id == "modulation.route-enable.v1") {
            const auto id = static_cast<ModulationRouteId>(
                *argument<std::int64_t>(command, "route_id"));
            auto next = modulation_configuration_;
            std::size_t index = next.route_count;
            for (std::size_t i = 0; i < next.route_count; ++i)
                if (next.routes[i].id == id)
                    index = i;
            if (index == next.route_count)
                error = "modulation route does not exist";
            else if (command.action_id == "modulation.route-select.v1") {
                state_.selected_modulation_route = id;
                changed_locked();
            } else {
                if (command.action_id == "modulation.route-delete.v1") {
                    for (std::size_t i = index + 1; i < next.route_count; ++i)
                        next.routes[i - 1] = next.routes[i];
                    --next.route_count;
                } else if (command.action_id == "modulation.route-depth.v1") {
                    const auto depth = *argument<double>(command, "depth");
                    if (!std::isfinite(depth) || depth < -1 || depth > 1)
                        error = "modulation depth must be -1..1";
                    else
                        next.routes[index].depth = static_cast<float>(depth);
                } else
                    next.routes[index].enabled =
                        *argument<bool>(command, "enabled");
                if (error.empty()) {
                    if (!engine_.publish_modulation(next))
                        error = "modulation publication queue is full";
                    else {
                        modulation_configuration_ = next;
                        project_.modulation = next;
                        if (command.action_id == "modulation.route-delete.v1" &&
                            state_.selected_modulation_route == id)
                            state_.selected_modulation_route = 0;
                        changed_locked();
                    }
                }
            }
        } else if (command.action_id == "modulation.source-configure.v1") {
            const auto source = *argument<std::int64_t>(command, "source");
            const auto waveform = *argument<std::int64_t>(command, "waveform");
            const auto scope = *argument<std::int64_t>(command, "scope");
            const auto rate = *argument<double>(command, "rate");
            const auto beats = *argument<double>(command, "beats");
            if (source < 0 || source >= static_cast<std::int64_t>(
                                            ModulationSourceId::count) ||
                waveform < 0 || waveform >
                                      static_cast<std::int64_t>(
                                          LfoWaveform::random) ||
                scope < 0 || scope >
                                 static_cast<std::int64_t>(
                                     ModulationScope::voice) ||
                !std::isfinite(rate) || rate < 0 || rate > 100 ||
                !std::isfinite(beats) || beats <= 0 || beats > 64)
                error = "invalid modulation source settings";
            else {
                auto next = modulation_configuration_;
                auto &settings = next.sources[static_cast<std::size_t>(source)];
                settings.waveform = static_cast<LfoWaveform>(waveform);
                settings.rate_hz = static_cast<float>(rate);
                settings.tempo_sync =
                    *argument<bool>(command, "tempo_sync");
                settings.beats_per_cycle = static_cast<float>(beats);
                settings.scope = static_cast<ModulationScope>(scope);
                if (settings.scope == ModulationScope::voice && source >= 4 &&
                    source != static_cast<std::int64_t>(
                                  ModulationSourceId::envelope) &&
                    source != static_cast<std::int64_t>(
                                  ModulationSourceId::velocity))
                    error = "source does not support per-voice scope";
                if (const auto *retrigger =
                        argument<bool>(command, "retrigger"))
                    settings.retrigger = *retrigger;
                const auto assign_envelope = [&](const char *name, float &field,
                                                 double minimum,
                                                 double maximum) {
                    if (const auto found = command.arguments.find(name);
                        found != command.arguments.end()) {
                        const auto value = number(found->second);
                        if (!std::isfinite(value) || value < minimum ||
                            value > maximum)
                            return false;
                        field = static_cast<float>(value);
                    }
                    return true;
                };
                if (!assign_envelope("attack", settings.attack_seconds, 0,
                                     60) ||
                    !assign_envelope("decay", settings.decay_seconds, 0, 60) ||
                    !assign_envelope("sustain", settings.sustain, 0, 1) ||
                    !assign_envelope("release", settings.release_seconds, 0,
                                     60))
                    error = "invalid modulation envelope settings";
                if (const auto *seed = argument<std::int64_t>(command, "seed")) {
                    if (*seed < 0 ||
                        *seed > std::numeric_limits<std::uint32_t>::max())
                        error = "modulation seed is out of range";
                    else
                        settings.seed = static_cast<std::uint32_t>(*seed);
                }
                bool incompatible{};
                for (std::size_t i = 0; i < next.route_count; ++i)
                    incompatible |= next.routes[i].source ==
                                        static_cast<ModulationSourceId>(source) &&
                                    next.routes[i].scope != settings.scope;
                if (error.empty() && incompatible)
                    error = "source scope conflicts with an existing route";
                else if (error.empty() && !engine_.publish_modulation(next))
                    error = "modulation publication queue is full";
                else if (error.empty()) {
                    modulation_configuration_ = next;
                    project_.modulation = next;
                    changed_locked();
                }
            }
        } else if (command.action_id == "modulation.source-reset.v1") {
            const auto source = *argument<std::int64_t>(command, "source");
            if (source < 0 || source >= static_cast<std::int64_t>(
                                            ModulationSourceId::count))
                error = "invalid modulation source";
            else if (!engine_.publish_modulation(modulation_configuration_))
                error = "modulation publication queue is full";
            else
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
                if (error.empty() &&
                    (!engine_.set_modulation_input(
                         ModulationSourceId::xy_x,
                         static_cast<float>(position.x * 2.0 - 1.0)) ||
                     !engine_.set_modulation_input(
                         ModulationSourceId::xy_y,
                         static_cast<float>(position.y * 2.0 - 1.0))))
                    error = "modulation input queue is full";
                if (error.empty())
                    changed_locked();
            }
        } else if (command.action_id == "modulation.midi-cc.v1") {
            const auto value = *argument<double>(command, "value");
            if (!std::isfinite(value) || value < 0 || value > 1)
                error = "MIDI CC modulation must be normalized";
            else if (!engine_.set_modulation_input(
                         ModulationSourceId::midi_cc,
                         static_cast<float>(value * 2.0 - 1.0)))
                error = "modulation input queue is full";
            else
                changed_locked();
        }
    }
    if (changed && (command.action_id.rfind("sample.", 0) == 0 ||
                    command.action_id.rfind("transport.", 0) == 0)) {
        std::lock_guard lock(mutex_);
        changed_locked();
    }
    if (error.empty()) {
        const auto &id = command.action_id;
        const auto musical_edit =
            id.rfind("step.", 0) == 0 || id.rfind("slice.", 0) == 0 ||
            id.rfind("sample.load", 0) == 0 ||
            id.rfind("sample.replace", 0) == 0 ||
            id.rfind("sample.unload", 0) == 0 ||
            id.rfind("parameter.", 0) == 0 || id.rfind("track.mute", 0) == 0 ||
            id.rfind("modulation.route", 0) == 0 ||
            id.rfind("modulation.source", 0) == 0 ||
            id.rfind("instrument.instance-parameter", 0) == 0 ||
            id.rfind("timeline.", 0) == 0 || id.rfind("edit.undo", 0) == 0 ||
            id.rfind("edit.redo", 0) == 0 ||
            (id.rfind("pad.", 0) == 0 && id != "pad.trigger.v1" &&
             id != "pad.release.v1" && id != "pad.select.v1" &&
             id != "pad.property-select.v1" && id != "pad.mode-select.v1");
        if (musical_edit) {
            std::lock_guard lock(mutex_);
            state_.project_dirty = true;
            changed_locked();
        }
    }
    return {error.empty(), sequence, snapshot().revision, std::move(error)};
}
void ApplicationService::synchronize_locked() const {
    auto recording = recording_.status();
    if (recording.state != RecordingState::armed &&
        recording.state != RecordingState::recording &&
        recording.state != RecordingState::completed)
        recording.configuration = recording_configuration_;
    if (recording.state == RecordingState::completed) {
        std::string recording_error;
        if (!recording_.finalize_to_slot(samples_, recording_error))
            state_.project_error = std::move(recording_error);
        else
            state_.project_dirty = true;
        recording = recording_.status();
    }
    const auto timeline = timeline_.snapshot();
    const auto history = timeline_.history_status();
    const auto playing = engine_.playing();
    const auto frame = engine_.frame_position();
    auto slots = samples_.states();
    const auto desired_tracks =
        std::min(timeline.timeline.tracks.size(), maximum_mixer_tracks);
    auto mixer_matches = mixer_configuration_.track_count == desired_tracks;
    for (std::size_t i = 0; mixer_matches && i < desired_tracks; ++i)
        mixer_matches = mixer_configuration_.tracks[i].track ==
                        timeline.timeline.tracks[i].id;
    if (!mixer_matches) {
        auto updated = mixer_configuration_;
        updated.track_count = desired_tracks;
        for (std::size_t i = 0; i < desired_tracks; ++i) {
            const auto id = timeline.timeline.tracks[i].id;
            const auto found = std::find_if(
                mixer_configuration_.tracks.begin(),
                mixer_configuration_.tracks.begin() +
                    mixer_configuration_.track_count,
                [&](const auto &channel) { return channel.track == id; });
            updated.tracks[i] = found == mixer_configuration_.tracks.begin() +
                                             mixer_configuration_.track_count
                                    ? MixerChannelSettings{}
                                    : *found;
            updated.tracks[i].track = id;
        }
        ++updated.revision;
        std::string ignored;
        if (engine_.publish_mixer(updated, ignored))
            mixer_configuration_ = updated;
    }
    const auto mixer = engine_.mixer_state();
    if (recording.state == RecordingState::publication_pending) {
        const auto active =
            std::find_if(slots.begin(), slots.end(), [&](const auto &slot) {
                return slot.id == recording.configuration.destination &&
                       slot.state == SampleSlotLoadState::loaded;
            });
        if (active != slots.end()) {
            recording_.acknowledge_audio_active();
            recording = recording_.status();
        }
    }
    auto slice_state = slices_.snapshot(state_.selected_sample_slot);
    auto parameters = parameters_.parameters();
    const auto modulation_runtime = engine_.modulation_snapshot();
    InstrumentInstanceId selected_instance = default_synth_instance_id;
    for (const auto &track : timeline.timeline.tracks)
        if (track.id == state_.selected_track)
            selected_instance = track.instrument_instance_id;
    for (auto &parameter : parameters) {
        if (const auto instance = synth_instance_parameters_.find(selected_instance);
            instance != synth_instance_parameters_.end())
            if (const auto value = instance->second.find(parameter.id);
                value != instance->second.end())
                parameter.current_value = value->second;
        parameter.effective_value = parameter.current_value;
        const auto destination = resolve_parameter_destination(parameter.id);
        if (!destination)
            continue;
        for (std::size_t route = 0;
             route < modulation_configuration_.route_count; ++route)
            if (modulation_configuration_.routes[route].destination.target ==
                    destination->target &&
                modulation_configuration_.routes[route]
                        .destination.parameter == destination->parameter &&
                (modulation_configuration_.routes[route]
                         .destination.instance == 0 ||
                 modulation_configuration_.routes[route]
                         .destination.instance == selected_instance))
                parameter.effective_value = static_cast<double>(
                    modulation_runtime.effective_values[route]);
    }
    if (state_.timeline_revision != timeline.revision &&
        !state_.project_loading)
        state_.project_dirty = true;
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
    external = external || state_.modulation_runtime.generation !=
                               modulation_runtime.generation;
    external = external || state_.mixer.revision != mixer.revision ||
               state_.mixer.master.left != mixer.master.left ||
               state_.mixer.master.right != mixer.master.right;
    if (!external)
        for (std::size_t i = 0; i < maximum_mixer_tracks; ++i)
            if (state_.mixer.tracks[i].left != mixer.tracks[i].left ||
                state_.mixer.tracks[i].right != mixer.tracks[i].right ||
                state_.mixer.tracks[i].clip != mixer.tracks[i].clip) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < mixer_aux_count; ++i)
            if (state_.mixer.aux[i].left != mixer.aux[i].left ||
                state_.mixer.aux[i].right != mixer.aux[i].right ||
                state_.mixer.aux[i].clip != mixer.aux[i].clip) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < maximum_mixer_inputs; ++i)
            if (state_.mixer.inputs[i].left != mixer.inputs[i].left ||
                state_.mixer.inputs[i].right != mixer.inputs[i].right ||
                state_.mixer.inputs[i].clip != mixer.inputs[i].clip) {
                external = true;
                break;
            }
    if (!external)
        for (std::size_t i = 0; i < mixer_external_fx_count; ++i)
            if (state_.mixer.external_returns[i].left !=
                    mixer.external_returns[i].left ||
                state_.mixer.external_returns[i].right !=
                    mixer.external_returns[i].right ||
                state_.mixer.external_send_available[i] !=
                    mixer.external_send_available[i] ||
                state_.mixer.external_return_available[i] !=
                    mixer.external_return_available[i]) {
                external = true;
                break;
            }
    const auto &old_recording = state_.recording;
    const auto &old_configuration = old_recording.configuration;
    const auto &new_configuration = recording.configuration;
    external =
        external || old_recording.state != recording.state ||
        old_configuration.source != new_configuration.source ||
        old_configuration.source_id != new_configuration.source_id ||
        old_configuration.destination != new_configuration.destination ||
        old_configuration.channels != new_configuration.channels ||
        old_configuration.monitoring != new_configuration.monitoring ||
        old_configuration.quantization != new_configuration.quantization ||
        old_configuration.start_frame != new_configuration.start_frame ||
        old_configuration.fixed_frames != new_configuration.fixed_frames ||
        old_recording.captured_frames != recording.captured_frames ||
        old_recording.dropped_frames != recording.dropped_frames ||
        old_recording.overflow_count != recording.overflow_count ||
        old_recording.finalization_microseconds !=
            recording.finalization_microseconds ||
        old_recording.peak_left != recording.peak_left ||
        old_recording.peak_right != recording.peak_right ||
        old_recording.error != recording.error;
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
    state_.recording = std::move(recording);
    state_.mixer = mixer;
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
                                     track.instrument_id, track.pattern.events,
                                     track.instrument_instance_id});
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
    state_.modulation = modulation_configuration_;
    state_.modulation_runtime = modulation_runtime;
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
                                 engine_.synth_state().maximum_voices,
                                 state_.recording.captured_frames,
                                 state_.recording.capacity_frames,
                                 state_.recording.dropped_frames,
                                 state_.recording.overflow_count,
                                 state_.recording.finalization_microseconds,
                                 state_.recording.memory_bytes,
                                 state_.mixer.rejected_publications};
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
