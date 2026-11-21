#include "neurogenic/audio/live_loop.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    using namespace neurogenic;
    AudioEngine engine(48'000, 8);
    SampleManager samples(engine);
    Timeline timeline;
    timeline.tracks.push_back({1, "Loop", false, {960, {}},
                               std::string(sample_instrument_id)});
    timeline.arrangement.playback_mode = TimelinePlaybackMode::song;
    timeline.arrangement.song_start = 0;
    timeline.arrangement.song_end = 20'000;
    TimelineService timelines(
        [&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        }, timeline);
    RecordingService recorder(48'000, 1024);
    engine.set_recording_service(&recorder);
    LiveLoopService loops(engine, recorder, samples, timelines);

    LiveLoopConfiguration configuration;
    configuration.destination_track = 1;
    configuration.destination_slot = SampleSlotId{3};
    configuration.source = RecordingSource::master_post_effects;
    configuration.quantization = LiveLoopQuantization::immediate;
    configuration.length_ticks = 4; // 100 frames at 120 BPM.
    std::string error;
    const auto id = loops.create(configuration, error);
    check(id && loops.arm(id, LiveLoopMode::capture, error) &&
              loops.start(id, error),
          "immediate live-loop capture did not start");
    std::array<float, 100> left{}, right{};
    left.fill(.25F);
    right.fill(.25F);
    recorder.capture_internal(RecordingSource::master_post_effects, left, right,
                              engine.callback_frame_position());
    loops.update();
    check(loops.find(id)->state == LiveLoopState::finalizing,
          "completed loop did not enter finalization");
    std::array<float, 1> ack_left{}, ack_right{};
    engine.process(ack_left, ack_right);
    loops.update();
    const auto first = loops.find(id);
    check(first && first->state == LiveLoopState::playing && first->clip_id &&
              first->actual_stop_frame - first->actual_start_frame == 100,
          "first pass did not publish and create a clip");
    const auto snapshot = timelines.snapshot().timeline;
    check(snapshot.arrangement.audio_clips.size() == 1 &&
              snapshot.arrangement.audio_clips.front().loop &&
              snapshot.arrangement.audio_clips.front().duration == 4,
          "live-loop clip metadata is incorrect");

    configuration.overdub_feedback = .5F;
    check(loops.configure(id, configuration, error) &&
              loops.arm(id, LiveLoopMode::overdub, error) &&
              loops.start(id, error),
          "overdub did not start");
    left.fill(.5F);
    right.fill(.5F);
    const auto overdub_start = loops.find(id)->requested_start_frame;
    recorder.capture_internal(RecordingSource::master_post_effects, left, right,
                              overdub_start);
    loops.update();
    engine.process(ack_left, ack_right);
    loops.update();
    const auto overdubbed = samples.active_sample(SampleSlotId{3});
    check(overdubbed && std::abs(overdubbed->interleaved.front() - .625F) < 1e-6F &&
              timelines.snapshot().timeline.arrangement.audio_clips.front().id ==
                  first->clip_id,
          "overdub was not phase-aligned immutable replacement");

    check(loops.arm(id, LiveLoopMode::replace, error) && loops.start(id, error),
          "replace pass did not start");
    left.fill(.1F);
    right.fill(.1F);
    const auto replace_start = loops.find(id)->requested_start_frame;
    recorder.capture_internal(RecordingSource::master_post_effects, left, right,
                              replace_start);
    loops.update();
    engine.process(ack_left, ack_right);
    loops.update();
    const auto replaced = samples.active_sample(SampleSlotId{3});
    check(replaced && std::abs(replaced->interleaved.front() - .1F) < 1e-6F,
          "replace pass did not preserve clip identity with new audio");

    engine.restart();
    engine.process(ack_left, ack_right);
    LiveLoopConfiguration beat = configuration;
    beat.destination_slot = SampleSlotId{4};
    beat.quantization = LiveLoopQuantization::beat;
    const auto beat_id = loops.create(beat, error);
    check(beat_id && loops.arm(beat_id, LiveLoopMode::capture, error),
          "beat-quantized loop did not arm");
    const auto beat_status = loops.find(beat_id);
    check(beat_status->requested_start_frame == engine.callback_frame_position() +
              24'000 && beat_status->requested_start_tick == 960,
          "beat boundary was not derived from authoritative transport");
    check(loops.cancel(beat_id, error), "armed loop cancellation failed");

    LiveLoopConfiguration unsafe = configuration;
    unsafe.source = RecordingSource::track_channel;
    unsafe.source_id = 1;
    unsafe.destination_slot = SampleSlotId{5};
    const auto unsafe_id = loops.create(unsafe, error);
    check(unsafe_id && !loops.arm(unsafe_id, LiveLoopMode::capture, error) &&
              error.find("itself") != std::string::npos,
          "same-track feedback capture was not rejected");

    for (std::size_t index = loops.sessions().size();
         index < LiveLoopService::maximum_sessions; ++index) {
        unsafe.destination_slot = SampleSlotId{
            static_cast<std::uint32_t>(10 + index)};
        check(loops.create(unsafe, error) != 0,
              "bounded live-loop session creation failed early");
    }
    check(loops.create(unsafe, error) == 0 &&
              error.find("capacity") != std::string::npos,
          "live-loop session capacity exhaustion was not reported");

    engine.set_recording_service(nullptr);
    std::cout << "neurogenic.live-loop-tests.v1 passed\n";
    return 0;
} catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
}
