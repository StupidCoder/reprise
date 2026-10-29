// The application's main loop. See app.hpp.
#include "shell/app.hpp"
#include "import/rom_import.hpp"
#include "story/a2_game.hpp"
#include "story/a3_saves.hpp"
#include "system/a2_checkpoint.hpp"
#include "system/a2_save_file.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace picarat::shell {

using platform::Event;
using platform::Key;
using platform::Rect;

bool App::Layer::resize(platform::Host& host, int w, int h) {
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (texture && image.width == w && image.height == h) {
        return false;
    }
    image = platform::Image(w, h);
    texture = host.createTexture(w, h, false);
    return true;
}

App::App(platform::Host& host, std::filesystem::path libraryRoot, bool testRoot)
    : host_(host), pack_(libraryRoot / kPackFolder),
      adventure3Pack_(libraryRoot / kAdventure3PackFolder),
      checkpoint_(libraryRoot / (testRoot ? "test-checkpoint.l2cp" : "checkpoint.l2cp")),
      adventure3Saves_(libraryRoot / (testRoot ? "test-c3jd-saves" : "c3jd-saves")),
      adventure4Saves_(libraryRoot / (testRoot ? "test-blfd-saves" : "blfd-saves")),
      adventure5Saves_(libraryRoot / (testRoot ? "test-akkd-saves" : "akkd-saves")),
      adventure1Pack_(libraryRoot / kAdventure1PackFolder),
      adventure1Language_(libraryRoot /
                          (testRoot ? "test-a5fp-language.txt" : "a5fp-language.txt")),
      adventure1Saves_(libraryRoot / (testRoot ? "test-a5fp-backup.sav" : "a5fp-backup.sav")),
      adventure1Checkpoint_(libraryRoot /
                            (testRoot ? "test-a5fp-checkpoint.sav" : "a5fp-checkpoint.sav")),
      importOnlyPacks_{libraryRoot / kAdventure4PackFolder, libraryRoot / kAdventure5PackFolder,
                       libraryRoot / kAdventure6PackFolder},
      puzzleMode_{PuzzleModeProgress(libraryRoot / (testRoot ? "test-yltd-puzzle-mode.txt"
                                                             : "yltd-puzzle-mode.txt")),
                  PuzzleModeProgress(libraryRoot / (testRoot ? "test-c3jd-puzzle-mode.txt"
                                                             : "c3jd-puzzle-mode.txt")),
                  PuzzleModeProgress(libraryRoot / (testRoot ? "test-a5fp-puzzle-mode.txt"
                                                             : "a5fp-puzzle-mode.txt")),
                  PuzzleModeProgress(libraryRoot / (testRoot ? "test-blfd-puzzle-mode.txt"
                                                             : "blfd-puzzle-mode.txt")),
                  PuzzleModeProgress(libraryRoot / (testRoot ? "test-akkd-puzzle-mode.txt"
                                                             : "akkd-puzzle-mode.txt")),
                  PuzzleModeProgress(libraryRoot / (testRoot ? "test-al6d-puzzle-mode.txt"
                                                             : "al6d-puzzle-mode.txt"))},
      minigames2_(pack_,
                  libraryRoot / (testRoot ? "test-yltd-minigames.bin" : "yltd-minigames.bin")),
      minigames3_(libraryRoot / (testRoot ? "test-c3jd-minigames.bin" : "c3jd-minigames.bin")),
      minigames4_(libraryRoot / (testRoot ? "test-blfd-minigames.bin" : "blfd-minigames.bin")),
      minigames1_(libraryRoot / (testRoot ? "test-a5fp-minigames.bin" : "a5fp-minigames.bin")),
      libraryScreen_(platform::Host::resourceDirectory()),
      touchAreasFile_(libraryRoot / (testRoot ? "test-touch-areas.txt" : "touch-areas.txt")) {
    reloadLibrary();
    std::ifstream file(touchAreasFile_);
    for (std::string line; std::getline(file, line);) {
        if (!line.empty()) {
            touchAreasOn_.insert(line);
        }
    }
}

const char* App::gameCode(int card) {
    return card == LibraryScreen::kAdventure3Card   ? "c3jd"
           : card == LibraryScreen::kAdventure1Card ? "a5fp"
           : card == LibraryScreen::kAdventure4Card ? "blfd"
           : card == LibraryScreen::kAdventure5Card ? "akkd"
           : card == LibraryScreen::kAdventure6Card ? "al6d"
                                                    : "yltd";
}

// The touch-area help applies to the story (where the rooms are).
bool App::touchAreasShown() const {
    return session_ && !movieMode_ && !minigameMode_ && playedPuzzle_ == 0 &&
           touchAreasOn_.contains(gameCode(playedCard_));
}

void App::setTouchAreas(bool on) {
    if (on) {
        touchAreasOn_.insert(gameCode(playedCard_));
    } else {
        touchAreasOn_.erase(gameCode(playedCard_));
    }
    std::ofstream file(touchAreasFile_);
    for (const auto& code : touchAreasOn_) {
        file << code << '\n';
    }
}

App::~App() {
    session_.reset();
    if (import_.cancel) {
        import_.cancel->store(true);
    }
    if (import_.worker.joinable()) {
        import_.worker.join();
    }
}

void App::run() {
    while (!quit_) {
        // Idle screens wait for input instead of spinning; a running game
        // is paced by the display's vertical sync.
        const bool active = (session_ || music_) && !sleeping_;
        // A finger on a scrolling page, or a swipe still moving, needs
        // frames at the display's rate too.
        const int wait = active ? 0 : sleeping_ ? 50 : libraryScreen_.tracking() ? 1 : 10;
        for (const auto& event : host_.poll(wait)) {
            handle(event);
        }
        update();
        if (!sleeping_) {
            render();
        }
    }
}

// --- Input -------------------------------------------------------------------

