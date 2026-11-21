#include "neurogenic/diagnostics/hardware.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string_view>
#ifdef __linux__
#include <sched.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#ifndef NEUROGENIC_VERSION
#define NEUROGENIC_VERSION "development"
#endif
#ifndef NEUROGENIC_COMMIT
#define NEUROGENIC_COMMIT "unknown"
#endif

namespace neurogenic {
namespace {
std::string escape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character >= 0x20)
                result += static_cast<char>(character);
        }
    }
    return result;
}
std::string value_after_colon(const std::string &line) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) return {};
    const auto first = line.find_first_not_of(" \t", colon + 1);
    return first == std::string::npos ? std::string{} : line.substr(first);
}
std::string os_release() {
    std::ifstream file("/etc/os-release");
    std::string line;
    while (std::getline(file, line))
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            auto value = line.substr(12);
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            return value;
        }
    return "unknown";
}
std::string cpu_name() {
    std::ifstream file("/proc/cpuinfo");
    std::string line;
    while (std::getline(file, line))
        if (line.rfind("model name", 0) == 0 ||
            line.rfind("Model", 0) == 0 || line.rfind("Hardware", 0) == 0)
            return value_after_colon(line);
    return "unknown";
}
void ports_json(std::ostringstream &out,
                const std::vector<DiagnosticAudioPort> &ports) {
    out << '[';
    for (std::size_t i = 0; i < ports.size(); ++i) {
        if (i) out << ',';
        out << "{\"name\":\"" << escape(ports[i].name) << "\",\"formats\":[";
        for (std::size_t j = 0; j < ports[i].formats.size(); ++j) {
            if (j) out << ',';
            out << "{\"sample_rate\":" << ports[i].formats[j].sample_rate
                << ",\"channels\":" << ports[i].formats[j].channels << '}';
        }
        out << "]}";
    }
    out << ']';
}
void midi_json(std::ostringstream &out,
               const std::vector<DiagnosticMidiPort> &ports) {
    out << '[';
    for (std::size_t i = 0; i < ports.size(); ++i) {
        if (i) out << ',';
        out << "{\"logical_id\":" << ports[i].logical_id << ",\"name\":\""
            << escape(ports[i].name) << "\",\"connected\":"
            << (ports[i].connected ? "true" : "false") << '}';
    }
    out << ']';
}
} // namespace

HardwareDiagnosticSnapshot system_diagnostics() {
    HardwareDiagnosticSnapshot result;
    result.application_version = NEUROGENIC_VERSION;
    result.application_commit = NEUROGENIC_COMMIT;
    result.cpu_model = cpu_name();
    result.distribution = os_release();
    result.total_memory_bytes = sample_system_now().total_memory_bytes;
    result.realtime = realtime_scheduling_diagnostics();
#ifdef __linux__
    utsname name{};
    if (uname(&name) == 0) {
        result.architecture = name.machine;
        result.kernel = std::string(name.sysname) + " " + name.release;
    }
#else
    result.architecture = "unknown";
    result.kernel = "unknown";
#endif
    return result;
}

