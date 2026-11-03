#include "game/enemy/Spitter.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"
#include "game/LevelDocument.hpp"

#include <algorithm>
#include <cmath>

namespace beacon::game::enemy {

using math::Vec3;

namespace {
constexpr float kIdleBlend = 0.1f;   // the attack's return to idle (0x6c5ce0: 0.1)

}

SpitterTuning loadSpitterTuning(const std::filesystem::path& root) {
    SpitterTuning t;
    data::Bytes bytes;
    if (!data::loadFile(root / "data" / "multi_platformer" / "property" / "inflate_enemy.xml", bytes)) {
        return t;
    }
    auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!xml) {
        return t;
    }
    auto read = [](const data::XmlNode* n, const char* name, float& value) {
        if (n) {
            if (auto text = n->childText(name)) {
                data::parseFloatList(*text, &value, 1);
            }
        }
    };
    auto readVec = [](const data::XmlNode* n, const char* name, Vec3& value) {
        if (n) {
            if (auto text = n->childText(name)) {
                float v[3];
                if (data::parseFloatList(*text, v, 3)) {
                    value = {v[0], v[1], v[2]};
                }
            }
        }
    };
    const data::XmlNode* def = xml->child("Default");
    read(def, "CollisionRadiusAtScaleOne", t.collisionRadius);
    read(def, "CollisionHalfLengthAtScaleOne", t.collisionHalfLength);
    if (auto text = xml->childText("CanBeDamagedByHeroHover")) {
        t.hoverDamage = *text == "true";
    }
    read(&*xml, "InvincibilityDurationOnPunch", t.invincibility);
    const data::XmlNode* inf = xml->child("Inflation");
    read(inf, "Smoothing", t.smoothing);
    read(inf, "RetractDuration", t.retractDuration);
    read(inf, "RetractXzCoeff", t.retractXz);
    read(inf, "RetractYCoeff", t.retractY);
    read(inf, "GrowDuration", t.growDuration);
    read(inf, "ExcessXzCoeff", t.excessXz);
    read(inf, "ExcessYCoeff", t.excessY);
    read(inf, "FinalScaleExcessXzCoeff", t.finalExcessXz);
    read(inf, "FinalScaleExcessYCoeff", t.finalExcessY);
    read(inf, "ShrinkDuration", t.shrinkDuration);
    read(xml->child("Explosion"), "ShakeDuration", t.shakeDuration);
    const data::XmlNode* atk = xml->child("Attack");
    read(atk, "ShotDelay", t.shotDelay);
    readVec(atk, "ShotSourceOffsetFromFace", t.shotOffset);
    read(atk, "LookAtSmoothing", t.lookAtSmoothing);
    read(atk, "AddAngle", t.addAngle);
    read(atk, "LookAtLimitDownAngle", t.lookDown);
    read(atk, "LookAtLimitUpAngle", t.lookUp);
    readVec(atk, "HmdTargetOffset", t.hmdTargetOffset);
    return t;
}

