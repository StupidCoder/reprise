// The story: the scene router and the room and event scenes.
//
// Once the opening is over, the game runs one loop for the rest of its life
// (overlay 2, 02068ccc): look at the current scene number in the game state
// and run that scene. Scenes hand over by writing the next scene number;
// most also set a "following" scene for after a sub-scene. The main ones:
//
//   Room (1)        exploring a place: tap people and objects, collect hint
//                   coins, switch to "move" mode to walk through exits
//                   (overlay 7). Before a room is shown, an automatic event
//                   for the place may run first.
//   Event (3)       a story event: its script moves characters, shows
//                   dialogue, starts puzzles, movies, minigames, sets flags
//                   and decides what comes next (overlay 8).
//   Puzzle (10)     one puzzle (a2_puzzle_game.cpp), then the puzzle result (8),
//                   which usually leads into a follow-up event.
//   Movie (6), Narration (13), the bag and its pages (17, 19, 23, 43),
//   minigames (14, 15, 16, 25..27), the staff roll (21), the shack (44)...
//
// After each change of place the story step is advanced and the place's
// version recomputed (a2_game_state.cpp), which is how the world changes as
// the story progresses. The game saves checkpoints at room entries and at
// the start of events and puzzles.
#include "story/a2_story_game.hpp"
#include "menus/a2_bag_menu.hpp"
#include "menus/a2_bonus_menus.hpp"
#include "menus/a2_challenge_menu.hpp"
#include "menus/a2_diary_menu.hpp"
#include "menus/a2_file_dialog.hpp"
#include "menus/a2_mystery_menu.hpp"
#include "menus/a2_nazoba_menu.hpp"
#include "menus/a2_notes_menu.hpp"
#include "menus/a2_puzzle_index.hpp"
#include "minigames/a2_camera_game.hpp"
#include "minigames/a2_hamster_game.hpp"
#include "minigames/a2_photo_game.hpp"
#include "minigames/a2_tea_game.hpp"
#include "puzzles/judgment.hpp"
#include "puzzles/a2_puzzle_game.hpp"
#include "story/a2_staff_roll.hpp"
#include "system/script.hpp"
#include "video/movie.hpp"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <stdexcept>

