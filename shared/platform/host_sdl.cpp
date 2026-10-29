// SDL 3 implementation of the host platform (see host.hpp).

#include "platform/host.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace platform {
struct Host::Implementation {
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* texture{};
    SDL_Texture* overlay_texture{};
    int texture_width{}, texture_height{};
    // True while a UI screen is shown, whose logical presentation maps mouse
    // clicks into the UI design space.
    bool ui_shown{true};
    float wheel_rest{}; // mouse wheel movement not yet a whole step
    SDL_FRect overlay_box{}; // where the last game overlay was shown (device pixels)
    SDL_Texture* note_texture{}; // present_game's note
    SDL_FRect game_box{};    // where the last game frame was shown (device pixels)
    int game_width{}, game_height{}; // that frame's size in pixels
    bool game_draws_pointer{};       // the game shows its own pointer
    bool press_in_game{};            // a press began over the game frame (pointer events)
    std::map<SDL_FingerID, std::array<float, 2>> fingers; // touching now (render coordinates)
    // The accelerometer (set_motion_wanted), closed while no game asks; whether the sensor
    // subsystem was started and none was found.
    SDL_Sensor* accelerometer{};
    bool sensors_started{}, no_accelerometer{};
    std::filesystem::path capture;   // capture_next_frame's file, until the next present
    SDL_Texture* gpu_target{};       // present_game_gpu's target, at the shown size
    int gpu_width{}, gpu_height{};
    // The screen filter stage (ScreenEffect): the frame drawn at its logical size, the shaders
    // compiled for the GPU renderer (a null state: the renderer cannot run that shader), and
    // how the last effect was applied.
    SDL_Texture* effect_source{};
    SDL_Texture* effect_output{};
    int effect_width{}, effect_height{};
    // Effects with regions (ScreenEffect::regions): the frame composed at its shown size, and per
    // region its picture, its filtered raster and (CPU form) its uploaded result.
    SDL_Texture* effect_composite{};
    std::vector<SDL_Texture*> region_sources, region_outputs, region_uploads;
    struct CompiledShader {
        SDL_GPUShader* shader{};
        SDL_GPURenderState* state{};
        std::string path;
    };
    std::map<std::string, CompiledShader> shaders;
    std::string effect_path;
    bool cursor_hidden{};
    bool mouse_captured{}; // relative mouse mode (set_mouse_captured)
    SDL_Gamepad* gamepad{};
    SDL_Gamepad* gamepad2{}; // a second one: the second joystick
    SDL_AudioStream* audio{};

    struct Dialog {
        std::mutex mutex;
        std::optional<Host::FileChoice> result;
        bool pending{};
    };

    std::shared_ptr<Dialog> dialog = std::make_shared<Dialog>();

