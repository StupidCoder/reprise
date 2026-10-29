// The first adventure's room scene. See a1_room.hpp.
#include "story/a1_room.hpp"
#include "sound/a1_sound.hpp"
#include "system/a1_script.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace picarat::adventure1 {

RoomScene::RoomScene(Story& s) : story(s), kit(s.kit) {
    // 0201c524.
    story.marker("room-ctor");
    GameState& state = story.state;
    if (story.sound) {
        story.sound->setupRoom(state.room, [this] { story.stall(1); });
    }
    // BG priorities (BGxCNT): text 0, BG2 0, 3D 1, BG3 3.
    kit.lowerPriority = std::array<int, 4>{1, 0, 0, 3};
    kit.clearText(SceneKit::Upper);
    kit.clearText(SceneKit::Lower);
    story.actors.clear();
    story.upperActors.clear();
    story.actorsActive = true;
    numbers_ = kit.load("?/puzzle_numbers.sbj", 0, 0, 1);
    mapIcon_ = kit.load("mapicon.sbj", 0, 0, 1);
    cursor_ = kit.load("cursor_wait.spr", 0xcc, 0x65, 1);
    kit.select(cursor_, "touch");
    cursor_.x = 0xcc;
    cursor_.y = 0x65;
    door_ = kit.load("exit_door.spr", 0, 0, 1);
    // The puzzle shack's question window (0201c524, room 'X').
    shack_ = state.room == 'X';
    if (shack_) {
        shackWindow_ = kit.load("prize_window.spr", 0x18, 0x38, 1);
        kit.select(shackWindow_, "window");
        shackWindow_.x = 0x18;
        shackWindow_.y = 0x38;
        shackYes_.setSprite(kit, 0x28, 0x68, "?/room_buttons.spr", "yes");
        shackYes_.visibleIdle = true;
        shackYes_.setPressedAnimation(kit, "yesp");
        shackYes_.sound = 3;
        shackNo_.setSprite(kit, 0x88, 0x68, "?/room_buttons.spr", "no");
        shackNo_.visibleIdle = true;
        shackNo_.setPressedAnimation(kit, "nop");
        shackNo_.sound = 4;
    }
    build();
    if (story.sound) {
        story.sound->startLoadedMusic(true);
        story.sound->rememberMusic();
    }
    state.byte9 = 0; // +9
    // The first room shows a hint to touch the screen (+0x710, rooms 1 and
    // 0x55).
    showFirstTouch_ = state.firstTouchHint && (state.room == 1 || state.room == 0x55);
    if (showFirstTouch_) {
        firstTouch_ = kit.load("?/firsttouch.spr", 0, 0, 1);
        firstTouch_.x = (0x100 - kit.frameWidth(firstTouch_)) >> 1;
        firstTouch_.y = (0xc0 - kit.frameHeight(firstTouch_)) >> 1;
    }
}

void RoomScene::build() {
    // 0201dbb4 (and the constructor): the touch icon, the room's picture,
    // the parameter script, the buttons.
    GameState& state = story.state;
    touchIcon_ = kit.load("touch_icon.spr", 0, 0, 1);
    touchIcon_.x = touchIcon_.y = -0x20;
    kit.lowerOverlay.clear();
    kit.lowerTint = {};
    const std::string picture = "room_" + std::to_string(state.room) + "_bg";
    if (fileExists(story.pack / "bg" / (picture + ".l2bg"))) {
        kit.lowerBackground = picture;
    }
    exits_.clear();
    examines_.clear();
    characters_.clear();
    markZones_.clear();
    coinZones_.clear();
    animations_.clear();
    decorations_.clear();
    dogSpots_.clear();
    dog_ = {};
    dog_.sprite = kit.load("cydog.spr", 0, 0, 1);
    story.runScript("script/rooms/room" + std::to_string(state.room) + "_param.gds", commands());
    options_.setSprite(kit, 0xe6, 2, "option_icon.spr", "options");
    options_.visibleIdle = true;
    options_.sound = 3;
    moveMode_.setSprite(kit, 0xe4, 0xa4, "movemode.spr", "boot");
    moveMode_.visibleIdle = true;
    moveMode_.sound = 3;
    examineWindow_ = kit.load("room_tobj.spr", 0x20, 0x48, 1);
    examineWindow_.x = 0x20;
    examineWindow_.y = 0x48;
    examineIcon_ = kit.load("room_tobjp.spr", 0x25, 0x54, 1);
    examineIcon_.x = 0x25;
    examineIcon_.y = 0x54;
    if (shack_) {
        shackNumbers_ = kit.load("nazobaba_num.spr", 0, 0, 1);
        shackShuffle_.setSprite(kit, 0xc, 5, "?/nazobaba_btn.spr", "off");
        shackShuffle_.setPressedAnimation(kit, "on");
        shackShuffle_.visibleIdle = true;
        shackShuffle_.sound = 0x2e;
    }
}

void RoomScene::pickDogSpot() {
    // 02065d34: one spot at random, then its chance in percent.
    if (dogSpots_.empty()) {
        return;
    }
    const int i = story.randomBelow(int(dogSpots_.size()));
    if (story.randomBelow(100) > dogSpots_[std::size_t(i)].chance) {
        return;
    }
    const DogSpot& spot = dogSpots_[std::size_t(i)];
    dog_.active = true;
    dog_.coin = spot.coin;
    dog_.target = spot.x;
    dog_.y = spot.y;
    dog_.facing = spot.facing;
    dog_.x = spot.facing ? spot.x - 0x10 : spot.x + 0x10;
    dog_.index = i;
    dog_.timer = 0xb4;
    dog_.alpha = 0;
}