namespace picarat {
namespace {
std::string number(int n, int width = 0) {
    char b[40];
    std::snprintf(b, sizeof b, "%0*d", width, n);
    return b;
}

std::string eventName(int id) {
    return number(id / 1000) + "_" + number(id % 1000, 3);
}

void mirrored(Surface& dst, const Surface& source, int x, int y, bool flip) {
    if (!flip) {
        blit(dst, source, x, y);
        return;
    }
    Surface copy = source;
    for (unsigned yy = 0; yy < copy.height; ++yy) {
        std::reverse(copy.pixels.begin() + yy * copy.width,
                     copy.pixels.begin() + (yy + 1) * copy.width);
    }
    blit(dst, copy, x, y);
}

std::string localized(std::string name) {
    auto p = name.find('?');
    if (p != name.npos) {
        name.replace(p, 1, "ge");
    }
    if (name.ends_with(".bgx")) {
        name.resize(name.size() - 4);
    }
    return name;
}

const char* sceneLabel(unsigned id) {
    switch (id) {
    case scene::NewGame:
        return "new game";
    case scene::Room:
        return "room";
    case scene::DramaEvent:
        return "drama event";
    case scene::Movie:
        return "movie";
    case scene::PuzzleResult:
        return "puzzle result";
    case scene::Puzzle:
        return "puzzle";
    case scene::Title:
        return "title";
    case scene::Narration:
        return "narration";
    case scene::SubCamera:
        return "sub camera";
    case scene::SubHerb:
        return "sub herb";
    case scene::SubHam:
        return "sub ham";
    case scene::Menu:
        return "menu";
    case scene::Staff:
        return "staff";
    case scene::Challenge:
        return "challenge";
    case scene::Name:
        return "name";
    case scene::Passcode:
        return "passcode";
    case scene::Diary:
        return "diary";
    case scene::Nazoba:
        return "nazoba";
    default:
        return "unnamed";
    }
}
} // namespace

StoryGame::StoryGame(GraphicsAssets& a, PresentFrame p, NameEditor n, AudioService* audio,
                     CheckpointStore* saves)
    : assets_(a), present_(std::move(p)), name_(std::move(n)), audio_(audio), text_(a.root),
      state_(a.root), saves_(saves) {}

// The present callback handed to sub-scenes (bag, menus, minigames): it
// shows the frame and leaves the sub-scene when the player asks to load the
// saved checkpoint.
PresentFrame StoryGame::subscenePresent() {
    return [this](const Surface& image, const GameSnapshot& snapshot) {
        auto input = present_(image, snapshot);
        if (input.load && saves_ && saves_->load()) {
            throw LoadCheckpoint{};
        }
        return input;
    };
}

std::string StoryGame::loadText(const std::string& name) {
    auto b = readFile(assets_.root / "text" / name);
    return text_.expand({reinterpret_cast<const char*>(b.data()), b.size()});
}

void StoryGame::frame() {
    GameSnapshot state{scene_, name_, tick_, detail_};
    state.storyBoundary = !boundary_.empty();
    state.boundary = boundary_;
    if (state_.puzzle.number > 0) {
        state.puzzleProgress = state_.progress.puzzleByte(std::uint32_t(state_.puzzle.number - 1));
    }
    state.coins = state_.progress.hintCoins();
    state.picarats = state_.progress.picarats();
    state.place = state_.progress.place();
    state.storyStep = state_.progress.storyStep();
    state.skippable = skippable_;
    if (exploring_) {
        state.touchAreas = roomAreas();
    }
    auto in = present_(render(), state);
    sleep_ = in.sleep;
    if (auto s = sound()) {
        s->frame(); // ov2 0206cb10 -> 0207faf4 once per main-loop frame
    }
    if (in.load && saves_ && saves_->load()) {
        throw LoadCheckpoint{};
    }
    if (in.restart) {
        throw RestartGame{};
    }
    if (in.puzzle) {
        throw JumpToPuzzle{};
    }
    // The DS keeps the last sampled stylus coordinate for a release. Host
    // "no contact" frames need not invent an in-range position.
    int x = in.down ? in.x : touch_.x, y = in.down ? in.y : touch_.y;
    touch_ = {in.down, in.down && !wasDown_, !in.down && wasDown_, x, y};
    wasDown_ = in.down;
    skip_ = in.skip;
    ++tick_;
}

void StoryGame::wait(int count) {
    if (count < 0 || count > 36000) {
        throw std::runtime_error("story wait bound");
    }
    while (count-- > 0) {
        frame();
    }
}

void StoryGame::waitRelease() {
    do {
        frame();
    } while (!touch_.released);
}

void StoryGame::fade(bool in, int count, int screens) {
    if (count < 1 || count > 36000) {
        throw std::runtime_error("story fade bound");
    }
    for (int i = 0; i < count; ++i) {
        int v = in ? -16 + 16 * i / count : -16 * i / count;
        if (screens & 1) {
            bottomBrightness_ = v;
        }
        if (screens & 2) {
            topBrightness_ = v;
        }
        frame();
    }
    if (screens & 1) {
        bottomBrightness_ = in ? 0 : -16;
    }
    if (screens & 2) {
        topBrightness_ = in ? 0 : -16;
    }
}

void StoryGame::stopAtBoundary(const std::string& reason) {
    // Explicit end of the reconstructed coverage. The last checkpoint was
    // written at the preceding stable boundary; nothing is substituted.
    boundary_ = reason;
    scene_ = SceneKind::Complete;
    detail_ = state_.eventId;
    topBrightness_ = bottomBrightness_ = 0;
    for (;;) {
        frame();
    }
}

void StoryGame::drawSprite(Surface& image, const std::string& path, const std::string& animation,
                           int x, int y) {
    const auto& s = assets_.sprite(path);
    auto index = s.animationNames.at(animation);
    if (s.animations.at(index).empty()) {
        return;
    }
    blit(image, s.images.at(s.animations[index][0].image), x, y);
}

void StoryGame::drawSpriteFrame(Surface& image, const Sprite& s, AnimationPlayer& player, int x,
                                int y) {
    if (player.animation >= s.animations.size() || s.animations[player.animation].empty()) {
        return;
    }
    blit(image, s.images.at(s.animations[player.animation][player.step].image), x, y);
    player.advance(tick_, s.animations);
}

const Sprite* StoryGame::eventCharacter(int id) {
    // ov8 020f0650: eventchr/chr%i.spr. Some IDs have no file in the ROM
    // (e.g. 86 in event 17230); the actor slot then exists without graphics.
    const auto path = "eventchr/chr" + number(id) + ".arc";
    if (!fileExists(assets_.root / "ani" / (path + ".l2sp"))) {
        return nullptr;
    }
    return &assets_.sprite(path);
}

void StoryGame::selectActor(Actor& a, unsigned index) {
    if (!a.sprite) {
        return;
    }
    a.body.select(index, tick_, a.sprite->animations);
    if (!a.sprite->child.empty()) {
        const auto& child = assets_.sprite(a.sprite->child);
        auto ci = a.sprite->attachments.at(index).animation;
        if (ci && ci < child.animations.size()) {
            a.child.select(ci, tick_, child.animations);
        }
    }
}

void StoryGame::actorAnimation(int id, const std::string& name) {
    if (name == "NONE") {
        return;
    }
    // ov2 02077444: an unknown animation name leaves the current one.
    for (auto& a : actors_) {
        if (a.id == id && a.sprite) {
            auto found = a.sprite->animationNames.find(name);
            if (found == a.sprite->animationNames.end()) {
                continue;
            }
            selectActor(a, found->second);
            if (id == talkSpeaker_) {
                talkSaved_ = found->second;
            }
        } // 0x0207d150 records it as the animation to restore
    }
}

void StoryGame::talk(bool on) { // 0x0207d1d4
    Actor* actor = nullptr;
    for (auto& a : actors_) {
        if (a.id == talkSpeaker_ && talkSpeaker_ > 0) {
            actor = &a;
            break;
        }
    }
    if (!actor || !actor->sprite) {
        return;
    }
    if (on && !talking_) {
        // 0x0207736c: the next animation index, kept only if it exists.
        talkSaved_ = actor->body.animation;
        const unsigned next = talkSaved_ + 1;
        if (next < actor->sprite->animations.size()) {
            selectActor(*actor, next);
        }
        talking_ = true;
    } else if (!on && talking_) {
        if (actor->body.animation != talkSaved_) {
            selectActor(*actor, talkSaved_);
        }
        talking_ = false;
    }
}

void StoryGame::pageWait(int voice) { // 0x02080e44
    // While a voice is set the mouth follows the stream's level meter
    // (0x0207fb34); the page ends on a touch once no tracked sound is busy.
    for (;;) {
        auto s = sound();
        const bool tracked = s && s->trackedBusy();
        const bool loud = s && s->streamLoud();
        if (voice >= 0) {
            talk(loud);
        }
        frame();
        if (!tracked && touch_.released) {
            talk(false);
            return;
        }
    }
}

namespace {
// Actor polygons blend with the DS 3D alpha formula (blend3D) below alpha 31.
void actorBlit(Surface& dst, const Surface& source, int x, int y, bool flip, int alpha) {
    if (alpha >= 31) {
        mirrored(dst, source, x, y, flip);
        return;
    }
    for (unsigned yy = 0; yy < source.height; ++yy) {
        for (unsigned xx = 0; xx < source.width; ++xx) {
            const auto p = source.pixels[yy * source.width + (flip ? source.width - 1 - xx : xx)];
            if (!(p & 0x8000)) {
                continue;
            }
            const int dx = x + int(xx), dy = y + int(yy);
            if (dx < 0 || dy < 0 || dx >= int(dst.width) || dy >= int(dst.height)) {
                continue;
            }
            auto& d = dst.pixels[std::size_t(dy) * dst.width + std::size_t(dx)];
            d = blend3D(p, alpha, d);
        }
    }
}
} // namespace

void StoryGame::drawActor(Surface& image, Actor& a) {
    // 02081e90: alpha += speed (binary32), clamped to [0,31]; drawn only
    // while visible and the truncated alpha exceeds 1.
    a.alpha += a.speed;
    if (a.alpha > 31.0f) {
        a.alpha = 31.0f;
    } else if (a.alpha < 0.0f) {
        a.alpha = 0.0f;
    }
    const int alpha = int(a.alpha);
    if (!a.visible || alpha <= 1 || !a.sprite ||
        a.sprite->animations.at(a.body.animation).empty()) {
        return;
    }
    // ov2 02081cf8 position literals and 02077a8c child mirroring. Draw then
    // step both players; animation time remains in native 60-Hz game frames.
    static constexpr int centers[]{48, 128, 208, 32, 88, 167, 224};
    const auto& s = *a.sprite;
    const auto& picture = s.images.at(s.animations[a.body.animation][a.body.step].image);
    if (a.position < 0 || a.position >= 7) {
        throw std::runtime_error("actor position");
    }
    bool flip = a.position == 0 || a.position == 3 || a.position == 4;
    int x = centers[a.position] - int(picture.width) / 2, y = 384 - int(picture.height);
    // ov2 02081e90 puts signed drawoff into the parent origin (+48/+4a).
    // Both 0207795c and 02077a8c attach the face to that adjusted origin.
    // 02076934 leaves the zero-initialized offsets when the sprite has none.
    if (const auto off = s.variables.find("drawoff"); off != s.variables.end()) {
        x += flip ? -off->second[0] : off->second[0];
        y += off->second[1];
    }
    // 02081e90: while the shake counter (+0xc, script 0x7e) is positive, two
    // RNG draws give offsets r%3-1 on x and y and the counter decreases.
    if (a.shake > 0) {
        const int dx = int(gameRandom(randomState_) % 3) - 1,
                  dy = int(gameRandom(randomState_) % 3) - 1;
        --a.shake;
        x += dx;
        y += dy;
    }
    actorBlit(image, picture, x, y, flip, alpha);
    if (!s.child.empty() && s.attachments[a.body.animation].animation) {
        const auto& child = assets_.sprite(s.child);
        const auto& p = s.attachments[a.body.animation];
        const auto& c =
            child.images.at(child.animations.at(a.child.animation).at(a.child.step).image);
        actorBlit(image, c, x + (flip ? int(picture.width) - p.x - int(c.width) : p.x), y + p.y,
                  flip, alpha);
        a.child.advance(tick_, child.animations);
    }
    a.body.advance(tick_, s.animations);
}

// Draws both screens for rooms and events: backgrounds, the characters on
// stage, the text window and room widgets.
Surface StoryGame::render() {
    Surface image{256, 384, std::vector<std::uint16_t>(256 * 384, 0x8000)};
    // Background shake: while a counter runs, two RNG draws per frame and
    // the BG3 scroll (0x0400001c / 0x0400101c) moves by d=(r%3)-1 on both axes.
    auto shake = [&](int& counter) {
        if (counter <= 0) {
            return 0;
        }
        gameRandom(randomState_);
        const int r = int(gameRandom(randomState_));
        --counter;
        int d = r % 3 - 1;
        if (d == 3) {
            d = 0;
        }
        return d;
    };
    const int subShake = shake(shakeSub_), mainShake = shake(shakeMain_);
    if (!top_.empty()) {
        blit(image, assets_.background(top_), -subShake, -subShake);
    }
    if (!bottom_.empty()) {
        Surface lower{256, 192, std::vector<std::uint16_t>(256 * 192, 0x8000)};
        blit(lower, assets_.background(bottom_), -mainShake, -mainShake);
        blit(image, lower, 0, 192);
    }
    const auto& font = assets_.font("fontevent.NFTR");
    if (roomVisible_) {
        // ov7 020f08e4 / 020f1b60: background animations, event objects, exits
        // in move mode, map marker and goal. Native layering; G03.
        for (auto& a : room_.animations) {
            drawSpriteFrame(image, assets_.sprite("bgani/" + a.name + ".arc"), a.animation, a.x,
                            192 + a.y);
        }
        for (auto& e : room_.events) {
            if (e.visible && e.sprite) {
                drawSpriteFrame(image, assets_.sprite("eventobj/obj_" + number(e.sprite) + ".arc"),
                                e.animation, e.x, 192 + e.y);
            }
        }
        const auto& pos = assets_.sprite("map/movemode.arc").variables.at("pos");
        drawSprite(image, "map/movemode.arc", moveMode_ ? "on" : "off", pos[0], 192 + pos[1]);
        {
            const auto& m = assets_.sprite("map/menu_icon.arc").variables.at("pos");
            const char* a = menuButton_.state == Button::State::Clicked ? "click"
                            : menuButton_.state == Button::State::Armed && menuButton_.inside
                                ? "on"
                                : "off";
            drawSprite(image, "map/menu_icon.arc", a, m[0], 192 + m[1]);
        }
        if (cameraVisible_) {
            const auto& m = assets_.sprite("map/camera_icon.arc").variables.at("pos");
            const char* a = cameraButton_.state == Button::State::Clicked ? "click"
                            : cameraButton_.state == Button::State::Armed && cameraButton_.inside
                                ? "on"
                                : "off";
            drawSprite(image, "map/camera_icon.arc", a, m[0], 192 + m[1]);
        }
        if (moveMode_) {
            for (std::size_t i = 0; i < room_.exits.size(); ++i) {
                const auto& e = room_.exits[i];
                const auto& s = assets_.sprite("map/exit_" + number(e.sprite) + ".arc");
                const auto anim = s.animationNames.count(int(i) == selectedExit_ ? "2" : "gfx")
                                      ? (int(i) == selectedExit_ ? "2" : "gfx")
                                  : s.animations.size() > 1 ? s.animationNames.begin()->first
                                                            : "gfx";
                const auto& steps = s.animations.at(s.animationNames.at(anim));
                if (steps.empty()) {
                    continue;
                }
                const auto& picture = s.images.at(steps[0].image);
                int x = e.x, y = e.y;
                if (!e.sprite) {
                    x = e.x + (e.w >> 1) - int(picture.width >> 1);
                    y = e.y + (e.h >> 1) - int(picture.height >> 1);
                }
                blit(image, picture, x, 192 + y);
            }
        }
        // ov2 02072bc0 subtracts half the event-font width (including spacing).
        if (!room_.name.empty()) {
            int width = 0;
            for (unsigned char c : room_.name) {
                width += textAdvance(font, c) + 1;
            }
            textLines(image, font, room_.name, 0x5c + (0xa0 - width) / 2, 3, 16);
        }
        // ov7 020f02dc..020f0350: goal_<n>.txt; a file that cannot be read
        // leaves the text empty (020f0310 skips the copy), so nothing shows.
        const std::string goalFile = "goal_" + number(state_.progress.goal()) + ".txt";
        const auto goal =
            fileExists(assets_.root / "text" / goalFile) ? loadText(goalFile) : std::string();
        int width = 0;
        for (unsigned char c : goal) {
            width += textAdvance(font, c) + 1;
        }
        textLines(image, font, goal, 128 - (width >> 1), 172, 16);
        if (!objectText_.empty()) {
            drawSprite(image, "tobj/window.arc", "gfx", 0, 192);
            if (objectIcon_ >= 0) {
                const auto& icon = assets_.sprite("tobj/icon.arc");
                const auto& steps = icon.animations.at(
                    std::min<std::size_t>(std::size_t(objectIcon_), icon.animations.size() - 1));
                if (!steps.empty()) {
                    blit(image, icon.images.at(steps[0].image), 37, 192 + 84);
                }
            }
            textLines(image, font, objectText_, 0x42, 192 + 0x5c, 16);
        }
    }
    for (auto& a : actors_) {
        drawActor(image, a);
    }
    if (narrationAlpha_ > 1) {
        static const char* letters[4]{"lukeletter/ge/op_message.arc", "lukeletter/ge/message.arc",
                                      "lukeletter/ge/message2.arc",
                                      "lukeletter/ge/end_message.arc"};
        const auto& s = assets_.sprite(letters[narrationMode_ & 3]);
        for (int i = narrationFirst_; i < narrationEnd_; ++i) {
            const auto& p = s.variables.at("pos" + number(i + 1));
            const auto& picture = s.images.at(s.animations.at(i + 1).at(0).image);
            blitNarrationPolygon(image, picture, p[0], 192 + p[1], narrationAlpha_);
        }
    }
    if (scene_ == SceneKind::Movie) {
        if (!movieImage_.pixels.empty()) {
            blit(image, movieImage_, 0, 192);
        }
        // ov6 020efe84 draws the current subtitle into the decoded frame's
        // bitmap with 0207360c(…, 171, text, -3, 128): lines step by the
        // event font's feed + 5 - 3 (14 px) and the block is centred on row
        // 171 (first line at 171 - (14 >> 1) (lines - 1)); 020734e8 centres
        // each line on x 128 by the 02072608 widths plus spacing 1, and
        // 020733b4 sets each glyph pixel to white (0xffff) with no shadow.
        constexpr int kStep = 12 + 5 - 3, kCenterY = 171, kCenterX = 128;
        std::vector<std::string> lines;
        for (std::size_t p = 0; p < movieSubtitle_.size();) {
            auto e = movieSubtitle_.find('\n', p);
            if (e == std::string::npos) {
                e = movieSubtitle_.size();
            }
            lines.push_back(movieSubtitle_.substr(p, e - p));
            p = e + 1;
        }
        int y = 192 + kCenterY - (kStep >> 1) * (int(lines.size()) - 1);
        for (const auto& line : lines) {
            int width = 0;
            for (unsigned char c : line) {
                width += textAdvance(font, c) + 1;
            }
            int x = kCenterX - (width >> 1);
            for (unsigned char c : line) {
                textGlyph(image, font, c, x, y, 0xffff);
                x += textAdvance(font, c) + 1;
            }
            y += kStep;
        }
    }
    for (std::size_t i = 0; i < listLines_.size(); ++i) {
        textLines(image, font, listLines_[i], 0x19, 192 + 0x11 + 0x16 * int(i), 16);
    }
    if (!prizeText_.empty()) {
        // ov8 prize window: system/prize_window2.spr at its pos, the icon
        // centered at y 0x38, text centered on x 0x80 at y 100 (02072f18).
        const auto& w = assets_.sprite("system/prize_window2.arc").variables.at("pos");
        drawSprite(image, "system/prize_window2.arc", "gfx", w[0], 192 + w[1]);
        if (!prizeIcon_.empty()) {
            const auto& icon = assets_.sprite(prizeIcon_);
            const auto a = icon.animationNames.find(prizeAnimation_);
            const auto& steps = icon.animations.at(
                a != icon.animationNames.end() ? a->second : (icon.animations.size() > 1 ? 1 : 0));
            if (!steps.empty()) {
                const auto& p = icon.images.at(steps[0].image);
                blit(image, p, prizeIconX_ >= 0 ? prizeIconX_ : int(0x100 - p.width) >> 1,
                     192 + prizeIconY_);
            }
        }
        int y = 192 + prizeY_;
        std::size_t p = 0;
        while (p <= prizeText_.size()) {
            auto e = prizeText_.find('\n', p);
            if (e == std::string::npos) {
                e = prizeText_.size();
            }
            const auto line = prizeText_.substr(p, e - p);
            int width = 0;
            for (unsigned char c : line) {
                width += textAdvance(font, c) + 1;
            }
            textLines(image, font, line, 0x80 - (width >> 1), y, 16);
            y += 16;
            p = e + 1;
        }
    }
    if (choiceButtons_) {
        // ov8 020f29fc: system/btn yes/no at their win_p positions.
        for (int i = 0; i < 2; ++i) {
            const auto path = std::string("system/btn/ge/") + (i ? "no" : "yes") + ".arc";
            const auto& p = assets_.sprite(path).variables.at("win_p");
            drawSprite(image, path, choice_ == i ? "on" : "off", p[0], 192 + p[1]);
        }
    }
    if (!caption_.empty()) {
        textLines(image, font, caption_, 14, 192 + 82, 16);
    }
    if (dialogueBox_) {
        drawSprite(image, "event/twindow.arc", "gfx", 0, 192 + 123);
        if (speaker_ > 0) {
            const auto path = "eventchr/ge/chr" + number(speaker_) + "_n.arc";
            if (fileExists(assets_.root / "ani" / (path + ".l2sp"))) {
                const auto& v = assets_.sprite(path).variables;
                const auto pos = v.find("pos");
                drawSprite(image, path, "gfx", pos != v.end() ? pos->second[0] : 0,
                           192 + (pos != v.end() ? pos->second[1] : 0));
            }
        }
        textLines(image, font, dialogue_, 9, 192 + 141, 16);
    }
    if (!boundary_.empty()) {
        textLines(image, font, boundary_, 8, 192 + 8, 16, 0xffff);
    }
    applyBrightness(image, topBrightness_, bottomBrightness_);
    return image;
}

void StoryGame::playVoice(int id, int part) {
    // ov8 020f0a10 / ov2 02081190: stop the previous part over time id 0x3f4,
    // wait until the stream controller is idle, then play route 2 (event
    // voice) with level metering.
    auto s = sound();
    if (!s) {
        return;
    }
    s->stopStream(s->duration(0x3f4));
    while (!s->streamIdle()) {
        frame();
    }
    s->playStream(2, id, part, 1, 0, true);
}

// Shows one dialogue line in the text window: the speaker's name, the text
// typed out a few characters per frame with a voice "blip", and the page
// waiting for a tap.
void StoryGame::say(std::string text, int speaker, int voice, int blip, bool quiet,
                    const std::string* after) {
    // ov2 02080f44: one byte per frame, release fast-forward, @p pause,
    // @c clear, @B newline. ROM escape conversion precedes this interpreter.
    // Without a voice clip, every fifth text byte (newline or glyph, not @
    // controls) plays the window's blip effect (+0x14) unless fast-forwarding.
    // Each byte sets the speaker talking unless the window is quiet (+0x18).
    dialogueBox_ = true;
    dialogue_.clear();
    speaker_ = speaker;
    talkSpeaker_ = speaker;
    bool fast = false;
    int part = 1;
    if (voice >= 0) {
        playVoice(voice, 0);
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        talk(!quiet);
        if (text[i] == '@' && i + 1 < text.size()) {
            char tag = text[++i];
            if (tag == 'B') {
                dialogue_ += '\n';
            } else if (tag == 'c' || tag == 'C') {
                dialogue_.clear();
            } else if (tag == 'p' || tag == 'P') {
                fast = false;
                if (voice < 0) {
                    talk(false);
                }
                if (after) {
                    pageWait(voice);
                } else {
                    waitRelease();
                }
                talk(!quiet);
                if (voice >= 0) {
                    playVoice(voice, part++);
                }
            } else if ((tag == 'w' || tag == 'W') && i + 1 < text.size()) {
                // 0208125c: @W<d> waits d*10 frames unless fast-forwarding.
                const int d = std::uint8_t(text[++i]) - '0';
                if (!fast && d > 0) {
                    wait(d * 10);
                }
            } else if ((tag == 'v' || tag == 'V') && i + 1 < text.size()) {
                // 0208128c: @V<d> always waits d*10 frames and skips the per-byte frame.
                const int d = std::uint8_t(text[++i]) - '0';
                if (d > 0) {
                    wait(d * 10);
                }
                continue;
            } else if (tag == 's' || tag == 'S') {
                // 020812e4: the stream stored by 0x99 category 2 starts tracked after 6 frames.
                if (windowStream_.id > 0) {
                    wait(6);
                    if (auto s = sound()) {
                        s->playTrackedStream(0, windowStream_.id, -1, windowStream_.volume,
                                             windowStream_.fade);
                    }
                }
            } else {
                throw std::runtime_error("unreviewed dialogue control");
            }
        } else {
            dialogue_ += text[i];
            if (!fast && blip >= 0 && voice < 0 && i % 5 == 0) {
                if (auto s = sound()) {
                    s->playEffect(blip);
                }
            }
        }
        if (!fast) {
            frame();
            if (touch_.released) {
                fast = true;
            }
        }
    }
    if (voice < 0) {
        talk(false);
    }
    if (!after) {
        waitRelease();
        if (auto s = sound()) {
            s->stopStream(s->duration(0x3f4)); // ov8 020f0b68
        }
        dialogueBox_ = false;
        dialogue_.clear();
        speaker_ = 0;
        return;
    }
    // ov8 020f0ad8..020f0b68: without a voice the after-animation is set
    // before the final page wait, with one only after the window closes.
    if (voice < 0) {
        actorAnimation(speaker, *after);
    }
    pageWait(voice);
    dialogueBox_ = false;
    dialogue_.clear();
    speaker_ = 0;
    if (auto s = sound()) {
        s->stopStream(s->duration(0x3f4));
    }
    if (voice >= 0) {
        actorAnimation(speaker, *after);
    }
}

// Plays dialogue record `id` of an event (event/t<event>_<id>.gds).
void StoryGame::dialogue(int event, int id) {
    auto b =
        readFile(assets_.root / "event" / ("t" + eventName(event) + "_" + number(id) + ".gds"));
    ScriptReader reader(b);
    ScriptArguments a{reader};
    int speaker = a.integer();
    auto animation = a.string();
    auto after = a.string();
    int window = a.integer();
    auto text = text_.expand(a.string());
    if (reader.next().tag != 12) {
        throw std::runtime_error("dialogue tail");
    }
    // ov2 02080b30: dialogue window style -> blip effect (SY 0x28/0x29/0x2a).
    // Styles 4..6 also make the window quiet (+0x18): typing does not animate the speaker.
    static constexpr int blips[7] = {0x29, 0x28, 0x29, 0x2a, 0x28, 0x29, 0x2a};
    const int blip = window >= 0 && window < 7 ? blips[window] : 0x29;
    const bool quiet = window >= 4 && window < 7;
    actorAnimation(speaker, animation);
    say(text, speaker, voice_, blip, quiet, &after);
    voice_ = -1;
}

int StoryGame::duration(int id) {
    auto b = readFile(assets_.root / "time.dlz");
    auto count = read16(b, 0), base = read16(b, 2), size = read16(b, 4);
    if (size != 4) {
        throw std::runtime_error("time table size");
    }
    for (unsigned i = 0; i < count; ++i) {
        if (read16(b, base + i * size) == id) {
            return read16(b, base + i * size + 2);
        }
    }
    throw std::runtime_error("time table key");
}

void StoryGame::mysteries() {
    // ov2 02069294: overlay 20 (src/a2_mystery_menu.cpp) with state +0x8218.
    MysteryMenu menu(assets_, subscenePresent(), audio_, state_, randomState_);
    menu.run();
}

void StoryGame::mysteryCommand(int menuIndex) {
    // ov8 020f1170 (0x71, index n-1) / 020f2d18 (0x7d, index n+9): fade
    // out (02071e84), hide the eight actors (+0x8), run ov20 with +0x8218
    // = index, reset it to -1 and fade out again; the script restores.
    fade(false, 17);
    for (auto& actor : actors_) {
        actor.visible = false;
    }
    state_.mysteryMenu = menuIndex;
    mysteries();
    state_.mysteryMenu = -1;
    top_.clear();
    bottom_.clear();
    topBrightness_ = bottomBrightness_ = -16;
    fade(false, 17);
}

// ---------------------------------------------------------------------------
// Scene router (overlay 2): which scene runs next.

void StoryGame::sceneLoop() {
    // 02068ccc: dispatch the current scene; room scenes run through 02068d18.
    for (;;) {
        if (state_.scene == scene::Room) {
            roomRunner();
        } else {
            dispatch();
        }
    }
}

void StoryGame::dispatch() {
    // 020688a8: jump table on state +0x478.
    switch (state_.scene) {
    case scene::Room:
        return;
    case scene::DramaEvent:
        checkpoint();
        eventScene();
        return;
    case scene::Movie:
        movieScene();
        return;
    case scene::PuzzleResult:
        puzzleResultScene();
        return;
    case scene::Puzzle:
        checkpoint();
        puzzleScene();
        return;
    case scene::Narration:
        narrationScene();
        return;
    case scene::Menu: {
        // 0206937c: overlay 17, the bag (the room checkpoint is the resume point).
        BagMenu bag(assets_, subscenePresent(), audio_, state_, randomState_, playSeconds());
        bag.mysteries = [&] {
            mysteries();
        }; // 02069294 with +0x8218 = -1
        bag.saveFile = saveFile;
        bag.foldTime = [this] {
            return foldForSave();
        };
        bag.playerName = name_.value();
        bag.run();
        return;
    }
    case scene::PuzzleIndex: {
        // 02069174: overlays 27 and 19 (puzzle index); unless a puzzle was
        // started, the bag's saved BGM resumes (0207faa4).
        {
            PuzzleIndex index(assets_, subscenePresent(), audio_, state_);
            index.run();
        }
        if (state_.scene != scene::Puzzle) {
            if (auto s = sound()) {
                s->playBgm(state_.savedBgm, 1.0f, 0.0f);
            }
        }
        return;
    }
    case 31: {
        // 0206a5b4: the puzzle index from the bonus menu; afterwards the
        // secret menu unless a puzzle was started.
        {
            PuzzleIndex index(assets_, subscenePresent(), audio_, state_);
            index.saveFile = saveFile;
            index.run();
        }
        if (state_.scene != scene::Puzzle) {
            state_.scene = 28;
        }
        return;
    }
    case 28: {
        // 0206a168: overlay 23, the secret menu; its back button leaves for
        // the title (scene 12).
        {
            SecretMenu menu(assets_, subscenePresent(), audio_, state_, saveFile);
            menu.run();
        }
        if (state_.scene == scene::Title) {
            throw ReturnToTitle{};
        }
        return;
    }
    case 29: {
        WifiMenu menu(assets_, subscenePresent(), audio_, state_, saveFile);
        menu.run();
        return;
    }
    case 22:
    case 38:
    case 39:
        // The WiFi puzzle list (22, overlays 19/27 with downloaded data) and
        // the wireless download and settings (38/39, the DWC library):
        // recorded gaps; back to the WiFi menu.
        unavailable("Wireless connection");
        state_.scene = 29;
        return;
    case 30: {
        ExtrasMenu menu(assets_, subscenePresent(), audio_, state_, saveFile);
        menu.run();
        return;
    }
    case 32: {
        ArtGallery menu(assets_, subscenePresent(), audio_, state_, saveFile);
        menu.run();
        return;
    }
    case 33: {
        CharacterFiles menu(assets_, subscenePresent(), audio_, state_, saveFile);
        menu.run();
        return;
    }
    case 34: {
        MusicRoom menu(assets_, subscenePresent(), audio_, state_, saveFile);
        menu.run();
        return;
    }
    case 35: {
        VoiceRoom menu(assets_, subscenePresent(), audio_, state_, saveFile);
        menu.run();
        return;
    }
    case 36: {
        MovieTheatre menu(assets_, subscenePresent(), audio_, state_, saveFile);
        menu.run();
        return;
    }
    case scene::Passcode: {
        PasscodePage menu(assets_, subscenePresent(), audio_, state_, saveFile);
        menu.run();
        return;
    }
    case 41:
    case 42: {
        // 020690b4: overlay 11, entering one of the two codes.
        CodeEntry entry(assets_, subscenePresent(), audio_, state_, saveFile);
        entry.run();
        return;
    }
    case scene::Challenge: {
        // Overlay 24: the challenges.
        ChallengeMenu menu(assets_, subscenePresent(), audio_, state_, saveFile);
        menu.run();
        return;
    }
    case scene::Notes: {
        // 02069234: overlay 18 (the protagonist's notes); back to the bag (17).
        NotesMenu menu(assets_, subscenePresent(), audio_, state_);
        menu.run();
        return;
    }
    case scene::Diary: {
        // 020692f4: overlay 21 (the old diary), then the bag's saved BGM.
        {
            DiaryMenu menu(assets_, subscenePresent(), audio_, state_);
            menu.run();
        }
        if (auto s = sound()) {
            s->playBgm(state_.savedBgm, 1.0f, 0.0f); // 0207faa4
        }
        return;
    }
    case scene::SubCamera: {
        // 02069f4c: overlay 13 (camera repair).
        CameraGame camera(assets_, subscenePresent(), audio_, state_);
        camera.run();
        return;
    }
    case scene::PhotoTaken:
    case scene::PhotoAlbum: {
        // 0206a8fc: overlay 16 (photo hunt); afterwards the following scene.
        PhotoGame photo(assets_, subscenePresent(), audio_, state_, randomState_);
        photo.run();
        return;
    }
    case scene::SubHam: {
        // Scene 16: overlay 15 (hamster); back to the bag, or event 18510 at level 6.
        HamsterGame hamster(assets_, subscenePresent(), audio_, state_, randomState_);
        hamster.run();
        return;
    }
    case scene::TeaRequest:
        state_.following = scene::DramaEvent;
        [[fallthrough]]; // 0206a0d4, then 02069fd8
    case scene::SubHerb: {
        // 02069fd8: overlay 14 (herb tea); afterwards the following scene.
        // No checkpoint here: format 2 does not carry the request (+0x8233);
        // the event that issued it is the resume point.
        if (researchTea) {
            researchTea(state_);
            state_.scene = state_.following;
            return;
        }
        TeaGame tea(assets_, subscenePresent(), audio_, state_, randomState_);
        tea.run();
        state_.scene = state_.following;
        return;
    }
    case scene::NewGame:
        throw ReturnToTitle{}; // 02069f20: new game state, then the title (scene 12)
    case scene::Nazoba: {
        // 0206a970: overlay 25 (the puzzle shack); it selects the
        // next scene itself (room, or a puzzle with following scene 44).
        NazobaMenu menu(assets_, subscenePresent(), audio_, state_);
        menu.run();
        return;
    }
    case scene::Staff: {
        // 0206a0f4: overlay 22 (staff roll), then the following scene.
        StaffRoll staff(assets_, subscenePresent(), audio_);
        staff.run();
        state_.scene = state_.following;
        return;
    }
    case scene::Name: {
        // 02069114 with ov12: scene 37 names the hamster. Accepting stores it
        // with 02082f08 (save +0x2f7, 20 bytes) and continues with 11101.
        if (!nameScene) {
            stopAtBoundary("Hamster name entry (scene 37) needs the name editor host.");
        }
        const auto hamster = nameScene();
        std::fill(state_.progress.bytes.begin() + 0x2f7, state_.progress.bytes.begin() + 0x30b, 0);
        const auto text = hamster.value();
        std::copy_n(text.begin(), std::min<std::size_t>(text.size(), 19),
                    state_.progress.bytes.begin() + 0x2f7);
        state_.eventId = 11101;
        state_.scene = scene::DramaEvent;
        return;
    }
    default:
        stopAtBoundary("Story reached scene " + std::to_string(state_.scene) + " (" +
                       sceneLabel(state_.scene) + ")\nafter event " +
                       std::to_string(state_.eventId) + ". Not reconstructed yet.");
    }
}

void StoryGame::roomRunner() {
    // 02068d18: an unviewed automatic event for the place runs first, unless
    // the following scene is already a drama event; kind 6 needs a place change.
    if (state_.findAutoEvent() && state_.following != scene::DramaEvent) {
        state_.loadEventInfo(state_.eventId);
        if (!(state_.current.kind == 6 && !state_.placeChanged)) {
            state_.setEventViewed(state_.eventId, true);
            eventScene();
        }
    }
    while (state_.scene != scene::Room) {
        dispatch();
    }
    checkpoint();
    // ov7 constructor 020efda0 (loads the place through 020f08e4), 020f2118.
    loadRoom();
    enterRoom();
    while (!roomScene()) {
    }
    roomVisible_ = false;
    moveMode_ = false;
    objectText_.clear();
}

// Screens, waiting and the story flow: fades, waits, the next scene,
// event, movie or puzzle, story counters and jumps.
void StoryGame::bindFlowCommands(ScriptContext& context, unsigned id) {
    // 0x02 FadeIn: Fades both screens in.
    context.bind(2, [this](CommandArguments&) { fade(true, 17); });
    // 0x03 FadeOut: Fades both screens out.
    context.bind(3, [this](CommandArguments&) { fade(false, 17); });
    // 0x04 Dialogue: Plays an event dialogue record.
    context.bind(4, [this, id](CommandArguments& a) { dialogue(int(id), a.integer()); });
    auto sceneNumber = [](const std::string& name) -> std::uint8_t {
        // 02077eb0 / 02078188: case-folded comparisons in this order.
        static const std::pair<const char*, std::uint8_t> names[]{
            {"room", 1},       {"drama event", 3}, {"puzzle", 10},     {"movie", 6},
            {"narration", 13}, {"staff", 21},      {"menu", 17},       {"name", 37},
            {"challenge", 24}, {"sub herb", 15},   {"sub camera", 14}, {"sub ham", 16},
            {"passcode", 40},  {"diary", 43},      {"nazoba", 44}};
        auto fold = [](std::string s) {
            for (auto& c : s) {
                if (c >= 'A' && c <= 'Z') {
                    c = char(c - 'A' + 'a');
                }
            }
            return s;
        };
        for (const auto& [n, v] : names) {
            if (fold(name) == n) {
                return v;
            }
        }
        throw std::runtime_error("unknown scene name: " + name);
    };
    // 0x06 RequestScene: Selects the next scene kind.
    context.bind(
        6, [this, sceneNumber](CommandArguments& a) { state_.scene = sceneNumber(a.string()); });
    // 0x07 FollowingScene: Selects the subsequent scene kind.
    context.bind(7, [this, sceneNumber](CommandArguments& a) {
        state_.following = sceneNumber(a.string());
    });
    // 0x08 SelectMovie: Sets the movie ID.
    context.bind(8, [this](CommandArguments& a) { movieId_ = a.integer(); });
    // 0x09 SelectEvent: Sets the next event ID.
    context.bind(9, [this](CommandArguments& a) {
        state_.eventId = std::uint16_t(a.integer());
    }); // 02078468
    // 0x0b SelectPuzzle: Sets the puzzle ID.
    context.bind(0xb, [this](CommandArguments& a) {
        state_.loadPuzzle(a.integer());
    }); // 020784d0 -> 02083c38
    // 0x31 WaitFrames: Waits the specified frame count.
    context.bind(0x31, [this](CommandArguments& a) { wait(a.integer()); });
    // 0x32 MainFadeIn: Fades the lower screen in.
    context.bind(0x32, [this](CommandArguments&) { fade(true, 17, 1); });
    // 0x33 MainFadeOut: Fades the lower screen out.
    context.bind(0x33, [this](CommandArguments&) { fade(false, 17, 1); });
    // 0x37 Blend: Configures the screen blend transition.
    context.bind(0x37, [](CommandArguments& a) {
        auto first = a.integer(), second = a.integer(), third = a.integer(), fourth = a.integer();
        if (first != 15 || second != 5 || third != 0 || fourth != 120) {
            throw std::runtime_error("unreviewed story blend");
        }
    });
    // 0x69 WaitRelease: Waits indefinitely for a touch release.
    context.bind(0x69, [this](CommandArguments&) {
        waitRelease(); // ov2 0207b128 -> 0206cec0, indefinite release wait.
    });
    // 0x6c WaitOrRelease: Waits until timeout or touch release.
    context.bind(0x6c, [this](CommandArguments& a) {
        int count = a.integer();
        for (int i = 0; i < count; ++i) {
            frame();
            if (touch_.released) {
                break;
            }
        }
    });
    // 0x7f MainFadeOutFrames: Main screen to black over N frames, float steps (0207b894 ->
    // 0207205c).
    context.bind(0x7f, [this](CommandArguments& a) {
        fade(false, std::max(a.integer(), 1), 1);
    }); // 0207205c, native integer steps
    // 0x81 MainFadeInFrames: Main screen from black over N frames (0207b91c -> 02071974).
    context.bind(
        0x81, [this](CommandArguments& a) { fade(true, std::max(a.integer(), 1), 1); }); // 02071974
    // 0x82 MainWhiteFadeIn: Main brightness +16 down to 0, one step per frame (ov8 020f37e8).
    context.bind(0x82, [this](CommandArguments&) {
        for (int b = 16; b >= 0; --b) {
            bottomBrightness_ = b;
            frame();
        }
    }); // 020f37e8
    // 0x83 CounterJump: Drama event as current/following scene when counter[a]==b (0207b994).
    context.bind(0x83, [this](CommandArguments& a) {
        int i = a.integer(), v = a.integer();
        if (state_.progress.compareCounter(std::uint32_t(i), std::uint32_t(v), 0)) {
            state_.scene = scene::DramaEvent;
            state_.following = scene::DramaEvent;
        }
    });
    // 0x90 ClearViewedBit: state +0x3d8 = n, EventViewed bit n cleared (0207bdc8).
    context.bind(0x90, [this](CommandArguments& a) {
        int e = a.integer();
        state_.progress.put16(0x310, std::uint16_t(e));
        state_.progress.setEventViewed(std::uint32_t(e), false);
    });
    // 0x91 SetViewedBit: state +0x3d8 = -1, EventViewed bit n set (0207be24).
    context.bind(0x91, [this](CommandArguments& a) {
        int e = a.integer();
        state_.progress.put16(0x310, 0xffff);
        state_.progress.setEventViewed(std::uint32_t(e), true);
    });
    // 0x9c CountJump. ov8 020f34f4: only for a 0 first argument; enough solved puzzles and
    // the event not yet viewed make it the next drama event (marked viewed).
    context.bind(0x9c, [this](CommandArguments& a) {
        const int mode = a.integer(), count = a.integer(), event = a.integer();
        if (mode != 0 || int(state_.solvedCount()) < count ||
            state_.eventViewed(std::uint32_t(event))) {
            return;
        }
        state_.scene = scene::DramaEvent;
        state_.setEventViewed(std::uint32_t(event), true);
        state_.eventId = std::uint16_t(event);
    });
    // 0x61 EndToTitle. ov8 020f0f5c: BGM fade over time 0x3f1, screen fade (02071dc0 step 1),
    // wait time 0x3f1, then scene 0: a new game state and the title.
    context.bind(0x61, [this](CommandArguments&) {
        if (auto s = sound()) {
            s->fadeOutBgm(float(duration(0x3f1)) / 60.0f);
        }
        fade(false, 17);
        wait(duration(0x3f1));
        state_.scene = scene::NewGame;
    });
    // 0207b47c -> 02071f6c / 0207b8d8 -> 02071884: fades over frames on both
    // screens. A zero duration is 16.0/0.0 = infinity in binary32: one frame,
    // then the end level.
    auto framesFade = [this](bool in, int count) {
        if (count == 0) {
            frame();
            topBrightness_ = bottomBrightness_ = in ? 0 : -16;
            return;
        }
        fade(in, count);
    };
    // 0x72 FadeOutFrames: Fades both screens out over a supplied duration.
    context.bind(0x72, [framesFade](CommandArguments& a) { framesFade(false, a.integer()); });
    // 0x80 FadeInFrames: Fades both screens in over n frames.
    context.bind(0x80, [framesFade](CommandArguments& a) { framesFade(true, a.integer()); });
    // 0x05 SetPlace. 02077da8: 020839b0 changes the place (and its version); the label
    // p_%u.txt of the argument is copied to state +0x81e8.
    context.bind(0x05, [this](CommandArguments& a) {
        int place = a.integer();
        state_.setPlace(place);
        if (const auto label = assets_.root / "text" / "jiten" / ("p_" + number(place) + ".txt");
            fileExists(label)) {
            state_.placeName = loadText("jiten/p_" + number(place) + ".txt");
        }
    });
    // 0x7c WaitFramesNoTouch: Waits frames without input (0207b77c).
    context.bind(0x7c, [this](CommandArguments& a) { wait(std::max(a.integer(), 0)); });
    // 0x87 SubFadeOutFrames: Fades the upper screen out.
    context.bind(0x87, [this](CommandArguments& a) { fade(false, a.integer(), 2); });
    // 0x88 SubFadeInFrames: Fades the upper screen in.
    context.bind(0x88, [this](CommandArguments& a) { fade(true, a.integer(), 2); });
    // 0x73 TeaRequest. 0207b4c0: scene 25 runs the herb-tea scene (0206a0d4 sets following 3,
    // then 02069fd8); the two arguments go to +0x8233/+0x8234.
    context.bind(0x73, [this](CommandArguments& a) {
        const int first = a.integer(), second = a.integer();
        state_.scene = 25;
        state_.following = scene::DramaEvent;
        state_.teaRequest[0] = std::uint8_t(first);
        state_.teaRequest[1] = std::uint8_t(second);
    });
    // 0x6a ShakeMain: Main background shake for N frames (0207b154 -> layer +0x8c).
    context.bind(0x6a,
                 [this](CommandArguments& a) { shakeMain_ = a.integer(); }); // 0207b154: +0x8c
    // 0x6b ShakeSub: Sub background shake for N frames (0207b190 -> layer +0x90).
    context.bind(0x6b, [this](CommandArguments& a) { shakeSub_ = a.integer(); }); // 0207b190: +0x90
    // 0x8e WaitDuration: Waits for a duration.
    context.bind(0x8e, [this](CommandArguments& a) { wait(duration(a.integer())); });
    // 0x92 RoomTutorial: Requests the room tutorial on entry.
    context.bind(0x92, [](CommandArguments&) {
        // Flag +821d requests the original first-touch sprite overlay; still unresolved.
    });
}

// The stage: backgrounds, actors and their animations, the chapter card
// and the voice of the next dialogue line.
void StoryGame::bindStageCommands(ScriptContext& context) {
    // 0x21 MainBackground: Loads a lower-screen background layer.
    // 0x22 SubBackground: Loads an upper-screen background layer.
    for (auto command : {0x21, 0x22}) {
        context.bind(command, [this, command](CommandArguments& a) {
            auto path = localized(a.string());
            if (a.integer() != 3) {
                throw std::runtime_error("story background layer");
            }
            assets_.background(path);
            (command == 0x21 ? bottom_ : top_) = path;
            wait(1);
        });
    }
    // 0x2a ShowActor: Makes an actor visible.
    // 0x2b HideActor: Hides an actor.
    for (auto command : {0x2a, 0x2b}) {
        context.bind(command, [this, command](CommandArguments& a) {
            actors_.at(a.integer()).visible = command == 0x2a;
        });
    }
    // 0x2d ChapterCard: Displays a chapter title and waits for dismissal.
    context.bind(0x2d, [this](CommandArguments& a) {
        auto chapter = a.integer();
        fade(false, 17);
        actors_.clear();
        top_.clear();
        bottom_ = "chapter/ge/chapter" + number(chapter);
        // ov2 02079450: BGM fades out over time id 0x44c while the chapter
        // jingle streams, then returns to full volume over id 0x44d.
        if (auto s = sound()) {
            s->fadeOutBgm(float(duration(0x44c)) / 60.0f);
        }
        if (auto s = sound()) {
            s->playStream(0, 20, -1, 1, 0, false);
        }
        fade(true, 17);
        wait(60);
        waitRelease();
        if (auto s = sound()) {
            s->bgmVolume(1.0f, float(duration(0x44d)) / 60.0f);
        }
        fade(false, 17);
        // The chapter's temporary background must not survive into the
        // native event-entry fade for the following narration handoff.
        // Host lifecycle cleanup, not a new GDS command. Full original
        // context/VRAM lifetime and transition timing remain unresolved.
        bottom_.clear();
    });
    // 0x7e ActorShake. 0207b838 -> ov8 020f37d4: actor slot shake counter (+0xc) in frames.
    context.bind(0x7e, [this](CommandArguments& a) {
        int slot = a.integer(), frames = a.integer();
        if (slot >= 0 && std::size_t(slot) < actors_.size()) {
            actors_[std::size_t(slot)].shake = frames;
        }
    });
    // 0x30 ActorPosition: Moves an actor slot to a stage position, then one frame (02079718 -> ov8
    // 020f0de4).
    context.bind(0x30, [this](CommandArguments& a) {
        int slot = a.integer(), position = a.integer();
        if (slot >= 0 && slot < 8 && std::size_t(slot) < actors_.size()) {
            actors_[std::size_t(slot)].position = position;
        }
        frame();
    });
    // 0x3f ActorAnimation. 02079cf8 -> ov8 020f0e24: animation by character ID, then 3 frames.
    context.bind(0x3f, [this](CommandArguments& a) {
        int actor = a.integer();
        auto name = a.string();
        actorAnimation(actor, name);
        wait(3);
    });
    // 0x5c SelectVoice: Sets the next dialogue voice ID.
    context.bind(0x5c, [this](CommandArguments& a) { voice_ = a.integer(); });
    // 0x2c ActorFade. 0207936c: show the actor (ov8 +0x24), set its alpha speed (+0x30),
    // then wait int(32/|speed|)+4 frames.
    context.bind(0x2c, [this](CommandArguments& a) {
        const int index = a.integer();
        const float speed = a.real();
        if (index >= 0 && index < 8 && std::size_t(index) < actors_.size()) {
            actors_[std::size_t(index)].visible = true;
            actors_[std::size_t(index)].speed = speed;
        }
        const float magnitude = speed < 0.0f ? -speed : speed;
        wait(int(32.0f / magnitude) + 4);
    });
}

// Music, sound effects and streamed sound.
void StoryGame::bindSoundCommands(ScriptContext& context) {
    // 0x5d StreamEffect. ov2 0207abf8 -> ov8 020f0eec: streamed effect ST_%03d.SAD (stream route
    // 0).
    context.bind(0x5d, [this](CommandArguments& a) {
        int id = a.integer();
        if (auto s = sound()) {
            s->playStream(0, id, -1, 1, 0, false);
        }
    });
    // 0x5e SoundEffect. ov2 0207ac44 -> ov8 020f0f34: sequenced effect in the group 0207f850
    // selects.
    context.bind(0x5e, [this](CommandArguments& a) {
        int id = a.integer();
        if (auto s = sound()) {
            s->playEffect(id);
        }
    });
    // 0x5f MusicVolumeFramesOnly. 0207ac90 -> ov8 020f0e7c -> 0207fae4: BGM volume over frames
    // / 60.0.
    context.bind(0x5f, [this](CommandArguments& a) {
        const float volume = a.real();
        const float seconds = float(a.integer()) / 60.0f;
        if (auto s = sound()) {
            s->bgmVolume(volume, seconds);
        }
    });
    // 0x62 SequenceMusic. ov2 0207ad68 -> 0207faa4: track, volume, fade frames / 60.
    context.bind(0x62, [this](CommandArguments& a) {
        int track = a.integer();
        float volume = a.real();
        int frames = a.integer();
        if (auto s = sound()) {
            s->playBgm(track, volume, float(frames) / 60.0f);
        }
    });
    // 0x89 StopAmbience: Stops the unique room effect (ov8 020f3adc).
    context.bind(0x89, [this](CommandArguments&) {
        if (auto s = sound()) {
            s->stopUniqueEffect(0);
        }
    });
    // 0x8c MusicTimed: BGM track, volume, time-table fade (ov8 020f0fd0).
    context.bind(0x8c, [this](CommandArguments& a) {
        int track = a.integer();
        float volume = a.real();
        int time = a.integer();
        if (auto s = sound()) {
            s->playBgm(track, volume, float(duration(time)) / 60.0f);
        }
    });
    // 0x98 SequencedEffect: Sequenced effect; mode 1 also starts it as the tracked effect
    // (0207c018).
    context.bind(0x98, [this](CommandArguments& a) {
        int id = a.integer(), mode = a.integer();
        if (auto s = sound()) {
            s->playEffect(id);
            if (mode == 1) {
                s->playTrackedEffect(id);
            }
        }
    });
    // 0x9f UniqueEffect: Starts the unique room effect id (ov8 020f3ab4).
    context.bind(0x9f, [this](CommandArguments& a) {
        int id = a.integer();
        if (auto s = sound()) {
            s->playUniqueEffect(id);
        }
    });
    // 0x8a MusicVolumeTimed / 0x8b MusicVolumeTimedOnly. ov2 0207bbb4/0207bc10 -> ov8
    // 020f0e90/020f0ec4: BGM volume over a time-table duration (frames / 60); 0x8a also stops the
    // unique effect.
    for (auto command : {0x8a, 0x8b}) {
        context.bind(command, [this, command](CommandArguments& a) {
            float volume = a.real();
            int durationId = a.integer();
            if (auto s = sound()) {
                s->bgmVolume(volume, float(duration(durationId)) / 60.0f);
                if (command == 0x8a) {
                    s->stopUniqueEffect(0);
                }
            }
        });
    }
    // 0x99 StreamAudio. 0207c098: category 0 plays stream route 0 (0207fb84), 1 the tracked
    // variant (0207fc3c), others store it for the text window's @S (ov8
    // 020f0f20 -> window +0x128/+0x12c/+0x130).
    context.bind(0x99, [this](CommandArguments& a) {
        const int id = a.integer();
        const float volume = a.real(), fadeTime = a.real();
        const int category = a.integer();
        auto s = sound();
        if (category == 0) {
            if (s) {
                s->playStream(0, id, -1, volume, fadeTime, false);
            }
        } else if (category == 1) {
            if (s) {
                s->playTrackedStream(0, id, -1, volume, fadeTime);
            }
        } else {
            windowStream_ = {id, volume, fadeTime};
        }
    });
    // 0xa0 StopEffect: Stops a sequenced effect over seconds (0207c334 -> 0207f918).
    context.bind(0xa0, [this](CommandArguments& a) {
        int id = a.integer();
        float seconds = a.real();
        if (auto s = sound()) {
            s->stopEffect(id, seconds);
        }
    });
    // 0x49 MusicVolumeFrames: BGM volume over frames/60 s, stops the unique effect (0207a15c -> ov8
    // 020f0e5c).
    context.bind(0x49, [this](CommandArguments& a) {
        const float volume = a.real();
        const float seconds = float(a.integer()) / 60.0f; // 0203af44 / 0203aa94
        if (auto s = sound()) {
            s->bgmVolume(volume, seconds);
            s->stopUniqueEffect(0);
        }
    });
}

// Progress and its notices: rewards, items, mysteries, companions,
// the save prompt and the popups that announce them.
void StoryGame::bindProgressCommands(ScriptContext& context) {
    // 0x74 PuzzleItemReward. ov8 020f171c: the solved puzzle's reward (state +0x8200, set from
    // puzzle byte +0x3f by ov27 020f25dc): 0..19 camera part (CameraPrt), 20..39 tea herb
    // (HerbteaELM), 40..59 hamster item (count +0x284), >= 60 diary key (diary page flags). Icons
    // from ov8 020f1584; texts tx 0xc9/0xca/0xcb/0xdc.
    context.bind(0x74, [this](CommandArguments&) {
        const int r = state_.reward;
        if (r == -1) {
            return;
        }
        if (auto s = sound()) {
            s->playEffect(0xd);
        }
        wait(16);
        auto named = [&](const char* format, const std::string& file) {
            auto t = loadText(format);
            const auto p = t.find("%s");
            if (p != t.npos) {
                t.replace(p, 2, loadText(file));
            }
            return t;
        };
        std::string text;
        int y = 100;
        if (r < 0x14 && r >= 0) {
            state_.progress.setBit(0x24c, 10, std::uint32_t(r), true);
            prizeIcon_ = "subgame/camera/camera_p_icon.arc";
            prizeAnimation_ = std::to_string(r + 1);
            prizeIconY_ = 0x3a;
            text = named("tx_201.txt", "cam_" + number(r) + ".txt");
            state_.progress.setBit(0x2f2, 16, 2, true);
        } else if (r >= 0x14 && r < 0x28) {
            state_.progress.setBit(0x280, 8, std::uint32_t(r - 0x14), true);
            prizeIcon_ = "subgame/tea/icon.arc";
            prizeAnimation_ = "s" + std::to_string(r - 0x13);
            prizeIconY_ = 0x3a;
            text = named("tx_202.txt", "herb_" + number(r - 0x14) + ".txt");
            state_.progress.setBit(0x2f2, 16, 4, true);
        } else if (r >= 0x28 && r < 0x3c) {
            if (unsigned(r - 0x28) < 10) {
                ++state_.progress.bytes[0x284 + std::size_t(r - 0x28)];
            }
            if (r == 0x28 && !state_.progress.bytes[0x283]) {
                state_.progress.bytes[0x283] = 1;
            }
            prizeIcon_ = "subgame/ham/seticon.arc";
            prizeAnimation_ = std::to_string(r - 0x27);
            prizeIconY_ = 0x3c;
            text = named("tx_203.txt", "ham_" + number(r - 0x28) + ".txt");
            state_.progress.setBit(0x2f2, 16, 3, true);
        } else if (r >= 0x3c) {
            unsigned before = 0, after = 0;
            for (unsigned i = 0; i < 12; ++i) {
                before += state_.progress.bit(0x314, 16, i);
            }
            state_.progress.setBit(0x314, 16, std::uint32_t(r - 0x3c), true);
            for (unsigned i = 0; i < 12; ++i) {
                after += state_.progress.bit(0x314, 16, i);
            }
            if (before != after) {
                state_.progress.setBit(0x316, 16, before, true);
            }
            prizeIcon_ = "event/diary_icon.arc";
            prizeAnimation_ = "gfx";
            prizeIconY_ = 0x3c;
            text = loadText("tx_220.txt");
            y = 0x66;
            state_.progress.setBit(0x2f2, 16, 6, true);
        }
        prizeY_ = y;
        prizeText_ = text;
        waitRelease();
        wait(15);
        prizeText_.clear();
        prizeIcon_.clear();
        prizeY_ = 100;
        prizeIconY_ = 0x38;
        prizeAnimation_ = "gfx";
        state_.reward = r + 1;
        frame();
        wait(2);
    });
    // 0x75 PuzzleIndexNotice: Shows the puzzle-index notification.
    context.bind(0x75, [this](CommandArguments&) {
        // ov8 020f1d38: puzzle-index notification. Native text-window adapter;
        // original icon fade/overlay still pending.
        auto message = loadText("tx_204.txt");
        auto pos = message.find("%.3d");
        if (pos == message.npos) {
            throw std::runtime_error("puzzle index format");
        }
        message.replace(pos, 4, number(state_.puzzle.number, 3));
        pos = message.find("%s");
        if (pos == message.npos) {
            throw std::runtime_error("puzzle index title format");
        }
        message.replace(pos, 2, text_.expand(state_.puzzle.title));
        if (auto s = sound()) {
            s->playEffect(0xd); // 020f1d38 before the notification window
        }
        say(message);
    });
    // 0x70 MysteryFound. 0207b390: a new mystery also marks MemoNewFlag and MenuNewFlag 0.
    context.bind(0x70, [this](CommandArguments& a) {
        const auto i = std::uint32_t(a.integer() - 1);
        if (!state_.progress.memo(i)) {
            state_.progress.setMemoNew(i, true);
            state_.progress.setBit(0x2f2, 16, 0, true);
        }
        state_.progress.setMemo(i, true);
    });
    // 0x71 MysteryMenu. 0207b430 -> ov8 020f1170: the mystery list revealing mystery n.
    context.bind(0x71, [this](CommandArguments& a) { mysteryCommand(a.integer() - 1); });
    // 0x7b SavePrompt. 0207b730 -> ov8 020f29fc: save prompt. A zero argument is the
    // completed-game save (tx 0xd1: +0x821e, step 490, place 0x52, story
    // flag 0x3a cleared, 18480 viewed with 138 solved), otherwise tx 0xd0.
    // Yes hides the actors and runs the save object (020866d0) with the
    // argument; natively that writes a checkpoint resuming at that event.
    context.bind(0x7b, [this](CommandArguments& a) {
        const int resume = a.integer();
        if (resume == 0) {
            prizeText_ = loadText("tx_209.txt");
            state_.cleared = true;
            state_.progress.setStoryStep(490);
            state_.progress.setPlace(0x52);
            state_.progress.setStoryFlag(0x3a, false);
            if (state_.allPuzzlesSolved(138)) {
                state_.setEventViewed(18480, true);
            }
        } else {
            prizeText_ = loadText("tx_208.txt");
        }
        if (auto s = sound()) {
            s->playEffect(0xd);
        }
        const bool save = yesNo();
        // Yes hides the eight actors (+0x8) and runs the file dialog without
        // an owner (020f2c58..020f2c84); the argument is the event saved as
        // pending (none for the completed-game save).
        if (save) {
            for (auto& actor : actors_) {
                actor.visible = false;
            }
            fileSave(resume);
        }
    });
    // 0x9b AdventureResult. ov8 020f0bdc: tx 1100 with the solved count (02083e7c) and the rank
    // tx 1101 (< 100), 1102 (< 120) or 1103, typed in the text window (style 2,
    // no speaker) and closed by the page wait 02080e44.
    context.bind(0x9b, [this](CommandArguments&) {
        const unsigned solved = state_.solvedCount();
        auto text = loadText("tx_1100.txt");
        const auto rank = loadText(solved < 100   ? "tx_1101.txt"
                                   : solved < 120 ? "tx_1102.txt"
                                                  : "tx_1103.txt");
        auto p = text.find("%d");
        if (p != text.npos) {
            text.replace(p, 2, std::to_string(solved));
        }
        p = text.find("%s");
        if (p != text.npos) {
            text.replace(p, 2, rank);
        }
        static const std::string none = "NONE";
        say(text, 0, -1, 0x29, false, &none);
    });
    // 0x9d PlacePrompt. ov8 020f356c: yes/no with tx 0xdd; yes writes place 0x23 (02082904)
    // directly, without a version update.
    context.bind(0x9d, [this](CommandArguments&) {
        prizeText_ = loadText("tx_221.txt");
        if (auto s = sound()) {
            s->playEffect(0xd);
        }
        if (yesNo()) {
            state_.progress.setPlace(0x23);
        }
    });
    // 0x9e TextNotice. ov8 020f23ec: BGM to 0 over time 0xd2, wait 0xd3, jingle stream 5,
    // tx n (mode 1 formats the hamster name) at y 0x5c, at least time 0xd4
    // and a touch, BGM back over 0xd5, wait 0xd6.
    context.bind(0x9e, [this](CommandArguments& a) {
        const int id = a.integer(), mode = a.integer();
        auto s = sound();
        if (s) {
            s->bgmVolume(0.0f, float(duration(0xd2)) / 60.0f);
        }
        wait(duration(0xd3));
        if (s) {
            s->playStream(0, 5, -1, 1, 0, false);
        }
        wait(16);
        auto text = loadText("tx_" + std::to_string(id) + ".txt");
        if (mode == 1) {
            const auto p = text.find("%s");
            std::string hamster(reinterpret_cast<const char*>(&state_.progress.bytes[0x2f7]));
            if (p != text.npos) {
                text.replace(p, 2, text_.expand(hamster));
            }
        }
        prizeY_ = 0x5c;
        prizeIcon_.clear();
        prizeText_ = text;
        const int minimum = duration(0xd4);
        for (int i = 0; i < minimum; ++i) {
            frame();
        }
        waitRelease();
        wait(15);
        prizeText_.clear();
        prizeY_ = 100;
        if (s) {
            s->bgmVolume(1.0f, float(duration(0xd5)) / 60.0f);
        }
        wait(duration(0xd6));
        frame();
        wait(2);
    });
    // 0xa1 ClearNotice. ov8 020f1000: unless the game was completed (+0x821e), jingle stream
    // 5 and tx 1200 at y 0x5c, at least time 0xd4 and a touch.
    context.bind(0xa1, [this](CommandArguments&) {
        if (state_.cleared) {
            return;
        }
        if (auto s = sound()) {
            s->playStream(0, 5, -1, 1, 0, false);
        }
        wait(16);
        prizeY_ = 0x5c;
        prizeIcon_.clear();
        prizeText_ = loadText("tx_1200.txt");
        const int minimum = duration(0xd4);
        for (int i = 0; i < minimum; ++i) {
            frame();
        }
        waitRelease();
        wait(15);
        prizeText_.clear();
        prizeY_ = 100;
        frame();
        wait(2);
    });
    // 0x84 PhotoPiece. ov8 020f382c: PhotoPieceFlag n, counter 0x18 += 1, SY 0xd, then the
    // prize window with the remaining count (tx 0xde / 0x640 / 0xd4 "%d").
    context.bind(0x84, [this](CommandArguments& a) {
        const int piece = a.integer();
        state_.progress.setBit(0x30b, 16, std::uint32_t(piece), true);
        state_.progress.addCounter(0x18, 1);
        if (auto s = sound()) {
            s->playEffect(0xd);
        }
        const int remaining = 0x10 - int(state_.progress.counter(0x18));
        std::string text;
        if (remaining == 0) {
            text = loadText("tx_222.txt");
        } else {
            text = loadText(remaining < 2 ? "tx_1600.txt" : "tx_212.txt");
            const auto p = text.find("%d");
            if (p != text.npos) {
                text.replace(p, 2, std::to_string(remaining));
            }
        }
        prizeIcon_ = "menu/bag/piece_icon.arc";
        prizeText_ = text;
        wait(16);
        waitRelease();
        wait(15);
        prizeText_.clear();
        prizeIcon_.clear();
        frame();
        wait(2);
    });
    // 0x79 FeaturePopup. 0207b694 -> ov8 020f21bc: BGM to 0 over time 0xdc, wait time 0xdd,
    // jingle stream 6, text by kind (0 tx 0xd2; 1 tx 0xce and save byte
    // +0x283 = 1 via 02082ac8; 2 tx 0xd3; 3 tx 0xdb with diary page flag 0,
    // its new flag and MenuNewFlag 6), at least time 0xde, then a touch.
    context.bind(0x79, [this](CommandArguments& a) {
        const int kind = a.integer();
        auto s = sound();
        if (s) {
            s->bgmVolume(0.0f, float(duration(0xdc)) / 60.0f);
        }
        wait(duration(0xdd));
        if (s) {
            s->playStream(0, 6, -1, 1, 0, false);
        }
        wait(16);
        std::string text;
        if (kind == 0) {
            text = loadText("tx_210.txt");
        } else if (kind == 1) {
            text = loadText("tx_206.txt");
            if (!state_.progress.bytes[0x283]) {
                state_.progress.bytes[0x283] = 1;
            }
        } else if (kind == 2) {
            text = loadText("tx_211.txt");
        } else if (kind == 3) {
            text = loadText("tx_219.txt");
            state_.progress.setBit(0x314, 16, 0, true);
            state_.progress.setBit(0x316, 16, 0, true);
            state_.progress.setBit(0x2f2, 16, 6, true);
        }
        prizeY_ = 0x5c;
        prizeIcon_.clear();
        prizeText_ = text;
        const int minimum = duration(0xde);
        for (int i = 0; i < minimum; ++i) {
            frame();
        }
        waitRelease();
        wait(15);
        prizeText_.clear();
        prizeY_ = 100;
        frame();
        wait(2);
    });
    // 0x94 HamsterNamed. 0207bf18 -> ov8 020f2da0: SY 0xd, tx 0xd7 formatted with the hamster
    // name (state +0x3bf = save +0x2f7) at y 0x5c, then a touch.
    context.bind(0x94, [this](CommandArguments&) {
        if (auto s = sound()) {
            s->playEffect(0xd);
        }
        wait(16);
        auto text = loadText("tx_215.txt");
        const auto p = text.find("%s");
        std::string hamster(reinterpret_cast<const char*>(&state_.progress.bytes[0x2f7]));
        if (p != text.npos) {
            text.replace(p, 2, text_.expand(hamster));
        }
        prizeY_ = 0x5c;
        prizeIcon_.clear();
        prizeText_ = text;
        waitRelease();
        wait(15);
        prizeText_.clear();
        prizeY_ = 100;
        frame();
        wait(2);
    });
    // 0x76 NazobaMemo. 0207b59c -> ov8 020f25e8: every unsolved puzzle 1..138 of the group
    // (nz_lst +0x34) gets puzzle-byte bit 6 (02083de8) and a "tx 0xcf" line;
    // eight lines per page on event/nazobamemo_bg, a touch per page. The
    // blinking touch icon and exact text layer are native (G44).
    context.bind(0x76, [this](CommandArguments& a) {
        const int group = a.integer();
        fade(false, 17);
        for (auto& actor : actors_) {
            actor.visible = false;
        }
        std::vector<std::string> lines;
        const auto format = loadText("tx_207.txt");
        for (int n = 1; n < 0x8b; ++n) {
            const auto info = state_.puzzleByNumber(n);
            if (!info || info->extra != group || state_.puzzleStatus(n) == 2) {
                continue;
            }
            const auto index = std::uint32_t(n - 1);
            if (index < 216) {
                state_.progress.puzzleByte(index) |= 0x40;
            }
            auto line = format;
            auto p = line.find("%.3d");
            if (p != line.npos) {
                line.replace(p, 4, number(n, 3));
            }
            p = line.find("%s");
            if (p != line.npos) {
                line.replace(p, 2, text_.expand(info->title));
            }
            lines.push_back(line);
        }
        bottom_ = "event/nazobamemo_bg";
        top_ = "event/ge/nazobamemo_sub";
        std::size_t shown = 0;
        do {
            listLines_.assign(lines.begin() + std::ptrdiff_t(shown),
                              lines.begin() + std::ptrdiff_t(std::min(lines.size(), shown + 8)));
            shown += listLines_.size();
            if (auto s = sound()) {
                s->playEffect(0xd);
            }
            if (bottomBrightness_ < 0) {
                fade(true, 9);
            }
            wait(60);
            waitRelease();
            if (auto s = sound()) {
                s->playEffect(3);
            }
        } while (shown < lines.size());
        listLines_.clear();
    });
    // 0x96 CompanionJoin / 0x97 CompanionLeave. 0207bf80/0207bfcc -> ov8 020f30ac/020f32cc:
    // companion bits in save +0x31e (state +0x3e6) are set/cleared; except for bit 2 a window with
    // map_icons animation 3 at (0x74,0x38) and tx 0xd9 / 0xda follows.
    for (auto command : {0x96, 0x97}) {
        context.bind(command, [this, command](CommandArguments& a) {
            const int bits = a.integer();
            auto& b = state_.progress.bytes[0x31e];
            b = command == 0x96 ? std::uint8_t(b | bits) : std::uint8_t(b & ~bits);
            if (bits == 2) {
                return;
            }
            if (auto s = sound()) {
                s->playEffect(0xd);
            }
            wait(17);
            prizeIcon_ = "map/map_icons.arc";
            prizeAnimation_ = "3";
            prizeIconX_ = 0x74;
            prizeIconY_ = 0x38;
            prizeY_ = 100;
            prizeText_ = loadText(command == 0x96 ? "tx_217.txt" : "tx_218.txt");
            waitRelease();
            wait(15);
            prizeText_.clear();
            prizeIcon_.clear();
            prizeIconX_ = -1;
            prizeAnimation_ = "gfx";
            frame();
            wait(2);
        });
    }
    // 0x95 TornPhoto. 0207bf4c -> ov8 020f2ee4: torn-photo popup. BGM to 0 over time 300,
    // wait time 301, jingle stream 40, tx_216 in the prize window; at least
    // time 302 frames, then a touch; window ramps; BGM back over 1 s; 60 frames.
    context.bind(0x95, [this](CommandArguments&) {
        auto s = sound();
        if (s) {
            s->bgmVolume(0.0f, float(duration(300)) / 60.0f);
        }
        wait(duration(301));
        if (s) {
            s->playStream(0, 40, -1, 1, 0, false);
        }
        prizeIcon_ = "menu/bag/piece_icon.arc";
        prizeText_ = loadText("tx_216.txt");
        wait(16);
        const int minimum = duration(302);
        for (int i = 0; i < minimum; ++i) {
            frame();
        }
        waitRelease();
        wait(15);
        prizeText_.clear();
        prizeIcon_.clear();
        frame();
        wait(2);
        if (s) {
            s->bgmVolume(1.0f, 1.0f);
        }
        wait(60);
    });
    // 0x7d MysterySolved. 0207b7ec -> ov8 020f2d18: the mystery list stamping mystery n solved.
    context.bind(0x7d, [this](CommandArguments& a) { mysteryCommand(a.integer() + 9); });
    // 0x77 GiveItem. ov8 020f1f98: SY 0xd for every item other than 2, then the item window;
    // the item becomes a story item (StoryItemFlag, 02082e44).
    context.bind(0x77, [this](CommandArguments& a) {
        int item = a.integer();
        state_.progress.setStoryItem(std::uint32_t(item), true);
        if (item != 2) {
            if (auto s = sound()) {
                s->playEffect(0xd);
            }
        }
        say(loadText("item_" + number(item) + ".txt"));
    });
    // 0x7a RemoveItem: Clears a story item flag (0207b6e0 -> 02082e44 with 0).
    context.bind(0x7a, [this](CommandArguments& a) {
        state_.progress.setStoryItem(std::uint32_t(a.integer()), false);
    });
    // 0x35 AddCounter: Adds a byte to save counter [index] (0207988c -> 020826b0).
    context.bind(0x35, [this](CommandArguments& a) {
        int i = a.integer(), v = a.integer();
        state_.progress.addCounter(std::uint32_t(i), std::uint8_t(v));
    });
    // 0x36 OrCounter: ORs a byte mask into save counter [index] (020798e8 -> 020826d0).
    context.bind(0x36, [this](CommandArguments& a) {
        int i = a.integer(), v = a.integer();
        state_.progress.orCounter(std::uint32_t(i), std::uint8_t(v));
    });
    // 0x78 SetReward: Reward (state +0x8200) for 0x74; run only by the ov8 pre-pass 0207cff0,
    // skipped by the main dispatcher (0207b634).
    context.bind(0x78, [](CommandArguments& a) {
        a.integer();
    }); // main dispatch skips it (0207cee0); see the pre-pass below
}

// A story event (overlay 8, 020f3b20): runs the event's script with the
// story commands bound below, then decides the next scene. Events also
// handle their dialogue records, actors on stage, background changes,
// sounds, rewards, items and the many small popups of the story.
void StoryGame::eventScene() {
    // 02068e50: overlay-8 event object. Constructor 020efed8 -> 020f0380 loads
    // d%i_%.3i.dat, ev_inf2 and a puzzle launch; run 020f3b20 executes
    // e%i_%.3i.gds, then updates the goal and the following event.
    const unsigned id = state_.eventId;
    scene_ = SceneKind::Story;
    detail_ = int(id);
    dialogueBox_ = false;
    actors_.clear();
    voice_ = -1;
    roomVisible_ = false;
    moveMode_ = false;
    windowStream_ = {}; // new text window per event object (020809d4)
    Bytes data(40, 0);
    {
        const auto path = assets_.root / "event" / ("d" + eventName(int(id)) + ".dat");
        if (fileExists(path)) {
            data = readFile(path);
            if (data.size() != 40) {
                throw std::runtime_error("event data size");
            }
        }
    }
    const unsigned goalKey = id; // this+0x98
    state_.loadEventInfo(id);
    if (id >= 20000 && id < 30000 && state_.current.puzzle != 0xffff) {
        state_.loadPuzzle(std::int16_t(state_.current.puzzle));
        state_.scene = scene::Puzzle;
        state_.following = scene::PuzzleResult;
    }
    if (state_.current.storyFlag != 0xffff) {
        state_.progress.setStoryFlag(state_.current.storyFlag, true);
    }
    state_.placeChanged = false;
    if (auto s = sound()) {
        // 020f051c: data bytes 4/5 (context +0x6c/+0x6d) choose whether the
        // room-sound row (ev_inf2 sound key) starts its music.
        const int key = state_.current.sound;
        if (data[5] == 2 || data[4] == 3) {
            s->roomSound(key, false);
            if (data[4] == 3) {
                s->fadeOutBgm(0.5f);
            } else {
                state_.roomSoundEvent = std::uint16_t(key);
            }
        } else {
            s->roomSound(key, true);
        }
    }
    if (read16(data, 0)) {
        bottom_ = "map/main" + number(read16(data, 0));
    }
    if (const auto sub = read16(data, 2)) {
        top_ = (sub >= 50 && sub <= 54 ? "event/ge/sub" : "event/sub") + number(sub);
    }
    for (unsigned i = 0; i < 8 && data[6 + i]; ++i) {
        Actor actor;
        actor.id = data[6 + i];
        actor.sprite = eventCharacter(actor.id);
        actor.position = data[14 + i];
        actor.visible = data[22 + i];
        selectActor(actor, data[30 + i]);
        actors_.push_back(actor);
    }
    // Run 020f3b20 begins by selecting the room as current scene; the script
    // changes it with 0x06.
    state_.scene = scene::Room;
    // Native event-boundary fade. Script-controlled fades below keep their
    // original counts; context ctor/destructor timing is not yet trace-equal.
    if (data[4] == 0) {
        fade(true, 17);
    }
    ScriptContext context;
    bindFlowCommands(context, id);
    bindStageCommands(context);
    bindSoundCommands(context);
    bindProgressCommands(context);
    ScriptInterpreter script(
        readFile(assets_.root / "event" / ("e" + eventName(int(id)) + ".gds")));
    script.onCommand = [&](CommandBoundary b) {
        if (b.entering && onCommand) {
            onCommand(int(id), b.command);
        }
    };
    {
        // ov8 020f3bf4 -> ov2 0207cff0: a pre-pass over the script runs only
        // the commands whose table entry is 0207b634, i.e. 0x78, which stores
        // its integer in the reward (state +0x8200). The main dispatcher
        // (0207cee0) skips 0x78, so 0x74 later shows the last value set here.
        // Like the ROM it stops at the end tag or a command 1.
        ScriptReader scan(readFile(assets_.root / "event" / ("e" + eventName(int(id)) + ".gds")));
        while (scan.position() < scan.size()) {
            const auto token = scan.next();
            if (token.tag == 12) {
                break;
            }
            if (token.tag != 0) {
                continue;
            }
            const auto command = scan.values().command;
            if (command == 1) {
                break;
            }
            if (command == 0x78) {
                ScriptArguments args(scan);
                state_.reward = args.integer();
            }
        }
    }
    // ov8 020f3b20: once the music is ready, the unique ambience (100) starts
    // unless the event keeps the previous sound (data[5]==2 or data[4]==3).
    if (auto s = sound()) {
        if (!(data[5] == 2 || data[4] == 3)) {
            s->playUniqueEffect(100);
        }
    }
    script.run(context);
    actors_.clear();
    dialogueBox_ = false;
    // 020f3d58: the goal row for this event updates the objective and, for a
    // nonzero kind, shows the objective window (ov8 020f11f8) with next_%i.txt.
    if (const auto goal = state_.goalInfo(goalKey)) {
        state_.progress.setGoal(goal->goal);
        if (goal->kind) {
            auto s = sound();
            if (s) {
                s->bgmVolume(0.0f, float(duration(0xe6)) / 60.0f); // 0207fae4
            }
            wait(duration(0xe7));
            if (s) {
                s->playStream(0, 21, -1, 1, 0, false);
            }
            const auto text = loadText("next_" + number(goal->goal) + ".txt");
            caption_.clear();
            bottom_.clear();
            // Native presentation of the mokuteki window (G41): window sprite,
            // text at (9,0x8e), minimum display time 0xe8, then a touch.
            objectText_ = text;
            objectIcon_ = -1;
            const int minimum = duration(0xe8);
            for (int i = 0; i < minimum; ++i) {
                frame();
            }
            waitRelease();
            objectText_.clear();
            const unsigned e = state_.eventId;
            if (s && e != 14510 && e != 15420 && e != 16070) {
                s->bgmVolume(1.0f, float(duration(0xe9)) / 60.0f), wait(duration(0xea));
            }
            wait(2);
        }
    }
    // 020f3db4: a pending chained event (state +0x3da) starts next.
    if (state_.progress.pendingEvent() > 0 && !state_.chainBlocked) {
        state_.eventId = std::uint16_t(state_.progress.pendingEvent());
        state_.scene = scene::DramaEvent;
        state_.progress.setPendingEvent(-1);
        if (auto s = sound()) {
            s->bgmVolume(0.0f, 2.0f); // 0207fae4(0, 2.0)
        }
        wait(20);
        fade(false, 17);
        wait(90); // 02071e84(step 1, wait 1)
        return;
    }
    // 020f3e40: closing fade by data byte 5 (1 keeps the screens):
    // 02071dc0 with step 2 is nine frames on both screens.
    if (data[5] != 1) {
        fade(false, 9);
    }
    if (state_.eventId == 18480 && state_.progress.place() == 63) {
        state_.eventId = 18000;
        state_.scene = scene::DramaEvent;
        return;
    }
    state_.reward = -1; // 020f3eb4
    if (state_.teaComplete() && !state_.eventViewed(18490)) {
        if (state_.scene == scene::DramaEvent) {
            state_.savedEvent = state_.eventId;
        }
        state_.eventId = 18490;
        state_.setEventViewed(18490, true);
        state_.scene = scene::DramaEvent;
        return;
    }
    if (state_.eventId == 18490 && state_.savedEvent) {
        state_.eventId = state_.savedEvent;
        state_.scene = scene::DramaEvent;
        state_.savedEvent = 0;
    }
    if (state_.eventId == 18490 && state_.teaTutorial != 0) {
        state_.eventId = 0;
        state_.scene = scene::Menu;
        state_.teaTutorial = 0;
    } // 020f3fe0
    if (state_.scene == scene::Narration) {
        state_.roomSoundEvent = state_.current.sound;
    }
}

// Scene 6: plays the selected movie.
void StoryGame::movieScene() {
    // 02068ef4: movie overlay with the selected movie, then the following scene.
    movie(state_.eventId);
    state_.scene = state_.following;
    if (state_.movieReplay) {
        state_.scene = 36; // replayed from the movie theatre (+0x821c)
    }
}

// Scene 13: narrated text over pictures.
void StoryGame::narrationScene() {
    // 02069040: overlay 10, then the following scene. ov10 020efe7c picks the
    // letter by the current event ID, which 0x09 has already set to the
    // event after the narration: 10010 opening letter, 10030 the assistant's letter,
    // 13190 message2, 17190 end_message.
    const unsigned e = state_.eventId;
    const int mode = e == 10010 ? 0 : e == 10030 ? 1 : e == 13190 ? 2 : e == 17190 ? 3 : -1;
    if (mode < 0) {
        throw std::runtime_error("narration without a letter");
    }
    narration(unsigned(mode), int(e));
    state_.scene = state_.following;
}

std::uint8_t StoryGame::tutorialsSeen() const {
    // ov27 020f5314 tests TutorialFlag bit (tutorial - 1) (02082f58), the
    // same bit layout PuzzleSession::tutorialsSeen uses.
    std::uint8_t bits = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (state_.progress.tutorial(i)) {
            bits |= std::uint8_t(1u << i);
        }
    }
    return bits;
}

