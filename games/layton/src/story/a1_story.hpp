#pragma once
// The first adventure from the logos to the staff roll.
//
// How the story runs
// ------------------
// The game is one loop (ARM9 02000cb4) around a router (02000d28) that runs
// whatever the state's scene number names: an event, a room, a puzzle, a
// movie, a page of the trunk. Each of these runs until it has set the next
// scene, then returns to the router.
//
// Almost everything the player sees in the story comes from scripts:
//  * an event (script/event/e<N>.gds) shows backgrounds, characters and
//    their talk, gives items, starts puzzles and movies, sets story flags and
//    names the next scene;
//  * entering a room first runs script/rooms/room<N>_in.gds, which may
//    start events, then the room itself is built from room<N>_param.gds:
//    its background, exits, characters to talk to (events), hidden hint
//    coins and things to touch.
// The native code supplies the screens the scripts use (talk windows,
// choices, prize windows, the save screen) and the screens around the
// story (title, name entry, the trunk's pages, the staff roll).
//
// Like the original, each screen is a blocking loop: it polls input, draws
// and presents frames until it is done.
#include "puzzles/a1_catalogue.hpp"
#include "story/a1_opening.hpp"
#include "story/a1_state.hpp"
#include "system/script.hpp"
#include <filesystem>
#include <memory>
#include <optional>

namespace picarat {
class Adventure1Sound;
}

namespace picarat::adventure1 {

class Story {
public:
    Story(GraphicsAssets& assets, PresentFrame present, const std::filesystem::path& pack,
          const std::string& language, Adventure1Sound* sound);
    ~Story();

    // 02000cb4: the main loop; returns only when the host stops the game
    // (the present callback throws).
    void run();

    // --- Services for scenes and script commands. ---------------------------
    SceneKit kit;
    TouchState touch;
    GameState state;
    Catalogue catalogue;
    Adventure1Sound* sound = nullptr;
    const std::filesystem::path pack;
    const std::string language;

    // 0200310c: input for a frame (keys, the 3D list, touch).
    void poll();
    // 02003ef8: present the frame (the game's frame counter counts it).
    void present();

    // poll, draw, present.
    template <class Draw>
    void frame(Draw draw) {
        poll();
        draw();
        present();
    }

    // 02003d64: n VBlank waits: the screen and the frame counter stand still,
    // the sound runs.
    void stall(int frames);
    // 02003d94: like stall, but a touch released ends it early.
    void stallUntilRelease(int frames);

    // A measured loading pause: holds until `frames` frames have passed since
    // `since` (a value of kit.frames), when the native code took fewer.
    void loadingPause(std::uint64_t since, int frames) {
        const auto spent = kit.frames - since;
        if (spent < std::uint64_t(frames)) {
            kit.hold(int(std::uint64_t(frames) - spent));
        }
    }

    // 02003de0: waits (screen standing) for a touch released or the A button.
    void waitForTouch();

    // A text file of one of the game's text folders ("etext", "e000",
    // "e1_t0.txt"), escapes expanded; empty when missing.
    std::string text(const std::string& folder, const std::string& archive,
                     const std::string& name) const;
    bool hasText(const std::string& folder, const std::string& archive,
                 const std::string& name) const;

    // The program's data (the pack's code/arm9.bin, loaded at 0x02000000):
    // a word, or the string a literal-pool word at `address` points to.
    std::uint32_t programWord(std::uint32_t address) const;
    std::string programString(std::uint32_t pointerAddress) const;

    // A picture a script names ("ebg_1.bgx", "?/ebg_2.bgx") into `layer`. A
    // file the cartridge lacks (event 8 names ebg_261 without "?/", which
    // only exists per language) is read as nothing by the original
    // (020033f0 copies a cleared buffer), and the old picture's map stays:
    // the layer keeps its picture (hypothesis; not compared with the ROM).
    void setPicture(std::string& layer, const std::string& scriptName);

    // A script file of the pack ("script/logo.gds"); empty when missing (the
    // original's loader leaves an empty script, which runs nothing).
    Bytes script(const std::string& path) const;
    // 02017dcc + 02018050: runs a script on the commands of `context`.
    void runScript(const std::string& path, const ScriptContext& context);

    // The commands every script object handles the same way (the handlers
    // that do not call the running object, and the base object's empty
    // virtual functions). Scenes bind their own commands on top.
    ScriptContext commonCommands();

