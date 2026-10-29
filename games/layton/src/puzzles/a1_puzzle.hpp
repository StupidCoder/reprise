#pragma once
// The first adventure's puzzle scene: one puzzle from its question to the
// answer (ARM9 02000f54 and the puzzle base object 0203fb94..02042ca0).
//
// How a puzzle plays
// ------------------
// The upper screen shows the question under a header with the puzzle's
// number, its current picarat value and the hint coins. The question is
// typed out two characters per frame while a "touch" prompt pulses on the
// lower screen; touching ends the typing. Then the lower screen belongs to
// the puzzle's engine, with two buttons of the frame: the hint button
// (three hints, each bought once with a hint coin) and the quit button.
// Each engine decides when the puzzle is answered, right or wrong.
//
// The puzzle's parameter script (script/qscript/q<N>_param.gds) names the
// engine (command 0x1b) and passes it its parameters; the frame handles the
// few commands every engine shares (backgrounds, flags).
#include "puzzles/a1_catalogue.hpp"
#include "system/a1_ui.hpp"
#include "system/script.hpp"
#include <functional>
#include <memory>

namespace picarat {
class Adventure1Sound;
}

namespace picarat::adventure1 {

// The parts of the game's state block (`*(020dec0c)`) a puzzle reads and
// writes.
struct PuzzleState {
    // +0x24 + N: per internal puzzle: 1 seen, 2 solved, 8 / 0x10 / 0x20
    // hint 1 / 2 / 3 bought, 0x40 / 0x80 wrong-answer count bits.
    std::array<std::uint8_t, 256> puzzles{};
    std::uint16_t coins = 0;                 // +0x534 hint coins
    std::uint32_t score = 0;                 // +0x640 picarats
    std::array<bool, 32> kindTutorialSeen{}; // +0x468 + kind
    bool solved = false;                     // +6: the last answer was right
    // Set by the parameter script, cleared by the result screen:
    std::uint8_t answerPicture = 0; // +0xc (command 0x1f): q<N>a_bg after a right answer
    bool answerLocalized = false;   // +0x13: that picture is in the language folder
    std::uint8_t wrongPicture = 0;  // +0xd (command 0xc0): q<N>f_bg after a wrong one
    // +0x4f0 (commands 0x66 / 0x67): the "r" judgment pictures. Puzzles
    // without either command keep the last value; a new game has 1
    // (measured at the first puzzle in the reference emulator).
    bool secondJudge = true;
    bool worthShown = false;  // +0x644: the title card showed the value
    // +0x538: no quit button (the new game's first puzzle, 0200129c); the
    // base object builds an off-screen rectangle instead (0205102c).
    bool hideQuit = false;
    int retries = 0;          // +0xb: tries since the puzzle was opened
    bool retry = false;       // +7: play the puzzle again
    std::uint32_t random = 0; // 020de964: the game's random state (020b31f8)

    // +0x71c / +0x720: the Tile engine's arrangement, kept between the tries
    // of one puzzle (02001044 clears it when a puzzle starts and ends).
    struct TileArrangement {
        bool valid = false;
        std::array<int, 24> x{}, y{}, slot{}, angle{};
        std::array<bool, 0x40> occupied{};
    } tiles;
};

// The hint screen's lower-screen parts: up to three tabs (the next shown
// once the previous hint is bought), the hint text or the question whether
// to unlock it, yes / no, back and the coins left. The puzzle scene's hint
// screen (020421c8, base object +0x44..+0x60) and the result's "hints first"
// (0205fb58, its own object) work alike.
class HintView {
public:
    HintView(SceneKit& kit, TouchState& touch, const Catalogue& catalogue, PuzzleState& state,
             unsigned id);

    // The tab opened first: the first hint not bought (with all bought, the
    // last tab stays), the first once solved; then its page.
    void open();
    // 0205f498 / the frame's part (02040e40): tabs, and yes / no while the
    // open hint is locked and coins are left.
    void drawControls();
    // 02051138 on back, then the coins at (0xac, 0x6b) (020419a4 /
    // 0205f6f8) while the open hint is locked.
    void drawBack();
    // After a frame: a tab, buying the open hint ("yes"); false on "back"
    // or "no".
    bool step();
    // On leaving: a tab past the hints falls back to the first.
    void close();

private:
    SceneKit& kit;
    TouchState& touch;
    const Catalogue& catalogue_;
    PuzzleState& state_;
    unsigned id_;
    Button tabs_[3], yes_, no_, back_;
    SceneKit::SpriteObject digits_;
    int tab_ = 0; // +4 / +0
    void show(int page);
};

class PuzzleScene;

// A puzzle engine (one of the 24 names of command 0x1b, 02011610).
class Engine {
public:
    virtual ~Engine() = default;
    // The engine's parameter commands (the object's virtual functions the
    // shared command handlers call).
    virtual void bind(ScriptContext& context, PuzzleScene& scene) = 0;
    // The engine's play loop (virtual +0x14): returns when the puzzle has
    // been answered (PuzzleScene::markSolved / markWrong) or abandoned.
    virtual void play(PuzzleScene& scene) = 0;
    // Drawing for one frame (virtual +0x274); `interactive` is 0 while the
    // question is typed out.
    virtual void draw(PuzzleScene& scene, bool interactive) = 0;

