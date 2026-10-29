// The first adventure's WiFi corner. See a1_wifi.hpp.
#include "menus/a1_wifi.hpp"
#include "menus/a1_scroll_bar.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "system/a1_script.hpp"
#include "system/a1_ui.hpp"
#include <cstdio>

namespace picarat::adventure1 {

namespace {

constexpr int kRowStep = 0x14;                     // a row's height
constexpr int kClipTop = 0x38, kClipBottom = 0x98; // the rows' area

void clearPages(SceneKit& kit) {
    kit.lowerPriority.reset();
    kit.clearText(SceneKit::Lower);
    kit.clearText(SceneKit::Upper);
    kit.lowerBackground.clear();
    kit.upperBackground.clear();
    kit.lowerOverlay.clear();
    kit.upperOverlay.clear();
    kit.windows[SceneKit::Lower] = {};
}

// --------------------------------------------------------------------------
// 0x1d: the menu.

struct WifiMenu {
    Story& story;
    SceneKit& kit;
    SpriteButton back;              // +0
    Button choices[3];              // +4..+0xc: the puzzles, the download, the settings
    SceneKit::SpriteObject newMark; // +0x10

    explicit WifiMenu(Story& s) : story(s), kit(s.kit) {
        // 02036424.
        clearPages(kit);
        story.present();
        if (story.sound) {
            story.sound->loadMusic(2, [this] { story.stall(1); });
            story.sound->startLoadedMusic(true);
        }
        kit.lowerBackground = kit.localized("?/wifimenu_bg");
        kit.upperBackground = kit.localized("?/wifimenu_sub");
        story.present();
        back.load(kit, "?/load_btn.spr", 4);
        for (int i = 0; i < 3; ++i) {
            Button& b = choices[i];
            b.setSprite(kit, 0x30, 0x20 + 0x30 * i, "?/wifimenu_btn.spr", std::to_string(i + 1));
            b.visibleIdle = true;
            b.pressDx = b.pressDy = 1;
            b.sound = 3;
        }
        newMark = kit.load("?/new_button.spr", 0x29, 0x1c, 1);
        newMark.x = 0x29;
        newMark.y = 0x1c;
        if (story.sound) {
            story.sound->setupScene(3, [this] { story.stall(1); });
        }
    }

    // 0201bad8: a received puzzle not looked at yet.
    bool anyNew() {
        for (const auto& w : story.state.wifiPuzzles) {
            if (!(story.state.progress(w.puzzle) & 1)) {
                return true;
            }
        }
        return false;
    }

    // 0203673c.
    void draw() {
        back.update(kit, story.touch);
        if (!story.state.wifiPuzzles.empty()) {
            choices[0].update(kit, story.touch);
        }
        choices[1].update(kit, story.touch);
        choices[2].update(kit, story.touch);
        if (anyNew()) {
            kit.draw(newMark);
        }
    }

    void run() {
        // 020367a8.
        story.frame([this] { draw(); });
        fadeIn(kit, Screens::Both);
        GameState& state = story.state;
        for (;;) {
            story.frame([this] { draw(); });
            if (!state.wifiPuzzles.empty() && choices[0].clicked(kit, story.touch)) {
                state.scene = 0x1e;
                return;
            }
            if (choices[1].clicked(kit, story.touch)) {
                state.scene = 0x21;
                return;
            }
            if (choices[2].clicked(kit, story.touch)) {
                state.scene = 0x20;
                return;
            }
            if (back.clicked()) {
                state.scene = scene::Continue;
                return;
            }
        }
    }

    // 02036668.
    void finish() {
        fadeOut(kit, Screens::Both);
        clearPages(kit);
        story.present();
    }
};

// --------------------------------------------------------------------------
// 0x1e: the puzzles received.

struct WifiPuzzles {
    Story& story;
    SceneKit& kit;
    GameState& state;
    Button up, down, upFast, downFast; // +4 / +8 / +0xc / +0x10
    Button play;                       // +0x14
    SpriteButton back;                 // +0x18
    Button rows[5];                    // +0x1c..
    SceneKit::SpriteObject digits;     // +0x30: ?/jiten_num.spr
    std::vector<std::string> titles;   // +0x3c: qtitle.gds
    std::vector<int> list{0};          // +0x40: entry 0 unused
    int shown = 0;                     // +0x38
    int scroll = 0;                    // +0x244
    ScrollBar bar;                     // +0x248
    int scrapeY = 0;

