#include "neurogenic/application/lifecycle.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace neurogenic {
namespace {
std::string escape(std::string_view value) {
    std::string result;
    for (const char c : value) {
        if (c == '"' || c == '\\') result += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) result += c;
    }
    return result;
}
bool valid(const AppliancePreferences &v, std::string &error) {
    if (!v.audio_input_channels || v.audio_input_channels > 6 ||
        !v.audio_output_channels || v.audio_output_channels > 6) {
        error = "preference audio channel counts must be 1..6";
        return false;
    }
    if (v.audio_buffer_frames < 16 || v.audio_buffer_frames > 8192) {
        error = "preference audio buffer must be 16..8192 frames";
        return false;
    }
    if (v.ui_scale && (*v.ui_scale < 1.0F || *v.ui_scale > 2.5F)) {
        error = "preference UI scale must be automatic or 1.0..2.5";
        return false;
    }
    if (v.midi_inputs.size() > 8 || v.midi_outputs.size() > 8) {
        error = "preference MIDI port list exceeds the bounded port capacity";
        return false;
    }
    return true;
}
} // namespace

const char *startup_stage_name(StartupStage value) noexcept {
    switch (value) {
    case StartupStage::idle: return "idle";
    case StartupStage::configuration: return "configuration";
    case StartupStage::project_selection: return "project-selection";
    case StartupStage::project_restoration: return "project-restoration";
    case StartupStage::audio: return "audio";
    case StartupStage::midi: return "midi";
    case StartupStage::controllers: return "controllers";
    case StartupStage::gui: return "gui";
    case StartupStage::ready: return "ready";
    case StartupStage::shutting_down: return "shutting-down";
    case StartupStage::stopped: return "stopped";
    }
    return "unknown";
}
void ApplianceLifecycle::transition(StartupStage stage, float progress) {
    state_.stage = stage;
    state_.progress = std::clamp(progress, 0.0F, 1.0F);
    state_.ready = stage == StartupStage::ready;
    state_.shutdown_cancelled = false;
    ++state_.revision;
}
void ApplianceLifecycle::issue(StartupStage stage, std::string message,
                               bool recoverable) {
    state_.issues.push_back({stage, std::move(message), recoverable});
    state_.degraded |= recoverable;
    ++state_.revision;
}
void ApplianceLifecycle::ready() { transition(StartupStage::ready, 1.0F); }
bool ApplianceLifecycle::request_shutdown(bool dirty,
                                          UnsavedShutdownChoice choice,
                                          std::string &reason) {
    if (dirty && choice == UnsavedShutdownChoice::cancel) {
        state_.shutdown_cancelled = true;
        ++state_.revision;
        reason = "project has unsaved changes; save or explicitly discard before shutdown";
        return false;
    }
    transition(StartupStage::shutting_down, 0.0F);
    reason.clear();
    return true;
}
void ApplianceLifecycle::stopped() { transition(StartupStage::stopped, 1.0F); }

