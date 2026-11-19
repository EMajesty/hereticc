#pragma once
#include "neurogenic/application/control.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace neurogenic {
enum class GuiView : std::uint8_t {
    sequencer,
    samples,
    parameters,
    xy,
    performance,
    settings,
    recording,
    mixer
};
struct UiRect {
    float x{}, y{}, w{}, h{};
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};
enum class GuiBreakpoint : std::uint8_t { legacy, compact, wide };
struct DisplayMetrics {
    int window_width{}, window_height{}, drawable_width{}, drawable_height{};
    float pixel_ratio_x{1}, pixel_ratio_y{1};
};
struct UiPoint {
    float x{}, y{};
};
[[nodiscard]] UiPoint window_to_drawable(const DisplayMetrics &display, float x,
                                         float y) noexcept;
struct GuiSettings {
    std::optional<float> ui_scale;
    bool fullscreen{};
};
struct GuiLayout {
    float scale{1}, logical_width{}, logical_height{};
    GuiBreakpoint breakpoint{GuiBreakpoint::legacy};
    bool inspector_visible{};
    UiRect top, nav, content, parameters, page_previous, page_next, resolution,
        target_note, instrument, scale_down, scale_up, scale_reset, fullscreen;
    std::vector<UiRect> tabs, grid, rows;
};
enum class UiColor : std::uint8_t {
    background,
    panel,
    text,
    muted,
    accent,
    selected,
    error,
    unavailable
};
struct DrawCommand {
    enum class Type : std::uint8_t { rectangle, text, line };
    Type type{};
    UiRect rect;
    UiColor color{};
    std::string text;
};
struct GuiFrame {
    std::uint64_t state_revision{}, performance_generation{}, ui_revision{};
    int viewport_width{}, viewport_height{};
    GuiLayout layout;
    std::vector<DrawCommand> commands;
};
enum class GuiKey : std::uint8_t {
    left,
    right,
    up,
    down,
    enter,
    escape,
    tab,
    space,
    shift,
    number,
    scale_up,
    scale_down,
    scale_reset,
    fullscreen
};
struct GuiInput {
    enum class Type : std::uint8_t {
        key_down,
        key_up,
        pointer_down,
        pointer_up,
        pointer_move,
        touch_down,
        touch_up,
        touch_move
    };
    Type type{};
    GuiKey key{};
    int number{};
    float x{}, y{};
    std::uint64_t timestamp_ms{};
};

class GuiModel {
  public:
    explicit GuiModel(ApplicationService &application,
                      GuiSettings settings = {},
                      std::filesystem::path settings_path = {});
    [[nodiscard]] GuiLayout layout(int width, int height) const;
    [[nodiscard]] const GuiFrame &frame(int width, int height);
    bool input(const GuiInput &, int width, int height);
    [[nodiscard]] GuiView view() const noexcept { return view_; }
    [[nodiscard]] const std::string &error() const noexcept { return error_; }
    [[nodiscard]] std::vector<std::filesystem::directory_entry>
    browse(const std::filesystem::path &, std::string &error) const;
    void set_sample_path(std::filesystem::path path) {
        sample_path_ = std::move(path);
    }
    [[nodiscard]] float effective_scale(int drawable_width,
                                        int drawable_height) const noexcept;
    [[nodiscard]] const GuiSettings &settings() const noexcept {
        return settings_;
    }
    bool set_ui_scale(std::optional<float>, std::string &error);
    bool adjust_ui_scale(float, std::string &error);
    bool save_settings(std::string &error) const;
    [[nodiscard]] bool take_fullscreen_toggle() noexcept {
        const bool value = fullscreen_toggle_;
        fullscreen_toggle_ = false;
        return value;
    }
    static GuiSettings load_settings(const std::filesystem::path &,
                                     std::string &error);
    static std::filesystem::path default_settings_path();

  private:
    void submit(ApplicationCommand);
    void render(const ApplicationState &, const GuiLayout &);
    ApplicationService &application_;
    GuiSettings settings_;
    std::filesystem::path settings_path_;
    GuiView view_{GuiView::sequencer};
    GuiFrame frame_;
    std::string error_;
    std::filesystem::path sample_path_;
    std::size_t parameter_index_{};
    bool pointer_down_{}, shift_{}, fullscreen_toggle_{};
    enum class SliceDrag : std::uint8_t { none, start, end };
    SliceDrag slice_drag_{SliceDrag::none};
    float waveform_zoom_{1.0F}, waveform_offset_{};
    std::uint64_t last_xy_ms_{}, ui_revision_{1};
};

struct GuiOptions {
    bool fullscreen{};
    int width{1280}, height{800};
    std::optional<float> ui_scale;
    bool reset_ui_scale{};
    std::filesystem::path settings_path;
    std::uint32_t max_frames{};
};
[[nodiscard]] bool gui_available() noexcept;
int run_gui(ApplicationService &, const GuiOptions &, std::string &error);
} // namespace neurogenic
