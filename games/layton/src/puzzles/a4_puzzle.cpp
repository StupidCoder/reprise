// The fourth adventure's puzzle loops. See a4_puzzle.hpp.
#include "puzzles/a4_puzzle.hpp"
#include "puzzles/a4_cut.hpp"
#include "puzzles/a4_jump.hpp"
#include "puzzles/a4_lead.hpp"
#include "puzzles/a4_line.hpp"
#include "puzzles/a4_ltgram.hpp"
#include "puzzles/a4_slide.hpp"
#include "puzzles/a4_slide2.hpp"
#include "puzzles/a4_trampoline.hpp"
#include "puzzles/a4_vanish.hpp"
#include "sound/a4_sound.hpp"
#include "system/a4_progress.hpp"
#include <cstdio>
#include <stdexcept>

namespace picarat::adventure4 {

// ------------------------------------------------------------ session

namespace {
// ov17's names for the session's flags and variables (overlay 17 loads at
// 021049e0; the names are Shift-JIS strings in it).
constexpr std::uint32_t kOverlay17 = 0x021049e0;
constexpr std::uint32_t kVerdict =
    0x02106210;                              // variable: -1 none, 0 wrong, 1 right, 2 again, 3 quit
constexpr std::uint32_t kRetry = 0x0210617c; // flag: playing again
constexpr std::uint32_t kHints = 0x02106188; // variable: hints open
constexpr std::uint32_t kComboCount = 0x0210621c; // variable: puzzles solved in a row
constexpr std::uint32_t kCombo = 0x0210622c;      // flag: the run is long enough
// Literal pool words naming lt4_prog_nazo, lt4_nazo_hint, _quit, _input.
constexpr std::uint32_t kMacros[] = {0x021050f0, 0x021050f4, 0x021050f8, 0x021050fc};
// The puzzle index's flag (lt4_def 2001 id 1606, "in the puzzle
// dictionary"), set by lt4_menu_jiten before it asks for a puzzle.
constexpr int kDictionaryFlag = 1606;

// lt4CLoopNazo (ov17): the session.
class SessionLoop final : public Loop {
public:
    SessionLoop(Scene& scene, Services& services, std::span<const Value> values)
        : Loop(scene, services, values) {
        id = 3000;
        // 02104c70: an unknown puzzle ends the session at once.
        Assets& assets = scene.assets;
        GameData& data = services.data;
        number_ = values_[0].toInt();
        const GameData::Puzzle p = findPuzzle(assets, number_);
        if (p.number == 0) {
            if (services.loops) {
                services.loops->pop();
            }
            return;
        }
        // The music playing goes back on at the end (+0x114 from the
        // sound manager's +0x9340).
        if (services.audio) {
            music_ = services.audio->music();
        }
        // The puzzle becomes current (020c3460) with its parameters
        // (020c3508); not a retry; the hints open are the ones bought
        // before (none once solved); now it has been seen (020c3690 with
        // the counts that follow).
        data.puzzle = p;
        loadPuzzleParameters(assets, data);
        data.setFlag(name(kRetry), false);
        const auto& progress = data.puzzles[number_];
        data.setVariable(name(kHints), progress.state == 2 ? 0 : progress.hints);
        if (progress.state == 0) {
            setPuzzleState(assets, data, number_, 1);
        }
        for (std::uint32_t pool : kMacros) {
            scene.loadScript(name(assets.codeWord(17, kOverlay17, pool)));
        }
        char path[32];
        std::snprintf(path, sizeof path, "res/naz/seq/n%03d.cq", number_);
        if (assets.exists(path)) {
            scene.loadScript(path);
        }
        // (The memo canvas "lt4_memo", 020d2710, is *not reconstructed
        // yet*.)
    }

    ~SessionLoop() override {
        // 02105104: the music from before goes on again.
        if (services_.audio && music_ >= 0) {
            services_.audio->selectMusic(music_, 0, 0);
        }
    }

