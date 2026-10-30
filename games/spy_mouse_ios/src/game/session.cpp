#include "game/session.hpp"

#include <algorithm>

#include <cctype>

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace doc::game {
namespace {

// PinholeManager::setPinholeType @0x1f0f6c: how far the hole of each kind reaches, in screen
// widths (+0x34): the plain hole and the bonus pattern 2/3, the star 1.458, the cat 1.042, the
// mouse 2.292, the tonsils 2.083.
float irisScale(int kind) {
    switch (kind) {
    case 1: return 1.45833337f;
    case 2: return 1.04166675f;
    case 3: return 2.29166675f;
    case 4: return 2.08333349f;
    default: return 0.666666687f;
    }
}


constexpr int kMaxCheeseTypes = 5;

// isFloatEqualToZero @0x19c804: true when the exponent is tiny (|x| below about 2^-95).
bool floatIsZero(float x) {
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    return (bits & 0x7C000000u) == 0;
}

bool isBankHole(int type) { return type >= 0x168 && type <= 0x16B; }

}  // namespace

Session::Session(Level level, std::uint32_t seed, Tuning tuning, int strength, bool wonBefore)
    : Session(std::move(level), seed, tuning, strength, nullptr, wonBefore) {}

std::unique_ptr<Session> Session::retry() const {
    // (The carry popup raised the profile's strength for good: a reload does not show it again.)
    return std::unique_ptr<Session>(new Session(level_, 0, tuning_, carryCapacity_, this, true));
}

Session::Session(Level level, std::uint32_t seed, Tuning tuning, int strength, const Session* previous,
                 bool wonBefore)
    : level_(std::move(level)),
      tuning_(tuning),
      floorWalls_([this] {
          std::vector<LevelGeometry> walls;
          for (int f = 0; f < level_.floorCount(); ++f) walls.push_back(*level_.floor(f).geometry);
          return walls;
      }()),
      objects_(level_.objects()),
      particles_(random_, level_.particleKinds()) {
    startStrength_ = strength;
    wonBefore_ = wonBefore;
    attempts_ = previous ? previous->attempts_ + 1 : 0;
    floorVisits_.assign(std::size_t(level_.floorCount()), 0);
    if (previous) {
        // restartGame: no new boot, so no music draw; the generator goes on.
        random_ = previous->random_;
        musicTrack_ = previous->musicTrack_;
        record_ = previous->record_;
        setPowerups(previous->powerups_, previous->powerupTutorialDone_, previous->metDigger_, previous->lateLevel_);
    } else {
        random_.seed(seed);
        // GameBoot::bootLevel @0x1f4fb8: one of three music tracks, then the game starts.
        musicTrack_ = random_.next() % 3;
    }
    if (auto start = level_.mouseStart()) mouse_.place(*start, {37.0f, 37.0f});
    // Cats walk the routes their tag names (ObjControllerCat::init; the route from postInit).
    for (const LevelObject& o : objects_)
        if (isPortedCat(o.type)) {
            cats_.emplace_back(o.id, o.type, o.floor, o.position, o.radius, CatRoute::link(level_.floor(o.floor).map, o.tag),
                               level_.catSheet(false, o.type), level_.catSheet(true, o.type));
            cats_.back().setCommonSheet(level_.catCommonSheet(o.type));
            cats_.back().setIdleSequences(level_.catIdleSequences(o.type));
            cats_.back().initForWorld(level_.info().world);  // (the level's world for the profile's)
            objects_[std::size_t(o.id)].radius = cats_.back().radius();
        }
    for (LevelObject& o : objects_) {
        if (Cat* cat = catFor(o.id)) o.state = ObjectState(cat->state());
        if (o.type == kDroid) {
            droids_.emplace_back(o.id, o.floor, CatRoute::link(level_.floor(o.floor).map, o.tag), o.radius, o.size,
                                 Droid::Sheets{level_.droidBodySheet(), level_.droidBaseSheet(), level_.droidWreckSheet()});
            o.position = droids_.back().position();
            o.anim = droids_.back().body;
        }
        if (o.type == kTv) {
            // Object::postInit links its route (the seats); loadAssets @0x1178c6 turns the
            // picture towards the first seat in steps of 45 degrees (run l22_watch: 270).
            for (const CatRoute::Point& p : CatRoute::link(level_.floor(o.floor).map, o.tag).points)
                o.seats.push_back(p.position);
            if (!o.seats.empty()) {
                Vec2 d = o.seats.front() - o.position;
                d.normalise(1.0f);
                float a = float(std::atan2(double(d.y), double(d.x))) * 57.2957764f + 22.5f;
                a = std::fmod(a, 360.0f);
                if (a < 0.0f) a = a + 360.0f;
                o.pictureDegrees = float(int(a) / 45 * 45);
            }
        }
    }
    {
        const SheetSize shuriken = level_.bossSheet("sprites/shuriken.png");
        projectiles_.setPictureSheet(kPictureShuriken, shuriken.width, shuriken.height);
    }
    setUpSpitterRoom();
    setUpVacuumRoom();
    setUpTurretRoom();
    setUpSwitches();
    {
        const SheetSize eyes = level_.catEyesSheet();
        catEyes_.setSheet(eyes.width, eyes.height, 22.0f);
        catEyes_.setAnim(0, catEyes_.frameCount() - 1, SpriteAnim::Loop, 200);
        if (previous) catEyes_ = previous->catEyes_;  // (a static of the game, kept across reloads)
    }
    // SceneGame::reloadFromSnapshot keeps the cat objects: their init runs again, a checkpoint
    // rolls them back (restoreCheckpoint, which also decides on resetCats), without one they
    // are reset.
    if (previous && (previous->retryFromPause_ || previous->retryAfterCaught_) && previous->cats_.size() == cats_.size()) {
        cats_ = previous->cats_;
        if (previous->checkpoint_) {
            for (Cat& cat : cats_) cat.reload();
        } else {
            for (Cat& cat : cats_) cat.restart();
        }
    }
    mouse_.setUpSprite(level_.mouseSequences(), usesBucket(level_.info().world));
    // A reload keeps the player object: where its drawn copy last slid stays (run l18_holes).
    if (previous) {
        mouse_.slidePosition = previous->mouse_.slidePosition;
        mouse_.slideTarget = previous->mouse_.slideTarget;
    }
    // Object set-up in object order (ObjControllerPickups::init @0x1b68dc: each cheese starts
    // its idle shine at a random point).
    objects_.reserve(objects_.size() + 16);  // (objects made during play keep references valid)
    for (LevelObject& o : objects_) {
        if (isCheese(o.type)) o.idleSparkleMs = int(random_.within(0.0f, 5000.0f));
        // ObjControllerPickups::loadAssets: the clockwork mouse's 4-frame walk, 200 ms a frame.
        if (o.type == kClockworkMouse) o.anim.setAnim(0, o.anim.frameCount() - 1, SpriteAnim::Loop, 200);
        // ObjControllerHideable::loadAssets: the couch's picture loops (paced below).
        if (o.type == kCouch) o.anim.setAnim(0, o.anim.frameCount() - 1, SpriteAnim::Loop, 200);
        // ObjControllerPickups::loadAssets @0x1b7156: the bonus pickups hold their frame.
        if (isBonusPickup(o.type) && o.type != kBonusHeart) o.anim.setAnim(bonusFrame(o.type), -1, SpriteAnim::Hold, 200);
        // ObjControllerPickups::loadAssets @0x1b72d2: the balloon floats on frames 0-3, 400 ms.
        if (o.type == kBalloon) o.anim.setAnim(0, 3, SpriteAnim::Loop, 400);
        // @0x1b747c: catnip grows once over "cat_catnip_time" (8.2 s), its frames shared out.
        if (o.type == kCatnip && o.anim.frameCount() > 0)
            o.anim.setAnim(0, o.anim.frameCount() - 1, SpriteAnim::Once, 8200 / o.anim.frameCount());
        // Object::loadAssets: the exit door shows its last frame (open).
        if (o.type == kExitDoor || o.type == kEntranceDoor || o.type == kBossExitDoor)
            o.anim.setAnim(o.anim.frameCount() - 1, o.anim.frameCount() - 1, SpriteAnim::Hold, 200);
        if (isRoamer(o.type))
            roamers_.emplace_back(o.id, o.type, o.position, o.radius, CatRoute::link(level_.floor(o.floor).map, o.tag),
                                  random_);
    }
    // Map decorations: animated ones start at once, the others wait for a trigger.
    for (const FluffItem& f : level_.fluff()) {
        FluffState state;
        state.anim.setSheet(f.sheet.width, f.sheet.height, f.kind.frameWidth);
        if (f.kind.autoplay) state.anim.setAnim(0, state.anim.frameCount() - 1, f.kind.mode, f.kind.frameMs);
        fluff_.push_back(state);
    }
    // A boss level opens with its "intro" cutscene, a later attempt with "restart"
    // (SceneGame::initGame @0x105ee4, by LevelState +0x30).
    if (level_.info().mode == "boss") fireEvent(attempts_ >= 1 ? "restart" : "intro");
    // SceneGame::reloadFromSnapshot @0x106a82: after the restart the snapshot is rolled back.
    if (previous && previous->checkpoint_) {
        checkpoint_ = previous->checkpoint_;
        if (previous->retryFromPause_ || previous->retryAfterCaught_) restoreCheckpoint(*checkpoint_);
    }
    // The level's ambient sound (SceneGame::playAmbientScheme): first delays drawn now. The
    // level boots inside App::update (from the menu's pinhole callback), before the ambience
    // player's update of the same frame, which therefore already counts once.
    ambience_.start(level_.ambience(), random_);
    hud_.init(float(level_.hudBonusWidth()));
    // Carrying capacity: the profile's strength, raised to what the level needs
    // (SceneGame::initGame @0x10639e). Raising it announces the new strength in a popup; so
    // does a first visit of 1-3 or 2-4 (+0x57d8c). (1.1.1) SceneGame::update opens it on its
    // first run (1.0.5's GameBoot::bootLevel pushed it at once).
    carryCapacity_ = strength < 1 ? 1 : strength;
    if (level_.info().trailLength > carryCapacity_) {
        carryCapacity_ = level_.info().trailLength;
        if (const LevelNotice* n = level_.carryNotice(carryCapacity_)) pendingNotice_ = *n;
    } else if (!previous && !wonBefore && level_.firstVisitNotice()) {
        pendingNotice_ = *level_.firstVisitNotice();
    }
    // The music: one of the level's three tracks (the <music play> list, each slot repeating
    // the list when it is shorter; Profile::loadLevelList @0x174a3a), CM_Menu1 if empty.
    {
        std::vector<std::string> tracks;
        std::string list = level_.info().music;
        for (int i = 0; i < 3; ++i) {
            const std::size_t comma = list.find(',');
            std::string track = list.substr(0, comma);
            list = comma == std::string::npos ? list : list.substr(comma + 1);
            while (!track.empty() && track.front() == ' ') track.erase(track.begin());
            tracks.push_back(track);
        }
        music_ = tracks[std::size_t(musicTrack_)];
        if (music_.empty()) music_ = "sound/music/CM_Menu1.m4a";
        // SceneGame::startGame @0x1068c2: not in a boss fight (game mode 2), where the music
        // playing goes on (the map's, or the level's after being caught) and updateGameMode
        // starts the level's only once none plays.
        if (level_.info().mode == "boss") {
            if (previous) musicPlaying_ = previous->musicPlaying_;
        } else {
            startMusic();
        }
    }
    for (const std::string& loop : ambience_.loops()) {
        SoundEvent e;
        e.file = loop;
        e.loop = true;
        soundEvents_.push_back(e);
    }
    // The boot frame's App::update runs the ambience after the scenes. A retry from the results
    // happens while handling a touch and the level's first tick follows in the same frame; one
    // from the pause menu happens in the pause scene's update, like a boot.
    const bool afterTouch = previous && !previous->retryFromPause_ && !previous->retryAfterCaught_;
    if (!afterTouch) ambienceSounds(ambience_.update(kTickMs, random_));
    // The iris opens on the mouse (SceneGame::openPinholeOnPlayer @0xf8f60 with the default
    // speed). PinholeManager::startOpenHole @0x1f150c: the radius that uncovers the whole screen
    // is 2/3 of its width (setPinholeType @0x1f0f6c, kind 0) plus the centre's larger distance
    // from the screen middle; kind 0 runs at 1.5 times "iris_transition_speed" (0.0008).
    iris_.opening = true;
    iris_.centre = mouse_.position;
    iris_.fullRadius = kScreenWidth * 0.666666687f +
                       std::max(std::fabs(iris_.centre.x - kScreenWidth * 0.5f),
                                std::fabs(iris_.centre.y - kScreenHeight * 0.5f));
    iris_.speed = 0.000800000038f * 1.5f;
    if (afterTouch) {
        // restartGame keeps the results' pinhole kind (the bonus pattern) at its plain speed.
        iris_.kind = 5;
        iris_.speed = 0.000800000038f;
    } else {
        if (previous && previous->retryAfterCaught_) {
            // After being caught the pinhole keeps the cat's silhouette, at its plain speed
            // and with its larger radius.
            iris_.kind = 2;
            iris_.speed = 0.000800000038f;
            iris_.fullRadius = kScreenWidth * 1.04166675f +
                               std::max(std::fabs(iris_.centre.x - kScreenWidth * 0.5f),
                                        std::fabs(iris_.centre.y - kScreenHeight * 0.5f));
        }
        // The boot frame's own App::update goes on after the scenes like any other: the iris
        // opens one step before the level's first tick (the shot of the boot frame + 1 shows it so).
        updateIris(kTickMs);
    }
    // The intro overlay with a spotlight on every marker object (SceneLevelIntro::showScene
    // @0x14a7e8, initSpotlights @0x149bfc).
    // (Engine::pushScene from SceneGame::startGame @0x1066d8; only in the normal game modes, 0
    // and 3: the boss chase and the boss fight have none.)
    // A boss fight's reload pushes it too (SceneGame::reloadFromSnapshot @0x106d48).
    intro_.active = (level_.info().mode != "infinite" && level_.info().mode != "boss") ||
                    (level_.info().mode == "boss" && previous && previous->checkpoint_);
    // SceneGame::postInitGame @0x105f98 pushes the finger for map 10 before the intro goes on
    // top of it; it waits until the intro has gone (the 59 is its ripple's frame width).
    if (level_.info().map == 10) hand_ = HandTutorial(level_.handRoute(), level_.rippleSheet(), 59.0f);
    if (previous && previous->retryFromPause_) {
        // SceneGame::reloadFromSnapshot does not push the finger again: the one on the scene
        // stack (if a path had not sent it away yet) goes on from where it stopped.
        hand_ = previous->hand_;
    }
    // The engine's clock (app frames, the bonus pattern's drift) runs on across a retry.
    if (previous) frames_ = previous->frames_;
    // (The profile is the same across a retry.)
    if (previous) {
        kiskaOffers_ = previous->kiskaOffers_;
        wallClock_ = previous->wallClock_;
    }
    {
        // The mouse's spotlight is 50 across from the start; a marker's grows to twice its
        // entity's d value (SceneLevelIntro::initSpotlights @0x149bfc).
        Spotlight mouseLight;
        mouseLight.centre = mouse_.position;
        mouseLight.size = mouseLight.target = 50.0f;
        mouseLight.fixed = true;
        intro_.spotlights.push_back(mouseLight);
        for (const LevelObject& o : objects_)
            if (o.type == 1112) {
                Spotlight light;
                light.centre = o.position;
                light.target = float(o.tag) * 2.0f;
                intro_.spotlights.push_back(light);
            }
    }
    if (!(previous && previous->checkpoint_ && (previous->retryFromPause_ || previous->retryAfterCaught_)))
        progress_.clock = level_.info().startTime;
    for (const LevelObject& o : objects_) {
        if (isCheese(o.type) || o.type == kMouseTrap || (isBonusPickup(o.type) && level_.infinite())) ++progress_.cheeseTotal;
        // SceneGame +0x13868: each floor's cheese and mouse traps (SceneGame::load).
        if ((isCheese(o.type) || o.type == kMouseTrap) && o.floor < int(floorCheese_.size())) ++floorCheese_[std::size_t(o.floor)];
    }
    requestExitOpen();
    // (1.1.1) The menu's iris has shut when the level boots (GameBoot::bootLevel inside
    // PinholeManager::fireEvent(0)); the event then reaches the new game's callback too
    // (SceneGame::pinholeCallback @0xfc358), which acts only with the game on top: in the boss
    // chase, which pushes no intro, the cheese and mouse traps pulse (run lb1_explore).
    if (!previous && !intro_.active && !cutscene_.active) irisShutOnBoot();
    if (previous) progress_.failures = previous->progress_.failures;
    if (previous && previous->retryAfterCaught_) {
        // SceneLevelOver::update @0x130394: "Try Again!" 35 above the mouse (below it near the
        // top of the screen), or above the hole it starts in, after 0.3 s; it fades in to 0.4,
        // holds and shrinks away (kTextLevelOver).
        Vec2 at = mouse_.position;
        if (mouse_.hole != -1)
            if (const LevelObject* hole = objectById(mouse_.hole)) at = hole->position;
        Vec2 offset{0.0f, 35.0f};
        if (at.y > 240.0f) offset.y = -offset.y;
        const data::StringTable* text = level_.strings();
        // ("Keep Going!", string 0x139, after a checkpoint.)
        const int message = checkpoint_ ? 0x139 : 0x13a;
        particles_.add(kTextLevelOver, shownFloor_, at + offset, text ? text->text(message) : std::string(), nullptr,
                       300, at + offset, false);
    }
}

LevelObject* Session::objectById(int id) {
    return id >= 0 && id < int(objects_.size()) ? &objects_[id] : nullptr;
}

int Session::carriedCheese() const {
    int n = 0;
    for (int id : carried_)
        if (isCheese(objects_[id].type)) ++n;
    return n;
}

int Session::carriedPickups() const {
    int n = 0;
    for (int id : carried_) {
        const int type = objects_[id].type;
        if (isCheese(type) || type == 0xB || type == kTwine || type == kCatnip) ++n;
    }
    return n;
}

// --- input and the drawn path -------------------------------------------------------------

// Touch events update the finger slot like SceneGame::mouseEvent: a down marks the finger as
// active, held and new; a move keeps it held; an up leaves it active for one more tick.
void Session::fingerDown(Vec2 screen) {
    // Touches belong to the scene on top: the popup (its button: pressNoticeButton), the
    // results screen or a cutscene (a speech bubble takes a tap when it is released).
    // (The level intro of a boss fight's retry sits above its restart cutscene.)
    if (notice_.active() || kiska_.active() || results_.active() || dossier_.active() ||
        (cutscene_.active && !intro_.active))
        return;
    if (intro_.active) intro_.skipped = true;  // SceneLevelIntro::mouseEvent: hurries the fade
    // (1.1.1) The level's GUI buttons first (the power-up button and bar); while the bar is
    // out the level takes no touches.
    else if (powerupTouchDown(screen)) return;
    finger_.active = finger_.held = finger_.began = true;
    finger_.screen = screen;
    finger_.downScreen = screen;
}

void Session::fingerMove(Vec2 screen) {
    if (notice_.active() || kiska_.active() || results_.active() || dossier_.active() ||
        (cutscene_.active && !intro_.active))
        return;
    if (!intro_.active && (bar_.showing() || bar_.tutorial() != 0)) return;
    finger_.active = finger_.held = true;
    finger_.screen = screen;
}

void Session::fingerUp(Vec2 screen) {
    if (cutscene_.active && !notice_.active() && !intro_.active) {
        // SceneSpeech's screen-wide button (enabled while a line shows) answers the release
        // with its sound (ui_button2) and asks for the next line.
        if (cutscene_.child == CutsceneRun::Child::Speech && cutscene_.box.isShowing() && !cutscene_.tapped) {
            sound(sfx::UiButton2);
            cutscene_.tapped = true;
        }
        return;
    }
    if (notice_.active() || kiska_.active() || results_.active() || dossier_.active()) return;
    if (!intro_.active && powerupTouchUp(screen)) return;
    finger_.active = true;
    finger_.held = false;
    finger_.screen = screen;
}

// The finger lifted (SceneGame::updateTouchPoints @0x1025ec): the path's last points are
// smoothed with the closing kernel (no resampling: 1.1.1) and pushed off the edges.
void Session::finishDrawing() {
    path_.smooth(true, true, &here(), viewLeft_);
    path_.drawing = false;
    fingerDrawing_ = false;
    moveAllPathPointsAwayFromEdges();
}

// SceneGame::moveAllPathPointsAwayFromEdges @0x100f44: every point keeps the mouse's reach and a
// little more (1.01; 1.0.5: 0.01) from plain walls.
void Session::moveAllPathPointsAwayFromEdges() {
    for (int i = 0; i < path_.size(); ++i) {
        Vec2 p = path_[i].xy();
        Vec2 normal{};
        here().collideSphereSpecific(p, mouse_.reachDistance + 1.00999999f, 0, &normal, viewLeft_);
        path_[i].x = p.x;
        path_[i].y = p.y;
    }
}

void Session::handleFinger() {
    // (SceneGame::update @0x103528 draws paths only for a mouse still in play.)
    if (mouseCaught_) return;
    // (1.1.1 SceneGame::updateTouchPoints @0x1017f8) Nothing is drawn for a mouse in states 5,
    // 9 or 0xC (taken out of the player's hands, the exit walk), while the door timer is not
    // past (SceneGame +0x13190: from a door's use until the floor shows, and that tick) or once the exit has shut (LevelState +0x18); a path being drawn is let go of as it is (run lb1_win: a
    // touch on the mouse walking into the exit).
    const bool blocked = (mouse_.state >= 0 && mouse_.state <= 12 && ((1u << mouse_.state) & 0x1220u) != 0) ||
                         !(doorReadyAtMs_ < gameTimeMs_) || exitShut_;
    if (blocked) {
        fingerDrawing_ = false;
        fingerHasPath_ = false;
        return;
    }
    if (!(finger_.active && finger_.held)) {
        if (fingerDrawing_) finishDrawing();
        fingerHasPath_ = false;
        return;
    }
    const Vec2 finger = screenToMap(finger_.screen);

    // (1.1.1 updateTouchPoints @0x101cb8) A finger held since elsewhere that comes within reach
    // of the mouse, more than sqrt(200) from where it touched down, starts a path there too
    // (run l41_win: a drag passing over the mouse picks it up).
    // (Only while the finger has no path: one that ended in a hole or filled up stays the
    // finger's until it lifts, DAT_005fca7c; run l3h2_win.)
    if (!finger_.began && !fingerHasPath_) {
        const float reach = tuning_.playerTouchSize * tuning_.playerTouchSize;
        const Vec2 down = screenToMap(finger_.downScreen);
        if ((finger - mouse_.position).lengthSquared() - reach < 0.0f && (finger - down).lengthSquared() > 200.0f) {
            Vec2 offset = mouse_.position - finger;
            path_.begin(finger + offset, offset);
            ++pathsDrawn_;
            fingerDrawing_ = fingerHasPath_ = true;
            // (Here the index is 0, @0x101d50: the mouse heads for its own place first, one step
            // along heading 0.)
            mouse_.pathIndex = 0;
            mouse_.snapGlowMs = 0;
            snapHold_ = -1;
            prediction_ = {};
            return;
        }
    }

    if (finger_.began) {
        const float reach = tuning_.playerTouchSize * tuning_.playerTouchSize;
        // A touch near the mouse starts a new path at the mouse.
        if ((finger - mouse_.position).lengthSquared() - reach < 0.0f) {
            Vec2 offset = mouse_.position - finger;
            path_.begin(finger + offset, offset);
            ++pathsDrawn_;  // SceneGame::updateTouchPoints: incrementMetric (hypothesis: "paths")
            fingerDrawing_ = fingerHasPath_ = true;
            // SceneGame::updateTouchPoints @0x101cf0: the mouse heads past the path's first point
            // (its own position) at once; 1.0.5 started at index 0, which let it take one step
            // along its old heading.
            mouse_.pathIndex = 1;
            mouse_.snapGlowMs = 0;
            snapHold_ = -1;
            prediction_ = {};  // Player::playerPredictivePathReset @0x12010c
            return;
        }
        // Else a hidden mouse may come out by the nearest object to the touch (within 45 units)
        // whose tag is the one it went in by (@0x101bfe); Object::getTouched @0x115d2c lets it
        // out only if that is an exit. The path starts there.
        if (mouse_.holeTag >= 0) {
            int from = -1;
            float nearest = 2025.0f;
            for (LevelObject& o : objects_) {
                if (!o.active || (o.floor != shownFloor_ && !level_.infinite()) || o.tag != mouse_.holeTag) continue;
                const float d = (o.position - finger).lengthSquared();
                if (d < nearest) {
                    nearest = d;
                    from = o.id;
                }
            }
            if (from >= 0 && !objects_[std::size_t(from)].exit) from = -1;
            if (from >= 0) {
                comeOutOf(objects_[std::size_t(from)]);
                Vec2 offset = mouse_.position - finger;
                path_.begin(finger + offset, offset);
                ++pathsDrawn_;
                fingerDrawing_ = fingerHasPath_ = true;
                mouse_.pathIndex = 1;
                mouse_.snapGlowMs = 0;
                snapHold_ = -1;
                prediction_ = {};
                return;
            }
        }
        // A touch near the end of the current path continues it ("allow_path_extend"),
        // unless the path already ended in a hole.
        if (!path_.empty() && !path_.snapped && mouse_.state != 5) {
            Vec2 end = path_.back().xy();
            if ((end - finger).lengthSquared() - reach < 0.0f) {
                path_.drawing = true;
                path_.snapped = false;
                path_.fingerOffset = end - finger;  // (1.1.1 @0x101f9a: the line goes on from its end)
                fingerDrawing_ = fingerHasPath_ = true;
                snapHold_ = -1;
            }
        }
        return;
    }

    if (!fingerDrawing_ || path_.empty()) return;
    // Extend the path toward the finger once it has moved far enough. A segment into a wall
    // is clipped; one that still crosses a solid wall after clipping does not count as
    // movement. (1.1.1 @0x10231e) An unclipped point is kept 12 off the solid walls.
    const Vec2 last = path_.back().xy();
    Vec2 candidate = finger + path_.fingerOffset;
    Vec2 delta;
    if (here().clipLine(last, candidate, viewLeft_)) {
        delta = here().lineTouchesType(last, candidate, 0, viewLeft_) ? Vec2{} : candidate - last;
    } else {
        here().collideSphereSpecific(candidate, 12.0f, 0, nullptr, viewLeft_);
        delta = candidate - last;
    }
    if (!(delta.lengthSquared() > 0.9604f)) return;

    if (path_.size() >= DrawnPath::kCapacity) {
        // (@0x102588: full; the same as lifting the finger, its smoothing without the view.)
        path_.smooth(true, true, &here(), 0.0f);
        path_.drawing = false;
        fingerDrawing_ = false;
        moveAllPathPointsAwayFromEdges();
        return;
    }
    if (path_.size() >= 4) path_.fingerOffset *= 0.95f;  // the line converges onto the finger
    path_.append(candidate);
    path_.resample(false, true, &here(), viewLeft_);
    path_.smooth(false, true, &here(), viewLeft_);
    if (int snap = snapPath(true)) {
        // (@0x10248a: both kinds resample and smooth on; a hole also ends the drawing.)
        path_.resample(true, true, &here(), viewLeft_);
        path_.smooth(false, true, &here(), 0.0f);
        if (snap != 1) {
            path_.drawing = false;
            path_.snapped = true;
            fingerDrawing_ = false;
        }
        mouse_.snapGlowMs = 1250;
    }
    // The end point keeps the mouse's reach (and 1.01 more) from plain walls.
    PathPoint& end = path_.back();
    Vec2 p = end.xy();
    Vec2 normal{};
    here().collideSphereSpecific(p, mouse_.reachDistance + 1.00999999f, 0, &normal, viewLeft_);
    end.x = p.x;
    end.y = p.y;
}

// SceneGame::snapPath @0x101070: when the end of the path comes within reach of a cheese or
// an open hole, the path is pulled onto it. Returns 0 (no snap), 1 (snapped beside a cheese,
// drawing goes on) or 2 (snapped into a hole).
int Session::snapPath(bool highlight) {
    if (path_.size() < 2) return 0;
    if (!carried_.empty() && objects_[carried_.front()].type == 3) return 0;
    // The end point as it was on entry (its slot may be reused below).
    const int lastIndex = path_.size() - 1;
    const Vec2 last = path_[lastIndex].xy();

    // While the end is still inside the object it last snapped onto, that point is dropped.
    if (snapHold_ >= 0) {
        const LevelObject& held = objects_[snapHold_];
        float r = held.radius;
        if (held.type == 0x104) r = r * 0.2f;
        Vec2 d = held.position - last;
        float distance = d.normalise(1.0f);
        if ((mouse_.reachDistance + r) - distance > 0.0f) path_.removeLast();
        else snapHold_ = -1;
    }

    // The nearest object within reach that the path may snap onto.
    float bestDistance = 9999.999f;
    int best = -1;
    for (const LevelObject& o : objects_) {
        // (Objects of the floor shown, or of any floor in a chase.)
        if (!o.active || (o.floor != shownFloor_ && !level_.infinite()) || o.state == ObjectState::Hidden) continue;
        const int t = o.type;
        bool holeLike = false;  // holes the predicted cheese will open
        if (prediction_.enoughCheese) {
            if (isMainHomeHole(t)) holeLike = true;
            else if (t < 0x140) holeLike = (t - 0x21u < 7u) && ((1u << (t - 0x21)) & 0x79u) != 0;
            else holeLike = t == 0x140 || t == 0x166;
        }
        const bool eligible = (prediction_.snappedToCheese && isBankHole(t)) || o.snappable || holeLike;
        if (!eligible || o.inTrail) continue;
        float r = o.radius;
        if (t == 0x104) r = r * 0.2f;
        // Objects still glowing from a snap are skipped (holes excepted; the intro's pulse
        // does not count).
        if (!floatIsZero(o.pulseMs) && !(t >= 0x14 && t <= 0x17) && !isBankHole(t) && !o.introPulse) continue;
        if (o.id == mouse_.lastHole) continue;
        if (o.id == mouse_.leftHole) continue;  // the hole just left (Player +0x2e8, @0x10136c)
        // Doors (0x1e, 0x1f, 0x21..0x23) only once the door timer allows (Player +0x364, @0x101374).
        if ((t == kDoorUp || t == kDoorDown || (t >= 0x21 && t <= 0x23)) && doorReadyAtMs_ > gameTimeMs_) continue;
        if (t == kKey && o.flying) continue;  // (a thrown key, @0x1013a0)
        Vec2 d = o.position - last;
        float distance = d.normalise(1.0f);
        if (mouse_.reachDistance + r < distance) continue;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = o.id;
        }
    }

    // Snapping stops at 0x8c edges (only on levels that have them): the path ends there, except
    // in a boss fight while its exit door is still shut (@0x1014f8).
    {
        static constexpr int kIgnored[] = {0x73};
        Vec2 probe = last;
        Vec2 normal{};
        if (here().collideSphere(probe, mouse_.reachDistance, viewLeft_, &normal, kIgnored) & contact::k8C) {
            if (level_.info().mode != "boss") return 2;
            const LevelObject* exit = firstObjectOfType(kBossExitDoor);
            if (exit && exit->state == ObjectState::HoleOpen) return 2;
        }
    }
    if (best < 0 || path_.snapTarget == best) return 0;
    path_.snapTarget = -1;
    LevelObject& target = objects_[best];
    snapHold_ = best;

    int result;
    if (target.snapsToCentre) {
        // A hole: the path ends at its centre.
        result = 2;
        if (path_.size() + 1 < DrawnPath::kCapacity) path_.append(target.position);
    } else {
        // Anything else: a point at the object's rim, 2 units inside it, toward the path; the
        // smoothing keeps it there (1.1.1: flag 99).
        result = 1;
        if (path_.size() + 1 < DrawnPath::kCapacity) {
            Vec2 toward = last - target.position;
            toward.normalise(target.radius + -2.0f);
            path_.append(target.position + toward).flag = kFixedPoint;
        }
    }
    // A full trail cannot take more cheese: no snap (the point stays).
    const int t = target.type;
    if (isCheese(t) || t == 0xB || t == 0xC || t == 0x13) {
        if (carryCapacity_ - 1 < int(carried_.size())) return 0;
    }
    if (highlight) {
        target.snapCooldownMs = 0;
        target.pulseMs = float(tuning_.objectPulseMs);
    }
    const int newLast = path_.size() - 1;
    if (newLast < path_.resampledEnd) path_.resampledEnd = newLast;
    if (newLast < path_.smoothedEnd) path_.smoothedEnd = newLast;
    path_.snapTarget = best;
    return result;
}

