#include "neurogenic/application/discovery.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/timeline/edit.hpp"
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    const bool json = argc == 1 ||
                      (argc == 2 && std::string_view(argv[1]) == "--json");
    if (!json) {
        std::cerr << "usage: neurogenic-discovery [--json]\n";
        return 2;
    }
    neurogenic::AudioEngine engine;
    neurogenic::SampleManager samples(engine);
    neurogenic::TimelineService timeline(
        [&](const neurogenic::Timeline &value, std::string &error) {
            return engine.queue_timeline(value, error);
        });
    neurogenic::ApplicationService application(engine, timeline, samples);
    auto discovery = neurogenic::discover_application(application);
    std::string error;
    if (!neurogenic::validate_discovery(discovery, error)) {
        std::cerr << "invalid discovery document: " << error << '\n';
        samples.shutdown();
        return 1;
    }
    std::cout << neurogenic::discovery_json(discovery) << '\n';
    samples.shutdown();
}
