#pragma once
// The puzzle scene: plays one puzzle from title card to result. See
// a2_puzzle_game.cpp for the flow and the engine of each puzzle type.
#include "graphics/text.hpp"
#include "puzzles/a2_answer_puzzle.hpp"
#include "puzzles/a2_bridge_puzzle.hpp"
#include "puzzles/a2_choice_puzzle.hpp"
#include "puzzles/circle_ink.hpp"
#include "puzzles/a2_color_puzzle.hpp"
#include "puzzles/a2_couple_puzzle.hpp"
#include "puzzles/judgment.hpp"
#include "puzzles/a2_knight_puzzle.hpp"
#include "puzzles/a2_lamp_puzzle.hpp"
#include "puzzles/line_puzzle.hpp"
#include "puzzles/a2_peg_puzzle.hpp"
#include "puzzles/a2_puzzle_progress.hpp"
#include "puzzles/a2_rose_puzzle.hpp"
#include "puzzles/a2_selection_puzzle.hpp"
#include "puzzles/a2_skate_puzzle.hpp"
#include "puzzles/a2_slide_puzzle.hpp"
#include "puzzles/slot_puzzle.hpp"
#include "puzzles/a2_tile_puzzle.hpp"
#include "puzzles/a2_toggle_puzzle.hpp"
#include "puzzles/a2_tower_puzzle.hpp"
#include "puzzles/trace_puzzle.hpp"
#include "sound/sound_object.hpp"
#include "system/audio_service.hpp"
#include "system/game_types.hpp"
#include <optional>

namespace picarat {

// How a puzzle is opened, as decided by whoever opens it.
struct PuzzleLaunch {
    // Original state +479: the scene after the puzzle. 0x13 is the puzzle
    // index (here: the native puzzle collection), 22 the special catalogue,
    // anything else a story scene.
    unsigned followingScene = 0;
    // Whether the hint button may be used (the special catalogue can turn it
    // off; practice in the collection always allows hints).
    bool hintAvailable = true;
    // Catalogue number of a special puzzle (original +81d0), if known.
    std::optional<unsigned> specialNumber;
};

class PuzzleGame : private PicaratCountHost, private JudgmentHost {
public:
    // `internalId` is the puzzle's record number (puzzle/n<id>.dat). Without
    // a `session`, the puzzle keeps its own progress.
    PuzzleGame(GraphicsAssets& assets, PresentFrame present, NameEditor name, AudioService* audio,
               PuzzleSession* session = nullptr, unsigned internalId = 1, PuzzleLaunch launch = {});

    // Plays the puzzle; true when solved, false when quit.
    bool run();

    // Draws the current frame (both screens).
    Surface render();

    GameSnapshot snapshot() const;

private:
    // A sprite button of the puzzle screen.
    struct Control {
        const Sprite* sprite = nullptr;
        Button button;
        int x = 0, y = 0;
        std::string idle = "off", pressed = "on";
    };

    GraphicsAssets& assets_;
    PresentFrame present_;
    NameEditor name_;
    AudioService* audio_;

    GameSound* music() { return audio_ ? audio_->sequenced() : nullptr; }

    void introCard();
    void drainSounds();
    bool soundsLive_ = false;
    void shutdownSounds();
    void startMusic();

    // Internal puzzle 0x87 uses other music, except when opened from the
    // puzzle index or scenes 0x16 / 0x1f (020f34a8).
    bool specialMusic() const {
        const auto f = launch_.followingScene;
        return internalId_ == 0x87 && f != 0x13 && f != 0x16 && f != 0x1f;
    }

    void effect(int id) {
        if (auto s = music()) {
            s->playEffect(id);
        }
    }

    GameText text_;
    unsigned internalId_;
    PuzzleLaunch launch_;
    // Hints unlocked during this visit when hints are free (replays and
    // special puzzles; original +8214).
    unsigned transientHints_ = 0;

