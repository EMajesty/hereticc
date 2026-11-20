#include "neurogenic/application/parameter_binding.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace neurogenic;
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void execute(ApplicationService &application, std::string id,
             std::map<std::string, ApplicationValue> arguments = {}) {
    const auto result = application.execute(
        {std::move(id), std::move(arguments), "binding-test", 0});
    if (!result.accepted) throw std::runtime_error(result.error);
}
BindingReference reference(std::string object, std::string parameter) {
    return {BindingTargetKind::explicit_object, std::move(object),
            std::move(parameter), {}, 0};
}
double number(const ApplicationValue &value) {
    if (const auto *v = std::get_if<double>(&value)) return *v;
    if (const auto *v = std::get_if<std::int64_t>(&value)) return *v;
    throw std::runtime_error("value is not numeric");
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
        ParameterBindingService bindings(application);
        std::array<float, 64> left{}, right{};
        const auto process = [&] { engine.process(left, right); };
        std::string registry_error;
        check(application.parameters().register_parameter(
                  {"test.enabled", "Test enabled", "", ParameterType::boolean,
                   false, true, false, false, 1.0, false, false, false, {}},
                  registry_error),
              "register boolean fixture");
        check(application.parameters().register_parameter(
                  {"test.mode", "Test mode", "", ParameterType::enumeration,
                   std::int64_t{0}, std::int64_t{2}, std::int64_t{0},
                   std::int64_t{0}, 1.0, false, false, false,
                   {"off", "low", "high"}},
                  registry_error),
              "register enum fixture");
        execute(application, "transport.stop.v1");

        const auto discovery = discover_application(application);
        check(std::all_of(discovery.parameters.begin(), discovery.parameters.end(),
                          [](const auto &parameter) {
                              return !parameter.id.empty() &&
                                     !parameter.parent.empty() &&
                                     !parameter.display_name.empty() &&
                                     !std::holds_alternative<std::monostate>(
                                         parameter.default_value);
                          }),
              "descriptor completeness");

        auto master = reference("master:main", "mixer.master.gain");
        auto read = bindings.read(master);
        check(read.available && std::fabs(number(read.base_value) - 1.0) < 1e-6,
              "read base value");
        check(!bindings.apply(master, {BindingOperation::absolute_native, 1.25}),
              "absolute native write");
        process();
        check(std::fabs(number(bindings.read(master).base_value) - 1.25) < 1e-6,
              "write uses semantic action");
        check(bindings.validate(master, 3.0, BindingOperation::absolute_native).code ==
                  BindingErrorCode::out_of_range,
              "range validation");
        check(bindings.validate(master, std::string("wrong"),
                                BindingOperation::absolute_native).code ==
                  BindingErrorCode::type_mismatch,
              "type validation");
        auto enumeration = reference("project:current", "test.mode");
        check(!bindings.validate(enumeration, std::string("high"),
                                 BindingOperation::absolute_native) &&
                  bindings.validate(enumeration, std::string("missing"),
                                    BindingOperation::absolute_native).code ==
                      BindingErrorCode::invalid_choice,
              "enum choice validation");
        auto boolean = reference("project:current", "test.enabled");
        check(!bindings.apply(boolean, {BindingOperation::toggle}),
              "boolean toggle");
        check(std::get<bool>(bindings.read(boolean).base_value),
              "toggle value");
        ControlInput release{BindingOperation::momentary};
        release.pressed = false;
        check(!bindings.apply(boolean, release) &&
                  !std::get<bool>(bindings.read(boolean).base_value),
              "momentary release");
        check(!bindings.apply(master,
                              {BindingOperation::absolute_normalized, 0.5}),
              "absolute normalized control");
        process();
        check(!bindings.apply(master,
                              {BindingOperation::absolute_normalized, 8192.0,
                               false, 16383}),
              "high-resolution absolute control");
        process();
        check(std::fabs(number(bindings.read(master).base_value) -
                        2.0 * 8192.0 / 16383.0) < 1e-5,
              "high-resolution value mapping");
        check(!bindings.apply(master, {BindingOperation::relative_steps, 1.0}),
              "relative encoder control");
        process();

        execute(application, "track.create.v1",
                {{"track_id", std::int64_t{11}},
                 {"name", std::string("Binding")},
                 {"length", std::int64_t{3840}}});
        execute(application, "track.select.v1", {{"track_id", std::int64_t{11}}});
        BindingReference selected{BindingTargetKind::selected_track, {},
                                  "mixer.track.gain", {}, 0};
        BindingError failure;
        check(bindings.resolve(selected, failure).has_value(),
              "selected object binding");

        execute(application, "effect.create.v1",
                {{"id", std::int64_t{91}},
                 {"type", std::string("native.gain.v1")}});
        process();
        auto fx = reference("fx-instance:91", "fx.91.gain");
        check(!bindings.apply(fx, {BindingOperation::absolute_native, 0.4}),
              "dynamic effect parameter write");
        process();
        check(std::fabs(number(bindings.read(fx).base_value) - 0.4) < 1e-5,
              "dynamic effect parameter read");

        execute(application, "modulator.create.v1",
                {{"type", std::int64_t{1}}, {"owner", std::int64_t{3}},
                 {"owner_id", std::int64_t{0}},
                 {"name", std::string("Binding LFO")}});
        process();
        const auto with_modulator = discover_application(application);
        const auto mod_object = std::find_if(
            with_modulator.objects.begin(), with_modulator.objects.end(),
            [](const auto &object) {
                return object.type == "modulator" &&
                       object.display_name == "Binding LFO";
            });
        check(mod_object != with_modulator.objects.end(),
              "dynamic modulator object discovery");
        const auto mod_parameter = std::find_if(
            with_modulator.parameters.begin(), with_modulator.parameters.end(),
            [&](const auto &parameter) {
                return parameter.parent == mod_object->id &&
                       parameter.id.ends_with(".rate");
            });
        check(mod_parameter != with_modulator.parameters.end(),
              "dynamic modulator parameter discovery");
        auto mod = reference(mod_object->id, mod_parameter->id);
        check(!bindings.apply(mod, {BindingOperation::absolute_native, 2.5}),
              "dynamic modulator write");
        process();
        check(std::fabs(number(bindings.read(mod).base_value) - 2.5) < 1e-5,
              "dynamic modulator read");

        BindingError state_error;
        check(bindings.resolve_state(
                  {BindingTargetKind::state_path, {}, {}, "transport.playing", 0},
                  state_error)
                  .has_value(),
              "typed state binding");
        check(!bindings.resolve_state(
                   {BindingTargetKind::state_path, {}, {}, "audio.raw-pcm", 0},
                   state_error)
                   .has_value() &&
                  state_error.code == BindingErrorCode::unknown_state_path,
              "unknown state binding rejection");

        FaderPickup pickup(0.01);
        check(!pickup.update(0.2), "pickup initial position");
        pickup.set_target(0.75);
        check(!pickup.update(0.5) && pickup.state().direction == 1,
              "pickup direction");
        check(pickup.update(0.8) && pickup.state().acquired,
              "pickup crossing");
        check(std::fabs(ParameterBindingService::normalize_high_resolution(8192) -
                        (8192.0 / 16383.0)) < 1e-9,
              "high resolution MIDI normalization");

        BindingUpdateStream updates(2);
        updates.observe(application.snapshot());
        check(updates.resync_required() && updates.dropped() > 0,
              "bounded update overflow");
        const auto overflow = updates.drain();
        check(overflow.size() == 1 &&
                  overflow.front().kind == BindingUpdateKind::resync_required,
              "explicit resynchronization marker");
        updates.resynchronize(application.snapshot());
        execute(application, "transport.play.v1");
        updates.observe(application.snapshot());
        check(!updates.drain().empty(), "ordinary state notification");

        execute(application, "effect.delete.v1", {{"id", std::int64_t{91}}});
        fx.discovery_revision = 1;
        check(!bindings.read(fx).available &&
                  (bindings.read(fx).error.code == BindingErrorCode::stale_reference ||
                   bindings.read(fx).error.code == BindingErrorCode::missing_object),
              "deleted object never rebinds");

        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) check(bindings.read(master).available, "read cost");
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - begin).count();
        std::cout << "neurogenic.parameter-binding-cost.v1 reads=100 us="
                  << elapsed << '\n';
        samples.shutdown();
        std::cout << "neurogenic.parameter-binding-tests.v1 passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "PARAMETER BINDING TEST FAILURE: " << error.what() << '\n';
        return 1;
    }
}
