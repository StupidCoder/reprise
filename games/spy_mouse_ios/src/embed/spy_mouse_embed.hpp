// Spy Mouse inside another application (Reprise). The game runs on the embedder's thread, one
// fixed 17 ms tick at a time, draws with the embedder's SDL renderer into a texture the embedder
// owns, and mixes its sound when the embedder asks. Nothing here opens a window, an audio
// device or a file dialog. This header needs none of the engine's include paths.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace spy_mouse {

// The play area the game draws (points; touches use the same units, y down).
inline constexpr int kPlayWidth = 480, kPlayHeight = 320;
// One tick of the original's fixed step (Application::s_nTickTime).
inline constexpr double kTickSeconds = 0.017;

// Whether the bytes are a ZIP archive with an app folder (Payload/<name>.app/): an iPhone app,
// though not necessarily this game.
bool looksLikeIpa(std::span<const std::uint8_t> bytes);

// Thrown by importIpa for an app archive that is not this game.
struct NotSpyMouse : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ImportedFile {
    std::string name;  // relative to the content folder: "SpyMouse", "res/..."
    std::vector<std::uint8_t> bytes;
};
// The files the game needs from the user's IPA, checked (every file's CRC-32, the program's
// file keys against the data, the level table). Throws NotSpyMouse for another app, or
// std::runtime_error with a message for the user when this game's archive cannot be used
// (damaged, or another version). `release` names the recognised version.
std::vector<ImportedFile> importIpa(std::span<const std::uint8_t> ipa, std::string& release);

class Game {
public:
    enum class Touch { Down, Move, Up };

    // `content`: the folder the imported files were installed in; `progressFile`: the
    // campaign progress (created when missing). Throws std::runtime_error on failure.
    static std::unique_ptr<Game> start(const std::filesystem::path& content, const std::filesystem::path& progressFile);
    virtual ~Game() = default;

    // Touches in order; each is handled at the start of the next tick.
    virtual void touch(Touch kind, float x, float y) = 0;
    virtual void tick() = 0;
    // The latest state into `target` (a render-target texture of `width` x `height` pixels made
    // with `renderer`); the renderer's target and scale are restored afterwards. Drawing
    // changes nothing in the game, so a picture can be taken at any time (save previews).
    virtual void draw(SDL_Renderer* renderer, SDL_Texture* target, int width, int height) = 0;
    // The sound of the time since the last call: `frames` interleaved stereo frames at `rate`.
    virtual void audio(float* stereo, int frames, int rate) = 0;

    // The map's quit button was pressed: the player wants to leave the game.
    virtual bool left() const = 0;
    // Why the game cannot go on, or empty.
    virtual std::string failure() const = 0;

    // Quick saves: the state as bytes, and restoring it into a game that has just started
    // (false with `error` on failure; the game is then unchanged).
    virtual std::vector<std::uint8_t> saveState() const = 0;
    virtual bool restore(std::span<const std::uint8_t> state, std::string& error) = 0;
};

// A few of the game's own pictures from an import, for the launcher's viewer (title, RGBA
// pixels). Throws std::runtime_error when the import cannot be opened.
struct Picture {
    std::string title;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
std::vector<Picture> previews(const std::filesystem::path& content);

// Where the standalone DoubleOCheese app kept its import ("game/") and progress
// ("progress.txt"), for taking them over.
std::filesystem::path legacyDataDirectory();
// The files of a folder the standalone app imported, checked like an import; throws
// std::runtime_error with a message for the user when they cannot be used (another version).
std::vector<ImportedFile> readImportedFolder(const std::filesystem::path& folder, std::string& release);

}  // namespace spy_mouse
