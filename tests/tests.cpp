#include "neurogenic/application/control.hpp"
#include "neurogenic/application/controller.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/core/control_dispatcher.hpp"
#include "neurogenic/core/spsc_queue.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include "neurogenic/sequencer/pattern_adapter.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/timeline/io.hpp"
#include "neurogenic/timeline/playback.hpp"
#include "neurogenic/ui/gui.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(float actual, float expected, const char *message) {
    if (std::fabs(actual - expected) > 0.0001F)
        throw std::runtime_error(message);
}

neurogenic::Sample constant_sample(std::size_t frames, float value = 1.0F) {
    return {48000, 1, std::vector<float>(frames, value)};
}

void write_pcm16_wav(const std::filesystem::path &path, std::uint32_t rate,
                     std::uint16_t channels, const std::vector<float> &values) {
    std::ofstream output(path, std::ios::binary);
    const auto bytes = static_cast<std::uint32_t>(values.size() * 2);
    auto write16 = [&](std::uint16_t value) {
        output.write(reinterpret_cast<const char *>(&value), 2);
    };
    auto write32 = [&](std::uint32_t value) {
        output.write(reinterpret_cast<const char *>(&value), 4);
    };
    output.write("RIFF", 4);
    write32(36 + bytes);
    output.write("WAVEfmt ", 8);
    write32(16);
    write16(1);
    write16(channels);
    write32(rate);
    write32(rate * channels * 2);
    write16(channels * 2);
    write16(16);
    output.write("data", 4);
    write32(bytes);
    for (const auto value : values) {
        const auto pcm =
            static_cast<std::int16_t>(std::clamp(value, -1.0F, 1.0F) * 32767);
        output.write(reinterpret_cast<const char *>(&pcm), 2);
    }
}

void write_wav_payload(const std::filesystem::path &path, std::uint16_t format,
                       std::uint16_t bits, const std::vector<char> &payload) {
    std::ofstream output(path, std::ios::binary);
    auto write16 = [&](std::uint16_t value) {
        output.write(reinterpret_cast<const char *>(&value), 2);
    };
    auto write32 = [&](std::uint32_t value) {
        output.write(reinterpret_cast<const char *>(&value), 4);
    };
    const auto bytes = static_cast<std::uint32_t>(payload.size());
    output.write("RIFF", 4);
    write32(36 + bytes);
    output.write("WAVEfmt ", 8);
    write32(16);
    write16(format);
    write16(1);
    write32(48000);
    write32(48000 * bits / 8);
    write16(bits / 8);
    write16(bits);
    output.write("data", 4);
    write32(bytes);
    output.write(payload.data(), payload.size());
}

void test_queue_and_scheduler() {
    using namespace neurogenic;
    SpscQueue<int, 4> queue;
    check(queue.push(1) && queue.push(2) && queue.push(3), "queue fill");
    check(!queue.push(4), "queue overflow");
    int item{};
    check(queue.pop(item) && item == 1, "queue order");

    Scheduler scheduler(1600, 60);
    Pattern first;
    first.steps[0] = {true, 42, 1};
    first.steps[4] = {true, 42, 1};
    first.revision = 1;
    scheduler.request_pattern(first);
    std::array<ScheduledEvent, 64> events{};
    auto count = scheduler.schedule(0, 1601, events);
    check(count == 2 && events[0].frame == 0 && events[1].frame == 1600,
          "sample offsets");
    Pattern second;
    second.steps[4] = {true, 43, 1};
    second.revision = 2;
    scheduler.request_pattern(second);
    scheduler.schedule(1601, 4799, events);
    check(scheduler.active_pattern().revision == 1, "early pattern swap");
    scheduler.schedule(6400, 2, events);
    check(scheduler.active_pattern().revision == 2, "bar pattern swap");

    Scheduler drift(44100, 123.0);
    Pattern every;
    for (auto &step : every.steps)
        step.enabled = true;
    drift.request_pattern(every);
    std::uint64_t absolute{}, seen{};
    for (std::size_t call = 0; seen < 5000; ++call) {
        const auto frames = static_cast<std::uint32_t>((call * 7919) % 997 + 1);
        auto n = drift.schedule(absolute, frames, events);
        for (std::size_t i = 0; i < n; ++i) {
            const auto expected = static_cast<std::uint64_t>(
                std::llround(static_cast<long double>(seen) * 44100.0L * 60.0L /
                             123.0L / 4.0L));
            check(absolute + events[i].frame == expected, "scheduler drift");
            ++seen;
        }
        absolute += frames;
    }
}

void test_polyphony_and_events() {
    using namespace neurogenic;
    AudioEngine overlap(48000, 4);
    overlap.set_sample(constant_sample(8));
    overlap.trigger(60, 1, 0);
    overlap.trigger(61, 1, 1);
    std::array<float, 8> left{}, right{};
    overlap.process(left, right);
    near(left[0], 0.8F, "first voice start");
    near(left[1], 1.6F, "overlapping retrigger");

    AudioEngine simultaneous(48000, 8);
    simultaneous.set_sample(constant_sample(4));
    simultaneous.trigger(60, 0.25F);
    simultaneous.trigger(61, 0.5F);
    simultaneous.trigger(62, 1.0F);
    simultaneous.process(left, right);
    near(left[0], 1.4F, "simultaneous triggers were not all consumed");

    AudioEngine ordered(48000, 2);
    ordered.set_sample(constant_sample(4));
    ordered.trigger(60, 0.25F);
    ordered.trigger(61, 0.5F);
    ordered.trigger(62, 1.0F);
    ordered.process(left, right);
    near(left[0], 1.2F, "equal-offset enqueue ordering");

    AudioEngine offsets(48000, 4);
    offsets.set_sample(constant_sample(8));
    offsets.trigger(60, 1, 5);
    offsets.process({left.data(), 2}, {right.data(), 2});
    near(left[0], 0, "early frame offset");
    offsets.process({left.data(), 4}, {right.data(), 4});
    near(left[2], 0, "frame before carried event");
    near(left[3], 0.8F, "carried frame offset");

    AudioEngine stealing(48000, 2);
    stealing.set_sample(constant_sample(8));
    stealing.trigger(60, 0.25F, 0);
    stealing.trigger(61, 0.5F, 1);
    stealing.trigger(62, 1.0F, 2);
    stealing.process(left, right);
    near(left[2], 1.2F, "oldest voice stealing policy");
    check(stealing.stolen_voices() == 1, "voice stealing counter");

    AudioEngine overflow;
    overflow.set_sample(constant_sample(2));
    for (int i = 0; i < 100; ++i)
        overflow.trigger(60, 1);
    check(overflow.dropped_triggers() == 36, "trigger overflow count");
    overflow.process(left, right);
    check(overflow.stolen_voices() == 32, "burst voice allocation");
    near(left[0], 25.6F, "burst trigger delivery");

    AudioEngine variable(48000, 4);
    variable.set_sample(constant_sample(32));
    variable.trigger(60, 1, 12);
    std::size_t elapsed{};
    for (auto size : {1U, 3U, 5U, 7U}) {
        variable.process({left.data(), size}, {right.data(), size});
        for (std::size_t i = 0; i < size; ++i)
            if (elapsed + i == 12)
                near(left[i], 0.8F, "variable callback event offset");
        elapsed += size;
    }

    Pattern invalid;
    invalid.steps[0].velocity = 2;
    check(!overflow.queue_pattern(invalid), "invalid pattern accepted");
    overflow.set_sample(constant_sample(1));
    overflow.process({left.data(), 1}, {right.data(), 1});
}

