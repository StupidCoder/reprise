// SDL 3 implementation of the host platform (see host.hpp).

#include "platform/host.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
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
    SDL_FRect overlay_box{}; // where the last game overlay was shown (device pixels)
    SDL_Gamepad* gamepad{};
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
        SDL_DestroyAudioStream(audio);
        SDL_DestroyTexture(texture);
        SDL_DestroyTexture(overlay_texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void connect() {
        if (gamepad && SDL_GamepadConnected(gamepad)) {
            return;
        }
        if (gamepad) {
            SDL_CloseGamepad(gamepad);
            gamepad = nullptr;
        }
        int count = 0;
        auto ids = SDL_GetGamepads(&count);
        if (count) {
            gamepad = SDL_OpenGamepad(ids[0]);
        }
        SDL_free(ids);
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
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        throw std::runtime_error(SDL_GetError());
    }
    auto& i = *impl_;
    if (!SDL_CreateWindowAndRenderer(std::string(title).c_str(), ui_width, ui_height,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &i.window, &i.renderer)) {
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
            input.dropped_file = utf8_path(event.drop.data);
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            SDL_ConvertEventToRenderCoordinates(i.renderer, &event);
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
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            const auto b = event.gbutton.button;
            input.accept |= b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START;
            input.back |= b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_BACK;
            input.menu |= b == SDL_GAMEPAD_BUTTON_GUIDE || b == SDL_GAMEPAD_BUTTON_BACK;
            input.nav_left |= b == SDL_GAMEPAD_BUTTON_DPAD_LEFT;
            input.nav_right |= b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
            input.nav_up |= b == SDL_GAMEPAD_BUTTON_DPAD_UP;
            input.nav_down |= b == SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            const auto key = event.key.key;
            const bool shortcut = event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI);
            input.quit |= shortcut && key == SDLK_Q;
            input.quick_save |= shortcut && key == SDLK_S;
            input.quick_load |= shortcut && key == SDLK_L;
            if (!shortcut) {
                input.back |= key == SDLK_ESCAPE;
                input.menu |= key == SDLK_ESCAPE;
                input.accept |= key == SDLK_RETURN || key == SDLK_KP_ENTER;
                input.remove |= key == SDLK_BACKSPACE || key == SDLK_DELETE;
                input.info |= key == SDLK_I;
                input.nav_left |= key == SDLK_LEFT || key == SDLK_A;
                input.nav_right |= key == SDLK_RIGHT || key == SDLK_D;
                input.nav_up |= key == SDLK_UP || key == SDLK_W;
                input.nav_down |= key == SDLK_DOWN || key == SDLK_S;
                input.toggle_mute |= key == SDLK_M;
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
    input.f1 = k[SDL_SCANCODE_F1] || (letters && k[SDL_SCANCODE_1]);
    input.f3 = k[SDL_SCANCODE_F3] || (letters && k[SDL_SCANCODE_3]);
    input.f5 = k[SDL_SCANCODE_F5] || (letters && k[SDL_SCANCODE_5]);
    input.f7 = k[SDL_SCANCODE_F7] || (letters && k[SDL_SCANCODE_7]);
    input.c64_space = letters && k[SDL_SCANCODE_P];
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
        input.f3 |= button(SDL_GAMEPAD_BUTTON_NORTH);
        input.f5 |= button(SDL_GAMEPAD_BUTTON_WEST);
    }
    return input;
}

double Host::seconds() const {
    return double(SDL_GetTicksNS()) / 1.0e9;
}

void Host::present(const Image& image) {
    auto& i = *impl_;
    i.ui_shown = true;
    i.upload(image);
    SDL_SetRenderLogicalPresentation(i.renderer, image.width, image.height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawColor(i.renderer, 0, 0, 0, 255);
    SDL_RenderClear(i.renderer);
    SDL_RenderTexture(i.renderer, i.texture, nullptr, nullptr);
    SDL_RenderPresent(i.renderer);
}

void Host::present_game(const Image& frame, double pixel_aspect, const Image* overlay, bool dim) {
    auto& i = *impl_;
    i.ui_shown = false;
    i.upload(frame);
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
    const Uint8 brightness = dim ? 90 : 255;
    SDL_SetTextureColorMod(i.texture, brightness, brightness, brightness);
    SDL_RenderTexture(i.renderer, i.texture, nullptr, &target);
    SDL_SetTextureColorMod(i.texture, 255, 255, 255);
    i.overlay_box = {};
    if (overlay) {
        if (!i.overlay_texture) {
            i.overlay_texture = SDL_CreateTexture(i.renderer, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STREAMING, ui_width, ui_height);
            SDL_SetTextureBlendMode(i.overlay_texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(i.overlay_texture, SDL_SCALEMODE_NEAREST);
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
    SDL_RenderPresent(i.renderer);
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
    char* p = SDL_GetPrefPath("NativeReconstruction", "Joyport");
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
    static const SDL_DialogFileFilter filters[] = {{"C64 and Amiga images", "tap;t64;d64;prg;adf"},
                                                   {"All files", "*"}};
    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* paths, int) {
            std::unique_ptr<std::shared_ptr<Implementation::Dialog>> handle(
                static_cast<std::shared_ptr<Implementation::Dialog>*>(userdata));
            FileChoice choice;
            if (!paths) {
                choice.error = "Could not open the file picker. You can drag an image onto the "
                               "window instead.";
            } else if (paths[0]) {
                choice.file = utf8_path(paths[0]);
            }
            std::lock_guard guard((*handle)->mutex);
            (*handle)->pending = false;
            (*handle)->result = std::move(choice);
        },
        context, impl_->window, filters, 2, nullptr, false);
}

std::optional<Host::FileChoice> Host::take_file_choice() {
    std::lock_guard guard(impl_->dialog->mutex);
    auto result = std::move(impl_->dialog->result);
    impl_->dialog->result.reset();
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
