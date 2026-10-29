// The puzzle scene (ARM9 overlay 27 and the shared code in overlay 2).
//
// One PuzzleGame plays one puzzle from start to finish:
//
//   1. The title card: the puzzle's number and title slide in on the upper
//      screen, the lower screen shows what it is worth in picarats (counting
//      down if the player has failed it before). See introCard().
//   2. The question: its text is revealed a few characters per frame on the
//      upper screen while the puzzle's picture fades in below; a tutorial
//      page may be shown first.
//   3. Play: every frame the puzzle engine (one of the *_puzzle classes, see
//      the constructor) gets the touch input, then the common buttons are
//      updated: Submit, Reset, Hints, Memo (a notepad over the puzzle) and
//      Quit ("later"). Some engines judge themselves instead of using Submit.
//   4. Judgment: a short animation reveals "correct" or "wrong"
//      (judgment.cpp).
//   5. Correct: the picarats are counted up, the answer picture and
//      explanation are shown, and the puzzle is marked solved. Wrong: the
//      explanation of the failure is shown, the failure is counted, and the
//      puzzle restarts from the title card.
//
// Comments name the original routines (overlay 27 unless stated otherwise).
// Timings that mention ROM frame numbers were measured in recorded runs of
// the original game.
#include "puzzles/a2_puzzle_game.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace picarat {

// Builds the puzzle: reads its data record and parameter script and creates
// the engine for its type, plus the common buttons.
//
//   type  engine                         type  engine
//   2     ChoicePuzzle (multiple choice) 23    TogglePuzzle (grid painting)
//   3     SelectionPuzzle                24    RosePuzzle
//   5     circle the answer (CircleInput) 25   SlidePuzzle (sliding blocks)
//   6,34  TracePuzzle                    26    TilePuzzle (tile placement)
//   9,15  LinePuzzle                     27    SkatePuzzle (ice skating)
//   10    ColorPuzzle (cycling pictures) 29    PegPuzzle (peg solitaire)
//   11    SlotPuzzle (tiles on slots)    30    CouplePuzzle (moving pairs)
//   13    TowerPuzzle (pancakes)         31    LampPuzzle (street lamps)
//   17    KnightPuzzle (knight's tour)   33    BridgePuzzle (stepping stones)
//   18    TilePuzzle (rotating tiles)
//   16, 20, 21, 22, 28, 32, 35: AnswerPuzzle (written answers)
PuzzleGame::PuzzleGame(GraphicsAssets& assets, PresentFrame present, NameEditor name,
                       AudioService* audio, PuzzleSession* session, unsigned internalId,
                       PuzzleLaunch launch)
    : assets_(assets), present_(std::move(present)), name_(std::move(name)), audio_(audio),
      text_(assets.root), internalId_(internalId), launch_(launch),
      data_(readFile(assets.root / ("puzzle/n" + std::to_string(internalId) + ".dat")), text_),
      session_(session ? *session : ownedSession_) {
    // Refuse any type or variant that has not been reconstructed.
    const bool answerType = data_.type == 16 || data_.type == 20 || data_.type == 21 ||
                            data_.type == 22 || data_.type == 28 || data_.type == 32 ||
                            data_.type == 35;
    // v0.26 engines: ov37 lines (9, 15), ov47 sliding blocks (25), ov45 toggles (23),
    // ov46 roses (24), ov52 lamps (31), ov49 skating (27),
    // ov51 pairs (30), ov53 stepping stones (33), ov34/ov54 traces (6, 34),
    // ov39 tiles on slots (11).
    const bool laterType = data_.type == 9 || data_.type == 15 || data_.type == 25 ||
                           data_.type == 23 || data_.type == 24 || data_.type == 31 ||
                           data_.type == 27 || data_.type == 30 || data_.type == 33 ||
                           data_.type == 6 || data_.type == 34 || data_.type == 11;
    if ((data_.type != 26 && data_.type != 5 && data_.type != 18 && data_.type != 13 &&
         data_.type != 3 && data_.type != 2 && data_.type != 10 && data_.type != 17 &&
         data_.type != 29 && !answerType && !laterType) ||
        ((data_.flags & 0x40) && data_.type != 3 && data_.type != 2 && data_.type != 26 &&
         !answerType && !laterType)) {
        throw std::runtime_error("unrecovered puzzle scene variant");
    }
    auto params = readFile(assets.root / ("puzzle/q" + std::to_string(internalId) + "_param.gds"));
    if (answerType) {
        // Written answers (overlay 42, 020f78a0) need the handwriting
        // recogniser and its database (loaded by overlay 2, 02077c50), plus
        // their own buttons: answer, submit, clear all, back, and one eraser
        // per box.
        recognizer_.emplace(readFile(assets.root / "decuma/databas_le.bin"));
        answers_.emplace(params, data_.type, internalId, *recognizer_);
        kaito_ = control("nazo/drawinput/ge/di_kaito.arc", "off", "on", "pos", 111);
        hantei_ = control("nazo/drawinput/ge/di_hantei.arc", "off", "on", "pos", -1);
        allreset_ = control("nazo/drawinput/ge/di_allreset.arc", "off", "on", "pos", 110);
        modoru_ = control("nazo/drawinput/ge/di_modoru.arc", "off", "on", "pos", 4);
        for (auto& e : erasers_) {
            e = control("nazo/drawinput/ge/di_erase2.arc", "off", "on", "pos4", 110);
        }
        if (internalId == 58) {
            // 0208003c(4,0x96,"q58_gfx.spr"): the sprite has no "pos" variable.
            const auto& sprite = assets_.sprite("nazo/drawinput/q58_gfx.arc");
            const auto& image =
                sprite.images.at(sprite.animations.at(sprite.animationNames.at("off")).at(0).image);
            pictureToggle_ = {&sprite, Button({4, 0x96, int(image.width), int(image.height)}, 3),
                              4,       0x96,
                              "off",   "on"};
        }
    } else if (data_.type == 25) {
        slides_.emplace(params, internalId);
    } else if (data_.type == 23) {
        toggles_.emplace(params);
    } else if (data_.type == 24) {
        roses_.emplace(params);
    } else if (data_.type == 31) {
        lamps_.emplace(params);
    } else if (data_.type == 30) {
        couples_.emplace(params);
    } else if (data_.type == 33) {
        bridge_.emplace(); // 0x14 records reach an empty virtual.
    } else if (data_.type == 6 || data_.type == 34) {
        traces_.emplace(params, data_.type);
    } else if (data_.type == 11) {
        slots_.emplace(params);
    } else if (data_.type == 27) {
        skate_.emplace(params);
        // Overlay 49 (020f78a0) creates the sprite with animation 1 (the idle
        // protagonist).
        const auto& sprite = assets_.sprite("nazo/iceskate/iceskate_gfx.arc");
        skater_.select(1, tick_, sprite.animations);
    } else if (data_.type == 9 || data_.type == 15) {
        lineScript_ = params;
        createLines();
    } else if (data_.type == 26 || data_.type == 18) {
        tiles_.emplace(params, internalId);
        tileAnimations_.resize(tiles_->pieces().size());
    } else if (data_.type == 29) {
        pegs_.emplace(params);
    } else if (data_.type == 17) {
        knight_.emplace(params);
    } else if (data_.type == 10) {
        colors_.emplace(params, assets_, internalId);
    } else if (data_.type == 2) {
        choices_.emplace(params, assets_);
    } else if (data_.type == 3) {
        selection_.emplace(params, assets_, internalId);
    } else if (data_.type == 13) {
        towers_.emplace(params, &session_.randomState);
    } else {
        // Type 5, circle the answer. Only the puzzles whose data has been
        // reviewed against the original are accepted.
        const std::array<unsigned, 20> reviewed{3,  6,  26,  40,  45,  47,  48,  52,  64,  75,
                                                77, 89, 116, 129, 130, 135, 166, 178, 186, 190};
        if (std::find(reviewed.begin(), reviewed.end(), internalId) == reviewed.end()) {
            throw std::runtime_error("unvalidated circle puzzle");
        }
        circleParameters_.emplace(params);
        // Puzzles with several pictures get left/right arrows to page through them.
        if (circleParameters_->groupCount() > 1) {
            auto arrow = [&](const char* name, int x) {
                const auto& sprite =
                    assets_.sprite(std::string("nazo/tracebutton/") + name + ".arc");
                const auto& image = sprite.images.at(
                    sprite.animations.at(sprite.animationNames.at("off")).at(0).image);
                return Control{&sprite, Button({x, 0, int(image.width), int(image.height)}),
                               x,       0,
                               "off",   "on"};
            };
            circleLeft_ = arrow("arrow_left", 2);
            circleRight_ = arrow("arrow_right", 158);
        }
    }
    // The common buttons (sprite, idle/pressed animation, position variable,
    // click sound).
    answer_ = control("system/btn/ge/hantei.arc");
    reset_ = control("system/btn/ge/reset.arc", "off", "on", "pos", 110);
    // ov27 020f4884: numbers >= 154 and the number-0 gift (206) use the single hint.
    hint_ = data_.number >= 154 || data_.number == 0
                ? control("system/btn/ge/hint_wifi.arc", "off", "on", "pos", 108)
                : control("system/btn/ge/hint.arc", "0_off", "0_on", "pos", 108);
    memo_ = control("system/btn/ge/memo.arc", "off", "on", "pos", 3);
    later_ = control("system/btn/ge/atode.arc", "off", "on", "pos", 3);
    back_ = control("system/btn/ge/modoru_memo.arc", "off", "on", "pos", 4);
    erase_ = control("system/btn/ge/erase.arc", "off", "on", "pos", 110);
    retry_ = control("nazo/system/ge/retry.arc", "off", "on", "pos", 3);
    hintBack_ = control("system/btn/ge/modoru_memo.arc", "off", "on", "pos_h", 4);
    hintYes_ = control("system/btn/ge/yes.arc", "off", "on", "hint_p", 0x76);
    hintNo_ = control("system/btn/ge/no.arc", "off", "on", "hint_p", 4);
    giveYes_ = control("system/btn/ge/yes.arc", "off", "on", "atode_p", 3);
    giveNo_ = control("system/btn/ge/no.arc", "off", "on", "atode_p", 4);
    for (unsigned i = 0; i < 3; ++i) {
        hintTabs_[i] = control("nazo/system/ge/hint" + std::to_string(i + 1) + ".arc", "off", "on",
                               "pos", 0x75);
        hintLocks_[i] = control("nazo/system/ge/hintlock" + std::to_string(i + 1) + ".arc", "off",
                                "on", "pos", 0x75);
    }
}

// Creates the line engine. A retry rebuilds it, as the original rebuilds the
// whole puzzle object.
void PuzzleGame::createLines() {
    // ov37 020f9fdc builds the engine; retry rebuilds the puzzle object.
    lines_.emplace(lineScript_, data_.type, data_.number);
    const auto& sprite = assets_.sprite("nazo/cut/ge/nomorelines.arc");
    const auto& image = sprite.images.at(sprite.animations.at(1).at(0).image); // 02075d10(sprite,1)
    lines_->setNoMoreLinesSize(int(image.width), int(image.height));
}

