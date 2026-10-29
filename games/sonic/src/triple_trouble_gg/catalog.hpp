#pragma once
#include "gg_engine/audio/music_player.hpp"
#include <array>

// Names the app shows for Sonic Triple Trouble (Game Gear): the level
// selector's start points (Sonic) and the sound test's tracks. Descriptive labels written for
// BlueStreak, based on the zone names the game shows on its title cards.
namespace triple_trouble_gg {

// A start point offered by the level selector: zone and act. The level
// numbers are indices into `levels`, then special_stage_start + 0-4 for the
// five special stages and ending_start / good_ending_start for the ending
// (the rival's short scene, or the credits with all six emeralds).
struct LevelEntry {
    unsigned zone, act;
    const char* world;
    const char* label;
};

inline constexpr unsigned special_stage_start = 50, special_stage_count = 5;
inline constexpr unsigned ending_start = 100, good_ending_start = 101;

inline constexpr std::array<LevelEntry, 18> levels{{
    {0, 0, "Great Turquoise", "Act 1"},
    {0, 1, "Great Turquoise", "Act 2"},
    {0, 2, "Great Turquoise", "Act 3"},
    {1, 0, "Sunset Park", "Act 1"},
    {1, 1, "Sunset Park", "Act 2"},
    {1, 2, "Sunset Park", "Act 3"},
    {2, 0, "Meta Junglira", "Act 1"},
    {2, 1, "Meta Junglira", "Act 2"},
    {2, 2, "Meta Junglira", "Act 3"},
    {3, 0, "Robotnik Winter", "Act 1"},
    {3, 1, "Robotnik Winter", "Act 2"},
    {3, 2, "Robotnik Winter", "Act 3"},
    {4, 0, "Tidal Plant", "Act 1"},
    {4, 1, "Tidal Plant", "Act 2"},
    {4, 2, "Tidal Plant", "Act 3"},
    {5, 0, "Atomic Destroyer", "Act 1"},
    {5, 1, "Atomic Destroyer", "Act 2"},
    {5, 2, "Atomic Destroyer", "Act 3"},
}};

// A sound test entry. `song` is the sound driver's request number (81-A3;
// 91, 96 and 99 are silent and left out); -1 plays the sampled "SEGA" voice
// from the logo screen instead.
using MusicTrack = gg_engine::SoundTestTrack;

inline constexpr std::array<MusicTrack, 33> music_tracks{{
    {-1, "SEGA voice", "Sampled voice from the original logo."},
    {0x86, "Title screen", "The original title screen."},
    {0x85, "Opening", "The opening scene before the title."},
    {0x87, "Character select", "Choosing the player."},
    {0x88, "Act start", "The jingle on each act's title card."},
    {0x81, "Great Turquoise", "Great Turquoise Acts 1 and 2."},
    {0x82, "Sunset Park", "Sunset Park Acts 1 and 2."},
    {0x93, "Sunset Park Act 3", "The train ride in Sunset Park Act 3."},
    {0x83, "Meta Junglira", "Meta Junglira Acts 1 and 2."},
    {0x84, "Robotnik Winter", "Robotnik Winter Acts 1 and 2."},
    {0x9A, "Tidal Plant", "Tidal Plant Acts 1 and 2."},
    {0x9B, "Atomic Destroyer", "Atomic Destroyer Acts 1 and 2."},
    {0x8A, "Boss battle", "The boss acts (Act 3)."},
    {0x9E, "Atomic Destroyer boss", "The chase through Atomic Destroyer Act 3."},
    {0x8B, "Final machine", "The last machine in Atomic Destroyer Act 3."},
    {0x97, "Rival", "Rival scenes, guardians and the short ending."},
    {0x9C, "Special stage", "The special stages on foot."},
    {0x9D, "Biplane stage", "The special stages flown in the biplane over the sea."},
    {0xA0, "Chaos Emerald", "The emerald-collection jingle."},
    {0x90, "Speed shoes", "The speed shoes power-up."},
    {0x92, "Invincibility", "The invincibility power-up."},
    {0x8F, "Flight power-up", "Sonic's flight from an item box."},
    {0x95, "Jet board", "Sonic's jet board from an item box."},
    {0x98, "Tails' flight", "Tails' flight from an item box."},
    {0x94, "Submarine", "Tails' submarine ride."},
    {0x8C, "Extra life", "The extra-life jingle."},
    {0x89, "Act clear", "The act-clear jingle."},
    {0x8D, "Life lost", "The life-lost jingle."},
    {0x9F, "Continue", "The continue screen's countdown."},
    {0x8E, "Game over", "The game-over screen."},
    {0xA3, "Ending", "The ending's last scene."},
    {0xA2, "Credits", "The ending with all six emeralds."},
    {0xA1, "Unused song", "Not requested anywhere in the reconstructed game."},
}};

} // namespace triple_trouble_gg