void App::handle(const Event& e) {
    switch (e.type) {
    case Event::Type::Quit:
        quit_ = true;
        return;
    case Event::Type::Sleep:
    case Event::Type::Wake:
        // Minimised or hidden: like closing the DS lid.
        sleeping_ = e.type == Event::Type::Sleep;
        repaintUntil_ = host_.seconds() + 1;
        if (session_) {
            session_->sleep(sleeping_ || gameMenu_.isOpen()); // the menu keeps it frozen
        }
        // The music player pauses too, and plays on when the window returns.
        if (music_ && (sleeping_ ? music_->state() == MusicPlayer::State::Playing : musicSlept_)) {
            music_->togglePause();
            musicSlept_ = sleeping_;
        }
        dirty_ = true;
        return;
    case Event::Type::Expose:
        repaintUntil_ = host_.seconds() + 1;
        dirty_ = true;
        return;
    default:
        break;
    }
    dirty_ = true;
    if (shortcut(e)) {
        return;
    }
    if (dialog_.isOpen()) {
        const auto answer = dialog_.handle(e);
        if (answer.id == kStoryEnd && answer.button == 0) {
            showLibrary(Back::Collection);
        } else if (answer.id == kRemoveGame && answer.button == 0) {
            removeGame(pending_.card);
        } else if (answer.id == kRestartStory && answer.button == 0) {
            clearCheckpoint(pending_.card);
            playStory(pending_.card, false);
        } else if (answer.id == kResetPuzzles && answer.button == 0) {
            resetPuzzles(pending_.card);
        } else if (answer.id == kResetMinigames && answer.button == 0 &&
                   (!importOnly(pending_.card) ||
                    pending_.card == LibraryScreen::kAdventure4Card)) {
            if (pending_.card == LibraryScreen::kAdventure4Card) {
                minigames4_.reset();
            } else if (pending_.card == LibraryScreen::kAdventure3Card) {
                minigames3_.reset();
            } else if (pending_.card == LibraryScreen::kAdventure1Card) {
                minigames1_.reset();
            } else {
                minigames2_.reset();
            }
            refreshProgress();
        }
        return;
    }
    if (e.type == Event::Type::DropFile) {
        startImport(e.file, -1);
        return;
    }
    if (session_ && gameMenu_.isOpen()) {
        switch (gameMenu_.handle(e)) {
        case GameMenu::Action::Resume:
            session_->sleep(sleeping_);
            break;
        case GameMenu::Action::Skip:
            session_->sleep(sleeping_);
            requestSkip();
            break;
        case GameMenu::Action::TouchAreas:
            setTouchAreas(gameMenu_.touchAreas());
            break;
        case GameMenu::Action::Quit:
            quitGame();
            break;
        case GameMenu::Action::None:
            break;
        }
        return;
    }
    if (session_ && e.type == Event::Type::KeyDown && !e.command && e.key == Key::Escape) {
        // Escape (a phone's Back key or gesture) opens the in-game menu.
        openGameMenu();
        return;
    }
    if (session_ && movieMode_) {
        // A theatre movie takes no other input; a double click may skip it.
        if (kSkipTriggers.doubleClick && e.type == Event::Type::MouseDown && e.button == 1 &&
            e.clicks >= 2) {
            requestSkip();
        }
        return;
    }
    if (session_) {
        const auto [width, height] = host_.windowSize();
        if (gameScreen_.handle(e, *session_, width, height) && kSkipTriggers.doubleClick) {
            requestSkip();
        }
        return;
    }
    // The library works in its design units.
    Event local = e;
    const auto view = libraryViewport();
    local.x = (e.x - view.area.x) / view.zoom;
    local.y = (e.y - view.area.y) / view.zoom;
    const auto action = libraryScreen_.handle(local);
    switch (action.request) {
    case LibraryScreen::Request::Import:
        chooseRom(action.card);
        break;
    case LibraryScreen::Request::CancelImport:
        cancelImport();
        break;
    case LibraryScreen::Request::Leave:
        host_.leaveToSystem();
        break;
    case LibraryScreen::Request::Play:
        if (action.card == LibraryScreen::kAdventure4Card) {
            playAdventure4(action.puzzle); // 0: the story from its title
            break;
        }
        if (action.card == LibraryScreen::kAdventure5Card ||
            action.card == LibraryScreen::kAdventure6Card) {
            playAdventure5(action.puzzle, action.card == LibraryScreen::kAdventure6Card);
            break; // 0: the fifth adventure's story
        }
        if (importOnly(action.card)) {
            break; // not playable yet
        }
        if (action.card == LibraryScreen::kAdventure3Card) {
            playAdventure3(action.puzzle);
        } else if (action.card == LibraryScreen::kAdventure1Card) {
            playAdventure1(action.puzzle);
        } else {
            play(action.puzzle);
        }
        break;
    case LibraryScreen::Request::ContinueStory:
        playStory(action.card, true);
        break;
    case LibraryScreen::Request::RestartStory:
        pending_ = action;
        dialog_.open(kRestartStory, "Start over from the title screen?",
                     "The checkpoint is cleared and the game opens on its title screen, where you "
                     "can start a new game or load a save slot. Save slots are not affected.",
                     {"Start over", "Cancel"});
        break;
    case LibraryScreen::Request::RemoveGame: {
        pending_ = action;
        std::uintmax_t bytes = 0;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it(packOf(action.card), error), end;
             !error && it != end; it.increment(error)) {
            if (it->is_regular_file(error)) {
                bytes += it->file_size(error);
            }
        }
        const std::string size = std::to_string((bytes + (1 << 19)) >> 20) + " MB";
        dialog_.open(kRemoveGame, std::string("Remove ") + libraryScreen_.title(action.card) + "?",
                     "Its imported game data (" + size +
                         ") is deleted from this device. Story saves and puzzle progress are "
                         "kept. To play again, add the game from its ROM.",
                     {"Remove", "Cancel"});
        break;
    }
    case LibraryScreen::Request::OpenMovies:
        libraryScreen_.showMovies(action.card);
        break;
    case LibraryScreen::Request::PlayMovie:
        playMovie(action.card, action.slot);
        break;
    case LibraryScreen::Request::PlayMinigame:
        playMinigame(action.card, action.slot);
        break;
    case LibraryScreen::Request::ResetMinigames:
        pending_ = action;
        dialog_.open(kResetMinigames, "Reset minigame progress?",
                     "Every minigame of this game starts over, with everything unlocked again. "
                     "Story saves are not affected. This can't be undone.",
                     {"Reset", "Cancel"});
        break;
    case LibraryScreen::Request::OpenMusic:
        openMusic(action.card);
        break;
    case LibraryScreen::Request::PlayTrack:
        if (music_) {
            music_->play(action.slot);
        }
        break;
    case LibraryScreen::Request::PauseMusic:
        if (music_) {
            music_->togglePause();
        }
        break;
    case LibraryScreen::Request::StopMusic:
        if (music_) {
            music_->stop();
        }
        break;
    case LibraryScreen::Request::CloseMusic:
        closeMusic();
        libraryScreen_.showHub(musicCard_);
        break;
    case LibraryScreen::Request::ResetPuzzles:
        pending_ = action;
        dialog_.open(kResetPuzzles, "Reset puzzle progress?",
                     "Every puzzle solved from the puzzle collection will be marked unsolved "
                     "again. Story saves are not affected. This can't be undone.",
                     {"Reset", "Cancel"});
        break;
    case LibraryScreen::Request::SetLanguage:
        setLanguage(action.slot);
        break;
    case LibraryScreen::Request::None:
        break;
    }
}

