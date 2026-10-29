// The first adventure's puzzle index. See a1_puzzle_index.hpp.
#include "menus/a1_puzzle_index.hpp"
#include "menus/a1_scroll_bar.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "system/a1_script.hpp"
#include "system/a1_ui.hpp"
#include <algorithm>
#include <cmath>

namespace picarat::adventure1 {

namespace {
constexpr int kRowStep = 0x14;                     // a row's height
constexpr int kClipTop = 0x38, kClipBottom = 0x98; // the rows' area

struct PuzzleIndex {
    Story& story;
    SceneKit& kit;
    GameState& state;
    Button up, down, upFast, downFast, close, tabAll, tabMarked, play;    // +4..+0x20
    Button rows[5], marks[5];                                             // +0x24 / +0x38
    SceneKit::SpriteObject markIcon, digits, upperDigits, hintBox, medal; // +0x4c..+0x5c
    // The lists (1-based, entry 0 unused): every puzzle found (+0x6c) and
    // the marked ones (+0x16c), in the order of their numbers.
    std::vector<int> all{0}, marked{0};
    int shownAll = 0, shownMarked = 0; // +0x60 / +0x64
    int lastAll = 0, lastMarked = 0;   // +0x26c / +0x26d
    bool firstMarked = true;           // +0x26e
    int scroll = 0;                    // +0x28c: pixels into the next row
    ScrollBar bars[2];                 // +0x290 / +0x294
    int scrapeY = 0;                   // 020db8a0
    // From the data scripts: titles (qtitle.gds 0xba), kind and place
    // (qscript.gds 0xdc), picarats at each try (pscript.gds 0xc3).
    std::vector<std::string> titles, kinds, places;
    std::vector<std::array<int, 3>> picarats;
    std::string shackPlace; // storytext map87.txt (+0x298)

    int& list() { return state.indexList; }

    int& first() { return state.indexFirst[state.indexList == 1 ? 1 : 0]; }

    const std::vector<int>& rowsOf() const { return state.indexList == 1 ? marked : all; }

    int number(int id) const { // 020daeb0
        return int(std::int32_t(story.programWord(0x020daeb0 + 4 * std::uint32_t(id))));
    }

    std::uint8_t progress(int id) { return state.progress(id); }

    bool isMarked(int id) const {
        return id >= 0 && id < 0x100 && (state.puzzleMarks[std::size_t(id >> 3)] >> (id & 7) & 1);
    }

    void setMarked(int id, bool on) { // 0201b030
        if (id < 0 || id > 0xff) {
            return;
        }
        auto& b = state.puzzleMarks[std::size_t(id >> 3)];
        b = std::uint8_t(on ? b | 1 << (id & 7) : b & ~(1 << (id & 7)));
    }

    // 02033378: the puzzle shown in the list on screen.
    int shown() const { return state.indexList != 0 && marked.size() > 1 ? shownMarked : shownAll; }

