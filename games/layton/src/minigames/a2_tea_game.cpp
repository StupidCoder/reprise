// Herb tea (overlay 14). See a2_tea_game.hpp for how it plays.
#include "minigames/a2_tea_game.hpp"
#include "puzzles/judgment.hpp"
#include "system/a2_character_names.hpp"
#include "system/resources.hpp"
#include <cstdio>
#include <stdexcept>

namespace picarat {
namespace {
std::string printf3(const char* pattern, int value) {
    char b[64];
    std::snprintf(b, sizeof b, pattern, value);
    return b;
}

std::string printf3(const char* pattern, int a, int b2) {
    char b[64];
    std::snprintf(b, sizeof b, pattern, a, b2);
    return b;
}

std::string printf3(const char* pattern, int a, int b2, int c) {
    char b[64];
    std::snprintf(b, sizeof b, pattern, a, b2, c);
    return b;
}

// ht_tlk row (0x12 bytes): s16 tea, then eight (speaker, face) signed pairs.
int talkSpeaker(const std::uint8_t* row, int line) {
    return std::int8_t(row[2 + line * 2]);
}

int talkFace(const std::uint8_t* row, int line) {
    return std::int8_t(row[3 + line * 2]);
}

int roundHalfUp(float v) {
    return int(0.5f + v);
} // 0203a290(0.5, v) then 0203aed0
} // namespace

// Fills %s / %d placeholders of a game text.
std::string TeaGame::format(std::string pattern, const std::vector<std::string>& args) {
    // sprintf with %s arguments only (the ROM's tea texts use nothing else).
    std::string out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '%' && i + 1 < pattern.size() && pattern[i + 1] == 's') {
            if (next < args.size()) {
                out += args[next];
            }
            ++next;
            ++i;
            continue;
        }
        out += pattern[i];
    }
    return out;
}

TeaGame::TeaGame(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                 GameState& state, std::uint32_t& random)
    : kit_(assets, std::move(present), audio, SceneKind::Tea), state_(state), random_(random) {}

// How many tea requests have been served correctly.
int TeaGame::correctTeas() const {
    int n = 0;
    for (unsigned i = 0; i < 24; ++i) {
        if (state_.progress.teaCorrect(i)) {
            ++n;
        }
    }
    if (state_.progress.hintCoin(127, 0)) {
        ++n;
    }
    if (state_.progress.hintCoin(127, 1)) {
        ++n;
    }
    return n;
}

void TeaGame::loadTalk() {} // 0206c230: ht_tlk.plz members are read on demand (text/ht/).

// ---------------------------------------------------------------------------
// Scene object (020efda0 / 020eff9c / 020efe88).

void TeaGame::run() {
    // 020efda0: 0207f780(10, 1) starts room sound 10; the tea tables are
    // loaded (02085128/02085334/02085758, here with GameState).
    if (auto s = kit_.sound()) {
        s->roomSound(10, true);
    }
    request_ = state_.following == scene::DramaEvent;
    kit_.detail = state_.eventId;
    int mode = request_ ? 1 : 0;
    std::optional<Menu> menu;
    std::optional<Brewer> brewer;
    if (mode == 0) {
        brewer.emplace();
        brewer->request = request_;
        brewerConstruct(*brewer);
    } else {
        menu.emplace();
        menu->request = request_;
        menuConstruct(*menu);
    }
    // 020eff9c: run the active screen; 5000 / result 1 switch screens.
    for (;;) {
        if (mode == 0) {
            brewerRun(*brewer);
            if (brewer->state == 6000) {
                state_.scene = state_.following;
                break;
            }
            if (brewer->state == 5000) {
                brewer.reset();
                menu.emplace();
                menu->request = request_;
                menuConstruct(*menu);
                mode = 1;
            }
        } else {
            menuRun(*menu);
            if (menu->result == 2) {
                state_.scene = state_.following;
                break;
            }
            if (menu->result == 1) {
                menuDestruct(*menu);
                menu.reset();
                brewer.emplace();
                brewer->request = request_;
                brewerConstruct(*brewer);
                mode = 0;
            }
        }
    }
    if (menu) {
        menuDestruct(*menu);
    }
    // 020efe88: from the bag the menu follows, or event 18490 once all teas
    // are served; a request returns to its drama event.
    if (!request_) {
        state_.following = state_.scene = scene::Menu;
        if (state_.eventId == 18490 && !state_.eventViewed(18490)) {
            state_.setEventViewed(18490, true);
            state_.following = state_.scene = scene::DramaEvent;
        }
    } else {
        state_.scene = scene::DramaEvent;
    }
}

// ---------------------------------------------------------------------------
// TeaMenu.

void TeaGame::menuClearTalk(Menu& m) {
    // 020f0c3c: upper text layer cleared, no talk.
    kit_.clearText(SceneKit::Upper);
    m.text.clear();
    m.count = -1;
    m.length = -1;
    m.speaker = -1;
}

// Tea menu: shows a line of conversation (optionally typed out).
void TeaGame::menuTalk(Menu& m, int speaker, const std::string& raw, bool typed) {
    // 020f0c70: expands (0206bdd4) into +0x90; typed texts start at count 0.
    menuClearTalk(m);
    if (raw.empty()) {
        return;
    }
    m.text = kit_.text.expand(raw);
    m.length = int(m.text.size());
    m.speaker = speaker;
    if (typed) {
        m.count = 0;
    }
}