void test_live_sample_management_and_resampling() {
    using namespace neurogenic;
    std::array<float, 16> left{}, right{};
    AudioEngine live(48000, 8);
    live.set_sample_slot(SampleSlotId{0}, constant_sample(6, 0.25F));
    live.trigger(36, 1);
    live.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.2F, "initial live sample");
    check(live.publish_sample_slot(SampleSlotId{0}, constant_sample(4, 0.5F)),
          "live replacement publication");
    live.trigger(36, 1);
    live.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.6F, "old voice did not survive replacement");
    check(live.unload_sample_slot(SampleSlotId{0}), "live unload publication");
    live.trigger(36, 1);
    live.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.6F, "active voices did not survive unload");
    check(live.missing_slot_triggers() == 1,
          "unloaded slot accepted a new trigger");
    live.process({left.data(), 8}, {right.data(), 8});
    live.reclaim_sample_versions();
    check(live.reclaimed_sample_versions() >= 2,
          "retired live samples were not reclaimed");

    AudioEngine independent(48000, 8);
    independent.set_sample_slot(SampleSlotId{0}, constant_sample(8, 0.25F));
    independent.set_sample_slot(SampleSlotId{1}, constant_sample(8, 0.5F));
    independent.process({left.data(), 1}, {right.data(), 1});
    check(independent.publish_sample_slot(SampleSlotId{0},
                                          constant_sample(8, 1.0F)),
          "independent slot replacement");
    independent.trigger(36, 1);
    independent.trigger(37, 1);
    independent.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 1.2F, "independent slots changed together");

    WindowedSincConverter converter;
    Sample mono{44100, 1, std::vector<float>(441, 0.25F)};
    auto up = prepare_sample(mono, 48000, converter);
    check(up.sample_rate == 48000 && up.channels == 1 && up.frames() == 480,
          "44.1 kHz conversion duration");
    auto unchanged = prepare_sample(mono, 44100, converter);
    check(unchanged.interleaved == mono.interleaved,
          "matching-rate sample was unnecessarily changed");
    Sample stereo{96000, 2, std::vector<float>(1920)};
    for (std::size_t i = 0; i < 960; ++i) {
        stereo.interleaved[i * 2] = 0.25F;
        stereo.interleaved[i * 2 + 1] = -0.5F;
    }
    auto down = prepare_sample(stereo, 48000, converter);
    check(down.channels == 2 && down.frames() == 480,
          "96 kHz stereo conversion duration");
    near(down.interleaved[200], 0.25F, "resampler left channel");
    near(down.interleaved[201], -0.5F, "resampler right channel");
    Sample tone{44100, 1, std::vector<float>(4410)};
    constexpr double pi = 3.141592653589793;
    for (std::size_t i = 0; i < tone.frames(); ++i)
        tone.interleaved[i] =
            static_cast<float>(std::sin(2 * pi * 1000 * i / 44100.0));
    auto converted = prepare_sample(tone, 48000, converter);
    std::size_t crossings{};
    for (std::size_t i = 1; i < converted.frames(); ++i)
        if (converted.interleaved[i - 1] <= 0 && converted.interleaved[i] > 0)
            ++crossings;
    check(crossings >= 99 && crossings <= 101,
          "resampling did not preserve pitch");
    bool bad_rate = false;
    try {
        (void)prepare_sample({0, 1, {1}}, 48000, converter);
    } catch (const std::invalid_argument &) {
        bad_rate = true;
    }
    check(bad_rate, "invalid source rate accepted");

    const auto base = std::filesystem::temp_directory_path();
    const auto wav44 = base / "neurogenic-live-44100.wav";
    const auto wav96 = base / "neurogenic-live-96000.wav";
    const auto invalid = base / "neurogenic-live-invalid.wav";
    write_pcm16_wav(wav44, 44100, 1, std::vector<float>(441, 0.25F));
    write_pcm16_wav(wav96, 96000, 2, std::vector<float>(1920, 0.5F));
    {
        std::ofstream broken(invalid);
        broken << "invalid";
    }
    const auto pcm24 = base / "neurogenic-pcm24.wav";
    const auto pcm32 = base / "neurogenic-pcm32.wav";
    const auto float32 = base / "neurogenic-float32.wav";
    write_wav_payload(pcm24, 1, 24, {0, 0, 0x40});
    std::int32_t pcm32_value = 1073741824;
    std::vector<char> pcm32_bytes(4);
    std::memcpy(pcm32_bytes.data(), &pcm32_value, 4);
    write_wav_payload(pcm32, 1, 32, pcm32_bytes);
    float float_value = 0.25F;
    std::vector<char> float_bytes(4);
    std::memcpy(float_bytes.data(), &float_value, 4);
    write_wav_payload(float32, 3, 32, float_bytes);
    near(decode_wav(pcm24).interleaved[0], 0.5F, "PCM24 decoding");
    near(decode_wav(pcm32).interleaved[0], 0.5F, "PCM32 decoding");
    near(decode_wav(float32).interleaved[0], 0.25F, "float32 decoding");
    AudioEngine managed(48000, 8);
    SampleManager manager(managed, 16);
    check(manager.load(SampleSlotId{2}, wav44), "sample load command");
    manager.wait_idle();
    auto state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::pending_activation &&
              !state.has_sample && state.pending_source_path == wav44,
          "prepared sample reported active before callback");
    check(managed.map_note_to_slot(52, SampleSlotId{2}),
          "managed slot mapping");
    managed.process({left.data(), 0}, {right.data(), 0});
    state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::loaded && state.has_sample &&
              state.source_sample_rate == 44100 &&
              state.engine_sample_rate == 48000 && state.frames == 480,
          "loaded slot state");
    managed.trigger(52, 1);
    managed.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.2F, "managed resampled playback");
    check(manager.replace(SampleSlotId{2}, wav96), "replace command");
    manager.wait_idle();
    state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::pending_activation &&
              state.has_sample && state.source_path == wav44 &&
              state.pending_source_path == wav96,
          "pending replacement obscured active metadata");
    managed.trigger(52, 1);
    managed.process({left.data(), 1}, {right.data(), 1});
    check(manager.status(SampleSlotId{2}).channels == 2,
          "stereo replacement state");
    check(manager.load(SampleSlotId{2}, invalid),
          "failed load command submission");
    manager.wait_idle();
    state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::error && state.has_sample &&
              state.source_path == wav96,
          "failed load did not preserve previous sample state");
    managed.trigger(52, 1);
    managed.process({left.data(), 1}, {right.data(), 1});
    check(left[0] != 0, "failed load replaced active sample");
    check(manager.unload(SampleSlotId{2}), "unload command");
    manager.wait_idle();
    state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::pending_activation &&
              state.has_sample,
          "unload reported empty before callback");
    managed.process({left.data(), 0}, {right.data(), 0});
    state = manager.status(SampleSlotId{2});
    check(state.state == SampleSlotLoadState::empty && !state.has_sample,
          "unload state");

    AudioEngine consistent(48000, 8);
    SampleManager consistent_manager(consistent, 16);
    check(consistent_manager.load(SampleSlotId{4}, wav44), "first rapid load");
    check(consistent_manager.load(SampleSlotId{4}, wav96), "second rapid load");
    state = consistent_manager.status(SampleSlotId{4});
    check(state.revision == 2 && state.pending_source_path == wav96 &&
              !state.has_sample,
          "older command overwrote newest queued state");
    consistent_manager.wait_idle();
    state = consistent_manager.status(SampleSlotId{4});
    check(state.revision == 2 &&
              state.state == SampleSlotLoadState::pending_activation &&
              state.pending_source_path == wav96 && !state.has_sample,
          "older completion overwrote newest prepared state");
    consistent.process({left.data(), 0}, {right.data(), 0});
    state = consistent_manager.status(SampleSlotId{4});
    check(state.state == SampleSlotLoadState::loaded &&
              state.source_path == wav96 && state.source_sample_rate == 96000,
          "rapid replacement activated stale metadata");
    check(consistent_manager.unload(SampleSlotId{4}), "mixed unload");
    check(consistent_manager.load(SampleSlotId{4}, wav44), "mixed reload");
    consistent_manager.wait_idle();
    state = consistent_manager.status(SampleSlotId{4});
    check(state.state == SampleSlotLoadState::pending_activation &&
              state.has_sample && state.source_path == wav96 &&
              state.pending_source_path == wav44,
          "mixed operations corrupted pending or active state");
    consistent.process({left.data(), 0}, {right.data(), 0});
    state = consistent_manager.status(SampleSlotId{4});
    check(state.state == SampleSlotLoadState::loaded && state.has_sample &&
              state.source_path == wav44,
          "mixed unload/load final state");
    check(consistent_manager.replace(SampleSlotId{4}, invalid),
          "consistent failed replacement submission");
    consistent_manager.wait_idle();
    state = consistent_manager.status(SampleSlotId{4});
    check(state.state == SampleSlotLoadState::error && state.has_sample &&
              state.source_path == wav44 && state.pending_source_path.empty(),
          "failed replacement changed active metadata");

    AudioEngine rapid(48000, 4, 512, 8);
    for (int i = 0; i < 100; ++i) {
        check(rapid.publish_sample_slot(
                  SampleSlotId{0},
                  constant_sample(2, static_cast<float>(i + 1) / 100)),
              "rapid replacement publication");
        rapid.process({left.data(), 1}, {right.data(), 1});
        rapid.reclaim_sample_versions();
    }
    check(rapid.reclaimed_sample_versions() >= 99,
          "rapid replacements leaked versions");

    AudioEngine concurrent(48000, 8);
    SampleManager concurrent_manager(concurrent, 32);
    std::atomic<bool> rendering{true};
    std::thread audio([&] {
        std::array<float, 17> l{}, r{};
        while (rendering.load())
            concurrent.process(l, r);
    });
    for (int i = 0; i < 20; ++i)
        check(concurrent_manager.load(
                  SampleSlotId{static_cast<std::uint32_t>(i % 2)},
                  i % 2 ? wav44 : wav96),
              "concurrent sample command");
    concurrent_manager.wait_idle();
    rendering = false;
    audio.join();
    concurrent_manager.shutdown();
    const auto concurrent_states = concurrent_manager.states();
    check(concurrent_states.size() == 2 && concurrent_states[0].has_sample &&
              concurrent_states[1].has_sample,
          "concurrent command state");
    AudioEngine shutdown_engine;
    SampleManager shutdown_manager(shutdown_engine, 4);
    check(shutdown_manager.load(SampleSlotId{0}, wav44),
          "pending shutdown command");
    shutdown_manager.shutdown();
    state = shutdown_manager.status(SampleSlotId{0});
    check(state.state == SampleSlotLoadState::pending_activation &&
              !state.has_sample,
          "shutdown mislabeled unpublished sample");
    shutdown_engine.process({left.data(), 0}, {right.data(), 0});
    state = shutdown_manager.status(SampleSlotId{0});
    check(state.state == SampleSlotLoadState::loaded && state.has_sample,
          "shutdown lost pending sample publication");
    std::filesystem::remove(wav44);
    std::filesystem::remove(wav96);
    std::filesystem::remove(invalid);
    std::filesystem::remove(pcm24);
    std::filesystem::remove(pcm32);
    std::filesystem::remove(float32);
}

void test_sample_slots() {
    using namespace neurogenic;
    std::array<float, 8> left{}, right{};
    AudioEngine engine(48000, 8);
    engine.set_sample_slot(SampleSlotId{0}, constant_sample(8, 0.25F));
    engine.set_sample_slot(SampleSlotId{1}, constant_sample(8, 0.5F));
    check(engine.sample_slot_count() == 2 &&
              engine.has_sample_slot(SampleSlotId{1}),
          "sample slot installation");
    engine.trigger(36, 1);
    engine.trigger(37, 1);
    engine.process(left, right);
    near(left[0], 0.6F, "different notes did not select different samples");

    AudioEngine same(48000, 4);
    same.set_sample_slot(SampleSlotId{0}, constant_sample(4, 0.25F));
    same.trigger(36, 1);
    same.trigger(36, 1);
    same.process(left, right);
    near(left[0], 0.4F, "same-slot polyphony");

    AudioEngine missing;
    missing.set_sample_slot(SampleSlotId{0}, constant_sample(4));
    missing.trigger(37, 1);
    missing.trigger(52, 1);
    missing.process(left, right);
    near(left[0], 0, "missing slots produced audio");
    check(missing.missing_slot_triggers() == 2, "missing slot counter");
    check(!missing.map_note_to_slot(52, SampleSlotId{999}),
          "invalid slot mapping accepted");

    AudioEngine arbitrary;
    arbitrary.set_sample_slot(SampleSlotId{1000}, constant_sample(4, 0.75F));
    check(arbitrary.map_note_to_slot(52, SampleSlotId{1000}),
          "arbitrary slot mapping rejected");
    arbitrary.trigger(52, 1);
    arbitrary.process(left, right);
    near(left[0], 0.6F, "arbitrary slot did not play");

    AudioEngine stealing(48000, 2);
    stealing.set_sample_slot(SampleSlotId{0}, constant_sample(8, 0.25F));
    stealing.set_sample_slot(SampleSlotId{1}, constant_sample(8, 0.5F));
    stealing.set_sample_slot(SampleSlotId{2}, constant_sample(8, 1.0F));
    stealing.trigger(36, 1);
    stealing.trigger(37, 1);
    stealing.trigger(38, 1);
    stealing.process(left, right);
    near(left[0], 1.2F, "voice stealing across samples");
    check(stealing.stolen_voices() == 1, "cross-sample steal counter");

    AudioEngine sequenced;
    sequenced.set_sample_slot(SampleSlotId{1}, constant_sample(8, 0.5F));
    Pattern pattern;
    pattern.steps[0] = {true, 37, 1};
    pattern.revision = 1;
    check(sequenced.queue_pattern(pattern), "sequencer slot pattern queue");
    sequenced.process(left, right);
    near(left[0], 0.4F, "sequencer note did not select slot");

    AudioEngine midi_engine;
    midi_engine.set_sample_slot(SampleSlotId{1}, constant_sample(8, 0.5F));
    MidiMapper mapper;
    mapper.set_note_handler(
        [&](auto note, auto velocity) { midi_engine.trigger(note, velocity); });
    check(mapper.receive({0x90, 37, 127, 0}), "slot MIDI enqueue");
    check(mapper.dispatch_pending() == 1, "slot MIDI dispatch");
    midi_engine.process(left, right);
    near(left[0], 0.4F, "MIDI note did not select slot");

    bool invalid_sample = false;
    try {
        AudioEngine invalid;
        invalid.set_sample_slot(SampleSlotId{0}, {44100, 1, {1.0F}});
    } catch (const std::invalid_argument &) {
        invalid_sample = true;
    }
    check(invalid_sample, "mismatched sample rate accepted");
    bool invalid_id = false;
    try {
        AudioEngine invalid;
        invalid.set_sample_slot(invalid_sample_slot, constant_sample(1));
    } catch (const std::invalid_argument &) {
        invalid_id = true;
    }
    check(invalid_id, "reserved sample slot identifier accepted");
    check(!AudioEngine{}.map_note_to_slot(36, SampleSlotId{0}),
          "unloaded default slot mapped");

    const auto invalid_path =
        std::filesystem::temp_directory_path() / "neurogenic-invalid.wav";
    {
        std::ofstream invalid(invalid_path, std::ios::binary);
        invalid << "not a wave";
    }
    bool invalid_wav = false;
    try {
        (void)load_wav(invalid_path);
    } catch (const std::runtime_error &) {
        invalid_wav = true;
    }
    std::filesystem::remove(invalid_path);
    check(invalid_wav, "invalid WAV accepted");
    bool missing_wav = false;
    try {
        (void)load_wav(invalid_path);
    } catch (const std::runtime_error &) {
        missing_wav = true;
    }
    check(missing_wav, "missing WAV accepted");
}

