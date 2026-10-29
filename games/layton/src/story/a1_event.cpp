// The first adventure's event scene. See a1_event.hpp.
#include "story/a1_event.hpp"
#include "graphics/text.hpp"
#include "menus/a1_save.hpp"
#include "sound/a1_sound.hpp"
#include "system/a1_script.hpp"
#include "system/console_id.hpp"
#include <algorithm>
#include <cstdio>

namespace picarat::adventure1 {

namespace {
constexpr int kTextX = 0xe, kTextY = 0x8d; // 02065ffc(talk, 0xe, 0x8d)
constexpr int kLineStep = 0x10;

} // namespace

EventScene::EventScene(Story& s) : story(s), kit(s.kit) {
    // 02025d48.
    GameState& state = story.state;
    story.marker("event-ctor");
    if (story.sound && state.event != 0x154) {
        story.sound->setupEvent(int(state.event), [this] { story.stall(1); });
    }
    // BG priorities (BGxCNT): text 0, BG2 1, 3D 2, BG3 3.
    kit.lowerPriority = std::array<int, 4>{2, 0, 1, 3};
    if (state.event < state.events.size()) {
        state.events[state.event] |= 1;
    }
    story.actors.clear();
    story.upperActors.clear();
    story.speaker = -1;
    story.talking = false;
    story.actorsActive = true;
    window_ = kit.load("event_window_1.spr", 2, 0x7f, 1);
    kit.select(window_, "gfx");
    window_.x = 2;
    window_.y = 0x7f;
    prizeWindow_ = kit.load("prize_window.spr", 0x18, 0x38, 1);
    kit.select(prizeWindow_, "window");
    prizeWindow_.x = 0x18;
    prizeWindow_.y = 0x38;
    prize_ = kit.load("prize.spr", 0x74, 0x45, 1);
    prize_.x = 0x74;
    prize_.y = 0x45;
    cursor_ = kit.load("cursor_wait.spr", 0xec, 0xae, 1);
    kit.select(cursor_, "touch");
    cursor_.x = 0xec;
    cursor_.y = 0xae;
    windowStyle(2); // V1a0
    // The scene after the event unless its script names one: the room.
    state.scene = scene::Room;
    yes_.setSprite(kit, 0x28, 0x68, "?/room_buttons.spr", "yes");
    yes_.visibleIdle = true;
    yes_.setPressedAnimation(kit, "yesp");
    yes_.sound = 3;
    no_.setSprite(kit, 0x88, 0x68, "?/room_buttons.spr", "no");
    no_.visibleIdle = true;
    no_.setPressedAnimation(kit, "nop");
    no_.sound = 4;
    // The two hotel rooms' buttons for a piece of furniture (+0x40 / +0x44;
    // their sprite names are the program's, 02026570 / 02026580).
    auto roomButton = [&](Button& b, int x, std::uint32_t name) {
        b.setSprite(kit, x, 0x68, story.programString(name), "off");
        b.visibleIdle = true;
        b.setPressedAnimation(kit, "on");
        b.sound = 3;
    };
    roomButton(roomA_, 0x28, 0x02026570);
    roomButton(roomB_, 0x88, 0x02026580);
    // The choices' default texts (V180..V18c with the event's sel files).
    static const int answerRows[5] = {0x2a, 0x57, 0x84, 0x3b, 0x72};
    for (int i = 0; i < 5; ++i) {
        answerButtons_[i].setRect(0, answerRows[i], 0x100, 0x1a);
        answerButtons_[i].sound = 3;
    }
    if (story.sound && state.event != 0x154 && state.event != 0x156) {
        story.sound->startLoadedMusic(state.scene != 0);
    }
}

Story::Actor* EventScene::actor(int n) {
    return n >= 0 && n < int(story.actors.size()) ? &story.actors[std::size_t(n)] : nullptr;
}

Story::Actor* EventScene::upperActor(int n) {
    // The upper list is bounded by the lower list's count (0x1e8), as in
    // the original's checks.
    return n >= 0 && n < int(story.actors.size()) && n < int(story.upperActors.size())
               ? &story.upperActors[std::size_t(n)]
               : nullptr;
}

void EventScene::run() {
    // 0202aea4.
    story.marker("event-run");
    kit.clearText(SceneKit::Lower);
    // The bonus passcode's event (0x103) shows the code for the next game:
    // the console's code with seed 20043 (passed as a byte, 0x4b), its
    // characters 0x10 apart from (0x45, 0xaa) on the upper screen (0200e638).
    if (story.state.event == 0x103) {
        const std::string code = consoleCode(consoleAddress(story.saveFile.parent_path()), 0x4b);
        const Font& font = kit.assets.font("fontevent.NFTR");
        Surface& layer = kit.textLayer(SceneKit::Upper);
        for (int i = 0; i < 8; ++i) {
            if (code[std::size_t(i)] != ' ') {
                textGlyph(layer, font, std::uint8_t(code[std::size_t(i)]), 0x45 + 0x10 * i, 0xaa,
                          textColour(0xff));
            }
        }
    }
    char path[48];
    std::snprintf(path, sizeof path, "script/event/e%u.gds", story.state.event);
    story.runScript(path, commands());
    story.speaker = -1;
    story.talking = false;
    showShackList();
}

void EventScene::finish() {
    // 02026994.
    GameState& state = story.state;
    if (state.afterPuzzle == 1 && state.puzzle.solved) {
        state.progress(state.puzzleNumber) |= 4;
    }
    story.actors.clear();
    story.upperActors.clear();
    kit.clearText(SceneKit::Upper);
    kit.clearText(SceneKit::Lower);
    kit.lowerBackground.clear();
    kit.upperBackground.clear();
    kit.lowerOverlay.clear();
    kit.lowerTint = {};
    story.present();
}

// ---------------------------------------------------------------------------
// The talk window.

void EventScene::drawWindow(int alpha) {
    // 02066038: the window, then the speaker's name plate (02066004).
    kit.draw(window_, alpha);
    if (talkSpeaker_ >= 0) {
        if (const auto* a = actor(talkSpeaker_); a && a->plate >= 0) {
            kit.draw(plates_[std::size_t(a->plate)]);
        }
    }
}

void EventScene::windowStyle(int style) {
    // 02028100 / 020281b0 / 02028260: name plates to (4, 0x7c); three
    // frames, the window's animation, three frames.
    if (style == 3) {
        fixedStyle_ = true;
    }
    for (auto& p : plates_) {
        p.x = 4;
        p.y = 0x7c;
    }
    for (int i = 0; i < 3; ++i) {
        story.frame([] {});
    }
    kit.select(window_, unsigned(style));
    for (int i = 0; i < 3; ++i) {
        story.frame([] {});
    }
}

void EventScene::waitTouch() {
    // 0206629c: the cursor fades in until a touch (or A).
    if (story.sound) {
        story.sound->retryTyping();
    }
    story.present();
    for (int alpha = 2;;) {
        story.poll();
        drawWindow(0x1f);
        cursor_.x = 0xea;
        cursor_.y = 0xad;
        kit.draw(cursor_, alpha);
        story.present();
        if (story.touch.released) {
            return;
        }
        if (alpha < 0x1f) {
            ++alpha;
        }
    }
}

void EventScene::typeText(const std::string& text) {
    // 02066340: one character a frame; the typing tick on every fifth; a
    // touch draws the rest up to the next "@p" at once.
    if (story.sound) {
        story.sound->setTypingEffect(0x29);
    }
    const Font& font = kit.assets.font("fontevent.NFTR");
    Surface& layer = kit.textLayer(SceneKit::Lower);
    int x = kTextX, y = kTextY, colour = 0xff;
    bool skip = false;
    auto tick = [&](std::size_t i) {
        if (!skip && i % 5 == 0 && story.sound) {
            story.sound->typingSound([this] { story.stall(1); });
        }
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (story.sound) {
            story.sound->retryTyping();
        }
        story.talking = true;
        if (!skip) {
            story.poll();
            drawWindow(0x1f);
        }
        const auto c = std::uint8_t(text[i]);
        if (c == '\n') {
            y += kLineStep;
            x = kTextX;
            tick(i);
        } else if (c == '@' && i + 1 < text.size()) {
            const char code = text[++i];
            switch (code) {
            case '1':
            case '2':
            case '3':
                if (story.sound) {
                    story.sound->setTypingEffect(0x27 + (code - '0'));
                }
                break;
            case 'c':
            case 'C':
                kit.clearText(SceneKit::Lower);
                x = kTextX;
                y = kTextY;
                while (i + 1 < text.size() && (text[i + 1] == '\n' || text[i + 1] == '\r')) {
                    ++i;
                }
                break;
            case 'p':
            case 'P':
                if (skip) {
                    story.poll();
                    drawWindow(0x1f);
                }
                skip = false;
                story.talking = false;
                waitTouch();
                story.talking = true;
                story.poll();
                drawWindow(0x1f);
                break;
            case 'w':
            case 'W':
                if (i + 1 < text.size()) {
                    ++i;
                    if (!skip) {
                        story.stall((text[i] - '0') * 10);
                    }
                }
                break;
            default:
                break;
            }
        } else if (c == '#' && i + 1 < text.size()) {
            const char k = text[++i];
            colour = k == 'R' ? 0xfc : k == 'G' ? 0xfb : k == 'W' ? 0xfe : 0xff;
        } else if (c == '&') {
            // "&SetAni n name&": character n (and its part) changes animation.
            std::string command;
            while (i + 1 < text.size() && text[i + 1] != '&') {
                command += text[++i];
            }
            ++i;
            char name[64] = {};
            int n = 0;
            if (std::sscanf(command.c_str(), "SetAni %d %63s", &n, name) == 2) {
                if (auto* a = actor(n)) {
                    story.setActorAnimation(*a, name);
                    if (auto* part = actor(a->part)) {
                        story.setActorAnimation(*part, name);
                    }
                }
            }
        } else {
            textGlyph(layer, font, c, x, y, textColour(colour));
            x += textAdvance(font, c) + 1;
            tick(i);
        }
        if (!skip) {
            story.present();
        }
        if (story.touch.released) {
            skip = true;
        }
    }
    if (story.sound) {
        story.sound->retryTyping();
    }
    story.talking = false;
}

void EventScene::talk(int number, int speaker) {
    // 0202ac28.
    GameState& state = story.state;
    talkSpeaker_ = speaker;
    const unsigned event = state.event;
    const char* folder = event < 100 ? "e000" : event < 200 ? "e100" : "e200";
    char name[64];
    std::snprintf(name, sizeof name, "e%u_t%d.txt", event, number);
    std::string text = story.text("etext", folder, name);
    if (text.empty() && !story.hasText("etext", folder, name)) {
        std::snprintf(name, sizeof name, "Event Text: E%u_T%d.txt is missing!", event, number);
        text = name;
    }
    if (story.sound) {
        story.sound->retryTyping();
    }
    // The speaking actor: its face part when it has one (0200f040).
    int talking = speaker;
    if (const auto* a = actor(speaker); a && a->part >= 0) {
        talking = a->part;
    }
    story.speaker = talking;
    // The window's tail points at the speaker unless the script fixed it.
    if (!fixedStyle_ && speaker >= 1 && speaker <= 15) {
        if (const auto* a = actor(speaker)) {
            if (a->x <= 0x7f) {
                windowStyle(2);
            } else if (a->x > 0x80) {
                windowStyle(1);
            }
        }
    }
    fixedStyle_ = false;
    // 020661d8: the window fades in.
    for (int alpha = 1; alpha < 0x1f; alpha += 2) {
        story.frame([&] { drawWindow(alpha); });
    }
    typeText(text);
    story.poll();
    drawWindow(0x1f);
    waitTouch();
    kit.clearText(SceneKit::Lower);
    // 0206622c: the window fades out.
    for (int alpha = 0x1f; alpha > 1; alpha -= 2) {
        story.frame([&] { drawWindow(alpha); });
    }
    story.frame([] {});
    story.stall(2);
    story.speaker = -1;
}

// ---------------------------------------------------------------------------
// Choices.

void EventScene::choose(int count) {
    // 020274b0 (three answers) / 02027828 (two): the lower picture is
    // replaced by the choice picture, the question centred at the top, the
    // answers in rows; an answer held shows red, one whose flag is already
    // set green. The answer's flag is set and its number kept (+0x714).
    GameState& state = story.state;
    fadeOut(kit, Screens::Lower, 2);
    const std::string saved = kit.lowerBackground;
    const auto savedTint = kit.lowerTint;
    story.actorsActive = false;
    story.poll();
    kit.lowerBackground = "choice_bg";
    kit.lowerTint = {};
    Button* buttons = count == 3 ? answerButtons_ : answerButtons_ + 3;
    static const int rows3[3] = {0x32, 0x5f, 0x8c}, rows2[2] = {0x3b, 0x72};
    auto draw = [&] {
        kit.clearText(SceneKit::Lower);
        drawCentredText(kit, SceneKit::Lower, 0xc, question_, false, 0, 0x80, 0xff);
        for (int i = 0; i < count; ++i) {
            buttons[i].update(kit, story.touch);
            int colour = 0xff;
            if (buttons[i].held(story.touch)) {
                colour = 0xfc;
            } else if (answerFlags_[i] != -1 && state.flag(answerFlags_[i])) {
                colour = 0xfb;
            }
            drawTextBox(kit, SceneKit::Lower, 0x12, count == 3 ? rows3[i] : rows2[i], answers_[i]);
            (void)colour; // (the answers' colours: drawTextBox keeps black for now)
        }
    };
    draw();
    story.present();
    fadeIn(kit, Screens::Lower, 2);
    state.choice = -1;
    for (;;) {
        story.poll();
        draw();
        story.present();
        int chosen = -1;
        for (int i = 0; i < count; ++i) {
            if (buttons[i].clicked(kit, story.touch)) {
                chosen = i;
                break;
            }
        }
        if (chosen >= 0) {
            state.choice = chosen + 1;
            if (answerFlags_[chosen] != -1) {
                state.setFlag(answerFlags_[chosen], true);
            }
            break;
        }
    }
    fadeOut(kit, Screens::Lower, 2);
    story.actorsActive = true;
    story.poll();
    kit.clearText(SceneKit::Lower);
    kit.lowerBackground = saved;
    kit.lowerTint = savedTint;
    story.present();
    fadeIn(kit, Screens::Lower, 2);
}

// ---------------------------------------------------------------------------
// Prize windows.

template <class Draw>
void EventScene::prizeWindow(const std::string& text, int textY, Draw draw) {
    // The common shape of 02028f74 / 02029238 / 02029790 ...: sound 0xd,
    // the window in (alpha 1..31, two a frame), the text centred, the
    // cursor fading in until a touch, then the window out.
    if (story.sound) {
        story.sound->effect(0xd);
    }
    for (int a = 1; a < 0x20; a += 2) {
        story.frame([&] {
            kit.draw(prizeWindow_, a);
            draw(a);
        });
    }
    drawCentredText(kit, SceneKit::Lower, textY, text);
    cursor_.x = 0xd4;
    cursor_.y = 0x74;
    for (int k = 2;;) {
        story.frame([&] {
            kit.draw(prizeWindow_, 0x1f);
            draw(0x1f);
            kit.draw(cursor_, k);
        });
        if (k < 0x1f) {
            ++k;
        }
        if (story.touch.released) {
            break;
        }
    }
    kit.clearText(SceneKit::Lower);
    cursor_.x = 0xea;
    cursor_.y = 0xad;
    for (int a = 0x1f; a > 1; a -= 2) {
        story.frame([&] {
            kit.draw(prizeWindow_, a);
            draw(a);
        });
    }
    story.frame([] {});
    story.stall(2);
}

void EventScene::item(int n) {
    // 02028f74: the item's picture (prize.spr animation n) and stext
    // p_<n>.txt in sys_300.txt; the item joins the list (02019afc).
    std::string name = story.text("stext", "stext", "p_" + std::to_string(n) + ".txt");
    if (name.empty()) {
        name = "missing p_" + std::to_string(n) + ".txt";
    }
    for (auto& slot : story.state.items) {
        if (slot == 0) {
            slot = std::uint8_t(n);
            break;
        }
    }
    std::string format = story.text("stext", "stext", "sys_300.txt");
    std::string line = format;
    if (const auto at = format.find("%s"); at != std::string::npos) {
        line.replace(at, 2, name);
    }
    kit.select(prize_, unsigned(n));
    prizeWindow(line, 0x6c, [&](int alpha) { kit.draw(prize_, alpha); });
}

void EventScene::puzzleNotice() {
    // 02029238: the current puzzle's number and title (sys_201.txt); then,
    // when 0201bbe4 says so, sys_202.txt.
    const GameState& state = story.state;
    const int id = state.puzzleNumber;
    const auto* info = story.catalogue.find(unsigned(id));
    std::string title = info ? info->title : "NO TITLE";
    std::string format = story.text("stext", "stext", "sys_201.txt");
    char line[512];
    std::snprintf(line, sizeof line, format.c_str(), info ? int(info->number) : 0, title.c_str());
    prizeWindow(line, 0x5a, [](int) {});
}

void EventScene::gizmo(int n) {
    // 02029790: a robot dog part (dogi_<n>.spr), sys_301.txt.
    if (n < 1 || n > 0x20) {
        return;
    }
    auto picture = kit.load("dogi_" + std::to_string(n) + ".spr", 0x74, 0x45, 1);
    picture.x = 0x74;
    picture.y = 0x45;
    auto& gizmos = story.state.gizmos;
    if (std::find(gizmos.begin(), gizmos.end(), std::uint8_t(n)) != gizmos.end()) {
        return; // 0201b23c: already found
    }
    if (auto free = std::find(gizmos.begin(), gizmos.end(), 0); free != gizmos.end()) {
        *free = std::uint8_t(n); // 0201b09c
    }
    story.state.newGizmo = true; // +0x705
    prizeWindow(story.text("stext", "stext", "sys_301.txt"), 0x6c,
                [&](int) { kit.draw(picture, 0x1f); });
}

void EventScene::paintingPiece(int n) {
    // 02029a4c: a piece of the torn painting (jicon.spr, a random one of
    // three), sys_302.txt.
    if (n < 1 || n > 0x14 || story.state.pieceOwned[std::size_t(n - 1)]) {
        return; // 0201b2f0
    }
    auto picture = kit.load("jicon.spr", 0x74, 0x45, 1);
    picture.x = 0x74;
    picture.y = 0x45;
    story.state.pieceOwned[std::size_t(n - 1)] = 1; // 0201b2b4
    story.state.piecePlace[std::size_t(n - 1)] = 0xff;
    story.state.newPainting = true; // +0x707
    kit.select(picture, unsigned(story.randomBelow(3) + 1));
    prizeWindow(story.text("stext", "stext", "sys_302.txt"), 0x6c,
                [&](int) { kit.draw(picture, 0x1f); });
}

void EventScene::furniture(int n) {
    // 02029d2c: a piece of furniture (f_<n>.spr) for one of the two hotel
    // rooms (each holds 20, 0201b60c / 0201b638). With room in both the
    // player chooses (sys_303.txt naming the piece, htext i_<n>.txt); with
    // one room full it goes to the other; with both full nothing happens.
    if (n < 1 || n > 0x20) {
        return;
    }
    auto& state = story.state;
    auto count = [](const auto& list) {
        return std::count(list.begin(), list.end(), 1);
    };
    const bool fullA = count(state.furnitureA) > 0x13, fullB = count(state.furnitureB) > 0x13;
    if (fullA && fullB) {
        return;
    }
    int room = fullA ? 1 : fullB ? 0 : -1;
    if (state.furnitureA[std::size_t(n - 1)] || state.furnitureB[std::size_t(n - 1)]) {
        return;
    }
    state.newFurniture = true; // +0x706
    auto picture = kit.load("f_" + std::to_string(n) + ".spr", 0x28, 0x42, 1);
    picture.x = 0x28;
    picture.y = 0x42;
    if (room < 0) {
        if (story.sound) {
            story.sound->effect(0xd);
        }
        for (int a = 1; a < 0x20; a += 2) {
            story.frame([&] { kit.draw(prizeWindow_, a); });
        }
        std::string name = story.text("htext", "htext", "i_" + std::to_string(n) + ".txt");
        if (name.empty()) {
            name = "NO FILE : " + std::to_string(n);
        }
        std::string question = story.text("stext", "stext", "sys_303.txt");
        if (const auto at = question.find("%s"); at != question.npos) {
            question.replace(at, 2, name);
        }
        story.poll();
        roomA_.cancel();
        roomB_.cancel();
        drawCentredText(kit, SceneKit::Lower, 0x48, question, false, 0, -0x46);
        auto frame = [&] {
            kit.draw(prizeWindow_, 0x1f);
            kit.draw(picture, 0x1f);
            roomA_.update(kit, story.touch);
            roomB_.update(kit, story.touch);
        };
        frame();
        story.present();
        for (;;) {
            story.frame(frame);
            if (roomA_.clicked(kit, story.touch)) {
                room = 0;
                break;
            }
            if (roomB_.clicked(kit, story.touch)) {
                room = 1;
                break;
            }
        }
        kit.clearText(SceneKit::Lower);
        for (int a = 0x1f; a > 1; a -= 2) {
            story.frame([&] { kit.draw(prizeWindow_, a); });
        }
        story.frame([] {});
        story.stall(2);
    }
    (room == 0 ? state.furnitureA : state.furnitureB)[std::size_t(n - 1)] = 1;
    picture.x = 0x74;
    picture.y = 0x45;
    kit.select(picture, 1u);
    prizeWindow(story.text("stext", "stext", room == 0 ? "sys_304.txt" : "sys_305.txt"), 0x6c,
                [&](int a) {
                    if (a >= 0x1f) {
                        kit.draw(picture, 0x1f);
                    }
                });
}

void EventScene::mystery(int n) {
    // 0202a930: a new mystery (flag 299 + n, storytext it_<n>.txt in
    // sys_200.txt).
    if (n < 1 || n >= 0xb) {
        return;
    }
    story.state.setFlag(n + 299, true);
    std::string name = story.text("storytext", "storytext", "it_" + std::to_string(n) + ".txt");
    std::string format = story.text("stext", "stext", "sys_200.txt");
    char line[512];
    std::snprintf(line, sizeof line, format.c_str(), name.c_str());
    prizeWindow(line, 0x5a, [](int) {});
}

void EventScene::shackPuzzle(int n) {
    // 0202a384: an unsolved puzzle goes to the shack (flag 800 + n); its
    // line (sys_400.txt: number, title) is listed when the event ends.
    if (n < 1 || n > 0x100 || story.state.solved(n) || shackLines_.size() >= 0x40) {
        return;
    }
    story.state.setFlag(n + 800, true);
    const auto* info = story.catalogue.find(unsigned(n));
    std::string format = story.text("stext", "stext", "sys_400.txt");
    char line[256];
    std::snprintf(line, sizeof line, format.c_str(), info ? int(info->number) : 0,
                  info ? info->title.c_str() : "NO TITLE");
    shackLines_.push_back(line);
}

void EventScene::showShackList() {
    // 0202a5dc: the puzzles sent to the shack, eight lines a page.
    if (shackLines_.empty()) {
        return;
    }
    kit.clearText(SceneKit::Upper);
    kit.clearText(SceneKit::Lower);
    story.actors.clear();
    story.upperActors.clear();
    story.present();
    story.poll();
    kit.lowerBackground = "nazobamemo_bg";
    kit.lowerTint = {};
    kit.upperBackground = "nazobamemo_sub";
    auto touch = kit.load("?/qend_touch.spr", 0, 0, 1);
    kit.select(touch, "touch");
    touch.x = (0x100 - kit.frameWidth(touch)) >> 1;
    touch.y = (0xc0 - kit.frameHeight(touch)) >> 1;
    auto message = kit.load("?/nazoba_mes.sbj", 0x28, 0x3c, 1);
    message.x = 0x28;
    message.y = 0x3c;
    auto page = [&](std::size_t first) {
        kit.clearText(SceneKit::Lower);
        for (std::size_t i = first, y = 0x12; i < shackLines_.size() && i < first + 8;
             ++i, y += 0x16) {
            drawTextBox(kit, SceneKit::Lower, 0x1d, int(y), shackLines_[i]);
        }
    };
    page(0);
    story.frame([&] { kit.draw(message); });
    if (story.sound) {
        story.sound->effect(0xd);
    }
    fadeIn(kit, Screens::Both, 2);
    for (std::size_t first = 0;;) {
        for (int frame = 0;; ++frame) {
            story.frame([&] {
                kit.draw(message);
                if (frame > 0x1e) {
                    kit.draw(touch, std::min(0x14, frame - 0x1e));
                }
            });
            if (story.touch.released) {
                break;
            }
        }
        if (story.sound) {
            story.sound->effect(3);
        }
        first += 8;
        if (first >= shackLines_.size()) {
            break;
        }
        page(first);
    }
    fadeOut(kit, Screens::Both, 2);
    shackLines_.clear();
}

void EventScene::savePrompt() {
    // 02028c8c: "save?" (sys_101.txt, or sys_102.txt in event 0x156) with
    // yes / no; yes opens the save screen.
    if (story.sound) {
        story.sound->setupScene(4, [this] { story.stall(1); });
        story.sound->effect(0xd);
    }
    for (int a = 1; a < 0x20; a += 2) {
        story.frame([&] { kit.draw(prizeWindow_, a); });
    }
    const std::string text =
        story.text("stext", "stext", story.state.event == 0x156 ? "sys_102.txt" : "sys_101.txt");
    drawCentredText(kit, SceneKit::Lower, 0x48, text);
    bool save = true;
    for (;;) {
        story.frame([&] {
            kit.draw(prizeWindow_, 0x1f);
            yes_.update(kit, story.touch);
            no_.update(kit, story.touch);
        });
        if (no_.clicked(kit, story.touch)) {
            save = false;
            break;
        }
        if (yes_.clicked(kit, story.touch)) {
            break;
        }
    }
    kit.clearText(SceneKit::Lower);
    for (int a = 0x1f; a > 1; a -= 2) {
        story.frame([&] { kit.draw(prizeWindow_, a); });
    }
    story.frame([] {});
    story.stall(2);
    if (save) {
        SaveScreen(story, SaveScreen::From::Event, prizeWindow_).run();
    }
}

// ---------------------------------------------------------------------------
// The commands.

ScriptContext EventScene::commands() {
    ScriptContext c = story.commonCommands();
    // 0x0b: the lower picture; the event's virtual +0x60 is empty. A new
    // picture clears the tint.
    c.rebind(command::MainBackground, [this](CommandArguments& a) {
        const std::string name = a.string();
        a.integer();
        story.setPicture(kit.lowerBackground, name);
        kit.lowerTint = {};
    });
    // 0x5a (V164 02026d88): the place's name (storytext map<N>.txt).
    c.rebind(0x5a, [this](CommandArguments& a) {
        const int map = a.integer();
        for (int i = 0; i < 4; ++i) {
            a.integer();
        }
        story.state.place =
            story.text("storytext", "storytext", "map" + std::to_string(map) + ".txt");
    });
    // 0x6c (V1cc 020279d0): a character (x, y, sprite, animation) and its
    // name plate (?/<sprite>_n.spr) when there is one.
    c.rebind(0x6c, [this](CommandArguments& a) {
        const int x = a.integer(), y = a.integer();
        const std::string sprite = a.string(), animation = a.string();
        if (story.actors.size() >= 16) {
            return;
        }
        const int n = story.addActor(false, x, y, sprite, animation);
        std::string base = sprite;
        if (base.size() > 4) {
            base.resize(base.size() - 4);
        }
        const std::string plate = "?/" + base + "_n.spr";
        auto localized = kit.localized(plate);
        localized.replace(localized.size() - 4, 4, ".arc");
        if (n >= 0 && fileExists(story.pack / "ani" / (localized + ".l2sp")) &&
            plates_.size() < 16) {
            plates_.push_back(kit.load(plate, 4, 0x7c, 1));
            plates_.back().x = 4;
            plates_.back().y = 0x7c;
            story.actors[std::size_t(n)].plate = int(plates_.size()) - 1;
        }
    });
    // 0x6d (V1d0 02027b34): a face part of character `owner`; it takes the
    // owner's alpha and flip, and its kind (+0x1c with 1, 0205a108) is 1 -
    // drawn only while its character talks (0200f0c4) - unless its sprite is
    // a mouth (02007d5c), kind 2. Hiding the owner (0x72) leaves the part
    // alone, so a quiet face is not drawn at all (event 31's map tutorial).
    c.rebind(0x6d, [this](CommandArguments& a) {
        const int x = a.integer(), y = a.integer();
        const std::string sprite = a.string(), animation = a.string();
        const int owner = a.integer();
        if (!actor(owner) || story.actors.size() >= 16) {
            return;
        }
        const int n = story.addActor(false, x, y, sprite, animation);
        if (n < 0) {
            return;
        }
        auto& part = story.actors[std::size_t(n)];
        auto& o = story.actors[std::size_t(owner)];
        if (part.kind != 2) {
            part.kind = 1;
        }
        o.part = n;
        part.flip = o.flip;
        part.alpha = o.alpha;
    });
    c.rebind(0x6e, [this](CommandArguments& a) { // V1d4: animation
        const int n = a.integer();
        const std::string name = a.string();
        if (auto* p = actor(n)) {
            story.setActorAnimation(*p, name);
        }
    });
    c.rebind(0x6f, [this](CommandArguments& a) { // V1d8: the part's animation
        const int n = a.integer();
        const std::string name = a.string();
        if (auto* p = actor(n); p && p->part >= 0) {
            story.setActorAnimation(story.actors[std::size_t(p->part)], name);
        }
    });
    c.rebind(0x70, [this](CommandArguments& a) { // V1dc: place at once
        const int n = a.integer(), x = a.integer(), y = a.integer();
        if (auto* p = actor(n)) {
            p->x = p->targetX = x;
            p->y = p->targetY = y;
        }
    });
    c.rebind(0x71, [this](CommandArguments& a) { // V1e0: show
        if (auto* p = actor(a.integer())) {
            p->visible = true;
        }
    });
    c.rebind(0x72, [this](CommandArguments& a) { // V1e4: hide
        if (auto* p = actor(a.integer())) {
            p->visible = false;
        }
    });
    // Alpha (+0x10) and its change per frame (+0x14), for the character
    // and its part.
    auto setFade = [this](int n, float fade) {
        if (auto* p = actor(n)) {
            p->fade = fade;
            if (auto* part = actor(p->part)) {
                part->fade = fade;
            }
        }
    };
    auto setAlpha = [this](int n, float alpha) {
        if (auto* p = actor(n)) {
            p->alpha = alpha;
            if (auto* part = actor(p->part)) {
                part->alpha = alpha;
            }
        }
    };
    auto frames = [this](int n) {
        for (int i = 0; i < n; ++i) {
            story.frame([] {});
        }
    };
    c.rebind(0x78, [=](CommandArguments& a) { // V1e8
        const int n = a.integer();
        setFade(n, a.real());
    });
    c.rebind(0x79, [=](CommandArguments& a) { // V1ec, then 3 frames
        const int n = a.integer();
        setAlpha(n, a.real());
        frames(3);
    });
    c.rebind(0xaf, [=](CommandArguments& a) { // 0201531c: fade in quickly
        const int n = a.integer();
        setAlpha(n, 0);
        frames(3);
        setFade(n, 6.0f);
        frames(0xc);
    });
    c.rebind(0xb0, [=](CommandArguments& a) { // 020153e4
        const int n = a.integer();
        setAlpha(n, 0);
        frames(3);
        setFade(n, 6.0f);
        frames(6);
    });
    c.rebind(0xb3, [=](CommandArguments& a) { // 02015564: fade out
        const int n = a.integer();
        setFade(n, -5.0f);
        frames(6);
    });
    c.rebind(0x8f, [this](CommandArguments& a) { // V1f4: jitter frames
        const int n = a.integer(), count = a.integer();
        if (auto* p = actor(n)) {
            p->shake = count;
        }
    });
    c.rebind(0x90, [this](CommandArguments& a) { // V1f8: movement mode
        const int n = a.integer();
        const std::string mode = a.string();
        if (auto* p = actor(n)) {
            if (mode == "Normal") {
                p->moveToTarget = false;
            } else if (mode == "Move To Target") {
                p->moveToTarget = true;
            }
        }
    });
    c.rebind(0x91, [this](CommandArguments& a) { // V1fc: target
        const int n = a.integer(), x = a.integer(), y = a.integer();
        if (auto* p = actor(n)) {
            p->targetX = x;
            p->targetY = y;
        }
    });
    c.rebind(0x92, [this](CommandArguments& a) { // V200: speed
        const int n = a.integer(), speed = a.integer();
        if (auto* p = actor(n)) {
            p->speed = speed;
        }
    });
    c.rebind(0x97, [this](CommandArguments& a) { // V238: drawn only while speaking
        const int n = a.integer(), kind = a.integer();
        if (auto* p = actor(n)) {
            p->kind = kind == 1 && p->kind == 2 ? 2 : kind;
        }
    });
    c.rebind(0xb4, [this](CommandArguments& a) { // V204: mirrored
        const int n = a.integer();
        const bool flip = a.truth();
        // (layout "ib": an integer token != 0 or the string "true")
        if (auto* p = actor(n)) {
            p->flip = flip;
            if (auto* part = actor(p->part)) {
                part->flip = flip;
            }
        }
    });
    // Upper 2D sprites (V214..V234).
    c.rebind(0x7e, [this](CommandArguments& a) {
        const int x = a.integer(), y = a.integer();
        const std::string sprite = a.string(), animation = a.string();
        if (story.upperActors.size() < 8) {
            story.addActor(true, x, y, sprite, animation);
        }
    });
    c.rebind(0x7f, [this](CommandArguments& a) {
        const int n = a.integer();
        const std::string name = a.string();
        if (auto* p = upperActor(n)) {
            story.setActorAnimation(*p, name);
        }
    });
    c.rebind(0x80, [this](CommandArguments& a) {
        const int n = a.integer(), x = a.integer(), y = a.integer();
        if (auto* p = upperActor(n)) {
            p->x = p->targetX = x;
            p->y = p->targetY = y;
        }
    });
    c.rebind(0x81, [this](CommandArguments& a) {
        if (auto* p = upperActor(a.integer())) {
            p->visible = true;
        }
    });
    c.rebind(0x82, [this](CommandArguments& a) {
        if (auto* p = upperActor(a.integer())) {
            p->visible = false;
        }
    });
    c.rebind(0x93, [this](CommandArguments& a) {
        const int n = a.integer(), count = a.integer();
        if (auto* p = upperActor(n)) {
            p->shake = count;
        }
    });
    c.rebind(0x94, [this](CommandArguments& a) {
        const int n = a.integer();
        const std::string mode = a.string();
        if (auto* p = upperActor(n)) {
            p->moveToTarget = mode == "Move To Target" ? true
                              : mode == "Normal"       ? false
                                                       : p->moveToTarget;
        }
    });
    c.rebind(0x95, [this](CommandArguments& a) {
        const int n = a.integer(), x = a.integer(), y = a.integer();
        if (auto* p = upperActor(n)) {
            p->targetX = x;
            p->targetY = y;
        }
    });
    c.rebind(0x96, [this](CommandArguments& a) {
        const int n = a.integer(), speed = a.integer();
        if (auto* p = upperActor(n)) {
            p->speed = speed;
        }
    });
    // Talk.
    c.rebind(0x98, [this](CommandArguments& a) {
        const int n = a.integer(), s = a.integer();
        talk(n, s);
    });
    c.rebind(0x99, [this](CommandArguments& a) {
        const int n = a.integer();
        windowStyle(1);
        talk(n, -1);
    });
    // 0x45 / 0x46 (02012a0c / 02012a40 -> V19c / V1a0): the window's first
    // and second style.
    c.rebind(0x45, [this](CommandArguments&) { windowStyle(1); });
    c.rebind(0x46, [this](CommandArguments&) { windowStyle(2); });
    // 0xe6 (020169fc -> V270 02026ef4): the puzzle shack's script
    // (nazobaba/babascript.gds) on this event.
    c.rebind(0xe6, [this](CommandArguments&) {
        story.runScript("script/nazobaba/babascript.gds", commands());
    });
    // 0x44 (020129bc -> V198): text n without a speaker, in the window as
    // it is.
    c.rebind(0x44, [this](CommandArguments& a) { talk(a.integer(), -1); });
    c.rebind(0x9a, [this](CommandArguments& a) {
        const int n = a.integer();
        windowStyle(2);
        talk(n, -1);
    });
    c.rebind(0x9b, [this](CommandArguments& a) {
        const int n = a.integer();
        windowStyle(3);
        talk(n, -1);
    });
    c.rebind(0x9c, [this](CommandArguments& a) {
        const int n = a.integer(), s = a.integer();
        windowStyle(1);
        talk(n, s);
    });
    c.rebind(0x9d, [this](CommandArguments& a) {
        const int n = a.integer(), s = a.integer();
        windowStyle(2);
        talk(n, s);
    });
    c.rebind(0xcd, [this](CommandArguments& a) {
        const int n = a.integer(), s = a.integer();
        windowStyle(3);
        talk(n, s);
    });
    c.rebind(0x65, [this](CommandArguments&) { windowStyle(3); }); // V1a4
    // Choices: the question and up to three answers (with their flags).
    c.rebind(0xd9, [this](CommandArguments& a) {
        a.string();
        question_ =
            story.text("etext", "etext", "sel" + std::to_string(story.state.event) + "_0.txt");
        if (question_.empty()) {
            question_ = "NO QUESTION";
        }
    });
    for (int i = 0; i < 3; ++i) {
        c.rebind(std::uint16_t(0xd6 + i), [this, i](CommandArguments& a) {
            a.string();
            answerFlags_[i] = a.integer();
            answers_[i] = story.text("etext", "etext",
                                     "sel" + std::to_string(story.state.event) + "_" +
                                         std::to_string(i + 1) + ".txt");
            if (answers_[i].empty()) {
                answers_[i] = "CHOICE TEXT " + std::to_string(i + 1);
            }
        });
    }
    c.rebind(0xd5, [this](CommandArguments&) { choose(3); });
    c.rebind(0xdb, [this](CommandArguments&) { choose(2); });
    // Prizes and notices.
    c.rebind(0x56, [this](CommandArguments& a) { item(a.integer()); });
    c.rebind(0x57, [this](CommandArguments&) { puzzleNotice(); });
    c.rebind(0x61, [this](CommandArguments&) { savePrompt(); });
    c.rebind(0xc4, [this](CommandArguments& a) { gizmo(a.integer()); });
    c.rebind(0xc5, [this](CommandArguments& a) { paintingPiece(a.integer()); });
    c.rebind(0xcc, [this](CommandArguments& a) { furniture(a.integer()); });
    c.rebind(0xe7, [this](CommandArguments& a) { shackPuzzle(a.integer()); });
    // 0xe5 (0201697c -> V26c 02026e5c), the shack script's table: when
    // puzzle p is the current one, its prizes - a painting piece, a robot
    // dog part, a piece of furniture (-1: none) - then the puzzle notice.
    c.rebind(0xe5, [this](CommandArguments& a) {
        const int puzzle = a.integer(), furniturePiece = a.integer(), part = a.integer(),
                  piece = a.integer();
        if (puzzle != story.state.puzzleNumber) {
            return;
        }
        if (piece != -1) {
            paintingPiece(piece);
        }
        if (part != -1) {
            gizmo(part);
        }
        if (furniturePiece != -1) {
            furniture(furniturePiece);
        }
        puzzleNotice();
    });
    c.rebind(0xe8, [this](CommandArguments& a) { mystery(a.integer()); });
    // 0xfe (V1f0 0202afd8): the movie's script (script/event/m<N>.gds) on
    // this object: it names the movie and its captions and goes to the movie.
    c.rebind(0xfe, [this](CommandArguments& a) {
        const int n = a.integer();
        story.movieCues = MovieCues{};
        story.runScript("script/event/m" + std::to_string(n) + ".gds", commands());
    });
    return c;
}

} // namespace picarat::adventure1
