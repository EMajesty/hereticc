#include "neurogenic/ui/gui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>

namespace neurogenic {
UiPoint window_to_drawable(const DisplayMetrics &display, float x,
                           float y) noexcept {
    return {x * display.pixel_ratio_x, y * display.pixel_ratio_y};
}
namespace {
ApplicationCommand
command(std::string id,
        std::map<std::string, ApplicationValue> arguments = {}) {
    ApplicationCommand result;
    result.action_id = std::move(id);
    result.arguments = std::move(arguments);
    result.source_id = "gui";
    return result;
}
std::uint64_t step_count(const TrackSummary &track, MusicalTick resolution) {
    return static_cast<std::uint64_t>((track.pattern_length + resolution - 1) /
                                      resolution);
}
const TrackSummary *selected_track(const ApplicationState &state) {
    const auto found = std::find_if(
        state.tracks.begin(), state.tracks.end(),
        [&](const auto &track) { return track.id == state.selected_track; });
    return found == state.tracks.end() ? nullptr : &*found;
}
StepResolution next_resolution(StepResolution value) {
    return static_cast<StepResolution>((static_cast<unsigned>(value) + 1) % 8);
}
} // namespace

GuiModel::GuiModel(ApplicationService &application, GuiSettings settings,
                   std::filesystem::path settings_path)
    : application_(application), settings_(settings),
      settings_path_(std::move(settings_path)) {}

std::filesystem::path GuiModel::default_settings_path() {
    if (const auto *xdg = std::getenv("XDG_CONFIG_HOME"))
        return std::filesystem::path(xdg) / "neurogenic/gui.conf";
    if (const auto *home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".config/neurogenic/gui.conf";
    return {};
}

GuiSettings GuiModel::load_settings(const std::filesystem::path &path,
                                    std::string &error) {
    GuiSettings result;
    if (path.empty() || !std::filesystem::exists(path)) {
        error.clear();
        return result;
    }
    std::ifstream input(path);
    std::string key;
    while (input >> key) {
        if (key == "ui_scale") {
            std::string value;
            input >> value;
            if (value != "auto") {
                try {
                    const auto scale = std::stof(value);
                    if (!std::isfinite(scale) || scale < 1.0F || scale > 2.5F)
                        throw std::out_of_range("scale");
                    result.ui_scale = scale;
                } catch (...) {
                    error = "invalid ui_scale in GUI settings; using automatic "
                            "scale";
                    result.ui_scale.reset();
                }
            }
        } else if (key == "fullscreen") {
            int value{};
            if (!(input >> value) || (value != 0 && value != 1)) {
                error = "invalid fullscreen value in GUI settings; using "
                        "windowed mode";
                result.fullscreen = false;
                input.clear();
            } else
                result.fullscreen = value != 0;
        } else {
            std::string ignored;
            std::getline(input, ignored);
        }
    }
    return result;
}

bool GuiModel::save_settings(std::string &error) const {
    if (settings_path_.empty()) {
        error.clear();
        return true;
    }
    try {
        std::filesystem::create_directories(settings_path_.parent_path());
        std::ofstream output(settings_path_, std::ios::trunc);
        if (!output)
            throw std::runtime_error("cannot open GUI settings file");
        output << "ui_scale ";
        if (settings_.ui_scale)
            output << *settings_.ui_scale;
        else
            output << "auto";
        output << "\nfullscreen " << (settings_.fullscreen ? 1 : 0) << '\n';
        error.clear();
        return true;
    } catch (const std::exception &exception) {
        error = exception.what();
        return false;
    }
}

float GuiModel::effective_scale(int width, int height) const noexcept {
    if (settings_.ui_scale)
        return std::clamp(*settings_.ui_scale, 1.0F, 2.5F);
    if (width >= 1800 && height >= 1100)
        return 1.5F;
    if (width >= 1400 && height >= 850)
        return 1.25F;
    return 1.0F;
}

bool GuiModel::set_ui_scale(std::optional<float> scale, std::string &error) {
    if (scale && (!std::isfinite(*scale) || *scale < 1.0F || *scale > 2.5F)) {
        error = "UI scale must be between 1.0 and 2.5";
        return false;
    }
    settings_.ui_scale = scale;
    ++ui_revision_;
    frame_.ui_revision = 0;
    return save_settings(error);
}

bool GuiModel::adjust_ui_scale(float delta, std::string &error) {
    const auto current = settings_.ui_scale.value_or(
        frame_.layout.scale >= 1.0F ? frame_.layout.scale : 1.0F);
    return set_ui_scale(std::clamp(current + delta, 1.0F, 2.5F), error);
}

GuiLayout GuiModel::layout(int width, int height) const {
    GuiLayout result;
    result.scale = effective_scale(width, height);
    const float logical_width = width / result.scale;
    const float logical_height = height / result.scale;
    result.logical_width = logical_width;
    result.logical_height = logical_height;
    result.breakpoint = logical_width >= 1180 && logical_height >= 700
                            ? GuiBreakpoint::wide
                        : logical_height >= 680 ? GuiBreakpoint::compact
                                                : GuiBreakpoint::legacy;
    result.inspector_visible = logical_width >= 900;
    auto r = [&](float x, float y, float w, float h) {
        return UiRect{x * result.scale, y * result.scale, w * result.scale,
                      h * result.scale};
    };
    const float top_height = 64, nav_height = 54, margin = 16;
    const float content_y = top_height + nav_height + 12;
    const float content_height =
        std::max(360.0F, logical_height - content_y - margin);
    const float inspector_width =
        result.inspector_visible
            ? (result.breakpoint == GuiBreakpoint::wide ? 300.0F : 232.0F)
            : 0.0F;
    const float gap = result.inspector_visible ? 16.0F : 0.0F;
    result.top = r(0, 0, logical_width, top_height);
    result.nav = r(0, top_height, logical_width, nav_height);
    result.content =
        r(margin, content_y, logical_width - margin * 2 - inspector_width - gap,
          content_height);
    result.parameters = result.inspector_visible
                            ? r(logical_width - margin - inspector_width,
                                content_y, inspector_width, content_height)
                            : UiRect{};
    const float tab_width = (logical_width - 32) / 11;
    for (int i = 0; i < 11; ++i)
        result.tabs.push_back(r(16 + i * tab_width, 72, tab_width - 8, 38));
    result.page_previous = r(28, content_y + 12, 64, 42);
    result.page_next = r(100, content_y + 12, 64, 42);
    result.resolution = r(172, content_y + 12, 100, 42);
    result.target_note = r(280, content_y + 12, 100, 42);
    result.instrument = r(388, content_y + 12, 180, 42);
    const float row_y = content_y + 66;
    const float row_width = result.content.w / result.scale - 24;
    for (int i = 0; i < 8; ++i)
        result.rows.push_back(r(28, row_y + i * 46, row_width, 38));
    const float grid_top = result.breakpoint == GuiBreakpoint::legacy
                               ? content_y + 230
                               : content_y + 250;
    const float grid_width = result.content.w / result.scale - 24;
    const float cell_gap = 8;
    const float cell = (grid_width - cell_gap * 7) / 8;
    const float grid_height = std::max(
        52.0F,
        std::min(92.0F, (logical_height - grid_top - margin - cell_gap) / 2));
    for (int i = 0; i < 16; ++i)
        result.grid.push_back(r(28 + (i % 8) * (cell + cell_gap),
                                grid_top + (i / 8) * (grid_height + cell_gap),
                                cell, grid_height));
    result.scale_down = r(28, content_y + 90, 100, 48);
    result.scale_up = r(136, content_y + 90, 100, 48);
    result.scale_reset = r(244, content_y + 90, 136, 48);
    result.fullscreen = r(28, content_y + 150, 208, 48);
    return result;
}

const GuiFrame &GuiModel::frame(int width, int height) {
    const auto state = application_.snapshot();
    if (frame_.state_revision != state.revision ||
        frame_.performance_generation != state.performance.generation ||
        frame_.ui_revision != ui_revision_ || frame_.viewport_width != width ||
        frame_.viewport_height != height) {
        frame_.state_revision = state.revision;
        frame_.performance_generation = state.performance.generation;
        frame_.viewport_width = width;
        frame_.viewport_height = height;
        frame_.ui_revision = ui_revision_;
        frame_.layout = layout(width, height);
        render(state, frame_.layout);
    }
    return frame_;
}
void GuiModel::submit(ApplicationCommand value) {
    auto result = application_.execute(value);
    error_ = result.accepted ? std::string{} : result.error;
    frame_.state_revision = std::numeric_limits<std::uint64_t>::max();
}

void GuiModel::render(const ApplicationState &state, const GuiLayout &l) {
    frame_.commands.clear();
    auto rect = [&](UiRect r, UiColor c) {
        frame_.commands.push_back({DrawCommand::Type::rectangle, r, c, {}});
    };
    auto text = [&](UiRect r, UiColor c, std::string value) {
        frame_.commands.push_back(
            {DrawCommand::Type::text, r, c, std::move(value)});
    };
    auto line = [&](float x1, float y1, float x2, float y2, UiColor color) {
        frame_.commands.push_back(
            {DrawCommand::Type::line, {x1, y1, x2, y2}, color, {}});
    };
    rect({0, 0, static_cast<float>(frame_.viewport_width),
          static_cast<float>(frame_.viewport_height)},
         UiColor::background);
    rect(l.top, UiColor::panel);
    text({l.top.x + 16 * l.scale, l.top.y + 12 * l.scale,
          l.top.w - 32 * l.scale, 30 * l.scale},
         UiColor::text,
         "NEUROGENIC   " +
             std::to_string(static_cast<int>(std::round(state.tempo_bpm))) +
             " BPM   " + std::to_string(state.time_signature_numerator) + "/" +
             std::to_string(state.time_signature_denominator) + "   " +
             (state.playing ? "PLAY" : "STOP") + "   TRACK " +
             std::to_string(state.selected_track) + "   " +
             pad_mode_name(state.pad_mode));
    const auto &perf = state.performance;
    text({l.top.x + l.top.w - 510 * l.scale, l.top.y + 38 * l.scale,
          500 * l.scale, 20 * l.scale},
         UiColor::muted,
         "AUDIO " +
             std::to_string(static_cast<int>(perf.audio.current_load_percent)) +
             "%  CPU " +
             (perf.system.process_cpu_available
                  ? std::to_string(
                        static_cast<int>(perf.system.process_cpu_percent))
                  : "n/a") +
             "%  MEM " +
             (perf.system.resident_memory_available
                  ? std::to_string(perf.system.resident_memory_bytes /
                                   (1024 * 1024))
                  : "n/a") +
             " MiB  VOICES " + std::to_string(perf.engine.active_voices) + "/" +
             std::to_string(perf.engine.maximum_voices));
    const char *names[]{"SEQUENCER", "SAMPLES", "PARAMETERS", "X/Y",
                        "PERF",      "DISPLAY", "RECORD",     "MIXER",
                        "ROUTING",   "MOD",     "MIDI"};
    for (std::size_t i = 0; i < l.tabs.size(); ++i) {
        rect(l.tabs[i], i == static_cast<std::size_t>(view_) ? UiColor::selected
                                                             : UiColor::panel);
        text(l.tabs[i], UiColor::text, names[i]);
    }
    rect(l.content, UiColor::panel);
    if (l.inspector_visible)
        rect(l.parameters, UiColor::panel);
    if (view_ == GuiView::sequencer) {
        const auto *track = selected_track(state);
        text({l.content.x + 12 * l.scale, l.content.y + 8 * l.scale,
              l.content.w - 24 * l.scale, 28 * l.scale},
             UiColor::accent,
             "TIMELINE / PAGE " + std::to_string(state.step_page + 1));
        for (const auto control : {l.page_previous, l.page_next, l.resolution,
                                   l.target_note, l.instrument})
            rect(control, UiColor::background);
        text(l.page_previous, UiColor::text, "< PAGE");
        text(l.page_next, UiColor::text, "PAGE >");
        text(l.resolution, UiColor::text,
             step_resolution_name(state.step_resolution));
        text(l.target_note, UiColor::text,
             "NOTE " + std::to_string(track ? track->target_note : 0));
        text(l.instrument, UiColor::text,
             track ? track->instrument_id + " #" +
                         std::to_string(track->instrument_instance_id)
                   : "NO INSTRUMENT");
        for (std::size_t i = 0;
             i < std::min<std::size_t>(3, state.tracks.size()); ++i) {
            const auto &item = state.tracks[i];
            rect(l.rows[i], item.id == state.selected_track
                                ? UiColor::selected
                                : UiColor::background);
            text(l.rows[i], item.muted ? UiColor::muted : UiColor::text,
                 std::to_string(item.id) + "  " + item.name + "  " +
                     item.instrument_id + "  " +
                     "#" + std::to_string(item.instrument_instance_id) + "  " +
                     std::to_string(item.pattern_length) + " ticks" +
                     (item.muted ? "  MUTED" : ""));
        }
        const auto steps = track ? step_count(*track, state.step_ticks) : 0;
        const auto playhead =
            track && track->pattern_length > 0
                ? static_cast<std::uint64_t>(
                      (state.transport_tick % track->pattern_length) /
                      state.step_ticks)
                : 0;
        for (std::size_t i = 0; i < l.grid.size(); ++i) {
            const auto step = state.step_page * 16 + i;
            const auto tick = static_cast<MusicalTick>(step) * state.step_ticks;
            const bool valid = step < steps;
            const bool active =
                valid &&
                std::any_of(track->events.begin(), track->events.end(),
                            [&](const MusicalEvent &event) {
                                return event.type == MusicalEventType::note &&
                                       event.tick == tick &&
                                       event.pitch == track->target_note;
                            });
            const bool locked =
                valid &&
                std::any_of(track->events.begin(), track->events.end(),
                            [&](const MusicalEvent &event) {
                                return event.type ==
                                           MusicalEventType::parameter &&
                                       event.tick == tick;
                            });
            const bool expressive =
                valid &&
                std::any_of(track->events.begin(), track->events.end(),
                            [&](const MusicalEvent &event) {
                                return event.type == MusicalEventType::note &&
                                       event.tick == tick &&
                                       (event.trigger_probability < 1.0F ||
                                        event.trigger_condition !=
                                            TriggerCondition::always ||
                                        event.ratchet_count > 1);
                            });
            UiColor color =
                !valid   ? UiColor::unavailable
                : active ? UiColor::accent
                : state.selected_step == static_cast<std::int64_t>(step)
                    ? UiColor::selected
                    : UiColor::background;
            rect(l.grid[i], color);
            text(l.grid[i],
                 step == playhead ? UiColor::selected : UiColor::text,
                 std::to_string(step + 1) + (locked ? " L" : "") +
                     (expressive ? " *" : "") + (step == playhead ? " >" : ""));
        }
    } else if (view_ == GuiView::samples) {
        text({l.content.x + 12, l.content.y + 8, l.content.w, 28 * l.scale},
             UiColor::accent,
             "SAMPLE " + std::to_string(state.selected_sample_slot.value) +
                 " / SLICES " + std::to_string(state.slices.size()) +
                 "   ENTER preview   SHIFT+ENTER 16-way slice");
        const UiRect wave{l.content.x + 16 * l.scale,
                          l.content.y + 46 * l.scale,
                          l.content.w - 32 * l.scale,
                          std::min(280 * l.scale, l.content.h * 0.48F)};
        rect(wave, UiColor::background);
        const auto center = wave.y + wave.h * 0.5F;
        const auto visible_frames =
            state.sample_frames == 0
                ? std::uint64_t{1}
                : std::max<std::uint64_t>(1,
                                          state.sample_frames / waveform_zoom_);
        const auto visible_start = static_cast<std::uint64_t>(
            waveform_offset_ * (state.sample_frames - visible_frames));
        const auto waveform_x = [&](std::uint64_t frame) {
            return wave.x +
                   wave.w * (static_cast<float>(frame - visible_start) /
                             static_cast<float>(visible_frames));
        };
        if (!state.waveform_peaks.empty()) {
            for (std::size_t i = 0; i < state.waveform_peaks.size(); ++i) {
                const auto &peak = state.waveform_peaks[i];
                const auto peak_frame =
                    state.sample_frames * i / state.waveform_peaks.size();
                if (peak_frame < visible_start ||
                    peak_frame >= visible_start + visible_frames)
                    continue;
                const auto x = waveform_x(peak_frame);
                if (state.sample_channels > 1) {
                    const auto left_center = wave.y + wave.h * 0.25F;
                    const auto right_center = wave.y + wave.h * 0.75F;
                    line(x, left_center - peak.left_max * wave.h * 0.2F, x,
                         left_center - peak.left_min * wave.h * 0.2F,
                         UiColor::accent);
                    line(x, right_center - peak.right_max * wave.h * 0.2F, x,
                         right_center - peak.right_min * wave.h * 0.2F,
                         UiColor::accent);
                } else
                    line(x, center - peak.left_max * wave.h * 0.42F, x,
                         center - peak.left_min * wave.h * 0.42F,
                         UiColor::accent);
            }
        }
        if (state.sample_frames > 0)
            for (const auto &slice : state.slices) {
                if (slice.end_frame < visible_start ||
                    slice.start_frame > visible_start + visible_frames)
                    continue;
                const auto sx =
                    waveform_x(std::max(slice.start_frame, visible_start));
                const auto ex = waveform_x(
                    std::min(slice.end_frame, visible_start + visible_frames));
                const auto color = slice.id == state.selected_slice
                                       ? UiColor::selected
                                       : UiColor::text;
                line(sx, wave.y, sx, wave.y + wave.h, color);
                line(ex, wave.y, ex, wave.y + wave.h, color);
            }
        const auto list_y = wave.y + wave.h + 12 * l.scale;
        for (std::size_t i = 0;
             i < std::min<std::size_t>(4, state.slices.size()); ++i) {
            UiRect row{wave.x, list_y + i * 42 * l.scale, wave.w, 34 * l.scale};
            const auto &slice = state.slices[i];
            rect(row, slice.id == state.selected_slice ? UiColor::selected
                                                       : UiColor::background);
            text(row, UiColor::text,
                 slice.name + "  " + std::to_string(slice.start_frame) + ".." +
                     std::to_string(slice.end_frame));
        }
        if (l.inspector_visible) {
            text({l.parameters.x + 12 * l.scale, l.parameters.y + 12 * l.scale,
                  l.parameters.w - 24 * l.scale, 28 * l.scale},
                 UiColor::accent, "PAD PLAYBACK");
            const auto &p = state.selected_pad_settings;
            const std::string values[]{
                "PAD " + std::to_string(state.selected_pad + 1) + " / " +
                    sample_playback_mode_name(p.mode),
                "CHOKE " + std::to_string(p.choke_group) +
                    (p.choke_release ? " release" : " immediate"),
                "PITCH " + std::to_string(p.semitones) + " st",
                "FINE " + std::to_string(p.fine_cents) + " ct",
                "GAIN " + std::to_string(p.gain),
                std::string("REVERSE ") + (p.reverse ? "ON" : "OFF"),
                "ATTACK " + std::to_string(p.attack_seconds) + " s",
                "DECAY " + std::to_string(p.decay_seconds) + " s",
                "SUSTAIN " + std::to_string(p.sustain),
                "RELEASE " + std::to_string(p.release_seconds) + " s",
                std::string("STRETCH ") + stretch_mode_name(p.stretch_mode),
                "TIME " + std::to_string(state.effective_stretch_ratio) + "x",
                "SOURCE " + std::to_string(p.source_beats) + " beats",
                "SOURCE " + std::to_string(p.source_bpm) + " BPM",
                std::string("QUALITY ") +
                    stretch_quality_name(p.stretch_quality) + " / " +
                    std::to_string(state.stretch_latency_frames) + " fr"};
            static constexpr std::string_view properties[]{
                "mode",    "choke",      "pitch",          "fine",
                "gain",    "reverse",    "attack",         "decay",
                "sustain", "release",    "stretch-mode",   "stretch-ratio",
                "beats",   "source-bpm", "stretch-quality"};
            for (std::size_t i = 0; i < 15; ++i) {
                UiRect row{l.parameters.x + 12 * l.scale,
                           l.parameters.y + (48 + i * 27) * l.scale,
                           l.parameters.w - 24 * l.scale, 23 * l.scale};
                rect(row, state.focused_element == "pad-playback" &&
                                  state.selected_pad_property == properties[i]
                              ? UiColor::selected
                              : UiColor::background);
                text(row, UiColor::text, values[i]);
            }
            const auto assigned =
                state.slice_pad_assignments[state.selected_pad];
            text({l.parameters.x + 12 * l.scale,
                  l.parameters.y + l.parameters.h - 42 * l.scale,
                  l.parameters.w - 24 * l.scale, 30 * l.scale},
                 assigned ? UiColor::accent : UiColor::muted,
                 assigned ? "SLICE " + std::to_string(*assigned)
                          : "DEFAULT SLOT MAPPING");
        }
    } else if (view_ == GuiView::parameters) {
        text({l.content.x + 12, l.content.y + 8, l.content.w, 28 * l.scale},
             UiColor::accent, "APPLICATION PARAMETERS");
        for (std::size_t i = 0;
             i < std::min(l.rows.size(), state.parameters.size()); ++i) {
            const auto &p = state.parameters[i];
            rect(l.rows[i], i == parameter_index_ ? UiColor::selected
                                                  : UiColor::background);
            std::string value =
                std::holds_alternative<double>(p.current_value)
                    ? std::to_string(std::get<double>(p.current_value))
                    : "value";
            text(l.rows[i], UiColor::text,
                 p.name + "  " + value + " " + p.unit);
        }
    } else if (view_ == GuiView::xy) {
        text({l.content.x + 12, l.content.y + 8, l.content.w, 28 * l.scale},
             UiColor::accent, "X/Y PERFORMANCE");
        UiRect pad{l.content.x + 60 * l.scale, l.content.y + 56 * l.scale,
                   std::min(360 * l.scale, l.content.w - 120 * l.scale),
                   360 * l.scale};
        rect(pad, UiColor::background);
        double x = .5, y = .5;
        for (const auto &p : state.parameters) {
            if (p.id == "performance.xy.x")
                x = std::get<double>(p.current_value);
            if (p.id == "performance.xy.y")
                y = std::get<double>(p.current_value);
        }
        rect({pad.x + static_cast<float>(x) * pad.w - 10 * l.scale,
              pad.y + static_cast<float>(y) * pad.h - 10 * l.scale,
              20 * l.scale, 20 * l.scale},
             UiColor::accent);
    } else if (view_ == GuiView::performance) {
        const auto micros = [](std::uint64_t ns) {
            return std::to_string(ns / 1000) + " us";
        };
        text({l.content.x + 12 * l.scale, l.content.y + 8 * l.scale,
              l.content.w - 24 * l.scale, 28 * l.scale},
             UiColor::accent, "PERFORMANCE MONITOR");
        const std::string rows[]{
            "Audio load current / average / peak: " +
                std::to_string(perf.audio.current_load_percent) + "% / " +
                std::to_string(perf.audio.average_load_percent) + "% / " +
                std::to_string(perf.audio.peak_load_percent) + "%",
            "Callback current / average / peak: " +
                micros(perf.audio.current_nanoseconds) + " / " +
                micros(perf.audio.average_nanoseconds) + " / " +
                micros(perf.audio.peak_nanoseconds),
            "Deadline: " + micros(perf.audio.deadline_nanoseconds) +
                "  measured overruns: " +
                std::to_string(perf.audio.deadline_overruns) +
                " (not backend XRUNs)",
            "Audio: " + std::to_string(perf.engine.sample_rate) + " Hz / " +
                std::to_string(perf.engine.buffer_frames) + " frames  voices " +
                std::to_string(perf.engine.active_voices) + "/" +
                std::to_string(perf.engine.maximum_voices),
            "Process CPU: " +
                (perf.system.process_cpu_available
                     ? std::to_string(perf.system.process_cpu_percent) +
                           "% of one core"
                     : "unavailable") +
                "  System CPU: " +
                (perf.system.system_cpu_available
                     ? std::to_string(perf.system.system_cpu_percent) + "%"
                     : "unavailable"),
            "Resident / system memory: " +
                (perf.system.resident_memory_available
                     ? std::to_string(perf.system.resident_memory_bytes /
                                      (1024 * 1024)) +
                           " MiB"
                     : "unavailable") +
                " / " +
                (perf.system.total_memory_available
                     ? std::to_string(perf.system.total_memory_bytes /
                                      (1024 * 1024)) +
                           " MiB"
                     : "unavailable"),
            "Queues trig/timeline/DSP " +
                std::to_string(perf.engine.dropped_triggers) + "/" +
                std::to_string(perf.engine.dropped_timelines) + "/" +
                std::to_string(perf.engine.dropped_dsp_commands) + "  synth " +
                std::to_string(perf.engine.active_synth_voices) + "/" +
                std::to_string(perf.engine.maximum_synth_voices) + " steals " +
                std::to_string(perf.engine.synth_stolen_voices) + "  inst/fx " +
                micros(perf.engine.instrument_nanoseconds) + "/" +
                micros(perf.engine.effect_nanoseconds),
            "Recording " +
                std::to_string(perf.engine.recording_captured_frames) + "/" +
                std::to_string(perf.engine.recording_capacity_frames) +
                " frames, dropped " +
                std::to_string(perf.engine.recording_dropped_frames) +
                "  Backend XRUNs unavailable"};
        for (std::size_t i = 0; i < std::size(rows) && i < l.rows.size(); ++i) {
            rect(l.rows[i], UiColor::background);
            text(l.rows[i], UiColor::text, rows[i]);
        }
    } else if (view_ == GuiView::mixer) {
        text({l.content.x + 12 * l.scale, l.content.y + 8 * l.scale,
              l.content.w - 24 * l.scale, 28 * l.scale},
             UiColor::accent, "TRACK MIXER / TWO AUX BUSES");
        const auto count = state.mixer.configuration.track_count;
        for (std::size_t i = 0; i < std::min(count, l.rows.size()); ++i) {
            const auto &channel = state.mixer.configuration.tracks[i];
            const auto &meter = state.mixer.tracks[i];
            rect(l.rows[i], i == state.selected_mixer_channel
                                ? UiColor::selected
                                : UiColor::background);
            text(l.rows[i], channel.mute ? UiColor::muted : UiColor::text,
                 "T" + std::to_string(channel.track) + "  G " +
                     std::to_string(channel.gain) + "  P " +
                     std::to_string(channel.pan) + "  A " +
                     std::to_string(channel.aux_send[0]) + "/" +
                     std::to_string(channel.aux_send[1]) + "  METER " +
                     std::to_string(std::max(meter.left, meter.right)) +
                     (channel.mute ? "  MUTE" : "") +
                     (channel.solo ? "  SOLO" : ""));
        }
        if (l.inspector_visible)
            text({l.parameters.x + 12 * l.scale, l.parameters.y + 48 * l.scale,
                  l.parameters.w - 24 * l.scale, 180 * l.scale},
                 UiColor::text,
                 "UP/DOWN channel\nLEFT/RIGHT gain\nENTER mute\nSHIFT+ENTER "
                 "solo\n\nMASTER " +
                     std::to_string(state.mixer.configuration.master.gain) +
                     "  PEAK " +
                     std::to_string(std::max(state.mixer.master.left,
                                             state.mixer.master.right)));
    } else if (view_ == GuiView::routing) {
        text({l.content.x + 12 * l.scale, l.content.y + 8 * l.scale,
              l.content.w - 24 * l.scale, 28 * l.scale},
             UiColor::accent, "EXTERNAL FX / ROUTING SCENES");
        for (std::size_t i = 0; i < mixer_external_fx_count; ++i) {
            const auto &loop = state.mixer.configuration.external_fx[i];
            const auto &meter = state.mixer.external_returns[i];
            rect(l.rows[i], i == state.selected_external_fx_loop
                                ? UiColor::selected
                                : UiColor::background);
            text(l.rows[i], loop.enabled ? UiColor::text : UiColor::muted,
                 std::string(loop.name.data()) + " [" +
                     std::to_string(loop.id) + "]" +
                     (loop.enabled ? " ON" : " OFF") + "  SEND " +
                     std::to_string(loop.send_left + 1) + "/" +
                     std::to_string(loop.send_right + 1) + " RETURN " +
                     std::to_string(loop.return_left + 1) + "/" +
                     std::to_string(loop.return_right + 1) + " WET " +
                     std::to_string(loop.wet) + " LAT " +
                     std::to_string(loop.latency_frames) + "f  LEVEL " +
                     std::to_string(std::max(meter.left, meter.right)));
        }
        for (std::size_t i = 0;
             i < state.routing_scenes.size() && i + 2 < l.rows.size(); ++i) {
            const auto &scene = state.routing_scenes[i];
            rect(l.rows[i + 2], scene.id == state.active_routing_scene
                                    ? UiColor::selected
                                    : UiColor::background);
            text(l.rows[i + 2], UiColor::text,
                 "SCENE " + std::to_string(scene.id) + "  " + scene.name);
        }
        if (l.inspector_visible)
            text({l.parameters.x + 12 * l.scale, l.parameters.y + 48 * l.scale,
                  l.parameters.w - 24 * l.scale, 220 * l.scale},
                 UiColor::text,
                 "UP/DOWN select loop\nLEFT/RIGHT wet/dry\nENTER "
                 "enable/disable\n\n"
                 "Scenes publish one validated snapshot.\nUnavailable channels "
                 "stay silent.\n"
                 "Latency compensation is manual.");
    } else if (view_ == GuiView::recording) {
        const auto &recording = state.recording;
        text({l.content.x + 12 * l.scale, l.content.y + 8 * l.scale,
              l.content.w - 24 * l.scale, 28 * l.scale},
             UiColor::accent, "AUDIO RECORDING");
        const auto usage =
            recording.capacity_frames
                ? 100.0 * recording.captured_frames / recording.capacity_frames
                : 0.0;
        const std::string rows[]{
            "State: " + std::string(recording_state_name(recording.state)),
            "Source: " + std::string(recording_source_name(
                             recording.configuration.source)),
            "Destination slot: " +
                std::to_string(recording.configuration.destination.value) +
                "  Channels: " +
                std::to_string(recording.configuration.channels),
            "Captured: " + std::to_string(recording.captured_frames) +
                " frames / " + std::to_string(usage) + "% buffer",
            "Peak L/R: " + std::to_string(recording.peak_left) + " / " +
                std::to_string(recording.peak_right),
            "Overflow / dropped: " + std::to_string(recording.overflow_count) +
                " / " + std::to_string(recording.dropped_frames),
            "Enter: arm/start/stop   Escape: cancel"};
        for (std::size_t i = 0; i < std::size(rows) && i < l.rows.size(); ++i) {
            rect(l.rows[i], UiColor::background);
            text(l.rows[i], UiColor::text, rows[i]);
        }
    } else if (view_ == GuiView::settings) {
        text({l.content.x + 12 * l.scale, l.content.y + 8 * l.scale,
              l.content.w - 24 * l.scale, 28 * l.scale},
             UiColor::accent, "DISPLAY SETTINGS");
        for (const auto control :
             {l.scale_down, l.scale_up, l.scale_reset, l.fullscreen})
            rect(control, UiColor::background);
        text(l.scale_down, UiColor::text, "SCALE -");
        text(l.scale_up, UiColor::text, "SCALE +");
        text(l.scale_reset, UiColor::text, "RESET AUTO");
        text(l.fullscreen, UiColor::text,
             settings_.fullscreen ? "WINDOWED" : "FULLSCREEN");
        text({l.content.x + 12 * l.scale, l.content.y + 220 * l.scale,
              l.content.w - 24 * l.scale, 180 * l.scale},
             UiColor::text,
             "UI scale: " +
                 (settings_.ui_scale ? std::to_string(*settings_.ui_scale)
                                     : std::string("automatic (current ") +
                                           std::to_string(l.scale) + ")") +
                 "\nKeyboard: - / + / 0, F11\nEncoder: Up/Down, Enter toggles "
                 "fullscreen\n\nPROJECT: " +
                 state.project_name + (state.project_dirty ? " *" : "") + "\n" +
                 (state.project_path.empty() ? "Not saved"
                                             : state.project_path.string()) +
                 "\nMissing assets: " +
                 std::to_string(state.missing_assets.size()) +
                 "  Shift+Enter: Save");
    } else if (view_ == GuiView::modulation) {
        text({l.content.x + 12 * l.scale, l.content.y + 8 * l.scale,
              l.content.w - 24 * l.scale, 28 * l.scale},
             UiColor::accent, "MODULATION MATRIX");
        const auto source_name = [](ModulationSourceId source) {
            static constexpr const char *names[]{
                "LFO 1", "LFO 2", "LFO 3", "LFO 4", "ENV", "S&H",
                "SMOOTH", "X", "Y", "VELOCITY", "MIDI CC"};
            return names[static_cast<std::size_t>(source)];
        };
        const auto destination_name = [&](ParameterDestination destination) {
            for (const auto &parameter : state.parameters) {
                const auto resolved =
                    resolve_parameter_destination(parameter.id);
                if (resolved && resolved->target == destination.target &&
                    resolved->parameter == destination.parameter)
                    return parameter.name;
            }
            return std::string("Unavailable destination");
        };
        for (std::size_t i = 0;
             i < std::min(state.modulation.route_count, l.rows.size()); ++i) {
            const auto &route = state.modulation.routes[i];
            rect(l.rows[i], route.id == state.selected_modulation_route
                                ? UiColor::selected
                                : UiColor::background);
            text(l.rows[i], route.enabled ? UiColor::text : UiColor::muted,
                 std::string(source_name(route.source)) + " -> " +
                     destination_name(route.destination) + "  " +
                     std::to_string(route.depth) +
                     (route.scope == ModulationScope::track
                          ? "  TRACK " + std::to_string(route.track_id)
                          : (route.scope == ModulationScope::voice
                                 ? "  VOICE"
                                 : "  GLOBAL")) +
                     (route.destination.instance == 0
                          ? " / ALL"
                          : " / INST " +
                                std::to_string(route.destination.instance)) +
                     "  EFF " + std::to_string(
                         state.modulation_runtime.effective_values[i]));
        }
        if (state.modulation.route_count == 0)
            text(l.rows[0], UiColor::muted,
                 "No routes. Use semantic route actions or a controller.");
    } else if (view_ == GuiView::midi) {
        text({l.content.x + 12 * l.scale, l.content.y + 8 * l.scale,
              l.content.w - 24 * l.scale, 28 * l.scale},
             UiColor::accent, "MIDI ROUTING / CLOCK");
        const auto &clock = state.external_midi_clock;
        const std::array<std::string, 5> rows{
            "CLOCK SOURCE  " +
                std::string(state.midi.clock_source == MidiClockSource::internal
                                ? "INTERNAL"
                                : "EXTERNAL"),
            "EXTERNAL  " + std::string(clock.running ? "RUNNING" : "STOPPED") +
                "  " + (clock.available ? std::to_string(clock.tempo_bpm)
                                             : std::string("NO CLOCK")) +
                (clock.dropout ? "  DROPOUT" : ""),
            "CLOCK OUTPUTS  " + std::to_string(state.midi.clock_output_count),
            "ROUTES  " + std::to_string(state.midi.route_count),
            "SENT " + std::to_string(state.midi_sent) + "  DROPPED " +
                std::to_string(state.midi_dropped)};
        for (std::size_t i = 0; i < rows.size(); ++i) {
            rect(l.rows[i], i == 0 ? UiColor::selected : UiColor::background);
            text(l.rows[i], i == 1 && clock.dropout ? UiColor::error
                                                    : UiColor::text,
                 rows[i]);
        }
        text(l.rows[6], UiColor::muted,
             "Enter: toggle clock source   Shift+Enter: MIDI panic");
    }
    if (view_ == GuiView::mixer || view_ == GuiView::routing ||
        view_ == GuiView::modulation || view_ == GuiView::midi ||
        !l.inspector_visible)
        return;
    text({l.parameters.x + 12, l.parameters.y + 12, l.parameters.w - 24,
          28 * l.scale},
         UiColor::accent, "STEP INSPECTOR");
    UiRect undo{l.parameters.x + 12 * l.scale, l.parameters.y + 48 * l.scale,
                92 * l.scale, 32 * l.scale},
        redo{l.parameters.x + 116 * l.scale, l.parameters.y + 48 * l.scale,
             92 * l.scale, 32 * l.scale};
    rect(undo, state.can_undo ? UiColor::background : UiColor::unavailable);
    rect(redo, state.can_redo ? UiColor::background : UiColor::unavailable);
    text(undo, UiColor::text, "UNDO");
    text(redo, UiColor::text, "REDO");
    if (state.selected_event) {
        const auto &event = *state.selected_event;
        const std::string values[]{
            "Velocity  " + std::to_string(event.velocity),
            "Duration  " + std::to_string(event.duration) + " ticks",
            "Pitch  " + std::to_string(event.pitch),
            "Timing  " + std::to_string(event.timing_offset) + " ticks"};
        const std::string ids[]{"velocity", "duration", "pitch", "timing"};
        for (int i = 0; i < 4; ++i) {
            UiRect row{l.parameters.x + 12 * l.scale,
                       l.parameters.y + (96 + i * 48) * l.scale,
                       l.parameters.w - 24 * l.scale, 38 * l.scale};
            rect(row, state.selected_step_property == ids[i]
                          ? UiColor::selected
                          : UiColor::background);
            text(row, UiColor::text, values[i]);
        }
        text(
            {l.parameters.x + 12 * l.scale, l.parameters.y + 300 * l.scale,
             l.parameters.w - 24 * l.scale, 70 * l.scale},
            UiColor::muted,
            "Event " + std::to_string(event.event_id) + "  " +
                std::to_string(event.index_at_step + 1) + "/" +
                std::to_string(event.events_at_step) + "\nP " +
                std::to_string(event.trigger_probability * 100.0F) +
                "%  Cond " +
                std::to_string(static_cast<unsigned>(event.trigger_condition)) +
                "  " + std::to_string(event.condition_n) + "/" +
                std::to_string(event.condition_m) + "  Ratchet " +
                std::to_string(event.ratchet_count) +
                "\nEnter cycles property\nUp/Down adjusts");
    } else
        text({l.parameters.x + 12 * l.scale, l.parameters.y + 100 * l.scale,
              l.parameters.w - 24 * l.scale, 90 * l.scale},
             UiColor::muted,
             "Select + step to inspect\nan existing note event.");
    std::string locks = std::string("FILL ") +
                        (state.fill_active ? "ACTIVE" : "OFF") + "\nLOCKS";
    for (const auto &lock : state.selected_step_locks)
        locks += "\n" + lock.parameter_id + " = " + std::to_string(lock.value);
    if (state.selected_step_locks.empty())
        locks += "\n(none)";
    text({l.parameters.x + 12 * l.scale, l.parameters.y + 380 * l.scale,
          l.parameters.w - 24 * l.scale, 120 * l.scale},
         UiColor::text, locks);
    if (!error_.empty())
        text({l.parameters.x + 12,
              l.parameters.y + l.parameters.h - 70 * l.scale,
              l.parameters.w - 24, 60 * l.scale},
             UiColor::error, error_);
}

bool GuiModel::input(const GuiInput &input, int width, int height) {
    const auto l = layout(width, height);
    const auto state = application_.snapshot();
    const auto *track = selected_track(state);
    auto simple = [&](std::string id) { submit(command(std::move(id))); };
    if (input.type == GuiInput::Type::key_down) {
        if (input.key == GuiKey::scale_up || input.key == GuiKey::scale_down ||
            input.key == GuiKey::scale_reset) {
            std::string settings_error;
            const bool accepted =
                input.key == GuiKey::scale_reset
                    ? set_ui_scale(std::nullopt, settings_error)
                    : adjust_ui_scale(input.key == GuiKey::scale_up ? 0.1F
                                                                    : -0.1F,
                                      settings_error);
            error_ = accepted ? std::string{} : std::move(settings_error);
            return true;
        }
        if (input.key == GuiKey::fullscreen) {
            settings_.fullscreen = !settings_.fullscreen;
            fullscreen_toggle_ = true;
            ++ui_revision_;
            save_settings(error_);
            return true;
        }
        if (input.key == GuiKey::shift) {
            shift_ = true;
            submit(command("modifier.shift.v1", {{"active", true}}));
            return true;
        }
        if (input.key == GuiKey::space) {
            simple(state.playing ? "transport.stop.v1" : "transport.play.v1");
            return true;
        }
        if (view_ == GuiView::sequencer) {
            if (state.focused_element == "step-properties" &&
                (input.key == GuiKey::up || input.key == GuiKey::down)) {
                submit(
                    command("navigation.adjust.v1",
                            {{"steps", input.key == GuiKey::up ? 1.0 : -1.0}}));
                return true;
            }
            if (input.key == GuiKey::enter) {
                if (state.focused_element == "step-properties")
                    simple("navigation.confirm.v1");
                else
                    simple(shift_ ? "step.event-select.v1" : "step.toggle.v1");
                return true;
            }
            if (input.key == GuiKey::left || input.key == GuiKey::right) {
                if (shift_)
                    simple(input.key == GuiKey::right
                               ? "step.page-next.v1"
                               : "step.page-previous.v1");
                else {
                    const auto steps =
                        track ? step_count(*track, state.step_ticks) : 0;
                    if (steps) {
                        auto step = state.selected_step +
                                    (input.key == GuiKey::right ? 1 : -1);
                        if (step < 0)
                            step = static_cast<std::int64_t>(steps - 1);
                        if (step >= static_cast<std::int64_t>(steps))
                            step = 0;
                        submit(command("step.select.v1", {{"step", step}}));
                    }
                }
                return true;
            }
            if (input.key == GuiKey::up || input.key == GuiKey::down) {
                if (shift_ && track) {
                    const auto note = std::clamp<int>(
                        track->target_note + (input.key == GuiKey::up ? 1 : -1),
                        0, 127);
                    submit(
                        command("track.note-target-set.v1",
                                {{"note", static_cast<std::int64_t>(note)}}));
                } else
                    simple(input.key == GuiKey::down ? "track.next.v1"
                                                     : "track.previous.v1");
                return true;
            }
            if (input.key == GuiKey::number && input.number >= 1 &&
                input.number <= 16) {
                const auto step = static_cast<std::int64_t>(
                    state.step_page * 16 + input.number - 1);
                submit(
                    command(shift_ ? "step.event-select.v1" : "step.toggle.v1",
                            {{"step", step}}));
                return true;
            }
        }
        if (view_ == GuiView::samples) {
            const auto selected = std::find_if(
                state.slices.begin(), state.slices.end(),
                [&](const auto &s) { return s.id == state.selected_slice; });
            if (shift_ &&
                (input.key == GuiKey::up || input.key == GuiKey::down)) {
                waveform_zoom_ = std::clamp(
                    waveform_zoom_ * (input.key == GuiKey::up ? 1.25F : 0.8F),
                    1.0F, 64.0F);
                ++ui_revision_;
                return true;
            }
            if (shift_ &&
                (input.key == GuiKey::left || input.key == GuiKey::right)) {
                waveform_offset_ = std::clamp(
                    waveform_offset_ +
                        (input.key == GuiKey::right ? 0.05F : -0.05F),
                    0.0F, 1.0F);
                ++ui_revision_;
                return true;
            }
            if ((input.key == GuiKey::up || input.key == GuiKey::down) &&
                !state.slices.empty()) {
                auto index = selected == state.slices.end()
                                 ? std::size_t{0}
                                 : static_cast<std::size_t>(
                                       selected - state.slices.begin());
                index = input.key == GuiKey::down
                            ? (index + 1) % state.slices.size()
                            : (index + state.slices.size() - 1) %
                                  state.slices.size();
                submit(command("slice.select.v1",
                               {{"slice_id", static_cast<std::int64_t>(
                                                 state.slices[index].id)}}));
                return true;
            }
            if ((input.key == GuiKey::left || input.key == GuiKey::right) &&
                selected != state.slices.end()) {
                constexpr std::uint64_t amount = 1;
                auto start = selected->start_frame;
                auto end = selected->end_frame;
                if (input.key == GuiKey::left)
                    start = start > static_cast<std::uint64_t>(amount)
                                ? start - amount
                                : 0;
                else
                    end = std::min(state.sample_frames, end + amount);
                submit(
                    command("slice.bounds-set.v1",
                            {{"start_frame", static_cast<std::int64_t>(start)},
                             {"end_frame", static_cast<std::int64_t>(end)}}));
                return true;
            }
            if (input.key == GuiKey::enter) {
                if (shift_)
                    submit(command("slice.equal-divisions.v1",
                                   {{"divisions", std::int64_t{16}}}));
                else
                    simple("slice.preview.v1");
                return true;
            }
            if (input.key == GuiKey::escape) {
                simple(shift_ ? "slice.delete.v1" : "slice.preview-stop.v1");
                return true;
            }
            if (input.key == GuiKey::number && input.number >= 1 &&
                input.number <= 16) {
                if (shift_)
                    submit(command("sample.slot-select.v1",
                                   {{"slot_id", static_cast<std::int64_t>(
                                                    input.number - 1)}}));
                else {
                    submit(command("pad.select.v1",
                                   {{"pad", static_cast<std::int64_t>(
                                                input.number - 1)}}));
                    if (state.selected_slice != 0)
                        submit(command("slice.pad-assign.v1",
                                       {{"pad", static_cast<std::int64_t>(
                                                    input.number - 1)}}));
                }
                return true;
            }
        }
        if (input.key == GuiKey::tab) {
            view_ =
                static_cast<GuiView>((static_cast<unsigned>(view_) + 1) % 11);
            frame_.state_revision = std::numeric_limits<std::uint64_t>::max();
            return true;
        }
        if (input.key == GuiKey::enter) {
            if (view_ == GuiView::midi) {
                if (shift_)
                    submit(command("midi.panic.v1"));
                else
                    submit(command(
                        "midi.clock-source-set.v1",
                        {{"source",
                          std::string(state.midi.clock_source ==
                                              MidiClockSource::internal
                                          ? "external"
                                          : "internal")}}));
            } else if (view_ == GuiView::routing) {
                const auto index = state.selected_external_fx_loop;
                const auto &loop = state.mixer.configuration.external_fx[index];
                const auto source =
                    loop.source == ExternalFxSource::track       ? "track"
                    : loop.source == ExternalFxSource::auxiliary ? "aux"
                                                                 : "master";
                submit(command(
                    "mixer.fx-loop-set.v1",
                    {{"index", static_cast<std::int64_t>(index)},
                     {"source", std::string(source)},
                     {"source_id", static_cast<std::int64_t>(loop.source_id)},
                     {"send_left", static_cast<std::int64_t>(loop.send_left)},
                     {"send_right", static_cast<std::int64_t>(loop.send_right)},
                     {"return_left",
                      static_cast<std::int64_t>(loop.return_left)},
                     {"return_right",
                      static_cast<std::int64_t>(loop.return_right)},
                     {"enabled", !loop.enabled},
                     {"bypass", loop.bypass},
                     {"mono_send", loop.mono_send},
                     {"mono_return", loop.mono_return}}));
            } else if (view_ == GuiView::mixer) {
                submit(command(shift_ ? "mixer.solo-toggle.v1"
                                      : "mixer.mute-toggle.v1"));
            } else if (view_ == GuiView::recording) {
                if (state.recording.state == RecordingState::recording)
                    submit(command("recording.stop.v1"));
                else if (state.recording.state == RecordingState::armed)
                    submit(command("recording.start.v1"));
                else {
                    submit(command("recording.arm.v1"));
                    submit(command("recording.start.v1"));
                }
            } else if (view_ == GuiView::modulation &&
                       state.selected_modulation_route) {
                const auto found = std::find_if(
                    state.modulation.routes.begin(),
                    state.modulation.routes.begin() +
                        state.modulation.route_count,
                    [&](const auto &route) {
                        return route.id == state.selected_modulation_route;
                    });
                if (found != state.modulation.routes.begin() +
                                 state.modulation.route_count)
                    submit(command(
                        "modulation.route-enable.v1",
                        {{"route_id", static_cast<std::int64_t>(found->id)},
                         {"enabled", !found->enabled}}));
            } else if (view_ == GuiView::samples && !sample_path_.empty())
                submit(
                    command("sample.replace.v1",
                            {{"slot_id", static_cast<std::int64_t>(
                                             state.selected_sample_slot.value)},
                             {"path", sample_path_.string()}}));
            else if (view_ == GuiView::settings) {
                if (shift_)
                    submit(command("project.save.v1"));
                else {
                    settings_.fullscreen = !settings_.fullscreen;
                    fullscreen_toggle_ = true;
                    ++ui_revision_;
                    save_settings(error_);
                }
            } else if (view_ == GuiView::parameters && shift_ &&
                       !state.parameters.empty()) {
                const auto &parameter =
                    state
                        .parameters[parameter_index_ % state.parameters.size()];
                double value{};
                if (const auto *real =
                        std::get_if<double>(&parameter.current_value))
                    value = *real;
                else if (const auto *integer = std::get_if<std::int64_t>(
                             &parameter.current_value))
                    value = static_cast<double>(*integer);
                submit(
                    command("step.lock-set.v1", {{"parameter_id", parameter.id},
                                                 {"value", value}}));
            } else
                simple("navigation.confirm.v1");
            return true;
        }
        if (input.key == GuiKey::escape) {
            if (view_ == GuiView::recording) {
                submit(command("recording.cancel.v1"));
                return true;
            }
            if (view_ == GuiView::parameters && shift_ &&
                !state.parameters.empty())
                submit(command(
                    "step.lock-remove.v1",
                    {{"parameter_id", state
                                          .parameters[parameter_index_ %
                                                      state.parameters.size()]
                                          .id}}));
            else
                simple("navigation.cancel.v1");
            return true;
        }
        if ((input.key == GuiKey::up || input.key == GuiKey::down) &&
            view_ == GuiView::routing) {
            const auto next = input.key == GuiKey::down
                                  ? (state.selected_external_fx_loop + 1) %
                                        mixer_external_fx_count
                                  : (state.selected_external_fx_loop +
                                     mixer_external_fx_count - 1) %
                                        mixer_external_fx_count;
            submit(command("mixer.fx-loop-select.v1",
                           {{"index", static_cast<std::int64_t>(next)}}));
            return true;
        }
        if ((input.key == GuiKey::up || input.key == GuiKey::down) &&
            view_ == GuiView::modulation &&
            state.modulation.route_count != 0) {
            std::size_t current{};
            for (std::size_t i = 0; i < state.modulation.route_count; ++i)
                if (state.modulation.routes[i].id ==
                    state.selected_modulation_route)
                    current = i;
            const auto next = input.key == GuiKey::down
                                  ? (current + 1) %
                                        state.modulation.route_count
                                  : (current + state.modulation.route_count - 1) %
                                        state.modulation.route_count;
            submit(command(
                "modulation.route-select.v1",
                {{"route_id", static_cast<std::int64_t>(
                                  state.modulation.routes[next].id)}}));
            return true;
        }
        if ((input.key == GuiKey::left || input.key == GuiKey::right) &&
            view_ == GuiView::modulation &&
            state.selected_modulation_route) {
            const auto found = std::find_if(
                state.modulation.routes.begin(),
                state.modulation.routes.begin() + state.modulation.route_count,
                [&](const auto &route) {
                    return route.id == state.selected_modulation_route;
                });
            if (found != state.modulation.routes.begin() +
                             state.modulation.route_count)
                submit(command(
                    "modulation.route-depth.v1",
                    {{"route_id", static_cast<std::int64_t>(found->id)},
                     {"depth", static_cast<double>(std::clamp(
                                   found->depth +
                                       (input.key == GuiKey::right ? 0.05F
                                                                   : -0.05F),
                                   -1.0F, 1.0F))}}));
            return true;
        }
        if ((input.key == GuiKey::left || input.key == GuiKey::right) &&
            view_ == GuiView::routing) {
            const auto index = state.selected_external_fx_loop;
            const auto &loop = state.mixer.configuration.external_fx[index];
            submit(command(
                "mixer.fx-loop-levels.v1",
                {{"index", static_cast<std::int64_t>(index)},
                 {"send_gain", static_cast<double>(loop.send_gain)},
                 {"return_gain", static_cast<double>(loop.return_gain)},
                 {"wet",
                  static_cast<double>(std::clamp(
                      loop.wet + (input.key == GuiKey::right ? .05F : -.05F),
                      0.0F, 1.0F))},
                 {"return_mute", loop.return_mute}}));
            return true;
        }
        if ((input.key == GuiKey::up || input.key == GuiKey::down) &&
            view_ == GuiView::mixer && state.mixer.configuration.track_count) {
            const auto count = state.mixer.configuration.track_count;
            const auto next =
                input.key == GuiKey::down
                    ? (state.selected_mixer_channel + 1) % count
                    : (state.selected_mixer_channel + count - 1) % count;
            submit(command("mixer.channel-select.v1",
                           {{"index", static_cast<std::int64_t>(next)}}));
            return true;
        }
        if ((input.key == GuiKey::left || input.key == GuiKey::right) &&
            view_ == GuiView::mixer &&
            state.selected_mixer_channel <
                state.mixer.configuration.track_count) {
            const auto current =
                state.mixer.configuration.tracks[state.selected_mixer_channel]
                    .gain;
            submit(command(
                "mixer.gain-set.v1",
                {{"value",
                  static_cast<double>(std::clamp(
                      current + (input.key == GuiKey::right ? 0.05F : -0.05F),
                      0.0F, 2.0F))}}));
            return true;
        }
        if ((input.key == GuiKey::up || input.key == GuiKey::down) &&
            view_ == GuiView::parameters && !state.parameters.empty()) {
            const auto &parameter =
                state.parameters[parameter_index_ % state.parameters.size()];
            if (shift_) {
                const auto found = std::find_if(
                    state.selected_step_locks.begin(),
                    state.selected_step_locks.end(), [&](const auto &lock) {
                        return lock.parameter_id == parameter.id;
                    });
                const auto current =
                    found == state.selected_step_locks.end()
                        ? (std::holds_alternative<double>(
                               parameter.current_value)
                               ? std::get<double>(parameter.current_value)
                               : static_cast<double>(std::get<std::int64_t>(
                                     parameter.current_value)))
                        : found->value;
                submit(command(
                    "step.lock-set.v1",
                    {{"parameter_id", parameter.id},
                     {"value",
                      std::clamp(
                          current + (input.key == GuiKey::up ? 1 : -1) *
                                        parameter.step,
                          std::holds_alternative<double>(parameter.minimum)
                              ? std::get<double>(parameter.minimum)
                              : static_cast<double>(
                                    std::get<std::int64_t>(parameter.minimum)),
                          std::holds_alternative<double>(parameter.maximum)
                              ? std::get<double>(parameter.maximum)
                              : static_cast<double>(std::get<std::int64_t>(
                                    parameter.maximum)))}}));
            } else
                submit(
                    command("parameter.adjust.v1",
                            {{"parameter_id", parameter.id},
                             {"steps", input.key == GuiKey::up ? 1.0 : -1.0}}));
            return true;
        }
        if ((input.key == GuiKey::up || input.key == GuiKey::down) &&
            view_ == GuiView::settings) {
            std::string settings_error;
            const auto accepted = adjust_ui_scale(
                input.key == GuiKey::up ? 0.1F : -0.1F, settings_error);
            error_ = accepted ? std::string{} : std::move(settings_error);
            return true;
        }
        if ((input.key == GuiKey::up || input.key == GuiKey::down) &&
            view_ == GuiView::recording) {
            if (shift_) {
                const std::array sources{"external-mono", "external-stereo",
                                         "master-pre", "master-post"};
                const auto current = state.recording.configuration.source;
                auto index = static_cast<int>(current);
                index = (index + (input.key == GuiKey::up ? 1 : 3)) % 4;
                submit(command("recording.source-select.v1",
                               {{"source", std::string(sources[index])}}));
            } else {
                auto slot = static_cast<std::int64_t>(
                    state.recording.configuration.destination.value);
                slot = std::max<std::int64_t>(
                    0, slot + (input.key == GuiKey::up ? 1 : -1));
                submit(command("recording.destination-select.v1",
                               {{"slot_id", slot}}));
            }
            return true;
        }
    }
    if (input.type == GuiInput::Type::key_up && input.key == GuiKey::shift) {
        shift_ = false;
        submit(command("modifier.shift.v1", {{"active", false}}));
        return true;
    }
    const bool down = input.type == GuiInput::Type::pointer_down ||
                      input.type == GuiInput::Type::touch_down;
    const bool move = input.type == GuiInput::Type::pointer_move ||
                      input.type == GuiInput::Type::touch_move;
    const bool up = input.type == GuiInput::Type::pointer_up ||
                    input.type == GuiInput::Type::touch_up;
    if (down) {
        pointer_down_ = true;
        for (std::size_t i = 0; i < l.tabs.size(); ++i)
            if (l.tabs[i].contains(input.x, input.y)) {
                view_ = static_cast<GuiView>(i);
                frame_.state_revision =
                    std::numeric_limits<std::uint64_t>::max();
                return true;
            }
        if (view_ == GuiView::recording) {
            if (!l.rows.empty() && l.rows[0].contains(input.x, input.y)) {
                if (state.recording.state == RecordingState::recording)
                    submit(command("recording.stop.v1"));
                else if (state.recording.state == RecordingState::armed)
                    submit(command("recording.start.v1"));
                else {
                    submit(command("recording.arm.v1"));
                    submit(command("recording.start.v1"));
                }
                return true;
            }
            if (l.rows.size() > 6 && l.rows[6].contains(input.x, input.y)) {
                submit(command("recording.cancel.v1"));
                return true;
            }
        }
        if (view_ == GuiView::midi && !l.rows.empty() &&
            l.rows[0].contains(input.x, input.y)) {
            submit(command(
                "midi.clock-source-set.v1",
                {{"source",
                  std::string(state.midi.clock_source ==
                                      MidiClockSource::internal
                                  ? "external"
                                  : "internal")}}));
            return true;
        }
        if (view_ == GuiView::mixer)
            for (std::size_t i = 0;
                 i <
                 std::min(l.rows.size(), state.mixer.configuration.track_count);
                 ++i)
                if (l.rows[i].contains(input.x, input.y)) {
                    submit(command("mixer.channel-select.v1",
                                   {{"index", static_cast<std::int64_t>(i)}}));
                    return true;
                }
        if (view_ == GuiView::routing) {
            for (std::size_t i = 0; i < mixer_external_fx_count; ++i)
                if (l.rows[i].contains(input.x, input.y)) {
                    submit(command("mixer.fx-loop-select.v1",
                                   {{"index", static_cast<std::int64_t>(i)}}));
                    return true;
                }
            for (std::size_t i = 0;
                 i < state.routing_scenes.size() && i + 2 < l.rows.size(); ++i)
                if (l.rows[i + 2].contains(input.x, input.y)) {
                    submit(command("mixer.scene-recall.v1",
                                   {{"id", static_cast<std::int64_t>(
                                               state.routing_scenes[i].id)}}));
                    return true;
                }
        }
        if (view_ == GuiView::sequencer) {
            UiRect undo{l.parameters.x + 12 * l.scale,
                        l.parameters.y + 48 * l.scale, 92 * l.scale,
                        32 * l.scale},
                redo{l.parameters.x + 116 * l.scale,
                     l.parameters.y + 48 * l.scale, 92 * l.scale, 32 * l.scale};
            if (undo.contains(input.x, input.y)) {
                simple("edit.undo.v1");
                return true;
            }
            if (redo.contains(input.x, input.y)) {
                simple("edit.redo.v1");
                return true;
            }
            const char *properties[]{"velocity", "duration", "pitch", "timing"};
            for (int i = 0; i < 4; ++i) {
                UiRect row{l.parameters.x + 12 * l.scale,
                           l.parameters.y + (96 + i * 48) * l.scale,
                           l.parameters.w - 24 * l.scale, 38 * l.scale};
                if (row.contains(input.x, input.y)) {
                    submit(command("step.property-select.v1",
                                   {{"property", std::string(properties[i])}}));
                    return true;
                }
            }
            if (l.page_previous.contains(input.x, input.y)) {
                simple("step.page-previous.v1");
                return true;
            }
            if (l.page_next.contains(input.x, input.y)) {
                simple("step.page-next.v1");
                return true;
            }
            if (l.resolution.contains(input.x, input.y)) {
                submit(
                    command("step.resolution-set.v1",
                            {{"resolution",
                              std::string(step_resolution_name(
                                  next_resolution(state.step_resolution)))}}));
                return true;
            }
            if (l.target_note.contains(input.x, input.y) && track) {
                submit(
                    command("track.note-target-set.v1",
                            {{"note", static_cast<std::int64_t>(std::min<int>(
                                          127, track->target_note + 1))}}));
                return true;
            }
            if (l.instrument.contains(input.x, input.y) && track) {
                submit(
                    command("track.instrument-set.v1",
                            {{"instrument_id",
                              track->instrument_id == subtractive_instrument_id
                                  ? std::string(sample_instrument_id)
                                  : std::string(subtractive_instrument_id)}}));
                return true;
            }
            for (std::size_t i = 0;
                 i < std::min<std::size_t>(3, state.tracks.size()); ++i)
                if (l.rows[i].contains(input.x, input.y)) {
                    submit(command("track.select.v1",
                                   {{"track_id", static_cast<std::int64_t>(
                                                     state.tracks[i].id)}}));
                    return true;
                }
            for (std::size_t i = 0; i < l.grid.size(); ++i)
                if (l.grid[i].contains(input.x, input.y)) {
                    submit(command(shift_ ? "step.event-select.v1"
                                          : "step.toggle.v1",
                                   {{"step", static_cast<std::int64_t>(
                                                 state.step_page * 16 + i)}}));
                    return true;
                }
        }
        if (view_ == GuiView::settings) {
            std::string settings_error;
            if (l.scale_down.contains(input.x, input.y))
                adjust_ui_scale(-0.1F, settings_error);
            else if (l.scale_up.contains(input.x, input.y))
                adjust_ui_scale(0.1F, settings_error);
            else if (l.scale_reset.contains(input.x, input.y))
                set_ui_scale(std::nullopt, settings_error);
            else if (l.fullscreen.contains(input.x, input.y)) {
                settings_.fullscreen = !settings_.fullscreen;
                fullscreen_toggle_ = true;
                ++ui_revision_;
                save_settings(settings_error);
            } else
                settings_error = "display control was not hit";
            if (settings_error != "display control was not hit") {
                error_ = std::move(settings_error);
                return true;
            }
        }
        if (view_ == GuiView::samples) {
            if (l.inspector_visible) {
                for (std::size_t i = 0; i < 15; ++i) {
                    UiRect row{l.parameters.x + 12 * l.scale,
                               l.parameters.y + (48 + i * 27) * l.scale,
                               l.parameters.w - 24 * l.scale, 23 * l.scale};
                    if (!row.contains(input.x, input.y))
                        continue;
                    const auto &p = state.selected_pad_settings;
                    static constexpr const char *properties[]{
                        "mode",    "choke",        "pitch",
                        "fine",    "gain",         "reverse",
                        "attack",  "decay",        "sustain",
                        "release", "stretch-mode", "stretch-ratio",
                        "beats",   "source-bpm",   "stretch-quality"};
                    submit(command("pad.property-select.v1",
                                   {{"property", std::string(properties[i])}}));
                    if (i == 0) {
                        const auto next = static_cast<SamplePlaybackMode>(
                            (static_cast<unsigned>(p.mode) + 1) % 3);
                        submit(command(
                            "pad.playback-mode-set.v1",
                            {{"mode",
                              std::string(sample_playback_mode_name(next))}}));
                    } else if (i == 1) {
                        if (shift_)
                            submit(command("pad.choke-release-set.v1",
                                           {{"release", !p.choke_release}}));
                        else
                            submit(command(
                                "pad.choke-group-set.v1",
                                {{"group", static_cast<std::int64_t>(
                                               (p.choke_group + 1) % 17)}}));
                    } else if (i == 2)
                        submit(
                            command("pad.pitch-set.v1",
                                    {{"semitones",
                                      static_cast<double>(
                                          p.semitones + (shift_ ? -1 : 1))}}));
                    else if (i == 3)
                        submit(command("pad.fine-set.v1",
                                       {{"cents", static_cast<double>(
                                                      p.fine_cents +
                                                      (shift_ ? -1 : 1))}}));
                    else if (i == 4)
                        submit(command(
                            "pad.gain-set.v1",
                            {{"gain", static_cast<double>(std::clamp(
                                          p.gain + (shift_ ? -0.05F : 0.05F),
                                          0.0F, 4.0F))}}));
                    else if (i == 5)
                        submit(command("pad.reverse-set.v1",
                                       {{"reverse", !p.reverse}}));
                    else if (i < 10) {
                        auto attack = p.attack_seconds;
                        auto decay = p.decay_seconds;
                        auto sustain = p.sustain;
                        auto release = p.release_seconds;
                        const auto delta = shift_ ? -0.01F : 0.01F;
                        if (i == 6)
                            attack = std::max(0.0F, attack + delta);
                        else if (i == 7)
                            decay = std::max(0.0F, decay + delta);
                        else if (i == 8)
                            sustain = std::clamp(sustain + delta, 0.0F, 1.0F);
                        else
                            release = std::max(0.0F, release + delta);
                        submit(command(
                            "pad.envelope-set.v1",
                            {{"attack", static_cast<double>(attack)},
                             {"decay", static_cast<double>(decay)},
                             {"sustain", static_cast<double>(sustain)},
                             {"release", static_cast<double>(release)}}));
                    } else if (i == 10)
                        submit(command(
                            "pad.stretch-mode-set.v1",
                            {{"mode",
                              std::string(p.stretch_mode == StretchMode::off
                                              ? "realtime"
                                              : "off")}}));
                    else if (i == 11)
                        submit(command(
                            "pad.stretch-ratio-set.v1",
                            {{"ratio",
                              static_cast<double>(std::clamp(
                                  p.stretch_ratio + (shift_ ? -0.05F : 0.05F),
                                  0.25F, 4.0F))}}));
                    else if (i == 12)
                        submit(command(
                            "pad.source-beats-set.v1",
                            {{"beats",
                              static_cast<double>(std::max(
                                  0.0F, p.source_beats +
                                            (shift_ ? -0.25F : 0.25F)))}}));
                    else if (i == 13)
                        submit(command(
                            "pad.source-bpm-set.v1",
                            {{"bpm",
                              static_cast<double>(std::max(
                                  0.0F,
                                  p.source_bpm + (shift_ ? -1.0F : 1.0F)))}}));
                    else {
                        const auto next = static_cast<StretchQuality>(
                            (static_cast<unsigned>(p.stretch_quality) + 1) % 3);
                        submit(command(
                            "pad.stretch-quality-set.v1",
                            {{"quality",
                              std::string(stretch_quality_name(next))}}));
                    }
                    return true;
                }
            }
            const UiRect wave{l.content.x + 16 * l.scale,
                              l.content.y + 46 * l.scale,
                              l.content.w - 32 * l.scale,
                              std::min(280 * l.scale, l.content.h * 0.48F)};
            if (wave.contains(input.x, input.y) && state.sample_frames > 0) {
                const auto visible = std::max<std::uint64_t>(
                    1, state.sample_frames / waveform_zoom_);
                const auto first = static_cast<std::uint64_t>(
                    waveform_offset_ * (state.sample_frames - visible));
                const auto frame =
                    first +
                    static_cast<std::uint64_t>(
                        std::clamp((input.x - wave.x) / wave.w, 0.0F, 1.0F) *
                        visible);
                const auto tolerance = std::max<std::uint64_t>(
                    1, static_cast<std::uint64_t>(visible * 10.0F / wave.w));
                const SliceState *choice{};
                for (const auto &slice : state.slices) {
                    if (frame >= slice.start_frame && frame <= slice.end_frame)
                        choice = &slice;
                    if (frame + tolerance >= slice.start_frame &&
                        frame <= slice.start_frame + tolerance) {
                        choice = &slice;
                        slice_drag_ = SliceDrag::start;
                        break;
                    }
                    if (frame + tolerance >= slice.end_frame &&
                        frame <= slice.end_frame + tolerance) {
                        choice = &slice;
                        slice_drag_ = SliceDrag::end;
                        break;
                    }
                }
                if (choice)
                    submit(command(
                        "slice.select.v1",
                        {{"slice_id", static_cast<std::int64_t>(choice->id)}}));
                else {
                    const auto end = std::min<std::uint64_t>(
                        state.sample_frames, frame + state.sample_rate / 4);
                    if (frame < end)
                        submit(command(
                            "slice.create.v1",
                            {{"start_frame", static_cast<std::int64_t>(frame)},
                             {"end_frame", static_cast<std::int64_t>(end)},
                             {"name", std::string("Slice")}}));
                }
                return true;
            }
            const auto list_y = wave.y + wave.h + 12 * l.scale;
            for (std::size_t i = 0;
                 i < std::min<std::size_t>(4, state.slices.size()); ++i) {
                UiRect row{wave.x, list_y + i * 42 * l.scale, wave.w,
                           34 * l.scale};
                if (row.contains(input.x, input.y)) {
                    submit(command("slice.select.v1",
                                   {{"slice_id", static_cast<std::int64_t>(
                                                     state.slices[i].id)}}));
                    return true;
                }
            }
        }
        if (view_ == GuiView::parameters)
            for (std::size_t i = 0;
                 i < std::min(l.rows.size(), state.parameters.size()); ++i)
                if (l.rows[i].contains(input.x, input.y)) {
                    parameter_index_ = i;
                    frame_.state_revision =
                        std::numeric_limits<std::uint64_t>::max();
                    return true;
                }
    }
    if (((move && pointer_down_) || down) && view_ == GuiView::xy) {
        UiRect pad{l.content.x + 60 * l.scale, l.content.y + 56 * l.scale,
                   std::min(360 * l.scale, l.content.w - 120 * l.scale),
                   360 * l.scale};
        if (pad.contains(input.x, input.y) &&
            (input.timestamp_ms >= last_xy_ms_ + 8 || down)) {
            last_xy_ms_ = input.timestamp_ms;
            submit(command(
                "modulation.xy.v1",
                {{"position",
                  NormalizedXY{
                      std::clamp((input.x - pad.x) / pad.w, 0.0F, 1.0F),
                      std::clamp((input.y - pad.y) / pad.h, 0.0F, 1.0F)}}}));
            return true;
        }
    }
    if (move && pointer_down_ && view_ == GuiView::samples &&
        slice_drag_ != SliceDrag::none && state.selected_slice != 0) {
        const UiRect wave{l.content.x + 16 * l.scale,
                          l.content.y + 46 * l.scale,
                          l.content.w - 32 * l.scale,
                          std::min(280 * l.scale, l.content.h * 0.48F)};
        const auto selected = std::find_if(
            state.slices.begin(), state.slices.end(), [&](const auto &slice) {
                return slice.id == state.selected_slice;
            });
        if (selected != state.slices.end() && state.sample_frames > 0) {
            const auto visible = std::max<std::uint64_t>(
                1, state.sample_frames / waveform_zoom_);
            const auto first = static_cast<std::uint64_t>(
                waveform_offset_ * (state.sample_frames - visible));
            const auto frame =
                first +
                static_cast<std::uint64_t>(
                    std::clamp((input.x - wave.x) / wave.w, 0.0F, 1.0F) *
                    visible);
            const auto start =
                slice_drag_ == SliceDrag::start ? frame : selected->start_frame;
            const auto end =
                slice_drag_ == SliceDrag::end ? frame : selected->end_frame;
            if (start < end)
                submit(
                    command("slice.bounds-set.v1",
                            {{"start_frame", static_cast<std::int64_t>(start)},
                             {"end_frame", static_cast<std::int64_t>(end)}}));
            return true;
        }
    }
    if (up) {
        pointer_down_ = false;
        slice_drag_ = SliceDrag::none;
        return true;
    }
    return false;
}

std::vector<std::filesystem::directory_entry>
GuiModel::browse(const std::filesystem::path &path, std::string &error) const {
    try {
        std::vector<std::filesystem::directory_entry> result;
        for (const auto &entry : std::filesystem::directory_iterator(path))
            if (entry.is_directory() || entry.path().extension() == ".wav")
                result.push_back(entry);
        std::sort(
            result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.path() < b.path(); });
        error.clear();
        return result;
    } catch (const std::exception &exception) {
        error = exception.what();
        return {};
    }
}
} // namespace neurogenic