// --- the mouse and what it carries ------------------------------------------------------

// Player::onFootprint @0x121874.
void Session::onFootprint() {
    const bool wasLeft = mouse_.footprintSide;
    mouse_.footprintSide = !wasLeft;
    Vec2 side{-libSin(mouse_.heading), libCos(mouse_.heading)};
    side.normalise(2.0f);
    mouse_.lastFootprint = mouse_.position + side * (wasLeft ? -1.0f : 1.0f);
    if (mouse_.state == 9) return;
    if (mouse_.wetMs > 0) {
        // @0x1218c4: wet feet leave a print (particle 0x31) on the step's side, as faint as
        // the time left, turned with the mouse, already 5.5 s old; water-blue, black or the
        // paint's colour.
        const int slot = particles_.add(0x2e, mouse_.floor, mouse_.position, {}, nullptr, 0, {}, true);
        if (Particle* p = particles_.at(slot)) {
            p->scale = float(mouse_.wetMs) * mouse_.wetFactor;
            p->position = mouse_.lastFootprint;
            p->rotation = mouse_.heading * 57.2957764f + -90.0f;
            p->ageMs = 5500;
            if (mouse_.wetKind == 0) {
                p->red = 0.379999995f, p->green = 0.680000007f, p->blue = 0.889999986f;
            } else if (mouse_.wetKind == 1) {
                p->red = p->green = p->blue = 0.0f;
            } else if (mouse_.wetKind == 2) {
                p->red = mouse_.wetRed, p->green = mouse_.wetGreen, p->blue = mouse_.wetBlue;
            }
        }
    }
    if (mouse_.state == 8) return;
    // Player::onFootprint @0x121a34: rand() % 2 picks one of two steps; the foot now down
    // (+0x518) picks left or right; swimming (state 4) and holding something have their own.
    // (The mouse never holds objects in the reconstructed levels.)
    const int pick = random_.next() % 2;
    const bool left = mouse_.footprintSide;
    if (mouse_.state == 4) sound(left ? sfx::SwimLeft : sfx::SwimRight);
    else if (mouse_.cup >= 0) sound(left ? 0x96 : 0x97);  // the teacup's steps (the draw is made all the same)
    else sound((left ? sfx::WalkLeft1 : sfx::WalkRight1) + pick);
}

// SceneGame::pulseScoreHUD @0xf8240: stars burst from a HUD panel, the later ones delayed.
void Session::pulseScoreHud(bool left, bool big) {
    hud_.scoreBounceMs = 500;
    // (The original sets these after starting the stars.)
    hud_.glowLeft = left;
    hud_.glowMs = 300;
    hud_.show();
    const int count = big ? 2 : 8;
    const int half = int(double(count) * 0.5);
    const int type = left ? 0x1d : 0x1e;
    const float panelHeight = float(left ? level_.hudLeftHeight() : level_.hudBonusHeight());
    Vec2 at{left ? 0.0f : kScreenWidth, kScreenHeight - panelHeight};
    for (int i = 0, step = 0; i < count; ++i, step += 15) {
        const int delay = i >= half ? step * i : 0;
        particles_.add(type, mouse_.floor, at, {}, nullptr, delay, {-1.0f, -1.0f}, true);
    }
}

void Session::updateMouse(int dtMs) {
    // Player::update @0x1252bc, in the original's order.
    if (mouseCaught_) {
        // Caught: the level knows the mouse is gone, its path is dropped and it does nothing
        // more. Each tick the last item of the trail drops off (@0x12532c, as Player::kill
        // does once; silent, the mouse is gone; run l3h2_win: o20 at the catch, o17 next).
        progress_.mouseGone = true;
        path_.truncate(0);  // (the points only: the drawing flag stays, 1.1.1 run l110_win)
        caughtMs_ += dtMs;
        if (!carried_.empty() && mouse_.position.x >= 0.0f) {
            objects_[std::size_t(carried_.back())].inTrail = false;
            droppedItem_ = carried_.back();
            dropGuardMs_ = 500;
            carried_.pop_back();
        }
        return;
    }
    if (mouse_.wetMs >= 1) mouse_.wetMs -= dtMs;
    if (int((mouse_.position - mouse_.lastFootprint).lengthSquared()) >= 101) onFootprint();
    if (mouse_.snapGlowMs > 0) mouse_.snapGlowMs -= dtMs;
    if (mouse_.lastHole >= 0 && mouse_.lastHoleUntilMs < gameTimeMs_) mouse_.lastHole = -1;
    if (mouse_.leftHole >= 0 && mouse_.leftHoleUntilMs < gameTimeMs_) mouse_.leftHole = -1;

    // Player::update @0x125a88 (before the slide, which stops the mouse): the speed cheese's time runs out; while it lasts a puff
    // (particle 0x32) is left behind the moving mouse every 150 ms.
    if (mouse_.chilliMs >= 1) {
        mouse_.chilliMs -= dtMs;
        if (mouse_.velocity.lengthSquared() > 0.0f && mouse_.chilliMs % 150 <= kTickMs) {
            const Vec2 behind = mouse_.position -
                                Vec2{libCos(mouse_.heading), libSin(mouse_.heading)} * mouse_.reachDistance;
            particles_.add(0x2f, mouse_.floor, behind, {}, nullptr, 0, {-1.0f, -1.0f}, true);
        }
    } else {
        mouse_.scoreBoost = 0.0f;
        mouse_.chilliMs = 0;
    }
    // Growing out of a hole the mouse stands still; its growth is taken before this tick's
    // part of it (Player +0x244, run l18_holes).
    mouse_.growing = mouse_.slideDirection == 1 && mouse_.slideRemainingMs >= 1;
    if (mouse_.slideRemainingMs >= 1) {
        // Sliding into (or out of) a hole: the drawn mouse eases toward the hole and shrinks.
        float fraction = float(mouse_.slideRemainingMs) / float(mouse_.slideDurationMs);
        if (mouse_.slideDirection == 1) fraction = 1.0f - fraction;
        mouse_.drawScale = tuning_.playerScale * fraction;
        mouse_.slidePosition = (mouse_.slidePosition * 5.0f + mouse_.slideTarget) * 0.16666667f;
        mouse_.slideRemainingMs -= dtMs;
        mouse_.velocity = {};
    } else if (mouse_.state != 2) {
        mouse_.drawScale = tuning_.playerScale;
    } else {
        mouse_.drawScale = 0.0f;
    }

    if (mouse_.state == 2 && mouse_.hole >= 0 && !carried_.empty()) {
        // Hidden: carried cheese that reaches the hole goes in.
        const LevelObject& hole = objects_[mouse_.hole];
        for (std::size_t i = 0; i < carried_.size();) {
            LevelObject& item = objects_[carried_[i]];
            Vec2 d = hole.position - item.position;
            float reach = hole.radius + item.radius;
            if (d.lengthSquared() < reach * reach) {
                if (hideObject(item, hole)) {
                    carried_.erase(carried_.begin() + std::ptrdiff_t(i));
                    continue;
                }
                item.restPosition = item.position;
                item.position = mouse_.position;
            }
            ++i;
        }
    }

    // (Player::update @0x125a30: the dropped item may be picked up again 500 ms after the
    // mouse last touched it.)
    if (dropGuardMs_ > 0) dropGuardMs_ -= dtMs;
    if (droppedItem_ >= 0 && dropGuardMs_ < 1) {
        droppedItem_ = -1;
        dropGuardMs_ = 0;
    }
    if (mouse_.state == 0xC && !exitShut_) {
        // Into the exit: the mouse walks on by itself and the rest of the update is skipped
        // (until the door has shut behind it, LevelState +0x18: then it stands, 1.1.1 run
        // lb1_win).
        walkIntoExit(dtMs);
        return;
    }
    const float weight = updateTrail(dtMs);
    // Player::update @0x126850: the picture shown is the one that runs: the worn teacup's, or
    // the mouse's (also in the bare half of the cup's flashing).
    if (mouse_.cup >= 0 && teacupFlashPhase(objects_[std::size_t(mouse_.cup)].timerMs,
                                            objects_[std::size_t(mouse_.cup)].playMs) != -1)
        mouse_.cupSprite.update(dtMs);
    else
        mouse_.sprite.update(dtMs);
    if (level_.infinite()) pushByViewEdge();
    mouse_.seenByBoss = false;
    for (const LevelObject& o : objects_)
        if (o.type == kBossCat) {  // (the first one, getFirstObjectByType)
            mouse_.seenByBoss = o.state == ObjectState(0x12);
            break;
        }
    mouse_.tick(dtMs, path_, weight, tuning_);
    // Player::update @0x126fd2: a worn teacup goes where the mouse stepped.
    if (mouse_.cup >= 0 && mouse_.moving()) objects_[std::size_t(mouse_.cup)].position = mouse_.position;
    mouse_.viewScrolling = scrollSpeed_ > 0.100000001f;
    // (Growing out of a hole counts as standing: the idle time runs on, run l35_win.)
    const bool cupRuns = mouse_.cup >= 0 && teacupFlashPhase(objects_[std::size_t(mouse_.cup)].timerMs,
                                                             objects_[std::size_t(mouse_.cup)].playMs) != -1;
    if (mouse_.state != 2) mouse_.updateAnimation(dtMs, random_, cupRuns);
    // The score popup ends with a burst of stars on the HUD.
    // Meanwhile the HUD's score climbs to the real score (Player::update @0x1269d6).
    if (progress_.lastScoreShowMs >= 1) {
        progress_.lastScoreShowMs -= dtMs;
        if (progress_.lastScoreShowMs <= 0) {
            pulseScoreHud(true, false);
            playScoreSound(false);  // Player::update @0x1269cc
            progress_.lastScoreShowMs = 0;
            hud_.countScore(dtMs, progress_.score, false);
        }
    } else {
        hud_.countScore(dtMs, progress_.score, false);
    }
}