    ~Implementation() {
        if (gamepad) {
            SDL_CloseGamepad(gamepad);
        }
        if (gamepad2) {
            SDL_CloseGamepad(gamepad2);
        }
        if (accelerometer) SDL_CloseSensor(accelerometer);
        SDL_DestroyAudioStream(audio);
        SDL_GPUDevice* device = renderer ? SDL_GetGPURendererDevice(renderer) : nullptr;
        for (auto& [name, compiled] : shaders) {
            if (compiled.state) SDL_DestroyGPURenderState(compiled.state);
            if (compiled.shader && device) SDL_ReleaseGPUShader(device, compiled.shader);
        }
        SDL_DestroyTexture(effect_output);
        SDL_DestroyTexture(effect_source);
        SDL_DestroyTexture(effect_composite);
        for (auto* t : region_sources) SDL_DestroyTexture(t);
        for (auto* t : region_outputs) SDL_DestroyTexture(t);
        for (auto* t : region_uploads) SDL_DestroyTexture(t);
        SDL_DestroyTexture(texture);
        SDL_DestroyTexture(overlay_texture);
        SDL_DestroyTexture(note_texture);
        SDL_DestroyTexture(gpu_target);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void connect() {
        const bool first = gamepad && SDL_GamepadConnected(gamepad);
        const bool second = gamepad2 && SDL_GamepadConnected(gamepad2);
        if (first && second) {
            return;
        }
        for (auto* pad : {&gamepad, &gamepad2}) {
            if (*pad && !SDL_GamepadConnected(*pad)) {
                SDL_CloseGamepad(*pad);
                *pad = nullptr;
            }
        }
        int count = 0;
        auto ids = SDL_GetGamepads(&count);
        for (int n = 0; n < count; ++n) {
            const auto id = ids[n];
            const bool open = (gamepad && SDL_GetGamepadID(gamepad) == id) || (gamepad2 && SDL_GetGamepadID(gamepad2) == id);
            if (open) {
                continue;
            }
            if (!gamepad) {
                gamepad = SDL_OpenGamepad(id);
            } else if (!gamepad2) {
                gamepad2 = SDL_OpenGamepad(id);
            }
        }
        SDL_free(ids);
    }

    // A point in render coordinates (device pixels during a game) in the game frame's own units;
    // empty without a game frame or while an overlay takes the pointer.
    std::optional<std::array<float, 2>> frame_point(float x, float y) const {
        if (ui_shown || overlay_box.w > 0 || game_box.w <= 0 || game_box.h <= 0) return std::nullopt;
        return std::array<float, 2>{(x - game_box.x) * float(game_width) / game_box.w,
                                    (y - game_box.y) * float(game_height) / game_box.h};
    }

    // Draws a game frame's texture into `target` (device pixels), then the note and the overlay,
    // and presents (present_game and present_game_gpu).
    void show_frame(SDL_Texture* frame, const SDL_FRect& target, bool dim, const Image* overlay, const Image* note,
                    float note_opacity, int note_offset, int cw, int ch, SDL_GPURenderState* effect = nullptr);

    // The render state that runs `shader` on the GPU renderer, compiled once; null where it
    // cannot run (another renderer, or no shader format in common).
    SDL_GPURenderState* shader_state(const FragmentShader& shader);

    // The current render target's pixels, read back (one-off captures, the CPU screen effect).
    Image read_pixels() {
        SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
        SDL_Surface* rgba = pixels ? SDL_ConvertSurface(pixels, SDL_PIXELFORMAT_RGBA32) : nullptr;
        SDL_DestroySurface(pixels);
        if (!rgba) {
            throw std::runtime_error(SDL_GetError());
        }
        Image image(rgba->w, rgba->h);
        for (int y = 0; y < rgba->h; ++y) {
            std::memcpy(image.pixels.data() + std::size_t(y) * rgba->w,
                        static_cast<const std::uint8_t*>(rgba->pixels) + std::size_t(y) * rgba->pitch,
                        std::size_t(rgba->w) * sizeof(Color));
        }
        SDL_DestroySurface(rgba);
        return image;
    }

    // A render-target texture of `width` x `height` in `texture`, made anew when its size changed.
    void ensure_target(SDL_Texture*& texture, int width, int height) {
        width = std::max(1, width);
        height = std::max(1, height);
        if (texture && (texture->w != width || texture->h != height)) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        if (!texture) {
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
            if (!texture) throw std::runtime_error(SDL_GetError());
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        }
    }

    // The screen filter stage for an effect with regions: effect_source holds the frame at its
    // logical size (`width` x `height`); effect_composite becomes the picture to show at
    // `shown_width` x `shown_height`: the frame as drawn, each region replaced by its filtered
    // picture (on the GPU, else read back once per region and filtered on the CPU).
    void filter_regions(const ScreenEffect& effect, int width, int height, int shown_width, int shown_height) {
        ensure_target(effect_composite, shown_width, shown_height);
        auto plain = [&](SDL_Texture* to) {
            SDL_SetRenderTarget(renderer, to);
            SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
            SDL_SetRenderScale(renderer, 1.0f, 1.0f);
            SDL_SetRenderViewport(renderer, nullptr);
        };
        plain(effect_composite);
        SDL_RenderTexture(renderer, effect_source, nullptr, nullptr);
        const std::size_t count = effect.regions.size();
        region_sources.resize(count, nullptr);
        region_outputs.resize(count, nullptr);
        region_uploads.resize(count, nullptr);
        SDL_GPURenderState* state = effect.shader ? shader_state(*effect.shader) : nullptr;
        const float sx = float(shown_width) / float(std::max(1, width));
        const float sy = float(shown_height) / float(std::max(1, height));
        std::string path;
        for (std::size_t k = 0; k < count; ++k) {
            const auto& r = effect.regions[k];
            // (Clipped to the frame.)
            const int x0 = std::clamp(r.x, 0, width), y0 = std::clamp(r.y, 0, height);
            const int x1 = std::clamp(r.x + r.width, 0, width), y1 = std::clamp(r.y + r.height, 0, height);
            if (x1 <= x0 || y1 <= y0) continue;
            const SDL_FRect source{float(x0), float(y0), float(x1 - x0), float(y1 - y0)};
            const SDL_FRect shown{float(x0) * sx, float(y0) * sy, float(x1 - x0) * sx, float(y1 - y0) * sy};
            // The region as a picture of its own (the shader samples a whole texture).
            ensure_target(region_sources[k], x1 - x0, y1 - y0);
            plain(region_sources[k]);
            SDL_RenderTexture(renderer, effect_source, &source, nullptr);
            SDL_Texture* filtered = region_sources[k];
            if (state && SDL_SetGPURenderStateFragmentUniforms(state, 0, r.uniforms.data(), Uint32(r.uniforms.size()))) {
                path = shaders[effect.shader->name].path;
                SDL_Texture* to = effect_composite;
                if (effect.raster_width > 0 && effect.raster_height > 0) {
                    ensure_target(region_outputs[k], effect.raster_width, effect.raster_height);
                    to = region_outputs[k];
                }
                plain(to);
                SDL_SetGPURenderState(renderer, state);
                SDL_RenderTexture(renderer, region_sources[k], nullptr, to == effect_composite ? &shown : nullptr);
                SDL_SetGPURenderState(renderer, nullptr);
                // (The uniforms are pushed when the draw runs: run it before the next region's.)
                SDL_FlushRenderer(renderer);
                if (to == effect_composite) continue;
                filtered = to;
            } else if (r.cpu) {
                const Image picture = r.cpu(read_pixels());
                auto& upload = region_uploads[k];
                if (upload && (upload->w != picture.width || upload->h != picture.height)) {
                    SDL_DestroyTexture(upload);
                    upload = nullptr;
                }
                if (!upload) {
                    upload = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                               picture.width, picture.height);
                    if (!upload) throw std::runtime_error(SDL_GetError());
                    SDL_SetTextureScaleMode(upload, SDL_SCALEMODE_NEAREST);
                }
                SDL_UpdateTexture(upload, nullptr, picture.pixels.data(), picture.width * int(sizeof(Color)));
                filtered = upload;
                path = "CPU";
            }
            plain(effect_composite);
            SDL_RenderTexture(renderer, filtered, nullptr, &shown);
        }
        if (path != effect_path && !path.empty()) {
            SDL_Log("Screen effect: %s per region (%s renderer)", path.c_str(), SDL_GetRendererName(renderer));
        }
        effect_path = path;
    }

    // Presents, writing the frame to `capture` first when one is asked for.
    void present_frame() {
        if (!capture.empty()) {
            if (SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr)) {
                if (SDL_Surface* rgba = SDL_ConvertSurface(pixels, SDL_PIXELFORMAT_RGBA32)) {
                    const auto path = capture.u8string();
                    SDL_SavePNG(rgba, reinterpret_cast<const char*>(path.c_str()));
                    SDL_DestroySurface(rgba);
                }
                SDL_DestroySurface(pixels);
            }
            capture.clear();
        }
        SDL_RenderPresent(renderer);
    }

    // Uploads an image into the streaming texture, recreating it on a size change.
    void upload(const Image& image) {
        if (!texture || texture_width != image.width || texture_height != image.height) {
            SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STREAMING, image.width, image.height);
            if (!texture) {
                throw std::runtime_error(SDL_GetError());
            }
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
            texture_width = image.width;
            texture_height = image.height;
        }
        SDL_UpdateTexture(texture, nullptr, image.pixels.data(), image.width * int(sizeof(Color)));
    }
};

