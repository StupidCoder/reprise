#include "platform/host.hpp"

#include <cmath>

#include <SDL3/SDL.h>

namespace doc::platform {

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = o.texture_;
        width_ = o.width_;
        height_ = o.height_;
        o.texture_ = nullptr;
    }
    return *this;
}

Texture::~Texture() {
    if (texture_) SDL_DestroyTexture(texture_);
}

std::unique_ptr<Host> Host::create(const std::string& title, std::string& error) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        error = SDL_GetError();
        return nullptr;
    }
    std::unique_ptr<Host> host(new Host());
    host->window_ = SDL_CreateWindow(title.c_str(), 960, 640, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!host->window_) {
        error = SDL_GetError();
        return nullptr;
    }
    host->renderer_ = SDL_CreateRenderer(host->window_, nullptr);
    if (!host->renderer_) {
        error = SDL_GetError();
        return nullptr;
    }
    SDL_SetRenderVSync(host->renderer_, 1);
    SDL_SetRenderLogicalPresentation(host->renderer_, 480, 320, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    return host;
}

std::unique_ptr<Host> Host::createOffscreen(std::string& error) {
    if (!SDL_Init(0)) {
        error = SDL_GetError();
        return nullptr;
    }
    std::unique_ptr<Host> host(new Host());
    host->offscreen_ = SDL_CreateSurface(480, 320, SDL_PIXELFORMAT_RGBA32);
    host->renderer_ = host->offscreen_ ? SDL_CreateSoftwareRenderer(host->offscreen_) : nullptr;
    if (!host->renderer_) {
        error = SDL_GetError();
        return nullptr;
    }
    return host;
}

std::unique_ptr<Host> Host::attach(SDL_Renderer* renderer) {
    if (!renderer) return nullptr;
    std::unique_ptr<Host> host(new Host());
    host->renderer_ = renderer;
    host->attached_ = true;
    return host;
}

bool Host::saveFrame(const std::string& path) const {
    return offscreen_ && SDL_SaveBMP(offscreen_, path.c_str());
}

Host::~Host() {
    if (attached_) return;  // the renderer, and SDL, belong to the embedding application
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (offscreen_) SDL_DestroySurface(offscreen_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

std::vector<Event> Host::pollEvents(bool hover) {
    std::vector<Event> events;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        SDL_ConvertEventToRenderCoordinates(renderer_, &event);
        Event e{};
        switch (event.type) {
        case SDL_EVENT_QUIT:
            e.kind = Event::Quit;
            events.push_back(e);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.repeat) break;
            e.kind = Event::Key;
            e.key = event.key.key == SDLK_ESCAPE ? Event::Escape
                    : event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER ? Event::Return
                                                                                     : Event::Other;
            events.push_back(e);
            break;
        case SDL_EVENT_DROP_FILE:
            if (event.drop.data) {
                e.kind = Event::DropFile;
                e.file = std::filesystem::path(reinterpret_cast<const char8_t*>(event.drop.data));
                events.push_back(e);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                pointerDown_ = true;
                e.kind = Event::PointerDown, e.x = event.button.x, e.y = event.button.y;
                events.push_back(e);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (pointerDown_ || hover) {
                e.kind = Event::PointerMove, e.x = event.motion.x, e.y = event.motion.y;
                events.push_back(e);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT && pointerDown_) {
                pointerDown_ = false;
                e.kind = Event::PointerUp, e.x = event.button.x, e.y = event.button.y;
                events.push_back(e);
            }
            break;
        default: break;
        }
    }
    return events;
}

void Host::chooseFile(const std::string& filterName, const std::string& pattern) {
    {
        std::lock_guard guard(dialog_->mutex);
        if (dialog_->pending) return;
        dialog_->pending = true;
        dialog_->result.reset();
    }
    // SDL keeps the filter pointers until the callback runs.
    static std::string name, extensions;
    name = filterName;
    extensions = pattern;
    static SDL_DialogFileFilter filters[2];
    filters[0] = {name.c_str(), extensions.c_str()};
    filters[1] = {"All files", "*"};
    auto* context = new std::shared_ptr<Dialog>(dialog_);
    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* paths, int) {
            std::unique_ptr<std::shared_ptr<Dialog>> handle(static_cast<std::shared_ptr<Dialog>*>(userdata));
            FileChoice choice;
            if (!paths) choice.error = "The file picker could not open. Drop the file on the window instead.";
            else if (paths[0]) choice.file = std::filesystem::path(reinterpret_cast<const char8_t*>(paths[0]));
            std::lock_guard guard((*handle)->mutex);
            (*handle)->pending = false;
            (*handle)->result = std::move(choice);
        },
        context, window_, filters, 2, nullptr, false);
}

std::optional<FileChoice> Host::takeFileChoice() {
    std::lock_guard guard(dialog_->mutex);
    auto result = std::move(dialog_->result);
    dialog_->result.reset();
    return result;
}

std::filesystem::path Host::dataDirectory() {
    char* pref = SDL_GetPrefPath("", "DoubleOCheese");
    if (!pref) return std::filesystem::current_path() / "DoubleOCheese-data";
    std::filesystem::path path(reinterpret_cast<const char8_t*>(pref));
    SDL_free(pref);
    return path;
}

