# Joyport

A growing collection of classic Commodore 64 and Amiga games, rebuilt as
native C++ programs. Joyport contains no game data: you import your own tape
or disk image, and Joyport reads the graphics, levels and sound from it while
running its own re-implementation of the game, with no emulator.

Game names are used only to describe which data Joyport can read. Joyport is
not affiliated with or endorsed by the games' publishers.

## Status (version 0.2.0)

| Game | Machine | State |
| --- | --- | --- |
| Fort Apocalypse | C64 | Playable from the title to the debrief; compared frame by frame with the original in scripted runs |
| Bruce Lee | C64 | Playable from the title through all 20 rooms to the end, with sound, the demo and both two-player modes; compared pass by pass with the original in scripted runs |
| Mindwalker | Amiga | Planned |
| Marble Madness | Amiga | All six courses complete in play: the original's marble physics, ground, course scripts, wall covers, bonuses, steelies, slinkies, acid, vacuums, pistons, critters, birds, Ultimate's moving goal area, music and sound effects (each compared update for update with the original from start to goal); the front end for one player: options screen, course introductions, finish bonus, game over and the end; the original's topaz font; quick save and load; a crowd race (seven computer-steered marbles, pause menu); not yet: two players |
| Wizball | C64 | Whole game rebuilt from the title: menu, wiztips, play through levels, bonus waves, the Wizlab and round ends (compared pass by pass with the original), game over, high scores, pause and volume keys; widescreen option |
| Cannon Fodder | Amiga | Whole game playable from the presentation to the ending: all 72 phases with vehicles, the campaign (recruits, ranks, heroes, hall of fame), the hill, the parallax, the briefing, the debrief, the load screen and the map overview, compared with the original; persistent quick saves; widescreen, smooth motion and VHS options; Control, Option or Command as the right mouse button |
| Rock 'n Roll | Amiga | Planned |
| Wings of Fury | Amiga | Planned |

See [PLAN.md](PLAN.md) for the roadmap.

## Building

You need CMake 3.24 or later, Ninja, a C++20 compiler and SDL 3.4 or later
(`brew install sdl3 cmake ninja`).

```bash
./build.sh
```

```bash
open build/Joyport.app
```

Or double-click `Play Joyport.command`, which builds on first use. Imported
content is stored in
`~/Library/Application Support/NativeReconstruction/Joyport/`.

## Collector's shelf interface

The library uses original screenprinted cover illustrations, a warm grey palette,
and bundled Lato typography. Everything in the shell, details and pause/options
screens is drawn through SDL; no native platform UI controls or system fonts are
used for these screens. `stb_truetype` rasterizes the bundled fonts portably. SDL
loads the covers and fonts through its resource IO, including Android assets.
Android packaging and device testing remain future work.

Use the sidebar to filter All games, Commodore 64, Amiga or Planned. Arrow keys
navigate the cards; Left from the first column enters the filters and Right
returns to the cards. Scroll, use Page Up/Down (gamepad shoulders), or click the
page arrows to reach the rest of the library. Enter or clicking a card opens its details, including planned titles. Start and Continue
sit beside the game description and saved screenshot. Documentation opens the asset browser with sidebar links to development notes
and available lessons or How to play. Within assets, Left/Right changes the page and Up/Down scrolls files.
Delete game files removes the import while keeping saved progress.

Pause and options use the same visual style. Long option lists scroll with the
selection, mouse wheel, Page Up/Down or the on-screen Up/Down buttons. Read more
(or I) displays a setting's full description; Esc returns. Game visuals and
original in-game title screens remain the game's own.