// Player::updateTrail @0x1236c0: carried items follow the mouse in a chain and slow it down.
// Returns the trail weight.
float Session::updateTrail(int dtMs) {
    if (carried_.empty()) return 0.0f;
    const float dt = float(dtMs);
    std::vector<int> types;
    const LevelObject* previous = nullptr;
    // Items keep 18 units apart. One whose line to what it follows crosses a solid wall
    // (type 0 only, 1.1.1) closes up to 2, and so do all items behind it: an item with a clear
    // line keeps the spacing of the item in front (the original only ever sets 2 or
    // "unchanged"). (1.1.1 also closes up while a type-0x109 object is the mouse's +0x304 or
    // while it is near a type-0x1b8 object, Player +0x6dc; no map places either.)
    float spacing = 18.0f;
    for (int id : carried_) {
        LevelObject& item = objects_[id];
        // Each item follows the one in front of it; the first follows the mouse (or the
        // hole the mouse went into).
        Vec2 anchor;
        if (previous == nullptr || previous->state == ObjectState::Hidden) {
            anchor = (mouse_.state == 2 && mouse_.hole >= 0) ? objects_[mouse_.hole].position : mouse_.position;
        } else {
            anchor = previous->position;
        }
        bool lineClear = false;
        if (mouse_.state != 9 && mouse_.state != 0xC) {
            if (!here().lineTouchesType(item.position, anchor, 0, viewLeft_)) lineClear = true;
            else spacing = 2.0f;
        }
        // Cheese turns to face what it follows: frame = int(angle x frames / 360 - 1), wrapped
        // once (0x10d972; Sprite::setAnim(frame, -1, hold, 200)).
        if (item.frames >= 1 && (isCheese(item.type) || item.type == 0xC)) {
            const int n = item.frames;
            int f = int((anchor - item.position).angleDegrees() * float(n) * 0.00277777785f + -1.0f);
            if (f <= -1) f += n;
            else if (f > n) f -= n;
            item.anim.setAnim(f, -1, SpriteAnim::Hold, 200);
        }

        if (mouse_.state == 2) {
            if (item.state != ObjectState::Hidden) {
                // Pulled into the hole, faster the farther away.
                Vec2 d = anchor - item.position;
                float distanceSquared = d.lengthSquared();
                d.normalise(1.0f);
                const float pull = isHomeHole(objects_[mouse_.hole].type) ? 0.00015f : 0.000225f;
                item.position += d * ((pull * distanceSquared) * dt);
                if (lineClear) here().collideSphereSpecific(item.position, item.radius, 0, nullptr, viewLeft_);
            }
        } else {
            Vec2 d = anchor - item.position;
            if (!(d.lengthSquared() > spacing * spacing)) {
                // (1.1.1) Close enough: only kept out of walls.
                here().collideSphereSpecific(item.position, item.radius, 0, nullptr, viewLeft_);
            } else {
                d.normalise(spacing);
                const Vec2 target = anchor - d;
                const int s = mouse_.state;
                if (s >= 0 && s < 13 && ((1 << s) & 0x1311) != 0) {
                    item.position = (target + item.position * 4.0f) * 0.2f;
                    if (lineClear) here().collideSphereSpecific(item.position, item.radius, 0, nullptr, viewLeft_);
                }
            }
        }
        types.push_back(item.type);
        previous = &item;
    }
    return trailWeight(types.data(), int(types.size()), tuning_);
}

// SceneGame::collideObjects @0x10023c: the mouse touches objects, then is kept out of walls.
// ObjControllerPickups::touchPlayer @0x1b85b2: a teacup that is not worn and not used up goes
// over the mouse: any other worn cup is gone, sound 0x27, the cup becomes Player +0x180 and
// the mouse is set to its state again (Player::setState(0): only its idle sequence stops),
// the cup is no snap target and starts running. Cats cannot see a mouse under a cup
// (Object::canSeePlayer).
void Session::wearTeacup(LevelObject& cup) {
    if (mouse_.cup == cup.id || cup.state == ObjectState(0x22)) return;
    if (mouse_.cup >= 0) {
        objects_[std::size_t(mouse_.cup)].active = false;
        mouse_.cup = -1;
    }
    sound(0x27);
    ++uses_.cups;
    mouse_.cup = cup.id;
    mouse_.stopIdleSequence();
    cup.snappable = false;
    cup.updating = true;
}

// ObjControllerPickups::update @0x1b7b64, a worn teacup: its time (+0x1dc) runs out. Under
// half of its whole time (+0x1d8; under a quarter, faster) it flashes with a period of a tenth
// of that mark, ticking (sound 0x98) whenever a flash goes off; used up it is gone (state
// 0x1e), the mouse is bare again, sound 0x99.
void Session::updateTeacup(LevelObject& cup, int dtMs) {
    const bool wasOn = teacupFlashPhase(cup.timerMs, cup.playMs) == 1;
    cup.timerMs -= dtMs;
    if (wasOn && teacupFlashPhase(cup.timerMs, cup.playMs) == -1) sound(0x98);
    if (cup.timerMs > 0) return;
    cup.state = ObjectState(0x22);
    cup.active = false;
    if (mouse_.cup >= 0) mouse_.cup = -1;
    sound(0x99);
}

// ObjControllerHideable::touchPlayer @0x1b4cac: going home with a cat on its heels (the first
// cat, in object order, chasing within 50 of the mouse): "Close call!" (string 0x267, particle
// 0x36 a line above the mouse; the line height of the 25-pixel font, 32, is assumed) and
// sound 0x5c (run l211_win).
void Session::closeCall() {
    for (const LevelObject& o : objects_) {
        if (!o.active) continue;
        const Cat* cat = catFor(o.id);
        if (!cat || cat->state() != 5) continue;
        if (!((cat->position() - mouse_.position).length() < 50.0f)) continue;
        const data::StringTable* text = level_.strings();
        particles_.add(kTextBubble, mouse_.floor, {mouse_.position.x, mouse_.position.y + 32.0f},
                       text ? text->text(0x26f) : std::string(), nullptr, 0);
        sound(0x5c);
        return;
    }
}

void Session::collideObjects() {
    const int s = mouse_.state;
    if (mouseCaught_) return;  // a mouse out of play touches nothing
    if (s >= 0 && s < 13 && ((1 << s) & 0x1222) != 0) return;
    for (LevelObject& o : objects_) {
        if (!o.active || o.state == ObjectState(0x15) || o.state == ObjectState(0x40)) continue;
        if (o.floor != mouse_.floor) continue;
        // (Object::touchPlayer @0x118198 also leaves objects of a floor not shown alone: the
        // floor a door leads to only counts once the iris has shut, run l38_win.)
        if (o.floor != shownFloor_ && !level_.infinite()) continue;
        Vec2 d = mouse_.position - o.position;
        float distance = d.normalise(1.0f);
        if ((mouse_.reachDistance + o.radius) - distance > 0.0f) {
            // Touching. No object of the reconstructed families pushes the mouse back.
            if (o.type == kCrumb) {
                hud_.show();
                eatCrumb(o);
            } else if (isBonusPickup(o.type)) {
                eatBonus(o);
            } else if (o.type == kDossier && o.state != ObjectState(0x22)) {
                // ObjControllerPickups::touchPlayer @0x1b8790 (1.1.1): a puff, the dossier
                // sound, and its page opens a quarter of a second later.
                particles_.add(0x15, shownFloor_, o.position);
                o.state = ObjectState(0x22);
                o.timerMs = 250;
                sound(0xb1);
            } else if (isSpeedItem(o.type)) {
                eatSpeedCheese(o);
            } else if (o.type == kMouseTrap && o.state == ObjectState(0x1d)) {
                touchTrap(o, true);
            } else if (o.type == kTeacup) {
                wearTeacup(o);
            } else if (o.type == kCheeseMachine) {
                // Object::touchPlayer @0x118290: touched while handing out with room in the trail,
                // it goes idle (the cheese stays in) and, with cheeses left, raises its buttons
                // again (message 2 to its tag).
                if (o.state == ObjectState(kMachineDispensing) && int(carried_.size()) < carryCapacity_) {
                    o.anim.setAnim(0, -1, SpriteAnim::Hold, 200);
                    o.state = ObjectState(kMachineIdle);
                    if (o.machineCount > 0) {
                        GameMessage m;
                        m.type = 2;
                        m.tag = o.tag;
                        m.sender = o.id;
                        sendMessage(m, true);
                    }
                }
            } else if (isCheese(o.type) || o.type == kClockworkMouse || o.type == kBalloon || (o.type == kKey && !o.flying) ||
                       o.type == kTwine || o.type == kCatnip) {
                // ObjControllerPickups::touchPlayer @0x1b7f7c: loose cheese (and a clockwork
                // mouse, twine, catnip) is picked up.
                const bool busy = o.state == ObjectState(0x22) || o.state == ObjectState::Hidden ||
                                  o.state == ObjectState(0x25) || o.state == ObjectState(0x35);
                // (On success the original also stops the cheese's own movement path; level
                // cheese does not move.)
                // (touchPlayer brightens the HUD for loose cheese first: SceneGame::showHud.)
                if (!o.inTrail && o.state != ObjectState::Hidden && isCheese(o.type)) hud_.show();
                if (!o.inTrail && !busy && pickup(o) && isCheese(o.type) && o.heldBy != -1) {
                    // @0x1b8096: a cheese from a cheese machine that has more to give raises the
                    // machine's buttons again (message 2 to its tag).
                    GameMessage m;
                    m.type = 2;
                    m.tag = o.heldBy;
                    sendMessage(m, true);
                }
            } else if (Cat* cat = catFor(o.id)) {
                // ObjControllerCat::touchPlayer: a cat that can catches the mouse.
                CatWorld world = catWorld(o.floor);
                Vec2 meetingPoint;
                const bool caught = cat->touchMouse(world.mouse, world, meetingPoint);
                if (caught) killMouse(meetingPoint);
                catEffects(world, cat->objectId());
                o.position = cat->position();
                o.state = ObjectState(cat->state());
                o.anim = cat->walk;  // Object +0x38; drawing picks the active picture (Cat::activeSprite)
            } else if (Switch* sw = switchFor(o.id)) {
                // ObjControllerSwitch::touchPlayer: the mouse presses it (@0x1ae97a: a fight
                // button starts running and is marked as touched this tick).
                if (sw->type == 0x9b) sw->updating = true;
                pressSwitch(o, *sw, 1);
                if (sw->type == 0x9b) sw->touched = 2;
            } else if (Spitter* spitter = spitterFor(o.id)) {
                // ObjControllerBoss::touchPlayer: the Spitter catches the mouse.
                BossWorld world = bossWorld(o.floor);
                spitter->touchMouse(mouse_.boostTimerMs > 0, world);
                o.state = ObjectState(spitter->state());
            } else if (Vacuum* vacuum = vacuumFor(o.id)) {
                // ObjControllerBoss::touchPlayer: the vacuum catches the mouse.
                BossWorld world = bossWorld(o.floor);
                vacuum->touchMouse(mouse_.boostTimerMs > 0, world);
                o.state = ObjectState(vacuum->state());
            } else if (Droid* droid = droidFor(o.id)) {
                // ObjControllerBoss::touchPlayer: the droid runs the mouse over.
                BossWorld world = bossWorld(o.floor);
                droid->touchMouse(mouse_.boostTimerMs > 0, world);
                o.state = ObjectState(droid->state());
            } else if (o.type == kTv) {
                // Object::touchPlayer @0x11874c: the play time starts again; an idle TV is
                // switched on (no longer a snap target, running, sound 0x48).
                o.timerMs = o.playMs;
                if (o.state == ObjectState(kTvOff)) {
                    o.snappable = false;
                    o.updating = true;
                    sound(0x48);
                    ++uses_.tvs;
                }
            } else if (isFloorDoor(o.type) || isRoomDoor(o.type)) {
                // (1.1.1 ObjControllerFloorTransport::touchPlayer @0x1b232c: a shut door, state
                // 0x29, is not used at all and leaves the door timer alone; run l37_win, the
                // mouse standing on a shut trapdoor.)
                if (o.state != ObjectState::HoleClosed) {
                    if (isFloorDoor(o.type)) useDoor(o);
                    else useTrapdoor(o);
                }
            } else if (o.type == kExitDoor || o.type == kBossExitDoor) {
                // Object::touchPlayer @0x11896a: the open exit door ends the chase (and the
                // boss fight).
                if (o.state == ObjectState::HoleOpen) finishScrollingLevel();
            } else if (isWallHole(o.type)) {
                // ObjControllerHideable::touchPlayer @0x1b4a78: the hole just left only
                // remembers the touch; any other takes the mouse in (sound 0x32 after hiding).
                if (mouse_.leftHole == o.id) {
                    mouse_.leftHoleUntilMs = gameTimeMs_ + 100;
                } else {
                    hideIn(o);
                    sound(0x32);
                }
            } else if (isPipe(o.type)) {
                // ObjControllerHideable::touchPlayer @0x1b4ebe: as a wall hole, with sound 0x27.
                if (mouse_.leftHole == o.id) {
                    mouse_.leftHoleUntilMs = gameTimeMs_ + 100;
                } else {
                    hideIn(o);
                    sound(0x27);
                }
            } else if (isMunchHole(o.type)) {
                // ObjControllerHideable::touchPlayer @0x1b4f9c: the hole just left only
                // remembers the touch; an open safe hole the path ends in takes the mouse in.
                if (mouse_.leftHole == o.id) {
                    mouse_.leftHoleUntilMs = gameTimeMs_ + 100;
                } else if (o.state == ObjectState::HoleOpen && gameTimeMs_ > mouse_.leftHoleUntilMs &&
                           path_.snapTarget == o.id) {
                    hideIn(o);  // (its sounds are Player::hide's in 1.1.1)
                }
            } else if (isHomeHole(o.type)) {
                // ObjControllerHideable::touchPlayer: an open home hole ends the level.
                if (o.state == ObjectState::HoleOpen) {
                    if (o.timerMs > 1000) o.timerMs = 1000;
                    closeCall();
                    hideIn(o);
                    hud_.pauseVisible = false;
                    progress_.over = true;
                    progress_.won = true;
                    if (!isMainHomeHole(o.type)) progress_.foundSecret = true;  // (LevelState +0x56)
                    scoreCheese(false);
                }
            }
        }
    }

    // Walls (@0x100682): what the mouse touches decides, the first of these that applies: solid
    // or bouncy walls push it out (water edges, 1 and 2, would also change its state: no level
    // of worlds 1-4 has them); a 0x7b edge stops the prints, to come back plain once it is out;
    // a paint edge (0x8d) likewise, in the paint's colour (the edge's tag); (1.1.1 @0x100766)
    // a checkpoint line (0x72) switches itself off and the level is snapshotted for a retry
    // ("Keep Going!", run lb2_win). (Lava, chasms, tripping edges, conveyors under the mouse,
    // exit lines (0x6e, 0x90, 0x78, 0x71, 0x7a) and 0x7c message edges, which come before the
    // checkpoint line, are in later worlds: not reconstructed.)
    static constexpr int kIgnored[] = {0x73};
    Vec2 p = mouse_.position;
    Vec2 normal{};
    unsigned touched = here().collideSphere(p, mouse_.reachDistance, viewLeft_, &normal, kIgnored);
    if (touched & contact::kBouncy) touched |= contact::kSolid;
    int pending = -1;
    if (touched & contact::kSolid) {
        mouse_.position = p;
    } else if (touched & contact::k7B) {
        pending = 1;
        mouse_.wetMs = 0;  // Player::stopFootprints
    } else if (touched & contact::kPaint) {
        pending = 2;
        mouse_.footprintColour = int(normal.x);
        mouse_.wetMs = 0;
    } else if ((touched & contact::k72) &&
               !(touched & (contact::k7C | contact::k6E | contact::k90 | contact::k78 | contact::kConveyor))) {
        const int index = int(normal.x);
        LevelGeometry& walls = floorWalls_[std::size_t(mouse_.floor)];
        if (index >= 0 && index < int(walls.walls().size()) && walls.walls()[std::size_t(index)].enabled) {
            walls.setWallEnabled(std::size_t(index), false);
            takeCheckpoint();
        }
    }
    if (pending == -1 && mouse_.footprintPending != -1)
        startFootprints(mouse_.footprintPending, 1500, mouse_.footprintPending == 2 ? mouse_.footprintColour : -1);
    mouse_.footprintPending = pending;
}

// Player::startFootprints @0x1217d4: prints for `ms`, of a kind, in a colour 0xRRGGBB.
void Session::startFootprints(int kind, int ms, int colour) {
    mouse_.wetMs = ms;
    mouse_.wetKind = kind;
    mouse_.wetFactor = 1.0f / float(ms);
    const unsigned c = unsigned(colour);
    mouse_.wetRed = float((c & 0xffffff) >> 16) * 0.00392156886f;
    mouse_.wetGreen = float((c & 0xffff) >> 8) * 0.00392156886f;
    mouse_.wetBlue = float(c & 0xff) * 0.00392156886f;
}

// SceneInGameTutorial::init @0x295618, update @0x29579c, runLevelTutorialScript @0x294da8:
// shown from the level's attempts on; on map 705 the first step until the cat has aimed for
// 0.1 s, then the second, mirrored when the mouse was left of the cat.
Session::TutorialView Session::tutorialView() const {
    TutorialView v;
    v.shown = !level_.tutorial().empty() && attempts_ >= level_.info().tutorialAfterAttempts;
    if (!v.shown || level_.info().map != 705) return v;
    v.slot = 0;
    // (The script looks at the level's first cat.)
    if (!cats_.empty() && tutorialAimMs_ >= 0 && tutorialMs_ - tutorialAimMs_ > 100) {
        v.slot = 1;
        v.mirrored = tutorialSide_ < 0;
    }
    return v;
}

// ObjControllerPickups::touchPlayer @0x1b7f7c (crumbs): eaten at once. Crumbs in quick
// succession (each within a second of the last) are worth 1, 2, 3, ... points; the run's
// count pops up over the crumb. Each crumb plays a note of a scale: the map gives it in the
// crumb's tag (1051..1063 for notes 0..12), else a random one.
void Session::eatCrumb(LevelObject& crumb) {
    crumb.active = false;
    crumbComboMs_ = 1000;
    ++crumbCombo_;
    const int points = crumbCombo_;
    particles_.add(0x15, crumb.floor, crumb.position);
    particles_.add(kTextPoints, crumb.floor, crumb.position, std::to_string(points));
    pulseScoreHud(true, true);
    progress_.score += points;
    sound(0x22);  // (crumb_pickup)
    int note = crumb.tag - 0x41b;
    if (note < 0 || note > 12) note = random_.next() % 13;
    sound(0x7c + note);
}

// Object::touchPlayer @0x118198 (type 60, armed). A clockwork mouse in the trail springs it:
// it leaves the trail and flies onto the trap, which lets go of its cheese (a new cheese,
// type 10, picked up at once; the level's cheese count stays). Without one, a touching mouse
// is caught 1 to the right of and 9 below the trap's middle (particle 4, sound 0x77) and the
// trap is gone.
void Session::touchTrap(LevelObject& trap, bool touching) {
    int clockwork = -1;
    for (int id : carried_)
        if (objects_[std::size_t(id)].type == kClockworkMouse || objects_[std::size_t(id)].type == 0x48) {
            clockwork = id;
            break;
        }
    if (clockwork < 0) {
        if (!touching) return;
        const Vec2 at = trap.position + Vec2{1.0f, -9.0f};
        mouse_.position = at;
        killMouse(at);
        particles_.add(4, trap.floor, at, {}, nullptr, 0, {-1.0f, -1.0f}, true);
        sound(0x77);
        trap.updating = false;
        trap.active = false;
        return;
    }
    LevelObject& thrown = objects_[std::size_t(clockwork)];
    thrown.inTrail = false;
    thrown.state = ObjectState(0x22);
    thrown.arcStart = thrown.position;
    thrown.arcTarget = trap.position;
    thrown.arcTimeMs = 0;
    thrown.updating = true;
    thrown.active = true;
    carried_.erase(std::find(carried_.begin(), carried_.end(), clockwork));
    const Vec2 at = trap.position;
    const int floor = trap.floor;
    const int trapId = trap.id;
    // SceneGame::addEntity takes the first unused object slot (run l19_win: an eaten heart's).
    // The cheese's picture is loaded after its set-up, so it keeps the vertex as its centre and
    // no touch circle.
    int slot = 0;
    while (slot < int(objects_.size()) && objects_[std::size_t(slot)].active) ++slot;
    // (Object::touchPlayer @0x118748: in the boss modes, 1 and 2, the trap lets go of a bonus
    // pickup instead, type 0x95, cheese_inf.png frame 3. In the chase, mode 1, the mouse eats it
    // on the spot (its touchPlayer is called at once, run lb1_trap); elsewhere it joins the
    // trail.)
    const std::string& mode = level_.info().mode;
    const bool chase = mode == "infinite";
    const bool bossMode = chase || mode == "boss";
    LevelObject made = level_.spawnObject(slot, bossMode ? kTrapPrize : 10, at, 0, floor);
    made.position = at;
    made.radius = 0.0f;
    if (bossMode) made.anim.setAnim(3, -1, SpriteAnim::Hold, 200);  // (ObjControllerPickups::loadAssets @0x1b7156)
    if (slot == int(objects_.size())) objects_.push_back(made);
    else objects_[std::size_t(slot)] = made;
    LevelObject& cheese = objects_[std::size_t(slot)];
    if (!bossMode) cheese.idleSparkleMs = int(random_.within(0.0f, 5000.0f));  // (ObjControllerPickups::init)
    if (chase) eatBonus(cheese);
    else pickup(cheese);
    LevelObject& sprung = objects_[std::size_t(trapId)];
    sprung.state = ObjectState(0x1e);
    sound(0x44);
    sprung.anim.setAnim(0, sprung.anim.frameCount() - 1, SpriteAnim::Once, 100);
    sprung.updating = true;
    sprung.snappable = false;
}

