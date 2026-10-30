// One play session of a level: input, the drawn path, the mouse, the level's objects and the
// level clock, advanced in the original's fixed ticks. Reconstructed from SceneGame::update
// @0x103528 (tick order), SceneGame::updatePure @0x100cb4, SceneGame::updateGameMode @0x102c84
// and the object and player routines named at each rule.
#pragma once

#include <memory>

#include "game/level.hpp"
#include "game/mouse.hpp"
#include "game/objects.hpp"
#include "game/random.hpp"
#include "game/particles.hpp"
#include "game/ambience.hpp"
#include "game/hands.hpp"
#include "game/kiska_popup.hpp"
#include "game/notice.hpp"
#include "game/hud.hpp"
#include "game/results.hpp"
#include "game/pause.hpp"
#include "game/cat.hpp"
#include "game/droid.hpp"
#include "game/projectiles.hpp"
#include "game/spitter.hpp"
#include "game/turret.hpp"
#include "game/vacuum.hpp"
#include "game/switches.hpp"
#include "game/world_complete.hpp"
#include "game/roamer.hpp"
#include "game/profile.hpp"
#include "game/sound.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace doc::game {

// A finger as the game sees it (SceneGame::mouseEvent @0x1050e0 keeps one slot per finger).
struct FingerState {
    bool active = false;  // touching this tick (or released this tick)
    bool held = false;    // still touching
    bool began = false;   // touched down this tick
    Vec2 screen;          // points, 480x320, y down
};

// Progress of the level (the original's global LevelState and SceneGame timers).
struct LevelProgress {
    int clock = 0;              // seconds left on the level clock
    int clockAccumulatorMs = 0;
    bool clockRunning = false;  // starts once a path exists
    bool clockHeld = false;     // stopped for good once the level is won
    int playTimeMs = 0;         // game time while the clock runs
    int cheeseTotal = 0;        // cheese placed on the level
    int delivered = 0;          // cheese taken home
    int score = 0;
    bool over = false;
    bool won = false;
    int bannerMs = 0;           // "level complete" banner before the results
    int resultsDelayMs = 0;     // waits while cheese is still going into the hole
    bool finished = false;      // the results screen has been requested
    int total = 0;              // results: score + seconds left
    int distance = 0;           // results: distance the mouse ran
    int lastScore = 0;          // points of the last trail taken home (shown 1.5 s)
    int lastScoreShowMs = 0;
    PokerHand lastHand = PokerHand::Bluff;  // hand of the last trail scored
    bool spotted = false;       // a cat has seen the mouse (LevelState +0x54; the "spotted" goal)
    bool mouseGone = false;     // the mouse was caught (LevelState +0x55)
    bool foundSecret = false;   // left by a secret exit (+0x56): the world's secret level opens
    int failures = 0;           // levels lost this session (SceneGame +0x57fb4)
};

// What the path being drawn is heading for (Player::playerPredictivePathUpdate @0x123bd4):
// cheese the path has snapped to counts as already carried, so the home hole can open in
// time for the path to go in.
struct PathPrediction {
    int lastTarget = -1;      // object id of the last snap target counted
    int cheeseAhead = 0;      // cheese the path will pick up
    bool enoughCheese = false;
    bool snappedToCheese = false;
};

class Session {
public:
    // Starts the level as the original's GameBoot::bootLevel does; `seed` is the state of the
    // C library generator at that moment (the original seeds it from the clock at start-up).
    // `strength`: how many cheeses the player's profile lets the mouse carry (a fresh profile: 1).
    // `wonBefore`: the profile already has this level won (no first-visit popup).
    Session(Level level, std::uint32_t seed, Tuning tuning = {}, int strength = 1, bool wonBefore = false);
    // Retry from the results (SceneGameResults::guiEvent → SceneGame::restartGame @0x1069d4):
    // the same level again at once, continuing the random generator and keeping the music
    // slot; the iris reopens on the mouse over the bonus pattern, and the finger comes back
    // (initialise → initGame clears LevelState +0x55 before postInitGame checks it).
    std::unique_ptr<Session> retry() const;