// The app's shortcuts; true when the event was one.
bool App::shortcut(const Event& e) {
    if (e.type != Event::Type::KeyDown || !e.command || e.repeat || e.key != Key::Character) {
        return false;
    }
    switch (e.symbol) {
    case '1':
        showLibrary(Back::Games);
        return true;
    case 'k':
        showLibrary(Back::Collection);
        return true;
    case 'r':
        if (!movieMode_ && !minigameMode_) {
            restart();
        }
        return true;
    case 'l':
        if (!movieMode_ && !minigameMode_) {
            loadCheckpoint();
        }
        return true;
    case '.':
        if (kSkipTriggers.shortcut) {
            requestSkip();
        }
        return true;
    case 'q':
        quit_ = true;
        return true;
    case 'v': {
        // Paste: the clipboard as typed text (the name entry).
        Event text;
        text.type = Event::Type::Text;
        text.text = host_.clipboardText();
        if (!dialog_.isOpen() && !text.text.empty()) {
            handle(text);
        }
        return true;
    }
    default:
        return false;
    }
}

// --- Per-frame work ------------------------------------------------------------

void App::update() {
    // Swipes on library pages carry on after the finger lifts.
    if (!session_ && libraryScreen_.animate(host_.seconds())) {
        dirty_ = true;
    }
    if (auto choice = host_.takeFileChoice()) {
        if (choice->file) {
            startImport(*choice->file, chosenCard_);
        } else if (!choice->error.empty()) {
            status_.message = choice->error;
        }
        dirty_ = true;
    }
    if (import_.running) {
        bool finished = false;
        {
            std::lock_guard lock(import_.mutex);
            if (status_.progress != import_.progress || status_.message != import_.message ||
                status_.card != import_.card) {
                status_.progress = import_.progress;
                status_.message = import_.message;
                status_.card = import_.card;
                dirty_ = true;
            }
            finished = import_.finished;
        }
        if (finished) {
            finishImport();
        }
    }
    libraryScreen_.setStatus(status_);
    tickMusic();

    if (session_) {
        if (playedPuzzle_ > 0 && session_->puzzleSolved()) {
            const int slot = puzzleModeSlot(playedCard_);
            if (!puzzleMode_[slot].solved().count(unsigned(playedPuzzle_))) {
                puzzleMode_[slot].markSolved(unsigned(playedPuzzle_));
                refreshProgress();
            }
        }
        if (session_->returnedToLibrary()) {
            showLibrary(movieMode_      ? Back::Movies
                        : minigameMode_ ? Back::Minigames
                                        : Back::Collection);
            return;
        }
        if (const auto error = session_->error(); !error.empty()) {
            showLibrary(Back::Hub);
            dialog_.open(kNotice, "The game could not continue", error, {"OK"});
            return;
        }
        // Wake the game for its next frame at 60 Hz. When the loop falls
        // far behind (a stall), the missed frames are dropped.
        const long due = long((host_.seconds() - tickStart_) * 60);
        if (due > ticksDone_) {
            ticksDone_ = due > ticksDone_ + 4 ? due : ticksDone_ + 1;
            session_->tick();
        }
        state_ = session_->state();
        if (state_.scene != SceneKind::Complete) {
            announced_ = false;
        }
        if (state_.storyBoundary && !announced_) {
            announced_ = true;
            dialog_.open(kStoryEnd, "End of reconstructed story",
                         state_.boundary +
                             "\n\nYour checkpoint at the last room or event is saved.",
                         {"Puzzle collection", "Stay here"});
        }
    }

    // Window title: the app, and in a game the scene.
    std::string title = "Picarat";
    if (session_ && movieMode_) {
        title += std::string(" — ") + movieLabel();
    } else if (session_ && minigameMode_) {
        title += std::string(" — ") + libraryScreen_.title(playedCard_);
    } else if (session_) {
        title += std::string(" — ") + Game::sceneName(state_.scene);
        if (state_.scene == SceneKind::Puzzle && state_.detail == 203) {
            title += " · Hold B for microphone";
        }
    }
    if (title != title_) {
        title_ = title;
        host_.setTitle(title_);
    }
    // Typed text goes to the name entry (which has the game's own keyboard,
    // so a phone shows none).
    host_.setTextInput(!dialog_.isOpen() && session_ && state_.scene == SceneKind::Name, false);
}

LibraryScreen::Viewport App::libraryViewport() {
    const auto [width, height] = host_.windowSize();
    const auto view = LibraryScreen::viewport(width, height);
    libraryScreen_.resize(view.width, view.height);
    return view;
}

