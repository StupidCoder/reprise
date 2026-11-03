#include "game/TreasureBox.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"

#include <algorithm>
#include <cmath>

namespace beacon::game {

using math::Vec3;

namespace {
constexpr float kPi = 3.14159265f;
}

TreasureBoxTuning loadTreasureBoxTuning(const std::filesystem::path& root) {
    TreasureBoxTuning t;
    data::Bytes bytes;
    if (!data::loadFile(root / "data" / "multi_platformer" / "multi_platformer.xml", bytes)) {
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
    auto readInt = [&](const data::XmlNode* n, const char* name, int& value) {
        float f = float(value);
        read(n, name, f);
        value = int(f);
    };
    read(xml->child("TreasureBox"), "UnbreakableTime", t.unbreakableTime);
    read(xml->child("TreasureBox"), "HorizontalGroupDistance", t.horizontalGroupDistance);
    read(xml->child("TreasureBox"), "VerticalGroupDistance", t.verticalGroupDistance);
    readInt(xml->child("Coin"), "CoinNumTreasureBoxSmall", t.coinsSmall);
    readInt(xml->child("Coin"), "CoinNumTresureBoxBig", t.coinsBig);
    // <Breakable> sits inside another section of the file; look it up anywhere.
    const data::XmlNode* breakable = nullptr;
    std::vector<const data::XmlNode*> stack{&*xml};
    while (!stack.empty() && !breakable) {
        const data::XmlNode* n = stack.back();
        stack.pop_back();
        if (n->name == "Breakable") {
            breakable = n;
            break;
        }
        for (const auto& c : n->children) {
            stack.push_back(&c);
        }
    }
    const data::XmlNode* p = breakable ? breakable->child("BreakableTreasure") : nullptr;
    read(p, "HorizontalForce", t.horizontalForce);
    read(p, "VerticalForce", t.verticalForce);
    read(p, "RotationForce", t.rotationForce);
    read(p, "ConeAngle", t.coneAngle);
    read(p, "VanishStartTimeMin", t.vanishStartMin);
    read(p, "VanishStartTimeMax", t.vanishStartMax);
    read(p, "VanishTime", t.vanishTime);
    read(p, "Restitution", t.restitution);
    const data::XmlNode* c = p ? p->child("CoinDropParameters") : nullptr;
    read(c, "AutoGetSpeed", t.autoGetSpeed);
    read(c, "AutoGetImplusePower", t.autoGetImpulsePower);
    read(c, "AutoGetFlyingDelay", t.autoGetFlyingDelay);
    read(c, "AutoGetSpawnDelay", t.autoGetSpawnDelay);
    read(c, "AutoGetVelocityBase", t.autoGetVelocityBase);
    read(c, "AutoGetVelocityNoise", t.autoGetVelocityNoise);
    read(c, "AutoGetAngleBase", t.autoGetAngleBase);
    read(c, "AutoGetAngleNoise", t.autoGetAngleNoise);
    return t;
}

TreasureBox::TreasureBox(std::size_t object, const math::Mat4& world, int color, bool big, const TreasureBoxTuning& t)
    : object_(object), world_(world), color_(color), big_(big), t_(t), random_(std::uint32_t(object) * 2654435761u) {
    // RandomAnimation (0x971d30): the idle clip starts at a random time of its length.
    idleStart_ = std::uniform_real_distribution<float>(0, 1)(random_);
}

bool TreasureBox::hit(int type, const Vec3& position, const Vec3& direction, const Vec3& attacker) {
    if (broken_ || 0 < unbreakable_) {
        return false;
    }
    // 0x394a10: the type's flag (TvHeroAttackable for 1-3, HoverAttackable for 4, Bazooka for 5;
    // all set for treasure boxes), then the break (0x9726b0 -> 0x395600).
    const bool allowed = (type >= 1 && type <= 3) || type == 4 || type == 5;
    if (!allowed) {
        return false;
    }
    broken_ = true;
    hitPosition_ = position;
    hitDirection_ = direction;
    dropCoins(attacker);
    return true;
}

// 0x395600 -> 0x3d5560: the coins of a treasure box (CoinNumTreasureBoxSmall / Big) start at the
// hit point, each AutoGetSpawnDelay after the previous one. Their launch (0x395600 / 0x3d5930):
// straight up turned towards the hit's horizontal direction - or, when it has none (the hover's
// straight-down hit), from the attacker to the box, or else -z - by a tuning angle (+0xdb0 of the
// parameter set), then by a random angle up to a noise around a random heading, at
// AutoGetVelocityBase (+ noise). **[fit]** both angles 15 degrees: runs roof_box1/2 (20 coins of
// hover breaks) lie within 13.5 degrees of an axis 14 degrees from up towards the box; which
// <CoinDropParameters> entries the code reads there is not mapped (AngleBase is 45, AngleNoise 15).
void TreasureBox::dropCoins(const Vec3& attacker) {
    constexpr float kLeanDegrees = 15, kSpreadDegrees = 15;   // [fit], see above
    const int n = big_ ? t_.coinsBig : t_.coinsSmall;
    std::uniform_real_distribution<float> unit(0, 1), sym(-1, 1);
    Vec3 lean{hitDirection_.x, 0, hitDirection_.z};
    if (math::length(lean) < 1.1920929e-07f) {
        lean = {world_[12] - attacker.x, 0, world_[14] - attacker.z};
    }
    lean = math::length(lean) < 1.1920929e-07f ? Vec3{0, 0, -1} : math::normalize(lean, {0, 0, -1});
    const float tilt = kLeanDegrees * kPi / 180;
    const Vec3 axis = Vec3{0, std::cos(tilt), 0} + lean * std::sin(tilt);
    // Two directions across the axis for the spread.
    const Vec3 across = math::normalize(math::cross(axis, lean), {1, 0, 0});
    const Vec3 across2 = math::cross(across, axis);
    float delay = t_.autoGetFlyingDelay;
    for (int i = 0; i < n; ++i) {
        DroppedCoin c;
        c.start = hitPosition_;
        c.position = hitPosition_;
        c.delay = delay;
        const float heading = unit(random_) * 2 * kPi;
        const float spread = sym(random_) * kSpreadDegrees * kPi / 180;
        const Vec3 side = across * std::cos(heading) + across2 * std::sin(heading);
        const float speed = t_.autoGetVelocityBase + sym(random_) * t_.autoGetVelocityNoise;
        c.launch = (axis * std::cos(spread) + side * std::sin(spread)) * speed;
        coins_.push_back(c);
        delay += t_.autoGetSpawnDelay;
    }
}

void TreasureBox::startFall(float drop, float delay) {
    fallTarget_ -= drop;
    if (fallSpeed_ < 1.1920929e-07f) {
        fallDelay_ = delay;
    }
    falling_ = true;
    unbreakable_ = t_.unbreakableTime;
}

// 0x973eb0: towards the target, the speed growing by 9.8 / s after the delay; reaching or passing
// it bounces back at -0.2 of the speed, the third time it stays (phase 1).
bool TreasureBox::updateFall(float dt) {
    if (0 < unbreakable_) {
        unbreakable_ -= dt;   // 0x974010
    }
    if (!falling_) {
        return false;
    }
    constexpr float kEpsilon = 1.1920929e-07f;
    const float diff = fallTarget_ - fallOffset_;
    if (std::fabs(diff) < kEpsilon) {
        bounces_ = 0;
        fallSpeed_ = 0;
        fallOffset_ = fallTarget_;
        falling_ = false;
        return false;
    }
    if (0 < fallDelay_) {
        fallDelay_ -= dt;
        return true;
    }
    fallSpeed_ += dt * 9.8f;
    const float sign = std::copysign(1.0f, diff);
    fallOffset_ += fallSpeed_ * sign * dt;
    if (0 <= diff) {
        if (++bounces_ == 3) {
            bounces_ = 0;
            fallSpeed_ = 0;
            fallOffset_ = fallTarget_;
            falling_ = false;
        } else {
            fallSpeed_ *= -0.2f;
            fallOffset_ = fallTarget_ + sign * fallSpeed_ * dt;
        }
    }
    return true;
}

math::Mat4 TreasureBox::pose() const {
    math::Mat4 m = world_;
    m[13] += fallOffset_;
    return m;
}

int TreasureBox::updateCoins(float dt, const Vec3& heroCentre) {
    int taken = 0;
    for (DroppedCoin& c : coins_) {
        if (c.taken) {
            continue;
        }
        if (c.fresh) {   // runs roof_box1/2: break at 4125, first step (t 0.025) at 4126
            c.fresh = false;
            continue;
        }
        if (c.delay > 0) {
            c.delay -= dt;
            if (c.delay > 0) {
                continue;
            }
        }
        c.shown = true;
        c.t += dt * t_.autoGetSpeed;
        const float k = std::min(c.t, 1.0f);
        const Vec3 a = c.start + (k * t_.autoGetImpulsePower) * c.launch;
        const Vec3 b = c.start + k * (heroCentre - c.start);
        c.position = a + k * (b - a);
        if (1 < c.t) {
            c.taken = true;
            c.shown = false;
            ++taken;
        }
    }
    return taken;
}

}   // namespace game
