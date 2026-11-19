#include "neurogenic/project/project.hpp"
#include "neurogenic/timeline/io.hpp"
#include "neurogenic/timeline/playback.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace neurogenic {
namespace {
constexpr std::uintmax_t maximum_metadata_size = 16U * 1024U * 1024U;
void bounded(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_metadata_size)
        throw std::runtime_error("project metadata is missing or oversized: " +
                                 path.string());
}
std::filesystem::path resolve_reference(const std::filesystem::path &root,
                                        const std::filesystem::path &value) {
    if (value.is_absolute())
        return value.lexically_normal();
    const auto normalized = value.lexically_normal();
    if (normalized.empty() || *normalized.begin() == "..")
        throw std::runtime_error("unsafe project-relative sample path: " +
                                 value.string());
    return (root / normalized).lexically_normal();
}
void checked_write(const std::filesystem::path &path,
                   const std::string &contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << contents) || !(output.flush()))
        throw std::runtime_error("cannot write project metadata: " +
                                 path.string());
}
std::string generation_name() {
    static std::atomic<std::uint64_t> sequence{};
    return "generation-" +
           std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()) +
           "-" + std::to_string(sequence.fetch_add(1));
}
} // namespace

void save_project(const Project &project,
                  const std::filesystem::path &directory,
                  ProjectIoMetrics *metrics) {
    const auto begin = std::chrono::steady_clock::now();
    PlaybackPlan validation;
    std::string validation_error;
    if (project.name.empty() ||
        !compile_timeline(project.timeline, validation, validation_error))
        throw std::invalid_argument("invalid project: " + validation_error);
    std::filesystem::create_directories(directory / "generations");
    const auto generation = generation_name();
    const auto temporary = directory / "generations" / (generation + ".tmp");
    const auto committed = directory / "generations" / generation;
    std::filesystem::create_directory(temporary);
    try {
        save_timeline(project.timeline, temporary / "timeline.ngt");
        auto samples = project.samples;
        for (auto &[slot, path] : samples.sample_paths) {
            (void)slot;
            if (!path.is_absolute())
                continue;
            const auto relative = path.lexically_normal().lexically_relative(
                directory.lexically_normal());
            if (!relative.empty() && *relative.begin() != "..")
                path = relative;
        }
        save_slice_project(samples, temporary / "samples.ngs");
        std::ostringstream parameters;
        parameters << "NEUROGENIC_PARAMETERS 1\n"
                   << std::setprecision(17);
        for (const auto &[id, value] : project.parameters)
            parameters << std::quoted(id) << ' ' << value << '\n';
        checked_write(temporary / "parameters.ngp", parameters.str());
        std::filesystem::rename(temporary, committed);
        std::ostringstream manifest;
        manifest << "NEUROGENIC_PROJECT " << Project::format_version << '\n'
                 << "name " << std::quoted(project.name) << '\n'
                 << "generation " << std::quoted(generation) << '\n';
        checked_write(directory / "manifest.tmp", manifest.str());
        std::filesystem::rename(directory / "manifest.tmp",
                                directory / "manifest.ngp");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
        throw;
    }
    if (metrics) {
        metrics->elapsed_microseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - begin)
                .count());
        metrics->metadata_bytes =
            std::filesystem::file_size(directory / "manifest.ngp") +
            std::filesystem::file_size(committed / "timeline.ngt") +
            std::filesystem::file_size(committed / "samples.ngs") +
            std::filesystem::file_size(committed / "parameters.ngp");
    }
}

Project load_project(const std::filesystem::path &directory,
                     ProjectIoMetrics *metrics) {
    const auto begin = std::chrono::steady_clock::now();
    const auto manifest_path = directory / "manifest.ngp";
    bounded(manifest_path);
    std::ifstream manifest(manifest_path);
    std::string magic, kind, name, generation;
    unsigned version{};
    if (!(manifest >> magic >> version) || magic != "NEUROGENIC_PROJECT" ||
        version == 0 || version > Project::format_version ||
        !(manifest >> kind >> std::quoted(name)) || kind != "name" ||
        !(manifest >> kind >> std::quoted(generation)) ||
        kind != "generation" || generation.empty() ||
        generation.find('/') != std::string::npos || generation == "..")
        throw std::runtime_error("unsupported or malformed project manifest");
    const auto files = directory / "generations" / generation;
    bounded(files / "timeline.ngt");
    bounded(files / "samples.ngs");
    bounded(files / "parameters.ngp");
    Project result;
    result.name = std::move(name);
    result.timeline = load_timeline(files / "timeline.ngt");
    result.samples = load_slice_project(files / "samples.ngs", false);
    for (auto &[slot, path] : result.samples.sample_paths) {
        path = resolve_reference(directory, path);
        if (!std::filesystem::exists(path))
            result.missing_assets.push_back(path);
    }
    std::ifstream parameters(files / "parameters.ngp");
    if (!(parameters >> magic >> version) || magic != "NEUROGENIC_PARAMETERS" ||
        version != 1)
        throw std::runtime_error("unsupported parameter metadata");
    while (parameters >> std::quoted(name)) {
        double value{};
        if (!(parameters >> value) || !std::isfinite(value))
            throw std::runtime_error("invalid project parameter");
        result.parameters.emplace(name, value);
        if (result.parameters.size() > 4096)
            throw std::runtime_error("too many project parameters");
    }
    if (metrics) {
        metrics->elapsed_microseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - begin)
                .count());
        metrics->metadata_bytes =
            std::filesystem::file_size(manifest_path) +
            std::filesystem::file_size(files / "timeline.ngt") +
            std::filesystem::file_size(files / "samples.ngs") +
            std::filesystem::file_size(files / "parameters.ngp");
    }
    return result;
}

bool relink_project_sample(Project &project,
                           const std::filesystem::path &missing,
                           const std::filesystem::path &replacement,
                           std::string &error) {
    if (!std::filesystem::is_regular_file(replacement)) {
        error = "replacement sample does not exist";
        return false;
    }
    for (auto &[slot, path] : project.samples.sample_paths)
        if (path == missing) {
            path = replacement;
            std::erase(project.missing_assets, missing);
            error.clear();
            return true;
        }
    error = "missing sample reference was not found";
    return false;
}
} // namespace neurogenic
