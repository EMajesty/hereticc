#pragma once

#include "neurogenic/performance/monitor.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace neurogenic {

struct DiagnosticAudioFormat {
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
};

struct DiagnosticAudioPort {
    std::string name;
    std::vector<DiagnosticAudioFormat> formats;
};

struct DiagnosticMidiPort {
    std::uint32_t logical_id{};
    std::string name;
    bool connected{};
};

struct DiagnosticDisplay {
    bool available{};
    int width{}, height{}, drawable_width{}, drawable_height{};
    float content_scale{1.0F};
    std::string backend;
};
struct RealtimeSchedulingDiagnostic {
    std::string policy{"unknown"};
    int priority{};
    std::uint64_t realtime_priority_limit{};
    std::uint64_t memory_lock_limit_bytes{};
    std::uint32_t available_cpus{};
    std::string cpu_governor{"unknown"};
    bool realtime_allowed{}, memory_lock_unlimited{};
    std::vector<std::string> warnings;
};

struct HardwareDiagnosticSnapshot {
    std::string schema{"neurogenic.hardware-diagnostics.v2"};
    std::string application_version;
    std::string application_commit;
    std::string architecture;
    std::string cpu_model;
    std::string kernel;
    std::string distribution;
    std::uint64_t total_memory_bytes{};
    std::string audio_backend;
    std::uint32_t preferred_sample_rate{48000};
    std::uint32_t selected_buffer_frames{};
    std::uint32_t selected_sample_rate{};
    std::uint16_t selected_input_channels{}, selected_output_channels{};
    bool audio_running{}, backend_xruns_available{};
    std::uint64_t backend_xruns{}, device_starts{}, device_stops{},
        device_reroutes{}, device_interruptions{};
    std::vector<DiagnosticAudioPort> audio_outputs, audio_inputs;
    std::string midi_input_backend, midi_output_backend;
    std::vector<DiagnosticMidiPort> midi_inputs, midi_outputs;
    DiagnosticDisplay display;
    RealtimeSchedulingDiagnostic realtime;
};

struct HardwareTestReport {
    std::string schema{"neurogenic.hardware-test-report.v1"};
    std::string application_commit;
    std::string hardware_identity;
    std::string os_kernel;
    std::string audio_configuration;
    std::string scenario;
    std::string status;
    std::string limitations;
    std::uint32_t sample_rate{}, requested_buffer_frames{},
        negotiated_buffer_frames{};
    std::uint16_t input_channels{}, output_channels{};
    bool physical_device{}, backend_xruns_available{};
    std::uint64_t backend_xruns{}, device_starts{}, device_stops{},
        device_reroutes{}, device_interruptions{};
    RealtimeSchedulingDiagnostic realtime;
    PerformanceSnapshot measurements;
};

[[nodiscard]] HardwareDiagnosticSnapshot system_diagnostics();
[[nodiscard]] RealtimeSchedulingDiagnostic realtime_scheduling_diagnostics();
[[nodiscard]] std::string hardware_diagnostics_json(
    const HardwareDiagnosticSnapshot &);
[[nodiscard]] std::string hardware_diagnostics_summary(
    const HardwareDiagnosticSnapshot &);
[[nodiscard]] std::string hardware_test_report_json(const HardwareTestReport &);

} // namespace neurogenic
