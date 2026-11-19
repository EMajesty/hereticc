#include "neurogenic/audio/device.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/audio/sample.hpp"
#include "neurogenic/audio/sample_manager.hpp"
#include "neurogenic/core/control_dispatcher.hpp"
#include "neurogenic/midi/input.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/project/project.hpp"
#include "neurogenic/scripting/runtime.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/timeline/io.hpp"
#include "neurogenic/ui/gui.hpp"
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::atomic<bool> running{true};
void stop(int) { running = false; }
void usage() {
    std::cout
        << "usage: neurogenic [OPTIONS] [SAMPLE.wav]\n"
           "  --slot ID=FILE    load FILE into stable numeric sample slot ID "
           "(repeatable)\n"
           "  --map NOTE=ID     map MIDI/sequencer NOTE to a loaded slot "
           "(repeatable)\n"
           "  --audio NAME      select audio output by index or name\n"
           "  --input NAME      open a duplex audio input by index or name\n"
           "  --input-channels N  request 1..6 device input channels\n"
           "  --output-channels N request 1..6 device output channels\n"
           "  --midi NAME       select MIDI input by index or name\n"
           "  --script FILE     Lua action for MIDI note 36\n"
           "  --timeline FILE   load a NEUROGENIC_TIMELINE version 1 file\n"
           "  --project PATH    restore a versioned NEUROGENIC project\n"
           "  --interactive     accept live load/unload/status/quit commands "
           "on stdin\n"
           "  --list-devices    enumerate audio and MIDI devices\n"
           "  --headless        explicitly select null audio\n"
           "  --gui             launch the optional graphical frontend\n"
           "  --fullscreen      launch the graphical frontend fullscreen\n"
           "  --ui-scale SCALE  override GUI scale (1.0..2.5)\n"
           "  --reset-ui-scale  restore automatic GUI scaling\n"
           "  --stats           print one performance.v1 JSON snapshot and "
           "exit\n"
           "  --synth           route the demo track and MIDI notes to the "
           "native synth\n";
}

void print_status(const neurogenic::SampleSlotStatus &state) {
    std::cout << "slot " << state.id.value << " "
              << neurogenic::sample_slot_state_name(state.state);
    if (state.has_sample)
        std::cout << " source=\"" << state.source_path.string() << "\" "
                  << state.source_sample_rate << "->"
                  << state.engine_sample_rate << " Hz " << state.channels
                  << "ch " << state.duration_seconds << "s";
    if (!state.pending_source_path.empty())
        std::cout << " pending=\"" << state.pending_source_path.string() << '"';
    if (!state.error.empty())
        std::cout << " error=\"" << state.error << '"';
    std::cout << '\n';
}

