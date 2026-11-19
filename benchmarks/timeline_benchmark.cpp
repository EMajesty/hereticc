#include "neurogenic/application/control.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/dsp/processor.hpp"
#include "neurogenic/instrument/subtractive_synth.hpp"
#include "neurogenic/performance/monitor.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/timeline/playback.hpp"
#include "neurogenic/ui/gui.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#ifdef __linux__
#include <sys/utsname.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
volatile std::uint64_t checksum{};
constexpr std::size_t iterations = 2000;
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
    const auto path = std::filesystem::temp_directory_path() /
                      "neurogenic-project-benchmark";
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
} // namespace
int main() {
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
    std::cout << "{\"schema\":\"neurogenic.benchmark.summary.v1\",\"checksum\":"
              << checksum
              << ",\"budget_passed\":" << (budget_failed ? "false" : "true")
              << "}\n";
    return budget_failed ? 1 : 0;
}
