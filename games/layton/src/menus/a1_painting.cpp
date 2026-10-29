// The first adventure's torn painting. See a1_painting.hpp.
#include "menus/a1_painting.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "system/a1_ui.hpp"
#include <cmath>
#include <cstring>

namespace picarat::adventure1 {

namespace {
constexpr int kPieces = 20;
constexpr int kTray = 20; // the place below the frame

unsigned turnsOf(float degrees) { // 65536 / 360 of a turn
    return unsigned(int(65536.0f * degrees / 360.0f)) & 0xffff;
}

// A large piece (02066930): its place on screen, the frame place it lies
// on, dragged or turned.
struct Piece {
    int x = -1, y = -1;                     // +0 / +4
    int first = -1;                         // +0x10: the first place it lay on
    bool dragging = false, turning = false; // +0x40 / +0x41
    int place = 0;                          // +0x4c (kTray: the tray)
    SceneKit::SpriteObject sprite;          // +0x44: jigsaw_piece_large.spr, frame n + 1

    bool hidden() const { return x == -1 && y == -1; }
};

struct Painting {
    Story& story;
    SceneKit& kit;
    GameState& state;
    int current = -1;                // +0: the piece in hand
    SceneKit::SpriteObject turnMark; // +4: pict_puzzle_rot.spr
    SceneKit::SpriteObject change;   // +8: jigsaw_main_select.spr "change"
    SceneKit::SpriteObject small;    // +0xc: jigsaw_piece_small.sbj
    float turnOffset = 0;            // +0x10
    float phase = 0;                 // +0x14
    int mode = 0;                    // +0x18: 0 the box on the touch screen, 1 the frame
    SceneKit::SpriteObject backLower, backUpper, arrow, shadow; // +0x1c..+0x28
    Button back, toFrame, toBox;                                // +0x2c / +0x30 / +0x34
    bool inBox[kPieces]{};                                      // +0x38
    Piece pieces[kPieces];                                      // +0x4c
    float angles[kPieces]{};                                    // +0xac
    std::vector<std::pair<int, int>> places; // +0x114: 20 frame places and the tray
    bool taken[64]{};                        // +0x41c
    bool fresh = false;                      // +0x430: a piece just taken from the box
    float dragAngle = 0;                     // 0220f104 +0xc
    static inline bool wholeShown = false;   // 0220f104: the "whole" sound played

    // The box's order of pieces (020da6a4) and their first turns (020da6b8).
    int boxOrder(int k) const {
        return int(story.programWord(0x020da6a4 + std::uint32_t(k)) & 0xff);
    }

    float firstTurn(int n) const {
        const std::uint32_t bits = story.programWord(0x020da6b8 + 4 * std::uint32_t(n));
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }

    // 0201b314: a piece's turn as saved (+0x670: quarters).
    float savedTurn(int n) const { return 90.0f * float(state.piece670[std::size_t(n)] & 3); }

    // 0201b3c8: a turn (degrees) saved as quarters; others leave it.
    void saveTurn(int n, float turn) {
        if (!state.pieceOwned[std::size_t(n)]) {
            return;
        }
        int t = int(std::fmod(std::fmod(turn, 360.0f) + 360.0f, 360.0f));
        if (t % 90 == 0) {
            state.piece670[std::size_t(n)] = std::uint8_t(t / 90 & 3);
        }
    }

    void addPlace(int x, int y, bool taken0) { // 0204bfa8
        if (places.size() > 0x3f) {
            return;
        }
        taken[places.size()] = taken0;
        places.push_back({x, y});
    }

