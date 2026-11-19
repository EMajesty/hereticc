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
std::string serialize_mixer(const MixerConfiguration &mixer) {
    std::ostringstream out;
    out << "NEUROGENIC_MIXER 1\n" << std::setprecision(9)
        << "master " << mixer.master.gain << ' '
        << static_cast<unsigned>(mixer.master.output_left) << ' '
        << static_cast<unsigned>(mixer.master.output_right) << ' '
        << mixer.master.mute << '\n';
    out << "tracks " << mixer.track_count << '\n';
    for (std::size_t i = 0; i < mixer.track_count; ++i) {
        const auto &c = mixer.tracks[i];
        out << c.track << ' ' << c.gain << ' ' << c.pan << ' ' << c.mute << ' '
            << c.solo << ' ' << static_cast<int>(c.output_pair);
        for (std::size_t a = 0; a < mixer_aux_count; ++a)
            out << ' ' << c.aux_send[a] << ' ' << c.aux_pre_fader[a];
        out << '\n';
    }
    out << "inputs " << mixer.input_count << '\n';
    for (std::size_t i = 0; i < mixer.input_count; ++i) {
        const auto &c = mixer.inputs[i];
        out << static_cast<unsigned>(c.physical_channel) << ' ' << c.gain << ' '
            << c.pan << ' ' << c.mute << ' ' << c.route_to_master;
        for (const auto send : c.aux_send)
            out << ' ' << send;
        out << '\n';
    }
    for (std::size_t a = 0; a < mixer_aux_count; ++a) {
        const auto &bus = mixer.aux[a];
        out << "aux " << a << ' ' << bus.gain << ' ' << bus.return_gain << ' '
            << static_cast<int>(bus.output_pair) << ' ' << bus.mute << ' '
            << bus.effect_enabled << '\n';
    }
    return out.str();
}
MixerConfiguration load_mixer(const std::filesystem::path &path) {
    bounded(path);
    std::ifstream input(path);
    std::string magic, label;
    unsigned version{}, left{}, right{};
    MixerConfiguration result;
    int output{};
    if (!(input >> magic >> version) || magic != "NEUROGENIC_MIXER" ||
        version != 1 || !(input >> label) || label != "master" ||
        !(input >> result.master.gain >> left >> right >> result.master.mute) ||
        left >= maximum_mixer_outputs || right >= maximum_mixer_outputs ||
        !(input >> label >> result.track_count) || label != "tracks" ||
        result.track_count > maximum_mixer_tracks)
        throw std::runtime_error("unsupported or malformed mixer metadata");
    result.master.output_left = static_cast<std::uint8_t>(left);
    result.master.output_right = static_cast<std::uint8_t>(right);
    for (std::size_t i = 0; i < result.track_count; ++i) {
        auto &c = result.tracks[i];
        if (!(input >> c.track >> c.gain >> c.pan >> c.mute >> c.solo >> output))
            throw std::runtime_error("malformed mixer track");
        c.output_pair = static_cast<std::int8_t>(output);
        for (std::size_t a = 0; a < mixer_aux_count; ++a)
            if (!(input >> c.aux_send[a] >> c.aux_pre_fader[a]))
                throw std::runtime_error("malformed mixer send");
    }
    if (!(input >> label >> result.input_count) || label != "inputs" ||
        result.input_count > maximum_mixer_inputs)
        throw std::runtime_error("malformed mixer inputs");
    for (std::size_t i = 0; i < result.input_count; ++i) {
        auto &c = result.inputs[i];
        unsigned physical{};
        if (!(input >> physical >> c.gain >> c.pan >> c.mute >>
              c.route_to_master) || physical >= maximum_mixer_inputs)
            throw std::runtime_error("malformed mixer input");
        c.physical_channel = static_cast<std::uint8_t>(physical);
        for (auto &send : c.aux_send)
            if (!(input >> send))
                throw std::runtime_error("malformed input send");
    }
    for (std::size_t i = 0; i < mixer_aux_count; ++i) {
        std::size_t index{};
        auto &bus = result.aux[i];
        if (!(input >> label >> index >> bus.gain >> bus.return_gain >> output >>
              bus.mute >> bus.effect_enabled) ||
            label != "aux" || index != i)
            throw std::runtime_error("malformed auxiliary bus");
        bus.output_pair = static_cast<std::int8_t>(output);
    }
    std::string error;
    if (!validate_mixer_configuration(result, maximum_mixer_inputs,
                                      maximum_mixer_outputs, error))
        throw std::runtime_error("invalid mixer metadata: " + error);
    result.revision = 1;
    return result;
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
    if (!validate_mixer_configuration(project.mixer, maximum_mixer_inputs,
                                      maximum_mixer_outputs,
                                      validation_error))
        throw std::invalid_argument("invalid project mixer: " +
                                    validation_error);
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
        checked_write(temporary / "mixer.ngm", serialize_mixer(project.mixer));
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
            std::filesystem::file_size(committed / "parameters.ngp") +
            std::filesystem::file_size(committed / "mixer.ngm");
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
    if (std::filesystem::exists(files / "mixer.ngm"))
        result.mixer = load_mixer(files / "mixer.ngm");
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
            std::filesystem::file_size(files / "parameters.ngp") +
            (std::filesystem::exists(files / "mixer.ngm")
                 ? std::filesystem::file_size(files / "mixer.ngm")
                 : 0);
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