// ObjControllerPickups::touchPlayer @0x1b808a (type 14): unless the running boost is larger
// than its flashing mark (Player::getChilliFlashingMark @0x120160: half the time, or a quarter
// in its last quarter; the boost factor never is, so it is always eaten), the cheese is gone
// and the mouse runs at 1 + 0.2 x 4 times its speed for 16 s ("chilli_speed_mult",
// "chilli_duration"); sound 0x36.
void Session::eatSpeedCheese(LevelObject& cheese) {
    ++uses_.chillies;
    int mark = 0;
    if (mouse_.chilliMs > 0) {
        const float half = float(int(float(mouse_.chilliTotalMs) * 0.5f));
        float m = half * 0.5f;
        if (m < float(mouse_.chilliMs)) m = half;
        mark = int(m);
    }
    if (mouse_.scoreBoost > float(mark)) return;
    cheese.active = false;
    cheese.state = ObjectState(0x22);
    // (@0x1b80ce: chilli_speed_mult 4 and chilli_duration 16 s, or chilli2's 7.2 and 8 s for
    // the speedy item, Player +0x21c/+0x220/+0x224, the item's type in +0x228.)
    const bool second = cheese.type == kSpeedy;
    mouse_.scoreBoost = second ? 7.19999695f : 4.0f;
    mouse_.chilliMs = mouse_.chilliTotalMs = second ? 8000 : 16000;
    sound(0x36);
}

// ObjControllerPickups::touchPlayer @0x1b7f7c (type 13): gone, 5 points with sparkles and
// the number (not in infinite mode), the crumb sound and a note (0x7c + tag - 1051, else
// random), no score sound.
void Session::eatBonus(LevelObject& heart) {
    heart.active = false;
    particles_.add(0x15, heart.floor, heart.position);
    const int points = bonusPoints(heart.type);
    if (level_.info().mode != "infinite") {
        particles_.add(kTextPoints, heart.floor, heart.position, std::to_string(points));
        pulseScoreHud(true, true);
        progress_.score += points;
    }
    sound(0x22);
    if (heart.type == kBonusHeart) {
        int note = heart.tag - 0x41b;
        if (note < 0 || note > 12) note = random_.next() % 13;
        sound(0x7c + note);
    } else {
        playScoreSound(true);  // (the bigger pickups: the bonus sound)
    }
}

// Player::pickup @0x121b58. Returns false when the trail is full.
bool Session::pickup(LevelObject& cheese) {
    // The item that just dropped off the trail is not picked up again while the mouse still
    // touches it (Player::pickup @0x121b58).
    if (cheese.id == droppedItem_) {
        dropGuardMs_ = 500;
        return false;
    }
    // Where the new item goes: after items of its own type, else ordered by score with the
    // lowest-scoring next to the mouse.
    int counted = 0;
    bool sameType = false;
    for (int id : carried_) {
        const int t = objects_[id].type;
        if (t != 0xF && t != 0x12) ++counted;
        if (t == cheese.type) sameType = true;
    }
    if (!carried_.empty()) {
        if (counted >= carryCapacity_ && cheese.type != 0xF && cheese.type != 0x12) return false;
        std::size_t at = carried_.size();
        if (cheese.type == kBalloon) {
            at = 0;  // (@0x121c4a: a balloon goes first, right behind the mouse)
        } else if (sameType) {
            for (std::size_t i = 0; i < carried_.size(); ++i)
                if (objects_[carried_[i]].type == cheese.type) at = i + 1;
        } else {
            const int points = cheesePoints(cheese.type);
            for (std::size_t i = 0; i < carried_.size(); ++i)
                if (points < cheesePoints(objects_[carried_[i]].type)) {
                    at = i;
                    break;
                }
        }
        carried_.insert(carried_.begin() + std::ptrdiff_t(at), cheese.id);
    } else {
        carried_.push_back(cheese.id);
    }
    if (isCheese(cheese.type) && prediction_.cheeseAhead > 0) --prediction_.cheeseAhead;
    cheese.inTrail = true;
    // (ObjControllerPickups::touchPlayer @0x1b8036 starts a clockwork mouse running, run
    // l19_win; cheese runs anyway, and other items keep their flag: catnip, run l45_win.)
    if (cheese.type == kClockworkMouse) cheese.updating = true;
    cheese.pulseMs = float(tuning_.objectPulseMs);
    // The trail's hand for the HUD; a new hand bounces (Player::pickup @0x121ed8; cheese only).
    if (isCheese(cheese.type)) {
        int counts[kMaxCheeseTypes] = {};
        for (int id : carried_)
            if (isCheese(objects_[id].type)) ++counts[objects_[id].type - 6];
        const PokerHand hand = pokerHand(counts);
        if (hand != hud_.hand) hud_.bounceHand();
        hud_.hand = hand;
        handKinds(counts, hud_.topType, hud_.secondType);
    }
    requestExitOpen();
    // Player::pickup @0x121eec: a cheese opens the safe holes.
    if (isCheese(cheese.type)) openBankHoles();
    if (isCheese(cheese.type)) {
        if (path_.drawing && snapPath(false) == 2) {
            mouse_.wasDrawing = false;
            path_.drawing = false;
            path_.snapped = true;
            mouse_.snapGlowMs = 1250;
        }
        // Sparkles run along the carried cheese of this type (cosmetic timers).
        int delay = 100;
        for (int id : carried_) {
            LevelObject& item = objects_[id];
            if (item.type != cheese.type) continue;
            item.updating = true;
            item.timerMs = delay;
            delay += 150;
        }
    }
    // Player::pickup @0x121d26 / 0x10cee0: the cheese sound, then the snap; other items have a
    // sound of their own (a clockwork mouse: 0x67, pickup_toy).
    if (isCheese(cheese.type)) {
        sound(sfx::PickupFood);
        sound(sfx::UiSnap);
    } else if (cheese.type == kClockworkMouse) {
        sound(0x71);
    } else if (cheese.type == kKey) {
        sound(0x6f);
    } else if (cheese.type == kBalloon) {
        sound(0x35);  // (@0x121d60)
        ++uses_.balloons;
    } else if (cheese.type == kTwine) {
        sound(0x70);  // pickup_string (@0x121d66)
        ++uses_.twine;
    } else if (cheese.type == kCatnip) {
        sound(0x37);
        ++uses_.catnip;
    }
    return true;
}

// ObjControllerFloorTransport::startFloorTransition @0x1b25ac with Player::changeFloor
// @0x124fe4: the door's sound, the iris shuts on the door (a star over the bonus pattern for a
// door without a picture), and the mouse goes in, already on the other floor. Doors work once
// every ten seconds.
void Session::useDoor(LevelObject& door) {
    int to = mouse_.floor + (door.type == kDoorDown ? -1 : 1);
    if (door.type == kDoorToFloor) to = door.tag;
    if (door.type == kDoorToGround) to = 0;
    if (doorReadyAtMs_ < gameTimeMs_ && to >= 0 && to < level_.floorCount()) {
        sound(0x8b);
        changeFloor(door, to, 1, false);
    }
    doorReadyAtMs_ = gameTimeMs_ + 10000;
}

// The same for an open trapdoor (the original's type 0x21 after loadAssets): down a floor, no
// sound of its own, the trapdoor shuts behind the mouse, the iris in the mouse's silhouette
// (it is drawn, pass 9), and the cheese carried is scored on the way. A shut one only keeps the
// next try a second away.
void Session::useTrapdoor(LevelObject& door) {
    const int to = mouse_.floor + 1;
    if (doorReadyAtMs_ < gameTimeMs_) {
        doorReadyAtMs_ = gameTimeMs_ + 1000;
        door.updating = true;
        if (door.state == ObjectState::HoleClosed) return;
        door.anim.setAnim(door.anim.frameCount() - 1, 0, SpriteAnim::Once, 200);
        if (to < level_.floorCount()) changeFloor(door, to, 3, true);
    }
    doorReadyAtMs_ = gameTimeMs_ + 10000;
}

void Session::changeFloor(LevelObject& door, int to, int irisKind, bool score) {
    finalPosition_ = door.position;
    doorTransition_ = true;
    roomDoorTransition_ = score;
    iris_ = Iris{};
    iris_.kind = irisKind;
    iris_.closing = true;
    iris_.centre = {door.position.x - viewLeft_, door.position.y};
    iris_.fullRadius = kScreenWidth * irisScale(irisKind) + std::max(std::fabs(iris_.centre.x - kScreenWidth * 0.5f),
                                                                     std::fabs(iris_.centre.y - kScreenHeight * 0.5f));
    iris_.progress = 1.0f;
    iris_.radius = iris_.fullRadius;
    iris_.speed = 0.000800000038f;
    mouse_.floor = to;
    hideIn(door);
    if (score) scoreCheese(false);
    // The mouse comes out where the door's route starts (its tag names it), else at the
    // door's own place.
    const CatRoute route = CatRoute::link(level_.floor(door.floor).map, door.tag);
    mouse_.position = route.points.empty() ? door.position : route.points.front().position;
    path_.truncate(0);
}

// Player::setState(0) from hiding: the mouse grows out of the hole in 80 ms; what it carries
// gathers at it.
void Session::leaveHole() {
    const int was = mouse_.hole;
    mouse_.state = 0;
    mouse_.hole = -1;
    mouse_.holeTag = -1;
    // The hole stays "just left" for 100 ms of not touching it (Player +0x168/+0x1ec).
    mouse_.leftHole = was;
    mouse_.leftHoleUntilMs = gameTimeMs_ + 100;
    mouse_.stateEntry = mouse_.position;
    // (Player::setState @0x123fd2: the grow out of the hole needs the hole, +0x2e4; after a
    // rollback to a checkpoint taken while hidden there is none, and the mouse is there at
    // once, run l24_win.)
    if (was >= 0) {
        mouse_.slideRemainingMs = mouse_.slideDurationMs = 80;
        mouse_.slideTarget = mouse_.position;
        mouse_.slidePosition = mouse_.position;
        mouse_.slideDirection = 1;
    }
    // What it carries comes out with it (state 0x1d; running again, all but type 0x13; @0x123f6c).
    // A type 0x12 item does not: it is gone.
    const bool hadTrail = !carried_.empty();
    auto dropFromTrail = [&](auto eaten) {
        for (std::size_t i = 0; i < carried_.size();) {
            LevelObject& item = objects_[std::size_t(carried_[i])];
            if (!eaten(item.type)) {
                ++i;
                continue;
            }
            item.active = false;
            item.inTrail = false;
            carried_.erase(carried_.begin() + std::ptrdiff_t(i));
        }
    };
    for (int id : carried_) {
        LevelObject& item = objects_[std::size_t(id)];
        item.position = mouse_.position + Vec2{0.0f, 0.00499999989f};
        item.state = ObjectState(0x21);
        if (item.type != 0x13) item.updating = true;
    }
    dropFromTrail([](int type) { return type == 0x12; });
    if (was >= 0) {
        const int type = objects_[std::size_t(was)].type;
        if (isMunchHole(type)) {
            // (@0x123fe4, 1.1.1) A munch hole keeps what was carried in, all but types 0xb, 0xc
            // and 0x13 (when touched out, Object::getTouched has taken them already); the safe
            // holes close.
            dropFromTrail([](int t) { return t != 0xb && t != 0xc && t != 0x13; });
            closeBankHoles();
        }
        // (@0x124060) Out of a wall hole sound 0x33; of the other holes below 0x14, 0x18..0x1d
        // and 0x28..0x30, sound 0x28 (a munch hole is quiet).
        if (isWallHole(type)) sound(0x33);
        else if (type < 0x14 || (type >= 0x18 && type <= 0x1d) || (type >= 0x28 && type <= 0x30)) sound(0x28);
    }
    // (@0x12408c: coming out with an empty trail, or of no hole, closes the safe holes.)
    if (!hadTrail || was < 0) closeBankHoles();
}

// Object::getTouched @0x115d2c: the mouse comes out just in front of the hole, its own radius
// plus the hole's less 5 away from the centre on the open side.
// ObjControllerHideable::update @0x1b4716 for a flip-flap: the mouse hidden in a pipe of its tag
// comes out here, two radii out along its facing (204 up, 205 left, 206 down, 207 right in map
// space); the flap swings (frames 1-2, 250 ms) and sound 0x47.
void Session::updatePipeFlap(LevelObject& o, int dtMs) {
    if (mouse_.holeTag == o.tag && mouse_.state == 2) {
        Vec2 at = o.position;
        const float r = o.radius;
        switch (o.type) {
        case 204: at.y = at.y + (r + r); break;
        case 205: at.x = at.x + r * -2.0f; break;
        case 206: at.y = at.y + r * -2.0f; break;
        default: at.x = at.x + (r + r); break;
        }
        mouse_.position = at;
        o.anim.playOneShot(1, 2, 250);
        leaveHole();
        mouse_.holeTag = -1;
        sound(0x47);
    }
    o.anim.update(dtMs);
}

void Session::comeOutOf(LevelObject& hole) {
    const float d = hole.radius + mouse_.reachDistance + -5.0f;
    Vec2 offset{0.0f, -d};
    // (Munch holes, 0x168..0x16b, face the same ways as wall holes 20..23.)
    switch (isMunchHole(hole.type) ? hole.type - 0x154 : hole.type) {
    case 21: offset = {d, 0.0f}; break;
    case 22: offset = {0.0f, d}; break;
    case 23: offset = {-d, 0.0f}; break;
    default: break;
    }
    mouse_.position = hole.position + offset;
    if (isMunchHole(hole.type)) {
        // (@0x115e44, 1.1.1, run l2s_win) Out of a safe hole: what was carried in stays there,
        // all but types 0xb, 0xc and 0x13 (they leave the trail, their carried flag as it was).
        for (std::size_t i = 0; i < carried_.size();) {
            LevelObject& item = objects_[std::size_t(carried_[i])];
            if (item.type == 0xb || item.type == 0xc || item.type == 0x13) {
                ++i;
                continue;
            }
            item.active = false;
            carried_.erase(carried_.begin() + std::ptrdiff_t(i));
        }
    }
    leaveHole();
    mouse_.leftHole = hole.id;
}

// SceneGame::pinholeCallback @0xfc358 (the iris has shut): the mouse's floor shows; on a floor
// entered for the first time an unwon level says "Hidden Area!"; the cheese pulses, the cats
// start over, the iris opens on the mouse.
void Session::arriveOnFloor() {
    bool checkpointHere = false;
    doorTransition_ = false;
    progress_.clockRunning = false;
    if (shownFloor_ != mouse_.floor) {
        if (roomDoorTransition_) {
            // SceneGame::changeFloor @0xfc054 with the room door's flag: cheese still in the
            // trail (delivered already) leaves it and stays behind; the rest comes along.
            for (std::size_t i = 0; i < carried_.size();) {
                LevelObject& item = objects_[std::size_t(carried_[i])];
                if (isCheese(item.type)) {
                    item.inTrail = false;
                    carried_.erase(carried_.begin() + std::ptrdiff_t(i));
                    continue;
                }
                ++i;
            }
        }
        if (mouse_.hole >= 0) leaveHole();
        doorReadyAtMs_ = gameTimeMs_;  // (SceneGame::changeFloor @0xfc054: +0x13190, run l17_win)
        floorVisits_[std::size_t(shownFloor_)] = 2;
        shownFloor_ = mouse_.floor;
        for (Cat& cat : cats_)
            if (const LevelObject* o = objectById(cat.objectId()); o && o->floor != shownFloor_) {
                CatWorld world = catWorld(o->floor);
                cat.leaveFloor(world);
                catEffects(world, cat.objectId());
            }
        // SceneGame::changeFloor @0xfc054 (leaving by a door): what the mouse carries comes along.
        for (int id : carried_) {
            objects_[std::size_t(id)].floor = shownFloor_;
            objects_[std::size_t(id)].position = mouse_.position + Vec2{0.0f, 0.1f};
        }
        // Object::onFloorChanged @0x11208c (1.1.1): left behind on another floor, twine a cat
        // has had (state 0x22) and catnip still being eaten are gone (run l45_win).
        for (LevelObject& o : objects_) {
            if (!o.active || o.floor == shownFloor_) continue;
            if (o.type == kTwine ? o.state != ObjectState(0x22) : (o.type != kCatnip || o.timerMs < 1)) continue;
            if (o.type == kCatnip) o.timerMs = 0;
            o.updating = false;
            o.active = false;
        }
        // (@0xfc27c: only a plain door marks a floor's first visit; through a room door the
        // count stays 0, so its floor text never shows (run l3h2_win). The check also skips
        // the mode at SceneGame +0x13828 == 1, not reconstructed.)
        if (!roomDoorTransition_ && floorVisits_[std::size_t(shownFloor_)] == 0)
            floorVisits_[std::size_t(shownFloor_)] = 1;
        // (Through a room door, SceneGame::changeFloor takes the level's snapshot, after
        // resetCats: a retry starts from this floor; see below.)
        checkpointHere = roomDoorTransition_;
        // The floor is loaded afresh (SceneGame::load): its picture and its ambience, whose
        // player starts over (first delays drawn). Hypothesis: the reload happens on every
        // change (the per-floor "loaded" flag is not traced).
        std::vector<std::string> stops = ambience_.loops();
        for (const std::string& file : ambience_.sounding()) stops.push_back(file);
        for (const std::string& loop : stops) {
            SoundEvent e;
            e.kind = SoundEvent::Stop;
            e.file = loop;
            soundEvents_.push_back(e);
        }
        ambience_.start(level_.floor(shownFloor_).ambience, random_);
        // The objects' pictures are loaded afresh too (Object::loadAssets for every object):
        // a floating balloon starts its loop over (run l35_down); buttons show their base frame
        // and their timers frame 0 whatever their state (ObjControllerSwitch::loadAssets
        // @0x1ae4cc), safe holes and cheese their first frame (runs l37_win, l38_win). Other
        // restarted pictures are
        // not reconstructed.
        for (LevelObject& o : objects_) {
            if (!o.active) continue;
            if (o.type == kBalloon && o.state != ObjectState(0x22)) o.anim.setAnim(0, 3, SpriteAnim::Loop, 400);
            if (Switch* sw = switchFor(o.id)) {
                // (The cheese button, 150, shows its pressed frame when down.)
                const bool down = sw->type == 0x96 && sw->state == kSwitchDown;
                sw->anim.setAnim(sw->baseFrame() + (down ? 1 : 0), -1, SpriteAnim::Hold, 200);
                sw->timer.setAnim(0, 0, SpriteAnim::Hold, 200);
                o.anim = sw->anim;
            }
            if (isMunchHole(o.type)) o.anim.setAnim(0, -1, SpriteAnim::Hold, 200);
            if (isCheese(o.type)) o.anim.setAnim(0, -1, SpriteAnim::Hold, 200);  // (run l38_win)
            if (o.type == kTwine) o.anim.setAnim(0, -1, SpriteAnim::Hold, 200);   // (run l45_win)
            if (o.type == kCatnip && o.anim.frameCount() > 0)
                o.anim.setAnim(0, o.anim.frameCount() - 1, SpriteAnim::Once, 8200 / o.anim.frameCount());
        }
        for (const std::string& loop : ambience_.loops()) {
            SoundEvent e;
            e.file = loop;
            e.loop = true;
            soundEvents_.push_back(e);
        }
    }
    pulseCheeseAndTraps();
    if (floorVisits_[std::size_t(shownFloor_)] == 1 && !wonBefore_) {
        const data::StringTable* text = level_.strings();
        // Above the screen's middle by the heading font's line height (32: getFontLineHeight
        // of g_pFont[2], the 25-pixel font).
        const int slot = particles_.add(kTextBubble, shownFloor_, {kScreenWidth * 0.5f, kScreenHeight * 0.5f + 32.0f},
                                        text ? text->text(0xee) : std::string(), nullptr, 1000);
        if (Particle* p = particles_.at(slot)) p->life = 3200;  // (+0x1ec, 3.2 s on screen)
    }
    iris_.opening = true;
    iris_.closing = false;
    iris_.closed = false;
    iris_.centre = mouse_.position;
    iris_.fullRadius = kScreenWidth * irisScale(iris_.kind) + std::max(std::fabs(iris_.centre.x - kScreenWidth * 0.5f),
                                                                       std::fabs(iris_.centre.y - kScreenHeight * 0.5f));
    iris_.progress = 0.0f;
    iris_.speed = 0.000800000038f;  // (kind 1 opens as fast as it closed: shots of run l17_win)
    // resetCats @0xf80d0 (1.1.1): every cat, on any floor, made again (Object::reInit, init and
    // postInit: back on its start, its picture's facing forgotten), except the boss cat, kind
    // 0x3a and a cat in state 0x11 (1.0.5 only moved them back).
    for (Cat& cat : cats_) {
        if (cat.type() == 0x35 || cat.type() == 0x3a || cat.state() == 0x11) continue;
        cat.reset();
        if (cat.objectId() >= 0 && cat.objectId() < int(objects_.size())) {
            LevelObject& o = objects_[std::size_t(cat.objectId())];
            o.position = cat.position();
            o.state = ObjectState(cat.state());
            o.anim = cat.walk;
        }
    }
    // (The snapshot holds the reset cats with their pictures as they were, run l3h2_win; then
    // the floor's objects load their pictures afresh, Object::loadAssets.)
    if (checkpointHere) takeCheckpoint();
    for (Cat& cat : cats_) {
        if (cat.type() == 0x35 || cat.type() == 0x3a || cat.state() == 0x11) continue;
        cat.reloadPictures();
        if (cat.objectId() >= 0 && cat.objectId() < int(objects_.size())) objects_[std::size_t(cat.objectId())].anim = cat.walk;
    }
    requestExitOpen();
}

