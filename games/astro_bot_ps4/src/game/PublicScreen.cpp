#include "game/PublicScreen.hpp"

#include "game/LevelDocument.hpp"

namespace beacon::game {

PublicScreenSetup publicScreenSetup(const Level& level, const LevelRuntime& runtime, std::size_t index) {
    const LevelObject& o = level.objects[index];
    PublicScreenSetup s;
    s.object = index;
    s.world = runtime.world(index);
    s.definition = o.odx.empty() ? "mup_co_public_screen_tv" : o.odx;
    s.flip = o.flag("Flip").value_or(false);
    s.externalDisplay = o.flag("ExternalDisplay").value_or(false);
    s.turnOn = resolveLink(level, o, "TurnOnTrigger");
    s.turnOff = resolveLink(level, o, "TurnOffTrigger");
    return s;
}

PublicScreen::PublicScreen(PublicScreenSetup setup) : setup_(std::move(setup)) {
    if (setup_.turnOn.empty() && !setup_.externalDisplay) {
        play("on");
        phase_ = Phase::TurningOn;
        request_ = true;
    } else {
        play("idle_off");
    }
}

void PublicScreen::play(const char* name) {
    clip_ = name;
    ++clipSerial_;
}

void PublicScreen::update(const LevelRuntime& runtime, bool clipEnded) {
    if (!setup_.turnOn.empty() && runtime.linkFired(setup_.turnOn)) {
        request_ = true;
    }
    if (!setup_.turnOff.empty() && runtime.linkFired(setup_.turnOff)) {
        request_ = false;
    }
    switch (phase_) {
    case Phase::Off:
        if (!setup_.turnOn.empty() && request_) {
            play("on");
            phase_ = Phase::TurningOn;
        }
        break;
    case Phase::TurningOn:
        if (clipEnded) {
            play("idle_on");
            phase_ = Phase::On;
        }
        break;
    case Phase::On:
        if (!setup_.turnOff.empty() && !request_) {
            play("off");
            phase_ = Phase::TurningOff;
        }
        break;
    case Phase::TurningOff:
        if (clipEnded) {
            play("idle_off");
            phase_ = Phase::Off;
        }
        break;
    }
}

}   // namespace game