void RoomScene::updateDog() {
    // 02065e34.
    if (!dog_.active) {
        return;
    }
    const int dir = dog_.x < dog_.target ? 1 : dog_.x > dog_.target ? -1 : 0;
    if (dir != 0) {
        if (dog_.timer == 0) {
            // Walking in: 2 pixels and 5 steps of alpha a frame.
            dog_.alpha = std::min(0x1f, dog_.alpha + 5);
            dog_.x += 2 * dir;
            if ((dir == 1 && dog_.x > dog_.target) || (dir == -1 && dog_.x < dog_.target) ||
                dog_.x == dog_.target) {
                dog_.x = dog_.target;
                kit.select(dog_.sprite, unsigned(dogSpots_[std::size_t(dog_.index)].pose));
                dog_.timer = 0x3c;
            }
        } else {
            // Waiting unseen; a hint coin taken meanwhile sends it away.
            if (dog_.timer > 0x28 && story.state.coinTaken(dog_.coin)) {
                dog_.busy = dog_.active = false;
                return;
            }
            if (--dog_.timer == 0x28) {
                if (story.sound) {
                    story.sound->effect(0x18); // the bark
                }
                if (story.state.dogFinds < 999) {
                    ++story.state.dogFinds;
                }
                dog_.busy = true;
            }
        }
    }
    if (dog_.x == dog_.target) {
        // In place a second, then fading out one step a frame.
        if (dog_.timer > 0) {
            --dog_.timer;
        } else if (dog_.alpha > 0) {
            --dog_.alpha;
        } else {
            dog_.busy = dog_.active = false;
        }
    }
    // 02065de8: drawn from alpha 2 on, mirrored when it came from the left.
    if (dog_.alpha >= 2) {
        dog_.sprite.x = dog_.x;
        dog_.sprite.y = dog_.y;
        if (dog_.facing) {
            kit.drawMirrored(dog_.sprite, dog_.alpha);
        } else {
            kit.draw(dog_.sprite, dog_.alpha);
        }
    }
}

void RoomScene::finish() {
    // 0201cdac.
    story.actors.clear();
    story.upperActors.clear();
    kit.clearText(SceneKit::Upper);
    kit.clearText(SceneKit::Lower);
    kit.lowerBackground.clear();
    kit.upperBackground.clear();
    kit.upperOverlay.clear();
    kit.lowerOverlay.clear();
    kit.lowerTint = {};
    story.present();
}

// ---------------------------------------------------------------------------
// The parameter script's commands.

