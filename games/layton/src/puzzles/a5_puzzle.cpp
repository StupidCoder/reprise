// The fifth adventure's puzzle session and standard play loop. See
// a5_puzzle.hpp.
#include "puzzles/a5_puzzle.hpp"
#include "graphics/a56_font.hpp"
#include "graphics/a56_text.hpp"
#include "puzzles/a5_special.hpp"
#include "puzzles/a6_puzzle.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace picarat::adventure5 {

PuzzleSession::PuzzleSession(Game& game, int puzzleId, bool standalone) : game_(game) {
    record_ = game.defs.puzzle(puzzleId);
    if (!record_) {
        throw std::runtime_error("unknown puzzle id");
    }
    game.puzzle = record_;
    game.special.reset();
    // A new game's state (the reference starts puzzles from "Neues
    // Spiel": 10 hint coins, ...).
    if (standalone) {
        game.applyInitialState();
    }
    // The screen that starts a puzzle fades both screens to black and
    // leaves them so; the puzzle's intro fades them back in (NazoFadeIn:
    // the top at once, under the hat mask and the four black covers it
    // creates a frame later). Starting at normal brightness showed the
    // whole puzzle picture for a frame.
    for (auto& b : game.scene.brightness) {
        b.set({-1.0f});
    }
    // Fonts the game keeps loaded (hypothesis: the boot's game loop loads
    // them; the services name them "_FNT_NRM" / "_FNT_SML").
    for (const auto& [name, path] :
         {std::pair<const char*, const char*>{"_FNT_NRM", "fnt/[eu]/nrm"},
          {"_FNT_SML", "fnt/[eu]/sml"}}) {
        if (!game.scene.resource(name)) {
            const auto folder = game.pack.find(path, ".xf");
            if (!folder.empty()) {
                adventure56::Resource r;
                r.kind = adventure56::Resource::Kind::Font;
                r.font = adventure56::loadFont(game.pack, folder);
                game.scene.addResource(name, std::move(r));
            }
        }
    }
    // The game loop's scripts (list at 0x182958: prog_common, prog_saveload,
    // prog_memo, prog_event; the sixth adventure's 0x1d6f5c loads prog_lt
    // after prog_common, which holds CreateNazoCamera there), loaded before
    // any session.
    for (const char* s : {"seq/prog_common", "seq/prog_lt", "seq/prog_saveload", "seq/prog_memo",
                          "seq/prog_event"}) {
        if (std::strcmp(s, "seq/prog_lt") != 0 || game.edition == Edition::Sixth) {
            game.loadScript(s);
        }
    }
    // 0x18394c: the frame's scripts, the puzzle's, its texts.
    for (const char* s : {"seq/prog_hint", "seq/prog_memo", "seq/prog_nazo", "seq/nazo_intro",
                          "seq/nazo_quit", "seq/nazo_judge"}) {
        game.loadScript(s);
    }
    char path[64];
    std::snprintf(path, sizeof path, "res/naz/seq/n%03d", record_->id);
    game.loadScript(path);
    std::snprintf(path, sizeof path, "txt/@/50/50_%06d.xs", record_->id);
    const auto texts = game.pack.find(path, "");
    if (!texts.empty()) {
        adventure56::Resource r;
        r.kind = adventure56::Resource::Kind::Text;
        r.text = adventure56::loadTextList(game.pack, texts);
        game.scene.addResource("nazo_text", std::move(r));
    }
    // The puzzle's parameters (res/lt5_naz.xb/nNNN.xs, one 3000 row: help
    // pictures index, music, ...) become the six values 16540 reads
    // (+0x12c80 of the save object). Hypothesis: the row's 3000 handler
    // stores them; the handler is not located yet.
    // The sixth adventure keeps seven values (16540: index < 7, 0x24d5b8)
    // and the session start writes all seven as numbers: the row's five,
    // then -1 and 0 (the reference's RAM at 0x4073fc during puzzle 002,
    // written at its start through 0x28a0c8; every row of lt6_naz has five
    // values). The help scripts read the seventh as the help animation's
    // number, 0 meaning the help text's ("nazo/@/hlp/h%02d").
    if (game.edition == Edition::Sixth) {
        game.puzzleValues.assign(7, xq::Value::ofInt(0));
        game.puzzleValues[5] = xq::Value::ofInt(-1);
    } else {
        game.puzzleValues.assign(6, xq::Value{});
    }
    std::snprintf(path, sizeof path, "%s/n%03d.xs", puzzleParametersPath(game.edition).c_str(),
                  record_->id);
    if (const auto params = game.pack.find(path, ""); !params.empty()) {
        const auto list = import::adventure56::parseXscr(game.pack.read(params));
        for (const auto& c : list.commands) {
            if (c.opcode != 3000) {
                continue;
            }
            for (unsigned i = 0; i < c.count && i < game.puzzleValues.size(); ++i) {
                const auto& o = list.operands[c.first + i];
                switch (o.type & 15) {
                case 1:
                    game.puzzleValues[i] = xq::Value::ofInt(std::int32_t(o.value));
                    break;
                case 2:
                    game.puzzleValues[i] = xq::Value::ofUint(o.value);
                    break;
                case 3: {
                    float f;
                    std::memcpy(&f, &o.value, sizeof f);
                    game.puzzleValues[i] = xq::Value::ofFloat(f);
                    break;
                }
                default:
                    break;
                }
            }
        }
    }
    thread_ = std::make_unique<xq::Thread>(game.machine, 16);
    // The loop values: puzzle id, number, kind (0), the session.
    thread_->values[0] = xq::Value::ofInt(record_->id);
    thread_->values[1] = xq::Value::ofInt(record_->number);
    thread_->values[2] = xq::Value::ofInt(0);
    update_ = std::make_unique<xq::Thread>(game.machine, 16);
    game.loopThreads[0] = thread_.get();
    game.loopThreads[1] = update_.get();
    thread_->start(*game.newestScript(), "Init");
}