    explicit Painting(Story& s) : story(s), kit(s.kit), state(s.state) {
        // 0204b7a8.
        for (int n = 0; n < kPieces; ++n) {
            angles[n] = firstTurn(n);
        }
        kit.lowerBackground = "jigsaw_main_bg";
        kit.upperBackground = "jigsaw_sub_bg";
        kit.lowerPriority = std::array<int, 4>{1, 0, 0, 3};
        for (int r = 0, n = 0; r < 4; ++r) {
            for (int c = 0; c < 5; ++c, ++n) {
                const int x = 0x20 + 0x24 * c, y = 0x16 + 0x24 * r;
                Piece& p = pieces[n];
                p.sprite = kit.load("jigsaw_piece_large.spr", x, y, unsigned(n + 1));
                p.x = p.sprite.x = x;
                p.y = p.sprite.y = y;
                addPlace(x, y, true);
                p.place = p.first = int(places.size()) - 1;
                angles[n] = savedTurn(n);
                taken[n] = false;
            }
        }
        for (int n = 0; n < kPieces; ++n) {
            Piece& p = pieces[n];
            const int where = std::int8_t(state.piecePlace[std::size_t(n)]); // 0201b384
            if (!state.pieceOwned[std::size_t(n)]) {
                p.x = p.y = -1;
                inBox[n] = false;
            } else if (where == -1) {
                inBox[n] = true;
                p.x = p.y = -1;
                angles[n] = savedTurn(n);
            } else {
                p.place = where;
                if (p.first == -1) {
                    p.first = where;
                }
                p.x = places[std::size_t(where)].first;
                p.y = places[std::size_t(where)].second;
                inBox[n] = false;
                taken[where] = true;
                angles[n] = savedTurn(n);
            }
        }
        addPlace(0x6c, 0x93, false);
        current = -1;
        turnMark = kit.load("pict_puzzle_rot.spr", 0, 0, 1);
        kit.swapScreens = true; // the box on the touch screen
        kit.clearText(SceneKit::Lower);
        kit.clearText(SceneKit::Upper);
        small = kit.load("jigsaw_piece_small.sbj", 0, 0, 1);
        backUpper = kit.load("?/jigsaw_sub_back.sbj", 0, 0, 1);
        backUpper.x = 200;
        backUpper.y = 2;
        backLower = kit.load("?/jigsaw_sub_back.spr", 0, 0, 1);
        backLower.x = 200;
        backLower.y = 2;
        back.setRect(200, 2, 0x36, 0x18);
        back.sound = 4;
        arrow = kit.load("jigsaw_sub_arrow.sbj", 0, 0, 1);
        arrow.x = 0x72;
        arrow.y = 3;
        toFrame.setRect(0x7d, 4, 0x1d, 0x1b);
        toFrame.sound = 3;
        toBox.setSprite(kit, 0x58, 0, "?/jigsaw_main_select.spr", "select");
        toBox.visibleIdle = true;
        toBox.sound = 3;
        change = kit.load("?/jigsaw_main_select.spr", 0, 0, 1);
        shadow = kit.load("jigsaw_piece_shadow.spr", 0, 0, 1);
        if (story.sound) {
            story.sound->setupScene(0xc, [this] { story.stall(1); });
            story.sound->loadMusic(8, [this] { story.stall(1); });
            story.sound->startLoadedMusic(true);
        }
        state.newPainting = false; // +0x707
    }

    // 0204bd20: the pieces' turns and places into the state.
    void save() {
        for (int n = 0; n < kPieces; ++n) {
            saveTurn(n, angles[n]);
            state.piecePlace[std::size_t(n)] =
                std::uint8_t(pieces[n].hidden() ? 0xff : pieces[n].place); // 0201b3ac
        }
    }

    bool inside(int x, int y, int left, int top, int w, int h) const { // 0200c118
        return x >= left && x <= left + w && y >= top && y <= top + h;
    }

    bool touchIn(int x, int y, int w, int h) const { // 0200ea58: where the press began
        const TouchState& t = story.touch;
        return t.pressX >= x && t.pressX <= x + w && t.pressY >= y && t.pressY <= y + h;
    }

    // 020669dc: the stylus on the piece (2 pixels up and left).
    bool onPiece(const Piece& p) {
        const int w = kit.frameWidth(p.sprite), h = kit.frameHeight(p.sprite);
        return touchIn(p.x - 2, p.y - 2, w, h) && story.touch.down;
    }

    // 02066a3c: the stylus on the piece's rim (its middle third left out).
    bool onRim(const Piece& p) {
        const int w = kit.frameWidth(p.sprite), h = kit.frameHeight(p.sprite), third = w / 3;
        if (!story.touch.down) {
            return false;
        }
        return touchIn(p.x - 4, p.y - 4, w + 4, h + 4) &&
               !touchIn(p.x + third - 2, p.y + third - 2, third + 4, third + 4);
    }

    // 0204c330: the stylus' angle about the piece's middle (+0x18), degrees.
    float touchAngle(const Piece& p) const {
        const int dx = story.touch.x - (p.x + 0x18), dy = story.touch.y - (p.y + 0x18);
        const float length = std::sqrt(float(dx * dx + dy * dy));
        if (length <= 0) {
            return 0;
        }
        float a = 180.0f * std::acos(float(dx) / length) / 3.14159f;
        if (dy < 0) {
            a = 360.0f - a;
        }
        return float(int(a));
    }