std::pair<std::uint32_t, std::string> assignment(std::string_view value,
                                                 const char *option) {
    const auto separator = value.find('=');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 == value.size())
        throw std::invalid_argument(std::string(option) +
                                    " expects NUMBER=VALUE");
    std::uint32_t number{};
    const auto first = value.data();
    const auto last = first + separator;
    const auto parsed = std::from_chars(first, last, number);
    if (parsed.ec != std::errc{} || parsed.ptr != last)
        throw std::invalid_argument(std::string(option) +
                                    " has an invalid number");
    return {number, std::string(value.substr(separator + 1))};
}
std::uint32_t unsigned_number(std::string_view value, const char *option) {
    std::uint32_t number{};
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument(std::string(option) +
                                    " has an invalid slot ID");
    return number;
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::string wav, audio_name, input_name, midi_name,
            script = "examples/generate.lua", timeline_file, project_file;
        std::vector<std::pair<std::uint32_t, std::filesystem::path>> slot_files;
        std::vector<std::pair<std::uint8_t, std::uint32_t>> note_maps;
        bool list = false, headless = false, interactive = false, gui = false,
             fullscreen = false, reset_ui_scale = false, stats = false,
             synth = false;
        std::optional<float> ui_scale;
        std::uint32_t input_channels{2}, output_channels{2};
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--audio" && i + 1 < argc)
                audio_name = argv[++i];
            else if (arg == "--input" && i + 1 < argc)
                input_name = argv[++i];
            else if (arg == "--input-channels" && i + 1 < argc)
                input_channels = unsigned_number(argv[++i], "--input-channels");
            else if (arg == "--output-channels" && i + 1 < argc)
                output_channels = unsigned_number(argv[++i], "--output-channels");
            else if (arg == "--midi" && i + 1 < argc)
                midi_name = argv[++i];
            else if (arg == "--script" && i + 1 < argc)
                script = argv[++i];
            else if (arg == "--timeline" && i + 1 < argc)
                timeline_file = argv[++i];
            else if (arg == "--project" && i + 1 < argc)
                project_file = argv[++i];
            else if (arg == "--slot" && i + 1 < argc) {
                auto [id, path] = assignment(argv[++i], "--slot");
                slot_files.emplace_back(id, path);
            } else if (arg == "--map" && i + 1 < argc) {
                auto [note, id_text] = assignment(argv[++i], "--map");
                if (note > 127)
                    throw std::invalid_argument("--map note must be 0..127");
                note_maps.emplace_back(static_cast<std::uint8_t>(note),
                                       unsigned_number(id_text, "--map"));
            } else if (arg == "--list-devices")
                list = true;
            else if (arg == "--headless")
                headless = true;
            else if (arg == "--stats") {
                stats = true;
                headless = true;
            } else if (arg == "--synth")
                synth = true;
            else if (arg == "--interactive")
                interactive = true;
            else if (arg == "--gui")
                gui = true;
            else if (arg == "--fullscreen") {
                gui = true;
                fullscreen = true;
            } else if (arg == "--ui-scale" && i + 1 < argc) {
                gui = true;
                ui_scale = std::stof(argv[++i]);
                if (!std::isfinite(*ui_scale) || *ui_scale < 1.0F ||
                    *ui_scale > 2.5F)
                    throw std::invalid_argument("--ui-scale must be 1.0..2.5");
            } else if (arg == "--reset-ui-scale") {
                gui = true;
                reset_ui_scale = true;
            } else if (arg == "--help") {
                usage();
                return 0;
            } else if (!arg.empty() && arg[0] != '-' && wav.empty())
                wav = arg;
            else {
                usage();
                return 2;
            }
        }

        neurogenic::AudioEngine engine;
        neurogenic::SampleManager samples(engine);
        neurogenic::MidiMapper mapper;
        neurogenic::ControlDispatcher dispatcher(mapper);
        neurogenic::AudioDevice audio(engine, headless);
        neurogenic::MidiInput midi(mapper);
        if (list) {
            std::cout << "Audio backend: " << audio.backend()
                      << "\nAudio outputs:\n";
            std::size_t index = 0;
            for (const auto &port : audio.output_capabilities())
                std::cout << "  [" << index++ << "] " << port.name
                          << (port.maximum_channels
                                  ? " (up to " +
                                        std::to_string(port.maximum_channels) +
                                        " ch)"
                                  : " (channel capability unknown)")
                          << '\n';
            if (index == 0)
                std::cout << "  (none discovered)\n";
            std::cout << "Audio inputs:\n";
            index = 0;
            for (const auto &port : audio.input_capabilities())
                std::cout << "  [" << index++ << "] " << port.name
                          << (port.maximum_channels
                                  ? " (up to " +
                                        std::to_string(port.maximum_channels) +
                                        " ch)"
                                  : " (channel capability unknown)")
                          << '\n';
            if (index == 0)
                std::cout << "  (none discovered)\n";
            std::cout << "MIDI backend: " << midi.backend()
                      << "\nMIDI inputs:\n";
            index = 0;
            for (const auto &name : midi.inputs())
                std::cout << "  [" << index++ << "] " << name << '\n';
            if (index == 0)
                std::cout << "  (none discovered)\n";
            if (wav.empty() && slot_files.empty() && !interactive && !gui)
                return 0;
        }
        if (wav.empty() && slot_files.empty() && project_file.empty() &&
            !interactive && !gui && !stats && !synth) {
            usage();
            return 2;
        }

        std::optional<neurogenic::Project> startup_project;
        if (!project_file.empty()) {
            if (!timeline_file.empty() || !wav.empty() || !slot_files.empty())
                throw std::invalid_argument(
                    "--project cannot be combined with sample or timeline inputs");
            startup_project = neurogenic::load_project(project_file);
            for (const auto &[slot, path] :
                 startup_project->samples.sample_paths)
                if (std::filesystem::exists(path))
                    slot_files.emplace_back(slot, path);
        }

        if (slot_files.empty() && !wav.empty())
            engine.set_sample(neurogenic::load_wav(wav, engine.sample_rate()));
        else {
            if (!wav.empty())
                slot_files.emplace_back(0, wav);
            for (const auto &[id, path] : slot_files) {
                if (!samples.load(neurogenic::SampleSlotId{id}, path))
                    throw std::runtime_error("sample command queue is full");
            }
            samples.wait_idle();
            for (const auto &[id, path] : slot_files) {
                (void)path;
                const auto state = samples.status(neurogenic::SampleSlotId{id});
                if (state.state == neurogenic::SampleSlotLoadState::error)
                    throw std::runtime_error("slot " + std::to_string(id) +
                                             " failed: " + state.error);
            }
            for (const auto &[note, id] : note_maps)
                if (!engine.map_note_to_slot(note,
                                             neurogenic::SampleSlotId{id}))
                    throw std::invalid_argument(
                        "--map references an unloaded sample slot");
            std::array<float, 0> no_audio{};
            engine.process(no_audio, no_audio);
            for (const auto &[id, path] : slot_files) {
                (void)path;
                print_status(samples.status(neurogenic::SampleSlotId{id}));
            }
        }

        neurogenic::Timeline editable_timeline;
        if (startup_project)
            editable_timeline = startup_project->timeline;
        else if (timeline_file.empty()) {
            neurogenic::Track track;
            track.id = 1;
            track.name = synth ? "Synth" : "Drums";
            track.instrument_id =
                synth ? std::string(neurogenic::subtractive_instrument_id)
                      : std::string(neurogenic::sample_instrument_id);
            track.pattern.length = neurogenic::ticks_per_quarter * 4;
            neurogenic::EventId id = 1;
            for (const auto tick : {0, 960, 1920, 2880})
                track.pattern.events.push_back(
                    {id++, tick, neurogenic::MusicalEventType::note,
                     static_cast<std::uint8_t>(synth ? 60 : 36), 1.0F, 240, 0,
                     0, 0});
            editable_timeline.tracks.push_back(std::move(track));
        } else
            editable_timeline = neurogenic::load_timeline(timeline_file);
        {
            std::string timeline_error;
            if (!engine.queue_timeline(editable_timeline, timeline_error))
                throw std::runtime_error("timeline queue failed: " +
                                         timeline_error);
        }
        neurogenic::TimelineService timeline_service(
            [&](const auto &timeline, std::string &error) {
                return engine.queue_timeline(timeline, error);
            },
            std::move(editable_timeline));
        neurogenic::ApplicationService application(engine, timeline_service,
                                                   samples);
        if (startup_project) {
            const auto restored = application.execute(
                {"project.open.v1", {{"path", project_file}}});
            if (!restored.accepted)
                throw std::runtime_error("project restore failed: " +
                                         restored.error);
            if (!startup_project->missing_assets.empty())
                std::cerr << "project opened with "
                          << startup_project->missing_assets.size()
                          << " missing sample asset(s)\n";
        }
        if (stats) {
            std::array<float, 256> left{}, right{};
            engine.process(left, right);
            std::cout << neurogenic::performance_json(
                             application.snapshot().performance)
                      << '\n';
            samples.shutdown();
            return 0;
        }
        neurogenic::ScriptRuntime lua(
            [&](auto pattern) { return engine.queue_pattern(pattern); },
            [&] { return engine.frame_position(); }, &timeline_service,
            &application.slices());
        if (synth)
            mapper.set_note_event_handler(
                [&](auto note, auto velocity, bool on) {
                    engine.trigger_synth(note, velocity, on);
                });
        else
            mapper.set_note_handler([&](auto note, auto velocity) {
                engine.trigger(note, velocity);
            });
        mapper.map_button(0, 36, "pattern.generate", [&] {
            std::string error;
            if (lua.execute_file(script, error))
                std::cout << "Lua pattern queued for next bar\n";
            else
                std::cerr << "Lua error: " << error << '\n';
        });
        dispatcher.start();
        std::string error;
        if (input_channels == 0 || input_channels > 6 || output_channels == 0 ||
            output_channels > 6)
            throw std::invalid_argument("audio channel counts must be 1..6");
        const auto audio_started = input_name.empty() && output_channels == 2
                                       ? audio.start(audio_name, error)
                                       : audio.start_duplex(audio_name,
                                                            input_name,
                                                            input_name.empty()
                                                                ? 0
                                                                : input_channels,
                                                            error,
                                                            output_channels);
        if (!audio_started) {
            std::cerr << "audio unavailable: " << error << '\n';
            return 1;
        }
        if (!midi.open(midi_name, error))
            std::cerr << "MIDI unavailable (audio continues): " << error
                      << '\n';
        else
            std::cout << "MIDI input active\n";
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        std::cout << "Playing " << engine.sample_slot_count()
                  << " sample slot(s). MIDI note 36 runs " << script
                  << ". Press Ctrl-C to stop.\n";
        int exit_code{};
        if (gui) {
            neurogenic::GuiOptions gui_options;
            gui_options.fullscreen = fullscreen;
            gui_options.ui_scale = ui_scale;
            gui_options.reset_ui_scale = reset_ui_scale;
            exit_code = neurogenic::run_gui(application, gui_options, error);
            if (exit_code != 0)
                std::cerr << "GUI unavailable: " << error << '\n';
        } else if (interactive) {
            std::cout << "Commands: load ID FILE | replace ID FILE | unload ID "
                         "| status [ID] | record-start ID SOURCE | record-stop "
                         "| record-status | record-save FILE [pcm16|float32] | quit\n";
            std::string line;
            while (running.load() && std::getline(std::cin, line)) {
                std::istringstream input(line);
                std::string command;
                input >> command;
                if (command == "quit" || command == "exit")
                    break;
                if (command == "load" || command == "replace") {
                    std::uint32_t id{};
                    std::string path;
                    input >> id >> std::ws;
                    std::getline(input, path);
                    if (path.empty() ||
                        !samples.load(neurogenic::SampleSlotId{id}, path))
                        std::cerr << "sample command rejected\n";
                } else if (command == "unload") {
                    std::uint32_t id{};
                    if (!(input >> id) ||
                        !samples.unload(neurogenic::SampleSlotId{id}))
                        std::cerr << "unload command rejected\n";
                } else if (command == "status") {
                    std::uint32_t id{};
                    if (input >> id)
                        print_status(
                            samples.status(neurogenic::SampleSlotId{id}));
                    else
                        for (const auto &state : samples.states())
                            print_status(state);
                } else if (command == "record-start") {
                    std::uint32_t slot{};
                    std::string source;
                    input >> slot >> source;
                    const auto select_source = application.execute(
                        {"recording.source-select.v1", {{"source", source}}});
                    const auto select_slot = application.execute(
                        {"recording.destination-select.v1",
                         {{"slot_id", static_cast<std::int64_t>(slot)}}});
                    const auto arm = application.execute({"recording.arm.v1", {}});
                    const auto start =
                        application.execute({"recording.start.v1", {}});
                    if (!select_source.accepted || !select_slot.accepted ||
                        !arm.accepted || !start.accepted)
                        std::cerr << "record command rejected: "
                                  << (!select_source.error.empty()
                                          ? select_source.error
                                          : !select_slot.error.empty()
                                                ? select_slot.error
                                                : !arm.error.empty() ? arm.error
                                                                     : start.error)
                                  << '\n';
                } else if (command == "record-stop") {
                    const auto result =
                        application.execute({"recording.stop.v1", {}});
                    if (!result.accepted)
                        std::cerr << result.error << '\n';
                } else if (command == "record-status") {
                    const auto state = application.snapshot().recording;
                    std::cout << neurogenic::recording_state_name(state.state)
                              << " " << state.captured_frames << "/"
                              << state.capacity_frames << " frames, overflows="
                              << state.overflow_count << '\n';
                } else if (command == "record-save") {
                    std::string path, format{"pcm16"};
                    input >> path >> format;
                    const auto result = application.execute(
                        {"recording.save.v1",
                         {{"path", path}, {"format", format}}});
                    if (!result.accepted)
                        std::cerr << result.error << '\n';
                } else
                    std::cerr << "unknown command\n";
            }
        } else
            while (running.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        audio.stop();
        midi.close();
        dispatcher.stop();
        samples.shutdown();
        std::cout << "Stopped. dropped MIDI=" << mapper.dropped_messages()
                  << ", triggers=" << engine.dropped_triggers()
                  << ", patterns=" << engine.dropped_patterns()
                  << ", timelines=" << engine.dropped_timelines()
                  << ", timeline overruns=" << engine.timeline_event_overruns()
                  << ", legacy events=" << engine.dropped_legacy_events()
                  << ", transport commands="
                  << engine.dropped_transport_commands()
                  << ", missing slots=" << engine.missing_slot_triggers()
                  << ", stolen voices=" << engine.stolen_voices() << '\n';
        return exit_code;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
#include "neurogenic/application/control.hpp"
