#include "app/catalog.hpp"
#include <algorithm>
#include "fa_18_interceptor_amiga/entry.hpp"
#include "spindizzy_worlds_amiga/entry.hpp"
#include "beach_volley_amiga/entry.hpp"
#include "stunt_car_racer_amiga/entry.hpp"
#include "spy_mouse_ios/reprise/entry.hpp"
#include "sonic/reprise/entry.hpp"
#include "layton/reprise/entry.hpp"
#include "bruce_lee_c64/entry.hpp"
#include "cannon_fodder_amiga/entry.hpp"
#include "fort_apocalypse_c64/entry.hpp"
#include "marble_madness_amiga/entry.hpp"
#include "mindwalker_amiga/entry.hpp"
#include "rock_n_roll_amiga/entry.hpp"
#include "tower_of_babel_amiga/entry.hpp"
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
        fodder::entry(),
        rocknroll::entry(),
        babel::entry(),
        wings::entry(),
        interceptor::entry(),
        spindizzy::entry(),
        beach_volley::entry(),
        stuntcar::entry(),
    };
    // Platform-specific release dates, not franchise debuts or reissue dates.
    // This editorial material is sourced separately in docs/catalog-history.md.
    struct Editorial { const char* id; int year; const char* description;
        const char* heading; const char* history; const char* sources; };
    const Editorial notes[] = {
        {"beach_volley_amiga", 1989,
         "Sun, sand and a net between you and victory. Serve, block and smash your way through a beach-volleyball tournament. Read the rally, cover the court and turn a perfect set into the winning shot.",
         "Ocean's French team takes to the beach",
         "Ocean France developed Beach Volley for Ocean. The Amiga credits name Marc Djan as designer and Michel Janicki as programmer, with graphics by Michèle Bacqué, Philippe Dessoly and Pierre-Eric Loriaux, and music by Jean Baudlot.",
         "Lemon Amiga: 1989 Amiga release and credits; original manual: rules."},
        {"stunt_car_racer_amiga", 1989,
         "Strap into a racing buggy on a track suspended high above the ground. Charge up ramps, leap across gaps and keep your wheels on the narrow course. Speed wins races, but a reckless landing can send your championship hopes over the edge.",
         "A racing circuit becomes a rollercoaster",
         "Geoff Crammond designed and programmed Stunt Car Racer, published by MicroStyle in 1989, with graphics by John Cumming. Its elevated circuits turned corners and jumps into a balancing act. The Amiga version also supported racing another player over a serial link.",
         "Lemon Amiga: release, credits and serial-link support; OpenRetro: overview."},
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
    for (auto& game : games) game.collection = Collection::commodore;
    // Deliberately non-playable previews until the source projects hand off.
    struct Incoming { const char* id; const char* title; Machine machine;
        Collection collection; const char* description; };
    const Incoming incoming[] = {
        {"curious_village_ds", "Curious Village", Machine::nintendo_ds, Collection::layton, "Explore an extraordinary village where every conversation might conceal a puzzle, and every clue brings its central mystery a little closer."},
        {"pandoras_box_ds", "Pandora's Box", Machine::nintendo_ds, Collection::layton, "Board a mysterious train and follow the trail of a peculiar box. A journey of puzzles leads far beyond the next station."},
        {"lost_future_ds", "Lost Future", Machine::nintendo_ds, Collection::layton, "A letter from the future leads to an unfamiliar London. Investigate its streets, solve its puzzles and untangle a mystery of time."},
        {"spectres_call_ds", "Spectre's Call", Machine::nintendo_ds, Collection::layton, "Discover a canal town troubled by a towering spectre. Follow the clues and learn how a celebrated puzzle-solving partnership began."},
        {"miracle_mask_3ds", "Miracle Mask", Machine::nintendo_3ds, Collection::layton, "A desert city of festivities hides a mystery behind a mask. Explore a collection of puzzles and curious challenges."},
        {"azran_legacy_3ds", "Azran Legacy", Machine::nintendo_3ds, Collection::layton, "An ancient civilisation leaves a trail across the world. A journey of discoveries and puzzles awaits."},
    };
    // Spy Mouse: connected (games/spy_mouse_ios/reprise); no collection, it belongs to All games.
    {
        auto game = spy_mouse_reprise::entry();
        game.release_year = 2011;
        game.description = "Draw a route, gather the cheese and slip past the cats. A steady hand and a clever escape plan can make all the difference.";
        game.history_title = "From Agent Squeek to Spy Mouse";
        game.history = "Firemint, the Australian studio behind Flight Control, first showed the game as an unnamed prototype at GDC 2010 and presented it as Agent Squeek in early 2011. After EA bought Firemint, a marketing revamp renamed it Spy Mouse shortly before its August 2011 release, carrying Flight Control's line drawing into a stealth game.";
        game.history_sources = "Engadget, March and July 2011; Wikipedia: Spy Mouse (release date).";
        games.push_back(std::move(game));
    }
    // BlueStreak's games (games/sonic/reprise), in the Sonic collection's order. The card texts
    // are the collection's own.
    {
        const char* descriptions[] = {
            "Run, leap and search for the Chaos Emeralds through a world of rolling hills, bridges and mechanical hazards.",
            "Race through mines, soaring skies and dangerous machinery on a quest to rescue Tails.",
            "Speed through colourful landscapes with Sonic and Tails, exploring branching routes and hunting for hidden emeralds.",
            "Race through colourful landscapes, gather rings and take on giant machines. Familiar speed and exploration find a new home on a handheld screen.",
        };
        // First release on each platform (docs/catalog-history.md).
        const int years[] = {1991, 1992, 1994, 2001};
        struct History { const char* heading; const char* text; const char* sources; };
        const History history[] = {
            {"A studio's first game",
             "The 8-bit Sonic was the first game of Ancient, the studio composer Yuzo Koshiro had founded, and the only Sonic game it made. It was built for the Game Gear, and Sega also had Ancient make a Master System version.",
             "Wikipedia: Sonic the Hedgehog (8-bit) and Ancient (company)."},
            {"A bigger cartridge for a bigger adventure",
             "Aspect developed this 8-bit sequel separately from the Mega Drive game, with its own levels, music and story of rescuing Tails. It was the first four-megabit Game Gear cartridge, the largest the handheld had seen.",
             "Wikipedia: Sonic the Hedgehog 2 (8-bit video game)."},
            {"A new rival in a handheld sequel",
             "Aspect followed Sonic Chaos with Triple Trouble, a Game Gear exclusive. It introduced the treasure hunter known as Fang the Sniper in Japan and Nack the Weasel abroad; early promotional pictures showed him with a gun that the finished game left out.",
             "Wikipedia: Sonic the Hedgehog: Triple Trouble; List of Sonic the Hedgehog characters."},
            {"Sonic on a Nintendo handheld",
             "Sonic Advance was the first Sonic game by Dimps, made with Sonic Team after Sega left the hardware business. It reached Japan in December 2001, around the same time as Sonic Adventure 2: Battle on the GameCube, among the first Sonic games on Nintendo machines.",
             "Wikipedia: Sonic Advance (and its talk page on the shared first-release date)."},
        };
        auto sonic = sonic_reprise::entries();
        for (std::size_t i = 0; i < sonic.size(); ++i) {
            sonic[i].description = descriptions[i];
            sonic[i].release_year = years[i];
            sonic[i].history_title = history[i].heading;
            sonic[i].history = history[i].text;
            sonic[i].history_sources = history[i].sources;
            games.push_back(std::move(sonic[i]));
        }
    }
    // The puzzle adventures are connected (games/layton/reprise): each card keeps its title,
    // text, year and collection from the list above.
    const auto layton = layton_reprise::entries();
    // Editorial history of the connected adventures (docs/catalog-history.md; never evidence).
    struct LaytonHistory { const char* id; const char* heading; const char* text; const char* sources; };
    const LaytonHistory layton_history[] = {
        {"curious_village_ds", "A puzzle book becomes a game",
         "Level-5's president Akihiro Hino had grown up with Akira Tago's best-selling puzzle books. The studio started a prototype in 2006 and brought Tago, a psychology professor, in as the puzzle master; his team and the developers worked through the puzzles together at what Level-5 called puzzle camps. The game reached Japan in February 2007.",
         "Wikipedia: Professor Layton and the Curious Village (development, credits)."},
        {"pandoras_box_ds", "The sequel came the same year",
         "Level-5 released the second adventure in Japan in November 2007, nine months after the first. Its downloadable weekly puzzles ran over Nintendo Wi-Fi Connection, and became unavailable when that service closed in May 2014.",
         "Wikipedia: Professor Layton and the Diabolical Box."},
        {"lost_future_ds", "The end of the first trilogy",
         "Released in Japan in November 2008, the third adventure closed the first trilogy. Its Japanese title translates roughly as \"the final time travel\"; it was released in Europe as Lost Future and in North America as Unwound Future in 2010.",
         "Wikipedia: Professor Layton and the Unwound Future."},
        {"spectres_call_ds", "Going back to the beginning",
         "The fourth adventure, released in Japan in November 2009, began a prequel trilogy set before the first game, telling how the professor met his young apprentice. It arrived in Europe two years later, in November 2011, as Spectre's Call.",
         "Wikipedia: Professor Layton and the Last Specter (release dates)."},
        {"miracle_mask_3ds", "A launch title for a new handheld",
         "Level-5 released the fifth adventure in Japan on 26 February 2011, the day the Nintendo 3DS launched there. Western players received it in October 2012, and Japan later got an enhanced download edition with new puzzles.",
         "Wikipedia: Professor Layton and the Miracle Mask; Nintendo World Report preview."},
        {"azran_legacy_3ds", "The professor's last leading role",
         "Released in Japan in February 2013, the sixth adventure completed the prequel trilogy. Level-5's Akihiro Hino said it would be the last game with the professor as its main character; Europe received it in November 2013.",
         "Wikipedia: Professor Layton and the Azran Legacy; Giant Bomb release list."},
    };
    for (const auto& item : incoming) {
        GameEntry game;
        const auto connected = std::find_if(layton.begin(), layton.end(),
                                            [&](const GameEntry& e) { return e.id == item.id; });
        if (connected != layton.end()) game = *connected;
        game.id = item.id; game.title = item.title; game.machine = item.machine;
        game.collection = item.collection;
        if (item.collection == Collection::layton) {
            const int years[] = {2007, 2007, 2008, 2009, 2011, 2013};
            game.release_year = years[&item - incoming];
        }
        // Release metadata comes with the source-project handoff.
        game.description = item.description;
        for (const auto& h : layton_history) {
            if (game.id == h.id) {
                game.history_title = h.heading;
                game.history = h.text;
                game.history_sources = h.sources;
            }
        }
        if (connected == layton.end()) {
            game.integration_pending = true;
            game.progress = "Coming to Reprise";
        }
        games.push_back(std::move(game));
    }
    return games;
}

const char* machine_name(Machine machine) {
    switch (machine) {
    case Machine::c64: return "Commodore 64";
    case Machine::amiga: return "Amiga";
    case Machine::game_gear: return "Game Gear";
    case Machine::nintendo_ds: return "Nintendo DS";
    case Machine::nintendo_3ds: return "Nintendo 3DS";
    case Machine::ios: return "iOS";
    case Machine::game_boy_advance: return "Game Boy Advance";
    }
    return "";
}

} // namespace app