// Draws the line engine's shapes the way the DS 3D hardware would.
void PuzzleGame::renderLines(Surface& image) {
    // ov2 3D service: 02070e8c rectangles (depth rising with the primitive
    // counter, so later rectangles cover earlier ones) and 02070fcc lines
    // (2x2 quads at one depth). The hardware renders opaque polygons sorted
    // by bottom then top Y (stable), with a strict less-than depth test, so
    // among equal-depth quads the first in that order keeps the pixel.
    struct Item {
        const LinePuzzle::Primitive* p;
        int depth;
    };

    std::vector<Item> items;
    int counter = 1;
    for (const auto& p : lines_->primitives()) {
        items.push_back({&p, p.line ? 1 : ++counter});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.p->y1 != b.p->y1 ? a.p->y1 < b.p->y1 : a.p->y0 < b.p->y0;
    });
    std::vector<int> depth(256 * 192, 0);
    for (const auto& [pp, d] : items) {
        const auto& p = *pp;
        for (int y = std::max(0, p.y0); y < std::min(192, p.y1); ++y) {
            for (int x = std::max(0, p.x0); x < std::min(256, p.x1); ++x) {
                auto& z = depth[std::size_t(y * 256 + x)];
                if (d <= z) {
                    continue;
                }
                z = d;
                image.pixels[std::size_t((y + 192) * 256 + x)] = std::uint16_t(p.color | 0x8000);
            }
        }
    }
    // Markers use the 02075e50 textured-polygon path with counter depth, so
    // they cover the line quads; later markers are nearer. No captured frame
    // overlaps two markers' opaque pixels (line-check-81-overlap).
    const auto& sprites = lines_->sprites();
    for (auto i = sprites.begin(); i != sprites.end(); ++i) {
        const char* path = i->sprite == LinePuzzle::Sprite::Point  ? "nazo/cut/line_point.arc"
                           : i->sprite == LinePuzzle::Sprite::Here ? "nazo/cut/line_here.arc"
                                                                   : "nazo/cut/ge/nomorelines.arc";
        const auto& sprite = assets_.sprite(path);
        blit(image, sprite.images.at(sprite.animations.at(1).at(0).image), i->x,
             192 + i->y); // 02075d10(sprite,1)
    }
}

// The image of a tile in the tiles-on-slots puzzle.
const Surface& PuzzleGame::slotFrame(const SlotPuzzle::Tile& tile) {
    auto path = tile.sprite;
    if (path.ends_with(".spr")) {
        path.replace(path.size() - 4, 4, ".arc");
    }
    const auto& sprite = assets_.sprite(path);
    return sprite.images.at(sprite.animations.at(std::size_t(tile.animation)).at(0).image);
}

// A button made from a sprite: placed at the sprite's position variable,
// sized like its idle image.
PuzzleGame::Control PuzzleGame::control(const std::string& path, const std::string& idle,
                                        const std::string& pressed, const std::string& position,
                                        int sound) {
    const auto& sprite = assets_.sprite(path);
    const auto& pos = sprite.variables.at(position);
    const auto& image =
        sprite.images.at(sprite.animations.at(sprite.animationNames.at(idle)).at(0).image);
    // ov2 0208003c reads "pos"; 02080204 gives ordinary sprite-button bounds.
    return {&sprite, Button({pos[0], pos[1], int(image.width), int(image.height)}, sound),
            pos[0],  pos[1],
            idle,    pressed};
}

// The state reported to the host with every frame (for saving, testing and
// the window title).
GameSnapshot PuzzleGame::snapshot() const {
    GameSnapshot s{scene_, name_, tick_};
    s.detail = int(internalId_);
    s.puzzleProgress = session_.first.packed;
    s.coins = session_.coins;
    s.picarats = session_.picarats;
    if (tiles_) {
        for (unsigned i = 0; i < tiles_->pieces().size() && i < s.pieceSlots.size(); ++i) {
            s.pieceSlots[i] = tiles_->pieces()[i].slot;
        }
    }
    return s;
}

// One frame: advance the sound engine, play the sounds the engines asked
// for, draw and present the screens, then read the input for the next frame.
void PuzzleGame::frame() {
    if (auto s = music()) {
        s->frame(); // ov2 0206cb10 -> 0207faf4 once per main-loop frame
    }
    if (soundsLive_) {
        drainSounds(); // engine calls made while rendering/animating, in order
    }
    auto input = present_(render(), snapshot());
    if (colors_) {
        colors_->animate(tick_);
    }
    // The puzzle scene (and its engine) only exists after the card (020efda0);
    // in the intro the pancakes start once the fade-in has finished (first ROM
    // bounce ~18 frames later, seq-audio-tower-*-initial-a).
    if (towers_ && scene_ != SceneKind::Puzzle && scene_ != SceneKind::PuzzleCard &&
        !(scene_ == SceneKind::PuzzleIntro && screenBrightness_ < 0)) {
        towers_->animate();
    }
    if (input.restart) {
        throw RestartGame{};
    }
    if (input.puzzle) {
        throw JumpToPuzzle{};
    }
    microphoneRange_ = input.blow ? 255 : 0;
    // ov2 0206e2c0 debounces presses and filters held-sample jumps.
    touch_ = touchFilter_.update(input.down, input.x, input.y);
    wasDown_ = input.down;
    ++tick_;
}

// Shows frames until the stylus is lifted.
void PuzzleGame::waitRelease() {
    do {
        frame();
    } while (!touch_.released);
}

void PuzzleGame::drawSprite(Surface& target, const Sprite& sprite, const std::string& animation,
                            int x, int y) {
    const auto& step = sprite.animations.at(sprite.animationNames.at(animation)).at(0);
    blit(target, sprite.images.at(step.image), x, y);
}

// Whether a common button is shown and usable in this puzzle (the engines'
// virtual functions +214..+220, and 020f3500 / 020f3be0). For example,
// engines that judge themselves have no Submit button.
bool PuzzleGame::controlAvailable(const Control* c) const {
    if (c == &hint_) {
        return launch_.hintAvailable;
    }
    if (choices_ && (c == &reset_ || c == &answer_)) {
        return false; // ov30 virtual +21c/+220.
    }
    // ov42 020f8788 never draws the common answer/reset buttons; the answer
    // screen shows only the hint button among the common controls.
    if (answers_ && (c == &reset_ || c == &answer_)) {
        return false;
    }
    if (answers_ && answers_->answerScreen && (c == &memo_ || c == &later_)) {
        return false;
    }
    if (selection_ && c == &reset_) {
        return false; // ov31 virtual +21c; reset method is a no-op.
    }
    if (tiles_ && tiles_->specialMode() == 2) {
        return false;
    }
    return !(c == &answer_ && (towers_ || knight_ || pegs_ || slides_ || roses_ || skate_ ||
                               couples_ || bridge_ || (tiles_ && tiles_->automatic())));
}

void PuzzleGame::drawControl(Surface& target, const Control& c) {
    drawSprite(target, *c.sprite,
               c.button.state == Button::State::Armed && c.button.inside ? c.pressed : c.idle, c.x,
               c.y + 192);
}

// The bar at the top of the question screen: puzzle number, current worth,
// full worth and hint coins.
void PuzzleGame::header(Surface& image) {
    const auto& s = assets_.sprite("nazo/system/ge/nazo_text.arj");
    if (data_.number >= 154) {
        // ov27 020f3880..020f3954: special header uses the external catalogue index.
        const auto& p = s.variables.at("nazo_p");
        drawSprite(image, s, "nazo", p[0], p[1]);
        drawSprite(image, s, "w", 23, 4);
        if (launch_.specialNumber) {
            const auto number = *launch_.specialNumber;
            if (number > 99) {
                throw std::runtime_error(
                    "special puzzle number outside recovered two-digit header");
            }
            drawSprite(image, s, std::to_string(number / 10), 29, 5);
            drawSprite(image, s, std::to_string(number % 10), 35, 5);
        } else {
            // Host disclosure: this ROM-only library has no downloaded catalogue.
            drawSprite(image, s, "question", 29, 5);
            drawSprite(image, s, "question", 35, 5);
        }
        return;
    }
    if (internalId_ == 206) {
        // ov27 020f37e0: internal 206 draws only the label and animation 15
        // ("?") at x=0x25/0x1e/0x17.
        const auto& p = s.variables.at("nazo_p");
        drawSprite(image, s, "nazo", p[0], p[1]);
        const auto& step = s.animations.at(15).at(0);
        for (int x : {0x25, 0x1e, 0x17}) {
            blit(image, s.images.at(step.image), x, 4);
        }
        return; // Branches to 020f3a7c: no picarat, hint or coin fields.
    }
    for (const auto& [label, position] : std::array<std::pair<const char*, const char*>, 3>{
             {{"nazo", "nazo_p"}, {"pk", "pk_p"}, {"hint", "hint_p"}}}) {
        const auto& p = s.variables.at(position);
        drawSprite(image, s, label, p[0], p[1]);
    }
    // ov27 020f37e0 number/remaining/original/coin fields use sprite variables.
    const std::array<unsigned, 4> numbers{data_.number,
                                          data_.picarats[std::min(session_.first.failures(), 2u)],
                                          data_.picarats[0], session_.coins};
    for (unsigned field = 0; field < 4; ++field) {
        const auto& p = s.variables.at(field ? "pos" + std::to_string(field + 1) : "pos");
        std::string n = std::to_string(numbers[field]);
        // The ROM number renderer pads puzzle numbers to 3 positions.
        if (field == 0) {
            n = std::string(3 - n.size(), '0') + n;
        }
        int x = p[0];
        for (auto i = n.rbegin(); i != n.rend(); ++i) {
            drawSprite(image, s, std::string(1, *i), x, p[1]);
            x -= p[2];
        }
    }
}

// One frame of the picarat count-up (see countPicarats()).
void PuzzleGame::present(std::uint32_t total, std::uint32_t remaining, std::uint32_t, bool) {
    countTotal_ = total;
    countRemaining_ = remaining;
    frame();
}

void PuzzleGame::wait(int frames) {
    for (int i = 0; i < std::max(frames, 1); ++i) {
        frame();
    }
}