// Scene 10: a story puzzle (see a2_puzzle_game.cpp), with its progress stored
// in the save.
void StoryGame::puzzleScene() {
    // 020697e0: an unseen puzzle becomes seen; the puzzle overlays run; the
    // following scene becomes current (ov27 020f35d8 forces 8 for story
    // launches). The puzzle session is a view onto the save progress.
    const int internal = state_.puzzle.internal, slot = state_.puzzle.number;
    if (internal < 0 || slot < 0) {
        throw std::runtime_error("story puzzle selection");
    }
    // The passcode puzzle (internal 206) has number 0: its progress byte is
    // out of range of the bounded accessors (020826f0 reads 0, 02082710
    // ignores the write), so it is played without keeping a status.
    std::uint8_t unkept = 0;
    std::uint8_t& progressByte =
        slot >= 1 ? state_.progress.puzzleByte(std::uint32_t(slot - 1)) : unkept;
    if (state_.puzzleStatus(slot) == 0) {
        state_.setPuzzleStatus(slot, 1, false);
    }
    PuzzleSession session;
    session.first.packed = progressByte;
    session.coins = state_.progress.hintCoins();
    session.picarats = state_.progress.picarats();
    session.randomState = randomState_;
    session.tutorialsSeen = tutorialsSeen();
    PuzzleLaunch launch;
    launch.followingScene = state_.following;
    if (researchSolvePuzzle) {
        // Research exploration only: outcome supplied by the tool.
        session.first.enter();
        if ((session.lastJudgedCorrect = researchSolvePuzzle(unsigned(internal)))) {
            PuzzleData data(readFile(assets_.root / "puzzle" / ("n" + number(internal) + ".dat")),
                            text_);
            std::uint32_t reward = 0;
            session.award(unsigned(internal), launch.followingScene, data.picarats, reward);
            session.first.setStatus(2);
        }
    } else {
        PuzzleGame puzzle(assets_, subscenePresent(), name_, audio_, &session, unsigned(internal),
                          launch);
        puzzle.run();
    }
    state_.puzzleJudgedCorrect = session.lastJudgedCorrect; // +0x7ebe
    const bool solvedNow = (progressByte & 3) != 2 && session.first.status() == 2;
    progressByte = slot >= 1 ? session.first.packed : 0;
    if (solvedNow) {
        PuzzleData data(readFile(assets_.root / "puzzle" / ("n" + number(internal) + ".dat")),
                        text_);
        state_.reward = data.completionValue;
    } // ov27 020f25dc
    state_.progress.setHintCoins(session.coins);
    state_.progress.setPicarats(session.picarats);
    randomState_ = session.randomState;
    for (unsigned i = 0; i < 3; ++i) {
        if (session.tutorialsSeen & (1u << i)) {
            state_.progress.setTutorial(i, true);
        }
    }
    const auto f = state_.following;
    if (f != 11 && f != 1 && f != 19 && f != 22 && f != 31 && f != 24 && f != 44) {
        state_.following = scene::PuzzleResult;
    }
    state_.scene = state_.following;
    // 020697e0 tail (02069dec..02069ee4): a solved shack puzzle plays event
    // 18000 (reward, index notice, back to scene 44) inside the puzzle scene;
    // after a completed game with all 138 solved, 18480 follows once.
    if (state_.following == scene::Nazoba && state_.puzzleStatus(slot) == 2) {
        state_.eventId = 18000;
        eventScene();
        if (state_.eventViewed(18480) || !state_.cleared || !state_.allPuzzlesSolved(138)) {
            return;
        }
        state_.eventId = 18480;
        state_.setEventViewed(18480, true);
        state_.scene = scene::DramaEvent;
    }
}