ScriptContext RoomScene::commands() {
    ScriptContext c = story.commonCommands();
    c.rebind(command::MainBackground, [this](CommandArguments& a) {
        const std::string name = a.string();
        a.integer();
        story.setPicture(kit.lowerBackground, name);
    });
    // 0x5a (V164 0201e388): the place: its name (storytext map<N>.txt), the
    // map picture (map_<M>.bgx, upper screen), the marker's place and
    // animation; the chapter title (mt_<chapter>.txt).
    c.rebind(0x5a, [this](CommandArguments& a) {
        const int name = a.integer(), map = a.integer(), x = a.integer(), y = a.integer(),
                  animation = a.integer();
        kit.upperBackground = "map_" + std::to_string(map);
        std::string place =
            story.text("storytext", "storytext", "map" + std::to_string(name) + ".txt");
        if (place.empty()) {
            place = "PLACE : " + std::to_string(name);
        }
        story.state.place = place;
        kit.select(mapIcon_, unsigned(animation));
        mapIcon_.x = x;
        mapIcon_.y = y;
        mapPicture_ = map;
    });
    // 0x22 (V150 0201e77c): a way out.
    c.rebind(0x22, [this](CommandArguments& a) {
        const int sprite = a.integer(), x = a.integer(), y = a.integer(), w = a.integer(),
                  h = a.integer(), target = a.integer(), mx = a.integer(), my = a.integer();
        if (exits_.size() >= 8) {
            return;
        }
        Exit e;
        e.target = target & 0xff;
        if (sprite == 0) {
            e.button.setRect(x, y, w, h);
        } else {
            e.arrow = true;
            e.button.setSprite(kit, x, y, "exit_" + std::to_string(sprite) + ".spr");
            e.button.visibleIdle = true;
            e.markerX = mx;
            e.markerY = my;
        }
        exits_.push_back(std::move(e));
    });
    // 0x5b (V168): the sound type of way n.
    c.rebind(0x5b, [this](CommandArguments& a) {
        const int n = a.integer(), type = a.integer();
        if (n >= 0 && n < int(exits_.size())) {
            exits_[std::size_t(n)].type = std::int8_t(type);
        }
    });
    // 0x43 (V158 0201e8d4): a spot with a remark.
    c.rebind(0x43, [this](CommandArguments& a) {
        const int icon = a.integer(), x = a.integer(), y = a.integer(), w = a.integer(),
                  h = a.integer(), text = a.integer();
        a.integer();
        if (examines_.size() >= 16) {
            return;
        }
        Examine e;
        e.button.setRect(x, y, w, h);
        e.button.sound = 3;
        e.icon = icon;
        e.text = text;
        examines_.push_back(std::move(e));
    });
    // 0x50 (V15c 0201e964): a character or thing to talk to (its event),
    // with its picture obj_<n>.spr when n != 0 and its mark (V14c).
    c.rebind(0x50, [this](CommandArguments& a) {
        const int x = a.integer(), y = a.integer(), w = a.integer(), h = a.integer(),
                  sprite = a.integer(), event = a.integer();
        if (characters_.size() >= 16) {
            return;
        }
        Character ch;
        ch.button.setRect(x, y, w, h);
        if (sprite != 0) {
            ch.hasPicture = true;
            ch.picture = kit.load("obj_" + std::to_string(sprite) + ".spr", x, y, 1);
            ch.picture.x = x;
            ch.picture.y = y;
        }
        ch.event = event;
        characters_.push_back(std::move(ch));
        Zone z;
        z.x = x;
        z.y = y;
        z.w = w;
        z.h = h;
        z.sprite = kit.load("icon_buttons.spr", 0, 0, 1);
        kit.select(z.sprite, "found"); // 020519a8 (the same picture as "3")
        z.sprite.x = x + (w >> 1);
        z.sprite.y = y + (h >> 1);
        z.event = event;
        markZones_.push_back(std::move(z));
    });
    // 0xd1 (V178 0201e5a0 -> 02065b2c) / 0xd2 (V17c -> 02065c44): a spot the
    // robot dog may sniff out once it is complete (all 20 parts, 0201b1e0):
    // an event not run yet, or a hint coin not taken yet; at most eight.
    // Arguments: the event or coin, x, y, facing, a number the handlers
    // skip (02016258 passes a chance of 30 percent instead), and a pose (0:
    // a random one of three).
    auto dogSpot = [this](CommandArguments& a, bool coin) {
        const int n = a.integer(), x = a.integer(), y = a.integer();
        const bool facing = a.truth();
        const int chance = 0x1e, pose = a.integer();
        const GameState& state = story.state;
        if (!state.dogComplete()) {
            return;
        }
        if (coin ? state.coinTaken(n)
                 : (n >= 0 && n < 0x1df && (state.events[std::size_t(n)] & 1))) {
            return;
        }
        if (dogSpots_.size() >= 8) {
            return;
        }
        dogSpots_.push_back(
            {coin ? n : -1, x, y, facing, chance, (pose == 0 ? story.randomBelow(3) : pose) + 2});
    };
    c.rebind(0xd1, [dogSpot](CommandArguments& a) { dogSpot(a, false); });
    c.rebind(0xd2, [dogSpot](CommandArguments& a) { dogSpot(a, true); });
    // 0xe4 (V160 0201eae8): in the puzzle shack, a spot showing the next
    // puzzle sent there (flag 800 + n) and not solved yet (0201b74c picks
    // them in turn), with a random pose of obj_60.spr; at most 16.
    c.rebind(0xe4, [this](CommandArguments& a) {
        const int x = a.integer(), y = a.integer(), w = a.integer(), h = a.integer();
        a.integer();
        if (story.state.shackWaiting() == 0 || characters_.size() >= 16) {
            return;
        }
        const int puzzle = story.state.nextShackPuzzle();
        if (puzzle < 1) {
            return;
        }
        for (const auto& ch : characters_) {
            if (ch.puzzle == puzzle) {
                return;
            }
        }
        Character ch;
        ch.button.setRect(x, y, w, h);
        ch.hasPicture = true;
        ch.picture = kit.load("obj_60.spr", x, y, 1);
        ch.picture.x = x;
        ch.picture.y = y;
        kit.select(ch.picture, unsigned(story.randomBelow(5) + 1));
        ch.puzzle = puzzle;
        ch.event = -puzzle;
        ch.hasPuff = true;
        ch.puff = kit.load("nazobaba_kemuri.spr", 0, 0, 1);
        ch.puff.x = x + 3 - (kit.frameWidth(ch.puff) >> 1);
        ch.puff.y = y + 1 - (kit.frameHeight(ch.puff) >> 1);
        ch.puffFrames = 100;
        Zone z;
        z.x = x;
        z.y = y;
        z.w = w;
        z.h = h;
        z.sprite = kit.load("icon_buttons.spr", 0, 0, 1);
        kit.select(z.sprite, "found"); // 020519a8 (the same picture as "3")
        z.sprite.x = x + (w >> 1);
        z.sprite.y = y + (h >> 1);
        z.event = ch.event;
        markZones_.push_back(std::move(z));
        characters_.push_back(std::move(ch));
    });
    // 0x7e (V214 0201e194): an upper-screen 2D sprite (at most eight).
    c.rebind(0x7e, [this](CommandArguments& a) {
        const int x = a.integer(), y = a.integer();
        const std::string sprite = a.string(), animation = a.string();
        if (story.upperActors.size() < 8) {
            story.addActor(true, x, y, sprite, animation);
        }
    });
    // 0x23 (V148 0201ee88): an animated part of the room
    // (room_<room>_<n>.spr).
    c.rebind(0x23, [this](CommandArguments& a) {
        const int x = a.integer(), y = a.integer();
        if (animations_.size() >= 10) {
            return;
        }
        auto s = kit.load("room_" + std::to_string(story.state.room) + "_" +
                              std::to_string(animations_.size() + 1) + ".spr",
                          x, y, 1);
        s.x = x;
        s.y = y;
        animations_.push_back(std::move(s));
    });
    // 0x5c (V16c 0201ef3c): a sprite of the room's scenery.
    c.rebind(0x5c, [this](CommandArguments& a) {
        const int x = a.integer(), y = a.integer();
        const std::string name = a.string();
        if (decorations_.size() >= 16) {
            return;
        }
        auto s = kit.load(name, x, y, 1);
        s.x = x;
        s.y = y;
        decorations_.push_back(std::move(s));
    });
    // 0x68 (V170 0201e6a0): a hint coin's hiding place, unless taken.
    c.rebind(0x68, [this](CommandArguments& a) {
        const int coin = a.integer(), x = a.integer(), y = a.integer(), w = a.integer(),
                  h = a.integer();
        a.integer();
        if (coinZones_.size() >= 16 || story.state.coinTaken(coin)) {
            return;
        }
        Zone z;
        z.x = x;
        z.y = y;
        z.w = w;
        z.h = h;
        z.sprite = kit.load("hintcoin.spr", 0, 0, 1);
        kit.select(z.sprite, "gfx");
        z.sprite.x = x + (w >> 1);
        z.sprite.y = y + (h >> 1);
        z.coin = coin;
        coinZones_.push_back(std::move(z));
    });
    // 0xab (V174): the event after a hint coin is taken (+0x3f8).
    c.rebind(0xab, [this](CommandArguments& a) { eventAfterCoin_ = a.integer(); });
    // 0x2c (V154): empty.
    c.rebind(0x2c, [](CommandArguments& a) { a.integer(); });
    // 0xd1 / 0xd2 (V178 / V17c 02065b2c / 02065c44): places where the
    // robot dog may sniff (only once it is built; not reconstructed yet).
    // 0xe4 (V160 0201eae8): the shack's puzzle spots (not reconstructed).
    return c;
}