// Tea menu: loads its sprites and buttons (serve, quit, brew, the twelve teas).
void TeaGame::menuConstruct(Menu& m) {
    // 020f00e8. Display reset (0206dabc/0206d9c0/0206de7c), priorities (0,1,3,2).
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    kit_.textAbove3D = false;
    if (m.request) {
        m.quit = kit_.button("subgame/tea/%s/yametoku.spr", 0, 0, 4);
    }
    m.serve = kit_.button("subgame/tea/%s/furumau.spr", 0, 0, -1);
    m.brew = kit_.button("subgame/tea/%s/chougo.spr", 0, 0, 3);
    for (int row = 0, i = 0; row < 2; ++row) {
        for (int col = 0; col < 6; ++col, ++i) {
            m.teas[std::size_t(i)] =
                kit_.button("subgame/tea/tea_icon.spr", 10 + col * 0x28, 0x6f + row * 0x1a, -1);
            kit_.setButtonAnimations(m.teas[std::size_t(i)], "on", "off", "on");
            m.numbers[std::size_t(i)] =
                kit_.load("subgame/tea/tea_number.spr", 0x12 + col * 0x28, 0x73 + row * 0x1a, 1);
            kit_.select(m.numbers[std::size_t(i)], unsigned(i + 1));
        }
    }
    for (int i = 0; i < 3; ++i) {
        m.herbs[std::size_t(i)] = kit_.load("subgame/tea/icon.spr", 0xa2 + i * 0x18, 8, 1);
    }
    kit_.textTitle(0xe, kit_.systemText(600), 0, 0x80, SceneKit::Lower);
    kit_.textCentered(0x38, kit_.systemText(0x259), 0, 0x80, SceneKit::Lower);
    kit_.lowerBackground = "subgame/tea/teaevent_bg";
    if (!m.request) {
        m.arrow = kit_.load("subgame/tea/arrow.sbj", 0, 0);
        m.assistant = kit_.load("subgame/tea/face_luke.sbj", 0, 0);
        m.protagonist = kit_.load("subgame/tea/face_protagonist.sbj", 0, 0);
        kit_.upperBackground = "subgame/tea/tea_sub";
        if (state_.teaTutorial != -1) {
            m.window = kit_.load("subgame/tea/window.sbj", 0, 0);
        }
        m.protagonistFace = m.assistantFace = 1;
        menuClearTalk(m);
    } else {
        kit_.upperBackground =
            "subgame/tea/" + kit_.language + "/teaevent_sub" + std::to_string(state_.teaRequest[0]);
        m.window = kit_.load("subgame/tea/window.sbj", 0, 0);
    }
    m.correct = correctTeas();
    m.limitIcon = kit_.load("subgame/tea/limit_icon.sbj", 0x60, 0xe);
    m.limitNumber = kit_.load("subgame/tea/limit_num.sbj", 0, 0);
    if (!m.request && state_.teaTutorial == 0) {
        kit_.textCentered(0x95, kit_.systemText(0x25b), 0, 0x80, SceneKit::Upper);
    }
    m.help = kit_.load("subgame/tea/?/help_mes.sbj", 0, 0);
    m.touchWindow = kit_.load("subgame/tea/%s/touch_window.spr", 0, 0);
    m.wait = kit_.load("cursor_wait.spr", 0, 0);
    loadTalk();
    m.selected = -1;
    m.tastedTea = -1;
    m.line = -1;
    m.result = 0;
    m.talking = false;
    m.cursor = false;
    if (!m.request && state_.teaTutorial == 1) {
        menuTalk(m, 0, talkText("ht_300_0.txt"), true);
        m.protagonistFace = 0;
        m.help.x = 0x1c;
        m.help.y = 0x5a;
    } else if (!m.request && state_.teaTutorial == 2) {
        menuTalk(m, 1, talkText("ht_301_0.txt"), true);
        m.assistantFace = 0;
        m.help.x = 0xc2;
        m.help.y = 0x5a;
    }
    if (m.request) {
        // The request hint (ht_hint_%i) is typed once, completely, on the upper layer.
        kit_.textTyped(
            0x11, 0x8e,
            kit_.text.expand(talkText("ht_hint_" + std::to_string(state_.teaRequest[0]) + ".txt")),
            -1);
        m.count = -1;
    }
    // Wait until the BGM controller has settled (0207fa78).
    if (auto s = kit_.sound()) {
        while (!s->bgmSettled()) {
            kit_.wait(1);
        }
    }
}

void TeaGame::menuDestruct(Menu&) {
    // 020f0a08: objects released, display reset, priorities (0,1,3,2).
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    kit_.textAbove3D = false;
}

// Tea menu: the counter of correctly served requests.
void TeaGame::menuCorrectCount(Menu& m) {
    // 020f2204: the correct-tea counter (two digits) outside requests.
    if (!m.correct || m.request) {
        return;
    }
    kit_.draw(m.limitIcon);
    kit_.select(m.limitNumber, std::to_string(m.correct % 10));
    m.limitNumber.x = 0x84;
    m.limitNumber.y = 0x11;
    kit_.draw(m.limitNumber);
    kit_.select(m.limitNumber, std::to_string(m.correct / 10));
    m.limitNumber.x = 0x7e;
    m.limitNumber.y = 0x11;
    kit_.draw(m.limitNumber);
}

// Tea menu: shows the herbs of the selected tea's recipe.
void TeaGame::menuRecipeHerbs(Menu& m) {
    if (m.selected != -1) {
        for (auto& h : m.herbs) {
            kit_.draw(h);
        }
    }
} // 020f22cc

// Tea menu: draws (and, with `update`, animates) the menu screen.
void TeaGame::menuDraw(Menu& m, bool update) {
    // 020f0cc8: buttons (updated when `update`), brewed teas, counters, faces
    // and the typed talk on the upper screen.
    if (researchTrace) {
        researchTrace(kit_.polls - 1, "menu sel " + std::to_string(m.selected) + " result " +
                                          std::to_string(m.result) + " update " +
                                          std::to_string(int(update)));
    }
    auto button = [&](SpriteButton& b) {
        if (update) {
            kit_.updateButton(b);
        }
        kit_.drawButton(b);
    };
    if (m.selected != -1) {
        button(m.serve);
    }
    if (m.quit.exists) {
        button(m.quit);
    }
    if (m.brew.exists) {
        button(m.brew);
    }
    for (unsigned i = 0; i < 12; ++i) {
        if (state_.progress.tea(i)) {
            button(m.teas[i]);
            kit_.draw(m.numbers[i]);
        }
    }
    menuCorrectCount(m);
    menuRecipeHerbs(m);
    if (m.request) {
        return;
    }
    if (state_.progress.teaRequestPending() && state_.teaTutorial != 0) {
        kit_.draw(m.help);
    }
    if (m.speaker >= 0) {
        kit_.select(m.arrow, m.speaker == 0 ? "left" : "right");
        m.arrow.x = m.speaker == 0 ? 0x41 : 0xa7;
        m.arrow.y = 0x73;
        kit_.draw(m.arrow);
    }
    static const char* protagonist[]{"lt2", "lt1", "lt3"};
    static const char* assistantFaces[]{"lk2", "lk1", "lk3"};
    if (m.protagonistFace >= 0 && m.protagonistFace < 3) {
        kit_.select(m.protagonist, protagonist[m.protagonistFace]);
    }
    m.protagonist.x = 0xc;
    m.protagonist.y = 0x23;
    kit_.draw(m.protagonist);
    if (m.assistantFace >= 0 && m.assistantFace < 3) {
        kit_.select(m.assistant, assistantFaces[m.assistantFace]);
    }
    m.assistant.x = 0xb4;
    m.assistant.y = 0x23;
    kit_.draw(m.assistant);
    if (m.length > 0) {
        kit_.textTyped(0x11, 0x86, m.text, m.count);
        if (m.count >= 0) {
            if (m.count % 5 == 0 && m.count != 0) {
                kit_.effect(0x29);
            }
            if (++m.count >= m.length) {
                m.count = -1;
            }
        }
    }
    if (!m.cursor) {
        return;
    }
    m.touchWindow.x = 0x52;
    m.touchWindow.y = 0x38;
    kit_.draw(m.touchWindow);
    m.wait.x = 0x9e;
    m.wait.y = 0x6c;
    kit_.draw(m.wait);
}

