// The level's music players (class bgm_player, vtable 0x1688090, constructor 0x30c290, start
// 0x30c7d0, update 0x30cb90, play 0x30c9b0, forced play 0x30cc70). Findings: .analysis/docs/sound.md
// "Level music".
//
// A player names up to three sound requests, one per music slot of the game's music system
// (MupMain -> 0x739dc0 starts a slot, 0x73a460 stops one, 0x73a2e0 stops all):
//   slot 0 Request, slot 1 RequestHero, slot 2 RequestController
// (ROOFTOPS: mup_city_rooftop_bgm and its vocal layer mup_city_rooftop_bgm_vox). Playing a player
// starts every slot that has a request and stops (FadeOutTime) every slot that has none; the
// slots start together, at the same volume (1 x the music volume), without a position.
//
// Its TriggerBox link and TriggerType (Normal = 0, HeroSpawnLanding = 1) decide when:
//   start   a Normal player without a link plays at once (state 1);
//   update  state 0: a Normal player without a link plays (state 1); a Normal player whose link
//                    fires plays (state 2); other types wait
//           state 1: a linked player whose link fires plays (state 2)
//           state 2: a Normal player without a link stops every slot (no fade) and goes to 0
//   landing the hero's spawn state (update 0x69ed20, on touching the ground at the end of the spawn
//           drop) calls 0x551ae0, which plays every player in the rooms' lists (+0x4a0 +0x388)
//           through 0x30cc70 (state 0, play, state 2). **[inference]** those lists hold the
//           HeroSpawnLanding players (the list's filling is not decoded).
// The game reads the fades as FadeInTime / FadeOutTime (defaults 0 and 0.5 s, 0x11a6350); the
// levels' bgm_player placements say FadeInDuration / FadeOutDuration, which it does not read.
// The hero's states drive the sound variable bgm_hero_state (0xc3cd60): entering or leaving Idle
// (0x4c8870 / 0x4c8ae0) and restarting its wait (0x4c9370) set "move"; Idle sets "idle" only when
// its random idle animation (0x6888b0) is one of three dances (clips 0x9e85ccf4, 0x150c6a8c,
// 0x04dd9726), played to the music's tempo. The rooftop vocal layer (mup_city_rooftop_bgm_vox)
// is at level 1 in "idle" and 0 in "move": the hero sings along only while it dances.
// Not ported: the start's two exceptions (owner flag +0x1b60 -> +0x210, a level whose name hash is
// 0x693c55c: state 1 without playing), the property-change callbacks (editor live edits).
#pragma once

#include "game/LevelRuntime.hpp"

#include <array>
#include <string>
#include <vector>

namespace beacon::game {

struct Level;

// One command for the music system: start `request` on `slot` (fading in over `fade` seconds),
// stop the slot (fading out over `fade`) when `request` is empty, or, with `variable` set, give a
// sound variable a new state (interactive pieces react to it, data::LevelRule).
struct MusicCue {
    int slot = 0;            // 0 Request, 1 RequestHero, 2 RequestController
    std::string request;     // sound request name, empty = stop
    float fade = 0;          // seconds
    std::string variable, state;   // e.g. bgm_hero_state = move
};

class LevelMusic {
public:
    static constexpr int kSlots = 3;

    // Collects the level's bgm_player placements (links resolved as the runtime does).
    void load(const Level& level);
    // The players' start (0x30c7d0), once after loading.
    void start();
    // One 1/60 s update (0x30cb90), after the level's objects.
    void update(const LevelRuntime& runtime);
    // The hero touched the ground at the end of its spawn drop (0x69ed20 -> 0x551ae0).
    void heroSpawnLanded();
    // A sound variable changes (0xc3cd60(name, state)); e.g. the hero's states set bgm_hero_state.
    void setVariable(const std::string& variable, const std::string& state) {
        cues_.push_back({0, {}, 0, variable, state});
    }

    // Slot commands since the last call, in order.
    std::vector<MusicCue> takeCues() { return std::move(cues_); }
    std::size_t playerCount() const { return players_.size(); }

private:
    struct Player {
        std::array<std::string, kSlots> requests;
        LevelLink link;
        bool normal = true;          // TriggerType Normal (0)
        float fadeIn = 0, fadeOut = 0.5f;
        int state = 0;               // +0x1ca4
    };
    void play(Player& p);            // 0x30c9b0
    void stopAll();                  // 0x73a2e0(0, ...)

    std::vector<Player> players_;
    std::vector<MusicCue> cues_;
};

}   // namespace game