// SceneGame::pinholeCallback @0xfc4ae: every cheese and mouse trap pulses.
void Session::pulseCheeseAndTraps() {
    for (LevelObject& o : objects_)
        if (isCheese(o.type) || o.type == kMouseTrap) o.pulseMs = float(tuning_.objectPulseMs);
}

// SceneGame::pinholeCallback for the boot's iris event: the mouse is on its floor already, so
// only the pulse (a first visit's "Hidden Area!" needs a floor entered by a door; the
// callback's openPinholeOnPlayer and requestExitOpen repeat what the boot did).
void Session::irisShutOnBoot() {
    pulseCheeseAndTraps();
}

// Player::hide @0x1248d4 with Player::setState(2): the mouse goes into a hole.
void Session::hideIn(LevelObject& hole) {
    if (mouse_.state == 2) return;
    // (1.1.1) Cats after the mouse remember where it went in; the mouse turns to the hole.
    for (Cat& cat : cats_) cat.mouseHidAt(mouse_.position, catWorld(cat.floor()));
    mouse_.heading = (hole.position - mouse_.position).angleDegrees() * 0.0174532924f;
    mouse_.hole = hole.id;
    mouse_.state = 2;
    // (setState @0x1243bc: a worn teacup comes off and is gone, without its end sound.)
    if (mouse_.cup >= 0) {
        objects_[std::size_t(mouse_.cup)].active = false;
        mouse_.cup = -1;
    }
    mouse_.lastHole = hole.id;
    mouse_.lastHoleUntilMs = gameTimeMs_ + 100;
    // The mouse leaves the map; its drawn copy slides from where it was into the hole.
    mouse_.slideRemainingMs = mouse_.slideDurationMs = 200;
    mouse_.slideTarget = hole.position;
    mouse_.slidePosition = mouse_.position;
    mouse_.stateEntry = mouse_.position;
    mouse_.slideDirection = -1;
    mouse_.position = {-99999.0f, -99999.0f};
    path_.truncate(0);
    mouse_.slideTarget = mouse_.slideTarget + Vec2{1.5f, -1.0f};
    // Home holes keep no tag on the mouse; other holes remember theirs (multi-floor levels
    // move the hidden mouse 100 units per tag).
    mouse_.holeTag = isMouseHole(hole.type) || hole.type == 0x1C ? -1 : hole.tag;
    mouse_.position.y = mouse_.position.y - float(mouse_.holeTag * 100);
    if (isPipe(hole.type)) {
        ++pipeUses_;  // (Player::hide @0x124ab4: the pipe metric of the "Only N Pipes" goal)
        // Player::hide @0x124ece: the whole trail goes into a pipe with the mouse
        // (Object::hide: a balloon pops and leaves the trail).
        for (std::size_t i = 0; i < carried_.size();) {
            LevelObject& item = objects_[std::size_t(carried_[i])];
            if (hideObject(item, hole)) {
                carried_.erase(carried_.begin() + std::ptrdiff_t(i));
                continue;
            }
            ++i;
        }
    }
    // Player::hide (1.1.1): bringing the trail into any home hole, a safe hole or a room door
    // (0x21) plays the woosh; safe holes and room doors also the "cp_enter" sound.
    if (isHomeHole(hole.type) || isMunchHole(hole.type) || hole.type == kHouseDoor) sound(sfx::UiWoosh);
    if (isMunchHole(hole.type) || hole.type == kHouseDoor) sound(0x5e);
    if (isHomeHole(hole.type)) {
        // Player::hide @0x124c64: the exit sound, and munching when the trail brings
        // something (Player +0x8 >= 1, read as the trail's length: hypothesis). (1.1.1 counts
        // the secret exits 0xf4..0xf7 too.)
        sound(sfx::UseExit);
        if (!carried_.empty()) sound(sfx::EatMunch);
        for (int id : carried_)
            if (isCheese(objects_[id].type)) {
                ++progress_.delivered;
                ++progress_.deliveredKinds[std::size_t(objects_[id].type - 6)];
                ++deliveredOn_[std::size_t(shownFloor_)];
            }
    }
    if (isRoomDoor(hole.type)) {
        // Player::hide @0x124b90: going down a room door (0x21) counts the cheese carried as
        // delivered too (LevelState +0x24; not the floor's own count); the trail follows the
        // mouse in and is gone there (Object::hide).
        for (int id : carried_)
            if (isCheese(objects_[std::size_t(id)].type)) {
                ++progress_.delivered;
                ++progress_.deliveredKinds[std::size_t(objects_[std::size_t(id)].type - 6)];
            }
    }
    if (isWallHole(hole.type)) ++uses_.wallHoles;
    if (isMunchHole(hole.type)) {
        ++uses_.safeHoles;
        // Player::hide @0x1248d4 for a safe hole: the cheese carried is delivered and scored
        // (the banked line); the trail walks into the hole after the mouse and is gone there
        // (Object::hide).
        // (Player::setState(2) @0x124120 would close the safe holes when its +0x164 is set: not
        // seen, run l24_safe.)
        for (int id : carried_)
            if (isCheese(objects_[std::size_t(id)].type)) {
                ++progress_.delivered;
                ++progress_.deliveredKinds[std::size_t(objects_[std::size_t(id)].type - 6)];
                ++deliveredOn_[std::size_t(shownFloor_)];
            }
        scoreCheese(true);
        // (@0x124d70) Then the level is snapshotted for a retry ("Keep Going!", run lb2_win),
        // without the trail's items other than the toy mouse and kinds 0xc and 0x13 (they are
        // out of play while it is taken).
        std::vector<int> outOfPlay;
        const std::vector<int> trail = carried_;
        carried_.clear();
        for (int id : trail) {
            LevelObject& item = objects_[std::size_t(id)];
            if (item.type == kClockworkMouse || item.type == 0xC || item.type == 0x13) {
                carried_.push_back(id);
            } else if (item.active) {
                item.active = false;
                outOfPlay.push_back(id);
            }
        }
        takeCheckpoint();
        for (int id : outOfPlay) objects_[std::size_t(id)].active = true;
        carried_ = trail;
    }
    // The HUD's hand starts over in home holes (and munch holes, type 0x21; Player::hide
    // @0x124c10).
    if (isHomeHole(hole.type) || (hole.type >= 0x168 && hole.type <= 0x16B) || isRoomDoor(hole.type))
        hud_.hand = PokerHand::Bluff;
}

// Object::hide @0x112e20: an item enters a hole. Returns whether it is gone.
bool Session::hideObject(LevelObject& object, const LevelObject& hole) {
    if (object.type == kBalloon) {
        // (@0x112e7e: a balloon does not fit into any hole: it pops where it is (state 0x1e,
        // running) and leaves the trail; run l211_win.)
        object.state = ObjectState(0x22);
        object.updating = true;
        object.pulseMs = float(tuning_.objectPulseMs);
        return true;
    }
    object.state = ObjectState::Hidden;
    bool gone = false;
    // Cheese is gone into safe holes, home holes and room doors; a clockwork mouse, twine and
    // catnip only into home holes (@0x112e50; run l45_win: catnip carried home).
    const bool lureLike = object.type == kClockworkMouse || object.type == kTwine || object.type == kCatnip;
    if ((isCheese(object.type) && (isBankHole(hole.type) || isMouseHole(hole.type) || isRoomDoor(hole.type))) ||
        (lureLike && isMouseHole(hole.type))) {
        object.active = false;
        object.inTrail = false;
        gone = true;
    }
    object.pulseMs = float(tuning_.objectPulseMs);
    return gone;
}

// Object::update @0x118dd0 and the controllers' update for the reconstructed families.
// Object::updatePositions @0x115430 ends a pickup's update, carried or not: walls push it out,
// and it takes the pushed place (run l45_win: carried twine). (1.1.1 @0x115494) Some kinds
// are never pushed: the toy mouse (its arc onto a trap passes over walls, run lb1_trap), the
// ghost cat, kinds 0x109/0x10a, 0x12d, 0x12f, 0x14a, 0x165..0x167 and 0x1a4/0x1a5; nor is
// anything in states 0x24 and 0x40. (1.0.5 pushed the toy mouse too, and kept the old place
// for some wall kinds.)
void Session::pushPickupOutOfWalls(LevelObject& o) {
    switch (o.type) {
    case kClockworkMouse: case 0x37: case 0x109: case 0x10a: case 0x12d: case 0x12f: case 0x14a:
    case 0x165: case 0x166: case 0x167: case 0x1a4: case 0x1a5:
        return;
    default:
        break;
    }
    if (o.state == ObjectState(0x24) || o.state == ObjectState(0x40)) return;
    wallsOf(o.floor).collideSphere(o.position, o.radius, viewLeft_, nullptr, kMovementIgnoredWalls);
}

void Session::updateObject(LevelObject& o, int dtMs) {
    if (o.type == kDossier) {
        // ObjControllerPickups::update @0x1b78f0 (1.1.1): touched, it waits, then it is gone, the
        // profile has it and its page opens over the level (Engine::pushScene("dossier")).
        if (o.timerMs > 0) {
            o.timerMs -= dtMs;
            if (o.timerMs < 1) {
                o.active = false;
                o.timerMs = 1500;
                const int id = o.tag;
                if (id >= 0 && id < int(dossiersFound_.size()) && !dossiersFound_[std::size_t(id)]) {
                    dossiersFound_[std::size_t(id)] = true;
                }
                foundDossier_ = id;
                int found = 0;
                for (bool f : dossiersFound_) found += f ? 1 : 0;
                dossier_.open(id, false, found);
                // showScene: the HUD goes, the pause button hides.
                hud_.retractAll(0);
                hud_.pauseHidden = true;
            }
        }
        return;
    }
    if (o.type == kSpitter || o.type == kRailTarget || o.type == kBeamEmitter) {
        updateSpitterRoomObject(o, dtMs);
        return;
    }
    if (o.type == kVacuum) {
        updateVacuumRoomObject(o, dtMs);
        return;
    }

    if (Droid* droid = droidFor(o.id)) {
        // ObjControllerBoss: in a cutscene the droid only animates (Object::update); else it
        // runs its own update.
        if (!o.updating) return;
        if (droid->state() == 0x40) {
            droid->updateAnimations(dtMs);
        } else {
            BossWorld world = bossWorld(o.floor);
            droid->update(dtMs, world);
        }
        if (droid->winRequested) {
            droid->winRequested = false;
            fireEvent("boss_win");
        }
        o.position = droid->position();
        o.state = ObjectState(droid->state());
        o.anim = droid->body;
        return;
    }
    if (o.pulseMs > 0.0f) {
        o.pulseMs = o.pulseMs - float(dtMs);
        if (o.pulseMs <= 0.0f) {
            o.introPulse = false;
            o.pulseMs = 0.0f;
        }
    }
    // (The pulse runs down for every object first, Object::update @0x118e4a.)
    if (isPipeFlap(o.type)) {
        updatePipeFlap(o, dtMs);
        return;
    }
    if (Switch* s = switchFor(o.id)) {
        updateSwitch(o, *s, dtMs);
        return;
    }
    if (Door* d = doorFor(o.id)) {
        if (o.updating) updateDoor(o, *d, dtMs);
        return;
    }
    if (o.type == kTwine) {
        // ObjControllerPickups::update @0x1b77ac: off the trail, while its roll time runs the
        // ball turns a frame every 128 ms of its clock, starting over after the last; then, in
        // the trail or not, walls push it (the update's common end). Only a running object
        // updates (a ball pulled into a hole out of sight has stopped, run l45_win).
        if (!o.updating) return;
        if (!o.inTrail) {
            o.rollMs = std::max(0, o.rollMs - dtMs);
            if (o.rollMs > 0) {
                o.rollClockMs += dtMs;
                int frame = o.rollClockMs >> 7;
                if (frame >= o.anim.frameCount()) frame = o.rollClockMs = 0;
                o.anim.setFrame(frame);
            }
        }
        pushPickupOutOfWalls(o);
        return;
    }
    if (o.type == kCatnip) {
        // @0x1b7b0c: once a cat eats it (let go, 0x1e, with its time running) it fades: at the
        // end of the time its last frame shows (0x21) for its fade time (+0x1d8, 2 s), then it
        // is gone. Walls push it at the end of the update.
        if (!o.updating) return;
        // Its picture runs while it does (12 frames over the time; after the eating cat's own
        // step on the first tick, run l4h1_win).
        o.anim.update(dtMs);
        o.timerMs -= dtMs;
        if (o.state != ObjectState(0x25)) {
            if (o.state == ObjectState(0x22) && o.timerMs < 1) {
                o.anim.setAnim(o.anim.frameCount() - 1, -1, SpriteAnim::Hold, 200);
                o.state = ObjectState(0x25);
                o.timerMs = o.playMs;
            } else {
                pushPickupOutOfWalls(o);
                return;
            }
        }
        if (o.timerMs < 1) {
            o.timerMs = 0;
            o.updating = false;
            o.active = false;
            return;
        }
        pushPickupOutOfWalls(o);
        return;
    }
    if (o.type == kCheeseMachine) {
        // Object::update @0x11903a: once the picture has run while handing out, the first cheese
        // it still holds comes out (loose, state 0; it forgets the machine with the last one),
        // and the machine goes idle.
        o.anim.update(dtMs);
        if (!o.anim.finished() || o.state != ObjectState(kMachineDispensing) || o.machineReleases < 1) return;
        --o.machineReleases;
        for (LevelObject& c : objects_) {
            if (!c.active || c.type != 10 || c.heldBy != o.tag || c.state != ObjectState(kHeldInMachine)) continue;
            if (o.machineCount == 0) c.heldBy = -1;
            c.state = ObjectState::Idle;
            break;
        }
        o.state = ObjectState(kMachineIdle);
        o.anim.setAnim(0, -1, SpriteAnim::Hold, 200);
        return;
    }
    if (isRoomDoor(o.type)) {
        // ObjControllerFloorTransport::update @0x1b21b0: the picture runs (its updateAnimations,
        // through the vtable), the arrow's time after opening runs down (the original holds it
        // while the pinhole closes), and updateAnimations runs once more after: the picture
        // goes at twice its rate (run l35_down: 150 ms frames last 4..5 ticks).
        o.anim.update(dtMs);
        if (o.timerMs > 0) o.timerMs -= dtMs;
        o.anim.update(dtMs);
        return;
    }
    // (A clockwork mouse runs once picked up; a cat that stopped updating, the boss cat out of
    // the view, keeps its picture.)
    // ObjControllerHideable::updateAnimations @0x1b4a4c: a looping picture rests 2.5 s on its
    // first frame and shows the others for 100 ms each (run l28_watch).
    if (o.type == kCouch && o.anim.mode() == SpriteAnim::Loop) o.anim.setFrameMs(o.anim.frame() != 0 ? 100 : 2500);
    // (A balloon held at a wall on the way into a hole keeps its frame too, run l36_win.)
    if ((o.type != kClockworkMouse && o.type != kBossCat && o.type != kBalloon) || o.updating) o.anim.update(dtMs);
    if (!o.updating || o.type < 1) return;
    if (o.type == kBomb) {
        updateVacuumRoomObject(o, dtMs);
        return;
    }
    if (o.type == kLauncher || o.type == kReflector || o.type == kBreakable) {
        updateTurretRoomObject(o, dtMs);
        return;
    }
    if (o.type == kTv) {
        // Object::update @0x119264: switched on it plays frames 1-4 (400 ms each, but its
        // picture is run here as well as above: 200 ms, run l22_tvon) until its time is up
        // (never with the 60001 it gets), then goes off and lets its seats go.
        if (o.state == ObjectState(kTvOff)) {
            o.state = ObjectState(kTvOn);
            o.anim.setAnim(1, 4, SpriteAnim::Loop, 400);
        }
        if (o.timerMs < 60001) o.timerMs -= dtMs;
        o.anim.update(dtMs);
        if (o.timerMs > 0) return;
        o.anim.setAnim(0, 0, SpriteAnim::Hold, 200);
        o.timerMs = 0;
        o.state = ObjectState(kTvOff);
        o.updating = false;
        o.watchers.fill(0);
        return;
    }
    if (o.type == kKey && o.flying) {
        // ObjControllerPickups::update @0x1b74da: the thrown key arcs to the marker (8 ms a
        // unit, height factor 1); there it lies (sound 3).
        if (o.arcTimeMs == 0) o.arcStart = o.position;
        if (const LevelObject* marker = firstObjectOfType(kKeyMarker)) {
            if (travelArc(o.arcStart, marker->position, o.position, o.arcTimeMs, 1.0f, 8.0f)) {
                o.flying = false;
                sound(3);
            }
        }
        return;
    }
    if (o.type == kMouseTrap) {
        // Object::update @0x118dd4: sprung, it fades out over 3 s; armed, a mouse on its floor
        // with a clockwork mouse in its trail within "player_windupmouse_throw_dist" (50) and a
        // clear line springs it.
        if (o.state == ObjectState(0x1e)) {
            o.timerMs -= dtMs;
            if (o.timerMs < 0) {
                o.timerMs = 0;
                o.active = false;
                o.updating = false;
            }
        } else if (o.state == ObjectState(0x1d) && mouse_.floor == o.floor) {
            bool hasClockwork = false;
            for (int id : carried_) hasClockwork = hasClockwork || objects_[std::size_t(id)].type == kClockworkMouse;
            if (hasClockwork && (mouse_.position - o.position).lengthSquared() < 50.0f * 50.0f &&
                !wallsOf(o.floor).lineTouches(o.position, mouse_.position, viewLeft_, -1))
                touchTrap(o, false);
        }
        return;
    }
    if (o.type == kBalloon) {
        // ObjControllerPickups::update @0x1b7d12: popped (0x1e), the pop (sound 4, frames 4-6
        // at 125 ms), gone on its last frame; its picture runs here as well as above (two
        // steps a tick, run l211_watch).
        if (o.state == ObjectState(0x22)) {
            if (o.anim.frame() <= 3) {
                sound(4);
                o.anim.playOneShot(4, 6, 125);
            } else if (o.anim.frame() == 6) {
                o.active = false;
            }
        }
        o.anim.update(dtMs);
        return;
    }
    if (o.type == kTeacup) {
        // (Running only once worn.)
        updateTeacup(o, dtMs);
        return;
    }
    if (o.type == kClockworkMouse) {
        // ObjControllerPickups::update: carried, it faces what it follows (the mouse or the item
        // in front); thrown, it flies onto the trap (4 ms a unit) and is gone.
        if (o.inTrail) {
            Vec2 ahead = mouse_.position;
            for (int id : carried_) {
                if (id == o.id) break;
                ahead = objects_[std::size_t(id)].position;
            }
            Vec2 d = ahead - o.position;
            d.normalise(1.0f);
            o.rotation = float(std::atan2(double(d.y), double(d.x))) * 57.2957802f * 0.0174532924f;
        }
        if (!o.inTrail && o.state == ObjectState(0x22) &&
            travelArc(o.arcStart, o.arcTarget, o.position, o.arcTimeMs, 1.0f, 4.0f)) {
            o.state = ObjectState::Idle;
            o.active = false;
        }
        if (o.active) pushPickupOutOfWalls(o);
        return;
    }
    if (isMunchHole(o.type)) {
        // ObjControllerHideable::update @0x1b4354: once per message, the safe hole's flags and
        // its picture (opening 0..last, or closing back to 0, 100 ms a frame).
        o.updating = false;
        const int last = o.frames - 1;
        if (o.state == ObjectState::HoleClosed) {
            o.passable = o.snappable = false;
            if (o.anim.frame() != last) o.anim.setAnim(0, 0, SpriteAnim::Once, 100);
            else o.anim.setAnim(last, 0, SpriteAnim::Once, 100);
        } else if (o.state == ObjectState::HoleOpen) {
            o.passable = o.snappable = true;
            if (o.anim.frame() == last) o.anim.setAnim(last, last, SpriteAnim::Once, 100);
            else o.anim.setAnim(0, last, SpriteAnim::Once, 100);
        }
        return;
    }
    if (isHomeHole(o.type)) {
        // ObjControllerHideable::update @0x1b4354.
        if (o.timerMs > 0) o.timerMs -= dtMs;
        if (progress_.over && progress_.won && o.state == ObjectState::HoleOpen && carried_.empty()) {
            // Everything is in: the hole closes behind the mouse.
            wallsOf(o.floor).setTagEnabled(o.tag, true);
            closeHole(o);
            closeBankHoles();  // (SceneGame::closeBankHoles, @0x1b44fc)
        }
        if (o.state == ObjectState::HoleClosed && o.tag != 0) {
            // A closed hole's door is a one-way wall: solid only from the side the mouse is on.
            if (Wall* door = wallsOf(o.floor).firstWithTag(o.tag)) {
                Vec2 along = door->b - door->a;
                Vec2 normal{-along.y, along.x};
                door->enabled = (door->a - mouse_.position).dot(normal) >= 0.0f;
            }
        }
    } else if (o.type == kExitDoor) {
        // Object::update @0x119cb2: a closed exit door opens again (once) when the view has
        // reached the end of the level and the boss cat, still updating, touches it.
        if (o.state == ObjectState::HoleClosed && o.timerMs == -1) {
            const float width = level_.floor(o.floor).width;
            const float end = float(int(width - kScreenWidth));
            const LevelObject* cat = nullptr;
            for (const LevelObject& c : objects_)
                if (c.type == kBossCat) {
                    cat = &c;
                    break;
                }
            if (viewLeft_ == end && cat != nullptr && cat->updating &&
                (cat->radius + o.radius) - (cat->position - o.position).length() > 0.0f) {
                sendExitDoorMessage(o, 1);
                o.timerMs = 1;
            }
        }
    } else if (isRoamer(o.type)) {
        for (Roamer& r : roamers_)
            if (r.objectId() == o.id) {
                r.update(dtMs, wallsOf(o.floor), viewLeft_, random_);
                o.position = r.position();
                o.anim = r.anim;
            }
    } else if (Cat* cat = catFor(o.id)) {
        CatWorld world = catWorld(o.floor);
        cat->update(dtMs, world);
        if (!std::isnan(world.scrollSpeed)) scrollSpeed_ = world.scrollSpeed;
        catEffects(world, cat->objectId());
        o.position = cat->position();
        o.radius = cat->radius();
        o.state = ObjectState(cat->state());
        o.anim = cat->walk;  // Object +0x38; drawing picks the active picture (Cat::activeSprite)
        // The boss cat at the end of its route stops (ObjControllerCat::patrol @0x1a40e4 clears
        // Object +5).
        if (cat->stoppedForGood()) o.updating = false;
    } else if (isCheese(o.type)) {
        // ObjControllerPickups::update @0x1b73a0.
        if (o.pulseDelayMs > 0) o.pulseDelayMs -= dtMs;
        if (o.snapCooldownMs >= 1) o.snapCooldownMs -= dtMs;
        o.idleSparkleMs += dtMs;
        if (o.timerMs >= 0) {
            // A burst of sparkles a moment after a pickup.
            o.timerMs -= dtMs;
            if (o.timerMs <= 0) {
                o.timerMs = -2;
                o.sparkle.playOneShot(0, o.sparkle.frameCount(), 75);
                const int count = int(random_.within(0.0f, 4.0f)) + 4;
                int last = -1;
                for (int i = 0; i < count;) {
                    if (last >= 17 && i > 3) break;
                    ++i;
                    last = particles_.add(0x1f, o.floor, o.position, {}, nullptr, 0, {-1.0f, -1.0f}, true);
                }
            }
        } else if (o.timerMs == -2) {
            o.sparkle.update(dtMs);
            if (o.sparkle.mode() != SpriteAnim::Once) o.timerMs = -1;
        }
    } else if (o.type == 601) {
        // Object::update @0x118dd0, ambient trigger: after its delay (plus a random part drawn
        // when the delay has passed) it plays the map decoration with its tag.
        if (o.triggerTimerMs >= 0) o.triggerTimerMs += dtMs;
        if (o.triggerTimerMs > o.triggerDelayMs) {
            bool play = o.triggerSpreadMs < 1;
            if (!play) {
                if (o.triggerExtraMs == -1) o.triggerExtraMs = random_.next() % o.triggerSpreadMs;
                else play = o.triggerTimerMs > o.triggerDelayMs + o.triggerExtraMs;
            }
            if (play) {
                playFluff(o.tag);
                o.triggerTimerMs = 0;
                o.triggerExtraMs = -1;
            }
        }
    }
}