// Tea menu: the cup being drunk.
void TeaGame::menuDrink(Menu& m) {
    // 020f2138: the cup on the upper screen for 210 frames; SE 0x1a4 at 10,
    // 0x1a7 at 100; from frame 46 its animation restarts every frame.
    m.window.x = 0x52;
    m.window.y = 0x23;
    for (int i = 0; i < 0xd2; ++i) {
        kit_.poll();
        menuDraw(m, false);
        if (i > 0x2d) {
            kit_.restart(m.window);
        }
        kit_.draw(m.window);
        kit_.present();
        if (i == 10) {
            kit_.effect(0x1a4);
        }
        if (i == 100) {
            kit_.effect(0x1a7);
        }
    }
}

// Tea menu: choose a tea, then serve or taste it, or go to the brewer.
void TeaGame::menuRun(Menu& m) {
    // 020f1014.
    kit_.poll();
    menuDraw(m, true);
    kit_.present();
    kit_.fadeIn();
    bool serving = false;
    for (;;) {
        if (!serving) {
            for (;;) {
                // A press while text is typing completes it (and is consumed by a re-poll).
                for (;;) {
                    kit_.poll();
                    if (!kit_.pressed() || m.count == -1) {
                        break;
                    }
                    m.count = -1;
                }
                if (!m.talking) {
                    break;
                }
                if (kit_.pressed()) {
                    bool done = true;
                    if (const auto* row = state_.teaTalk(std::uint32_t(m.tastedTea));
                        row && m.line < 8 && talkSpeaker(row, m.line) >= 0) {
                        const int speaker = talkSpeaker(row, m.line);
                        menuTalk(m, speaker,
                                 talkText(printf3("ht_%03d_%d.txt", m.tastedTea, m.line)), true);
                        done = false;
                        (speaker == 0 ? m.protagonistFace : m.assistantFace) =
                            talkFace(row, m.line);
                        ++m.line;
                    }
                    if (done) {
                        m.protagonistFace = m.assistantFace = 1;
                        menuClearTalk(m);
                        m.tastedTea = -1;
                        m.line = -1;
                        m.talking = false;
                        m.cursor = false;
                        if (!m.request && state_.teaTutorial == 0) {
                            kit_.textCentered(0x95, kit_.systemText(0x25c), 0, 0x80,
                                              SceneKit::Upper);
                            loadTalk();
                        }
                    }
                }
                menuDraw(m, false);
                kit_.present();
            }
            menuDraw(m, true);
            for (int i = 0; i < 12; ++i) {
                if (!kit_.clicked(m.teas[std::size_t(i)])) {
                    continue;
                }
                kit_.effect(3);
                if (m.selected != -1) {
                    kit_.setButtonIdle(m.teas[std::size_t(m.selected)], "off");
                }
                kit_.setButtonIdle(m.teas[std::size_t(i)], "select");
                if (const auto* recipe = state_.teaRecipe(std::uint32_t(i))) {
                    if (m.selected != i) {
                        kit_.clearText(SceneKit::Lower);
                        kit_.textAt(0x18, 0xe,
                                    kit_.text.expand(reinterpret_cast<const char*>(recipe + 4)),
                                    SceneKit::Lower);
                        kit_.textAt(0x12, 0x26,
                                    kit_.text.expand(reinterpret_cast<const char*>(recipe + 0x20)),
                                    SceneKit::Lower);
                    }
                    const unsigned code = le16(recipe + 2);
                    for (unsigned k = 0; k < 3; ++k) {
                        kit_.select(m.herbs[k],
                                    "w" + std::to_string(int((code >> ((2 - k) * 4)) & 15) + 1));
                    }
                }
                if (!m.request && m.selected == -1 && state_.teaTutorial == 0) {
                    kit_.clearText(SceneKit::Upper);
                    kit_.textCentered(0x95, kit_.systemText(0x25c), 0, 0x80, SceneKit::Upper);
                    loadTalk();
                }
                m.selected = i;
            }
            kit_.present();
            serving = kit_.clicked(m.serve);
            if (serving) {
                kit_.effect(0x6f);
                if (m.request || state_.teaTutorial == 0) {
                    if (m.request) {
                        // Serving a request: correct tea -> +1 (TeaCorrectFlag),
                        // otherwise +2; revisits (+0x8228) take 4 off.
                        int event = state_.eventId;
                        if (m.selected == state_.teaRequest[1]) {
                            state_.progress.setTeaCorrect(std::uint32_t(state_.teaRequest[0] - 1),
                                                          true);
                            event += 1;
                        } else {
                            event += 2;
                        }
                        if (state_.revisitVariant) {
                            event -= 4;
                        }
                        menuDrink(m);
                        state_.teaRoll = 0;
                        state_.eventId = std::uint16_t(event);
                        state_.scene = scene::DramaEvent;
                        kit_.fadeOut();
                        m.result = 2;
                        return;
                    }
                    menuDrink(m);
                    m.tastedTea = m.selected;
                    if (const auto* row = state_.teaTalk(std::uint32_t(m.tastedTea))) {
                        menuTalk(m, talkSpeaker(row, 0),
                                 talkText(printf3("ht_%03d_0.txt", m.tastedTea)), true);
                        m.line = 1;
                        m.talking = true;
                        m.cursor = true;
                        kit_.effect(0x19a);
                    }
                    serving = false; // continues with the quit/brew checks below
                } else {
                    if (menuTutorial(m)) {
                        state_.eventId = 18490;
                        kit_.fadeOut();
                        m.result = 2;
                        return;
                    }
                    serving = false;
                    continue;
                }
            }
        }
        if (m.quit.exists && kit_.clicked(m.quit)) {
            if (m.request) {
                int event = state_.eventId + 3;
                if (state_.revisitVariant) {
                    event = state_.eventId - 1;
                }
                state_.eventId = std::uint16_t(event);
            }
            m.result = 2;
            kit_.fadeOut();
            return;
        }
        if (kit_.clicked(m.brew)) {
            m.result = 1;
            kit_.fadeOut();
            return;
        }
    }
}