RealtimeSchedulingDiagnostic realtime_scheduling_diagnostics() {
    RealtimeSchedulingDiagnostic result;
#ifdef __linux__
    const int policy = sched_getscheduler(0);
    switch (policy) {
    case SCHED_FIFO: result.policy = "fifo"; break;
    case SCHED_RR: result.policy = "round-robin"; break;
    case SCHED_OTHER: result.policy = "other"; break;
#ifdef SCHED_BATCH
    case SCHED_BATCH: result.policy = "batch"; break;
#endif
#ifdef SCHED_IDLE
    case SCHED_IDLE: result.policy = "idle"; break;
#endif
    default: result.policy = "unknown"; break;
    }
    sched_param parameter{};
    if (sched_getparam(0, &parameter) == 0)
        result.priority = parameter.sched_priority;
    rlimit limit{};
    if (getrlimit(RLIMIT_RTPRIO, &limit) == 0) {
        result.realtime_priority_limit = limit.rlim_cur == RLIM_INFINITY
                                             ? UINT64_MAX
                                             : limit.rlim_cur;
        result.realtime_allowed = limit.rlim_cur != 0 || policy == SCHED_FIFO ||
                                  policy == SCHED_RR;
    }
    if (getrlimit(RLIMIT_MEMLOCK, &limit) == 0) {
        result.memory_lock_unlimited = limit.rlim_cur == RLIM_INFINITY;
        result.memory_lock_limit_bytes = result.memory_lock_unlimited
                                             ? UINT64_MAX
                                             : limit.rlim_cur;
    }
    const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus > 0) result.available_cpus = static_cast<std::uint32_t>(cpus);
    std::ifstream governor(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    governor >> result.cpu_governor;
    if (!result.realtime_allowed)
        result.warnings.emplace_back(
            "real-time scheduling is unavailable; configure an RT-priority "
            "limit for the user before low-buffer hardware testing");
    if (!result.memory_lock_unlimited && result.memory_lock_limit_bytes < 64ULL * 1024 * 1024)
        result.warnings.emplace_back(
            "memory-lock limit is below 64 MiB; page faults may affect "
            "low-latency testing");
    if (result.cpu_governor == "powersave")
        result.warnings.emplace_back(
            "CPU governor is powersave; compare with a performance-oriented "
            "profile during controlled tests");
#else
    result.warnings.emplace_back("real-time scheduling diagnostics require Linux");
#endif
    return result;
}

std::string hardware_diagnostics_json(const HardwareDiagnosticSnapshot &v) {
    std::ostringstream out;
    out << "{\"schema\":\"" << escape(v.schema)
        << "\",\"application\":{\"version\":\"" << escape(v.application_version)
        << "\",\"commit\":\"" << escape(v.application_commit)
        << "\"},\"system\":{\"architecture\":\"" << escape(v.architecture)
        << "\",\"cpu_model\":\"" << escape(v.cpu_model)
        << "\",\"kernel\":\"" << escape(v.kernel)
        << "\",\"distribution\":\"" << escape(v.distribution)
        << "\",\"total_memory_bytes\":" << v.total_memory_bytes
        << "},\"audio\":{\"backend\":\"" << escape(v.audio_backend)
        << "\",\"preferred_sample_rate\":" << v.preferred_sample_rate
        << ",\"selected_buffer_frames\":" << v.selected_buffer_frames
        << ",\"selected_sample_rate\":" << v.selected_sample_rate
        << ",\"selected_input_channels\":" << v.selected_input_channels
        << ",\"selected_output_channels\":" << v.selected_output_channels
        << ",\"running\":" << (v.audio_running ? "true" : "false")
        << ",\"backend_xruns_available\":"
        << (v.backend_xruns_available ? "true" : "false")
        << ",\"backend_xruns\":" << v.backend_xruns
        << ",\"device_events\":{\"starts\":" << v.device_starts
        << ",\"stops\":" << v.device_stops << ",\"reroutes\":"
        << v.device_reroutes << ",\"interruptions\":"
        << v.device_interruptions << '}'
        << ",\"outputs\":";
    ports_json(out, v.audio_outputs);
    out << ",\"inputs\":";
    ports_json(out, v.audio_inputs);
    out << "},\"midi\":{\"input_backend\":\""
        << escape(v.midi_input_backend) << "\",\"output_backend\":\""
        << escape(v.midi_output_backend) << "\",\"inputs\":";
    midi_json(out, v.midi_inputs);
    out << ",\"outputs\":";
    midi_json(out, v.midi_outputs);
    out << "},\"display\":{\"available\":"
        << (v.display.available ? "true" : "false") << ",\"width\":"
        << v.display.width << ",\"height\":" << v.display.height
        << ",\"drawable_width\":" << v.display.drawable_width
        << ",\"drawable_height\":" << v.display.drawable_height
        << ",\"content_scale\":" << v.display.content_scale
        << ",\"backend\":\"" << escape(v.display.backend)
        << "\"},\"realtime\":{\"policy\":\""
        << escape(v.realtime.policy) << "\",\"priority\":"
        << v.realtime.priority << ",\"rtprio_limit\":"
        << v.realtime.realtime_priority_limit << ",\"memlock_limit_bytes\":"
        << v.realtime.memory_lock_limit_bytes << ",\"memlock_unlimited\":"
        << (v.realtime.memory_lock_unlimited ? "true" : "false")
        << ",\"available_cpus\":" << v.realtime.available_cpus
        << ",\"cpu_governor\":\"" << escape(v.realtime.cpu_governor)
        << "\",\"warnings\":[";
    for (std::size_t i = 0; i < v.realtime.warnings.size(); ++i) {
        if (i) out << ',';
        out << '"' << escape(v.realtime.warnings[i]) << '"';
    }
    out << "]}}";
    return out.str();
}