    // Touch events from the host, in screen points (480x320, y down).
    void fingerDown(Vec2 screen);
    void fingerMove(Vec2 screen);
    void fingerUp(Vec2 screen);

    void tick(int dtMs);

    // A popup over the level (see notice.hpp). While it is up, touches belong to it and the
    // level does not run; its button closes it.
    const NoticePopup& notice() const { return notice_; }
    const Hud& hud() const { return hud_; }
    // GuiButton::mouseEvent @0x1d7438: a release over the pressed button plays its sound
    // (the shared ui_button) before the screen handles it.
    void pressNoticeButton() {
        if (!notice_.active()) return;
        sound(sfx::UiButton);
        notice_.dismiss();
    }
    // Kiska's offer (see kiska_popup.hpp). The profile's record of it goes in before the first
    // tick with the wall clock (time(), seconds) and comes back changed once it has shown.
    void setKiskaOffers(const KiskaOffers& offers, std::int64_t now) {
        kiskaOffers_ = offers;
        wallClock_ = now;
    }
    const KiskaOffers& kiskaOffers() const { return kiskaOffers_; }
    const KiskaPopup& kiska() const { return kiska_; }
    // A button of the popup released (GuiButton::mouseEvent plays ui_button first).
    void pressKiskaButton(const std::string& button);

    // The profile's strength after this level started (raised to the level's trail length).
    int strength() const { return carryCapacity_; }
    int carriedCheese() const;                       // cheese in the trail
    // Cheese, type 0xb, twine and catnip in the trail: ObjControllerPickups::render greys
    // loose pickups once these fill the trail (LevelState +0x3c).
    int carriedPickups() const;
    int gameTimeMs() const { return gameTimeMs_; }   // play time of the scene (SceneGame +8)
    float viewLeft() const { return viewLeft_; }     // the view's scroll (infinite levels)
    float handBonus(PokerHand hand) const { return scoring_.handBonus[int(hand)]; }

    const Level& level() const { return level_; }
    // The walls of the floor the mouse is on.
    const LevelGeometry& walls() const { return floorWalls_[std::size_t(mouse_.floor)]; }
    const LevelGeometry& wallsOfFloor(int floor) const { return floorWalls_[std::size_t(floor)]; }
    int floorCount() const { return int(floorWalls_.size()); }
    const std::vector<Roamer>& roamers() const { return roamers_; }
    // The floor on screen (SceneGame +0x130): the mouse's, changed when the iris has shut on a
    // door.
    int shownFloor() const { return shownFloor_; }
    const std::vector<LevelObject>& objects() const { return objects_; }
    const Mouse& mouse() const { return mouse_; }
    Mouse& mouse() { return mouse_; }
    // What drawing a frame changes in the game state (the original's render passes): the
    // mouse's heading and the cats' aims and facings are folded. Called once per presented
    // frame, after the tick.
    void prepareFrame();
    // The game clock for sound decisions (app frames x the tick).
    std::int64_t clockMs() const { return std::int64_t(frames_) * kTickMs; }
    const DrawnPath& path() const { return path_; }
    DrawnPath& path() { return path_; }
    const LevelProgress& progress() const { return progress_; }
    const std::vector<int>& carried() const { return carried_; }  // object ids, nearest first
    const PathPrediction& prediction() const { return prediction_; }
    GameRandom& random() { return random_; }
    const Particles& particles() const { return particles_; }
    int musicTrack() const { return musicTrack_; }
    // What the sound output should do since the last call (music, effects, ambience).
    std::vector<SoundEvent> takeSoundEvents() { return std::move(soundEvents_); }
    const std::vector<SoundEvent>& peekSoundEvents() const { return soundEvents_; }

    struct FluffState {
        SpriteAnim anim;
    };
    const std::vector<FluffState>& fluff() const { return fluff_; }