// Scene 8: after a story puzzle, pick the follow-up event (solved or not).
void StoryGame::puzzleResultScene() {
    // 02069468: the result event is the puzzle event +3 when solved (+1 more
    // with +8220), +4 otherwise (internal 0x87: by the last judgment); -1 for
    // a retry variant, -2 with +821f.
    state_.following = scene::Room;
    const unsigned base = state_.eventId;
    unsigned result = base;
    // Internal 0x87 compares +0x479 with 0x13/0x16/0x1f after it was set to
    // 1 above, so its result always follows the last judgment (+0x7ebe).
    if (state_.puzzle.internal == 0x87) {
        result += state_.puzzleJudgedCorrect ? 3 : 4;
    } else if (state_.puzzleStatus(state_.puzzle.number) == 2) {
        result += 3;
        if (state_.resultAdjustB) {
            result += 1;
        }
    } else {
        result += 4;
    }
    if (state_.retryVariant) {
        result -= 1;
    }
    if (state_.resultAdjustA) {
        result -= 2;
    }
    if (!state_.eventViewed(18480) && false) {
    } // 18480 needs all-puzzle state (+821e); see docs
    state_.eventId = std::uint16_t(result);
    eventScene();
    state_.scene = state_.following;
    state_.retryVariant = false;
    state_.resultAdjustA = false;
    state_.resultAdjustB = false;
    if (state_.eventId >= 20000) {
        state_.eventId = std::uint16_t(base);
    }
}