// The extra result screen when replaying a puzzle from the puzzle index:
// the title, how many hints were used, and a jingle.
void PuzzleGame::replayResult() {
    // ov27 020f1da4..020f207c: only from the puzzle index (0x13), 0x1f, or a
    // solved challenge (0x18) - the title and the hints seen this time (+8214).
    const auto f = launch_.followingScene;
    if (!(f == 0x13 || f == 0x1f || (f == 0x18 && session_.first.status() == 2))) {
        return;
    }
    const unsigned hints = transientHints_;
    auto text = [&](const std::string& name) {
        auto b = readFile(assets_.root / "text" / name);
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    };
    char line[512];
    std::snprintf(line, sizeof line, text("tx_700.txt").c_str(), data_.title.c_str());
    resultTitle_ = text_.expand(line);
    if (hints == 0) {
        resultHints_ = text_.expand(text("tx_701.txt"));
    } else if (hints < 2) {
        resultHints_ = text_.expand(text("tx_1601.txt"));
    } else {
        std::snprintf(line, sizeof line, text("tx_702.txt").c_str(), int(hints));
        resultHints_ = text_.expand(line);
    }
    resultScreen_ = true;
    auto s = music();
    if (s) {
        s->bgmVolume(0, 1);
    }
    wait(0x23);
    frame();
    // Jingle: stream ST_005 without hints, SY 13 with hints, both tracked.
    if (s) {
        if (hints == 0) {
            s->playTrackedStream(0, 5, -1, 1, 0);
        } else {
            s->playTrackedEffect(0xd);
        }
    }
    // Per frame: level = min(20, f2iz(15 * sin(t))), t += 0.05 (0206b35c is
    // a float sine, checked in the oracle). Once the jingle has ended and the
    // level exceeds 1, an animation steps (not drawn natively) and the first
    // time the music returns over 1 s.
    bool restore = true;
    float t = 0;
    for (;;) {
        int level = int(15.0f * float(std::sin(double(t))));
        if (level > 20) {
            level = 20;
        }
        t += 0.05f;
        if (s && !s->trackedBusy() && level > 1 && restore) {
            s->bgmVolume(1, 1);
            restore = false;
        }
        frame();
        if (!(s && s->trackedBusy()) && touch_.released) {
            break;
        }
    }
    effect(3);
    resultScreen_ = false;
}

void PuzzleGame::voice(unsigned id) {
    // Outcome voice of the judgment (ov2 0206b9cc/0206ba04): tracked stream
    // route 0, i.e. /data_lt2/stream/nazo/ge/ST_%04d.SAD for ids from 100.
    if (auto s = music()) {
        s->playTrackedStream(0, int(id), -1, 1, 0);
    }
}

// The verdict picture of the judgment sequence (flag bit 0 picks the set).
std::string PuzzleGame::judgmentResult(bool wrong) const {
    return std::string("nazo/hantei/ge/judge_") + (data_.flags & 1 ? "l" : "r") +
           (wrong ? "114" : "14") + "_bg";
}

// Plays the judgment sequence for a correct or wrong answer.
void PuzzleGame::judgment(bool wrong) {
    scene_ = SceneKind::PuzzleJudgment;
    judgmentImage_.clear();
    judgmentFinal_ = false;
    // ov27 020f251c first starts the answer voice; ov2 0206b4fc chooses
    // the outcome voice later. Other engine consumers of RNG remain unrecovered.
    if (auto s = music()) {
        s->playStream(0,
                      100 + ((data_.flags & 2) ? 5 : 0) + int(gameRandom(session_.randomState) % 3),
                      -1, 1, 0, false); // ov27 020f2580
    }
    const auto table = readFile(assets_.root / "puzzle/judgment.tbl");
    runJudgment(table, (data_.flags & 1 ? 0 : 2) | (wrong ? 1 : 0), false,
                gameRandom(session_.randomState), *this);
}

// Draws both screens (upper screen in rows 0..191, lower in 192..383) for
// the current part of the scene.
namespace {
// A frame's texture chunks (stored sizes), or the bounds the older sprite
// version kept.
std::span<const Rect> textureChunks(const Sprite& s, unsigned image) {
    if (image < s.quads.size() && !s.quads[image].empty()) {
        return s.quads[image];
    }
    return image < s.chunks.size() ? std::span<const Rect>(s.chunks[image])
                                   : std::span<const Rect>{};
}
} // namespace

