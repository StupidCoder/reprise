#include "game/Respawn.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"

#include <cmath>

namespace beacon::game {

namespace {
constexpr float kActivateBehind = -0.15f;   // a marker activates once hero z - 0.15 < its z (0x124dc4c)
constexpr float kHeroRespawnHold = 3.0f;    // "herorespawn" before "after_herorespawn" (0x124dc48)
constexpr float kPi = 3.14159265f;
}   // namespace

RespawnTuning loadRespawnTuning(const std::filesystem::path& root) {
    RespawnTuning t;
    data::Bytes bytes;
    if (!data::loadFile(root / "data" / "multi_platformer" / "multi_platformer.xml", bytes)) {
        return t;
    }
    auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!xml) {
        return t;
    }
    auto reader = [](const data::XmlNode* n) {
        return [n](const char* name, float& v) {
            if (n) {
                if (auto s = n->childText(name)) {
                    data::parseFloatList(*s, &v, 1);
                }
            }
        };
    };
    const data::XmlNode* hero = xml->child("Hero");
    const data::XmlNode* respawn = hero ? hero->child("Respawn") : nullptr;
    auto r = reader(respawn);
    r("RespawnEffectTime", t.respawnEffectTime);
    r("FlySpawnSomerSaultHeight", t.flySpawnSomerSaultHeight);
    r("InvincibleTimeAfterRespawn", t.invincibleTimeAfterRespawn);
    if (respawn) {
        if (auto s = respawn->childText("RespawnMarkerVisible")) {
            t.markerVisible = *s == "true";
        }
    }
    const data::XmlNode* vr = xml->child("VrPlayer");
    auto w = reader(vr ? vr->child("Warp") : nullptr);
    w("WarpWaitTimeForRespawn", t.warpWaitTimeForRespawn);
    w("WarpFadeOutTime", t.warpFadeOutTime);
    w("WarpFadeBlackTime", t.warpFadeBlackTime);
    w("WarpFadeInTime", t.warpFadeInTime);
    w("RespawnAreaAngle", t.respawnAreaAngle);
    w("RespawnAreaRadius", t.respawnAreaRadius);
    w("RespawnAreaZ", t.respawnAreaZ);
    return t;
}

// ---- markers -----------------------------------------------------------------------------------

void RespawnMarkers::load(const Level& level, const ClipLengths& clips, bool markerVisible) {
    markers_.clear();
    clips_ = clips;
    markerVisible_ = markerVisible;
    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        const LevelObject& o = level.objects[i];
        if (o.className != "respawn_marker") {
            continue;
        }
        Marker m;
        m.placement = i;
        m.position = {o.transform.translate[0], o.transform.translate[1], o.transform.translate[2]};
        m.spawnTrigger = resolveLink(level, o, "SpawnTrigger");
        m.activateTrigger = resolveLink(level, o, "ActivateTrigger");
        m.delayTime = o.number("DelayTime").value_or(0.0f);
        m.spawnDelayTime = o.number("SpawnDelayTime").value_or(0.0f);
        // Setup 0x7c9530: without a SpawnTrigger the marker stands from the start ("idle", 3).
        if (m.spawnTrigger.empty()) {
            m.state = 3;
        }
        markers_.push_back(m);
    }
}

void RespawnMarkers::play(Marker& m, const char* clip) {
    m.clip = clip;
    m.clipTime = 0;
    ++m.clipSerial;
}

