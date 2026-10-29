// Floor buttons and doors in the session (switches.hpp has the rules in short).
#include "game/session.hpp"

namespace doc::game {
namespace {

constexpr int kSoundPress = 0x25, kSoundRelease = 0x73, kSoundHurry = 0x74;
constexpr int kSoundTickFast = 0x75, kSoundTickSlow = 0x76;
constexpr int kSoundDoorOpen = 0x58, kSoundDoorClose = 0x59, kSoundTrapdoor = 0x26;
constexpr float kQuarter = 1.57079637f, kHalf = 3.14159274f;

}  // namespace

const char* tagColour(int tag) {
    if (tag < 100) return "blue";
    if (tag < 200) return "green";
    if (tag < 300) return "red";
    return "yellow";
}

int Switch::baseFrame() const {
    if (type == 0x96 || type == 0x9b) return 0;  // (their own pictures: up 0, down 1)
    if (type >= 0x97 && type <= 0x9a) return 6;
    if (type == 65) return 0;
    if (type == 66) return 2;
    return 4;
}

Switch* Session::switchFor(int objectId) {
    for (Switch& s : switches_)
        if (s.objectId == objectId) return &s;
    return nullptr;
}

Door* Session::doorFor(int objectId) {
    for (Door& d : doors_)
        if (d.objectId == objectId) return &d;
    return nullptr;
}

// ObjControllerSwitch::init/loadAssets and ObjControllerDoors::init/loadAssets, then the
// doors' postInit in object order: each door switches its tag's walls (on while closed); an
// open door tells its tag's buttons quietly (message 1, extra 1).
void Session::setUpSwitches() {
    for (LevelObject& o : objects_) {
        if (isSwitch(o.type)) {
            Switch s;
            s.objectId = o.id;
            s.type = o.type;
            s.tag = o.tag;
            if (o.type == 80) s.durationMs = 20000;
            if (o.type == 81) s.durationMs = 30000;
            s.anim = o.anim;
            s.anim.setAnim(s.baseFrame(), -1, SpriteAnim::Hold, 200);
            const SheetSize timer = level_.switchTimerSheet();
            s.timer.setSheet(timer.width, timer.height, 18.0f);
            s.timer.setAnim(0, 0, SpriteAnim::Hold, 200);
            // (A snap target by Object::init's default; messages keep it for raised buttons.
            // Buttons only cats press, 151..154, start as no snap target and not passable.)
            s.snappable = !isCatSwitch(o.type);
            o.state = ObjectState(s.state);
            o.snappable = s.snappable;
            if (isCatSwitch(o.type)) o.passable = false;
            o.anim = s.anim;
            switches_.push_back(s);
        } else if (isDoor(o.type)) {
            Door d;
            d.objectId = o.id;
            d.type = o.type;
            d.tag = o.tag;
            d.state = o.type >= 218 ? kDoorOpen : kDoorClosed;
            // Turned by the type's last two bits from 211: a quarter turn, a half, three quarters.
            switch ((o.type - 210) % 4) {
            case 1: d.rotation = kQuarter; break;
            case 2: d.rotation = kHalf; break;
            case 3: d.rotation = 4.71238899f; break;
            default: d.rotation = 0.0f; break;
            }
            d.home = o.position;
            d.anim = o.anim;
            d.anim.setAnim(d.state == kDoorOpen ? 2 : 0, -1, SpriteAnim::Hold, 200);
            o.state = ObjectState(d.state);
            o.updating = true;
            o.anim = d.anim;
            doors_.push_back(d);
        }
    }
    for (const LevelObject& o : objects_) {
        const Door* d = doorFor(o.id);
        if (!d) continue;
        const int floor = level_.infinite() ? 0 : o.floor;
        floorWalls_[std::size_t(floor)].setTagEnabled(o.tag, d->state == kDoorClosed, -1);
        if (d->state != kDoorClosed) {
            GameMessage m;
            m.type = 1;
            m.tag = o.tag;
            m.extra = 1;
            sendMessage(m, false);
        }
    }
}

// Object::playMovementSound for a button's ticking.
void Session::switchTickSound(Switch& s, int effect) {
    if (s.movementSound == effect) return;
    if (s.movementSound != -1) sound(s.movementSound, SoundEvent::Stop);
    if (effect != -1) sound(effect, SoundEvent::Play, true);
    s.movementSound = effect;
}

// ObjControllerSwitch::triggerPressed @0x1aec54: `presser` is the type of what steps on it (1
// the mouse). Buttons 151..154 take only cats' presses, the others none of cats (types
// 0x32..0x3b, 0x11d); a refused press changes nothing. The message by kind: close buttons (66,
// 152) send 2, 154 sends 3, 150 and 155 send 0, the others 1.
bool Session::pressSwitch(LevelObject& o, Switch& s, int presser) {
    const bool byCat = (presser >= 0x32 && presser <= 0x3b) || presser == 0x11d;
    if (isCatSwitch(s.type) ? presser == 1 : byCat) return false;
    const int entry = s.state;
    if (isTimedSwitch(s.type)) {
        if (entry == kSwitchUp) {
            s.anim.setAnim(s.baseFrame() + 1, -1, SpriteAnim::Hold, 200);
            s.timer.setAnim(31 - s.durationMs / 1000, 31, SpriteAnim::Once, 1000);
            if (s.timerMs <= s.durationMs - 100) {
                if (presser == 1) ++uses_.switches;
                s.updating = true;
                s.snappable = false;
                s.state = kSwitchDown;
                GameMessage m;
                m.type = 1;
                m.tag = s.tag;
                m.sender = o.id;
                sendMessage(m, true);
                sound(kSoundPress);
            }
            s.timerMs = s.durationMs;
            s.warned = true;
        }
    } else {
        s.anim.setAnim(s.baseFrame() + 1, -1, SpriteAnim::Hold, 200);
        if (s.timerMs < gameTimeMs_ && s.state == kSwitchUp) {
            s.state = kSwitchDown;
            if (presser == 1) ++uses_.switches;  // (the "switch" metric, @0x1aef98)
            GameMessage m;
            m.type = (s.type == 66 || s.type == 0x98) ? 2 : s.type == 0x9a ? 3 : (s.type == 0x96 || s.type == 0x9b) ? 0 : 1;
            m.tag = s.tag;
            m.sender = o.id;
            sendMessage(m, true);
            sound(kSoundPress);
        }
    }
    s.snappable = s.state == kSwitchUp;
    o.state = ObjectState(s.state);
    o.snappable = s.snappable;
    o.updating = s.updating;
    o.anim = s.anim;
    return true;
}

// SceneGame::collideObjects @0x10085c (after the mouse's part, whatever the mouse does): each
// raised button of the shown floor, in order, against the cats (the scene's cat list) of that
// floor; the first touching cat whose press is taken ends that button's turn.
void Session::pressSwitchesByCats() {
    for (Switch& s : switches_) {
        LevelObject* o = objectById(s.objectId);
        if (!o || !o->active || s.state != kSwitchUp || o->floor != shownFloor_) continue;
        for (const Cat& cat : cats_) {
            const LevelObject* c = objectById(cat.objectId());
            if (!c || !c->active || c->floor != shownFloor_) continue;
            const float reach = o->radius + c->radius;
            if ((o->position - c->position).lengthSquared() < reach * reach && pressSwitch(*o, s, c->type)) break;
        }
    }
}

// ObjControllerSwitch::update: a timed button counting down.
void Session::updateSwitch(LevelObject& o, Switch& s, int dtMs) {
    if (s.type == 0x9b && s.updating) {
        // ObjControllerSwitch::update @0x1ae7e0: a fight button pops up again on the first
        // tick nothing touches it (it flips its reflectors once a press).
        if (s.touched == 1 && s.state == kSwitchDown) {
            s.state = kSwitchUp;
            s.anim.setAnim(s.baseFrame(), -1, SpriteAnim::Hold, 200);
            s.timerMs = 0;
            s.snappable = true;
        }
        s.touched = 1;
        o.state = ObjectState(s.state);
        o.snappable = s.snappable;
        o.anim = s.anim;
        return;
    }
    if (!s.updating || !isTimedSwitch(s.type)) return;
    s.timerMs -= dtMs;
    s.timer.update(dtMs);
    int tick = kSoundTickSlow;
    if (s.timerMs < 5000) {
        GameMessage m;
        m.type = 5;
        m.tag = s.tag;
        m.sender = o.id;
        m.extra = s.timerMs;
        sendMessage(m, true);
        if (s.warned) {
            sound(kSoundHurry);
            s.warned = false;
        }
        tick = kSoundTickFast;
    }
    switchTickSound(s, tick);
    if (s.timerMs < 1) {
        s.anim.setAnim(s.type == 0x99 ? 6 : 4, -1, SpriteAnim::Hold, 200);
        s.timer.setAnim(0, -1, SpriteAnim::Hold, 200);
        s.updating = false;
        s.snappable = true;
        s.state = kSwitchUp;
        sound(kSoundRelease);
        GameMessage m;
        m.type = 2;
        m.tag = s.tag;
        m.sender = o.id;
        sendMessage(m, true);
        switchTickSound(s, -1);
    }
    o.state = ObjectState(s.state);
    o.snappable = s.snappable;
    o.updating = s.updating;
    o.anim = s.anim;
}

// ObjControllerDoors::update: the picture runs, then is set straight to its end (so a door
// opens or closes in one frame), and the drawing pass follows its state.
void Session::updateDoor(LevelObject& o, Door& d, int dtMs) {
    d.anim.update(dtMs);
    if (d.state == kDoorClosed) {
        if (d.anim.frame() != 0) d.anim.setAnim(0, -1, SpriteAnim::Hold, 200);
    } else if (d.state == kDoorOpen && d.anim.frame() != 2) {
        d.anim.setAnim(2, -1, SpriteAnim::Hold, 200);
    }
    d.pass = d.state == kDoorOpen ? 1 : 2;
    if (d.type == 215 || d.type == 217) d.pass = 1;
    o.anim = d.anim;
}

void Session::switchReceiveMessage(LevelObject& o, Switch& s, const GameMessage& message) {
    const int type = message.type;
    if (s.type == 66 || s.type == 0x98) {
        // A close button mirrors the open ones.
        if (type == 1) {
            s.state = kSwitchUp;
            s.anim.setAnim(s.baseFrame(), -1, SpriteAnim::Hold, 200);
            if (message.extra != 1) sound(kSoundRelease);
        } else if (type == 2) {
            s.state = kSwitchDown;
            s.anim.setAnim(s.baseFrame() + 1, -1, SpriteAnim::Hold, 200);
        }
    } else {
        if (type == 1) {
            s.state = kSwitchDown;
            s.anim.setAnim(s.baseFrame() + 1, -1, SpriteAnim::Hold, 200);
        } else if (type == 2) {
            s.state = kSwitchUp;
            s.anim.setAnim(s.baseFrame(), -1, SpriteAnim::Hold, 200);
            s.timerMs = 0;
            sound(kSoundRelease);
        }
    }
    s.snappable = s.state == kSwitchUp;
    o.state = ObjectState(s.state);
    o.snappable = s.snappable;
    o.anim = s.anim;
}

void Session::doorReceiveMessage(LevelObject& o, Door& d, const GameMessage& message) {
    const int type = message.type;
    if (message.extra != 1) {
        if (type == 1) sound(kSoundDoorOpen);
        else if (type == 2) sound(kSoundDoorClose);
    }
    int target = -1;
    if (type == 1) {
        d.pass = 1;
        d.home = o.position;
        target = kDoorOpen;
    } else if (type == 2) {
        d.pass = 2;
        d.home = o.position;
        target = kDoorClosed;
    } else if (type == 5) {
        // The last seconds of a timed button: the bars rattle about their place (the scene's
        // two noise draws of this tick), still at the end.
        if (message.extra < 1) {
            o.position = d.home;
        } else {
            o.position.x = d.home.x + randomWithinFrom(-1.0f, 1.0f, noise_[0]);
            o.position.y = d.home.y + randomWithinFrom(-1.0f, 1.0f, noise_[1]);
        }
        return;
    } else {
        d.home = o.position;
        return;
    }
    if (d.state != target) {
        if (target == kDoorClosed) d.anim.setAnim(2, 0, SpriteAnim::Once, 200);
        else d.anim.setAnim(0, 2, SpriteAnim::Once, 200);
        d.state = target;
    }
    o.state = ObjectState(d.state);
    o.anim = d.anim;
}

// ObjControllerFloorTransport::receiveMessage @0x1b2378: message 1 opens a shut trapdoor
// (a snap target again, its frames run open, sound 0x26, the arrow over it for 5 s), message 2
// shuts an open one. Going down an open trapdoor (startFloorTransition) is not reconstructed.
void Session::trapdoorReceiveMessage(LevelObject& o, const GameMessage& message) {
    if (message.type == 2 && o.state == ObjectState::HoleOpen) {
        o.snappable = false;
        o.state = ObjectState::HoleClosed;
        o.anim.setAnim(o.anim.frameCount() - 1, 0, SpriteAnim::Once, 150);
    } else if (message.type == 1 && o.state == ObjectState::HoleClosed) {
        o.snappable = true;
        o.state = ObjectState::HoleOpen;
        o.anim.setAnim(1, o.anim.frameCount() - 1, SpriteAnim::Once, 150);
        sound(kSoundTrapdoor);
        o.timerMs = 5000;
    }
}

}  // namespace doc::game