    explicit PuzzleIndex(Story& s) : story(s), kit(s.kit), state(s.state) {
        // 02031ef4.
        kit.lowerPriority = std::array<int, 4>{1, 2, 0, 3};
        story.present();
        if (story.sound) {
            story.sound->setupRoom(state.room, [this] { story.stall(1); });
            story.sound->startLoadedMusic(true);
        }
        // The rows' area is a window (WIN0 y 0x3a..0x9a): the text layer
        // shows only inside it.
        auto& win = kit.windows[SceneKit::Lower];
        win = {};
        win.enabled[0] = true;
        win.x1[0] = 0;
        win.x2[0] = 0xff;
        win.y1[0] = 0x3a;
        win.y2[0] = 0x9a;
        win.inside[0] = 0xf;
        win.outside = 0xd;
        auto button = [&](Button& b, int x, int y, const char* idle, const char* pressed,
                          int sound) {
            b.setSprite(kit, x, y, "?/jiten_btn.spr", idle);
            b.setPressedAnimation(kit, pressed);
            b.visibleIdle = true;
            b.sound = sound;
        };
        button(up, 0x6b, 0x20, "up_off", "up_on", 0);
        button(upFast, 0x8b, 0x20, "upfast_off", "upfast_on", 0);
        button(down, 0x6b, 0x9c, "down_off", "down_on", 0);
        button(downFast, 0x8b, 0x9c, "downfast_off", "downfast_on", 0);
        button(close, 0xb7, 0xa1, "close_off", "close_on", 0x15);
        tabAll.setRect(0xe, 0x26, 0x20, 0x10);
        tabAll.sound = 0x11;
        tabMarked.setRect(0x2f, 0x26, 0x30, 0x10);
        tabMarked.sound = 0x11;
        button(play, 0xb2, 2, "pazzle_off", "pazzle_on", 0xb);
        for (int k = 0; k < 5; ++k) {
            marks[k].setRect(0x13, 0x3a + kRowStep * k, 0x10, 0xc);
            marks[k].sound = 3;
            rows[k].setRect(0x26, 0x3a + kRowStep * k, 0xbf, 0x14);
            rows[k].sound = 3;
        }
        markIcon = kit.load("?/jiten_btn.spr", 0, 0, 1);
        digits = kit.load("?/jiten_num.spr", 0, 0, 1);
        upperDigits = kit.load("?/jiten_num.sbj", 0, 0, 1);
        hintBox = kit.load("jiten_hintbox.sbj", 0, 0, 1);
        medal = kit.load("jiten_prize.sbj", 0, 0, 1);
        shackPlace = story.text("storytext", "storytext", "map87.txt");
        if (shackPlace.empty()) {
            shackPlace = "missing map87.txt";
        }
        loadData();
        // The scroll bars (track 0x37..0x99 at x 0xe3).
        for (auto& bar : bars) {
            bar.x = 0xe3;
            bar.y = bar.top = 0x37;
            bar.bottom = 0x99;
            bar.sprite = kit.load("jiten_scroll.spr", 0, 0, 1);
        }
        collectAll();
        collectMarked();
        bars[0].setValue(kit, state.indexFirst[0]);
        bars[1].setRange(kit, int(marked.size()) - 6);
        bars[1].setValue(kit, state.indexFirst[1]);
        kit.lowerBackground = "jiten_bg";
        if (state.indexList == 0) {
            kit.lowerOverlay = kit.localized("?/jiten_over");
        } else if (state.indexList == 1) {
            kit.lowerOverlay = kit.localized("?/jiten_over2");
        }
        story.present();
        kit.upperBackground = kit.localized("?/jiten_sub");
        story.present();
        if (state.indexShown == -1) {
            showPuzzle(all.size() > 1 ? all[1] : 0);
            state.indexShown = shown();
        } else {
            showPuzzle(state.indexShown);
        }
        story.present();
        if (story.sound) {
            story.sound->setupScene(3, [this] { story.stall(1); });
        }
        lastMarked = all.size() > 1 ? all[1] : 0;
        firstMarked = true;
    }

    void loadData() {
        titles.assign(0x100, {});
        kinds.assign(0x100, {});
        places.assign(0x100, {});
        picarats.assign(0x100, {0, 0, 0});
        ScriptContext c = story.commonCommands();
        // 0xba (02033240): puzzle n's title.
        c.rebind(0xba, [this](CommandArguments& a) {
            const int n = a.integer();
            const std::string title = kit.text.expand(a.string()); // 0200c2d8
            if (n > 0 && n < 0x100) {
                titles[std::size_t(n)] = title;
            }
        });
        // 0xdc (02033274): its kind and where it was found - the shack once
        // it was sent there (flag 800 + n).
        c.rebind(0xdc, [this](CommandArguments& a) {
            const int n = a.integer();
            const std::string kind = kit.text.expand(a.string()),
                              place = kit.text.expand(a.string());
            if (n > 0 && n < 0x100) {
                kinds[std::size_t(n)] = kind;
                places[std::size_t(n)] = state.flag(800 + n) ? shackPlace : place;
            }
        });
        // 0xc3 (V24 02033094): the picarats at the first, second and later tries.
        c.rebind(0xc3, [this](CommandArguments& a) {
            const int n = a.integer(), p0 = a.integer(), p1 = a.integer(), p2 = a.integer();
            if (n >= 0 && n < 0x100) {
                picarats[std::size_t(n)] = {p0 & 0xff, p1 & 0xff, p2 & 0xff};
            }
        });
        story.runScript("script/puzzletitle/" + story.language + "/qtitle.gds", c);
        story.runScript("script/qinfo/" + story.language + "/qscript.gds", c);
        story.runScript("script/pcarot/pscript.gds", c);
    }