Surface PuzzleGame::render() {
    Surface image{256, 384, std::vector<std::uint16_t>(256 * 384, 0x8000)};
    const auto& font = assets_.font("fontq.NFTR");
    if (scene_ == SceneKind::PuzzleCard) {
        if (card_.visible) {
            renderCard(image);
        }
        return image;
    }
    if (scene_ == SceneKind::PuzzleJudgment) {
        if (!judgmentImage_.empty()) {
            const auto& bg = assets_.background(judgmentImage_);
            // Native window/scroll service: sample each 192-line screen
            // separately, preventing cross-screen blits. DS scroll adds offsets.
            for (int y = 0; y < 192; ++y) {
                for (int x = 0; x < 256; ++x) {
                    const int by = y + judgmentBottom_, ty = y + judgmentTop_;
                    if (by >= 0 && by < 192) {
                        image.pixels[(y + 192) * 256 + x] = bg.pixels[by * 256 + x];
                    }
                    if (judgmentFinal_ && ty >= 0 && ty < 192) {
                        image.pixels[y * 256 + x] = bg.pixels[ty * 256 + x];
                    }
                }
            }
            applyBrightness(image, 0, judgmentBrightness_);
        }
        return image;
    }
    // Upper screen: the question text on the puzzle's theme background.
    blit(image, assets_.background("nazo/system/nazo_text" + std::to_string(data_.theme)), 0, 0);
    header(image);
    textLines(image, font, data_.prompt, 13, 22, 12, 0x8000, true);
    // Picarat reward: the count-up on the lower screen.
    if (scene_ == SceneKind::PuzzleReward) {
        blit(image, assets_.background(judgmentResult(false)), 0, 0);
        blit(image, assets_.background("nazo/system/ge/picarat_get"), 0, 192);
        for (const auto& [path, value] : std::array<std::pair<const char*, std::uint32_t>, 2>{
                 {{"nazo/system/picaratGetBig.arc", countTotal_},
                  {"nazo/system/picaratGetSmall.arc", countRemaining_}}}) {
            const auto& sprite = assets_.sprite(path);
            const auto& p = sprite.variables.at("pos");
            auto digits = std::to_string(value);
            int x = p[0];
            for (auto i = digits.rbegin(); i != digits.rend(); ++i) {
                drawSprite(image, sprite, std::string(1, *i), x, 192 + p[1]);
                x -= p[2];
            }
        }
        return image;
    }
    if (scene_ == SceneKind::PuzzleSolved || scene_ == SceneKind::Complete) {
        // ov27 020f15a8..166c: flag 0x10 selects an answer illustration;
        // otherwise retain the final correct-judgment card on the upper screen.
        blit(image,
             assets_.background((data_.flags & 0x10)
                                    ? std::string("nazo/") + ((data_.flags & 0x40) ? "ge/" : "") +
                                          "q" + std::to_string(data_.background) + "a"
                                    : judgmentResult(false)),
             0, 0);
        if (resultScreen_) {
            // 020f1e1c: jiten_seikai background; title and hint lines at y 0x37
            // and 0x71 (02072d80, centred - native text window, unverified).
            blit(image, assets_.background("nazo/system/jiten_seikai"), 0, 192);
            auto centred = [&](const std::string& text, int y) {
                std::size_t start = 0;
                for (int row = 0; start <= text.size(); ++row) {
                    auto end = text.find('\n', start);
                    if (end == std::string::npos) {
                        end = text.size();
                    }
                    const auto l = text.substr(start, end - start);
                    int width = 0;
                    for (unsigned char c : l) {
                        width += textAdvance(font, c) + 1;
                    }
                    textLines(image, font, l, 128 - (width >> 1), 192 + y + row * 16, 16);
                    start = end + 1;
                }
            };
            centred(resultTitle_, 0x37);
            centred(resultHints_, 0x71);
            return image;
        }
        blit(image, assets_.background("nazo/system/nazo_seikai" + std::to_string(data_.theme)), 0,
             192);
        textLines(image, font, data_.correct, 13, 214, 12, 0x8000, true);
        return image;
    }
    // Wrong answer: the explanation, then the "try again" button.
    if (scene_ == SceneKind::PuzzleWrong) {
        blit(image, assets_.background(judgmentResult(true)), 0, 0);
        blit(image, assets_.background("nazo/system/nazo_fail" + std::to_string(data_.theme)), 0,
             192);
        textLines(image, font, data_.incorrect, 13, 214, 12, 0x8000, true);
        if (retryMenu_) {
            blit(image, assets_.background("nazo/system/qend_retry"), 0, 192);
            drawControl(image, retry_);
        }
        return image;
    }
    // Hints: tabs for unlocked hints, a lock for the next one, and the
    // "use a hint coin?" confirmation.
    if (scene_ == SceneKind::PuzzleHint) {
        blit(image,
             assets_.background("nazo/system/ge/" +
                                std::string(hintConfirm_ && freeHints() ? "jitenhint_" : "hint_") +
                                std::to_string(hintPage_ + 1) +
                                (hintConfirm_ && !freeHints() ? "_2" : "")),
             0, 192);
        drawControl(image, hintBack_);
        for (unsigned i = 0; i < (singleHint() ? 1u : 3u); ++i) {
            if (i < unlockedHints()) {
                drawControl(image, hintTabs_[i]);
            } else if (i == unlockedHints()) {
                drawControl(image, hintLocks_[i]);
            }
        }
        if (hintConfirm_) {
            drawControl(image, hintYes_);
            drawControl(image, hintNo_);
            if (!freeHints()) {
                const auto& s = assets_.sprite("nazo/system/hint_num.arc");
                const auto& p = s.variables.at("pos");
                int x = p[0];
                auto digits = std::to_string(session_.coins);
                for (auto i = digits.rbegin(); i != digits.rend(); ++i) {
                    drawSprite(image, s, std::string(1, *i), x, 192 + p[1]);
                    x -= p[2];
                }
            }
        } else {
            const auto& p = assets_.sprite("nazo/system/hint_num.arc").variables.at("hinttext_p");
            textLines(image, font, data_.hints.at(hintPage_), p[0], 192 + p[1], 12, 0x8000, true);
        }
        return image;
    }
    // "Quit this puzzle for now?"
    if (giveUp_) {
        blit(image, assets_.background("nazo/system/ge/atode_bg"), 0, 192);
        drawControl(image, giveYes_);
        drawControl(image, giveNo_);
        return image;
    }
    if (answers_ && answers_->answerScreen && !tutorial_) {
        renderAnswerScreen(image);
        return image;
    }
    // Lower screen: the puzzle's picture (or a tutorial page). Internal
    // puzzle 58 can toggle to an alternative picture "nazo/q%ix" (overlay 42,
    // 020f89cc; no language folder).
    const bool alternate = answers_ && answers_->alternateBackground;
    blit(image,
         assets_.background(
             tutorial_ ? "nazo/system/ge/puzzlet" + std::to_string(data_.tutorial) + "_0"
                       : std::string("nazo/") + ((data_.flags & 0x20) && !alternate ? "ge/" : "") +
                             "q" + std::to_string(data_.background) + (alternate ? "x" : "") +
                             (circlePage_ ? "_" + std::to_string(circlePage_) : "")),
         0, 192);
    if (tutorial_) {
        applyBrightness(image, screenBrightness_, screenBrightness_);
        return image;
    }
    // Then whatever the engine draws on top of the picture.
    if (tiles_) {
        if (tiles_->specialMode() == 1) {
            const auto& arrow = assets_.sprite("nazo/tilerotate/q2_button.arc");
            const auto& step = arrow.animations.at(arrow.animationNames.at("gfx")).at(0);
            int alpha = std::clamp(16 + int(15 * std::sin(double(tiles_->pulseAngle()))), 1, 31);
            blitNarrationPolygon(image, arrow.images.at(step.image), 92, 192 + 55, alpha);
        }
        auto rotated = [&](const Sprite& s, const std::string& animation, int x, int y,
                           float angle) {
            const auto& step = s.animations.at(s.animationNames.at(animation)).at(0);
            blitRotated(image, s.images.at(step.image), x, 192 + y, angle, 192,
                        textureChunks(s, step.image));
        };
        // ov48 020fa344: accepted/withdrawn keys render behind the hole mask.
        for (int pass = 0; pass < 2; ++pass) {
            const auto& drawOrder = tiles_->drawOrder();
            for (auto i : drawOrder) {
                const auto& p = tiles_->pieces()[i];
                bool behind = p.animationState > 1 && p.animationState != 6;
                if ((pass == 0) != behind) {
                    continue;
                }
                const auto& s = assets_.sprite(tiles_->sprites().at(p.sprite));
                std::string animation = p.animationState == 3     ? "1"
                                        : p.animationState == 7   ? "2"
                                        : i == tiles_->selected() ? p.held
                                                                  : p.idle;
                if (tiles_->rotating()) {
                    int ax[]{p.anchorX, -p.anchorY, -p.anchorX, p.anchorY},
                        ay[]{p.anchorY, p.anchorX, -p.anchorY, -p.anchorX};
                    auto& player = tileAnimations_.at(i);
                    player.select(s.animationNames.at(animation), tick_, s.animations);
                    const auto& step = s.animations.at(player.animation).at(player.step);
                    blitRotated(image, s.images.at(step.image),
                                int(p.displayX) - ax[p.rotation & 3],
                                192 + int(p.displayY) - ay[p.rotation & 3], p.displayAngle, 192,
                                textureChunks(s, step.image));
                    player.advance(tick_, s.animations, scene_ != SceneKind::Puzzle);
                } else {
                    drawSprite(image, s, animation, int(p.displayX), 192 + int(p.displayY));
                }
            }
            if (pass == 0 && tiles_->specialMode() == 1) {
                drawSprite(image, assets_.sprite("nazo/tilerotate/q2_button.arc"), "hole", 151,
                           192 + 51);
            }
        }
        if (tiles_->rotating() && tiles_->selected() >= 0 &&
            (tiles_->state() == 1 || tiles_->state() == 2)) {
            const auto& p = tiles_->pieces()[tiles_->selected()];
            rotated(assets_.sprite(internalId_ == 203 ? "nazo/tilerotate/pict_puzzle_rot_q203.arc"
                                                      : "nazo/tilerotate/pict_puzzle_rot.arc"),
                    tiles_->state() == 1 ? "gfx" : "rot", int(p.displayX + 0.5f),
                    int(p.displayY + 0.5f), 0);
        }
        if (tiles_->brightness()) {
            applyBrightness(image, 0, tiles_->brightness());
        }
    }
    if (pegs_) {
        pegs_->animate();
        pegs_->render(image, assets_.sprite("nazo/pegsolitaire/solitaire_gfx.arc"), 192);
    }
    if (knight_) {
        knight_->animate();
        knight_->render(image, assets_.sprite("nazo/knight/knight_gfx.arc"), 192);
    }
    if (colors_) {
        colors_->render(image, 192);
    }
    if (lines_) {
        renderLines(image);
    }
    if (traces_ && scene_ != SceneKind::PuzzleMemo) {
        blit(image, traceInk_.surface, 0, 192);
    }
    if (slots_) {
        // ov39 020f7eb8: back to front, so the first entry is drawn last.
        const auto& order = slots_->order();
        for (auto k = order.size(); k-- > 0;) {
            const auto& tile = slots_->tiles()[std::size_t(order[k])];
            blit(image, slotFrame(tile), tile.x, 192 + tile.y);
        }
    }
    if (bridge_) {
        // ov53 020f7d44: footprints, pulsing next steps (textured polygons).
        const auto& sprite = assets_.sprite("nazo/bridge/q99_foot.arc");
        for (const auto& d : bridge_->draws()) {
            const auto& frame = sprite.images.at(
                sprite.animations.at(sprite.animationNames.at(d.animation)).at(0).image);
            if (d.alpha >= 31) {
                blit(image, frame, d.x, 192 + d.y);
            } else {
                blitNarrationPolygon(image, frame, d.x, 192 + d.y, d.alpha);
            }
        }
    }
    if (couples_) {
        // ov51 020f80e8: four-digit move counter, items, touch points, held pair.
        const auto& digits = assets_.sprite("nazo/common/counter_number.arc");
        int moves = couples_->moves();
        for (int i = 0; i < 4; ++i) {
            drawSprite(image, digits, std::to_string(moves % 10), 131 - 6 * i, 192 + 11);
            moves /= 10;
        }
        const auto& sprite = assets_.sprite("nazo/couple/couple_gfx.arc");
        for (const auto& d : couples_->draws()) {
            drawSprite(image, sprite, d.animation, d.x, 192 + d.y);
        }
    }
    if (skate_) {
        // ov49 020f83dc: the protagonist (animated sprite +0x198), then the arrows.
        const auto& sprite = assets_.sprite("nazo/iceskate/iceskate_gfx.arc");
        const char* move = skate_->protagonistAnimation();
        skater_.select(move ? sprite.animationNames.at(move) : 1u, tick_, sprite.animations);
        blit(image,
             sprite.images.at(sprite.animations.at(skater_.animation).at(skater_.step).image),
             skate_->x(), 192 + skate_->y());
        skater_.advance(tick_, sprite.animations);
        for (const auto& a : skate_->arrows()) {
            drawSprite(image, sprite, a.animation, a.x, 192 + a.y);
        }
    }
    if (lamps_) {
        // ov52 020f812c: streets through the 3D service (anti-aliasing on,
        // set by 020711a0), then the lamp sprites.
        lamps_->streets().render(image, 192, true);
        const auto& sprite = assets_.sprite("nazo/lamp/lamp_gfx.arc");
        for (const auto& p : lamps_->lamps()) {
            drawSprite(image, sprite, "lamp", p[0], 192 + p[1]);
        }
    }
    if (roses_) {
        // ov46 020f82dc: row-major light counts, then planted roses.
        const auto& sprite = assets_.sprite("nazo/rose/rose_gfx.arc");
        int count = 0;
        const auto draws = roses_->draws(count);
        for (int i = 0; i < count; ++i) {
            drawSprite(image, sprite, draws[std::size_t(i)].animation, draws[std::size_t(i)].x,
                       192 + draws[std::size_t(i)].y);
        }
    }
    if (toggles_) {
        for (const auto& c : toggles_->cells()) {
            // ov45 020f7e2c: 02070e8c rectangles with alpha +0x82 over the 2D
            // background (3D layer blending).
            for (int y = std::max(0, c.y); y < std::min(192, c.y + c.height); ++y) {
                for (int x = std::max(0, c.x); x < std::min(256, c.x + c.width); ++x) {
                    auto& px = image.pixels[std::size_t((y + 192) * 256 + x)];
                    px = blend3D(toggles_->color(), toggles_->alpha(), px);
                }
            }
        }
    }
    if (slides_) {
        // ov47 020f8a78: pieces in index order (textured polygons, later
        // nearer) at display + 0.5, then the four-digit move counter.
        for (int i = 0; i < int(slides_->pieces().size()); ++i) {
            const auto& p = slides_->pieces()[std::size_t(i)];
            const auto& sprite = assets_.sprite(
                slides_->sprites()
                    .at(std::size_t(p.sprite))
                    .substr(0, slides_->sprites().at(std::size_t(p.sprite)).rfind('.')) +
                ".arc");
            drawSprite(image, sprite, i == slides_->selected() ? p.held : p.idle,
                       int(0.5f + p.displayX), 192 + int(0.5f + p.displayY));
        }
        const auto& digits = assets_.sprite("nazo/common/counter_number.arc");
        int moves = slides_->moves();
        for (int i = 0; i < 4; ++i) {
            drawSprite(image, digits, std::to_string(moves % 10), 131 - 6 * i, 192 + 11);
            moves /= 10;
        }
    }
    if (choices_) {
        for (const auto& c : choices_->choices()) {
            blit(image,
                 *(c.button.state == Button::State::Armed && c.button.inside ? c.pressed : c.idle),
                 c.x, 192 + c.y);
        }
    }
    if (selection_) {
        for (const auto& c : selection_->choices()) {
            if (c.selected) {
                blit(image, *c.image, c.bounds.x, 192 + c.bounds.y);
            }
        }
    }
    if (towers_) {
        const auto& sprite = assets_.sprite("nazo/pancake/pancake_gfx.arc");
        for (int i : towers_->drawOrder()) {
            const auto& p = towers_->pieces()[i];
            drawSprite(image, sprite, std::to_string(i + 1), int(p.displayX + 0.5f),
                       192 + int(p.displayY + 0.5f));
        }
        const auto& digits = assets_.sprite("nazo/common/counter_number.arc");
        int moves = towers_->moves();
        for (int i = 0; i < 4; ++i) {
            drawSprite(image, digits, std::to_string(moves % 10), 131 - 6 * i, 192 + 11);
            moves /= 10;
        }
    }
    if (circleParameters_) {
        if (scene_ != SceneKind::PuzzleMemo) {
            blit(image, circleInk_.surface, 0, 192);
        }
        const auto& stroke = circle_.stroke;
        if (stroke.invalid) {
            drawSprite(image, assets_.sprite("nazo/tracebutton/ge/retry_trace.arc"), "gfx", 43,
                       192 + 88);
        } else if (stroke.markerX > 2 && stroke.markerY >= 0) {
            drawSprite(image, assets_.sprite("nazo/tracebutton/point_trace.arc"), "gfx",
                       stroke.markerX, 192 + stroke.markerY);
        }
        if (circleParameters_->groupCount() > 1) {
            drawControl(image, circleLeft_);
            drawControl(image, circleRight_);
        }
    }
    // The memo: the player's notes over the puzzle.
    if (scene_ == SceneKind::PuzzleMemo) {
        // 3D: the window over the puzzle, then the two buttons; above them
        // the text layer (priorities 0206d9f4(1, 0, 3, 2)), shown only inside
        // WIN0. While opening it still holds the puzzle's own ink; open, the
        // memo's ink (restored by 0201cd84); closing, nothing (02072a48).
        const auto& w = memoWindow_;
        drawMemoWindow(image, 256 - w.width, 0, w.width, w.height);
        drawControl(image, erase_);
        drawControl(image, back_);
        auto textLayer = [&](const Surface& layer) {
            for (int y = 0; y < 192; ++y) {
                for (int x = 0; x < 256; ++x) {
                    const auto p = layer.pixels[std::size_t(y * 256 + x)];
                    if (p && memoShowsTextLayer(x, y)) {
                        image.pixels[std::size_t((y + 192) * 256 + x)] = p;
                    }
                }
            }
        };
        if (w.stage == MemoStage::Opening) {
            if (traces_) {
                textLayer(traceInk_.surface);
            }
            if (circleParameters_) {
                textLayer(circleInk_.surface);
            }
        } else if (w.stage == MemoStage::Open) {
            textLayer(memoInk_.surface);
        }
        return image;
    }
    if (answers_) {
        // 020f8788 question screen: q58 toggle, hint, give up, memo, "Antwort eingeben".
        if (internalId_ == 58) {
            drawControl(image, pictureToggle_);
        }
        for (const auto* c : {&hint_, &later_, &memo_}) {
            if (controlAvailable(c)) {
                drawControl(image, *c);
            }
        }
        drawControl(image, kaito_);
    } else {
        for (const auto* c : {&answer_, &reset_, &hint_, &memo_, &later_}) {
            if (controlAvailable(c)) {
                drawControl(image, *c);
            }
        }
    }
    // During the question reveal: a "touch" prompt in the middle.
    if (scene_ == SceneKind::PuzzleIntro) {
        const auto& s = assets_.sprite("nazo/system/ge/touch.arc");
        const auto& f = s.images.at(s.animations.at(1).at(0).image);
        blit(image, f, (256 - int(f.width)) / 2, 192 + (192 - int(f.height)) / 2);
    }
    applyBrightness(image, screenBrightness_, screenBrightness_);
    return image;
}

