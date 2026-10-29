// The first adventure's puzzle scene. See a1_puzzle.hpp.
#include "puzzles/a1_puzzle.hpp"
#include "puzzles/a1_engines.hpp"
#include "sound/a1_sound.hpp"
#include "system/a1_script.hpp"
#include <cmath>
#include <stdexcept>

namespace picarat::adventure1 {

namespace {
// Hint-progress bits of a puzzle's state byte (02040e40, 020421c8).
constexpr std::uint8_t kSolved = 2, kHint1 = 8, kHint2 = 0x10, kHint3 = 0x20;

// The question text's position on the upper screen (02053190: +0x18 = 9,
// or 4 when state +0x754 is 1, which it is on this cartridge: the
// European program draws single-byte text).
constexpr int kQuestionX = 4, kQuestionY = 0x17;
// Hint texts on the lower screen (02041ac4: x 0x16, or 4 with +0x754).
constexpr int kHintX = 4, kHintY = 0x15;
// The number of hint tabs (base object +0x23; 3 in every puzzle observed).
constexpr int kHints = 3;

// The bought bit of hint page 1..3.
std::uint8_t hintBit(int page) {
    return page == 2 ? kHint2 : page == 3 ? kHint3 : kHint1;
}
} // namespace

PuzzleScene::PuzzleScene(SceneKit& k, TouchState& t, const Catalogue& catalogue, unsigned id,
                         PuzzleState& state)
    : kit(k), touch(t), catalogue_(catalogue), state_(state), id_(id),
      hints_(k, t, catalogue, state, id) {
    const PuzzleInfo* info = catalogue.find(id);
    if (!info) {
        throw std::runtime_error("no such puzzle: " + std::to_string(id));
    }
    // 02053190 (question text object): the question and the header digits.
    question_ = catalogue.text(id, "q");
    numbers_ = kit.load("q_numbers.sbj", 0, 0, 1);
    numbers2_ = kit.load("q_numbers2.sbj", 0, 0, 1);
    // 020534cc: the upper background.
    kit.upperBackground = kit.localized("?/q_bg");
    // 0203fb94 (base constructor): the frame's buttons, idle sprites shown.
    hint_.setSprite(kit, 0xb8, 0, "?/hint_buttons.spr", "hintb1");
    hint_.visibleIdle = true;
    hint_.sound = 8;
    if (state_.hideQuit) {
        quit_.setRect(0x100, 0x100, 10, 10);
    } else {
        quit_.setSprite(kit, 0xb8, 0x18, "?/pass_button.spr", "pass");
        quit_.visibleIdle = true;
    }
    quit_.sound = 3;
    quitYes_.setSprite(kit, 0x30, 0x78, "?/later_buttons.spr", "yes");
    quitYes_.visibleIdle = true;
    quitYes_.sound = 3;
    quitNo_.setSprite(kit, 0x90, 0x78, "?/later_buttons.spr", "no");
    quitNo_.visibleIdle = true;
    quitNo_.sound = 4;
    laterMessage_ = kit.load("?/later_mes.spr", 0, 0x4f, 1);
    laterMessage_.x = (0x100 - kit.frameWidth(laterMessage_)) >> 1;
    laterMessage_.y = 0x4f;
    pagePrevious_.setRect(0, 0, 0x4c, 0x20);
    pageClose_.setRect(0x60, 0, 0x40, 0x10);
    pageNext_.setRect(0xc0, 0, 0x40, 0x10);
    // The "touch" prompt, centred on the lower screen (0200c680/0200c630).
    touchPrompt_ = kit.load("?/qend_touch.spr", 0, 0, 1);
    kit.select(touchPrompt_, "touch");
    touchPrompt_.x = (0x100 - kit.frameWidth(touchPrompt_)) >> 1;
    touchPrompt_.y = (0xc0 - kit.frameHeight(touchPrompt_)) >> 1;
}

PuzzleScene::~PuzzleScene() = default;

void PuzzleScene::beginFrame() {
    kit.poll();
    const HostInput& host = kit.hostInput();
    touch.update(host.down, host.x, host.y);
}

void PuzzleScene::endFrame() {
    kit.present();
}

void PuzzleScene::bindCommon(ScriptContext& context) {
    // Commands every engine receives through the base object.
    // 0x0b (02010864): a lower-screen background ("?/q23_bg.bgx", layer 3).
    context.bind(command::MainBackground, [this](CommandArguments& a) {
        std::string name = a.string();
        a.integer(); // the layer (3 in every puzzle)
        if (name.ends_with(".bgx")) {
            name.resize(name.size() - 4);
        }
        kit.lowerBackground = engineBackground_ = kit.localized(name);
    });
    // 0x1c (02011a48): virtual +0x28, empty in the base object.
    context.bind(0x1c, [](CommandArguments& a) { a.integer(); });
    // 0x1f (02011b30): state +0xc = the first value (byte; nonzero: the
    // puzzle has an answer picture), +0x13 = the second value != 0 (the
    // picture is in the language folder).
    context.bind(0x1f, [this](CommandArguments& a) {
        state_.answerPicture = std::uint8_t(a.integer());
        state_.answerLocalized = a.integer() != 0;
    });
    // 0xc0 (02015c2c): state +0xd: a picture after a wrong answer.
    context.bind(0xc0,
                 [this](CommandArguments& a) { state_.wrongPicture = std::uint8_t(a.integer()); });
    // 0x66 / 0x67 (0201369c / 020136d0): state +0x4f0 = 0 / 1, which of
    // the two judgment picture sets the result shows.
    context.bind(0x66, [this](CommandArguments&) { state_.secondJudge = false; });
    context.bind(0x67, [this](CommandArguments&) { state_.secondJudge = true; });
    // 0x42 (020128a4): palette entry 0xfd of the lower text layer from
    // 8-bit red, green, blue (0200d384, 5 bits each).
    context.bind(0x42, [this](CommandArguments& a) {
        const int r = a.integer(), g = a.integer(), b = a.integer();
        inkColour_ = std::uint16_t(0x8000 | (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
    });
    // 0xf6 (02016f24): state +0x750 = 1 (no engine tutorial pages).
    context.bind(0xf6, [this](CommandArguments&) { noTutorial_ = true; });
}

PuzzleScene::Outcome PuzzleScene::run() {
    const Bytes script =
        readFile(kit.assets.root / "script/qscript" / ("q" + std::to_string(id_) + "_param.gds"));
    // Setting the scene up (02000f54: the question object's sprites, the
    // text archive, the frame's buttons) holds the black screen 31 frames
    // on the cartridge before the engine adds its own loading (measured in
    // the reference emulator on puzzles 2, 23, 105 and 115, 30..32 frames
    // from the end of the title card to the engine's constructor; a
    // loading pause).
    kit.hold(31);
    ScriptInterpreter interpreter(script, scripts());
    ScriptContext scene;
    ScriptContext engineContext;
    bindCommon(scene);
    scene.bind(command::Engine, [&](CommandArguments& a) {
        // 02011610: create the engine and hand it the rest of the script.
        engineName_ = a.string();
        // 0203fb94 (the base of every engine's constructor): the puzzle's
        // effects (02004a50).
        if (sound) {
            sound->setupPuzzle(int(id_), engineKind(), [this] { soundWait(); });
        }
        engine_ = createEngine(engineName_);
        bindCommon(engineContext);
        engine_->bind(engineContext, *this);
        interpreter.runRest(engineContext);
    });
    interpreter.run(scene);
    if (!engine_) {
        throw std::runtime_error("puzzle script without an engine");
    }
    state_.puzzles[id_] |= 1; // 0203c85c: seen
    engine_->play(*this);
    return quitDone_ ? Outcome::Quit : state_.solved ? Outcome::Solved : Outcome::Wrong;
}

std::unique_ptr<Engine> PuzzleScene::createEngine(const std::string& name) {
    if (auto engine = createFirstAdventureEngine(name, *this)) {
        return engine;
    }
    throw std::runtime_error("unreconstructed puzzle engine: " + name);
}

void PuzzleScene::drawHeader() {
    // 02053a24: the number (three digits), the hint coins (no leading
    // zeros) and the picarat value (no leading zeros) with the upper digit
    // sprites; digit d is animation d + 1.
    const PuzzleInfo& info = *catalogue_.find(id_);
    auto digit = [&](SceneKit::SpriteObject& s, int value, int x) {
        kit.select(s, unsigned(value + 1));
        s.x = x;
        s.y = 6;
        kit.draw(s);
    };
    const int number = int(info.number);
    digit(numbers_, number % 1000 / 100, 0x19);
    digit(numbers_, number % 100 / 10, 0x20);
    digit(numbers_, number % 10, 0x27);
    const int coins = state_.coins;
    if (coins > 99) {
        digit(numbers2_, coins % 1000 / 100, 0xe3);
    }
    if (coins > 9) {
        digit(numbers2_, coins % 100 / 10, 0xea);
    }
    digit(numbers2_, coins % 10, 0xf1);
    // The picarat value for the wrong answers so far (title card 0203d1c0).
    const unsigned failures = (state_.puzzles[id_] >> 6 & 1) + 2 * (state_.puzzles[id_] >> 7 & 1);
    const int value = int(info.picarats[std::min(failures, 2u)]);
    if (value > 99) {
        digit(numbers2_, value % 1000 / 100, 0x4a);
    }
    if (value > 9) {
        digit(numbers2_, value % 100 / 10, 0x51);
    }
    digit(numbers2_, value % 10, 0x58);
}

void PuzzleScene::drawCommon(bool) {
    // 02040e40.
    drawHeader();
    const std::uint8_t progress = state_.puzzles[id_];
    if (!hintOpen_ && !quitOpen_) {
        const unsigned level = progress & kHint3   ? 4
                               : progress & kHint2 ? 3
                               : progress & kHint1 ? 2
                                                   : 1;
        kit.select(hint_.sprite, level);
        hint_.update(kit, touch);
        quit_.update(kit, touch);
    }
    if (hintOpen_) {
        hints_.drawControls();
    }
    if (quitOpen_) {
        quitYes_.update(kit, touch);
        quitNo_.update(kit, touch);
        kit.draw(laterMessage_);
    }
}

int PuzzleScene::engineKind() const {
    // 02011610: the engine's index in the name table at 020d2938 (24
    // pointers); "Draw Input2" (index 23) counts as kind 3, Draw Input.
    constexpr std::uint32_t base = 0x02000000, table = 0x020d2938;
    const Bytes arm9 = readFile(kit.assets.root / "code/arm9.bin");
    for (int i = 0; i < 24; ++i) {
        std::string name;
        for (std::uint32_t c = read32(arm9, table - base + 4 * std::uint32_t(i)); arm9.at(c - base);
             ++c) {
            name += char(arm9.at(c - base));
        }
        if (name == engineName_) {
            return i == 23 ? 3 : i;
        }
    }
    return -1;
}

void PuzzleScene::intro() {
    // 0204132c. The engine kind's tutorial pages follow the question the
    // first time a kind is played (seen flags state +0x468 + kind), unless
    // the script says otherwise (0xf6). Their count is the number of
    // pictures ?/puzzlet<kind>_<page> (up to 10).
    const int kind = engineKind();
    int pages = 0;
    if (kind >= 0) {
        while (pages < 10 &&
               fileExists(kit.assets.root / "bg" /
                                       (kit.localized("?/puzzlet" + std::to_string(kind) + "_" +
                                                      std::to_string(pages)) +
                                        ".l2bg"))) {
            ++pages;
        }
    }
    const bool tutorial = !noTutorial_ && pages > 0 && !state_.kindTutorialSeen[std::size_t(kind)];
    // While the question is typed before such pages, the lower screen shows
    // the puzzle's own question picture q<N>q_bg if it has one, instead of
    // the engine.
    bool questionPicture = false;
    if (tutorial) {
        const std::string name = "q" + std::to_string(id_) + "q_bg";
        if (fileExists(kit.assets.root / "bg" / (name + ".l2bg"))) {
            questionPicture = true;
            kit.lowerBackground = name;
        }
    }
    auto drawLower = [&] {
        if (!questionPicture) {
            engine_->draw(*this, false);
        }
    };
    // Before its first frame the intro holds the black screen two more
    // frames, five when tutorial pages follow (measured on puzzles 23 and
    // 2, 105, 115; looking the pictures up, hypothesis).
    kit.hold(tutorial ? 5 : 2);
    const std::size_t length = question_.size();
    kit.clearText(SceneKit::Upper);
    frame(drawLower);
    // 0200f7b4(4, 1): both screens from black in steps of 4, one frame
    // each; the last drawn frame stays on screen meanwhile (measured: the
    // first frame at -12, the fourth at 0, then the typing starts).
    fadeIn(kit, Screens::Both, 4);
    std::size_t typed = 0;
    int typingSounds = 0;
    float angle = 0;
    int shown = 0;
    for (;;) {
        beginFrame();
        drawLower();
        drawHeader();
        // 02053640: two more characters each frame. 0200df2c may wait for
        // the typing sound (02005414) to start; frames pass meanwhile.
        if (typed < length) {
            typingTick(sound, question_, typed, typingSounds, [&] { kit.hold(1); });
            drawTextBox(kit, SceneKit::Upper, kQuestionX, kQuestionY, question_, typed, typed + 2);
            typed += 2;
        }
        // The prompt pulses from the 16th frame: alpha (of 31)
        // min(20, int(15 sin(angle)) + 20), angle += 0.05 per frame.
        const int alpha = std::min(20, int(15.0f * float(std::sin(double(angle)))) + 20);
        angle += 0.05f;
        if (shown + 1 <= 30 || alpha <= 1) {
            shown += 2;
        } else {
            kit.draw(touchPrompt_, alpha);
            shown += 1;
        }
        endFrame();
        if (touch.released) {
            break;
        }
    }
    kit.effect(tutorial ? 3 : 0xd);
    // 020534cc: the whole question at once.
    kit.clearText(SceneKit::Upper);
    drawTextBox(kit, SceneKit::Upper, kQuestionX, kQuestionY, question_);
    if (questionPicture) {
        adventure1::fadeOut(kit, Screens::Lower, 4); // 0200ff00(4)
        kit.lowerBackground.clear();
    }
    if (tutorial) {
        tutorialPages(kind, pages);
        // 02009d60 puts the engine's picture back; the black screen holds
        // a frame meanwhile (measured, puzzle 2).
        kit.hold(1);
        kit.lowerBackground = engineBackground_;
        frame([&] { engine_->draw(*this, false); });
        fadeIn(kit, Screens::Lower, 4); // 0200fb90(4)
    } else {
        frame([&] { engine_->draw(*this, false); });
        fadeIn(kit, Screens::Both, 4);
    }
    // Ten frames that only read the touch screen (0200ebc4 + 02003d50).
    for (int i = 0; i < 10; ++i) {
        const HostInput& host = kit.hostInput();
        touch.update(host.down, host.x, host.y);
        kit.hold(1); // 02003d50 only: no swap, no frame count
    }
}

void PuzzleScene::tutorialPages(int kind, int pages) {
    // 0204132c (tail): the pictures carry their own buttons; the frame's
    // rectangles are "back" (0,0)-(0x4c,0x20), "to the puzzle" (0x60,0,
    // 0x40 wide) and "next" (0xc0,0). A touch elsewhere also moves on.
    state_.kindTutorialSeen[std::size_t(kind)] = true;
    int page = 0;
    for (bool done = false, first = true; !done; first = false) {
        if (!first) {
            // Loading another page's picture holds the black screen a frame
            // (measured, puzzle 2).
            kit.hold(1);
        }
        kit.lowerBackground =
            kit.localized("?/puzzlet" + std::to_string(kind) + "_" + std::to_string(page));
        frame([&] { drawHeader(); });
        if (page == 0) {
            fadeIn(kit, Screens::Both, 1); // 0200f7b4(1, 1)
        } else {
            fadeIn(kit, Screens::Lower, 1); // 0200fb90
        }
        const int last = pages - 1;
        for (;;) {
            frame([&] {
                drawHeader();
                pagePrevious_.update(kit, touch);
                pageClose_.update(kit, touch);
                pageNext_.update(kit, touch);
            });
            if (pagePrevious_.clicked(kit, touch) && page > 0) {
                kit.effect(0x14);
                --page;
                break;
            }
            if (pageClose_.clicked(kit, touch) ||
                (touch.released && (pages == 1 || page == last))) {
                kit.effect(0xd);
                done = true;
                break;
            }
            if ((pageNext_.clicked(kit, touch) || touch.released) && page < last) {
                kit.effect(0x14);
                ++page;
                break;
            }
        }
        adventure1::fadeOut(kit, Screens::Lower, 2); // 0200ff00(2)
    }
    kit.lowerBackground.clear();
}

void PuzzleScene::hintScreen() {
    stopLoopedEffect(); // 020421d4
    // 020421c8.
    hintOpen_ = true;
    hints_.open();
    for (;;) {
        beginFrame();
        drawCommon(false);
        hints_.drawBack();
        endFrame();
        if (!hints_.step()) {
            break;
        }
    }
    kit.clearText(SceneKit::Lower);
    kit.lowerBackground = engineBackground_;
    hintOpen_ = false;
    // One frame with only the frame's own parts (02040e40), no engine.
    frame([&] { drawCommon(false); });
    hints_.close();
}

HintView::HintView(SceneKit& k, TouchState& t, const Catalogue& catalogue, PuzzleState& state,
                   unsigned id)
    : kit(k), touch(t), catalogue_(catalogue), state_(state), id_(id) {
    // The hint screen's controls (0203fb94): tabs from ?/buttons.spr, shown
    // locked ("hintNl") while the hint is not bought and the puzzle not solved.
    const std::uint8_t progress = state_.puzzles[id_];
    for (int i = 0; i < kHints; ++i) {
        const std::string name = "hint" + std::to_string(i + 1);
        auto& tab = tabs_[i];
        tab.setSprite(kit, 0x38 * i, 0, "?/buttons.spr", name);
        if (!(progress & hintBit(i + 1)) && !(progress & kSolved)) {
            kit.select(tab.sprite, name + "l");
        }
        tab.visibleIdle = true;
        tab.sound = 0x11;
    }
    yes_.setSprite(kit, 0x38, 0x88, "?/yesnobuttons.spr", "yes");
    yes_.setPressedAnimation(kit, "yesp");
    yes_.visibleIdle = true;
    yes_.sound = 0x12;
    no_.setSprite(kit, 0x88, 0x88, "?/yesnobuttons.spr", "no");
    no_.setPressedAnimation(kit, "nop");
    no_.visibleIdle = true;
    no_.sound = 4;
    back_.setSprite(kit, 200, 0, "?/buttons.spr", "modoru");
    back_.visibleIdle = true;
    back_.sound = 9;
    digits_ = kit.load("q_numbers.spr", 0, 0, 1);
}

void HintView::open() {
    // The tab opened first: the first hint not bought yet (the first once
    // solved; with all three bought the last tab stays, as the original).
    const std::uint8_t progress = state_.puzzles[id_];
    if (!(progress & kHint1)) {
        tab_ = 0;
    } else if (!(progress & kHint2)) {
        tab_ = 1;
    } else if (!(progress & kHint3)) {
        tab_ = 2;
    }
    if (progress & kSolved) {
        tab_ = 0;
    }
    show(tab_ + 1);
}

void HintView::drawControls() {
    // Tab 2 once hint 1 is bought, tab 3 once hint 2 is (or solved);
    // yes / no while the open hint is locked and coins are left.
    const std::uint8_t progress = state_.puzzles[id_];
    const bool solved = progress & kSolved;
    tabs_[0].update(kit, touch);
    if (progress & kHint1 || solved) {
        tabs_[1].update(kit, touch);
    }
    if (progress & kHint2 || solved) {
        tabs_[2].update(kit, touch);
    }
    if (state_.coins != 0 && !(progress & hintBit(tab_ + 1)) && !solved) {
        yes_.update(kit, touch);
        no_.update(kit, touch);
    }
}

void HintView::drawBack() {
    back_.update(kit, touch);
    const std::uint8_t now = state_.puzzles[id_];
    const bool locked = !(now & hintBit(tab_ + 1)) && !(now & kSolved);
    if (state_.coins != 0 && locked) {
        // 020419a4: the coins at (0xac, 0x6b), 9 pixels a digit.
        const int coins = state_.coins;
        auto digit = [&](int value, int x) {
            kit.select(digits_, unsigned(value + 1));
            digits_.x = x;
            digits_.y = 0x6b;
            kit.draw(digits_);
        };
        if (coins > 99) {
            digit(coins % 1000 / 100, 0xac);
        }
        if (coins > 9) {
            digit(coins % 100 / 10, 0xac + 9);
        }
        digit(coins % 10, 0xac + 0x12);
    }
}

bool HintView::step() {
    bool switched = false;
    for (int i = 0; i < kHints; ++i) {
        if (tab_ != i && tabs_[i].clicked(kit, touch)) {
            switched = true;
            tab_ = i;
            show(i + 1);
        }
    }
    if (no_.clicked(kit, touch) || back_.clicked(kit, touch)) {
        return false;
    }
    const int page = tab_ + 1;
    const std::uint8_t bit = hintBit(page);
    if (!switched && state_.coins != 0 && !(state_.puzzles[id_] & kSolved) &&
        !(state_.puzzles[id_] & bit) && yes_.clicked(kit, touch)) {
        // Buy it: one coin, the bit, the tab unlocked.
        --state_.coins;
        yes_.cancel();
        state_.puzzles[id_] |= bit;
        kit.select(tabs_[tab_].sprite, "hint" + std::to_string(page));
        show(page);
    }
    return true;
}

void HintView::close() {
    if (tab_ >= kHints) {
        tab_ = 0;
    }
}

void HintView::show(int page) {
    // 02041ac4: a bought hint (or any hint once solved) shows hint_<page>
    // with its text h_<N>_<page>; a locked one asks to unlock it
    // (?/hint_<page>_2, or ?/hint_<page>_3 without coins), without text.
    const std::uint8_t progress = state_.puzzles[id_];
    const std::string number = std::to_string(page);
    std::string text;
    // Loading holds the previous frame: one frame for a picture, four when
    // the hint text is read too (the whole text archive, 0200381c;
    // measured on puzzle 23).
    const bool open = progress & hintBit(page) || progress & kSolved;
    for (int i = 0; i < (open ? 4 : 1); ++i) {
        kit.hold(1);
    }
    if (open) {
        kit.lowerBackground = "hint_" + number;
        text = catalogue_.text(id_, "h_" + number);
    } else {
        kit.lowerBackground = kit.localized("?/hint_" + number + (state_.coins == 0 ? "_3" : "_2"));
    }
    kit.clearText(SceneKit::Lower);
    drawTextBox(kit, SceneKit::Lower, kHintX, kHintY, text);
}

bool PuzzleScene::quitDialog() {
    stopLoopedEffect(); // 02042a58
    // 02042a4c / 020427b0: later_bg with the puzzle's title centred at row
    // 0x36, the message and yes / no. Loading the picture and the title
    // (from the text archive) holds the previous frame for four frames
    // (measured).
    for (int i = 0; i < 4; ++i) {
        kit.hold(1);
    }
    kit.lowerBackground = "later_bg";
    kit.clearText(SceneKit::Lower);
    std::string title = catalogue_.text(id_, "t");
    while (!title.empty() && (title.back() == '\n' || title.back() == '\r')) {
        title.pop_back();
    }
    drawCentredLine(kit, SceneKit::Lower, 0x36, title.empty() ? "NO TITLE" : title);
    quitOpen_ = true;
    for (;;) {
        frame([&] { drawCommon(false); });
        if (quitYes_.clicked(kit, touch)) {
            // State +8: no result screen.
            quitDone_ = true;
            adventure1::fadeOut(kit, Screens::Both); // 0200fce8(1, 1)
            quitOpen_ = false;
            kit.clearText(SceneKit::Lower);
            kit.clearText(SceneKit::Upper);
            kit.lowerBackground.clear();
            kit.upperBackground.clear();
            return true;
        }
        if (quitNo_.clicked(kit, touch)) {
            break;
        }
    }
    kit.clearText(SceneKit::Lower);
    kit.lowerBackground = engineBackground_;
    quitOpen_ = false;
    frame([&] { drawCommon(false); }); // 02040e40 only, as after the hints
    return false;
}

void PuzzleScene::resetQuitButton() {
    if (!state_.hideQuit) {
        quit_.setSprite(kit, 0xb8, 0x18, "?/pass_button.spr", "pass");
    }
}

void PuzzleScene::setEngineBackground(const std::string& name) {
    kit.lowerBackground = engineBackground_ = name;
}

void PuzzleScene::fadeOut() {
    adventure1::fadeOut(kit, Screens::Both);
}

void PuzzleScene::markSolved() {
    // 02042bcc: state +6 = 1 and the solved bit.
    state_.solved = true;
    state_.puzzles[id_] |= kSolved;
}

void PuzzleScene::markWrong() {
    state_.solved = false; // 02042c28
}

void PuzzleScene::startLoopedEffect(int id) {
    if (sound) {
        sound->startLoopedEffect(id);
    }
}

void PuzzleScene::stopLoopedEffect() {
    if (sound) {
        sound->stopLoopedEffect();
    }
}

void typingTick(Adventure1Sound* sound, std::string_view text, std::size_t typed, int& ticks,
                const std::function<void()>& hold) {
    const bool tick = typingWaits(text, typed, typed + 2);
    if (!sound) {
        // Measured without sound: the first two ticks of a text do not hold
        // the frame, every later one holds it for one frame.
        if (tick && ++ticks > 2) {
            hold();
        }
        return;
    }
    sound->retryTyping();
    if (tick) {
        sound->typingSound(hold);
    }
    sound->retryTyping();
}

} // namespace picarat::adventure1