void App::render() {
    const auto [width, height] = host_.windowSize();
    const float density = host_.pixelDensity();
    const double now = host_.seconds();
    if (!session_ || movieMode_) {
        host_.motion(false); // the accelerometer only while a game shows
    }
    if (session_ && movieMode_) {
        renderMovie(width, height);
    } else if (session_) {
        host_.beginFrame({0, 0, 0});
        // The game's screens: one texture holding both, drawn with sharp
        // pixels (DS 256x192 twice; 3DS 400x240 above 320x240).
        const ScreenGeometry g = session_->geometry();
        if (!screens_ || screensGeometry_ != g) {
            screens_ = host_.createTexture(g.frameWidth(), g.frameHeight(), false);
            screensGeometry_ = g;
        }
        gameScreen_.update(now, *session_, width, height); // the zoom; held presses
        session_->motion(host_.motion(!movieMode_));       // phones: the accelerometer
        if (session_->frame(frame_)) {
            platform::Image image(g.frameWidth(), g.frameHeight());
            std::memcpy(image.pixels.data(), frame_.data(),
                        std::min(frame_.size(), image.pixels.size() * 4));
            screens_->update(image);
            // Through the pinch zoom (phones; GameScreen).
            const auto l = GameScreen::layout(width, height, g);
            const ScreenRect t = gameScreen_.zoomed(l.top), b = gameScreen_.zoomed(l.bottom);
            const Rect top{0, 0, float(g.topWidth), float(g.topHeight)},
                bottom{0, float(g.topHeight), float(g.bottomWidth), float(g.bottomHeight)};
            host_.draw(*screens_, &top, {float(t.x), float(t.y), float(t.width), float(t.height)});
            host_.draw(*screens_, &bottom,
                       {float(b.x), float(b.y), float(b.width), float(b.height)});
        }
        // The touch-area help over the lower screen (redrawn when the areas
        // or the window change).
        if (touchAreasShown() && !state_.touchAreas.empty()) {
            const auto l = GameScreen::layout(width, height, session_->geometry());
            const auto same = [](const std::vector<TouchArea>& a, const std::vector<TouchArea>& b) {
                return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                                  [](const TouchArea& p, const TouchArea& q) {
                                      return p.kind == q.kind && p.x == q.x && p.y == q.y &&
                                             p.w == q.w && p.h == q.h;
                                  });
            };
            const bool resized =
                touchLayer_.resize(host_, int(std::lround(float(l.bottom.width) * density)),
                                   int(std::lround(float(l.bottom.height) * density)));
            if (resized || !same(state_.touchAreas, touchAreasDrawn_)) {
                style::Canvas canvas(touchLayer_.image, density);
                GameScreen::drawTouchAreas(
                    canvas, float(l.bottom.width) / float(session_->geometry().bottomWidth),
                    state_.touchAreas);
                touchLayer_.texture->update(touchLayer_.image);
                touchAreasDrawn_ = state_.touchAreas;
            }
            const ScreenRect b = gameScreen_.zoomed(l.bottom);
            host_.draw(*touchLayer_.texture, nullptr,
                       {float(b.x), float(b.y), float(b.width), float(b.height)});
        }
        // The microphone puzzle's floating "Hold to blow" button.
        if (const Rect blow = GameScreen::blowButton(width, height, state_); blow.w > 0) {
            blowLayer_.resize(host_, int(std::lround(blow.w * density)),
                              int(std::lround(blow.h * density)));
            style::Canvas canvas(blowLayer_.image, density);
            gameScreen_.drawBlowButton(canvas, blow);
            blowLayer_.texture->update(blowLayer_.image);
            host_.draw(*blowLayer_.texture, nullptr, blow);
        }
    } else {
        const auto view = libraryViewport();
        const Rect area = view.area;
        const int w = int(std::lround(area.w * density)), h = int(std::lround(area.h * density));
        const bool resized = libraryLayer_.resize(host_, w, h);
        // An unchanged library page (and dialog) needs no new frame; idle
        // screens then cost no drawing, which matters on battery.
        if (!resized && !dirty_ && now > repaintUntil_) {
            return;
        }
        host_.beginFrame(ui::kPaper);
        style::Canvas canvas(libraryLayer_.image, w / view.width);
        libraryScreen_.draw(canvas);
        libraryLayer_.texture->update(libraryLayer_.image);
        host_.draw(*libraryLayer_.texture, nullptr, area);
    }
    // The in-game menu over the frozen game.
    if (session_ && gameMenu_.isOpen()) {
        const bool resized = menuLayer_.resize(host_, int(std::lround(width * density)),
                                               int(std::lround(height * density)));
        if (resized || dirty_) {
            style::fill(menuLayer_.image, {0, 0, 0, 0});
            style::Canvas canvas(menuLayer_.image, density);
            gameMenu_.draw(canvas, width, height);
            menuLayer_.texture->update(menuLayer_.image);
        }
        host_.draw(*menuLayer_.texture, nullptr, {0, 0, width, height});
    }
    if (dialog_.isOpen()) {
        const bool resized = dialogLayer_.resize(host_, int(std::lround(width * density)),
                                                 int(std::lround(height * density)));
        if (resized || dirty_) {
            style::fill(dialogLayer_.image, {0, 0, 0, 0});
            style::Canvas canvas(dialogLayer_.image, density);
            dialog_.draw(canvas, width, height);
            dialogLayer_.texture->update(dialogLayer_.image);
        }
        host_.draw(*dialogLayer_.texture, nullptr, {0, 0, width, height});
    }
    dirty_ = false;
    host_.endFrame();
}

// --- Library and import --------------------------------------------------------

void App::reloadLibrary() {
    library_ = loadLibrary(pack_);
    adventure3_ = loadAdventure3(adventure3Pack_);
    adventure1_ = loadAdventure1(adventure1Pack_);
    for (int k = 0; k < 3; ++k) {
        importOnly_[k] = loadImportOnly(importOnlyPacks_[k], LibraryScreen::kAdventure4Card + k);
    }
    libraryScreen_.setLibrary(LibraryScreen::kAdventure2Card, library_);
    libraryScreen_.setLibrary(LibraryScreen::kAdventure3Card, adventure3_);
    libraryScreen_.setLibrary(LibraryScreen::kAdventure1Card, adventure1_);
    for (int k = 0; k < 3; ++k) {
        libraryScreen_.setLibrary(LibraryScreen::kAdventure4Card + k, importOnly_[k]);
    }
    refreshLanguage();
    // The movie theatres' lists and thumbnails (the packs' own pictures).
    auto movies = [](auto read, const std::filesystem::path& pack, bool ready) {
        try {
            return ready ? read(pack) : std::vector<MovieEntry>{};
        } catch (const std::exception&) {
            return std::vector<MovieEntry>{};
        }
    };
    libraryScreen_.setMovies(LibraryScreen::kAdventure2Card,
                             movies(adventure2Movies, pack_, library_.ready));
    libraryScreen_.setMovies(LibraryScreen::kAdventure3Card,
                             movies(adventure3Movies, adventure3Pack_, adventure3_.ready));
    libraryScreen_.setMovies(LibraryScreen::kAdventure1Card,
                             movies(adventure1Movies, adventure1Pack_, adventure1_.ready));
    refreshProgress();
    if (!library_.problem.empty()) {
        status_.message = library_.problem;
    } else if (!adventure3_.problem.empty()) {
        status_.message = adventure3_.problem;
    } else if (!adventure1_.problem.empty()) {
        status_.message = adventure1_.problem;
    }
    for (const Library& game : importOnly_) {
        if (!game.problem.empty()) {
            status_.message = game.problem;
        }
    }
    dirty_ = true;
}