// The answer screen of the written-answer puzzles.
void PuzzleGame::renderAnswerScreen(Surface& image) {
    // ov42 020f8788 with +0x68 == 1: picture, text/ink layer, hint, Loeschen,
    // Antwort, Zurueck, per-box erasers and the delayed value previews.
    blit(image, assets_.background(answers_->background), 0, 192);
    blit(image, answerInk_.surface, 0, 192);
    if (controlAvailable(&hint_)) {
        drawControl(image, hint_);
    }
    drawControl(image, allreset_);
    drawControl(image, hantei_);
    drawControl(image, modoru_);
    for (int i = 0; i < answers_->boxCount; ++i) {
        drawControl(image, erasers_[size_t(i)]);
    }
    const auto& numbers = assets_.sprite("nazo/drawinput/inputnumbers.arc");
    for (const auto& p : answers_->previews()) {
        if (p.digit) {
            // 020f9028: animation index value+1 drawn at (x-4,y-4).
            const auto& step = numbers.animations.at(unsigned(*p.digit)).at(0);
            blit(image, numbers.images.at(step.image), p.x - 4, 192 + p.y - 4);
        }
    }
    if (answerFailure_) {
        // 020f89cc: "inputfail" centred until the stylus is released.
        const auto& fail = assets_.sprite("nazo/drawinput/ge/inputfail.arc");
        const auto& f =
            fail.images.at(fail.animations.at(fail.animationNames.at("gfx")).at(0).image);
        blit(image, f, (256 - int(f.width)) / 2, 192 + (192 - int(f.height)) / 2);
    }
    applyBrightness(image, screenBrightness_, screenBrightness_);
}

// Fades both screens in from or out to black over 16 frames.
void PuzzleGame::fadeScreens(bool in) {
    // ov2 02071790 / 02071e84 with step 1, interval 1 (17 iterations).
    if (in) {
        for (int i = -16; i <= 0; ++i) {
            frame();
            screenBrightness_ = std::min(0, screenBrightness_ + 1);
            if (screenBrightness_ == 0) {
                break;
            }
        }
    } else {
        for (int i = 0; i >= -16; --i) {
            frame();
            screenBrightness_ = std::max(-16, screenBrightness_ - 1);
        }
    }
}

// One frame of the written-answer puzzles' buttons and writing boxes.
void PuzzleGame::answerFrame(bool input) {
    // ov42 020f8788: button updates, box input (unless "Antwort" is armed)
    // and the text-layer preview. Ink/text layer edits are applied here.
    auto& a = *answers_;
    if (input) {
        if (a.answerScreen) {
            allreset_.button.update(touch_);
            hantei_.button.update(touch_);
            modoru_.button.update(touch_);
            for (int i = 0; i < a.boxCount; ++i) {
                erasers_[size_t(i)].button.update(touch_);
                if (hantei_.button.state != Button::State::Armed) {
                    a.updateBox(i, touch_);
                }
            }
        } else {
            if (internalId_ == 58) {
                pictureToggle_.button.update(touch_);
            }
            kaito_.button.update(touch_);
        }
    }
    if (a.answerScreen) {
        a.updatePreview();
    }
    const auto& brush = assets_.font("fontq.NFTR");
    if (a.clearText) {
        answerInk_.clear();
        a.clearText = false;
    }
    for (const auto& r : a.textClears) {
        for (int y = std::max(0, r.y); y < std::min(192, r.y + r.height); ++y) {
            for (int x = std::max(0, r.x); x < std::min(256, r.x + r.width); ++x) {
                answerInk_.surface.pixels[size_t(y * 256 + x)] = 0;
            }
        }
    }
    a.textClears.clear();
    for (const auto& c : a.ink) {
        CircleInkCommand cmd;
        cmd.kind = c.line ? CircleInkCommand::Kind::Line : CircleInkCommand::Kind::Dot;
        cmd.x0 = c.x0;
        cmd.y0 = c.y0;
        cmd.x1 = c.x1;
        cmd.y1 = c.y1;
        answerInk_.apply(cmd, brush, 0);
    }
    a.ink.clear();
    if (a.answerScreen) {
        for (const auto& p : a.previews()) {
            if (!p.clear) {
                continue;
            }
            // 02072a48 clears an 18x18 cell; 02072ab0 draws "%c" with font18.
            for (int y = std::max(0, p.y); y < std::min(192, p.y + 0x12); ++y) {
                for (int x = std::max(0, p.x); x < std::min(256, p.x + 0x12); ++x) {
                    answerInk_.surface.pixels[size_t(y * 256 + x)] = 0;
                }
            }
            if (p.text) {
                textGlyph(answerInk_.surface, assets_.font("font18.NFTR"),
                          unsigned((*p.text)[0]) & 0xff, p.x, p.y, 0x8000);
            }
        }
    }
}

// Handles the written-answer buttons; true when the answer is to be judged.
bool PuzzleGame::updateAnswers() {
    // ov42 020f89cc (virtual +208). Returns true when judgment must run.
    auto& a = *answers_;
    if (internalId_ == 58 && !a.answerScreen && pictureToggle_.button.consumeClick()) {
        a.alternateBackground = !a.alternateBackground;
    }
    if (a.answerScreen) {
        if (hantei_.button.consumeClick()) {
            switch (a.submit()) {
            case AnswerPuzzle::Submit::Unrecognized:
                // ov42 020f89cc: SY 4, wait for a release, then SY 3.
                effect(4);
                answerFailure_ = true;
                do {
                    frame();
                    answerFrame(false);
                } while (!touch_.released);
                answerFailure_ = false;
                effect(3);
                return false;
            case AnswerPuzzle::Submit::Missing:
                effect(4);
                return false;
            case AnswerPuzzle::Submit::Judge:
                effect(0x6f);
                return true;
            }
        } else if (allreset_.button.consumeClick()) {
            a.reset();
        }
        for (int i = 0; i < a.boxCount; ++i) {
            if (erasers_[size_t(i)].button.consumeClick()) {
                a.erase(i);
                break;
            }
        }
    }
    if (!a.answerScreen && kaito_.button.consumeClick()) {
        fadeScreens(false);
        a.openAnswerScreen();
        for (int i = 0; i < a.boxCount; ++i) {
            // 020801a8(button,"posN",spacing*i,0).
            auto& e = erasers_[size_t(i)];
            const auto& v = e.sprite->variables.at(a.eraserVariables[size_t(i)]);
            const int first = a.mode == 6 && i >= 2 ? 2 : 0;
            e.x = v[0] + v[2] * (i - first);
            e.y = v[1];
            e.button.bounds.x = e.x;
            e.button.bounds.y = e.y;
        }
        answerFrame(false);
        frame(); // 0206cfb8 ends one frame before the fade-in.
        fadeScreens(true);
        return false;
    }
    if (a.answerScreen && modoru_.button.consumeClick()) {
        fadeScreens(false);
        a.closeAnswerScreen();
        answerFrame(false);
        frame();
        fadeScreens(true);
    }
    return false;
}

// The hint screen. Up to three hints per puzzle; each costs one hint coin
// the first time (free when replaying a solved puzzle). Puzzles numbered 154
// and above, and the number-0 gift, have a single hint.
void PuzzleGame::hint() {
    // ov27 020f6470 opens the next locked hint (or hint 3 after all unlocked).
    scene_ = SceneKind::PuzzleHint;
    hintPage_ = singleHint() ? 0 : std::min(unlockedHints(), 2u);
    hintConfirm_ = hintPage_ >= unlockedHints();
    for (;;) {
        frame();
        hintBack_.button.update(touch_);
        if (hintBack_.button.consumeClick()) {
            break;
        }
        // 020f64f4 exposes all unlocked tabs plus exactly the next locked tab.
        for (unsigned i = 0; i < (singleHint() ? 1u : 3u); ++i) {
            auto& c = i < unlockedHints() ? hintTabs_[i] : hintLocks_[i];
            if (i <= unlockedHints() && i != hintPage_) {
                c.button.update(touch_);
                if (c.button.consumeClick()) {
                    hintPage_ = i;
                    hintConfirm_ = i >= unlockedHints();
                }
            }
        }
        if (hintConfirm_) {
            hintNo_.button.update(touch_);
            hintYes_.button.update(touch_);
            if (hintNo_.button.consumeClick()) {
                break;
            }
            if (hintYes_.button.consumeClick()) {
                // ov27 020f6c40..020f6ca0: replay/special hints only change transient +8214.
                if (freeHints()) {
                    transientHints_ = singleHint() ? 1 : hintPage_ + 1;
                    hintConfirm_ = false;
                } else if (session_.purchaseHint(hintPage_ + 1)) {
                    hintConfirm_ = false;
                }
            }
        }
    }
    updateHintButton();
    scene_ = SceneKind::Puzzle;
}

