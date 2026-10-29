#pragma once
#include "graphics/text.hpp"
#include "puzzles/a2_puzzle_progress.hpp"
#include "sound/sound_object.hpp"
#include "story/a2_game_state.hpp"
#include "story/a2_narration.hpp"
#include "system/audio_service.hpp"
#include "system/a2_checkpoint.hpp"
#include "system/game_types.hpp"
#include "system/a2_save_file.hpp"

namespace picarat {
class ScriptContext;

// The story after the opening: the scene router (overlay 2, 02068ccc /
// 020688a8), the room scene (overlay 7, 020f2118) and the event scene
// (overlay 8, 020f3b20), working on GameState. See a2_story_game.cpp for an
// overview. Room widgets, menus and some event effects are drawn natively;
// their known differences are recorded in the research notes.
class StoryGame final : private NarrationHost {
public:
    StoryGame(GraphicsAssets& assets, PresentFrame present, NameEditor name, AudioService* audio,
              CheckpointStore* saves = nullptr);
    void run(bool puzzleShortcut = false);
    void resume(const Checkpoint& checkpoint);
    // The title's Continue / Secret after a slot was loaded into `loaded`
    // (ov9 020f0584..020f065c): the chp_inf event of its story step, or
    // the room; Secret opens the bonus menu (scene 28).
    void continueFrom(const GameState& loaded, bool secret);
    // The app's movie player: replays a movie as the movie theatre does
    // (ov23 020f2f1c): its event with the replay flag (+0x821c) and the
    // theatre (scene 36) following; returns when the theatre would show.
    // Nothing is saved.
    void replayMovie(unsigned event);
    // The app's minigame free play: runs one minigame scene (camera 14,
    // tea 15, hamster 16, photo album 27) on `progress`, the free play's own
    // progress block, and returns when the minigame is left. What the scene
    // would do next (back to the bag, or an event when a goal is reached) is
    // not run. `progress` holds the result, also when the game is stopped.
    void playMinigame(std::uint8_t scene, SaveProgress& progress);
    std::function<void(int, unsigned)> onCommand; // Optional research observer.
    // Research-only exploration hook (local story explorer): replaces the
    // puzzle scene's play with a solved/unsolved outcome. The app never sets it.
    std::function<bool(unsigned internal)> researchSolvePuzzle;
    // Research-only (local story explorer): replaces the herb tea scene's
    // play; it must apply the scene's own state changes. The app never sets it.
    std::function<void(GameState&)> researchTea;
    // ov12 name editor for scene 37 (hamster), supplied by Game.
    std::function<NameEditor()> nameScene;
    // The cartridge save image (three slots), supplied by Game; null in
    // research tools, where saving does nothing.
    SaveFile* saveFile = nullptr;

    const GameState& state() const { return state_; }

    // One loaded place file (n_place%i_%i.dat, 910 bytes; ov7 020f06f8).
    struct RoomObject {
        int x = 0, y = 0, w = 0, h = 0, icon = 0;
        unsigned text = 0;
    };

    struct RoomEvent {
        int x = 0, y = 0, w = 0, h = 0, sprite = 0;
        unsigned event = 0;
        bool visible = true;
        AnimationPlayer animation;
    };

    struct RoomExit {
        int x = 0, y = 0, w = 0, h = 0, sprite = 0, type = 0, floor = 0, markerX = 0, markerY = 0;
        unsigned target = 0;
    };

    struct RoomAnimation {
        int x = 0, y = 0;
        std::string name;
        AnimationPlayer animation;
    };

    struct Room {
        unsigned label = 0;
        int markerX = 0, markerY = 0, bottom = 0, top = 0, sound = 0;
        std::array<std::array<int, 4>, 4> coins{};
        std::vector<RoomObject> objects;
        std::vector<RoomEvent> events;
        std::vector<RoomExit> exits;
        std::vector<RoomAnimation> animations;
        std::string name;
    };