// ---------------------------------------------------------------------------
// Drawing.

void RoomScene::updateZone(Zone& z, bool interactive) {
    // 0205192c: a press inside the zone touches it; 020519a8: drawing (the
    // popping-up animation after a touch).
    const TouchState& t = story.touch;
    if (interactive) {
        const bool inside = t.down && z.x <= t.pressX && t.pressX <= z.x + z.w && z.y <= t.pressY &&
                            t.pressY <= z.y + z.h;
        if (inside) {
            z.touched = z.latch = true;
        } else if (z.latch) {
            z.latch = false;
        } else {
            z.touched = false;
        }
    }
    if (z.event > 0 && z.event < 0x1e0 && (story.state.events[std::size_t(z.event)] & 2)) {
        return;
    }
    const int cx = z.x + (z.w >> 1), cy = z.y + (z.h >> 1);
    if (z.state == 1) {
        z.time += 0.1f;
        if (z.time >= 3.1415925f) {
            z.time = 0;
            z.state = 2;
        } else {
            z.sprite.x = cx;
            z.sprite.y = cy - int(30.0f * std::sin(z.time));
            kit.draw(z.sprite);
        }
    }
    if (z.state == 2) {
        z.sprite.x = cx;
        z.sprite.y = cy;
        kit.draw(z.sprite);
    }
}

void RoomScene::drawUpper() {
    // The upper screen: the place's name (y 7, centred on 0xa7), the
    // chapter's title (y 0xac), the solved puzzles' count (0201f1dc).
    kit.clearText(SceneKit::Upper);
    drawCentredText(kit, SceneKit::Upper, 7, story.state.place, false, 0, 0xa7);
    std::string chapter =
        story.text("storytext", "storytext", "mt_" + std::to_string(story.state.chapter) + ".txt");
    if (chapter.empty()) {
        chapter = "NO DATA : " + std::to_string(story.state.chapter);
    }
    drawCentredText(kit, SceneKit::Upper, 0xac, chapter);
    kit.select(numbers_, "text");
    numbers_.x = 1;
    numbers_.y = 4;
    kit.draw(numbers_);
    // 0201f0c4: up to three digits from (0x36, 9), 7 pixels apart; leading
    // zeros left out.
    const int count = story.state.solvedCount();
    int x = 0x36;
    if (count / 100) {
        kit.select(numbers_, unsigned(count / 100 % 10 + 1));
        numbers_.x = x;
        numbers_.y = 9;
        kit.draw(numbers_);
        x += 7;
    }
    if (count / 10) {
        kit.select(numbers_, unsigned(count / 10 % 10 + 1));
        numbers_.x = x;
        numbers_.y = 9;
        kit.draw(numbers_);
        x += 7;
    }
    kit.select(numbers_, unsigned(count % 10 + 1));
    numbers_.x = x;
    numbers_.y = 9;
    kit.draw(numbers_);
    // 0200cfe4: the marker is centred on its place (the frame's size halved).
    const int markerX = mapIcon_.x, markerY = mapIcon_.y;
    mapIcon_.x = markerX - (kit.frameWidth(mapIcon_) >> 1);
    mapIcon_.y = markerY - (kit.frameHeight(mapIcon_) >> 1);
    kit.draw(mapIcon_);
    mapIcon_.x = markerX;
    mapIcon_.y = markerY;
}

void RoomScene::draw(bool interactive) {
    // 0201f34c.
    for (auto& d : decorations_) {
        kit.draw(d, 0x1f);
    }
    for (auto& ch : characters_) {
        ch.button.update(kit, story.touch);
        if (ch.hasPicture && alpha_ > 1) {
            kit.draw(ch.picture, alpha_);
        }
    }
    if (shack_) {
        for (const auto& ch : characters_) {
            drawShackNumber(ch.picture.x + 3, ch.picture.y - 0xb, ch.puzzle);
        }
        if (story.state.shackWaiting() > 8) {
            shackShuffle_.update(kit, story.touch);
        }
    }
    for (auto& a : animations_) {
        kit.draw(a, 0x1f);
    }
    for (auto& z : markZones_) {
        updateZone(z, interactive);
    }
    for (auto& e : exits_) {
        if (!e.arrow) {
            e.button.update(kit, story.touch);
        }
    }
    if (story.state.room != 'w' && !exits_.empty()) {
        moveMode_.update(kit, story.touch);
    }
    for (auto& e : examines_) {
        if (interactive) {
            e.button.update(kit, story.touch);
        }
    }
    for (auto& z : coinZones_) {
        if (!story.state.coinTaken(z.coin)) {
            updateZone(z, interactive);
        }
    }
    drawUpper();
    if (story.state.room != 'w') {
        options_.update(kit, story.touch);
    }
}