    // Special-catalogue puzzles and internal puzzle 206 have a single hint.
    bool singleHint() const { return launch_.followingScene == 22 || internalId_ == 206; }

    // Hints cost nothing for those, and for puzzles already solved.
    bool freeHints() const { return singleHint() || session_.first.status() == 2; }

    unsigned unlockedHints() const {
        return freeHints() ? transientHints_ : session_.first.hints();
    }

    void updateHintButton();
    PuzzleData data_; // the puzzle's record: type, texts, picarats, pictures

    // The engine of this puzzle; exactly one is created (see the constructor).
    std::optional<TilePuzzle> tiles_;
    std::optional<TowerPuzzle> towers_;
    std::optional<SelectionPuzzle> selection_;
    std::optional<ColorPuzzle> colors_;
    std::optional<KnightPuzzle> knight_;
    std::optional<PegPuzzle> pegs_;
    std::optional<ChoicePuzzle> choices_;
    // ov42 handwritten answers: recognizer, engine, own controls and layers.
    std::optional<handwriting::Recognizer> recognizer_;
    std::optional<AnswerPuzzle> answers_;
    Control kaito_, hantei_, allreset_, modoru_, pictureToggle_;
    std::array<Control, 5> erasers_;
    CircleInk answerInk_; // ov42 text/ink layer (palette entry 15, observed black).
    bool answerFailure_ = false;
    int screenBrightness_ = 0; // Both-screen fade level (ov2 02071790/02071e84).
    void answerFrame(bool input);
    bool updateAnswers();
    void fadeScreens(bool in);
    void renderAnswerScreen(Surface& image);
    // ov37 grid lines: engine, its script (retry recreates it) and 3D layer.
    std::optional<LinePuzzle> lines_;
    std::optional<SlidePuzzle> slides_;   // ov47 sliding blocks
    std::optional<TogglePuzzle> toggles_; // ov45 toggle grid
    std::optional<RosePuzzle> roses_;     // ov46 rose placement
    std::optional<LampPuzzle> lamps_;     // ov52 street lamps
    std::optional<SkatePuzzle> skate_;    // ov49 ice skating
    AnimationPlayer skater_;
    std::optional<CouplePuzzle> couples_; // ov51 moving pairs
    std::optional<BridgePuzzle> bridge_;  // ov53 stepping stones
    std::optional<TracePuzzle> traces_;   // ov34/ov54 traced strokes
    std::optional<SlotPuzzle> slots_;     // ov39 tiles on labelled slots
    const Surface& slotFrame(const SlotPuzzle::Tile& tile);
    CircleInk traceInk_; // Text layer 0x2088b44, palette entry 0xd (observed black).
    std::vector<std::uint8_t> lineScript_;
    void createLines();
    void renderLines(Surface& image);
    std::optional<CircleParameters> circleParameters_;
    CircleInput circle_;
    unsigned circlePage_ = 0;
    Control circleLeft_, circleRight_;
    CircleInk circleInk_;
    int circleInitialX_ = -1, circleLastX_ = -1, circleLastY_ = -1;
    void updateCircle();
    void resetEngine();
    SceneKind scene_ = SceneKind::PuzzleIntro;

    // The title card (020efda0 / 020f05c8 / 020f0860): position, fades and
    // the worth shown on the lower screen.
    struct Card {
        bool visible = false, title = false, lower = false;
        int y = 192, brightness = -16, lowerBrightness = -16;
        unsigned value = 0;
    } card_;