    const Room& room() const { return room_; }

    bool moveMode() const { return moveMode_; }

private:
    struct Actor {
        const Sprite* sprite = nullptr;
        AnimationPlayer body, child;
        int id = 0, position = 0;
        bool visible = false;
        float alpha = 31.0f, speed = 0.0f; // ov2 actor +0x10 / +0x14 (02081e90)
        int shake = 0;                     // ov2 actor +0x0c (script 0x7e)
    };

    GraphicsAssets& assets_;
    PresentFrame present_;
    NameEditor name_;
    AudioService* audio_;
    GameText text_;
    GameState state_;
    SceneKind scene_ = SceneKind::Story;
    int detail_ = 0, topBrightness_ = 0, bottomBrightness_ = 0;
    std::uint32_t tick_ = 0;
    Touch touch_;
    bool wasDown_ = false, skip_ = false;
    // Reported to the host while a movie or narration plays: its skip
    // (system/game_types.hpp) ends them through skip_.
    bool skippable_ = false;
    // Set while the room waits for a tap: the frame reports the room's
    // touch areas (TouchArea; the host's touch-area help).
    bool exploring_ = false;
    std::string top_, bottom_, dialogue_, caption_, boundary_;
    std::vector<Actor> actors_;
    int speaker_ = 0, voice_ = -1;
    bool dialogueBox_ = false;

    struct {
        int id = -1;
        float volume = 1.0f, fade = 0.0f;
    } windowStream_; // text window +0x128..+0x130 (020809d4)

    unsigned narrationMode_ = 0;
    int narrationAlpha_ = 0, narrationFirst_ = 0, narrationEnd_ = 0;
    Surface movieImage_;
    std::string movieSubtitle_;
    Room room_;
    bool roomVisible_ = false, moveMode_ = false;
    int selectedExit_ = -1;
    std::string objectText_;
    int objectIcon_ = -1;
    std::vector<std::string> listLines_; // ov8 020f25e8 memo lines (x 0x19, y 0x11 + 0x16*i)
    std::string prizeText_, prizeIcon_, prizeAnimation_ = "gfx";
    int prizeY_ = 100, prizeIconY_ = 0x38,
        prizeIconX_ = -1; // ov8 prize window (+0x04 prize_window2.spr)
    int choice_ = -1;
    bool choiceButtons_ = false; // yes/no buttons on the prize window (pressed: 0/1)
    void checkpointAt(std::uint8_t scene, std::uint16_t event);
    std::uint32_t randomState_ = 1;    // Native session seed; see G12.
    int shakeMain_ = 0, shakeSub_ = 0; // ov2 layer manager +0x8c / +0x90 (0206fcb8 / 0206fbc4)
    std::uint8_t tutorialsSeen() const;
    CheckpointStore* saves_;
    bool checkpointEnabled_ = true;
    void checkpoint();
    int movieId_ = 0;
    Surface render();
    void frame();
    void wait(int count);
    void waitRelease();
    void fade(bool in, int frames, int screens = 3);
    [[noreturn]] void stopAtBoundary(const std::string& reason);
    void drawSprite(Surface&, const std::string&, const std::string&, int, int);
    void drawSpriteFrame(Surface&, const Sprite&, AnimationPlayer&, int, int);
    const Sprite* eventCharacter(int id);
    void selectActor(Actor&, unsigned);
    void actorAnimation(int, const std::string&);
    void drawActor(Surface&, Actor&);
    std::string loadText(const std::string&);
    PresentFrame subscenePresent();
    void say(std::string text, int speaker = 0, int voice = -1, int blip = -1, bool quiet = false,
             const std::string* after = nullptr);
    // Speaker talk animation (ov2 talk object 0x0207d1d4: +0x84 speaker,
    // +0x88 talking, +0x89 animation to restore) and the page wait 0x02080e44.
    int talkSpeaker_ = 0;
    bool talking_ = false;
    unsigned talkSaved_ = 0;
    void talk(bool on);
    void pageWait(int voice);
    bool sleep_ = false;  // HostInput::sleep of the last frame (movie scene only)
    Button menuButton_;   // ov7 +0x500 menu_icon.spr
    Button cameraButton_; // ov7 +0x504 camera_icon.spr
    bool roomPhoto_ = false, cameraVisible_ = false; // ov7 +0x52e / +0x52f (020f08e4)
    void takePhoto();                                // ov7 020f2118 camera branch and 020f3c8c
    // Native notice for a bag destination that is not reconstructed yet.
    void unavailable(const std::string& what);
    bool yesNo();                      // native yes/no window (0x7b adapter)
    std::uint32_t playSeconds() const; // ov2 02083f80 (progress +0x244 plus this session)
    std::uint32_t sessionStartTick_ = 0;
    void foldPlayTime(SaveProgress& p) const;
    void fileSave(int resume);   // script 0x7b: ov2 020866d0 file dialog
    std::uint32_t foldForSave(); // 02083ff4

