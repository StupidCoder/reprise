// Respawn: the checkpoints (respawn_marker, class vtable 0x16c0238), the VR player's warp back to
// the last one (0xa63d00 / 0xa60560) and the timing the hero's second state machine follows
// (Vanish 0x51, DefaultRespawn 0x5c; the hero's part is Hero::respawnAt). Findings:
// .analysis/docs/hero.md "Respawn".
#pragma once

#include "game/LevelDocument.hpp"
#include "game/LevelRuntime.hpp"
#include "math/Vector.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace beacon::game {

struct RespawnTuning {
    // <Hero><Respawn> (hero parameter object +0x49a0, +0x4a40, +0x4f10, +0x5540).
    bool markerVisible = true;              // RespawnMarkerVisible: markers activate by position
    float respawnEffectTime = 0.8f;         // before the somersault
    float flySpawnSomerSaultHeight = 2.0f;  // times the hero's scale above the respawn point
    float invincibleTimeAfterRespawn = 1.0f;
    // <VrPlayer><Warp> (VR player parameters +0x7f50 .. +0x84d0).
    float warpWaitTimeForRespawn = 0.3f, warpFadeOutTime = 0.3f, warpFadeBlackTime = 0.2f, warpFadeInTime = 0.3f;
    float respawnAreaAngle = 60, respawnAreaRadius = 0.3f, respawnAreaZ = 0.5f;
};
RespawnTuning loadRespawnTuning(const std::filesystem::path& root);

// The respawn_marker placements (setup 0x7c9530, update 0x7c9650). State +0x2150: 0 waiting for
// the SpawnTrigger, 1 SpawnDelayTime, 2 "spawn", 3 idle (not yet activated), 4 "activate",
// 5 "activate_idle", 6 the hero respawned here ("herorespawn" for 3 s), 7 "after_herorespawn",
// 8 "remove" (an older marker once a newer one is activated), then gone.
class RespawnMarkers {
public:
    struct ClipLengths {
        float spawn = 0.583f, activate = 1.317f, afterHeroRespawn = 1.117f, remove = 0.5f;
    };
    struct Marker {
        std::size_t placement = 0;
        math::Vec3 position;
        LevelLink spawnTrigger, activateTrigger;
        float delayTime = 0, spawnDelayTime = 0;
        int state = 0;
        float timer = 0;           // +0x2148 (state 1), +0x214c (state 6)
        float delayTimer = 0;      // +0x2144
        bool activated = false;    // +0x2140 (on for the links of the next marker: +0x2141)
        bool pending = false;      // +0x2142: the ActivateTrigger fired, DelayTime running
        bool removed = false;
        const char* clip = "idle";
        float clipTime = 0;
        std::uint32_t clipSerial = 0;
    };

    void load(const Level& level, const ClipLengths& clips, bool markerVisible);
    // One update; returns the marker activated in it, or -1. `vrBusy`: the VR player warps
    // (+0x498, 0xa641d0).
    int update(float dt, LevelRuntime& runtime, bool heroAlive, const math::Vec3& heroFeet, bool vrBusy);
    // The warp's black phase ends at this marker (0x7ca390): "herorespawn".
    void heroRespawned(int marker);
    const std::vector<Marker>& markers() const { return markers_; }
    bool shown(const Marker& m) const { return m.state >= 2 && !m.removed; }

private:
    void play(Marker& m, const char* clip);
    std::vector<Marker> markers_;
    ClipLengths clips_;
    bool markerVisible_ = true;
};

// The VR player's warp (+0x498: 1 wait, 2 fade out, 3 black, 4 fade in; times from the start
// call 0xa63d00). The rig moves to the target when the fade-out ends; the marker hears of the
// respawn when the black ends.
struct VrWarp {
    int state = 0;
    float timer = 0;
    float wait = 0, fadeOut = 0, black = 0, fadeIn = 0;
    math::Vec3 target;
    bool skip = false;   // the update after the rig moved does not count (run roof_g6, 3734)
    bool busy() const { return state != 0; }
    float fade() const;   // 0 clear .. 1 black
    void start(const RespawnTuning& t, const math::Vec3& target);
    struct Events {
        bool moveRig = false, respawned = false, done = false;
    };
    Events update(float dt);   // 0xa60560
};

// 0x69d700: whether the VR player has to warp before the hero respawns at `point` - not when the
// point lies in the respawn area in front of the player: at most RespawnAreaZ behind it, at least
// RespawnAreaRadius away and at least RespawnAreaAngle off the +z axis (|atan2(dx, dz)|).
bool respawnNeedsWarp(const RespawnTuning& t, const math::Vec3& point, const math::Vec3& vrPosition);

}   // namespace game