// Tea menu: a lead character asks for a tea from the bag.
bool TeaGame::menuTutorial(Menu& m) {
    // 020f16dc: the protagonist (1) or the assistant (2) asked for a tea from the bag menu.
    const auto& names = loadCharacterNames(kit_.assets.root).teaRequest;
    const std::string name = state_.teaTutorial == 1   ? names[0]
                             : state_.teaTutorial == 2 ? names[1]
                                                       : "";
    const auto* recipe = state_.teaRecipe(std::uint32_t(m.selected));
    if (!recipe) {
        throw std::runtime_error("tea tutorial without a recipe");
    }
    m.window.x = 0x52;
    m.window.y = 0x23;
    for (int i = 0; i < 0xd2; ++i) {
        kit_.poll();
        menuDraw(m, false);
        if (i > 0x2d) {
            kit_.restart(m.window);
        }
        kit_.draw(m.window);
        kit_.present();
        if (i == 10) {
            kit_.effect(0x1a4);
        }
        if (i == 100) {
            kit_.effect(0x1a7);
        }
    }
    const auto teaName = talkText(printf3("ht_tea%02d.txt", le16(recipe)));
    menuTalk(m, -1, format(talkText("ht_400_0.txt"), {name, teaName}), false);
    m.cursor = true;
    kit_.effect(0x19a);
    for (;;) {
        kit_.poll();
        menuDraw(m, false);
        kit_.present();
        if (kit_.pressed() || SceneKit::buttonA()) {
            break;
        }
    }
    menuClearTalk(m);
    bool ok = false;
    if (state_.teaTutorial == 1) {
        if (m.selected == 0) {
            menuTalk(m, 0, talkText("ht_300_1.txt"), true);
            m.protagonistFace = 1;
            state_.progress.setHintCoin(127, 0, true);
            ok = true;
        } else {
            menuTalk(m, 0, talkText("ht_300_2.txt"), true);
            m.protagonistFace = 0;
        }
    } else if (state_.teaTutorial == 2) {
        if (m.selected == 1) {
            menuTalk(m, 1, talkText("ht_301_1.txt"), true);
            m.assistantFace = 1;
            state_.progress.setHintCoin(127, 1, true);
            ok = true;
        } else {
            menuTalk(m, 1, talkText("ht_301_2.txt"), true);
            m.assistantFace = 0;
        }
    }
    if (ok) {
        state_.progress.setHintCoins(std::uint16_t(state_.progress.hintCoins() + 1));
        state_.progress.setHintCoinsFound(std::uint16_t(state_.progress.hintCoinsFound() + 1));
    } // 0208285c
    for (;;) {
        kit_.poll();
        menuDraw(m, false);
        kit_.present();
        if (!kit_.pressed() && !SceneKit::buttonA()) {
            continue;
        }
        if (m.count >= 0) {
            m.count = -1;
            continue;
        }
        const auto raw = talkText(ok ? "ht_400_1.txt" : "ht_400_2.txt");
        if (ok) {
            ++m.correct;
            state_.progress.setTeaRequestPending(false);
        }
        menuTalk(m, -1,
                 format(raw, {name, kit_.text.expand(reinterpret_cast<const char*>(recipe + 4))}),
                 false);
        kit_.effect(ok ? 0x1a1 : 0x1a2);
        for (int i = 0;;) {
            kit_.poll();
            menuDraw(m, false);
            kit_.present();
            if (i <= 100) {
                ++i;
                continue;
            }
            if (kit_.pressed() || SceneKit::buttonA()) {
                break;
            }
        }
        m.cursor = false;
        if (!ok) {
            if (state_.teaTutorial == 1) {
                menuTalk(m, 0, talkText("ht_300_0.txt"), true);
                m.protagonistFace = 0;
            } else if (state_.teaTutorial == 2) {
                menuTalk(m, 1, talkText("ht_301_0.txt"), true);
                m.assistantFace = 0;
            }
        } else {
            kit_.textAbove3D = true;
            menuHintCoin(m);
            kit_.textAbove3D = false; // 0206d9f4(1,0,3) / (0,1,3)
            if (state_.teaComplete() && !state_.eventViewed(18490)) {
                return true;
            }
            menuClearTalk(m);
            state_.teaTutorial = 0;
        }
        kit_.setButtonIdle(m.teas[std::size_t(m.selected)], "off");
        m.selected = -1;
        kit_.poll();
        menuDraw(m, false);
        kit_.present();
        kit_.clearText(SceneKit::Lower);
        kit_.textTitle(0xe, kit_.systemText(600), 0, 0x80, SceneKit::Lower);
        kit_.textCentered(0x38, kit_.systemText(0x259), 0, 0x80, SceneKit::Lower);
        if (state_.teaTutorial == 0) {
            kit_.textCentered(0x95, kit_.systemText(0x25b), 0, 0x80, SceneKit::Upper);
        }
        loadTalk();
        return false;
    }
}

// Tea menu: "a hint coin was in the cup" popup.
void TeaGame::menuHintCoin(Menu& m) {
    // 020f1edc: prize window "a hint coin was in the cup" (tx 0x25a).
    auto window = kit_.load("system/prize_window2.spr", 0, 0);
    int x = 0, y = 0;
    if (kit_.variable(window, "pos", x, y)) {
        window.x = x;
        window.y = y;
    }
    auto cursor = kit_.load("cursor_wait.spr", 0xe3, 0x7b);
    kit_.effect(0x74);
    kit_.clearTextRect(0xc, 0x34, 0xe8, 0x58);
    for (int alpha = 1; alpha < 0x20; alpha += 2) {
        kit_.poll();
        menuDraw(m, false);
        kit_.draw(window, alpha);
        kit_.present();
    }
    kit_.textCentered(0x5a, kit_.systemText(0x25a), 5, 0x80, SceneKit::Lower);
    for (int i = 0, alpha = 2;;) {
        kit_.poll();
        menuDraw(m, false);
        kit_.draw(window, 31);
        kit_.draw(cursor, alpha);
        if (alpha < 31) {
            ++alpha;
        }
        kit_.present();
        if (i <= 0x2e) {
            ++i;
            continue;
        }
        if (kit_.released() || SceneKit::buttonA()) {
            break;
        }
    }
    kit_.clearText(SceneKit::Lower);
    for (int alpha = 31; alpha > 1; alpha -= 2) {
        kit_.poll();
        menuDraw(m, false);
        kit_.draw(window, alpha);
        kit_.present();
    }
}

// ---------------------------------------------------------------------------
// TeaBrewer.

void TeaGame::brewerClearTalk(Brewer& b) {
    // 020f2c88.
    kit_.clearText(SceneKit::Upper);
    b.text.clear();
    b.textSource.clear();
    b.count2 = -1;
    b.length = -1;
    b.speaker = -1;
}