Host::Host(std::string_view title) : impl_(std::make_unique<Implementation>()) {
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        throw std::runtime_error(SDL_GetError());
    }
    auto& i = *impl_;
    i.window = SDL_CreateWindow(std::string(title).c_str(), ui_width, ui_height,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
#ifdef __ANDROID__
                                | SDL_WINDOW_FULLSCREEN
#endif
                                );
    if (!i.window) {
        throw std::runtime_error(SDL_GetError());
    }
    // SDL's GPU renderer first (Vulkan, Metal), which runs the screen effects' shaders, telling
    // it the shader formats Reprise ships; where it is missing or fails, the platform's default
    // renderer (the effects then run on the CPU). A renderer chosen with the SDL_RENDER_DRIVER
    // hint or environment variable is used as it is.
    if (!SDL_GetHint(SDL_HINT_RENDER_DRIVER)) {
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, i.window);
        SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, "gpu");
        SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_MSL_BOOLEAN, true);
        i.renderer = SDL_CreateRendererWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (!i.renderer) {
        i.renderer = SDL_CreateRenderer(i.window, nullptr);
    }
    if (!i.renderer) {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_SetRenderVSync(i.renderer, 1);
    i.connect();
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, audio_sample_rate};
    i.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!i.audio) {
        throw std::runtime_error(std::string("Cannot open audio: ") + SDL_GetError());
    }
    clear_audio();
    if (!SDL_ResumeAudioStreamDevice(i.audio)) {
        throw std::runtime_error(SDL_GetError());
    }
}

Host::~Host() = default;

