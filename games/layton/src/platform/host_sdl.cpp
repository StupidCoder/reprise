// The platform layer on SDL 3 (see host.hpp): window and renderer, input
// events, textures, the file picker and sound output.
#include "platform/host.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace picarat::platform {

namespace {

std::filesystem::path utf8Path(const char* text) {
    const std::string_view s(text);
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string pathText(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

[[noreturn]] void fail(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

Key keyOf(SDL_Keycode code) {
    switch (code) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return Key::Return;
    case SDLK_ESCAPE:
    case SDLK_AC_BACK: // Android's back button or gesture
        return Key::Escape;
    case SDLK_BACKSPACE:
        return Key::Backspace;
    case SDLK_DELETE:
        return Key::Delete;
    case SDLK_LEFT:
        return Key::Left;
    case SDLK_RIGHT:
        return Key::Right;
    case SDLK_UP:
        return Key::Up;
    case SDLK_DOWN:
        return Key::Down;
    case SDLK_HOME:
        return Key::Home;
    case SDLK_END:
        return Key::End;
    case SDLK_PAGEUP:
        return Key::PageUp;
    case SDLK_PAGEDOWN:
        return Key::PageDown;
    default:
        // Printable keys have their character as the key code.
        return (code >= 0x20 && code < 0x7f) || (code >= 0xa0 && code < SDLK_SCANCODE_MASK)
                   ? Key::Character
                   : Key::None;
    }
}

} // namespace

// --- Texture ---------------------------------------------------------------

struct Texture::Handle {
    SDL_Texture* texture = nullptr;
};

Texture::~Texture() {
    if (handle_) {
        SDL_DestroyTexture(handle_->texture);
        delete handle_;
    }
}

void Texture::update(const Image& image) {
    if (image.width != width_ || image.height != height_) {
        throw std::logic_error("texture update with a different size");
    }
    if (!SDL_UpdateTexture(handle_->texture, nullptr, image.pixels.data(),
                           image.width * int(sizeof(Color)))) {
        fail("Cannot update a texture");
    }
}

// --- Host ------------------------------------------------------------------

struct Host::Implementation {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool textInput = false, screenKeyboard = false;
    // Sleep sources; the app sleeps while any of them is set.
    bool minimised = false, hidden = false, background = false;
    bool sleeping = false;
    // The accelerometer (Host::motion); closed while no game asks.
    SDL_Sensor* accelerometer = nullptr;
    bool sensorsStarted = false, noAccelerometer = false;

    // The file picker reports back from another thread.
    struct Dialog {
        std::mutex mutex;
        std::optional<Host::FileChoice> result;
        bool pending = false;
        std::string filterName, extensions;
        SDL_DialogFileFilter filters[2]{};
    };

    std::shared_ptr<Dialog> dialog = std::make_shared<Dialog>();

    // Units. The app works in points; density() device pixels make one
    // point. SDL measures the window in its own units: points on macOS,
    // device pixels on Android and on Windows without DPI awareness, where
    // the display scale (e.g. 2.6 on a phone) says how large a point is.
    float density() const {
        const float d = SDL_GetWindowDisplayScale(window);
        return d > 0 ? d : 1;
    }

    // Points per SDL window unit.
    float pointsPerUnit() const {
        const float pixels = SDL_GetWindowPixelDensity(window);
        return (pixels > 0 ? pixels : 1) / density();
    }

    // The part of the window clear of camera cut-outs and system bars
    // (phones), in points. The app lays out and draws inside it; the host
    // shifts drawing and pointer positions by its origin. The whole window
    // on desktops.
    SDL_FRect safeArea() const {
        SDL_Rect area{};
        if (!SDL_GetWindowSafeArea(window, &area) || area.w <= 0 || area.h <= 0) {
            area.x = area.y = 0;
            SDL_GetWindowSize(window, &area.w, &area.h);
        }
        const float p = pointsPerUnit();
        return {area.x * p, area.y * p, area.w * p, area.h * p};
    }

    // A pointer position in SDL window units, in points within the safe area.
    std::pair<float, float> point(float x, float y, const SDL_FRect& area) const {
        const float p = pointsPerUnit();
        return {x * p - area.x, y * p - area.y};
    }

    // Turns the sleep sources into one Sleep or Wake event when the
    // combined state changes.
    void updateSleep(std::vector<Event>& events) {
        const bool now = minimised || hidden || background;
        if (now != sleeping) {
            sleeping = now;
            Event e;
            e.type = now ? Event::Type::Sleep : Event::Type::Wake;
            events.push_back(e);
        }
    }
};

Host::Host(std::string_view title, int width, int height, int minimumWidth, int minimumHeight)
    : impl_(std::make_unique<Implementation>()) {
    SDL_SetAppMetadata("Picarat", nullptr, "local.picarat");
    // Android: the back gesture arrives as a key (Escape) instead of closing the app.
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fail("Cannot start SDL");
    }
    auto& i = *impl_;
    if (!SDL_CreateWindowAndRenderer(std::string(title).c_str(), width, height,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &i.window, &i.renderer)) {
        fail("Cannot open the window");
    }
    SDL_SetWindowMinimumSize(i.window, minimumWidth, minimumHeight);
    SDL_SetRenderVSync(i.renderer, 1);
    SDL_StopTextInput(i.window);
}

Host::~Host() {
    auto& i = *impl_;
    if (i.accelerometer) {
        SDL_CloseSensor(i.accelerometer);
    }
    SDL_DestroyRenderer(i.renderer);
    SDL_DestroyWindow(i.window);
    SDL_Quit();
}

std::optional<std::array<float, 3>> Host::motion(bool wanted) {
    auto& i = *impl_;
    if (!wanted) {
        if (i.accelerometer) {
            SDL_CloseSensor(i.accelerometer);
            i.accelerometer = nullptr;
        }
        return std::nullopt;
    }
    if (!i.accelerometer && !i.noAccelerometer) {
        if (!i.sensorsStarted) {
            i.sensorsStarted = true;
            if (!SDL_InitSubSystem(SDL_INIT_SENSOR)) {
                i.noAccelerometer = true;
                return std::nullopt;
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
        i.noAccelerometer = i.accelerometer == nullptr;
    }
    float d[3] = {};
    if (!i.accelerometer || !SDL_GetSensorData(i.accelerometer, d, 3)) {
        return std::nullopt;
    }
    // SDL reports m/s^2 in the device's natural orientation (x right, y up
    // the screen held upright, z towards the viewer), gravity included as
    // "up". Turned into the window's axes for the display's orientation.
    const float x = d[0] / SDL_STANDARD_GRAVITY, y = d[1] / SDL_STANDARD_GRAVITY,
                z = d[2] / SDL_STANDARD_GRAVITY;
    switch (SDL_GetCurrentDisplayOrientation(SDL_GetDisplayForWindow(i.window))) {
    case SDL_ORIENTATION_LANDSCAPE: // the device turned a quarter anticlockwise
        return std::array<float, 3>{-y, x, z};
    case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
        return std::array<float, 3>{y, -x, z};
    case SDL_ORIENTATION_PORTRAIT_FLIPPED:
        return std::array<float, 3>{-x, -y, z};
    default:
        return std::array<float, 3>{x, y, z};
    }
}

std::vector<Event> Host::poll(int waitMilliseconds) {
    auto& i = *impl_;
    std::vector<Event> events;
    SDL_Event e;
    bool have = SDL_PollEvent(&e);
    if (!have && waitMilliseconds > 0) {
#ifdef SDL_PLATFORM_ANDROID
        // SDL's timed wait kept a core fully busy on Android (SDL 3.4.16,
        // measured on the emulator); a plain sleep idles at almost no load.
        SDL_Delay(Uint32(waitMilliseconds));
        have = SDL_PollEvent(&e);
#else
        have = SDL_WaitEventTimeout(&e, waitMilliseconds);
#endif
    }
    const SDL_FRect area = i.safeArea();
    while (have) {
        Event out;
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            out.type = Event::Type::Quit;
            events.push_back(out);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            out.type = e.type == SDL_EVENT_KEY_DOWN ? Event::Type::KeyDown : Event::Type::KeyUp;
            out.key = keyOf(e.key.key);
            if (out.key == Key::None) {
                break;
            }
            if (out.key == Key::Character) {
                out.symbol =
                    e.key.key < 0x80 ? char32_t(std::tolower(int(e.key.key))) : char32_t(e.key.key);
            }
#ifdef __APPLE__
            out.command = (e.key.mod & SDL_KMOD_GUI) != 0;
#else
            out.command = (e.key.mod & SDL_KMOD_CTRL) != 0;
#endif
            out.shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
            out.repeat = e.key.repeat;
            events.push_back(out);
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            if (e.text.text) {
                out.type = Event::Type::Text;
                out.text = e.text.text;
                events.push_back(out);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            out.type = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? Event::Type::MouseDown
                                                             : Event::Type::MouseUp;
            std::tie(out.x, out.y) = i.point(e.button.x, e.button.y, area);
            out.button = e.button.button;
            out.clicks = e.button.clicks;
            out.touch = e.button.which == SDL_TOUCH_MOUSEID;
            events.push_back(out);
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.which == SDL_TOUCH_MOUSEID) {
                // A lifted finger does not hover: the pointer leaves the
                // window, so nothing stays highlighted under the last touch.
                Event away;
                away.type = Event::Type::MouseMove;
                away.x = away.y = -1e6f;
                events.push_back(away);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            out.type = Event::Type::MouseMove;
            std::tie(out.x, out.y) = i.point(e.motion.x, e.motion.y, area);
            out.button = (e.motion.state & SDL_BUTTON_LMASK) ? 1 : 0;
            out.touch = e.motion.which == SDL_TOUCH_MOUSEID;
            events.push_back(out);
            break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            // Touch screens only (a trackpad's fingers are not reported):
            // the finger's place, from the window's 0..1 to window points.
            if (SDL_GetTouchDeviceType(e.tfinger.touchID) != SDL_TOUCH_DEVICE_DIRECT) {
                break;
            }
            out.type = e.type == SDL_EVENT_FINGER_DOWN     ? Event::Type::TouchDown
                       : e.type == SDL_EVENT_FINGER_MOTION ? Event::Type::TouchMove
                                                           : Event::Type::TouchUp;
            out.finger = std::uint64_t(e.tfinger.fingerID);
            int w = 0, h = 0;
            SDL_GetWindowSize(i.window, &w, &h);
            std::tie(out.x, out.y) = i.point(e.tfinger.x * float(w), e.tfinger.y * float(h), area);
            out.touch = true;
            events.push_back(out);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            out.type = Event::Type::Wheel;
            out.wheel = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
            std::tie(out.x, out.y) = i.point(e.wheel.mouse_x, e.wheel.mouse_y, area);
            events.push_back(out);
            break;
        case SDL_EVENT_DROP_FILE:
            if (e.drop.data) {
                out.type = Event::Type::DropFile;
                out.file = utf8Path(e.drop.data);
                events.push_back(out);
            }
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            i.minimised = true;
            i.updateSleep(events);
            break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
            i.minimised = false;
            i.updateSleep(events);
            break;
        case SDL_EVENT_WINDOW_HIDDEN:
            i.hidden = true;
            i.updateSleep(events);
            break;
        case SDL_EVENT_WINDOW_SHOWN:
            i.hidden = false;
            i.updateSleep(events);
            break;
        case SDL_EVENT_DID_ENTER_BACKGROUND:
            i.background = true;
            i.updateSleep(events);
            break;
        case SDL_EVENT_WILL_ENTER_FOREGROUND:
            i.background = false;
            i.updateSleep(events);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            out.type = Event::Type::FocusLost;
            events.push_back(out);
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_RENDER_TARGETS_RESET:
        case SDL_EVENT_RENDER_DEVICE_RESET:
            out.type = Event::Type::Expose;
            events.push_back(out);
            break;
        case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN:
            out.type = Event::Type::KeyboardHidden;
            events.push_back(out);
            break;
        default:
            break;
        }
        have = SDL_PollEvent(&e);
    }
    return events;
}

std::array<float, 2> Host::windowSize() const {
    const SDL_FRect area = impl_->safeArea();
    return {std::max(area.w, 1.0f), std::max(area.h, 1.0f)};
}

float Host::pixelDensity() const {
    return impl_->density();
}

void Host::setTitle(std::string_view title) {
    SDL_SetWindowTitle(impl_->window, std::string(title).c_str());
}

void Host::leaveToSystem() {
#ifdef SDL_PLATFORM_ANDROID
    SDL_SendAndroidBackButton(); // Android's own back handling, past the trap
#endif
}

void Host::setTextInput(bool on, bool screenKeyboard) {
    auto& i = *impl_;
    screenKeyboard = on && screenKeyboard;
    if (on == i.textInput && screenKeyboard == i.screenKeyboard) {
        return;
    }
    if (i.textInput && i.screenKeyboard && !screenKeyboard) {
        SDL_StopTextInput(i.window); // hides the on-screen keyboard
    }
    i.textInput = on;
    i.screenKeyboard = screenKeyboard;
    // SDL shows the on-screen keyboard when text input starts, unless told
    // otherwise; starting again brings it back after the user closed it.
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, screenKeyboard ? "auto" : "0");
    if (on) {
        SDL_StartTextInput(i.window);
    } else {
        SDL_StopTextInput(i.window);
    }
}

std::string Host::clipboardText() const {
    char* text = SDL_GetClipboardText();
    std::string result = text ? text : "";
    SDL_free(text);
    return result;
}

void Host::beginFrame(Color background) {
    auto& i = *impl_;
    SDL_SetRenderDrawColor(i.renderer, background.r, background.g, background.b, 255);
    SDL_RenderClear(i.renderer);
}

std::unique_ptr<Texture> Host::createTexture(int width, int height, bool smooth) {
    std::unique_ptr<Texture> texture(new Texture);
    SDL_Texture* t = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!t) {
        fail("Cannot create a texture");
    }
    SDL_SetTextureScaleMode(t, smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    texture->handle_ = new Texture::Handle{t};
    texture->width_ = width;
    texture->height_ = height;
    return texture;
}

void Host::draw(const Texture& texture, const Rect* source, const Rect& target,
                std::uint8_t alpha) {
    auto& i = *impl_;
    const float d = i.density();
    const SDL_FRect area = i.safeArea();
    const SDL_FRect to{(target.x + area.x) * d, (target.y + area.y) * d, target.w * d,
                       target.h * d};
    SDL_FRect from{};
    if (source) {
        from = {source->x, source->y, source->w, source->h};
    }
    SDL_SetTextureAlphaMod(texture.handle_->texture, alpha);
    SDL_RenderTexture(i.renderer, texture.handle_->texture, source ? &from : nullptr, &to);
}

void Host::endFrame() {
    SDL_RenderPresent(impl_->renderer);
}

double Host::seconds() const {
    return double(SDL_GetTicksNS()) / 1.0e9;
}

void Host::chooseFile(std::string_view filterName, std::string_view extensions) {
    auto state = impl_->dialog;
    {
        std::lock_guard guard(state->mutex);
        if (state->pending) {
            return;
        }
        state->pending = true;
        state->result.reset();
        state->filterName = filterName;
        state->extensions = extensions;
        state->filters[0] = {state->filterName.c_str(), state->extensions.c_str()};
        state->filters[1] = {"All files", "*"};
    }
    // The callback owns one reference to the shared state until it has run.
    auto* context = new std::shared_ptr<Implementation::Dialog>(state);
    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* paths, int) {
            std::unique_ptr<std::shared_ptr<Implementation::Dialog>> handle(
                static_cast<std::shared_ptr<Implementation::Dialog>*>(userdata));
            FileChoice choice;
            if (!paths) {
                choice.error = "Could not open the file picker. You can drop the ROM file on "
                               "the window instead.";
            } else if (paths[0]) {
                choice.file = utf8Path(paths[0]);
            }
            std::lock_guard guard((*handle)->mutex);
            (*handle)->pending = false;
            (*handle)->result = std::move(choice);
        },
        context, impl_->window, state->filters, 2, nullptr, false);
}