// ---------------------------------------------------------------------------
// Room scene (ov7).

// Loads the current place's version: background pictures, the people and
// things that can be tapped, hint coins, exits.
void StoryGame::loadRoom() {
    // 020f06f8: place version first, then n_place%i_%i.dat, label p_%u.txt,
    // room-sound key. plc_data1 holds places below 40, plc_data2 the rest.
    state_.updatePlaceVersion();
    const auto place = state_.progress.place(), version = state_.progress.placeVersion();
    const auto b =
        readFile(assets_.root / "place" / (number(place) + "_" + number(version) + ".dat"));
    if (b.size() < 0x38e) {
        throw std::runtime_error("place data size");
    }
    Room r;
    r.label = b[0];
    r.markerX = b[0x18];
    r.markerY = b[0x19];
    r.bottom = b[0x1a];
    r.top = b[0x1b];
    r.sound = read16(b, 0x38c);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.coins[i][j] = b[0x1c + i * 4 + j];
        }
    }
    for (int i = 0; i < 16; ++i) {
        const auto o = std::size_t(0x2c + i * 10);
        if (!b[o] && !b[o + 1] && !b[o + 2] && !b[o + 3]) {
            continue;
        }
        r.objects.push_back({b[o], b[o + 1], b[o + 2], b[o + 3], b[o + 4], read16(b, o + 6)});
    }
    for (int i = 0; i < 12; ++i) {
        const auto o = std::size_t(0xcc + i * 32);
        std::string name(reinterpret_cast<const char*>(&b[o + 2]), 30);
        name = name.c_str();
        if (name.empty()) {
            break;
        }
        RoomAnimation a;
        a.x = b[o];
        a.y = b[o + 1];
        a.name = name.substr(0, name.rfind('.'));
        r.animations.push_back(a);
    }
    for (int i = 0; i < 16; ++i) {
        const auto o = std::size_t(0x24c + i * 8);
        if (!b[o] && !b[o + 1] && !b[o + 2] && !b[o + 3]) {
            break;
        }
        RoomEvent e;
        e.x = b[o];
        e.y = b[o + 1];
        e.w = b[o + 2];
        e.h = b[o + 3];
        e.sprite = b[o + 4];
        e.event = read16(b, o + 6);
        r.events.push_back(e);
    }
    for (int i = 0; i < 12; ++i) {
        const auto o = std::size_t(0x2cc + i * 12);
        if (!b[o] && !b[o + 1] && !b[o + 2] && !b[o + 3]) {
            break;
        }
        r.exits.push_back({b[o], b[o + 1], b[o + 2], b[o + 3], b[o + 4], b[o + 5], b[o + 7],
                           b[o + 8], b[o + 9], read16(b, o + 10)});
    }
    const auto label = assets_.root / "text" / "jiten" / ("p_" + number(int(r.label)) + ".txt");
    if (fileExists(label)) {
        r.name = loadText("jiten/p_" + number(int(r.label)) + ".txt");
    }
    state_.placeName = r.name; // +0x81e8, shown by the bag (ov17 020f126c)
    state_.roomSoundEvent = std::uint16_t(r.sound);
    room_ = std::move(r);
}

