# Picarat

Native C++ recreations of three Nintendo DS puzzle adventures, rebuilt from
the original games' code and played with data from your own copies.

Picarat contains no game data. It reads graphics, text, music, voices and films
from ROMs you dump yourself from cartridges you own, and runs its own C++
reconstruction of the game logic natively, with no emulator. Even the constant
tables inside the games' program code, such as the sound drivers' pitch tables
and the movie decoder's code tables, are read from your ROM during the import
instead of being part of this repository.

Picarat is an independent project. It is not affiliated with or endorsed by
Nintendo or the games' developer and publisher, and all trademarks belong to
their owners.

## Status (version 0.71)

Version 0.5 marks a milestone: the whole original trilogy can be played from
start to finish, on the Mac and on Android phones.

| Game | Supported release | Playable |
|---|---|---|
| *Curious Village* | European edition (English, French, German, Spanish, Italian), game code `A5FP` | Complete: the story from the logos and title to the end credits, all puzzles, the trunk with the three minigames (robot dog, hotel, torn painting), the mysteries and the puzzle index, the save slots, the bonus section (challenges, character files, art, music, voices, movies, passcode), movies, music and sound effects, in any of the five languages. Wi-Fi downloads end in the original's connection error, since the service is gone. |
| *Pandora's Box* | German edition, game code `YLTD` | Complete: the story from the opening to the ending and into the post-game, all 187 puzzles, the four minigames, the bag, the save slots, the bonus section, movies, voices, music and sound effects. |
| *Lost Future* | German edition, game code `C3JD` | The story from the logos to the end credits, all 224 puzzles (every special type), the three minigames, the bag, the save slots, movies, voices, music and sound effects. Not yet: the parrot's story event, the puzzle index, the dictionary, the secrets and the Wi-Fi menus (none of them is needed to finish the story). |
| *Spectre's Call* | German edition, game code `BLFD` | The story from the title to the end credits, with the save slots (event saves, the bag's save, continuing from the title), the bonus section (puzzle index, Layton's challenges, the secrets: profiles, gallery, music, voices, videos and the secret door), the bag's three minigames (model railway, puppet show, fish tank; in the story and in minigame free play), the whack-a-mouse side game, the lost-puzzle list (puzzles passed by go to the cat Kietz), 228 of the 229 catalogue puzzles in the puzzle collection, movies, music and sound effects. Wi-Fi downloads end in the original's connection error, since the service is gone. |
| *Miracle Mask* | German edition (Nintendo 3DS, unencrypted cartridge image), game code `AKKD` | Work in progress: all 150 puzzles of the puzzle collection play, every one solved through to the verdict in testing: the shared play loop with written answers (the 3DS handwriting recognition), and all fifteen special types (among them sliding lines and blocks, piles, floating blocks with the D-pad and tilt, the corn cob, the totem bricks, sliding penguins, cats, strokes and balloons), with the 3DS screens, 3D models, the verdict scene, music, voices and sound effects. Not yet: the story. |
| *Azran Legacy* | German edition (Nintendo 3DS, unencrypted cartridge image), game code `AL6D` | Import only, as above. |

Only these revision-0 ROMs are accepted; the importer identifies them by
SHA-256:

| Game code | SHA-256 |
|---|---|
| `A5FP` | `d808728d3e930b7a6c952d05e5e39db4a15b685c9c0919b57ece07b5243f493f` |
| `YLTD` | `76ed9184ce07c7d4cb342aff4cc960412f36b3d99558aebdb37a2db41cfc275e` |
| `C3JD` | `e8d72745b7530ba3cf2fa713d1840d0f9aebb20a835f2746f32b6bdcd8ea373b` |
| `BLFD` | `f26b840d79ca67f9d7621c5bfbd30bb7db2f20a773b75360d81537bbdfdf35d2` |
| `AKKD` | `3fae2d2f5c3bc10933a9446904c9dcef5f02b80a311a3c20d71ae857aaec9788` |
| `AL6D` | `4ca8db98214909a1111034d0d8183f4238b038885cac3b27aa16a801c61c5910` |

3DS images are read only when they are unencrypted (as dumped to a `.cci` or
`.3ds` file with decryption); Picarat contains no keys and decrypts nothing. Picarat is built
and tested on macOS 26 (Apple silicon) and, as a sideloaded APK, on Android
(64-bit ARM; played on a phone and tested on the Android 16 emulator). The
code is portable C++ on SDL 3, aimed at Linux and Windows as well.

## Shell design

The SDL shell uses an ivory, teal and brass reading-room theme with smooth
Lato typography, the existing illustrated title cards, and matching game hubs,
collection screens and pause menus. Portrait layouts retain touch scrolling;
short landscape windows use a compact pause menu, and narrow dialogs stack
their buttons. Fonts are bundled on desktop and Android. See
[the design and validation notes](design/READING-ROOM.md).

## Conveniences in every game

The games play as they did on the DS, and Picarat adds the same set of
conveniences to each of them. They are the standard for every game that
joins the collection later:

- **Music player**: every track of the game's soundtrack, listed with the
  names the game's own music room gives them, with play controls and a
  waveform.
- **Movie theatre**: every film of the game, with its thumbnail and
  subtitles, played full size on one screen.
- **Puzzle mode**: every puzzle on its own, sortable and filterable by type,
  with its own record of solved puzzles that never touches the story.
- **Minigame free play**: the game's minigames with everything unlocked,
  each with its own progress.
- **Skipping**: logos, movies, timed scripted sequences and narrated
  pictures can be skipped from the in-game menu; the game skips by its own
  rules, so nothing that happens during them is lost.
- **Start at the title and continue**: the games start at their title
  screen, without the publisher logos, and keep an automatic checkpoint
  whenever you reach a room. "Continue story" goes straight back there;
  a small restart button opens the title with the games' own save slots.
- **Touch-area help**: a switch in the in-game menu outlines everything a
  room offers to touch, coloured by what a touch starts: a remark, a
  conversation, a puzzle not solved yet or a hidden hint coin. Each game
  works this out from the room's own data with its own rules.
- **Pinch to zoom** (phones): two fingers zoom the screens and move around
  them, e.g. from a puzzle up to its question; one finger stays the stylus.

The game hub also adds and removes games (to free the space, or to import
another version of a game) without touching saves and progress.

## How the games were rebuilt

Every finding comes from the user's own ROMs and from running them; no
existing disassembly, decompilation, port or fan documentation of these games
was used.

1. **Reading the code.** The ARM9 program and its overlays (code modules
   loaded on demand) were disassembled and decompiled from each ROM. Each
   routine was then rewritten in readable C++ that keeps the original's control
   flow, state and rules. Comments name the original routine (overlay and
   address) a piece of code comes from.
2. **Running the data.** What the games drive from data runs from that data:
   the first two games' event and puzzle scripts (GDS) run on one script
   interpreter, each game with its own command set; the third game's scenes,
   menus and puzzles are LSCR scripts and run on a reconstructed LSCR script
   machine; the fourth game's are GSS scripts on a reconstructed GSS machine,
   and its minigames read their courses, tanks and schedules from the game's
   data files. Nothing in the scripts is rewritten by hand.
3. **Checking against the original.** A research harness runs the original
   ROM in an emulator, which serves only as a measuring instrument, and
   compares the reconstruction with it: RAM state after each frame, screen
   captures, sound driver ticks and audio samples. The harness, the emulator
   and the evidence stay outside this repository.

Most of the code is reconstructed and checked this way. Where a routine could
not be fully recovered yet, the behaviour was measured on the running original
in the emulator and modelled from those observations. The tables below list
those places, and every such substitute is labelled in the code.

### Reconstructed from the original code

- **Shared services**:
  - the ROM's file system and decompression, pictures, sprites and fonts;
  - the GDS script interpreter;
  - the sound drivers and their hardware model;
  - the movie decoder: all 27,994 frames of the second game's 23 movies decode
    bit-exactly like the ROM's routines;
  - the handwriting recogniser for written answers;
  - the turned and scaled sprites of the 2D/3D pipeline.
- ***Curious Village***:
  - the story router: logos, title, name entry, opening, rooms with their
    map, events, movies, the trunk and the staff roll; an automated play
    from a new game reaches the end credits;
  - the trunk's pages: the robot dog, the hotel, the torn painting, the
    mysteries, the puzzle index and the puzzle shack, and the robot dog
    sniffing out spots in rooms;
  - the save and load screens in the cartridge's save layout;
  - the title's bonus section (art, characters, music, voices, movies,
    challenges, passcode, the Wi-Fi corner);
  - the script dialect with its structured flow control (if/else, loops);
  - the puzzle frame: title card, picarat values, hints, quitting, retrying
    and the result screens;
  - all 20 puzzle engines, used by the 164 puzzles;
  - the sound archive player and sequencer, and the streamed audio;
  - the language rule that picks the cartridge language from the system
    language.

  Every engine was compared frame by frame with runs of the ROM, as were the
  title, the load screen, the trunk, rooms, the robot dog and every page of
  the bonus section (from saves written by Picarat).