std::optional<Host::FileChoice> Host::takeFileChoice() {
    std::lock_guard guard(impl_->dialog->mutex);
    auto result = std::move(impl_->dialog->result);
    impl_->dialog->result.reset();
    return result;
}

std::filesystem::path Host::resourceDirectory() {
#ifdef SDL_PLATFORM_ANDROID
    // Relative paths name files in the APK's assets, which SDL reads directly.
    return {};
#else
    const char* base = SDL_GetBasePath();
    return base ? utf8Path(base) : std::filesystem::current_path();
#endif
}

std::string Host::preferredLanguage() {
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    std::string language;
    if (locales && count > 0 && locales[0]->language) {
        language = locales[0]->language;
        if (locales[0]->country) {
            language += std::string("_") + locales[0]->country;
        }
    }
    SDL_free(locales);
    return language;
}

std::filesystem::path Host::dataDirectory(std::string_view appName) {
    char* path = SDL_GetPrefPath("", std::string(appName).c_str());
    if (!path) {
        fail("No folder for the app's data");
    }
    auto result = utf8Path(path);
    SDL_free(path);
    return result;
}

std::vector<std::uint8_t> Host::readFile(const std::filesystem::path& path,
                                         std::uint64_t maxBytes) {
    SDL_IOStream* io = SDL_IOFromFile(pathText(path).c_str(), "rb");
    if (!io) {
        throw std::runtime_error(std::string("Could not open the file: ") + SDL_GetError());
    }
    const Sint64 size = SDL_GetIOSize(io);
    std::vector<std::uint8_t> bytes;
    bool ok = size >= 0 && std::uint64_t(size) <= maxBytes;
    if (ok) {
        bytes.resize(std::size_t(size));
        ok = SDL_ReadIO(io, bytes.data(), bytes.size()) == bytes.size();
    }
    SDL_CloseIO(io);
    if (!ok) {
        throw std::runtime_error(size >= 0 && std::uint64_t(size) > maxBytes
                                     ? "The file is too large."
                                     : "Could not read the file.");
    }
    return bytes;
}

