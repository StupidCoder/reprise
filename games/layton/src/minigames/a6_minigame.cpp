// The sixth adventure's minigame loops. See a6_minigame.hpp.
#include "minigames/a6_minigame.hpp"
#include "graphics/a56_font.hpp"
#include "minigames/a6_garden.hpp"
#include "minigames/a6_nut_roller.hpp"
#include <cstring>
#include <stdexcept>
#include <utility>

namespace picarat::adventure6 {

using adventure5::Game;

const char* minigameScript(int loopId) {
    switch (loopId) {
    case AirShipLoopId:
        return "seq/mini_airship";
    case FashionLoopId:
        return "seq/mini_fashion";
    case GardenLoopId:
        return "seq/mini_garden";
    case RisuLoopId:
        return "seq/mini_risu";
    default:
        return nullptr;
    }
}

std::unique_ptr<adventure5::SpecialPuzzle> makeMinigame(Game& game, int loopId) {
    switch (loopId) {
    case GardenLoopId:
        return std::make_unique<Garden>(game);
    case RisuLoopId:
        return std::make_unique<NutRoller>(game);
    default:
        return nullptr;
    }
}

MinigameSession::MinigameSession(Game& game, int loopId, std::vector<xq::Value> values)
    : game_(game) {
    const char* script = minigameScript(loopId);
    if (!script) {
        throw std::runtime_error("not a minigame loop");
    }
    game.special.reset();
    // The fonts and the game loop's scripts, as for a puzzle (a5_puzzle.cpp).
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
    for (const char* s : {"seq/prog_common", "seq/prog_lt", "seq/prog_saveload", "seq/prog_memo",
                          "seq/prog_event"}) {
        game.loadScript(s);
    }
    game.loadScript(script);
    game.special = makeMinigame(game, loopId);
    main_ = std::make_unique<xq::Thread>(game.machine, 16);
    second_ = std::make_unique<xq::Thread>(game.machine, 16);
    for (std::size_t i = 0; i < values.size() && i < 8; ++i) {
        main_->values[i] = values[i];
    }
    game.loopThreads[0] = main_.get();
    game.loopThreads[1] = second_.get();
    main_->start(*game.newestScript(), "Init");
}

bool MinigameSession::frame(const adventure5::Input& input) {
    game_.beginFrame(input);
    update();
    game_.endFrame();
    return state_ != State::Ended;
}

bool MinigameSession::update() {
    switch (state_) {
    case State::Init:
        main_->run();
        if (!main_->running()) {
            state_ = State::Main;
            main_->start(*game_.newestScript(), "Main");
            main_->run();
        }
        break;
    case State::Main:
        if (game_.special) {
            game_.special->update();
        }
        main_->run();
        second_->run();
        if (!main_->running()) {
            state_ = State::Ended;
        }
        break;
    case State::Ended:
        break;
    }
    return state_ != State::Ended;
}

} // namespace picarat::adventure6
