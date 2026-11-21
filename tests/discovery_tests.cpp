#include "neurogenic/application/discovery.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace neurogenic;
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
bool has_object(const ApplicationDiscovery &value, std::string_view type) {
    return std::any_of(value.objects.begin(), value.objects.end(),
                       [&](const auto &object) { return object.type == type; });
}
void execute(ApplicationService &application, std::string id,
             std::map<std::string, ApplicationValue> arguments = {}) {
    const auto result = application.execute(
        {std::move(id), std::move(arguments), "discovery-test", 0});
    if (!result.accepted)
        throw std::runtime_error(result.error);
}
} // namespace

int main() {
    try {
        AudioEngine engine;
        SampleManager samples(engine);
        TimelineService timeline([&](const Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        });
        ApplicationService application(engine, timeline, samples);
        auto initial = discover_application(application);
        std::string error;
        check(validate_discovery(initial, error), "initial schema validation");
        check(discovery_json(initial) ==
                  discovery_json(discover_application(application)),
              "deterministic discovery serialization");
        check(supports_discovery_schema(1, 1) &&
                  !supports_discovery_schema(2, 3),
              "capability version negotiation");
        check(validate_state_binding(initial, "transport.playing", error) &&
                  !validate_state_binding(initial, "audio.raw-pcm", error),
              "state binding validation");
        check(has_object(initial, "modulator-type") &&
                  has_object(initial, "fx-processor-type") &&
                  has_object(initial, "midi-port"),
              "static type and logical-port discovery");
        check(std::any_of(initial.actions.begin(), initial.actions.end(),
                          [](const auto &action) {
                              return action.descriptor.id == "track.create.v1" &&
                                     !action.descriptor.arguments.empty();
                          }),
              "semantic action registry export");

        execute(application, "track.create.v1",
                {{"track_id", std::int64_t{11}}, {"name", std::string("Discovery")},
                 {"length", std::int64_t{3840}}});
        execute(application, "mixer.group-create.v1",
                {{"name", std::string("Bus")}});
        execute(application, "effect.create.v1",
                {{"id", std::int64_t{91}}, {"type", std::string("native.gain.v1")}});
        execute(application, "modulator.create.v1",
                {{"type", std::int64_t{1}}, {"owner", std::int64_t{3}},
                 {"owner_id", std::int64_t{0}}, {"name", std::string("Motion")}});
        execute(application, "pattern.create.v1",
                {{"id", std::int64_t{71}}, {"name", std::string("Verse")},
                 {"length", std::int64_t{3840}}});
        auto clip_sample = std::make_shared<const Sample>(
            Sample{48000, 1, std::vector<float>(480, .25F)});
        check(samples.publish_prepared(SampleSlotId{0}, clip_sample,
                                       "discovery-clip.wav"),
              "audio clip sample publication");
        std::array<float, 64> left{}, right{};
        engine.process(left, right);
        execute(application, "arrangement.range-set.v1",
                {{"start", std::int64_t{0}},
                 {"end", std::int64_t{3840}}});
        execute(application, "arrangement.mode-set.v1",
                {{"mode", std::string("song")}});
        execute(application, "arrangement.audio-clip-create.v1",
                {{"id", std::int64_t{81}},
                 {"track", std::int64_t{11}},
                 {"sample_slot", std::int64_t{0}},
                 {"start", std::int64_t{0}},
                 {"duration", std::int64_t{960}}});
        execute(application, "live-loop.create.v1",
                {{"track", std::int64_t{11}}, {"slot", std::int64_t{0}},
                 {"source", std::string("master-post")},
                 {"quantization", std::string("beat")},
                 {"length_ticks", std::int64_t{960}}});
        execute(application, "macro.create.v1",
                {{"name", std::string("Macro")}, {"owner", std::string("project")},
                 {"value", 0.25}});
        execute(application, "macro.scene-capture.v1",
                {{"name", std::string("Scene")}});
        engine.process(left, right);

        const auto populated = discover_application(application);
        check(validate_discovery(populated, error), "populated schema validation");
        for (const auto type : {"track", "mixer-group", "fx-instance",
                                "modulator", "pattern", "performance-macro",
                                "scene", "audio-clip", "live-loop-session"})
            check(has_object(populated, type), "dynamic object discovery");
        check(std::any_of(populated.parameters.begin(), populated.parameters.end(),
                          [](const auto &parameter) {
                              return parameter.parent == "fx-instance:91" &&
                                     parameter.writable && parameter.automatable;
                          }),
              "effect instance parameter discovery");

        execute(application, "project.new.v1",
                {{"name", std::string("Replacement")}, {"discard", true}});
        engine.process(left, right);
        const auto replaced = discover_application(application);
        check(validate_discovery(replaced, error), "replacement schema validation");
        check(!std::any_of(replaced.objects.begin(), replaced.objects.end(),
                           [](const auto &object) {
                               return object.id == "track:11" ||
                                      object.id == "fx-instance:91";
                           }),
              "no stale discovery identities after project replacement");

        auto unsupported = replaced;
        unsupported.schema_version = 99;
        check(!validate_discovery(unsupported, error),
              "unsupported schema rejection");
        samples.shutdown();
        std::cout << "neurogenic.discovery-tests.v1 passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "DISCOVERY TEST FAILURE: " << error.what() << '\n';
        return 1;
    }
}