    // The level intro overlay (SceneLevelIntro, "static pinhole" kind): spotlights on the mouse
    // and on the level's marker objects (type 1112) while the level is shown dimmed; the cheese
    // pulses once; it fades out after 4 s or faster after a touch. Touches pass through.
    struct Spotlight {
        Vec2 centre;
        float size = 0.0f;      // current diameter
        float target = 0.0f;    // full diameter
        int timerMs = 0;
        bool fixed = false;     // the mouse's: always at full size
    };
    struct Intro {
        bool active = false;
        int ageMs = 0;
        int fadeMs = 1600;
        int holdMs = 4000;
        bool skipped = false;
        bool pulsed = false;
        int totalFadeMs = 1600;
        std::vector<Spotlight> spotlights;  // the mouse's first, then the markers
    };
    const Intro& intro() const { return intro_; }
    // The demonstrating finger of map 10 (inactive elsewhere and once a path is drawn).
    const HandTutorial& hand() const { return hand_; }

    // The iris over the level (PinholeManager): kind 0 opens the level onto the mouse (the
    // menu's pattern outside a round hole), kind 5 closes it onto the bonus pattern behind
    // the results. startOpenHole @0x1f150c, startCloseHole @0x1f1214, update @0x1f0ad8.
    struct Iris {
        int kind = 0;         // setPinholeType @0x1f0f6c
        bool opening = false;
        bool closing = false;
        bool closed = false;  // all pattern
        Vec2 centre;          // map space
        float progress = 0;   // 0..1 of the full radius (+0x30)
        float fullRadius = 0; // +0x2c: reaches the farthest screen corner
        float radius = 0;     // +0x28
        float speed = 0;      // per ms (+0x44)
        // Just shut: still "animating" until the next update (+0x38, isAnimating @0x1f09e0).
        bool settling = false;
        bool moving() const { return opening || closing || settling; }
    };
    const Iris& iris() const { return iris_; }
    // The in-game tutorial over the floor (SceneInGameTutorial): whether it shows, which of
    // its two steps (map 705's script switches to the second when the cat takes aim, turned
    // to the mouse's side), and its text alpha.
    struct TutorialView {
        bool shown = false;
        int slot = -1;          // -1: every piece; else only this step's
        bool mirrored = false;
        float alpha = 1.0f;
    };
    TutorialView tutorialView() const;
    const ResultsScreen& results() const { return results_; }
    const WorldCompleteScreen& worldComplete() const { return worldComplete_; }
    // A boss fight keeps the music that plays when it starts (the map's).
    bool keepsMusic() const { return level_.info().mode == "boss"; }
    // The world's goals for its card (the app counts them in the profile).
    void setWorldGoals(int won, int total) { worldComplete_.setGoals(won, total); }
    // The card's continue (SceneWorldCompleteResults::guiEvent): back to the map.
    void pressWorldContinue() {
        if (worldComplete_.active() && worldComplete_.continueFrame().progress() >= 1.0f) exitRequested_ = true;
    }
    // What the profile knew about this level before the run (medals won, best total).
    void setRecord(const LevelRecord& record) { record_ = record; }
    // Continue or retry on the results screen; false while its buttons are not in yet.
    // Continue fades the screen out (state 7); retry asks the app for retry() at once.
    bool pressResultsButton(bool retry) {
        if (!results_.canLeave()) return false;
        sound(sfx::UiButton);
        if (retry) retryRequested_ = true;
        else results_.leave(false);
        return true;
    }
    bool retryRequested() const { return retryRequested_; }
    bool levelOver() const { return levelOver_; }
    // Ambience one-shots still sounding (a reload stops them with the loops).
    std::vector<std::string> ambienceSounding() const { return ambience_.sounding(kTickMs); }
    bool mouseCaught() const { return mouseCaught_; }
    Vec2 finalPosition() const { return finalPosition_; }
    // The screen shake (SceneGame::shakeScreen @0xf662c, updatePure @0x100e1c): while it runs
    // the whole picture is drawn grown by up to 5% / 7.5% and moved (SceneGame::render
    // @0xffd24, in the original's y-up screen space), the effect fading over its time.
    struct Shake {
        int delayMs = 0, timeMs = 0, durationMs = 0, step = 0;
        float amount = 0.0f;
        Vec2 direction;
        bool active() const { return timeMs >= 1; }
    };
    const Shake& shake() const { return shake_; }
    bool inCutscene() const { return cutscene_.active; }
    // The speech bubble on screen (drawing): its frame and line, or null.
    const BounceFrame* speechBox() const {
        return cutscene_.active && cutscene_.child == CutsceneRun::Child::Speech ? &cutscene_.box : nullptr;
    }
    const data::Speech& speechLine() const { return cutscene_.shown; }
    int spottedTimerMs() const { return spottedTimerMs_; }
    // The pause menu (ScenePause): opened by the pause button, its buttons by name.
    void pressPause();
    void pressPauseButton(const std::string& name);
    const PauseMenu& pause() const { return pause_; }
    const std::vector<Cat>& cats() const { return cats_; }
    const std::vector<Droid>& droids() const { return droids_; }
    // World 2's fight: the Spitter, the rail spheres, the wall devices and what is thrown.
    const std::vector<Spitter>& spitters() const { return spitters_; }
    const std::vector<RailTarget>& railTargets() const { return railTargets_; }
    const std::vector<BeamEmitter>& emitters() const { return emitters_; }
    const Projectiles& projectiles() const { return projectiles_; }
    // World 3's fight: the vacuum.
    const std::vector<Vacuum>& vacuums() const { return vacuums_; }
    // Floor buttons and doors (world 3 on).
    const std::vector<Switch>& switches() const { return switches_; }
    const std::vector<Door>& doors() const { return doors_; }
    // The cats' shared "seen you" eyes (s_SpriteSpotted: cat_eyes.png, 22 wide, looping 200 ms;
    // one step per tick from the cats' awareness checks).
    const SpriteAnim& catEyes() const { return catEyes_; }
    bool exitRequested() const { return exitRequested_; }  // leave for the map
    float dimFade() const { return dimFade_; }             // SceneGame +0x57714
    // A medal tapped on the results (GuiLevelGoalButton → guiEvent @0x1ec4d4): its sound, its
    // line in the info popup.
    void pressResultsGoal(int i) {
        if (!results_.active()) return;
        sound(sfx::UiButton);
        results_.pressGoal(i);
    }
    // A touch on the results screen that no button takes: the rest of it hurries.
    void touchResults() { results_.hurry(); }
    // The goals this run won (for the profile once the results are done).
    std::array<bool, 3> goalsWon() const;
    bool goalPartWon(const std::string& type, int value) const;
    // The uses the "at most" goals count (kinds 17..27).
    struct GoalUses {
        int wallHoles = 0, safeHoles = 0, switches = 0, furniture = 0, cups = 0, chillies = 0, balloons = 0, tvs = 0,
            other = 0;
        int shurikenCheese = 0;  // carried cheese struck by a shuriken (an "at least" count)
        // Twine and catnip picked up (Player::pickup's achievement metrics; the "string" and
        // "fish" goals compare them as "at most": hypothesis, run l45_win).
        int twine = 0, catnip = 0;
    } uses_;
    int pathsDrawn() const { return pathsDrawn_; }
    // Where the sheen sweeping over loose cheese is (SceneGame +0x138fc, map x).
    float sheenX() const { return sheenX_; }
    std::int64_t ticks() const { return ticks_; }    // game updates (not while a popup is up)
    std::int64_t frames() const { return frames_; }  // every tick since the level booted