// Player::kill @0x12079c (by a cat): the mouse is gone from play, its last carried item drops,
// the death sound plays and the music stops; the fight cloud is where the two met
// (SceneGame::setFinalPos @0x102ba8, by the cat after the player's own position).
void Session::killMouse(Vec2 meetingPoint) {
    if (mouseCaught_) return;
    mouseCaught_ = true;
    finalPosition_ = mouse_.position;
    // The last item of the trail drops off where it is (Player +0x170, kept from being picked
    // up again for 500 ms by Player +0x214; no drop sound, the mouse is already gone).
    if (!carried_.empty() && mouse_.position.x >= 0.0f) {
        objects_[std::size_t(carried_.back())].inTrail = false;
        droppedItem_ = carried_.back();
        dropGuardMs_ = 500;
        carried_.pop_back();
    }
    sound(sfx::MouseDies);
    SoundEvent stop;
    stop.kind = SoundEvent::MusicStop;
    soundEvents_.push_back(stop);
    musicPlaying_ = false;
    finalPosition_ = meetingPoint;
}

void Session::prepareFrame() {
    mouse_.normaliseHeadingForFrame();  // Player::render @0x122932
    for (Droid& droid : droids_) {
        droid.prepareFrame();
        if (LevelObject* o = objectById(droid.objectId())) o->anim = droid.body;
    }
    // Object::render draws only what is on the shown floor.
    for (Cat& cat : cats_)
        if (const LevelObject* o = objectById(cat.objectId()); o && o->floor == shownFloor_) cat.prepareFrame();
    // runLevelTutorialScript (drawn every frame): map 705 notes when the first cat takes aim
    // (state 5) and on which side of it the mouse is.
    if (level_.info().map == 705 && tutorialAimMs_ < 0 && !cats_.empty() && cats_.front().state() == 5) {
        tutorialAimMs_ = tutorialMs_;
        tutorialSide_ = (mouse_.position - cats_.front().position()).x < 0.0f ? -1 : 1;
    }
}

// What a cat sees of the level and the mouse.
CatWorld Session::catWorld(int floor) {
    // (The scroll speed stays NaN, "not touched": a cat other than the boss must not stop the
    // view, run lb2_explore.)
    CatWorld world{floorWalls_[std::size_t(floor)], random_, particles_, gameTimeMs_, viewLeft_, level_.infinite()};
    world.cats = &cats_;
    world.projectiles = &projectiles_;
    for (LevelObject& o : objects_)
        if (o.type == kTv) world.playthings.push_back(&o);
    world.mouse.position = mouse_.position;
    world.mouse.velocity = mouse_.velocity;
    world.mouse.state = mouse_.state;
    world.mouse.floor = mouse_.floor;
    world.mouse.active = !mouseCaught_;
    world.mouse.trailLength = int(carried_.size());
    world.mouse.carriesSomething = mouse_.cup >= 0;
    world.objects = &objects_;
    for (int id : carried_) {
        LevelObject& item = objects_[std::size_t(id)];
        world.trail.push_back(&item);
        if (item.type == kTwine || item.type == kCatnip) world.mouse.carriesLure = true;
    }
    world.dropFromTrail = [this](int id) { dropFromTrail(id); };
    return world;
}

// What a cat did: its sounds, and the level's "spotted" flag.
void Session::catEffects(const CatWorld& world, int objectId) {
    for (const SoundEvent& e : world.sounds) {
        // LoopingSoundManager::start/stop @0x1f0000/0x1db510: a looping sound (the purr) is one
        // voice shared by the objects that hold it; it starts with the first and stops with
        // the last. Objects of a won level start none (the manager keeps their ids,
        // LoopingSoundManager::start @0x1f0000).
        if (e.effect == 0x1d && (e.kind == SoundEvent::Stop || e.loop)) {
            if (loopsSilenced_) continue;
            std::set<int>& holders = loopHolders_[e.effect];
            if (e.kind == SoundEvent::Stop) {
                if (holders.erase(objectId) && holders.empty()) soundEvents_.push_back(e);
            } else {
                if (holders.empty()) soundEvents_.push_back(e);
                holders.insert(objectId);
            }
            continue;
        }
        soundEvents_.push_back(e);
    }
    // (updateSpecific: a cat took the trail's last cheese-free lure: the safe holes shut.)
    if (world.closeBankHoles) closeBankHoles();
    if (world.meanCatSpotted) {
        // Player::spottedByMeanCatOMG @0x123db8: unless it carries twine (0xc) or catnip
        // (0x13), the mouse plays its "surprised" sequence facing the cat (8 facings), once.
        bool calm = false;
        for (int id : carried_)
            if (objects_[std::size_t(id)].type == 0xc || objects_[std::size_t(id)].type == 0x13) calm = true;
        const int surprised = mouse_.sequenceIds[Mouse::kSurprised];
        if (!calm && mouse_.sprite.currentSequence() != surprised)
            if (const Cat* cat = catFor(objectId)) {
                const Vec2 d = cat->position() - mouse_.position;
                float a = float(std::atan2(double(d.y), double(d.x))) * 57.2957764f * 0.0174532924f;
                a = std::fmod(a, 6.28318548f);
                if (a < 0.0f) a = a + 6.28318548f;
                mouse_.sprite.playSequence(surprised, SpriteAnim::SequenceOnce, spriteDirection(a, 8));
                mouse_.idleStep = 1;
            }
    }
    if (world.bossSawMouse) {
        // ObjControllerCat::awarenessCheck @0x1a5a22: seen by the boss cat the level is lost at
        // once: "<spotted>" (string 0x268) over the mouse, half the heading font's line height
        // (32) up, the results in 2 s (SceneGame +0x13890), and the music pauses after the cat's
        // sounds.
        progress_.over = true;
        progress_.won = false;
        const data::StringTable* text = level_.strings();
        particles_.add(kTextBubble, mouse_.floor, mouse_.position + Vec2{0.0f, 16.0f}, text ? text->text(0x270) : std::string(),
                       nullptr, 0, {-1.0f, -1.0f}, true);
        spottedDelayMs_ = 2000;
        SoundEvent pause;
        pause.kind = SoundEvent::MusicStop;
        soundEvents_.push_back(pause);
        musicPlaying_ = false;
    }
    if (world.spottedMouse) progress_.spotted = true;
}

// Map::playFluffAnimation @0x13dd34.
void Session::playFluff(int type) {
    for (std::size_t i = 0; i < fluff_.size(); ++i) {
        const FluffItem& f = level_.fluff()[i];
        if (f.type != type) continue;
        // Map::fluffAnimationStartedCallback @0x13dcb0: bushes rustle audibly.
        if (f.type == 0x23C) sound(sfx::Bush, SoundEvent::PlayOnce);
        fluff_[i].anim.setAnim(0, fluff_[i].anim.frameCount() - 1, f.kind.mode, f.kind.frameMs);
    }
}

// Map::update @0x13dbd0: decorations animate; a bush that has finished rustling lets a bug out
// (Map::fluffAnimationJustFinishedCallback @0x13da8c).
void Session::updateFluff(int dtMs) {
    for (std::size_t i = 0; i < fluff_.size(); ++i) {
        SpriteAnim& anim = fluff_[i].anim;
        const bool wasFinished = anim.finished();
        anim.update(dtMs);
        if (anim.finished() && !wasFinished && level_.fluff()[i].type == 0x23C)
            particles_.add(0x31, 0, level_.fluff()[i].position, {}, nullptr, 0, {-1.0f, -1.0f}, true);
    }
}

// --- holes and doors --------------------------------------------------------------------

// SceneGame::requestExitOpen @0xf88a0: home holes open once the cheese delivered plus the
// cheese carried covers the level's total.
void Session::requestExitOpen() {
    if (level_.info().mode == "boss") {
        // In a boss fight the key in the trail opens the way out (message 1 to the boss exit,
        // type 0x166; home holes 0xf0..0xf7 would hear it too, map 791 has none).
        bool key = false;
        for (int id : carried_)
            if (objects_[std::size_t(id)].type == kKey) key = true;
        if (!key) return;
        for (LevelObject& o : objects_)
            if (o.type == kBossExitDoor) sendExitDoorMessage(o, 1);
        return;
    }
    const int carried = carriedCheese();
    if (progress_.delivered + carried < progress_.cheeseTotal) {
        // Not enough for the home holes: the room doors and trapdoors (type 0x21) open once the
        // cheese of the shown floor is delivered or carried, else they shut (message 2; the
        // home holes hear a message 2 too, not reconstructed: they are shut by then).
        const int type = deliveredOn_[std::size_t(shownFloor_)] + carried < floorCheese_[std::size_t(shownFloor_)] ? 2 : 1;
        GameMessage m;
        m.type = type;
        m.targetType = 0x21;
        sendMessage(m, false);
        return;
    }
    for (LevelObject& o : objects_)
        if (isHomeHole(o.type) && o.floor == mouse_.floor) openHole(o);
}

// ObjControllerHideable::receiveMessage @0x1b5008, "open".
void Session::openHole(LevelObject& hole) {
    if (hole.tag != 0) wallsOf(hole.floor).setTagEnabled(hole.tag, false);
    if (hole.state == ObjectState::HoleClosed) {
        hole.timerMs = 5000;
        sound(sfx::ExitReady);
        hole.anim.setAnim(0, 3, SpriteAnim::Once, 100);
        hole.state = ObjectState::HoleOpen;
        hole.passable = hole.snappable = true;
    }
}

// ObjControllerHideable::receiveMessage @0x1b5008 for safe holes (0x168..0x16b; sent by
// SceneGame::openBankHoles/closeBankHoles @0xf8c1c/@0xf8bc0, so the shown floor's only): message 1
// opens a closed one (its tag's walls off, state 0x26, sound 0x5f); message 2 closes an open
// one unless the mouse is inside it (sound 0x5d). Either way it runs its update once.
void Session::openBankHoles() {
    for (LevelObject& o : objects_) {
        if (!o.active || !isMunchHole(o.type) || o.floor != shownFloor_ || o.state == ObjectState::HoleOpen) continue;
        o.updating = true;
        if (o.tag != 0) wallsOf(mouse_.floor).setTagEnabled(o.tag, false);
        o.state = ObjectState::HoleOpen;
        sound(0x5f);
    }
}

void Session::closeBankHoles() {
    for (LevelObject& o : objects_) {
        if (!o.active || !isMunchHole(o.type) || o.floor != shownFloor_ || o.state == ObjectState::HoleClosed) continue;
        o.updating = true;
        if (mouse_.hole != o.id) o.state = ObjectState::HoleClosed;
        sound(0x5d);
    }
}

// ObjControllerHideable::receiveMessage, "close".
void Session::closeHole(LevelObject& hole) {
    if (hole.state != ObjectState::HoleOpen) return;
    hole.anim.setAnim(3, 0, SpriteAnim::Once, 100);
    hole.state = ObjectState::HoleClosed;
    hole.passable = hole.snappable = false;
}

// Player::playerPredictivePathUpdate @0x123bd4: counts the cheese the path has snapped onto
// and opens the home hole's door early when that will be enough.
void Session::updatePrediction() {
    const int carried = carriedCheese();
    // The cheese of the mouse's floor, less what the floors before it hold (carried along).
    // (A single floor keeps the level's total, which in a chase also counts the bonus pickups;
    // the original's floor count does not: hypothesis that this never differs there.)
    int need = progress_.cheeseTotal;
    int previousFloors = 0;
    if (level_.floorCount() > 1 && mouse_.floor < int(floorCheese_.size())) {
        need = floorCheese_[std::size_t(mouse_.floor)];
        for (int f = 0; f < mouse_.floor; ++f) previousFloors += floorCheese_[std::size_t(f)];
    }
    if (path_.drawing && path_.snapTarget >= 0 && prediction_.lastTarget != path_.snapTarget) {
        const LevelObject& target = objects_[path_.snapTarget];
        if (isCheese(target.type)) {
            ++prediction_.cheeseAhead;
            prediction_.snappedToCheese = true;
            const int ahead = prediction_.cheeseAhead < carryCapacity_ ? prediction_.cheeseAhead : carryCapacity_;
            prediction_.enoughCheese = need <= progress_.delivered + (carried - previousFloors) + ahead;
            if (carryCapacity_ < prediction_.cheeseAhead + carried) prediction_.enoughCheese = false;
        } else if (target.type == 0x14A) {
            prediction_.enoughCheese = true;
        }
        prediction_.lastTarget = path_.snapTarget;
    }
    for (const LevelObject& o : objects_) {
        if (o.floor != mouse_.floor || o.tag == 0) continue;
        if (isBankHole(o.type)) {
            wallsOf(o.floor).setTagEnabled(o.tag, mouse_.holeTag == -1 && !prediction_.snappedToCheese && carried < 1);
        } else if (isHomeHole(o.type)) {
            if (prediction_.enoughCheese || progress_.delivered + carried >= need)
                wallsOf(o.floor).setTagEnabled(o.tag, false);
        }
    }
}

// --- score and the level clock ----------------------------------------------------------

// Player::scoreCurrentTrail @0x120e40: points of the carried cheese, times the poker-hand
// bonus, times the home-hole bonus.
int Session::scoreTrail(bool intoHome, PokerHand& hand) const {
    int counts[kMaxCheeseTypes] = {};
    for (int id : carried_)
        if (isCheese(objects_[id].type)) ++counts[objects_[id].type - 6];
    int sum = 0;
    for (int i = 0; i < kMaxCheeseTypes; ++i) sum += counts[i] * scoring_.cheesePoints[i];
    // The original also walks the trail checking whether a straight is in order, but never
    // uses the result: any five different cheeses score as a straight.
    hand = pokerHand(counts);
    int score = int(float(sum) * scoring_.handBonus[int(hand)]);
    if (intoHome) score = int(float(score) * scoring_.exitBonusMultiplier);
    return score;
}

// Player::scoreCheese @0x12131c.
void Session::scoreCheese(bool bankHole) {
    const LevelObject* hole = objectById(mouse_.hole);
    const bool intoHome = hole && isHomeHole(hole->type);
    hud_.exitBonus = intoHome;
    if (intoHome) hud_.bounceHand();
    PokerHand hand = PokerHand::Bluff;
    const int score = scoreTrail(intoHome, hand);
    progress_.lastHand = hand;
    // The hand's metric for the combo goals (scoreCurrentTrail @0x120f84: hands 1..7).
    if (int(hand) >= 1 && int(hand) <= 7) ++handsScored_[std::size_t(hand)];
    if (score > 0) {
        progress_.lastScore = score;
        progress_.lastScoreShowMs = 1000;  // (1.0.5: 1500)
    }
    hud_.show();
    hud_.handBounce = -1.0f;
    progress_.score += score;
    const Vec2 centre{240.0f, 160.0f};
    const data::StringTable* text = level_.strings();
    if (bankHole) {
        // A bank hole: its line of text in the middle after 200 ms (1.1.1: the woosh is
        // Player::hide's now).
        particles_.add(kTextMission, mouse_.floor, centre, text ? text->text(0x16a) : std::string(), nullptr, 200,
                       {-1.0f, -1.0f}, true);
    } else if (progress_.won) {
        // The banner: "Accomplished!" in the middle of the screen and "Mission" above it, as
        // text particles after 200 ms (Player::scoreCheese @0x1214e0).
        particles_.add(kTextMission, mouse_.floor, centre, text ? text->text(0x169) : std::string(), nullptr, 200, {-1.0f, -1.0f},
                       true);
        particles_.add(kTextMission, mouse_.floor, centre + Vec2{0.0f, 30.0f}, text ? text->text(0x168) : std::string(), nullptr,
                       200, {-1.0f, -1.0f}, true);
    }
    if (progress_.won) {
        // SceneGame::onLevelWon @0xf85b4 (called just before): every object's looping sounds
        // stop (Object::onLevelWon, LoopingSoundManager::onLevelWon @0x1f0184; the cats' purr,
        // run l21_win).
        loopsSilenced_ = true;
        for (auto& [effect, holders] : loopHolders_) {
            if (holders.empty()) continue;
            holders.clear();
            SoundEvent stop;
            stop.kind = SoundEvent::Stop;
            stop.effect = effect;
            soundEvents_.push_back(stop);
        }
        // The music stops for "<track>_Win.m4a", played once (@0x1215fc..0x10c3d8).
        SoundEvent e;
        e.kind = SoundEvent::Music;
        e.file = music_.substr(0, music_.find('.')) + "_Win.m4a";
        soundEvents_.push_back(e);
        musicPlaying_ = true;
        // SceneGame::onLevelWon: banner, clock stopped; the HUD leaves as the banner ends
        // (1.1.1; 1.0.5: a second before).
        progress_.bannerMs = tuning_.winBannerMs;
        progress_.clockHeld = true;
        hud_.retractAll(progress_.bannerMs);
    }
}