// The memo: a notepad for scribbling over the puzzle picture. Its contents
// stay until the puzzle ends.
void PuzzleGame::memo() {
    // 020f4068: the controls slide away, then the window opens from the top
    // right, 24x24 growing by 16 x 12 a frame (the full size is first drawn
    // by the memo loop); SE 0x70 as it starts.
    slideMemoControls(true);
    scene_ = SceneKind::PuzzleMemo;
    effect(0x70);
    memoWindow_ = {MemoStage::Opening, 24, 24};
    for (;;) {
        frame();
        erase_.button.update(touch_);
        back_.button.update(touch_);
        memoWindow_.width = std::min(memoWindow_.width + 16, 256);
        memoWindow_.height = std::min(memoWindow_.height + 12, 192);
        if (memoWindow_.width == 256 && memoWindow_.height == 192) {
            break;
        }
    }
    // 020f4570: the pen draws black (text colour 15) with the shared ink
    // service while it is held inside (11..250, 6..181); leaving that area or
    // lifting it starts a new stroke. The ink under the memo button (now at
    // the top right, under "erase") and under "back" is wiped every frame.
    memoWindow_.stage = MemoStage::Open;
    int lastX = -1, lastY = -1;
    const auto& pen = assets_.font("fontq.NFTR");
    for (;;) {
        frame();
        back_.button.update(touch_);
        erase_.button.update(touch_);
        const bool held = touchFilter_.state == 1 || touchFilter_.state == 2; // 0206e20c
        if (erase_.button.consumeClick()) {
            releaseControls();
            memoInk_.clear(); // 02072a10
            lastX = lastY = -1;
        } else if (held && touchFilter_.x >= 11 && touchFilter_.x < 251 && touchFilter_.y >= 6 &&
                   touchFilter_.y < 182) {
            const int x = touchFilter_.x, y = touchFilter_.y; // 0206e288
            CircleInkCommand stroke;
            stroke.kind =
                lastX < 0 && lastY < 0 ? CircleInkCommand::Kind::Dot : CircleInkCommand::Kind::Line;
            stroke.x0 = lastX < 0 && lastY < 0 ? x : lastX;
            stroke.y0 = lastX < 0 && lastY < 0 ? y : lastY;
            stroke.x1 = x;
            stroke.y1 = y;
            memoInk_.apply(stroke, pen, 0);
            lastX = x;
            lastY = y;
            for (const auto* c : {&memo_, &back_}) {
                const auto& r = c->button.bounds;
                for (int py = std::max(0, r.y); py < std::min(192, r.y + r.height); ++py) {
                    for (int px = std::max(0, r.x); px < std::min(256, r.x + r.width); ++px) {
                        memoInk_.surface.pixels[std::size_t(py * 256 + px)] = 0;
                    }
                }
            }
        } else {
            lastX = lastY = -1;
        }
        if (back_.button.consumeClick()) {
            break;
        }
    }
    // Back: the buttons let go, SE 0x71, the ink is kept (0201bf48 copy),
    // and 020f4304 shrinks the window by 32 x 24 a frame with the text layer
    // cleared; then the controls slide back.
    releaseControls();
    effect(0x71);
    memoWindow_ = {MemoStage::Closing, 256, 192};
    for (;;) {
        frame();
        back_.button.update(touch_);
        erase_.button.update(touch_);
        memoWindow_.width = std::max(memoWindow_.width - 32, 0);
        memoWindow_.height = std::max(memoWindow_.height - 24, 0);
        if (memoWindow_.width == 0 && memoWindow_.height == 0) {
            break;
        }
    }
    memoWindow_.stage = MemoStage::Hidden;
    scene_ = SceneKind::Puzzle;
    slideMemoControls(false);
}

// The controls make way for the memo and come back afterwards: the memo,
// hint and give-up buttons move up 8 px a frame (the memo button stops at
// the top), the answer button down and the reset button right; back the
// same way to their places.
void PuzzleGame::slideMemoControls(bool away) {
    Control* answer = answers_ ? &kaito_ : &answer_;
    const bool hasAnswer = answers_ ? !answers_->answerScreen : controlAvailable(&answer_);
    const bool hasReset = controlAvailable(&reset_);
    auto place = [](Control& c, int x, int y) { // 02080184
        c.x = c.button.bounds.x = x;
        c.y = c.button.bounds.y = y;
    };
    if (away) {
        memoHome_ = memo_.y; // +0x40
    }
    const int memoHome = memoHome_;
    for (;;) {
        if (away ? memo_.y <= 0 : memo_.y >= memoHome) {
            break;
        }
        const int step = away ? -8 : 8;
        place(memo_, memo_.x, std::clamp(memo_.y + step, 0, memoHome));
        place(hint_, hint_.x, hint_.y + step);
        place(later_, later_.x, later_.y + step);
        if (hasAnswer) {
            const int rest = 192 - answer->button.bounds.height;
            if (away && answer->y < 192) {
                place(*answer, answer->x, answer->y + 8);
            } else if (!away && answer->y >= rest) {
                place(*answer, answer->x, std::max(answer->y - 8, rest));
            }
        }
        if (hasReset) {
            const int rest = 256 - reset_.button.bounds.width;
            if (away && reset_.x < 256) {
                place(reset_, reset_.x + 8, reset_.y);
            } else if (!away && reset_.x >= rest) {
                place(reset_, std::max(reset_.x - 8, rest), reset_.y);
            }
        }
        frame();
    }
}

// The memo window: a frame of 8x8 tiles (memo_window.spr animations 1..8:
// corners and edges) and, inside it, a grey (180, 180, 180) rectangle at
// alpha 15 over the puzzle (02070e8c).
void PuzzleGame::drawMemoWindow(Surface& image, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    const auto& tiles = assets_.sprite("system/memo_window.arc");
    auto tile = [&](int animation, int tx, int ty) {
        drawSprite(image, tiles, std::to_string(animation), tx, 192 + ty);
    };
    // Top row (1, 2.., 3), left column (4.., 6), right column (5.., 8),
    // bottom row (6, 7.., 8), in the original's order.
    int right = x;
    for (int left = width, tx = x; left > 0; left -= 8, tx += 8) {
        tile(left == width ? 1 : left - 8 <= 0 ? 3 : 2, tx, y);
        right = tx;
    }
    int bottom = y;
    for (int left = height - 8, ty = y + 8; left > 0; left -= 8, ty += 8) {
        tile(left - 8 <= 0 ? 6 : 4, x, ty);
        bottom = ty;
    }
    for (int left = height - 8, ty = y + 8; left > 0; left -= 8, ty += 8) {
        tile(left - 8 <= 0 ? 8 : 5, right, ty);
    }
    for (int left = width, tx = x; left > 0; left -= 8, tx += 8) {
        tile(left == width ? 6 : left - 8 <= 0 ? 8 : 7, tx, bottom);
    }
    if (width - 16 <= 0 || height - 16 <= 0) {
        return;
    }
    constexpr std::uint16_t grey = 22 | 22 << 5 | 22 << 10; // 0206b114(180, 180, 180)
    for (int py = y + 8; py < std::min(192, y + height - 8); ++py) {
        for (int px = std::max(0, x + 8); px < std::min(256, x + width - 8); ++px) {
            auto& p = image.pixels[std::size_t((py + 192) * 256 + px)];
            p = blend3D(grey, 15, p);
        }
    }
}

bool PuzzleGame::memoShowsTextLayer(int x, int y) const {
    // WIN0 while opening: from 8 px inside the window's left edge to the
    // screen edge (its right edge 1 wraps, so column 0 shows too) and down
    // to the window's bottom; once open, 0..254 x 0..191.
    const auto& w = memoWindow_;
    if (w.stage == MemoStage::Opening) {
        return (x >= 256 - w.width + 8 || x < 1) && y < w.height;
    }
    return x < 255;
}

void PuzzleGame::releaseControls() {
    for (auto* c : {&memo_, &back_, &erase_, &hint_, &answer_, &later_, &reset_}) {
        c->button.state = Button::State::Idle; // 02080204(button, 0)
    }
}

// The Reset button: the engine returns to the puzzle's starting position.
void PuzzleGame::resetEngine() {
    if (pegs_) {
        pegs_->reset();
    }
    if (lines_) {
        lines_->reset();
    } else if (slides_) {
        slides_->reset();
    } else if (toggles_) {
        toggles_->reset();
    } else if (roses_) {
        roses_->reset();
    } else if (lamps_) {
        lamps_->reset();
    } else if (skate_) {
        skate_->reset();
    } else if (couples_) {
        couples_->reset();
    } else if (bridge_) {
        bridge_->reset();
    } else if (slots_) {
        slots_->reset();
    } else if (traces_) {
        traces_->reset();
        traceInk_.clear();
        traces_->clearInk();
    } else if (knight_) {
        knight_->reset();
    } else if (colors_) {
        colors_->reset(tick_);
    } else if (tiles_) {
        tiles_->reset();
    } else if (towers_) {
        towers_->reset();
    } else {
        circle_.stroke.reset();
        circleInk_.clear();
    }
}

// One frame of drawing for the circle-the-answer puzzles.
void PuzzleGame::updateCircle() {
    if (touch_.pressed) {
        circleInitialX_ = touch_.x;
    }
    if (touch_.held) {
        circleLastX_ = touch_.x;
        circleLastY_ = touch_.y;
    }
    const int x = touch_.x >= 0 ? touch_.x : circleLastX_,
              y = touch_.y >= 0 ? touch_.y : circleLastY_;
    const int phase = touch_.pressed ? 1 : touch_.held ? 2 : touch_.released ? 3 : 0;
    const auto& sprite = assets_.sprite("nazo/tracebutton/point_trace.arc");
    int height = int(
        sprite.images.at(sprite.animations.at(sprite.animationNames.at("gfx")).at(0).image).height);
    auto command =
        circle_.update({phase, x, y, x, circleInitialX_}, circleParameters_->group(circlePage_),
                       circleParameters_->maximumCenters, height);
    circleInk_.apply(command, assets_.font("fontq.NFTR"), circleParameters_->drawingColor);
}

// The hint button shows how many hints are unlocked.
void PuzzleGame::updateHintButton() {
    // ov27 020f4880: the single-label Wi-Fi button does not use numbered animations.
    if (data_.number >= 154 || data_.number == 0) {
        return;
    }
    hint_.idle = std::to_string(unlockedHints()) + "_off";
    hint_.pressed = std::to_string(unlockedHints()) + "_on";
}

