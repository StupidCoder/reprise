// Gameplay tuning values. The original keeps them in a table of named settings whose defaults
// are set in code (Values::init @0x143548; names from Values::settingChanged @0x141e40) and
// reads them through a global (g_pVal). Values below are the defaults, confirmed by reading
// the table from the running original (reference run l11_60, .analysis/docs/level-1-1.md).
#pragma once

namespace doc::game {

struct Tuning {
    float playerRunSpeed = 1.2f;          // +0x08 player_run_speed
    float playerScoreSpeedMult = 0.2f;    // +0x0c player_score_speed_mult
    float playerCarrySpeed = 1.2f;        // +0x14 (used while carrying in state 8)
    float playerTouchSize = 48.0f;        // +0x1c player_touch_size
    float playerSpeedRecoverMs = 475.0f;  // +0x48 player_speed_recover (int in the table)
    float waterSpeedMult = 0.9f;          // +0x54 playerwater_speed_mult
    float cheeseWeight[5] = {7.0f, 9.0f, 11.5f, 14.5f, 17.0f};  // +0x15c.. cheese1..5_weight (types 10..6)
    float otherWeight = 4.0f;             // +0x198 weight_other
    float totalWeight = 9.0f;             // +0x19c weight_total: heavier trails slow the mouse
    float chainMod = 0.2f;                // +0x1a0 chain_mod
    float playerScale = 1.0f;             // +0x3c drawn size of the mouse (shrinks into holes)
    int clockStepMs = 200;                // +0x1a4 game time per tick of the level clock
    int clockStep = 1;                    // +0x1a8 clock decrement per step
    int objectPulseMs = 600;              // +0x2f4 glow after a snap or pickup
    int winBannerMs = 3000;               // +0x2f8 level-won banner before the results
};

// Settings read by name with a default in code (ManagerSettings::getVarF/getVarI); the
// defaults are what the shipped game uses.
struct ScoreSettings {
    int cheesePoints[5] = {50, 25, 20, 15, 10};  // "Cheese5".."Cheese1": types 6..10
    // Trail multiplier per poker hand (PokerHand order): "hand_bluff", "hand_pair",
    // "hand_threes", "hand_twopair", "hand_fours", "hand_fullhouse", "hand_straight",
    // "hand_flush" (Profile::getHandBonus @0x173200). A straight is worth more than a flush.
    float handBonus[8] = {1.0f, 2.0f, 3.0f, 4.0f, 4.0f, 5.0f, 7.0f, 6.0f};
    float exitBonusMultiplier = 2.0f;  // "exit_bonus_multiplier" (Player::scoreCurrentTrail)
    int timerMultiply = 1;             // "timer_multiply"
    int resultsDelayMs = 500;          // results wait while cheese is still going in
};

// Fixed-step clock of the original (iPhone::Update @0x111098, Application::s_nTickTime @0x5f1510):
// 17 ms ticks on every device except the first iPhone/iPod generation (33 ms).
constexpr int kTickMs = 17;
// The mouse moves `speed * dt * kSpeedScale` per tick (Player::update @0x126df2, 0.05 @0x1270d4).
constexpr float kSpeedScale = 0.05f;

}  // namespace doc::game
