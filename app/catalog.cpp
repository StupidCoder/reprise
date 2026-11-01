#include "app/catalog.hpp"
#include <algorithm>
#include "platform/resources.hpp"
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
    // App-owned editorial articles also load from Android's packaged assets.
    struct Article { const char* id; const char* title; const char* sources; };
    constexpr Article articles[] = {
        {"captain_toad_3ds", "Small worlds, big discoveries",
         "Shinya Hiratake and Koichi Hayashida interviews: GameSpot and Nintendo Life (2014), Miiverse (2015), Polygon via Nintendo Everything (2018); Nintendo."},
        {"bruce_lee_c64", "A fortress, a rival and an Atari inheritance",
         "Electronic Games, November 1984; Retro Gamer 145, 2015; Reprise's C64 research."},
        {"stunt_car_racer_amiga", "The jump that became a racing game",
         "Geoff Crammond interview, The Making of Stunt Car Racer (archived by Retro Isle); Reprise's Amiga research."},
        {"fort_apocalypse_c64", "A helicopter beneath the surface",
         "Steve Hales interviews: Halcyon Days, AtariOnline (2009), Retro Gamer 44; Reprise's C64 research."},
        {"marble_madness_amiga", "A small marble, an elaborate world",
         "Mark Cerny interview, Retro Gamer 53; Electronic Arts' original packaging; Reprise's Amiga research."},
        {"mindwalker_amiga", "An unfamiliar machine, an inner world",
         "Bill Williams interview, YAAM, February 1989; Commodore's original manual; Reprise's Amiga research."},
        {"wizball_c64", "Painting a world into existence",
         "Jon Hare interviews: Talkin'Games and Retro Gamesmaster; Reprise's C64 research."},
        {"cannon_fodder_amiga", "Small soldiers, lasting losses",
         "Jon Hare interviewed by Gary Penn, Sensible Software 1986\u20131999; Jon Hare and Stoo Cambridge interviews, Arcade Attack; Reprise's Amiga research."},
        {"rock_n_roll_amiga", "A maze with momentum",
         "Contemporary reviews: Amiga Computing, Amiga Format and CU Amiga; published credits; Reprise's Amiga research."},
        {"tower_of_babel_amiga", "A puzzle box you can rebuild",
         "Amiga Computing, July 1990; Amiga Format, June 1990; CU Amiga, April 1990; Reprise's Amiga research."},
        {"wings_of_fury_amiga", "Relearning the flight of a Hellcat",
         "Broderbund's original user's guide; Colin Fox and John Boechler interviewed for Game Not Over, episode 72; Reprise's Amiga research."},
        {"fa_18_interceptor_amiga", "The arcade programmer who wanted to fly",
         "Bob Dinnerman interviewed by Tom Steinberg, 2004 (preserved transcription); Amiga Computing, July 1988; Reprise's Amiga research."},
        {"spindizzy_worlds_amiga", "Building worlds from a small vocabulary",
         "Paul Shirley interview, Halcyon Days; Amiga Format, January 1991; Reprise's Amiga research."},
        {"beach_volley_amiga", "A sporting cartoon from Ocean France",
         "Marc Djan interview, Codetapper; original credits and presentation; Reprise's Amiga research."},
        {"spy_mouse_ios", "From flight paths to a secret agent",
         "Rob Murray interviews: Gamezebo, Pocket Gamer and GameSpot (2011); Rich McBride portfolio; EA launch announcement; Reprise's iOS research."},
        {"sonic_gg", "A small Sonic starts a studio",
         "Yuzo Koshiro interviews: Red Bull Music Academy (2018), testimony reproduced by Film Stories (2021); Reprise's cartridge research."},
        {"sonic2_gg", "A sequel with a different destination",
         "Sega's original Game Gear manual (1992); Reprise's cartridge research."},
        {"triple_trouble_gg", "A new rival finds his personality",
         "Shinichi Higashi interviewed by Jack Yarwood, Time Extension (2026); Reprise's cartridge research."},
        {"sonic_advance_gba", "New hardware, familiar momentum",
         "Yuji Naka and Shigeru Miyamoto interview (2001), translated by shmuplations; Takashi Iizuka interview (2010); GameSpot review (2002); Reprise's cartridge research."},
        {"curious_village_ds", "A puzzle book opens onto a village",
         "Akihiro Hino interviews: GameSpot (2008), Iwata Asks (2011); Reprise's DS research."},
        {"pandoras_box_ds", "The journey that replaced a ghost island",
         "Akihiro Hino interviews: Wired and Kotaku (2009); Reprise's DS research."},
        {"lost_future_ds", "A familiar adventure with a new engine",
         "Akihiro Hino interview, Nintendo Life (2017); Nintendo's original game description; Reprise's DS research."},
        {"spectres_call_ds", "Beginning again beneath a familiar surface",
         "Nintendo's launch announcement (2011); Nintendo UK confirmation reported by Nintendo Insider; Reprise's DS research."},
        {"miracle_mask_3ds", "Rebuilding Layton for depth",
         "Akihiro Hino, Iwata Asks (2011); GameSpot review and Nintendo launch material (2012); Reprise's 3DS research."},
        {"azran_legacy_3ds", "A world tour to bring the journey home",
         "Nintendo launch announcement (2013); Nintendo World Report review (2014); Akihiro Hino interview, Nintendo Life (2017); Reprise's 3DS research."},
    };
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
        game.history_title = "From flight paths to a secret agent";
        game.history = "After Flight Control, Firemint explored line-drawing prototypes and developed one into a stealth adventure. Rob Murray described more than two years of work on Spy Mouse, with individually designed levels and a name chosen to make its premise immediately clear.";
        game.history_sources = "Rob Murray interview, Gamezebo, 2011.";
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
            {"A small Sonic starts a studio",
             "Ancient took shape around Sega's commission for an 8-bit Sonic. Yuzo Koshiro recalled considerable freedom to create new stages, and said the game's success helped lead to work on Streets of Rage 2.",
             "Yuzo Koshiro interviews: Red Bull Music Academy (2018), testimony reproduced by Film Stories (2021); Reprise's cartridge research."},
            {"A sequel with a different destination",
             "Aspect's Game Gear sequel has its own rescue story and a different engine from the first handheld Sonic. The cartridge is twice the size, and its technical lineage continues into Triple Trouble.",
             "Sega's original Game Gear manual (1992); Reprise's cartridge research."},
            {"A new rival finds his personality",
             "Artist Shinichi Higashi recalled revising Fang's appearance and weapon as the new rival became more comical. He credited Tadashi Ihoroi with the character's personality and backstory.",
             "Shinichi Higashi interviewed by Jack Yarwood, Time Extension (2026); Reprise's cartridge research."},
            {"New hardware, familiar momentum",
             "Sonic Advance began Sonic Team's collaboration with Dimps. Yuji Naka described the appeal of carefully tuned 2D play and the challenges of linking the handheld game to the GameCube.",
             "Yuji Naka and Shigeru Miyamoto interview (2001), translated by shmuplations; Takashi Iizuka interview (2010); GameSpot review (2002); Reprise's cartridge research."},
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
        {"curious_village_ds", "A puzzle book opens onto a village",
         "Akihiro Hino combined Akira Tago's puzzle-book tradition with a story-driven adventure designed to welcome players with little gaming experience. Overseas editions replaced puzzles that depended on Japanese language or customs.",
         "Akihiro Hino interviews: GameSpot (2008), Iwata Asks (2011); Reprise's DS research."},
        {"pandoras_box_ds", "The journey that replaced a ghost island",
         "Hino recalled replacing a planned ghost-island premise and working to connect puzzles more closely to the narrative. Beneath its familiar appearance, the sequel substantially reorganised the first game's engine.",
         "Akihiro Hino interviews: Wired and Kotaku (2009); Reprise's DS research."},
        {"lost_future_ds", "A familiar adventure with a new engine",
         "Hino later named the third adventure his favourite, citing its time-travel story. Improved hints and a more capable memo pad accompanied a new scripting system beneath the familiar DS presentation.",
         "Akihiro Hino interview, Nintendo Life (2017); Nintendo's original game description; Reprise's DS research."},
        {"spectres_call_ds", "Beginning again beneath a familiar surface",
         "The fourth adventure tells how Layton met Luke and introduces Emmy. Its familiar hand-drawn presentation runs on a new engine design; the European edition omitted the separate London Life bonus found in Japan and North America.",
         "Nintendo's launch announcement (2011); Nintendo UK confirmation reported by Nintendo Insider; Reprise's DS research."},
        {"miracle_mask_3ds", "Rebuilding Layton for depth",
         "Hino described substantially reworking the game after E3 2010 to demonstrate the 3DS hardware. New exploration and rendering techniques preserved the series' illustrated identity while adding depth.",
         "Akihiro Hino, Iwata Asks (2011); GameSpot review and Nintendo launch material (2012); Reprise's 3DS research."},
        {"azran_legacy_3ds", "A world tour to bring the journey home",
         "Originally presented as the conclusion of the prequel arc, Azran Legacy expanded the journey into a world tour. It developed Miracle Mask's engine and added new diversions and StreetPass treasure hunts.",
         "Nintendo launch announcement (2013); Nintendo World Report review (2014); Akihiro Hino interview, Nintendo Life (2017); Reprise's 3DS research."},
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
    // Reserved identity for the upcoming Diorama integration. Keep this ID and cover
    // when replacing the placeholder with the connected module's entry.
    {
        GameEntry game;
        game.id = "captain_toad_3ds";
        game.title = "Captain Toad: Treasure Tracker";
        game.machine = Machine::nintendo_3ds;
        game.release_year = 2018;
        game.description = "Tiny worlds hide big adventures. Guide Captain Toad through miniature gardens, towers and twisting paths, looking around every corner for hidden treasure and a route to the star. With a heavy backpack and no jump to rely on, finding the way is a puzzle of its own.";
        game.collection = Collection::none;
        game.integration_pending = true;
        game.progress = "Coming to Reprise";
        game.history_title = "Small worlds, big discoveries";
        game.history = "Nintendo built the miniature-stage concept around a character who could not jump. Captain Toad's backpack made him a natural fit, and the 3DS adaptation later pursued the appeal of viewing these tiny worlds in stereoscopic depth.";
        game.history_sources = "Hiratake and Hayashida interviews: Nintendo Life (2014), Polygon via Nintendo Everything (2018).";
        games.push_back(std::move(game));
    }
    // Apply articles after every collection and standalone game has been added.
    for (auto& game : games) for (const auto& article : articles) {
        if (game.id != article.id) continue;
        try {
            const auto bytes = platform::resource("articles/" + game.id + ".txt");
            game.history.assign(bytes.begin(), bytes.end());
            game.history_title = article.title;
            game.history_sources = std::string(article.sources) +
                " References and evidence limits: docs/articles/sources.md.";
        } catch (const std::exception&) {
            // A missing optional article retains the catalog's short sourced note.
        }
        break;
    }
    // Declarative capabilities: opening details must never construct engines or read imports.
    // Keep this audit beside the catalog until each module owns its declaration.
    using F = GameFeature;
    for (auto& game : games) {
        const auto& id = game.id;
        const auto is = [&](std::initializer_list<const char*> ids) {
            return std::any_of(ids.begin(), ids.end(), [&](const char* name) { return id == name; });
        };
        if (is({"fort_apocalypse_c64", "bruce_lee_c64", "wizball_c64", "cannon_fodder_amiga",
                "beach_volley_amiga", "wings_of_fury_amiga", "rock_n_roll_amiga",
                "spindizzy_worlds_amiga", "fa_18_interceptor_amiga", "tower_of_babel_amiga"}))
            game.features = {F::widescreen, F::high_framerate};
        if (is({"sonic_gg", "sonic2_gg", "triple_trouble_gg"})) game.features = {F::widescreen};
        if (game.collection == Collection::layton || is({"fort_apocalypse_c64", "bruce_lee_c64", "wizball_c64", "cannon_fodder_amiga",
                "beach_volley_amiga", "wings_of_fury_amiga", "rock_n_roll_amiga",
                "spindizzy_worlds_amiga", "marble_madness_amiga", "mindwalker_amiga",
                "sonic_gg", "sonic2_gg", "triple_trouble_gg"}))
            game.features.push_back(F::cheats); // Includes level selection and Layton's hidden touch-area hints.
        if (is({"fort_apocalypse_c64", "bruce_lee_c64", "wizball_c64", "marble_madness_amiga"}))
            game.features.push_back(F::extra_modes); // Carnage / Crowd race, not original modes.
        if (game.collection == Collection::layton ||
            is({"spy_mouse_ios", "fa_18_interceptor_amiga", "stunt_car_racer_amiga"}))
            game.features.push_back(F::touch);
        // No module currently exposes an actual motion-sensor control scheme.
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