// Shows the room: music and ambience for the place, the fade-in, and the
// place's name.
void StoryGame::enterRoom() {
    // 020f08e4: room sound, backgrounds, background animations, event
    // objects (hidden once viewed for kind 1, or solved for kind 4) and exits.
    if (auto s = sound()) {
        s->roomSound(room_.sound, true);
        s->playUniqueEffect(100);
    }
    scene_ = SceneKind::Room;
    detail_ = state_.progress.place();
    actors_.clear();
    dialogueBox_ = false;
    bottom_ = "map/main" + number(room_.bottom);
    top_ = "map/map" + number(room_.top);
    topBrightness_ = bottomBrightness_ = 0;
    for (auto& a : room_.animations) {
        const auto& s = assets_.sprite("bgani/" + a.name + ".arc");
        a.animation.select(s.animations.size() > 1 ? 1 : 0, tick_, s.animations);
    }
    for (auto& e : room_.events) {
        e.visible = true;
        state_.loadEventInfo(e.event);
        if (state_.current.kind == 1 && state_.eventViewed(e.event)) {
            e.visible = false;
        } else if (state_.current.kind == 4 && state_.puzzleStatus(state_.puzzleNumber(
                                                   std::int16_t(state_.current.puzzle))) == 2) {
            e.visible = false;
        }
        if (e.sprite) {
            const auto& s = assets_.sprite("eventobj/obj_" + number(e.sprite) + ".arc");
            e.animation.select(s.animations.size() > 1 ? 1 : 0, tick_, s.animations);
        }
        // Tea-event selection (+0x434, ht_event, RNG) is not reconstructed (G43).
    }
    // 020f08e4 end: a place with a photo (sb_pht) shows the camera button
    // once the camera works and the photo is not taken yet.
    {
        const auto* photo = state_.photoByPlace(std::uint32_t(state_.progress.place()));
        roomPhoto_ = photo != nullptr;
        cameraVisible_ = false;
        if (photo && state_.progress.cameraSolved()) {
            cameraVisible_ = !state_.progress.photoTaken(photo[0]);
        }
    }
    roomVisible_ = true;
    moveMode_ = false;
    selectedExit_ = -1;
    objectText_.clear();
}

