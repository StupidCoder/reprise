// =============================================================================================
// StageBirds.hpp - the small birds of a course (StageObjects.hpp, game/Bird.hpp)
// =============================================================================================
//
// What the player sees: birds peck on the ground and turn about; when the protagonist comes
// close they fly away.
//
// Bird (creator 0x453e34, vtable 0x4ce6c4, init 0x38e378): the opening's recovered bird
// (game/Bird.hpp: GroundWait / Turn / Flee, the shared xorshift128 at 0x4f8064, the player's Head
// sensor against the bird's Body r400 starting the flee).
// Birds with a SwitchStart link (here: on the lever B switch and on the timer switch) take part
// only once that switch is on (they are made to appear by the switch, 0x2bfc74 family; the
// revival fly-in N3 0x38df30 they would start with is not ported: they appear on the ground).
// IsPerch birds (N4 0x38e678) are not ported.
// =============================================================================================
#pragma once

#include "game/Bird.hpp"
#include "game/StageObjects.hpp"

#include <algorithm>
#include <cmath>

namespace captain_toad::RecoveredStageBirds {

using namespace captain_toad::RecoveredStage;

// The birds share the game's global generator (as RecoveredBird documents); seeded once.
inline captain_toad::RecoveredBird::XorShift128& birdRng() {
    static captain_toad::RecoveredBird::XorShift128 rng;
    return rng;
}

struct Bird : Behaviour {
    int lastPlays = -1;
    captain_toad::RecoveredBird::Bird bird;
    bool waiting = false;   // waits for its SwitchStart switch

    explicit Bird(Object& o) {
        const V3 p = o.translation();
        bird.pos[0] = p.x;
        bird.pos[1] = p.y;
        bird.pos[2] = p.z;
        bird.yaw = -std::atan2(o.placement.m[2], o.placement.m[0]) * float(180.0 / M_PI);
        waiting = o.link("SwitchStart") != nullptr;
        o.visible = !waiting;
    }
    void control(World& w, Object& o) override {
        if (waiting) {
            if (!w.switchOn(o, "SwitchStart")) {
                return;
            }
            waiting = false;
            o.visible = true;
        }
        if (bird.dead) {
            o.alive = false;
            return;
        }
        const float head[3] = {w.player.pos.x + w.player.up.x * 125.0f, w.player.pos.y + w.player.up.y * 125.0f,
                               w.player.pos.z + w.player.up.z * 125.0f};
        bird.headContact(head);
        bird.tick(birdRng());
        // The bird keeps its own clip clock (game/Bird.hpp) in 30 Hz ticks; the object's clip frame
        // counts 60 Hz frames (two per baked sample). A new clip (or a restart) is an action for
        // the effect and sound keepers (e.g. FlyWait -> BirdFly).
        if (bird.plays != lastPlays) {
            o.action = bird.clip;
            o.actionAge = 0;
            o.actionStarts++;
            lastPlays = bird.plays;
        }
        o.anim.name = bird.clip;
        o.anim.frame = 2.0f * bird.clipFrame;
        o.anim.age = std::max(o.anim.age, 0);
        // The bird's yaw field holds the negated Y rotation (game/Bird.hpp).
        const float r = -bird.yaw * float(M_PI / 180.0), c = std::cos(r), s = std::sin(r);
        const float m[12] = {c, 0, s, bird.pos[0], 0, 1, 0, bird.pos[1], -s, 0, c, bird.pos[2]};
        for (int k = 0; k < 12; k++) {
            o.world.m[k] = m[k];
        }
    }
};

inline const Register<Bird> kBird{"Bird"};

}   // namespace captain_toad::RecoveredStageBirds