Interface assets and their licences/provenance are in `resources/`. The bundle
includes those assets; source-tree files are not needed to launch it. Fonts:
[Lato](https://github.com/google/fonts/tree/main/ofl/lato), SIL Open Font License;
renderer: [stb_truetype](https://github.com/nothings/stb), MIT/public domain.

## Playing

Select a game and press Enter. Joystick: arrow keys or WASD, fire with Space
or Z, or a gamepad. C64 keys: F1/F3/F5/F7 (or 1/3/5/7), P for the C64 space
bar.

Esc (or the gamepad's Back/Guide button) opens the pause menu: resume,
restart, quick save and load (also Cmd+S and Cmd+L), sound on/off (also M),
the game's options and the way back to the launcher. Quick-save/load shortcuts
show a brief confirmation over gameplay without opening the pause menu. The options you choose
are kept for each game (in `options/` in the data folder) and set again the
next time you start it.

Fort Apocalypse, Bruce Lee, Wizball, Marble Madness, Mind Walker and Wings of Fury
keep quick saves on disk,
with a screenshot and the settings used when saving. Continue in the details
screen restores that moment after closing and reopening Joyport. Start begins
fresh but preserves the old save until you explicitly save again. Saves live in
`saves/` beside `games/` in the data folder, with the preceding save retained as
a recovery copy. A save must match the imported release and save-format version;
incompatible or damaged progress is rejected without replacing a running game.

The pause menu reports success, session-only storage or failure explicitly.

### Cannon Fodder options and controls

- **Widescreen** — shows more of the map on both sides, up to 21:9; the
  panel stays at the left edge, the map's margins can be clicked (walk,
  fire, throw), and near a map's edge the view slides so the map fills it.
  The game plays the same: the camera and the scrolling are the original's.
- **Smooth motion** — soldiers, vehicles, shots and the scrolling map glide
  at the display's own rate instead of stepping about 17 times a second.
  Drawing only: the game plays the same.
- **VHS look** — a worn video tape's colour bleed and noise.
- **Controls** — the original's: click to walk, hold the right button to
  fire, both buttons to throw a grenade or rocket. In addition, for
  one-button trackpads: hold Command (or Control) to fire, and click while
  holding it to walk there, the squad firing on (it never throws); hold
  Option to throw.
- **Cheats** (off by default):
  - *Top rank* — the first recruit on the roster is a General, with a
    General's faster, further-reaching shots. This is the original's own
    cheat (a saved game named JOOLS).
  - *Bullet proof* — enemy bullets and the enemy's grenades, rockets,
    missiles and shells do the recruits no harm; mines, traps, water and
    their own grenades still do.
  - *Infinite ammo* — grenades and rockets never run out.

### Fort Apocalypse options

- **Enemy pilot** — *Original* keeps the famous bug: a stray byte of text
  overlaps the first instruction of the enemy helicopter's climb routine, so
  it checks the wrong spot before climbing and often flies into rock.
  *Fixed* makes it check its own path, and it also crashes on rock that
  the original lets it fly through: the C64's collision hardware counts one
  of the four multicolour pixel kinds as background, and most of the rock
  (level 1's and 3's brown) is drawn in it.
- **Particle effects** — debris and fire on the C64's pixel grid and in its 16
  colours: shots, the tanks' shells and the enemy's shots throw debris where
  they hit; the tanks' missiles trail smoke and sparks; destroyed tanks burst and toss their turret, which tumbles and
  bounces off the landscape; shot-down mines drop in an arc, turning, and
  blow up on the ground; a shot-down helicopter, yours or the enemy's, keeps
  its flashing but bursts apart, and its wreck bounces off what it rammed
  with what way it had left, then falls slower than the small debris, as
  the big machine it is. On its next hard impact it bursts into tumbling
  chunks of itself that bounce like a tank's turret (if it comes down too
  gently, it bursts where it settles), so no intact helicopter is left on
  the ground. When yours
  crashes, the playfield flashes only for a short shock (8 frames), then
  stays black so the wreck can be followed.
  Before your next helicopter comes, the game waits for that (about a
  second after the wreck stops, six seconds at most; fire or the space bar
  goes on at once), while tanks, mines, shells, the enemy and the men carry
  on: from the crash's first frame a copy of the game is shown, its camera
  following the wreck (by the game's own scrolling) until it bursts, and is
  dropped at "GET READY". The HUD stays the real game's. Wrecks
  and debris hit only the landscape, not tanks, mines or men, and a wreck
  that starts inside rock drops out of it.
- **Smooth motion** — the helicopter banks smoothly (its level sprite turned
  instead of the stepped tilt shapes, the game's bank steps eased into one
  turn) and leans into the way it moves while it still faces the screen, tanks glide from cell to cell and rock
  over the rough ground, mines and shells glide instead of jumping (mines
  wobbling as they fly), the men run smoothly, the fuel depots and the
  landing pad sink smoothly, and tanks that come back rise out of the ground
  at their home (with particle effects, throwing up the rock).
- **Widescreen** — the picture fills the window to its edges, without the
  C64's border, and the playfield shows more of the map left and right, up
  to 21:9 (as in BlueStreak's Game Gear games). The HUD stays in the middle.
  Shots that fly past the original window's left edge hit what the wider
  view shows them cross; in the original they test cells far to their right
  there (an unsigned subtraction), which no one could see.
- **VHS tape** — the picture as from a worn video tape on an old TV, in four
  steps (its faults are the tape's lines, two to each of the C64's):
  *Light* (soft, bleeding colour, a little line wobble, the tracking
  band and head-switching noise at the bottom, grain, scanlines), *Medium*
  (more of it, dropouts, the odd tear), *Heavy* and *Broken* (tears full of
  static, the colour fading out for moments, the vertical hold slipping).
  A shared filter (`src/ui/vhs.cpp`) for the other games too.

All of these change only the picture (and widescreen the one shot rule
above): the game plays exactly the same.

**Cheats** (a page of their own in the options; not in the attract game):
*Infinite lives* (a crash costs no pilot), *Infinite fuel* (the tank stays
full), *No laser barriers* (the
flashing barriers and the laser grids stay dark; level 1's cavern barriers
stay, they are its teleport), *Enemies don't fire* (no tank missiles, no
enemy helicopter shots) and *Slower moving walls* (the sweeping walls move
half as fast).

**Carnage mode** (its own page in the options; not the original game, not
in the attract game): hold fire for a rapid-fire gun. Its tracers (which end at the edges of
the view, as the original's shots do) carve the
brown rock pixel by pixel in a spray of debris and sparks; a carved cell
with only a few pixels left crumbles away. What you carve is real: the game
draws and tests carved cells as they are, so tunnels and pits can be flown
into. Tracers destroy tanks, mines and the breakable rock as shots do, and
can shoot down the enemy helicopter; the men are spared. Switched off
mid-game, the original rules carry on in the carved world (the next level
is built new). Things come loose: rock you cut free (a piece that holds on
to nothing fixed) falls as a chunk and shatters where it lands; a tank
whose ground you shoot away tumbles down and blows up where it lands
(scoring as a kill, blasting a crater); six hits (or a blast) shake a
laser barrier's emitter off its frame: it tumbles down still firing, its
beam sweeping all round, melting lines into the rock and burning what it
meets (another emitter, a tank, you). The
ladder towers rock when you hit them (the higher, the harder); shoot the
rock away beside one and keep hitting it high, and it tips over into the
gap or down to the level below, where it stays as part of the place. Keep
shooting a fuel depot and it smokes, catches fire, pops and blazes, and at
last goes up in a fireball that blasts a crater (and you, if you are too
close); there is no refuelling there any more. On the last level that
depot is where the mission ends: blow it up and the game tells you what it
thinks of that, and ends. What falls on your
helicopter crashes it. Kills in quick succession build a combo ("CARNAGE X7", with a bonus that
grows with it) and fill the overdrive meter: a full one gives six seconds
of a faster gun firing in a fan of three. Big kills hold the action a
moment and shake the screen, and the helicopter gains momentum (it speeds
up and glides) instead of the original's stop-start flight.

### Bruce Lee keys and options

On the title: F3 one or two players taking turns, F5 the Yamo played by a
second player (the second gamepad, or the numeric keypad: 8 4 6 2 and 0
for fire), F7 or fire to begin. In play: P (the C64's space bar) pauses,
fire ends a pause, F1 abandons the game.

- **Joystick 1 / Joystick 2** — *Left-handed* turns the stick a quarter, as
  the title's L and R keys do on the C64 (an "L" shows in the status line).
- **Title music** — the title plays its tune every fifth time it comes up;
  *Off* is the original's S key.
- **Smooth motion** — Bruce, the Ninja, the Yamo and room 18's fireballs
  glide over the three frames between the game's steps instead of jumping;
  the lift scrolls one row at a time instead of two.
- **Particle effects** — sparks where a blow lands, a burst of light from a
  lantern taken, cracks crawling across a crumbling floor before it opens
  (and closing as it mends) and its falling pieces, embers behind the
  fireballs; on the C64's pixel grid and in its colours.
- **Light effects** — a soft light around each hanging lantern (dithered,
  in yellow and light grey) that brightens and dims with the lantern's
  flame, flames that lick up for two frames instead of blinking from one
  pattern to the next, and a white flash on whoever a blow lands on.
- **Gore** — a defeated Ninja or Yamo, and anyone (Bruce too) caught by
  spikes, a laser or another deadly cell or by a fireball, bursts into
  bloody pieces that fly, bounce on the floors, lie there and fade. Not in
  the original (which just lets the body lie and vanish), so it is its own
  option.
- **Widescreen** — the picture fills the window (up to 21:9), and where
  the C64 has its border the rooms beside the one you're in show through,
  dimmed and still, lined up with this room's doors (where two doors into
  the same room are further apart on one side than the other, the
  neighbour slides up or down as Bruce moves between them, so it meets
  whichever door he reaches). Taking an exit
  scrolls the view over (or down, for a fall): the old room dims as the
  new one lights up. The rooms don't make one consistent map, so each room
  shows the neighbours its own doors lead to.
- **VHS look** — the shared tape filter.

Again only the picture changes: the game's places, collisions and sounds
are the original's.

The **Carnage** page (not the original game, and not in the attract demo):
*Carnage mode* — Bruce's blows send the Ninja and the Yamo flying in arcs,
and the Yamo's blows send Bruce. They bounce off the walls and floors
(hard hits hurt), smash the decoration they fly through into its own
pixels (a statue whose pedestal breaks topples and bursts on the floor,
crushing whoever is under it), knock each other over, and die where the
game's own hazards kill. Each enemy beaten brings the next sooner
(*WAVE 2*, *3*, …). In the wizard's room a blow sends a fireball back up
into his face (1000 points). Hits in quick succession build a combo that
scores; big hits hold the picture for a moment and shake it. Only
decoration breaks (nothing Bruce can stand on, climb or take).

The in-game options leave out the original's own settings (the
joysticks' handedness, the title tune every fifth time): they stay at its
defaults.

The options page's **Cheats** page (not in the original): *Infinite lives*
(a lost fall is not counted) and *Enemy hits don't count* (the Ninja and
the Yamo still strike and land on Bruce, but their hits do him no harm;
falls, flames and other hazards still do).

## Importing

Select a game and press Enter to choose its image, or drop the image onto the
window. Joyport decodes the image the way the original loader does, checks
every checksum, recognises the release from the decoded content and installs
only the game's files, not the image.

Command line, for testing:

```bash
build/joyport --import Fort_Apocalypse.tap
```

Bruce Lee is imported from a G64 image of the original disk (a raw copy of
the disk's tracks; a D64 cannot hold its copy protection, and cracked
versions are different programs).

## License

Joyport's source code is released under the [MIT License](LICENSE): use, change
and reuse any part of it. One exception: `src/c64/character_rom.cpp` holds 64
character shapes from the Commodore 64's character ROM (Bruce Lee shows its
text with them); they are not Joyport's work and not covered by that licence. It contains no game data; the games stay the
property of their rights holders.

### Launcher motion

The SDL collector's shelf lifts focused or hovered cards, carries their covers
into Details, and transitions documentation content while keeping the cover
still. Buttons compress on activation, successful imports settle onto the shelf
with a brief Ready light, and changed save screenshots fade in on return.
Quick-save/load notices slide and fade in the native-resolution app layer.
Animations never delay navigation or game actions.

The library scrolls continuously between rows with keyboard, controller, wheel
or page navigation; pointer selection follows the moving cards. Asset images
crossfade directly over their existing dark backdrop, without a paper flash.
Reduced Motion remains supported through `Reduced motion = On` in
`options/launcher.txt` in the app's data folder, without a front-page toggle.
