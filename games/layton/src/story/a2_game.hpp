#pragma once
#include "graphics/graphics.hpp"
#include "story/a2_game_state.hpp"
#include "system/audio_service.hpp"
#include "system/a2_character_names.hpp"
#include "system/a2_checkpoint.hpp"
#include "system/game_types.hpp"
#include "system/a2_save_file.hpp"
#include "system/script.hpp"
#include "system/a2_timing.hpp"
#include <deque>
#include <functional>
#include <memory>

namespace picarat {
// The game from power-on: the logo sequence (driven by logo.gds), the
// title screen with its train animation and Start/Continue buttons, name
// entry with the on-screen keyboard (overlay 12), and then the story
// (StoryGame) until the app closes. It also hosts Picarat's puzzle
// collection, a native addition where every puzzle can be played on its
// own without touching the story save.
//
// The host app only supplies frames, input and sound output; everything
// else happens here.
class Game final : public OpeningHost, public FrameHost {
public:
    Game(std::filesystem::path assets, PresentFrame present, AudioService* audio = nullptr,
         CheckpointStore* saves = nullptr);
    void run();
    // A host convenience: the boot goes straight to the title, without the
    // logo sequence (logo.gds only shows the logos and plays their jingle;
    // the title sets its screens up itself). The app sets it; research tools
    // keep the power-on timeline.
    bool startAtTitle = false;
    // A host convenience: the first start continues from the checkpoint
    // (StoryGame::resume: the room or event it was written at), without the
    // title; without a checkpoint it can continue from, the title as usual.
    bool continueCheckpoint = false;
    // The app's movie player: replays the movie of a movie-theatre event
    // (StoryGame::replayMovie) and returns.
    void replayMovie(unsigned event);
    // The app's minigame free play (StoryGame::playMinigame).
    void playMinigame(std::uint8_t scene, SaveProgress& progress);
    void runNameForTesting();
    NameEditor nameHamster();
    Surface render();

    GameSnapshot snapshot() const {
        GameSnapshot s{scene_, name_, tick_};
        s.skippable = skippable_;
        return s;
    }

    static const char* sceneName(SceneKind scene);

private:
    struct Actor {
        const Sprite* sprite = nullptr;
        AnimationPlayer player;
        int x = 0, y = 0;
    };

    using Restart = RestartGame;
    GraphicsAssets assets_;
    PresentFrame present_;
    CheckpointStore* saves_;
    AudioService* audio_; // Optional only for render/input-only research probes.
    OpeningTiming timing_{*this};
    SceneKind scene_ = SceneKind::Logos;
    std::string mainBackground_, subBackground_;
    Touch touch_;
    bool wasDown_ = false, resume_ = false;
    // The host's skip (system/game_types.hpp): the logos are skippable.
    bool skip_ = false, skippable_ = false;
    bool nameTestOnly_ = false;
    std::uint32_t tick_ = 0;
    NameEditor name_;
    CharacterNames names_ = loadCharacterNames(assets_.root);
    std::deque<NameInput> nameInput_;
    std::array<Bytes, 4> keys_;
    Actor train_, tracks_, frame_, start_, logo_, cursor_, nameOk_, normal_, special_, erase_,
        space_, badName_;
    Actor continue_, secret_; // ov9 +0x04 / +0x08, shown with a used save slot
    Button continueButton_, secretButton_;
    bool slotsUsed_ = false;             // ov9 +0x24
    std::unique_ptr<SaveFile> saveFile_; // the cartridge save (three slots)
    // The slot chosen with Continue / Secret: its state and the player's name.
    std::unique_ptr<GameState> loaded_;
    NameEditor loadedName_;
    Button startButton_, okButton_, normalButton_, specialButton_, eraseButton_, spaceButton_,
        shiftButton_, symbolButton_;
    std::array<Button, 52> keyButtons_;
    int titleScroll_ = 0;
    int titleBlend_ = 15;
    bool showBadName_ = false;
    bool advanceAnimations_ = true;
    enum class TitleChoice { NewGame, Continue, Secret };
    TitleChoice title();
    void checkSave();        // 0206bbac
    bool loadSlot(int mode); // ov9 020f04d4..020f0660: the load dialog
    void collection(int initialId);
    void nameEntry(bool hamster = false);
    void initializeName(bool hamster = false);
    void nameUpdate();
    void selectLayout(int layout);
    void initializeActor(Actor& actor, const std::string& path,
                         const std::string& animation = "gfx", int x = 0, int y = 0,
                         const std::string& position = "");
    void drawActor(Surface& image, Actor& actor, int screenOffset = 192);
    void updateVisual(Button& button, Actor& actor, bool labelAnimations);
    void nextFrame();
    void background(bool sub, const std::string& name, std::int32_t layer) override;

    void fadeIn() override { timing_.fadeIn(); }

    void fadeOut() override { timing_.fadeOut(); }

    void wait(std::int32_t count, bool touchRelease) override;
    void audioRequest(std::int32_t, std::uint32_t, std::uint32_t, std::int32_t) override;

    void prepareFrame() override {}

    void submitFrame() override {}

    void waitVBlank() override { nextFrame(); }

    void afterVBlank() override { ++tick_; }

    bool pollTouchRelease() override { return touch_.released; }

    void brightness(bool sub, std::int32_t value) override;
};
} // namespace picarat