// Player::updateScrollingLevelEdge @0x120ac0: in an infinite level the view's left edge pushes
// the mouse along (half its picture, 37 wide, from the edge) unless it is hiding; with a wall
// in the way it is crushed instead.
void Session::pushByViewEdge() {
    if (mouse_.state == 2 || mouse_.state == 9 || mouse_.hole >= 0) return;
    const float half = 37.0f * 0.5f;
    if (!(mouse_.position.x - half < viewLeft_) || !(viewLeft_ > 0.0f)) return;
    const Vec2 ahead = mouse_.position + Vec2{half * 0.5f, 0.0f};
    if (!here().lineTouchesType(mouse_.position, ahead, 0, viewLeft_)) {
        // Pushed while the view moves and the mouse has stood a moment: it braces against
        // the edge (the push-wall sequence, held; Player +0x288).
        if (scrollSpeed_ > 0.100000001f && mouse_.idleMs > 100 &&
            mouse_.sprite.currentSequence() != mouse_.sequenceIds[Mouse::kPushWall])
            mouse_.sprite.playSequence(mouse_.sequenceIds[Mouse::kPushWall], SpriteAnim::SequenceLoop, 0);
        mouse_.position.x = viewLeft_ + half;
    } else {
        killMouse(mouse_.position);  // (crushed against a wall; the original's follow-up is not traced)
    }
}

// Player::finishedScrollingLevel @0x1250c4: at the exit door the mouse stops following its
// path (state 0xc, 1.0.5's 0xb; path dropped) and the track's "_Win" tune plays once.
void Session::finishScrollingLevel() {
    if (mouse_.state != 0xC) finalPosition_ = mouse_.position;  // SceneGame::setFinalPos
    // (Player::setState @0x123e6c has no work for leaving 0 or entering 0xc; it would stop a
    // running sequence only with Player +0x180 set, which the chase does not do.)
    mouse_.state = 0xC;
    path_.clear();
    SoundEvent e;
    e.kind = SoundEvent::Music;
    e.file = music_.substr(0, music_.find('.')) + "_Win.m4a";
    soundEvents_.push_back(e);
    // (Played once; the port counts it as playing to the end of the level: the boss fight's
    // music check does not run under the world card.)
    musicPlaying_ = true;
}

// Player::update @0x125e52 (state 0xb): the mouse steers for the level's exit door (type 0x167;
// 0x166 in the boss fight) at 0.04 units a millisecond. Turning toward it while the door is
// ahead, simply walking right once past it. When it is half its width beyond the door's
// middle the door is told (message 2) and the level is won. Its trail follows after the step.
// (Map 0x30e walks along y instead; not a world 1 level.)
void Session::walkIntoExit(int dtMs) {
    const int doorType = level_.info().mode == "boss" ? 0x166 : kExitDoor;
    LevelObject* door = nullptr;
    // (getFirstObjectByType: a door scrolled away in the chase is freed, run lb1_win.)
    for (LevelObject& o : objects_)
        if (o.active && o.type == doorType) {
            door = &o;
            break;
        }
    const float dt = float(dtMs);
    if (door != nullptr) {
        const Vec2 d = door->position - mouse_.position;
        if (mouse_.position.x > door->position.x + mouse_.size.x * 0.5f) {
            sendExitDoorMessage(*door, 2);
            progress_.over = true;
            progress_.won = true;
            exitShut_ = true;
        }
        if (d.x >= 0.0f) {
            mouse_.velocity += ((d * dt) * 0.0399999991f) * 0.100000001f;
            mouse_.velocity.normalise(dt * 0.0399999991f);
        } else {
            mouse_.velocity = Vec2{0.0399999991f, 0.0f};
            mouse_.velocity = mouse_.velocity * dt;
        }
    }
    mouse_.position += mouse_.velocity;
    mouse_.heading = std::atan2(mouse_.velocity.y, mouse_.velocity.x);
    mouse_.sprite.update(dtMs);
    updateTrail(dtMs);
}

// Object::receiveMessage @0x116658 for the exit door: message 1 opens a closed door (0x25 to
// 0x26, frames forward at 200 ms), message 2 closes an open one (0x26 to 0x25, back to frame 0
// at 10 ms for this door, which also shakes the screen and slams, sound 0x94). Either way the
// door updates from then on.
void Session::sendExitDoorMessage(LevelObject& door, int message) {
    if (message == 1 && door.state == ObjectState::HoleClosed) {
        door.state = ObjectState::HoleOpen;
        door.anim.setAnim(0, door.anim.frameCount() - 1, SpriteAnim::Once, 200);
    } else if (message == 2 && door.state == ObjectState::HoleOpen) {
        door.state = ObjectState::HoleClosed;
        // (The chase's exit door and the boss fight's entrance, types 0x167 and 0x165, slam.)
        const bool slams = (door.type | 2) == 0x167;
        door.anim.setAnim(door.anim.frame(), 0, SpriteAnim::Once, slams ? 10 : 200);
        if (slams) {
            shakeScreen(150, 70);
            sound(0x94);
        }
    }
    door.updating = true;
}

void Session::shakeScreen(int durationMs, int delayMs) {
    shake_.durationMs = durationMs;
    shake_.delayMs = delayMs;
    if (delayMs == 0) shake_.timeMs = durationMs;
}

// SceneGame::updatePure @0x100e1c: after its delay the shake runs its time; its strength falls
// from 1 to 0 and the picture jumps between four corners, one a tick.
void Session::updateShake(int dtMs) {
    if (shake_.delayMs >= 1) {
        shake_.delayMs -= dtMs;
        if (shake_.delayMs <= -1) shake_.timeMs = shake_.durationMs;
    }
    if (shake_.timeMs < 1) return;
    shake_.timeMs -= dtMs;
    const float done = float(shake_.durationMs - shake_.timeMs) * (1.0f / float(shake_.durationMs));
    shake_.amount = 1.0f - std::min(1.0f, std::max(0.0f, done));
    shake_.step = shake_.step + 1 > 3 ? shake_.step - 3 : shake_.step + 1;
    static const Vec2 corners[4] = {{-1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, 1.0f}, {1.0f, -1.0f}};
    shake_.direction = corners[shake_.step];
}

// SceneGame::updateScrolling @0x102800 (game mode 1): the view moves on by the scroll speed,
// within the map; objects that have scrolled out of it to the left are gone (the boss cat and
// carried items stay; the mouse dies with a hiding place that goes).
void Session::updateScrolling() {
    const float width = level_.floor(shownFloor_).width;
    if (viewLeft_ < width) {
        viewLeft_ = viewLeft_ + scrollSpeed_;
        if (viewLeft_ > width - kScreenWidth) viewLeft_ = width - kScreenWidth;
        else if (viewLeft_ < 0.0f) viewLeft_ = 0.0f;
    }
    for (LevelObject& o : objects_) {
        if (!o.active) continue;
        if (!(o.position.x + o.size.x < viewLeft_)) continue;
        if (!o.inTrail && o.type != kBossCat) o.active = false;
        if (mouse_.hole == o.id) killMouse(o.position);
    }
}

// SceneGame::updateGameMode @0x102c84 (normal levels).
void Session::startMusic() {
    SoundEvent e;
    e.kind = SoundEvent::Music;
    e.file = music_;
    e.loop = true;
    soundEvents_.push_back(e);
    musicPlaying_ = true;
}

void Session::updateGameMode(int dtMs) {
    if (level_.infinite()) updateScrolling();
    // The boss fight (@0x103150): the level's music once none plays (after being caught).
    else if (level_.info().mode == "boss" && !musicPlaying_) startMusic();
    // The boss chase and the boss fight (game modes 1 and 2) end at once when the mouse is
    // lost, carried items or not; the normal modes wait "delay_results_on_level_fail".
    const bool bossMode = level_.infinite() || level_.info().mode == "boss";
    if (!mouseInPlay_) {
        // No mouse left in play: the level is lost after "delay_results_on_level_fail".
        if (!progress_.over && !bossMode) progress_.resultsDelayMs = 1000;
        progress_.over = true;
        progress_.won = false;
    }
    hud_.updateHand(dtMs, scoring_.handBonus[int(hud_.hand)]);
    if (progress_.clockRunning) {
        progress_.playTimeMs += dtMs;
        progress_.clockAccumulatorMs = scoring_.timerMultiply * dtMs + progress_.clockAccumulatorMs;
        if (tuning_.clockStepMs <= progress_.clockAccumulatorMs) {
            progress_.clockAccumulatorMs -= tuning_.clockStepMs;
            progress_.clock -= tuning_.clockStep;
        }
        if (progress_.clock < 1) progress_.clock = 0;  // "time_up_kill" is off by default
    }
    if (!progress_.over || progress_.finished) return;
    // Seen by the boss cat (@0x1031b8): after the wait the level's numbers are taken (the score is
    // the total) and the lost iris will close on the mouse; the cat forgets the sighting.
    if (level_.infinite())
        for (const LevelObject& o : objects_)
            if (o.type == kBossCat) {  // (the first one)
                Cat* boss = catFor(o.id);
                if (boss && boss->sawMouse()) {
                    if (spottedDelayMs_ > 0) {
                        spottedTimerMs_ += dtMs;  // (SceneGame +0x138a0: the caught line runs on it)
                        return;
                    }
                    boss->forgetMouse();
                    progress_.distance = int(mouse_.distanceWalked);
                    progress_.total = progress_.score;
                    finalPosition_ = mouse_.position;  // SceneGame::setFinalPos (the player's place)
                    return;
                }
                break;
            }
    if (carried_.empty() || bossMode) {
        progress_.resultsDelayMs -= dtMs;
    } else {
        // (1.1.1 @0x103608: while anything is still carried the wait starts over, 250 ms, every
        // tick; run l3s_win, caught with cheese.)
        progress_.resultsDelayMs = scoring_.resultsDelayMs;
        return;
    }
    if (spottedDelayMs_ > 0) return;
    if (progress_.resultsDelayMs < 1 && progress_.bannerMs < 1 && !progress_.won) {
        // The results' numbers are taken as for a win; the level is not completed.
        progress_.clockRunning = false;
        progress_.distance = int(mouse_.distanceWalked);
        progress_.total = int(float(progress_.score) + float(progress_.clock));
        progress_.finished = true;
        ++progress_.failures;
        startLevelOver();
        return;
    }
    if (worldCompletePending_) return;  // (updateGameMode @0x103300: only while the game is on top)
    if (progress_.resultsDelayMs < 1 && progress_.bannerMs < 1) {
        progress_.clockRunning = false;
        progress_.distance = int(mouse_.distanceWalked);
        progress_.total = int(float(progress_.score) + float(progress_.clock));
        if (level_.info().mode == "boss" && !record_.completed) {
            // @0x103368: a boss fight won for the first time ends the world instead
            // (SceneWorldCompleteResults::showScene closes the same iris).
            worldCompletePending_ = true;
            // (startCloseHole runs before setPinholeType(5) there: after the level's plain
            // iris, kind 0, it closes at 1.5 times the speed; run lbf_win.)
            const bool plain = iris_.kind == 0;
            startResultsIris();
            if (plain) iris_.speed = 0.000800000038f * 1.5f;
            return;
        }
        progress_.finished = true;  // the original pushes the results scene here
        startResultsIris();
    }
}

// --- the tick ---------------------------------------------------------------------------

// SceneLevelIntro::update @0x14a390: in every update of its fade the intro may push Kiska's
// offer: never once she is bought, nor in a boss fight (game modes 2 and 3; challenges are not
// ported); the first time on map 758 (2-8) or on map 521 (3-6), or else once this sitting has
// lost 8 levels (the table @0x2e713c (1.0.5): 8, 8, 8 by the profile's step; the count starts over);
// and then only twice in all, at least 79201 s (22 hours) after the last time by the clock.
void Session::offerKiska() {
    static constexpr int kLostLevels[3] = {8, 8, 8};
    KiskaOffers& k = kiskaOffers_;
    if (k.owned || level_.info().mode == "boss") return;
    const int map = level_.info().map;
    bool due = false;
    if (!k.offeredHere && map == 758) {
        k.offeredHere = due = true;
    } else if (!k.offeredLater && map == 521) {
        k.offeredLater = due = true;
    } else if (progress_.failures >= kLostLevels[std::clamp(k.step, 0, 2)]) {
        k.offeredHere = due = true;
        progress_.failures = 0;
        if (k.step <= 1) ++k.step;
    }
    if (k.shown > 1 || !due || wallClock_ - k.lastShown < 79201) return;
    // SceneLevelSkip::showScene: the pause button goes (SceneGame +0x57704 hidden).
    kiska_.open();
    hud_.pauseHidden = true;
    k.lastShown = wallClock_;
    ++k.shown;
}

// SceneLevelSkip::hideScene @0x1fb764: the pause button comes back and the level's music is
// asked for again (SceneGame::playMusic), unless the pause menu is up. The track already
// playing just goes on (no new start in run l28_later).
void Session::closeKiska() {
    if (pause_.active()) return;
    hud_.pauseHidden = false;
    if (!musicPlaying_) startMusic();
}

void Session::pressKiskaButton(const std::string& button) {
    if (!kiska_.active()) return;
    if (button != "BTN_LATER" && button != "BTN_BUY") return;
    sound(sfx::UiButton);
    kiska_.press(button);
    if (!kiska_.active()) closeKiska();
}

// SceneLevelIntro::update @0x149f8c, after the game's own update it drives.
void Session::updateIntro(int dtMs) {
    intro_.ageMs += dtMs;
    for (Spotlight& light : intro_.spotlights) {
        light.timerMs += dtMs;
        if (light.fixed) continue;
        const int t = light.timerMs;
        // A marker's spotlight opens over 1-1.5 s, stays (the cheese pulses once while it is
        // fully lit: SceneGame::pulseAllCheeses) and closes at 4-4.15 s.
        if (t <= 1000) {
            light.size = 0.0f;
        } else if (t - 1001 < 500) {
            light.size = (float(t - 1000) * light.target) * 0.00200000009f;
        } else if (t - 1501 <= 2498) {
            if (!intro_.pulsed && t - 1500 > 500) {
                for (LevelObject& o : objects_)
                    if (isCheese(o.type) && !o.introPulse) o.pulseMs = float(tuning_.objectPulseMs);
                intro_.pulsed = true;
            }
        } else if (t - 4001 <= 148) {
            light.size = (float(t - 4000) * -0.00666666683f + 1.0f) * light.target;
        } else if (t >= 4150) {
            light.size = 0.0f;
        }
    }
    if (intro_.skipped || intro_.ageMs >= intro_.holdMs) {
        // (1.0.5 offered Kiska's level skip here, offerKiska; 1.1.1 has no such offer: Kiska is
        // a power-up, its popup pushed only from the power-up bar, SceneGame::guiEvent; runs
        // l28_watch, l36_watch.)
        const float rate = intro_.skipped ? 2.5f : 1.0f;
        intro_.fadeMs = int(float(intro_.fadeMs) - float(dtMs) * rate);
    }
    if (intro_.fadeMs < 1) {
        intro_.active = false;
        hud_.protractAll(0);  // the HUD comes in (SceneLevelIntro::update @0x14a3ac)
        if (level_.info().uid != 1) pulseCheesesAfterIntro();
    }
}

// SceneLevelIntro::hideScene @0x14a690 → SceneGame::pulseAllCheeses(true) @0xf8424 (not on the
// first level): every loose cheese not already pulsing pulses once, the kinds one after another
// (an eighth of a second apart by kind, plus 25 ms per earlier cheese of the same kind); the
// pulse lasts its normal length plus half the wait, and is held at full size while it waits.
void Session::pulseCheesesAfterIntro() {
    std::array<int, 5> sameKind{};
    for (LevelObject& o : objects_) {
        if (!isCheese(o.type) || o.introPulse || !floatIsZero(o.pulseMs)) continue;
        int& earlier = sameKind[std::size_t(o.type - 6)];
        o.pulseDelayMs = o.type * 125 + earlier - 500;
        o.pulseMs = float(tuning_.objectPulseMs) + float(o.pulseDelayMs) * 0.5f;
        o.updating = true;
        o.introPulse = true;
        earlier += 25;
    }
}

// PinholeManager::update @0x1f0ad8 (kinds 0, 2 and 5): opening speeds up with its progress,
// from at least a tenth; it is done once the radius passes the full radius.
void Session::updateIris(int dtMs) {
    // PinholeManager::update: the pending "shut" is handled, a tick after the radius reached
    // nothing; for a door that is SceneGame::pinholeCallback.
    const bool shutPending = iris_.settling;
    iris_.settling = false;
    if (shutPending && doorTransition_) arriveOnFloor();
    if (iris_.opening) {
        // Kinds 0, 2 and 5 speed up with their progress; the others (the door's star) open
        // at a steady rate.
        if (iris_.kind == 0 || iris_.kind == 2 || iris_.kind == 5)
            iris_.progress += float(dtMs) * iris_.speed * std::max(0.1f, iris_.progress) * 4.0f;
        else
            iris_.progress += float(dtMs) * iris_.speed;
        iris_.radius = iris_.progress * iris_.fullRadius;
        if (iris_.radius > iris_.fullRadius) iris_.opening = false;
    } else if (iris_.closing) {
        // Closing runs at a steady speed and stays shut.
        iris_.progress -= float(dtMs) * iris_.speed;
        iris_.radius = iris_.progress * iris_.fullRadius;
        if (iris_.radius <= 0.0f) {
            iris_.radius = 0.0f;
            iris_.closing = false;
            iris_.closed = true;
            iris_.settling = true;
        }
    }
}

// SceneGameResults::showScene @0x1ed7c0: the counts start from the level's score and the
// seconds left; the medals are judged (Profile::isGoalAchieved @0x172648: "score" by the
// total, "time" by the whole seconds played, "paths" by the paths drawn; other kinds are not
// reconstructed yet and count as not won); the iris closes on the screen centre onto the
// bonus pattern (setPinholeType(5), startCloseHole with the default speed).
void Session::startResults() {
    ResultsScreen::Setup setup;
    setup.score = progress_.score;
    setup.bonus = progress_.clock;
    setup.playTimeMs = progress_.playTimeMs;
    setup.goals = goalLines(true);
    setup.goalPopupHeight = float(level_.goalPopupHeight());
    // A new best (LevelState +0x1c, set by Profile::completeLevel, the first completion
    // included) holds the screen for 500 ms in state 5; the badge needs the level to have
    // been completed before as well (+0x1a = level data +0xf6 clear, set by SceneGame::initGame
    // @0x10636c).
    // (1.1.1 Profile::completeLevel @0x173a9e: the boss chase, game mode 1, keeps LevelState
    // +0x24, the cheese brought home, instead of the total; see recordedScore.)
    setup.newBest = record_.best < 0 || record_.best < recordedScore();
    setup.improvedScore = record_.best >= 0 && setup.newBest;
    // (1.1.1) The cheese badge: the balance before, and with the cheese brought home
    // (LevelState +0x70, CheeseTracker::add @0x220488 into the profile's tracker).
    setup.delivered = progress_.delivered;
    setup.cheeseBefore = cheeseBalance_;
    setup.cheeseAfter = cheeseBalance_;
    for (int n : progress_.deliveredKinds) setup.cheeseAfter += n;
    results_.start(setup);
}

BossWorld Session::bossWorld(int floor) {
    BossWorld world{floorWalls_[std::size_t(floor)], random_, particles_, gameTimeMs_, viewLeft_, mouse_.position,
                     mouse_.state, mouse_.stateEntry, soundEvents_, {}, {}, {}, {}, {}, {}, nullptr, floor};
    world.sendMessage = [this, floor](int type, int tag, int lineType) { sendLineMessage(floor, type, tag, lineType); };
    world.killMouse = [this] { killMouse(mouse_.position); };
    world.mouse.position = mouse_.position;
    world.mouse.velocity = mouse_.velocity;
    world.mouse.state = mouse_.state;
    world.mouse.floor = mouse_.floor;
    world.mouse.active = !mouseCaught_;
    world.mouse.trailLength = int(carried_.size());
    world.mouse.carriesSomething = mouse_.cup >= 0;
    world.send = [this](const GameMessage& m, bool applyMap) { sendMessage(m, applyMap); };
    world.fireEvent = [this](const std::string& event) { fireEvent(event); };
    world.setWalls = [this, floor](int tag, bool enabled, int type) {
        floorWalls_[std::size_t(floor)].setTagEnabled(tag, enabled, type);
    };
    world.projectiles = &projectiles_;
    world.floor = floor;
    return world;
}

// SceneGame::sendMessage @0xf87e4 with the map applied: message 0 flips the walls with the tag
// (Map::toggleStates; map 791 has no decorations to flip), others switch them on (2) or off;
// then the objects on that floor carrying the tag, or of the line's type, receive it.
void Session::sendLineMessage(int floor, int type, int tag, int lineType) {
    LevelGeometry& walls = floorWalls_[std::size_t(floor)];
    if (type == 0) walls.toggleTag(tag);
    else walls.setTagEnabled(tag, type == 2, lineType);
    for (LevelObject& o : objects_) {
        if (o.floor != floor || (o.tag != tag && o.type != lineType)) continue;
        // (Only the droid reacts to these on the reconstructed levels.)
        if (Droid* droid = droidFor(o.id)) {
            BossWorld world = bossWorld(floor);
            droid->receiveMessage(type, world);
        }
    }
}

