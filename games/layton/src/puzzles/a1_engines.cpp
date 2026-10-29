// The first adventure's own puzzle engines. See a1_engines.hpp.
//
// Free Button (multiple choice; engine 14, ARM9 02049618..02049b80)
// ------------------------------------------------------------------
// How it plays: the answers are drawn on the lower background; each has an
// invisible button that shows its sprite only while held. Tapping one
// answers the puzzle at once, right or wrong. Puzzle 23 also has a crank
// to turn: tapping it shows the next of its four views.
//
// The rule matches the second adventure's multiple choice (overlay 30,
// puzzles/a2_choice_puzzle.*): the first completed click answers with that
// button's flag, at most 24 buttons. The engines are kept apart because
// everything around that rule differs: the command (0x5d with an integer
// flag and an unused fifth value, against 0x14 with a truth value and a
// click sound), the button behaviour (a1_ui.hpp: a click completes
// on the release, the sprite shows only while held), and the crank of
// puzzle 23, which is part of this engine's code.
//
// Draw Input2 (written answers; engine 23, ARM9 0204568c..02046880)
// -----------------------------------------------------------------
// How it plays: first the lower screen is a memo pad over the puzzle's
// picture; the stylus draws on it, and a button wipes it (nothing stays
// drawn over that button). "Enter answer" opens the answer screen: one or
// two answer slots and a writing box. Whatever is written in the box is
// read by the handwriting recogniser; the reading shows in the selected
// slot (a "?" after a second when nothing could be read) and, once the
// stylus has rested for 15 frames, the character is entered and the slot
// moves on. Slots fill left to right but show their characters right-
// aligned. "Erase" takes the slot's last character back, "clear" empties
// every slot, tapping a slot selects it, "back" returns to the (wiped) memo
// pad. "Submit" does nothing while a slot is empty; otherwise the answer is
// judged: every slot must hold exactly its expected characters.
//
// The writing box is the second adventure's (puzzles/written_answer.*, the
// first adventure's rules: ARM9 02052140..020528e8 is the same routine as
// overlay 42's, with -2 for "nothing read"), and the recogniser is shared
// (puzzles/handwriting.*, ARM9 020196e4). The memo pad is the same box
// recording nothing. Everything around it (slots, the delayed entry, the
// screens) is this engine's own: the second adventure writes one character
// per box and has no memo pad.
#include "puzzles/a1_engines.hpp"
#include "graphics/text.hpp"
#include "puzzles/circle_ink.hpp"
#include "puzzles/written_answer.hpp"
#include <optional>

namespace picarat::adventure1 {

namespace {

class FreeButton final : public Engine {
public:
    void bind(ScriptContext& context, PuzzleScene& scene) override {
        // 02049618 -> 02040ddc: the puzzle's picture q<N>_bg, if it has one
        // (a background command of the script, as puzzle 23's, replaces it).
        const std::string picture = "q" + std::to_string(scene.id()) + "_bg";
        if (fileExists(scene.kit.assets.root / "bg" / (picture + ".l2bg"))) {
            scene.setEngineBackground(picture);
        }
        // 0x5d (02013388 -> virtual +0x100, 020498f8): x, y, sprite, the
        // answer flag (!= 0) and a value this engine ignores.
        context.bind(0x5d, [&](CommandArguments& a) {
            const int x = a.integer(), y = a.integer();
            const std::string sprite = a.string();
            const bool correct = a.integer() != 0;
            a.integer();
            if (choices_.size() >= 24) {
                return; // 0x18 buttons at most
            }
            Choice choice;
            choice.button.setSprite(scene.kit, x, y, sprite);
            choice.button.sound = 0xb;
            choice.correct = correct;
            choices_.push_back(std::move(choice));
        });
        // 02049618: loading the engine's sprites and picture holds the
        // black screen 8 frames (measured, puzzle 23; a loading pause).
        scene.kit.hold(8);
        // Puzzle 23 (02049618): the crank's sprite (animation 1..4) and a
        // rectangle that turns it.
        if (scene.id() == 23) {
            crank_ = scene.kit.load("q23_gear.spr", 0, 0, 1);
            crankButton_.setRect(0x17, 0x14, 0x50, 0x4a);
            crankView_ = 1;
        }
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 020499c8.
        if (interactive) {
            for (auto& choice : choices_) {
                choice.button.update(scene.kit, scene.touch);
            }
        }
        if (scene.id() == 23) {
            crankButton_.update(scene.kit, scene.touch);
            scene.kit.draw(crank_);
            if (crankButton_.clicked(scene.kit, scene.touch, false)) {
                scene.kit.effect(0x11);
                crankView_ = crankView_ % 4 + 1;
                scene.kit.select(crank_, unsigned(crankView_));
            }
        }
        scene.drawCommon(interactive);
    }

