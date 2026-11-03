// The spitter (inflate_enemy, model mup_ch_enemy_spitter; MupInflateEnemy, constructor 0x6bf440 on
// the enemy base 0x606e90, vtable 0x16b5048). Findings: .analysis/docs/enemies.md "Spitter".
//
// A blob that stays where it is placed, turns to the VR player while its chase trigger is on and
// spits stones at the player's head: charging (StartChargeTime .. EndStartTime), the attack clip,
// the stone ShotDelay into it, then a cooldown (StartCooldown .. EndCooldown). Each hit (punch,
// kick, the hover laser) costs a hit point and inflates it: it squashes (RetractDuration), grows
// past its next size (GrowDuration) and settles (ShrinkDuration); sizes, stones and times go from
// the Start to the End values as its hit points run out (progress 1 - (hp - 1) / (max - 1)). The
// last hit makes it explode.
#pragma once

#include "game/LostBot.hpp"   // LostBotClip: a clip request for the view
#include "math/Transform.hpp"
#include "math/Vector.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace beacon::game {
struct LevelObject;
}

namespace beacon::game::enemy {

// property/inflate_enemy.xml (singleton 0x6bdac0; value offsets in the comments).
struct SpitterTuning {
    float collisionRadius = 0.51f, collisionHalfLength = 0.37f;   // at scale one (+0x290 / +0x340)
    bool hoverDamage = true;                                       // CanBeDamagedByHeroHover (+0x550)
    float invincibility = 0.05f;                                   // InvincibilityDurationOnPunch (+0x5f0)
    // <Inflation>
    float smoothing = 0.87f;                                       // (+0x1880)
    float retractDuration = 0.2f, retractXz = 0.5f, retractY = 1.2f;   // (+0x1930 / +0x19e0 / +0x1a90)
    float growDuration = 0.1f, excessXz = 1.5f, excessY = 0.5f;        // (+0x1b40 / +0x1bf0 / +0x1ca0)
    float finalExcessXz = 1.2f, finalExcessY = 1.1f;                   // (+0x1d50 / +0x1e00)
    float shrinkDuration = 0.2f;                                       // (+0x1eb0)
    float shakeDuration = 0.5f;                                        // <Explosion> (+0x1f60)
    // <Attack>
    float shotDelay = 0.53f;                                       // (+0x2010)
    math::Vec3 shotOffset{0, -0.021f, 0.68f};                      // ShotSourceOffsetFromFace (+0x20c0)
    float lookAtSmoothing = 0.95f;                                 // (+0x2380)
    float addAngle = 15, lookDown = 15, lookUp = 90;               // degrees (+0x2430 / +0x2640 / +0x26f0)
    math::Vec3 hmdTargetOffset{};                                  // (+0x27a0)
};
SpitterTuning loadSpitterTuning(const std::filesystem::path& root);

// A stone the spitter spits (0x6c6170): from `start` towards `target` at `speed`, of `scale`.
struct SpitterShot {
    math::Vec3 start, target;
    float speed = 0, scale = 0;
};

// <Projectile> and <Gum> of property/inflate_enemy.xml (the stone's singleton 0x6cb0e0).
struct SpitterStoneTuning {
    float massAtScaleOne = 2, lifeTime = 5, afterHitLifeTime = 2;
    bool changeLifeTimeOnHit = true;
    float gravity = -3;              // (+0x3f0) the flight's own gravity
    float angularSpeed = 5;          // the tumble, radians per second
    float bounceAngleMultiplier = 1.2f, bounceVelMultiplier = 3;
    bool forceBounceUp = true;
    float radius = 0.2f;             // the shape of mup_ch_enemy_spitted_stone_col at scale one
    // <Gum><AttachOnHmd>
    math::Vec3 hmdPosition{0, 0, -0.3f}, hmdRotation{-90, 180, 0}, hmdScale{4, 4, 4};
    float attachedLife = 2, detachedLife = 1;
    float gumRadius = 0.05f;         // DefaultCollisionRadius
};
SpitterStoneTuning loadSpitterStoneTuning(const std::filesystem::path& root);

// A spat stone (task object, vtable 0x16b5678: start 0x6b9c60, update 0x6bbc10). It flies on the
// ballistic arc that meets the target after |target - start| / speed seconds under its own
// gravity, tumbling; meeting the VR player's head it becomes the gum on the player's view; meeting
// anything else it bounces and falls as a physics body until its life (AfterHitLifeTime) ends.
struct SpitterStone {
    math::Vec3 position, velocity, spinAxis;
    float scale = 0.12f, life = 5, spin = 0;
    bool flying = true, done = false;
    int body = -1;   // the physics body after the bounce
};
SpitterStone launchStone(const SpitterShot& shot, const SpitterStoneTuning& t, std::uint32_t seed);

// The gum (0x6bbc10 case 2 -> object vtable 0x16b5548, model ef_vr_screen_spitter_gum): a stone
// that meets the VR player's head sticks on the view (AttachOnHmd: HmdLocalPosition / Rotation /
// Scale) for LifeTimeOnceAttached, playing "attach", then slides off ("slide") for
// LifeTimeOnceDetached. **[guess]** detaching by time only (head shaking, 0x16b5548, not followed).
struct SpitterGum {
    bool active = false, detached = false;
    float time = 0;
    std::uint32_t serial = 0;   // a new gum restarts the view's clip
};

// The placement's attributes (block at +0x2570, binder 0x6ca290).
struct SpitterSetup {
    math::Mat4 world;
    float scale = 1;                     // the placement's scale (the starting size)
    int hitpoints = 3;
    float startScale = 0, endScale = 0.8f;   // startScale 0: the placement's scale
    float startChargeTime = 1, endChargeTime = 2;   // StartChargeTime / EndStartTime
    float startStoneScale = 0.12f, endStoneScale = 0.35f;
    float startStoneSpeed = 3.5f, endStoneSpeed = 2;
    float startCooldown = 2, endCooldown = 3;
    int attackLimit = -1;                // < 0: no limit
    std::string reward;
};
SpitterSetup spitterSetup(const LevelObject& o, const math::Mat4& world);


struct SpitterWorld {
    float dt = 1.0f / 60;
    bool targetOn = false;               // the chase trigger is on (the VR player inside)
    math::Vec3 head;                     // the VR player's head
    math::Mat4 face = math::identity();  // face_root_joint's world matrix (the shot source's frame)
};

// The definition's clip lengths (seconds at their playback speed), by name.
using SpitterClipLengths = std::map<std::string, float>;

class Spitter {
public:
    Spitter(std::size_t placement, const SpitterSetup& setup, const SpitterTuning& tuning, SpitterClipLengths clips);

