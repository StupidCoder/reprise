#pragma once
// The adventures embedded in another application (Reprise): import, the
// installed pack's checks and previews, and one running game - without any
// of the engine's own headers (its "platform/...", "shell/..." and game
// types stay inside this library).
//
// A running game (Session) advances only when the host steps it: one frame
// at a time, each with its input and its sound (60 frames a second; the
// sound is rendered in step with the frames). Between steps the game thread
// waits, so pausing is not stepping, and ending the session stops the thread.
// The host shows the two screens as it likes; stylus positions are lower
// screen pixels.
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace picarat::embed {

enum class Title { CuriousVillage, PandorasBox, LostFuture, SpectresCall, MiracleMask, AzranLegacy };

// The game's frames per second (the session is stepped at this rate).
inline constexpr double kFramesPerSecond = 60.0;

struct Size {
    int width = 0, height = 0;
};

// The two screens, in pixels: the DS's 256x192 each, the 3DS's 400x240
// upper and 320x240 lower screen.
struct Screens {
    Size top, bottom;
};
Screens screens(Title title);

// Where the two screens go in an area of `width` x `height`: stacked or side
// by side, whichever shows them larger, each keeping its shape, centred (the
// standalone app's layout, shell/screen_layout.hpp).
struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
};
struct Layout {
    Rect top, bottom;
    bool sideBySide = false;
    double scale = 1; // area pixels per screen pixel
};
Layout layout(double width, double height, const Screens& screens);

// --- Import -----------------------------------------------------------------

// Random access to a cartridge image without reading it whole (the 3DS
// images are 1 GiB and more).
class ImageSource {
public:
    virtual ~ImageSource() = default;
    virtual std::uint64_t size() const = 0;
    // Fills `out` from `offset`; throws std::runtime_error if it cannot.
    virtual void read(std::uint64_t offset, std::span<std::uint8_t> out) = 0;
};

// Which of the adventures an image is, from its header's game code (no
// check of the release yet), or none.
std::optional<Title> identify(ImageSource& image);

// Imports a cartridge image of `title` into the folder `pack` (created; it
// must not exist): checks the release (only the recognised revision of each
// game is accepted), then unpacks and converts what the game needs,
// streaming the image. Returns a description of the release for the user.
// Throws std::runtime_error with a message for the user.
std::string importImage(Title title, ImageSource& image, const std::filesystem::path& pack,
                        const std::function<void(double fraction)>& progress = {});

// Whether an installed pack can be played: empty when it can, else why not
// (an older pack layout, another release, an incomplete pack).
std::string checkPack(Title title, const std::filesystem::path& pack);

// The release an installed pack holds, for the user.
std::string packRelease(Title title, const std::filesystem::path& pack);

// The standalone Picarat app (which held these games before): its data
// folder and the pack folder of a game there.
std::filesystem::path legacyDataDirectory();
std::string legacyPackFolder(Title title);
// Its progress files of a game, as (file there, file name in this game's
// progress folder): the cartridge saves and save slots. Its own conveniences
// (automatic checkpoints, puzzle-mode and minigame free-play records) are
// not part of the game's progress.
std::vector<std::pair<std::filesystem::path, std::string>> legacyProgress(Title title);

// --- Pictures ----------------------------------------------------------------

