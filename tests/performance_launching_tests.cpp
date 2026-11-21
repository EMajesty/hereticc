#include "neurogenic/timeline/io.hpp"
#include "neurogenic/timeline/performance_launching.hpp"
#include "neurogenic/application/control.hpp"
#include "neurogenic/application/discovery.hpp"
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
         SlotFollowAction::random_eligible, 0, 1, 99, true}};
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
    const LaunchSlotId eligible[]{101, 102};
    assert(launcher.follow(101, 1, eligible) == 102);
    const auto random = launcher.follow(102, 1, eligible);
    assert(random == launcher.follow(102, 1, eligible));
    timeline.arrangement.velocity_policy = VelocityPolicy::fixed;
    timeline.arrangement.fixed_velocity = 0.75F;
    assert(launcher.configure(timeline, error));
    assert(launcher.velocity(0.2F, false, false) == 0.75F);
    assert(launcher.velocity(0.2F, true, true) == 1.0F);
    const auto path = std::filesystem::temp_directory_path() / "neurogenic-launching.timeline";
    save_timeline(timeline, path);
    const auto loaded = load_timeline(path);
    std::filesystem::remove(path);
    assert(loaded.arrangement.launch_slots.size() == 2);
    assert(loaded.arrangement.performance_scenes.size() == 1);
    assert(loaded.arrangement.velocity_policy == VelocityPolicy::fixed);
    auto invalid = timeline;
    invalid.arrangement.launch_slots[0].source_id = 999;
    assert(!launcher.configure(invalid, error));
    AudioEngine engine(48000, 8);
    SampleManager samples(engine);
    TimelineService service([&](const Timeline &value, std::string &publish_error) {
        return engine.queue_timeline(value, publish_error);
    }, fixture());
    ApplicationService application(engine, service, samples);
    auto result = application.execute({"performance.slot-duplicate.v1",
        {{"source", std::int64_t{101}}, {"slot", std::int64_t{103}}}, "test", 1});
    assert(result.accepted);
    result = application.execute({"performance.velocity-policy.v1",
        {{"policy", std::string("fixed")}, {"velocity", 0.6}}, "test", 2});
    assert(result.accepted);
    const auto state = application.snapshot();
    assert(state.performance_launching.slots.size() == 3);
    assert(state.performance_launching.velocity_policy == VelocityPolicy::fixed);
    const auto discovery = discover_application(application);
    assert(std::any_of(discovery.objects.begin(), discovery.objects.end(),
        [](const auto &object) { return object.type == "performance-launch-slot"; }));
    samples.shutdown();
    std::cout << "neurogenic.performance-launching-tests.v1 passed\n";
}