// Brewer: shows a line of conversation.
void TeaGame::brewerTalk(Brewer& b, int speaker, const std::string& raw, const std::string& source,
                         bool typed) {
    // 020f2cc0: unchanged while the same text pointer is shown (source).
    if (b.textSource == source && !source.empty()) {
        return;
    }
    brewerClearTalk(b);
    if (raw.empty()) {
        return;
    }
    b.text = kit_.text.expand(raw);
    b.textSource = source;
    b.length = int(b.text.size());
    b.speaker = speaker;
    if (typed) {
        b.count2 = 0;
    }
}

// Brewer: empties the pot.
void TeaGame::brewerReset(Brewer& b) {
    // 020f2b98: ingredients return to the shelf, lid and pot reset.
    b.state = 1000;
    b.timer = 0;
    b.touchX = -1;
    b.touchY = -1;
    if (b.count > 0) {
        b.selected = -1;
        b.previous = -2;
    }
    b.count = 0;
    b.lidX = 0x9f;
    b.lidY = 0x61;
    b.lidFx = float(b.lidX);
    b.lidFy = float(b.lidY);
    b.potItems = {};
    for (int i = 0; i < 8; ++i) {
        auto& s = b.shelf[std::size_t(i)];
        s.x = i * 0x1d + 0xf;
        s.y = 0x88;
        s.fx = float(s.x);
        s.fy = float(s.y);
    } // 020f2eb0
    kit_.select(b.teapot, "pot");
    kit_.select(b.lid, "huta");
}

// Brewer: loads the herbs, pot, lid and buttons.
void TeaGame::brewerConstruct(Brewer& b) {
    // 020f230c (display reset without priority change).
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    if (!b.request) {
        b.exit = kit_.button("subgame/tea/?/modoru_btn.spr", 0, 0, 4);
    } else {
        b.cover = kit_.button("subgame/tea/modoru_cover.spr", 0, 0, -1);
    }
    b.reset = kit_.button("subgame/tea/?/return_btn.spr", 0, 0, 4);
    std::string third = b.request ? "subgame/tea/?/furumau2.spr" : "subgame/tea/?/recipe_btn.spr";
    if (!b.request && state_.teaTutorial != 0) {
        third = "subgame/tea/?/help_btn.spr";
    }
    b.recipes = kit_.button(third, 0, 0, 3);
    b.name = kit_.load("subgame/tea/?/name.spr", 0, 0);
    b.icon = kit_.load("subgame/tea/icon.spr", 0, 0);
    b.teapot = kit_.load("subgame/tea/tea_pot.spr", 0, 0);
    b.lid = kit_.load("subgame/tea/tea_futa.spr", 0, 0);
    b.window = kit_.load("subgame/tea/window.spr", 0, 0);
    b.touchWindow = kit_.load("subgame/tea/?/touch_window.spr", 0, 0);
    b.wait = kit_.load("cursor_wait.spr", 0, 0);
    b.arrow = kit_.load("subgame/tea/arrow.sbj", 0, 0);
    b.windowTop = kit_.load("subgame/tea/window.spj", 0, 0);
    b.assistant = kit_.load("subgame/tea/face_luke.sbj", 0, 0);
    b.protagonist = kit_.load("subgame/tea/face_protagonist.sbj", 0, 0);
    b.correct = correctTeas();
    if (b.correct) {
        b.limitIcon = kit_.load("subgame/tea/limit_icon.sbj", 0x60, 0xe);
        b.limitNumber = kit_.load("subgame/tea/limit_num.sbj", 0, 0);
    }
    kit_.lowerBackground = "subgame/tea/tea_bg";
    kit_.upperBackground = "subgame/tea/tea_sub";
    b.success = false;
    b.code = -1;
    brewerClearTalk(b);
    b.talkId = -1;
    b.line = -1;
    b.protagonistFace = 1;
    b.assistantFace = 1;
    state_.progress.setMenuNew(4, false);
    b.help = kit_.load("subgame/tea/?/help_mes.sbj", 0, 0);
    if (state_.teaTutorial == 1) {
        b.help.x = 0x1c;
        b.help.y = 0x5a;
    } else if (state_.teaTutorial == 2) {
        b.help.x = 0xc2;
        b.help.y = 0x5a;
    }
    loadTalk();
    brewerReset(b);
    if (auto s = kit_.sound()) {
        while (!s->bgmSettled()) {
            kit_.wait(1);
        }
    }
}

// Brewer: runs until the player goes back to the menu.
void TeaGame::brewerRun(Brewer& b) {
    // 020f2f18: the serve/recipe button returns to the menu (5000), the exit
    // button leaves (6000) after fading the BGM (0207fad4, time id 0x3e9).
    brewerFrame(b);
    kit_.fadeIn();
    for (;;) {
        brewerFrame(b);
        if (b.recipes.exists && kit_.clicked(b.recipes)) {
            kit_.fadeOut();
            b.state = 5000;
            return;
        }
        if (b.exit.exists && kit_.clicked(b.exit)) {
            if (auto s = kit_.sound()) {
                s->fadeOutBgm(s->duration(0x3e9));
            }
            kit_.fadeOut();
            b.state = 6000;
            return;
        }
    }
}

// Brewer: one frame (input, logic, drawing).
void TeaGame::brewerFrame(Brewer& b) {
    // 020f2fdc.
    kit_.poll();
    if (researchTrace) {
        researchTrace(kit_.polls - 1, "state " + std::to_string(b.state) + " count " +
                                          std::to_string(b.count) + " sel " +
                                          std::to_string(b.selected) + " code " +
                                          std::to_string(b.code & 0xffff) + " ok " +
                                          std::to_string(int(b.success)) + " talk " +
                                          std::to_string(b.talkId) + " " + std::to_string(b.line));
    }
    brewerLogic(b);
    brewerDraw(b);
    kit_.present();
}

// Brewer: the remark about the herbs in the pot.
void TeaGame::brewerComment(Brewer& b) {
    // 020f2d28: a random remark for the pot contents (ht_2<count-1><speaker>_<0..4>),
    // replaced by the recipe remark (ht_rcp%i) once a brewed recipe is complete.
    const int speaker = int(gameRandom(random_) & 1);
    const int pick = int(gameRandom(random_)) % 5;
    const auto name = printf3("ht_2%d%d_%d.txt", b.count - 1, speaker, pick);
    brewerTalk(b, speaker, talkText(name), name, true);
    if (b.count < 3) {
        return;
    }
    const auto* recipe =
        state_.teaRecipeByCode(GameState::teaRecipeCode(b.pot[0], b.pot[1], b.pot[2]));
    if (!recipe || !state_.progress.tea(le16(recipe))) {
        return;
    }
    const auto pattern = talkText(printf3("ht_rcp%d.txt", speaker + 1));
    if (pattern.empty()) {
        return;
    }
    const auto tea = talkText(printf3("ht_tea%02d.txt", le16(recipe)));
    brewerTalk(b, speaker, format(pattern, {tea}), "#buffer", true);
}

