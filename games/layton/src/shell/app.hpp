#pragma once
// The application: the window's main loop, switching between the library
// and a running game, the ROM import, shortcuts and message boxes.
//
// How it fits together:
//  * The main thread runs the window (platform::Host), draws the shell's
//    screens in software and turns input into actions.
//  * The ROM import (src/import) runs on a worker thread; its progress is
//    shown on the library's game card, and Cancel stops it at its next step.
//  * The game runs on its own thread (GameSession) and is woken at 60 Hz.
//  * Sound is mixed on the audio thread (AudioOutput).
//
// Shortcuts use Command on macOS and Control elsewhere:
//   1 library, K puzzle collection, R restart, L resume the checkpoint,
//   . skip a movie or narration, V paste, Q quit.
#include "import/rom_image.hpp"
#include "platform/host.hpp"
#include "shell/game_menu.hpp"
#include "shell/game_screen.hpp"
#include "shell/game_session.hpp"
#include "shell/library.hpp"
#include "shell/library_screen.hpp"
#include "shell/minigames.hpp"
#include "shell/music_player.hpp"
#include "shell/puzzle_mode.hpp"
#include "shell/widgets.hpp"
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace picarat::shell {

// Skipping (system/game_types.hpp): the function, apart from what triggers
// it. Every trigger calls App::requestSkip(); which triggers are on is
// decided here, in one place. The in-game menu's Skip is the default; a
// double click on the screens is ready but off (it could happen by accident
// on a phone); the shortcut is Command/Control + period.
struct SkipTriggers {
    bool menu = true;
    bool doubleClick = false;
    bool shortcut = true;
};

inline constexpr SkipTriggers kSkipTriggers{};

class App {
public:
    // `libraryRoot` holds the imported pack; with `testRoot` the checkpoint
    // is kept there too, apart from the player's own.
    App(platform::Host& host, std::filesystem::path libraryRoot, bool testRoot);
    ~App();

    // Runs until the window is closed.
    void run();

private:
    // A software-drawn image and the texture that shows it.
    struct Layer {
        platform::Image image;
        std::unique_ptr<platform::Texture> texture;
        // Makes the layer `w` x `h` pixels; true when it was (re)created.
        bool resize(platform::Host& host, int w, int h);
    };

    // The import's progress, shared with its worker thread.
    struct Import {
        std::thread worker;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::mutex mutex;
        bool running = false, finished = false, cancelling = false;
        double progress = 0;
        std::string message;
        int outcome = 0; // 0 imported, 1 failed, 2 cancelled
        std::string error;
        int card = -1;                    // the card showing the import
        std::optional<import::Game> game; // known once the ROM is identified
    };

    // Message boxes, so the answer can be routed.
    enum DialogId {
        kNotice = 1,
        kStoryEnd = 2,
        kResetPuzzles = 4,
        kResetMinigames = 6,
        kRemoveGame = 8,
        kRestartStory = 9,
    };

    // Where the library returns to when a game stops.
    enum class Back { Games, Hub, Collection, Movies, Minigames };

    void handle(const platform::Event& event);
    bool shortcut(const platform::Event& event);
    void update();
    void render();

    void reloadLibrary();
    void chooseRom(int card);
    // `card` shows the progress until the ROM is identified (-1: none).
    void startImport(const std::filesystem::path& rom, int card);
    void cancelImport();
    void finishImport();

    // `resume`: the story continues from the app's checkpoint.
    void play(int puzzle, bool resume = false);
    void playAdventure3(int number, bool resume = false);
    // The first adventure's language setting (-1: follow the system).
    void setLanguage(int chosen);
    void refreshLanguage();
    void stopGame();
    void showLibrary(Back back);
    // Hands both games' story saves and puzzle-mode progress to the library.
    void refreshProgress();
    // The imported data of a game (its pack folder); saves and progress
    // live beside the packs and stay.
    const std::filesystem::path& packOf(int card) const;
    void removeGame(int card);
    // The music player: opened from a game's hub, ticked at 60 Hz while open.
    // A movie from the movie theatre, as a session shown on one screen.
    void playMovie(int card, int index);
    void renderMovie(float width, float height);
    // A minigame in free play, as its own session; its progress is stored
    // whenever it is left.
    void playMinigame(int card, int index);
    std::string movieLabel() const; // "Pandora's Box · Movie 02"
    // The in-game menu (Escape / Back): opening freezes the game; Quit
    // returns to the page the game was started from.
    void openGameMenu();

    // Skipping: every trigger calls requestSkip() (see kSkipTriggers).
    void requestSkip();
    void quitGame();
    std::string sessionTitle() const;
    void openMusic(int card);
    std::string adventure1LanguageCode() const; // the language the first game plays in
    void closeMusic();
    void tickMusic();
    void resetPuzzles(int card);
    void playAdventure1(int id, bool resume = false);
    // One puzzle of the fourth adventure's collection (its catalogue
    // number).
    void playAdventure4(int id);
    void playAdventure5(int id, bool sixth = false);
    void playStory(int card, bool resume);
    void sessionStarted();
    // Removes the app's checkpoint of a game (the story then starts on the
    // title screen).
    void clearCheckpoint(int card);
    // puzzleMode_ index of a library card.
    static int puzzleModeSlot(int card);
    void restart();
    void loadCheckpoint();

    // Where the library sits in the window, its design size and scale
    // (points per design unit); also hands the design size to the screen.
    LibraryScreen::Viewport libraryViewport();

    platform::Host& host_;
    std::filesystem::path pack_, adventure3Pack_, checkpoint_, adventure3Saves_, adventure4Saves_, adventure5Saves_;
    std::filesystem::path adventure1Pack_, adventure1Language_, adventure1Saves_,
        adventure1Checkpoint_;
    // The fourth to sixth adventures (cards kAdventure4Card...): imported
    // only, except the fourth's puzzle collection.
    std::filesystem::path importOnlyPacks_[3];
    // Puzzle mode's own progress: the second adventure, the third, the
    // first, the fourth.
    PuzzleModeProgress puzzleMode_[6];
    // Minigame free play's own progress.
    Adventure2Minigames minigames2_;
    Adventure3Minigames minigames3_;
    Adventure4Minigames minigames4_;
    Adventure1Minigames minigames1_;
    Library library_;
    Library adventure3_;
    Library adventure1_;
    Library importOnly_[3];

    static bool importOnly(int card) { return card >= LibraryScreen::kAdventure4Card; }

    int chosenCard_ = -1; // the card whose Import opened the file chooser
    LibraryScreen libraryScreen_;
    LibraryScreen::Status status_;
    GameScreen gameScreen_;
    ui::Dialog dialog_;
    Import import_;

    std::unique_ptr<GameSession> session_;
    std::unique_ptr<MusicPlayer> music_;
    bool movieMode_ = false;    // the session plays one movie of the theatre
    bool minigameMode_ = false; // the session plays one minigame in free play
    int movieNumber_ = 0;       // its number on the page
    std::string minigameLabel_; // "Pandora's Box · Hamster"
    GameMenu gameMenu_;
    // The touch-area help (the menu's switch), on or off per game, kept in
    // touch-areas.txt (one game code per line: on).
    std::filesystem::path touchAreasFile_;
    std::set<std::string> touchAreasOn_;
    std::vector<TouchArea> touchAreasDrawn_; // what touchLayer_ shows
    static const char* gameCode(int card);
    bool touchAreasShown() const;
    void setTouchAreas(bool on);
    int musicCard_ = 0;
    bool musicSlept_ = false; // paused while the window is minimised
    double musicStart_ = 0;
    long musicTicks_ = 0;
    int playedCard_ = LibraryScreen::kAdventure2Card; // whose hub or collection to return to
    int playedPuzzle_ = 0;                            // the collection puzzle playing, or 0
    LibraryScreen::Action pending_;                   // what a confirmation dialog is about
    GameSnapshot state_{};
    bool announced_ = false; // the story-end message was shown for this boundary
    double tickStart_ = 0;
    long ticksDone_ = 0;

    bool quit_ = false, sleeping_ = false, dirty_ = true;
    // Idle library frames are skipped, except until this time (host
    // seconds): after waking or a lost picture, a phone may drop the first
    // frames while it recreates the window's surface.
    double repaintUntil_ = 0;
    std::string title_;
    Layer libraryLayer_, blowLayer_, touchLayer_, menuLayer_, dialogLayer_;
    std::unique_ptr<platform::Texture> screens_; // the two game screens, stacked
    ScreenGeometry screensGeometry_;             // the screens_ texture's layout
    std::vector<std::uint8_t> frame_;
};

} // namespace picarat::shell
