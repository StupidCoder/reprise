#include "game/MoviePrompt.hpp"

#include "game/Hero.hpp"
#include "game/LevelDocument.hpp"

#include <array>
#include <utility>

namespace beacon::game {

namespace {

constexpr float kHideDelay = 0.5f;   // 0x120c014: from the hide request to the hide clip

PromptType promptType(const std::string& name) {
    static const std::array<std::pair<const char*, PromptType>, 12> kNames{{
        {"HeroMove", PromptType::HeroMove}, {"HeroJump", PromptType::HeroJump}, {"HeroHover", PromptType::HeroHover},
        {"HeroPunch", PromptType::HeroPunch}, {"HeroThrow", PromptType::HeroThrow},
        {"VrPlayerHeadbutt", PromptType::VrPlayerHeadbutt}, {"GadgetRopeFlick", PromptType::GadgetRopeFlick},
        {"GadgetRopePull", PromptType::GadgetRopePull}, {"GadgetSlingShot", PromptType::GadgetSlingShot},
        {"LostBot", PromptType::LostBot}, {"SoccerHeading", PromptType::SoccerHeading}, {"Generic", PromptType::Generic},
    }};
    for (const auto& [n, t] : kNames) {
        if (name == n) {
            return t;
        }
    }
    return PromptType::Generic;
}

}   // namespace

MoviePromptSetup moviePromptSetup(const Level& level, const LevelRuntime& runtime, std::size_t index) {
    const LevelObject& o = level.objects[index];
    MoviePromptSetup s;
    s.object = index;
    s.world = runtime.world(index);
    s.type = promptType(o.text("PromptType", "Generic"));
    s.movie = o.text("MovieName");
    s.show = resolveLink(level, o, "ShowTrigger");
    s.hide = resolveLink(level, o, "HideTrigger");
    s.showDelay = o.number("ShowDelayTime").value_or(0);
    s.onTouchPad = o.text("Location") == "TouchPad";
    return s;
}

// 0x58c9a0 by PromptType (each type tests its own hero state, original ids in comments), or the
// hide trigger. **[not ported]** the remaining types' conditions (the lost bot's rescue, gadgets,
// the VR player's headbutt) and the hero_from_ds4 movie's flag (+0x27bb).
bool MoviePrompt::hideRequested(const LevelRuntime& runtime, HeroState hero) const {
    switch (setup_.type) {
    case PromptType::HeroMove:
        if (hero == HeroState::Walk) {   // 2
            return true;
        }
        break;
    case PromptType::HeroJump:
        if (hero == HeroState::Jump) {   // 3
            return true;
        }
        break;
    case PromptType::HeroHover:
        if (hero == HeroState::Hover) {   // 6
            return true;
        }
        break;
    case PromptType::HeroPunch:
        if (hero == HeroState::Punch) {   // 8
            return true;
        }
        break;
    default:
        break;   // HeroThrow (0x15): the throw is not ported
    }
    return !setup_.hide.empty() && runtime.linkFired(setup_.hide);
}

void MoviePrompt::play(const char* name) {
    clip_ = name;
    ++clipSerial_;
}

void MoviePrompt::update(float dt, const LevelRuntime& runtime, HeroState hero, const Feedback& view) {
    if (setup_.onTouchPad || setup_.movie.empty()) {
        return;   // **[not ported]** prompts on the controller's touch pad
    }
    if (phase_ != Phase::Waiting && phase_ != Phase::Done) {
        hideRequest_ = hideRequest_ || hideRequested(runtime, hero);
    }
    switch (phase_) {
    case Phase::Waiting:
        if (setup_.show.empty() || runtime.linkFired(setup_.show)) {
            phase_ = Phase::Delay;
            timer_ = 0;
        }
        break;
    case Phase::Delay:
        // ShowDelayTimeAfterClearing replaces the delay on a level cleared before (0x73b100):
        // **[not ported]** (no save data yet).
        timer_ += dt;
        if (hideRequest_) {
            hideRequest_ = false;
            phase_ = Phase::Waiting;
        } else if (timer_ > setup_.showDelay) {
            deviceOpened_ = true;
            phase_ = Phase::Starting;
        }
        break;
    case Phase::Starting:
        if (view.deviceOpen) {
            playing_ = true;
            shown_ = true;
            play("show");
            phase_ = Phase::Showing;
        }
        break;
    case Phase::Showing:
        if (view.showEnded) {
            play("idle");
            phase_ = Phase::Playing;
        }
        break;
    case Phase::Playing:
        if (hideRequest_) {
            phase_ = Phase::Lingering;
            timer_ = 0;
        }
        break;
    case Phase::Lingering:
        timer_ += dt;
        if (timer_ > kHideDelay) {
            play("hide");
            phase_ = Phase::Hiding;
        }
        break;
    case Phase::Hiding:
        if (view.hideEnded) {
            playing_ = false;
            shown_ = false;
            deviceOpened_ = false;
            phase_ = Phase::Stopping;
        }
        break;
    case Phase::Stopping:
        if (view.deviceClosed) {
            phase_ = Phase::Done;   // **[not ported]** the repeat flag (+0x4e8) that goes back to 0
        }
        break;
    case Phase::Done:
        break;
    }
}

}   // namespace game
