#pragma once
// One running game: the game loop (Game, src/story/a2_game.hpp) on its own
// thread, and what it shares with the window's thread.
//
// The game thread produces one frame, hands its pixels over and waits. The
// window's thread wakes it 60 times a second with tick(), passing the latest
// input. This keeps the game at the DS's 60 frames per second, whatever the
// display's refresh rate.
//
// Minimising the window (or hiding the app) works like closing the DS lid:
// the game pauses its sound and sleeps until the window returns.
#include "shell/screen_layout.hpp"
#include "system/game_types.hpp"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace picarat {
class Game;
}

namespace picarat::shell {

class GameSession {
public:
    // Starts the story (puzzle 0) or one puzzle of the collection (its ID);
    // with `movieEvent`, only replays that movie-theatre event's movie, then
    // returns to the library.
    GameSession(std::filesystem::path assets, std::filesystem::path checkpoint, int puzzle,
                unsigned movieEvent = 0);
    // The second adventure's story, continuing from its checkpoint
    // (Game::continueCheckpoint; the title when there is none).
    static std::unique_ptr<GameSession> adventure2Continue(std::filesystem::path assets,
                                                           std::filesystem::path checkpoint);
    // Plays puzzle `number` of the third adventure from its pack (no sound
    // yet), then returns to the library.
    static std::unique_ptr<GameSession> adventure3Puzzle(std::filesystem::path pack, int number);
    // Plays puzzle `id` (its catalogue number) of the fourth adventure from
    // its pack, as its puzzle index does (no sound yet), then returns to
    // the library.
    static std::unique_ptr<GameSession> adventure4Puzzle(std::filesystem::path pack, int id);
    // Plays one puzzle of the fifth adventure (its catalogue id; the 3DS
    // screens, see geometry()).
    // `sixth`: the sixth adventure (same runtime, its own definitions).
    // The fifth adventure's story from the boot (no save files yet).
    // `checkpoint`: the app's checkpoint file (written whenever a place is
    // ready; empty: none); with `resume` the story continues from it instead
    // of opening the title.
    static std::unique_ptr<GameSession> adventure5Story(std::filesystem::path pack,
                                                        std::filesystem::path saves,
                                                        std::filesystem::path checkpoint = {},
                                                        bool resume = false);
    static std::unique_ptr<GameSession> adventure5Puzzle(std::filesystem::path pack, int id,
                                                         bool sixth = false);
    // Plays the fourth adventure from its title (adventure4::Game); its save
    // slots live in the folder `saves`.
    static std::unique_ptr<GameSession> adventure4Story(std::filesystem::path pack,
                                                        std::filesystem::path saves);
    // Minigame free play of the fourth adventure: minigame `which` (0 the
    // railway, 1 the puppet show, 2 the fish) on `progress`
    // (adventure4::minigameRecord); finish() hands the record back.
    static std::unique_ptr<GameSession> adventure4Minigame(std::filesystem::path pack, int which,
                                                           std::vector<std::uint8_t> progress);
    // Plays puzzle `id` (internal number) of the first adventure in the
    // language folder `language`, then returns to the library.
    static std::unique_ptr<GameSession> adventure1Puzzle(std::filesystem::path pack, int id,
                                                         std::string language);
    // Plays the first adventure from the title (adventure1::Story) in the
    // language folder `language`; its cartridge backup is the file `backup`,
    // the app's checkpoint the file `checkpoint`. With `resume` it continues
    // from the checkpoint instead (the title when there is none).
    static std::unique_ptr<GameSession> adventure1Story(std::filesystem::path pack,
                                                        std::filesystem::path backup,
                                                        std::filesystem::path checkpoint,
                                                        std::string language, bool resume);
    // Plays the third adventure from the title (adventure3::Story); its
    // save slots and the app's checkpoint live in `saves`. With `resume` it
    // continues from the checkpoint (the title when there is none).
    static std::unique_ptr<GameSession> adventure3Story(std::filesystem::path pack,
                                                        std::filesystem::path saves, bool resume);
    // Plays only movie `movie` of the third adventure, as its movie page does,
    // then returns to the library.
    static std::unique_ptr<GameSession> adventure3Movie(std::filesystem::path pack, int movie);
    // The first adventure's theatre: movie n alone, then back to the library.
    static std::unique_ptr<GameSession> adventure1Movie(std::filesystem::path pack,
                                                        std::string language, int movie);
    // Minigame free play: one minigame on its own progress, then back to the
    // library. Second adventure: a minigame scene (Game::playMinigame) on a
    // 0x320-byte progress block. Third adventure: minigame `which` (0 the
    // parrot's menu, 1 the toy car's courses, 2 the picture books) with
    // everything open, on `progress` (Adventure3Minigames' record).
    static std::unique_ptr<GameSession> adventure2Minigame(std::filesystem::path assets,
                                                           std::filesystem::path checkpoint,
                                                           std::uint8_t scene,
                                                           std::vector<std::uint8_t> progress);
    static std::unique_ptr<GameSession> adventure3Minigame(std::filesystem::path pack, int which,
                                                           std::vector<std::uint8_t> progress);
    // The first adventure's trunk pages in free play (0 robot dog, 1 hotel,
    // 2 painting) on the record of shell/minigames.hpp.
    static std::unique_ptr<GameSession> adventure1Minigame(std::filesystem::path pack,
                                                           std::string language, int which,
                                                           std::vector<std::uint8_t> progress);
    // Stops the game and returns a minigame's progress (empty for others).
    std::vector<std::uint8_t> finish();
    // Stops the game thread and waits for it.
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // Lets the game run its next frame (call at 60 Hz).
    void tick();
    // For embedding hosts (src/embed), instead of tick(): runs exactly one
    // frame and waits until it is out, then renders that frame's sound into
    // `stereo` (interleaved stereo at `rate`; the game's AudioStreams join
    // the session, platform/audio_pull.hpp). The host decides when the game
    // advances: between steps the game thread waits. False (nothing run)
    // while the game is still loading its first frame, or once it has ended.
    bool step(std::span<float> stereo, int rate);
    // The game thread has ended: the story is over, an error (error()), or
    // back to the library (returnedToLibrary()).
    bool ended() const;