// The title card shown before the puzzle (and again after a wrong answer).
void PuzzleGame::introCard() {
    // ov27 020efda0 (card constructor): fade the music out over time id 1003,
    // wait 60 frames (0206ce18) and switch the effect banks to the puzzle's
    // room-sound row without starting its music.
    const SceneKind previous = scene_;
    scene_ = SceneKind::PuzzleCard;
    card_ = {};
    if (auto s = music()) {
        s->fadeOutBgm(s->duration(0x3eb));
    }
    const int brightness = screenBrightness_;
    screenBrightness_ = 0;
    for (int i = 0; i < 0x3c + 8; ++i) {
        frame(); // 60-frame wait, then 8 loading frames (ROM 1237 -> 1305)
    }
    if (auto s = music()) {
        s->roomSound(int(data_.soundId), false);
    }
    for (int i = 0; i < 4; ++i) {
        frame(); // ROM 1305 -> 1309
    }
    // 020f05c8: the card appears on the lower screen with jingle stream 10
    // (0207fb84 route 0), fades in (02071790), waits 22 frames (0206ce18) and
    // scrolls up 16 pixels per frame onto the upper screen; then the title
    // text (020f04a8). Timings follow seq-audio-puzzle-entry-a frames 1309-1363.
    card_.visible = true;
    card_.y = 192;
    card_.value = picaratValue(int(session_.first.failures()) - 1);
    if (auto s = music()) {
        s->playStream(0, 10, -1, 1, 0, false);
    }
    frame();
    frame();
    frame();
    for (int i = 0; i < 16; ++i) {
        ++card_.brightness;
        frame();
    }
    for (int i = 0; i < 0x16; ++i) {
        frame();
    }
    for (int y = 0; y < 0xc0; y += 0x10) {
        card_.y = 192 - y;
        frame();
    }
    card_.y = 0;
    card_.title = true;
    frame();
    // 020f0860: the lower screen shows the puzzle's worth; after a failure it
    // counts down from the previous worth with GE 0x77 (020f0860, the same
    // stepping as the reward count). It ends on a touch release or button.
    card_.lower = true;
    for (int i = 0; i < 24; ++i) {
        frame();
    }
    for (int i = 0; i < 16; ++i) {
        ++card_.lowerBrightness;
        frame();
    }
    {
        unsigned shown = card_.value;
        const unsigned target = picaratValue(int(session_.first.failures()));
        const int difference = int(shown) - int(target);
        if (difference > 0) {
            effect(0x77);
        }
        bool done = false;
        int elapsed = 0;
        do {
            if (target < shown) {
                --shown;
            }
            const int next = elapsed + 1;
            if (target < shown) {
                if (difference < 0xf) {
                    frame();
                }
                if (difference > 0x18) {
                    --shown;
                }
            }
            if (next > 3) {
                if (touch_.released) {
                    done = true;
                    shown = target;
                }
                if (shown == target) {
                    if (auto s = music()) {
                        s->stopEffect(0x77, 0);
                    }
                }
            }
            card_.value = shown;
            frame();
            elapsed = next;
        } while (!done);
    }
    // The card fades both screens out before the puzzle scene (frames 1448-1463).
    for (int i = 0; i < 16; ++i) {
        --card_.brightness;
        --card_.lowerBrightness;
        frame();
    }
    card_.visible = false;
    for (int i = 0; i < 10; ++i) {
        frame(); // ROM 1463 -> 1474: the puzzle scene is built
    }
    screenBrightness_ = std::min(brightness, -16);
    scene_ = previous;
}

// The puzzle's worth after `failures` wrong answers.
unsigned PuzzleGame::picaratValue(int failures) const {
    return failures < 1   ? data_.picarats[0]
           : failures < 2 ? data_.picarats[std::size_t(failures)]
                          : data_.picarats[2];
}

// Draws a number with digit sprites (overlay 2, 020722fc).
void PuzzleGame::drawNumber(Surface& image, const Sprite& sprite, const std::string& variable,
                            unsigned value, bool pad, int dx, int dy) {
    // ov2 020722fc: digits right to left from the variable position, stepping
    // by its spacing plane, up to its maximum plane (default 99); digit d is
    // animation d+1. Leading zeros only when padded.
    const auto& v = sprite.variables.at(variable);
    const int maximum = v.size() > 5 && v[5] ? v[5] : 99;
    if (maximum < 2) {
        return;
    }
    int x = v[0];
    const int y = v[1], step = v.size() > 2 ? v[2] : 0;
    for (long place = 1, next = 10; place < maximum; place *= 10, next *= 10) {
        const unsigned digit = unsigned((value / place) % 10);
        if (place == 1 || digit > 0 || pad || long(value) >= next) {
            const auto& a = sprite.animations.at(digit + 1);
            blit(image, sprite.images.at(a.at(0).image), x + dx, y + dy);
        }
        x -= step;
    }
}

// Draws the title card and, below it, the worth in picarats.
void PuzzleGame::renderCard(Surface& image) {
    std::fill(image.pixels.begin(), image.pixels.end(), 0x8000);
    // Card surface: nazo_title<theme> with the title sprite (020f0108/020f0080).
    Surface card{256, 192, std::vector<std::uint16_t>(256 * 192, 0x8000)};
    blit(card, assets_.background("nazo/system/nazo_title" + std::to_string(data_.theme)), 0, 0);
    const auto& s = assets_.sprite("nazo/system/ge/nazo_title.arc");
    auto at = [&](const char* name, const char* variable) {
        const auto& p = s.variables.at(variable);
        drawSprite(card, s, name, p[0], p[1]);
    };
    at("nazo", "pos2");
    at("puzzle", "pos3");
    if (internalId_ == 0xce) {
        for (int x : {0xb9, 0x9b, 0x7d}) {
            drawSprite(card, s, "question", x, 0x32);
        }
    } else if (data_.number > 0x99) {
        drawSprite(card, s, "w", 0x73, 0x32);
        if (launch_.specialNumber) {
            const unsigned n = *launch_.specialNumber;
            blit(card, s.images.at(s.animations.at(n / 10 % 10 + 1).at(0).image), 0x9b, 0x32);
            blit(card, s.images.at(s.animations.at(n % 10 + 1).at(0).image), 0xb9, 0x32);
        } else {
            for (int x : {0x9b, 0xb9}) {
                drawSprite(card, s, "question", x, 0x32);
            }
        }
    } else {
        drawNumber(card, s, "pos", data_.number, true, 0, 0);
    }
    if (card_.title) {
        // 020f04a8 -> 02072e34/02072bc0: event font, centred on 128 at "title".
        const auto& font = assets_.font("fontevent.NFTR");
        int width = 0;
        for (unsigned char c : data_.title) {
            width += textAdvance(font, c) + 1;
        }
        textLines(card, font, data_.title, 128 - (width >> 1), s.variables.at("title")[1], 16);
    }
    for (int y = 0; y < 192; ++y) {
        const int dy = card_.y + y;
        if (dy < 0 || dy >= 384) {
            continue;
        }
        std::copy_n(card.pixels.begin() + y * 256, 256, image.pixels.begin() + dy * 256);
    }
    if (card_.lower) {
        // 020f0514: worth (picaratBig), maximum (picaratTotal), total picarats (picaratSmall).
        Surface lower{256, 192, std::vector<std::uint16_t>(256 * 192, 0x8000)};
        blit(lower, assets_.background("nazo/system/ge/picarat_bg"), 0, 0);
        drawNumber(lower, assets_.sprite("nazo/system/picaratBig.arc"), "pos", card_.value, false,
                   0, 0);
        drawNumber(lower, assets_.sprite("nazo/system/picaratTotal.arc"), "pos", data_.picarats[0],
                   false, 0, 0);
        drawNumber(lower, assets_.sprite("nazo/system/picaratSmall.arc"), "pos", session_.picarats,
                   false, 0, 0);
        applyBrightness(lower, card_.lowerBrightness, card_.lowerBrightness);
        std::copy(lower.pixels.begin(), lower.pixels.end(), image.pixels.begin() + 192 * 256);
    }
    applyBrightness(image, card_.brightness, card_.lower ? 0 : card_.brightness);
}

// Plays the sound effects the engines asked for, in order.
void PuzzleGame::drainSounds() {
    // Engine calls to 0207f8f0/0207f918, in the order the engines made them.
    auto drain = [&](EffectQueue& q) {
        for (const auto& r : q.take()) {
            if (auto s = music()) {
                if (r.volume >= 0) {
                    s->bgmVolume(r.volume, r.seconds);
                } else if (r.stop) {
                    s->stopEffect(r.id, r.seconds);
                } else {
                    s->playEffect(r.id);
                }
            }
        }
    };
    if (tiles_) {
        drain(tiles_->sounds);
    }
    if (lines_) {
        drain(lines_->sounds);
    }
    if (toggles_) {
        drain(toggles_->sounds);
    }
    if (roses_) {
        drain(roses_->sounds);
    }
    if (lamps_) {
        drain(lamps_->sounds);
    }
    if (couples_) {
        drain(couples_->sounds);
    }
    if (bridge_) {
        drain(bridge_->sounds);
    }
    if (slots_) {
        drain(slots_->sounds);
    }
    if (skate_) {
        drain(skate_->sounds);
    }
    if (slides_) {
        drain(slides_->sounds);
    }
    if (selection_) {
        drain(selection_->sounds);
    }
    if (knight_) {
        drain(knight_->sounds);
    }
    if (pegs_) {
        drain(pegs_->sounds);
    }
    if (colors_) {
        drain(colors_->sounds);
    }
    if (towers_) {
        drain(towers_->sounds);
    }
}

// Stops engine sounds when the puzzle ends.
void PuzzleGame::shutdownSounds() {
    // Engine destructors run before the scene destructor's music fade.
    if (lines_) {
        lines_->shutdownSounds();
    }
    drainSounds();
}

// Starts the puzzle music.
void PuzzleGame::startMusic() {
    // ov27 020f2650 (scene constructor): stop the previous music over time id
    // 1003, then the room-sound row at puzzle +0x3c. 020f34a8 selects the
    // alternative for internal 0x87 unless the following scene (+0x479) is
    // 0x13/0x16/0x1f: fade out, row 0x456, then restart at full volume.
    auto s = music();
    if (!s) {
        return;
    }
    if (!specialMusic()) {
        s->stopBgm(s->duration(0x3eb));
        s->roomSound(int(data_.soundId), true);
    } else {
        s->fadeOutBgm(s->duration(0x3eb));
        s->roomSound(0x456, true);
        s->restartBgm(1.0f, 0);
    }
}