SpitterStoneTuning loadSpitterStoneTuning(const std::filesystem::path& root) {
    SpitterStoneTuning t;
    data::Bytes bytes;
    if (!data::loadFile(root / "data" / "multi_platformer" / "property" / "inflate_enemy.xml", bytes)) {
        return t;
    }
    auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!xml) {
        return t;
    }
    auto read = [](const data::XmlNode* n, const char* name, float& value) {
        if (n) {
            if (auto text = n->childText(name)) {
                data::parseFloatList(*text, &value, 1);
            }
        }
    };
    auto readFlag = [](const data::XmlNode* n, const char* name, bool& value) {
        if (n) {
            if (auto text = n->childText(name)) {
                value = *text == "true";
            }
        }
    };
    auto readVec = [](const data::XmlNode* n, const char* name, Vec3& value) {
        if (n) {
            if (auto text = n->childText(name)) {
                float v[3];
                if (data::parseFloatList(*text, v, 3)) {
                    value = {v[0], v[1], v[2]};
                }
            }
        }
    };
    const data::XmlNode* p = xml->child("Projectile");
    read(p, "MassAtScaleOne", t.massAtScaleOne);
    read(p, "DefaultLifeTime", t.lifeTime);
    readFlag(p, "ChangeLifeTimeOnHit", t.changeLifeTimeOnHit);
    read(p, "AfterHitLifeTime", t.afterHitLifeTime);
    read(p, "Gravity", t.gravity);
    read(p, "AngularSpeed", t.angularSpeed);
    read(p, "BounceAngleMultiplier", t.bounceAngleMultiplier);
    read(p, "BounceVelMultiplier", t.bounceVelMultiplier);
    readFlag(p, "ForceBounceUp", t.forceBounceUp);
    const data::XmlNode* g = xml->child("Gum");
    read(g, "DefaultCollisionRadius", t.gumRadius);
    read(g, "LifeTimeOnceAttached", t.attachedLife);
    read(g, "LifeTimeOnceDetached", t.detachedLife);
    const data::XmlNode* h = g ? g->child("AttachOnHmd") : nullptr;
    readVec(h, "HmdLocalPosition", t.hmdPosition);
    readVec(h, "HmdLocalRotation", t.hmdRotation);
    readVec(h, "HmdLocalScale", t.hmdScale);
    return t;
}

// 0x6b9c60: the velocity that meets the target after |target - start| / speed seconds under the
// stone's gravity (+0x70): (d.x / T, d.y / T - 0.5 g T, d.z / T); a random tumble axis.
SpitterStone launchStone(const SpitterShot& shot, const SpitterStoneTuning& t, std::uint32_t seed) {
    SpitterStone s;
    s.position = shot.start;
    s.scale = shot.scale;
    s.life = t.lifeTime;
    const Vec3 d = shot.target - shot.start;
    const float time = std::max(math::length(d), 1e-4f) / std::max(shot.speed, 1e-4f);
    s.velocity = {d.x / time, d.y / time - 0.5f * t.gravity * time, d.z / time};
    std::uint32_t r = seed * 2654435761u + 12345u;
    auto unit = [&r] {
        r = r * 1664525u + 1013904223u;
        return float(r >> 8) / float(1u << 24) * 2 - 1;
    };
    s.spinAxis = math::normalize(Vec3{unit(), unit(), unit()}, {0, 1, 0});
    return s;
}

SpitterSetup spitterSetup(const LevelObject& o, const math::Mat4& world) {
    SpitterSetup s;
    s.world = world;
    s.scale = std::sqrt(world[0] * world[0] + world[1] * world[1] + world[2] * world[2]);
    s.hitpoints = int(o.number("Hitpoints").value_or(float(s.hitpoints)));
    s.startScale = o.number("StartScale").value_or(0);
    s.endScale = o.number("EndScale").value_or(s.endScale);
    s.startChargeTime = o.number("StartChargeTime").value_or(s.startChargeTime);
    s.endChargeTime = o.number("EndStartTime").value_or(s.endChargeTime);
    s.startStoneScale = o.number("StartStoneScale").value_or(s.startStoneScale);
    s.endStoneScale = o.number("EndStoneScale").value_or(s.endStoneScale);
    s.startStoneSpeed = o.number("StartStoneSpeed").value_or(s.startStoneSpeed);
    s.endStoneSpeed = o.number("EndStoneSpeed").value_or(s.endStoneSpeed);
    s.startCooldown = o.number("StartCooldown").value_or(s.startCooldown);
    s.endCooldown = o.number("EndCooldown").value_or(s.endCooldown);
    s.attackLimit = int(o.number("AttackLimit").value_or(-1));
    s.reward = o.text("Rewards");
    return s;
}

Spitter::Spitter(std::size_t placement, const SpitterSetup& setup, const SpitterTuning& tuning, SpitterClipLengths clips)
    : placement_(placement), setup_(setup), t_(tuning), hp_(setup.hitpoints), maxHp_(setup.hitpoints),
      clipLengths_(std::move(clips)) {
    if (setup_.startScale <= 0) {
        setup_.startScale = setup_.scale;   // **[hypothesis]** without the attribute: the placed size
    }
    // Setup 0x6bff00: every scale starts at the node's.
    from_ = base_ = current_ = shown_ = Vec3{setup_.scale, setup_.scale, setup_.scale};
    play("idle", 0, true);
}

