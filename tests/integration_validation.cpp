#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/live_loop.hpp"
#include "neurogenic/audio/recording.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/midi/routing.hpp"
#include "neurogenic/performance/monitor.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/timeline/arrangement.hpp"
#include "neurogenic/timeline/arrangement_recording.hpp"
#include "neurogenic/timeline/performance_launching.hpp"
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

void run_performance_path() {
    AudioEngine engine(48'000, 16);
    SampleManager samples(engine);
    const auto source = std::make_shared<const Sample>(fixture(.2F));
    if (!samples.publish_prepared(SampleSlotId{0}, source, "performance.wav"))
        fail("performance sample publication failed");
    std::array<float, 1> ack_left{}, ack_right{};
    engine.stop();
    engine.process(ack_left, ack_right);

    Timeline timeline;
    timeline.transport.microseconds_per_quarter = 500'000;
    timeline.tracks = {{1, "Pattern"}, {2, "Audio"}, {3, "Live loop"}};
    PatternDefinition pattern;
    pattern.id = 11;
    pattern.name = "Performance beat";
    pattern.length = 16;
    pattern.tracks.push_back({1, {16, {{1, 0, MusicalEventType::note, 36,
                                       .75F, 4}}}});
    timeline.arrangement.patterns.push_back(pattern);
    timeline.arrangement.clips.push_back({1, 11, 0, 32, true});
    AudioClip first;
    first.id = 21;
    first.track = 2;
    first.sample_slot = 0;
    first.start = 0;
    first.duration = 24;
    first.source_end_frame = source->frames();
    first.loop_start_frame = 0;
    first.loop_end_frame = source->frames();
    first.loop = true;
    first.stretch_mode = AudioClipStretchMode::tempo_sync;
    first.source_bpm = 100.0F;
    first.loop_crossfade_frames = 16;
    auto second = first;
    second.id = 22;
    second.start = 16;
    second.pan = .25F;
    timeline.arrangement.audio_clips = {first, second};
    timeline.arrangement.audio_crossfades.push_back(
        {1, 21, 22, 8, AudioClipFadeCurve::equal_power, true});
    timeline.arrangement.playback_mode = TimelinePlaybackMode::song;
    timeline.arrangement.song_start = 0;
    timeline.arrangement.song_end = 48;
    timeline.arrangement.loop_enabled = true;
    timeline.arrangement.loop_start = 0;
    timeline.arrangement.loop_end = 48;
    timeline.arrangement.launch_slots = {
        {101, 1, LaunchSourceType::pattern, 11, LaunchBehavior::loop,
         LaunchQuantization::bar, ticks_per_quarter / 4,
         SlotFollowAction::next, 0, 1, 123, true},
        {102, 1, LaunchSourceType::pattern, 11, LaunchBehavior::loop,
         LaunchQuantization::bar, ticks_per_quarter / 4,
         SlotFollowAction::stop, 0, 1, 321, true},
        {103, 2, LaunchSourceType::audio_clip, 21, LaunchBehavior::loop,
         LaunchQuantization::bar}};
    timeline.arrangement.performance_scenes.push_back(
        {301, "Pattern and audio", {{1, 101}, {2, 103}},
         LaunchQuantization::bar, ticks_per_quarter / 4});

    std::string error;
    if (!validate_arrangement(timeline, error)) fail(error);
    TimelineService timelines(
        [&](const Timeline &value, std::string &publish_error) {
            return engine.queue_timeline(value, publish_error);
        }, timeline);
    if (!engine.queue_timeline(timeline, error)) fail(error);
    engine.restart();

    MixerConfiguration mixer;
    mixer.revision = 1;
    mixer.track_count = 3;
    mixer.group_count = 1;
    mixer.groups[0].id = 50;
    mixer.groups[0].gain = .85F;
    for (std::size_t i = 0; i < mixer.track_count; ++i) {
        mixer.tracks[i].track = i + 1;
        mixer.tracks[i].destination_group = 50;
        mixer.tracks[i].aux_send = {.1F, .05F};
    }
    mixer.effects.instance_count = 1;
    mixer.effects.instances[0].id = 1;
    mixer.effects.instances[0].type = EffectProcessorType::saturation;
    mixer.effects.instances[0].parameter_count = 1;
    mixer.effects.instances[0].parameters[0] = {1, 1.05F};
    mixer.effects.groups[0] = {{1}, 1, false};
    if (!engine.publish_mixer(mixer, error)) fail(error);

    MidiRoutingConfiguration midi_routing;
    midi_routing.accept_external_clock = true;
    if (!engine.publish_midi_routing(midi_routing) ||
        !engine.receive_midi({1, {0xFA, 0, 0}, 1, 1}))
        fail("external MIDI clock setup failed");
    for (std::uint64_t pulse = 1; pulse <= 48; ++pulse)
        if (!engine.receive_midi(
                {1, {0xF8, 0, 0}, 1, pulse * 20'833'333ULL}))
            fail("external MIDI clock queue overflowed");

    PerformanceLauncher launcher(48'000);
    if (!launcher.configure(timeline, error)) fail(error);
    std::vector<PreparedLaunch> scene;
    if (!launcher.prepare_scene(301, 0, 0, scene, error) || scene.size() != 2 ||
        scene[0].activation_frame != scene[1].activation_frame)
        fail("coordinated scene preparation failed");
    std::vector<PreparedLaunch> activated;
    launcher.acknowledge(scene.front().activation_frame, &activated);
    if (activated.size() != 2) fail("scene activation acknowledgement lost");
    PreparedLaunch follow;
    if (!launcher.prepare_follow(activated.front(), follow, error) ||
        follow.slot != 102 || !follow.follow_transition)
        fail("follow transition did not resolve deterministically");

    ArrangementRecordingService capture;
    if (!capture.arm(true, error) ||
        !capture.start(0, ArrangementRecordMode::replace, 0, false, error))
        fail("performance capture did not start");
    for (const auto &launch : activated)
        if (!capture.capture_performance(launch, launch.activation_frame,
                                         launch.scene, false, .75F))
            fail("scene activation was not captured");
    if (!capture.capture_performance(follow, follow.activation_frame, 0, true,
                                     .75F) ||
        !capture.stop(64, error) || !capture.commit(timelines, error))
        fail(error.empty() ? "performance capture commit failed" : error);
    if (!timelines.history_status().can_undo)
        fail("performance capture was not a coherent history operation");

    // Exercise playback, external clock consumption, stretched overlapping
    // clips, effects, routing, and callback partition independence constraints.
    std::array<float, 257 * 2> rendered{};
    for (const auto frames : {31U, 127U, 99U}) {
        engine.process({rendered.data(), frames},
                       {rendered.data() + 257, frames});
        if (std::any_of(rendered.begin(), rendered.begin() + frames,
                        [](float value) { return !std::isfinite(value); }))
            fail("integrated performance render produced NaN/Inf");
    }

    RecordingService recorder(48'000, 256);
    engine.set_recording_service(&recorder);
    LiveLoopService loops(engine, recorder, samples, timelines);
    LiveLoopConfiguration loop;
    loop.destination_track = 3;
    loop.destination_slot = SampleSlotId{7};
    loop.source = RecordingSource::external_mono;
    loop.channels = 1;
    loop.quantization = LiveLoopQuantization::immediate;
    loop.length_ticks = 4; // 100 frames at 120 BPM.
    const auto loop_id = loops.create(loop, error);
    if (!loop_id || !loops.arm(loop_id, LiveLoopMode::capture, error) ||
        !loops.start(loop_id, error))
        fail("integrated gapless live loop did not arm");
    std::array<float, 150> input{}, loop_left{}, loop_right{};
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = .1F + static_cast<float>(i) * .0001F;
    engine.process_duplex(loop_left, loop_right, input, 1);
    loops.update();
    const auto session = loops.find(loop_id);
    if (!session || !engine.gapless_handoff_completed() ||
        engine.gapless_first_playback_frame() !=
            session->actual_stop_frame)
        fail("gapless live-loop handoff timing mismatch");
    engine.set_recording_service(nullptr);
    samples.shutdown();
}
} // namespace

int main(int argc, char **argv) {
    constexpr std::array tiers{
        Tier{"light", 4, 1, 8, 24}, Tier{"medium", 16, 4, 32, 24},
        Tier{"heavy", 32, 8, 64, 16}, Tier{"stress", 32, 8, 128, 12}};
    constexpr std::array partitions{std::size_t{256}, std::size_t{127},
                                    std::size_t{65}, std::size_t{191}};
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--soak-seconds") {
            const auto seconds = std::max(1, std::stoi(argv[2]));
            const auto start = Clock::now();
            const auto before = sample_system_now();
            std::uint64_t passes{};
            do {
                (void)run(tiers[1], partitions);
                run_performance_path();
                ++passes;
            } while (Clock::now() - start < std::chrono::seconds(seconds));
            const auto after = sample_system_now();
            const auto growth = before.resident_memory_available &&
                                        after.resident_memory_available
                                    ? static_cast<std::int64_t>(after.resident_memory_bytes) -
                                          static_cast<std::int64_t>(before.resident_memory_bytes)
                                    : 0;
            std::cout << "{\"schema\":\"neurogenic.performance-soak.v1\","
                      << "\"passes\":" << passes << ",\"rss_growth_bytes\":"
                      << growth << "}\n";
            return 0;
        }
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
        run_performance_path();
        std::cout << "{\"schema\":\"neurogenic.live-performance-validation.v1\","
                     "\"status\":\"passed\",\"sample_rate\":48000}\n";
        std::cout << "neurogenic.integration-validation.summary.v1 passed=5\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "INTEGRATION VALIDATION FAILURE: " << error.what() << '\n';
        return 1;
    }
}