// One frame of exploring a room; returns true when the room is left.
// Taps are tried in the original order: event objects (people), hint coins,
// examinable spots, then the move-mode exits.
bool StoryGame::roomScene() {
    // 020f2118: one input step per frame in the original order. Returns true
    // when the room overlay ends (another scene was requested).
    {
        const auto& pos = assets_.sprite("map/menu_icon.arc").variables.at("pos");
        menuButton_ = Button({pos[0], pos[1], 28, 30}, 3);
    }
    {
        const auto& pos = assets_.sprite("map/camera_icon.arc").variables.at("pos");
        cameraButton_ = Button({pos[0], pos[1], 24, 24}, -1);
    }
    fade(true, 9); // 02071790(step 2, interval 1)
    for (;;) {
        exploring_ = true;
        frame();
        exploring_ = false;
        if (roomEventObjects()) {
            break;
        }
        if (roomHintCoins() && state_.scene != scene::Room) {
            break;
        }
        if (roomTouchObjects()) {
            continue;
        }
        // ov7 movemode.spr button (0x4fc) opens the exit selection (020f2948).
        if (touch_.released && Rect{228, 158, 28, 34}.contains(touch_.x, touch_.y)) {
            if (auto s = sound()) {
                s->playEffect(3);
            }
            if (roomMoveMode()) {
                break;
            }
            continue;
        }
        // ov7 menu_icon.spr button (+0x500, sound 3): after the button's six
        // updates the bag (scene 17) follows with following scene 1 (020f2118).
        menuButton_.update(touch_);
        if (menuButton_.consumeClick()) {
            state_.following = scene::Room;
            state_.scene = scene::Menu;
            break;
        }
        if (roomDirectExits()) {
            break;
        }
        // Camera button (+0x504, no click sound): take this place's photo.
        if (cameraVisible_) {
            cameraButton_.update(touch_);
            if (cameraButton_.consumeClick()) {
                takePhoto();
            }
        }
    }
    fade(false, 9);
    wait(20); // 02071e84(step 2, wait 1), 0206ce18(20)
    return true;
}

// The camera button in a room (once the camera is repaired): take a photo
// for the photo hunt.
void StoryGame::takePhoto() {
    // 020f2118: SE 0xec and a white flash on the lower screen (brightness 16
    // down to 0, one room frame each), then 020f3c8c: prize window with
    // tx 0xd5 and tx 0xd6 (each until a release, SE 3). The photo becomes
    // current (+0x81d1) and taken (TakePhotoFlag, 02082da4).
    cameraVisible_ = false;
    if (auto s = sound()) {
        s->playEffect(0xec);
    }
    for (int level = 16; level >= 0; --level) {
        bottomBrightness_ = level;
        frame();
    }
    const auto* photo = state_.photoByPlace(std::uint32_t(state_.progress.place()));
    prizeIcon_.clear();
    prizeY_ = 0x5a - 8;
    for (int id : {0xd5, 0xd6}) {
        prizeText_ = loadText("tx_" + number(id) + ".txt");
        waitRelease();
        if (auto s = sound()) {
            s->playEffect(3);
        }
    }
    prizeText_.clear();
    prizeY_ = 100;
    if (photo) {
        state_.photo = photo[0];
        state_.progress.setPhotoTaken(photo[0], true);
    }
}

bool StoryGame::puzzleEventSolved(unsigned event) {
    const auto* fix = state_.eventFix(event);
    return fix && state_.puzzleStatus(state_.puzzleNumber(
                      std::int32_t(read16(std::span<const std::uint8_t>(fix, 6), 2)))) == 2;
}

// Tapping a person or thing that starts an event.
bool StoryGame::roomEventObjects() {
    // 020f1c98: the first visible event object under the release point plays
    // its spotting sound and triggers its event (photo-piece and tea branches
    // are outside current coverage).
    if (!touch_.released) {
        return false;
    }
    for (const auto& e : room_.events) {
        if (!e.visible || !eventAnswers(e) || !eventRect(e).contains(touch_.x, touch_.y)) {
            continue;
        }
        spotSound(e.event);
        triggerRoomEvent(e.event);
        return true;
    }
    return false;
}

bool StoryGame::eventAnswers(const RoomEvent& e) const {
    // 020f1d40: the object of this place's photo puzzle answers only once
    // the photo hunt for it is complete.
    const auto* photo =
        roomPhoto_ ? state_.photoByPlace(std::uint32_t(state_.progress.place())) : nullptr;
    return !(photo && e.event == unsigned(photo[4] | photo[5] << 8) &&
             !state_.progress.photoComplete(photo[0]));
}

Rect StoryGame::eventRect(const RoomEvent& e) {
    if (e.sprite) {
        // ov2 02077800: hit test against the sprite's current frame bounds.
        const auto& s = assets_.sprite("eventobj/obj_" + number(e.sprite) + ".arc");
        if (e.animation.animation < s.animations.size() &&
            !s.animations[e.animation.animation].empty()) {
            const auto& p =
                s.images.at(s.animations[e.animation.animation][e.animation.step].image);
            return {e.x, e.y, int(p.width), int(p.height)};
        }
    }
    return {e.x, e.y, e.w, e.h};
}

int StoryGame::coinPlace() const {
    // 020f1ef8: place 92 shares 38's coin bits.
    const int place = state_.progress.place();
    return place == 0x5c ? 0x26 : place;
}

// The touch-area help (TouchArea): what the room's taps would start, by the
// rules of the tap handlers above - event objects (020f1c98; a puzzle event
// 20000..29999 whose puzzle is not solved yet plays its puzzle, 020f2e1c),
// hint coins not found yet (020f1ef8), examinable spots (020f1bfc).
std::vector<TouchArea> StoryGame::roomAreas() {
    std::vector<TouchArea> out;
    auto add = [&out](TouchArea::Kind kind, const Rect& r) {
        if (r.width > 0 && r.height > 0) {
            out.push_back({kind, r.x, r.y, r.width, r.height});
        }
    };
    for (const auto& e : room_.events) {
        if (!e.visible || !eventAnswers(e)) {
            continue;
        }
        const bool puzzle = e.event >= 20000 && e.event < 30000 && state_.eventFix(e.event) &&
                            !puzzleEventSolved(e.event);
        add(puzzle ? TouchArea::Kind::Puzzle : TouchArea::Kind::Dialog, eventRect(e));
    }
    for (int i = 0; i < 4; ++i) {
        const auto& c = room_.coins[std::size_t(i)];
        if (!state_.progress.hintCoin(std::uint32_t(coinPlace()), std::uint32_t(i))) {
            add(TouchArea::Kind::HintCoin, {c[0], c[1], c[2], c[3]});
        }
    }
    for (const auto& o : room_.objects) {
        add(TouchArea::Kind::Text, {o.x, o.y, o.w, o.h});
    }
    return out;
}

// Tapping a hidden hint coin: it is collected once per spot.
bool StoryGame::roomHintCoins() {
    // 020f1ef8: four coin rectangles per place.
    if (!touch_.released) {
        return false;
    }
    const int place = coinPlace();
    for (int i = 0; i < 4; ++i) {
        const auto& c = room_.coins[std::size_t(i)];
        if (state_.progress.hintCoin(std::uint32_t(place), std::uint32_t(i)) ||
            !Rect{c[0], c[1], c[2], c[3]}.contains(touch_.x, touch_.y)) {
            continue;
        }
        if (auto s = sound()) {
            s->playEffect(0x74);
        }
        // The coin flight animation (+0x50c, 0208076c) is omitted (G03).
        showObjectText(loadText("hintcoin.txt"), -1);
        state_.progress.setHintCoin(std::uint32_t(place), std::uint32_t(i), true);
        state_.progress.setHintCoins(std::uint16_t(state_.progress.hintCoins() + 1)); // 0208285c
        state_.progress.setHintCoinsFound(std::uint16_t(state_.progress.hintCoinsFound() + 1));
        if (place == 3 && i == 0) {
            state_.eventId = 10080;
            state_.scene = scene::DramaEvent;
        }
        return true;
    }
    return false;
}

// Tapping an examinable spot: a short description.
bool StoryGame::roomTouchObjects() {
    // 020f1bfc: the first object rectangle under the release opens its text.
    if (!touch_.released) {
        return false;
    }
    for (const auto& o : room_.objects) {
        if (!Rect{o.x, o.y, o.w, o.h}.contains(touch_.x, touch_.y)) {
            continue;
        }
        if (auto s = sound()) {
            s->playEffect(3); // 020f10c0 opens the object text
        }
        showObjectText(loadText("tobj" + number(int(o.text)) + ".txt"), o.icon + 1);
        return true;
    }
    return false;
}

void StoryGame::showObjectText(const std::string& text, int icon) {
    // 020f10c0: tobj/window.spr with the icon at (37,84) and the text at
    // (0x42,0x5c); waits for a touch. Window alpha ramps are omitted.
    objectText_ = text;
    objectIcon_ = icon;
    wait(15);
    waitRelease();
    objectText_.clear();
    objectIcon_ = -1;
    wait(16); // closing alpha ramp (15 frames) and one more frame
}

// Move mode: the exits of the room appear and can be tapped.
bool StoryGame::roomMoveMode() {
    // 020f2948: exits are shown; pressing selects the exit under the stylus,
    // releasing on it takes it; releasing anywhere else closes the mode.
    moveMode_ = true;
    selectedExit_ = -1;
    for (;;) {
        frame();
        if (touch_.pressed) {
            selectedExit_ = -1;
            for (std::size_t i = 0; i < room_.exits.size(); ++i) {
                const auto& e = room_.exits[i];
                if (Rect{e.x, e.y, e.w, e.h}.contains(touch_.x, touch_.y)) {
                    selectedExit_ = int(i);
                }
            }
        }
        if (touch_.released) {
            const int chosen = selectedExit_;
            if (chosen >= 0) {
                const auto& e = room_.exits[std::size_t(chosen)];
                if (!Rect{e.x, e.y, e.w, e.h}.contains(touch_.x, touch_.y)) {
                    selectedExit_ = -1;
                }
            }
            moveMode_ = false;
            if (selectedExit_ < 0) {
                return false;
            }
            const auto exit = room_.exits[std::size_t(selectedExit_)];
            selectedExit_ = -1;
            return takeExit(exit);
        }
    }
}

bool StoryGame::roomDirectExits() {
    // 020f2b5c: exits of type 1 or 3 also respond without move mode when
    // both the press and the release lie inside.
    if (!touch_.released) {
        return false;
    }
    for (const auto& e : room_.exits) {
        if (e.type != 1 && e.type != 3) {
            continue;
        }
        if (Rect{e.x, e.y, e.w, e.h}.contains(touch_.x, touch_.y)) {
            return takeExit(e);
        }
    }
    return false;
}

// Walks through an exit to another place.
bool StoryGame::takeExit(const RoomExit& e) {
    // 020f2620. Types 2/3 trigger an event; otherwise footsteps by floor
    // type, place change, automatic event check and the new room.
    if (unsigned(e.type - 2) < 2) {
        if (e.type == 3) {
            spotSound(e.target);
        } else if (auto s = sound()) {
            s->playEffect(0x73);
        }
        triggerRoomEvent(e.target);
        return true;
    }
    exitSound(e.type, e.floor, e.target);
    if (e.target == 63) {
        state_.scene = scene::Nazoba;
        return true;
    }
    state_.setPlace(std::int32_t(e.target));
    if (state_.findAutoEvent()) {
        state_.setEventViewed(state_.eventId, true);
        state_.scene = scene::DramaEvent;
        state_.following = scene::DramaEvent;
        return true;
    }
    state_.following = scene::Room;
    // 020f2620 waits for the tracked footstep (0207f9c0) before the entry.
    fade(false, 17, 1);
    if (auto s = sound()) {
        while (s->trackedBusy()) {
            frame();
        }
    }
    loadRoom();
    enterRoom();
    checkpoint();
    fade(true, 17, 1);
    return false;
}

void StoryGame::triggerRoomEvent(unsigned event) {
    // 020f2e1c: event variants. Puzzle events (20000..29999) use +1 when the
    // puzzle was seen and +2 when solved; talk events (>= 30000) +4 once
    // viewed. ev_inf2 kinds 1/2/5 then mark or advance the event.
    state_.retryVariant = false;
    state_.revisitVariant = false;
    if (event >= 30000) {
        if (state_.eventViewed(event)) {
            event += 4;
            state_.revisitVariant = true;
        } else {
            state_.setEventViewed(event, true);
        }
    } else if (event >= 20000) {
        if (const auto* fix = state_.eventFix(event)) {
            const auto status =
                state_.puzzleStatus(state_.puzzleNumber(std::int32_t(fix[2] | fix[3] << 8)));
            if (status == 1) {
                event += 1;
                state_.retryVariant = true;
                if (event == 21251) {
                    state_.setEventViewed(21251, true);
                }
            } else if (status == 2) {
                event += 2;
            }
        }
    }
    state_.loadEventInfo(event);
    switch (state_.current.kind) {
    case 1:
        state_.setEventViewed(event, true);
        break;
    case 2:
        if (state_.eventViewed(event)) {
            event += 1;
        } else {
            state_.setEventViewed(event, true);
        }
        break;
    case 5:
        if (const auto* fix = state_.eventFix(event)) {
            if (state_.solvedCount() >= unsigned(fix[2] | fix[3] << 8)) {
                event += 2;
            } else if (state_.eventViewed(event)) {
                event += 1;
            } else {
                state_.setEventViewed(event, true);
            }
        }
        break;
    default:
        break;
    }
    state_.eventId = std::uint16_t(event);
    state_.scene = scene::DramaEvent;
}