// Brewer: drag herbs into the pot, close the lid, and brew: the three
// herbs form a recipe code (GameState::teaRecipeCode, any order), and a
// matching recipe makes that tea.
void TeaGame::brewerLogic(Brewer& b) {
    // 020f3010: one state step, the typed talk, then position easing.
    ++b.timer;
    b.previous = b.selected;
    auto shelfSlot = [](int x) {
        return (x + 0x1c1) / 0x1d - 0x10;
    }; // 020f2ec0
    auto potSlot = [](int x) {
        return (x + 0x178) / 0x18 - 0x10;
    }; // 020f2ef8
    auto potX = [](int i) {
        return i * 0x18 + 8;
    }; // 020f2ee8
    auto shelfX = [](int i) {
        return i * 0x1d + 0xf;
    }; // 020f2eb0
    auto recipeKnown = [&] {
        const auto* r =
            state_.teaRecipeByCode(GameState::teaRecipeCode(b.pot[0], b.pot[1], b.pot[2]));
        return r && state_.progress.tea(le16(r));
    };
    switch (b.state) {
    case 1000: {
        kit_.updateButton(b.exit);
        kit_.updateButton(b.reset);
        kit_.updateButton(b.recipes);
        kit_.updateButton(b.cover);
        b.selected = -2;
        b.timer = 0;
        b.success = false;
        b.code = -1;
        b.talkId = -1;
        b.line = -1;
        if (b.recipes.exists && kit_.clicked(b.reset)) {
            brewerReset(b);
            break;
        }
        if (!kit_.pressed()) {
            break;
        }
        int x = -1, y = -1;
        kit_.position(x, y);
        if (b.count >= 3 && x >= 0x9f && x < 0xbf && y >= 0x61 && y < 0x81) {
            // Lid picked up.
            kit_.effect(0x19e);
            b.state = 4000;
            b.touchX = x;
            b.touchY = y;
            kit_.select(b.lid, "huta");
            b.lidX = x - 0x10;
            b.lidY = y - 0x10;
            break;
        }
        if (y >= 0x88 && y < 0xa4) {
            const int slot = shelfSlot(x);
            if (slot >= 0 && state_.progress.herb(std::uint32_t(slot))) {
                kit_.effect(0x19a);
                b.state = 2000;
                b.touchX = x;
                b.touchY = y;
                b.selected = slot;
                b.shelf[std::size_t(slot)].x = x - 0xe;
                b.shelf[std::size_t(slot)].y = y - 0xe;
                break;
            }
        }
        if (b.count > 0 && y >= 0x37 && y < 0x4f) {
            const unsigned slot = unsigned(potSlot(x));
            if (slot < unsigned(b.count)) {
                kit_.effect(0x19a);
                b.state = 3000;
                b.touchX = x;
                b.touchY = y;
                b.selected = int(slot);
                b.potItems[slot].x = x - 0xe;
                b.potItems[slot].y = y - 0xe;
            }
        }
        break;
    }
    case 2000: { // dragging a herb from the shelf
        auto& s = b.shelf[std::size_t(b.selected)];
        if (!kit_.released()) {
            kit_.position(b.touchX, b.touchY);
            if (b.touchY < 0x45) {
                b.touchY = 0x45;
            }
            s.x = b.touchX - 0xe;
            s.y = b.touchY - 0xe;
            break;
        }
        b.state = 1000;
        s.x = shelfX(b.selected);
        s.y = 0x88;
        if (b.count < 3 && b.touchX >= 0x70 && b.touchX < 0x90 && b.touchY >= 0x44 &&
            b.touchY < 0x64) {
            b.pot[std::size_t(b.count)] = b.selected;
            auto& p = b.potItems[std::size_t(b.count)];
            p.x = potX(b.count);
            p.y = 0x37;
            p.fx = float(p.x);
            p.fy = float(p.y);
            ++b.count;
            kit_.select(b.teapot, "pot");
            kit_.select(b.teapot, "poyon");
            b.state = 2100;
            b.timer = 0;
            brewerComment(b);
            kit_.effect(0x19c);
        } else {
            kit_.effect(0x19b);
        }
        b.touchX = b.touchY = -1;
        break;
    }
    case 2100: // herb dropped into the pot
        if (b.timer > 0xe) {
            b.state = 1000;
            if (b.count > 2) {
                kit_.effect(recipeKnown() ? 0x1a8 : 0x19d);
                kit_.select(b.lid, "katakata");
            }
        }
        break;
    case 3000: { // dragging a herb out of the pot
        auto& p = b.potItems[std::size_t(b.selected)];
        if (!kit_.released()) {
            kit_.position(b.touchX, b.touchY);
            if (b.touchY < 0x43) {
                b.touchY = 0x43;
            }
            p.x = b.touchX - 0xc;
            p.y = b.touchY - 0xc;
            break;
        }
        p.x = potX(b.selected);
        p.y = 0x37;
        kit_.effect(0x19b);
        if (b.touchY >= 0x88) {
            for (int i = b.selected + 1; i < b.count; ++i) {
                b.pot[std::size_t(i - 1)] = b.pot[std::size_t(i)];
            }
            --b.count;
            kit_.select(b.lid, "huta");
        }
        brewerComment(b);
        b.state = 1000;
        b.touchX = b.touchY = -1;
        break;
    }
    case 4000: { // dragging the lid
        if (!kit_.released()) {
            kit_.position(b.touchX, b.touchY);
            b.lidX = b.touchX - 0x10;
            b.lidY = b.touchY - 0x10;
            if (b.lidY < 0x37) {
                b.lidY = 0x37;
            }
            break;
        }
        b.state = 1000;
        kit_.select(b.lid, "katakata");
        b.lidX = 0x9f;
        b.lidY = 0x61;
        if (b.touchX < 0x70 || b.touchX > 0x8f || b.touchY < 0x44 || b.touchY > 99) {
            kit_.effect(0x19f);
        } else {
            kit_.select(b.lid, "huta");
            b.state = 4100;
            b.timer = 0;
            b.lidX = 0x70;
            b.lidY = 0x44;
            kit_.clearText(SceneKit::Lower);
            brewerClearTalk(b);
            b.code = int(GameState::teaRecipeCode(b.pot[0], b.pot[1], b.pot[2]));
            const auto* recipe = state_.teaRecipeByCode(std::uint32_t(b.code));
            kit_.clearText(SceneKit::Lower);
            if (!recipe || !state_.progress.tea(le16(recipe))) {
                kit_.textCentered(0x1c, kit_.text.expand(talkText("ht_step_m5.txt")), 0, 0x80,
                                  SceneKit::Lower);
            } else {
                kit_.textCentered(
                    0x1c,
                    kit_.text.expand(format(talkText("ht_step_m4.txt"),
                                            {reinterpret_cast<const char*>(recipe + 4)})),
                    0, 0x80, SceneKit::Lower);
            }
            if (!recipe) {
                // An unknown blend: a remark chosen from the codes' digits.
                b.success = false;
                if (b.pot[0] != b.pot[1] || b.pot[0] != b.pot[2]) {
                    const unsigned c = unsigned(b.code);
                    const int t = int((c >> 4) & 15) * -0xc6 + int((c >> 8) & 15) * -0x71 +
                                  int(c & 15) * 0x137;
                    b.talkId = t % 10;
                } else {
                    b.talkId = 10;
                }
                b.talkId += 100;
                b.line = 0;
            } else {
                b.success = true;
                if (!state_.progress.tea(le16(recipe))) {
                    state_.progress.setTea(le16(recipe), true);
                }
                b.talkId = le16(recipe);
                b.line = 0;
            }
            brewerTalk(b, -1, talkText("ht_step_s5.txt"), "ht_step_s5.txt", false);
            kit_.effect(0x1a0);
        }
        b.touchX = b.touchY = -1;
        break;
    }
    case 4100: // brewing
        if (b.timer > 0x3b) {
            if (b.window.loaded() && b.windowTop.loaded()) {
                kit_.select(b.window, "n");
                kit_.select(b.windowTop, "n");
                kit_.select(b.window, b.success ? "o" : "x");
                kit_.select(b.windowTop, b.success ? "o" : "x");
            }
            kit_.effect(0x1a3);
            b.state = 4200;
            b.timer = 0;
        }
        break;
    case 4200:
        if (b.timer > 0x2c) {
            kit_.effect(0x1a4);
            ++b.state;
            b.timer = 0;
        }
        break;
    case 4201:
        if (b.timer > 0x5e) {
            kit_.clearText(SceneKit::Lower);
            brewerClearTalk(b);
            kit_.textCentered(
                0x1c, kit_.text.expand(talkText(b.success ? "ht_step_m6s.txt" : "ht_step_m6f.txt")),
                0, 0x80, SceneKit::Lower);
            kit_.effect(b.success ? 0x1a1 : 0x1a2);
            b.protagonistFace = b.assistantFace = b.success ? 1 : 0;
            b.state = 4300;
            b.timer = 0;
        }
        break;
    case 4300:
        if (b.timer > 0x59) {
            kit_.clearText(SceneKit::Lower);
            kit_.effect(0x1a6);
            b.state = 4400;
            b.timer = 0;
        }
        break;
    case 4400:
        if (b.timer > 0x14) {
            b.state = 4500;
            b.timer = 0;
        }
        break;
    case 4500:
    case 4501: {
        if (b.state == 4500) {
            if (b.timer < 0x1e) {
                break;
            }
            if (!b.success) {
                kit_.textCentered(0x1c, kit_.text.expand(talkText("ht_step_m7f.txt")), 0, 0x80,
                                  SceneKit::Lower);
            } else if (const auto* recipe = state_.teaRecipeByCode(std::uint32_t(b.code))) {
                kit_.textCentered(0x1c, kit_.text.expand(reinterpret_cast<const char*>(recipe + 4)),
                                  0, 0x80, SceneKit::Lower);
            }
            ++b.state;
            b.timer = 0;
        }
        if (b.timer != 0 && !kit_.pressed()) {
            break;
        }
        kit_.effect(0x29);
        bool done = true;
        if (b.count2 < 0) {
            if (const auto* row = state_.teaTalk(std::uint32_t(b.talkId));
                row && b.line < 8 && talkSpeaker(row, b.line) >= 0) {
                const int speaker = talkSpeaker(row, b.line);
                const auto name = printf3("ht_%03d_%d.txt", b.talkId, b.line);
                brewerTalk(b, speaker, talkText(name), name, true);
                done = false;
                (speaker == 0 ? b.protagonistFace : b.assistantFace) = talkFace(row, b.line);
                ++b.line;
            }
        } else {
            done = false;
            b.count2 = -1;
        }
        if (done) {
            b.protagonistFace = 1;
            b.assistantFace = 1;
            b.state = 1000;
            b.count = 0;
            b.timer = 0;
            b.selected = -1;
            b.previous = -2;
            kit_.effect(0x1a5);
            b.lidX = 0x9f;
            b.lidY = 0x61;
            brewerClearTalk(b);
        }
        break;
    }
    default:
        break;
    }
    // Typed talk on the upper screen; SE 0x29 every fifth character.
    if (b.length > 0) {
        kit_.textTyped(0x11, 0x86, b.text, b.count2);
        if (b.count2 >= 0) {
            if (b.count2 % 5 == 0) {
                kit_.effect(0x29);
            }
            if (++b.count2 >= b.length) {
                b.count2 = -1;
            }
        }
    }
    // Easing: position = target + 0.5 * (position - target), binary32.
    auto ease = [](float& f, int target) {
        const float t = float(target);
        f = t + 0.5f * (f - t);
    };
    ease(b.lidFx, b.lidX);
    ease(b.lidFy, b.lidY);
    for (int i = 0; i < b.count; ++i) {
        auto& p = b.potItems[std::size_t(i)];
        ease(p.fx, p.x);
        ease(p.fy, p.y);
    }
    for (auto& s : b.shelf) {
        ease(s.fx, s.x);
        ease(s.fy, s.y);
    }
}