    // 0204c0e8: a piece let go: outside the frame back into the box, else
    // onto the nearest place (swapping with a piece there).
    void drop(Piece& p) {
        if (current != -1 && !inside(p.x + 0x18, p.y + 0x18, 0x16, -0x10, 0xd1, 0xad)) {
            p.place = kTray;
            p.x = p.y = -1;
            inBox[current] = true;
            taken[pieces[current].place] = false;
            current = -1;
            if (story.sound) {
                story.sound->effect(0x25d);
            }
            return;
        }
        if (story.sound) {
            story.sound->effect(0x25b);
        }
        int nearest = 0;
        float best = 1000.0f;
        for (int k = 0; k < kPieces; ++k) {
            const float dx = float(places[std::size_t(k)].first - p.x),
                        dy = float(places[std::size_t(k)].second - p.y);
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < best) {
                best = d;
                nearest = k;
            }
        }
        const int old = p.place;
        if (taken[nearest]) {
            Piece* other = nullptr;
            for (auto& q : pieces) {
                if (q.place == nearest && !q.hidden()) {
                    other = &q;
                }
            }
            p.x = places[std::size_t(nearest)].first;
            p.y = places[std::size_t(nearest)].second;
            p.place = nearest;
            if (other && other != &p) {
                if (old >= 0) {
                    other->x = places[std::size_t(old)].first;
                    other->y = places[std::size_t(old)].second;
                }
                other->place = old;
            }
            return;
        }
        taken[old] = false;
        p.x = places[std::size_t(nearest)].first;
        p.y = places[std::size_t(nearest)].second;
        p.place = nearest;
        taken[nearest] = true;
    }

    // 02066bac: a piece follows the stylus or is let go, then 02066ae8 draws
    // it (turned about its middle).
    void updatePiece(Piece& p, int n, bool interactive) {
        if (p.hidden() && !p.dragging) {
            return;
        }
        const TouchState& t = story.touch;
        if (interactive) {
            if (t.down && p.dragging && story.touch.down) {
                const int tx = std::clamp(t.x - 2, 10, 0xf0), ty = std::clamp(t.y - 2, 10, 0xb4);
                p.x = tx - (kit.frameWidth(p.sprite) >> 1);
                p.y = ty - (kit.frameHeight(p.sprite) >> 1);
            }
            if (t.released && p.dragging) {
                p.dragging = false;
                drop(p);
                if (story.sound) {
                    story.sound->effect(state.puzzleNumber == 0x3f ? 0xd3 : 0xb0);
                }
                return;
            }
            if (t.released && p.turning) {
                p.turning = false;
            }
        }
        drawPiece(p, angles[n]);
    }

    void drawPiece(Piece& p, float turn) {
        if (turn != 0) {
            p.sprite.x = p.x + (kit.frameWidth(p.sprite) >> 1);
            p.sprite.y = p.y + (kit.frameHeight(p.sprite) >> 1);
            kit.drawRotated(p.sprite, turnsOf(turn));
        } else {
            p.sprite.x = p.x;
            p.sprite.y = p.y;
            kit.draw(p.sprite);
        }
    }

    // 0204c480: the box (its pieces small, turned) and its buttons.
    void drawBox() {
        for (int r = 0, k = 0; r < 4; ++r) {
            for (int c = 0; c < 5; ++c, ++k) {
                const int n = boxOrder(k);
                if (n < kPieces && inBox[n]) {
                    kit.select(small, unsigned(n + 1));
                    small.x = 0x11 + 0x27 * c + (kit.frameWidth(small) >> 1);
                    small.y = 0x12 + 0x27 * r + (kit.frameHeight(small) >> 1);
                    kit.drawRotated(small, turnsOf(angles[n]));
                }
            }
        }
        if (mode == 0) {
            kit.draw(arrow);
            kit.draw(backUpper);
            back.update(kit, story.touch);
            toFrame.update(kit, story.touch);
        }
    }

    void drawShadow() {
        Piece& p = pieces[current];
        kit.select(shadow, unsigned(current + 1));
        shadow.x = p.x + 0x1a;
        shadow.y = p.y + 0x1a;
        if (p.place == kTray) {
            kit.drawRotated(shadow, turnsOf(angles[current])); // alpha 15 in the original
        } else {
            kit.draw(shadow, 0xf);
        }
    }