    // The screens' sizes (DS unless the game says otherwise).
    ScreenGeometry geometry() const;
    // The latest finished frame: the two screens, RGBA, top first, each
    // row geometry().frameWidth() pixels wide (the lower screen at the
    // left).
    // Returns false when no frame has been produced yet.
    bool frame(std::vector<std::uint8_t>& rgba) const;
    GameSnapshot state() const;

    // The game ended with an error (shown to the player), or a puzzle
    // started from the collection has ended.
    std::string error() const;
    bool returnedToLibrary() const;
    // A puzzle started from the collection was solved (for puzzle mode's
    // own progress).
    bool puzzleSolved() const;
    // Forgets the third adventure's collection records kept while the app
    // runs (hints, failures, solved state), when puzzle mode is reset.
    static void forgetAdventure3Collection();
    static void forgetAdventure4Collection();

    // Stylus input on the lower screen, in DS pixels; `dragged` marks a
    // move while pressed.
    void touch(bool down, int x, int y, bool dragged);
    // Drops pending stylus input and lifts the stylus.
    void liftStylus();
    void nameInput(NameInput input);
    void blow(bool on);
    // The pad held (HostInput::pad bits) and the tilt.
    void pad(unsigned bits);
    void tilt(float x, float y);
    void shake(bool on);
    // The device's accelerometer (Host::motion), nothing without one.
    void motion(const std::optional<std::array<float, 3>>& up);
    void sleep(bool on);
    void restart();
    void loadCheckpoint();
    void skip();

private:
    struct Shared;
    GameSession() = default;
    GameSession(std::filesystem::path assets, std::filesystem::path checkpoint, int puzzle,
                std::function<void(picarat::Game&, Shared&)> body);
    static std::unique_ptr<GameSession>
    adventure1Session(std::filesystem::path pack, std::filesystem::path backup,
                      std::string language, int movie, int minigame = -1,
                      std::vector<std::uint8_t> progress = {},
                      std::filesystem::path checkpoint = {}, bool resume = false);
    static std::unique_ptr<GameSession>
    adventure3Session(std::filesystem::path pack, std::filesystem::path saves, int movie,
                      std::optional<std::vector<std::uint8_t>> minigame, int which = 0,
                      bool resume = false);
    // Starts a game thread running `body`; its sound joins the session's pull.
    static std::thread spawn(std::shared_ptr<Shared> shared, std::function<void()> body);
    std::shared_ptr<Shared> shared_;
    std::thread worker_;
};

} // namespace picarat::shell
