#pragma once

#include "neurogenic/application/discovery.hpp"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>

namespace neurogenic {

enum class BindingTargetKind : std::uint8_t {
    explicit_object,
    selected_track,
    state_path,
    telemetry_path
};
enum class BindingOperation : std::uint8_t {
    read,
    absolute_native,
    absolute_normalized,
    relative_steps,
    toggle,
    momentary
};
enum class BindingErrorCode : std::uint8_t {
    none,
    missing_object,
    unknown_parameter,
    unknown_state_path,
    stale_reference,
    type_mismatch,
    out_of_range,
    invalid_choice,
    read_only,
    unsupported_operation,
    action_rejected,
    update_overflow
};

struct BindingReference {
    BindingTargetKind kind{BindingTargetKind::explicit_object};
    std::string object_id, parameter_id, state_path;
    std::uint64_t discovery_revision{};
};
struct BindingError {
    BindingErrorCode code{BindingErrorCode::none};
    std::string message, object_id, parameter_id;
    std::uint64_t current_revision{};
    explicit operator bool() const noexcept { return code != BindingErrorCode::none; }
};
struct ResolvedParameterBinding {
    BindingReference reference;
    std::string resolved_object_id;
    DiscoveryParameter descriptor;
    std::uint64_t state_revision{};
};
struct BindingReadResult {
    bool available{};
    ApplicationValue base_value, effective_value;
    BindingError error;
};
struct ControlInput {
    BindingOperation operation{BindingOperation::read};
    double value{};
    bool pressed{};
    std::uint16_t high_resolution_max{};
};
struct PickupState {
    bool acquired{};
    std::int8_t direction{};
    double target{}, physical{};
};

class ParameterBindingService {
  public:
    explicit ParameterBindingService(ApplicationService &application)
        : application_(application) {}
    [[nodiscard]] std::optional<ResolvedParameterBinding>
    resolve(const BindingReference &, BindingError &) const;
    [[nodiscard]] BindingReadResult read(const BindingReference &) const;
    [[nodiscard]] std::optional<DiscoveryStateBinding>
    resolve_state(const BindingReference &, BindingError &) const;
    [[nodiscard]] BindingError validate(const BindingReference &,
                                        const ApplicationValue &,
                                        BindingOperation) const;
    [[nodiscard]] BindingError apply(const BindingReference &,
                                     const ControlInput &);
    [[nodiscard]] std::vector<std::string>
    compatible_actions(const BindingReference &) const;
    [[nodiscard]] static double normalize_high_resolution(
        std::uint32_t value, std::uint32_t maximum = 16383) noexcept;
  private:
    ApplicationService &application_;
};

class FaderPickup {
  public:
    explicit FaderPickup(double tolerance = 0.01) : tolerance_(tolerance) {}
    void set_target(double normalized) noexcept;
    [[nodiscard]] bool update(double normalized) noexcept;
    [[nodiscard]] PickupState state() const noexcept { return state_; }
  private:
    double tolerance_;
    PickupState state_;
};

enum class BindingUpdateKind : std::uint8_t {
    capabilities,
    state,
    telemetry,
    resync_required
};
struct BindingUpdate {
    std::uint64_t sequence{};
    BindingUpdateKind kind{BindingUpdateKind::state};
    std::uint64_t revision{};
};
class BindingUpdateStream {
  public:
    explicit BindingUpdateStream(std::size_t capacity = 64)
        : capacity_(std::max<std::size_t>(1, capacity)) {}
    void observe(const ApplicationState &);
    [[nodiscard]] std::vector<BindingUpdate> drain();
    void resynchronize(const ApplicationState &) noexcept;
    [[nodiscard]] bool resync_required() const noexcept { return overflow_; }
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }
  private:
    void push(BindingUpdateKind, std::uint64_t);
    std::size_t capacity_;
    std::deque<BindingUpdate> queue_;
    std::uint64_t sequence_{1}, state_revision_{}, telemetry_revision_{},
        dropped_{};
    bool initialized_{}, overflow_{};
};

} // namespace neurogenic
