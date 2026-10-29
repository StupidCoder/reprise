#pragma once
#include "gg_engine/objects/scripts.hpp"
#include "sonic2_gg/data/assets.hpp"
#include "sonic2_gg/game/routines.hpp"
#include "sonic2_gg/game/types.hpp"

// Sonic 2's object scripts (gg_engine/objects/scripts.hpp): read by 5F47, the
// commands at 6539; the state tables at 5E41 (bank 1).
namespace sonic2_gg {

using ScriptStep = gg_engine::ScriptStep<Routine>;

// The bank holding an object type's scripts and handlers (5DBD).
unsigned object_bank(unsigned type);

class Scripts : public gg_engine::Scripts<Routine> {
public:
    explicit Scripts(const Assets& rom);
};

} // namespace sonic2_gg
