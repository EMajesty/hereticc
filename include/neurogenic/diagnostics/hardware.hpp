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

struct HardwareDiagnosticSnapshot {
    std::string schema{"neurogenic.hardware-diagnostics.v1"};
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
    std::vector<DiagnosticAudioPort> audio_outputs, audio_inputs;
    std::string midi_input_backend, midi_output_backend;
    std::vector<DiagnosticMidiPort> midi_inputs, midi_outputs;
    DiagnosticDisplay display;
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
    PerformanceSnapshot measurements;
};

[[nodiscard]] HardwareDiagnosticSnapshot system_diagnostics();
[[nodiscard]] std::string hardware_diagnostics_json(
    const HardwareDiagnosticSnapshot &);
[[nodiscard]] std::string hardware_diagnostics_summary(
    const HardwareDiagnosticSnapshot &);
[[nodiscard]] std::string hardware_test_report_json(const HardwareTestReport &);

} // namespace neurogenic