    explicit WifiPuzzles(Story& s) : story(s), kit(s.kit), state(s.state) {
        // 020368ec: the sounds of room 1.
        clearPages(kit);
        story.present();
        if (story.sound) {
            const std::uint8_t room = state.room;
            state.room = 1;
            story.sound->setupRoom(1, [this] { story.stall(1); });
            state.room = room;
            story.sound->startLoadedMusic(true);
        }
        // BG priorities 3D 1, text 2, BG2 0, BG3 3; the rows' window (WIN0 y
        // 0x3a..0x9a).
        kit.lowerPriority = std::array<int, 4>{1, 2, 0, 3};
        auto& win = kit.windows[SceneKit::Lower];
        win = {};
        win.enabled[0] = true;
        win.x1[0] = 0;
        win.x2[0] = 0xff;
        win.y1[0] = 0x3a;
        win.y2[0] = 0x9a;
        win.inside[0] = 0xf;
        win.outside = 0xd;
        auto button = [&](Button& b, int x, int y, const char* idle, const char* pressed) {
            b.setSprite(kit, x, y, "?/jiten_btn.spr", idle);
            b.setPressedAnimation(kit, pressed);
            b.visibleIdle = true;
            b.pressDx = b.pressDy = 0;
        };
        button(up, 0x62, 0x20, "up_off", "up_on");
        button(upFast, 0x82, 0x20, "upfast_off", "upfast_on");
        button(down, 0x62, 0x9c, "down_off", "down_on");
        button(downFast, 0x82, 0x9c, "downfast_off", "downfast_on");
        back.load(kit, "?/load_btn.spr", 0x15);
        play.setSprite(kit, 0xb8, 2, "?/wifi_play_btn.spr", "off");
        play.setPressedAnimation(kit, "on");
        play.sound = 0xb;
        play.visibleIdle = true;
        play.pressDx = play.pressDy = 0;
        for (int k = 0; k < 5; ++k) {
            rows[k].setRect(0x26, 0x3a + kRowStep * k, 0xbf, 0x14);
            rows[k].sound = 3;
        }
        digits = kit.load("?/jiten_num.spr", 0, 0, 1);
        loadTitles();
        bar.x = 0xe3;
        bar.y = bar.top = 0x37;
        bar.bottom = 0x99;
        bar.sprite = kit.load("jiten_scroll.spr", 0, 0, 1);
        collect();
        bar.setValue(kit, state.wifiFirst);
        kit.lowerBackground = kit.localized("?/wifi_play_bg");
        kit.lowerOverlay = "wifi_play_over";
        story.present();
        kit.upperBackground = kit.localized("?/wifi_play_sub");
        story.present();
        // The first time the first puzzle is shown (state +0x728 = 1).
        if (state.wifiShown == 0) {
            show(list.size() > 1 ? list[1] : 0);
            state.wifiShown = shown;
        } else {
            show(state.wifiShown);
        }
        story.present();
        if (story.sound) {
            story.sound->setupScene(3, [this] { story.stall(1); });
        }
    }

    void loadTitles() {
        titles.assign(0x100, {});
        ScriptContext c = story.commonCommands();
        c.rebind(0xba, [this](CommandArguments& a) {
            const int n = a.integer();
            const std::string title = kit.text.expand(a.string());
            if (n > 0 && n < 0x100) {
                titles[std::size_t(n)] = title;
            }
        });
        story.runScript("script/puzzletitle/" + story.language + "/qtitle.gds", c);
    }

    // 02037834: the rows (from 1) in the order received.
    void collect() {
        list.assign(1, 0);
        for (const auto& w : state.wifiPuzzles) {
            list.push_back(w.puzzle);
        }
        bar.setRange(kit, int(list.size()) - 6);
    }

    // 020372c4.
    void show(int id) {
        if (id != 0) {
            shown = id;
        }
    }

    // 02037708: the puzzle's state: new (not seen), solved (o) or not (x).
    void drawState(int x, int y, int id) {
        kit.select(digits, "x");
        if (id != 0) {
            const auto p = state.progress(id);
            if (p & 2) {
                kit.select(digits, "o");
            } else if (!(p & 1)) {
                kit.select(digits, "New");
            }
        }
        digits.x = x;
        digits.y = y + 1;
        kit.draw(digits);
    }

