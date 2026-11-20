#include "neurogenic/application/parameter_binding.hpp"
#include "neurogenic/dsp/effect_chain.hpp"
#include <algorithm>
#include <cmath>

namespace neurogenic {
namespace {
double numeric(const ApplicationValue &value, bool &valid) {
    if (const auto *v = std::get_if<double>(&value)) { valid = true; return *v; }
    if (const auto *v = std::get_if<std::int64_t>(&value)) { valid = true; return static_cast<double>(*v); }
    valid = false; return 0;
}
BindingError error(BindingErrorCode code, std::string message,
                   const BindingReference &reference, std::uint64_t revision) {
    return {code, std::move(message), reference.object_id,
            reference.parameter_id, revision};
}
} // namespace

std::optional<ResolvedParameterBinding>
ParameterBindingService::resolve(const BindingReference &reference,
                                 BindingError &failure) const {
    const auto state = application_.snapshot();
    auto discovery = discover_application(application_);
    std::string object = reference.object_id;
    if (reference.kind == BindingTargetKind::selected_track) {
        if (!state.selected_track) {
            failure = error(BindingErrorCode::missing_object,
                            "no track is selected", reference, state.revision);
            return std::nullopt;
        }
        object = "track:" + std::to_string(state.selected_track);
    }
    const auto found_object = std::find_if(
        discovery.objects.begin(), discovery.objects.end(),
        [&](const auto &candidate) { return candidate.id == object; });
    if (found_object == discovery.objects.end()) {
        failure = error(reference.discovery_revision &&
                                reference.discovery_revision < state.revision
                            ? BindingErrorCode::stale_reference
                            : BindingErrorCode::missing_object,
                        "binding target does not exist", reference,
                        state.revision);
        return std::nullopt;
    }
    const auto parameter = std::find_if(
        discovery.parameters.begin(), discovery.parameters.end(),
        [&](const auto &candidate) {
            return candidate.id == reference.parameter_id &&
                   candidate.parent == object;
        });
    auto resolved_parameter = parameter;
    // Selected-object bindings may use an existing selection-aware semantic
    // parameter. Explicit-object bindings never receive this fallback: doing
    // so could silently control a different stable object.
    if (resolved_parameter == discovery.parameters.end() &&
        reference.kind == BindingTargetKind::selected_track) {
        resolved_parameter = std::find_if(
            discovery.parameters.begin(), discovery.parameters.end(),
            [&](const auto &candidate) {
                return candidate.id == reference.parameter_id &&
                       candidate.parent == "project:current";
            });
    }
    if (resolved_parameter == discovery.parameters.end()) {
        failure = error(BindingErrorCode::unknown_parameter,
                        "parameter is not available on the target object",
                        reference, state.revision);
        return std::nullopt;
    }
    failure = {};
    return ResolvedParameterBinding{reference, object, *resolved_parameter,
                                    state.revision};
}

std::optional<DiscoveryStateBinding> ParameterBindingService::resolve_state(
    const BindingReference &reference, BindingError &failure) const {
    const auto state = application_.snapshot();
    const auto discovery = discover_application(application_);
    const auto found = std::find_if(
        discovery.state_bindings.begin(), discovery.state_bindings.end(),
        [&](const auto &candidate) { return candidate.path == reference.state_path; });
    if (found == discovery.state_bindings.end()) {
        failure = error(BindingErrorCode::unknown_state_path,
                        "state path is not discoverable", reference,
                        state.revision);
        return std::nullopt;
    }
    failure = {};
    return *found;
}

BindingReadResult ParameterBindingService::read(
    const BindingReference &reference) const {
    BindingError failure;
    const auto resolved = resolve(reference, failure);
    if (!resolved) return {false, {}, {}, std::move(failure)};
    return {resolved->descriptor.readable, resolved->descriptor.base_value,
            resolved->descriptor.effective_value, {}};
}

BindingError ParameterBindingService::validate(
    const BindingReference &reference, const ApplicationValue &value,
    BindingOperation operation) const {
    BindingError failure;
    const auto resolved = resolve(reference, failure);
    if (!resolved) return failure;
    const auto &descriptor = resolved->descriptor;
    if (operation != BindingOperation::read && !descriptor.writable)
        return error(BindingErrorCode::read_only, "parameter is read-only",
                     reference, resolved->state_revision);
    if (operation == BindingOperation::toggle &&
        descriptor.type != ParameterType::boolean)
        return error(BindingErrorCode::unsupported_operation,
                     "toggle requires a boolean parameter", reference,
                     resolved->state_revision);
    if (descriptor.type == ParameterType::boolean) {
        if (!std::holds_alternative<bool>(value))
            return error(BindingErrorCode::type_mismatch,
                         "boolean value required", reference,
                         resolved->state_revision);
        return {};
    }
    if (descriptor.type == ParameterType::enumeration) {
        if (const auto *choice = std::get_if<std::string>(&value)) {
            if (std::find(descriptor.choices.begin(), descriptor.choices.end(),
                          *choice) != descriptor.choices.end())
                return {};
        } else if (const auto *index = std::get_if<std::int64_t>(&value)) {
            if (*index >= 0 && static_cast<std::size_t>(*index) <
                                   descriptor.choices.size())
                return {};
        } else {
            return error(BindingErrorCode::type_mismatch,
                         "enum choice name or index required", reference,
                         resolved->state_revision);
        }
        return error(BindingErrorCode::invalid_choice,
                     "invalid enum choice", reference,
                     resolved->state_revision);
    }
    bool valid{};
    const auto proposed = numeric(value, valid);
    if (!valid || !std::isfinite(proposed))
        return error(BindingErrorCode::type_mismatch,
                     "finite numeric value required", reference,
                     resolved->state_revision);
    bool min_valid{}, max_valid{};
    const auto minimum = numeric(descriptor.minimum, min_valid);
    const auto maximum = numeric(descriptor.maximum, max_valid);
    if ((min_valid && proposed < minimum) || (max_valid && proposed > maximum))
        return error(BindingErrorCode::out_of_range,
                     "value is outside the parameter range", reference,
                     resolved->state_revision);
    return {};
}

std::vector<std::string> ParameterBindingService::compatible_actions(
    const BindingReference &reference) const {
    BindingError failure;
    const auto resolved = resolve(reference, failure);
    if (!resolved || !resolved->descriptor.writable) return {};
    if (resolved->resolved_object_id.rfind("fx-instance:", 0) == 0)
        return {"effect.parameter-set.v1"};
    if (resolved->resolved_object_id.rfind("modulator:", 0) == 0)
        return {"modulator.parameter-set.v1"};
    return {"parameter.set-normalized.v1", "parameter.adjust.v1",
            "parameter.reset.v1"};
}

BindingError ParameterBindingService::apply(const BindingReference &reference,
                                            const ControlInput &input) {
    BindingError failure;
    const auto resolved = resolve(reference, failure);
    if (!resolved) return failure;
    const auto &descriptor = resolved->descriptor;
    ApplicationCommand command;
    command.source_id = "parameter-binding";
    if (input.operation == BindingOperation::absolute_normalized ||
        input.operation == BindingOperation::relative_steps) {
        if (!descriptor.writable)
            return error(BindingErrorCode::read_only, "parameter is read-only",
                         reference, resolved->state_revision);
    }
    const auto fx_prefix = std::string_view("fx-instance:");
    const auto mod_prefix = std::string_view("modulator:");
    if (resolved->resolved_object_id.rfind(fx_prefix, 0) == 0) {
        if (input.operation != BindingOperation::absolute_native)
            return error(BindingErrorCode::unsupported_operation,
                         "FX binding requires an absolute native value",
                         reference, resolved->state_revision);
        const auto id = std::stoll(resolved->resolved_object_id.substr(fx_prefix.size()));
        const auto state = application_.snapshot();
        const auto instance = std::find_if(
            state.mixer.configuration.effects.instances.begin(),
            state.mixer.configuration.effects.instances.begin() +
                state.mixer.configuration.effects.instance_count,
            [&](const auto &value) { return value.id == id; });
        if (instance == state.mixer.configuration.effects.instances.begin() +
                            state.mixer.configuration.effects.instance_count)
            return error(BindingErrorCode::stale_reference,
                         "effect instance disappeared", reference,
                         resolved->state_revision);
        const auto descriptors = effect_parameter_descriptors(instance->type);
        const auto parameter = std::find_if(
            descriptors.begin(), descriptors.end(), [&](const auto &candidate) {
                return reference.parameter_id.ends_with(
                    "." + std::string(candidate.stable_id));
            });
        if (parameter == descriptors.end())
            return error(BindingErrorCode::unknown_parameter,
                         "effect parameter disappeared", reference,
                         resolved->state_revision);
        const auto validation = validate(reference, input.value,
                                         input.operation);
        if (validation) return validation;
        command.action_id = "effect.parameter-set.v1";
        command.arguments = {{"id", static_cast<std::int64_t>(id)},
                             {"parameter", static_cast<std::int64_t>(parameter->id)},
                             {"value", input.value}};
    } else if (resolved->resolved_object_id.rfind(mod_prefix, 0) == 0) {
        if (input.operation != BindingOperation::absolute_native)
            return error(BindingErrorCode::unsupported_operation,
                         "modulator binding requires an absolute native value",
                         reference, resolved->state_revision);
        const auto validation = validate(reference, input.value,
                                         input.operation);
        if (validation) return validation;
        const auto id = std::stoll(resolved->resolved_object_id.substr(mod_prefix.size()));
        const auto separator = reference.parameter_id.rfind('.');
        command.action_id = "modulator.parameter-set.v1";
        command.arguments = {{"id", static_cast<std::int64_t>(id)},
                             {"parameter", reference.parameter_id.substr(separator + 1)},
                             {"value", input.value}};
    } else {
        if (input.operation == BindingOperation::absolute_native) {
            const auto validation = validate(reference, input.value,
                                             input.operation);
            if (validation) return validation;
            bool min_valid{}, max_valid{};
            const auto minimum = numeric(descriptor.minimum, min_valid);
            const auto maximum = numeric(descriptor.maximum, max_valid);
            if (!min_valid || !max_valid || maximum <= minimum)
                return error(BindingErrorCode::unsupported_operation,
                             "parameter has no numeric absolute range",
                             reference, resolved->state_revision);
            command.action_id = "parameter.set-normalized.v1";
            command.arguments = {
                {"parameter_id", reference.parameter_id},
                {"value", std::clamp((input.value - minimum) /
                                          (maximum - minimum),
                                      0.0, 1.0)}};
        } else if (input.operation == BindingOperation::absolute_normalized) {
            auto normalized = input.value;
            if (input.high_resolution_max) {
                const auto raw = static_cast<std::uint32_t>(std::llround(
                    std::clamp(input.value, 0.0,
                               static_cast<double>(input.high_resolution_max))));
                normalized = normalize_high_resolution(
                    raw, input.high_resolution_max);
            }
            command.action_id = "parameter.set-normalized.v1";
            command.arguments = {{"parameter_id", reference.parameter_id},
                                 {"value", std::clamp(normalized, 0.0, 1.0)}};
        } else if (input.operation == BindingOperation::relative_steps) {
            command.action_id = "parameter.adjust.v1";
            command.arguments = {{"parameter_id", reference.parameter_id},
                                 {"steps", input.value}};
        } else if (input.operation == BindingOperation::toggle) {
            const auto current = read(reference);
            const auto *value = std::get_if<bool>(&current.base_value);
            if (!value)
                return error(BindingErrorCode::unsupported_operation,
                             "toggle requires a boolean parameter", reference,
                             resolved->state_revision);
            command.action_id = "parameter.set-normalized.v1";
            command.arguments = {{"parameter_id", reference.parameter_id},
                                 {"value", *value ? 0.0 : 1.0}};
        } else if (input.operation == BindingOperation::momentary) {
            if (descriptor.type != ParameterType::boolean)
                return error(BindingErrorCode::unsupported_operation,
                             "momentary control requires a boolean parameter",
                             reference, resolved->state_revision);
            command.action_id = "parameter.set-normalized.v1";
            command.arguments = {{"parameter_id", reference.parameter_id},
                                 {"value", input.pressed ? 1.0 : 0.0}};
        } else {
            return error(BindingErrorCode::unsupported_operation,
                         "operation is unsupported for this parameter",
                         reference, resolved->state_revision);
        }
    }
    const auto outcome = application_.execute(command);
    if (!outcome.accepted)
        return error(BindingErrorCode::action_rejected, outcome.error,
                     reference, outcome.state_revision);
    return {};
}

double ParameterBindingService::normalize_high_resolution(
    std::uint32_t value, std::uint32_t maximum) noexcept {
    if (!maximum) return 0;
    return static_cast<double>(std::min(value, maximum)) / maximum;
}

void FaderPickup::set_target(double normalized) noexcept {
    state_.target = std::clamp(normalized, 0.0, 1.0);
    state_.acquired = false;
    state_.direction = state_.physical < state_.target ? 1 : -1;
}
bool FaderPickup::update(double normalized) noexcept {
    const auto previous = state_.physical;
    state_.physical = std::clamp(normalized, 0.0, 1.0);
    if (!state_.acquired &&
        (std::fabs(state_.physical - state_.target) <= tolerance_ ||
         (previous < state_.target && state_.physical >= state_.target) ||
         (previous > state_.target && state_.physical <= state_.target)))
        state_.acquired = true;
    state_.direction = state_.acquired ? 0
                       : state_.physical < state_.target ? 1 : -1;
    return state_.acquired;
}

void BindingUpdateStream::push(BindingUpdateKind kind, std::uint64_t revision) {
    if (overflow_) { ++dropped_; return; }
    if (queue_.size() == capacity_) {
        queue_.clear(); overflow_ = true; ++dropped_;
        queue_.push_back({sequence_++, BindingUpdateKind::resync_required,
                          revision});
        return;
    }
    queue_.push_back({sequence_++, kind, revision});
}
void BindingUpdateStream::observe(const ApplicationState &state) {
    const auto telemetry = state.performance.generation ^
                           state.modulation_runtime.generation ^
                           state.mixer.revision;
    if (!initialized_) {
        initialized_ = true;
        state_revision_ = state.revision;
        telemetry_revision_ = telemetry;
        push(BindingUpdateKind::capabilities, application_discovery_schema_version);
        push(BindingUpdateKind::state, state.revision);
        push(BindingUpdateKind::telemetry, telemetry);
        return;
    }
    if (state.revision != state_revision_) {
        state_revision_ = state.revision;
        push(BindingUpdateKind::state, state.revision);
    }
    if (telemetry != telemetry_revision_) {
        telemetry_revision_ = telemetry;
        push(BindingUpdateKind::telemetry, telemetry);
    }
}
std::vector<BindingUpdate> BindingUpdateStream::drain() {
    std::vector<BindingUpdate> result(queue_.begin(), queue_.end());
    queue_.clear(); return result;
}
void BindingUpdateStream::resynchronize(const ApplicationState &state) noexcept {
    queue_.clear(); overflow_ = false; initialized_ = true;
    state_revision_ = state.revision;
    telemetry_revision_ = state.performance.generation ^
                          state.modulation_runtime.generation ^
                          state.mixer.revision;
}
} // namespace neurogenic