    // 0204c5c4: a frame's input and drawing (`interactive`: the frame takes
    // input).
    void draw(bool interactive) {
        drawBox();
        if (current != -1 && !pieces[current].dragging) {
            drawShadow();
        }
        if (mode == 1) {
            toBox.update(kit, story.touch);
            kit.draw(backLower);
        }
        if (mode == 1 && interactive) {
            // Turns not on a quarter move toward the nearest, 5 degrees a frame.
            for (int n = 0; n < kPieces; ++n) {
                angles[n] = float(int(angles[n]));
                if (pieces[n].turning || int(angles[n]) % 90 == 0) {
                    continue;
                }
                for (int k = 0; k < 5; ++k) {
                    const int a = int(angles[n]) % 90;
                    if (a == 0) {
                        break;
                    }
                    if (a < 0x2e) {
                        angles[n] -= 1.0f;
                        if (angles[n] < 0) {
                            angles[n] += 360.0f;
                        }
                    } else {
                        angles[n] += 1.0f;
                        if (angles[n] >= 360.0f) {
                            angles[n] -= 360.0f;
                        }
                    }
                }
            }
            const TouchState& t = story.touch;
            if (t.pressed && !toBox.held(t)) {
                if (current != -1 && onRim(pieces[current])) {
                    pieces[current].turning = true;
                    turnOffset = touchAngle(pieces[current]) - angles[current];
                } else if (current != -1 && onPiece(pieces[current])) {
                    if (!pieces[current].dragging && story.sound) {
                        story.sound->effect(0x25a);
                    }
                    pieces[current].dragging = true;
                } else {
                    for (int n = 0; n < kPieces; ++n) {
                        if (!pieces[n].hidden() && onPiece(pieces[n])) {
                            if (story.sound) {
                                story.sound->effect(0x25a);
                            }
                            pieces[n].dragging = true;
                            current = n;
                            break;
                        }
                    }
                }
            } else if (t.down && current != -1 && pieces[current].turning) {
                float a = touchAngle(pieces[current]) - turnOffset;
                while (a < 0) {
                    a += 360.0f;
                }
                while (a >= 360.0f) {
                    a -= 360.0f;
                }
                angles[current] = dragAngle = a;
            }
            if (t.released && current != -1 && pieces[current].turning) {
                angles[current] = dragAngle;
                if (story.sound) {
                    story.sound->effect(0x25b);
                }
            }
        }
        // The pieces at rest, then the one in hand on top.
        for (int n = 0; n < kPieces; ++n) {
            if (!pieces[n].dragging && !pieces[n].turning && n != current) {
                updatePiece(pieces[n], n, interactive);
            }
        }
        for (int n = 0; n < kPieces; ++n) {
            if (pieces[n].dragging || n == current || pieces[n].turning) {
                updatePiece(pieces[n], n, interactive);
                break;
            }
        }
        if (current != -1) {
            // The turning mark, pulsing.
            kit.select(turnMark, pieces[current].turning ? 2u : 1u);
            turnMark.x = pieces[current].x + 8;
            turnMark.y = pieces[current].y + 8;
            phase += 0.1f;
            const int alpha = int(10.0f * std::sin(phase)) + 10;
            if (alpha > 1) {
                kit.draw(turnMark, alpha);
            }
        }
        if (current != -1 && pieces[current].place == kTray && fresh) {
            kit.select(change, "change");
            change.x = 0x6c;
            change.y = 0x8f;
            kit.draw(change, 0xf);
        }
        for (int n = 0; n < kPieces; ++n) {
            if (!pieces[n].dragging && !pieces[n].turning && n != current &&
                pieces[n].place == kTray) {
                drawPiece(pieces[n], angles[n]);
            }
        }
        back.update(kit, story.touch);
    }

    void frame(bool interactive = true) {
        story.poll();
        draw(interactive);
        story.present();
    }

    // 0204ce24: the screens swap (both faded out and in, 2 a step).
    void swap() {
        fadeOut(kit, Screens::Both, 2);
        kit.swapScreens = mode != 0;
        mode = mode == 0 ? 1 : 0;
        story.poll();
        draw(false);
        story.present();
        fadeIn(kit, Screens::Both, 2);
    }

    bool settled() const { // 0204c548: every turn on a quarter
        for (int n = 0; n < kPieces; ++n) {
            if (int(angles[n]) % 90 != 0) {
                return false;
            }
        }
        return true;
    }

    // Pieces left in the tray go back into the box.
    void emptyTray() {
        for (int n = 0; n < kPieces; ++n) {
            if (pieces[n].place == kTray) {
                pieces[n].x = pieces[n].y = -1;
                pieces[n].dragging = pieces[n].turning = false;
                fresh = false;
                inBox[n] = true;
            }
        }
        current = -1;
    }

