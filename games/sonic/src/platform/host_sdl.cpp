// SDL 3 implementation of the host platform (see host.hpp).

#include "gamegear/lcd_filter.hpp"
#include "platform/host.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace platform {
struct Host::Implementation {
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* texture{};
    SDL_Texture* overlay_texture{};
    SDL_Texture* badge_texture{};
    // LCD look: filtered "diffusion" copy and the cached subpixel mask.
    SDL_Texture* glow_texture{};
    SDL_Texture* mask_texture{};
    int glow_width{}, glow_height{}, mask_width{}, mask_height{};
    std::array<double, 6> mask_key{};
    gg::LcdFilter lcd;
    bool lcd_active{};
    double last_present{-1};
    int overlay_width{}, overlay_height{}, badge_width{}, badge_height{};
    int logical_width{960}, logical_height{640};
    SDL_Gamepad* gamepad{};
    SDL_AudioStream* audio{};
    int width{}, height{};

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
        SDL_DestroyTexture(overlay_texture);
        SDL_DestroyTexture(badge_texture);
        SDL_DestroyTexture(glow_texture);
        SDL_DestroyTexture(mask_texture);
        SDL_DestroyTexture(texture);
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
};

Host::Host(std::string_view title) : impl_(std::make_unique<Implementation>()) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        throw std::runtime_error(SDL_GetError());
    }
    auto& i = *impl_;
    if (!SDL_CreateWindowAndRenderer(std::string(title).c_str(), 960, 640,
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
            // Game canvases follow the window; UI overlays keep a 960x640 design space.
            const double scale =
                std::max(0.001, std::min(i.logical_width / 960.0, i.logical_height / 640.0));
            input.mouse_x =
                int(std::floor((event.button.x - (i.logical_width - 960 * scale) / 2) / scale));
            input.mouse_y =
                int(std::floor((event.button.y - (i.logical_height - 640 * scale) / 2) / scale));
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            auto b = event.gbutton.button;
            input.accept |= b == SDL_GAMEPAD_BUTTON_SOUTH;
            input.back |= b == SDL_GAMEPAD_BUTTON_EAST;
            input.start_pressed |= b == SDL_GAMEPAD_BUTTON_START;
            input.menu |= b == SDL_GAMEPAD_BUTTON_GUIDE;
            input.nav_left |= b == SDL_GAMEPAD_BUTTON_DPAD_LEFT;
            input.nav_right |= b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
            input.nav_up |= b == SDL_GAMEPAD_BUTTON_DPAD_UP;
            input.nav_down |= b == SDL_GAMEPAD_BUTTON_DPAD_DOWN;
            input.prev_group |= b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
            input.next_group |= b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
            input.quick_save |= event.key.key == SDLK_S;
            input.quick_load |= event.key.key == SDLK_L;
        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            input.menu |= event.key.key == SDLK_ESCAPE;
            input.back |= event.key.key == SDLK_ESCAPE;
            input.accept |= event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE;
            input.start_pressed |= event.key.key == SDLK_RETURN;
            input.nav_left |= event.key.key == SDLK_LEFT || event.key.key == SDLK_A;
            input.nav_right |= event.key.key == SDLK_RIGHT || event.key.key == SDLK_D;
            input.nav_up |= event.key.key == SDLK_UP || event.key.key == SDLK_W;
            input.nav_down |= event.key.key == SDLK_DOWN || event.key.key == SDLK_S;
            input.space_pressed |= event.key.key == SDLK_SPACE;
            input.stop_playback |= event.key.key == SDLK_BACKSPACE;
            input.reset |= event.key.key == SDLK_R;
            input.toggle_view |= event.key.key == SDLK_TAB;
            input.toggle_mute |= event.key.key == SDLK_M;
            input.prev_group |= event.key.key == SDLK_Q;
            input.next_group |= event.key.key == SDLK_E;
        }
    }
    const bool* k = SDL_GetKeyboardState(nullptr);
    // WASD only without Ctrl/Cmd, so Ctrl+S does not also press Down.
    const bool letters = !(SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI));
    input.left = k[SDL_SCANCODE_LEFT] || (letters && k[SDL_SCANCODE_A]);
    input.right = k[SDL_SCANCODE_RIGHT] || (letters && k[SDL_SCANCODE_D]);
    input.up = k[SDL_SCANCODE_UP] || (letters && k[SDL_SCANCODE_W]);
    input.down = k[SDL_SCANCODE_DOWN] || (letters && k[SDL_SCANCODE_S]);
    input.start = k[SDL_SCANCODE_RETURN];
    input.button1 = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_Z];
    input.button2 = k[SDL_SCANCODE_X];
    if (i.gamepad) {
        auto button = [&](SDL_GamepadButton b) {
            return SDL_GetGamepadButton(i.gamepad, b);
        };
        const int x = SDL_GetGamepadAxis(i.gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        const int y = SDL_GetGamepadAxis(i.gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        input.left |= button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -12000;
        input.right |= button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 12000;
        input.up |= button(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -12000;
        input.down |= button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 12000;
        input.start |= button(SDL_GAMEPAD_BUTTON_START);
        input.button1 |= button(SDL_GAMEPAD_BUTTON_SOUTH);
        input.button2 |= button(SDL_GAMEPAD_BUTTON_EAST);
    }
    return input;
}

std::array<int, 2> Host::canvas_size() const {
    int width = 960, height = 640;
    SDL_GetWindowSize(impl_->window, &width, &height);
    return {std::max(width, 1), std::max(height, 1)};
}

double Host::seconds() const {
    return double(SDL_GetTicksNS()) / 1.0e9;
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

namespace {
void upload(SDL_Renderer* renderer, SDL_Texture*& texture, int& width, int& height,
            const Image& image, SDL_BlendMode blend) {
    if (!texture || width != image.width || height != image.height) {
        SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                    image.width, image.height);
        if (!texture) {
            throw std::runtime_error(SDL_GetError());
        }
        width = image.width;
        height = image.height;
        if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST) ||
            !SDL_SetTextureBlendMode(texture, blend)) {
            throw std::runtime_error(SDL_GetError());
        }
    }
    if (!SDL_UpdateTexture(texture, nullptr, image.pixels.data(), image.width * sizeof(Color))) {
        throw std::runtime_error(SDL_GetError());
    }
}

SDL_FRect fit(int width, int height, int cw, int ch) {
    const double scale = std::min(double(cw) / width, double(ch) / height);
    const float w = std::max(1, int(width * scale)), h = std::max(1, int(height * scale));
    return {float(int((cw - w) / 2)), float(int((ch - h) / 2)), w, h};
}
} // namespace