    void update(const SpitterWorld& world);
    // A hit (0x6c9120): type 1/2 punch or kick, 4 the hover laser, 5 damage by amount; true when it
    // took the hit.
    bool hit(int type, int damage = 1);

    std::size_t placement() const { return placement_; }
    int state() const { return state_; }
    int attackState() const { return attack_; }
    int hitpoints() const { return hp_; }
    bool removed() const { return removed_; }
    bool exploded() const { return state_ == 9; }
    const math::Vec3& scale() const { return shown_; }   // the drawn scale (+0x3810)
    math::Mat4 pose() const;                              // placement with the drawn scale
    const LostBotClip& clip() const { return clip_; }
    // Shots fired this update.
    std::vector<SpitterShot>& shots() { return shots_; }
    const math::Vec3& target() const { return target_; }
    float progress() const;   // 0x6c9dd0

private:
    void setState(int s, bool force = false);    // 0x6c4ed0
    void setAttack(int s);                        // 0x6c6840
    void play(const char* name, float blend, bool loop);
    void stepAttack(const SpitterWorld& w);       // 0x6c5ce0
    bool canAttack(const SpitterWorld& w) const;  // 0x6c9e20
    bool clipDone() const;                        // the base clip played to its end (0x60af70)
    void shoot(const SpitterWorld& w);            // 0x6c6170

    std::size_t placement_;
    SpitterSetup setup_;
    SpitterTuning t_;
    int state_ = 2, attack_ = 0;
    float timer_ = 0, attackTimer_ = -1.0f / 60, invincible_ = 0;
    int hp_, maxHp_, attacks_ = 0;
    math::Vec3 from_, base_, current_, shown_;   // +0x3860, +0x3870, +0x3880, +0x3810
    math::Vec3 target_;
    bool removed_ = false;
    LostBotClip clip_;
    SpitterClipLengths clipLengths_;
    float clipTime_ = 0;
    bool clipLoop_ = true;
    std::vector<SpitterShot> shots_;
};

}   // namespace beacon::game::enemy
