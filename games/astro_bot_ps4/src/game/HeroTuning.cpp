#include "game/HeroTuning.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"

namespace beacon::game {

std::optional<HeroTuning> loadHeroTuning(const std::filesystem::path& root, std::string* error) {
    const auto path = root / "data" / "multi_platformer" / "multi_platformer.xml";
    data::Bytes bytes;
    if (!data::loadFile(path, bytes)) {
        if (error) {
            *error = path.string() + ": cannot read";
        }
        return std::nullopt;
    }
    auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), error);
    if (!xml) {
        return std::nullopt;
    }
    HeroTuning t;
    const data::XmlNode* hero = xml->child("Hero");
    if (!hero) {
        return t;   // shipped defaults
    }
    auto read = [&](const char* name, float& value) {
        if (auto text = hero->childText(name)) {
            data::parseFloatList(*text, &value, 1);
        }
    };
    read("Scale", t.scale);
    read("Speed", t.speed);
    read("AccelLimitMin", t.accelLimitMin);
    read("AccelLimitMax", t.accelLimitMax);
    read("Gravity", t.gravity);
    read("JumpHeightMin", t.jumpHeightMin);
    read("JumpHeightMax", t.jumpHeightMax);
    read("JumpableTimeAfterFalling", t.jumpableTimeAfterFalling);
    read("ControlOnJumpMin", t.controlOnJumpMin);
    read("ControlOnJumpMax", t.controlOnJumpMax);
    read("HoverTime", t.hoverTime);
    read("HoverTimeMin", t.hoverTimeMin);
    read("HoverVelocityOnStart", t.hoverVelocityOnStart);
    read("HoverGravityStart", t.hoverGravityStart);
    read("HoverGravityEnd", t.hoverGravityEnd);
    read("WalkAnimSpeedScale", t.walkAnimSpeedScale);
    read("RunAnimSpeedScale", t.runAnimSpeedScale);
    read("AnimBlendStartSpeed", t.animBlendStartSpeed);
    read("AnimBlendEndSpeed", t.animBlendEndSpeed);
    read("ClimbableStepHeight", t.climbableStepHeight);
    read("SlopeMaxAngle", t.slopeMaxAngle);
    read("SlopeMinAngle", t.slopeMinAngle);
    read("SlopeMinAngleOnSliding", t.slopeMinAngleOnSliding);
    read("SlopeSideFriction", t.slopeSideFriction);
    read("SlopeSideControl", t.slopeSideControl);
    read("SlopeAccel", t.slopeAccel);
    read("TiltSmoothRatio", t.tiltSmoothRatio);
    read("TiltScale", t.tiltScale);
    read("TiltMaxAngle", t.tiltMaxAngle);
    read("CollisionYOffset", t.collisionYOffset);
    read("CollisionHeight", t.collisionHeight);
    read("CollisionRadius", t.collisionRadius);
    read("EnemySensorCollisionRadius", t.enemySensorRadius);
    read("EnemySensorCollisionHeight", t.enemySensorHeight);
    read("CollisionForWallYOffset", t.collisionForWallYOffset);
    read("CollisionForWallHeight", t.collisionForWallHeight);
    read("CollisionForWallRadius", t.collisionForWallRadius);
    read("MinStickInput", t.minStickInput);
    if (const data::XmlNode* punch = hero->child("Punch")) {
        auto readPunch = [&](const char* name, float& value) {
            if (auto text = punch->childText(name)) {
                data::parseFloatList(*text, &value, 1);
            }
        };
        readPunch("NonCancelableTime", t.punchNonCancelableTime);
        readPunch("CollisionStartTime", t.punchCollisionStartTime);
        readPunch("CollisionTime", t.punchCollisionTime);
        readPunch("CollisionRadius", t.punchCollisionRadius);
        readPunch("AnimCancelableTime", t.punchAnimCancelableTime);
    }
    if (const data::XmlNode* damage = hero->child("Damage")) {
        auto readDamage = [&](const char* name, float& value) {
            if (auto text = damage->childText(name)) {
                data::parseFloatList(*text, &value, 1);
            }
        };
        readDamage("LightDamageDownTime", t.lightDamageDownTime);
        readDamage("MiddleDamageDownTime", t.middleDamageDownTime);
        readDamage("HeavyDamageDownTime", t.heavyDamageDownTime);
        readDamage("DamageVelocityXZ", t.damageVelocityXZ);
        readDamage("DamageVelocityY", t.damageVelocityY);
        readDamage("InvincibleTimeAfterKnockDown", t.invincibleTimeAfterKnockDown);
    }
    if (const data::XmlNode* disp = hero->child("Disp")) {
        auto readDisp = [&](const char* name, float* value, int n) {
            if (auto text = disp->childText(name)) {
                data::parseFloatList(*text, value, n);
            }
        };
        if (auto type = disp->childText("ShadowType")) {
            t.decalShadow = *type == "Decal";
        }
        readDisp("DecalShadowColor", t.decalShadowColor, 4);
        readDisp("DecalShadowHeight", &t.decalShadowHeight, 1);
        readDisp("DecalShadowFadeHeightRatio", &t.decalShadowFadeHeightRatio, 1);
        readDisp("DecalShadowOffsetY", &t.decalShadowOffsetY, 1);
    }
    return t;
}

}   // namespace game
