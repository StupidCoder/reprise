// The first adventure's hotel rooms. See a1_hotel.hpp.
#include "menus/a1_hotel.hpp"
#include "graphics/text.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "system/a1_script.hpp"
#include "system/a1_ui.hpp"
#include <cmath>

namespace picarat::adventure1 {

namespace {
constexpr int kCells = 0x18; // a room's grid of icons

// 0200c1ac: 8-bit components to the 3D engine's 15-bit colour.
std::uint16_t rgb(int r, int g, int b) {
    return std::uint16_t((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

// A piece of furniture in a room (020646ac, set by 0xbc / 0xbd).
struct Piece {
    int state = 0;                    // +0: 0 absent, 1 coming, 2 leaving, 3 there
    int alpha = 0;                    // +4
    int x = 0, y = 0;                 // +8 / +0xc: where it shows now
    int parent = -1;                  // +0x10: the piece it stands on (id)
    int dx = 0, dy = 0;               // +0x14 / +0x18: its place on that piece
    int baseX = 0, baseY = 0;         // +0x1c / +0x20: its place on the floor
    int layer = 0, stackedLayer = -1; // +0x24 / +0x28
    int room = 0;                     // +0x2c
    int id = 0;                       // +0x30
    int onParent = -1;                // +0x34: its furniture_2 frame on its parent
    float puffScale = 1.0f;           // +0x40
};

// An icon in a room's grid (02062f40: f_<id>.sbj).
struct Icon {
    SceneKit::SpriteObject sprite;
    int id = 0;
    int x = 0, y = 0;         // +0xc / +0x10
    int dragX = 0, dragY = 0; // +0x14 / +0x18
    bool dragging = false;    // +0x1c
    int room = 0, cell = 0;   // +0x20 / +0x24
};

struct Hotel {
    Story& story;
    SceneKit& kit;
    GameState& state;
    // The rooms' furniture (the data object 0206318c, hotelscript.gds).
    std::vector<Piece> pieces[2];
    std::string arrive[2][32], again[2][32];                // 0xc6 / 0xc8, 0xc7 / 0xc9
    int againIf[2][32]{};                                   // the condition id of 0xc7 / 0xc9
    std::vector<std::string> hints[2];                      // 0xca / 0xcb (hintscript.gds)
    std::string names[32];                                  // 0xcf
    SceneKit::SpriteObject furniture, onParentSprite, puff; // furniture_1/2.spr, hotel_kemuri.spr
    SceneKit::SpriteObject bubbles[2];                      // hukidashi_*.sbj (+0x73c / +0x740)
    SceneKit::SpriteObject faces[2];                        // ?/manzoku_*.spr (+0x428 / +0x42c)
    int speaker = 0;                                        // +0x738
    // The text being typed (02064d50: at (0x2d, 0x74) on the touch screen).
    std::string talk;
    int typed = -1;
    // The icons (02062608).
    std::vector<Icon> icons;
    int cells[2][kCells]{}; // the icon (index + 1) in each cell, 0 empty
    // The page (0202467c).
    SceneKit::SpriteObject happiness, roomButtons[2], backButton;
    Button ask[2], back;

    static std::string playerSprite(const Story& s, std::uint32_t address) {
        return s.programString(address);
    }

    explicit Hotel(Story& s) : story(s), kit(s.kit), state(s.state) {
        // 0202467c.
        story.present();
        if (story.sound) {
            story.sound->setupScene(0xd, [this] { story.stall(1); });
            story.sound->loadMusic(8, [this] { story.stall(1); });
            story.sound->startLoadedMusic(true);
        }
        kit.lowerPriority = std::array<int, 4>{0, 3, 1, 2};
        kit.lowerBackground = "house_a0_0";
        kit.upperBackground = "house_ico_bg";
        story.present();
        kit.swapScreens = true; // the icons on the touch screen
        loadData();
        loadIcons();
        // The guests' buttons (pressed looks; the program names them,
        // 020249a8 / 020249ac) and the close button.
        roomButtons[1] = kit.load(playerSprite(story, 0x020249a8), 0, 0, 1);
        roomButtons[1].x = 0x98;
        roomButtons[1].y = 0xae;
        roomButtons[0] = kit.load(playerSprite(story, 0x020249ac), 0, 0, 1);
        roomButtons[0].x = 2;
        roomButtons[0].y = 0xae;
        backButton = kit.load("?/hotel_button_back.sbj", 0, 0, 2);
        backButton.x = 0x6a;
        backButton.y = 0xae;
        ask[0].setRect(2, 0xae, 0x66, 0x10);
        ask[0].sound = 3;
        ask[1].setRect(0x98, 0xae, 0x66, 0x10);
        ask[1].sound = 3;
        back.setRect(0x6a, 0xae, 0x2c, 0x10);
        back.sound = 4;
        happiness = kit.load("?/happiness.spr", 0, 0, 1);
        state.newFurniture = false; // +0x706
    }

    // 0206318c: the furniture of both rooms and what the guests say.
    void loadData() {
        furniture = kit.load("furniture_1.spr", 0, 0, 1);
        onParentSprite = kit.load("furniture_2.spr", 0, 0, 1);
        bubbles[0] = kit.load(playerSprite(story, 0x02063638), 0, 0, 1);
        bubbles[0].x = 2;
        bubbles[0].y = 0x6c;
        bubbles[1] = kit.load(playerSprite(story, 0x0206363c), 0, 0, 1);
        bubbles[1].x = 0xd6;
        bubbles[1].y = 0x7c;
        ScriptContext c = story.commonCommands();
        auto piece = [this](CommandArguments& a, int room) {
            Piece p;
            p.id = a.integer();
            p.x = p.baseX = a.integer();
            p.y = p.baseY = a.integer();
            p.layer = a.integer();
            p.parent = a.integer();
            p.dx = a.integer();
            p.dy = a.integer();
            p.stackedLayer = a.integer();
            p.onParent = a.integer();
            p.room = room;
            if (pieces[room].size() < 0x20) {
                pieces[room].push_back(p);
            }
        };
        c.rebind(0xbc, [piece](CommandArguments& a) { piece(a, 0); }); // V248 0206423c
        c.rebind(0xbd, [piece](CommandArguments& a) { piece(a, 1); }); // V244 02064304
        // 0xc6 / 0xc8 (V24c / V254): what the guest says when a piece
        // arrives; 0xc7 / 0xc9 (V250 / V258): instead, when piece n is there.
        auto said = [this](CommandArguments& a, std::string(*list)[32], int room) {
            const std::size_t at = pieces[room].empty() ? 0 : pieces[room].size() - 1;
            list[room][at] = kit.text.expand(a.string());
        };
        c.rebind(0xc6, [this, said](CommandArguments& a) { said(a, arrive, 0); });
        c.rebind(0xc8, [this, said](CommandArguments& a) { said(a, arrive, 1); });
        c.rebind(0xc7, [this, said](CommandArguments& a) {
            said(a, again, 0);
            againIf[0][pieces[0].empty() ? 0 : pieces[0].size() - 1] = a.integer();
        });
        c.rebind(0xc9, [this, said](CommandArguments& a) {
            said(a, again, 1);
            againIf[1][pieces[1].empty() ? 0 : pieces[1].size() - 1] = a.integer();
        });
        // 0xcf (V264 02063c94): a piece's name.
        c.rebind(0xcf, [this](CommandArguments& a) {
            const int n = a.integer();
            const std::string name = kit.text.expand(a.string());
            if (n >= 0 && n < 32 && names[n].empty()) {
                names[n] = name;
            }
        });
        // 0xca / 0xcb (V25c / V260): the guests' words for each happiness.
        c.rebind(0xca, [this](CommandArguments& a) {
            hints[0].push_back(kit.text.expand(a.string()));
            a.integer();
            a.integer();
        });
        c.rebind(0xcb, [this](CommandArguments& a) {
            hints[1].push_back(kit.text.expand(a.string()));
            a.integer();
            a.integer();
        });
        story.runScript("script/hotelroom/" + story.language + "/hotelscript.gds", c);
        // The pieces the rooms hold are there already.
        for (int room = 0; room < 2; ++room) {
            const auto& has = room == 0 ? state.furnitureA : state.furnitureB;
            for (std::size_t i = 0; i < pieces[room].size() && i < has.size(); ++i) {
                if (has[i]) {
                    setState(pieces[room][i], 3);
                    setState(pieces[room][i], 1);
                    setState(pieces[room][i], 3);
                }
            }
        }
        faces[0] = kit.load("?/manzoku_layton.spr", 0, 0, 1);
        faces[0].x = 1;
        faces[0].y = 0x9f;
        faces[1] = kit.load("?/manzoku_luke.spr", 0, 0, 1);
        faces[1].x = 0x7f;
        faces[1].y = 0x9f;
        puff = kit.load("hotel_kemuri.spr", 0, 0, 1);
        story.runScript("script/hotelroom/" + story.language + "/hintscript.gds", c);
    }

    // 02062608: an icon for every piece a room holds.
    void loadIcons() {
        for (int room = 0; room < 2; ++room) {
            const auto& has = room == 0 ? state.furnitureA : state.furnitureB;
            for (int id = 1; id < 0x1f; ++id) {
                if (has[std::size_t(id - 1)]) {
                    addIcon(room, id);
                }
            }
        }
    }

    std::pair<int, int> cellPlace(int room, int cell) const { // 020628a4
        return {(room == 0 ? 4 : 0x84) + 0x18 * (cell % 5), 8 + 0x18 * (cell / 5)};
    }

    void addIcon(int room, int id) { // 0206275c
        if (icons.size() > 0x1f) {
            return;
        }
        for (int cell = 0; cell < kCells; ++cell) {
            if (cells[room][cell] == 0) {
                Icon icon;
                icon.sprite = kit.load("f_" + std::to_string(id) + ".sbj", 0, 0, 1);
                icon.id = id;
                std::tie(icon.x, icon.y) = cellPlace(room, cell);
                icon.room = room;
                icon.cell = cell;
                icons.push_back(icon);
                cells[room][cell] = int(icons.size());
                return;
            }
        }
    }

    // 02064428 / 020643cc: piece `id` is in `room` (coming or there).
    bool present(int room, int id) const {
        if (id < 1) {
            return false;
        }
        for (const auto& p : pieces[room]) {
            if (p.id == id && p.state != 0 && p.state != 2) {
                return true;
            }
        }
        return false;
    }

    bool onItsParent(const Piece& p) const { return p.parent != -1 && present(p.room, p.parent); }

    // 02064754: where a piece belongs (on its parent if that is there).
    std::pair<int, int> target(const Piece& p) const {
        int x = p.baseX, y = p.baseY;
        if (onItsParent(p)) {
            for (const auto& q : pieces[p.room]) {
                if (q.id == p.parent && q.state != 0) {
                    x = q.x;
                    y = q.y;
                }
            }
            x += p.dx;
            y += p.dy;
        }
        return {x, y};
    }

    int layerOf(const Piece& p) const { // 02064730
        return onItsParent(p) && p.stackedLayer != -1 ? p.stackedLayer : p.layer;
    }

    int frameOf(const Piece& p) const { return p.room == 0 ? p.id : p.id + 0x1e; }

    // 02064824.
    void setState(Piece& p, int s) {
        if (s == 2 || s == 3) {
            p.alpha = 0x1f;
        }
        p.state = s;
        if (s == 1) {
            p.alpha = 0;
            std::tie(p.x, p.y) = target(p);
            p.puffScale = 1.0f;
        }
    }

    // 02064d74: the guest's words, typed from the start.
    void say(const std::string& text) {
        talk = text.size() > 0x3ff ? text.substr(0, 0x3ff) : text;
        typed = -1;
        kit.clearText(SceneKit::Upper);
    }

    // 02064df4: one more character a frame (0200e418: fontevent, lines 16
    // apart, on the touch screen's text layer).
    void typeTalk() {
        if (typed < 0 && !talk.empty()) {
            typed = 0;
        }
        if (typed >= 0) {
            kit.clearText(SceneKit::Upper);
            const Font& font = kit.assets.font("fontevent.NFTR");
            Surface& layer = kit.textLayer(SceneKit::Upper);
            int x = 0x2d, y = 0x74;
            for (int i = 0; i < typed && i < int(talk.size()); ++i) {
                const auto c = std::uint8_t(talk[std::size_t(i)]);
                if (c == '\n') {
                    y += 12 + 4;
                    x = 0x2d;
                    continue;
                }
                if (c == '@') {
                    ++i;
                    continue;
                }
                textGlyph(layer, font, c, x, y, textColour(0xff));
                x += textAdvance(font, c) + 1;
            }
            if (typed < int(talk.size())) {
                ++typed;
                if (story.sound && typed % 5 == 0) {
                    story.sound->typingSound([this] { story.stall(1); });
                }
            }
        }
    }

    // 02064484: the rooms (by layer), the puffs, the guest's bubble, the
    // words, the happiness bars and faces (02063d70).
    void drawRooms() {
        kit.draw(bubbles[speaker]);
        const bool odd = (kit.frames & 1) != 0; // 020649d8 steps every second frame
        for (int layer = 0; layer < 0x20; ++layer) {
            for (int room = 0; room < 2; ++room) {
                for (auto& p : pieces[room]) {
                    if (layerOf(p) != layer) {
                        continue;
                    }
                    if (odd) {
                        if (p.state == 0) {
                            p.alpha = 0;
                        } else if (p.state == 3) {
                            p.alpha = 0x1f;
                        } else if (p.state == 1) {
                            p.alpha = std::min(p.alpha + 3, 0x1f);
                            if (p.alpha >= 0x1f) {
                                setState(p, 3);
                            }
                        } else if (p.state == 2) {
                            p.alpha = std::max(p.alpha - 3, 0);
                            if (p.alpha < 1) {
                                setState(p, 0);
                            }
                        }
                    }
                    drawPiece(p);
                }
            }
        }
        for (int room = 0; room < 2; ++room) {
            for (auto& p : pieces[room]) {
                if (p.state != 1) {
                    continue;
                }
                const auto [x, y] = target(p);
                kit.select(furniture, unsigned(frameOf(p)));
                puff.x = x + (kit.frameWidth(furniture) >> 1);
                puff.y = y + (kit.frameHeight(furniture) >> 1);
                if (0x1f - p.alpha > 0) {
                    kit.drawScaled(puff, p.puffScale);
                    p.puffScale += 0.1f;
                }
            }
        }
        for (int room = 0; room < 2; ++room) {
            auto& has = room == 0 ? state.furnitureA : state.furnitureB;
            for (std::size_t i = 0; i < pieces[room].size() && i < has.size(); ++i) {
                const int s = pieces[room][i].state;
                has[i] = std::uint8_t(s == 1 || s == 3);
            }
        }
        typeTalk();
        // 02063d70.
        const int a = state.hotelHappiness(0);
        kit.rectangle(0x1b, 0xa8, a * 0x14, 0x16, rgb(0xc0, 0x70, 0x28), 0xf);
        if (a != 0) {
            kit.select(faces[0], unsigned(a));
            kit.draw(faces[0]);
        }
        const int b = state.hotelHappiness(1);
        if (b != 0) {
            kit.rectangle(0x99, 0xa8, b * 0x14, 0x16, rgb(0x58, 0x78, 0x88), 0xf);
            kit.select(faces[1], unsigned(b));
            kit.draw(faces[1]);
        }
    }

    // 02064ba8: a piece glides (4 pixels a step) to where it belongs.
    void drawPiece(Piece& p) {
        if (p.state == 0 || p.alpha < 2) {
            return;
        }
        const auto [tx, ty] = target(p);
        p.x = p.x < tx ? std::min(p.x + 4, tx) : std::max(p.x - 4, tx);
        p.y = p.y < ty ? std::min(p.y + 4, ty) : std::max(p.y - 4, ty);
        if (onItsParent(p) && p.x == tx && p.y == ty && p.onParent != -1) {
            kit.select(onParentSprite, unsigned(p.onParent));
            onParentSprite.x = p.x;
            onParentSprite.y = p.y;
            kit.draw(onParentSprite, p.alpha);
            return;
        }
        kit.select(furniture, unsigned(frameOf(p)));
        furniture.x = p.x;
        furniture.y = p.y;
        kit.draw(furniture, p.alpha);
    }

    // What a guest says about piece `id` arriving in (or put back into) a
    // room (02063e64 / 02063eac).
    void comment(int room, int id) {
        speaker = room;
        const int i = id - 1;
        if (i < 0 || i >= 32) {
            return;
        }
        say(present(room, againIf[room][i]) ? again[room][i] : arrive[room][i]);
    }

    // 02062ad4: an icon let go: onto the nearest cell of either grid
    // (swapping with an icon there); between rooms the pieces move.
    void drop(Icon& icon) {
        int bestRoom = 0, bestCell = 0;
        float best = 10000.0f;
        for (int room = 0; room < 2; ++room) {
            for (int cell = 0; cell < 20; ++cell) {
                const auto [cx, cy] = cellPlace(room, cell);
                const float dx = float(cx - icon.dragX), dy = float(cy - icon.dragY);
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < best) {
                    best = d;
                    bestRoom = room;
                    bestCell = cell;
                }
            }
        }
        const int fromRoom = icon.room, fromCell = icon.cell;
        const int otherIndex = cells[bestRoom][bestCell];
        Icon* other = otherIndex ? &icons[std::size_t(otherIndex - 1)] : nullptr;
        if (fromRoom == bestRoom) {
            comment(fromRoom, icon.id);
        }
        if (fromCell == bestCell && fromRoom == bestRoom) {
            return;
        }
        if (bestRoom != fromRoom) {
            if (other) {
                setState(pieces[bestRoom][std::size_t(other->id - 1)], 2);
            }
            setState(pieces[bestRoom][std::size_t(icon.id - 1)], 1);
            setState(pieces[fromRoom][std::size_t(icon.id - 1)], 2);
            if (other) {
                setState(pieces[fromRoom][std::size_t(other->id - 1)], 1);
            }
        }
        const int self = cells[fromRoom][fromCell];
        cells[bestRoom][bestCell] = self;
        std::tie(icon.x, icon.y) = cellPlace(bestRoom, bestCell);
        icon.room = bestRoom;
        icon.cell = bestCell;
        cells[fromRoom][fromCell] = otherIndex;
        if (other) {
            std::tie(other->x, other->y) = cellPlace(fromRoom, fromCell);
            other->room = fromRoom;
            other->cell = fromCell;
        }
        if (bestRoom != fromRoom) {
            comment(bestRoom, icon.id);
        }
    }

    // 02062e14: the rooms, then the icons (the one dragged on top) and
    // their input.
    void updateHouse(bool interactive) {
        drawRooms();
        const TouchState& t = story.touch;
        Icon* held = nullptr;
        for (auto& icon : icons) {
            if (interactive && icon.dragging) {
                if (t.down) {
                    icon.dragX = t.x - 0x10;
                    icon.dragY = t.y - 0x10;
                }
                if (t.released) {
                    if (story.sound) {
                        story.sound->effect(0x263);
                    }
                    icon.dragging = false;
                    drop(icon);
                }
            }
            if (icon.dragging) {
                held = &icon;
            } else {
                icon.sprite.x = icon.x;
                icon.sprite.y = icon.y;
                kit.draw(icon.sprite);
            }
        }
        if (held) {
            held->sprite.x = held->dragX;
            held->sprite.y = held->dragY;
            kit.draw(held->sprite);
        } else if (interactive && t.pressed) {
            for (auto& icon : icons) {
                // 02063010: the press on the icon (2 pixels up and left).
                icon.dragX = icon.x;
                icon.dragY = icon.y;
                if (t.pressX >= icon.x - 2 && t.pressX <= icon.x + 0x16 && t.pressY >= icon.y - 2 &&
                    t.pressY <= icon.y + 0x16) {
                    say(names[std::size_t(icon.id)]);
                    if (story.sound) {
                        story.sound->effect(0x262);
                    }
                    icon.dragging = true;
                    speaker = icon.room;
                    break;
                }
            }
        }
    }

    // 02024b84.
    void draw(bool interactive) {
        for (int k = 0; k < 2; ++k) {
            happiness.x = k == 0 ? 0x25 : 0xa3;
            happiness.y = 0xae;
            kit.draw(happiness);
        }
        updateHouse(interactive);
        ask[0].update(kit, story.touch);
        ask[1].update(kit, story.touch);
        back.update(kit, story.touch);
        for (int k = 0; k < 2; ++k) {
            if (ask[k].held(story.touch)) {
                kit.draw(roomButtons[k]);
            }
        }
        kit.select(backButton, back.held(story.touch) ? 1u : 2u);
        kit.draw(backButton);
    }

    void frame() {
        story.poll();
        draw(true);
        story.present();
    }

    // 02063d00: one guest (at random) says how they like the room.
    void greet() {
        const int room = story.randomBelow(2) == 1 ? 0 : 1;
        speaker = room;
        const int level = state.hotelHappiness(room);
        if (level < int(hints[room].size())) {
            say(hints[room][std::size_t(level)]);
        }
    }

    void run() {
        // 02024c58.
        greet();
        frame();
        fadeIn(kit, Screens::Both);
        for (;;) {
            frame();
            if (back.clicked(kit, story.touch)) {
                return;
            }
            for (int k = 0; k < 2; ++k) {
                if (ask[k].clicked(kit, story.touch)) {
                    const int level = state.hotelHappiness(k);
                    if (level < int(hints[k].size())) {
                        say(hints[k][std::size_t(level)]);
                    }
                    speaker = k;
                }
            }
            if (state.hotelHappiness(0) == 5 && state.hotelHappiness(1) == 5 &&
                !(state.events[257] & 1)) {
                story.stall(0x1e);
                state.scene = scene::Event;
                state.event = 0x101;
                return;
            }
        }
    }

    // 020249bc.
    void finish() {
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

void hotelPage(Story& story) {
    Hotel page(story);
    page.run();
    page.finish();
}

} // namespace picarat::adventure1
