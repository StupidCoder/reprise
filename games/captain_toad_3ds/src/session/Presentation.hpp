// =============================================================================================
// Presentation.hpp - drawing between the game's ticks (enhancement)
// =============================================================================================
//
// Not in the original: the game logic runs at 30 Hz (as recovered; its rules are tuned per tick),
// while displays refresh at 60, 120 Hz or more. After every tick the session records what the
// renderer reads from the game (the camera, the protagonist's bone poses and placement, each
// animated object's clip frame and placement, each placed object's world matrix); a frame drawn
// between two ticks shows the blend of the last two records by the time since the last tick.
// The picture so runs one tick (33 ms) behind the logic. Positions, scales and frames blend
// linearly, rotations along the shortest arc. Not blended: a clip change, a frame that went
// backwards (a loop) or jumped, a placement that moved too far in one tick (a respawn or warp),
// a camera cut. The logic never sees a blended pose: the session restores the tick's own pose
// after drawing (some rules read joints of the last pose).
// Blended elsewhere by the same rule: particles (OpeningEffects::blend, by particle), the grass
// sway (GrassWindState), layouts between their 30 Hz steps (captain_toad::ui::Instance::applyAt) and baked clip
// samples (CharacterAnimation::smoothSamples).
// =============================================================================================
#pragma once

#include "game/Camera.hpp"
#include "render/CharacterAnimation.hpp"

#include <string>
#include <vector>

namespace captain_toad::session {

// The blend of two affine transforms at t (0: a, 1: b): translation and scale linear, rotation
// along the shortest arc (normalised quaternion interpolation; the steps are small).
Affine blendAffine(const Affine& a, const Affine& b, float t);
captain_toad::RecoveredVec::M34 blendM34(const captain_toad::RecoveredVec::M34& a,
                                         const captain_toad::RecoveredVec::M34& b, float t);
// The same on row-major 3x4 arrays.
void blendRows34(const float* a, const float* b, float t, float* out);
// Camera views: eye, target, up and field of view.
captain_toad::RecoveredCamera::Pose blendPose(const captain_toad::RecoveredCamera::Pose& a,
                                              const captain_toad::RecoveredCamera::Pose& b, float t);

// What the renderer reads after one tick.
struct PresentTick {
    bool valid = false;
    captain_toad::RecoveredCamera::Pose view;
    bool player = false;
    std::vector<Affine> playerLocal;
    Affine playerPlacement;
    struct Actor {
        std::string clip;
        float frame = -1;   // the behaviour's frame (-1: the actor runs on the clock, not blended)
        Affine placement;
        bool hidden = false;
    };
    std::vector<Actor> actors;
    std::vector<captain_toad::RecoveredVec::M34> objectWorld;
    std::vector<std::vector<captain_toad::RecoveredVec::M34>> objectDraws;   // objects drawn several times
    // The goal sequence's layout clips (course clear, title, wipe): name and frame.
    std::pair<std::string, float> layouts[3];
};

// Limits of a blendable step (world units per tick, frames per tick).
constexpr float kPresentMaxMove = 400.0f;
constexpr float kPresentMaxFrames = 12.0f;
constexpr float kPresentCameraCut = 800.0f;

}   // namespace captain_toad::session
