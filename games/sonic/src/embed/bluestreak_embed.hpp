// BlueStreak's games inside another application (Reprise). The games run on the embedder's
// thread one fixed update at a time, give their frame as a picture and their sound as samples,
// and never open a window, an audio device or a file dialog. This header needs none of the
// engine's include paths: only the host's shared "platform/image.hpp", which the engine is
// compiled against when embedded (games/sonic/CMakeLists.txt).
#pragma once

#include "platform/image.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bluestreak {

enum class Title { sonic1_gg, sonic2_gg, triple_trouble_gg, sonic_advance_gba };

// The controller for one update. `jump` is the first action button (Game Gear button 1, GBA A),
// `action` the second (button 2, GBA B).
struct Buttons {
    bool left{}, right{}, up{}, down{}, jump{}, action{}, start{};
};

// The optional enhancements BlueStreak offers (all off: the original game). A game ignores the
// ones it does not offer (offered()).
enum class Enhancement { shield_transparency, widescreen, water, rings, lcd_screen };
struct Enhancements {
    bool widescreen{}, shield_transparency{}, water{}, rings{};
};
// The enhancements a game offers, in BlueStreak's menu order.
std::vector<Enhancement> offered(Title);

// A level selector start point (BlueStreak's own labels).
struct StartPoint {
    int id;
    std::string world, label, hint;
};
std::vector<StartPoint> start_points(Title);

// --- Import -------------------------------------------------------------------------------------

// Thrown by extract() for bytes that are not this game at all (another cartridge or no ROM).
struct NotThisTitle : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct PackFile {
    std::string name; // relative to the pack folder, '/' between folders
    std::vector<std::uint8_t> bytes;
};

// The asset pack made from the user's ROM: the cartridge is identified by its header (another
// cartridge: NotThisTitle), then the one supported revision by SHA-256 (another revision or a
// damaged dump: std::runtime_error with a message for the user). `release` names it.
std::vector<PackFile> extract(Title, std::span<const std::uint8_t> rom, std::string& release);
// The supported revision's name, as extract() gives it.
std::string release(Title);

// Loads every file of an installed pack; throws std::runtime_error when it cannot be used.
void validate(Title, const std::filesystem::path& pack);

// The standalone BlueStreak app's pack of the game below its data folder `root` (for taking it
// over), and whether the current extractor made it (older packs lack data the game reads).
std::filesystem::path standalone_pack(Title, const std::filesystem::path& root);
bool standalone_pack_current(Title, const std::filesystem::path& root);
// The standalone app's quick save file of the game below `root` (it may not exist).
std::filesystem::path standalone_quick_save(Title, const std::filesystem::path& root);

// --- Playing ------------------------------------------------------------------------------------

class Session {
public:
    // A game from an installed pack: from power-on (`start_point` < 0) or a level selector start
    // point. Throws std::runtime_error when the pack cannot be loaded.
    static std::unique_ptr<Session> start(Title, const std::filesystem::path& pack,
                                          int start_point = -1);
    // A game restored from a quick save (save()); throws when the save cannot be used.
    static std::unique_ptr<Session> restore(Title, const std::filesystem::path& pack,
                                            std::span<const std::uint8_t> state);
    virtual ~Session() = default;

    // Length of one update in seconds (one video frame of the original).
    virtual double step_seconds() const = 0;
    // The view the next updates are presented in: a widescreen view changes object activation,
    // so the game must know it before updating. `canvas`: the window's size (only its aspect).
    virtual void set_view(bool widescreen, int canvas_width, int canvas_height) = 0;
    // One update; returns its sound (interleaved stereo at sample_rate()).
    virtual std::vector<float> tick(const Buttons&, const Enhancements&) = 0;
    static constexpr int sample_rate() { return 48000; }
    // The current frame: 160 x 144 on the Game Gear (larger in the widescreen view), 240 x 160
    // on the Game Boy Advance. Drawing changes nothing in the game.
    virtual platform::Image render(const Enhancements&) const = 0;

    // True while an act is played (not menus, demos or cut scenes): Start then belongs to the
    // application's pause menu, as in BlueStreak.
    virtual bool gameplay() const = 0;
    // What is running, for menus ("Green Hill / Act 1").
    virtual std::string status() const = 0;
    // Why the game stopped (a part not reconstructed yet), or empty while it runs.
    virtual std::string stopped() const = 0;
    // The state for a quick save; throws std::runtime_error with the reason when the game cannot
    // be saved now (or not at all).
    virtual std::vector<std::uint8_t> save() = 0;
};

// --- Previews -----------------------------------------------------------------------------------

// The sound test's tracks of a game (none for Sonic Advance): each title and its sound, rendered
// through the game's sound driver and PSG model from the pack, up to `longest` seconds
// (interleaved stereo at Session::sample_rate()).
struct Track {
    std::string title, description;
    std::vector<float> stereo;
};
std::vector<Track> sound_test(Title, const std::filesystem::path& pack, double longest);

// --- The Game Gear LCD look -------------------------------------------------------------------

// BlueStreak's optional Game Gear screen look (gamegear/lcd_filter.hpp): an STN panel's colour
// response and pixel persistence at the frame's resolution, then a subpixel mask and a soft glow
// at the display's resolution. Presentation only.
class LcdLook {
public:
    LcdLook();
    ~LcdLook();
    void reset();
    // Colour response plus persistence; `elapsed`: real presentation time since the last call.
    const platform::Image& apply(const platform::Image& frame, double elapsed);
    // The multiplicative subpixel mask for `width` x `height` device pixels showing frame pixels
    // `cell_width` x `cell_height` device pixels large, the first one starting at `origin`.
    static platform::Image mask(int width, int height, double cell_width, double cell_height,
                                double origin_x, double origin_y);
    // The glow added over the masked picture (0..255).
    static int glow();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace bluestreak
