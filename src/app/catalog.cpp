#include "app/catalog.hpp"
#include "bruce_lee_c64/entry.hpp"
#include "fort_apocalypse_c64/entry.hpp"
#include "marble_madness_amiga/entry.hpp"
#include "mindwalker_amiga/entry.hpp"
#include "wings_of_fury_amiga/entry.hpp"
#include "wizball_c64/entry.hpp"

namespace app {

std::vector<GameEntry> catalog() {
    // Planned games have no importer yet; they show what the collection is
    // heading for.
    std::vector<GameEntry> games{
        fort_apocalypse::entry(),
        bruce_lee::entry(),
        wizball::entry(),
        mindwalker::entry(),
        marble_madness::entry(),
        {.id = "cannon_fodder_amiga", .title = "Cannon Fodder", .machine = Machine::amiga,
         .media = "the disk images (ADF)"},
        {.id = "rock_n_roll_amiga", .title = "Rock 'n Roll", .machine = Machine::amiga,
         .media = "the disk image (ADF)"},
        {.id = "tower_of_babel_amiga", .title = "Tower of Babel", .machine = Machine::amiga,
         .media = "the disk image (ADF)"},
        wings::entry(),
    };
    // Platform-specific release dates, not franchise debuts or reissue dates.
    // This editorial material is sourced separately in docs/catalog-history.md.
    struct Editorial { const char* id; int year; const char* description;
        const char* heading; const char* history; const char* sources; };
    const Editorial notes[] = {
        {"fort_apocalypse_c64", 1983,
         "Take a helicopter deep into an underground fortress. Blast a path through its defenses, rescue prisoners and keep an eye on your fuel. Getting in is dangerous; getting everyone back out is the real mission.",
         "An Atari original, a C64 conversion",
         "Steve Hales created Fort Apocalypse for Atari computers in 1982. Joe Vierra handled the C64 conversion. Its dates can be confusing: the C64 release is catalogued as 1983, while the later US Gold tape carries a 1984 copyright notice.",
         "Lemon64: release and conversion credits; MobyGames: Steve Hales credits."},
        {"bruce_lee_c64", 1984,
         "Enter a fortress of lanterns, ladders and deadly surprises. Guide Bruce through its chambers, collect the lanterns and evade the Ninja and Green Yamo. Quick feet and a well-timed kick are your best chance of reaching the mysterious wizard within.",
         "An enemy becomes a second player",
         "In a 1984 interview, artist Kelly Day recalled Ron Fortier experimenting with joystick control of Green Yamo. That experiment became the two-player twist: between turns as Bruce, the other player could control his adversary. A playful development experiment made the rivalry personal.",
         "Electronic Games, November 1984: Fortier and Day interview; MobyGames: C64 credits."},
        {"wizball_c64", 1987,
         "Color has drained from Wizworld. Bounce into its strange landscapes, battle the invaders and gather paint with your feline companion, Catellite. Build up your powers and bring the scenery back to life, one splash of color at a time.",
         "A musical partnership makes a game",
         "Jon Hare and Chris Yates had been writing songs together since they were teenagers. Hare later described Wizball as another expression of that creative partnership: arcade shooting met the surreal idea of a wizard in a ball, restoring color to a landscape.",
         "Antstream: Jon Hare interview; Lemon64: C64 release and credits."},
        {"mindwalker_amiga", 1986,
         "Step inside a fractured mind. Explore a surreal landscape, change forms and piece together a path toward recovery. Action, navigation and puzzles become parts of the same strange journey, where understanding the world matters as much as surviving it.",
         "A big imagination in 256 KB",
         "Bill Williams told YAAM that fitting Mind Walker into the original Amiga's 256 KB was a struggle. He doubted that configuration would last, but still had to support it. He also remembered the game as one of Synapse's last projects, licensed to Commodore.",
         "YAAM, February 1989: Bill Williams interview; Commodore's 1986 manual."},
        {"marble_madness_amiga", 1986,
         "A marble, a maze and a ticking clock. Race down impossible slopes, thread narrow paths and dodge hazards in a world where momentum is both your ally and your worst enemy. Every careful correction can save a run; every wild turn can send it over the edge.",
         "From the arcade to the Amiga",
         "Electronic Arts brought Atari's arcade game home with an Amiga version by Larry Reed. The original packaging credits Reed separately from the other computer conversions, alongside artwork by Ian Gooding and sound consulting by Jim Nitchals: a reminder that ports had their own creative teams.",
         "Electronic Arts: original packaging; MobyGames: Amiga credits (1986)."},
        {"cannon_fodder_amiga", 1993,
         "Lead a small squad through hostile territory with a few clicks and a lot of care. Split your team, steer around danger and bring your soldiers home. Behind the cartoon battlefield, every recruit has a name, and every loss leaves a mark.",
         "The team's strangest day out",
         "Jon Hare recalled hiring a half-track and military uniforms for the game's promotional music video. Armed with plastic guns, the team spent a day filming silly sketches in an English poppy field. It was, he said, one of the most enjoyable days of the project.",
         "Pixel Ninjas: Jon Hare interview, 2022, question 13 and release discussion."},
        {"rock_n_roll_amiga", 1989,
         "Roll a ball through a maze that refuses to play fair. Find keys, dodge holes and contend with ice, fans and magnets on the way to the exit. A steady hand helps, but a good route and a little forward planning matter just as much.",
         "A title with its own soundtrack",
         "The name plays on both rolling a ball and the rock-and-roll music accompanying it. The Amiga credits pair programmer Frank Prasse and designer Martin Gaksch with musician Chris Hülsbeck. Its mouse controls give this edition a different feel from the joystick-driven C64 version.",
         "Lemon Amiga: release and credits; Rock 'n' Roll reference article: controls and title."},
        {"tower_of_babel_amiga", 1990,
         "Three robot spiders. One tower full of problems. Coordinate the Pusher, Zapper and Grabber, each with a different job, to overcome traps and complete your objectives. Plan their actions together or take direct control when your carefully laid scheme needs a helping hand.",
         "Build a tower of your own",
         "Pete Cooke's puzzle game reached the Atari ST in 1989 and the Amiga in 1990. It also shipped with a tower designer. CU Amiga's April 1990 review highlighted the freedom to rearrange objects, change the palette and build puzzles across four floors.",
         "MobyGames: platform releases; CU Amiga, April 1990: review; Computing History: overview."},
        {"wings_of_fury_amiga", 1990,
         "Launch from your carrier and take the battle to the islands below. Make attack runs, watch your fuel and ammunition, then bring your aircraft safely back to the deck. The shooting is only half the challenge: you still have to land.",
         "An Apple II game takes flight again",
         "Steve Waldo's original appeared on the Apple II in 1987. Unlimited Software brought it to the Amiga in 1990, with programming by Colin Fox, graphics by John Boechler and music by Kris Hatlelid. This is that later conversion's place in the timeline.",
         "MobyGames: platform releases; GameFAQs: platform credits; Wings of Fury reference article."},
    };
    for (auto& game : games) for (const auto& note : notes) if (game.id == note.id) {
        game.release_year = note.year;
        game.description = note.description;
        game.history_title = note.heading;
        game.history = note.history;
        game.history_sources = note.sources;
        break;
    }
    return games;
}

const char* machine_name(Machine machine) {
    return machine == Machine::c64 ? "C64" : "Amiga";
}

} // namespace app