Host::FileReader::FileReader(const std::filesystem::path& path) {
    SDL_IOStream* io = SDL_IOFromFile(pathText(path).c_str(), "rb");
    if (!io) {
        throw std::runtime_error(std::string("Could not open the file: ") + SDL_GetError());
    }
    const Sint64 size = SDL_GetIOSize(io);
    if (size < 0) {
        SDL_CloseIO(io);
        throw std::runtime_error("Could not read the file.");
    }
    io_ = io;
    size_ = std::uint64_t(size);
}

Host::FileReader::~FileReader() {
    SDL_CloseIO(static_cast<SDL_IOStream*>(io_));
}

void Host::FileReader::read(std::uint64_t offset, std::span<std::uint8_t> out) {
    auto* io = static_cast<SDL_IOStream*>(io_);
    if (offset > size_ || out.size() > size_ - offset ||
        SDL_SeekIO(io, Sint64(offset), SDL_IO_SEEK_SET) < 0 ||
        SDL_ReadIO(io, out.data(), out.size()) != out.size()) {
        throw std::runtime_error("Could not read the file.");
    }
}

std::optional<Image> Host::loadPng(const std::filesystem::path& path) {
    SDL_Surface* loaded = SDL_LoadPNG(pathText(path).c_str());
    if (!loaded) {
        return std::nullopt;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!rgba) {
        return std::nullopt;
    }
    Image image(rgba->w, rgba->h);
    for (int y = 0; y < rgba->h; ++y) {
        std::memcpy(&image.pixels[std::size_t(y) * std::size_t(rgba->w)],
                    static_cast<const std::uint8_t*>(rgba->pixels) +
                        std::size_t(y) * std::size_t(rgba->pitch),
                    std::size_t(rgba->w) * 4);
    }
    SDL_DestroySurface(rgba);
    return image;
}

// --- AudioStream -----------------------------------------------------------

struct AudioStream::Implementation {
    SDL_AudioStream* stream = nullptr;
    Fill fill;
    std::vector<std::int16_t> buffer;

    // SDL asks for `additional` more bytes on its audio thread.
    static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int) {
        auto& self = *static_cast<Implementation*>(user);
        if (additional <= 0) {
            return;
        }
        const int frames = (additional + 3) / 4;
        self.buffer.assign(std::size_t(frames) * 2, 0);
        self.fill(self.buffer.data(), frames);
        SDL_PutAudioStreamData(stream, self.buffer.data(), frames * 4);
    }
};

AudioStream::AudioStream(int sampleRate, Fill fill) : impl_(std::make_unique<Implementation>()) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fail("Cannot start sound output");
    }
    impl_->fill = std::move(fill);
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, sampleRate};
    impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                              &Implementation::callback, impl_.get());
    if (!impl_->stream) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        fail("Cannot open the sound device");
    }
    SDL_ResumeAudioStreamDevice(impl_->stream);
}

AudioStream::~AudioStream() {
    // Destroying the stream waits for a running callback to finish.
    SDL_DestroyAudioStream(impl_->stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

} // namespace picarat::platform