// Plays the puzzle. Returns true when it was solved, false when the player
// quit it ("later").
bool PuzzleGame::run() {
    session_.first.enter();
    session_.lastJudgedCorrect = false; // ov27 020f26c8
    updateHintButton();
    if (auto s = music()) {
        s->stopStream(0);
        s->stopUniqueEffect(0);
    } // ov2 020697e0 puzzle-mode entry
    introCard();
    startMusic();
    drainSounds(); // engine construction (e.g. ov49 reset 020f7a28) follows the room sound
    soundsLive_ = true;
    // The question: 020f4be8 fades in (02071790), then each frame runs the question
    // reveal 020f37e0 (virtual +1f8) until a release. The reveal waits for the
    // fade object (02088a10) to finish, then advances two characters of the
    // expanded prompt per frame and plays SY 0x29 whenever the count is a
    // multiple of 5; a new touch reveals everything. The text itself is still
    // drawn complete natively (G10).
    screenBrightness_ = -16;
    {
        int shown = 0;
        bool done = false;
        do {
            const bool fading = screenBrightness_ < 0;
            if (!fading && !done) {
                if (shown % 5 == 0) {
                    effect(0x29);
                }
                if (touch_.pressed || shown >= int(data_.prompt.size())) {
                    done = true;
                } else {
                    shown += 2;
                }
            }
            frame();
            if (fading) {
                ++screenBrightness_;
            }
        } while (!touch_.released);
    }
    // Tutorial pages (1..3, from the puzzle record), each shown once per save:
    // 020f5314 plays sound 3 when it opens the tutorial; 020f4be8 then plays
    // sound 0xd.
    if (data_.tutorial == 1 || data_.tutorial == 2 || data_.tutorial == 3) {
        // 020f5314: shown only while its save bit is clear (02082f58), then set (02082f7c).
        const auto bit = std::uint8_t(1u << (data_.tutorial - 1));
        if (!(session_.tutorialsSeen & bit)) {
            effect(3);
            tutorial_ = true;
            waitRelease();
            tutorial_ = false;
            session_.tutorialsSeen |= bit;
        }
    } else if (data_.tutorial != 0) {
        throw std::runtime_error("unrecovered puzzle tutorial");
    }
    effect(0xd);
    // The play loop.
    scene_ = SceneKind::Puzzle;
    for (;;) {
        frame();
        // 1. The engine takes the input.
        if (tiles_) {
            tiles_->update(touch_, microphoneRange_);
        }
        if (towers_) {
            towers_->update(touch_);
        }
        if (pegs_) {
            pegs_->update(touch_);
        }
        if (knight_) {
            knight_->update(touch_);
        }
        if (colors_) {
            colors_->update(touch_, tick_);
        }
        if (selection_) {
            selection_->update(touch_);
        }
        if (choices_) {
            choices_->update(touch_);
        }
        if (answers_) {
            answerFrame(true);
        }
        // ov37 020fa12c: 020f3be0 takes input only while +4f3 is clear, then 020f83d0.
        const bool commonInput =
            (!lines_ || lines_->commonInput()) && (!slides_ || slides_->commonInput()) &&
            (!skate_ || skate_->commonInput()) && (!couples_ || couples_->commonInput()) &&
            (!bridge_ || bridge_->commonInput());
        if (lines_) {
            lines_->input(touchFilter_);
        }
        // ov47 020f7c5c: update (020f8734), then easing and drawing (020f8a78).
        if (slides_) {
            slides_->update(touchFilter_);
            slides_->animate();
        }
        if (toggles_) {
            toggles_->update(touchFilter_); // ov45 020f7b8c -> 020f7d48
        }
        if (roses_) {
            roses_->update(touchFilter_); // ov46 020f7e78 -> 020f7fa4
        }
        if (lamps_) {
            lamps_->update(touchFilter_);
            lamps_->tick();
        } // ov52 020f7ad8
        if (skate_) {
            skate_->update(touchFilter_); // ov49 020f7b14 -> 020f7c84
        }
        if (couples_) {
            couples_->update(touchFilter_); // ov51 020f7af8 -> 020f7b60
        }
        if (bridge_) {
            bridge_->update(touchFilter_); // ov53 020f7a54 -> 020f7c2c
        }
        if (slots_) {
            // ov39 020f7eb8: front-first pick via the sprite hit path
            // (02075f44 -> 0207430c, origin -2, inclusive bounds).
            slots_->update(touchFilter_, [&](int i) {
                const auto& tile = slots_->tiles()[std::size_t(i)];
                const auto& f = slotFrame(tile);
                return Rect{tile.x - 2, tile.y - 2, int(f.width), int(f.height)}.contains(
                    touchFilter_.x, touchFilter_.y);
            });
        }
        if (traces_) {
            // ov34 020f7b04 / ov54 020f7cd4: stroke input with ink and mask.
            traces_->update(touchFilter_);
            if (traces_->clearInk()) {
                traceInk_.clear();
            }
            for (const auto& c : traces_->takeInk()) {
                traceInk_.apply(c, assets_.font("fontq.NFTR"), 0);
            }
        }
        // 2. The common buttons, only while the engine is idle (020f8ac4).
        if ((!pegs_ || pegs_->state == 0) && (!knight_ || knight_->state != 2) &&
            (!tiles_ || !tiles_->dragging()) && (!towers_ || towers_->state() != 2) &&
            commonInput) {
            for (auto* c : {&answer_, &reset_, &hint_, &memo_, &later_}) {
                if (controlAvailable(c)) {
                    c->button.update(touch_);
                }
            }
        }
        if (tiles_) {
            tiles_->animate();
        }
        if (towers_) {
            towers_->animate();
        }
        drainSounds();
        // 3. Their actions: reset, hints, memo, quit.
        if (reset_.button.consumeClick()) {
            resetEngine(); // 020f4e40 calls virtual +204
        }
        // ov27 020f4570/020f47d0/020f4a78 call virtual +20c after the sub-screen.
        if (hint_.button.consumeClick()) {
            hint();
            if (lines_) {
                lines_->cancel();
            }
        }
        if (memo_.button.consumeClick()) {
            memo();
            if (lines_) {
                lines_->cancel();
            }
        }
        if (later_.button.consumeClick()) {
            giveUp_ = true;
            bool leave = false;
            for (;;) {
                frame();
                giveYes_.button.update(touch_);
                giveNo_.button.update(touch_);
                if (giveYes_.button.consumeClick()) {
                    leave = true;
                    break;
                }
                if (giveNo_.button.consumeClick()) {
                    break;
                }
            }
            giveUp_ = false;
            // 020f3248 (+0x55 set): engine teardown, then the music stops over time id 1004.
            if (leave) {
                shutdownSounds();
                if (auto s = music()) {
                    if (specialMusic()) {
                        s->bgmVolume(1, s->duration(0x3ec));
                    } else {
                        s->stopBgm(s->duration(0x3ec));
                    }
                }
                return false;
            }
            if (lines_) {
                lines_->cancel();
            }
        }
        if (circleParameters_) {
            // ov33 020f8014: wrap picture index and reset the drawing on change.
            bool consumed = false;
            if (circleParameters_->groupCount() > 1) {
                circleLeft_.button.update(touch_);
                circleRight_.button.update(touch_);
                if (circleLeft_.button.consumeClick()) {
                    circlePage_ = (circlePage_ + circleParameters_->groupCount() - 1) %
                                  circleParameters_->groupCount();
                    resetEngine();
                    consumed = true;
                } else if (circleRight_.button.consumeClick()) {
                    circlePage_ = (circlePage_ + 1) % circleParameters_->groupCount();
                    resetEngine();
                    consumed = true;
                }
                consumed = consumed || circleLeft_.button.state == Button::State::Armed ||
                           circleRight_.button.state == Button::State::Armed;
            }
            if (!consumed) {
                updateCircle();
            }
        }
        // 4. Judge when the player submits, or when an engine that judges
        //    itself is done.
        const auto choiceAnswer = choices_ ? choices_->consume() : std::optional<bool>{};
        const bool answerJudge = answers_ && updateAnswers();
        bool answerPressed = false;
        if (answerJudge || choiceAnswer.has_value() ||
            (answerPressed = answer_.button.consumeClick()) ||
            (tiles_ && tiles_->automatic() && tiles_->solved()) || (towers_ && towers_->solved()) ||
            (knight_ && knight_->solved()) || (pegs_ && pegs_->solved()) ||
            (slides_ && slides_->complete()) || (roses_ && roses_->solved()) ||
            (skate_ && skate_->solved()) || (couples_ && couples_->complete()) ||
            (bridge_ && bridge_->complete())) {
            // ov33 020f7d68 rejects invalid/empty drawings without running judgment.
            if (circleParameters_ && (circle_.stroke.invalid || circle_.stroke.markerX <= 2 ||
                                      circle_.stroke.markerY < 0)) {
                circle_.stroke.invalid = true;
                continue;
            }
            // ov27 020f4e40 virtual +1fc -> ov48 020f8a48/020f8928.
            const bool solved = slots_       ? slots_->solved()
                                : traces_    ? traces_->solved()
                                : bridge_    ? bridge_->complete()
                                : couples_   ? couples_->complete()
                                : skate_     ? skate_->solved()
                                : lamps_     ? lamps_->solved()
                                : roses_     ? roses_->solved()
                                : toggles_   ? toggles_->solved()
                                : slides_    ? slides_->complete()
                                : lines_     ? lines_->solved()
                                : answers_   ? answers_->solved()
                                : tiles_     ? tiles_->solved()
                                : towers_    ? towers_->solved()
                                : knight_    ? knight_->solved()
                                : pegs_      ? pegs_->solved()
                                : selection_ ? selection_->solved()
                                : colors_    ? colors_->solved()
                                : choices_   ? choiceAnswer.value_or(false)
                                             : circle_.stroke.solved();
            // ov52 virtual +210 (020f7b10): 60 frames of every lit street.
            // ov27 020f4e40: the answer button plays GE 0x6f (type 34 also
            // fades the music there); otherwise the scene destructor 020f3248
            // fades it to 0 over time id 1004 before judgment.
            if (auto s = music()) {
                if (answerPressed || choiceAnswer.has_value()) {
                    s->playEffect(0x6f);
                }
            }
            shutdownSounds();
            if (auto s = music()) {
                s->bgmVolume(0, s->duration(0x3ec));
            }
            if (lamps_) {
                lamps_->showAll();
                for (int i = 0; i < 0x3c; ++i) {
                    lamps_->tick();
                    frame();
                }
            }
            session_.lastJudgedCorrect = solved; // ov27 020f5010 / 020f5018
            judgment(!solved);
            // 5a. Correct: count the reward up, show the answer, done.
            if (solved) {
                countTotal_ = session_.picarats;
                if (session_.award(internalId_, launch_.followingScene, data_.picarats,
                                   countRemaining_)) {
                    scene_ = SceneKind::PuzzleReward;
                    frame();
                    if (countRemaining_ != 0) {
                        effect(0x77); // ov27 020f1308 count sound
                    }
                    countPicarats(countTotal_, countRemaining_, *this);
                }
                // 020f1948: wait while the tracked outcome voice plays (0207f9c0),
                // then restore the music over time id 1201.
                while (music() && music()->trackedBusy()) {
                    frame();
                }
                if (auto s = music()) {
                    s->bgmVolume(1, s->duration(0x4b1));
                }
                scene_ = SceneKind::PuzzleSolved;
                waitRelease();
                replayResult();
                session_.first.setStatus(2);
                session_.completionValue = data_.completionValue;
                scene_ = SceneKind::Complete;
                frame();
                return true;
            }
            // 5b. Wrong: show why, count the failure, and start again from the
            //     title card (which now shows the reduced worth).
            scene_ = SceneKind::PuzzleWrong;
            retryMenu_ = false;
            waitRelease();
            retryMenu_ = true;
            do {
                frame();
                retry_.button.update(touch_);
            } while (!retry_.button.consumeClick());
            // ov27 020f251c increments failures after the wrong-result screen.
            session_.first.finishFailure();
            retryMenu_ = false;
            scene_ = SceneKind::Puzzle;
            introCard();
            startMusic(); // The restarted puzzle reruns the card and 020f2650.
            // The ROM restarts the puzzle (intro card replay remains G10); ov42
            // is reconstructed, so the question screen and empty boxes return.
            if (lines_) {
                createLines();
            }
            if (lamps_) {
                lamps_->retry(); // +230 restores lamps and lit streets.
            }
            if (answers_) {
                answers_->restart();
                answerFrame(false);
                for (auto* c : {&kaito_, &hantei_, &allreset_, &modoru_, &pictureToggle_}) {
                    c->button = Button(c->button.bounds, c->button.sound);
                }
                for (auto& e : erasers_) {
                    e.button = Button(e.button.bounds, e.button.sound);
                }
            }
        }
        if (selection_) {
            selection_->consume();
            drainSounds();
        }
    }
}
} // namespace picarat
