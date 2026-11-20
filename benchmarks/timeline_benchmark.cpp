#include "neurogenic/application/control.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/dsp/processor.hpp"
#include "neurogenic/instrument/subtractive_synth.hpp"
#include "neurogenic/midi/routing.hpp"
#include "neurogenic/performance/monitor.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/timeline/playback.hpp"
#include "neurogenic/ui/gui.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#ifdef __linux__
#include <sys/utsname.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
volatile std::uint64_t checksum{};
std::size_t iterations = 2000;
bool budget_failed{};
std::string cpu_model() {
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line))
        if (line.rfind("model name", 0) == 0 || line.rfind("Model", 0) == 0) {
            const auto colon = line.find(':');
            return colon == std::string::npos ? line : line.substr(colon + 2);
        }
    return "unknown";
}
std::string kernel() {
#ifdef __linux__
    utsname value{};
    if (uname(&value) == 0)
        return std::string(value.sysname) + " " + value.release;
#endif
    return "unknown";
}
void result(const std::string &scenario, std::uint32_t rate,
            std::uint32_t frames, std::uint64_t ns,
            std::uint64_t preparation = 0) {
    if (const auto *budget = std::getenv("NEUROGENIC_BENCHMARK_BUDGET_NS"))
        if (ns && ns > std::stoull(budget))
            budget_failed = true;
    const auto memory = neurogenic::sample_system_now();
    const double deadline = 1e9 * frames / rate;
    std::cout << "{\"schema\":\"neurogenic.benchmark.v1\",\"scenario\":\""
              << scenario << "\",\"architecture\":\""
#if defined(__aarch64__)
              << "aarch64"
#elif defined(__x86_64__)
              << "x86_64"
#else
              << "portable"
#endif
              << "\",\"cpu_model\":\"" << cpu_model() << "\",\"os_kernel\":\""
              << kernel() << "\",\"compiler\":\"" << __VERSION__
              << "\",\"build_type\":\""
#ifdef NDEBUG
              << "release"
#else
              << "debug"
#endif
              << "\",\"sample_rate\":" << rate
              << ",\"buffer_frames\":" << frames
              << ",\"processing_ns_per_buffer\":" << ns
              << ",\"preparation_ns\":" << preparation
              << ",\"realtime_factor\":" << (ns ? deadline / ns : 0)
              << ",\"resident_memory_bytes\":"
              << (memory.resident_memory_available
                      ? memory.resident_memory_bytes
                      : 0)
              << ",\"iterations\":" << iterations << "}\n";
}
neurogenic::Sample sample(std::uint32_t rate, std::size_t frames = 8192) {
    return {rate, 1, std::vector<float>(frames, 0.25F)};
}
void audio_case(std::string name, std::size_t voices,
                std::uint32_t rate = 48000, std::uint32_t frames = 256) {
    neurogenic::AudioEngine engine(rate, 64);
    engine.set_sample(sample(rate));
    std::vector<float> left(frames), right(frames);
    engine.process(left, right);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        if (i % 32 == 0)
            for (std::size_t voice = 0; voice < voices; ++voice)
                engine.trigger(36, 0.5F);
        engine.process(left, right);
        checksum += left[0] != 0;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now() - begin)
                        .count() /
                    iterations;
    result(name, rate, frames, ns);
}
void synth_case(std::size_t voices, bool filter = true) {
    neurogenic::SubtractiveSynth synth(48000, 32);
    synth.set_parameter(
        static_cast<std::uint32_t>(neurogenic::SynthParameter::filter_enabled),
        filter ? 1.0F : 0.0F);
    for (std::size_t i = 0; i < voices; ++i)
        synth.note_on(static_cast<std::uint8_t>(48 + i), 0.8F);
    std::vector<float> left(256), right(256);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        std::fill(left.begin(), left.end(), 0);
        std::fill(right.begin(), right.end(), 0);
        synth.process(left, right);
        checksum += left[0] != 0;
    }
    result("synth_" + std::to_string(voices) +
               (filter ? "_filter" : "_no_filter"),
           48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void effect_case(bool enabled) {
    neurogenic::StereoDelay delay;
    delay.set_bypass(!enabled);
    delay.set_parameter(
        static_cast<std::uint32_t>(neurogenic::DelayParameter::mix), 0.25F);
    std::vector<float> left(256, 0.1F), right(256, 0.1F);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        delay.process(left, right);
        checksum += left[0] != 0;
    }
    result(enabled ? "effect_enabled" : "effect_bypassed", 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void midi_case() {
    neurogenic::MidiOutputScheduler output;
    neurogenic::MidiClockGenerator clock(48000, 487805);
    neurogenic::MidiRouter router;
    std::string error;
    const std::array<neurogenic::MidiRoute, 4> routes{{
        {1, 1, 1, 0, 1}, {2, 1, 2, 0, 2}, {3, 1, 3, 0, 3},
        {4, 1, 4, 0, 4}}};
    if (!router.configure(routes, error))
        throw std::runtime_error(error);
    const std::array<neurogenic::MidiPortId, 4> ports{1, 2, 3, 4};
    clock.start(output, ports);
    std::uint64_t frame{};
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        for (std::uint8_t note = 0; note < 16; ++note)
            (void)router.route(
                {1, {0x90, static_cast<std::uint8_t>(48 + note), 100}, 3},
                output);
        clock.process(256, frame, output, ports);
        frame += 256;
        checksum += output.dispatch([](const auto &) { return true; });
    }
    result("midi_four_ports_dense_clock", 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void external_clock_case() {
    neurogenic::MidiClockFollower follower(48000);
    follower.receive({1, {0xFA, 0, 0}, 1, 1});
    std::uint64_t timestamp{1};
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        timestamp += 20'833'333ULL + (i % 3 == 0 ? 50'000 : 0);
        follower.receive({1, {0xF8, 0, 0}, 1, timestamp});
        follower.advance(1000);
        checksum += follower.phase_q32() & 0xFF;
    }
    result("midi_external_clock_tracking", 48000, 1000,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void mixed_case() {
    neurogenic::AudioEngine engine(48000, 32);
    engine.set_sample(sample(48000));
    std::vector<float> left(256), right(256);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        if (i % 32 == 0) {
            for (int v = 0; v < 8; ++v) {
                engine.trigger(36, 0.5F);
                engine.trigger_synth(static_cast<std::uint8_t>(60 + v), 0.5F);
            }
        }
        engine.process(left, right);
        checksum += left[0] != 0;
    }
    result("mixed_sample_synth", 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
neurogenic::Timeline timeline(std::size_t events,
                              neurogenic::MusicalTick length = 960) {
    neurogenic::Timeline value;
    neurogenic::Track track;
    track.id = 1;
    track.pattern.length = length;
    for (std::size_t i = 0; i < events; ++i)
        track.pattern.events.push_back({i + 1, 0,
                                        neurogenic::MusicalEventType::note_on,
                                        36, 0.5F, 0, 0, 0});
    value.tracks.push_back(std::move(track));
    return value;
}
void dense_case() {
    neurogenic::PlaybackPlan plan;
    std::string error;
    const auto source = timeline(1024, 1);
    const auto prep = Clock::now();
    if (!neurogenic::compile_timeline(source, plan, error))
        throw std::runtime_error(error);
    const auto prep_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             Clock::now() - prep)
                             .count();
    neurogenic::TimelineScheduler scheduler(48000);
    scheduler.set_plan(&plan, 0);
    std::vector<neurogenic::TimelineScheduledEvent> output(2048);
    std::uint64_t frame{};
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        checksum += scheduler.schedule(frame, 256, output);
        frame += 256;
    }
    result("dense_timeline", 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations,
           prep_ns);
}
void publication_case() {
    neurogenic::AudioEngine engine;
    auto source = timeline(64);
    std::vector<float> left(256), right(256);
    std::string error;
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        if (!engine.queue_timeline(source, error))
            throw std::runtime_error(error);
        engine.process(left, right);
    }
    result("rapid_publication", 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void expressive_case(std::string name, std::uint8_t ratchets,
                     neurogenic::TriggerCondition condition) {
    neurogenic::Timeline source;
    source.generation_seed = 42;
    neurogenic::Track track;
    track.id = 1;
    track.pattern.length = 960;
    for (std::size_t i = 0; i < 64; ++i) {
        neurogenic::MusicalEvent event;
        event.id = i + 1;
        event.tick = static_cast<neurogenic::MusicalTick>(i * 15);
        event.type = neurogenic::MusicalEventType::note;
        event.pitch = static_cast<std::uint8_t>(36 + i % 8);
        event.velocity = 0.7F;
        event.duration = 15;
        event.trigger_probability = 0.65F;
        event.trigger_condition = condition;
        event.condition_n = 2;
        event.condition_m = 3;
        event.ratchet_count = ratchets;
        track.pattern.events.push_back(event);
    }
    source.tracks.push_back(std::move(track));
    neurogenic::PlaybackPlan plan;
    std::string error;
    const auto preparation = Clock::now();
    if (!neurogenic::compile_timeline(source, plan, error))
        throw std::runtime_error(error);
    const auto prep_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             Clock::now() - preparation)
                             .count();
    neurogenic::TimelineScheduler scheduler(48000);
    scheduler.set_plan(&plan, 0);
    scheduler.set_fill(true);
    std::vector<neurogenic::TimelineScheduledEvent> output(1024);
    std::uint64_t frame{};
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        checksum += scheduler.schedule(frame, 256, output);
        frame += 256;
    }
    result(std::move(name), 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations,
           prep_ns);
}
void automation_case(std::string name, std::size_t locks, bool mixed = false) {
    neurogenic::Timeline source;
    neurogenic::Track track;
    track.id = 1;
    track.instrument_id = std::string(neurogenic::subtractive_instrument_id);
    track.pattern.length = 960;
    track.pattern.events.push_back(
        {1, 0, neurogenic::MusicalEventType::note, 60, 0.8F, 480});
    const std::string destinations[]{"synth.cutoff", "synth.resonance",
                                     "synth.gain", "effect.delay.mix"};
    for (std::size_t i = 0; i < locks; ++i) {
        neurogenic::MusicalEvent event;
        event.id = i + 2;
        event.tick = static_cast<neurogenic::MusicalTick>((i * 37) % 960);
        event.type = neurogenic::MusicalEventType::parameter;
        event.parameter_destination = destinations[i % 4];
        event.value = i % 4 == 0 ? 1000.0F + static_cast<float>(i)
                                 : static_cast<float>((i % 8) + 1) / 10.0F;
        track.pattern.events.push_back(std::move(event));
    }
    source.tracks.push_back(std::move(track));
    neurogenic::AudioEngine engine(48000, 32);
    if (mixed)
        engine.set_sample(sample(48000));
    std::string error;
    const auto preparation = Clock::now();
    if (!engine.queue_timeline(source, error))
        throw std::runtime_error(error);
    const auto preparation_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                             preparation)
            .count();
    std::vector<float> left(256), right(256);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        if (mixed && i % 32 == 0)
            engine.trigger(36, 0.5F);
        engine.process(left, right);
        checksum += left[0] != 0;
    }
    result(std::move(name), 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations,
           preparation_ns);
}
void automation_publication_case() {
    neurogenic::AudioEngine engine;
    std::vector<float> left(256), right(256);
    std::string error;
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        neurogenic::Timeline source;
        neurogenic::Track track;
        track.id = 1;
        track.instrument_id =
            std::string(neurogenic::subtractive_instrument_id);
        track.pattern.length = 960;
        neurogenic::MusicalEvent event;
        event.id = 1;
        event.tick = 0;
        event.type = neurogenic::MusicalEventType::parameter;
        event.parameter_destination = "synth.cutoff";
        event.value = 1000.0F + static_cast<float>(i % 100);
        track.pattern.events.push_back(std::move(event));
        source.tracks.push_back(std::move(track));
        if (!engine.queue_timeline(source, error))
            throw std::runtime_error(error);
        engine.process(left, right);
    }
    result("rapid_automation_publication", 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void resample_case() {
    auto source = sample(44100, 44100);
    neurogenic::WindowedSincConverter converter;
    const auto begin = Clock::now();
    auto converted = converter.convert(source, 48000);
    checksum += converted.frames();
    result("resample_44100_to_48000", 48000, 256, 0,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
               .count());
}
void slice_playback_case(std::string name, std::size_t voices,
                         bool rapid_retrigger) {
    neurogenic::AudioEngine engine(48000, 64);
    std::uint64_t publication{};
    auto source =
        std::make_shared<const neurogenic::Sample>(sample(48000, 16384));
    if (!engine.publish_sample_slot(neurogenic::SampleSlotId{0}, source,
                                    &publication))
        throw std::runtime_error("slice benchmark publication failed");
    std::vector<float> left(256), right(256);
    engine.process({left.data(), 0}, {right.data(), 0});
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        if (rapid_retrigger || i % 32 == 0)
            for (std::size_t voice = 0; voice < voices; ++voice)
                if (!engine.trigger_slice(neurogenic::SampleSlotId{0},
                                          publication, (voice * 97) % 4096,
                                          8192 + (voice * 97) % 4096, 1.0F,
                                          0.5F))
                    break;
        engine.process(left, right);
        checksum += left[0] != 0;
    }
    result(std::move(name), 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void expressive_sample_case(std::string name, std::size_t voices,
                            neurogenic::SamplePlaybackSettings settings,
                            bool release_each_buffer = false,
                            std::uint16_t channels = 1, bool mixed = false) {
    neurogenic::AudioEngine engine(48000, 64);
    std::uint64_t publication{};
    auto prepared = sample(48000, 16384);
    if (channels == 2) {
        prepared.channels = 2;
        prepared.interleaved.assign(16384 * 2, 0.25F);
    }
    auto source =
        std::make_shared<const neurogenic::Sample>(std::move(prepared));
    if (!engine.publish_sample_slot(neurogenic::SampleSlotId{0}, source,
                                    &publication))
        throw std::runtime_error("expressive sample publication failed");
    for (std::size_t pad = 0; pad < 16; ++pad) {
        auto pad_settings = settings;
        if (mixed && pad >= 8)
            pad_settings.stretch_mode = neurogenic::StretchMode::off;
        if (!engine.map_note_to_slice(static_cast<std::uint8_t>(36 + pad),
                                      neurogenic::SampleSlotId{0}, publication,
                                      pad * 128, 8192 + pad * 128, 1.0F,
                                      pad_settings))
            throw std::runtime_error("expressive sample mapping failed");
    }
    std::vector<float> left(256), right(256);
    engine.process({left.data(), 0}, {right.data(), 0});
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        if (i % 32 == 0)
            for (std::size_t voice = 0; voice < voices; ++voice)
                if (!engine.trigger_pad(
                        static_cast<std::uint8_t>(36 + voice % 16), 0.75F, true,
                        static_cast<float>(voice % 12)))
                    break;
        if (release_each_buffer)
            for (std::size_t pad = 0; pad < 16; ++pad)
                if (!engine.trigger_pad(static_cast<std::uint8_t>(36 + pad), 0,
                                        false))
                    break;
        engine.process(left, right);
        checksum += left[0] != 0;
    }
    result(std::move(name), 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void stretch_preparation_case() {
    neurogenic::TimeStretchConfiguration configuration{
        neurogenic::StretchMode::realtime, neurogenic::StretchQuality::balanced,
        1.5F, 1.0F, 48000};
    neurogenic::TimeStretchState state;
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        neurogenic::RealtimeTimeStretcher::reset(state, configuration);
        checksum += state.window_frames;
    }
    result("stretch_voice_preparation", 48000, 256, 0,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void slice_analysis_cases() {
    auto source = sample(48000, 48000 * 4);
    for (std::size_t frame = 0; frame < source.frames(); frame += 6000)
        source.interleaved[frame] = 1.0F;
    auto begin = Clock::now();
    const auto peaks = neurogenic::build_waveform_peaks(source);
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       Clock::now() - begin)
                       .count();
    checksum += peaks.size();
    result("waveform_peak_generation", 48000, 256, 0, elapsed);
    begin = Clock::now();
    const auto transients =
        neurogenic::detect_transient_frames(source, 0.35F, 2048);
    elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  Clock::now() - begin)
                  .count();
    checksum += transients.size();
    result("transient_detection", 48000, 256, 0, elapsed);
}
void gui_case(int width, int height) {
    neurogenic::AudioEngine engine;
    neurogenic::TimelineService timeline_service(
        [&](const auto &value, std::string &error) {
            return engine.queue_timeline(value, error);
        },
        timeline(16));
    neurogenic::SampleManager samples(engine);
    neurogenic::ApplicationService app(engine, timeline_service, samples);
    neurogenic::GuiModel gui(app);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto &frame = gui.frame(width + (i & 1), height);
        checksum += frame.commands.size();
    }
    result("gui_frame_" + std::to_string(width) + "x" + std::to_string(height),
           48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
    samples.shutdown();
}
void project_io_case() {
    const auto path =
        std::filesystem::temp_directory_path() / "neurogenic-project-benchmark";
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
    neurogenic::Project project;
    project.name = "Benchmark";
    project.timeline = timeline(128);
    project.parameters["synth.cutoff"] = 4000;
    neurogenic::ProjectIoMetrics save, load;
    neurogenic::save_project(project, path, &save);
    const auto restored = neurogenic::load_project(path, &load);
    checksum += restored.timeline.tracks.size();
    result("project_save_metadata", 48000, 256, 0,
           save.elapsed_microseconds * 1000);
    result("project_load_metadata", 48000, 256, 0,
           load.elapsed_microseconds * 1000);
    std::filesystem::remove_all(path, ignored);
}
void recording_case() {
    neurogenic::AudioEngine engine;
    neurogenic::RecordingService recorder(48000, iterations * 256);
    engine.set_recording_service(&recorder);
    neurogenic::RecordingConfiguration configuration;
    configuration.source = neurogenic::RecordingSource::master_post_effects;
    configuration.destination = neurogenic::SampleSlotId{31};
    configuration.channels = 2;
    configuration.fixed_frames = iterations * 256;
    std::string error;
    recorder.arm(configuration, error);
    recorder.start(0, error);
    std::array<float, 256> left{}, right{};
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i)
        engine.process(left, right);
    const auto processing =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                             begin)
            .count() /
        iterations;
    const auto preparation_begin = Clock::now();
    const auto completed = recorder.completed_sample();
    const auto preparation =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                             preparation_begin)
            .count();
    checksum += completed.frames();
    result("internal_master_recording", 48000, 256, processing, preparation);
}
void mixer_case(std::size_t channels, bool auxiliaries, bool six_io) {
    neurogenic::Mixer mixer;
    neurogenic::MixerConfiguration configuration;
    configuration.revision = 1;
    configuration.track_count = channels;
    for (std::size_t i = 0; i < channels; ++i) {
        configuration.tracks[i].track = i + 1;
        configuration.tracks[i].aux_send =
            auxiliaries ? std::array<float, 2>{0.25F, 0.125F}
                        : std::array<float, 2>{};
    }
    if (six_io) {
        configuration.input_count = 6;
        for (std::size_t i = 0; i < 6; ++i)
            configuration.inputs[i].physical_channel =
                static_cast<std::uint8_t>(i);
    }
    std::string error;
    if (!mixer.publish(configuration, error))
        throw std::runtime_error(error);
    std::array<float, 256> left{}, right{};
    std::array<float, 256 * 6> physical{};
    const auto begin = Clock::now();
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        mixer.begin_block(left.size());
        left.fill(0);
        right.fill(0);
        physical.fill(0);
        for (std::size_t frame = 0; frame < left.size(); ++frame) {
            for (std::size_t channel = 0; channel < channels; ++channel)
                (void)mixer.add_track(channel + 1, frame, 0.01F, 0.01F);
            if (six_io)
                for (std::size_t input = 0; input < 6; ++input)
                    mixer.add_input(static_cast<std::uint8_t>(input), frame,
                                    0.005F);
        }
        mixer.process(left, right,
                      six_io ? std::span<float>{physical} : std::span<float>{},
                      six_io ? 6 : 0);
        mixer.apply_master_gain(left, right);
        mixer.route_master_output(left, right,
                                  six_io ? std::span<float>{physical}
                                         : std::span<float>{},
                                  six_io ? 6 : 0);
        checksum += left[0] != 0;
    }
    result("mixer_" + std::to_string(channels) +
               (auxiliaries ? "_two_aux" : "") + (six_io ? "_six_io" : ""),
           48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void mixer_publication_case() {
    neurogenic::Mixer mixer;
    neurogenic::MixerConfiguration configuration;
    configuration.track_count = 16;
    for (std::size_t i = 0; i < configuration.track_count; ++i)
        configuration.tracks[i].track = i + 1;
    std::string error;
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        configuration.revision = i + 1;
        configuration.tracks[i % configuration.track_count].gain =
            0.5F + static_cast<float>(i % 100) / 100.0F;
        if (!mixer.publish(configuration, error))
            throw std::runtime_error(error);
        mixer.begin_block(256);
    }
    const auto preparation =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                             begin)
            .count() /
        iterations;
    std::array<float, 256> left{}, right{};
    const auto processing_begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        mixer.begin_block(256);
        mixer.process(left, right);
        mixer.apply_master_gain(left, right);
        mixer.route_master_output(left, right);
    }
    result("mixer_rapid_publication", 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(
               Clock::now() - processing_begin)
                   .count() /
               iterations,
           preparation);
}
void external_fx_case() {
    neurogenic::Mixer mixer;
    neurogenic::MixerConfiguration configuration;
    configuration.revision = 1;
    configuration.track_count = 8;
    for (std::size_t i = 0; i < configuration.track_count; ++i)
        configuration.tracks[i].track = i + 1;
    for (std::size_t i = 0; i < neurogenic::mixer_external_fx_count; ++i) {
        auto &loop = configuration.external_fx[i];
        loop.id = i + 1;
        loop.source = neurogenic::ExternalFxSource::track;
        loop.source_id = i + 1;
        loop.send_left = static_cast<std::uint8_t>(2 + i * 2);
        loop.send_right = static_cast<std::uint8_t>(3 + i * 2);
        loop.return_left = static_cast<std::uint8_t>(i * 2);
        loop.return_right = static_cast<std::uint8_t>(1 + i * 2);
        loop.enabled = true;
        loop.latency_frames = 128;
    }
    std::string error;
    if (!mixer.publish(configuration, error))
        throw std::runtime_error(error);
    std::array<float, 256> left{}, right{};
    std::array<float, 256 * 6> physical{};
    const auto begin = Clock::now();
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        mixer.begin_block(256);
        left.fill(0);
        right.fill(0);
        physical.fill(0);
        for (std::size_t frame = 0; frame < 256; ++frame) {
            for (std::size_t channel = 0; channel < 8; ++channel)
                (void)mixer.add_track(channel + 1, frame, .01F, .01F);
            for (std::uint8_t input = 0; input < 4; ++input)
                mixer.add_input(input, frame, .005F);
        }
        mixer.process(left, right, physical, 6);
        checksum += physical[2] != 0;
    }
    result("mixer_two_external_fx_latency", 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void modulation_case(std::string name, std::size_t route_count,
                     bool tempo_sync = false) {
    neurogenic::AudioEngine engine(48000, 32);
    neurogenic::MixerConfiguration mixer;
    mixer.revision = 1;
    mixer.track_count = 1;
    mixer.tracks[0].track = 1;
    std::string mixer_error;
    if (!engine.publish_mixer(mixer, mixer_error))
        throw std::runtime_error(mixer_error);
    neurogenic::ModulationConfiguration configuration;
    configuration.route_count =
        std::min(route_count, configuration.routes.size());
    configuration.update_interval = 32;
    for (std::size_t source = 0; source < 4; ++source) {
        configuration.sources[source].waveform =
            static_cast<neurogenic::LfoWaveform>(source);
        configuration.sources[source].rate_hz = 0.5F + source * 0.25F;
        configuration.sources[source].tempo_sync = tempo_sync;
        configuration.sources[source].beats_per_cycle = 1.0F + source;
    }
    for (std::size_t route = 0; route < configuration.route_count; ++route) {
        auto &value = configuration.routes[route];
        value.id = static_cast<neurogenic::ModulationRouteId>(route + 1);
        value.source = static_cast<neurogenic::ModulationSourceId>(route % 4);
        value.depth = (route & 1U) ? -0.1F : 0.1F;
        if (route % 6 == 0) {
            value.destination = {
                neurogenic::ParameterTarget::subtractive_synth,
                static_cast<std::uint32_t>(neurogenic::SynthParameter::cutoff)};
            value.minimum = 20;
            value.maximum = 20000;
            value.base_value = 8000;
        } else if (route % 6 == 1) {
            value.destination = {neurogenic::ParameterTarget::subtractive_synth,
                                 static_cast<std::uint32_t>(
                                     neurogenic::SynthParameter::resonance)};
            value.minimum = 0;
            value.maximum = 0.95F;
            value.base_value = 0.15F;
        } else if (route % 6 == 2) {
            value.destination = {
                neurogenic::ParameterTarget::stereo_delay,
                static_cast<std::uint32_t>(neurogenic::DelayParameter::mix)};
            value.minimum = 0;
            value.maximum = 1;
            value.base_value = 0.2F;
        } else if (route % 6 == 3) {
            value.destination = {
                neurogenic::ParameterTarget::sample_voice,
                static_cast<std::uint32_t>(
                    neurogenic::SampleModulationParameter::pitch)};
            value.minimum = -24;
            value.maximum = 24;
            value.base_value = 0;
        } else if (route % 6 == 4) {
            value.destination = {
                neurogenic::ParameterTarget::mixer_track,
                static_cast<std::uint32_t>(
                    neurogenic::MixerTrackModulationParameter::pan),
                1};
            value.minimum = -1;
            value.maximum = 1;
            value.base_value = 0;
        } else {
            value.destination = {
                neurogenic::ParameterTarget::external_fx,
                static_cast<std::uint32_t>(
                    neurogenic::ExternalFxModulationParameter::wet),
                1};
            value.minimum = 0;
            value.maximum = 1;
            value.base_value = 0.5F;
        }
    }
    (void)engine.publish_modulation(configuration);
    std::vector<float> left(256), right(256);
    engine.process(left, right);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        engine.process(left, right);
        checksum += engine.modulation_snapshot().generation != 0;
    }
    result(std::move(name), 48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void synth_instance_case(std::size_t instance_count, bool voice_modulation) {
    neurogenic::AudioEngine engine(48000, 32);
    neurogenic::Timeline timeline;
    for (std::size_t i = 0; i < instance_count; ++i) {
        neurogenic::Track track;
        track.id = i + 1;
        track.instrument_id =
            std::string(neurogenic::subtractive_instrument_id);
        track.instrument_instance_id =
            static_cast<neurogenic::InstrumentInstanceId>(i + 1);
        track.pattern.length = neurogenic::ticks_per_quarter * 4;
        track.pattern.events.push_back({i + 1, 0,
                                        neurogenic::MusicalEventType::note,
                                        static_cast<std::uint8_t>(48 + i), 0.8F,
                                        neurogenic::ticks_per_quarter * 4});
        timeline.tracks.push_back(std::move(track));
    }
    if (voice_modulation) {
        neurogenic::ModulationConfiguration configuration;
        configuration.update_interval = 32;
        configuration
            .sources[static_cast<std::size_t>(
                neurogenic::ModulationSourceId::envelope)]
            .scope = neurogenic::ModulationScope::voice;
        configuration.sources[0].scope = neurogenic::ModulationScope::voice;
        configuration.route_count = std::min<std::size_t>(
            instance_count * 2, configuration.routes.size());
        for (std::size_t i = 0; i < configuration.route_count; ++i) {
            auto &route = configuration.routes[i];
            route.id = static_cast<std::uint32_t>(i + 1);
            route.source = (i & 1U) ? neurogenic::ModulationSourceId::envelope
                                    : neurogenic::ModulationSourceId::lfo_1;
            route.destination = {neurogenic::ParameterTarget::subtractive_synth,
                                 static_cast<std::uint32_t>(
                                     (i & 1U)
                                         ? neurogenic::SynthParameter::gain
                                         : neurogenic::SynthParameter::cutoff),
                                 static_cast<std::uint32_t>(i / 2 + 1)};
            route.minimum = (i & 1U) ? 0.0F : 20.0F;
            route.maximum = (i & 1U) ? 1.0F : 20000.0F;
            route.base_value = (i & 1U) ? 0.25F : 8000.0F;
            route.depth = 0.2F;
            route.scope = neurogenic::ModulationScope::voice;
        }
        (void)engine.publish_modulation(configuration);
    }
    std::string error;
    if (!engine.queue_timeline(timeline, error))
        throw std::runtime_error(error);
    engine.play();
    std::array<float, 256> left{}, right{};
    engine.process(left, right);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        engine.process(left, right);
        checksum += left[0] != 0;
    }
    result("synth_instances_" + std::to_string(instance_count) +
               (voice_modulation ? "_voice_mod" : ""),
           48000, 256,
           std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                begin)
                   .count() /
               iterations);
}
void integrated_workstation_case() {
    constexpr std::size_t sample_tracks = 8;
    constexpr std::size_t synth_tracks = 4;
    neurogenic::AudioEngine engine(48000, 64);
    for (std::size_t slot = 0; slot < sample_tracks; ++slot)
        engine.set_sample_slot(neurogenic::SampleSlotId{
                                   static_cast<std::uint32_t>(slot)},
                               sample(48000, 32768));
    neurogenic::Timeline source;
    for (std::size_t index = 0; index < sample_tracks + synth_tracks; ++index) {
        neurogenic::Track track;
        track.id = index + 1;
        track.pattern.length = neurogenic::ticks_per_quarter * 4;
        track.instrument_id =
            index < sample_tracks
                ? std::string(neurogenic::sample_instrument_id)
                : std::string(neurogenic::subtractive_instrument_id);
        if (index >= sample_tracks)
            track.instrument_instance_id =
                static_cast<neurogenic::InstrumentInstanceId>(
                    index - sample_tracks + 1);
        track.pattern.events.push_back(
            {index + 1, 0, neurogenic::MusicalEventType::note,
             static_cast<std::uint8_t>(index < sample_tracks ? 36 + index
                                                             : 48 + index),
             0.7F, neurogenic::ticks_per_quarter * 3});
        source.tracks.push_back(std::move(track));
    }
    neurogenic::MixerConfiguration mixer;
    mixer.revision = 1;
    mixer.track_count = source.tracks.size();
    for (std::size_t index = 0; index < mixer.track_count; ++index) {
        mixer.tracks[index].track = source.tracks[index].id;
        mixer.tracks[index].aux_send = {0.15F, 0.08F};
    }
    mixer.aux[0].effect_enabled = true;
    mixer.aux[1].effect_enabled = true;
    std::string error;
    if (!engine.publish_mixer(mixer, error))
        throw std::runtime_error(error);
    neurogenic::ModulationConfiguration modulation;
    modulation.update_interval = 32;
    modulation.route_count = 8;
    for (std::size_t index = 0; index < modulation.route_count; ++index) {
        auto &route = modulation.routes[index];
        route.id = index + 1;
        route.source = static_cast<neurogenic::ModulationSourceId>(index % 4);
        route.destination = {
            neurogenic::ParameterTarget::subtractive_synth,
            static_cast<std::uint32_t>(neurogenic::SynthParameter::cutoff),
            static_cast<std::uint32_t>(index % synth_tracks + 1)};
        route.minimum = 20;
        route.maximum = 20000;
        route.base_value = 5000;
        route.depth = 0.1F;
    }
    if (!engine.publish_modulation(modulation) ||
        !engine.queue_timeline(source, error))
        throw std::runtime_error(error.empty() ? "integrated publication failed"
                                               : error);
    neurogenic::TimelineService timeline_service(
        [&](const auto &value, std::string &publish_error) {
            return engine.queue_timeline(value, publish_error);
        },
        source);
    neurogenic::SampleManager samples(engine);
    neurogenic::ApplicationService application(engine, timeline_service,
                                                samples);
    neurogenic::GuiModel gui(application);
    // ApplicationService publishes its initial snapshots during construction;
    // restore the combined-workload mixer and modulation snapshots afterward.
    if (!engine.publish_mixer(mixer, error) ||
        !engine.publish_modulation(modulation))
        throw std::runtime_error(error.empty() ? "integrated state publication failed"
                                               : error);
    neurogenic::RecordingService recorder(48000, iterations * 256 + 256);
    neurogenic::RecordingConfiguration recording;
    recording.source = neurogenic::RecordingSource::master_post_effects;
    recording.destination = neurogenic::SampleSlotId{31};
    recording.channels = 2;
    recording.fixed_frames = iterations * 256;
    if (!recorder.arm(recording, error) || !recorder.start(0, error))
        throw std::runtime_error(error);
    engine.set_recording_service(&recorder);
    neurogenic::MidiOutputScheduler midi;
    engine.set_midi_output_scheduler(&midi);
    neurogenic::MidiClockOutputConfiguration clock;
    clock.enabled = true;
    clock.port_count = 1;
    clock.ports[0] = 1;
    (void)engine.publish_midi_clock_output(clock);
    engine.play();
    std::array<float, 256> left{}, right{};
    engine.process(left, right);
    std::uint64_t callback_total{};
    const auto gui_begin = Clock::now();
    for (std::size_t index = 0; index < iterations; ++index) {
        const auto callback_begin = Clock::now();
        engine.process(left, right);
        callback_total +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - callback_begin)
                .count();
        checksum += left[0] != 0;
        checksum += midi.dispatch([](const auto &) { return true; });
        checksum += gui.frame(1920 + static_cast<int>(index & 1U), 1200)
                        .commands.size();
    }
    const auto combined_elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                             gui_begin)
            .count() /
        iterations;
    result("integrated_8_sample_4_synth_mod_aux_record_midi", 48000, 256,
           callback_total / iterations);
    result("integrated_gui_state_sync_1920x1200", 48000, 256,
           combined_elapsed - callback_total / iterations);
    const auto callback = engine.callback_statistics();
    std::cout << "{\"schema\":\"neurogenic.integration.metrics.v1\","
                 "\"peak_callback_ns\":"
              << callback.peak_nanoseconds << ",\"deadline_overruns\":"
              << callback.deadline_overruns << ",\"midi_dropped\":"
              << midi.dropped() << ",\"recording_overflows\":"
              << recorder.status().overflow_count << "}\n";
    engine.set_midi_output_scheduler(nullptr);
    engine.set_recording_service(nullptr);
    samples.shutdown();
}
} // namespace
int main(int argc, char **argv) {
    for (int argument = 1; argument < argc; ++argument) {
        const std::string_view option{argv[argument]};
        if (option != "--iterations" || argument + 1 >= argc) {
            std::cerr << "usage: neurogenic_benchmark [--iterations COUNT]\n";
            return 2;
        }
        const std::string_view value{argv[++argument]};
        std::size_t parsed{};
        const auto conversion =
            std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (conversion.ec != std::errc{} ||
            conversion.ptr != value.data() + value.size() || parsed == 0) {
            std::cerr << "--iterations requires a positive integer\n";
            return 2;
        }
        iterations = parsed;
    }
    audio_case("idle", 0);
    audio_case("voices_8", 8);
    audio_case("voices_16", 16);
    audio_case("voices_32", 32);
    audio_case("sample_44100", 16, 44100);
    audio_case("sample_48000", 16, 48000);
    synth_case(1);
    synth_case(8);
    synth_case(16);
    synth_case(32);
    synth_case(16, false);
    effect_case(false);
    effect_case(true);
    midi_case();
    external_clock_case();
    mixed_case();
    dense_case();
    expressive_case("probability_conditions", 1,
                    neurogenic::TriggerCondition::n_of_m);
    expressive_case("maximum_ratchets_fill", 8,
                    neurogenic::TriggerCondition::fill_active);
    publication_case();
    automation_case("parameter_locks_sparse", 4);
    automation_case("parameter_locks_dense", 256);
    automation_case("mixed_sample_synth_automation", 64, true);
    automation_publication_case();
    resample_case();
    slice_playback_case("slice_triggers_16", 16, false);
    slice_playback_case("slice_voices_32_overlapping", 32, false);
    slice_playback_case("slice_rapid_retrigger", 16, true);
    neurogenic::SamplePlaybackSettings expressive;
    expressive.semitones = 3;
    expressive.fine_cents = 17;
    expressive_sample_case("pitched_slice_voices_16", 16, expressive);
    expressive_sample_case("pitched_slice_voices_32", 32, expressive);
    expressive.reverse = true;
    expressive_sample_case("reverse_slice_playback", 16, expressive);
    expressive = {};
    expressive.choke_group = 1;
    expressive_sample_case("choke_heavy_sample_playback", 16, expressive);
    expressive = {};
    expressive.mode = neurogenic::SamplePlaybackMode::gate;
    expressive.release_seconds = 0.01F;
    expressive_sample_case("gate_release_envelopes", 16, expressive, true);
    expressive = {};
    expressive.stretch_mode = neurogenic::StretchMode::realtime;
    expressive.stretch_ratio = 0.5F;
    expressive_sample_case("stretch_mono_0_5x", 1, expressive);
    expressive.stretch_ratio = 1.5F;
    expressive_sample_case("stretch_mono_1_5x", 1, expressive);
    expressive_sample_case("stretch_stereo_1_5x", 1, expressive, false, 2);
    expressive.stretch_ratio = 2.0F;
    expressive_sample_case("stretch_mono_2x", 1, expressive);
    expressive_sample_case("stretch_voices_8", 8, expressive);
    expressive_sample_case("stretch_voices_16", 16, expressive);
    expressive_sample_case("stretch_mixed_ordinary_16", 16, expressive, false,
                           1, true);
    stretch_preparation_case();
    slice_analysis_cases();
    gui_case(1920, 1200);
    gui_case(1024, 800);
    gui_case(1024, 600);
    project_io_case();
    recording_case();
    mixer_case(4, false, false);
    mixer_case(8, true, false);
    mixer_case(16, true, false);
    mixer_case(16, true, true);
    mixer_publication_case();
    external_fx_case();
    modulation_case("modulation_lfos_routes_16", 16);
    modulation_case("modulation_routes_32", 32);
    modulation_case("modulation_tempo_sync", 16, true);
    synth_instance_case(4, false);
    synth_instance_case(8, false);
    synth_instance_case(16, false);
    synth_instance_case(8, true);
    synth_instance_case(16, true);
    integrated_workstation_case();
    std::cout << "{\"schema\":\"neurogenic.benchmark.summary.v1\",\"checksum\":"
              << checksum
              << ",\"budget_passed\":" << (budget_failed ? "false" : "true")
              << "}\n";
    return budget_failed ? 1 : 0;
}