void test_timeline_and_tracks() {
    using namespace neurogenic;
    Timeline timeline;
    timeline.transport = {500000, 7, 8};
    Track second;
    second.id = 20;
    second.pattern.length = 720;
    second.pattern.events.push_back(
        {3, 0, MusicalEventType::note, 37, 0.5F, 240, 0, 0});
    Track first;
    first.id = 10;
    first.pattern.length = 960;
    first.pattern.events.push_back(
        {2, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    first.pattern.events.push_back(
        {1, 0, MusicalEventType::note_on, 38, 0.25F, 0, 0, 0});
    first.pattern.events.push_back(
        {4, 480, MusicalEventType::note_off, 36, 0, 0, 0, 0, 2});
    first.pattern.events.push_back(
        {5, 240, MusicalEventType::parameter, 0, 0, 0, 77, 0.75F});
    timeline.tracks = {second, first};

    PlaybackPlan plan;
    std::string error;
    check(compile_timeline(timeline, plan, error), "timeline compilation");
    check(plan.tracks.size() == 2 && plan.events.size() == 6,
          "compiled timeline dimensions");
    TimelineScheduler scheduler(48000);
    scheduler.set_plan(&plan, 0);
    check(scheduler.frame_at_tick(960) == 24000, "tempo-to-frame conversion");
    std::array<TimelineScheduledEvent, 32> events{};
    auto count = scheduler.schedule(0, 1, events);
    check(count == 3, "simultaneous multi-track events");
    check(events[0].track == 10 && events[0].id == 1 && events[1].id == 2 &&
              events[2].track == 20,
          "deterministic event ordering");

    Timeline variable;
    Track odd;
    odd.id = 1;
    odd.pattern.length = 721;
    odd.pattern.events.push_back(
        {1, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    variable.tracks.push_back(odd);
    check(compile_timeline(variable, plan, error),
          "arbitrary pattern length compilation");
    scheduler = TimelineScheduler(44100);
    scheduler.set_plan(&plan, 0);
    std::uint64_t absolute{};
    std::vector<std::uint64_t> observed;
    for (std::size_t call = 0; observed.size() < 20; ++call) {
        const auto frames = static_cast<std::uint32_t>((call * 37) % 509 + 1);
        count = scheduler.schedule(absolute, frames, events);
        for (std::size_t i = 0; i < count; ++i)
            observed.push_back(absolute + events[i].frame);
        absolute += frames;
    }
    for (std::size_t i = 0; i < observed.size(); ++i) {
        const auto expected = static_cast<std::uint64_t>(
            std::llround(static_cast<long double>(i) * 721 * 44100 * 500000 /
                         (960.0L * 1000000)));
        check(observed[i] == expected,
              "timeline timing drift across variable buffers");
    }

    Timeline muted = variable;
    muted.tracks[0].muted = true;
    check(compile_timeline(muted, plan, error), "muted timeline compilation");
    scheduler = TimelineScheduler(48000);
    scheduler.set_plan(&plan, 0);
    check(scheduler.schedule(0, 48000, events) == 0,
          "muted track produced events");

    Timeline boundary;
    Track looping;
    looping.id = 9;
    looping.pattern.length = 960;
    looping.pattern.events = {
        {1, 959, MusicalEventType::note_on, 36, 1, 0, 0, 0},
        {2, 0, MusicalEventType::note_on, 37, 1, 0, 0, 0}};
    boundary.tracks.push_back(looping);
    check(compile_timeline(boundary, plan, error), "loop boundary compilation");
    scheduler = TimelineScheduler(48000);
    scheduler.set_plan(&plan, 0);
    count = scheduler.schedule(0, 24001, events);
    check(count == 3 && events[0].pitch == 37 && events[1].pitch == 36 &&
              events[2].pitch == 37 && events[2].frame == 24000,
          "loop boundary scheduling");

    Timeline invalid = boundary;
    invalid.tracks[0].pattern.events[1].id = 1;
    check(!compile_timeline(invalid, plan, error) && !error.empty(),
          "duplicate event identifier accepted");
    Timeline scalable;
    Track crowded;
    crowded.id = 1;
    crowded.pattern.length = 2000;
    for (std::size_t i = 0; i < 1000; ++i)
        crowded.pattern.events.push_back({i + 1, static_cast<MusicalTick>(i),
                                          MusicalEventType::note_on, 36, 1, 0,
                                          0, 0});
    scalable.tracks.push_back(std::move(crowded));
    check(compile_timeline(scalable, plan, error) && plan.events.size() == 1000,
          "preparation-time timeline scaling");

    AudioEngine plan_queue;
    Timeline one;
    Track one_track;
    one_track.id = 1;
    one_track.pattern.length = 960;
    one_track.pattern.events.push_back(
        {1, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    one.tracks.push_back(one_track);
    check(plan_queue.queue_timeline(one, error) &&
              plan_queue.queue_timeline(one, error) &&
              plan_queue.queue_timeline(one, error),
          "timeline queue usable capacity");
    check(!plan_queue.queue_timeline(one, error) &&
              plan_queue.dropped_timelines() == 1,
          "timeline queue overflow behavior");
    AudioEngine reclamation;
    check(reclamation.queue_timeline(one, error), "initial reclamation plan");
    std::array<float, 1> reclaim_left{}, reclaim_right{};
    reclamation.process(reclaim_left, reclaim_right);
    for (int i = 0; i < 100; ++i) {
        check(reclamation.queue_timeline(one, error),
              "rapid replacement queue");
        reclamation.process(reclaim_left, reclaim_right);
    }
    check(reclamation.queue_timeline(one, error) &&
              reclamation.reclaimed_timeline_plans() >= 99,
          "retired plans were not reclaimed on control thread");

    AudioEngine engine(48000, 8);
    engine.set_sample_slot(SampleSlotId{0}, constant_sample(30000, 0.25F));
    engine.set_sample_slot(SampleSlotId{1}, constant_sample(30000, 0.5F));
    Timeline audio;
    Track audio_track;
    audio_track.id = 1;
    audio_track.pattern.length = 960;
    audio_track.pattern.events = {
        {1, 0, MusicalEventType::note, 36, 1, 480, 0, 0},
        {2, 0, MusicalEventType::note, 37, 1, 480, 0, 0}};
    audio.tracks.push_back(audio_track);
    check(engine.queue_timeline(audio, error), "audio timeline queue");
    std::vector<float> left(12000), right(12000);
    engine.process(left, right);
    near(left[0], 0.6F, "timeline tracks did not select sample slots");
    near(left.back(), 0.6F, "note duration ended early");
    std::array<float, 2> tail_left{}, tail_right{};
    engine.process(tail_left, tail_right);
    near(tail_left[0], 0, "note-off did not stop timeline voices");

    AudioEngine durations(48000, 4);
    durations.set_sample_slot(SampleSlotId{0}, constant_sample(30000));
    Timeline overlap;
    Track overlap_track;
    overlap_track.id = 2;
    overlap_track.pattern.length = 1920;
    overlap_track.pattern.events = {
        {1, 0, MusicalEventType::note, 36, 0.25F, 480, 0, 0},
        {2, 0, MusicalEventType::note, 36, 0.5F, 720, 0, 0}};
    overlap.tracks.push_back(overlap_track);
    check(durations.queue_timeline(overlap, error),
          "overlapping duration queue");
    durations.process(left, right);
    near(left[0], 0.6F, "overlapping duration notes did not start");
    durations.process(tail_left, tail_right);
    near(tail_left[0], 0.4F,
         "one note-off stopped an unrelated overlapping note");
    std::vector<float> remainder_left(5998), remainder_right(5998);
    durations.process(remainder_left, remainder_right);
    durations.process(tail_left, tail_right);
    near(tail_left[0], 0, "second duration note did not stop");

    Pattern legacy;
    legacy.steps[3] = {true, 39, 0.75F};
    legacy.revision = 4;
    auto adapted = adapt_step_pattern(legacy, 42);
    check(adapted.tracks.size() == 1 && adapted.tracks[0].id == 42,
          "legacy pattern track adapter");
    check(adapted.tracks[0].pattern.length == ticks_per_quarter * 4 &&
              adapted.tracks[0].pattern.events[0].tick ==
                  ticks_per_quarter * 3 / 4,
          "legacy pattern timing adapter");
    check(compile_timeline(adapted, plan, error),
          "legacy pattern did not compile");
}

void test_transport_integration_and_timeline_io() {
    using namespace neurogenic;
    std::string error;
    Timeline timeline;
    Track track;
    track.id = 1;
    track.pattern.length = 3840;
    track.pattern.events.push_back(
        {1, 0, MusicalEventType::note, 36, 1, 1920, 0, 0});
    timeline.tracks.push_back(track);
    AudioEngine transport(48000, 4);
    transport.set_sample_slot(SampleSlotId{0}, constant_sample(100000));
    check(transport.queue_timeline(timeline, error),
          "transport timeline queue");
    std::array<float, 4> left{}, right{};
    transport.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.8F, "transport did not initially play");
    check(transport.frame_position() == 1 && transport.playing(),
          "initial transport state");
    transport.stop();
    transport.process({left.data(), 2}, {right.data(), 2});
    near(left[0], 0, "stop did not clean voices");
    check(transport.frame_position() == 1 && !transport.playing(),
          "stopped transport advanced");
    transport.play();
    transport.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0, "play unexpectedly restarted timeline");
    check(transport.frame_position() == 2 && transport.playing(),
          "play did not resume");
    transport.restart();
    transport.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.8F, "restart did not return to tick zero");
    check(transport.frame_position() == 1, "restart frame position");
    AudioEngine command_overflow;
    for (int i = 0; i < 20; ++i)
        command_overflow.play();
    check(command_overflow.dropped_transport_commands() == 5,
          "transport command overflow counter");

    Timeline replacement;
    Track muted;
    muted.id = 2;
    muted.muted = true;
    muted.pattern.length = 960;
    replacement.tracks.push_back(muted);
    check(transport.queue_timeline(replacement, error), "replacement queue");
    transport.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0, "timeline replacement did not clean active notes");

    Timeline crossing;
    Track short_loop;
    short_loop.id = 8;
    short_loop.pattern.length = 480;
    short_loop.pattern.events.push_back(
        {99, 0, MusicalEventType::note, 36, 1, 720, 0, 0});
    crossing.tracks.push_back(short_loop);
    PlaybackPlan crossing_plan;
    check(compile_timeline(crossing, crossing_plan, error),
          "cross-loop duration compilation");
    TimelineScheduler crossing_scheduler(48000);
    crossing_scheduler.set_plan(&crossing_plan, 0);
    std::array<TimelineScheduledEvent, 16> scheduled{};
    const auto crossing_count =
        crossing_scheduler.schedule(0, 30001, scheduled);
    check(crossing_count == 5 &&
              scheduled[0].type == MusicalEventType::note_on &&
              scheduled[1].frame == 12000 &&
              scheduled[2].type == MusicalEventType::note_off &&
              scheduled[2].frame == 18000,
          "cross-loop note scheduling");
    Timeline tiny;
    Track tiny_track;
    tiny_track.id = 1;
    tiny_track.pattern.length = 1;
    tiny_track.pattern.events.push_back(
        {1, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    tiny.tracks.push_back(tiny_track);
    PlaybackPlan tiny_plan;
    check(compile_timeline(tiny, tiny_plan, error),
          "one-tick loop compilation");
    TimelineScheduler tiny_scheduler(48000);
    tiny_scheduler.set_plan(&tiny_plan, 0);
    check(tiny_scheduler.schedule(0, 100, scheduled) == 4,
          "one-tick loop scheduling");

    AudioEngine edges(48000, 2);
    edges.set_sample_slot(SampleSlotId{0}, constant_sample(100));
    Timeline edge_timeline;
    Track edge_track;
    edge_track.id = 5;
    edge_track.pattern.length = 960;
    edge_track.pattern.events = {
        {1, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0},
        {2, 1, MusicalEventType::note_off, 36, 0, 0, 0, 0, 1}};
    edge_timeline.tracks.push_back(edge_track);
    check(edges.queue_timeline(edge_timeline, error),
          "explicit note edges queue");
    std::array<float, 26> edge_left{}, edge_right{};
    edges.process(edge_left, edge_right);
    near(edge_left[0], 0.8F, "explicit note-on missing");
    near(edge_left[25], 0, "explicit note-off did not match target");
    auto invalid_edges = edge_timeline;
    invalid_edges.tracks[0].pattern.events[1].target_event_id = 0;
    PlaybackPlan invalid_edge_plan;
    check(!compile_timeline(invalid_edges, invalid_edge_plan, error),
          "untargeted explicit note-off accepted");

    AudioEngine stolen(48000, 1);
    stolen.set_sample_slot(SampleSlotId{0}, constant_sample(100));
    Timeline stolen_timeline;
    Track stolen_track;
    stolen_track.id = 1;
    stolen_track.pattern.length = 960;
    stolen_track.pattern.events = {
        {1, 0, MusicalEventType::note, 36, 0.5F, 1, 0, 0},
        {2, 0, MusicalEventType::note, 36, 1, 2, 0, 0}};
    stolen_timeline.tracks.push_back(stolen_track);
    check(stolen.queue_timeline(stolen_timeline, error),
          "stolen voice timeline queue");
    stolen.process(edge_left, edge_right);
    near(edge_left[25], 0.8F,
         "note-off for stolen voice stopped unrelated owner");

    AudioEngine overflow(48000, 4, 2);
    overflow.set_sample_slot(SampleSlotId{0}, constant_sample(2));
    Timeline burst;
    Track burst_track;
    burst_track.id = 1;
    burst_track.pattern.length = 960;
    for (std::uint64_t id = 1; id <= 5; ++id)
        burst_track.pattern.events.push_back(
            {id, 0, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    burst.tracks.push_back(burst_track);
    check(overflow.queue_timeline(burst, error),
          "staging overflow timeline queue");
    overflow.process(left, right);
    check(overflow.dropped_timeline_events() == 1,
          "timeline staging deferral counter");
    overflow.process(left, right);
    overflow.process(left, right);
    check(overflow.dropped_timeline_events() == 2, "dense backlog diagnostics");

    AudioEngine cleanup(48000, 2, 1);
    cleanup.set_sample_slot(SampleSlotId{0}, constant_sample(100));
    Timeline cleanup_timeline;
    Track cleanup_track;
    cleanup_track.id = 1;
    cleanup_track.pattern.length = 960;
    cleanup_track.pattern.events.push_back(
        {1, 0, MusicalEventType::note, 36, 1, 1, 0, 0});
    cleanup_timeline.tracks.push_back(cleanup_track);
    check(cleanup.queue_timeline(cleanup_timeline, error),
          "cleanup overflow queue");
    cleanup.process(edge_left, edge_right);
    near(edge_left[25], 0.8F, "deferred note-off ended voice without delivery");
    cleanup.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0, "deferred note-off left a stuck voice");

    AudioEngine shared(48000, 8);
    shared.set_sample_slot(SampleSlotId{0}, constant_sample(2, 0.25F));
    shared.set_sample_slot(SampleSlotId{1}, constant_sample(2, 0.5F));
    Timeline slow;
    slow.transport.microseconds_per_quarter = 1'000'000;
    Track slow_track;
    slow_track.id = 1;
    slow_track.pattern.length = 3840;
    slow_track.pattern.events.push_back(
        {1, 960, MusicalEventType::note_on, 37, 1, 0, 0, 0});
    slow.tracks.push_back(slow_track);
    Pattern legacy;
    legacy.steps[4] = {true, 36, 1};
    legacy.revision = 1;
    check(shared.queue_pattern(legacy) && shared.queue_timeline(slow, error),
          "shared transport setup");
    std::vector<float> before_left(48000), before_right(48000);
    shared.process(before_left, before_right);
    near(before_left.back(), 0, "shared transport events arrived early");
    shared.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.6F,
         "legacy and timeline workflows do not share tempo/position");

    AudioEngine tempo_change(48000, 2);
    tempo_change.set_sample_slot(SampleSlotId{0}, constant_sample(8));
    Timeline empty;
    Track empty_track;
    empty_track.id = 1;
    empty_track.pattern.length = 960;
    empty.tracks.push_back(empty_track);
    check(tempo_change.queue_timeline(empty, error), "initial tempo plan");
    std::vector<float> advance_left(12000), advance_right(12000);
    tempo_change.process(advance_left, advance_right);
    Timeline changed;
    changed.transport.microseconds_per_quarter = 1'000'000;
    Track changed_track;
    changed_track.id = 1;
    changed_track.pattern.length = 960;
    changed_track.pattern.events.push_back(
        {1, 240, MusicalEventType::note_on, 36, 1, 0, 0, 0});
    changed.tracks.push_back(changed_track);
    check(tempo_change.queue_timeline(changed, error), "tempo transition plan");
    tempo_change.process({left.data(), 1}, {right.data(), 1});
    near(left[0], 0.8F, "tempo transition did not use authoritative position");

    Timeline large;
    Track large_track;
    large_track.id = 1;
    large_track.pattern.length = std::numeric_limits<MusicalTick>::max();
    large_track.pattern.events.push_back(
        {1, std::numeric_limits<MusicalTick>::max() - 1,
         MusicalEventType::note_on, 36, 1, 0, 0, 0});
    large.tracks.push_back(large_track);
    PlaybackPlan large_plan;
    check(compile_timeline(large, large_plan, error),
          "large-position compilation");
    TimelineScheduler large_scheduler(192000);
    large_scheduler.set_plan(&large_plan, 0);
    check(large_scheduler.frame_at_tick(
              std::numeric_limits<MusicalTick>::max() - 1) ==
              std::numeric_limits<std::uint64_t>::max(),
          "large position did not saturate safely");

    const auto path =
        std::filesystem::temp_directory_path() / "neurogenic-timeline-test.ngt";
    save_timeline(timeline, path);
    auto loaded = load_timeline(path);
    std::filesystem::remove(path);
    check(loaded.tracks.size() == 1 &&
              loaded.tracks[0].pattern.events[0].duration == 1920 &&
              loaded.tracks[0].id == 1,
          "timeline file round trip");
    const auto invalid_path = std::filesystem::temp_directory_path() /
                              "neurogenic-invalid-timeline.ngt";
    {
        std::ofstream invalid(invalid_path);
        invalid << "NEUROGENIC_TIMELINE 99\n";
    }
    bool rejected = false;
    try {
        (void)load_timeline(invalid_path);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    std::filesystem::remove(invalid_path);
    check(rejected, "unsupported timeline version accepted");
    const auto legacy_path = std::filesystem::temp_directory_path() /
                             "neurogenic-legacy-note-off.ngt";
    {
        std::ofstream legacy_file(legacy_path);
        legacy_file
            << "NEUROGENIC_TIMELINE 1\ntransport 500000 4 4\ntrack 1 960 0 "
               "\"legacy\"\nnote_on 1 10 0 36 1\nnote_off 1 11 120 36 0\n";
    }
    auto legacy_loaded = load_timeline(legacy_path);
    std::filesystem::remove(legacy_path);
    check(legacy_loaded.tracks[0].pattern.events[1].target_event_id == 10,
          "legacy note-off target resolution");
}

void test_midi_control_dispatch() {
    using namespace neurogenic;
    MidiMapper mapper;
    std::atomic<int> notes{}, actions{};
    std::thread::id action_thread;
    mapper.set_note_handler([&](auto, float) { ++notes; });
    mapper.map_button(0, 12, "test", [&] {
        action_thread = std::this_thread::get_id();
        ++actions;
    });
    check(mapper.receive({0x90, 12, 1, 0}), "MIDI enqueue");
    check(actions == 0, "action ran in MIDI callback");
    check(mapper.dispatch_pending() == 1 && notes == 1 && actions == 1,
          "deterministic MIDI dispatch");
    check(!mapper.receive({0x80, 12, 1, 0}), "MIDI note-off filtering");

    MidiMapper threaded;
    std::promise<void> delivered;
    auto ready = delivered.get_future();
    std::thread::id worker_id;
    threaded.map_button(0, 13, "threaded", [&] {
        worker_id = std::this_thread::get_id();
        delivered.set_value();
    });
    ControlDispatcher dispatcher(threaded);
    dispatcher.start();
    check(threaded.receive({0x90, 13, 127, 0}), "threaded MIDI enqueue");
    ready.get();
    dispatcher.stop();
    check(worker_id != std::this_thread::get_id(),
          "action did not run on control thread");

    MidiMapper pending;
    std::atomic<int> drained{};
    pending.map_button(0, 14, "drain", [&] { ++drained; });
    for (int i = 0; i < 20; ++i)
        check(pending.receive({0x90, 14, 1, 0}), "pending MIDI enqueue");
    ControlDispatcher shutdown(pending);
    shutdown.start();
    shutdown.stop();
    check(drained == 20, "shutdown did not drain pending events");

    MidiMapper full;
    for (int i = 0; i < 300; ++i)
        full.receive({0x90, 15, 1, 0});
    check(full.dropped_messages() == 44, "MIDI queue overflow count");
    check(full.dispatch_pending() == 256, "MIDI queue capacity");
}

void test_timeline_editing_and_generation() {
    using namespace neurogenic;
    std::string error;
    Timeline published;
    std::size_t publications{};
    TimelineService service([&](const Timeline &timeline, std::string &) {
        published = timeline;
        ++publications;
        return true;
    });
    auto edit = service.edit();
    check(edit.create_track(1, "drums", 960, error), "track creation");
    check(edit.create_track(2, "hats", 720, error),
          "independent track creation");
    const auto first_id = edit.next_event_id();
    check(edit.add_note(1, first_id, 13, 36, 0.8F, 120, error),
          "note insertion");
    check(edit.modify_note(1, first_id, 17, 38, 0.5F, 180, error),
          "note modification");
    const auto parameter_id = edit.next_event_id();
    check(edit.add_parameter(1, parameter_id, 120, 42, 0.25F, error) &&
              edit.modify_parameter(1, parameter_id, 240, 42, 0.5F, error),
          "parameter event editing");
    check(edit.add_note(2, edit.next_event_id(), 0, 42, 0.7F, 60, error),
          "second-track insertion");
    check(edit.set_track_muted(2, true, error), "track mute");
    check(edit.set_tempo_bpm(123, error) &&
              edit.set_time_signature(7, 8, error),
          "transport editing");
    edit.set_generation_seed(77);
    check(service.commit(std::move(edit),
                         TimelineCommitPolicy::next_audio_callback, error),
          "timeline transaction commit");
    auto snapshot = service.snapshot();
    check(snapshot.revision == 2 && snapshot.generation_seed == 77 &&
              snapshot.timeline.tracks.size() == 2 &&
              snapshot.timeline.tracks[0].pattern.events[0].id == first_id &&
              snapshot.timeline.tracks[1].pattern.length == 720,
          "committed timeline state");
    check(publications == 1 && published.tracks.size() == 2,
          "timeline publication callback");

    auto invalid = service.edit();
    check(!invalid.set_track_length(1, 10, error),
          "invalid shortening accepted");
    check(service.snapshot().revision == 2 &&
              service.snapshot().timeline.tracks[0].pattern.length == 960,
          "invalid edit escaped working copy");
    auto winner = service.edit(), stale = service.edit();
    check(winner.set_track_name(1, "winner", error) &&
              service.commit(std::move(winner),
                             TimelineCommitPolicy::next_audio_callback, error),
          "winning concurrent edit");
    check(stale.set_track_name(1, "stale", error) &&
              !service.commit(std::move(stale),
                              TimelineCommitPolicy::next_audio_callback,
                              error) &&
              error.find("conflict") != std::string::npos,
          "stale edit silently overwrote winner");
    auto removal = service.edit();
    check(removal.remove_event(1, first_id, error) &&
              removal.remove_event(1, parameter_id, error) &&
              removal.delete_track(2, error) &&
              service.commit(std::move(removal),
                             TimelineCommitPolicy::next_audio_callback, error),
          "event and track deletion");
    check(service.snapshot().timeline.tracks.size() == 1 &&
              service.snapshot().timeline.tracks[0].pattern.events.empty(),
          "deletion state");

    MusicalPattern pattern;
    pattern.length = 960;
    pattern.events = {{1, 13, MusicalEventType::note, 60, 0.5F, 120, 0, 0, 0},
                      {2, 240, MusicalEventType::note, 64, 1.0F, 120, 0, 0, 0}};
    check(quantize(pattern, 120, error) && pattern.events[0].tick == 0,
          "quantization boundary");
    check(rotate(pattern, 120, error) && pattern.events[0].tick == 120,
          "pattern rotation");
    check(reverse(pattern, error) && pattern.events[0].tick == 840,
          "pattern reversal");
    check(transpose(pattern, 12, error) && pattern.events[0].pitch == 72,
          "transpose");
    check(scale_velocities(pattern, 0.5, error) &&
              std::fabs(pattern.events[1].velocity - 0.5F) < 0.0001F,
          "velocity scaling");
    EventId next = 3;
    const std::array<MusicalTick, 1> offsets{240};
    check(duplicate(pattern, offsets, next, error) &&
              pattern.events.size() == 4 && pattern.events[2].id == 3,
          "event duplication IDs");
    DeterministicRandom random_a(12345), random_b(12345);
    for (int i = 0; i < 100; ++i)
        check(random_a.next_u64() == random_b.next_u64(),
              "deterministic random stream");
    auto rhythm = euclidean_rhythm(8, 3, 0, error);
    check(rhythm.size() == 8 && rhythm[0] && rhythm[3] && rhythm[6],
          "Euclidean rhythm");
    check(!euclidean_rhythm(8, 9, 0, error).size(),
          "invalid Euclidean rhythm accepted");
    DeterministicRandom human_a(9), human_b(9);
    auto first = pattern, second = pattern;
    check(humanize(first, human_a, 12, 0.1, error) &&
              humanize(second, human_b, 12, 0.1, error) &&
              first.events[0].tick == second.events[0].tick &&
              first.events[0].velocity == second.events[0].velocity,
          "deterministic humanize");

    AudioEngine engine;
    TimelineService live([&](const Timeline &timeline, std::string &message) {
        return engine.queue_timeline(timeline, message);
    });
    for (int i = 0; i < 50; ++i) {
        auto transaction = live.edit();
        if (transaction.timeline().tracks.empty())
            check(transaction.create_track(1, "live", 960, error),
                  "live track creation");
        check(transaction.set_track_name(1, "revision-" + std::to_string(i),
                                         error),
              "rapid edit");
        check(live.commit(std::move(transaction),
                          TimelineCommitPolicy::next_audio_callback, error),
              "rapid live commit");
        std::array<float, 1> left{}, right{};
        engine.process(left, right);
    }
    check(live.snapshot().revision == 51, "rapid commit revision");
}

void test_application_control_and_controllers() {
    using namespace neurogenic;
    std::string error;
    ActionRegistry actions;
    check(actions.find("transport.play.v1") &&
              actions.find("track.mute-toggle.v1") &&
              !actions.find("transport.play.v1")->description.empty(),
          "action discovery");
    check(actions.find("edit.undo.v1")->available &&
              actions.find("edit.redo.v1")->available,
          "history actions unavailable");
    check(actions.validate("track.select.v1", {{"track_id", std::int64_t{1}}},
                           error),
          "typed action validation");
    check(!actions.validate("track.select.v1",
                            {{"track_id", std::string("bad")}}, error),
          "invalid action argument accepted");
    check(actions.validate("edit.undo.v1", {}, error),
          "undo action validation");

    ParameterRegistry parameters;
    ParameterDescriptor integer{"test.integer",
                                "Integer",
                                "units",
                                ParameterType::integer,
                                std::int64_t{0},
                                std::int64_t{10},
                                std::int64_t{5},
                                std::int64_t{5},
                                2,
                                false,
                                true,
                                false,
                                {}};
    check(parameters.register_parameter(integer, error),
          "parameter registration");
    check(parameters.adjust("test.integer", 2, error) &&
              std::get<std::int64_t>(
                  parameters.get("test.integer")->current_value) == 9,
          "relative encoder adjustment");
    check(parameters.set("test.integer", std::int64_t{10}, error) &&
              parameters.reset("test.integer", error) &&
              std::get<std::int64_t>(
                  parameters.get("test.integer")->current_value) == 5,
          "parameter set/reset");
    check(!parameters.set("test.integer", std::int64_t{11}, error),
          "out-of-range parameter accepted");
    check(parameters.set_normalized("test.integer", 0.5, error) &&
              std::get<std::int64_t>(
                  parameters.get("test.integer")->current_value) == 5,
          "absolute normalized parameter mapping");
    ParameterDescriptor enumeration{"test.mode",
                                    "Mode",
                                    "",
                                    ParameterType::enumeration,
                                    std::int64_t{0},
                                    std::int64_t{2},
                                    std::int64_t{0},
                                    std::int64_t{0},
                                    1,
                                    false,
                                    false,
                                    false,
                                    {"a", "b", "c"}};
    check(parameters.register_parameter(enumeration, error),
          "enumeration registration");

    std::ifstream profile_file(std::filesystem::path(NEUROGENIC_SOURCE_DIR) /
                               "examples/controllers/generic-midi.ngc");
    std::string profile_source((std::istreambuf_iterator<char>(profile_file)),
                               {});
    ControllerProfile parsed;
    check(parse_controller_profile(profile_source, parsed, error) &&
              validate_profile(parsed, actions, error),
          error.c_str());
    check(!parse_controller_profile("controller|bad|Bad\ncontrol|x|mystery|X\n",
                                    parsed, error),
          "invalid profile parsed");
    auto full = full_performance_profile(),
         minimal = minimal_controller_profile(), midi = generic_midi_profile();
    check(validate_profile(full, actions, error) &&
              validate_profile(minimal, actions, error) &&
              validate_profile(midi, actions, error),
          "built-in controller profile validation");
    check(full.capabilities.controls.size() >= 36 &&
              minimal.capabilities.controls.size() >= 4,
          "controller capabilities missing");
    ControllerMapper midi_mapper(midi, actions, error);
    check(midi_mapper.valid(), error.c_str());
    auto absolute =
        midi_mapper.resolve({"fader.1", 0.75, 0, true, 1}, {}, error);
    check(absolute.parameter_value &&
              std::fabs(std::get<double>(*absolute.parameter_value) - 0.75) <
                  0.0001,
          "absolute fader mapping");
    auto relative =
        midi_mapper.resolve({"encoder.1", -2, 0, true, 2}, {}, error);
    check(relative.parameter_id == "transport.tempo" &&
              relative.relative_steps == -2,
          "relative encoder mapping");
    ControllerProfile shifted;
    shifted.capabilities = {
        "shift.test",
        "Shift test",
        {{"button", "Button", ControlType::momentary_button, {}}},
        false};
    ControlBinding base;
    base.control_id = "button";
    base.action_id = "transport.play.v1";
    ControlBinding layer = base;
    layer.action_id = "transport.stop.v1";
    layer.shift = true;
    shifted.bindings = {base, layer};
    ControllerMapper shifted_mapper(shifted, actions, error);
    check(shifted_mapper.resolve({"button", 1, 0, true, 1}, {}, error)
                  .action->action_id == "transport.play.v1",
          "base layer resolution");
    ModifierState shift;
    shift.shift = true;
    check(shifted_mapper.resolve({"button", 1, 0, true, 2}, shift, error)
                  .action->action_id == "transport.stop.v1",
          "shift layer resolution");
    shifted.bindings = {base};
    ControllerMapper fallback_mapper(shifted, actions, error);
    check(fallback_mapper.resolve({"button", 1, 0, true, 3}, shift, error)
                  .action->action_id == "transport.play.v1",
          "unbound shift fallback");
    shifted.fallback_to_base = false;
    ControllerMapper strict_mapper(shifted, actions, error);
    check(
        !strict_mapper.resolve({"button", 1, 0, true, 4}, shift, error).action,
        "disabled shift fallback");

    PerformanceGrid grid;
    grid.set_step_page(3);
    check(grid.press(2).type == GridEventType::toggle_step &&
              grid.press(2).index == 50,
          "paged step toggle");
    check(grid.press(2, true).type == GridEventType::select_step,
          "Select-modified step semantics");
    const auto grid_command =
        grid_application_command(grid.press(2), "grid", 9);
    check(grid_command && grid_command->action_id == "step.toggle.v1" &&
              std::get<std::int64_t>(grid_command->arguments.at("step")) == 50,
          "grid application command");
    grid.set_mode(PadMode::pad);
    check(grid.press(4).type == GridEventType::trigger_pad &&
              grid.press(4).slot == SampleSlotId{4},
          "pad semantics");
    check(!grid_application_command(grid.press(4)),
          "PAD event coupled to step editor");
    grid.set_mode(PadMode::levels);
    grid.set_selected_slot(SampleSlotId{9});
    check(grid.press(15).type == GridEventType::trigger_level &&
              grid.press(15).velocity == 127 &&
              grid.press(15).slot == SampleSlotId{9},
          "16-level semantics");
    grid.set_mode(PadMode::notes);
    grid.set_root_note(60);
    check(grid.press(15).note == 75, "16-note semantics");
    grid.set_mode(PadMode::step);
    grid.set_step_page(3);
    const auto select_command = grid_application_command(grid.press(2, true));
    check(select_command && select_command->action_id == "step.event-select.v1",
          "grid Select did not resolve event selection");

    TextEntryModel text;
    text.begin();
    for (char target : std::string("Hi")) {
        while (text.state().selected != target)
            text.select_relative(1);
        text.insert();
    }
    text.commit();
    check(text.state().committed && text.state().text == "Hi",
          "encoder-only text entry");
    text.begin("Hxy");
    text.move_cursor(-1);
    text.erase_or_cancel();
    text.direct("e");
    text.commit();
    check(text.state().text == "Hey", "text cursor/delete/direct editing");
    text.begin("discard");
    text.discard();
    check(text.state().discarded, "text discard");
    XYControl xy({true, false, false, true, false, 0.5, 2});
    auto position = xy.apply(0.0, 0.75);
    check(std::fabs(position.x - 0.75) < 0.0001 && position.y == 1.0,
          "X/Y normalization and mapping");
    xy.release();
    check(xy.value().x == 0.5 && xy.value().y == 0.5, "X/Y spring return");

    AudioEngine engine;
    Timeline initial;
    initial.tracks = {{1, "one", false, {960, {}}},
                      {2, "two", false, {720, {}}}};
    TimelineService timeline(
        [&](const Timeline &value, std::string &message) {
            return engine.queue_timeline(value, message);
        },
        initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples, 64);
    auto selection = application.execute(
        {"track.select.v1", {{"track_id", std::int64_t{2}}}, "test", 1});
    check(selection.accepted && application.snapshot().selected_track == 2,
          "track selection command");
    auto mute = application.execute({"track.mute-toggle.v1", {}, "test", 2});
    check(mute.accepted && timeline.snapshot().timeline.tracks[1].muted,
          "track mute command");
    auto mode = application.execute(
        {"pad.mode-select.v1", {{"mode", std::string("notes")}}, "test", 3});
    check(mode.accepted && application.snapshot().pad_mode == PadMode::notes,
          "mode switch");
    auto focus_before = application.snapshot().revision;
    check(application.execute({"navigation.focus-next.v1", {}, "test", 4})
                  .accepted &&
              application.snapshot().revision > focus_before,
          "focus navigation revision");
    check(application
              .execute({"modulation.xy.v1",
                        {{"position", NormalizedXY{0.25, 0.8}}},
                        "test",
                        5})
              .accepted,
          "X/Y application command");
    check(std::fabs(std::get<double>(application.parameters()
                                         .get("performance.xy.y")
                                         ->current_value) -
                    0.8) < 0.0001,
          "X/Y parameter state");
    check(application.execute({"edit.undo.v1", {}, "test", 6}).accepted &&
              !timeline.snapshot().timeline.tracks[1].muted,
          "application undo action");
    check(application.execute({"edit.redo.v1"}).accepted &&
              timeline.snapshot().timeline.tracks[1].muted,
          "application redo action");
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer)
        producers.emplace_back([&application, producer] {
            for (int i = 0; i < 10; ++i)
                check(application.submit(
                          {"step.select.v1",
                           {{"step", std::int64_t{producer * 10 + i}}},
                           "thread",
                           static_cast<std::uint64_t>(i)}),
                      "concurrent command submission");
        });
    for (auto &thread : producers)
        thread.join();
    const auto results = application.dispatch_pending();
    check(results.size() == 40 &&
              std::is_sorted(results.begin(), results.end(),
                             [](const auto &a, const auto &b) {
                                 return a.sequence < b.sequence;
                             }),
          "deterministic concurrent command ordering");
    const auto state = application.snapshot();
    check(state.revision >= 44 &&
              state.timeline_revision == timeline.snapshot().revision &&
              !state.available_actions.empty(),
          "application state revision consistency");
    ControllerMapper full_mapper(full, actions, error);
    const auto feedback = full_mapper.feedback(state, "controller.input");
    check(!feedback.empty() &&
              feedback.front().state_revision == state.revision,
          "controller feedback generation");
    const auto filtered = full_mapper.feedback(state, "led.play");
    check(std::none_of(
              filtered.begin(), filtered.end(),
              [](const auto &item) { return item.destination == "led.play"; }),
          "feedback loop suppression");
    ApplicationService bounded(engine, timeline, samples, 2);
    check(bounded.submit({"navigation.confirm.v1"}) &&
              bounded.submit({"navigation.cancel.v1"}) &&
              !bounded.submit({"navigation.focus-next.v1"}) &&
              bounded.rejected_commands() == 1,
          "application queue backpressure");
    samples.shutdown();
    check(std::any_of(feedback.begin(), feedback.end(),
                      [](const auto &item) {
                          return item.destination == "parameter.display.0" &&
                                 std::holds_alternative<double>(item.value);
                      }),
          "parameter value feedback");
}

void test_interactive_step_sequencer() {
    using namespace neurogenic;
    std::string error;
    bool publish = true;
    std::size_t publications{};
    Timeline initial;
    initial.tracks = {
        {10,
         "drums",
         false,
         {4000,
          {{7, 100, MusicalEventType::note, 50, 0.5F, 90, 0, 0, 0},
           {8, 240, MusicalEventType::parameter, 60, 1, 0, 99, 0.25F, 0}}}},
        {20,
         "other",
         false,
         {1920, {{9, 0, MusicalEventType::note, 40, 1, 120, 0, 0, 0}}}}};
    AudioEngine engine;
    TimelineService timeline(
        [&](const Timeline &value, std::string &message) {
            ++publications;
            if (!publish) {
                message = "publication rejected";
                return false;
            }
            return engine.queue_timeline(value, message);
        },
        initial);
    SampleManager samples(engine);
    ApplicationService app(engine, timeline, samples);
    auto state = app.snapshot();
    check(state.step_resolution == StepResolution::sixteenth &&
              state.step_ticks == 240 && state.tracks[0].target_note == 36 &&
              state.tracks[1].target_note == 37,
          "default resolution and per-track targets");
    const std::array<std::pair<std::string_view, MusicalTick>, 8> resolutions{
        {{"1/4", 960},
         {"1/8", 480},
         {"1/16", 240},
         {"1/32", 120},
         {"1/4T", 640},
         {"1/8T", 320},
         {"1/16T", 160},
         {"1/32T", 80}}};
    for (const auto &[name, ticks] : resolutions) {
        const auto parsed = parse_step_resolution(name);
        check(parsed && step_resolution_ticks(*parsed) == ticks &&
                  name == step_resolution_name(*parsed),
              "step resolution conversion");
    }
    const auto before_revision = timeline.snapshot().revision;
    check(
        app.execute({"step.select.v1", {{"step", std::int64_t{5}}}}).accepted &&
            timeline.snapshot().revision == before_revision,
        "selection modified timeline");
    check(app.execute({"step.toggle.v1",
                       {{"step", std::int64_t{4}},
                        {"velocity", 0.7},
                        {"duration_ticks", std::int64_t{180}}}})
              .accepted,
          "step insertion");
    auto edited = timeline.snapshot();
    const auto inserted = std::find_if(
        edited.timeline.tracks[0].pattern.events.begin(),
        edited.timeline.tracks[0].pattern.events.end(), [](const auto &event) {
            return event.tick == 960 && event.pitch == 36;
        });
    check(inserted != edited.timeline.tracks[0].pattern.events.end() &&
              inserted->velocity == 0.7F && inserted->duration == 180,
          "step note values");
    check(edited.timeline.tracks[0].pattern.events[0].id == 7 &&
              edited.timeline.tracks[1].pattern.events[0].id == 9,
          "unrelated stable event IDs");
    check(app.execute({"step.toggle.v1", {{"step", std::int64_t{4}}}}).accepted,
          "step removal");
    edited = timeline.snapshot();
    check(std::none_of(edited.timeline.tracks[0].pattern.events.begin(),
                       edited.timeline.tracks[0].pattern.events.end(),
                       [](const auto &event) {
                           return event.tick == 960 && event.pitch == 36;
                       }),
          "repeated toggle removal");
    check(
        app.execute({"track.note-target-set.v1", {{"note", std::int64_t{42}}}})
                .accepted &&
            app.snapshot().tracks[0].target_note == 42,
        "note target selection");
    check(app.execute({"step.resolution-set.v1",
                       {{"resolution", std::string("1/8T")}}})
                  .accepted &&
              app.snapshot().step_ticks == 320,
          "triplet resolution");
    check(
        app.execute({"step.toggle.v1", {{"step", std::int64_t{12}}}}).accepted,
        "partial final grid position");
    check(
        !app.execute({"step.toggle.v1", {{"step", std::int64_t{13}}}}).accepted,
        "out-of-pattern step accepted");
    check(app.execute({"step.resolution-set.v1",
                       {{"resolution", std::string("1/32")}}})
              .accepted,
          "32nd resolution");
    check(app.execute({"step.page-next.v1"}).accepted &&
              app.snapshot().step_page == 1,
          "step page navigation");
    check(app.execute({"track.select.v1", {{"track_id", std::int64_t{20}}}})
                  .accepted &&
              app.snapshot().tracks[1].target_note == 37,
          "independent track target");
    check(
        !app.execute({"step.toggle.v1", {{"step", std::int64_t{16}}}}).accepted,
        "independent track length bound");
    publish = false;
    const auto failed = timeline.snapshot();
    check(
        !app.execute({"step.toggle.v1", {{"step", std::int64_t{1}}}}).accepted,
        "publication failure accepted");
    check(timeline.snapshot().revision == failed.revision &&
              timeline.snapshot().timeline.tracks[1].pattern.events.size() ==
                  failed.timeline.tracks[1].pattern.events.size(),
          "publication failure changed timeline");
    publish = true;
    {
        std::array<float, 37> left{}, right{};
        for (int i = 0; i < 8; ++i)
            engine.process(left, right);
    }
    engine.play();
    for (int step = 0; step < 8; ++step) {
        check(app.execute({"step.toggle.v1", {{"step", std::int64_t{step}}}})
                  .accepted,
              "rapid live step edit");
        std::array<float, 37> left{}, right{};
        engine.process(left, right);
    }
    check(publications >= 11 && engine.playing(),
          "live editing interrupted transport");
    ActionRegistry actions;
    ControllerMapper minimal(minimal_controller_profile(), actions, error);
    auto enter = minimal.resolve({"enter", 1, 0, true, 1}, {}, error);
    check(enter.action && app.execute(*enter.action).accepted &&
              app.snapshot().focused_element == "tracks",
          "encoder workflow track focus");
    auto turn = minimal.resolve({"data", -1, 0, true, 2}, {}, error);
    check(turn.action &&
              std::get<double>(turn.action->arguments.at("steps")) == -1 &&
              app.execute(*turn.action).accepted,
          "relative navigation mapping");
    enter = minimal.resolve({"enter", 1, 0, true, 3}, {}, error);
    check(enter.action && app.execute(*enter.action).accepted &&
              app.snapshot().focused_element == "steps",
          "encoder workflow step focus");
    turn = minimal.resolve({"data", 1, 0, true, 4}, {}, error);
    check(turn.action && app.execute(*turn.action).accepted &&
              app.snapshot().selected_step == 1,
          "encoder-only step navigation");
    enter = minimal.resolve({"enter", 1, 0, true, 5}, {}, error);
    check(enter.action && app.execute(*enter.action).accepted &&
              app.snapshot().focused_element == "step-properties" &&
              app.snapshot().selected_event,
          "encoder-only property entry");
    turn = minimal.resolve({"data", 1, 0, true, 6}, {}, error);
    check(turn.action && app.execute(*turn.action).accepted,
          "encoder-only property adjustment");
    auto back = minimal.resolve({"back", 1, 0, true, 7}, {}, error);
    check(back.action && app.execute(*back.action).accepted &&
              app.snapshot().focused_element == "steps",
          "property editor Back");
    check(app.execute(*back.action).accepted &&
              app.snapshot().focused_element == "tracks",
          "encoder workflow Back");
    samples.shutdown();
}

void test_undo_redo_and_step_properties() {
    using namespace neurogenic;
    std::string error;
    bool publish = true;
    std::size_t publications{};
    Timeline initial;
    initial.tracks = {
        {1,
         "drums",
         false,
         {1920,
          {{1, 0, MusicalEventType::note, 36, 0.5F, 120, 0, 0, 0},
           {2, 20, MusicalEventType::note, 40, 0.75F, 240, 0, 0, 0},
           {3, 0, MusicalEventType::parameter, 0, 0, 0, 7, 0.25F, 0}}}}};
    AudioEngine engine;
    TimelineService timeline(
        [&](const Timeline &, std::string &message) {
            ++publications;
            if (!publish) {
                message = "publication rejected";
                return false;
            }
            return true;
        },
        initial, {}, 32);
    SampleManager samples(engine);
    ApplicationService app(engine, timeline, samples);
    check(!app.snapshot().can_undo && !app.snapshot().can_redo &&
              app.snapshot().history_capacity == 32,
          "initial history state");
    check(app.execute({"step.toggle.v1", {{"step", std::int64_t{1}}}})
              .accepted,
          "step insertion for history");
    check(app.execute({"edit.undo.v1"}).accepted, "undo step insertion");
    const auto after_insert_undo = timeline.snapshot();
    check(std::none_of(after_insert_undo.timeline.tracks[0].pattern.events.begin(),
                       after_insert_undo.timeline.tracks[0].pattern.events.end(),
                       [](const MusicalEvent &event) {
                           return event.type == MusicalEventType::note &&
                                  event.tick == 240 && event.pitch == 36;
                       }),
          "undo step insertion state");
    check(app.execute({"edit.redo.v1"}).accepted,
          "redo step insertion");
    check(app.execute({"step.toggle.v1", {{"step", std::int64_t{1}}}})
              .accepted &&
              app.execute({"edit.undo.v1"}).accepted,
          "undo step removal");
    check(app.execute({"edit.redo.v1"}).accepted,
          "redo step removal");
    check(app.execute(
                 {"step.event-select.v1",
                  {{"step", std::int64_t{0}}, {"event_id", std::int64_t{2}}}})
              .accepted,
          "specific event selection");
    auto state = app.snapshot();
    check(state.selected_event && state.selected_event->event_id == 2 &&
              state.selected_event->events_at_step == 2 &&
              state.selected_event->index_at_step == 1,
          "multiple note selection metadata");
    check(app.execute({"step.velocity-set.v1", {{"velocity", 0.25}}}).accepted,
          "velocity edit");
    auto changed = timeline.snapshot();
    check(changed.timeline.tracks[0].pattern.events[1].id == 2 &&
              changed.timeline.tracks[0].pattern.events[1].velocity == 0.25F &&
              changed.timeline.tracks[0].pattern.events[0].id == 1 &&
              changed.timeline.tracks[0].pattern.events[2].type ==
                  MusicalEventType::parameter,
          "velocity edit identity/isolation");
    check(app.execute({"step.duration-set.v1",
                       {{"duration_ticks", std::int64_t{600}}}})
              .accepted,
          "duration edit");
    check(app.execute({"step.pitch-set.v1", {{"pitch", std::int64_t{42}}}})
              .accepted,
          "pitch edit");
    check(app.execute({"step.timing-offset-set.v1",
                       {{"offset_ticks", std::int64_t{30}}}})
              .accepted,
          "timing offset edit");
    state = app.snapshot();
    check(state.selected_event && state.selected_event->event_id == 2 &&
              state.selected_event->duration == 600 &&
              state.selected_event->pitch == 42 &&
              state.selected_event->timing_offset == 30,
          "step properties not reflected");
    check(!app.execute({"step.timing-offset-set.v1",
                        {{"offset_ticks", std::int64_t{240}}}})
               .accepted,
          "invalid timing offset accepted");
    check(app.execute({"step.property-reset.v1",
                       {{"property", std::string("timing")}}})
                  .accepted &&
              app.snapshot().selected_event->timing_offset == 0,
          "timing reset");
    const auto before_group = timeline.snapshot();
    const auto depth_before = timeline.history_status().undo_depth;
    check(app.execute({"edit.group-begin.v1"}).accepted, "group begin");
    check(app.execute({"step.velocity-set.v1", {{"velocity", 0.9}}}).accepted &&
              app.execute({"step.duration-set.v1",
                           {{"duration_ticks", std::int64_t{300}}}})
                  .accepted,
          "grouped property edits");
    check(timeline.snapshot().revision == before_group.revision,
          "group leaked partial edits");
    check(app.execute({"edit.group-commit.v1"}).accepted &&
              timeline.history_status().undo_depth == depth_before + 1,
          "group did not create one history entry");
    check(app.execute({"edit.undo.v1"}).accepted, "group undo");
    changed = timeline.snapshot();
    check(changed.timeline.tracks[0].pattern.events[1].velocity ==
                  before_group.timeline.tracks[0].pattern.events[1].velocity &&
              changed.timeline.tracks[0].pattern.events[1].duration ==
                  before_group.timeline.tracks[0].pattern.events[1].duration,
          "group undo state");
    check(
        app.execute({"edit.redo.v1"}).accepted &&
            timeline.snapshot().timeline.tracks[0].pattern.events[1].velocity ==
                0.9F,
        "group redo");
    check(app.execute({"edit.undo.v1"}).accepted &&
              app.execute({"step.event-select.v1",
                           {{"step", std::int64_t{0}},
                            {"event_id", std::int64_t{2}}}})
                  .accepted &&
              app.execute({"step.pitch-set.v1", {{"pitch", std::int64_t{44}}}})
                  .accepted &&
              !app.snapshot().can_redo,
          "new edit did not invalidate redo");
    const auto before_failure = timeline.snapshot();
    const auto history_before = timeline.history_status();
    publish = false;
    check(!app.execute({"step.velocity-set.v1", {{"velocity", 0.1}}}).accepted,
          "failed publication accepted");
    check(
        timeline.snapshot().revision == before_failure.revision &&
            timeline.history_status().undo_depth == history_before.undo_depth &&
            timeline.snapshot().timeline.tracks[0].pattern.events[1].velocity ==
                before_failure.timeline.tracks[0].pattern.events[1].velocity,
        "failed publication changed state/history");
    publish = true;
    auto stale = timeline.edit();
    check(app.execute({"step.duration-set.v1",
                       {{"duration_ticks", std::int64_t{480}}}})
              .accepted,
          "conflict winner");
    check(stale.set_track_name(1, "stale", error) &&
              !timeline.commit(std::move(stale),
                               TimelineCommitPolicy::next_audio_callback,
                               error) &&
              error.find("conflict") != std::string::npos,
          "revision conflict accepted");
    engine.play();
    check(app.execute({"edit.undo.v1"}).accepted && engine.playing(),
          "undo interrupted playback");
    std::array<float, 37> left{}, right{};
    engine.process(left, right);
    check(publications > 0, "history publication not exercised");
    samples.shutdown();

    TimelineService bounded(
        [](const Timeline &, std::string &) { return true; }, initial, {}, 2);
    for (int i = 0; i < 4; ++i) {
        auto edit = bounded.edit();
        check(edit.set_track_name(1, "v" + std::to_string(i), error) &&
                  bounded.commit(std::move(edit),
                                 TimelineCommitPolicy::next_audio_callback,
                                 error),
              "bounded history commit");
    }
    check(bounded.history_status().undo_depth == 2 &&
              bounded.undo(TimelineCommitPolicy::next_audio_callback, error) &&
              bounded.undo(TimelineCommitPolicy::next_audio_callback, error) &&
              !bounded.undo(TimelineCommitPolicy::next_audio_callback, error),
          "history capacity exhaustion");
}

void test_gui_model() {
    using namespace neurogenic;
    AudioEngine engine;
    Timeline initial;
    initial.tracks = {
        {1,
         "kick",
         false,
         {3840, {{1, 0, MusicalEventType::note, 36, 1, 120, 0, 0, 0}}}},
        {2, "hat", true, {1920, {}}}};
    TimelineService timeline([](const Timeline &, std::string &) { return true; },
                             initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    GuiModel gui(application);
    const auto small = gui.layout(1024, 600), large = gui.layout(1920, 1080);
    check(small.scale == 1 && small.content.w > 0 && small.grid.size() == 16,
          "1024x600 GUI layout");
    check(large.scale > 1.0F && large.content.x + large.content.w <= 1920,
          "scaled GUI layout");
    const auto &first = gui.frame(1024, 600);
    check(first.state_revision == application.snapshot().revision &&
              !first.commands.empty(),
          "GUI state synchronization");
    const auto first_content_width = first.layout.content.w;
    const auto &resized = gui.frame(1600, 900);
    check(resized.viewport_width == 1600 &&
              resized.layout.content.w > first_content_width,
          "GUI resize invalidation and expansion");
    check(gui.input({GuiInput::Type::key_down, GuiKey::space}, 1024, 600),
          "keyboard transport input");
    (void)application.dispatch_pending();
    check(application.snapshot().playing, "GUI semantic transport command");
    check(gui.input({GuiInput::Type::key_down, GuiKey::number, 5}, 1024, 600),
          "number step selection");
    (void)application.dispatch_pending();
    check(application.snapshot().selected_step == 4, "GUI step selection");
    const auto before_pointer = timeline.snapshot().revision;
    GuiInput step_click;
    step_click.type = GuiInput::Type::pointer_down;
    step_click.x = small.grid[1].x + 2;
    step_click.y = small.grid[1].y + 2;
    check(gui.input(step_click, 1024, 600) &&
              timeline.snapshot().revision == before_pointer + 1,
          "pointer step toggle");
    check(gui.input({GuiInput::Type::key_down, GuiKey::right}, 1024, 600) &&
              gui.input({GuiInput::Type::key_down, GuiKey::enter}, 1024, 600),
          "keyboard step navigation and toggle");
    check(gui.input({GuiInput::Type::key_down, GuiKey::shift}, 1024, 600) &&
              gui.input({GuiInput::Type::key_down, GuiKey::number, 1}, 1024,
                        600) &&
              gui.input({GuiInput::Type::key_up, GuiKey::shift}, 1024, 600) &&
              application.snapshot().selected_event,
          "GUI Select+step event selection");
    GuiInput property_click;
    property_click.type = GuiInput::Type::pointer_down;
    property_click.x = small.parameters.x + 20;
    property_click.y = small.parameters.y + 104;
    check(gui.input(property_click, 1024, 600) &&
              application.snapshot().focused_element == "step-properties",
          "GUI property inspector selection");
    const auto velocity_before =
        application.snapshot().selected_event->velocity;
    check(gui.input({GuiInput::Type::key_down, GuiKey::down}, 1024, 600) &&
              application.snapshot().selected_event->velocity < velocity_before,
          "GUI property adjustment");
    GuiInput undo_click;
    undo_click.type = GuiInput::Type::pointer_down;
    undo_click.x = small.parameters.x + 20;
    undo_click.y = small.parameters.y + 55;
    check(gui.input(undo_click, 1024, 600) && application.snapshot().can_redo,
          "GUI undo interaction");
    GuiInput click;
    click.type = GuiInput::Type::pointer_down;
    click.x = small.tabs[3].x + 2;
    click.y = small.tabs[3].y + 2;
    check(gui.input(click, 1024, 600) && gui.view() == GuiView::xy,
          "mouse view selection");
    GuiInput touch;
    touch.type = GuiInput::Type::touch_down;
    touch.x = small.content.x + 240;
    touch.y = small.content.y + 240;
    touch.timestamp_ms = 20;
    check(gui.input(touch, 1024, 600), "touch-equivalent X/Y input");
    (void)application.dispatch_pending();
    check(std::get<double>(
              application.parameters().get("performance.xy.x")->current_value) >
              0,
          "X/Y application integration");
    std::string error;
    const auto folder =
        std::filesystem::temp_directory_path() / "neurogenic-gui-browser";
    std::filesystem::create_directories(folder);
    write_pcm16_wav(folder / "sample.wav", 48000, 1, {0, 0});
    {
        std::ofstream ignored(folder / "ignored.txt");
        ignored << "x";
    }
    const auto entries = gui.browse(folder, error);
    check(error.empty() && entries.size() == 1 &&
              entries[0].path().extension() == ".wav",
          "sample browser filtering");
    std::filesystem::remove_all(folder);
    samples.shutdown();
}

void test_lua_and_offline() {
    using namespace neurogenic;
    bool submitted = false;
    Timeline lua_published;
    TimelineService timeline([&](const Timeline &value, std::string &) {
        lua_published = value;
        return true;
    });
    ScriptRuntime lua(
        [&](Pattern pattern) {
            submitted = pattern.steps[0].enabled && pattern.steps[0].note == 64;
            return true;
        },
        [] { return 123ULL; }, &timeline);
    std::string error;
#ifdef NEUROGENIC_HAS_LUA
    check(lua.execute("local "
                      "p=neurogenic.v1.pattern.new();p:set_step(1,true,64,0.75)"
                      ";neurogenic.v1.pattern.queue(p)",
                      error) &&
              submitted,
          "Lua execution");
    check(!lua.execute("this is not lua", error) && !error.empty(),
          "invalid Lua accepted");
    check(!lua.execute(
              "local p=neurogenic.v1.pattern.new();p:set_step(1,true,200,1)",
              error),
          "invalid Lua pattern accepted");
    const auto script_path =
        std::filesystem::temp_directory_path() / "neurogenic-script-test.lua";
    {
        std::ofstream script(script_path);
        script << "local p=neurogenic.v1.pattern.new(); "
                  "p:set_step(1,true,64,1); neurogenic.v1.pattern.queue(p)\n";
    }
    submitted = false;
    check(lua.execute_file(script_path, error) && submitted,
          "Lua file execution");
    std::filesystem::remove(script_path);
    check(
        lua.execute("local p=neurogenic.v1.timeline.current(); local "
                    "a=p:create_track(1,'kick',3840); "
                    "a:add_note{tick=0,pitch=36,velocity=1,duration=120}; "
                    "local b=p:create_track(2,'hat',720); "
                    "b:generate_euclidean(6,3,0,42,0.7,60); p:set_tempo(110); "
                    "p:set_time_signature(4,4); p:set_seed(123); p:commit()",
                    error),
        error.c_str());
    auto timeline_state = timeline.snapshot();
    check(timeline_state.revision == 2 &&
              timeline_state.generation_seed == 123 &&
              timeline_state.timeline.tracks.size() == 2 &&
              timeline_state.timeline.tracks[1].pattern.length == 720,
          "Lua multitrack generation");
    const auto preserved = timeline_state.timeline.tracks[1].pattern.events;
    check(
        lua.execute("local p=neurogenic.v1.timeline.current(); local "
                    "t=p:track(1); t:transpose(2); t:quantize(120); p:commit()",
                    error),
        error.c_str());
    timeline_state = timeline.snapshot();
    check(timeline_state.timeline.tracks[0].pattern.events[0].pitch == 38 &&
              timeline_state.timeline.tracks[1].pattern.events.size() ==
                  preserved.size() &&
              timeline_state.timeline.tracks[1].pattern.events[0].pitch ==
                  preserved[0].pitch,
          "Lua mutation did not preserve unchanged track");
    const auto before_error = timeline.snapshot();
    check(!lua.execute("local p=neurogenic.v1.timeline.current(); "
                       "p:track(1):set_name('must rollback'); p:commit(); "
                       "error('after commit')",
                       error),
          "Lua error after staged commit accepted");
    check(timeline.snapshot().revision == before_error.revision &&
              timeline.snapshot().timeline.tracks[0].name ==
                  before_error.timeline.tracks[0].name,
          "Lua error failed to roll back transaction");
    check(!lua.execute(
              "local p=neurogenic.v1.timeline.current(); "
              "p:track(1):add_note{tick=-1,pitch=36,duration=10}; p:commit()",
              error),
          "invalid Lua timeline edit accepted");
    auto conflicting = timeline.edit();
    check(conflicting.set_track_name(1, "external", error) &&
              timeline.commit(std::move(conflicting),
                              TimelineCommitPolicy::next_audio_callback, error),
          "external edit before Lua conflict test");
    check(!lua.execute("local first=neurogenic.v1.timeline.current(); local "
                       "second=neurogenic.v1.timeline.current(); "
                       "first:track(1):set_name('first'); first:commit(); "
                       "second:track(1):set_name('stale'); second:commit()",
                       error),
          "multiple Lua commits accepted");
    check(timeline.snapshot().timeline.tracks[0].name == "external",
          "failed Lua execution published a partial edit");
    check(!lua.execute("while true do end", error) &&
              error.find("instruction limit") != std::string::npos,
          "Lua instruction limit");
    check(
        lua.execute(
            "local a=neurogenic.random(99); local b=neurogenic.random(99); for "
            "i=1,100 do assert(a:integer(1,100000)==b:integer(1,100000)) end",
            error),
        "Lua deterministic random stream");
    auto run_examples = [](const std::vector<std::string> &scripts) {
        TimelineService example_service(
            [](const Timeline &, std::string &) { return true; });
        ScriptRuntime runtime([](Pattern) { return true; }, [] { return 0ULL; },
                              &example_service);
        std::string example_error;
        for (const auto &script : scripts)
            check(runtime.execute_file(
                      std::filesystem::path(NEUROGENIC_SOURCE_DIR) /
                          "examples" / script,
                      example_error),
                  example_error.c_str());
        return example_service.snapshot();
    };
    const auto drum_example =
        run_examples({"timeline_drums.lua", "timeline_mutate.lua"});
    check(drum_example.timeline.tracks.size() == 4 &&
              drum_example.revision == 3,
          "drum and mutation examples");
    const auto poly_example = run_examples({"timeline_polyrhythm.lua"});
    check(poly_example.timeline.tracks.size() == 3 &&
              poly_example.timeline.tracks[0].pattern.length !=
                  poly_example.timeline.tracks[1].pattern.length,
          "polyrhythm example");
    const auto variation_example = run_examples({"timeline_variation.lua"});
    check(variation_example.generation_seed == 20261119 &&
              variation_example.timeline.tracks.size() == 1,
          "variation example");
#else
    check(!lua.execute("return 1", error) && !error.empty(),
          "Lua unexpectedly available");
#endif
    const auto path =
        std::filesystem::temp_directory_path() / "neurogenic-offline-test.wav";
    AudioEngine offline;
    offline.set_sample(constant_sample(16));
    offline.trigger(60, 1);
    offline.render_wav(path, 64);
    std::ifstream file(path, std::ios::binary);
    std::array<char, 4> riff{};
    file.read(riff.data(), 4);
    check(std::string(riff.data(), 4) == "RIFF", "offline WAV");
    std::filesystem::remove(path);
}
} // namespace

int main() {
    try {
        test_queue_and_scheduler();
        test_polyphony_and_events();
        test_sample_slots();
        test_live_sample_management_and_resampling();
        test_timeline_and_tracks();
        test_transport_integration_and_timeline_io();
        test_midi_control_dispatch();
        test_timeline_editing_and_generation();
        test_application_control_and_controllers();
        test_interactive_step_sequencer();
        test_undo_redo_and_step_properties();
        test_gui_model();
        test_lua_and_offline();
        std::cout << "all tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return 1;
    }
}
