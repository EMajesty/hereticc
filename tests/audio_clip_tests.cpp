#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/timeline/arrangement.hpp"
#include "neurogenic/timeline/io.hpp"
#include "neurogenic/timeline/playback.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
neurogenic::Timeline timeline_with_clip(bool loop = false) {
    using namespace neurogenic;
    Timeline timeline;
    timeline.tracks.push_back({1, "Audio", false, {960, {}},
                               std::string(sample_instrument_id)});
    timeline.arrangement.playback_mode = TimelinePlaybackMode::song;
    timeline.arrangement.song_start = 0;
    timeline.arrangement.song_end = 12;
    AudioClip clip;
    clip.id = 91;
    clip.track = 1;
    clip.sample_slot = 0;
    clip.start = 2;
    clip.duration = 4;
    clip.source_start_frame = 0;
    clip.source_end_frame = loop ? 10 : 100;
    clip.loop = loop;
    clip.loop_start_frame = 0;
    clip.loop_end_frame = clip.source_end_frame;
    timeline.arrangement.audio_clips.push_back(clip);
    return timeline;
}
std::vector<float> render(bool partitioned, bool loop = false,
                          std::uint64_t seek = 0) {
    using namespace neurogenic;
    AudioEngine engine(48000, 8);
    SampleManager samples(engine);
    auto pcm = std::make_shared<const Sample>(
        Sample{48000, 1, std::vector<float>(loop ? 10 : 100, .5F)});
    check(samples.publish_prepared(SampleSlotId{0}, pcm, "clip.wav"),
          "sample publication failed");
    std::array<float, 1> ack_l{}, ack_r{};
    engine.stop();
    engine.process(ack_l, ack_r);
    std::string error;
    check(engine.queue_timeline(timeline_with_clip(loop), error),
          "audio clip timeline publication failed");
    engine.restart();
    if (seek) {
        engine.process(ack_l, ack_r);
        check(engine.seek_frame(seek), "audio clip seek queue rejected");
    }
    std::vector<float> output(300), right(300);
    if (!partitioned) {
        engine.process(output, right);
    } else {
        std::size_t cursor{};
        for (const auto count : {17U, 31U, 7U, 83U, 59U, 103U}) {
            engine.process({output.data() + cursor, count},
                           {right.data() + cursor, count});
            cursor += count;
        }
    }
    return output;
}
} // namespace

int main() try {
    using namespace neurogenic;
    auto timeline = timeline_with_clip();
    std::string error;
    check(validate_arrangement(timeline, error), "valid audio clip rejected");
    PlaybackPlan plan;
    check(compile_timeline(timeline, plan, error) && plan.audio_clips.size() == 1,
          "audio clip did not compile into playback plan");
    TimelineScheduler scheduler(48000);
    scheduler.set_plan(&plan, 0);
    std::array<TimelineScheduledEvent, 8> events{};
    const auto count = scheduler.schedule(0, 300, events);
    check(count == 2 && events[0].frame == 50 && events[1].frame == 150 &&
              events[0].type == MusicalEventType::audio_clip_on &&
              events[1].type == MusicalEventType::audio_clip_off,
          "audio clip start/end timing mismatch");

    auto contiguous = render(false);
    auto partitioned = render(true);
    check(contiguous == partitioned, "audio clip callback partition changed PCM");
    check(std::all_of(contiguous.begin(), contiguous.begin() + 50,
                      [](float value) { return value == 0; }) &&
              std::any_of(contiguous.begin() + 50, contiguous.begin() + 150,
                          [](float value) { return value > .1F; }) &&
              std::all_of(contiguous.begin() + 150, contiguous.end(),
                          [](float value) { return value == 0; }),
          "native audio clip boundaries produced incorrect PCM");

    const auto looped = render(false, true);
    check(std::all_of(looped.begin() + 50, looped.begin() + 150,
                      [](float value) { return value > .1F; }),
          "long clip did not loop without repeated trigger events");
    const auto sought = render(false, true, 75);
    check(sought.front() > .1F,
          "seeking into looping clip did not restore clip-relative phase");

    {
        AudioEngine engine(48000, 4);
        SampleManager samples(engine);
        const auto original = std::make_shared<const Sample>(
            Sample{48000, 1, std::vector<float>(100, .5F)});
        check(samples.publish_prepared(SampleSlotId{0}, original, "old.wav"),
              "replacement test source publication failed");
        std::array<float, 1> ack_l{}, ack_r{};
        engine.stop();
        engine.process(ack_l, ack_r);
        auto replacement_timeline = timeline_with_clip();
        replacement_timeline.arrangement.audio_clips.front().duration = 8;
        replacement_timeline.arrangement.song_end = 12;
        check(engine.queue_timeline(replacement_timeline, error),
              "replacement test timeline failed");
        engine.restart();
        std::array<float, 75> before_l{}, before_r{};
        engine.process(before_l, before_r);
        const auto replacement = std::make_shared<const Sample>(
            Sample{48000, 1, std::vector<float>(100, 0.0F)});
        check(samples.publish_prepared(SampleSlotId{0}, replacement,
                                       "new.wav"),
              "replacement publication failed");
        std::array<float, 25> after_l{}, after_r{};
        engine.process(after_l, after_r);
        check(std::any_of(after_l.begin(), after_l.end(),
                          [](float value) { return value > .1F; }),
              "active clip voice lost its immutable source after replacement");
    }

    auto overlap = timeline;
    auto second = overlap.arrangement.audio_clips.front();
    second.id = 92;
    second.start = 4;
    overlap.arrangement.audio_clips.push_back(second);
    check(!validate_arrangement(overlap, error) &&
              error.find("overlap") != std::string::npos,
          "ambiguous same-track overlap was accepted");
    second.start = 6;
    overlap.arrangement.audio_clips.back() = second;
    check(validate_arrangement(overlap, error), "adjacent audio clips rejected");

    TimelineService service([](const Timeline &, std::string &) { return true; },
                            timeline);
    auto edit = service.edit();
    auto duplicate = edit.timeline().arrangement.audio_clips.front();
    duplicate.id = edit.next_audio_clip_id();
    duplicate.start = 6;
    check(edit.create_audio_clip(duplicate, error) &&
              edit.update_audio_clip(duplicate, error) &&
              edit.delete_audio_clip(duplicate.id, error) &&
              service.commit(std::move(edit),
                             TimelineCommitPolicy::next_audio_callback, error),
          "transactional audio clip edits failed");
    check(service.undo(TimelineCommitPolicy::next_audio_callback, error) &&
              service.redo(TimelineCommitPolicy::next_audio_callback, error),
          "audio clip edits did not participate in history");

    const auto path = std::filesystem::temp_directory_path() /
                      "neurogenic-audio-clips.timeline";
    save_timeline(timeline, path);
    const auto restored = load_timeline(path);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    check(restored.arrangement.audio_clips.size() == 1 &&
              restored.arrangement.audio_clips.front().id == 91 &&
              restored.arrangement.audio_clips.front().sample_publication == 0,
          "timeline-v8 audio clip round trip failed");

    auto stretched = timeline;
    stretched.arrangement.audio_clips.front().stretch_mode =
        AudioClipStretchMode::tempo_sync;
    stretched.arrangement.audio_clips.front().source_bpm = 90;
    check(validate_arrangement(stretched, error), "tempo-synced clip rejected");
    stretched.arrangement.audio_clips.front().loop = true;
    check(!validate_arrangement(stretched, error),
          "unsupported stretched looping was not rejected");

    std::cout << "neurogenic.audio-clip-tests.v1 passed\n";
    return 0;
} catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
}
