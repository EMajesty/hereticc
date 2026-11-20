#pragma once

#include "neurogenic/application/control.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neurogenic {

inline constexpr std::uint32_t application_discovery_schema_version = 1;

enum class DiscoveryValueKind : std::uint8_t {
    boolean,
    integer,
    real,
    text,
    object,
    collection
};

enum class DiscoveryUpdateRate : std::uint8_t { static_value, state, telemetry };

struct DiscoveryCapability {
    std::string id;
    std::uint32_t version{1};
    bool available{true};
    std::string reason;
};

struct DiscoveryObjectType {
    std::string id, display_name;
    bool creatable{}, deletable{}, selectable{};
};

struct DiscoveryObject {
    std::string type, id, parent, display_name;
    bool available{true}, selected{};
    std::string diagnostic;
};

struct DiscoveryParameter {
    std::string id, parent, display_name, unit;
    ParameterType type{ParameterType::real};
    ApplicationValue minimum, maximum, default_value, base_value,
        effective_value;
    double step{1.0};
    bool readable{true}, writable{true}, continuous{}, automatable{},
        modulatable{};
    std::vector<std::string> choices;
};

struct DiscoveryAction {
    ActionDescriptor descriptor;
    std::string target_type, capability;
};

struct DiscoveryStateBinding {
    std::string path, parent, description;
    DiscoveryValueKind kind{DiscoveryValueKind::real};
    DiscoveryUpdateRate update_rate{DiscoveryUpdateRate::state};
    bool readable{true};
};

struct DiscoveryLimit {
    std::string id;
    std::uint64_t used{}, maximum{};
};

struct ApplicationDiscovery {
    std::string schema{"neurogenic.application-discovery.v1"};
    std::uint32_t schema_version{application_discovery_schema_version};
    std::uint32_t minimum_client_version{1};
    std::vector<DiscoveryCapability> capabilities;
    std::vector<DiscoveryObjectType> object_types;
    std::vector<DiscoveryObject> objects;
    std::vector<DiscoveryParameter> parameters;
    std::vector<DiscoveryAction> actions;
    std::vector<DiscoveryStateBinding> state_bindings;
    std::vector<DiscoveryLimit> limits;
    std::vector<std::string> validation_errors;
};

[[nodiscard]] ApplicationDiscovery
discover_application(const ApplicationService &application);
[[nodiscard]] bool validate_discovery(const ApplicationDiscovery &,
                                      std::string &error);
[[nodiscard]] bool validate_state_binding(const ApplicationDiscovery &,
                                          std::string_view path,
                                          std::string &error);
[[nodiscard]] bool supports_discovery_schema(std::uint32_t client_minimum,
                                             std::uint32_t client_maximum)
    noexcept;
[[nodiscard]] std::string discovery_json(const ApplicationDiscovery &);

} // namespace neurogenic
