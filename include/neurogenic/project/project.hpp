#pragma once
#include "neurogenic/audio/mixer.hpp"
#include "neurogenic/audio/slicing.hpp"
#include "neurogenic/dsp/modulation.hpp"
#include "neurogenic/midi/routing.hpp"
#include "neurogenic/timeline/model.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace neurogenic {
struct Project {
    static constexpr unsigned format_version = 3;
    std::string name{"Untitled"};
    Timeline timeline;
    SliceProject samples;
    std::map<std::string, double> parameters;
    MixerConfiguration mixer;
    std::vector<RoutingScene> routing_scenes;
    std::uint64_t active_routing_scene{};
    ModulationConfiguration modulation;
    MidiProjectConfiguration midi;
    std::vector<std::filesystem::path> missing_assets;
};

struct ProjectIoMetrics {
    std::uint64_t elapsed_microseconds{};
    std::uintmax_t metadata_bytes{};
};

void save_project(const Project &, const std::filesystem::path &directory,
                  ProjectIoMetrics *metrics = nullptr);
Project load_project(const std::filesystem::path &directory,
                     ProjectIoMetrics *metrics = nullptr);
bool relink_project_sample(Project &, const std::filesystem::path &missing,
                           const std::filesystem::path &replacement,
                           std::string &error);
} // namespace neurogenic