    // The puzzles in the order of their numbers (020db2b0 from the table
    // 020daeb0); number 0 is no puzzle.
    std::vector<int> byNumber() const {
        std::vector<int> order(0x100, 0);
        for (int n = 0; n < 0x100; ++n) {
            for (int id = 0; id < 0x100; ++id) {
                if (number(id) == n) {
                    order[std::size_t(n)] = id;
                }
            }
        }
        order[0] = 0;
        return order;
    }

    // 02033b98: the puzzles found, the WiFi puzzles left out (0201ad10).
    void collectAll() {
        all.assign(1, 0);
        for (int id : byNumber()) {
            if (id != 0 && (progress(id) & 1) && !state.isWifiPuzzle(id)) {
                all.push_back(id);
            }
        }
        bars[0].setRange(kit, int(all.size()) - 6);
    }

    // 02033c98: the marked puzzles.
    void collectMarked() {
        marked.assign(1, 0);
        for (int id : byNumber()) {
            if (isMarked(id)) {
                marked.push_back(id);
            }
        }
    }

    // 02032f04: the picarats the puzzle is worth now (fewer after wrong
    // answers: bits 0x40 / 0x80 of its progress).
    int worth(int id) {
        const auto p = progress(id);
        const auto& tiers = picarats[std::size_t(std::clamp(id, 0, 0xff))];
        if ((p & 0x40) && !(p & 0x80)) {
            return tiers[1];
        }
        return (p & 0x80) ? tiers[2] : tiers[0];
    }

    // 020330d4: puzzle `id` on the upper screen.
    void showPuzzle(int id) {
        if (id <= 0) {
            return;
        }
        story.poll();
        char name[32];
        std::snprintf(name, sizeof name, "jiten_q%d", id);
        kit.upperOverlay =
            fileExists(story.pack / "bg" / (std::string(name) + ".l2bg"))
                ? name
                : "jiten_q00";
        if (state.indexList == 0 || marked.size() < 2) {
            shownAll = id;
        } else if (state.indexList == 1 && marked.size() > 1) {
            shownMarked = id;
        }
        kit.clearText(SceneKit::Upper);
        const auto at = std::size_t(std::clamp(id, 0, 0xff));
        drawCentredText(kit, SceneKit::Upper, 0x1a, titles[at], true, 0, 0x80);
        drawCentredLine(kit, SceneKit::Upper, 0x8e, kinds[at], 0x9e);
        drawCentredLine(kit, SceneKit::Upper, 0xa3, places[at], 0x9e);
        draw();
        story.present();
    }

    // 02033630 / 02033748 / 02033848: three-digit numbers.
    void lowerNumber(int x, int y, int value) {
        const int d[3] = {value % 1000 / 100, value % 100 / 10, value % 10};
        for (int k = 0; k < 3; ++k) {
            kit.select(digits, unsigned(d[k] + 1));
            digits.x = x + 7 * k;
            digits.y = y;
            kit.draw(digits);
        }
    }

    void upperDigit(int x, int y, int value) {
        kit.select(upperDigits, unsigned(value + 1));
        upperDigits.x = x;
        upperDigits.y = y;
        kit.draw(upperDigits);
    }

