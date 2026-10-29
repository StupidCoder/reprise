// The game loop from power-on: opening, title, name entry, story. See a2_game.hpp.
#include "story/a2_game.hpp"
#include "menus/a2_file_dialog.hpp"
#include "puzzles/a2_puzzle_game.hpp"
#include "story/a2_story_game.hpp"
#include "system/scene_kit.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <sstream>
#include <stdexcept>

namespace picarat {
Game::Game(std::filesystem::path assets, PresentFrame present, AudioService* audio,
           CheckpointStore* saves)
    : assets_(std::move(assets)), present_([callback = std::move(present), saves](
                                               const Surface& image, const GameSnapshot& state) {
          auto in = callback(image, state);
          if (in.collection) {
              throw OpenCollection{in.collection};
          }
          if (in.load && saves && saves->load()) {
              throw LoadCheckpoint{};
          }
          return in;
      }),
      saves_(saves), audio_(audio), keys_(assets_.keyboard()) {
    // The cartridge save image sits beside the native checkpoint (research
    // tools without a checkpoint store run without one).
    if (saves_) {
        saveFile_ = std::make_unique<SaveFile>(SaveFile::besideCheckpoint(saves_->path()));
    }
}

const char* Game::sceneName(SceneKind scene) {
    switch (scene) {
    case SceneKind::Logos:
        return "Logos";
    case SceneKind::Title:
        return "Title";
    case SceneKind::Name:
        return "Name entry";
    case SceneKind::Accepted:
        return "Name accepted";
    case SceneKind::Story:
        return "Prologue";
    case SceneKind::Movie:
        return "Opening movie";
    case SceneKind::Room:
        return "Explore";
    case SceneKind::PuzzleIntro:
        return "Puzzle introduction";
    case SceneKind::Puzzle:
        return "Puzzle";
    case SceneKind::PuzzleHint:
        return "Puzzle hint";
    case SceneKind::PuzzleMemo:
        return "Puzzle memo";
    case SceneKind::PuzzleJudgment:
        return "Judgment";
    case SceneKind::PuzzleWrong:
        return "Try again";
    case SceneKind::PuzzleReward:
        return "Picarats";
    case SceneKind::PuzzleSolved:
        return "Puzzle solved";
    case SceneKind::Collection:
        return "Puzzle collection";
    case SceneKind::Complete:
        return "Reconstruction checkpoint";
    case SceneKind::PuzzleCard:
        return "Puzzle card";
    case SceneKind::Tea:
        return "Herb tea";
    case SceneKind::Bag:
        return "Bag";
    case SceneKind::Camera:
        return "Camera";
    case SceneKind::Photo:
        return "Photo hunt";
    case SceneKind::Hamster:
        return "Hamster";
    case SceneKind::Staff:
        return "Staff roll";
    case SceneKind::Shack:
        return "Puzzle shack";
    case SceneKind::Mysteries:
        return "Mysteries";
    case SceneKind::Notes:
        return "Protagonist's notes";
    case SceneKind::Diary:
        return "Old diary";
    case SceneKind::PuzzleIndex:
        return "Puzzle index";
    case SceneKind::Challenge:
        return "Challenge";
    case SceneKind::Passcode:
        return "Passcode";
    case SceneKind::Files:
        return "Save files";
    case SceneKind::Bonus:
        return "Bonus";
    }
}

// Naming the hamster (story scene 37) reuses the name-entry screen
// (overlay 12); the player's own name is kept.
NameEditor Game::nameHamster() {
    const auto player = name_;
    nameEntry(true);
    const auto hamster = name_;
    name_ = player;
    return hamster;
}

// The whole game: logos, title, name entry, then the story, for as long as
// the app runs. Restarting, returning to the title, loading the checkpoint,
// jumping to the first puzzle and opening the puzzle collection all leave
// the running scene by exception and start over here in the right place.
void Game::run() {
    bool direct = false, load = false, titleOnly = false;
    int library = 0;
    if (continueCheckpoint && saves_) {
        try {
            load = saves_->load().has_value();
        } catch (const std::exception&) {
            // A checkpoint this version cannot continue: the title instead.
        }
    }

    // Button click effects (ov2 02080300) go to the overlay-2 sound object.
    struct ClickSound {
        ClickSound(AudioService* audio) {
            Button::clickSound = [audio](int id) {
                if (auto s = audio ? audio->sequenced() : nullptr) {
                    s->playEffect(id);
                }
            };
        }

        ~ClickSound() { Button::clickSound = nullptr; }
    } clickSound(audio_);

    for (;;) {
        try {
            if (audio_) {
                audio_->reset();
            }
            scene_ = SceneKind::Logos;
            timing_.mainBrightness = timing_.subBrightness = -16;
            tick_ = 0;
            wasDown_ = false;
            touch_ = {};
            mainBackground_.clear();
            subBackground_.clear();
            if (library) {
                collection(library);
                library = -1;
            }
            if (load) {
                auto saved = saves_->load();
                if (!saved) {
                    throw std::runtime_error("No native checkpoint");
                }
                StoryGame story(assets_, present_, saved->name, audio_, saves_);
                story.saveFile = saveFile_.get();
                story.nameScene = [this]() {
                    return nameHamster();
                };
                story.resume(*saved);
            }
            auto choice = TitleChoice::NewGame;
            if (!direct) {
                if (!titleOnly && !startAtTitle) {
                    auto script = readFile(assets_.root / "logo.gds");
                    ScriptInterpreter opening(script);

                    // The logos are a timed script the host may skip: its
                    // waits end and its fades jump to their end.
                    struct Skippable {
                        Game& game;

                        explicit Skippable(Game& g) : game(g) {
                            game.skippable_ = true;
                            game.timing_.interrupted = [&g] {
                                return g.skip_;
                            };
                        }

                        ~Skippable() {
                            game.skippable_ = false;
                            game.timing_.interrupted = nullptr;
                        }
                    } skippable(*this);

                    opening.run(openingContext(*this));
                    if (skip_) {
                        // Skipped: the logo jingle (logo.gds 0x99) stops too.
                        if (auto s = audio_ ? audio_->sequenced() : nullptr) {
                            s->stopStream(0);
                        }
                    }
                }
                titleOnly = false;
                checkSave(); // scene 0 (02069f20) before the title
                choice = title();
                if (choice == TitleChoice::NewGame) {
                    nameEntry();
                }
            }
            if (choice != TitleChoice::NewGame) {
                // ov9 020f0584..020f065c: Continue starts the chp_inf event
                // of the loaded story step (or the room); Secret opens the
                // bonus menu (scene 28, +0x8221 set).
                StoryGame story(assets_, present_, loadedName_, audio_, saves_);
                story.saveFile = saveFile_.get();
                story.nameScene = [this]() {
                    return nameHamster();
                };
                story.continueFrom(*loaded_, choice == TitleChoice::Secret);
            } else {
                StoryGame story(assets_, present_, name_, audio_, saves_);
                story.saveFile = saveFile_.get();
                story.nameScene = [this]() {
                    return nameHamster();
                };
                story.run(direct);
            }
        } catch (const OpenCollection& request) {
            library = request.internalId;
            direct = false;
            load = false;
            continue;
        } catch (const Restart&) {
            library = 0;
            direct = false;
            load = false;
            continue;
        } catch (const ReturnToTitle&) {
            library = 0;
            direct = false;
            load = false;
            titleOnly = true;
            continue;
        } catch (const JumpToPuzzle&) {
            library = 0;
            direct = true;
            load = false;
            continue;
        } catch (const LoadCheckpoint&) {
            library = 0;
            load = true;
            direct = false;
            continue;
        }
    }
}

// Picarat's puzzle collection (a native addition): the list of all puzzles,
// each played with its own temporary progress. It never reads or writes the
// story checkpoint.
void Game::collection(int initialId) {
    const auto raw = readFile(assets_.root / "puzzle/library.txt");
    std::istringstream ids(std::string(raw.begin(), raw.end()));
    std::vector<unsigned> entries;
    unsigned id;
    while (ids >> id) {
        entries.push_back(id);
    }
    GameText text(assets_.root);
    std::vector<PuzzleData> data;
    for (auto entry : entries) {
        data.emplace_back(readFile(assets_.root / ("puzzle/n" + std::to_string(entry) + ".dat")),
                          text);
    }
    std::map<unsigned, PuzzleSession> progress;
    int selected = initialId;
    bool down = false;
    unsigned page = 0;
    constexpr unsigned pageSize = 10;
    for (;;) {
        try {
            if (selected > 0) {
                if (std::find(entries.begin(), entries.end(), unsigned(selected)) ==
                    entries.end()) {
                    throw std::runtime_error("puzzle not in validated collection");
                }
                const auto row =
                    std::size_t(std::find(entries.begin(), entries.end(), unsigned(selected)) -
                                entries.begin());
                // The collection stands for the ROM's puzzle index (scene 0x13, ov19,
                // which stores its own scene in +479) and special catalogue (0x16).
                PuzzleLaunch launch;
                launch.followingScene = data[row].number >= 154 ? 22 : 0x13;
                PuzzleGame puzzle(assets_, present_, name_, audio_, &progress[unsigned(selected)],
                                  unsigned(selected), launch);
                puzzle.run();
                selected = -1;
                down = false;
                if (auto s = audio_ ? audio_->sequenced() : nullptr) {
                    s->stopStream(0);
                    s->stopBgm(0);
                    s->stopUniqueEffect(0);
                }
            }
            Surface image{256, 384, std::vector<std::uint16_t>(256 * 384, 0xffff)};
            const auto& font = assets_.font("fontq.NFTR");
            textLines(image, font, "Picarat - Puzzle collection", 8, 12, 14);
            textLines(image, font,
                      "Choose a puzzle below.\nLater-game puzzles may spoil the story.\n\nProgress "
                      "lasts for this collection visit.\nStory saves are not changed.\n\nCmd-K: "
                      "return to this list\nCmd-L: resume story checkpoint",
                      8, 40, 14);
            for (unsigned n = page * pageSize;
                 n < std::min(unsigned(entries.size()), (page + 1) * pageSize); ++n) {
                std::string label = std::to_string(data[n].number) + "  " + data[n].title;
                if (progress.contains(entries[n]) && progress[entries[n]].first.status() == 2) {
                    label = "* " + label;
                }
                textLines(image, font, label, 5, 198 + int(n % pageSize) * 16, 14);
            }
            textLines(image, font,
                      "Page " + std::to_string(page + 1) + " / " +
                          std::to_string((entries.size() + pageSize - 1) / pageSize),
                      8, 172, 14);
            if (page > 0) {
                textLines(image, font, "< Previous", 5, 368, 14);
            }
            if ((page + 1) * pageSize < entries.size()) {
                textLines(image, font, "Next >", 200, 368, 14);
            }
            auto input = present_(image, {SceneKind::Collection, name_, tick_++, int(page)});
            if (input.restart) {
                throw RestartGame{};
            }
            if (input.puzzle) {
                throw JumpToPuzzle{};
            }
            if (input.load && saves_ && saves_->load()) {
                throw LoadCheckpoint{};
            }
            if (!input.down && down && input.x >= 0 && input.x < 256) {
                if (input.y >= 6 && input.y < 166) {
                    const auto row = page * pageSize + unsigned(input.y - 6) / 16;
                    if (row < entries.size()) {
                        selected = int(entries[row]);
                    }
                } else if (input.y >= 174 && input.y < 192) {
                    if (input.x < 100 && page > 0) {
                        --page;
                    } else if (input.x >= 170 && (page + 1) * pageSize < entries.size()) {
                        ++page;
                    }
                }
            }
            down = input.down;
        } catch (const OpenCollection& request) {
            selected = request.internalId;
            down = false;
            if (audio_) {
                audio_->reset();
            }
        }
    }
}

void Game::replayMovie(unsigned event) {
    if (audio_) {
        audio_->reset();
    }
    StoryGame story(assets_, present_, NameEditor{}, audio_, nullptr);
    story.replayMovie(event);
}

void Game::playMinigame(std::uint8_t scene, SaveProgress& progress) {
    if (audio_) {
        audio_->reset();
    }
    StoryGame story(assets_, present_, NameEditor{}, audio_, nullptr);
    story.playMinigame(scene, progress);
}

void Game::runNameForTesting() {
    nameTestOnly_ = true;
    nameEntry();
}

void Game::nextFrame() {
    if (auto s = audio_ ? audio_->sequenced() : nullptr) {
        s->frame(); // main loop 0206cb00
    }
    auto input = present_(render(), snapshot());
    if (scene_ == SceneKind::Name && input.name.action != NameInput::Action::None) {
        nameInput_.push_back(input.name);
    }
    if (input.load && saves_ && saves_->load()) {
        throw LoadCheckpoint{};
    }
    if (input.restart) {
        throw Restart{};
    }
    if (input.puzzle) {
        throw JumpToPuzzle{};
    }
    touch_ = {input.down, input.down && !wasDown_, !input.down && wasDown_, input.x, input.y};
    wasDown_ = input.down;
    resume_ = input.resume;
    skip_ = input.skip;
}

void Game::brightness(bool sub, std::int32_t value) {
    if (sub) {
        timing_.subBrightness = value;
    } else {
        timing_.mainBrightness = value;
    }
}

void Game::background(bool sub, const std::string& name, std::int32_t layer) {
    if (layer != 3 || name.size() < 4) {
        throw std::runtime_error("opening background outside reviewed subset");
    }
    auto path = name.substr(0, name.size() - 4);
    assets_.background(path);
    (sub ? subBackground_ : mainBackground_) = path;
    timing_.wait(1);
}

void Game::wait(std::int32_t count, bool release) {
    if (release) {
        timing_.waitForTouchRelease(count);
    } else {
        timing_.wait(count);
    }
}

void Game::audioRequest(std::int32_t id, std::uint32_t volume, std::uint32_t fade,
                        std::int32_t route) {
    // logo.gds 0099 -> ov2 0207c098: category 0 plays stream route 0 (0207fb84),
    // category 1 the tracked variant (0207fc3c); other categories go to a
    // scene virtual that is not recovered.
    if (route != 0 && route != 1) {
        throw std::runtime_error("opening stream request outside reviewed subset");
    }
    auto s = audio_ ? audio_->sequenced() : nullptr;
    if (!s) {
        return;
    }
    if (route == 0) {
        s->playStream(0, id, -1, std::bit_cast<float>(volume), std::bit_cast<float>(fade), false);
    } else {
        s->playTrackedStream(0, id, -1, std::bit_cast<float>(volume), std::bit_cast<float>(fade));
    }
}

void Game::initializeActor(Actor& a, const std::string& path, const std::string& animation, int x,
                           int y, const std::string& position) {
    a = {};
    a.sprite = &assets_.sprite(path);
    a.x = x;
    a.y = y;
    if (!position.empty()) {
        const auto& p = a.sprite->variables.at(position);
        a.x += p[0];
        a.y += p[1];
    }
    a.player.select(a.sprite->animationNames.at(animation), tick_, a.sprite->animations);
}

void Game::drawActor(Surface& target, Actor& actor, int screenOffset) {
    const auto& step = actor.sprite->animations.at(actor.player.animation).at(actor.player.step);
    blit(target, actor.sprite->images.at(step.image), actor.x, actor.y + screenOffset);
    if (advanceAnimations_) {
        actor.player.advance(tick_, actor.sprite->animations);
    }
}

void Game::updateVisual(Button& button, Actor& actor, bool labels) {
    if (labels) {
        const char* label = button.state == Button::State::Clicked                  ? "click"
                            : button.state == Button::State::Armed && button.inside ? "on"
                                                                                    : "off";
        actor.player.select(actor.sprite->animationNames.at(label), tick_,
                            actor.sprite->animations);
    }
}

// The title screen: the train animation, then the Start / Continue buttons.
Game::TitleChoice Game::title() {
    // ov9:020efda0. With a used save slot (+0x24) the start button moves to
    // its "pos" variable (020801a8) and Continue and Secret appear.
    scene_ = SceneKind::Title;
    mainBackground_ = "title/title";
    subBackground_ = "title/title_sub";
    initializeActor(train_, "title/train.arc", "gfx", 0, 0, "pos");
    initializeActor(tracks_, "title/senro.arc", "gfx", 0, 0, "pos");
    initializeActor(frame_, "title/waku.arc");
    slotsUsed_ = saveFile_ && saveFile_->anyUsed();
    initializeActor(start_, "title/ge/start.arc", "off", 0, 0, slotsUsed_ ? "pos" : "pos2");
    if (slotsUsed_) {
        initializeActor(continue_, "title/ge/continue.arc", "off", 0, 0, "pos");
        initializeActor(secret_, "title/ge/secret.arc", "off", 0, 0, "pos");
        auto hit = [](const Actor& a) {
            const auto& image = a.sprite->images[0];
            return Button({a.x - 2, a.y - 2, int(image.width), int(image.height)});
        };
        continueButton_ = hit(continue_);
        secretButton_ = hit(secret_);
    }
    initializeActor(logo_, "title/ge/logo.arj", "gfx", 0, 0, "pos");
    // ov2:0207430c offsets sprite hit origins by -2, with inclusive bounds.
    const auto& startImage = start_.sprite->images[0];
    startButton_ =
        Button({start_.x - 2, start_.y - 2, int(startImage.width), int(startImage.height)});
    titleScroll_ = 0;
    titleBlend_ = 0;
    // ov9 020f0704: stop the previous stream, then play bgm/BG_001 (route 1).
    if (auto s = audio_ ? audio_->sequenced() : nullptr) {
        s->stopStream(0);
        s->playStream(1, 1, -1, 1, 0, false);
    }
    for (int i = 0; i < 5; ++i) {
        --titleScroll_;
        timing_.wait(1);
    }
    timing_.wait(1);
    // ov2:02071a30 reveals SUB first. The original buffers stay unchanged
    // during these single-frame waits, while brightness alone changes.
    advanceAnimations_ = false;
    for (int i = 0; i < 16; ++i) {
        timing_.wait(1);
        timing_.subBrightness = std::min(0, timing_.subBrightness + 1);
    }
    advanceAnimations_ = true;
    unsigned intro = 0;
    bool suppressClick = false;
    for (;;) {
        ++intro;
        --titleScroll_;
        if (titleBlend_ < 15) {
            if (touch_.pressed) {
                titleBlend_ = 15;
                suppressClick = true;
            } else if (intro > 40 && intro % 6 == 1) {
                ++titleBlend_;
            }
            if (titleBlend_ == 15) {
                timing_.wait(1);
                advanceAnimations_ = false;
                // ov2:02071d28 uses 17 iterations to reveal MAIN.
                for (int i = 0; i < 17; ++i) {
                    timing_.wait(1);
                    timing_.mainBrightness = std::min(0, timing_.mainBrightness + 1);
                }
                advanceAnimations_ = true;
            }
        }
        if (!suppressClick) {
            startButton_.update(touch_);
            if (slotsUsed_) {
                continueButton_.update(touch_);
                secretButton_.update(touch_);
            }
        }
        updateVisual(startButton_, start_, true);
        if (slotsUsed_) {
            updateVisual(continueButton_, continue_, true);
            updateVisual(secretButton_, secret_, true);
        }
        if (touch_.released) {
            suppressClick = false;
        }
        timing_.wait(1);
        if (startButton_.consumeClick()) {
            break;
        }
        // 020f04a4: Continue (mode 0) or Secret (mode 1): SE 207, the music
        // stops (time 0x3ed), the logo stays, the main screen fades
        // (02072260); once the stream is quiet the load dialog runs.
        int mode = -1;
        if (slotsUsed_ && continueButton_.consumeClick()) {
            mode = 0;
        } else if (slotsUsed_ && secretButton_.consumeClick()) {
            mode = 1;
        }
        if (mode >= 0) {
            auto* s = audio_ ? audio_->sequenced() : nullptr;
            if (s) {
                s->playEffect(207);
                s->stopStream(s->duration(0x3ed));
            }
            titleBlend_ = 15;
            for (int i = 0; i <= 16; ++i) {
                timing_.wait(1);
                timing_.mainBrightness = std::max(-16, timing_.mainBrightness - 1);
            }
            timing_.mainBrightness = -16;
            while (s && s->streamBusy()) {
                timing_.wait(1);
            }
            if (loadSlot(mode)) {
                return mode == 0 ? TitleChoice::Continue : TitleChoice::Secret;
            }
            // 020f0660: cancelled: the title again, its music, room sound 1.
            while (s && s->streamBusy()) {
                timing_.wait(1);
            }
            scene_ = SceneKind::Title;
            mainBackground_ = "title/title";
            timing_.wait(1);
            if (s) {
                s->stopStream(0);
                s->playStream(1, 1, -1, 1, 0, false);
            }
            for (int i = 0; i < 17; ++i) {
                timing_.wait(1);
                timing_.mainBrightness = std::min(0, timing_.mainBrightness + 1);
            }
            if (s) {
                s->roomSound(1, true);
            }
        }
    }
    // ov9 020f04ec: new game stops this stream over time id 0x3ed.
    if (auto s = audio_ ? audio_->sequenced() : nullptr) {
        s->stopStream(s->duration(0x3ed));
    }
    timing_.fadeOut();
    timing_.wait(100);
    return TitleChoice::NewGame;
}

bool Game::loadSlot(int mode) {
    // 02086ad4 on a dialog whose owner is the title (its logo, 020f02cc,
    // stays on the upper screen).
    if (!saveFile_) {
        return false;
    }
    SceneKit kit(assets_, present_, audio_, SceneKind::Files);
    kit.upperBackground = "title/title_sub";
    kit.setUpperBrightness(timing_.subBrightness);
    kit.setLowerBrightness(timing_.mainBrightness);
    // The title's logo object (020746d0) sits at its "pos" variable.
    auto logo = kit.load("title/?/logo.sbj", 0, 0, 1);
    kit.select(logo, "gfx");
    kit.variable(logo, "pos", logo.x, logo.y);
    loaded_ = std::make_unique<GameState>(assets_.root);
    FileDialog dialog(kit, *loaded_, *saveFile_, [&] { kit.draw(logo); }, false);
    std::string name;
    const int slot = dialog.load(mode, name);
    if (slot < 0) {
        timing_.mainBrightness = -16;
        return false;
    }
    loadedName_.clear();
    for (char c : name) {
        loadedName_.insert(std::uint8_t(c));
    }
    timing_.mainBrightness = timing_.subBrightness = -16;
    return true;
}

void Game::checkSave() {
    // 0206bbac: the save is checked whenever the game returns to scene 0.
    // A damaged part shows system/?/save_corrupt (main screen) with the OK
    // button until touched, then the damaged parts are cleared and written.
    if (!saveFile_) {
        return;
    }
    const unsigned damaged = saveFile_->boot();
    if (!damaged) {
        return;
    }
    SceneKit kit(assets_, present_, audio_, SceneKind::Files);
    auto ok = kit.button("system/btn/?/save_ok.spr", 96, 122, 3);
    kit.setButtonAnimations(ok, "gfx", "gfx", "gfx");
    ok.pressX = ok.pressY = 1;
    kit.poll();
    kit.drawButton(ok);
    kit.lowerBackground = kit.localized("system/?/save_corrupt");
    kit.present();
    kit.fadeInLower();
    for (;;) {
        kit.poll();
        kit.updateButton(ok);
        kit.drawButton(ok);
        kit.present();
        if (kit.clicked(ok)) {
            break;
        }
    }
    saveFile_->repair(damaged);
    kit.fadeOutLower();
}

void Game::initializeName(bool hamster) {
    // ov12 constructor and keyboard layout routines; positions are literal ROM
    // operands or extracted sprite variables, not fitted to a screenshot.
    // Scene 37 (hamster) selects name_sub_ham instead (020efdf0).
    scene_ = SceneKind::Name;
    subBackground_ = hamster ? "name/ge/name_sub_ham" : "name/ge/name_sub";
    name_.clear();
    nameInput_.clear();
    showBadName_ = false;
    initializeActor(cursor_, "nazo/drawinput/di_cursor.arc", "noname", 52, 29);
    initializeActor(nameOk_, "name/ge/name_ok.arc", "gfx", 96, 161);
    initializeActor(normal_, "name/name_btn.arc", "normal", 3, 129);
    initializeActor(special_, "name/name_btn.arc", "tokushu", 34, 129);
    initializeActor(erase_, "name/name_btn.arc", "erase", 194, 129);
    initializeActor(space_, "name/name_btn.arc", "space", 68, 129);
    initializeActor(badName_, "name/ge/bad_name.arc", "gfx", 0, 59);
    badName_.x = (256 - int(badName_.sprite->images[0].width)) / 2;
    okButton_ = Button({94, 159, 64, 24});
    normalButton_ = Button({1, 127, 28, 20});
    specialButton_ = Button({32, 127, 28, 20});
    eraseButton_ = Button({192, 127, 59, 20});
    spaceButton_ = Button({66, 127, 120, 20});
    shiftButton_ = Button({5, 87, 28, 18});
    symbolButton_ = Button({5, 106, 37, 18});
    selectLayout(0);
}

void Game::selectLayout(int layout) {
    name_.layout = layout;
    mainBackground_ = "name/name_bg" + std::to_string(layout + 1);
    for (unsigned i = 0; i < (layout == 3 ? 52u : 45u); ++i) {
        keyButtons_[i] = Button(NameEditor::keyBounds(i, layout));
    }
}

void Game::nameUpdate() {
    for (auto* b : {&okButton_, &eraseButton_, &spaceButton_, &normalButton_, &specialButton_}) {
        b->update(touch_);
    }
    if (name_.layout != 3) {
        shiftButton_.update(touch_);
        symbolButton_.update(touch_);
    }
    if (shiftButton_.consumeClick()) {
        selectLayout(name_.layout == 1 ? 0 : 1);
    }
    if (symbolButton_.consumeClick()) {
        selectLayout(2);
    }
    if (normalButton_.consumeClick()) {
        selectLayout(0);
    }
    if (specialButton_.consumeClick()) {
        selectLayout(3);
    }
    if (eraseButton_.consumeClick()) {
        name_.erase();
    }
    if (spaceButton_.consumeClick()) {
        name_.insert(32);
    }
    const auto layout = name_.layout;
    for (unsigned i = 0; i < (layout == 3 ? 52u : 45u); ++i) {
        auto& b = keyButtons_[i];
        b.update(touch_);
        if (b.state == Button::State::Armed) {
            return;
        }
        if (b.consumeClick()) {
            name_.insert(keys_[layout].at(i));
            if (layout == 2) {
                selectLayout(0);
            }
            return;
        }
    }
}

// Name entry (overlay 12): an on-screen keyboard with letter, symbol and
// special-character layouts. "OK" accepts the name unless it is empty or one
// of the lead characters' names, which the game refuses.
void Game::nameEntry(bool hamster) {
    initializeName(hamster);
    timing_.mainBrightness = timing_.subBrightness = -16;
    auto sound = [&]() {
        return audio_ ? audio_->sequenced() : nullptr;
    };
    if (auto s = sound()) {
        s->roomSound(5, true); // ov12 020efda0: row 5, no music, GE bank 2
    }
    timing_.fadeIn();
    for (;;) {
        NameInput key;
        if (!nameInput_.empty()) {
            key = nameInput_.front();
            nameInput_.pop_front();
        }
        const bool confirm = key.action == NameInput::Action::Confirm;
        if (showBadName_) {
            if (touch_.released || confirm || key.action == NameInput::Action::Erase) {
                showBadName_ = false;
                if (auto s = sound()) {
                    s->playEffect(0x6f);
                }
                name_.clear();
            }
        } // 020f02f0
        else if (scene_ == SceneKind::Accepted) {
            if (resume_) {
                scene_ = SceneKind::Name;
                resume_ = false;
            }
        } else {
            // Native keyboard convenience: accept only the ROM keyboard's
            // characters, then use its existing insertion/deletion semantics.
            if (key.action == NameInput::Action::Erase) {
                name_.erase();
            }
            if (key.action == NameInput::Action::Insert) {
                bool allowed = key.character == 32;
                for (const auto& layout : keys_) {
                    allowed |=
                        std::find(layout.begin(), layout.end(), key.character) != layout.end();
                }
                if (allowed) {
                    name_.insert(key.character);
                }
            }
            nameUpdate();
            if (okButton_.consumeClick() || confirm) {
                // ov12 020f09d8/020f02f0: reserved or empty names play SY 4, an accepted name GE
                // 0x6f.
                if (name_.reserved(names_.nameEntry)) {
                    showBadName_ = true;
                    if (auto s = sound()) {
                        s->playEffect(4);
                    }
                } else if (!name_.value().empty()) {
                    scene_ = SceneKind::Accepted;
                    if (auto s = sound()) {
                        s->playEffect(0x6f);
                    }
                } else if (auto s = sound()) {
                    s->playEffect(4);
                }
            }
        }
        timing_.wait(1);
        if (scene_ == SceneKind::Accepted && !nameTestOnly_) {
            return;
        }
    }
}

Surface Game::render() {
    Surface image{256, 384, std::vector<std::uint16_t>(256 * 384, 0x8000)};
    if (!subBackground_.empty()) {
        blit(image, assets_.background(subBackground_), 0, 0);
    }
    if (!mainBackground_.empty()) {
        blit(image, assets_.background(mainBackground_), 0, 192);
    }
    if (scene_ == SceneKind::Title) {
        // ov9:020f01a0 uses signed remainder, not a positive wrapped coordinate.
        tracks_.x = -(titleScroll_ * 3 % 256);
        drawActor(image, tracks_);
        tracks_.x -= 256;
        drawActor(image, tracks_);
        drawActor(image, train_);
        drawActor(image, frame_);
        drawActor(image, start_);
        if (slotsUsed_) {
            drawActor(image, continue_);
            drawActor(image, secret_);
        }
        if (titleBlend_ > 0) {
            const auto& s = logo_.sprite->animations[logo_.player.animation][logo_.player.step];
            blit(image, logo_.sprite->images[s.image], logo_.x, logo_.y, titleBlend_,
                 15 - titleBlend_);
        }
    } else if (scene_ == SceneKind::Name || scene_ == SceneKind::Accepted) {
        auto buttonActor = [&](Actor& a, const Button& b) {
            int x = a.x, y = a.y;
            if (b.state == Button::State::Armed && b.inside) {
                ++a.x;
                ++a.y;
            }
            drawActor(image, a);
            a.x = x;
            a.y = y;
        };
        buttonActor(nameOk_, okButton_);
        buttonActor(erase_, eraseButton_);
        buttonActor(space_, spaceButton_);
        buttonActor(normal_, normalButton_);
        buttonActor(special_, specialButton_);
        const auto& font = assets_.font("fontevent.NFTR");
        for (unsigned i = 0; i < 10; ++i) {
            drawGlyph(image, font, name_.characters[i], 52 + 16 * i, 192 + 18);
        }
        cursor_.x = 52 + 16 * name_.cursor;
        drawActor(image, cursor_);
        if (showBadName_) {
            drawActor(image, badName_);
        }
    }
    applyBrightness(image, timing_.subBrightness, timing_.mainBrightness);
    return image;
}
} // namespace picarat
