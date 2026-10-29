#pragma once
// The library screen, drawn entirely by the shell. It has three views:
//  * the game cards (two rows of three in a landscape window, one column
//    that scrolls in a portrait one). A game not imported yet offers
//    an "Add game..." button across its picture that imports its ROM;
//    the card then shows the import's progress bar and Cancel, and once
//    imported opens the game's hub;
//  * the game hub (game_hub.cpp): the story (continued from the app's
//    checkpoint when there is one), and tiles for
//    the puzzle collection (with its own progress and a reset), minigames,
//    movies and music. Features that are not supported yet are greyed out.
//    "Remove game" in its bar deletes the imported data (the app asks
//    first; saves and progress stay);
//  * the music player (music_screen.cpp): the game's track list, the track
//    playing with a waveform, and play controls; the app plays the music
//    (MusicPlayer) and hands its state over with setMusic();
//  * the movie theatre (movie_screen.cpp): the game's movies as the
//    thumbnails its bonus page shows; the app plays a chosen movie;
//  * the minigames (minigame_screen.cpp): a card each with its progress in
//    free play; the app plays a chosen one;
//  * the puzzle collection of either game: a puzzle-type filter, a list
//    sortable by column with the puzzles solved in this mode ticked, and
//    buttons to go back or play the chosen puzzle.
//
// A landscape window shows a fixed 1080 x 720 design, scaled to fit the
// window; a portrait one (taller than wide) a design 480 units wide and as
// tall as the window's shape gives (see viewport() and style::Canvas).
#include "platform/host.hpp"
#include "shell/library.hpp"
#include "shell/minigames.hpp"
#include "shell/movies.hpp"
#include "shell/style.hpp"
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace picarat::shell {

class LibraryScreen {
public:
    static constexpr float kWidth = 1080, kHeight = 720, kPortraitWidth = 480;

    // Where the screen goes in a window of `width` x `height` points: the
    // area, its design size, and the design units per point.
    struct Viewport {
        platform::Rect area;
        float width, height, zoom;
    };

    static Viewport viewport(float width, float height);

    // Loads the logo and card illustrations from the app's resources.
    explicit LibraryScreen(const std::filesystem::path& resources);

    // Sets the design size from viewport(); call before handle() and draw().
    void resize(float width, float height);

    // The cards of the games that can be imported.
    static constexpr int kAdventure1Card = 0, kAdventure2Card = 1, kAdventure3Card = 2,
                         kAdventure4Card = 3, kAdventure5Card = 4, kAdventure6Card = 5;
    static constexpr int kImportableGames = 6;

    // What the app shows while a ROM imports or after it.
    struct Status {
        bool importing = false;
        int card = -1;       // the card showing the import; -1 until the ROM is known
        double progress = 0; // 0..1 while importing
        std::string message; // progress step, or the outcome of the last import
    };

    enum class Request {
        None,
        Import,         // card
        CancelImport,   //
        Play,           // card, puzzle (0: the story)
        ResetPuzzles,   // card: asks the app to confirm and reset puzzle mode
        OpenMusic,      // card: the app opens the player, then showMusic()
        PlayTrack,      // slot: the track to play
        PauseMusic,     // play or pause
        StopMusic,      //
        CloseMusic,     // back to the hub; the app stops the player
        OpenMovies,     // card: the app opens the theatre (showMovies())
        PlayMovie,      // card, slot: the movie to play (index into the game's movies)
        PlayMinigame,   // card, slot: the minigame to play (index into the game's minigames)
        ResetMinigames, // card: asks the app to confirm and reset the minigames' progress
        SetLanguage,    // card, slot: index into the game's languages, -1 to follow the system
        RemoveGame,     // card: asks the app to confirm and delete the imported data
        ContinueStory,  // card: the story from the app's checkpoint
        RestartStory,   // card: asks the app to confirm, clear the checkpoint, open the title
        Leave,          // Escape on the game list: the app steps back to the system (phones)
    };

    struct Action {
        Request request = Request::None;
        int puzzle = 0; // Play: 0 for the story, else a puzzle ID
        int card = -1;  // the game
        int slot = -1;  // PlayTrack: the track; PlayMovie, PlayMinigame, SetLanguage: the index
    };

    // The data shown for a game; call again after an import, a game or a
    // change to its saves.
    void setLibrary(int card, const Library& library);
    // The app's checkpoint (none: the story starts on the title screen).
    void setCheckpoint(int card, std::optional<StorySave> checkpoint);
    void setSolved(int card, std::set<unsigned> solved);
    void setStatus(const Status& status);

    // Whether the game has a music player (its pack holds the track list).
    // A card's game title ("Pandora's Box").
    const char* title(int card) const { return cards_[std::size_t(card)].title; }

    void setMusicTracks(int card, int count);
    // The game's minigames in free play; `saved`: there is progress to reset.
    void setMinigames(int card, std::vector<MinigameStatus> minigames, bool saved);
    // The game's movies (empty: no movie theatre).
    void setMovies(int card, std::vector<MovieEntry> movies);

    // The text language of a game whose cartridge holds several (the
    // library's `languages`): the setting (-1: follow the system, else an
    // index into them) and the language that results from it.
    struct LanguageChoice {
        int chosen = -1;
        int used = 0;
    };

    void setLanguage(int card, LanguageChoice choice);

    const std::vector<MovieEntry>& movies(int card) const { return games_[slotOf(card)].movies; }

    // What the music player shows, from the app's MusicPlayer.
    struct MusicStatus {
        std::vector<std::string> titles;
        int track = -1; // playing or paused, -1 for none
        enum class State { Stopped, Starting, Playing, Paused } state = State::Stopped;
        double elapsed = 0;      // seconds
        std::vector<float> wave; // recent output, mono, -1..1
    };

    void setMusic(MusicStatus music);

    void showGames();
    // The hub of the game on `card`, if it is imported.
    void showHub(int card);
    // The puzzle collection of the game on `card`, if it is imported.
    void showCollection(int card);

    bool inCollection() const { return view_ == View::Collection; }

    // The music player of the game on `card` (after OpenMusic).
    void showMusic(int card);

    bool inMusic() const { return view_ == View::Music; }

    // The movie theatre of the game on `card` (after OpenMovies).
    void showMovies(int card);
    // The minigames of the game on `card`.
    void showMinigames(int card);

    // Handles an event; `x` and `y` of mouse events are already converted to
    // design units by the caller.
    Action handle(const platform::Event& event);
    void draw(style::Canvas& canvas);

    // Touch scrolling keeps going after a swipe and slows down. Call once
    // per frame with the host's clock; true when the page moved (redraw).
    bool animate(double now);

    // A finger or the mouse is down on something that scrolls, or a swipe is
    // still moving: the app should run frames without idling.
    bool tracking() const {
        return pressed_ || listPressed_ || musicPressed_ || draggingThumb_ || fling_.moving;
    }

private:
    enum class View { Games, Hub, Collection, Music, Movies, Minigames };

    struct Card {
        const char* title;
        const char* platform;
        std::optional<platform::Image> art;
        platform::Image scaled; // the art at the size last drawn on a card
        platform::Image grey;   // ... greyed out (not imported yet)
        platform::Image hub;    // ... and in the hub
        int year = 0;           // first release
    };

    // One importable game: its data and what the hub shows of it.
    struct Game {
        Library library;
        std::optional<StorySave> checkpoint;
        std::set<unsigned> solved; // puzzle mode
        int musicTracks = 0;       // 0: no music player
        std::vector<MinigameStatus> minigames;
        bool minigamesSaved = false;
        std::vector<MovieEntry> movies;
        std::vector<platform::Image> thumbnails; // the movies' pictures at the size last drawn
        LanguageChoice language;
    };

    // Collection view layout for the current design size.
    struct Table {
        platform::Rect frame;
        float bodyTop, bodyHeight;
        float columnX[3]; // column left edges
        float columnsEnd; // right edge of the last column (the scroll bar follows)
        int pageRows;
    };

    // The hub's tiles.
    enum Tile { kPuzzles, kMinigames, kMovies, kMusic, kTiles };

    bool portrait() const { return width_ < height_; }

    float margin() const { return portrait() ? 24 : 48; }

    Table table() const;

    // Slots: 0 the second adventure, 1 the third, 2 the first; the fourth to
    // sixth have the slots of their cards.
    static int slotOf(int card) {
        return card == kAdventure3Card   ? 1
               : card == kAdventure1Card ? 2
               : card >= kAdventure4Card ? card
                                         : 0;
    }

    int cardOf(int slot) const;

    // Pages that scroll in a portrait window (cards, hub): presses act on
    // release unless they turned into a drag.
    Action handlePage(const platform::Event& e);
    void scrollPage(float delta);

    bool clickable(float y) const { return !panning_ && y < footerTop_; }

    // The scroll offset (and its maximum) of the current view, for flings.
    float* scrollTarget(float& max);

    bool dragging() const { return panning_ || listPanning_ || musicPanning_; }

    // A press that lands while a swipe is still moving only stops it.
    void catchFling(const platform::Event& e);

    void drawGames(style::Canvas& c);
    void drawCard(style::Canvas& c, int i, const platform::Rect& r);
    void drawGamesFooter(style::Canvas& c);
    std::string footerHint() const;
    float footerHeight(const style::Canvas& c) const;
    Action clickGames(float x, float y);

    // Game hub (game_hub.cpp).
    void drawHub(style::Canvas& c);
    void drawTile(style::Canvas& c, Tile tile, const platform::Rect& r, bool rows);
    // The language control under the story button; returns its height.
    float drawLanguage(style::Canvas& c, float x, float y, float w);
    Action clickHub(float x, float y);
    Action playStory() const;

    // Music player (music_screen.cpp).
    void drawMusic(style::Canvas& c);
    void drawScope(style::Canvas& c, const platform::Rect& r);
    Action handleMusic(const platform::Event& e);
    Action playTrack(int index) const;
    float musicListMax() const;
    void scrollMusicTo(int row);

    // Minigames (minigame_screen.cpp).
    void drawMinigames(style::Canvas& c);
    Action clickMinigames(float x, float y);

    // Movie theatre (movie_screen.cpp).
    void drawMovies(style::Canvas& c);
    Action clickMovies(float x, float y);

    void drawCollection(style::Canvas& c);
    Action handleCollection(const platform::Event& e);

    // The open game (hub or collection).
    const Game& game() const { return games_[game_]; }

    const Library& collectionLibrary() const { return games_[game_].library; }

    void updateKinds();
    void filter();
    void select(int row);
    void scrollTo(int row);
    float maxScroll() const;
    int rowAt(float x, float y) const;
    Action launchSelection() const;

    // The importable games, in the order of kImportable (.cpp).
    Game games_[kImportableGames];
    int game_ = 0; // index into games_ of the hub or collection shown
    View view_ = View::Games;
    Status status_;
    std::vector<Card> cards_;
    std::optional<platform::Image> logo_;
    platform::Image logoScaled_; // the logo at the size last drawn
    float width_ = kWidth, height_ = kHeight;
    float mouseX_ = -1, mouseY_ = -1;

    // Scrolling pages (cards, hub).
    float pageScroll_ = 0, pageScrollMax_ = 0;
    float footerTop_ = kHeight; // nothing under a pinned footer can be clicked
    bool pressed_ = false, panning_ = false;
    float pressX_ = 0, pressY_ = 0, pressScroll_ = 0;

    // Games view: control rectangles from the last draw.
    platform::Rect cardArea_[kImportableGames]{}, cancelButton_{};

    // Hub view.
    platform::Rect hubBack_{}, hubRemove_{}, storyButton_{}, restartButton_{}, tiles_[kTiles]{},
        resetButtons_[kTiles]{};
    std::vector<platform::Rect> languageButtons_; // "System", then the languages

    // Music view.
    MusicStatus music_;
    int musicRow_ = 0;           // the keyboard's row in the list
    float musicScroll_ = 0;      // list scroll offset
    platform::Rect musicList_{}; // the list's area
    platform::Rect musicBack_{}, transport_[4]{};
    bool musicPressed_ = false, musicPanning_ = false;
    float musicPressY_ = 0, musicPressScroll_ = 0;

    // Minigames view.
    platform::Rect minigamesBack_{};
    std::vector<platform::Rect> minigameCells_;

    // Movies view.
    platform::Rect moviesBack_{};
    std::vector<platform::Rect> movieCells_;

    // Collection view.
    std::vector<std::string> kinds_; // "All puzzle types", then the kinds present
    int kind_ = 0;
    bool kindsOpen_ = false;
    int sortColumn_ = 0; // 0 number, 1 title, 2 kind
    bool ascending_ = true;
    std::vector<int> shown_; // indices into the collection's puzzles, filtered and sorted
    int selected_ = -1;      // index into shown_
    float scroll_ = 0;       // list scroll offset in design units
    bool draggingThumb_ = false;
    float dragStart_ = 0, dragScroll_ = 0;
    // A press on the rows: it scrolls the list once it moves, otherwise it
    // selects the row on release (a double click plays it).
    bool listPressed_ = false, listPanning_ = false;
    int listPressRow_ = -1, listPressClicks_ = 0;
    float listPressY_ = 0, listPressScroll_ = 0;

    // Momentum of the last swipe on any scrolling view.
    struct Fling {
        View view = View::Games;
        bool dragging = false, moving = false;
        float velocity = 0;   // design units per second, in scroll direction
        float lastScroll = 0; // offset at the last movement while dragging
        double lastMove = 0;  // when it moved
        double time = 0;      // the previous animation step
    };

    Fling fling_;
    platform::Rect kindButton_{}, backButton_{}, playButton_{};
    std::vector<platform::Rect> kindItems_;
};

} // namespace picarat::shell