// 0x6c9dd0: 0 at full hit points, 1 at the last one.
float Spitter::progress() const {
    if (hp_ < 0 || maxHp_ < 2) {
        return 1;
    }
    return 1 - float(hp_ - 1) / float(maxHp_ - 1);
}

void Spitter::play(const char* name, float blend, bool loop) {
    clip_.name = name;
    clip_.blend = blend;
    clip_.duration = 0;   // the clip's own length
    ++clip_.serial;
    clipTime_ = 0;
    clipLoop_ = loop;
}

bool Spitter::clipDone() const {
    const auto it = clipLengths_.find(clip_.name);
    return !clipLoop_ && (it == clipLengths_.end() || it->second <= clipTime_);
}

math::Mat4 Spitter::pose() const {
    math::Mat4 m = setup_.world;
    const float s = setup_.scale > 0 ? setup_.scale : 1;
    for (int c = 0; c < 3; ++c) {   // the placement's rotation with the drawn scale per axis
        const float k = (c == 0 ? shown_.x : c == 1 ? shown_.y : shown_.z) / s;
        m[c * 4 + 0] *= k;
        m[c * 4 + 1] *= k;
        m[c * 4 + 2] *= k;
    }
    return m;
}

// 0x6c4ed0: entering a state.
void Spitter::setState(int s, bool force) {
    if (state_ == s && !force) {
        return;
    }
    state_ = s;
    switch (s) {
    case 3: {   // squash, then grow to the size for the hit points left
        from_ = base_;
        const float p = progress();
        const float b = (1 - p) * setup_.startScale + p * setup_.endScale;
        base_ = {b, b, b};
        timer_ = t_.retractDuration;
        break;
    }
    case 4:
        timer_ = t_.growDuration;
        break;
    case 5:
        timer_ = t_.shrinkDuration;
        break;
    case 6: {   // swollen for the explosion, shaking
        from_ = base_;
        const float e = setup_.endScale;
        current_ = base_ = {e * t_.finalExcessXz, e * t_.finalExcessY, e * t_.finalExcessXz};
        timer_ = t_.shakeDuration;
        play("shaking", 0, true);
        break;
    }
    case 9:
        play("explode", 0, false);
        break;
    default:
        break;
    }
}

// 0x6c6840: entering an attack state.
void Spitter::setAttack(int s) {
    if (state_ == 6 || state_ == 9 || attack_ == s) {
        return;
    }
    attack_ = s;
    const float p = progress();
    if (s == 3) {
        ++attacks_;
        play("attack", -1, false);
        attackTimer_ = t_.shotDelay;   // 0x6c8620
    } else if (s == 2) {
        play("charging", -1, true);
        attackTimer_ = (1 - p) * setup_.startChargeTime + p * setup_.endChargeTime;
    }
}

// 0x6c9e20: a target, and attacks left.
bool Spitter::canAttack(const SpitterWorld& w) const {
    return w.targetOn && (setup_.attackLimit < 0 || attacks_ < setup_.attackLimit);
}

void Spitter::shoot(const SpitterWorld& w) {
    const float p = progress();
    SpitterShot s;
    s.scale = (1 - p) * setup_.startStoneScale + p * setup_.endStoneScale;
    s.speed = (1 - p) * setup_.startStoneSpeed + p * setup_.endStoneSpeed;
    s.target = target_;
    // The source (0x6c8aa0): ShotSourceOffsetFromFace in the face joint's frame (with the model's
    // scale).
    const math::Mat4& f = w.face;
    const Vec3& o = t_.shotOffset;
    s.start = {f[0] * o.x + f[4] * o.y + f[8] * o.z + f[12], f[1] * o.x + f[5] * o.y + f[9] * o.z + f[13],
               f[2] * o.x + f[6] * o.y + f[10] * o.z + f[14]};
    shots_.push_back(s);
}

