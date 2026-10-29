// The sixth adventure's pad hooks (see a6_pad_hooks.hpp).
#include "puzzles/a6_pad_hooks.hpp"
#include <map>

namespace picarat::adventure5 {

std::vector<PadHook>& padHooks(const Game& game) {
    static std::map<const Game*, std::vector<PadHook>> hooks;
    return hooks[&game];
}

void runPadHooks(Game& game) {
    const std::uint32_t down = game.input.buttons & ~game.previousInput.buttons;
    if (!down) {
        return;
    }
    xq::Thread* t = game.loopThreads[0];
    if (!t) {
        return;
    }
    // A copy: a handler may register or drop hooks.
    const std::vector<PadHook> hooks = padHooks(game);
    for (const PadHook& h : hooks) {
        if (h.mask & down) {
            t->callback(h.block, h.arguments);
        }
    }
}

} // namespace picarat::adventure5
