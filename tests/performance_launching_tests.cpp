#include "neurogenic/timeline/io.hpp"
#include "neurogenic/timeline/performance_launching.hpp"
#include "neurogenic/application/control.hpp"
#include "neurogenic/application/discovery.hpp"
#include "neurogenic/timeline/arrangement_recording.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace neurogenic;
namespace {
Timeline fixture() {
    Timeline t;
    t.tracks = {{1, "Drums"}, {2, "Loop"}};
    PatternDefinition p;
    p.id = 11; p.name = "Beat"; p.length = ticks_per_quarter * 4;
    p.tracks.push_back({1, {p.length, {}}});
    t.arrangement.patterns.push_back(p);
    auto alternate = p;
    alternate.id = 12; alternate.name = "Break";
    t.arrangement.patterns.push_back(alternate);
    AudioClip clip;
    clip.id = 21; clip.track = 2; clip.sample_slot = 1;
    clip.start = 0; clip.duration = ticks_per_quarter * 4;
    clip.source_end_frame = 48000; clip.loop = true;
    clip.loop_end_frame = 48000;
    t.arrangement.audio_clips.push_back(clip);
    t.arrangement.launch_slots = {
        {101, 1, LaunchSourceType::pattern, 11, LaunchBehavior::loop,
         LaunchQuantization::bar, ticks_per_quarter / 4,
         SlotFollowAction::next, 0, 1, 7, true},
        {102, 2, LaunchSourceType::live_loop, 21, LaunchBehavior::legato,
         LaunchQuantization::bar, ticks_per_quarter / 4,
         SlotFollowAction::random_eligible, 0, 1, 99, true},
        {103, 1, LaunchSourceType::pattern, 12, LaunchBehavior::loop,
         LaunchQuantization::bar, ticks_per_quarter / 4,
         SlotFollowAction::stop, 0, 1, 11, true}};
    t.arrangement.performance_scenes.push_back(
        {301, "Both", {{1, 101}, {2, 102}}, LaunchQuantization::bar,
         ticks_per_quarter / 4});
    return t;
}
}
int main() {
    auto timeline = fixture();
    PerformanceLauncher launcher(48000);
    std::string error;
    assert(launcher.configure(timeline, error));
    PreparedLaunch launch;
    assert(launcher.prepare_slot(101, PreparedLaunchOperation::start, 100, 5000,
                                 launch, error));
    assert(launch.activation_tick == ticks_per_quarter * 4);
    assert(launch.activation_frame == 5000 + 93500);
    std::vector<PreparedLaunch> scene;
    assert(launcher.prepare_scene(301, 100, 5000, scene, error));
    assert(scene.size() == 2 && scene[0].activation_frame == scene[1].activation_frame);
    assert(scene[1].preserve_phase);
    const LaunchSlotId eligible[]{101, 103};
    assert(launcher.follow(101, 1, eligible) == 103);
    PerformanceLauncher follow_launcher(48000);
    assert(follow_launcher.configure(timeline, error));
    PreparedLaunch first;
    assert(follow_launcher.prepare_slot(101, PreparedLaunchOperation::start,
                                        100, 5000, first, error));
    std::vector<PreparedLaunch> activated;
    follow_launcher.acknowledge(first.activation_frame, &activated);
    assert(activated.size() == 1 && activated.front().slot == 101);
    PreparedLaunch followed;
    assert(follow_launcher.prepare_follow(activated.front(), followed, error));
    assert(followed.slot == 103 && followed.follow_transition &&
           followed.activation_tick == launch.activation_tick + ticks_per_quarter * 4);
    const auto random = launcher.follow(102, 1, std::span(eligible));
    assert(random == launcher.follow(102, 1, std::span(eligible)));
    timeline.arrangement.velocity_policy = VelocityPolicy::fixed;
    timeline.arrangement.fixed_velocity = 0.75F;
    assert(launcher.configure(timeline, error));
    assert(launcher.velocity(0.2F, false, false) == 0.75F);
    assert(launcher.velocity(0.2F, true, true) == 1.0F);
    const auto path = std::filesystem::temp_directory_path() / "neurogenic-launching.timeline";
    save_timeline(timeline, path);
    const auto loaded = load_timeline(path);
    std::filesystem::remove(path);
    assert(loaded.arrangement.launch_slots.size() == 3);
    assert(loaded.arrangement.performance_scenes.size() == 1);
    assert(loaded.arrangement.velocity_policy == VelocityPolicy::fixed);
    auto invalid = timeline;
    invalid.arrangement.launch_slots[0].source_id = 999;
    assert(!launcher.configure(invalid, error));
    AudioEngine engine(48000, 8);
    SampleManager samples(engine);
    TimelineService service([&](const Timeline &, std::string &publish_error) {
        publish_error.clear(); return true;
    }, fixture());
    ApplicationService application(engine, service, samples);
    auto result = application.execute({"performance.slot-duplicate.v1",
        {{"source", std::int64_t{101}}, {"slot", std::int64_t{104}}}, "test", 1});
    assert(result.accepted);
    result = application.execute({"performance.scene-create.v1",
        {{"scene", std::int64_t{302}}, {"name", std::string("Partial")}}, "test", 2});
    assert(result.accepted);
    result = application.execute({"performance.scene-slot-set.v1",
        {{"scene", std::int64_t{302}}, {"track", std::int64_t{1}},
         {"slot", std::int64_t{101}}}, "test", 3});
    assert(result.accepted);
    result = application.execute({"performance.velocity-policy.v1",
        {{"policy", std::string("fixed")}, {"velocity", 0.6}}, "test", 4});
    assert(result.accepted);
    const auto state = application.snapshot();
    assert(state.performance_launching.slots.size() == 4);
    assert(state.performance_launching.scenes.size() == 2);
    assert(state.performance_launching.velocity_policy == VelocityPolicy::fixed);
    const auto discovery = discover_application(application);
    assert(std::any_of(discovery.objects.begin(), discovery.objects.end(),
        [](const auto &object) { return object.type == "performance-launch-slot"; }));

    TimelineService capture_service([](const Timeline &, std::string &value) {
        value.clear(); return true;
    }, fixture());
    ArrangementRecordingService capture;
    assert(capture.arm(true, error));
    assert(capture.start(0, ArrangementRecordMode::overdub, 0, false, error));
    PreparedLaunch pattern{101, 1, PreparedLaunchOperation::start, 0, 100,
                           LaunchSourceType::pattern, 11};
    PreparedLaunch audio{102, 2, PreparedLaunchOperation::start,
                         ticks_per_quarter * 4, 96100,
                         LaunchSourceType::live_loop, 21};
    audio.scene = 301;
    assert(capture.capture_performance(pattern, 100, 0, false, 0.8F));
    assert(capture.capture_performance(audio, 96100, 301, true, 1.0F));
    assert(capture.stop(ticks_per_quarter * 8, error));
    assert(capture.commit(capture_service, error));
    const auto captured = capture_service.snapshot().timeline.arrangement;
    assert(captured.clips.size() == 1);
    assert(captured.audio_clips.size() == 2);
    assert(capture_service.history_status().can_undo);
    samples.shutdown();
    std::cout << "neurogenic.performance-launching-tests.v1 passed\n";
}
