#pragma once
// The third adventure's puzzle screens, from the title card to the verdict.
//
// A puzzle is played as a short series of script scenes, each a fresh set of
// objects with its own thread (the game's lt3CLoopNazo, overlay 35):
//
//  1. the title card - evt/prg/lt3_nazo_start, section MAIN;
//  2. the puzzle itself - evt/lt3_nazo with the hint, quit and answer-input
//     programs and the puzzle's own script rc/nazo/naz_sc<N/100>/nNNN,
//     section NAZO_MAIN, on the common engine or a special one
//     (a3_engines.hpp);
//  3. unless the player left the puzzle, the verdict - evt/prg/lt3_nazo_result,
//     section MAIN; asking to try again starts over at 1.
//
// The scenes run the game's own scripts (src/system/a3_lscr_scene.*); this loop
// only does what the game's code does between them: it reads the puzzle's
// texts and header (rc/nazo/?/naz_df<N%10>/nNNN, commands 3050..3057), sets
// the game variables the scripts read and records the result.
#include "graphics/text.hpp"
#include "system/a3_lscr_scene.hpp"
#include <array>
#include <functional>
#include <map>
#include <memory>

namespace picarat {
class Adventure3Sound;
}

namespace picarat::adventure3 {

// What the game remembers about one puzzle.
struct PuzzleRecord {
    int state = 0; // 0 new, 1 seen, 2 solved (020ea490)
    int failures = 0;
    int hints = 0;          // hints opened
    bool available = false; // +0x100: can be played again (its phase came)
    bool marked = false;    // +0x200
};

// Whether the loop can play a puzzle: its engine is reconstructed and every
// command and condition its scripts can reach is reconstructed. Empty when
// it can; otherwise what is missing.
class PuzzleSupport {
public:
    PuzzleSupport(Assets& assets, const GameText& text);
    std::string check(unsigned number);

private:
    Assets& assets_;
    lscr::Scene scene_; // the shared programs, compiled once
};

class PuzzleLoop {
public:
    // `sound`, when given, plays the puzzle's sound.
    // `setState`, when given, records the puzzle found (1) and solved (2)
    // the way the story does - the game's 020e61b8 through 020ea4a4, which
    // also counts and awards picarats; without it only `record` changes.
    PuzzleLoop(Assets& assets, const GameText& text, lscr::GameState& state, unsigned number,
               PuzzleRecord& record, Adventure3Sound* sound = nullptr,
               std::function<void(int number, int state)> setState = {});
    ~PuzzleLoop();

    void frame(const lscr::SceneInput& input);
    Surface render() const;

    bool finished() const { return phase_ == Phase::Done; }

    lscr::Scene* scene() { return scene_.get(); }

    // The brightness the first scene starts from (the screen the puzzle was
    // opened from faded out to it).
    void setBrightness(int level);

    // Called with each new scene once its thread has started (research
    // traces).
    std::function<void(lscr::Scene&)> onScene;

private:
    enum class Phase { Start, Engine, Result, Done };
    void prepare();
    void begin(Phase phase);
    void ended();

    Assets& assets_;
    const GameText& text_;
    lscr::GameState& state_;
    PuzzleRecord& record_;
    int id_ = 0, number_ = 0, type_ = 0;
    std::array<std::string, 16> strings_;    // 1540: the puzzle's texts
    std::vector<lscr::Scene::Piece> pieces_; // remembered while the loop runs
    std::map<std::string, Surface> memos_;   // the memo's ink, likewise
    Phase phase_ = Phase::Start;
    std::unique_ptr<lscr::SceneEngine> engine_; // outlives the scene using it
    std::unique_ptr<lscr::Scene> scene_;
    std::array<int, 2> brightness_{};
    Adventure3Sound* sound_ = nullptr;
    std::function<void(int, int)> setState_;

public:
    // Whether frame() also runs the sound's frame (false when a host that
    // owns the sound clock - the story - runs the loop).
    bool drivesSound = true;
};

// The sound sets (rc/ge/lt3_rc/lt3_snd, 5301 rows): set -> music, GE bank,
// SI bank.
std::map<int, std::array<int, 3>> soundSets(Assets& assets);

} // namespace picarat::adventure3
