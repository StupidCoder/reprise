#pragma once
#include <array>

// Names the app shows for Sonic 1 (Game Gear): the level selector's start
// points and the sound test's tracks. These are descriptive labels written for
// BlueStreak, based on where each act or track is used in the game.
namespace sonic1_gg {

// A start point offered by the level selector. `id` is the game's act number:
// 0-17 are the six zones' three acts each, 28-35 the eight special stages.
// Linked rooms (acts 18-26) are reached through their owning act instead.
// 100 and 101 are not acts: the finale after the escape (area 18) into the
// ending, without and with all six emeralds.
struct LevelEntry {
    unsigned id;
    const char* world;
    const char* label;
};

inline constexpr unsigned ending_start = 100, good_ending_start = 101;

inline constexpr std::array<LevelEntry, 28> levels{{
    {0, "Green Hill", "Act 1"},
    {1, "Green Hill", "Act 2"},
    {2, "Green Hill", "Act 3"},
    {3, "Bridge", "Act 1"},
    {4, "Bridge", "Act 2"},
    {5, "Bridge", "Act 3"},
    {6, "Jungle", "Act 1"},
    {7, "Jungle", "Act 2"},
    {8, "Jungle", "Act 3"},
    {9, "Labyrinth", "Act 1"},
    {10, "Labyrinth", "Act 2"},
    {11, "Labyrinth", "Act 3"},
    {12, "Scrap Brain", "Act 1"},
    {13, "Scrap Brain", "Act 2"},
    {14, "Scrap Brain", "Act 3"},
    {15, "Sky Base", "Act 1"},
    {16, "Sky Base", "Act 2"},
    {17, "Sky Base", "Act 3"},
    {28, "Special stage", "1"},
    {29, "Special stage", "2"},
    {30, "Special stage", "3"},
    {31, "Special stage", "4"},
    {32, "Special stage", "5"},
    {33, "Special stage", "6"},
    {34, "Special stage", "7"},
    {35, "Special stage", "8"},
    {ending_start, "Ending", "Standard"},
    {good_ending_start, "Ending", "All emeralds"},
}};

// A sound test entry. `music_id` is the sound driver's music number; -1 plays
// the sampled "SEGA" voice from the logo screen instead.
struct MusicTrack {
    int music_id;
    const char* title;
    const char* description;
};

inline constexpr std::array<MusicTrack, 17> music_tracks{{
    {-1, "SEGA voice", "Sampled voice from the original logo."},
    {6, "Title screen", "The original title screen."},
    {7, "World map", "The route shown before an act."},
    {0, "Green Hill", "Green Hill Acts 1, 2 and 3."},
    {1, "Bridge", "Bridge Acts 1, 2 and 3."},
    {2, "Jungle", "Jungle Acts 1, 2 and 3."},
    {3, "Labyrinth", "Labyrinth Acts 1, 2 and 3."},
    {4, "Scrap Brain / Sky Base", "Scrap Brain, Sky Base Acts 1 and 3, and the final escape."},
    {5, "Sky Base Act 2", "Sky Base Act 2."},
    {11, "Boss battle", "Boss fights, including the final boss."},
    {16, "Special stages", "All eight special stages."},
    {8, "Invincibility", "The invincibility power-up."},
    {9, "Act clear", "The act-clear jingle."},
    {10, "Life lost", "The life-lost jingle."},
    {20, "Chaos Emerald", "The emerald-collection jingle."},
    {19, "Ending", "The emerald flight in the ending."},
    {14, "Credits", "The closing credits."},
}};

} // namespace sonic1_gg
