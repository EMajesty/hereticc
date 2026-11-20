#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/recording.hpp"
#include "neurogenic/midi/routing.hpp"
#include "neurogenic/performance/monitor.hpp"
#include "neurogenic/project/project.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace neurogenic;
using Clock = std::chrono::steady_clock;

struct Tier {
    std::string_view name;
    std::size_t tracks, groups, routes, blocks;
};

struct Result {
    double energy{};
    float peak{};
    std::uint64_t average_ns{}, worst_ns{}, overruns{}, midi_drops{},
        recording_overflows{};
    std::size_t rss{};
};

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(std::string(message));
}

Sample fixture(float amplitude) {
    constexpr std::size_t frames = 32768;
    std::vector<float> pcm(frames);
    for (std::size_t i = 0; i < frames; ++i)
        pcm[i] = amplitude * std::sin(6.283185307179586 * i / 97.0);
    return {48000, 1, std::move(pcm)};
}

Result run(const Tier &tier, std::span<const std::size_t> partitions) {
    AudioEngine engine(48000, 64);
    for (std::size_t slot = 0; slot < std::min<std::size_t>(tier.tracks, 16);
         ++slot)
        engine.set_sample_slot(SampleSlotId{static_cast<std::uint32_t>(slot)},
                               fixture(0.08F + 0.002F * slot));

    Timeline timeline;
    timeline.transport.microseconds_per_quarter = 487805; // ~123 BPM
    for (std::size_t i = 0; i < tier.tracks; ++i) {
        Track track;
        track.id = i + 1;
        track.pattern.length = ticks_per_quarter * 4;
        const bool synth = i >= std::min<std::size_t>(tier.tracks, 16);
        track.instrument_id = synth ? std::string(subtractive_instrument_id)
                                    : std::string(sample_instrument_id);
        if (synth)
            track.instrument_instance_id =
                static_cast<InstrumentInstanceId>((i % 16) + 1);
        for (std::size_t step = 0; step < 4; ++step)
            track.pattern.events.push_back(
                {i * 8 + step + 1,
                 static_cast<MusicalTick>(step * ticks_per_quarter),
                 MusicalEventType::note,
                 static_cast<std::uint8_t>(synth ? 48 + (i % 12)
                                                 : 36 + (i % 16)),
                 0.45F, ticks_per_quarter / 2});
        timeline.tracks.push_back(std::move(track));
    }

    MixerConfiguration mixer;
    mixer.revision = 1;
    mixer.track_count = tier.tracks;
    mixer.group_count = tier.groups;
    for (std::size_t i = 0; i < tier.groups; ++i) {
        mixer.groups[i].id = 100 + i;
        mixer.groups[i].gain = 0.8F;
        mixer.groups[i].aux_send = {0.05F, 0.025F};
    }
    for (std::size_t i = 0; i < tier.tracks; ++i) {
        mixer.tracks[i].track = i + 1;
        mixer.tracks[i].gain = 0.6F;
        mixer.tracks[i].pan = static_cast<float>(static_cast<int>(i % 5) - 2) /
                              5.0F;
        mixer.tracks[i].aux_send = {0.08F, 0.04F};
        if (tier.groups)
            mixer.tracks[i].destination_group = 100 + (i % tier.groups);
    }
    mixer.aux[0].effect_enabled = true;
    mixer.aux[1].effect_enabled = true;
    mixer.effects.instance_count = 2;
    mixer.effects.instances[0].id = 1;
    mixer.effects.instances[0].type = EffectProcessorType::gain;
    mixer.effects.instances[0].parameter_count = 1;
    mixer.effects.instances[0].parameters[0] = {1, 0.9F};
    mixer.effects.instances[1].id = 2;
    mixer.effects.instances[1].type = EffectProcessorType::saturation;
    mixer.effects.instances[1].parameter_count = 1;
    mixer.effects.instances[1].parameters[0] = {1, 1.1F};
    mixer.effects.master = {{1, 2}, 2, false};

    ModulationConfiguration modulation;
    modulation.update_interval = 32;
    const auto modulators = std::min<std::size_t>(tier.routes, 32);
    modulation.modulator_count = modulators;
    for (std::size_t i = 0; i < modulators; ++i) {
        auto &instance = modulation.modulators[i];
        instance.id = 1000 + i;
        instance.type = ModulatorType::lfo;
        instance.owner_kind = ModulatorOwnerKind::master;
        instance.settings.rate_hz = 0.1F + 0.03F * i;
        instance.settings.manual_value = 0.2F;
    }
    modulation.route_count = tier.routes;
    for (std::size_t i = 0; i < tier.routes; ++i) {
        auto &route = modulation.routes[i];
        route.id = 2000 + i;
        route.source_instance = 1000 + (i % modulators);
        route.source_output = 1;
        route.destination = {
            ParameterTarget::mixer_track,
            static_cast<std::uint32_t>(i & 1U ? MixerTrackModulationParameter::pan
                                              : MixerTrackModulationParameter::gain),
            static_cast<std::uint32_t>((i % tier.tracks) + 1)};
        route.minimum = i & 1U ? -1.0F : 0.0F;
        route.maximum = i & 1U ? 1.0F : 2.0F;
        route.base_value = i & 1U ? 0.0F : 0.6F;
        route.depth = 0.08F;
    }
    std::string error;
    const auto project_path = std::filesystem::temp_directory_path() /
                              ("neurogenic-integration-" +
                               std::string(tier.name));
    std::error_code ignored;
    std::filesystem::remove_all(project_path, ignored);
    Project project;
    project.name = "Integration " + std::string(tier.name);
    project.timeline = timeline;
    project.mixer = mixer;
    project.modulation = modulation;
    save_project(project, project_path);
    const auto restored = load_project(project_path);
    std::filesystem::remove_all(project_path, ignored);
    if (restored.timeline.tracks.size() != tier.tracks ||
        restored.mixer.group_count != tier.groups ||
        restored.modulation.route_count != tier.routes ||
        restored.timeline.tracks.front().id != timeline.tracks.front().id)
        fail("integrated project round-trip lost stable configuration");
    // Performance-macro sources are derived from separately persisted macro
    // definitions and are intentionally not serialized as ordinary modulators.
    modulation.modulators[0].type = ModulatorType::performance_macro;
    if (!engine.publish_mixer(mixer, error) ||
        !engine.publish_modulation(modulation) ||
        !engine.queue_timeline(timeline, error))
        fail(error.empty() ? "integrated configuration publication failed"
                           : error);

    std::size_t captured_frames{};
    for (std::size_t block = 0; block < tier.blocks; ++block)
        captured_frames += partitions[block % partitions.size()];
    RecordingService recorder(48000, captured_frames + 256);
    RecordingConfiguration capture;
    capture.source = RecordingSource::master_post_effects;
    capture.destination = SampleSlotId{31};
    capture.channels = 2;
    capture.fixed_frames = captured_frames;
    if (!recorder.arm(capture, error) || !recorder.start(0, error))
        fail("integrated recorder setup failed");
    engine.set_recording_service(&recorder);
    MidiOutputScheduler midi;
    engine.set_midi_output_scheduler(&midi);
    MidiClockOutputConfiguration clock;
    clock.enabled = true;
    clock.port_count = 1;
    clock.ports[0] = 1;
    if (!engine.publish_midi_clock_output(clock))
        fail("MIDI clock publication failed");
    MacroMorphCommand morph;
    morph.morph_id = 1;
    morph.instance = 1000;
    morph.target_value = 0.8F;
    morph.duration = 0.25F;
    morph.curve = MacroMorphCurve::s_curve;
    if (!engine.schedule_macro_morph(morph))
        fail("macro morph queue rejected prepared command");

    engine.play();
    std::array<float, 256 * 6> output{};
    std::array<float, 256 * 6> input{};
    Result result;
    std::uint64_t total_ns{};
    for (std::size_t block = 0; block < tier.blocks; ++block) {
        const auto frames = partitions[block % partitions.size()];
        const auto begin = Clock::now();
        engine.process_io({output.data(), frames * 6}, 6,
                          {input.data(), frames * 6}, 6);
        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                 begin)
                .count());
        total_ns += elapsed;
        result.worst_ns = std::max(result.worst_ns, elapsed);
        for (std::size_t frame = 0; frame < frames; ++frame)
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const float value = output[frame * 6 + channel];
                if (!std::isfinite(value))
                    fail("NaN or Inf in integrated output");
                result.energy += std::fabs(value);
                result.peak = std::max(result.peak, std::fabs(value));
            }
        (void)midi.dispatch([](const MidiPacket &) { return true; });
    }
    result.average_ns = total_ns / tier.blocks;
    const auto callback = engine.callback_statistics();
    result.overruns = callback.deadline_overruns;
    result.midi_drops = midi.dropped();
    result.recording_overflows = recorder.status().overflow_count;
    const auto memory = sample_system_now();
    result.rss = memory.resident_memory_available
                     ? static_cast<std::size_t>(memory.resident_memory_bytes)
                     : 0;
    engine.set_recording_service(nullptr);
    engine.set_midi_output_scheduler(nullptr);
    if (result.energy <= 0.01 || result.peak <= 0 || result.peak > 4.0F)
        fail("integrated output is silent or excessively clipped");
    if (result.midi_drops || result.recording_overflows ||
        recorder.status().state != RecordingState::completed ||
        recorder.status().captured_frames != captured_frames)
        fail("bounded MIDI or recording queue overflowed");
    return result;
}
} // namespace