// ---------------------------------------------------------------------------
// Actions.

void RoomScene::examine(int index) {
    // 0201f5e8: the remark window: room/tobj t_<n>.txt (t_0.txt for -1),
    // its icon, the cursor until a touch.
    kit.select(examineIcon_, index < 0 ? 3u : unsigned(examines_[std::size_t(index)].icon));
    kit.clearText(SceneKit::Lower);
    const int number = index < 0 ? 0 : examines_[std::size_t(index)].text;
    std::string text = story.text("room/tobj", "tobj", "t_" + std::to_string(number) + ".txt");
    if (text.empty()) {
        text = "NO DATA : " + std::to_string(index);
    }
    for (int a = 2; a < 0x1f; a += 2) {
        story.frame([&] {
            draw(false);
            kit.draw(examineWindow_, a);
        });
    }
    // 0200de00(layer, 0x42, 0x5a, text, -2): left at x 0x42, centred on y 0x5a.
    {
        Surface& layer = kit.textLayer(SceneKit::Lower);
        drawCentredText(kit, layer, 0x5a, text, false, -2, -0x42);
    }
    for (int k = 2;;) {
        story.frame([&] {
            draw(false);
            kit.draw(examineWindow_, 0x1f);
            kit.draw(examineIcon_, 0x1f);
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
    for (int a = 0x1f; a > 1; a -= 2) {
        story.frame([&] {
            draw(false);
            kit.draw(examineWindow_, a);
        });
    }
    story.frame([&] { draw(false); });
}

void RoomScene::takeCoin(int index) {
    // 0201f238: the coin pops up (sound 0x10), then counts.
    Zone& z = coinZones_[std::size_t(index)];
    if (story.state.coinTaken(z.coin)) {
        return;
    }
    if (story.sound) {
        story.sound->effect(0x10);
    }
    z.state = 1;
    z.time = 0;
    while (z.state == 1) {
        story.frame([&] { draw(false); });
    }
    z.state = 0;
    story.state.puzzle.coins = std::uint16_t(story.state.puzzle.coins + 1);
    story.state.takeCoin(z.coin);
}

void RoomScene::leaveBy(const Exit& e) {
    // The way's sound (type 1: 0x36, 2: 0x3a, else 0x32), the room to
    // enter, the sound to play there (+0x4ec: 0x37 / 0x3b / 0x33).
    GameState& state = story.state;
    if (story.sound) {
        story.sound->effect(e.type == 1 ? 0x36 : e.type == 2 ? 0x3a : 0x32);
    }
    state.setPuzzle(-1);
    state.room = std::uint8_t(e.target);
    fadeOut(kit, Screens::Both);
    state.scene = scene::Room;
    state.enterSound = e.type == 1 ? 0x37 : e.type == 2 ? -1 : e.type == 4 ? 0x3b : 0x33;
}

bool RoomScene::eventLeadsToPuzzle(int event) {
    // An event's script (e<N>.gds) decides by the game's state whether it
    // plays a puzzle: it makes its puzzle current (0x48) and leaves the
    // scene on "question" (0x51) only on some branches - e.g. the car in the
    // first room talks without its puzzle while the story number (0x63) is
    // 1, and a puzzle already done sets "question" and then "room" again.
    // The script runs here as the room's touch starts it (scene Event,
    // before its puzzle) on a copy of the state, with the game's conditions
    // and the commands that set what they test, the next event or the next
    // scene (0x48, 0x62, 0x8e, 0x60, 0x51); everything else does nothing. A
    // puzzle follows when the scene is Question at the end; an event that
    // hands over to another (0x60, 0x51 "event") is followed into it.
    auto [it, fresh] = eventPuzzles_.try_emplace(event, false);
    if (!fresh) {
        return it->second;
    }

    struct Restore {
        Story& story;
        GameState saved;

        ~Restore() { story.state = std::move(saved); }
    } restore{story, story.state};

    story.state.event = std::uint32_t(event);
    story.state.scene = scene::Event;
    story.state.afterPuzzle = 0;
    ScriptContext c = story.commonCommands();
    for (const auto& info : scripts().commands) {
        const std::uint16_t kept[] = {0x48, 0x62, 0x8e, 0x60, 0x51};
        if (std::find(std::begin(kept), std::end(kept), info.command) != std::end(kept) ||
            c.hasCondition(info.command)) {
            continue;
        }
        c.rebind(info.command, [](CommandArguments& a) {
            while (!a.consumed()) {
                a.skip();
            }
        });
    }
    try {
        for (int step = 0; step < 4 && story.state.scene == scene::Event; ++step) {
            const unsigned current = story.state.event;
            const Bytes data = story.script("script/event/e" + std::to_string(current) + ".gds");
            if (data.size() < 4) {
                break;
            }
            ScriptInterpreter(data, scripts()).run(c);
            if (story.state.scene == scene::Event && story.state.event == current) {
                break; // stays: the event itself ends here
            }
            story.state.afterPuzzle = 0;
        }
        it->second = story.state.scene == scene::Question;
    } catch (const std::exception&) {
    }
    return it->second;
}

std::vector<TouchArea> RoomScene::touchAreas() {
    std::vector<TouchArea> out;
    auto add = [&out](TouchArea::Kind kind, int x, int y, int w, int h) {
        if (w > 0 && h > 0) {
            out.push_back({kind, x, y, w, h});
        }
    };
    for (const auto& ch : characters_) {
        // As the touch handler: an event (0..0x1df), else the shack's puzzle.
        const bool open = ch.event >= 0 ? eventLeadsToPuzzle(ch.event > 0x1df ? 0 : ch.event)
                                        : ch.puzzle > 0 && !(story.state.progress(ch.puzzle) & 4);
        add(open ? TouchArea::Kind::Puzzle : TouchArea::Kind::Dialog, ch.button.x, ch.button.y,
            ch.button.width, ch.button.height);
    }
    for (const auto& e : examines_) {
        add(TouchArea::Kind::Text, e.button.x, e.button.y, e.button.width, e.button.height);
    }
    for (const auto& z : coinZones_) {
        if (!story.state.coinTaken(z.coin)) {
            add(TouchArea::Kind::HintCoin, z.x, z.y, z.w, z.h);
        }
    }
    return out;
}

void RoomScene::reportTargets(bool move) {
    if (!story.researchRoom) {
        return;
    }
    std::vector<Story::ResearchTarget> list;
    for (const auto& ch : characters_) {
        list.push_back({Story::ResearchTarget::Character, ch.button.x, ch.button.y, ch.button.width,
                        ch.button.height, ch.event});
    }
    for (const auto& e : exits_) {
        Story::ResearchTarget t{Story::ResearchTarget::Exit,
                                e.button.x,
                                e.button.y,
                                e.button.width,
                                e.button.height,
                                e.target,
                                e.arrow};
        if (move && !e.arrow) {
            placeDoor(e);
            t.x = door_.x;
            t.y = door_.y;
            t.w = kit.frameWidth(door_);
            t.h = kit.frameHeight(door_);
        }
        list.push_back(t);
    }
    for (const auto& z : coinZones_) {
        if (!story.state.coinTaken(z.coin)) {
            list.push_back({Story::ResearchTarget::Coin, z.x, z.y, z.w, z.h, z.coin});
        }
    }
    if (story.state.room != 'w' && !exits_.empty()) {
        list.push_back({Story::ResearchTarget::MoveMode, moveMode_.x, moveMode_.y, moveMode_.width,
                        moveMode_.height, 0});
    }
    story.researchRoom(move, list);
}

void RoomScene::drawMoveMode(float phase) {
    // 0201fb08: the room without its remarks and coins; plain ways out are
    // marked by a pulsing door (alpha 15 sin(phase) + 16), arrows show.
    story.poll();
    for (auto& d : decorations_) {
        kit.draw(d, 0x1f);
    }
    for (auto& ch : characters_) {
        ch.button.update(kit, story.touch);
        if (ch.hasPicture) {
            kit.draw(ch.picture, 0x1f);
        }
    }
    for (auto& e : exits_) {
        if (!e.arrow) {
            placeDoor(e);
            const int alpha = int(15.0f * std::sin(phase)) + 0x10;
            if (alpha > 1) {
                kit.draw(door_, alpha);
            }
        }
        e.button.update(kit, story.touch);
    }
    drawUpper();
    reportTargets(true);
    story.present();
}

void RoomScene::placeDoor(const Exit& e) {
    door_.x = e.button.x + (e.button.width >> 1) - (kit.frameWidth(door_) >> 1);
    door_.y = e.button.y + (e.button.height >> 1) - (kit.frameHeight(door_) >> 1);
}

void RoomScene::walkMarker(const Exit& e) {
    // 0201f858: the map marker walks to the way's place on the map, three
    // pixels a step along the line, a step every two frames.
    if (e.markerX == 0 && e.markerY == 0) {
        return;
    }
    const float dx = float(e.markerX - mapIcon_.x), dy = float(e.markerY - mapIcon_.y);
    const float steps = std::sqrt(dx * dx + dy * dy) / 3.0f;
    const float stepX = std::fabs(dx) / steps, stepY = std::fabs(dy) / steps;
    float x = float(mapIcon_.x), y = float(mapIcon_.y);
    for (;;) {
        if (int(x) < e.markerX) {
            x = std::min(x + stepX, float(e.markerX));
        } else if (int(x) > e.markerX) {
            x = std::max(x - stepX, float(e.markerX));
        }
        if (int(y) < e.markerY) {
            y = std::min(y + stepY, float(e.markerY));
        } else if (int(y) > e.markerY) {
            y = std::max(y - stepY, float(e.markerY));
        }
        mapIcon_.x = int(x);
        mapIcon_.y = int(y);
        if (mapIcon_.x == e.markerX && mapIcon_.y == e.markerY) {
            break;
        }
        story.poll();
        drawUpper();
        story.present();
        story.stall(1);
    }
    mapIcon_.x = e.markerX;
    mapIcon_.y = e.markerY;
    story.poll();
    drawUpper();
    story.present();
}

bool RoomScene::moveMode() {
    // 0201fce8: until a way out is chosen (true: the room scene ends) or
    // the stylus is lifted elsewhere. Plain ways (and arrows of type -1)
    // leave the room; an arrow walks the map marker to the next place and
    // builds it in place.
    GameState& state = story.state;
    state.byte12 = 0;
    float phase = 1.5707964f; // +0x4e8
    for (;;) {
        phase += 0.1f;
        drawMoveMode(phase);
        for (auto& e : exits_) {
            // A plain way is hit where its door is drawn.
            bool door = false;
            if (!e.arrow && story.touch.released) {
                placeDoor(e);
                const int w = kit.frameWidth(door_), h = kit.frameHeight(door_);
                // 0200ea58: where the press began.
                door = door_.x <= story.touch.pressX && story.touch.pressX <= door_.x + w &&
                       door_.y <= story.touch.pressY && story.touch.pressY <= door_.y + h;
            }
            if (!e.button.clicked(kit, story.touch) && !door) {
                continue;
            }
            if (!e.arrow || door || e.type == -1) {
                if (story.sound && e.type != -1) {
                    story.sound->effect(e.type == 1 ? 0x36 : e.type == 2 ? 0x3a : 0x32);
                }
                state.setPuzzle(-1);
                state.room = std::uint8_t(e.target);
                fadeOut(kit, Screens::Both, 2);
                state.scene = scene::Room;
                if (e.type == 1) {
                    state.enterSound = 0x37;
                } else if (e.type == 4) {
                    state.enterSound = 0x3b;
                } else if (e.type != 2 && e.type != -1) {
                    state.enterSound = 0x33;
                }
                return true;
            }
            // An arrow: the next place is built in place.
            state.enterSound = -1;
            if (story.sound) {
                if (e.type == 3) {
                    story.sound->effect(0x32);
                    state.enterSound = 3;
                } else if (e.type == 1) {
                    story.sound->effect(0x36);
                } else if (e.type == 2) {
                    story.sound->effect(0x3a);
                } else {
                    story.sound->startLoopedEffect15(); // footsteps
                }
            }
            fadeOut(kit, Screens::Lower, 2);
            const Exit way = e;
            walkMarker(way);
            if (story.sound && way.type != 1 && way.type != 2) {
                story.sound->stopLoopedEffect15();
            }
            state.byte9 = 0;
            state.room = std::uint8_t(way.target);
            story.runScript("script/rooms/room" + std::to_string(state.room) + "_in.gds",
                            story.commonCommands());
            if (state.scene != scene::Room) {
                fadeOut(kit, Screens::Upper, 2);
                return true;
            }
            // 0201d714 + 0201dbb4: the room's contents again.
            const int map = mapPicture_;
            story.upperActors.clear();
            build();
            pickDogSpot(); // 0201dd70
            if (mapPicture_ != map) {
                fadeOut(kit, Screens::Upper, 2);
            }
            for (int k = 0; k < 5; ++k) {
                story.poll();
                for (auto& ch : characters_) {
                    ch.button.update(kit, story.touch);
                }
                draw(false);
                story.present();
            }
            if (story.sound) {
                if (way.type == 3) {
                    story.sound->effect(0x33);
                } else if (way.type == 1) {
                    story.sound->effect(0x37);
                } else if (way.type == 4) {
                    story.sound->effect(0x3b);
                }
            }
            fadeIn(kit, mapPicture_ == map ? Screens::Lower : Screens::Both,
                   mapPicture_ == map ? 1 : 2);
            return false;
        }
        if (story.touch.released) {
            return false;
        }
    }
}

bool RoomScene::askPuzzle() {
    // 02020b14: "play puzzle N?" (sys_203.txt with the puzzle's number and
    // title, or "NO DATA: n") in the question window with yes / no; "yes"
    // fades the music out (02005354).
    shackShuffle_.drawOnly = true;
    const int puzzle = story.state.puzzleNumber;
    story.poll();
    draw(true);
    shackYes_.cancel();
    shackNo_.cancel();
    const auto& puzzles = story.catalogue.puzzles();
    const auto info = puzzles.find(unsigned(puzzle));
    std::string text;
    if (info == puzzles.end() || info->second.title.empty()) {
        text = "NO DATA: " + std::to_string(puzzle);
    } else {
        text = story.text("stext", "stext", "sys_203.txt");
        if (const auto at = text.find("%d"); at != text.npos) {
            text.replace(at, 2, std::to_string(shackNumber(puzzle)));
        }
        if (const auto at = text.find("%s"); at != text.npos) {
            text.replace(at, 2, info->second.title);
        }
    }
    kit.clearText(SceneKit::Lower);
    drawCentredText(kit, SceneKit::Lower, 0x48, text, true, 0, 0x80);
    auto window = [&] {
        kit.draw(shackWindow_, 0x1f);
        shackYes_.update(kit, story.touch);
        shackNo_.update(kit, story.touch);
    };
    window();
    story.present();
    bool yes = true;
    for (;;) {
        story.poll();
        draw(true);
        window();
        story.present();
        if (shackNo_.clicked(kit, story.touch)) {
            yes = false;
            story.poll();
            break;
        }
        if (shackYes_.clicked(kit, story.touch)) {
            if (story.sound) {
                story.sound->stopMusicSoftly();
            }
            break;
        }
    }
    kit.clearText(SceneKit::Lower);
    shackShuffle_.drawOnly = false;
    return yes;
}

int RoomScene::shackNumber(int puzzle) const {
    // The puzzles' numbers as shown (the program's table at 020daeb0).
    return int(std::int32_t(story.programWord(0x020daeb0 + 4 * std::uint32_t(puzzle))));
}

void RoomScene::drawShackNumber(int x, int y, int puzzle) {
    // 0201efa0: three digits (nazobaba_num.spr, 6 pixels apart) while the
    // pictures show.
    if (alpha_ < 2) {
        return;
    }
    const int number = shackNumber(puzzle);
    const int digits[3] = {number % 1000 / 100, number % 100 / 10, number % 10};
    for (int k = 0; k < 3; ++k) {
        kit.select(shackNumbers_, unsigned(digits[k] + 1));
        shackNumbers_.x = x + 6 * k;
        shackNumbers_.y = y;
        kit.draw(shackNumbers_);
    }
}

void RoomScene::shuffleShack() {
    // 0201ed20: the pictures fade out (4 a frame), the next puzzles come with
    // new poses and a puff of smoke (effect 0x2f), the pictures fade back in
    // (1 a frame); 50 frames in all.
    shackShuffle_.drawOnly = true;
    for (auto& ch : characters_) {
        ch.puffFrames = 0;
    }
    int step = -4;
    for (int k = 0; k < 0x32; ++k) {
        story.poll();
        alpha_ += step;
        if (alpha_ < -2) {
            alpha_ = -2;
            step = 1;
            for (auto& ch : characters_) {
                ch.puzzle = story.state.nextShackPuzzle();
                ch.event = -ch.puzzle;
                kit.select(ch.picture, unsigned(story.randomBelow(5) + 1));
            }
            if (story.sound) {
                story.sound->effect(0x2f);
            }
        }
        alpha_ = std::min(alpha_, 0x1f);
        draw(true);
        for (auto& ch : characters_) {
            if (ch.hasPuff && ch.puffFrames < 100) {
                kit.draw(ch.puff);
                ++ch.puffFrames;
            }
        }
        story.present();
    }
    shackShuffle_.drawOnly = false;
    alpha_ = 0x1f;
}

void RoomScene::run() {
    // 02020280.
    story.marker("room-run");
    GameState& state = story.state;
    iconShown_ = false;
    pickDogSpot();
    for (int i = 0; i < 2; ++i) {
        story.frame([&] {
            draw(false);
            if (showFirstTouch_) {
                kit.draw(firstTouch_, 0x1f);
            }
        });
    }
    if (state.enterSound != -1 && story.sound) {
        story.sound->effect(state.enterSound);
    }
    fadeIn(kit, Screens::Both, 2); // 0200f7b4(2, 1)
    if (showFirstTouch_) {
        do {
            story.frame([&] {
                draw(true);
                kit.draw(firstTouch_, 0x1f);
            });
        } while (!story.touch.released);
    }
    state.firstTouchHint = false;
    story.saveCheckpoint(); // the room is ready: the app's checkpoint
    for (;;) {
        // While the dog has barked, frames pass without touches (02065d2c).
        do {
            story.frame([&] {
                draw(true);
                if (iconShown_) {
                    kit.draw(touchIcon_, 0x1f);
                }
                updateDog();
                reportTargets(false);
                if (!dog_.busy) {
                    kit.touchAreas = touchAreas();
                }
            });
        } while (dog_.busy);
        bool idle = true;
        // The shack's shuffle (shown with more than eight puzzles waiting).
        if (shack_ && state.shackWaiting() > 8 && shackShuffle_.clicked(kit, story.touch)) {
            iconShown_ = false;
            idle = false;
            shuffleShack();
        }
        // The trunk.
        if (state.room != 'w' && options_.clicked(kit, story.touch)) {
            iconShown_ = false;
            state.scene = scene::TrunkMenu;
            fadeOut(kit, Screens::Both);
            return;
        }
        // Hint coins.
        for (std::size_t i = 0; i < coinZones_.size(); ++i) {
            if (coinZones_[i].touched && story.touch.released) {
                coinZones_[i].touched = false;
                iconShown_ = false;
                idle = false;
                takeCoin(int(i));
                if (eventAfterCoin_ != -1) {
                    state.scene = scene::Event;
                    state.event = std::uint32_t(
                        eventAfterCoin_ < 0 || eventAfterCoin_ > 0x1df ? 0 : eventAfterCoin_);
                    fadeOut(kit, Screens::Both);
                    return;
                }
            }
        }
        // Characters and things: their event.
        for (auto& ch : characters_) {
            if (ch.button.clicked(kit, story.touch)) {
                iconShown_ = false;
                idle = false;
                if (story.sound) {
                    story.sound->effect(0xb);
                }
                if (state.room != 'w') {
                    for (auto& z : markZones_) {
                        if (z.event == ch.event) {
                            z.state = 1;
                            z.time = 0;
                            while (z.state == 1) {
                                story.frame([&] { draw(false); });
                            }
                            z.state = 0;
                        }
                    }
                    if (story.sound) {
                        story.sound->effect(0xd);
                    }
                }
                if (ch.event >= 0) {
                    state.scene = scene::Event;
                    state.afterPuzzle = 0;
                    state.event = std::uint32_t(ch.event > 0x1df ? 0 : ch.event);
                    fadeOut(kit, Screens::Both);
                    return;
                }
                state.setPuzzle(ch.puzzle);
                if (askPuzzle()) {
                    state.scene = scene::Event;
                    state.event = 300;
                    state.afterPuzzle = 0;
                    state.returnScene = scene::Event;
                    fadeOut(kit, Screens::Both);
                    return;
                }
            }
        }
        // Plain ways out.
        for (auto& e : exits_) {
            if (!e.arrow && e.button.clicked(kit, story.touch)) {
                iconShown_ = false;
                leaveBy(e);
                return;
            }
        }
        // Remarks.
        for (std::size_t i = 0; i < examines_.size(); ++i) {
            if (examines_[i].button.clicked(kit, story.touch)) {
                iconShown_ = false;
                idle = false;
                examine(int(i));
            }
        }
        // Move mode.
        if (!exits_.empty() && state.room != 'w' && moveMode_.clicked(kit, story.touch)) {
            iconShown_ = false;
            idle = false;
            if (moveMode()) {
                state.byte12 = 1;
                return;
            }
        }
        // The magnifier follows a press (touch_icon.spr at the press - 7).
        if (story.touch.pressed && !iconShown_) {
            iconShown_ = true;
            touchIcon_.x = story.touch.x - 7;
            touchIcon_.y = story.touch.y - 7;
        }
        if (story.touch.released && idle) {
            iconShown_ = false;
        }
    }
}

} // namespace picarat::adventure1