    void renderCard(Surface& image);
    void drawNumber(Surface& image, const Sprite& sprite, const std::string& variable,
                    unsigned value, bool pad, int dx, int dy); // ov2 020722fc
    unsigned picaratValue(int failures) const; // ov2 02083d50 index into +0x35..+0x37
    Touch touch_;                              // this frame's stylus state
    TouchFilter touchFilter_;                  // the same, as the overlay-2 filter reports it
    unsigned microphoneRange_ = 0;
    std::vector<AnimationPlayer> tileAnimations_;
    std::uint32_t tick_ = 0; // frames since the scene started
    // Which overlay is showing.
    bool wasDown_ = false, tutorial_ = false, retryMenu_ = false, hintConfirm_ = false,
         giveUp_ = false;
    PuzzleSession ownedSession_;
    PuzzleSession& session_;
    unsigned hintPage_ = 0;
    std::uint32_t countTotal_ = 0, countRemaining_ = 0;
    // The judgment sequence's current picture, brightness and slide.
    std::string judgmentImage_;
    bool judgmentFinal_ = false;
    int judgmentBrightness_ = -16, judgmentBottom_ = 0, judgmentTop_ = -191;

    // JudgmentHost: the judgment sequence draws through these.
    bool aborted() override { return false; }

    void image(const std::string& path, bool final) override {
        judgmentImage_ = path;
        judgmentFinal_ = final;
    }

    void brightness(int value) override { judgmentBrightness_ = value; }

    void present() override { frame(); }

    void wait(int frames) override;

    void slide(int bottom, int top) override {
        judgmentBottom_ = bottom;
        judgmentTop_ = top;
    }

    void effectBank(int number) override {
        if (auto s = music()) {
            s->loadBank(2, number);
        }
    }

    void sound(unsigned id) override {
        if (auto s = music()) {
            s->playEffect(int(id));
        }
    } // ov2 0207f850/0207f8f0

    void voice(unsigned id) override;
    void replayResult(); // ov27 020f1da4: puzzle-index replay result and its jingle
    bool resultScreen_ = false;
    std::string resultTitle_, resultHints_;
    void judgment(bool wrong);
    std::string judgmentResult(bool wrong) const;

    // PicaratCountHost: the reward count-up draws through these.
    void waitOneFrame() override { frame(); }

    bool confirmed() override { return touch_.released; }

    void stopCountSound() override {
        if (auto s = music()) {
            s->stopEffect(0x77, 0);
        }
    } // ov27 020f1548

    void update() override {}

    void present(std::uint32_t total, std::uint32_t remaining, std::uint32_t, bool) override;
    // The common buttons, the hint screen's buttons and the "quit" dialog's.
    Control answer_, reset_, hint_, memo_, later_, back_, retry_;
    Control hintBack_, hintYes_, hintNo_, giveYes_, giveNo_;
    std::array<Control, 3> hintTabs_, hintLocks_;
    // The memo (ov27: open 020f4068, run 020f4570, close 020f4304). Its ink
    // is drawn with the shared pen on the lower text layer and kept between
    // visits; the window grows from the top right while opening (only the
    // window shows the text layer) and shrinks back with the layer empty.
    Control erase_; // +0xc erase.spr: clears the ink
    CircleInk memoInk_;
    enum class MemoStage { Hidden, Opening, Open, Closing };

    struct {
        MemoStage stage = MemoStage::Hidden;
        int width = 0, height = 0; // window at the top right of the lower screen
    } memoWindow_;

    int memoHome_ = 0; // the memo button's place while it is away (+0x40)

    void slideMemoControls(bool away);                                        // 020f4068 / 020f4304
    void drawMemoWindow(Surface& image, int x, int y, int width, int height); // 020f3da0
    bool memoShowsTextLayer(int x, int y) const; // WIN0 while the memo is open
    void releaseControls();                      // virtual +228 (020f3634)
    void frame();
    Control control(const std::string& path, const std::string& idle = "off",
                    const std::string& pressed = "on", const std::string& position = "pos",
                    int sound = -1);
    void drawControl(Surface& target, const Control& c);
    bool controlAvailable(const Control* c) const;
    void drawSprite(Surface& target, const Sprite& sprite, const std::string& animation, int x,
                    int y);
    void header(Surface& target);
    void waitRelease();
    void hint();
    void memo();
};
} // namespace picarat