    void begin() override {
        // 02105148: on every return to the top, the next step.
        if (number_ <= 0 || !services_.loops) {
            return;
        }
        GameData& data = services_.data;
        const int index = values_[1].toInt();
        auto request = [&](int id, int a = -1, int b = -1) {
            services_.loops->request(id, {number_, index, a, b, -1, -1});
        };
        switch (state_) {
        case 0: // the title card
            state_ = 1;
            request(2200);
            return;
        case 1: // the puzzle, by its type, with no verdict yet
            data.setVariable(name(kVerdict), -1);
            state_ = 2;
            request(playLoopFor(data.puzzle.type), data.puzzle.type);
            return;
        case 2: {
            // (The verdicts of the escape and packing puzzles, read from
            // the game's own flags "正方形の詰め込み", "６×６の魔方陣",
            // "クイーンの配置", "脱出パズル", are *not reconstructed*.)
            const int v = data.variable(name(kVerdict));
            if (v == -1) {
                state_ = 1;
                request(2200);
                return;
            }
            if (v == 3) {
                state_ = 0;
                services_.loops->pop();
                return;
            }
            state_ = 3;
            // A run of solved puzzles shows on the verdict (main and
            // challenge puzzles not solved before).
            const bool counts = data.puzzles[number_].state != 2 && index < 170;
            data.setFlag(name(kCombo), counts && data.variable(name(kComboCount)) > 4);
            request(2210);
            return;
        }
        case 3: {
            const bool before = data.puzzles[number_].state != 2;
            const int v = data.variable(name(kVerdict));
            bool counts = before;
            if (v == 1) {
                counts = setPuzzleState(scene_.assets, data, number_, 2);
            } else if (data.puzzles[number_].state != 2) {
                ++data.puzzles[number_].failures; // 020c2fe8
            }
            if (counts && index < 170) {
                const int run = data.puzzles[number_].state == 2 ? data.variable(name(kComboCount)) + 1 : 0;
                data.setVariable(name(kComboCount), run);
            }
            if (v == 2) {
                state_ = 1;
                request(2200);
                return;
            }
            state_ = 0;
            services_.loops->pop();
            return;
        }
        default:
            return;
        }
    }

    // 2400 1000 / 1001 from the play loop (ov17 02105690 / 0210575c):
    // saves and restores the puzzle pieces (objects of a kind above 14).
    bool pieces(gss::Call& call, int op) {
        if (op == 1000) {
            // Every piece's place, position (with its offset, rounded) and
            // value 15; at most 64.
            saved_.clear();
            for (const Chara* o : scene_.objects()) {
                if (o->kind <= 14) {
                    continue;
                }
                const auto p = o->position.value(), d = o->offset.value();
                saved_.push_back({o->hash, o->placeHash, (p[0] + d[0] + 0x800) >> 12,
                                  (p[1] + d[1] + 0x800) >> 12,
                                  o->thread.values.size() > 15 ? o->thread.values[15] : Value{}});
                if (saved_.size() >= 64) {
                    break;
                }
            }
            call.result = Value::ofInt(1);
            return true;
        }
        if (op == 1001) {
            // Back onto the saved place (when it had one) and position.
            for (const Saved& s : saved_) {
                Chara* o = scene_.find(s.object);
                if (!o) {
                    continue;
                }
                if (Place* pl = scene_.findPlace(s.place)) {
                    o->placeHash = pl->hash;
                    o->parentMatrix = pl->matrix;
                    o->parentTranslation = pl->position;
                }
                o->position.set({s.x * kOne, s.y * kOne});
                if (o->thread.values.size() > 15) {
                    o->thread.values[15] = Value::ofInt(s.value15.toInt());
                }
            }
            saved_.clear();
            call.result = Value::ofInt(1);
            return true;
        }
        return false;
    }

private:
    std::string name(std::uint32_t address) const {
        return scene_.assets.codeText(17, kOverlay17, address);
    }