    void digit(int x, int y, int frame) {
        kit.select(digits, unsigned(frame));
        digits.x = x;
        digits.y = y;
        kit.draw(digits);
    }

    // 0203748c: the day it came: yy.mm.dd from x 0x22.
    void drawDate(int y, int row) {
        const auto& w = state.wifiPuzzles[std::size_t(row - 1)];
        const int values[3] = {w.year, w.month, w.day};
        const int xs[3] = {0x22, 0x33, 0x44};
        for (int k = 0; k < 3; ++k) {
            const int v = values[k] % 100;
            digit(xs[k], y, v / 10 + 1);
            digit(xs[k] + 7, y, v % 10 + 1);
        }
        for (const int x : {0x30, 0x41}) {
            kit.select(digits, "dot");
            digits.x = x;
            digits.y = y;
            kit.draw(digits);
        }
    }

    // 02037388: the rows, `scroll` pixels moved up.
    void drawRows() {
        kit.clearTextRect(0x56, 0x3a, 0x7e, 0x9e, SceneKit::Lower);
        int y = 0x26 - scroll;
        for (int k = 0; k < 7; ++k, y += kRowStep) {
            const int row = state.wifiFirst + k;
            if (row < 0 || row >= int(list.size()) || list[std::size_t(row)] == 0) {
                continue;
            }
            const int id = list[std::size_t(row)];
            if (id == shown) {
                // 02037304: the highlight, clipped to the rows' area.
                int top = y - 2, h = 0x12;
                if (top + 0x12 >= kClipTop && top < kClipBottom) {
                    if (top < kClipTop) {
                        h -= kClipTop - top;
                        top = kClipTop;
                    }
                    if (top + 0x12 >= kClipBottom) {
                        h = kClipBottom - top;
                    }
                    kit.rectangle(0x10, top, 0xd0, h, rgb(0, 0, 0x40), 5);
                }
            }
            drawState(0x12, y - 2, id);
            drawDate(y - 1, row);
            drawTextBox(kit, SceneKit::Lower, 0x5b, y + 3, titles[std::size_t(id)]);
        }
    }

    // 020378d4.
    void draw() {
        if (shown != 0) {
            play.update(kit, story.touch);
        }
        if (list.size() > 6) {
            bar.update(story, scrapeY);
            if (bar.moved) {
                state.wifiFirst = bar.value;
            }
        }
        up.update(kit, story.touch);
        upFast.update(kit, story.touch);
        down.update(kit, story.touch);
        downFast.update(kit, story.touch);
        back.update(kit, story.touch);
        for (auto& r : rows) {
            r.update(kit, story.touch);
        }
        drawRows();
    }

    void frame() {
        story.frame([this] { draw(); });
    }

    // 0203797c / 02037a40.
    void scrollBy(int direction, int step) {
        const int size = int(list.size());
        if (direction < 0 ? state.wifiFirst < 1 : size < state.wifiFirst + 7) {
            return;
        }
        if (story.touch.pressed && story.sound) {
            story.sound->effect(0x2b);
        }
        for (int moved = step; moved < kRowStep; moved += step) {
            scroll += direction * step;
            frame();
        }
        scroll = 0;
        state.wifiFirst += direction;
        bar.setValue(kit, state.wifiFirst);
    }

    void run() {
        // 02037b0c.
        frame();
        fadeIn(kit, Screens::Both);
        for (;;) {
            const int firstRow = state.wifiFirst;
            frame();
            if (back.clicked()) {
                state.scene = 0x1d;
                return;
            }
            for (int k = 0; k < 5; ++k) {
                const int row = firstRow + 1 + k;
                if (row < int(list.size()) && list[std::size_t(row)] != 0 &&
                    rows[k].clicked(kit, story.touch)) {
                    show(list[std::size_t(row)]);
                    state.wifiShown = shown;
                }
            }
            if (shown != 0 && play.clicked(kit, story.touch)) {
                // The puzzle counts as looked at; it plays (scene 0x1f) and
                // the list comes back.
                state.progress(shown) |= 1;
                state.returnScene = 0x1e;
                state.setPuzzle(shown > 0xff ? 0 : shown);
                state.scene = 0x1f;
                return;
            }
            if (up.held(story.touch)) {
                scrollBy(-1, 4);
            } else if (upFast.held(story.touch)) {
                scrollBy(-1, 8);
            } else if (down.held(story.touch)) {
                scrollBy(1, 4);
            } else if (downFast.held(story.touch)) {
                scrollBy(1, 8);
            }
        }
    }

