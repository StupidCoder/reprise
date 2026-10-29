#pragma once
#include "gg_engine/audio/music_player.hpp"
#include <array>

// Names the app shows for Sonic 2 (Game Gear): the level selector's start
// points and the sound test's tracks. These are descriptive labels written for BlueStreak, based on
// the zone names the game shows on its title cards.
namespace sonic2_gg {

// A start point offered by the level selector: zone (0-6) and act (0-2).
// The level numbers are indices into `levels`; ending_start and
// good_ending_start (not in the table) start the ending, zone 7, without
// and with all six emeralds.
struct LevelEntry {
    unsigned zone, act;
    const char* world;
    const char* label;
};

inline constexpr unsigned ending_start = 100, good_ending_start = 101;
inline constexpr int ending_zone = 7;

inline constexpr std::array<const char*, 7> zone_names{
    "Under Ground", "Sky High",      "Aqua Lake",  "Green Hills",
    "Gimmick Mt.",  "Scrambled Egg", "Crystal Egg"};

inline constexpr std::array<LevelEntry, 21> levels{{
    {0, 0, "Under Ground", "Act 1"},  {0, 1, "Under Ground", "Act 2"},
    {0, 2, "Under Ground", "Act 3"},  {1, 0, "Sky High", "Act 1"},
    {1, 1, "Sky High", "Act 2"},      {1, 2, "Sky High", "Act 3"},
    {2, 0, "Aqua Lake", "Act 1"},     {2, 1, "Aqua Lake", "Act 2"},
    {2, 2, "Aqua Lake", "Act 3"},     {3, 0, "Green Hills", "Act 1"},
    {3, 1, "Green Hills", "Act 2"},   {3, 2, "Green Hills", "Act 3"},
    {4, 0, "Gimmick Mt.", "Act 1"},   {4, 1, "Gimmick Mt.", "Act 2"},
    {4, 2, "Gimmick Mt.", "Act 3"},   {5, 0, "Scrambled Egg", "Act 1"},
    {5, 1, "Scrambled Egg", "Act 2"}, {5, 2, "Scrambled Egg", "Act 3"},
    {6, 0, "Crystal Egg", "Act 1"},   {6, 1, "Crystal Egg", "Act 2"},
    {6, 2, "Crystal Egg", "Act 3"},
}};

// A sound test entry. `song` is the sound driver's request number (81-95;
// 89 and 8A point outside the song data and are left out); -1 plays the
// sampled "SEGA" voice from the logo screen instead.
using MusicTrack = gg_engine::SoundTestTrack;

inline constexpr std::array<MusicTrack, 20> music_tracks{{
    {-1, "SEGA voice", "Sampled voice from the original logo."},
    {0x87, "Opening / Scrambled Egg", "The opening scene and Scrambled Egg Acts 1, 2 and 3."},
    {0x88, "Title screen", "The original title screen."},
    {0x93, "Act start", "The jingle on each act's title card."},
    {0x82, "Under Ground", "Under Ground Acts 1, 2 and 3."},
    {0x86, "Sky High", "Sky High Acts 1, 2 and 3."},
    {0x81, "Aqua Lake", "Aqua Lake Acts 1, 2 and 3."},
    {0x85, "Green Hills", "Green Hills Acts 1, 2 and 3."},
    {0x83, "Gimmick Mt.", "Gimmick Mt. Acts 1, 2 and 3."},
    {0x84, "Crystal Egg", "Crystal Egg Acts 1 and 2."},
    {0x94, "Boss battle", "Boss fights and Crystal Egg Act 3."},
    {0x8B, "Invincibility", "The invincibility power-up."},
    {0x8C, "Act clear", "The act-clear jingle."},
    {0x8D, "Life lost", "The life-lost jingle."},
    {0x8F, "Chaos Emerald", "The emerald-collection jingle."},
    {0x8E, "Continue", "The continue screen's countdown."},
    {0x90, "Game over", "The game-over screen."},
    {0x91, "Ending", "The ending without all six emeralds."},
    {0x95, "Ending (all emeralds)", "The ending with all six emeralds."},
    {0x92, "Ending finale", "The short theme as the ending's last scene begins."},
}};

} // namespace sonic2_gg