    // 020340cc: the shown puzzle's medal (02033d10), number, hints used,
    // picarats now and at best.
    void drawDetails() {
        const int id = shown();
        if (id < 1) {
            return;
        }
        const auto p = progress(id);
        int frame = 4;
        if ((p & 2) && !(p & 0xf8)) {
            frame = 1; // solved without hints and at the first try
        } else if ((p & 2) && !(p & 0xc0)) {
            frame = 2;
        } else if (p & 2) {
            frame = 3;
        }
        kit.select(medal, unsigned(frame));
        medal.x = 8;
        medal.y = 4;
        kit.draw(medal);
        const int n = number(id);
        upperDigit(0x2f, 8, n % 1000 / 100);
        upperDigit(0x2f + 7, 8, n % 100 / 10);
        upperDigit(0x2f + 0xe, 8, n % 10);
        static const int hintX[3] = {0x7a, 0x8b, 0x9c};
        for (int h = 0; h < 3; ++h) {
            if (p & (8 << h)) {
                kit.select(hintBox, unsigned(h + 1));
                hintBox.x = hintX[h];
                hintBox.y = 0x73;
                kit.draw(hintBox);
            }
        }
        const int now = worth(id), best = picarats[std::size_t(id)][0];
        if (now / 10 > 0) {
            upperDigit(0xca, 0x12, now % 100 / 10);
        }
        upperDigit(0xca + 7, 0x12, now % 10);
        if (best / 10 > 0) {
            upperDigit(0xe3, 0x12, best % 100 / 10);
        }
        upperDigit(0xea, 0x12, best % 10);
    }

