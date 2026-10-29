#pragma once
#include "gg_engine/objects/scripts.hpp"
#include "triple_trouble_gg/data/assets.hpp"
#include "triple_trouble_gg/game/routines.hpp"
#include "triple_trouble_gg/game/types.hpp"

// Triple Trouble's object scripts (gg_engine/objects/scripts.hpp): read by
// 5A86/5AAF, the commands at 5C19; the state tables at 5B2D (bank 1), the
// types' banks at 502C.
namespace triple_trouble_gg {

using ScriptStep = gg_engine::ScriptStep<Routine>;

class Scripts : public gg_engine::Scripts<Routine> {
public:
    explicit Scripts(const Assets& rom);
};

} // namespace triple_trouble_gg