std::filesystem::path Host::resourceDirectory() {
    const char* base = SDL_GetBasePath();
    return base ? std::filesystem::path(reinterpret_cast<const char8_t*>(base)) : std::filesystem::current_path();
}

std::uint64_t Host::milliseconds() const { return SDL_GetTicks(); }

Texture Host::createTexture(const Image& image, bool crisp) {
    SDL_Surface* surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_RGBA32,
                                                 const_cast<std::uint8_t*>(image.rgba.data()), image.width * 4);
    if (!surface) return {};
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    if (texture && crisp) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return Texture(texture, image.width, image.height);
}

SDL_FRect Host::placeRect(float x, float y, float w, float h) const {
    float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    place(x0, y0);
    place(x1, y1);
    if (transform_.scaleX != 1.0f || transform_.scaleY != 1.0f) {
        x0 = std::round(x0), y0 = std::round(y0), x1 = std::round(x1), y1 = std::round(y1);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

void Host::beginFrame() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
}

void Host::draw(const Texture& texture, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh,
                double degrees) {
    if (!texture) return;
    SDL_FRect src{sx, sy, sw, sh};
    SDL_FRect dst = placeRect(dx, dy, dw, dh);
    SDL_RenderTextureRotated(renderer_, texture.get(), &src, &dst, degrees, nullptr, SDL_FLIP_NONE);
}

namespace {
SDL_BlendMode blendMode(DrawStyle::Blend blend) {
    switch (blend) {
    case DrawStyle::Add: return SDL_BLENDMODE_ADD;
    case DrawStyle::Multiply: return SDL_BLENDMODE_MOD;
    default: return SDL_BLENDMODE_BLEND;
    }
}
}  // namespace

void Host::draw(const Texture& texture, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh,
                const DrawStyle& style) {
    if (!texture) return;
    SDL_Texture* t = texture.get();
    SDL_SetTextureColorModFloat(t, style.r, style.g, style.b);
    SDL_SetTextureAlphaModFloat(t, style.a);
    SDL_SetTextureBlendMode(t, blendMode(style.blend));
    SDL_FRect src{sx, sy, sw, sh};
    SDL_FRect dst = placeRect(dx, dy, dw, dh);
    int flip = SDL_FLIP_NONE;
    if (style.flipX) flip |= SDL_FLIP_HORIZONTAL;
    if (style.flipY) flip |= SDL_FLIP_VERTICAL;
    SDL_RenderTextureRotated(renderer_, t, &src, &dst, style.degrees, nullptr, SDL_FlipMode(flip));
    SDL_SetTextureColorModFloat(t, 1, 1, 1);
    SDL_SetTextureAlphaModFloat(t, 1);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
}

void Host::drawTriangles(const Texture* texture, const std::vector<Vertex>& vertices, const std::vector<int>& indices,
                         DrawStyle::Blend blend) {
    if (vertices.empty()) return;
    std::vector<SDL_Vertex> v(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const Vertex& s = vertices[i];
        float x = s.x, y = s.y;
        place(x, y);
        v[i].position = {x, y};
        v[i].color = {s.r, s.g, s.b, s.a};
        v[i].tex_coord = {s.u, s.v};
    }
    SDL_Texture* t = texture ? texture->get() : nullptr;
    if (t) SDL_SetTextureBlendMode(t, blendMode(blend));
    else SDL_SetRenderDrawBlendMode(renderer_, blendMode(blend));  // plain colour: the draw blend applies
    SDL_RenderGeometry(renderer_, t, v.data(), int(v.size()), indices.empty() ? nullptr : indices.data(),
                       int(indices.size()));
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
}

void Host::fillRect(float x, float y, float w, float h, std::uint32_t rgba) {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, Uint8(rgba >> 24), Uint8(rgba >> 16), Uint8(rgba >> 8), Uint8(rgba));
    SDL_FRect r = placeRect(x, y, w, h);
    SDL_RenderFillRect(renderer_, &r);
}

void Host::setClip(float x, float y, float w, float h) {
    const SDL_FRect r = placeRect(x, y, w, h);
    const SDL_Rect clip{int(std::floor(r.x)), int(std::floor(r.y)), int(std::ceil(r.x + r.w) - std::floor(r.x)),
                        int(std::ceil(r.y + r.h) - std::floor(r.y))};
    SDL_SetRenderClipRect(renderer_, &clip);
}

void Host::clearClip() { SDL_SetRenderClipRect(renderer_, nullptr); }

void Host::drawText(float x, float y, const std::string& text, std::uint32_t rgba) {
    SDL_SetRenderDrawColor(renderer_, Uint8(rgba >> 24), Uint8(rgba >> 16), Uint8(rgba >> 8), Uint8(rgba));
    SDL_RenderDebugText(renderer_, x, y, text.c_str());
}

void Host::beginFrameInto(SDL_Texture* target, int width, int height) {
    previousTarget_ = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, target);
    // (The launcher sets its own presentation for each of its frames.)
    SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderClipRect(renderer_, nullptr);
    SDL_SetRenderScale(renderer_, float(width) / 480.0f, float(height) / 320.0f);
    intoTarget_ = true;
    beginFrame();
}

void Host::endFrame() {
    if (intoTarget_) {
        SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
        SDL_SetRenderTarget(renderer_, previousTarget_);
        intoTarget_ = false;
        return;
    }
    if (!attached_) SDL_RenderPresent(renderer_);
}

}  // namespace doc::platform
