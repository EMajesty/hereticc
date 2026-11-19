#include "neurogenic/ui/gui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>

namespace neurogenic {
UiPoint window_to_drawable(const DisplayMetrics& display,float x,float y) noexcept{return {x*display.pixel_ratio_x,y*display.pixel_ratio_y};}
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
                    error = "invalid ui_scale in GUI settings; using automatic scale";
                    result.ui_scale.reset();
                }
            }
        } else if (key == "fullscreen") {
            int value{};
            if (!(input >> value) || (value != 0 && value != 1)) {
                error = "invalid fullscreen value in GUI settings; using windowed mode";
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
    const float content_height = std::max(360.0F, logical_height - content_y - margin);
    const float inspector_width = result.inspector_visible
                                      ? (result.breakpoint == GuiBreakpoint::wide
                                             ? 300.0F
                                             : 232.0F)
                                      : 0.0F;
    const float gap = result.inspector_visible ? 16.0F : 0.0F;
    result.top = r(0, 0, logical_width, top_height);
    result.nav = r(0, top_height, logical_width, nav_height);
    result.content = r(margin, content_y,
                       logical_width - margin * 2 - inspector_width - gap,
                       content_height);
    result.parameters = result.inspector_visible
                            ? r(logical_width - margin - inspector_width,
                                content_y, inspector_width, content_height)
                            : UiRect{};
    const float tab_width = (logical_width - 32) / 6;
    for (int i = 0; i < 6; ++i)
        result.tabs.push_back(r(16 + i * tab_width, 72, tab_width - 8, 38));
    result.page_previous = r(28, content_y + 12, 64, 42);
    result.page_next = r(100, content_y + 12, 64, 42);
    result.resolution = r(172, content_y + 12, 100, 42);
    result.target_note = r(280, content_y + 12, 100, 42);
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
    const float grid_height = std::max(52.0F,
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
        frame_.ui_revision != ui_revision_ ||
        frame_.viewport_width != width || frame_.viewport_height != height) {
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
    const auto& perf=state.performance;
    text({l.top.x + l.top.w - 510 * l.scale,l.top.y + 38 * l.scale,
          500 * l.scale,20 * l.scale},UiColor::muted,
         "AUDIO " + std::to_string(static_cast<int>(perf.audio.current_load_percent)) +
         "%  CPU " + (perf.system.process_cpu_available?std::to_string(static_cast<int>(perf.system.process_cpu_percent)):"n/a") +
         "%  MEM " + (perf.system.resident_memory_available?std::to_string(perf.system.resident_memory_bytes/(1024*1024)):"n/a") +
         " MiB  VOICES " + std::to_string(perf.engine.active_voices) + "/" +
         std::to_string(perf.engine.maximum_voices));
    const char *names[]{"SEQUENCER", "SAMPLES", "PARAMETERS", "X/Y",
                        "PERF", "DISPLAY"};
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
        for (const auto control :
             {l.page_previous, l.page_next, l.resolution, l.target_note})
            rect(control, UiColor::background);
        text(l.page_previous, UiColor::text, "< PAGE");
        text(l.page_next, UiColor::text, "PAGE >");
        text(l.resolution, UiColor::text,
             step_resolution_name(state.step_resolution));
        text(l.target_note, UiColor::text,
             "NOTE " + std::to_string(track ? track->target_note : 0));
        for (std::size_t i = 0;
             i < std::min<std::size_t>(3, state.tracks.size()); ++i) {
            const auto &item = state.tracks[i];
            rect(l.rows[i], item.id == state.selected_track
                                ? UiColor::selected
                                : UiColor::background);
            text(l.rows[i], item.muted ? UiColor::muted : UiColor::text,
                 std::to_string(item.id) + "  " + item.name + "  " +
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
            UiColor color =
                !valid   ? UiColor::unavailable
                : active ? UiColor::accent
                : state.selected_step == static_cast<std::int64_t>(step)
                    ? UiColor::selected
                    : UiColor::background;
            rect(l.grid[i], color);
            text(l.grid[i],
                 step == playhead ? UiColor::selected : UiColor::text,
                 std::to_string(step + 1) + (step == playhead ? " >" : ""));
        }
    } else if (view_ == GuiView::samples) {
        text({l.content.x + 12, l.content.y + 8, l.content.w, 28 * l.scale},
             UiColor::accent, "SAMPLE SLOTS");
        for (std::size_t i = 0; i < l.rows.size(); ++i) {
            const auto slot = static_cast<std::uint32_t>(i);
            const auto found = std::find_if(
                state.sample_slots.begin(), state.sample_slots.end(),
                [&](const auto &s) { return s.id.value == slot; });
            std::string label = "SLOT " + std::to_string(slot) + "  EMPTY";
            if (found != state.sample_slots.end())
                label = "SLOT " + std::to_string(slot) + "  " +
                        sample_slot_state_name(found->state) + "  " +
                        found->source_path.filename().string() +
                        (found->error.empty() ? "" : "  " + found->error);
            rect(l.rows[i], slot == state.selected_sample_slot.value
                                ? UiColor::selected
                                : UiColor::background);
            text(l.rows[i],
                 found != state.sample_slots.end() && !found->error.empty()
                     ? UiColor::error
                     : UiColor::text,
                 label);
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
    } else if(view_==GuiView::performance){
        const auto micros=[](std::uint64_t ns){return std::to_string(ns/1000)+" us";};
        text({l.content.x+12*l.scale,l.content.y+8*l.scale,l.content.w-24*l.scale,28*l.scale},UiColor::accent,"PERFORMANCE MONITOR");
        const std::string rows[]{
          "Audio load current / average / peak: " + std::to_string(perf.audio.current_load_percent) + "% / " + std::to_string(perf.audio.average_load_percent) + "% / " + std::to_string(perf.audio.peak_load_percent) + "%",
          "Callback current / average / peak: " + micros(perf.audio.current_nanoseconds) + " / " + micros(perf.audio.average_nanoseconds) + " / " + micros(perf.audio.peak_nanoseconds),
          "Deadline: " + micros(perf.audio.deadline_nanoseconds) + "  measured overruns: " + std::to_string(perf.audio.deadline_overruns) + " (not backend XRUNs)",
          "Audio: " + std::to_string(perf.engine.sample_rate) + " Hz / " + std::to_string(perf.engine.buffer_frames) + " frames  voices " + std::to_string(perf.engine.active_voices) + "/" + std::to_string(perf.engine.maximum_voices),
          "Process CPU: " + (perf.system.process_cpu_available?std::to_string(perf.system.process_cpu_percent)+"% of one core":"unavailable") + "  System CPU: " + (perf.system.system_cpu_available?std::to_string(perf.system.system_cpu_percent)+"%":"unavailable"),
          "Resident / system memory: " + (perf.system.resident_memory_available?std::to_string(perf.system.resident_memory_bytes/(1024*1024))+" MiB":"unavailable") + " / " + (perf.system.total_memory_available?std::to_string(perf.system.total_memory_bytes/(1024*1024))+" MiB":"unavailable"),
          "Queue overflow: triggers " + std::to_string(perf.engine.dropped_triggers) + ", timelines " + std::to_string(perf.engine.dropped_timelines) + ", commands " + std::to_string(perf.engine.rejected_commands),
          "Backend XRUN counter: unavailable"
        };
        for(std::size_t i=0;i<std::size(rows)&&i<l.rows.size();++i){rect(l.rows[i],UiColor::background);text(l.rows[i],UiColor::text,rows[i]);}
    } else {
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
              l.content.w - 24 * l.scale, 100 * l.scale},
             UiColor::text,
             "UI scale: " +
                 (settings_.ui_scale ? std::to_string(*settings_.ui_scale)
                                     : std::string("automatic (current ") +
                                           std::to_string(l.scale) + ")") +
                 "\nKeyboard: - / + / 0, F11\nEncoder: Up/Down, Enter toggles fullscreen");
    }
    if (!l.inspector_visible)
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
        text({l.parameters.x + 12 * l.scale, l.parameters.y + 300 * l.scale,
              l.parameters.w - 24 * l.scale, 70 * l.scale},
             UiColor::muted,
             "Event " + std::to_string(event.event_id) + "  " +
                 std::to_string(event.index_at_step + 1) + "/" +
                 std::to_string(event.events_at_step) +
                 "\nEnter cycles property\nUp/Down adjusts");
    } else
        text({l.parameters.x + 12 * l.scale, l.parameters.y + 100 * l.scale,
              l.parameters.w - 24 * l.scale, 90 * l.scale},
             UiColor::muted,
             "Select + step to inspect\nan existing note event.");
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
            const bool accepted = input.key == GuiKey::scale_reset
                                      ? set_ui_scale(std::nullopt, settings_error)
                                      : adjust_ui_scale(input.key == GuiKey::scale_up
                                                            ? 0.1F
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
        if (input.key == GuiKey::tab) {
            view_ =
                static_cast<GuiView>((static_cast<unsigned>(view_) + 1) % 6);
            frame_.state_revision = std::numeric_limits<std::uint64_t>::max();
            return true;
        }
        if (input.key == GuiKey::enter) {
            if (view_ == GuiView::samples && !sample_path_.empty())
                submit(
                    command("sample.replace.v1",
                            {{"slot_id", static_cast<std::int64_t>(
                                             state.selected_sample_slot.value)},
                             {"path", sample_path_.string()}}));
            else if (view_ == GuiView::settings) {
                settings_.fullscreen = !settings_.fullscreen;
                fullscreen_toggle_ = true;
                ++ui_revision_;
                save_settings(error_);
            } else
                simple("navigation.confirm.v1");
            return true;
        }
        if (input.key == GuiKey::escape) {
            simple("navigation.cancel.v1");
            return true;
        }
        if ((input.key == GuiKey::up || input.key == GuiKey::down) &&
            view_ == GuiView::parameters && !state.parameters.empty()) {
            submit(command(
                "parameter.adjust.v1",
                {{"parameter_id",
                  state.parameters[parameter_index_ % state.parameters.size()]
                      .id},
                 {"steps", input.key == GuiKey::up ? 1.0 : -1.0}}));
            return true;
        }
        if ((input.key == GuiKey::up || input.key == GuiKey::down) &&
            view_ == GuiView::settings) {
            std::string settings_error;
            const auto accepted = adjust_ui_scale(input.key == GuiKey::up
                                                      ? 0.1F
                                                      : -0.1F,
                                                  settings_error);
            error_ = accepted ? std::string{} : std::move(settings_error);
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
        if (view_ == GuiView::samples)
            for (std::size_t i = 0; i < l.rows.size(); ++i)
                if (l.rows[i].contains(input.x, input.y)) {
                    submit(
                        command("sample.slot-select.v1",
                                {{"slot_id", static_cast<std::int64_t>(i)}}));
                    return true;
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
    if (up) {
        pointer_down_ = false;
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