int RespawnMarkers::update(float dt, LevelRuntime& runtime, bool heroAlive, const math::Vec3& heroFeet, bool vrBusy) {
    int activatedNow = -1;
    for (std::size_t i = 0; i < markers_.size(); ++i) {
        Marker& m = markers_[i];
        m.clipTime += dt;
        switch (m.state) {
        case 0:   // until the SpawnTrigger fires (0x552f00)
            if (!m.activated && runtime.linkFired(m.spawnTrigger)) {
                m.state = 1;
                m.timer = 0;
            }
            break;
        case 1:   // SpawnDelayTime, then shown with "spawn"
            m.timer += dt;
            if (m.spawnDelayTime < m.timer) {
                play(m, "spawn");
                m.state = 2;
            }
            break;
        case 2:
            if (m.clipTime >= clips_.spawn) {
                play(m, "idle");
                m.state = 3;
            }
            break;
        case 3: {
            if (m.activated) {
                break;
            }
            bool activate = false;
            if (!m.pending) {
                if (m.activateTrigger.empty()) {
                    // By position (with RespawnMarkerVisible): a living hero (0x66ce40) whose z
                    // less 0.15 lies before the marker's, while the VR player is not warping.
                    activate = markerVisible_ && heroAlive && !vrBusy && m.position.z > heroFeet.z + kActivateBehind;
                } else if (runtime.linkFired(m.activateTrigger)) {
                    m.pending = true;
                }
            }
            if (!activate && m.pending) {
                m.delayTimer += dt;
                if (m.delayTime < m.delayTimer && !vrBusy) {
                    activate = true;
                    m.pending = false;
                }
            }
            if (activate) {
                // 0x7c9d80: the heroes' and the VR player's respawn points (the scene), "activate".
                m.activated = true;
                play(m, "activate");
                m.state = 4;
                activatedNow = int(i);
            }
            break;
        }
        case 4:
            if (m.clipTime >= clips_.activate) {
                play(m, "activate_idle");
                m.state = 5;
            }
            break;
        case 6:
            m.timer += dt;
            if (kHeroRespawnHold < m.timer) {
                play(m, "after_herorespawn");
                m.state = 7;
            }
            break;
        case 7:
            if (m.clipTime >= clips_.afterHeroRespawn) {
                play(m, "activate_idle");
                m.state = 5;
            }
            break;
        case 8:
            if (m.clipTime >= clips_.remove) {
                m.removed = true;
            }
            break;
        default:
            break;
        }
        // The links of later markers see the activation (+0x2141, virtual +0xa0).
        runtime.setOn(m.placement, m.activated);
    }
    if (activatedNow >= 0) {
        // The marker activated before goes (0xa5afa0 -> 0x7ca170: "remove", state 8).
        for (std::size_t i = 0; i < markers_.size(); ++i) {
            Marker& m = markers_[i];
            if (int(i) != activatedNow && m.activated && m.state != 8 && !m.removed) {
                play(m, "remove");
                m.state = 8;
            }
        }
    }
    return activatedNow;
}

void RespawnMarkers::heroRespawned(int marker) {
    if (marker < 0 || std::size_t(marker) >= markers_.size()) {
        return;
    }
    Marker& m = markers_[std::size_t(marker)];
    m.state = 6;
    m.timer = 0;
    play(m, "herorespawn");
}

// ---- VR warp -----------------------------------------------------------------------------------

void VrWarp::start(const RespawnTuning& t, const math::Vec3& to) {
    state = 1;
    timer = 0;
    skip = false;
    wait = t.warpWaitTimeForRespawn;
    fadeOut = t.warpFadeOutTime;
    black = t.warpFadeBlackTime;
    fadeIn = t.warpFadeInTime;
    target = to;
}

float VrWarp::fade() const {
    switch (state) {
    case 2:
        return std::min(1.0f, timer / std::max(fadeOut, 1e-6f));
    case 3:
        return 1;
    case 4:
        return std::max(0.0f, 1 - timer / std::max(fadeIn, 1e-6f));
    default:
        return 0;
    }
}

VrWarp::Events VrWarp::update(float dt) {
    Events e;
    if (state == 0) {
        return e;
    }
    if (skip) {
        // **[fit]** run roof_g6: the warp timer (+0x480) stays 0 for one more update after the
        // fade-out ended (3733, 3734), then counts again; the code path is not followed.
        skip = false;
        return e;
    }
    timer += dt;
    switch (state) {
    case 1:
        if (wait < timer) {
            state = 2;
            timer = 0;
        }
        break;
    case 2:
        if (fadeOut < timer) {
            state = 3;
            timer = 0;
            skip = true;
            e.moveRig = true;
        }
        break;
    case 3:
        if (black < timer) {
            state = 4;
            timer = 0;
            e.respawned = true;
        }
        break;
    case 4:
        if (fadeIn < timer) {
            state = 0;
            timer = 0;
            e.done = true;
        }
        break;
    default:
        break;
    }
    return e;
}

bool respawnNeedsWarp(const RespawnTuning& t, const math::Vec3& point, const math::Vec3& vr) {
    const math::Vec3 d = point - vr;
    if (d.z > t.respawnAreaZ) {
        return true;
    }
    if (std::sqrt(d.x * d.x + d.z * d.z) < t.respawnAreaRadius) {
        return true;
    }
    return std::fabs(std::atan2(d.x, d.z)) < t.respawnAreaAngle * kPi / 180;
}

}   // namespace game