- ***Pandora's Box*** (complete):
  - the story router: game state, rooms, events, place versions and the
    scene router; its state was checked against 227 RAM snapshots of the ROM;
  - the 25 puzzle types with judgment, hints, picarats and the memo;
  - the herb tea, camera repair, photo hunt and hamster minigames, with the
    hamster's 3D world;
  - the bag and its pages (mysteries, notes, diary, puzzle index);
  - the shack, the staff roll and the name entry;
  - the three save slots in the cartridge's save layout;
  - the title's bonus section (art, characters, music, voices, movies,
    challenges, passcodes).
- ***Lost Future***:
  - the LSCR script machine and its loop stack, logos to credits;
  - the puzzle scene with all puzzle types and the memo;
  - the name entry, the bag, the save and load screens (three slots) and the
    mystery menu;
  - the parrot, toy car and picture book minigames;
  - sound, voices and movies.
- ***Spectre's Call***:
  - the GSS script machine with its loop stack and scene services, from the
    title to the end credits; an automated play reaches the staff roll;
  - the story's bookkeeping: steps, event kinds, items, flags and variables
    in the game's own layout, puzzles left behind (sent to Kietz's list);
  - the puzzle session and the special puzzle types, the written answers,
    the memo;
  - the bag's minigames - model railway, puppet show and fish tank - and the
    whack-a-mouse; each was compared frame by frame with the ROM (the
    whack-a-mouse with the ROM's random numbers copied in);
  - the save slots, the name entry, the bonus section and the secrets;
  - the sound driver and the movies.

### Modelled from observing the original (emulator inspection)

These parts work, but follow measurements or approximations rather than fully
recovered code:

- **All games**:
  - Frame pacing is native: the game is woken at exactly 60 Hz. The ROM's
    loading pauses, where the cartridge read time shifts later frames, are
    either left out or inserted as measured constants, so sound-gated waits
    can end a frame or a few frames apart.
  - The DS 3D renderer is a clean-room rasterizer written from the hardware's
    documented behaviour. It looks right but is not pixel-exact; edge pixels
    and blending can differ by a few pixels.
- ***Pandora's Box***:
  - The prize, item, photo-piece, feature and save-prompt windows show the
    game's own texts and icons with a native layout. Their fades are
    approximated by waits.
  - Event transitions, chapter cards and narration set-up use native timing.
  - The dialogue bubble's timing and the puzzle intro, hint and result
    animations are only partly recovered (some sprites show only their first
    frame).
  - Movie pacing and audio/video synchronization are native.
  - A few visual effects use their own random sequence: pancake entrances,
    background shakes and key-arrow pulses.
  - The passcodes are computed from a per-installation stand-in for the
    console's network address.
- ***Curious Village***:
  - Loading pauses come from measured per-scene constants; some story and
    bonus pages load a little faster than on the console.
  - The robot dog's pose in rooms and the spot it picks
    use their own random numbers.
  - The Wi-Fi download fails as on a console without an access point; the
    console's Wi-Fi settings utility is not part of the game and is absent.
  - The passcodes use the same per-installation stand-in for the console's
    network address as *Pandora's Box*, so the code one game shows works in
    the other.
  - Free play hands out the hotel's furniture alternately to the two rooms;
    in the story the player chooses.
  - Sound can sit one sample off when a note starts close to a sample boundary.
  - A few edge pixels differ in the cup puzzle and on lines drawn over markers.
  - The letter mode of the written answers is implemented from the code but
    not yet compared with the ROM.
- ***Lost Future***:
  - Some upper-screen pictures are slightly brighter than on the console.
  - The thick ink line is drawn with a square pen.
  - The random result picture uses its own random numbers.
  - The touch filter is assumed to be the second game's.
  - The parrot's flight and both other minigames have not yet been compared
    with captures of the ROM.
- ***Spectre's Call***:
  - Sound-gated waits and result windows can come a frame or more earlier
    than on the console, which first loads the sound.
  - The Wi-Fi download fails as on a console without an access point.
  - The lost-puzzle list and its notice follow the code but have not been
    compared with a ROM save that holds lost puzzles.

### Additions that are not in the original games

- the game library with the ROM importer and each game's hub page;
- the conveniences listed above: music player, movie theatre, puzzle mode,
  minigame free play, skipping, starting at the title, checkpoints, the
  touch-area help and pinch zoom;
- the in-game menu (Escape, or Back on a phone);
- typing names on the keyboard, pasting and holding **B** as the
  microphone;
- window layouts for wide, narrow and portrait windows;
- a language choice for *Curious Village*, which on the console followed the
  system setting.

## Known open items

- ***Curious Village***:
  - the save write error screen;
  - comparing the save screen, the trunk's pages and the staff roll with
    captures of the ROM.
- ***Pandora's Box***:
  - the first-touch tutorial overlay in the first room;
  - the remaining room widgets (map icons, puzzle counter, hint-coin flight
    animations);
  - the background blend of script command 0x37, which is accepted but has
    no visual effect;
  - the room variant with puzzle bottles (place 63);
  - the window and fade presentation listed above;
  - the Wi-Fi puzzles and downloads;
  - the reset prompt in the bag restarts the game natively instead of the
    console's soft reset.
- ***Lost Future***:
  - the parrot's story event, the puzzle index, the dictionary, the secrets
    and the Wi-Fi menus;
  - the colours of message palettes and the ink;
  - the "prize" page in puzzle-index mode;
  - comparing the minigames with captures of the ROM.
- ***Spectre's Call***:
  - the story's puzzles are played, but parts of the game around them
    (lighting, some map routes) are still approximations;
  - the weekly Wi-Fi puzzles (the service is gone), and the one catalogue
    entry that is a leftover without a puzzle.
- ***Miracle Mask***: the story and everything around the puzzle
  collection; the puzzles' remaining open details are listed in the
  research notes.
- ***Azran Legacy***: everything beyond the import (its resources are
  extracted and checked; no part of it plays yet).
- **All games**:
  - builds and testing on Linux and Windows, and Android on a real phone;
  - a physical microphone (holding **B** or the floating *Hold to blow*
    button stands in for it).

## Features

- A game library with an integrated ROM importer. The importer recognises the
  game by its SHA-256, converts the ROM into a compact native pack in a staging
  folder, verifies it and installs it in one step. The original file is never
  changed and is not needed after the import.
- A hub page per game, with:
  - the story, continued from its automatic checkpoint (the games' own save
    slots are on their title screens);
  - the puzzle mode, minigame free play, the movie theatre and the music
    player (see *Conveniences in every game*);
  - "Remove game" to delete the imported data again.

  Features that are not reconstructed yet are greyed out.
- The games' own three save slots in the original cartridge layout.
- The bonus sections from the title of *Curious Village* and *Pandora's
  Box*: art gallery, character files, music and voice rooms, movie theatre,
  the challenges and the passcodes.
- Window layouts for wide, narrow and portrait windows: the two screens fill
  the window side by side or stacked, scaled with their aspect ratio kept;
  the mouse acts as the stylus.
- The in-game menu (**Escape**): Skip while something skippable plays, the
  touch-area switch with its colour key, and Quit back to the page the game
  was started from.
- Shortcuts:
  - **⌘1** returns to the library and **⌘K** opens the puzzles;
  - **⌘.** skips, **⌘R** restarts, **⌘V** pastes, and in *Pandora's Box*
    **⌘L** goes back to the last checkpoint;
  - type names on the keyboard, and hold **B** (or the floating *Hold to
    blow* button) to blow into the microphone;
  - the arrow keys are the D-pad; with **Shift** they tilt the console,
    and holding **S** shakes it (the 3DS motion sensor).

  Shortcuts use **Ctrl** instead of **⌘** outside macOS.
- Its own user interface: the library, the hubs, the in-game menu and the
  message boxes are drawn by Picarat itself in C++, so they look and behave
  the same on every platform.
- Android: touch is the stylus, two fingers zoom, the layout follows the
  phone's orientation and keeps clear of the system bars and camera cut-out,
  the back gesture opens the in-game menu (and leaves the app from the game
  list), the name entry uses the game's own keyboard, and the phone's
  motion sensor tilts and shakes the 3DS games (measured from the way the
  phone is held when a game starts).

## Building

You need a C++20 compiler, CMake 3.20 or later, Ninja, zlib and SDL 3.4 or later
(on macOS: the Xcode command-line tools and `brew install cmake ninja sdl3`).
Without SDL, CMake builds only the core library, the importer and the tests.

```bash
./build.sh
```

On macOS this configures `build-release`, builds the app and the tests, runs the
tests, and packages an ad-hoc signed `dist/v<version>/Picarat.app` (with SDL
bundled) and a ZIP. Earlier releases are kept; bump the version in
`packaging/Info.plist.in` for a new one.

To build without packaging (any platform):

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

### Android

`build-android.sh` builds an APK for 64-bit ARM phones and tablets
(Android 8 or later) for sideloading. It needs the Android SDK with a
platform, build-tools, the NDK and platform-tools (in `$ANDROID_HOME` or
`~/Library/Android/sdk`, e.g. installed with Android Studio's SDK manager), a
JDK 17, CMake and Ninja. SDL's release source is downloaded once and checked
against its SHA-256. No Gradle is involved; the SDK's own tools assemble the APK.

```bash
./build-android.sh
./build-android.sh --install
```

The APK goes to `build-android/Picarat.apk` and `dist/v<version>/`. It is
signed with the SDK's debug key (`~/.android/debug.keystore`), so newer builds
install over older ones as long as they come from the same computer.
`--install` installs it with `adb` on a connected phone (USB debugging on) or a
running emulator. Without `adb`, copy the APK to the phone and open it there;
Android asks once to allow installs from that app.

## Playing

Open `Picarat.app` (elsewhere: run `build-release/picarat`), click "Add game…"
on a greyed-out game card and choose your `.nds` file (3DS games: the
unencrypted `.cci` or `.3ds` image), or drop it on the window; the importer
recognises which game it is. Import takes a few seconds (the 1 GB 3DS images
about 20).
Afterwards the card opens the game's hub page. "Remove game" on the hub page
deletes the imported data again (to free the space, or to import another
version); story saves and puzzle progress are kept.

Every game keeps an automatic checkpoint whenever you reach a room. Its hub
then shows "Continue story", which goes straight back there; the small round
arrow beside it clears the checkpoint (after asking) and opens the game's
title screen, with its new game and save slots.

On Android, tap "Add game…" on a card and pick the ROM file in the system's file
browser (internal storage, SD card or a cloud folder); the game is imported
into the app's own storage, so the ROM file can be deleted afterwards. The
back gesture works like Escape on the desktop.

Imported data is stored in the per-user app data folder, together with the
saves: `~/Library/Application Support/Picarat/` on macOS,
`~/.local/share/Picarat/` on Linux, `%APPDATA%\Picarat\` on Windows, the app's
private storage on Android (removed when the app is uninstalled). The
command line can import as well (the folder name is the game code and the pack
version):

```bash
build-release/picarat_import game.nds ~/Library/Application\ Support/Picarat/yltd-v11
```

The other games use `a5fp-v3`, `c3jd-v2`, `blfd-v1`, `akkd-v1` and `al6d-v1`.

## Tests

`ctest --test-dir build-release` runs the unit tests. Tests that need game data
read packs in the build folder and are reported as skipped without them:

```bash
build-release/picarat_import --pack game.nds build-release/native-assets
build-release/picarat_import --pack first-game.nds build-release/a5fp-assets
ctest --test-dir build-release --output-on-failure
```

`build.sh` additionally runs the app's story and first-puzzle self-test for
*Pandora's Box* when its pack exists.

## Source layout

| Path | Contents |
|---|---|
| `src/story/` | The stories: the second game's scene router and game state, events, opening, narration and staff roll; the third and fourth games' loops, story, talk, name entry and saves |
| `src/puzzles/` | The puzzle scenes and one engine per kind of puzzle for each game (each file starts with how that puzzle plays); judgment, scoring, the handwriting recogniser |
| `src/minigames/` | Herb tea, camera repair, photo hunt and the hamster; the parrot, the toy car and the picture book; the model railway, the puppet show, the fish tank and the whack-a-mouse |
| `src/menus/` | The bag and its pages, the file dialog, the bonus section and the challenges, the shack |
| `src/system/` | Foundations: resource files, the GDS, LSCR and GSS script machines, input and buttons, frame timing, the shared scene kit, saves and checkpoints, language rules |
| `src/graphics/` | 2D drawing, text and message windows, the DS 2D/3D pipeline and the 3D renderers (`nds3d/`) |
| `src/video/` | Film playback and the Mobiclip decoder |
| `src/sound/` | The sound drivers (both sequencer families), sequenced music and effects, streamed audio |
| `src/import/` | ROM importer: cartridge file system, decompression, resource decoders and one pack builder per game |
| `src/shell/` | The application: library, game hubs, puzzle mode, minigame free play, movie theatre, music player, game screen (with pinch zoom and the touch-area overlay), in-game menu, message boxes, the drawing kit, the game thread and sound output |
| `src/platform/` | The platform layer (window, input, textures, sound device, file picker) on SDL 3 |
| `art/` | Logo and card illustrations (original artwork; `prompts.json` records how the cards were made) |
| `packaging/` | macOS bundle metadata |
| `tests/` | Unit tests |
| `android/` | Android manifest and the small Java activity around SDL |
| `tools/import_rom.cpp` | `picarat_import` command-line importer |

Everything is standard C++20 plus zlib; the app uses SDL 3 for the window,
input and sound device only, and draws its whole user interface itself. No
third-party game code, emulator or media library is used.

## Credits

- [SDL 3](https://libsdl.org) (zlib license), bundled with the macOS app.
- The user-interface font is generated from the pixel font *Terminal 16
  Unicode*, made with Bits'n'Picas.

## License

Picarat's source code is released under the [MIT License](LICENSE). The games,
their data and their trademarks belong to their respective owners; no ROM or
data derived from one is part of this repository.
