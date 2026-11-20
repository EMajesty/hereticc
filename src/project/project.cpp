#include "neurogenic/project/project.hpp"
#include "neurogenic/timeline/io.hpp"
#include "neurogenic/timeline/playback.hpp"
#include <algorithm>
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
    out << "NEUROGENIC_MIXER 2\n"
        << std::setprecision(9) << "master " << mixer.master.gain << ' '
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
    for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
        const auto &loop = mixer.external_fx[i];
        out << "external " << i << ' ' << loop.id << ' '
            << std::quoted(std::string(loop.name.data())) << ' '
            << static_cast<unsigned>(loop.source) << ' ' << loop.source_id
            << ' ' << static_cast<unsigned>(loop.send_left) << ' '
            << static_cast<unsigned>(loop.send_right) << ' '
            << static_cast<unsigned>(loop.return_left) << ' '
            << static_cast<unsigned>(loop.return_right) << ' ' << loop.send_gain
            << ' ' << loop.return_gain << ' ' << loop.wet << ' '
            << loop.latency_frames << ' ' << loop.enabled << ' ' << loop.bypass
            << ' ' << loop.return_mute << ' ' << loop.mono_send << ' '
            << loop.mono_return << '\n';
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
        (version != 1 && version != 2) || !(input >> label) ||
        label != "master" ||
        !(input >> result.master.gain >> left >> right >> result.master.mute) ||
        left >= maximum_mixer_outputs || right >= maximum_mixer_outputs ||
        !(input >> label >> result.track_count) || label != "tracks" ||
        result.track_count > maximum_mixer_tracks)
        throw std::runtime_error("unsupported or malformed mixer metadata");
    result.master.output_left = static_cast<std::uint8_t>(left);
    result.master.output_right = static_cast<std::uint8_t>(right);
    for (std::size_t i = 0; i < result.track_count; ++i) {
        auto &c = result.tracks[i];
        if (!(input >> c.track >> c.gain >> c.pan >> c.mute >> c.solo >>
              output))
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
              c.route_to_master) ||
            physical >= maximum_mixer_inputs)
            throw std::runtime_error("malformed mixer input");
        c.physical_channel = static_cast<std::uint8_t>(physical);
        for (auto &send : c.aux_send)
            if (!(input >> send))
                throw std::runtime_error("malformed input send");
    }
    for (std::size_t i = 0; i < mixer_aux_count; ++i) {
        std::size_t index{};
        auto &bus = result.aux[i];
        if (!(input >> label >> index >> bus.gain >> bus.return_gain >>
              output >> bus.mute >> bus.effect_enabled) ||
            label != "aux" || index != i)
            throw std::runtime_error("malformed auxiliary bus");
        bus.output_pair = static_cast<std::int8_t>(output);
    }
    if (version >= 2)
        for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
            std::size_t index{};
            unsigned source{}, send_left{}, send_right{}, return_left{},
                return_right{};
            auto &loop = result.external_fx[i];
            std::string name;
            if (!(input >> label >> index >> loop.id >> std::quoted(name) >>
                  source >> loop.source_id >> send_left >> send_right >>
                  return_left >> return_right >> loop.send_gain >>
                  loop.return_gain >> loop.wet >> loop.latency_frames >>
                  loop.enabled >> loop.bypass >> loop.return_mute >>
                  loop.mono_send >> loop.mono_return) ||
                label != "external" || index != i || source > 2 ||
                send_left >= maximum_mixer_outputs ||
                send_right >= maximum_mixer_outputs ||
                return_left >= maximum_mixer_inputs ||
                return_right >= maximum_mixer_inputs)
                throw std::runtime_error("malformed external FX loop");
            if (name.size() >= loop.name.size())
                throw std::runtime_error("external FX loop name is too long");
            std::copy(name.begin(), name.end(), loop.name.begin());
            loop.source = static_cast<ExternalFxSource>(source);
            loop.send_left = static_cast<std::uint8_t>(send_left);
            loop.send_right = static_cast<std::uint8_t>(send_right);
            loop.return_left = static_cast<std::uint8_t>(return_left);
            loop.return_right = static_cast<std::uint8_t>(return_right);
        }
    std::string error;
    if (!validate_mixer_configuration(result, maximum_mixer_inputs,
                                      maximum_mixer_outputs, error))
        throw std::runtime_error("invalid mixer metadata: " + error);
    result.revision = 1;
    return result;
}
std::string serialize_modulation(const ModulationConfiguration &configuration) {
    std::ostringstream out;
    out << "NEUROGENIC_MODULATION 3\n"
        << std::setprecision(9) << "interval " << configuration.update_interval
        << '\n';
    for (std::size_t i = 0; i < configuration.source_count; ++i) {
        const auto &source = configuration.sources[i];
        out << "source " << i << ' ' << static_cast<unsigned>(source.waveform)
            << ' ' << source.rate_hz << ' ' << source.tempo_sync << ' '
            << source.beats_per_cycle << ' ' << source.retrigger << ' '
            << static_cast<unsigned>(source.scope) << ' '
            << source.attack_seconds << ' ' << source.decay_seconds << ' '
            << source.sustain << ' ' << source.release_seconds << ' '
            << source.seed << ' ' << source.follower_attack_seconds << ' '
            << source.follower_release_seconds << ' ' << source.follower_gain
            << ' ' << source.follower_threshold << '\n';
    }
    out << "routes " << configuration.route_count << '\n';
    for (std::size_t i = 0; i < configuration.route_count; ++i) {
        const auto &route = configuration.routes[i];
        out << route.id << ' ' << static_cast<unsigned>(route.source) << ' '
            << static_cast<unsigned>(route.destination.target) << ' '
            << route.destination.instance << ' ' << route.destination.parameter
            << ' ' << route.minimum << ' ' << route.maximum << ' '
            << route.base_value << ' ' << route.depth << ' ' << route.enabled
            << ' ' << static_cast<unsigned>(route.polarity) << ' '
            << static_cast<unsigned>(route.scope) << ' ' << route.track_id
            << '\n';
    }
    return out.str();
}
ModulationConfiguration load_modulation(const std::filesystem::path &path) {
    bounded(path);
    std::ifstream input(path);
    std::string magic, label;
    unsigned version{};
    ModulationConfiguration result;
    if (!(input >> magic >> version) || magic != "NEUROGENIC_MODULATION" ||
        (version == 0 || version > 3) ||
        !(input >> label >> result.update_interval) || label != "interval")
        throw std::runtime_error(
            "unsupported or malformed modulation metadata");
    const auto stored_source_count =
        version >= 3 ? result.source_count : result.source_count - 1;
    for (std::size_t i = 0; i < stored_source_count; ++i) {
        std::size_t index{};
        unsigned waveform{}, scope{};
        auto &source = result.sources[i];
        if (!(input >> label >> index >> waveform >> source.rate_hz >>
              source.tempo_sync >> source.beats_per_cycle >> source.retrigger >>
              scope >> source.attack_seconds >> source.decay_seconds >>
              source.sustain >> source.release_seconds >> source.seed) ||
            label != "source" || index != i ||
            waveform > static_cast<unsigned>(LfoWaveform::random) ||
            scope > static_cast<unsigned>(ModulationScope::voice))
            throw std::runtime_error("malformed modulation source");
        source.waveform = static_cast<LfoWaveform>(waveform);
        source.scope = static_cast<ModulationScope>(scope);
        if (version >= 3 &&
            !(input >> source.follower_attack_seconds >>
              source.follower_release_seconds >> source.follower_gain >>
              source.follower_threshold))
            throw std::runtime_error("malformed modulation follower source");
    }
    if (!(input >> label >> result.route_count) || label != "routes" ||
        result.route_count > result.routes.size())
        throw std::runtime_error("malformed modulation route count");
    for (std::size_t i = 0; i < result.route_count; ++i) {
        unsigned source{}, target{}, polarity{}, scope{};
        auto &route = result.routes[i];
        if (!(input >> route.id >> source >> target) ||
            (version >= 2 && !(input >> route.destination.instance)) ||
            !(input >> route.destination.parameter >> route.minimum >>
              route.maximum >> route.base_value >> route.depth >>
              route.enabled >> polarity >> scope >> route.track_id) ||
            source >= static_cast<unsigned>(ModulationSourceId::count) ||
            target > static_cast<unsigned>(ParameterTarget::external_fx) ||
            polarity > static_cast<unsigned>(ModulationPolarity::unipolar) ||
            scope > static_cast<unsigned>(ModulationScope::voice))
            throw std::runtime_error("malformed modulation route");
        route.source = static_cast<ModulationSourceId>(source);
        route.destination.target = static_cast<ParameterTarget>(target);
        route.polarity = static_cast<ModulationPolarity>(polarity);
        route.scope = static_cast<ModulationScope>(scope);
        if (version == 1 &&
            route.destination.target == ParameterTarget::subtractive_synth &&
            route.scope != ModulationScope::global)
            route.destination.instance = default_synth_instance_id;
    }
    ModulationEngine validation;
    if (!validation.configure(result))
        throw std::runtime_error("invalid modulation configuration");
    return result;
}
std::string serialize_midi(const MidiProjectConfiguration &configuration) {
    std::ostringstream out;
    out << "NEUROGENIC_MIDI 2\nclock "
        << static_cast<unsigned>(configuration.clock_source) << ' '
        << configuration.clock_output_count << ' '
        << configuration.forward_external_clock;
    for (std::size_t i = 0; i < configuration.clock_output_count; ++i)
        out << ' ' << configuration.clock_outputs[i];
    out << "\nroutes " << configuration.route_count << '\n';
    for (std::size_t i = 0; i < configuration.route_count; ++i) {
        const auto &route = configuration.routes[i];
        out << route.id << ' ' << route.input_port << ' ' << route.output_port
            << ' ' << int(route.input_channel) << ' '
            << int(route.output_channel) << ' ' << route.notes << ' '
            << route.control_change << ' ' << route.program_change << ' '
            << route.pitch_bend << ' ' << route.clock << ' ' << route.transport
            << ' ' << route.enabled << '\n';
    }
    for (std::size_t i = 0; i < maximum_midi_ports; ++i)
        out << "port " << i + 1 << ' ' << configuration.output_latency_frames[i]
            << ' ' << std::quoted(configuration.input_selectors[i].data())
            << ' ' << std::quoted(configuration.output_selectors[i].data())
            << '\n';
    return out.str();
}
MidiProjectConfiguration load_midi(const std::filesystem::path &path) {
    bounded(path);
    std::ifstream input(path);
    std::string magic, label;
    unsigned version{}, source{};
    MidiProjectConfiguration result;
    if (!(input >> magic >> version) || magic != "NEUROGENIC_MIDI" ||
        (version != 1 && version != 2) ||
        !(input >> label >> source >> result.clock_output_count) ||
        label != "clock" || source > 1 ||
        result.clock_output_count > maximum_midi_ports)
        throw std::runtime_error("unsupported or malformed MIDI metadata");
    result.clock_source = static_cast<MidiClockSource>(source);
    if (version >= 2 && !(input >> result.forward_external_clock))
        throw std::runtime_error("malformed MIDI forwarding preference");
    for (std::size_t i = 0; i < result.clock_output_count; ++i)
        if (!(input >> result.clock_outputs[i]) || !result.clock_outputs[i] ||
            result.clock_outputs[i] > maximum_midi_ports)
            throw std::runtime_error("malformed MIDI clock output");
    if (!(input >> label >> result.route_count) || label != "routes" ||
        result.route_count > maximum_midi_routes)
        throw std::runtime_error("malformed MIDI route count");
    for (std::size_t i = 0; i < result.route_count; ++i) {
        auto &route = result.routes[i];
        int input_channel{}, output_channel{};
        if (!(input >> route.id >> route.input_port >> route.output_port >>
              input_channel >> output_channel >> route.notes >>
              route.control_change >> route.program_change >>
              route.pitch_bend >> route.clock >> route.transport >>
              route.enabled))
            throw std::runtime_error("malformed MIDI route");
        route.input_channel = static_cast<std::int8_t>(input_channel);
        route.output_channel = static_cast<std::int8_t>(output_channel);
    }
    std::string error;
    if (!validate_midi_routes({result.routes.data(), result.route_count},
                              error))
        throw std::runtime_error("invalid MIDI routes: " + error);
    for (std::size_t i = 0; i < maximum_midi_ports; ++i) {
        std::size_t port{};
        std::string in, out;
        if (!(input >> label >> port >> result.output_latency_frames[i] >>
              std::quoted(in) >> std::quoted(out)) ||
            label != "port" || port != i + 1 || in.size() >= 64 ||
            out.size() >= 64)
            throw std::runtime_error("malformed MIDI port assignment");
        std::copy(in.begin(), in.end(), result.input_selectors[i].begin());
        std::copy(out.begin(), out.end(), result.output_selectors[i].begin());
    }
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
                                      maximum_mixer_outputs, validation_error))
        throw std::invalid_argument("invalid project mixer: " +
                                    validation_error);
    if (project.routing_scenes.size() > 32)
        throw std::invalid_argument("too many routing scenes");
    for (std::size_t i = 0; i < project.routing_scenes.size(); ++i) {
        const auto &scene = project.routing_scenes[i];
        if (!scene.id || scene.name.empty() ||
            !validate_mixer_configuration(scene.mixer, maximum_mixer_inputs,
                                          maximum_mixer_outputs,
                                          validation_error))
            throw std::invalid_argument("invalid routing scene: " +
                                        validation_error);
        for (std::size_t j = 0; j < i; ++j)
            if (project.routing_scenes[j].id == scene.id)
                throw std::invalid_argument("duplicate routing scene ID");
    }
    if (project.active_routing_scene &&
        std::none_of(project.routing_scenes.begin(),
                     project.routing_scenes.end(), [&](const auto &scene) {
                         return scene.id == project.active_routing_scene;
                     }))
        throw std::invalid_argument("active routing scene does not exist");
    ModulationEngine modulation_validation;
    if (!modulation_validation.configure(project.modulation))
        throw std::invalid_argument("invalid project modulation configuration");
    for (std::size_t i = 0; i < project.modulation.route_count; ++i)
        if (!valid_legacy_parameter(
                project.modulation.routes[i].destination.target,
                project.modulation.routes[i].destination.parameter))
            throw std::invalid_argument("invalid modulation destination");
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
        parameters << "NEUROGENIC_PARAMETERS 1\n" << std::setprecision(17);
        for (const auto &[id, value] : project.parameters)
            parameters << std::quoted(id) << ' ' << value << '\n';
        checked_write(temporary / "parameters.ngp", parameters.str());
        checked_write(temporary / "mixer.ngm", serialize_mixer(project.mixer));
        std::ostringstream scenes;
        scenes << "NEUROGENIC_ROUTING_SCENES 1\nactive "
               << project.active_routing_scene << "\ncount "
               << project.routing_scenes.size() << '\n';
        for (const auto &scene : project.routing_scenes) {
            const auto file = "scene-" + std::to_string(scene.id) + ".ngm";
            scenes << scene.id << ' ' << std::quoted(scene.name) << ' '
                   << std::quoted(file) << '\n';
            checked_write(temporary / file, serialize_mixer(scene.mixer));
        }
        checked_write(temporary / "scenes.ngr", scenes.str());
        checked_write(temporary / "modulation.ngm",
                      serialize_modulation(project.modulation));
        checked_write(temporary / "midi.ngm", serialize_midi(project.midi));
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
            std::filesystem::file_size(committed / "mixer.ngm") +
            std::filesystem::file_size(committed / "scenes.ngr") +
            std::filesystem::file_size(committed / "modulation.ngm");
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
    if (std::filesystem::exists(files / "scenes.ngr")) {
        bounded(files / "scenes.ngr");
        std::ifstream scenes(files / "scenes.ngr");
        std::size_t count{};
        if (!(scenes >> magic >> version) ||
            magic != "NEUROGENIC_ROUTING_SCENES" || version != 1 ||
            !(scenes >> kind >> result.active_routing_scene) ||
            kind != "active" || !(scenes >> kind >> count) || kind != "count" ||
            count > 32)
            throw std::runtime_error("unsupported or malformed routing scenes");
        for (std::size_t i = 0; i < count; ++i) {
            RoutingScene scene;
            std::string file;
            if (!(scenes >> scene.id >> std::quoted(scene.name) >>
                  std::quoted(file)) ||
                !scene.id || scene.name.empty() || file.empty() ||
                file.find('/') != std::string::npos ||
                file.find("..") != std::string::npos)
                throw std::runtime_error("malformed routing scene entry");
            scene.mixer = load_mixer(files / file);
            result.routing_scenes.push_back(std::move(scene));
        }
        if (result.active_routing_scene &&
            std::none_of(result.routing_scenes.begin(),
                         result.routing_scenes.end(), [&](const auto &scene) {
                             return scene.id == result.active_routing_scene;
                         }))
            throw std::runtime_error("active routing scene was not found");
    }
    if (std::filesystem::exists(files / "modulation.ngm"))
        result.modulation = load_modulation(files / "modulation.ngm");
    if (std::filesystem::exists(files / "midi.ngm"))
        result.midi = load_midi(files / "midi.ngm");
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
                 : 0) +
            (std::filesystem::exists(files / "scenes.ngr")
                 ? std::filesystem::file_size(files / "scenes.ngr")
                 : 0) +
            (std::filesystem::exists(files / "modulation.ngm")
                 ? std::filesystem::file_size(files / "modulation.ngm")
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