// 0x6c5ce0: the attack, beside the inflation states.
void Spitter::stepAttack(const SpitterWorld& w) {
    if (state_ == 6 || state_ == 9) {
        return;
    }
    bool expired = false;
    if (0 < attackTimer_) {
        attackTimer_ -= w.dt;
        expired = attackTimer_ <= 0;
    }
    target_ = w.head + t_.hmdTargetOffset;
    switch (attack_) {
    case 0:
        if (1 < state_ && canAttack(w)) {
            setAttack(1);
        } else if (clipDone()) {
            play("idle", kIdleBlend, true);
        }
        break;
    case 1:
        if (!w.targetOn) {
            setAttack(0);
        } else if (0 < attackTimer_) {
            if (clipDone()) {
                play("idle", kIdleBlend, true);
            }
        } else {
            setAttack(2);
        }
        break;
    case 2:
        if (expired) {
            setAttack(3);
        }
        break;
    case 3:
        if (expired) {
            shoot(w);
        }
        if (clipDone()) {
            const float p = progress();
            attackTimer_ = (1 - p) * setup_.startCooldown + p * setup_.endCooldown;
            play("idle", kIdleBlend, true);
            attack_ = -1;   // force the entry
            setAttack(w.targetOn ? 1 : 0);
        }
        break;
    default:
        break;
    }
}

void Spitter::update(const SpitterWorld& w) {
    shots_.clear();
    if (removed_) {
        return;
    }
    if (0 < invincible_) {
        invincible_ -= w.dt;
    }
    clipTime_ += w.dt;
    // 0x6c3350: the state's timer.
    bool expired = false;
    if (0 < timer_) {
        timer_ -= w.dt;
        expired = timer_ <= 0;
    }
    const float left = std::max(timer_, 0.0f);
    auto mix = [](const Vec3& a, const Vec3& b, float k) { return a * k + b * (1 - k); };
    switch (state_) {
    case 3: {   // squash: from the old size to it x (RetractXz, RetractY)
        const Vec3 squashed{from_.x * t_.retractXz, from_.y * t_.retractY, from_.z * t_.retractXz};
        current_ = mix(from_, squashed, std::clamp(left / t_.retractDuration, 0.0f, 1.0f));
        if (expired) {
            setState(0 < hp_ ? 4 : 6);
        }
        break;
    }
    case 4: {   // grow past the new size: x (ExcessXz, ExcessY)
        const Vec3 squashed{from_.x * t_.retractXz, from_.y * t_.retractY, from_.z * t_.retractXz};
        const Vec3 excess{base_.x * t_.excessXz, base_.y * t_.excessY, base_.z * t_.excessXz};
        current_ = mix(squashed, excess, std::clamp(left / t_.growDuration, 0.0f, 1.0f));
        if (expired) {
            setState(5);
        }
        break;
    }
    case 5: {   // settle to the new size
        const Vec3 excess{base_.x * t_.excessXz, base_.y * t_.excessY, base_.z * t_.excessXz};
        current_ = mix(excess, base_, std::clamp(left / t_.shrinkDuration, 0.0f, 1.0f));
        if (expired) {
            setState(2);
        }
        break;
    }
    case 6:
        if (expired) {
            setState(9);
        }
        break;
    case 9:
        if (clipDone()) {
            removed_ = true;   // +0x3802: the object goes
        }
        break;
    default:
        break;
    }
    stepAttack(w);
    // 0x6c23a0: the drawn size follows at Smoothing per 1/60 s.
    const float k = std::pow(t_.smoothing, w.dt * 60);
    shown_ = shown_ * k + current_ * (1 - k);
}

// 0x6c9120.
bool Spitter::hit(int type, int damage) {
    if (state_ == 9 || removed_ || 0 < invincible_) {
        return false;
    }
    int lost = 1;
    if (type == 4) {
        if (!t_.hoverDamage) {
            return false;
        }
    } else if (type == 5) {
        lost = damage;
    } else if (type != 1 && type != 2 && type != 3) {
        return false;
    }
    invincible_ = t_.invincibility;
    hp_ = std::max(0, hp_ - lost);
    setState(hp_ < 1 ? 9 : 3, hp_ >= 1);
    return true;
}

}   // namespace beacon::game::enemy