    void play(PuzzleScene& scene) override {
        // 02049a88.
        scene.intro();
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                return;
            }
            for (auto& choice : choices_) {
                if (choice.button.clicked(scene.kit, scene.touch)) {
                    if (choice.correct) {
                        scene.markSolved();
                    } else {
                        scene.markWrong();
                    }
                    // 02049804 (destructor): both screens to black.
                    scene.fadeOut();
                    return;
                }
            }
        }
    }

private:
    struct Choice {
        Button button;
        bool correct = false;
    };

    std::vector<Choice> choices_;
    SceneKit::SpriteObject crank_;
    Button crankButton_;
    int crankView_ = 1;
};

// 0200c1ac: 8-bit components to the 3D engine's 15-bit colour.
std::uint16_t rgb(int r, int g, int b) {
    return std::uint16_t((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

class DrawInput final : public Engine {
public:
    explicit DrawInput(PuzzleScene& scene)
        : recognizer_(readFile(scene.kit.assets.root / "decuma/_databas_le.bin")) {
        // 0204568c.
        SceneKit& kit = scene.kit;
        // 02040ddc: the memo pad's picture q<N>_bg, if the puzzle has one.
        const std::string memo = "q" + std::to_string(scene.id()) + "_bg";
        if (fileExists(kit.assets.root / "bg" / (memo + ".l2bg"))) {
            scene.setEngineBackground(memo);
        }
        erase_.load(kit, "nazo/drawinput/?/erase.spr", 4);
        clear_.load(kit, "nazo/drawinput/?/clear.spr", 10);
        back_.load(kit, "nazo/drawinput/?/back.spr", 4);
        clearMemo_.setSprite(kit, 0xc0, 0x2f, "?/clear_btn.spr", "gfx");
        clearMemo_.visibleIdle = true;
        clearMemo_.sound = 10;
        enterAnswer_.setSprite(kit, 0x48, 0xa7, "?/input_ans.spr", "gfx");
        enterAnswer_.visibleIdle = true;
        enterAnswer_.sound = 0xb;
        // +0x7c: a rectangle under the quit button, updated but never asked.
        quitRect_.setRect(0xbc, 0x18, 0x43, 0xd);
        quitRect_.sound = 10;
        submit_.setSprite(kit, 0xba, 0x9d, "?/submit.spr", "gfx");
        submit_.visibleIdle = true;
        reading_ = kit.load("inputnumbers.spr", 0, 0, 1);
        for (auto& slot : slots_) {
            slot.digits = kit.load("inputnumbers.spr", 0, 0, 1);
        }
        cursor_ = kit.load("nazo/drawinput/di_cursor.spr", 0, 0, 1);
        // Loading the recogniser's database (0201969c: 119504 bytes of
        // decuma/_databas_le.bin), the picture, the sprites and font18
        // holds the black screen another 18 frames (measured in the
        // reference emulator, puzzles 2 and 105; a loading pause).
        kit.hold(18);
    }

    void bind(ScriptContext& context, PuzzleScene&) override {
        // 0xa8 (virtual +0x4c, 02045dec): the input mode (only 0..3 are
        // taken, anything else is 0) and the number of slots (below 4).
        context.bind(0xa8, [this](CommandArguments& a) {
            const int mode = a.integer(), count = a.integer();
            if (count < 0 || count >= 4) {
                return;
            }
            mode_ = mode >= 0 && mode <= 3 ? mode : 0;
            for (int i = 0; i < count; ++i) {
                slots_[std::size_t(i)].mode = mode_;
            }
            count_ = count;
        });
        // 0xfa (virtual +0x54, 02045e8c): slot index, x, y and its number
        // of characters (at most 16); the slot is 14 pixels a character
        // plus 3 wide and 0x18 high, and is its own button.
        context.bind(0xfa, [this](CommandArguments& a) {
            const int index = a.integer(), x = a.integer(), y = a.integer(), length = a.integer();
            if (index < 0 || index >= count_) {
                return;
            }
            Slot& slot = slots_[std::size_t(index)];
            slot.area = {x, y, length * 0xe + 3, 0x18};
            slot.button.setRect(x, y, length * 0xe + 3, 0x18);
            slot.length = std::min(length, 16);
        });
        // 0xfb (virtual +0x58, 02066f00): the slot's expected characters:
        // digit values (mode 0) or characters folded to upper case (modes
        // 1, 4 and 5; the table at 020ce3b8). Unwritten places stay -1.
        context.bind(0xfb, [this](CommandArguments& a) {
            const int index = a.integer();
            const std::string text = a.string();
            if (index < 0 || index >= count_) {
                return;
            }
            Slot& slot = slots_[std::size_t(index)];
            for (std::size_t i = 0; i < text.size() && i < 16; ++i) {
                const int c = static_cast<signed char>(text[i]);
                if (slot.mode == 0) {
                    slot.expected[i] = c - '0';
                } else if (slot.mode == 1 || slot.mode == 4 || slot.mode == 5) {
                    slot.expected[i] = c >= 'a' && c <= 'z' ? c - 0x20 : c;
                }
            }
        });
        // 0xfc (virtual +0x5c): the answer screen's background.
        context.bind(0xfc, [this](CommandArguments& a) { answerBackground_ = a.string(); });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 02046154 (virtual +0x274): the memo pad's buttons.
        if (interactive) {
            if (memoButton_) {
                clearMemo_.update(scene.kit, scene.touch);
            }
            enterAnswer_.update(scene.kit, scene.touch);
        } else {
            clearMemo_.draw(scene.kit);
            enterAnswer_.draw(scene.kit);
        }
        scene.drawCommon(interactive);
    }

    void play(PuzzleScene& scene) override {
        // 02046880: the memo pad and the answer screen in turn.
        createCanvas();
        memoBackground_ = scene.engineBackground();
        for (bool first = true;; first = false) {
            if (first) {
                scene.intro();
            } else {
                scene.frame([&] { draw(scene, false); });
                fadeIn(scene.kit, Screens::Lower); // 0200fb90(1)
            }
            if (!memoPad(scene) || !answerScreen(scene)) {
                break;
            }
        }
        // 02045a6c (destructor): both screens to black.
        scene.fadeOut();
    }

private:
    // An answer slot (ARM9 02066d94..0206738c, 0xa8 bytes).
    struct Slot {
        Rect area;                     // +0..+0xc (its button's rectangle)
        int mode = 0;                  // +0x10
        int length = 0;                // +0x14: characters
        int cursor = 0;                // +0x18: where the next character goes
        std::array<int, 16> written;   // +0x1c: -1 = empty
        std::array<int, 16> expected;  // +0x5c
        SceneKit::SpriteObject digits; // +0xa0: inputnumbers.spr
        Button button;                 // +0xa4

        Slot() {
            written.fill(-1);
            expected.fill(-1);
        }

        // 02067290.
        void reset() {
            cursor = 0;
            written.fill(-1);
        }

        // 020672d0: the reading becomes the slot's character; the cursor
        // moves on unless the slot is full.
        void enter() {
            if (cursor >= length || cursor + 1 >= 16) {
                return;
            }
            written[std::size_t(++cursor)] = -1;
        }

        // 020672f8: the last character (searched from the 16th place) is
        // taken back and the cursor returns there.
        void erase() {
            for (int i = 15; i >= 0; --i) {
                if (written[std::size_t(i)] != -1) {
                    written[std::size_t(i)] = -1;
                    cursor = i;
                    return;
                }
            }
        }

        // 020672b0: the current reading in the cursor's place (the last
        // place once the slot is full).
        void show(int reading) {
            const int i = cursor >= length ? cursor - 1 : cursor;
            written[std::size_t(std::max(i, 0))] = reading;
        }

        bool empty() const { return written[0] < 0; } // 0206738c

        // 0206732c.
        bool correct() const {
            for (int i = 0; i < length; ++i) {
                if (written[std::size_t(i)] != expected[std::size_t(i)]) {
                    return false;
                }
            }
            return true;
        }
    };

    // The canvas (ARM9 02052140): box 0 is the memo pad (02046704: the
    // whole screen from (5, 5), ink only), box 1 the answer screen's
    // writing box (02045f30: 0x59 x 0x58 at (0x36, 0x3c)). The recogniser
    // settings follow the input mode (020522c4).
    static constexpr int kMemo = 0, kWriting = 1;

    void createCanvas() {
        WrittenAnswer::Setup setup;
        setup.rules = WrittenAnswer::Rules::Adventure1;
        WrittenAnswer::BoxSetup memo;
        memo.area = {5, 5, 0x100, 0xc0};
        memo.record = false;
        WrittenAnswer::BoxSetup writing;
        writing.area = {0x36, 0x3c, 0x59, 0x58};
        auto& r = writing.recognition;
        r.categories = mode_ == 0 ? 0x10 : mode_ == 1 ? 0xc : 0x1c;
        r.categories2 = -1;
        r.baseline = 0x50;
        r.helpline = 0x28;
        writing.reading = mode_ == 0   ? WrittenAnswer::Reading::Digit
                          : mode_ == 1 ? WrittenAnswer::Reading::Letter
                                       : WrittenAnswer::Reading::Unread;
        setup.boxes = {memo, writing};
        canvas_.emplace(setup, recognizer_);
    }

    // 02052634 on one box for a frame: the stroke, the ink it leaves (the
    // given colour, fontq's brush) and the rest counter (+0xe0, up to 15
    // while the stylus is up; restarted when a stroke has been read).
    void write(PuzzleScene& scene, int box, std::uint16_t colour) {
        const TouchState& t = scene.touch;
        if (canvas_->write(box, Touch{t.down, t.pressed, t.released, t.x, t.y})) {
            rest_ = 0;
        }
        if (!t.down && rest_ < 15) {
            ++rest_;
        }
        const Font& brush = scene.kit.assets.font("fontq.NFTR");
        for (const auto& c : canvas_->ink) {
            CircleInkCommand cmd;
            cmd.kind = c.line ? CircleInkCommand::Kind::Line : CircleInkCommand::Kind::Dot;
            cmd.x0 = c.x0;
            cmd.y0 = c.y0;
            cmd.x1 = c.x1;
            cmd.y1 = c.y1;
            CircleInk::stamp(scene.kit.textLayer(SceneKit::Lower), cmd, brush, colour);
        }
        canvas_->ink.clear();
    }

    // 02052268 and the reading's delay (+0x9d).
    void resetCanvas() {
        canvas_->clear();
        delay_ = 0;
    }

    // 02046704: true when the player moves on to the answer screen.
    bool memoPad(PuzzleScene& scene) {
        SceneKit& kit = scene.kit;
        for (;;) {
            scene.beginFrame();
            draw(scene, true);
            if (memoButton_) {
                write(scene, kMemo, scene.inkColour());
                kit.clearTextRect(clearMemo_.x, clearMemo_.y, clearMemo_.width, clearMemo_.height);
            }
            scene.endFrame();
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (memoButton_ && clearMemo_.clicked(kit, scene.touch)) {
                kit.clearText(SceneKit::Lower); // 0200d8bc
            }
            if (enterAnswer_.clicked(kit, scene.touch)) {
                break;
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                resetCanvas();
                return false;
            }
        }
        fadeOut(kit, Screens::Lower); // 0200ff00(1)
        return true;
    }

    // 0204629c: true when the player goes back to the memo pad.
    bool answerScreen(PuzzleScene& scene) {
        SceneKit& kit = scene.kit;
        TouchState& touch = scene.touch;
        scene.resetQuitButton();
        kit.clearText(SceneKit::Lower);
        std::string background = answerBackground_;
        if (background.ends_with(".bgx")) {
            background.resize(background.size() - 4);
        }
        scene.setEngineBackground(kit.localized(background));
        // One frame drawn before the input is read (a fresh 3D list: the
        // fade's frames only waited), then the fade.
        kit.clearSubmissions();
        drawAnswer(scene, true);
        scene.endFrame();
        fadeIn(kit, Screens::Lower); // 0200fb90(1)
        delay_ = 0;
        bool back = false;
        for (bool done = false; !done;) {
            scene.beginFrame();
            drawAnswer(scene, true);
            quitRect_.update(kit, touch);
            write(scene, kWriting, textColour(0xff));
            scene.endFrame();
            Slot& current = slots_[std::size_t(current_)];
            if (scene.hintClicked()) {
                resetCanvas();
                scene.hintScreen();
            }
            if (clear_.clicked()) {
                kit.clearText(SceneKit::Lower);
                resetCanvas();
                for (auto& slot : slots_) {
                    slot.reset();
                }
            }
            if (rest_ == 15 && canvas_->box(kWriting).distinct != 0) {
                current.enter();
                resetCanvas();
                kit.clearText(SceneKit::Lower);
            }
            if (erase_.clicked()) {
                current.erase();
                resetCanvas();
                kit.clearText(SceneKit::Lower);
            }
            for (int i = 0; i < count_; ++i) {
                if (slots_[std::size_t(i)].button.clicked(kit, touch)) {
                    current_ = i;
                    resetCanvas();
                    kit.clearText(SceneKit::Lower);
                    break;
                }
            }
            if (submit_.clicked(kit, touch)) {
                bool full = true;
                for (int i = 0; i < count_; ++i) {
                    full = full && !slots_[std::size_t(i)].empty();
                }
                if (full) {
                    kit.effect(0xb);
                    bool right = true;
                    for (int i = 0; i < count_; ++i) {
                        right = right && slots_[std::size_t(i)].correct();
                    }
                    if (right) {
                        scene.markSolved(); // 02042bcc
                    } else {
                        scene.markWrong(); // 02042c28
                    }
                    break;
                }
            }
            if (scene.quitClicked()) {
                resetCanvas();
                if (scene.quitDialog()) {
                    break;
                }
            }
            if (back_.clicked()) {
                fadeOut(kit, Screens::Lower); // 0200ff00(1)
                scene.resetQuitButton();
                kit.clearText(SceneKit::Lower);
                if (!memoBackground_.empty()) {
                    scene.setEngineBackground(memoBackground_);
                }
                scene.frame([&] { draw(scene, false); });
                back = true;
                done = true;
            }
        }
        resetCanvas();
        return back;
    }

    // 020461a4.
    void drawAnswer(PuzzleScene& scene, bool interactive) {
        SceneKit& kit = scene.kit;
        if (interactive) {
            clear_.update(kit, scene.touch);
            erase_.update(kit, scene.touch);
            back_.update(kit, scene.touch);
            submit_.update(kit, scene.touch);
        }
        for (int i = 0; i < count_; ++i) {
            drawSlot(kit, slots_[std::size_t(i)], i == current_);
        }
        if (interactive) {
            for (int i = 0; i < count_; ++i) {
                slots_[std::size_t(i)].button.update(kit, scene.touch);
            }
        }
        // 02066fa4: the cursor under the slot's last place (02066fa4 selects
        // animation 0, which 0200c6d0 ignores: the first one stays).
        Slot& current = slots_[std::size_t(current_)];
        cursor_.x = (current.length - 1) * 0xe + current.area.x + 4;
        cursor_.y = current.area.y + 0x15;
        kit.draw(cursor_);
        if (!canvas_->box(kWriting).strokes.empty()) {
            current.show(canvas_->box(kWriting).results[0]);
            drawReading(kit, current);
        }
        scene.drawCommon(interactive);
    }

    // 0206718c and 02067038: the slot's frame (a darker border, a lighter
    // inside; the selected slot a pixel larger), then its characters,
    // right-aligned: they start as many places from the left as places are
    // free before the first empty one.
    void drawSlot(SceneKit& kit, Slot& slot, bool selected) {
        const Rect& a = slot.area;
        if (selected) {
            kit.rectangle(a.x - 1, a.y - 1, a.width + 2, a.height + 2, rgb(0x78, 0x68, 0x50), 31);
            kit.rectangle(a.x + 1, a.y + 1, a.width - 2, a.height - 2, rgb(0xf8, 0xd8, 0xa8), 31);
        } else {
            kit.rectangle(a.x, a.y, a.width, a.height, rgb(0x78, 0x68, 0x50), 31);
            kit.rectangle(a.x + 2, a.y + 2, a.width - 4, a.height - 4, rgb(0xf8, 0xf0, 0xd8), 31);
        }
        int used = 0;
        while (used < slot.length && slot.written[std::size_t(used)] != -1) {
            ++used;
        }
        int x = (slot.length - used) * 0xe + 4 + a.x;
        if (slot.mode == 0) {
            kit.restart(slot.digits); // 0200c750
        }
        for (int i = 0; i < slot.length; ++i, x += 0xe) {
            const int c = slot.written[std::size_t(i)];
            if (c < 0) {
                continue;
            }
            if (slot.mode == 0) {
                // 02066ff8: inputnumbers animation value + 1.
                kit.select(slot.digits, unsigned(c + 1));
                slot.digits.x = x - 4;
                slot.digits.y = a.y;
                kit.draw(slot.digits);
            } else if (slot.mode == 1 || slot.mode == 4 || slot.mode == 5) {
                drawCharacter(kit, x, a.y + 4, c);
            }
        }
    }

    // 0200d8f4 + 0200d960: an 18-pixel cell of the text layer cleared and
    // the character drawn with font18 (the layer's second font).
    static void drawCharacter(SceneKit& kit, int x, int y, int c) {
        kit.clearTextRect(x, y, 0x12, 0x12);
        textGlyph(kit.textLayer(SceneKit::Lower), kit.assets.font("font18.NFTR"),
                  unsigned(c) & 0xff, x, y, textColour(0xff));
    }

    // 02045fa8: the reading in the slot's last place, after 16 frames of
    // writing; "?" (animation 0xb) after 60 when nothing was read.
    void drawReading(SceneKit& kit, Slot& slot) {
        const int x = (slot.length - 1) * 0xe + slot.area.x, y = slot.area.y;
        if (mode_ == 0) {
            kit.restart(reading_);
        } else if (mode_ == 1) {
            kit.clearTextRect(x + 4, y + 4, 0x12, 0x12);
        }
        const int reading = canvas_->box(kWriting).results[0];
        if (delay_ < (reading >= 0 ? 0x10 : 0x3c)) {
            ++delay_;
            return;
        }
        if (mode_ == 0) {
            kit.select(reading_, unsigned(reading >= 0 ? reading + 1 : 0xb));
            reading_.x = x;
            reading_.y = y;
            kit.draw(reading_);
        } else if (mode_ == 1) {
            drawCharacter(kit, x + 4, y + 4, reading >= 0 ? reading : '?');
        }
    }

    handwriting::Recognizer recognizer_;
    std::optional<WrittenAnswer> canvas_;
    SpriteButton erase_, clear_, back_;                                // +0x90 / +0x94 / +0x98
    Button clearMemo_, enterAnswer_;                                   // +0x74 / +0x78
    Button quitRect_, submit_;                                         // +0x7c / +0x80
    SceneKit::SpriteObject reading_, cursor_;                          // +0x88 / the current slot's
    bool memoButton_ = true;                                           // +0x9c
    std::array<Slot, 4> slots_;                                        // +0xa4
    int mode_ = 0, count_ = 0, current_ = 0;                           // +0xbc / +0xb4 / +0xb8
    int rest_ = 0;                                                     // canvas +0xe0
    unsigned delay_ = 0;                                               // +0x9d
    std::string answerBackground_ = "drawinput1.bgx", memoBackground_; // +0x100 / +0xc0
};

} // namespace

std::unique_ptr<Engine> createFirstAdventureEngine(const std::string& name, PuzzleScene& scene) {
    if (name == "Free Button") {
        return std::make_unique<FreeButton>();
    }
    if (name == "Draw Input2") {
        return std::make_unique<DrawInput>(scene);
    }
    if (name == "Trace Button") {
        return createTraceButton(scene);
    }
    if (name == "Cut Puzzle") {
        return createCutPuzzle(scene);
    }
    if (name == "Tile") {
        return createTile(scene);
    }
    if (name == "Slide Puzzle" || name == "Slide Puzzle 2") {
        return createSlideBlocks(scene);
    }
    if (name == "Queen") {
        return createQueen(scene);
    }
    if (name == "On Off") {
        return createOnOff(scene);
    }
    if (name == "Trace") {
        return createTrace(scene);
    }
    if (name == "Cup") {
        return createCup(scene);
    }
    if (name == "Match") {
        return createMatch(scene);
    }
    if (name == "Match Tutorial") {
        return createMatchTutorial(scene);
    }
    if (name == "Drag Puzzle") {
        return createDragPuzzle(scene);
    }
    if (name == "Scale") {
        return createScale(scene);
    }
    if (name == "Connect") {
        return createConnect(scene);
    }
    if (name == "River Cross" || name == "River Cross 2") {
        return createRiverCross(scene, name == "River Cross 2");
    }
    if (name == "Place Target") {
        return createPlaceTarget(scene);
    }
    if (name == "Coin") {
        return createCoin(scene);
    }
    return nullptr;
}

bool engineReconstructed(const std::string& name) {
    return name == "Free Button" || name == "Draw Input2" || name == "Trace Button" ||
           name == "Cut Puzzle" || name == "Tile" || name == "Slide Puzzle" ||
           name == "Slide Puzzle 2" || name == "Queen" || name == "On Off" || name == "Trace" ||
           name == "Cup" || name == "Match" || name == "Match Tutorial" || name == "Drag Puzzle" ||
           name == "Scale" || name == "Connect" || name == "River Cross" ||
           name == "River Cross 2" || name == "Place Target" || name == "Coin";
}

} // namespace picarat::adventure1
