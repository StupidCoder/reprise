// The public screen (class public_screen, MupPublicScreen: vtable 0x16bf3a0, constructor
// 0x7bcef0, setup 0x7bd500, update 0x7bdd00, placement 0x7be0f0). Findings:
// .analysis/docs/objects.md "Public screen".
//
// A TV (mup_co_public_screen_tv) whose screen (mup_public_screen, a quad placed at the TV's
// "screen" joint) shows the level as the public-screen camera sees it: the quad's texture node
// "/file1" is given that camera's picture (0x7bd500 -> 0xf946b0, hash 0x789ee24b). It is turned
// on and off by links:
//   - each update TurnOnTrigger firing sets the request, TurnOffTrigger firing clears it;
//   - 0 off:      idle_off; the request -> the screen shows, "on", the camera runs -> 1;
//   - 1 on:       "on" ends -> idle_on -> 2;
//   - 2 lit:      the request cleared -> "off" -> 3 **[hypothesis: the decompiled branch is
//                 empty; the other states fix what it must do]**;
//   - 3 off:      "off" ends -> the screen hides, idle_off, the camera stops -> 0.
// Without a TurnOnTrigger the screen starts on unless ExternalDisplay is set (0x7bd500).
#pragma once

#include "game/LevelRuntime.hpp"
#include "math/Transform.hpp"

#include <cstdint>
#include <string>

namespace beacon::game {

struct Level;

struct PublicScreenSetup {
    std::size_t object = 0;
    math::Mat4 world = math::identity();
    std::string definition;          // Odx (mup_co_public_screen_tv)
    bool flip = false;               // Flip: the screen mirrored in x (without ExternalDisplay)
    bool externalDisplay = false;    // ExternalDisplay
    LevelLink turnOn, turnOff;
};
PublicScreenSetup publicScreenSetup(const Level& level, const LevelRuntime& runtime, std::size_t index);

class PublicScreen {
public:
    enum class Phase { Off, TurningOn, On, TurningOff };

    explicit PublicScreen(PublicScreenSetup setup);
    // One update; `clipEnded`: the TV's one-shot clip ("on" / "off") has ended.
    void update(const LevelRuntime& runtime, bool clipEnded);
    // Sets the on request as the links do (debugging: BEACON_DEBUG_PUBLIC_SCREEN).
    void request(bool on) { request_ = on; }

    const PublicScreenSetup& setup() const { return setup_; }
    Phase phase() const { return phase_; }
    bool screenShown() const { return phase_ != Phase::Off; }
    bool cameraOn() const { return phase_ != Phase::Off; }
    // The screen's x scale: -1 when flipped (0x124c840 by Flip; 1 with ExternalDisplay).
    float screenScaleX() const { return setup_.externalDisplay || !setup_.flip ? 1.0f : -1.0f; }
    const std::string& clip() const { return clip_; }
    std::uint32_t clipSerial() const { return clipSerial_; }

private:
    void play(const char* name);

    PublicScreenSetup setup_;
    Phase phase_ = Phase::Off;
    bool request_ = false;   // +0x214c
    std::string clip_;
    std::uint32_t clipSerial_ = 0;
};

}   // namespace game