void App::playMinigame(int card, int index) {
    const bool fourth = card == LibraryScreen::kAdventure4Card;
    if (import_.running || (importOnly(card) && !fourth)) {
        return;
    }
    const bool third = card == LibraryScreen::kAdventure3Card,
               first = card == LibraryScreen::kAdventure1Card;
    const auto status = fourth  ? minigames4_.status()
                        : third ? minigames3_.status()
                        : first ? minigames1_.status()
                                : minigames2_.status();
    if (index < 0 || index >= int(status.size()) || !status[std::size_t(index)].available) {
        return;
    }
    stopGame();
    playedCard_ = card;
    playedPuzzle_ = 0;
    try {
        session_ =
            fourth ? GameSession::adventure4Minigame(importOnlyPacks_[0], index,
                                                     minigames4_.progress())
            : third
                ? GameSession::adventure3Minigame(adventure3Pack_, index, minigames3_.progress())
            : first ? GameSession::adventure1Minigame(adventure1Pack_, adventure1LanguageCode(),
                                                      index, minigames1_.progress())
                    : GameSession::adventure2Minigame(pack_, checkpoint_,
                                                      Adventure2Minigames::kScenes[index],
                                                      minigames2_.progress());
    } catch (const std::exception& error) {
        dialog_.open(kNotice, "The minigame could not start", error.what(), {"OK"});
        return;
    }
    minigameMode_ = true;
    minigameLabel_ =
        std::string(libraryScreen_.title(card)) + " · " + status[std::size_t(index)].name;
    sessionStarted();
}

void App::playMovie(int card, int index) {
    const auto& movies = libraryScreen_.movies(card);
    if (index < 0 || index >= int(movies.size()) || import_.running) {
        return;
    }
    const MovieEntry& movie = movies[std::size_t(index)];
    stopGame();
    playedCard_ = card;
    playedPuzzle_ = 0;
    // The second adventure replays the movie's event; the third pushes its
    // movie loop as the movie page does.
    session_ =
        card == LibraryScreen::kAdventure3Card
            ? GameSession::adventure3Movie(adventure3Pack_, movie.id)
        : card == LibraryScreen::kAdventure1Card
            ? GameSession::adventure1Movie(adventure1Pack_, adventure1LanguageCode(), movie.id)
            : std::make_unique<GameSession>(pack_, checkpoint_, 0, movie.event);
    movieMode_ = true;
    movieNumber_ = index + 1;
    sessionStarted();
}

// One screen, enlarged: the second adventure plays its movies on the lower
// screen (with the subtitles on the picture); the third on the upper screen,
// with its captions on the lower one, whose caption band is shown beneath.
void App::renderMovie(float width, float height) {
    host_.beginFrame({0, 0, 0});
    if (!screens_) {
        screens_ = host_.createTexture(256, 384, false);
    }
    if (session_->frame(frame_)) {
        platform::Image image(256, 384);
        std::memcpy(image.pixels.data(), frame_.data(),
                    std::min(frame_.size(), image.pixels.size() * 4));
        screens_->update(image);
    }
    // Both games letterbox their films to 256x144 (screen rows 16..159);
    // the player shows that picture and a 32-row strip under it (rows
    // 160..191 of the lower screen) for the subtitles, so both look alike.
    const bool upper = playedCard_ == LibraryScreen::kAdventure3Card;
    constexpr float kPictureTop = 16, kPictureRows = 144, kStripTop = 160, kStripRows = 32;
    const float rows = kPictureRows + kStripRows;
    const float scale = std::max(0.01f, std::min(width / 256, height / rows));
    const float x = std::floor((width - 256 * scale) / 2);
    const float y = std::floor((height - rows * scale) / 2);
    if (playedCard_ == LibraryScreen::kAdventure1Card) {
        // The first adventure plays its theatre's movies on the whole lower
        // screen, the captions on the picture (02018fa4, row 0xab).
        const float s1 = std::max(0.01f, std::min(width / 256, height / 192));
        const float x1 = std::floor((width - 256 * s1) / 2);
        const float y1 = std::floor((height - 192 * s1) / 2);
        const Rect lower{0, 192, 256, 192};
        host_.draw(*screens_, &lower, {x1, y1, 256 * s1, 192 * s1});
    } else {
        const Rect picture{0, (upper ? 0.0f : 192.0f) + kPictureTop, 256, kPictureRows};
        host_.draw(*screens_, &picture, {x, y, 256 * scale, kPictureRows * scale});
        const Rect strip{0, 192 + kStripTop, 256, kStripRows};
        host_.draw(*screens_, &strip,
                   {x, y + kPictureRows * scale, 256 * scale, kStripRows * scale});
    }
}

void App::openGameMenu() {
    if (!session_ || gameMenu_.isOpen()) {
        return;
    }
    const bool story = !movieMode_ && !minigameMode_ && playedPuzzle_ == 0;
    gameMenu_.open(sessionTitle(), kSkipTriggers.menu && state_.skippable,
                   story ? std::optional(touchAreasOn_.contains(gameCode(playedCard_)))
                         : std::nullopt);
    session_->sleep(true); // frozen: no frames, the sound paused
    dirty_ = true;
}

// The skip function, whatever triggered it: the session passes it to the
// game while the game reports something skippable (and ignores it otherwise).
void App::requestSkip() {
    if (session_) {
        session_->skip();
    }
}

// Quit: back to the page the game was started from.
void App::quitGame() {
    showLibrary(movieMode_          ? Back::Movies
                : minigameMode_     ? Back::Minigames
                : playedPuzzle_ > 0 ? Back::Collection
                                    : Back::Hub);
}

std::string App::sessionTitle() const {
    if (movieMode_) {
        return movieLabel();
    }
    if (minigameMode_) {
        return minigameLabel_;
    }
    return std::string(libraryScreen_.title(playedCard_)) +
           (playedPuzzle_ > 0 ? " · Puzzle collection" : " · Story");
}

std::string App::movieLabel() const {
    return std::string(libraryScreen_.title(playedCard_)) + " · Movie " +
           (movieNumber_ < 10 ? "0" : "") + std::to_string(movieNumber_);
}

void App::openMusic(int card) {
    if (importOnly(card)) {
        return; // no music player yet
    }
    closeMusic();
    try {
        music_ = card == LibraryScreen::kAdventure3Card ? MusicPlayer::adventure3(adventure3Pack_)
                 : card == LibraryScreen::kAdventure1Card
                     ? MusicPlayer::adventure1(adventure1Pack_, adventure1LanguageCode())
                     : MusicPlayer::adventure2(pack_);
    } catch (const std::exception& error) {
        dialog_.open(kNotice, "The music player could not start", error.what(), {"OK"});
        return;
    }
    musicCard_ = card;
    musicStart_ = host_.seconds();
    musicTicks_ = 0;
    libraryScreen_.setMusic({});
    tickMusic();
    libraryScreen_.showMusic(card);
}

const std::filesystem::path& App::packOf(int card) const {
    return card == LibraryScreen::kAdventure3Card   ? adventure3Pack_
           : card == LibraryScreen::kAdventure1Card ? adventure1Pack_
           : importOnly(card) ? importOnlyPacks_[card - LibraryScreen::kAdventure4Card]
                              : pack_;
}