    struct Saved {
        std::uint32_t object = 0, place = 0;
        int x = 0, y = 0;
        gss::Value value15;
    };

    int number_ = 0;
    int state_ = 0; // +0x110
    int music_ = -1;
    std::vector<Saved> saved_;
};

// lt4CLoopNazoCommon (ov17): a play loop starts Main of the newest script
// (the puzzle's) and ends itself when Main is done. Its first values are
// the puzzle, its index, its type and the session.
std::unique_ptr<Loop> playLoop(std::unique_ptr<Loop> l, int id, Loop* session) {
    l->id = id;
    l->endsWithMain = true;
    l->beginsWithMain = true;
    if (auto* s = dynamic_cast<SessionLoop*>(session)) {
        l->commands = [s](gss::Call& call, int op) {
            return s->pieces(call, op);
        };
    }
    return l;
}
} // namespace

int playLoopFor(int type) {
    // ov17 02105148.
    switch (type) {
    case 10:
        return 3110;
    case 11:
        return 3120;
    case 12:
    case 30:
        return 3130;
    case 13:
        return 3140;
    case 14:
        return 3150;
    case 19:
        return 3160;
    case 24:
        return 3170;
    case 26:
        return 3180;
    case 27:
        return 3190;
    case 28:
        return 3200;
    default:
        return 3100;
    }
}

std::unique_ptr<Loop> makePuzzleLoop(int id, Scene& scene, Services& services,
                                     std::span<const Value> values, Loop* below) {
    switch (id) {
    case 3000:
        return std::make_unique<SessionLoop>(scene, services, values);
    case 3100:
        return playLoop(std::make_unique<Loop>(scene, services, values), id, below);
    case 3110:
        return playLoop(std::make_unique<CutLoop>(scene, services, values), id, below);
    case 3120:
        return playLoop(std::make_unique<LineLoop>(scene, services, values), id, below);
    case 3130:
        return playLoop(std::make_unique<SlideLoop>(scene, services, values), id, below);
    case 3140:
        return playLoop(std::make_unique<Slide2Loop>(scene, services, values), id, below);
    case 3150:
        return playLoop(std::make_unique<LtgramLoop>(scene, services, values), id, below);
    case 3170:
        return playLoop(std::make_unique<JumpLoop>(scene, services, values), id, below);
    case 3180:
        return playLoop(std::make_unique<LeadLoop>(scene, services, values), id, below);
    case 3190:
        return playLoop(std::make_unique<VanishLoop>(scene, services, values), id, below);
    case 3200:
        return playLoop(std::make_unique<TrampolineLoop>(scene, services, values), id, below);
    default:
        return nullptr;
    }
}

// ----------------------------------------------- a puzzle on its own

PuzzleSession::PuzzleSession(Assets& a, int number, bool fromIndex, Progress progress,
                             Adventure4Sound* sound)
    : assets(a), game_(std::make_unique<Game>(a, sound, false)), number_(number),
      scene(game_->scene), data(game_->data), services(game_->services) {
    data.puzzles[number] = progress;
    if (fromIndex) {
        data.setFlag(flagName(assets, kDictionaryFlag), true);
    }
    game_->request(3000, {number, -1, -1, -1, -1, -1});
}

PuzzleSession::~PuzzleSession() = default;

PuzzleSession::Progress PuzzleSession::progress() const {
    const auto it = data.puzzles.find(number_);
    return it == data.puzzles.end() ? Progress{} : it->second;
}

bool PuzzleSession::playable(int type) {
    return type != 19;
}

void PuzzleSession::frame(bool touching, int x, int y) {
    game_->frame(touching, x, y);
}

bool PuzzleSession::finished() const {
    return game_->depth() == 0 && !game_->pending();
}

void PuzzleSession::render(Surface& out) const {
    game_->render(out);
}

int PuzzleSession::verdict() const {
    return data.variable(assets.codeText(17, kOverlay17, kVerdict));
}

} // namespace picarat::adventure4