    // Screen points (y down) to map space (y up). Observed for unscrolled levels; the general
    // transform (DAT_003466f8, scroll offset) is not yet recovered.
    Vec2 screenToMap(Vec2 screen) const { return {screen.x + viewLeft_, kScreenHeight - screen.y}; }

private:
    Session(Level level, std::uint32_t seed, Tuning tuning, int strength, const Session* previous, bool wonBefore);

    // Input and the drawn path.
    void handleFinger();
    void finishDrawing();
    int snapPath(bool highlight);
    void moveAllPathPointsAwayFromEdges();

    // The mouse and what it carries.
    void updateMouse(int dtMs);
    float updateTrail(int dtMs);
    void collideObjects();
    void updateObject(LevelObject& object, int dtMs);
    void onFootprint();
    void pulseScoreHud(bool left, bool big);
    void playFluff(int type);
    void updateFluff(int dtMs);
    void updateIntro(int dtMs);
    void wearTeacup(LevelObject& cup);
    void closeCall();
    void updateTeacup(LevelObject& cup, int dtMs);
    void offerKiska();
    void closeKiska();
    CatWorld catWorld(int floor);
    void killMouse(Vec2 meetingPoint);
    void updatePure(int dtMs);
    void startLevelOver();
    void catEffects(const CatWorld& world, int objectId);
    std::map<int, std::set<int>> loopHolders_;  // looping sound -> objects holding it
    bool loopsSilenced_ = false;  // the level was won: no object starts a looping sound again
    void pulseCheesesAfterIntro();
    void updateIris(int dtMs);
    void startResults();
    void startResultsIris();
    void step(int dtMs);
    bool pickup(LevelObject& cheese);
    void hideIn(LevelObject& hole);
    // Floors: a door (types 30 up, 31 down) hides the mouse and shuts the iris on it; shut,
    // the other floor shows and the mouse comes out (ObjControllerFloorTransport,
    // Player::changeFloor, SceneGame::pinholeCallback / changeFloor).
    void useDoor(LevelObject& door);
    void useTrapdoor(LevelObject& door);
    void startFootprints(int kind, int ms, int colour);
    void changeFloor(LevelObject& door, int to, int irisKind, bool score);
    bool roomDoorTransition_ = false;  // going through a room door (Player::changeFloor's flag)
    void arriveOnFloor();
    void leaveHole();
    // --- boss levels: events and cutscenes (session_boss.cpp) ---
    // SceneCutscene @0x1c98d4.. with its child scene (SceneScriptSequence for sequences and
    // paths, SceneSpeech for dialogues) on top of the level.
    struct CutsceneRun {
        bool active = false;
        std::string name;
        std::deque<data::CutsceneStep> queue;
        enum class Child { None, Script, Speech } child = Child::None;
        data::CutsceneStep step;
        int scriptState = 0;      // SceneScriptSequence +0xb8: 0 first call, 1 running, 2 done
        int timerMs = 0;          // the sequence's "nTimer"
        int pathPoint = 0;        // "nCurrPathPoint"
        int oldState = 0;         // "nOldState"
        CatRoute path;            // the path of a path step (SceneScriptSequence::setPath)
        int elapsedMs = 0;        // the script scene's own clock (Scene +8, by the tick)
        Vec2 arcStart, arcEnd;    // "vStart", "vEnd" (launchHenchman)
        int travelMs = 0;         // "nTravelTime"
        float startHeight = 0.0f; // "startHeight" (dunkHenchman2)
        // SceneSpeech @0x187694: the lines still to say, the bubble (a GuiDialogueBox, a bounce
        // frame), and a tap on it (guiEvent: the next line).
        std::deque<data::Speech> speeches;
        BounceFrame box = BounceFrame::hidden();
        data::Speech shown;       // the line in the bubble (configureDialogueBox)
        bool tapped = false;
    };
    void fireEvent(const std::string& event);
    void startCutscene(const std::string& name, const std::vector<data::CutsceneStep>& steps);
    void endCutscene();
    void cutsceneTick(int dtMs);
    void startCutsceneStep(const data::CutsceneStep& step);
    void speechTick(int dtMs);
    void runSequence();
    void sequenceShowFluffy();
    void sequenceWalkPath();
    void sequenceSlamEntrance();
    void sequenceWait();
    void sequenceHideFluffy();
    void sequenceCloseExit();
    void sequenceLaunchHenchman();
    void sequenceShockHenchman();
    LevelObject* firstObjectOfType(int type);
    void updateScrolling();
    void pushByViewEdge();
    void finishScrollingLevel();
    void walkIntoExit(int dtMs);
    void sendExitDoorMessage(LevelObject& door, int message);
    void shakeScreen(int durationMs, int delayMs);
    void updateShake(int dtMs);
    void eatBonus(LevelObject& heart);
    void eatSpeedCheese(LevelObject& cheese);
    void touchTrap(LevelObject& trap, bool touching);
    void comeOutOf(LevelObject& hole);
    bool hideObject(LevelObject& object, const LevelObject& hole);