    // A number below `n` from the game's random state (020b31f8).
    int randomBelow(int n);
    // The cartridge backup, kept in `saveFile` (the host's; empty: only in
    // memory). Read on first use; a missing or damaged file is an empty
    // backup (02024538).
    std::filesystem::path saveFile;
    const SaveImage& saves();
    // 0201a8e8: the game into slot n, written out at once.
    void storeGame(int slot);
    // The bonus section's saves (0201a8e8 into the loaded slot, scene 3).
    void saveBonusProgress();
    // 0201aadc: slot n into the state; false when it does not hold a game.
    bool loadGame(int slot);
    // The app's checkpoint (a host convenience): with `checkpointFile` set,
    // every room writes the game into it when it is ready (a backup image
    // of its own, slot 0; 0201a8e8); `continueCheckpoint` makes run() load
    // it and enter its room instead of booting to the title (as a loaded
    // slot continues, without the story-so-far event 0x12d).
    std::filesystem::path checkpointFile;
    bool continueCheckpoint = false;
    void saveCheckpoint();
    bool loadCheckpoint();
    void bootSound(); // 02001efc: sounds of kind 1, voice group 0

    // Whether a save slot holds a game (02024338 on the slot headers).
    bool hasSavedGame() { return saves().anyUsed(); }

    // Stops the game where a part of it is not reconstructed yet.
    [[noreturn]] void missing(const std::string& what) const;

    // Research hook: named points of the original's flow (constructors and
    // run functions), to line native timelines up with reference runs.
    std::function<void(const char* name)> onMarker;

    void marker(const char* name) {
        if (onMarker) {
            onMarker(name);
        }
    }

    // Research only (local tools): replaces playing puzzle `id` with its
    // outcome (true: solved). The app never sets it.
    std::function<bool(unsigned id)> researchSolvePuzzle;

    // Research only: what a room offers to touch, reported every frame of
    // the room's loop and of its move mode (an auto-player picks from it).
    struct ResearchTarget {
        enum Kind { Character, Exit, MoveMode, Options, Coin } kind;

        int x, y, w, h;
        int value;          // the character's event, the exit's room, the coin
        bool arrow = false; // an exit shown only in move mode
    };

    std::function<void(bool moveMode, const std::vector<ResearchTarget>&)> researchRoom;
    // Research only: instead of the boot, starts from a new game's state
    // that this function changes (e.g. an event to run).
    std::function<void(GameState&)> researchStart;
    // A host convenience: the boot leaves out logo.gds (it only shows the
    // logos and plays their effect) and goes straight to the title. The app
    // sets it; research tools keep the power-on timeline.
    bool startAtTitle = false;

    // Research hook: called before each script command runs.
    std::function<void(const std::string& script, std::uint16_t command)> onCommand;

    // The movie being described by a movie script (0xef / 0xf0 / 0xfd).
    MovieCues movieCues;

    // The actor managers (ARM9 021ebb3c: characters, 3D sprites on the lower
    // screen, oSpriteObject 02059fa8; 021ebb2c: 2D sprites on the upper
    // screen, oSubSpriteObject 0205a3f0). Scripts add actors; while the
    // managers are active, every poll moves and draws them in the order
    // they were added (0200310c -> 0200f090, 0200f0c4).
    struct Actor {
        SceneKit::SpriteObject sprite;
        bool upper = false;
        bool visible = true;              // +8
        bool flip = false;                // +9: drawn mirrored (0x5b4)
        int shake = 0;                    // +0xc: frames left of a one-pixel jitter
        float alpha = 31.0f, fade = 0.0f; // +0x10 / +0x14 (lower actors)
        bool moveToTarget = false;        // +0x18 ("Normal" / "Move To Target")
        int targetX = 0, targetY = 0, x = 0, y = 0, speed = 1; // +0x1c.. +0x2c
        // +0x30: 0 always drawn; 1 drawn only for the speaker while it talks;
        // 2 a mouth: its "*" animation turns into the next one while its
        // character talks.
        int kind = 0;
        int part = -1;  // the event's link to a face part (+0x1a8)
        int plate = -1; // its name plate (+0x214)
    };

    std::vector<Actor> actors, upperActors;
    bool actorsActive = true; // 020dec3c byte 0
    int speaker = -1;         // manager +0x44
    bool talking = false;     // manager +0x42
    // 02059fa8 / 0205a3f0: a new actor; returns its index.
    int addActor(bool upper, int x, int y, const std::string& sprite, const std::string& animation);
    void setActorAnimation(Actor& actor, const std::string& name);

private:
    void countPlayTime();
    std::uint64_t countedFrames_ = 0;
    void moveAndDrawActors(); // 0200f090 + 0200f0c4
    std::string currentScript_;
    PresentFrame presentFrame_;
    std::optional<SaveImage> saves_;
    Bytes program_;

    void route();        // 02000d28
    void boot();         // 0200129c: logos, title, new game or continue
    void logos();        // 02001efc: script/logo.gds
    void roomScene();    // 02002324: room<N>_in.gds, then the room
    void newGame();      // 0200129c after "new game"
    void playQuestion(); // 02001044 with the current puzzle
};

} // namespace picarat::adventure1
