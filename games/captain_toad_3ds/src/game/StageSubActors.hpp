// =============================================================================================
// StageSubActors.hpp - models an actor creates for itself (InitSubActor; StageObjects.hpp)
// =============================================================================================
//
// What the player sees: pieces that fly apart when something breaks, and what stays behind.
//
// An archive's InitSubActor.byml lists the models its actor makes at init (CreatorList: ClassName,
// ModelName, ObjectName, IsAlive). The exporter places each as an object "<host>/<model>"
// (string parameter SpawnedBy = the host, IsAliveAtStart = IsAlive); the host finds them by
// SpawnedBy and class, and shows them.
//
// BreakModel (ctor 0x2f068c, vtable 0x4d2060; the sub-actor factory 0x23cb18 passes ActionName,
// default "Break", and JointName): init 0x21b5ec loads the model and waits dead. Its appear
// (vtable +0x10, 0x21b658) takes the host's pose (or the joint's), starts its action and shows it;
// nerve 0x419914 kills it when the action ends.
// SubActorModel (a CreatorList entry without ClassName; port name): a plain model; with
// IsUseHostPlacementInfo at the host's placement. Its host starts an action on it and shows it
// (the sign: "Appear", 0x2fd7b0 then vtable +0x10); it stays.
// =============================================================================================
#pragma once

#include "game/StageObjects.hpp"

#include <string>

namespace captain_toad::RecoveredStageSubActors {

using namespace captain_toad::RecoveredStage;

// Created dead unless IsAlive: hidden, not collided with.
inline void startHidden(Object& o) {
    if (o.number("IsAliveAtStart", 0) == 0) {
        o.visible = false;
        o.alive = false;
    }
}

struct BreakModel : Behaviour {
    bool shown = false;
    explicit BreakModel(Object& o) { startHidden(o); }
    void appear(Object& o, const Object& host, const std::string& action = "Break") {   // 0x21b658
        o.world = host.world;
        o.visible = true;
        o.alive = true;
        shown = true;
        o.startAction(action);
    }
    void control(World&, Object& o) override {   // nerve 0x419914
        if (shown && o.isActionEnd()) {
            o.alive = false;
        }
    }
};

struct SubActorModel : Behaviour {
    explicit SubActorModel(Object& o) { startHidden(o); }
    void appear(Object& o, const std::string& action) {
        o.world = o.placement;
        o.visible = true;
        o.alive = true;
        o.startAction(action);
    }
};

// The object of `cls` spawned by `host` (-1: none).
inline int spawnedBy(const World& w, const Object& host, const std::string& cls) {
    for (size_t i = 0; i < w.objects.size(); i++) {
        if (w.objects[i].cls == cls && w.objects[i].string("SpawnedBy", "") == host.id) {
            return int(i);
        }
    }
    return -1;
}

inline const Register<BreakModel> kBreakModel{"BreakModel"};
inline const Register<SubActorModel> kSubActorModel{"SubActorModel"};

}   // namespace captain_toad::RecoveredStageSubActors