Droid* Session::droidFor(int objectId) {
    for (Droid& droid : droids_)
        if (droid.objectId() == objectId) return &droid;
    return nullptr;
}

Cat* Session::catFor(int objectId) {
    for (Cat& cat : cats_)
        if (cat.objectId() == objectId) return &cat;
    return nullptr;
}

std::array<ResultsGoal, 3> Session::goalLines(bool withThisRun) const {
    std::array<ResultsGoal, 3> lines{};
    const auto& goals = level_.info().goals;
    const std::array<bool, 3> won = withThisRun ? goalsWon() : std::array<bool, 3>{};
    for (std::size_t i = 0; i < lines.size() && i < goals.size(); ++i) {
        ResultsGoal& g = lines[i];
        g.kind = level_.goalKind(goals[i].type);
        g.wonBefore = record_.goals[i];
        g.won = g.wonBefore || won[i];
        // Its line for the info popup: the string "GOAL_INFO_<KIND>_<value>"
        // (LevelGoal::lookupGoalInfoTextIds @0x1fee2c, at level-list load).
        std::string key = "GOAL_INFO_";
        for (char c : goals[i].type) key += char(std::toupper(static_cast<unsigned char>(c)));
        key += "_" + std::to_string(goals[i].value);
        if (const data::StringTable* text = level_.strings(); text && text->id(key) >= 0) g.infoText = text->text(key);
    }
    return lines;
}

void Session::pressPause() {
    // The pause button (a GUI button of the level, hidden in the home hole and under the menu)
    // pushes ScenePause on release.
    if (pause_.active() || results_.active() || progress_.finished || notice_.active() || !hud_.pauseVisible) return;
    sound(sfx::UiButton);
    pause_.open(float(level_.goalPopupHeight()), goalLines(false));
    hud_.retractAll(0);         // ScenePause::showScene: SceneGame::retractAllHudItems(0)
    hud_.pauseHidden = true;    // ... hidePauseButton(true)
}

void Session::pressPauseButton(const std::string& name) {
    if (!pause_.active()) return;
    // GuiButton plays its sound on release whatever the screen then does; the resume button
    // has the other one (setButtonAltSound).
    sound(name == "BTN_CONTINUE" ? sfx::UiButton2 : sfx::UiButton);
    // ScenePause::guiEvent @0x14debc answers only while the iris is still.
    if (iris_.moving()) return;
    if (name.rfind("LEVEL_GOAL_BTN_", 0) == 0) {
        pause_.pressGoal(name.back() - '1');
        return;
    }
    PauseMenu::Action action = PauseMenu::Action::None;
    if (name == "BTN_CONTINUE" || name == "BTN_CONTINUE_2") action = PauseMenu::Action::Resume;
    if (name == "BTN_RESTART") action = PauseMenu::Action::Retry;
    if (name == "BTN_QUIT") action = PauseMenu::Action::Exit;
    if (!pause_.press(action)) return;
    if (action == PauseMenu::Action::Retry || action == PauseMenu::Action::Exit) {
        // The iris closes on the mouse (setPinholeType(0), startCloseHole at the player).
        iris_ = Iris{};
        iris_.kind = 0;
        iris_.closing = true;
        iris_.centre = mouse_.position;
        iris_.fullRadius = kScreenWidth * 0.666666687f +
                           std::max(std::fabs(iris_.centre.x - kScreenWidth * 0.5f),
                                    std::fabs(iris_.centre.y - kScreenHeight * 0.5f));
        iris_.progress = 1.0f;
        iris_.radius = iris_.fullRadius;
        iris_.speed = 0.000800000038f * 1.5f;
    }
}

// SceneGame::updatePaused @0xff028: the darkening over the level comes up while the pause menu
// shows and goes back down after (0.005 per ms; drawn at 0.6 of it).
// SceneGame::update @0x103528 while the power-up bar shows: its message timers, then
// updatePaused (the HUD's items; the darkening comes up) and Scene::update (the clock and the
// GUI items, the bar's slide among them).
void Session::pausedStep(int dtMs) {
    hud_.updateItems(dtMs);
    updateDim(dtMs, true);
    gameTimeMs_ += dtMs;
    bar_.slide().update(dtMs);
    bar_.card().update(dtMs);
    updatePowerupHighlights(dtMs);
    if (bar_.card().takeWobble()) sound(sfx::UiWobble);
}

void Session::updateDim(int dtMs, bool paused) {
    if (paused) dimFade_ = std::min(1.0f, dimFade_ + float(dtMs) * 0.00499999989f);
    else if (dimFade_ > 0.0f) dimFade_ = std::max(0.0f, dimFade_ + float(dtMs) * -0.00499999989f);
}

std::array<bool, 3> Session::goalsWon() const {
    std::array<bool, 3> won{};
    const auto& goals = level_.info().goals;
    for (std::size_t i = 0; i < won.size() && i < goals.size(); ++i) {
        // A group in parentheses is won when all its parts are (Profile::isGoalAchieved
        // @0x172648 walks the parts).
        won[i] = !goals[i].parts.empty();
        for (const auto& [type, value] : goals[i].parts) won[i] = won[i] && goalPartWon(type, value);
    }
    return won;
}

bool Session::goalPartWon(const std::string& type, int value) const {
    if (type == "score") return progress_.total >= value;
    if (type == "time") return value >= progress_.playTimeMs / 1000;
    if (type == "paths") return value >= pathsDrawn_;
    if (type == "bump") {
        // "head_bump": counted once per cat, at its first pounce into a wall (chase @0x1a3850).
        int cats = 0;
        for (const Cat& cat : cats_)
            if (cat.headBumps() > 0) ++cats;
        return value == cats;
    }
    if (type == "all_crumbs") {
        // Every crumb of the level eaten.
        for (const LevelObject& o : objects_)
            if (o.type == kCrumb && o.active) return false;
        return true;
    }
    if (type == "spotted") {
        // The achievements metric counts each cat once, at its first sighting of the
        // mouse (awarenessCheck @0x1a5c9a); won when it equals the value (no cat for 0).
        int cats = 0;
        for (const Cat& cat : cats_)
            if (cat.sightings() > 0) ++cats;
        return value == cats;
    }
    // Kinds 16..27 ("Only N Pipes", "No Balloons", ...) are won while their metric stays within
    // the value (isGoalAchieved @0x172de2; teacup and bucket read the same metric). The
    // metrics count the uses this run: pipes taken, wall holes and safe holes entered, buttons
    // pressed, cups worn, chillies eaten, balloons picked up, TVs switched on. Hypothesis: the
    // exact points where the original counts (only "at most 0" goals and pipes are checked
    // against runs).
    if (type == "pipe") return pipeUses_ <= value;
    if (type == "mouse_hole") return uses_.wallHoles <= value;
    if (type == "safe_hole") return uses_.safeHoles <= value;
    if (type == "switch") return uses_.switches <= value;
    if (type == "furniture") return uses_.furniture <= value;
    if (type == "teacup" || type == "bucket") return uses_.cups <= value;
    if (type == "chilli") return uses_.chillies <= value;
    if (type == "balloon") return uses_.balloons <= value;
    if (type == "tv") return uses_.tvs <= value;
    if (type == "string") return uses_.twine <= value;
    if (type == "fish") return uses_.catnip <= value;
    // "shuriken_cheese": carried cheese struck by a shuriken (Object::touchProjectile's metric);
    // won with at least the value (HYPOTHESIS: "at least"; run l41_win shows it won with one).
    if (type == "shuriken_cheese") return uses_.shurikenCheese >= value;
    // combo_pair .. combo_flush (kinds 1..7, the hands in the same order): won when the number
    // of trails scored with that hand equals the value (isGoalAchieved @0x172938 compares for
    // equality).
    const int kind = level_.goalKind(type);
    if (kind >= 1 && kind <= 7) return value == handsScored_[std::size_t(kind)];
    return false;
}

// The iris starts closing in the tick that asks for the results (SceneGameResults::showScene
// runs when the scene is pushed; its first update comes a tick later).
void Session::startResultsIris() {
    iris_ = Iris{};
    iris_.kind = 5;
    iris_.closing = true;
    iris_.centre = {kScreenWidth * 0.5f, kScreenHeight * 0.5f};
    iris_.fullRadius = kScreenWidth * 0.666666687f;
    iris_.progress = 1.0f;
    iris_.radius = iris_.fullRadius;
    iris_.speed = 0.000800000038f;
}

void Session::sound(int effect, SoundEvent::Kind kind, bool loop) {
    SoundEvent e;
    e.kind = kind;
    e.effect = effect;
    e.loop = loop;
    soundEvents_.push_back(e);
}

void Session::ambienceSounds(const std::vector<std::string>& started) {
    for (const std::string& file : started) {
        SoundEvent e;
        e.file = file;
        soundEvents_.push_back(e);
    }
}

// SceneGame::playScoreSound @0xf8544: the bonus sound, or the next of six rising notes (back
// to the first after "cheese_crumb_sound_reset", 600 ms, without one).
void Session::playScoreSound(bool big) {
    if (big) {
        sound(sfx::ScoreBonus);
        return;
    }
    const auto& notes = level_.scoreNotes();
    if (noteStep_ >= 0 && noteStep_ < int(notes.size())) sound(notes[std::size_t(noteStep_)], SoundEvent::PlayOnce);
    noteStep_ = noteStep_ + 1 > 6 ? noteStep_ - 5 : noteStep_ + 1;
    noteTimerMs_ = 600;
}

void Session::setDossiersFound(const std::array<bool, 21>& found) {
    dossiersFound_ = found;
    for (LevelObject& o : objects_)
        if (o.type == kDossier && o.tag >= 0 && o.tag < 21 && found[std::size_t(o.tag)]) o.active = false;
}

void Session::tick(int dtMs) {
    ++frames_;
    // The results screen, pushed in the previous tick (its iris started closing then), runs
    // from this tick on.
    if (progress_.finished && progress_.won && !results_.active()) startResults();
    if (worldCompletePending_ && !worldComplete_.active()) {
        WorldCompleteScreen::Setup setup;
        setup.world = level_.info().world;
        worldComplete_.start(setup);
    }
    if (notice_.active()) {
        // Only the popup (the top scene) runs; the level and its intro wait below it.
        notice_.update(dtMs);
    } else if (dossier_.active()) {
        // SceneDossier::update @0x286dc0: the HUD's update (SceneGame::updateHUD), then the page.
        // Gone, the level carries on (hideScene: the HUD comes back).
        hud_.updateItems(dtMs);
        if (!dossier_.update(dtMs)) {
            hud_.protractAll(0);
            hud_.pauseHidden = false;
        }
        for (int e : dossier_.takeSounds()) sound(e);
    } else if (kiska_.active()) {
        // SceneLevelSkip::update @0x1faabc: its frames, the level's paused update (the darkening
        // comes up) and the particles; the level and its intro wait below. Sent away by a
        // button, it leaves in this update.
        const bool left = kiska_.update(dtMs);
        updateDim(dtMs, true);
        particles_.update(dtMs);
        if (left) closeKiska();
    } else if (pause_.active()) {
        // ScenePause::update: its items, the level's paused update, then either the HUD items
        // (nothing to do yet) or the action that is due.
        pause_.updateItems(dtMs);
        updateDim(dtMs, true);
        switch (pause_.due(iris_.moving())) {
        case PauseMenu::Action::None:
            hud_.updateItems(dtMs);
            break;
        case PauseMenu::Action::Resume:
            pause_.done();
            pause_.close();             // Engine::popScene → hideScene:
            hud_.protractAll(0);        // protractAllHudItems(0)
            hud_.pauseHidden = false;   // hidePauseButton(false)
            break;
        case PauseMenu::Action::Retry:
            pause_.done();
            retryFromPause_ = true;     // SceneGame::reloadFromSnapshot: the level again
            retryRequested_ = true;
            break;
        case PauseMenu::Action::Exit:
            pause_.done();
            exitRequested_ = true;      // Engine::setRootScene("menu_map")
            break;
        }
        for (int n = pause_.takeWobbles(); n > 0; --n) sound(sfx::UiWobble);
        // The reload happens in the pause scene's update; this frame's ambience and iris steps
        // after the scenes belong to the new level (its constructor makes them).
        if (retryRequested_) return;
    } else if (worldComplete_.active()) {
        // SceneWorldCompleteResults::update @0x218f90: its frames, then the level's whole update
        // (game->update: the mouse walks on under the card and its draws go on, run lbf_win;
        // with the card on top no scene is pushed again); a frame squashing for the first time
        // wobbles (played while drawing).
        worldComplete_.updateFrames(dtMs);
        step(dtMs);
        for (int n = worldComplete_.takeWobbles(); n > 0; --n) sound(sfx::UiWobble);
    } else if (levelOver_) {
        // SceneLevelOver::update @0x130070: while the iris moves the level runs on purely;
        // shut, the level is reloaded (SceneGame::reloadFromSnapshot, as for a retry from the
        // pause menu) and its iris opens again in the cat's shape with "Try Again!".
        if (iris_.moving()) {
            updatePure(dtMs);
        } else {
            retryAfterCaught_ = true;
            retryRequested_ = true;
            return;
        }
    } else if (cutscene_.active && !intro_.active) {
        // (A retry's level intro is pushed above the boss's restart cutscene: the cutscene
        // waits under it.)
        cutsceneTick(dtMs);
    } else {
        results_.updateFrames(dtMs);
        step(dtMs);
        if (intro_.active) {
            updateIntro(dtMs);
        } else if (hand_.active() && !results_.active()) {
            // SceneHandTutorial::update: the level's update (above), then the finger.
            hand_.update(dtMs, int(path_.size()));
        }
        results_.update(dtMs);
        results_.checkWobbles();  // (the original plays them while drawing)
    }
    // Pushing the results scene (in the tick that asks for it) silences the ambience at once
    // (SceneGameResults::showScene @0x1ee0ca: AmbiencePlayer::unload).
    // (So does the world card's, SceneWorldCompleteResults::showScene @0x219af4.)
    if (((progress_.finished && progress_.won) || worldCompletePending_) && !ambience_.stopped()) {
        std::vector<std::string> stops = ambience_.loops();
        for (const std::string& file : ambience_.sounding()) stops.push_back(file);
        for (const std::string& loop : stops) {
            SoundEvent e;
            e.kind = SoundEvent::Stop;
            e.file = loop;
            soundEvents_.push_back(e);
        }
        ambience_.stop();
    }
    // After the scenes: the iris, then the ambience (App::update @0x1825c8; a door's pinhole
    // callback loads the new floor's ambience before its update).
    updateIris(dtMs);
    ambienceSounds(ambience_.update(dtMs, random_));
    for (int n = particles_.takeTextPops(); n > 0; --n) sound(sfx::TextPop);
    for (int e : particles_.takeSounds()) sound(e);
    for (int e : results_.takeSounds())
        sound(e < 0 ? -e : e, e < 0 ? SoundEvent::Stop : SoundEvent::Play, e == sfx::ScoreCountLoop);
}

// SceneGame::updatePure @0x100cb4: the mouse, what it touches, the objects, the map's
// decorations and the particles.
void Session::updatePure(int dtMs) {
    updateMouse(dtMs);
    mouseInPlay_ = !mouseCaught_;  // SceneGame +0x12ec4, counted after the mouse's update
    collideObjects();
    pressSwitchesByCats();
    collideProjectiles();  // (still in collideObjects, whether or not the mouse is in play)
    if (!cats_.empty()) catEyes_.update(dtMs);  // (awarenessCheck @0x1a554a, once per tick)
    // (The count is read each step: an object made during the loop is updated in it too.)
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        LevelObject& o = objects_[i];
        if (o.active && o.floor == shownFloor_) updateObject(o, dtMs);
    }
    updateFluff(dtMs);
    particles_.update(dtMs);
    updateProjectiles(dtMs);
    updateShake(dtMs);
}

// SceneLevelOver::showScene @0x1305f4: pushed over the lost level; the iris closes on where
// the mouse was caught, cut in the cat's silhouette over the death pattern
// (PinholeManager::startCloseHole @0x1f1214: "iris_transition_speed", 0.0008; the radius
// that uncovers the screen is 500 for this kind plus the centre's larger distance from the
// screen middle).
void Session::startLevelOver() {
    levelOver_ = true;
    iris_ = Iris{};
    iris_.kind = 2;
    iris_.closing = true;
    iris_.centre = {finalPosition_.x - viewLeft_, finalPosition_.y};
    iris_.fullRadius = kScreenWidth * 1.04166675f + std::max(std::fabs(iris_.centre.x - kScreenWidth * 0.5f),
                                                             std::fabs(iris_.centre.y - kScreenHeight * 0.5f));
    iris_.progress = 1.0f;
    iris_.radius = iris_.fullRadius;
    iris_.speed = 0.000800000038f;
}

void Session::step(int dtMs) {
    // SceneGame::update @0x103528. (1.1.1) A notice the start asked for opens first, once the
    // power-up bar is still (it is: hidden), and the rest of the update waits for the next
    // tick; the iris finishes opening seven times faster (PinholeManager::finishTransition).
    // (1.1.1) The power-up bar out, or Digger's tutorial on: the level waits (the paused part
    // of the update only).
    if (pendingNotice_ && bar_.slide().finished()) {
        notice_.open(*pendingNotice_);
        pendingNotice_.reset();
        iris_.speed *= 7.0f;
        return;
    }
    // (Digger gone, his tutorial is over.)
    if (bar_.tutorial() == 5 && bar_.digger().digger.finished()) setPowerupTutorial(0);
    if (bar_.tutorial() != 0) {
        // His continue button shows (highlighted) once its wait is over; the layout updates.
        PowerupBar::Tutorial& d = bar_.digger();
        if (d.continueDelayMs > 0) {
            d.continueDelayMs -= dtMs;
            if (d.continueDelayMs < 1) {
                d.continueShown = true;
                d.continueHighlightMs = 0;
                d.continueDelayMs = 0;
            }
        }
        bar_.updateTutorialLayout(dtMs);
        pausedStep(dtMs);
        return;
    }
    if (bar_.slide().animatingIn() || bar_.slide().progress() > 0.0f) {
        pausedStep(dtMs);
        return;
    }
    // First the sheen over loose cheese moves on (0.15 per ms)
    // and starts again at the left once past the view (@0x103558).
    sheenX_ += float(dtMs) * 0.150000006f;
    if (sheenX_ > kScreenWidth + viewLeft_) sheenX_ = viewLeft_;
    if (noteTimerMs_ >= 1) {  // @0x103b54
        noteTimerMs_ -= dtMs;
        if (noteTimerMs_ <= -1) noteStep_ = 0;
    }
    // The clock starts with the first path; after a door it starts again on its own once the
    // iris has opened (@0x10382c: no path, the iris still, and play time past the last
    // checkpoint's). After a room door, which takes a checkpoint, only a path starts it (run
    // l35_down).
    const int checkpointPlayTimeMs = checkpoint_ ? checkpoint_->playTimeMs : 0;
    if (!progress_.over && !progress_.clockRunning &&
        (path_.size() >= 1 || (!iris_.moving() && progress_.playTimeMs > checkpointPlayTimeMs)))
        progress_.clockRunning = true;
    if (progress_.clockHeld) progress_.clockRunning = false;
    // (@0x103a36: hidden in a safe hole, the clock stands.)
    if (mouse_.state == 2)
        if (const LevelObject* h = objectById(mouse_.hole); h && isMunchHole(h->type)) progress_.clockRunning = false;
    tutorialMs_ += dtMs;
    // A run of crumbs ends a second after the last one.
    if (crumbComboMs_ > 0) {
        crumbComboMs_ -= dtMs;
        if (crumbComboMs_ < 1) {
            crumbComboMs_ = 0;
            crumbCombo_ = 0;
        }
    }
    // The clock stands while a door's iris runs (SceneGame +0x1384e, set by
    // startFloorTransition, cleared with the clock by pinholeCallback); a new path restarts it.
    if (doorTransition_) progress_.clockRunning = false;
    // Two random numbers every tick (screen-shake noise).
    noise_[0] = random_.next();
    noise_[1] = random_.next();
    // Scene::update @0x103ae0: the game's clock (SceneGame +8) moves on here, before the
    // objects' update (world 4's henchman sways by it, run lbf4_clock).
    gameTimeMs_ += dtMs;
    bar_.slide().update(dtMs);  // (the scene's GUI items)
    bar_.card().update(dtMs);
    updatePowerupHighlights(dtMs);
    hud_.updateCounters(dtMs);
    progress_.bannerMs = progress_.bannerMs < 1 ? 0 : progress_.bannerMs - dtMs;
    // The wait after the boss cat has seen the mouse (SceneGame +0x13890, @0x103b3e).
    {
        const int before = spottedDelayMs_;
        spottedDelayMs_ = before <= 0 ? 0 : before - dtMs;
    }
    handleFinger();

    updatePure(dtMs);

    updateGameMode(dtMs);
    finger_.began = false;
    if (!finger_.held) finger_.active = false;
    updatePrediction();
    // SceneGame::updatePaused @0xff028, last in SceneGame::update (1.1.1): the HUD's items
    // (updateHUD), then the dimming. (1.0.5 updated the HUD before the objects; run l11_hud:
    // the win's retract timer is counted in the tick that sets it.)
    hud_.updateItems(dtMs);
    updateDim(dtMs, false);
    ++ticks_;
}

}  // namespace doc::game
