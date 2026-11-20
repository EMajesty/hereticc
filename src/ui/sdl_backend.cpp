#include "neurogenic/ui/gui.hpp"
#ifdef NEUROGENIC_HAS_SDL3
#include <SDL3/SDL.h>
#endif
#include <chrono>
#include <cmath>

namespace neurogenic {
DisplayDiagnostic query_display_diagnostic() noexcept {
#ifdef NEUROGENIC_HAS_SDL3
    DisplayDiagnostic result;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        result.error = SDL_GetError();
        return result;
    }
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display);
    if (!display || !mode) {
        result.error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return result;
    }
    result.available = true;
    result.width = mode->w;
    result.height = mode->h;
    result.drawable_width = mode->w;
    result.drawable_height = mode->h;
    result.content_scale = SDL_GetDisplayContentScale(display);
    result.backend = SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown";
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return result;
#else
    return {false, 0, 0, 0, 0, 1.0F, "not compiled", "SDL3 support unavailable"};
#endif
}
#ifdef NEUROGENIC_HAS_SDL3
namespace {
void color(SDL_Renderer *renderer, UiColor value) {
    struct C {
        Uint8 r, g, b;
    };
    static constexpr C colors[] = {
        {10, 13, 18},  {22, 27, 35}, {225, 232, 240}, {112, 124, 140},
        {0, 210, 190}, {37, 69, 82}, {255, 82, 100},  {86, 91, 103}};
    const auto c = colors[static_cast<std::size_t>(value)];
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
}
GuiKey key(SDL_Keycode value, int &number) {
    if (value == SDLK_LEFT)
        return GuiKey::left;
    if (value == SDLK_RIGHT)
        return GuiKey::right;
    if (value == SDLK_UP)
        return GuiKey::up;
    if (value == SDLK_DOWN)
        return GuiKey::down;
    if (value == SDLK_RETURN || value == SDLK_KP_ENTER)
        return GuiKey::enter;
    if (value == SDLK_ESCAPE || value == SDLK_BACKSPACE)
        return GuiKey::escape;
    if (value == SDLK_TAB)
        return GuiKey::tab;
    if (value == SDLK_SPACE)
        return GuiKey::space;
    if (value == SDLK_LSHIFT || value == SDLK_RSHIFT)
        return GuiKey::shift;
    if (value == SDLK_EQUALS || value == SDLK_KP_PLUS)
        return GuiKey::scale_up;
    if (value == SDLK_MINUS || value == SDLK_KP_MINUS)
        return GuiKey::scale_down;
    if (value == SDLK_0 || value == SDLK_KP_0)
        return GuiKey::scale_reset;
    if (value == SDLK_F11)
        return GuiKey::fullscreen;
    if (value >= SDLK_1 && value <= SDLK_9) {
        number = static_cast<int>(value - SDLK_0);
        return GuiKey::number;
    }
    number = 0;
    return GuiKey::tab;
}
DisplayMetrics metrics(SDL_Window *window) {
    DisplayMetrics result;
    SDL_GetWindowSize(window, &result.window_width, &result.window_height);
    SDL_GetWindowSizeInPixels(window, &result.drawable_width,
                              &result.drawable_height);
    if (result.window_width > 0)
        result.pixel_ratio_x =
            static_cast<float>(result.drawable_width) / result.window_width;
    if (result.window_height > 0)
        result.pixel_ratio_y =
            static_cast<float>(result.drawable_height) / result.window_height;
    return result;
}
} // namespace
bool gui_available() noexcept { return true; }
int run_gui(ApplicationService &application, const GuiOptions &options,
            std::string &error) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        error = SDL_GetError();
        return 1;
    }
    const auto settings_path = options.settings_path.empty()
                                   ? GuiModel::default_settings_path()
                                   : options.settings_path;
    auto settings = GuiModel::load_settings(settings_path, error);
    if (options.reset_ui_scale)
        settings.ui_scale.reset();
    if (options.ui_scale)
        settings.ui_scale = options.ui_scale;
    settings.fullscreen = options.fullscreen || settings.fullscreen;
    SDL_Window *window{};
    SDL_Renderer *renderer{};
    const auto flags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
        (settings.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (!SDL_CreateWindowAndRenderer("NEUROGENIC", options.width,
                                     options.height, flags, &window,
                                     &renderer)) {
        error = SDL_GetError();
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 1024, 600);
    GuiModel model(application, settings, settings_path);
    SDL_StartTextInput(window);
    bool running = true;
    std::uint32_t frames{};
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const auto display = metrics(window);
            GuiInput input;
            input.timestamp_ms = event.common.timestamp;
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN ||
                     event.type == SDL_EVENT_KEY_UP) {
                int number{};
                input.type = event.type == SDL_EVENT_KEY_DOWN
                                 ? GuiInput::Type::key_down
                                 : GuiInput::Type::key_up;
                input.key = key(event.key.key, number);
                input.number = number;
                if (event.key.key == SDLK_Q && (event.key.mod & SDL_KMOD_CTRL))
                    running = false;
                else
                    model.input(input, display.drawable_width,
                                display.drawable_height);
            } else if (event.type == SDL_EVENT_TEXT_INPUT) {
                input.type = GuiInput::Type::text_input;
                input.text = event.text.text;
                model.input(input, display.drawable_width,
                            display.drawable_height);
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                       event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                input.type = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                                 ? GuiInput::Type::pointer_down
                                 : GuiInput::Type::pointer_up;
                const auto point =
                    window_to_drawable(display, event.button.x, event.button.y);
                input.x = point.x;
                input.y = point.y;
                model.input(input, display.drawable_width,
                            display.drawable_height);
            } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                input.type = GuiInput::Type::pointer_move;
                const auto point =
                    window_to_drawable(display, event.motion.x, event.motion.y);
                input.x = point.x;
                input.y = point.y;
                model.input(input, display.drawable_width,
                            display.drawable_height);
            } else if (event.type == SDL_EVENT_FINGER_DOWN ||
                       event.type == SDL_EVENT_FINGER_UP ||
                       event.type == SDL_EVENT_FINGER_MOTION) {
                input.type = event.type == SDL_EVENT_FINGER_DOWN
                                 ? GuiInput::Type::touch_down
                             : event.type == SDL_EVENT_FINGER_UP
                                 ? GuiInput::Type::touch_up
                                 : GuiInput::Type::touch_move;
                input.x = event.tfinger.x * display.drawable_width;
                input.y = event.tfinger.y * display.drawable_height;
                model.input(input, display.drawable_width,
                            display.drawable_height);
            }
        }
        if (model.take_fullscreen_toggle()) {
            SDL_SetWindowFullscreen(window, model.settings().fullscreen);
        }
        (void)application.dispatch_pending();
        const auto display = metrics(window);
        const auto &frame =
            model.frame(display.drawable_width, display.drawable_height);
        color(renderer, UiColor::background);
        SDL_RenderClear(renderer);
        for (const auto &item : frame.commands) {
            color(renderer, item.color);
            if (item.type == DrawCommand::Type::rectangle) {
                SDL_FRect rect{item.rect.x, item.rect.y, item.rect.w,
                               item.rect.h};
                SDL_RenderFillRect(renderer, &rect);
            } else if (item.type == DrawCommand::Type::line) {
                SDL_RenderLine(renderer, item.rect.x, item.rect.y, item.rect.w,
                               item.rect.h);
            } else if (item.type == DrawCommand::Type::text) {
                const float text_scale =
                    std::max(2.0F, std::round(frame.layout.scale * 2.0F));
                SDL_SetRenderScale(renderer, text_scale, text_scale);
                SDL_RenderDebugText(renderer, item.rect.x / text_scale,
                                    item.rect.y / text_scale,
                                    item.text.c_str());
                SDL_SetRenderScale(renderer, 1, 1);
            }
        }
        SDL_RenderPresent(renderer);
        if (options.max_frames && ++frames >= options.max_frames)
            running = false;
        else
            SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    error.clear();
    return 0;
}
#else
bool gui_available() noexcept { return false; }
int run_gui(ApplicationService &, const GuiOptions &, std::string &error) {
    error = "GUI support was not built (SDL3 not found or "
            "NEUROGENIC_ENABLE_GUI=OFF)";
    return 1;
}
#endif
} // namespace neurogenic