    // 02036f6c.
    void finish() {
        fadeOut(kit, Screens::Both);
        state.wifiShown = shown;
        clearPages(kit);
        story.present();
    }
};

// --------------------------------------------------------------------------
// 0x21: the download.

struct WifiDownload {
    Story& story;
    SceneKit& kit;
    // The screens of the download (+0): 1 asking, 2 connecting, 4 leaving,
    // 5 an error; each starts with ten frames before its pictures show
    // (+4 the screen's step, +0x34 its frames).
    int screen = 0, step = 0, frames = 0;
    bool done = false;                       // +8
    bool antenna = true;                     // +9: the signal sprite shown
    bool bar = false;                        // +10: the message bar shown
    SceneKit::SpriteObject message;          // +0xc: ?/wifi_download_msg.spr
    SceneKit::SpriteObject robot, professor; // +0x10 / +0x14: wifi_download_gfx.spr
    SceneKit::SpriteObject signal;           // +0x18: wifi_ant.sbj (upper)
    Button cancelConnect, okError;           // +0x1c / +0x20
    Button connect, cancel;                  // +0x24 / +0x28
    SceneKit::SpriteObject messageBar;       // +0x48: wifi_bar.sbj (upper)
    int connectingFrames = 0;                // the port's stand-in for the WiFi library

    // The connection attempt's length before it fails: measured on the
    // reference emulator, which has no access point (error 52000 about 1500
    // frames after "connect").
    static constexpr int kAttempt = 1500;

    explicit WifiDownload(Story& s) : story(s), kit(s.kit) {
        // 02037d64.
        clearPages(kit);
        story.present();
        kit.lowerBackground = kit.localized("?/wifi_download_bg");
        kit.upperBackground = kit.localized("?/wifi_download_sub");
        story.present();
        setScreen(1);
        message = kit.load("?/wifi_download_msg.spr", 0, 0x42, 1);
        message.y = 0x42;
        message.x = (0x100 - kit.frameWidth(message)) >> 1;
        robot = kit.load("wifi_download_gfx.spr", 0, 0, 1);
        robot.x = 0x32;
        robot.y = 0x68;
        professor = kit.load("wifi_download_gfx.spr", 0, 0, 1);
        professor.x = 0xbf;
        professor.y = 0x5c;
        signal = kit.load("wifi_ant.sbj", 6, 6, 5);
        signal.x = signal.y = 6;
        auto button = [&](Button& b, int x, int y, const char* name, int sound) {
            b.setSprite(kit, x, y, "?/wifi_download_btn.spr", name);
            b.visibleIdle = true;
            b.pressDx = b.pressDy = 1;
            b.sound = sound;
        };
        button(cancelConnect, 0x60, 0x55, "cancel", 4);
        button(okError, 0x60, 0x55, "ok", 3);
        button(cancel, 0x87, 0x5c, "cancel", 4);
        button(connect, 0x39, 0x5c, "ok", 3);
        messageBar = kit.load("wifi_bar.sbj", 10, 0xa8, 1);
        messageBar.x = 10;
        messageBar.y = 0xa8;
        kit.lowerPriority = std::array<int, 4>{1, 0, 2, 3};
        if (story.sound) {
            story.sound->setupScene(3, [this] { story.stall(1); });
        }
    }

    // 02038610.
    void setScreen(int s) {
        screen = s;
        step = 0;
        frames = 0;
    }

    // 02038440 with 020385bc / 02038588: message n of the WiFi texts (flow<n>
    // or err<n>), centred on row 100 of the upper screen (row 0x5d for an
    // error, which also shows the message bar).
    void writeMessage(const std::string& name, bool error) {
        std::string text = story.text("wifi", "wifi", name);
        if (text.empty()) {
            text = "No Message";
        }
        kit.clearText(SceneKit::Upper);
        if (error) {
            bar = true;
        }
        drawCentredText(kit, SceneKit::Upper, error ? 0x5d : 100, text, false, -2);
    }