    // 020333a8: the rows, `scroll` pixels moved up.
    void drawRows() {
        const auto& list = rowsOf();
        const int firstRow = first();
        int y = 0x26 - scroll;
        const int current = state.indexList == 1 ? shownMarked : shown();
        kit.clearText(SceneKit::Lower); // 0200d8f4 on the rows' text
        for (int k = 0; k < 7; ++k, y += kRowStep) {
            const int row = firstRow + k;
            if (row < 0 || row >= int(list.size()) || list[std::size_t(row)] == 0) {
                continue;
            }
            const int id = list[std::size_t(row)];
            if (id == current) {
                // 020332f4: the highlight, clipped to the rows' area.
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
            kit.select(markIcon, isMarked(id) ? "fav_on" : "fav_off"); // 020335dc
            markIcon.x = 0x15;
            markIcon.y = y;
            kit.draw(markIcon);
            kit.select(digits, (progress(id) & 2) ? "o" : "x"); // 020339b4
            digits.x = 0x26;
            digits.y = y - 1;
            kit.draw(digits);
            kit.select(digits, "nazo");
            digits.x = 0x26 + 0xe;
            kit.draw(digits);
            lowerNumber(0x47, y - 1, number(id));
            drawTextBox(kit, SceneKit::Lower, 0x65, y + 3, titles[std::size_t(id)]);
        }
    }

    // 020342e4: a frame's drawing and buttons.
    void draw() {
        drawDetails();
        const int current = shown();
        if (state.indexList == 0 && current > 0 && (progress(current) & 2)) {
            play.update(kit, story.touch);
        } else if (state.indexList == 1 && shownMarked > 0 && (progress(shownMarked) & 2) &&
                   marked.size() > 1) {
            play.update(kit, story.touch);
        }
        if (state.indexList == 0 && all.size() > 6) {
            bars[0].update(story, scrapeY);
            if (bars[0].moved) {
                state.indexFirst[0] = bars[0].value;
            }
        } else if (state.indexList == 1 && marked.size() > 6) {
            bars[1].update(story, scrapeY);
            if (bars[1].moved) {
                state.indexFirst[1] = bars[1].value;
            }
        }
        drawRows();
        for (Button* b : {&up, &upFast, &down, &downFast, &close, &tabAll, &tabMarked}) {
            b->update(kit, story.touch);
        }
        const int count = std::min(int(all.size()) - 1, 5);
        for (int k = 0; k < count; ++k) {
            marks[k].update(kit, story.touch);
            rows[k].update(kit, story.touch);
        }
    }

    void frame() {
        story.poll();
        draw();
        story.present();
    }

    // 0203451c / 02034604: one row up or down, `step` pixels a frame.
    void scrollBy(int direction, int step) {
        const int size = int(rowsOf().size());
        if (direction < 0 ? first() < 1 : size < first() + 7) {
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
        first() += direction;
        bars[state.indexList == 1 ? 1 : 0].setValue(kit, first());
    }

    // 02033a98: the list shown.
    void showList(int which) {
        if (which == 0) {
            scroll = 0;
            kit.lowerOverlay = kit.localized("?/jiten_over");
        } else {
            state.indexList = 1;
            scroll = 0;
            collectMarked();
            const int last = int(marked.size()) - 6;
            bars[1].setRange(kit, last);
            state.indexFirst[1] = 0;
            if (!firstMarked) {
                for (int k = 1; k < int(marked.size()); ++k) {
                    if (marked[std::size_t(k)] == lastMarked) {
                        state.indexFirst[1] = std::max(0, std::min(k - 1, last));
                        break;
                    }
                }
            }
            bars[1].setValue(kit, state.indexFirst[1]);
            kit.lowerOverlay = kit.localized("?/jiten_over2");
        }
        state.indexList = which;
    }

    // 0203471c.
    void run() {
        frame();
        fadeIn(kit, Screens::Both);
        for (;;) {
            const int firstRow = first();
            frame();
            if (close.clicked(kit, story.touch)) {
                state.scene = scene::TrunkMenu;
                return;
            }
            if (state.indexList != 1 && tabMarked.clicked(kit, story.touch)) {
                showList(1);
                if (marked.size() > 1) {
                    if (firstMarked) {
                        lastMarked = marked[1];
                        firstMarked = false;
                    }
                    showPuzzle(lastMarked);
                }
            } else if (state.indexList != 0 && tabAll.clicked(kit, story.touch)) {
                showList(0);
                showPuzzle(lastAll);
            }
            const auto& list = rowsOf();
            for (int k = 0; k < 5; ++k) {
                const std::size_t row = std::size_t(firstRow + 1 + k);
                if (row >= list.size()) {
                    continue;
                }
                const int id = list[row];
                if (state.indexList == 0 && marks[k].clicked(kit, story.touch)) {
                    if (!isMarked(id)) {
                        setMarked(id, true);
                        if (marked.size() == 1) {
                            firstMarked = true;
                        }
                        marked.push_back(id);
                    } else {
                        setMarked(id, false);
                        if (lastMarked == id) {
                            firstMarked = true;
                        }
                        marked.pop_back();
                    }
                }
                if (id != 0 && rows[k].clicked(kit, story.touch)) {
                    showPuzzle(id);
                    (state.indexList == 0 ? lastAll : lastMarked) = id;
                }
            }
            // Play the puzzle shown (solved ones only): scene 4, back here.
            const int id = state.indexList == 0 ? shown() : shownMarked;
            const bool can =
                id > 0 && (progress(id) & 2) && (state.indexList == 0 || marked.size() > 1);
            if (can && play.clicked(kit, story.touch)) {
                state.returnScene = scene::PuzzleIndex;
                state.setPuzzle(id > 0xff ? 0 : id);
                state.scene = scene::IndexPuzzle;
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

    // 0203294c.
    void finish() {
        fadeOut(kit, Screens::Both);
        state.indexShown = shown();
        if (story.sound) {
            story.sound->stopLoopedEffect();
        }
        kit.windows[SceneKit::Lower] = {};
        kit.clearText(SceneKit::Lower);
        kit.clearText(SceneKit::Upper);
        kit.lowerBackground.clear();
        kit.lowerOverlay.clear();
        kit.upperBackground.clear();
        kit.upperOverlay.clear();
        story.present();
    }
};
} // namespace

void puzzleIndex(Story& story) {
    PuzzleIndex index(story);
    index.run();
    index.finish();
}

} // namespace picarat::adventure1