void App::removeGame(int card) {
    if (session_ || import_.running) {
        return;
    }
    if (music_ && musicCard_ == card) {
        closeMusic();
    }
    std::error_code error;
    std::filesystem::remove_all(packOf(card), error);
    reloadLibrary();
    libraryScreen_.showGames();
    if (error) {
        dialog_.open(kNotice, "The game could not be removed completely", error.message(), {"OK"});
    } else {
        status_.message = std::string(libraryScreen_.title(card)) + " was removed.";
        libraryScreen_.setStatus(status_);
    }
    dirty_ = true;
}

void App::closeMusic() {
    music_.reset();
    musicSlept_ = false;
}

void App::tickMusic() {
    if (!music_) {
        return;
    }
    // Frames at 60 Hz, like a game; after a stall the missed ones are dropped.
    const long due = long((host_.seconds() - musicStart_) * 60);
    if (due > musicTicks_) {
        musicTicks_ = due > musicTicks_ + 4 ? due : musicTicks_ + 1;
        try {
            music_->tick();
        } catch (const std::exception& error) {
            closeMusic();
            libraryScreen_.showHub(musicCard_);
            dialog_.open(kNotice, "The music stopped", error.what(), {"OK"});
            return;
        }
    }
    LibraryScreen::MusicStatus status;
    for (const auto& track : music_->tracks()) {
        status.titles.push_back(track.title);
    }
    status.track = music_->track();
    status.state = LibraryScreen::MusicStatus::State(int(music_->state()));
    status.elapsed = music_->elapsed();
    status.wave = music_->waveform(1024);
    libraryScreen_.setMusic(std::move(status));
    dirty_ = true; // the waveform moves every frame
}

// The first adventure's language: a folder code in a small text file, or
// nothing to follow the system (the console used its own setting).
void App::setLanguage(int chosen) {
    const auto& languages = adventure1_.languages;
    std::error_code ignored;
    if (chosen >= 0 && chosen < int(languages.size())) {
        std::ofstream(adventure1Language_, std::ios::trunc)
            << languages[std::size_t(chosen)].code << "\n";
    } else {
        std::filesystem::remove(adventure1Language_, ignored);
    }
    refreshLanguage();
}

void App::refreshLanguage() {
    const auto& languages = adventure1_.languages;
    std::string code;
    std::ifstream(adventure1Language_) >> code;
    LibraryScreen::LanguageChoice choice;
    for (std::size_t i = 0; i < languages.size(); ++i) {
        if (languages[i].code == code) {
            choice.chosen = int(i);
        }
    }
    choice.used = usedLanguage(languages, choice.chosen, platform::Host::preferredLanguage());
    libraryScreen_.setLanguage(LibraryScreen::kAdventure1Card, choice);
    // The puzzle list's titles follow the language.
    if (choice.used >= 0 && choice.used < int(languages.size())) {
        loadAdventure1Puzzles(adventure1_, adventure1Pack_,
                              languages[std::size_t(choice.used)].code);
        libraryScreen_.setLibrary(LibraryScreen::kAdventure1Card, adventure1_);
    }
    dirty_ = true;
}

void App::refreshProgress() {
    libraryScreen_.setCheckpoint(LibraryScreen::kAdventure2Card,
                                 adventure2Checkpoint(pack_, checkpoint_));
    libraryScreen_.setCheckpoint(LibraryScreen::kAdventure3Card,
                                 adventure3Checkpoint(adventure3Saves_));
    libraryScreen_.setCheckpoint(LibraryScreen::kAdventure1Card,
                                 adventure1Checkpoint(adventure1Checkpoint_));
    libraryScreen_.setSolved(LibraryScreen::kAdventure2Card, puzzleMode_[0].solved());
    libraryScreen_.setSolved(LibraryScreen::kAdventure3Card, puzzleMode_[1].solved());
    libraryScreen_.setSolved(LibraryScreen::kAdventure1Card, puzzleMode_[2].solved());
    libraryScreen_.setSolved(LibraryScreen::kAdventure4Card, puzzleMode_[3].solved());
    libraryScreen_.setSolved(LibraryScreen::kAdventure5Card, puzzleMode_[4].solved());
    libraryScreen_.setSolved(LibraryScreen::kAdventure6Card, puzzleMode_[5].solved());
    // Minigame free play (needs the imported game).
    libraryScreen_.setMinigames(
        LibraryScreen::kAdventure2Card,
        library_.ready ? minigames2_.status() : std::vector<MinigameStatus>{}, minigames2_.saved());
    libraryScreen_.setMinigames(LibraryScreen::kAdventure3Card,
                                adventure3_.ready ? minigames3_.status()
                                                  : std::vector<MinigameStatus>{},
                                minigames3_.saved());
    libraryScreen_.setMinigames(LibraryScreen::kAdventure4Card,
                                importOnly_[0].ready ? minigames4_.status()
                                                     : std::vector<MinigameStatus>{},
                                minigames4_.saved());
    libraryScreen_.setMinigames(LibraryScreen::kAdventure1Card,
                                adventure1_.ready && !adventure1_.languages.empty()
                                    ? minigames1_.status()
                                    : std::vector<MinigameStatus>{},
                                minigames1_.saved());
    // The music players need their track lists in the packs.
    auto tracks = [](auto read, const std::filesystem::path& pack, bool ready) {
        try {
            return ready ? int(read(pack).size()) : 0;
        } catch (const std::exception&) {
            return 0;
        }
    };
    libraryScreen_.setMusicTracks(LibraryScreen::kAdventure2Card,
                                  tracks(adventure2Music, pack_, library_.ready));
    libraryScreen_.setMusicTracks(LibraryScreen::kAdventure3Card,
                                  tracks(adventure3Music, adventure3Pack_, adventure3_.ready));
    libraryScreen_.setMusicTracks(LibraryScreen::kAdventure1Card,
                                  adventure1_.ready && !adventure1_.languages.empty() &&
                                          fileExists(adventure1Pack_ / "stream" / "BGM_09.SAD")
                                      ? 19
                                      : 0);
    dirty_ = true;
}

void App::resetPuzzles(int card) {
    if (importOnly(card) && card != LibraryScreen::kAdventure4Card &&
        card != LibraryScreen::kAdventure5Card && card != LibraryScreen::kAdventure6Card) {
        return; // no puzzle mode yet
    }
    puzzleMode_[puzzleModeSlot(card)].reset();
    if (card == LibraryScreen::kAdventure3Card) {
        GameSession::forgetAdventure3Collection();
    }
    if (card == LibraryScreen::kAdventure4Card) {
        GameSession::forgetAdventure4Collection();
    }
    refreshProgress();
}