void StoryGame::spotSound(unsigned event) {
    // ov7 020f2c48: puzzle events (20000..29999) whose puzzle is not solved
    // play GE 0x6f, show the exclamation (native: not drawn, G10) and then
    // SY 0xd; other events play GE 0x73 and wait 10 frames.
    auto s = sound();
    const bool puzzle = event >= 20000 && event < 30000 && !puzzleEventSolved(event);
    if (puzzle) {
        if (s) {
            s->playEffect(0x6f);
            s->playEffect(0xd);
        }
    } else {
        if (s) {
            s->playEffect(0x73);
        }
        for (int i = 0; i < 10; ++i) {
            frame();
        }
    }
}

void StoryGame::exitSound(int type, int floor, unsigned target) {
    // ov7 020f2620: event exits (type 2/3) and footsteps by floor type.
    auto s = sound();
    if (!s) {
        return;
    }
    if (unsigned(type - 2) < 2) {
        if (type == 3) {
            spotSound(target);
        } else {
            s->playEffect(0x73);
        }
        return;
    }
    static constexpr int steps[5] = {0xe6, 0xe7, -1, 0xeb, 0xe9};
    if (unsigned(floor) < 5 && steps[floor] >= 0) {
        s->playTrackedEffect(steps[floor]);
    }
}

// ---------------------------------------------------------------------------

void StoryGame::draw(int actor, int alpha) {
    if (alpha != narrationAlpha_) {
        narrationFirst_ = actor;
        narrationEnd_ = actor;
    }
    narrationFirst_ = std::min(narrationFirst_, actor);
    narrationEnd_ = std::max(narrationEnd_, actor + 1);
    narrationAlpha_ = alpha;
}

// Narrated text pages over a picture.
void StoryGame::narration(unsigned mode, int event) {
    // ov10 020efe7c: room sound for the narrating event (+81d2), music on.
    // Mode table (letter sprite, bottom background, intro loop count +0x1e)
    // and page groups of 020f03f4 / 020f0790..020f099c.
    (void)event;
    if (mode > 3) {
        throw std::runtime_error("narration mode");
    }
    if (auto s = sound()) {
        s->roomSound(state_.roomSoundEvent, true);
    }
    narrationMode_ = mode;
    actors_.clear();
    scene_ = SceneKind::Story;
    detail_ = mode == 0 ? 10001 : mode == 1 ? 10021 : int(state_.eventId);
    static constexpr int introLoops[4]{0, 16, 12, 12};
    top_.clear();
    bottom_ = mode == 1 ? "lukeletter/luke_letter" : "map/main0";
    narrationAlpha_ = 0;
    fade(true, 17);
    wait(introLoops[mode] * 5);
    wait(25);
    if (mode == 1) {
        caption_ = loadText("tx_1400.txt");
        auto p = caption_.find("%s");
        if (p != caption_.npos) {
            caption_.replace(p, 2, name_.value());
        }
        wait(120);
        caption_.clear();
    }
    NarrationPage page;
    page.mode = std::uint8_t(mode);
    skip_ = false;
    // The pages answer the skip check (ov2 0206ca88) except the closing
    // message (mode 3), which the game lets play through.
    skippable_ = mode != 3;
    auto group = [&](int first, int end, int voice) {
        if (page.presentPages(*this, first, end, voice, mode == 0 ? 2 : 1)) {
            return true;
        }
        narrationAlpha_ = 0;
        return false;
    };
    if (mode == 0) {
        group(0, 3, 1);
    } else if (mode == 1) {
        if (!group(0, 2, 11)) {
            group(2, 4, 11);
        }
    } else if (mode == 2) {
        if (!group(0, 3, 156)) {
            group(3, 6, 156);
        }
    } else {
        if (!group(0, 1, 338) && !group(1, 2, 338)) {
            page.fadeMusicOnExit = true;
            group(2, 4, 338);
        }
    }
    // ov10 020efdb4: stop the voice and fade the music over time id 0x3ee.
    narrationAlpha_ = 0;
    if (auto s = sound()) {
        s->stopStream(s->duration(0x3ee));
        s->fadeOutBgm(s->duration(0x3ee));
    }
    skippable_ = false;
    fade(false, 17);
    wait(30);
    skip_ = false;
}

// Plays a film with its sound and subtitles (video/movie.hpp).
void StoryGame::movie(int) {
    if (movieId_ < 1 || movieId_ > 23) {
        throw std::runtime_error("movie id");
    }
    const auto movieName = "m" + number(movieId_);
    MovieFrames frames(assets_.root, movieName);

    struct Subtitle {
        int id;
        float first, last;
    };

    std::vector<Subtitle> subs;
    ScriptContext context;
    // 0xa2 MovieSubtitle: Schedules a subtitle between two timestamps.
    context.bind(0xa2, [&](CommandArguments& a) {
        int id = a.integer();
        float start = a.real(), end = a.real();
        subs.push_back({id, start, end});
    });
    if (fileExists(assets_.root / ("movie/" + movieName + ".gds"))) {
        ScriptInterpreter script(readFile(assets_.root / ("movie/" + movieName + ".gds")));
        script.run(context);
    }
    scene_ = SceneKind::Movie;
    detail_ = movieId_;
    top_.clear();
    bottom_.clear();
    actors_.clear();
    topBrightness_ = bottomBrightness_ = 0;
    roomVisible_ = false;
    // ov6 020f00e0: room sound of the requesting event (+81d2) with music,
    // wait until the BGM settles, open the movie audio (route 3) without
    // starting it, then start it once the video timer runs.
    auto s = sound();
    if (s) {
        s->roomSound(state_.roomSoundEvent, true);
        while (!s->bgmSettled()) {
            frame();
        }
        s->prepareStream(3, movieId_, -1, false);
        s->startStream(1, 0);
    }
    skip_ = false;
    skippable_ = true;
    const double secondsPerFrame = double(frames.denominator) / double(frames.numerator);
    for (std::uint64_t tick = 0;; ++tick) {
        auto i = tick * frames.numerator / (60ull * frames.denominator);
        if (i >= frames.count) {
            break;
        }
        movieImage_ = frames.frame(unsigned(i));
        movieSubtitle_.clear();
        for (const auto& sub : subs) {
            if (double(tick) / 60 >= sub.first && double(tick) / 60 < sub.last) {
                movieSubtitle_ = loadText(movieName + "_" + number(sub.id) + ".txt");
            }
        }
        frame();
        if (skip_) {
            break;
        }
        if (sleep_ && s) {
            // ov6 020f0558..020f0640: lid closed. Stop at once, pause, sleep,
            // two frames, resume; then restart the stream silently, wait until
            // it plays, move it to the current video frame and raise the volume.
            s->stopStreamFade(0);
            s->pauseSound();
            while (sleep_) {
                frame();
            }
            frame();
            frame();
            s->resumeSound();
            if (s->streamOpen()) {
                s->restartStream(0, 0);
                while (!s->streamBusy()) {
                    s->frame();
                }
                const float target = float(double(i) * secondsPerFrame);
                s->seekStream(target - s->streamPosition());
                s->streamVolume(1, 0);
            }
        }
    }
    // 020f0688..020f06d4: stop over 0.5 s and wait until the controller is idle.
    if (s) {
        s->stopStream(0.5f);
        while (!s->streamIdle()) {
            frame();
        }
    }
    movieImage_ = {};
    movieSubtitle_.clear();
    scene_ = SceneKind::Story;
    skippable_ = false;
    skip_ = false;
}

// Saves a checkpoint at the current scene boundary.
void StoryGame::checkpoint() {
    if (!saves_ || !checkpointEnabled_) {
        return;
    }
    Checkpoint c;
    c.name = name_;
    c.progress = state_.progress;
    c.scene = state_.scene;
    c.following = state_.following;
    c.eventId = state_.eventId;
    c.puzzle = state_.puzzle.internal;
    c.retryVariant = state_.retryVariant;
    c.revisitVariant = state_.revisitVariant;
    c.cleared = state_.cleared;
    c.randomState = randomState_;
    foldPlayTime(c.progress);
    saves_->save(c);
}

void StoryGame::foldPlayTime(SaveProgress& p) const {
    // The saved play time (+0x244, 64-bit seconds) includes this session.
    const auto seconds = playSeconds();
    p.put32(0x244, std::int32_t(seconds));
    p.put32(0x248, 0);
}

// The yes/no window used by save prompts and questions.
bool StoryGame::yesNo() {
    // Native yes/no window (prize window with system/btn yes/no at win_p),
    // used by the 0x7b save prompt adapter (G41).
    prizeY_ = 0x4c;
    prizeIcon_.clear();
    choiceButtons_ = true;
    choice_ = -1;
    Rect yes{50, 101, 66, 28}, no{140, 101, 66, 28};
    bool result = false;
    int armed = -1;
    for (;;) {
        frame();
        if (touch_.pressed) {
            armed = yes.contains(touch_.x, touch_.y) ? 0 : no.contains(touch_.x, touch_.y) ? 1 : -1;
        }
        choice_ = touch_.held && armed >= 0 && (armed ? no : yes).contains(touch_.x, touch_.y)
                      ? armed
                      : -1;
        if (touch_.released && armed >= 0 && (armed ? no : yes).contains(touch_.x, touch_.y)) {
            if (auto s = sound()) {
                s->playEffect(armed ? 4 : 3);
            }
            result = armed == 0;
            break;
        }
        if (touch_.released) {
            armed = -1;
        }
    }
    choiceButtons_ = false;
    choice_ = -1;
    prizeText_.clear();
    prizeY_ = 100;
    return result;
}

// The save file dialog over the current screen (script 0x7b).
void StoryGame::fileSave(int resume) {
    // ov8 020f2c84 -> 020866d0 on the event's screens; the native file
    // checkpoint also follows the original's resume point.
    SceneKit kit(assets_, subscenePresent(), audio_, SceneKind::Files);
    kit.upperBackground = top_;
    kit.lowerBackground = bottom_;
    kit.setUpperBrightness(topBrightness_);
    kit.setLowerBrightness(bottomBrightness_);
    if (saveFile) {
        FileDialog dialog(kit, state_, *saveFile, nullptr, false);
        dialog.save(resume, [this] { return foldForSave(); }, name_.value());
    }
    topBrightness_ = bottomBrightness_ = -16;
    if (resume) {
        checkpointAt(scene::DramaEvent, std::uint16_t(resume));
    } else {
        checkpointAt(scene::Room, 0);
    }
}

// 02083ff4: the session's play time goes into the progress block and the
// session clock restarts; returns the total seconds (02083f80).
std::uint32_t StoryGame::foldForSave() {
    foldPlayTime(state_.progress);
    sessionStartTick_ = tick_;
    return playSeconds();
}

void StoryGame::unavailable(const std::string& what) {
    // Native notice: a bag destination whose overlay is not reconstructed yet.
    scene_ = SceneKind::Bag;
    actors_.clear();
    dialogueBox_ = false;
    roomVisible_ = false;
    top_ = "menu/bag/ge/bag_sub";
    bottom_ = "menu/bag/ge/bag_bg";
    topBrightness_ = bottomBrightness_ = 0;
    prizeY_ = 0x4c;
    prizeIcon_.clear();
    prizeText_ = text_.expand(what) + "\nis not reconstructed yet.";
    waitRelease();
    prizeText_.clear();
    prizeY_ = 100;
    top_.clear();
    bottom_.clear();
}

std::uint32_t StoryGame::playSeconds() const {
    // 02083f80: saved seconds (+0x244, 64-bit) plus this session's OS ticks;
    // natively one DS frame is 560190 / 33513982 s. Capped at 99:59:59.
    const std::uint64_t saved = std::uint64_t(std::uint32_t(state_.progress.s32(0x244))) |
                                std::uint64_t(std::uint32_t(state_.progress.s32(0x248))) << 32;
    const std::uint64_t total =
        saved + std::uint64_t(tick_ - sessionStartTick_) * 560190ull / 33513982ull;
    return std::uint32_t(std::min<std::uint64_t>(total, 0x57e3f));
}

void StoryGame::checkpointAt(std::uint8_t sceneId, std::uint16_t event) {
    // Native stand-in for the original save menu: the checkpoint resumes the
    // given scene/event with the current progress.
    if (!saves_ || !checkpointEnabled_) {
        return;
    }
    Checkpoint c;
    c.name = name_;
    c.progress = state_.progress;
    c.scene = sceneId;
    c.following = state_.following;
    c.eventId = event;
    c.puzzle = state_.puzzle.internal;
    c.cleared = state_.cleared;
    c.randomState = randomState_;
    foldPlayTime(c.progress);
    saves_->save(c);
}

// Continues the story from a checkpoint.
void StoryGame::resume(const Checkpoint& c) {
    name_ = c.name;
    state_.progress = c.progress;
    state_.scene = c.scene;
    state_.following = c.following;
    state_.eventId = c.eventId;
    if (c.puzzle >= 0) {
        state_.loadPuzzle(c.puzzle);
    }
    state_.retryVariant = c.retryVariant;
    state_.revisitVariant = c.revisitVariant;
    state_.cleared = c.cleared;
    randomState_ = c.randomState;
    sessionStartTick_ = tick_;
    state_.updatePlaceVersion();
    sceneLoop();
}

void StoryGame::continueFrom(const GameState& loaded, bool secret) {
    state_.newGame(); // 02086c94 (the dialog's 02083368 before 020837fc)
    if (saveFile) {
        state_.shared = saveFile->shared(); // state +0x3e8 (read at boot)
        state_.consoleMac = saveFile->consoleAddress();
    }
    state_.progress = loaded.progress;
    state_.cleared = loaded.cleared;
    state_.saveSlot = loaded.saveSlot;
    sessionStartTick_ = tick_;
    if (secret) {
        state_.scene = 28;
        state_.chainBlocked = true; // +0x8221 (020f0658)
    } else if (const auto event = state_.continueEvent()) {
        state_.eventId = *event;
        state_.scene = scene::DramaEvent;
    } else {
        state_.scene = scene::Room; // the room loads its place version (020f06f8)
    }
    sceneLoop();
}

void StoryGame::replayMovie(unsigned event) {
    state_.newGame();
    checkpointEnabled_ = false;
    state_.movieReplay = true;
    state_.eventId = event;
    state_.scene = scene::DramaEvent;
    state_.following = 36;
    // The scene loop (02068ccc) until the theatre's turn; a room would mean
    // the event did not end in the theatre, so the replay ends there too.
    while (state_.scene != 36 && state_.scene != scene::Room) {
        dispatch();
    }
}

void StoryGame::playMinigame(std::uint8_t minigame, SaveProgress& progress) {
    state_.newGame();
    checkpointEnabled_ = false;
    state_.progress = progress;
    state_.scene = minigame;
    state_.following = scene::Menu;

    struct Keep { // copied back however the scene ends
        SaveProgress& out;
        const SaveProgress& in;

        ~Keep() { out = in; }
    } keep{progress, state_.progress};

    dispatch();
}

// Starts the story after the opening.
void StoryGame::run(bool puzzleShortcut) {
    state_.newGame();
    if (saveFile) {
        state_.shared = saveFile->shared();
        state_.consoleMac = saveFile->consoleAddress();
    }
    if (puzzleShortcut) {
        // Test shortcut (HostInput::puzzle, used by research tools): the
        // puzzle-001 event in the hallway of place 91, without replacing the
        // story checkpoint. Story flags before it are not set.
        checkpointEnabled_ = false;
        state_.setPlace(91);
        state_.eventId = 20000;
        state_.scene = scene::DramaEvent;
    }
    // After name entry the original writes scene 1 (ov12 -> 020837cc); the
    // room runner's automatic event then selects 18390.
    sceneLoop();
}
} // namespace picarat