// Brewer: the two-digit count of correctly served teas, near the top.
void TeaGame::brewerLimit(Brewer& b) {
    // 020f4c18.
    if (!b.correct) {
        return;
    }
    kit_.draw(b.limitIcon);
    kit_.select(b.limitNumber, std::to_string(b.correct % 10));
    b.limitNumber.x = 0x84;
    b.limitNumber.y = 0x11;
    kit_.draw(b.limitNumber);
    kit_.select(b.limitNumber, std::to_string(b.correct / 10));
    b.limitNumber.x = 0x7e;
    b.limitNumber.y = 0x11;
    kit_.draw(b.limitNumber);
}

// Brewer: draws the brewing screen.
void TeaGame::brewerDraw(Brewer& b) {
    // 020f41e8.
    const int group = b.state / 100 * 100;
    if (b.selected != b.previous || (b.state == 2100 && b.timer == 0)) {
        kit_.clearText(SceneKit::Lower);
        if (group == 1000) {
            switch (b.count) {
            case 0:
                kit_.textCentered(0x1c, kit_.text.expand(talkText("ht_step_m0.txt")), 0, 0x80,
                                  SceneKit::Lower);
                brewerClearTalk(b);
                kit_.textCentered(0x96, kit_.text.expand(talkText("ht_step_s0.txt")), 0, 0x80,
                                  SceneKit::Upper);
                break;
            case 1:
                kit_.textCentered(0x1c, kit_.text.expand(talkText("ht_step_m1.txt")), 0, 0x80,
                                  SceneKit::Lower);
                break;
            case 2:
                kit_.textCentered(0x1c, kit_.text.expand(talkText("ht_step_m2.txt")), 0, 0x80,
                                  SceneKit::Lower);
                break;
            case 3: {
                const auto* r =
                    state_.teaRecipeByCode(GameState::teaRecipeCode(b.pot[0], b.pot[1], b.pot[2]));
                if (!r || !state_.progress.tea(le16(r))) {
                    kit_.textCentered(0x1c, kit_.text.expand(talkText("ht_step_m3.txt")), 0, 0x80,
                                      SceneKit::Lower);
                } else {
                    kit_.textCentered(0x1c, kit_.text.expand(reinterpret_cast<const char*>(r + 4)),
                                      0, 0x80, SceneKit::Lower);
                }
                break;
            }
            default:
                break;
            }
        }
    }
    kit_.drawButton(b.exit);
    kit_.drawButton(b.reset);
    kit_.drawButton(b.recipes);
    kit_.drawButton(b.cover);
    if (!b.request && state_.teaTutorial != 0) {
        kit_.draw(b.help);
    }
    // The herb in hand: icon, name and description (ht_elm).
    int held = -1;
    if (b.state == 2000) {
        held = b.selected;
    } else if (b.state == 3000) {
        held = b.pot[std::size_t(b.selected)];
    }
    if (const auto* info = held >= 0 ? state_.teaHerbInfo(std::uint32_t(held)) : nullptr) {
        kit_.select(b.icon, "b" + std::to_string(held + 1));
        b.icon.x = 0xe;
        b.icon.y = 0x10;
        kit_.draw(b.icon);
        kit_.select(b.name, std::to_string(held + 1));
        b.name.x = 0x36;
        b.name.y = 0x12;
        kit_.draw(b.name);
        kit_.textAt(0x36, 0x23, kit_.text.expand(reinterpret_cast<const char*>(info + 2)),
                    SceneKit::Lower);
    }
    b.teapot.x = 0x51;
    b.teapot.y = 0x37;
    kit_.draw(b.teapot);
    if (b.state != 4000) {
        b.lid.x = roundHalfUp(b.lidFx);
        b.lid.y = roundHalfUp(b.lidFy);
        kit_.draw(b.lid);
    }
    for (int i = 0; i < 8; ++i) {
        if (!state_.progress.herb(std::uint32_t(i))) {
            continue;
        }
        if (b.selected == i && (b.state == 2000 || b.state == 2100)) {
            continue;
        }
        const auto& s = b.shelf[std::size_t(i)];
        kit_.select(b.icon, "s" + std::to_string(i + 1));
        b.icon.x = roundHalfUp(s.fx);
        b.icon.y = roundHalfUp(s.fy);
        kit_.draw(b.icon);
    }
    for (int i = 0; i < b.count; ++i) {
        if (b.state == 3000 && b.selected == i) {
            continue;
        }
        const auto& p = b.potItems[std::size_t(i)];
        kit_.select(b.icon, "w" + std::to_string(b.pot[std::size_t(i)] + 1));
        b.icon.x = roundHalfUp(p.fx);
        b.icon.y = roundHalfUp(p.fy);
        kit_.draw(b.icon);
    }
    if (b.state == 2000 && b.selected >= 0) {
        const auto& s = b.shelf[std::size_t(b.selected)];
        kit_.select(b.icon, "s" + std::to_string(b.selected + 1));
        b.icon.x = roundHalfUp(s.fx);
        b.icon.y = roundHalfUp(s.fy);
        kit_.draw(b.icon);
    }
    if (b.state == 3000 && b.selected >= 0) {
        const auto& p = b.potItems[std::size_t(b.selected)];
        kit_.select(b.icon, "w" + std::to_string(b.pot[std::size_t(b.selected)] + 1));
        b.icon.x = roundHalfUp(p.fx);
        b.icon.y = roundHalfUp(p.fy);
        kit_.draw(b.icon);
    }
    if (b.state == 4000) {
        b.lid.x = roundHalfUp(b.lidFx);
        b.lid.y = roundHalfUp(b.lidFy);
        kit_.draw(b.lid);
    }
    if (b.speaker >= 0) {
        kit_.select(b.arrow, b.speaker == 0 ? "left" : "right");
        b.arrow.x = b.speaker == 0 ? 0x41 : 0xa7;
        b.arrow.y = 0x73;
        kit_.draw(b.arrow);
    }
    static const char* protagonist[]{"lt2", "lt1", "lt3"};
    static const char* assistantFaces[]{"lk2", "lk1", "lk3"};
    if (b.protagonistFace >= 0 && b.protagonistFace < 3) {
        kit_.select(b.protagonist, protagonist[b.protagonistFace]);
    }
    b.protagonist.x = 0xc;
    b.protagonist.y = 0x23;
    kit_.draw(b.protagonist);
    if (b.assistantFace >= 0 && b.assistantFace < 3) {
        kit_.select(b.assistant, assistantFaces[b.assistantFace]);
    }
    b.assistant.x = 0xb4;
    b.assistant.y = 0x23;
    kit_.draw(b.assistant);
    if (group == 4200 || group == 4300 || group == 4400 || group == 4500) {
        // The result window rises from the lower screen to the upper one.
        int y = 0x38;
        if (group == 4400) {
            y = 0x38 - b.timer * 10;
        } else if (group == 4500) {
            y = -0x9d;
        }
        if (y != -0x48 && y + 0x48 >= 0) {
            b.window.x = 0x52;
            b.window.y = y;
        }
        if (y + 0x48 < 1) {
            b.window.x = 0x52;
            b.window.y = 0xc0;
        }
        kit_.draw(b.window);
        b.windowTop.x = 0x52;
        b.windowTop.y = y + 0xc0 < 0xbc ? y + 0xc0 : 0xc0;
        kit_.draw(b.windowTop);
    }
    if (group == 4500) {
        b.touchWindow.x = 0x52;
        b.touchWindow.y = 0x38;
        kit_.draw(b.touchWindow);
        b.wait.x = 0x9e;
        b.wait.y = 0x6c;
        kit_.draw(b.wait);
    }
    brewerLimit(b);
}
} // namespace picarat