int App::puzzleModeSlot(int card) {
    return card == LibraryScreen::kAdventure3Card   ? 1
           : card == LibraryScreen::kAdventure1Card ? 2
           : card == LibraryScreen::kAdventure4Card ? 3
           : card == LibraryScreen::kAdventure5Card ? 4
           : card == LibraryScreen::kAdventure6Card ? 5
                                                    : 0;
}

void App::chooseRom(int card) {
    if (!import_.running) {
        chosenCard_ = card;
        host_.chooseFile("Nintendo DS or 3DS ROM (.nds, .cci, .3ds)", "nds;cci;3ds");
    }
}

// The importer runs on a worker thread. Cancelling stops it at its next
// step and its staging folder is removed, so no partial game appears in the
// library. The ROM decides which game it is; importing a game twice is
// refused by the importer.
void App::startImport(const std::filesystem::path& rom, int card) {
    if (import_.running || session_) {
        return;
    }
    if (import_.worker.joinable()) {
        import_.worker.join();
    }
    import_.running = true;
    import_.finished = import_.cancelling = false;
    import_.progress = 0;
    import_.message = "Checking the ROM…";
    import_.outcome = 0;
    import_.error.clear();
    import_.card = card;
    import_.game.reset();
    status_ = {true, card, 0, import_.message};
    import_.cancel = std::make_shared<std::atomic<bool>>(false);
    import_.worker = std::thread([this, rom, cancel = import_.cancel] {
        int outcome = 0;
        std::string error;
        // Called on this thread once the ROM is identified.
        auto destination = [this](import::Game game) {
            std::lock_guard lock(import_.mutex);
            import_.game = game;
            switch (game) {
            case import::Game::Adventure1:
                import_.card = LibraryScreen::kAdventure1Card;
                return adventure1Pack_;
            case import::Game::Adventure2:
                import_.card = LibraryScreen::kAdventure2Card;
                return pack_;
            case import::Game::Adventure4:
            case import::Game::Adventure5:
            case import::Game::Adventure6: {
                const int k = int(game) - int(import::Game::Adventure4);
                import_.card = LibraryScreen::kAdventure4Card + k;
                return importOnlyPacks_[k];
            }
            case import::Game::Adventure3:
                break;
            }
            import_.card = LibraryScreen::kAdventure3Card;
            return adventure3Pack_;
        };
        try {
            // The ROM streams from disk (3DS images are 1 GiB); DS ROMs are
            // read whole by the importer.
            struct Source final : import::RomSource {
                platform::Host::FileReader file;
                explicit Source(const std::filesystem::path& path) : file(path) {}
                std::uint64_t size() const override { return file.size(); }
                void read(std::uint64_t offset, std::span<std::uint8_t> out) override {
                    file.read(offset, out);
                }
            } source(rom);
            import::importRom(
                source, destination,
                [this](double fraction, const std::string& message) {
                    std::lock_guard lock(import_.mutex);
                    if (!import_.cancelling) {
                        import_.progress = fraction;
                        import_.message = message;
                    }
                },
                cancel.get());
        } catch (const import::ImportCancelled&) {
            outcome = 2;
        } catch (const std::exception& failure) {
            outcome = 1;
            error = failure.what();
        }
        std::lock_guard lock(import_.mutex);
        import_.outcome = outcome;
        import_.error = error;
        import_.finished = true;
    });
    dirty_ = true;
}

void App::cancelImport() {
    std::lock_guard lock(import_.mutex);
    if (import_.running && import_.cancel) {
        import_.cancelling = true;
        import_.message = "Cancelling import…";
        import_.cancel->store(true);
    }
}

void App::finishImport() {
    import_.worker.join();
    import_.running = false;
    status_.importing = false;
    if (import_.outcome == 0) {
        reloadLibrary();
        if (import_.game == import::Game::Adventure1) {
            status_.message = adventure1_.ready ? "Curious Village imported. Its puzzles are ready."
                                                : "The import could not be verified.";
        } else if (import_.game == import::Game::Adventure3) {
            status_.message = adventure3_.ready ? "Lost Future imported. Your adventure is ready."
                                                : "The import could not be verified.";
        } else if (import_.game >= import::Game::Adventure4) {
            const int k = int(*import_.game) - int(import::Game::Adventure4);
            status_.message = !importOnly_[k].ready ? "The import could not be verified."
                              : k == 0 ? std::string(import::knownRom(*import_.game).title) +
                                             " imported. Your adventure is ready."
                                       : std::string(import::knownRom(*import_.game).title) +
                                             " imported. It cannot be played yet.";
        } else {
            status_.message = library_.ready ? "Import complete. Your adventure is ready."
                                             : "The import could not be verified.";
        }
    } else if (import_.outcome == 2) {
        status_.message = "Import cancelled. No game was added.";
    } else {
        status_.message = import_.error.empty() ? "Import failed." : import_.error;
        dialog_.open(kNotice, "The ROM could not be imported",
                     status_.message +
                         "\n\nThis build supports the verified ROMs of Curious Village "
                         "(European edition, game code A5FP), Pandora's Box (YLTD), Lost "
                         "Future (C3JD), Spectre's Call (BLFD) and, as unencrypted 3DS "
                         "cartridge images, Miracle Mask (AKKD) and Azran Legacy (AL6D). "
                         "Choose one of these files and try again.",
                     {"OK"});
    }
    dirty_ = true;
}

// --- Playing -------------------------------------------------------------------

// Starts the story (puzzle 0) or one puzzle of the collection.
// After a session was started: fresh gestures, state and frame clock.
void App::sessionStarted() {
    gameScreen_.reset();
    state_ = {};
    announced_ = false;
    // Ticks fall half a frame away from the display's refresh, so timing
    // noise does not make the game skip or double a frame.
    tickStart_ = host_.seconds() - 0.5 / 60;
    ticksDone_ = 0;
    dirty_ = true;
}

void App::playStory(int card, bool resume) {
    if (card == LibraryScreen::kAdventure4Card) {
        playAdventure4(0); // its own save slots; no app checkpoint
        return;
    }
    if (card == LibraryScreen::kAdventure5Card) {
        playAdventure5(0, false); // from the boot; no saves yet
        return;
    }
    if (importOnly(card)) {
        return; // not playable yet
    }
    if (card == LibraryScreen::kAdventure3Card) {
        playAdventure3(0, resume);
    } else if (card == LibraryScreen::kAdventure1Card) {
        playAdventure1(0, resume);
    } else {
        play(0, resume);
    }
}