struct Picture {
    std::string title;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

// Some of the pictures of an installed pack, decoded (the importer's
// output, not cover art).
std::vector<Picture> previews(Title title, const std::filesystem::path& pack);

// The languages of a multi-language cartridge's pack (Curious Village), as
// (folder code, name in itself); empty for single-language games.
std::vector<std::pair<std::string, std::string>> languages(Title title,
                                                          const std::filesystem::path& pack);
// The language the game picks for a system language ("de-DE"), as the
// console does; one of languages()' codes.
std::string systemLanguage(Title title, const std::filesystem::path& pack, const std::string& iso);

// --- A running game ------------------------------------------------------------

// Whether the story can be played from the title, and whether the session's
// automatic checkpoint (written whenever a room is ready) can continue it.
bool storyPlayable(Title title);
bool hasCheckpoints(Title title);

// One stylus event on the lower screen (pixels; may lie outside the screen
// while a press that began on it goes on).
struct Touch {
    enum class Kind { Down, Move, Up };
    Kind kind = Kind::Down;
    int x = 0, y = 0;
};

// A key for the name entry's typed names.
struct NameKey {
    enum class Kind { Character, Erase, Confirm };
    Kind kind = Kind::Character;
    std::uint8_t character = 0; // Windows-1252
};

// The input of one frame.
struct Input {
    std::vector<Touch> touches; // in order
    bool left = false, right = false, up = false, down = false;
    // The console's tilt (-1..1 to the right and towards the player) and a
    // shake (held); the 3DS games' motion sensor.
    float tiltX = 0, tiltY = 0;
    bool shake = false;
    // A real accelerometer (phones), the direction away from the earth in G
    // in the screen's axes; none without one.
    std::optional<std::array<float, 3>> motion;
    bool blow = false; // into the microphone (held)
    bool skip = false; // skip what plays now (a movie, a timed wait, a voice)
    std::vector<NameKey> name;
};

// What the host may show about the frame.
struct Status {
    bool skippable = false;  // a skip would end what plays
    bool listening = false;  // the scene listens to the microphone
    bool naming = false;     // the name entry takes typed keys
    bool movie = false;      // a movie plays
    bool puzzle = false;     // a puzzle is being solved
    // The touch-area help: rectangles on the lower screen and their kind
    // (0 text, 1 dialog, 2 puzzle not solved yet, 3 hint coin not found yet).
    struct Area {
        int x, y, width, height, kind;
    };
    std::vector<Area> areas;
};

struct StartOptions {
    // Continue from this checkpoint (the bytes checkpoint() returned) instead
    // of opening the title.
    std::optional<std::vector<std::uint8_t>> checkpoint;
    std::string language; // a languages() code, for multi-language games
};

class Session {
public:
    // Starts the story of an installed pack. Its progress (the cartridge
    // saves and save slots, the session's checkpoint) lives in `progress`.
    // Throws std::runtime_error when the game cannot be started.
    static std::unique_ptr<Session> start(Title title, const std::filesystem::path& pack,
                                          const std::filesystem::path& progress,
                                          const StartOptions& options);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Runs one frame with `input` and renders its sound into `stereo`
    // (interleaved, at `rate`). False when nothing ran: the game is still
    // loading, or has ended.
    bool step(const Input& input, std::span<float> stereo, int rate);

    Screens screens() const;
    // The latest frame: the upper screen's rows, then the lower screen's,
    // RGBA, each row as wide as the wider screen (narrower ones at the
    // left). False before the first frame.
    bool frame(std::vector<std::uint8_t>& rgba) const;
    Status status() const;

    // Why the game stopped (an error), or empty.
    std::string error() const;
    // The game has ended (story over, or an error).
    bool ended() const;

    // The session's automatic checkpoint, once one was written in this
    // session (or it continued from one): the game's own save record of the
    // last room that was ready. Empty for games without checkpoints.
    std::optional<std::vector<std::uint8_t>> checkpoint() const;
    // Counts the checkpoints written so far in this session: it changes on
    // the step whose frame was shown when a new checkpoint was written (the
    // host keeps that frame as the save's picture).
    std::uint64_t checkpointGeneration() const;

    // Checks a checkpoint's bytes with the game's own reader before a
    // session is started from them (in a scratch folder made and removed
    // inside `scratch`); empty when they can be used, else why not.
    static std::string checkCheckpoint(Title title, const std::filesystem::path& pack,
                                       const std::filesystem::path& scratch,
                                       std::span<const std::uint8_t> bytes);

private:
    struct Implementation;
    explicit Session(std::unique_ptr<Implementation> implementation);
    std::unique_ptr<Implementation> impl_;
};

} // namespace picarat::embed