    // The black screen while the result loads its music (02004b08 in the
    // constructor, 0203e248). The loader first waits until no sound plays:
    // 174 frames when the "Submit" sound (0xb) is still playing (measured on
    // puzzles 1 and 23), less after engines that end without it.
    virtual int resultLoadFrames() const { return 174; }
};

class PuzzleScene {
public:
    enum class Outcome { Solved, Wrong, Quit };

    PuzzleScene(SceneKit& kit, TouchState& touch, const Catalogue& catalogue, unsigned id,
                PuzzleState& state);
    ~PuzzleScene();

    // Runs the parameter script and the engine's play loop.
    Outcome run();

    // The result's loading pause after this puzzle (Engine::resultLoadFrames).
    int resultLoadFrames() const { return engine_ ? engine_->resultLoadFrames() : 174; }

    // --- Services for engines.
    SceneKit& kit;
    TouchState& touch;
    // The game's sound object (null in research runs without sound).
    Adventure1Sound* sound = nullptr;

    // A frame of the scene while the sound object waits for sounds to end.
    void soundWait() { kit.present(); }

    // The looped effect (0200552c) and its stop (02005594; out over 5
    // frames).
    void startLoopedEffect(int id);
    void stopLoopedEffect();

    unsigned id() const { return id_; }

    PuzzleState& state() { return state_; }

    // One frame: input (0200310c and the touch update), `draw`, present
    // (02003ef8).
    template <class Draw>
    void frame(Draw draw) {
        beginFrame();
        draw();
        endFrame();
    }

    void beginFrame();
    void endFrame();

    // 0204132c: types the question out; ends on a touch.
    void intro();
    // 02040e40: the header, the frame's buttons and its overlays.
    void drawCommon(bool interactive);

    // The frame's buttons as engines check them after a frame.
    bool hintClicked() { return hint_.clicked(kit, touch); }

    bool quitClicked() { return quit_.clicked(kit, touch); }

    // The frame's two buttons themselves (engines test them without sound).
    Button& hintButton() { return hint_; }

    Button& quitButton() { return quit_; }

    // 02050f1c on the quit button: the same sprite again (a pending press
    // is forgotten).
    void resetQuitButton();

    // The engine's lower background (command 0x0b, or the engine's own);
    // the hint screen and the quit dialog return to it.
    const std::string& engineBackground() const { return engineBackground_; }

    void setEngineBackground(const std::string& name);

    // Palette entry 0xfd of the lower text layer (command 0x42), the memo
    // ink's colour; black unless a script sets it.
    std::uint16_t inkColour() const { return inkColour_; }

    // 020421c8: the hint screen: three tabs; a locked hint is bought with a
    // hint coin; "back" (or "no") returns to the puzzle.
    void hintScreen();
    // 02042a4c: "Quit?" - true when the player leaves the puzzle.
    bool quitDialog();
    // 0200fce8(1, 1): both screens to black, one step a frame (the last
    // drawn frame stays).
    void fadeOut();
    // 02042bcc / 02042c28: the answer was right / wrong.
    void markSolved();
    void markWrong();

private:
    const Catalogue& catalogue_;
    PuzzleState& state_;
    unsigned id_;
    std::unique_ptr<Engine> engine_;
    std::string question_; // q_N.txt, escapes expanded
    Button hint_, quit_;
    // The hint screen (base object +0x44..+0x60) and the quit dialog
    // (+0x68/+0x6c yes/no, +0x70 message).
    HintView hints_;
    Button quitYes_, quitNo_;
    SceneKit::SpriteObject numbers_, numbers2_, touchPrompt_, laterMessage_;
    // Tutorial page buttons (+0x34 back, +0x38 to the puzzle, +0x30 next).
    Button pagePrevious_, pageClose_, pageNext_;
    bool quitDone_ = false, noTutorial_ = false;
    bool hintOpen_ = false, quitOpen_ = false; // +0x24 / +0x25
    std::string engineName_, engineBackground_;
    std::uint16_t inkColour_ = 0x8000;
    void bindCommon(ScriptContext& context);
    void drawHeader();
    void tutorialPages(int kind, int pages);
    int engineKind() const;
    std::unique_ptr<Engine> createEngine(const std::string& name);
};

// Playing one puzzle as 02001044 does: the title card (QuestionIntroObject,
// 0203c85c), the puzzle scene and the result (QuestionEndObject, 0203e248;
// not reconstructed yet).
class PuzzlePlay {
public:
    PuzzlePlay(GraphicsAssets& assets, PresentFrame present, const Catalogue& catalogue,
               const std::string& language, unsigned id, PuzzleState& state,
               Adventure1Sound* sound = nullptr);

    PuzzleScene::Outcome play();
    // The title card alone (0203db80).
    void titleCard();
    // The result screens (0203e248 / 0203f1c4) after an answer.
    void result(int loadFrames);

    SceneKit kit;
    TouchState touch;

private:
    const Catalogue& catalogue_;
    unsigned id_;
    PuzzleState& state_;
    Adventure1Sound* sound_ = nullptr;

    void voiceLine(std::uint32_t table, std::uint32_t random);
};

// The text box's typing tick for the two characters typed this frame
// (0200df2c: 02005414 at every fifth character, 0200546c after each). The
// tick waits for the previous one to end; `hold` shows a frame it holds up.
// `ticks` counts a text's ticks for the measured holds without sound.
void typingTick(Adventure1Sound* sound, std::string_view text, std::size_t typed, int& ticks,
                const std::function<void()>& hold);

} // namespace picarat::adventure1