void App::clearCheckpoint(int card) {
    if (importOnly(card)) {
        return; // no checkpoints yet
    }
    std::error_code error;
    if (card == LibraryScreen::kAdventure3Card) {
        adventure3::SaveSlots(adventure3Saves_).erase(adventure3::SaveSlots::kCheckpoint);
    } else if (card == LibraryScreen::kAdventure1Card) {
        std::filesystem::remove(adventure1Checkpoint_, error);
    } else {
        std::filesystem::remove(checkpoint_, error);
    }
    refreshProgress();
}

void App::play(int puzzle, bool resume) {
    if (!library_.ready || import_.running) {
        return;
    }
    stopGame();
    playedCard_ = LibraryScreen::kAdventure2Card;
    playedPuzzle_ = puzzle;
    session_ = resume ? GameSession::adventure2Continue(pack_, checkpoint_)
                      : std::make_unique<GameSession>(pack_, checkpoint_, puzzle);
    sessionStarted();
}

void App::playAdventure4(int id) {
    const Library& fourth = importOnly_[0];
    if (!fourth.ready || import_.running) {
        return;
    }
    stopGame();
    playedCard_ = LibraryScreen::kAdventure4Card;
    playedPuzzle_ = id;
    session_ = id == 0 ? GameSession::adventure4Story(importOnlyPacks_[0], adventure4Saves_)
                       : GameSession::adventure4Puzzle(importOnlyPacks_[0], id);
    sessionStarted();
}

// Plays one puzzle of the fifth (or sixth) adventure's collection (its
// catalogue id); both run on the same runtime.
void App::playAdventure5(int id, bool sixth) {
    const int k = sixth ? 2 : 1;
    const Library& game = importOnly_[k];
    // 0: the story from the boot (the fifth adventure only so far).
    if (!game.ready || import_.running || id < 0 || (id == 0 && sixth)) {
        return;
    }
    stopGame();
    playedCard_ = sixth ? LibraryScreen::kAdventure6Card : LibraryScreen::kAdventure5Card;
    playedPuzzle_ = id;
    session_ = id == 0 ? GameSession::adventure5Story(importOnlyPacks_[k], adventure5Saves_)
                       : GameSession::adventure5Puzzle(importOnlyPacks_[k], id, sixth);
    sessionStarted();
}

// Plays one puzzle of the third adventure's collection (its number).
void App::playAdventure3(int number, bool resume) {
    if (!adventure3_.ready || import_.running) {
        return;
    }
    stopGame();
    playedCard_ = LibraryScreen::kAdventure3Card;
    playedPuzzle_ = number;
    // 0: the story from the logos, with its save slots.
    session_ = number == 0 ? GameSession::adventure3Story(adventure3Pack_, adventure3Saves_, resume)
                           : GameSession::adventure3Puzzle(adventure3Pack_, number);
    sessionStarted();
}

// Plays one puzzle of the first adventure's collection (its internal
// number; 0: the story from the logos, with the cartridge backup), in the
// language the hub shows.
std::string App::adventure1LanguageCode() const {
    // The language chosen on the game's card, else the system's.
    if (adventure1_.languages.empty()) {
        return {};
    }
    std::string code;
    std::ifstream(adventure1Language_) >> code;
    int chosen = -1;
    for (std::size_t i = 0; i < adventure1_.languages.size(); ++i) {
        if (adventure1_.languages[i].code == code) {
            chosen = int(i);
        }
    }
    const int used =
        usedLanguage(adventure1_.languages, chosen, platform::Host::preferredLanguage());
    return adventure1_.languages[std::size_t(used)].code;
}

void App::playAdventure1(int id, bool resume) {
    if (!adventure1_.ready || import_.running || adventure1_.languages.empty()) {
        return;
    }
    stopGame();
    playedCard_ = LibraryScreen::kAdventure1Card;
    playedPuzzle_ = id;
    const std::string language = adventure1LanguageCode();
    session_ = id == 0 ? GameSession::adventure1Story(adventure1Pack_, adventure1Saves_,
                                                      adventure1Checkpoint_, language, resume)
                       : GameSession::adventure1Puzzle(adventure1Pack_, id, language);
    sessionStarted();
}

void App::stopGame() {
    closeMusic();
    if (session_ && minigameMode_) {
        // The minigame's progress, however it was left.
        const auto result = session_->finish();
        if (!result.empty()) {
            if (playedCard_ == LibraryScreen::kAdventure4Card) {
                minigames4_.store(result);
            } else if (playedCard_ == LibraryScreen::kAdventure3Card) {
                minigames3_.store(result);
            } else if (playedCard_ == LibraryScreen::kAdventure1Card) {
                minigames1_.store(result);
            } else {
                minigames2_.store(result);
            }
        }
    }
    session_.reset();
    movieMode_ = minigameMode_ = false;
    gameMenu_.close();
    frame_.clear();
}

void App::showLibrary(Back back) {
    const bool played = session_ != nullptr;
    stopGame();
    if (played) {
        refreshProgress(); // the game may have saved
    }
    // The hub and collection need the game imported; else the cards show.
    switch (back) {
    case Back::Games:
        libraryScreen_.showGames();
        break;
    case Back::Hub:
        libraryScreen_.showHub(playedCard_);
        break;
    case Back::Movies:
        libraryScreen_.showMovies(playedCard_);
        break;
    case Back::Minigames:
        libraryScreen_.showMinigames(playedCard_);
        break;
    case Back::Collection:
        libraryScreen_.showCollection(playedCard_);
        if (!libraryScreen_.inCollection()) {
            libraryScreen_.showGames();
        }
        break;
    }
    dirty_ = true;
}

void App::restart() {
    if (!session_) {
        play(0);
        return;
    }
    session_->restart();
    gameScreen_.reset();
    announced_ = false;
}

void App::loadCheckpoint() {
    if (!library_.ready) {
        return;
    }
    try {
        if (!CheckpointStore(checkpoint_).load()) {
            dialog_.open(
                kNotice, "No checkpoint yet",
                "Story progress is saved when you return to exploration or finish puzzle 001.",
                {"OK"});
            return;
        }
    } catch (const std::exception& error) {
        showLibrary(Back::Games);
        status_.message = error.what();
        return;
    }
    if (!session_) {
        play(0);
    }
    session_->loadCheckpoint();
    gameScreen_.reset();
    announced_ = false;
}

} // namespace picarat::shell