Controls Host::poll() {
    auto& i = *impl_;
    Controls input;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            input.quit = true;
        }
        if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            i.connect();
        }
        if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
            input.dropped_files.push_back(utf8_path(event.drop.data));
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            input.mouse_dx += event.motion.xrel;
            input.mouse_dy += event.motion.yrel;
        }
        if (event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_MOTION) {
            SDL_ConvertEventToRenderCoordinates(i.renderer, &event);
            i.fingers[event.tfinger.fingerID] = {event.tfinger.x, event.tfinger.y};
        }
        if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED) {
            i.fingers.erase(event.tfinger.fingerID);
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION && i.press_in_game) {
            SDL_ConvertEventToRenderCoordinates(i.renderer, &event);
            if (const auto at = i.frame_point(event.motion.x, event.motion.y))
                input.pointer_events.push_back({PointerEvent::Kind::move, (*at)[0], (*at)[1],
                                                event.motion.which == SDL_TOUCH_MOUSEID});
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT && i.press_in_game) {
            SDL_ConvertEventToRenderCoordinates(i.renderer, &event);
            i.press_in_game = false;
            if (const auto at = i.frame_point(event.button.x, event.button.y))
                input.pointer_events.push_back({PointerEvent::Kind::up, (*at)[0], (*at)[1],
                                                event.button.which == SDL_TOUCH_MOUSEID});
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            SDL_ConvertEventToRenderCoordinates(i.renderer, &event);
            if (const auto at = i.frame_point(event.button.x, event.button.y);
                at && (*at)[0] >= 0 && (*at)[1] >= 0 && (*at)[0] < float(i.game_width) && (*at)[1] < float(i.game_height)) {
                i.press_in_game = true;
                input.pointer_events.push_back({PointerEvent::Kind::down, (*at)[0], (*at)[1],
                                                event.button.which == SDL_TOUCH_MOUSEID});
            }
            input.click = true;
            if (i.ui_shown) {
                // UI screens use a ui_width x ui_height logical presentation.
                input.mouse_x = int(std::floor(event.button.x));
                input.mouse_y = int(std::floor(event.button.y));
            } else if (i.overlay_box.w > 0) {
                input.mouse_x = int((event.button.x - i.overlay_box.x) * ui_width / i.overlay_box.w);
                input.mouse_y = int((event.button.y - i.overlay_box.y) * ui_height / i.overlay_box.h);
            }
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            // Whole steps; trackpads deliver fractions, which add up here. The
            // value already follows the system's scroll direction setting.
            input.wheel_precise -= event.wheel.y;
            i.wheel_rest -= event.wheel.y;
            const int steps = int(i.wheel_rest);
            i.wheel_rest -= float(steps);
            input.wheel += steps;
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            const auto b = event.gbutton.button;
            input.accept |= b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START;
            input.back |= b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_BACK;
            input.menu |= b == SDL_GAMEPAD_BUTTON_GUIDE || b == SDL_GAMEPAD_BUTTON_BACK;
            input.nav_left |= b == SDL_GAMEPAD_BUTTON_DPAD_LEFT;
            input.nav_right |= b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
            input.nav_up |= b == SDL_GAMEPAD_BUTTON_DPAD_UP;
            input.nav_down |= b == SDL_GAMEPAD_BUTTON_DPAD_DOWN;
            input.page_up |= b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
            input.page_down |= b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            const auto key = event.key.key;
            const bool shortcut = event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI);
            input.quit |= shortcut && key == SDLK_Q;
            input.quick_save |= shortcut && key == SDLK_S;
            input.quick_load |= shortcut && key == SDLK_L;
            if (!shortcut) {
                input.back |= (key == SDLK_ESCAPE || key == SDLK_AC_BACK);
                input.menu |= (key == SDLK_ESCAPE || key == SDLK_AC_BACK);
                input.accept |= key == SDLK_RETURN || key == SDLK_KP_ENTER;
                input.remove |= key == SDLK_BACKSPACE || key == SDLK_DELETE;
                input.info |= key == SDLK_I;
                input.nav_left |= key == SDLK_LEFT || key == SDLK_A;
                input.nav_right |= key == SDLK_RIGHT || key == SDLK_D;
                input.nav_up |= key == SDLK_UP || key == SDLK_W;
                input.nav_down |= key == SDLK_DOWN || key == SDLK_S;
                input.toggle_mute |= key == SDLK_M;
                input.page_up |= key == SDLK_PAGEUP;
                input.page_down |= key == SDLK_PAGEDOWN;
            }
        }
    }
    const bool* k = SDL_GetKeyboardState(nullptr);
    // WASD only without Ctrl/Cmd, so shortcuts do not also move the joystick.
    const bool letters = !(SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI));
    input.left = k[SDL_SCANCODE_LEFT] || (letters && k[SDL_SCANCODE_A]);
    input.right = k[SDL_SCANCODE_RIGHT] || (letters && k[SDL_SCANCODE_D]);
    input.up = k[SDL_SCANCODE_UP] || (letters && k[SDL_SCANCODE_W]);
    input.down = k[SDL_SCANCODE_DOWN] || (letters && k[SDL_SCANCODE_S]);
    input.fire = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_Z];
    input.button2 = letters && k[SDL_SCANCODE_X];
    input.start = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];
    input.f1 = k[SDL_SCANCODE_F1] || (letters && k[SDL_SCANCODE_1]);
    input.f3 = k[SDL_SCANCODE_F3] || (letters && k[SDL_SCANCODE_3]);
    input.f5 = k[SDL_SCANCODE_F5] || (letters && k[SDL_SCANCODE_5]);
    input.f7 = k[SDL_SCANCODE_F7] || (letters && k[SDL_SCANCODE_7]);
    input.c64_space = letters && k[SDL_SCANCODE_P];
    // The C64 key matrix (row * 8 + column).
    static constexpr struct {
        SDL_Scancode scancode;
        int key;
    } c64_map[] = {
        {SDL_SCANCODE_BACKSPACE, 0},  {SDL_SCANCODE_RETURN, 1},     {SDL_SCANCODE_KP_ENTER, 1},
        {SDL_SCANCODE_3, 8},          {SDL_SCANCODE_W, 9},          {SDL_SCANCODE_A, 10},
        {SDL_SCANCODE_4, 11},         {SDL_SCANCODE_Z, 12},         {SDL_SCANCODE_S, 13},
        {SDL_SCANCODE_E, 14},         {SDL_SCANCODE_5, 16},         {SDL_SCANCODE_R, 17},
        {SDL_SCANCODE_D, 18},         {SDL_SCANCODE_6, 19},         {SDL_SCANCODE_C, 20},
        {SDL_SCANCODE_F, 21},         {SDL_SCANCODE_T, 22},         {SDL_SCANCODE_X, 23},
        {SDL_SCANCODE_7, 24},         {SDL_SCANCODE_Y, 25},         {SDL_SCANCODE_G, 26},
        {SDL_SCANCODE_8, 27},         {SDL_SCANCODE_B, 28},         {SDL_SCANCODE_H, 29},
        {SDL_SCANCODE_U, 30},         {SDL_SCANCODE_V, 31},         {SDL_SCANCODE_9, 32},
        {SDL_SCANCODE_I, 33},         {SDL_SCANCODE_J, 34},         {SDL_SCANCODE_0, 35},
        {SDL_SCANCODE_M, 36},         {SDL_SCANCODE_K, 37},         {SDL_SCANCODE_O, 38},
        {SDL_SCANCODE_N, 39},         {SDL_SCANCODE_P, 41},         {SDL_SCANCODE_L, 42},
        {SDL_SCANCODE_MINUS, 43},     {SDL_SCANCODE_PERIOD, 44},    {SDL_SCANCODE_APOSTROPHE, 45},
        {SDL_SCANCODE_COMMA, 47},     {SDL_SCANCODE_SEMICOLON, 50}, {SDL_SCANCODE_EQUALS, 53}, {SDL_SCANCODE_RIGHTBRACKET, 54},
        {SDL_SCANCODE_SLASH, 55},     {SDL_SCANCODE_1, 56},         {SDL_SCANCODE_2, 59},
        {SDL_SCANCODE_Q, 62},         {SDL_SCANCODE_TAB, 63},
    };
    if (letters) {
        for (const auto& m : c64_map) input.c64_keys[std::size_t(m.key)] = input.c64_keys[std::size_t(m.key)] || k[m.scancode];
    }
    // ... and with Ctrl held (not Cmd): Ctrl+letter keys for the games, the
    // shortcuts' letters left out.
    const auto mods = SDL_GetModState();
    if ((mods & SDL_KMOD_CTRL) && !(mods & SDL_KMOD_GUI)) {
        for (const auto& m : c64_map) {
            if (m.scancode == SDL_SCANCODE_Q || m.scancode == SDL_SCANCODE_S || m.scancode == SDL_SCANCODE_L) continue;
            input.ctrl_keys[std::size_t(m.key)] = input.ctrl_keys[std::size_t(m.key)] || k[m.scancode];
        }
    }
    if (i.gamepad) {
        auto button = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(i.gamepad, b); };
        const int x = SDL_GetGamepadAxis(i.gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        const int y = SDL_GetGamepadAxis(i.gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        input.left |= button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -12000;
        input.right |= button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 12000;
        input.up |= button(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -12000;
        input.down |= button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 12000;
        input.fire |= button(SDL_GAMEPAD_BUTTON_SOUTH);
        input.f7 |= button(SDL_GAMEPAD_BUTTON_START);
        input.button2 |= button(SDL_GAMEPAD_BUTTON_EAST);
        input.start |= button(SDL_GAMEPAD_BUTTON_START);
        input.f3 |= button(SDL_GAMEPAD_BUTTON_NORTH);
        input.f5 |= button(SDL_GAMEPAD_BUTTON_WEST);
    }
    // The second joystick: the numeric keypad (8 4 6 2, fire 0) or a second gamepad.
    input.up2 = k[SDL_SCANCODE_KP_8];
    input.down2 = k[SDL_SCANCODE_KP_2] || k[SDL_SCANCODE_KP_5];
    input.left2 = k[SDL_SCANCODE_KP_4];
    input.right2 = k[SDL_SCANCODE_KP_6];
    input.fire2 = k[SDL_SCANCODE_KP_0];
    if (i.gamepad2) {
        auto button = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(i.gamepad2, b); };
        const int x = SDL_GetGamepadAxis(i.gamepad2, SDL_GAMEPAD_AXIS_LEFTX);
        const int y = SDL_GetGamepadAxis(i.gamepad2, SDL_GAMEPAD_AXIS_LEFTY);
        input.left2 |= button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -12000;
        input.right2 |= button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 12000;
        input.up2 |= button(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -12000;
        input.down2 |= button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 12000;
        input.fire2 |= button(SDL_GAMEPAD_BUTTON_SOUTH);
    }
    // The mouse over the game frame, for games played with a mouse.
    float mx = 0, my = 0;
    const auto buttons = SDL_GetMouseState(&mx, &my);
    input.mouse_held = (buttons & SDL_BUTTON_LMASK) != 0;
    input.mouse_right_held = (buttons & SDL_BUTTON_RMASK) != 0;
    const auto held = SDL_GetModState();
    input.command_held = (held & SDL_KMOD_GUI) != 0;
    input.option_held = (held & SDL_KMOD_ALT) != 0;
    input.control_held = (held & SDL_KMOD_CTRL) != 0;
    input.shift_held = (held & SDL_KMOD_SHIFT) != 0;
    float rx = 0, ry = 0;
    if (i.ui_shown && SDL_GetMouseFocus() == i.window && SDL_RenderCoordinatesFromWindow(i.renderer, mx, my, &rx, &ry))
        input.ui_pointer = std::array<int, 2>{int(std::floor(rx)), int(std::floor(ry))};
    if (!i.ui_shown && i.game_box.w > 0 && SDL_RenderCoordinatesFromWindow(i.renderer, mx, my, &rx, &ry)) {
        const float fx = (rx - i.game_box.x) * float(i.game_width) / i.game_box.w;
        const float fy = (ry - i.game_box.y) * float(i.game_height) / i.game_box.h;
        if (fx >= 0 && fy >= 0 && fx < float(i.game_width) && fy < float(i.game_height)) {
            input.game_pointer = std::array<int, 2>{int(fx), int(fy)};
        }
    }
    if (i.accelerometer) {
        // SDL reports m/s^2 in the device's natural orientation (x right, y up the screen held
        // upright, z towards the viewer), gravity as "up"; turned into the window's axes.
        float d[3] = {};
        // (Nothing until the first real reading: a sensor just opened reads zero.)
        if (SDL_GetSensorData(i.accelerometer, d, 3) &&
            d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 0.25f * SDL_STANDARD_GRAVITY * SDL_STANDARD_GRAVITY) {
            const float x = d[0] / SDL_STANDARD_GRAVITY, y = d[1] / SDL_STANDARD_GRAVITY,
                        z = d[2] / SDL_STANDARD_GRAVITY;
            switch (SDL_GetCurrentDisplayOrientation(SDL_GetDisplayForWindow(i.window))) {
            case SDL_ORIENTATION_LANDSCAPE: // the device turned a quarter anticlockwise
                input.motion = std::array<float, 3>{-y, x, z};
                break;
            case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
                input.motion = std::array<float, 3>{y, -x, z};
                break;
            case SDL_ORIENTATION_PORTRAIT_FLIPPED:
                input.motion = std::array<float, 3>{-x, -y, z};
                break;
            default:
                input.motion = std::array<float, 3>{x, y, z};
                break;
            }
        }
    }
    for (const auto& [id, at] : i.fingers) {
        if (const auto point = i.frame_point(at[0], at[1])) {
            input.fingers.push_back({std::uint64_t(id), (*point)[0], (*point)[1]});
        }
    }
    // Only one pointer: the system's hidden over a game drawing its own.
    const bool hide = input.game_pointer && i.game_draws_pointer && i.overlay_box.w <= 0;
    if (hide != i.cursor_hidden && !i.mouse_captured) {
        hide ? SDL_HideCursor() : SDL_ShowCursor();
        i.cursor_hidden = hide;
    }
    return input;
}

void Host::set_game_draws_pointer(bool draws) {
    impl_->game_draws_pointer = draws;
}

void Host::set_mouse_captured(bool captured) {
    auto& i = *impl_;
    if (captured == i.mouse_captured) return;
    SDL_SetWindowRelativeMouseMode(i.window, captured);
    i.mouse_captured = captured;
}

double Host::seconds() const {
    return double(SDL_GetTicksNS()) / 1.0e9;
}

void Host::present(const Image& image) {
    auto& i = *impl_;
    i.ui_shown = true;
    i.upload(image);
    SDL_SetTextureScaleMode(i.texture, SDL_SCALEMODE_LINEAR);
    SDL_SetRenderLogicalPresentation(i.renderer, image.width, image.height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawColor(i.renderer, 0, 0, 0, 255);
    SDL_RenderClear(i.renderer);
    SDL_RenderTexture(i.renderer, i.texture, nullptr, nullptr);
    i.present_frame();
}

void Host::present_game(const Image& frame, double pixel_aspect, const Image* overlay, bool dim, const Image* note, float note_opacity, int note_offset, const ScreenEffect* effect) {
    auto& i = *impl_;
    if (effect) {
        // Upload software pictures, then use precisely the same GPU filter stage.
        i.upload(frame);
        SDL_SetTextureScaleMode(i.texture, SDL_SCALEMODE_NEAREST);
        present_game_gpu(frame.width, frame.height, pixel_aspect,
            [&](const GpuFrame& target) {
                SDL_RenderTexture(target.renderer, i.texture, nullptr, nullptr);
            }, overlay, dim, note, note_opacity, note_offset, effect);
        return;
    }
    i.ui_shown = false;
    i.upload(frame);
    SDL_SetTextureScaleMode(i.texture, SDL_SCALEMODE_NEAREST);
    // Without a logical presentation the renderer works in device pixels.
    SDL_SetRenderLogicalPresentation(i.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    int cw = ui_width, ch = ui_height;
    SDL_GetRenderOutputSize(i.renderer, &cw, &ch);
    SDL_SetRenderDrawColor(i.renderer, 0, 0, 0, 255);
    SDL_RenderClear(i.renderer);
    const double shown_width = frame.width * pixel_aspect;
    const double scale = std::min(cw / shown_width, double(ch) / frame.height);
    const float w = float(shown_width * scale), h = float(frame.height * scale);
    const SDL_FRect target{float(int((cw - w) / 2)), float(int((ch - h) / 2)), w, h};
    i.game_box = target;
    i.game_width = frame.width;
    i.game_height = frame.height;
    i.show_frame(i.texture, target, dim, overlay, note, note_opacity, note_offset, cw, ch);
}

void Host::present_game_gpu(int width, int height, double pixel_aspect,
                            const std::function<void(const GpuFrame&)>& draw, const Image* overlay, bool dim,
                            const Image* note, float note_opacity, int note_offset, const ScreenEffect* effect) {
    auto& i = *impl_;
    i.ui_shown = false;
    SDL_SetRenderLogicalPresentation(i.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    int cw = ui_width, ch = ui_height;
    SDL_GetRenderOutputSize(i.renderer, &cw, &ch);
    const double shown_width = width * pixel_aspect;
    const double scale = std::min(cw / shown_width, double(ch) / height);
    const int w = std::max(1, int(shown_width * scale)), h = std::max(1, int(height * scale));
    const SDL_FRect target{float((cw - w) / 2), float((ch - h) / 2), float(w), float(h)};
    i.game_box = target;
    i.game_width = width;
    i.game_height = height;
    if (effect && (effect->shader || effect->cpu)) {
        // The screen filter stage: the game draws at its logical size, the effect makes the
        // picture shown (on the GPU, or once read back on the CPU), then note and overlay.
        if (!i.effect_source || i.effect_width != width || i.effect_height != height) {
            SDL_DestroyTexture(i.effect_source);
            i.effect_source = SDL_CreateTexture(i.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                                std::max(1, width), std::max(1, height));
            if (!i.effect_source) {
                throw std::runtime_error(SDL_GetError());
            }
            SDL_SetTextureScaleMode(i.effect_source, SDL_SCALEMODE_NEAREST);
            i.effect_width = width;
            i.effect_height = height;
        }
        SDL_SetRenderTarget(i.renderer, i.effect_source);
        draw(GpuFrame{i.renderer, i.effect_source, width, height});
        if (!effect->regions.empty()) {
            // Each region filtered on its own, composed at the shown size over the frame as drawn.
            i.filter_regions(*effect, width, height, w, h);
            SDL_SetRenderTarget(i.renderer, nullptr);
            SDL_SetRenderLogicalPresentation(i.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
            SDL_SetRenderScale(i.renderer, 1.0f, 1.0f);
            SDL_SetRenderViewport(i.renderer, nullptr);
            SDL_SetRenderDrawColor(i.renderer, 0, 0, 0, 255);
            SDL_RenderClear(i.renderer);
            i.show_frame(i.effect_composite, target, dim, overlay, note, note_opacity, note_offset, cw, ch);
            return;
        }
        SDL_GPURenderState* state = effect->shader ? i.shader_state(*effect->shader) : nullptr;
        std::string path;
        SDL_Texture* shown = i.effect_source;
        if (state && SDL_SetGPURenderStateFragmentUniforms(state, 0, effect->uniforms.data(),
                                                           Uint32(effect->uniforms.size()))) {
            path = i.shaders[effect->shader->name].path;
        } else if (effect->cpu) {
            state = nullptr;
            const Image picture = effect->cpu(i.read_pixels());
            i.upload(picture);
            SDL_SetTextureScaleMode(i.texture, SDL_SCALEMODE_NEAREST);
            shown = i.texture;
            path = "CPU";
        } else {
            state = nullptr; // (no form of the effect can run here: the frame as drawn)
        }
        if (path != i.effect_path && !path.empty()) {
            SDL_Log("Screen effect: %s (%s renderer)", path.c_str(), SDL_GetRendererName(i.renderer));
        }
        i.effect_path = path;
        if (state && effect->raster_width > 0 && effect->raster_height > 0) {
            if (i.effect_output && (i.effect_output->w != effect->raster_width ||
                                    i.effect_output->h != effect->raster_height)) {
                SDL_DestroyTexture(i.effect_output);
                i.effect_output = nullptr;
            }
            if (!i.effect_output) {
                i.effect_output = SDL_CreateTexture(i.renderer, SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET, effect->raster_width, effect->raster_height);
                if (!i.effect_output) throw std::runtime_error(SDL_GetError());
                SDL_SetTextureScaleMode(i.effect_output, SDL_SCALEMODE_NEAREST);
            }
            SDL_SetRenderTarget(i.renderer, i.effect_output);
            SDL_SetRenderLogicalPresentation(i.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
            SDL_SetRenderScale(i.renderer, 1.0f, 1.0f);
            SDL_SetRenderViewport(i.renderer, nullptr);
            SDL_SetGPURenderState(i.renderer, state);
            SDL_RenderTexture(i.renderer, shown, nullptr, nullptr);
            SDL_SetGPURenderState(i.renderer, nullptr);
            shown = i.effect_output;
            state = nullptr;
        }
        SDL_SetRenderTarget(i.renderer, nullptr);
        SDL_SetRenderLogicalPresentation(i.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderScale(i.renderer, 1.0f, 1.0f);
        SDL_SetRenderViewport(i.renderer, nullptr);
        SDL_SetRenderDrawColor(i.renderer, 0, 0, 0, 255);
        SDL_RenderClear(i.renderer);
        i.show_frame(shown, target, dim, overlay, note, note_opacity, note_offset, cw, ch, state);
        return;
    }
    if (!i.gpu_target || i.gpu_width != w || i.gpu_height != h) {
        SDL_DestroyTexture(i.gpu_target);
        i.gpu_target = SDL_CreateTexture(i.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!i.gpu_target) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_SetTextureScaleMode(i.gpu_target, SDL_SCALEMODE_NEAREST); // shown 1:1
        i.gpu_width = w;
        i.gpu_height = h;
    }
    // The game draws first, into its own target; then the window is composed as for
    // present_game.
    SDL_SetRenderTarget(i.renderer, i.gpu_target);
    draw(GpuFrame{i.renderer, i.gpu_target, w, h});
    SDL_SetRenderTarget(i.renderer, nullptr);
    SDL_SetRenderLogicalPresentation(i.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderScale(i.renderer, 1.0f, 1.0f);
    SDL_SetRenderViewport(i.renderer, nullptr);
    SDL_SetRenderDrawColor(i.renderer, 0, 0, 0, 255);
    SDL_RenderClear(i.renderer);
    i.show_frame(i.gpu_target, target, dim, overlay, note, note_opacity, note_offset, cw, ch);
}

Image Host::capture(int width, int height, const std::function<void(const GpuFrame&)>& draw) {
    auto& i = *impl_;
    SDL_Texture* texture = SDL_CreateTexture(i.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    if (!texture) {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_Texture* previous = SDL_GetRenderTarget(i.renderer);
    SDL_SetRenderTarget(i.renderer, texture);
    draw(GpuFrame{i.renderer, texture, width, height});
    SDL_SetRenderTarget(i.renderer, texture);
    std::optional<Image> image;
    std::string error;
    try {
        image = i.read_pixels();
    } catch (const std::exception& e) {
        error = e.what();
    }
    SDL_SetRenderTarget(i.renderer, previous);
    SDL_DestroyTexture(texture);
    if (!image) {
        throw std::runtime_error(error);
    }
    return std::move(*image);
}

SDL_GPURenderState* Host::Implementation::shader_state(const FragmentShader& source) {
    const auto found = shaders.find(source.name);
    if (found != shaders.end()) {
        return found->second.state;
    }
    CompiledShader& compiled = shaders[source.name];
    SDL_GPUDevice* device = SDL_GetGPURendererDevice(renderer);
    if (!device) {
        return nullptr; // not SDL's GPU renderer
    }
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderCreateInfo info{};
    if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) && !source.spirv.empty()) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = source.spirv.data();
        info.code_size = source.spirv.size();
        info.entrypoint = "main";
        compiled.path = "GPU shader (SPIR-V)";
    } else if ((formats & SDL_GPU_SHADERFORMAT_MSL) && !source.msl.empty()) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.code = source.msl.data();
        info.code_size = source.msl.size();
        info.entrypoint = "main0";
        compiled.path = "GPU shader (MSL)";
    } else {
        return nullptr;
    }
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 1;
    info.num_uniform_buffers = 1;
    compiled.shader = SDL_CreateGPUShader(device, &info);
    if (!compiled.shader) {
        SDL_Log("Screen effect %s: the shader was not accepted (%s)", source.name, SDL_GetError());
        return nullptr;
    }
    SDL_GPURenderStateCreateInfo state{};
    state.fragment_shader = compiled.shader;
    compiled.state = SDL_CreateGPURenderState(renderer, &state);
    if (!compiled.state) {
        SDL_Log("Screen effect %s: no render state (%s)", source.name, SDL_GetError());
    }
    return compiled.state;
}

std::string Host::screen_effect_path() const {
    return impl_->effect_path;
}

std::string Host::renderer_name() const {
    const char* name = SDL_GetRendererName(impl_->renderer);
    return name ? name : "";
}

void Host::Implementation::show_frame(SDL_Texture* frame, const SDL_FRect& target, bool dim, const Image* overlay,
                                      const Image* note, float note_opacity, int note_offset, int cw, int ch,
                                      SDL_GPURenderState* effect) {
    auto& i = *this;
    const float w = target.w, h = target.h;
    const Uint8 brightness = dim ? 90 : 255;
    SDL_SetTextureColorMod(frame, brightness, brightness, brightness);
    // A screen effect's shader for the frame alone: note and overlay are drawn without it.
    if (effect) SDL_SetGPURenderState(i.renderer, effect);
    SDL_RenderTexture(i.renderer, frame, nullptr, &target);
    if (effect) SDL_SetGPURenderState(i.renderer, nullptr);
    SDL_SetTextureColorMod(frame, 255, 255, 255);
    if (note && note->width > 0 && note->height > 0) {
        if (i.note_texture && (i.note_texture->w != note->width || i.note_texture->h != note->height)) {
            SDL_DestroyTexture(i.note_texture);
            i.note_texture = nullptr;
        }
        if (!i.note_texture) {
            i.note_texture = SDL_CreateTexture(i.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                               note->width, note->height);
            SDL_SetTextureBlendMode(i.note_texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(i.note_texture, SDL_SCALEMODE_NEAREST);
        }
        SDL_UpdateTexture(i.note_texture, nullptr, note->pixels.data(), note->width * int(sizeof(Color)));
        const float margin = float(std::max(4, int(std::min(w, h) / 60)));
        const SDL_FRect at{float(int(target.x + w - note->width - margin)), float(int(target.y + margin) - note_offset),
                           float(note->width), float(note->height)};
        SDL_SetTextureAlphaMod(i.note_texture, Uint8(std::clamp(note_opacity, 0.0f, 1.0f) * 255));
        SDL_RenderTexture(i.renderer, i.note_texture, nullptr, &at);
    }
    i.overlay_box = {};
    if (overlay) {
        if (!i.overlay_texture) {
            i.overlay_texture = SDL_CreateTexture(i.renderer, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STREAMING, ui_width, ui_height);
            SDL_SetTextureBlendMode(i.overlay_texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(i.overlay_texture, SDL_SCALEMODE_LINEAR);
        }
        SDL_UpdateTexture(i.overlay_texture, nullptr, overlay->pixels.data(),
                          overlay->width * int(sizeof(Color)));
        const double ui_scale = std::min(double(cw) / ui_width, double(ch) / ui_height);
        const float ow = float(ui_width * ui_scale), oh = float(ui_height * ui_scale);
        const SDL_FRect box{float(int((cw - ow) / 2)), float(int((ch - oh) / 2)), ow, oh};
        SDL_RenderTexture(i.renderer, i.overlay_texture, nullptr, &box);
        // Mouse clicks map to the overlay's design space.
        i.ui_shown = false;
        i.overlay_box = box;
    }
    i.present_frame();
}

void Host::set_motion_wanted(bool wanted) {
    auto& i = *impl_;
    if (!wanted) {
        if (i.accelerometer) {
            SDL_CloseSensor(i.accelerometer);
            i.accelerometer = nullptr;
        }
        return;
    }
    if (i.accelerometer || i.no_accelerometer) {
        return;
    }
    if (!i.sensors_started) {
        i.sensors_started = true;
        if (!SDL_InitSubSystem(SDL_INIT_SENSOR)) {
            i.no_accelerometer = true;
            return;
        }
    }
    int count = 0;
    SDL_SensorID* ids = SDL_GetSensors(&count);
    for (int k = 0; ids && k < count && !i.accelerometer; ++k) {
        if (SDL_GetSensorTypeForID(ids[k]) == SDL_SENSOR_ACCEL) {
            i.accelerometer = SDL_OpenSensor(ids[k]);
        }
    }
    SDL_free(ids);
    i.no_accelerometer = i.accelerometer == nullptr;
}

void Host::capture_next_frame(const std::filesystem::path& file) {
    impl_->capture = file;
}

std::array<int, 2> Host::canvas_size() const {
    int cw = ui_width, ch = ui_height;
    SDL_GetRenderOutputSize(impl_->renderer, &cw, &ch);
    return {cw, ch};
}

void Host::pause_audio(bool paused) {
    if (paused) {
        SDL_PauseAudioStreamDevice(impl_->audio);
    } else {
        clear_audio();
        SDL_ResumeAudioStreamDevice(impl_->audio);
    }
}

void Host::audio(std::span<const float> samples) {
    // Bound the latency after a stall (more than 0.2 s queued): start afresh.
    if (SDL_GetAudioStreamQueued(impl_->audio) > int(audio_sample_rate * sizeof(float) * 2 / 5)) {
        clear_audio();
    }
    if (!SDL_PutAudioStreamData(impl_->audio, samples.data(), int(samples.size_bytes()))) {
        throw std::runtime_error(SDL_GetError());
    }
}

void Host::clear_audio() {
    if (!SDL_ClearAudioStream(impl_->audio)) {
        throw std::runtime_error(SDL_GetError());
    }
    // Two video frames of headroom absorb display scheduling jitter.
    const std::array<float, 3200> silence{};
    if (!SDL_PutAudioStreamData(impl_->audio, silence.data(), sizeof(silence))) {
        throw std::runtime_error(SDL_GetError());
    }
}

void Host::mute(bool muted) {
    if (!SDL_SetAudioStreamGain(impl_->audio, muted ? 0.0f : 1.0f)) {
        throw std::runtime_error(SDL_GetError());
    }
}

std::filesystem::path data_directory() {
    char* p = SDL_GetPrefPath("NativeReconstruction", "Reprise");
    if (!p) {
        throw std::runtime_error(SDL_GetError());
    }
    auto result = utf8_path(p);
    SDL_free(p);
    return result;
}

void Host::choose_image() {
    auto state = impl_->dialog;
    {
        std::lock_guard guard(state->mutex);
        if (state->pending) {
            return;
        }
        state->pending = true;
        state->result.reset();
    }
    auto* context = new std::shared_ptr<Implementation::Dialog>(state);
    static const SDL_DialogFileFilter filters[] = {{"Game images", "tap;t64;d64;prg;adf;ipa;nds;cci"},
                                                   {"All files", "*"}};
    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* paths, int) {
            std::unique_ptr<std::shared_ptr<Implementation::Dialog>> handle(
                static_cast<std::shared_ptr<Implementation::Dialog>*>(userdata));
            FileChoice choice;
            if (!paths) {
                choice.error = "Could not open the file picker. You can drag an image onto the "
                               "window instead.";
            } else {
                for (auto* path = paths; *path; ++path) {
                    choice.files.push_back(utf8_path(*path));
                }
            }
            std::lock_guard guard((*handle)->mutex);
            (*handle)->pending = false;
            (*handle)->result = std::move(choice);
        },
        context, impl_->window, filters, 2, nullptr, true);
}

std::optional<Host::FileChoice> Host::take_file_choice() {
    std::lock_guard guard(impl_->dialog->mutex);
    auto result = std::move(impl_->dialog->result);
    impl_->dialog->result.reset();
    // Return Android content URIs immediately too. Library opens every image through
    // FileSource/SDL IO on the import worker, including small in-memory importers.
    // Copying documents here used to block the UI after the picker closed.

    return result;
}

void save_png(const Image& image, const std::filesystem::path& file) {
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_RGBA32,
                              const_cast<Color*>(image.pixels.data()), image.width * 4);
    if (!surface) {
        throw std::runtime_error(SDL_GetError());
    }
    const auto path = file.u8string();
    const bool saved = SDL_SavePNG(surface, reinterpret_cast<const char*>(path.c_str()));
    SDL_DestroySurface(surface);
    if (!saved) {
        throw std::runtime_error(SDL_GetError());
    }
}

} // namespace platform
