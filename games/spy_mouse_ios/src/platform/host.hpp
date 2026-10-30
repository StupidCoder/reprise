// Host services on SDL 3: a window with a 480x320 logical play area (letterboxed and scaled),
// textures, simple drawing, touch/mouse and key input in play-area points, dropped files, an
// asynchronous file picker, the per-user data folder and a millisecond clock.
#pragma once

#include "platform/image.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Surface;
struct SDL_FRect;

namespace doc::platform {

class Texture {
public:
    Texture() = default;
    Texture(SDL_Texture* texture, int w, int h) : texture_(texture), width_(w), height_(h) {}
    Texture(Texture&& o) noexcept { *this = std::move(o); }
    Texture& operator=(Texture&& o) noexcept;
    ~Texture();
    SDL_Texture* get() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    explicit operator bool() const { return texture_ != nullptr; }

private:
    SDL_Texture* texture_ = nullptr;
    int width_ = 0, height_ = 0;
};

struct Event {
    enum Kind { PointerDown, PointerMove, PointerUp, Key, DropFile, Quit } kind;
    float x = 0, y = 0;  // play-area points, y down
    enum KeyCode { Other, Escape, Return } key = Other;
    std::filesystem::path file;  // DropFile
};

// How a picture is put down: rotation (degrees clockwise about the destination centre),
// mirroring, a colour/alpha multiplier and the blend (alpha, additive, or "multiply" for shadows).
struct DrawStyle {
    enum Blend { Alpha, Add, Multiply };
    double degrees = 0.0;
    bool flipX = false, flipY = false;
    float r = 1, g = 1, b = 1, a = 1;
    Blend blend = Alpha;
};

// A vertex of textured triangles: position in play-area points, texture coordinates in 0..1.
struct Vertex {
    float x = 0, y = 0, u = 0, v = 0;
    float r = 1, g = 1, b = 1, a = 1;
};

struct FileChoice {
    std::optional<std::filesystem::path> file;  // empty when cancelled
    std::string error;
};

class Host {
public:
    static std::unique_ptr<Host> create(const std::string& title, std::string& error);
    // Renders into a 480x320 image without a window (previews and regression shots).
    static std::unique_ptr<Host> createOffscreen(std::string& error);
    // Draws with an embedding application's renderer (Reprise): no window, events or presenting
    // of its own; frames go into textures the application owns (beginFrameInto).
    static std::unique_ptr<Host> attach(SDL_Renderer* renderer);
    ~Host();
    // Offscreen hosts only: writes the last frame as a BMP file.
    bool saveFrame(const std::string& path) const;

    // Window events since the last call (pointer moves only while the button is held, plus
    // hover moves when `hover` is set).
    std::vector<Event> pollEvents(bool hover = false);
    // Asynchronous file picker; poll takeFileChoice() for the result.
    void chooseFile(const std::string& filterName, const std::string& pattern);
    std::optional<FileChoice> takeFileChoice();
    // Per-user folder for imported game files and settings; the folder holding the app's own
    // resources (the bundle's Resources on macOS).
    static std::filesystem::path dataDirectory();
    static std::filesystem::path resourceDirectory();
    std::uint64_t milliseconds() const;

    // `crisp`: nearest-neighbour scaling (pixel-font screens) instead of smooth scaling.
    Texture createTexture(const Image& image, bool crisp = false);
    void beginFrame();
    // Attached hosts: the next frame is drawn into `target` (`width` x `height` pixels), the
    // 480x320 play area scaled to fill it; endFrame() then gives the renderer back as it was.
    void beginFrameInto(SDL_Texture* target, int width, int height);
    // Draws `source` (pixels in the texture) to `dest` (play-area points), rotated by
    // `degrees` clockwise about the destination centre.
    void draw(const Texture& texture, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh,
              double degrees = 0.0);
    void draw(const Texture& texture, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh,
              const DrawStyle& style);
    // Triangles (three indices each) with an optional texture (nullptr: plain colour).
    void drawTriangles(const Texture* texture, const std::vector<Vertex>& vertices, const std::vector<int>& indices,
                       DrawStyle::Blend blend = DrawStyle::Alpha);
    void fillRect(float x, float y, float w, float h, std::uint32_t rgba);
    void drawText(float x, float y, const std::string& text, std::uint32_t rgba);
    void endFrame();
    // Everything drawn after this is scaled about the play area's top-left corner and then
    // moved (screen shake); reset with setTransform(1, 1, 0, 0).
    void setTransform(float scaleX, float scaleY, float offsetX, float offsetY) {
        transform_ = {scaleX, scaleY, offsetX, offsetY};
    }

private:
    Host() = default;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Surface* offscreen_ = nullptr;
    bool attached_ = false;            // the renderer belongs to the embedding application
    SDL_Texture* previousTarget_ = nullptr;
    bool intoTarget_ = false;
    bool pointerDown_ = false;
    struct Transform {
        float scaleX = 1, scaleY = 1, offsetX = 0, offsetY = 0;
    } transform_;
    void place(float& x, float& y) const {
        x = x * transform_.scaleX + transform_.offsetX;
        y = y * transform_.scaleY + transform_.offsetY;
    }
    // A rectangle through the transform; scaled, its edges go to whole pixels so neighbouring
    // pictures (the background strips) keep meeting without a seam.
    SDL_FRect placeRect(float x, float y, float w, float h) const;
    struct Dialog {
        std::mutex mutex;
        bool pending = false;
        std::optional<FileChoice> result;
    };
    std::shared_ptr<Dialog> dialog_ = std::make_shared<Dialog>();
};

}  // namespace doc::platform