std::string hardware_diagnostics_summary(const HardwareDiagnosticSnapshot &v) {
    std::ostringstream out;
    out << "NEUROGENIC " << v.application_version << " (" << v.application_commit
        << ")\nSystem: " << v.cpu_model << " / " << v.architecture << " / "
        << v.distribution << " / " << v.kernel << "\nMemory: "
        << v.total_memory_bytes / (1024 * 1024) << " MiB\nAudio: "
        << v.audio_backend << ", preferred " << v.preferred_sample_rate
        << " Hz, negotiated " << v.selected_sample_rate << " Hz / "
        << v.selected_buffer_frames << " frames / " << v.selected_input_channels
        << " in / " << v.selected_output_channels << " out\n  outputs: "
        << v.audio_outputs.size() << ", inputs: " << v.audio_inputs.size()
        << "\nXRUN counter: "
        << (v.backend_xruns_available ? std::to_string(v.backend_xruns)
                                      : "unavailable")
        << "\nScheduling: " << v.realtime.policy << " priority "
        << v.realtime.priority << ", RT limit "
        << v.realtime.realtime_priority_limit << ", governor "
        << v.realtime.cpu_governor << "\nMIDI: " << v.midi_input_backend << " in / "
        << v.midi_output_backend << " out; " << v.midi_inputs.size() << "/"
        << v.midi_outputs.size() << " ports\nDisplay: ";
    if (v.display.available)
        out << v.display.width << 'x' << v.display.height << " scale "
            << v.display.content_scale;
    else
        out << "unavailable (headless or no display server)";
    return out.str();
}

std::string hardware_test_report_json(const HardwareTestReport &v) {
    std::ostringstream out;
    out << "{\"schema\":\"" << escape(v.schema) << "\",\"application_commit\":\""
        << escape(v.application_commit) << "\",\"hardware_identity\":\""
        << escape(v.hardware_identity) << "\",\"os_kernel\":\""
        << escape(v.os_kernel) << "\",\"audio_configuration\":\""
        << escape(v.audio_configuration) << "\",\"scenario\":\""
        << escape(v.scenario) << "\",\"status\":\"" << escape(v.status)
        << "\",\"limitations\":\"" << escape(v.limitations)
        << "\",\"device\":{\"physical\":"
        << (v.physical_device ? "true" : "false")
        << ",\"sample_rate\":" << v.sample_rate
        << ",\"requested_buffer_frames\":" << v.requested_buffer_frames
        << ",\"negotiated_buffer_frames\":" << v.negotiated_buffer_frames
        << ",\"input_channels\":" << v.input_channels
        << ",\"output_channels\":" << v.output_channels
        << ",\"backend_xruns_available\":"
        << (v.backend_xruns_available ? "true" : "false")
        << ",\"backend_xruns\":" << v.backend_xruns
        << ",\"starts\":" << v.device_starts << ",\"stops\":"
        << v.device_stops << ",\"reroutes\":" << v.device_reroutes
        << ",\"interruptions\":" << v.device_interruptions
        << "},\"scheduling\":{\"policy\":\""
        << escape(v.realtime.policy) << "\",\"priority\":"
        << v.realtime.priority << ",\"rtprio_limit\":"
        << v.realtime.realtime_priority_limit << ",\"memlock_limit_bytes\":"
        << v.realtime.memory_lock_limit_bytes << ",\"cpu_governor\":\""
        << escape(v.realtime.cpu_governor) << "\"}"
        << ",\"measurements\":" << performance_json(v.measurements) << '}';
    return out.str();
}
} // namespace neurogenic
