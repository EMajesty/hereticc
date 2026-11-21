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
                          std::uint64_t seek = 0,
                          const neurogenic::Timeline *custom = nullptr) {
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
    const auto timeline = custom ? *custom : timeline_with_clip(loop);
    check(engine.queue_timeline(timeline, error),
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
    check(validate_arrangement(overlap, error),
          "same-track audio clip overlap was rejected");
    AudioClipCrossfade crossfade{1, 91, 92, 2,
                                 AudioClipFadeCurve::equal_power, true};
    overlap.arrangement.audio_crossfades.push_back(crossfade);
    check(validate_arrangement(overlap, error), "valid crossfade rejected");
    PlaybackPlan overlap_plan;
    check(compile_timeline(overlap, overlap_plan, error) &&
              overlap_plan.audio_clips.size() == 2 &&
              overlap_plan.peak_audio_clip_voices == 2 &&
              overlap_plan.audio_clips[0].crossfade_out == 2 &&
              overlap_plan.audio_clips[1].crossfade_in == 2,
          "crossfade was not compiled into clip envelopes");
    {
        AudioEngine constrained(48000, 1);
        check(!constrained.queue_timeline(overlap, error) &&
                  error.find("requires 2 voices") != std::string::npos,
              "audio clip voice-budget exhaustion was not rejected");
    }
    auto faded = overlap;
    faded.arrangement.audio_crossfades.clear();
    faded.arrangement.audio_clips.resize(1);
    faded.arrangement.audio_clips.front().clip_fade_in = 2;
    faded.arrangement.audio_clips.front().clip_fade_out = 2;
    const auto faded_pcm = render(false, false, 0, &faded);
    check(faded_pcm[50] == 0.0F && faded_pcm[75] > faded_pcm[60] &&
              faded_pcm[149] < faded_pcm[125],
          "clip-time linear fade envelope is incorrect");
    const auto crossfaded_pcm = render(false, false, 0, &overlap);
    check(crossfaded_pcm[100] > 0.1F && crossfaded_pcm[125] > 0.4F &&
              crossfaded_pcm[149] > 0.1F,
          "equal-power overlapping crossfade produced invalid PCM");
    second.start = 6;
    overlap.arrangement.audio_clips.back() = second;
    check(!validate_arrangement(overlap, error),
          "crossfade without an actual overlap was accepted");
    overlap.arrangement.audio_crossfades.clear();
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
          "timeline-v9 audio clip round trip failed");

    const auto crossfade_path = std::filesystem::temp_directory_path() /
                                "neurogenic-audio-crossfades.timeline";
    auto persisted = faded;
    auto persisted_second = persisted.arrangement.audio_clips.front();
    persisted_second.id = 92;
    persisted_second.start = 4;
    persisted.arrangement.audio_clips.push_back(persisted_second);
    persisted.arrangement.audio_crossfades.push_back(crossfade);
    TimelineService crossfade_service(
        [](const Timeline &, std::string &) { return true; }, persisted);
    auto crossfade_edit = crossfade_service.edit();
    check(crossfade_edit.delete_audio_clip(92, error) &&
              crossfade_edit.timeline().arrangement.audio_crossfades.empty(),
          "clip deletion left a stale crossfade reference");
    save_timeline(persisted, crossfade_path);
    const auto restored_crossfade = load_timeline(crossfade_path);
    std::filesystem::remove(crossfade_path, ignored);
    check(restored_crossfade.arrangement.audio_crossfades.size() == 1 &&
              restored_crossfade.arrangement.audio_clips.front().clip_fade_in == 2,
          "clip fade/crossfade persistence round trip failed");

    auto stretched = timeline;
    stretched.arrangement.audio_clips.front().stretch_mode =
        AudioClipStretchMode::tempo_sync;
    stretched.arrangement.audio_clips.front().source_bpm = 90;
    check(validate_arrangement(stretched, error), "tempo-synced clip rejected");
    stretched.arrangement.audio_clips.front().loop = true;
    stretched.arrangement.audio_clips.front().loop_start_frame = 0;
    stretched.arrangement.audio_clips.front().loop_end_frame = 32;
    stretched.arrangement.audio_clips.front().loop_crossfade_frames = 4;
    check(validate_arrangement(stretched, error),
          "loop-aware stretched clip rejected");
    save_timeline(stretched, path);
    const auto restored_stretched = load_timeline(path);
    std::filesystem::remove(path, ignored);
    check(restored_stretched.arrangement.audio_clips.front().loop &&
              restored_stretched.arrangement.audio_clips.front().stretch_mode ==
                  AudioClipStretchMode::tempo_sync &&
              restored_stretched.arrangement.audio_clips.front()
                      .loop_crossfade_frames == 4,
          "stretched loop persistence round trip failed");

    std::cout << "neurogenic.audio-clip-tests.v1 passed\n";
    return 0;
} catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
}