    // Holes, doors and messages.
    void requestExitOpen();
    void openHole(LevelObject& hole);
    // SceneGame::openBankHoles @0xf8c1c / closeBankHoles @0xf8bc0: messages 1 and 2 to every
    // safe (munch) hole.
    void openBankHoles();
    void closeBankHoles();
    void closeHole(LevelObject& hole);
    void updatePrediction();

    // Score and the level clock.
    void scoreCheese(bool bankHole);
    int scoreTrail(bool intoHome, PokerHand& hand) const;
    void updateGameMode(int dtMs);

    LevelObject* objectById(int id);

    Level level_;
    Tuning tuning_;
    ScoreSettings scoring_;
    std::vector<Roamer> roamers_;            // cars and butterflies
    std::vector<LevelGeometry> floorWalls_;  // per floor; doors switch walls on and off
    int shownFloor_ = 0;
    bool doorTransition_ = false;       // SceneGame +0x1384e
    int doorReadyAtMs_ = 0;             // Player +0x1e0: doors work again from this game time
    std::vector<int> floorVisits_;      // SceneGame +0x11640: 0 unseen, 1 just entered, 2 left
    LevelGeometry& wallsOf(int floor) { return floorWalls_[std::size_t(floor)]; }
    LevelGeometry& here() { return floorWalls_[std::size_t(mouse_.floor)]; }
    std::vector<LevelObject> objects_;
    Mouse mouse_;
    DrawnPath path_;
    GameRandom random_;
    Particles particles_;
    Ambience ambience_;
    std::vector<FluffState> fluff_;
    Intro intro_;
    bool retryRequested_ = false;
    bool retryFromPause_ = false;
    bool exitRequested_ = false;
    PauseMenu pause_;
    std::vector<Cat> cats_;
    std::vector<Droid> droids_;              // the boss droid of a boss fight
    std::vector<Spitter> spitters_;          // world 2's boss
    std::vector<RailTarget> railTargets_;
    std::vector<BeamEmitter> emitters_;
    std::vector<Vacuum> vacuums_;            // world 3's boss
    std::vector<Launcher> launchers_;        // world 4's fight
    std::vector<ReflectorCatch> reflectorCatches_;
    Projectiles projectiles_;                // SceneGame +0x1165c
    std::vector<Switch> switches_;
    std::vector<Door> doors_;
    // The two random draws of every tick (SceneGame +0x136bc, +0x136c0): screen-shake noise,
    // and doors rattle by them.
    int noise_[2] = {0, 0};
    int pipeUses_ = 0;  // pipes the mouse went into this run
    SpriteAnim catEyes_;
    bool mouseCaught_ = false;  // Player +0x70 cleared by Player::kill
    int caughtMs_ = 0;          // Player +0x1f8: time since
    int droppedItem_ = -1;      // Player +0x170: the item that last dropped off the trail
    int dropGuardMs_ = 0;       // Player +0x214
    bool mouseInPlay_ = true;
    Vec2 finalPosition_;        // SceneGame::setFinalPos: where the caught iris closes
    // SceneLevelOver (the level lost): the iris closes on the cat silhouette while the level
    // goes on without its clock and finger (SceneGame::updatePure); shut, the level restarts.
    bool levelOver_ = false;
    bool retryAfterCaught_ = false;
    // The level as it stood when the boss's intro cutscene ended (SnapshotHandler::takeSnapshot
    // from SceneCutscene::hideScene): a retry goes on from here (rollback in
    // SceneGame::reloadFromSnapshot). The snapshot holds the game scene, the map, the player
    // and the objects; the C library generator is not part of it.
    struct Checkpoint {
        int floor = 0;                    // SceneGame +0x130
        int clock = 0, delivered = 0, score = 0;  // LevelState +0x20, +0x24, +0x2c
        int playTimeMs = 0;               // +0x34 (SnapshotHandler +0)
        std::vector<bool> wallsEnabled;   // the floor's map
        Vec2 mousePosition;               // Player +0x1b8
        int mouseState = 0;               // +0x130
        float mouseHeading = 0.0f;        // +0x1d8
        std::vector<int> carried;         // the trail (+0x174 and the objects' links)
        float scoreBoost = 0.0f;          // +0x21c
        int chilliMs = 0, chilliTotalMs = 0;  // +0x220, +0x224
        std::vector<ObjectSnapshot> objects;
    };
    std::shared_ptr<const Checkpoint> checkpoint_;
    void takeCheckpoint();
    void sequenceBlowUpDroid();
    void throwKey(Vec2 at, int floor);
    void restoreCheckpoint(const Checkpoint& c);
    Cat* catFor(int objectId);
    Droid* droidFor(int objectId);
    Spitter* spitterFor(int objectId);
    RailTarget* railTargetFor(int objectId);
    BeamEmitter* emitterFor(int objectId);
    // session_spitter.cpp: world 2's fight.
    void setUpSpitterRoom();
    void updateSpitterRoomObject(LevelObject& o, int dtMs);
    // session_vacuum.cpp: world 3's fight.
    Vacuum* vacuumFor(int objectId);
    void setUpVacuumRoom();
    void updateVacuumRoomObject(LevelObject& o, int dtMs);
    void dropBomb(const LevelObject& dropper);
    // session_turret.cpp: world 4's fight.
    Launcher* launcherFor(int objectId);
    void setUpTurretRoom();
    void updateTurretRoomObject(LevelObject& o, int dtMs);
    bool turretTouchProjectile(LevelObject& o, int projectile);
    bool turretReceiveMessage(LevelObject& o, const GameMessage& message);
    void sequenceTurretSwitch();
    void sequenceDunkHenchman2();
    // SceneGame::sendMessage @0xf87e4: with `applyMap` the walls carrying the tag react first
    // (message 0 flips them, 2 switches them on, others off; of the target type only, if
    // given); then every object on the sender's floor (the shown one for the scene) except the
    // sender that carries the tag or is of the target type receives it.
    void sendMessage(const GameMessage& message, bool applyMap);
    void receiveMessage(LevelObject& o, const GameMessage& message);
    // SceneGame::collideObjects @0x1009dc: each flying projectile against the mouse, what it
    // carries, the objects and the walls.
    void collideProjectiles();
    // Object::touchProjectile @0x112ef4 for the objects that react (the rail spheres).
    void touchProjectile(LevelObject& o, int projectile);
    void dropFromTrail(int id);
    void pushPickupOutOfWalls(LevelObject& o);  // Object::updatePositions for pickups
    // Player::touchProjectile @0x1244a4.
    void projectileHitsMouse(int projectile);
    void updateProjectiles(int dtMs);
    // Whether the level is the top scene (no cutscene, popup or overlay above it).
    bool levelOnTop() const;
    void blowUpSpitter(Spitter& spitter, LevelObject& o);
    static int bossTypeOfWorld(int world);
    // session_switches.cpp: floor buttons and doors.
    Switch* switchFor(int objectId);
    Door* doorFor(int objectId);
    void setUpSwitches();
    void updatePipeFlap(LevelObject& o, int dtMs);
    void switchTickSound(Switch& s, int effect);
    bool pressSwitch(LevelObject& o, Switch& s, int presser);
    void pressSwitchesByCats();
    void updateSwitch(LevelObject& o, Switch& s, int dtMs);
    void updateDoor(LevelObject& o, Door& d, int dtMs);
    void switchReceiveMessage(LevelObject& o, Switch& s, const GameMessage& message);
    void doorReceiveMessage(LevelObject& o, Door& d, const GameMessage& message);
    void trapdoorReceiveMessage(LevelObject& o, const GameMessage& message);
    // What the droid sees of the level, and where its messages and catches go.
    BossWorld bossWorld(int floor);
    // SceneGame::sendMessage(message, true) from a trigger line on `floor`.
    void sendLineMessage(int floor, int type, int tag, int lineType);
    float dimFade_ = 0.0f;
    std::array<ResultsGoal, 3> goalLines(bool withThisRun) const;
    void updateDim(int dtMs, bool paused);
    int startStrength_ = 1;
    HandTutorial hand_;
    Iris iris_;
    ResultsScreen results_;
    WorldCompleteScreen worldComplete_;
    bool worldCompletePending_ = false;  // pushed in the tick that won; runs from the next
    LevelRecord record_;
    int pathsDrawn_ = 0;  // the "paths" metric of the level goals
    std::array<int, 8> handsScored_{};  // trails scored per poker hand (combo goals)
    float sheenX_ = 0.0f;
    NoticePopup notice_;
    KiskaPopup kiska_;
    KiskaOffers kiskaOffers_;
    std::int64_t wallClock_ = 0;
    Hud hud_;
    int musicTrack_ = 0;
    std::string music_;               // the level's track (SceneGame::playMusic @0xf8ed8)
    bool musicPlaying_ = true;        // MusicPlayer::isPlaying (the map's music at the start)
    void startMusic();
    std::vector<SoundEvent> soundEvents_;
    int noteStep_ = 0;                // SceneGame +0x13854: next score note
    int attempts_ = 0;
    bool wonBefore_ = false;                // plays of the level in this sitting (retries count)
    int tutorialMs_ = 0;              // SceneInGameTutorial +8
    int tutorialAimMs_ = -1;          // map 705: when the cat took aim
    int tutorialSide_ = 1;
    int crumbComboMs_ = 0;            // SceneGame +0x13898: a crumb within this continues the run
    int crumbCombo_ = 0;              // SceneGame +0x1389c: crumbs in the current run
    void eatCrumb(LevelObject& crumb);
    int noteTimerMs_ = 0;             // +0x13858: back to the first note when it runs out
    void sound(int effect, SoundEvent::Kind kind = SoundEvent::Play, bool loop = false);
    void playScoreSound(bool big);
    void ambienceSounds(const std::vector<std::string>& started);
    FingerState finger_;
    LevelProgress progress_;
    PathPrediction prediction_;
    std::vector<int> carried_;
    int carryCapacity_ = 1;  // cheese the mouse can drag at once
    int snapHold_ = -1;      // object the path end is still inside after a snap
    bool fingerDrawing_ = false;  // the finger is drawing the path (the original's "current path")
    float viewLeft_ = 0.0f;       // horizontal scroll of the view (0 on single-screen levels)
    Shake shake_;
    CutsceneRun cutscene_;
    int spottedDelayMs_ = 0;      // SceneGame +0x13890: the wait after the boss cat saw the mouse
    int spottedTimerMs_ = 0;      // SceneGame +0x138a0: time into that wait (the caught line)
    float scrollSpeed_ = 0.3f;    // infinite levels: the view's step per tick (SceneGame +0x13834)
    int gameTimeMs_ = 0;
    std::array<int, 5> deliveredOn_{};  // SceneGame +0x1387c: cheese delivered on each floor
    std::array<int, 5> floorCheese_{};  // +0x13868: cheese (and mouse traps) placed on each floor
    std::int64_t ticks_ = 0;
    std::int64_t frames_ = 0;
};

}  // namespace doc::game