int main() {
    constexpr std::array tiers{
        Tier{"light", 4, 1, 8, 24}, Tier{"medium", 16, 4, 32, 24},
        Tier{"heavy", 32, 8, 64, 16}, Tier{"stress", 32, 8, 128, 12}};
    constexpr std::array partitions{std::size_t{256}, std::size_t{127},
                                    std::size_t{65}, std::size_t{191}};
    try {
        for (const auto &tier : tiers) {
            const auto result = run(tier, partitions);
            std::cout << "{\"schema\":\"neurogenic.integration-validation.v1\","
                      << "\"tier\":\"" << tier.name << "\",\"status\":\"passed\","
                      << "\"sample_rate\":48000,\"average_callback_ns\":"
                      << result.average_ns << ",\"worst_callback_ns\":"
                      << result.worst_ns << ",\"deadline_overruns\":"
                      << result.overruns << ",\"peak\":" << result.peak
                      << ",\"midi_drops\":" << result.midi_drops
                      << ",\"recording_overflows\":"
                      << result.recording_overflows
                      << ",\"resident_memory_bytes\":" << result.rss << "}\n";
        }
        std::cout << "neurogenic.integration-validation.summary.v1 passed=4\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "INTEGRATION VALIDATION FAILURE: " << error.what() << '\n';
        return 1;
    }
}