bool PuzzleSession::playable(int type, Edition edition) {
    if (edition == Edition::Sixth) {
        return adventure6::playLoop(type) == adventure6::kCommonLoop ||
               adventure6::fifthTypeWithSixthRules(type) != 0 || adventure6::hasOwnClass(type);
    }
    return (type >= 1 && type <= 8) || type == 20 || type == 44 || type == 45 || type == 48 ||
           hasSpecialRules(type);
}

bool PuzzleSession::frame(const Input& input) {
    game_.beginFrame(input);
    update();
    game_.endFrame();
    return state_ != State::Ended;
}

bool PuzzleSession::update() {
    switch (state_) {
    case State::Init:
        thread_->run();
        if (!thread_->running()) {
            // 0x18320c: the verdict "ナゾ判定" starts at -1, then the play
            // loop for the puzzle's type (3100 here).
            game_.setVariable(names::verdict, -1);
            state_ = State::Play;
            // The sixth adventure's type numbers are its own: some of its
            // loops reuse a fifth-adventure class, some have their own
            // (a6_puzzle.hpp).
            if (game_.edition == Edition::Fifth) {
                game_.special = makeSpecialPuzzle(game_, record_->type);
            } else if (adventure6::hasOwnClass(record_->type)) {
                game_.special = makeSixthLoop(
                    game_, makeSixthPuzzle(game_, adventure6::playLoop(record_->type)));
            } else {
                game_.special = makeSixthLoop(
                    game_,
                    makeSpecialPuzzle(game_, adventure6::fifthTypeWithSixthRules(record_->type)));
            }
            thread_->start(*game_.newestScript(), "Main");
            thread_->run();
        }
        break;
    case State::Play:
        // 0x37a030: the loop's own update before the frame's scripts.
        if (game_.special) {
            game_.special->update();
        }
        thread_->run();
        update_->run();
        if (!thread_->running()) {
            state_ = State::Ended;
        }
        break;
    case State::Ended:
        break;
    }
    return state_ != State::Ended;
}

} // namespace picarat::adventure5