    void drawFigures() {
        kit.draw(message);
        kit.draw(robot);
        kit.draw(professor);
    }

    // 02038634.
    void draw() {
        if (antenna) {
            kit.draw(signal);
        }
        switch (screen) {
        case 1: // 020386dc: asking
            if (step == 0) {
                kit.lowerOverlay.clear();
                step = 4;
            } else if (step == 4) {
                if (++frames > 9) {
                    writeMessage("flow2.txt", false);
                    kit.lowerOverlay = kit.localized("?/wifi_download_over2");
                    step = 1;
                }
            }
            if (step == 1) {
                cancel.update(kit, story.touch);
                connect.update(kit, story.touch);
                if (cancel.clicked(kit, story.touch)) {
                    setScreen(4);
                } else if (connect.clicked(kit, story.touch)) {
                    setScreen(2);
                }
            }
            break;
        case 2: // 020387c0: connecting
            if (step == 0) {
                kit.lowerOverlay.clear();
                step = 4;
                // 02039114: while the library runs, the signal icon shows the
                // link level (020934dc + 1: 1, no access point here).
                kit.select(signal, 1u);
            } else if (step == 4) {
                if (++frames > 9) {
                    // 02039194: the library starts, "connecting" (flow8).
                    writeMessage("flow8.txt", false);
                    kit.lowerOverlay = "wifi_download_over";
                    kit.select(message, "setsuzoku");
                    kit.select(robot, "robo_now");
                    kit.select(professor, "layton_now");
                    step = 1;
                }
            }
            if (step == 1) {
                drawFigures();
                cancelConnect.update(kit, story.touch);
                if (cancelConnect.clicked(kit, story.touch)) {
                    setScreen(4);
                } else if (++connectingFrames >= kAttempt) {
                    fail(52000);
                }
            }
            break;
        case 4: // 02038de0: leaving
            if (bar) {
                kit.draw(messageBar);
            }
            if (step == 0) {
                kit.lowerOverlay.clear();
                step = 4;
            } else if (step == 4 && ++frames > 9) {
                done = true;
            }
            break;
        case 5: // 02038b24: an error
            if (bar) {
                kit.draw(messageBar);
            }
            if (step == 0) {
                kit.lowerOverlay.clear();
                step = 4;
            } else if (step == 4) {
                if (++frames > 9) {
                    kit.lowerOverlay = "wifi_download_over";
                    kit.select(message, "x");
                    kit.select(robot, "robo_x");
                    kit.select(professor, "layton_x");
                    if (story.sound) {
                        story.sound->effect(0x2c);
                    }
                    step = 1;
                }
            }
            if (step == 1) {
                drawFigures();
                okError.update(kit, story.touch);
                if (okError.clicked(kit, story.touch)) {
                    setScreen(4);
                }
            }
            break;
        default:
            break;
        }
    }

    // 02039aac: an error of the WiFi library: its class's message (code
    // 52000, no access point, is class 9) and sys_500.txt with the code on
    // row 0xab.
    void fail(int code) {
        writeMessage("err9.txt", true);
        std::string format = story.text("stext", "stext", "sys_500.txt");
        if (format.empty()) {
            format = "missing sys_500.txt";
        }
        drawCentredLine(kit, SceneKit::Upper, 0xab, formatText(format, {code}), 0x80);
        setScreen(5);
    }

    void run() {
        // 02038f38.
        story.frame([this] { draw(); });
        fadeIn(kit, Screens::Both);
        while (!done) {
            story.frame([this] { draw(); });
        }
        story.state.scene = 0x1d; // 020028cc
    }

    // 020381e8.
    void finish() {
        fadeOut(kit, Screens::Both);
        clearPages(kit);
        story.present();
    }
};

} // namespace

void wifiMenu(Story& story) {
    WifiMenu page(story);
    page.run();
    page.finish();
}

void wifiPuzzles(Story& story) {
    WifiPuzzles page(story);
    page.run();
    page.finish();
}

void wifiSettings(Story& story) {
    // 0200274c runs the system's WiFi utility (not part of the game) and
    // comes back to the menu; there is none here.
    story.state.scene = 0x1d;
}

void wifiDownload(Story& story) {
    WifiDownload page(story);
    page.run();
    page.finish();
}

} // namespace picarat::adventure1
