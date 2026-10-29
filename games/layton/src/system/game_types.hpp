#pragma once
// The types shared between the game and its host (the native app or a test
// driver): what the host sends each frame, the scene being shown, what the
// game reports back, and the exceptions used to leave a scene.
#include "graphics/graphics.hpp"
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace picarat {
// A key typed on the computer keyboard during name entry (a native
// convenience; the name editor still applies the game's rules).
struct NameInput {
    enum class Action { None, Insert, Erase, Confirm } action = Action::None;
    std::uint8_t character = 0;
};

// Everything the host passes in for one frame: the stylus (the mouse), and
// requests such as restart, skip a movie, open the puzzle collection, or
// "blow into the microphone".
// Skipping (the same for every game of the collection): a game reports in
// GameSnapshot::skippable whenever what plays can be skipped - a movie, a
// timed scripted wait (logos, still pictures) or a wait for a voice to end.
// The host's request arrives as HostInput::skip; the session keeps it set
// for as long as the game goes on reporting skippable, so a whole sequence
// (all the logos, all narration pages) ends with one request. What triggers
// the request (the in-game menu, a gesture, a key) is the host's business.
struct HostInput {
    bool down = false, restart = false, resume = false;
    int x = -1, y = -1;
    bool skip = false; // the host asks to skip what plays (see above)
    bool puzzle = false, load = false;
    NameInput name;
    int collection = 0;
    bool blow = false;
    // The pad held (bits: left, right, up, down) and the console's tilt
    // (-1..1 to the right and towards the player; the 3DS games' motion
    // sensor). Only games that read them get them.
    unsigned pad = 0;
    float tiltX = 0, tiltY = 0;
    bool shake = false; // the console shaken from side to side (held)
    // A real accelerometer (phones): the direction away from the earth in G,
    // in the screen's axes (x right, y up, z out of the screen; Host::motion).
    std::optional<std::array<float, 3>> motion;
    bool sleep = false; // lid closed (window minimised); only the movie scene receives it, other
                        // scenes sleep in the host
};
// Which kind of scene is being shown (reported to the host; also used for
// scene-specific drawing and input).
enum class SceneKind {
    Logos,
    Title,
    Name,
    Accepted,
    Story,
    Movie,
    Room,
    PuzzleIntro,
    Puzzle,
    PuzzleHint,
    PuzzleMemo,
    PuzzleJudgment,
    PuzzleWrong,
    PuzzleReward,
    PuzzleSolved,
    Complete,
    Collection,
    PuzzleCard,
    Tea,
    Bag,
    Camera,
    Photo,
    Hamster,
    Staff,
    Shack,
    Mysteries,
    Notes,
    Diary,
    PuzzleIndex,
    Challenge,
    Passcode,
    Files,
    Bonus
};

// Touch-area help (the same for every game of the collection; a host
// convenience): while the player looks around a room, the game reports
// what there is to touch and what a touch starts, read from the room's own
// data by the game's own rules - a remark ("text"), a conversation or other
// event ("dialog"), an event that leads to a puzzle not solved yet
// ("puzzle"), a hint coin not found yet. Ways out are left out (the games
// show them). Whether and how the areas are shown is the host's business.
struct TouchArea {
    enum class Kind : std::uint8_t { Text, Dialog, Puzzle, HintCoin };
    Kind kind = Kind::Text;
    int x = 0, y = 0, w = 0, h = 0; // lower-screen pixels
};

// What the game reports with every frame. Beyond the scene and name, the
// fields serve research tools and tests; the host makes no game decisions
// from them.
struct GameSnapshot {
    SceneKind scene;
    NameEditor name;
    std::uint32_t tick;
    int detail = 0; // Event, movie, room or puzzle ID; research/self-test visibility.
    unsigned puzzleProgress = 0, coins = 0,
             picarats = 0;        // Research visibility, not host decisions.
    bool storyBoundary = false;   // Native test-build stop, distinct from puzzle completion.
    std::string boundary{};       // Reason shown when the story reaches unreconstructed coverage.
    int place = 0, storyStep = 0; // Story router state (research visibility).
    bool skippable = false;       // a movie or scripted wait the host's skip ends (HostInput)
    std::vector<TouchArea> touchAreas{}; // the room's touchable things (see TouchArea)
    std::array<int, 6> pieceSlots{-1, -1, -1, -1, -1, -1};
};

// Shows a finished frame (both screens) and returns the next frame's input.
using PresentFrame = std::function<HostInput(const Surface&, const GameSnapshot&)>;

// Thrown to leave the running scene; caught by the outermost game loop.
struct RestartGame {};

// Original scene 0 (02069f20): new game state, then the title (scene 12)
// without the boot logos.
struct ReturnToTitle {};

struct JumpToPuzzle {};

struct LoadCheckpoint {};

struct OpenCollection {
    int internalId = -1;
};
} // namespace picarat