void Host::present_game(const Image& frame, bool fill, bool paused, const Image* overlay,
                        const Image* badge, bool lcd) {
    auto& i = *impl_;
    const auto canvas = canvas_size();
    const int cw = canvas[0], ch = canvas[1];
    i.logical_width = cw;
    i.logical_height = ch;
    SDL_SetRenderLogicalPresentation(i.renderer, cw, ch, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawColor(i.renderer, 0, 0, 0, 255);
    SDL_RenderClear(i.renderer);
    const double now = seconds();
    if (lcd && !i.lcd_active) {
        i.lcd.reset();
    }
    i.lcd_active = lcd;
    const double elapsed = i.last_present < 0 ? 0 : now - i.last_present;
    i.last_present = now;
    const Image& shown = lcd ? i.lcd.apply(frame, elapsed) : frame;
    upload(i.renderer, i.texture, i.width, i.height, shown, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(i.texture, SDL_SCALEMODE_NEAREST);
    const Uint8 brightness = paused ? 85 : 255;
    SDL_SetTextureColorMod(i.texture, brightness, brightness, brightness);
    auto destination = fit(frame.width, frame.height, cw, ch);
    SDL_FRect source{0, 0, float(frame.width), float(frame.height)};
    const double cover = std::max(double(cw) / frame.width, double(ch) / frame.height);
    if (fill && frame.width - cw / cover < 2 && frame.height - ch / cover < 2) {
        // Crop only the logical buffer's rounding margin. Water has already
        // been resolved into these pixels; no display-resolution resampling.
        // A view capped at its aspect limit is letterboxed by fit() instead.
        source.w = float(cw / cover);
        source.h = float(ch / cover);
        source.x = (frame.width - source.w) / 2;
        source.y = (frame.height - source.h) / 2;
        destination = {0, 0, float(cw), float(ch)};
    }
    int ow = cw, oh = ch;
    SDL_GetRenderOutputSize(i.renderer, &ow, &oh);
    const double ratio = double(ow) / cw;
    if (lcd) {
        // Integer scaling: a fractional number of device pixels per dot makes
        // the subpixel mask beat against the device grid (moire). Show the
        // whole logical frame at the largest whole-number device scale that
        // fits, centred with black borders. The logical view size is kept,
        // because the expanded view's size also drives object activation.
        const int k =
            int(std::floor(std::min(double(ow) / frame.width, double(oh) / frame.height) + 1e-6));
        if (k >= 3) {
            const int w = frame.width * k, h = frame.height * k;
            source = {0, 0, float(frame.width), float(frame.height)};
            destination = {float(((ow - w) / 2) / ratio), float(((oh - h) / 2) / ratio),
                           float(w / ratio), float(h / ratio)};
        }
    }
    SDL_RenderTexture(i.renderer, i.texture, &source, &destination);
    if (lcd) {
        // Subpixel structure at device resolution: vertical R/G/B stripes and
        // a dark matrix, multiplied over the nearest-neighbour dots. Then a
        // bilinear copy is added back for brightness and LCD diffusion.
        const double cell_w = destination.w / source.w * ratio,
                     cell_h = destination.h / source.h * ratio;
        const std::array<double, 6> key{double(destination.w * ratio),
                                        double(destination.h * ratio),
                                        cell_w,
                                        cell_h,
                                        source.x,
                                        source.y};
        if (!i.mask_texture || key != i.mask_key) {
            i.mask_key = key;
            const Image mask =
                gg::LcdFilter::mask(int(std::ceil(key[0])), int(std::ceil(key[1])), cell_w, cell_h,
                                    -source.x * cell_w, -source.y * cell_h);
            upload(i.renderer, i.mask_texture, i.mask_width, i.mask_height, mask,
                   SDL_BLENDMODE_MOD);
        }
        SDL_RenderTexture(i.renderer, i.mask_texture, nullptr, &destination);
        upload(i.renderer, i.glow_texture, i.glow_width, i.glow_height, shown, SDL_BLENDMODE_ADD);
        SDL_SetTextureScaleMode(i.glow_texture, SDL_SCALEMODE_LINEAR);
        const Uint8 glow = Uint8(gg::LcdFilter::glow * brightness / 255);
        SDL_SetTextureColorMod(i.glow_texture, glow, glow, glow);
        SDL_RenderTexture(i.renderer, i.glow_texture, &source, &destination);
    }
    // Reset modulation because the same scene texture also serves shell screens.
    SDL_SetTextureColorMod(i.texture, 255, 255, 255);
    if (overlay) {
        upload(i.renderer, i.overlay_texture, i.overlay_width, i.overlay_height, *overlay,
               SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(i.overlay_texture, SDL_SCALEMODE_LINEAR);
        auto target = fit(overlay->width, overlay->height, cw, ch);
        SDL_RenderTexture(i.renderer, i.overlay_texture, nullptr, &target);
    }
    if (badge) {
        upload(i.renderer, i.badge_texture, i.badge_width, i.badge_height, *badge,
               SDL_BLENDMODE_NONE);
        const float padding = 3 * std::clamp(ch / 320, 2, 4);
        SDL_FRect target{cw - badge->width - padding, padding, float(badge->width),
                         float(badge->height)};
        SDL_RenderTexture(i.renderer, i.badge_texture, nullptr, &target);
    }
    SDL_RenderPresent(i.renderer);
}

void Host::present(const Image& image, std::string_view) {
    auto& i = *impl_;
    i.logical_width = image.width;
    i.logical_height = image.height;
    if (i.width != image.width || i.height != image.height) {
        SDL_DestroyTexture(i.texture);
        i.texture = SDL_CreateTexture(i.renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STREAMING, image.width, image.height);
        if (!i.texture) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_SetTextureScaleMode(i.texture, SDL_SCALEMODE_NEAREST);
        i.width = image.width;
        i.height = image.height;
    }
    SDL_SetTextureScaleMode(i.texture, SDL_SCALEMODE_LINEAR);
    SDL_UpdateTexture(i.texture, nullptr, image.pixels.data(), image.width * int(sizeof(Color)));
    SDL_SetRenderLogicalPresentation(i.renderer, image.width, image.height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawColor(i.renderer, 18, 22, 28, 255);
    SDL_RenderClear(i.renderer);
    SDL_RenderTexture(i.renderer, i.texture, nullptr, nullptr);
    SDL_RenderPresent(i.renderer);
    SDL_Delay(1);
}

void Host::pause_audio(bool paused) {
    if (paused) {
        if (!SDL_PauseAudioStreamDevice(impl_->audio)) {
            throw std::runtime_error(SDL_GetError());
        }
    } else {
        clear_audio();
        if (!SDL_ResumeAudioStreamDevice(impl_->audio)) {
            throw std::runtime_error(SDL_GetError());
        }
    }
}

std::filesystem::path Host::data_directory() const {
    char* p = SDL_GetPrefPath("NativeReconstruction", "BlueStreak");
    if (!p) {
        throw std::runtime_error(SDL_GetError());
    }
    auto result = utf8_path(p);
    SDL_free(p);
    return result;
}

void Host::choose_rom() {
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
    static const SDL_DialogFileFilter filters[] = {{"Game Gear and GBA ROMs", "gg;gba;bin"},
                                                   {"All files", "*"}};
    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* paths, int) {
            std::unique_ptr<std::shared_ptr<Implementation::Dialog>> handle(
                static_cast<std::shared_ptr<Implementation::Dialog>*>(userdata));
            FileChoice choice;
            if (!paths) {
                choice.error =
                    "Could not open the file picker. You can drag a ROM onto the library instead.";
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

} // namespace platform