    // 0204cf78: the box on the touch screen; true: close the page.
    bool boxMode() {
        toFrame.cancel();
        for (;;) {
            frame();
            if (story.touch.released) {
                for (int r = 0, k = 0; r < 4; ++r) {
                    for (int c = 0; c < 5; ++c, ++k) {
                        const int n = boxOrder(k);
                        if (n >= kPieces || !inBox[n] ||
                            !touchIn(0x1e + 0x27 * c, 0x1f + 0x27 * r, 0x27, 0x27)) {
                            continue;
                        }
                        takeFromBox(n, c, r);
                        return false;
                    }
                }
            }
            if (toFrame.clicked(kit, story.touch)) {
                current = -1;
                swap();
                return false;
            }
            if (back.clicked(kit, story.touch)) {
                return true;
            }
        }
    }

    // A piece taken out: it flies up off the box toward x 0x6c, the screens
    // swap, it rises into the tray.
    void takeFromBox(int n, int column, int row) {
        if (story.sound) {
            story.sound->effect(0x25a);
        }
        float x = float(column * 0x27 + 0x1e) - 0xd;
        const float stepX = std::fabs(float(column * 0x27 + 0x1e) - float(0x6c)) /
                            (float(row * 0x27 + 0x4f) / 6.0f);
        inBox[n] = false;
        if (story.sound) {
            story.sound->effect(600);
        }
        for (int y = row * 0x27 + 0x12; y + 0x2a > 0; y -= 6) {
            if (x < float(0x6c)) {
                x = std::min(x + stepX, float(0x6c));
            } else if (x > float(0x6c)) {
                x = std::max(x - stepX, float(0x6c));
            }
            story.poll();
            kit.select(small, unsigned(n + 1));
            small.x = int(x) + (kit.frameWidth(small) >> 1);
            small.y = y - 6 + (kit.frameHeight(small) >> 1);
            kit.drawRotated(small, turnsOf(angles[n]));
            draw(true);
            story.present();
        }
        current = n;
        Piece& p = pieces[n];
        p.place = kTray;
        if (p.first == -1) {
            p.first = kTray;
        }
        fresh = true;
        p.x = 0x6c;
        p.y = 0xcb;
        swap();
        current = n;
        for (int y = 0xcb; y > 0x8f;) {
            y -= 6;
            p.y = y;
            frame();
        }
        if (story.sound) {
            story.sound->effect(0x259);
        }
    }

    // 0204d390: the frame on the touch screen; true: close the page (or the
    // painting is whole: the scene is then the event).
    bool frameMode() {
        toBox.cancel();
        for (;;) {
            for (int n = 0; n < kPieces; ++n) {
                Piece& p = pieces[n];
                if (current == -1 || p.place == kTray || current == n) {
                    continue;
                }
                if (!inside(p.x + 0x18, p.y + 0x18, 0x16, -0x10, 0xd1, 0xad) || p.y > 0xb0) {
                    p.place = kTray;
                    p.x = p.y = -1;
                    inBox[n] = true;
                    taken[p.place] = false;
                }
            }
            frame();
            if (!story.touch.down) {
                save();
            }
            const bool whole = state.paintingComplete();
            if (!whole) {
                wholeShown = false;
            } else if (!wholeShown && !(state.events[0x102] & 1)) {
                if (story.sound) {
                    story.sound->effect(0x25c);
                }
                wholeShown = true;
                for (int k = 0; k < 0x1e; ++k) {
                    frame();
                }
                state.event = 0x102;
                state.scene = scene::Event;
                state.returnScene = scene::Event;
                return true;
            }
            if (settled() && toBox.clicked(kit, story.touch)) {
                emptyTray();
                swap();
                return false;
            }
            if (settled() && back.clicked(kit, story.touch)) {
                emptyTray();
                return true;
            }
        }
    }

    void run() {
        // 0204ceb4: the 3D layer blends 15:15.
        frame();
        frame();
        fadeIn(kit, Screens::Both);
        for (bool done = false; !done;) {
            done = mode == 0 ? boxMode() : frameMode();
        }
    }

    // 0204bd98.
    void finish() {
        save();
        fadeOut(kit, Screens::Both);
        if (story.sound) {
            story.sound->fadeOutMusicSlowly();
        }
        story.stall(10);
        if (story.sound) {
            story.sound->forgetLoadedMusic();
            story.sound->setupRoom(state.room, [this] { story.stall(1); });
        }
        kit.swapScreens = false;
        kit.lowerPriority.reset();
        kit.clearText(SceneKit::Lower);
        kit.clearText(SceneKit::Upper);
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        story.present();
    }
};
} // namespace

void paintingPage(Story& story) {
    Painting page(story);
    page.run();
    page.finish();
}

} // namespace picarat::adventure1