std::filesystem::path default_preferences_path() {
    if (const auto *xdg = std::getenv("XDG_CONFIG_HOME"))
        return std::filesystem::path(xdg) / "neurogenic/appliance.conf";
    if (const auto *home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".config/neurogenic/appliance.conf";
    return {};
}
bool load_preferences(const std::filesystem::path &path,
                      AppliancePreferences &result, std::string &error) {
    result = {};
    if (path.empty() || !std::filesystem::exists(path)) {
        error.clear();
        return true;
    }
    std::ifstream input(path);
    std::string key;
    std::uint32_t file_version{};
    while (input >> key) {
        if (key == "version") input >> file_version;
        else if (key == "reopen_last_project") input >> result.reopen_last_project;
        else if (key == "last_project") {
            std::string value; input >> std::quoted(value); result.last_project = value;
        }
        else if (key == "audio_output") input >> std::quoted(result.audio_output);
        else if (key == "audio_input") input >> std::quoted(result.audio_input);
        else if (key == "audio_input_channels") input >> result.audio_input_channels;
        else if (key == "audio_output_channels") input >> result.audio_output_channels;
        else if (key == "audio_buffer_frames") input >> result.audio_buffer_frames;
        else if (key == "fullscreen") input >> result.fullscreen;
        else if (key == "ui_scale") {
            std::string value; input >> value;
            if (value != "auto") {
                try {
                    result.ui_scale = std::stof(value);
                } catch (...) {
                    error = "invalid UI scale in appliance preferences";
                    result = {};
                    return false;
                }
            }
        } else if (key == "midi_input") {
            std::string value; input >> std::quoted(value); result.midi_inputs.push_back(std::move(value));
        } else if (key == "midi_output") {
            std::string value; input >> std::quoted(value); result.midi_outputs.push_back(std::move(value));
        } else {
            std::string ignored; std::getline(input, ignored);
        }
        if (!input) { error = "malformed appliance preferences"; result = {}; return false; }
    }
    if (file_version != AppliancePreferences::version) {
        error = "unsupported appliance preference version";
        result = {};
        return false;
    }
    return valid(result, error);
}
bool save_preferences(const std::filesystem::path &path,
                      const AppliancePreferences &value, std::string &error) {
    if (!valid(value, error)) return false;
    try {
        if (path.empty()) throw std::runtime_error("preference path is unavailable");
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".tmp";
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open preference temporary file");
        output << "version " << AppliancePreferences::version
               << "\nreopen_last_project " << value.reopen_last_project
               << "\nlast_project " << std::quoted(value.last_project.string())
               << "\naudio_output " << std::quoted(value.audio_output)
               << "\naudio_input " << std::quoted(value.audio_input)
               << "\naudio_input_channels " << value.audio_input_channels
               << "\naudio_output_channels " << value.audio_output_channels
               << "\naudio_buffer_frames " << value.audio_buffer_frames
               << "\nfullscreen " << value.fullscreen << "\nui_scale ";
        if (value.ui_scale) output << *value.ui_scale; else output << "auto";
        output << '\n';
        for (const auto &port : value.midi_inputs) output << "midi_input " << std::quoted(port) << '\n';
        for (const auto &port : value.midi_outputs) output << "midi_output " << std::quoted(port) << '\n';
        output.flush();
        if (!output) throw std::runtime_error("cannot write appliance preferences");
        output.close();
        std::filesystem::rename(temporary, path);
        error.clear();
        return true;
    } catch (const std::exception &exception) {
        error = exception.what();
        return false;
    }
}
std::filesystem::path select_startup_project(
    const AppliancePreferences &preferences,
    const std::filesystem::path &explicit_project, std::string &warning) {
    if (!explicit_project.empty()) { warning.clear(); return explicit_project; }
    if (!preferences.reopen_last_project || preferences.last_project.empty()) {
        warning.clear(); return {};
    }
    if (!std::filesystem::exists(preferences.last_project)) {
        warning = "last project is unavailable: " + preferences.last_project.string();
        return {};
    }
    warning.clear();
    return preferences.last_project;
}
std::string lifecycle_json(const LifecycleSnapshot &value) {
    std::ostringstream out;
    out << "{\"schema\":\"neurogenic.appliance-lifecycle.v1\",\"revision\":"
        << value.revision << ",\"stage\":\"" << startup_stage_name(value.stage)
        << "\",\"progress\":" << value.progress << ",\"ready\":"
        << (value.ready ? "true" : "false") << ",\"degraded\":"
        << (value.degraded ? "true" : "false") << ",\"shutdown_cancelled\":"
        << (value.shutdown_cancelled ? "true" : "false") << ",\"issues\":[";
    for (std::size_t i = 0; i < value.issues.size(); ++i) {
        if (i) out << ',';
        out << "{\"stage\":\"" << startup_stage_name(value.issues[i].stage)
            << "\",\"recoverable\":" << (value.issues[i].recoverable ? "true" : "false")
            << ",\"message\":\"" << escape(value.issues[i].message) << "\"}";
    }
    return out.str() + "]}";
}
} // namespace neurogenic