    GameSound* sound() { return audio_ ? audio_->sequenced() : nullptr; }

    // Recovered scene router.
    void sceneLoop();  // 02068ccc main loop
    void dispatch();   // 020688a8
    void roomRunner(); // 02068d18
    void eventScene(); // 02068e50 + ov8 020f3b20
    // The event script's command handlers, by topic (eventScene).
    void bindFlowCommands(ScriptContext& context, unsigned id);
    void bindStageCommands(ScriptContext& context);
    void bindSoundCommands(ScriptContext& context);
    void bindProgressCommands(ScriptContext& context);
    void movieScene();        // 02068ef4
    void narrationScene();    // 02069040
    void puzzleScene();       // 020697e0
    void puzzleResultScene(); // 02069468
    void dialogue(int event, int text);
    void narration(unsigned mode, int event);
    void movie(int event);
    void mysteries();
    void mysteryCommand(int menuIndex);
    // ov7 room scene.
    void loadRoom();                             // 020f06f8
    void enterRoom();                            // 020f08e4
    bool roomScene();                            // 020f2118
    bool roomEventObjects();                     // 020f1c98
    bool eventAnswers(const RoomEvent& e) const; // 020f1c98 (photo puzzle object)
    Rect eventRect(const RoomEvent& e);          // 020f1c98, 02077800
    int coinPlace() const;                       // 020f1ef8
    std::vector<TouchArea> roomAreas();
    bool roomHintCoins();                                   // 020f1ef8
    bool roomTouchObjects();                                // 020f1bfc
    bool roomMoveMode();                                    // 020f2948
    bool roomDirectExits();                                 // 020f2b5c
    bool takeExit(const RoomExit&);                         // 020f2620
    void triggerRoomEvent(unsigned event);                  // 020f2e1c
    void showObjectText(const std::string& text, int icon); // 020f10c0
    bool puzzleEventSolved(unsigned event);
    int duration(int id);
    void spotSound(unsigned event);
    void exitSound(int type, int floor, unsigned target);
    void playVoice(int id, int part);

    void update() override {}

    void waitOneFrame() override { frame(); }

    void present() override { frame(); }

    void draw(int actor, int alpha) override;

    bool skipRequested() override { return skip_; }

    bool abortRequested() override { return false; }

    // ov10 020f01f4: tracked stream; 0207fb14 busy; 0207fbcc stop.
    void startVoice(int route, int id, int part, float volume, float fade) override {
        if (auto s = sound()) {
            s->playTrackedStream(route, id, part, volume, fade);
        }
    }

    bool voicePlaying() override {
        auto s = sound();
        return s && s->streamBusy();
    }

    void stopVoice(float fade) override {
        if (auto s = sound()) {
            s->stopStream(fade);
        }
    }

    void fadeMusic(int, float) override {}
};
} // namespace picarat
