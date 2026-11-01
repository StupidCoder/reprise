# Development article sources and evidence limits

Editorial research checked 30 October 2026. Articles are original prose.
No externally sourced implementation was consulted or used to change game rules.
The app uses short credits; this file is the detailed audit trail.

## Stunt Car Racer — The jump that became a racing game

Body: [article](../../resources/articles/stunt_car_racer_amiga.txt).

- Paragraphs 1–3: Crammond's first-person recollections in "The Making of Stunt
  Car Racer", archived by [Retro Isle](https://www.retroisle.com/gamedetails.php?id=23344&n=Geoff+Crammond).
  The search index returned the interview passages; the live page currently
  fails with a database error. Access is therefore an **indexed interview
  excerpt**, not a checked full magazine scan. A bibliographic lead identifies
  Edge 125 (July 2003), pp. 114–117; issue/page attribution still needs checking.
  The [Retro Gamer 244 preview](https://pocketmags.com/es/retro-gamer-magazine/issue-244/articles/ultimate-guide-stunt-car-racer)
  independently reports the prototype-to-jumps story and more than two years.
  We use the interview's conservative duration, not an exact three-year claim.
  Less than 200 words of the article draw on the archived interview.
- Paragraphs 4–5: [game.md](../../games/stunt_car_racer_amiga/game.md),
  "Program structure: a C64 game converted to the 68000 (Interpreted)".
  Byte-wide arithmetic, pointer byte order and retained compact addressing are
  recorded observations. The shared 6502 ancestry is an interpretation. The
  C64 executable has not been compared; do not claim proven instruction-for-
  instruction identity or describe a particular conversion tool as established.
  Some word operations are adapted to the 68000.
- Paragraph 6: the same game.md, track representation and height profiles.
  This is local research on the supplied Amiga image, not outside port research.
- Paragraph 7: editorial synthesis of the above, not an additional historical
  claim. No financial hardship, precise release-order claim or causal connection
  to Hard Drivin' is asserted.

## Bruce Lee — A fortress, a rival and an Atari inheritance

Body: [article](../../resources/articles/bruce_lee_c64.txt).

- Paragraphs 1–3: [Electronic Games, November 1984](https://www.ataricompendium.com/archives/magazines/electronic_games/electronic_games_nov84.pdf),
  "The Men Behind the Computerized Ninja: Fortier and Day". Interview excerpts
  establish their Zaxxon collaboration, Day's visual research and his account
  of Fortier experimenting with joystick-controlled Yamo. Search-index text
  consulted; the full 40 MB PDF exceeded the browser's retrieval limit.
  The prose's observations about scenery and play are editorial interpretation.
- Paragraph 4: [Retro Gamer 145 (2015), "The Making of Bruce Lee", p. 43](https://electronicsandbooks.com/edt/manual/Magazine/R/Retro%20Gamer%20UK/145.pdf).
  Indexed interview text directly attributes "Atari emulator" and first
  conversion to Fortier. Full PDF retrieval timed out. Only those attributed
  recollections are used. His associated CPU-speed comparison is not repeated:
  it has not been reconciled with hardware timing. "Compatibility layer" and
  whole-machine/CPU emulation are **not** established by this source or by our
  local C64 research. The two-word quote is deliberately kept in attribution.
- Paragraph 5: [game.md](../../games/bruce_lee_c64/game.md), "Charsets" and
  "Rooms": shared graphics, literal/repeated cell runs, two-row map cells.
- Paragraph 6: the same game.md, "Actors": AI and player inputs feed character
  movement. This supports a high-level explanation, not a claim about the
  historical order in which those routines were written.
- Paragraph 7: the same game.md, "Room setups and scripts": per-room native
  logic, conditions and effects. The article says room-specific logic, not a
  general-purpose script language. Its concluding connection is interpretation.

## Reading in the app

Documentation → Development notes. Sources are shown after the seven paragraphs.
The files above are editable prose; no generated duplicate article copy is kept.

## Fort Apocalypse — A helicopter beneath the surface

Body: [article](../../resources/articles/fort_apocalypse_c64.txt). Seven paragraphs.
Research checked 30 October 2026.

- Paragraphs 1–2 and the cartridge ambition in paragraph 3:
  [Steve Hales, Halcyon Days](https://dadgum.com/halcyon/BOOK/HALES.HTM),
  interview text read in full. Dream, collaboration with Ihor Wolosenko,
  interruption for Slime, response to Choplifter, deliberately impossible
  setting, difficult enemy AI and CES playtesting. Fifty levels and the 16 KB
  cartridge are **Hales's recollection**, not a measured description of our C64
  cassette. Under 200 words of article prose derive from this source.
- Paragraph 3's actual finished layouts, paragraph 4's C64 attribution and
  paragraph 7's short-story framing:
  [Karol Wisniewski interviews Steve Hales, AtariOnline, 24 May 2009](https://www.atarionline.pl/v01/index.php?archive=&ct=wywiady&id=1243163472&start_from=20&subaction=showfull&ucat=5),
  interview text read in full. Four playable designs, additional paper designs,
  two removed after testing; Vierra's port and Hales's account of shared code.
  The article does not convert his estimated 95% into measured equivalence.
- Paragraph 4's duration caveat:
  [Retro Gamer 44, The Making of Fort Apocalypse](https://electronicsandbooks.com/edt/manual/Magazine/R/Retro%20Gamer%20UK/44.pdf),
  indexed interview passage says about a month; AtariOnline says about a week.
  Only the indexed passage was consulted for that contrast, not a full issue.
  We do not choose an exact duration. Hales's Blue Thunder inspiration and CES
  dating were omitted because the chronology needs further reconciliation;
  release-year metadata is outside this article update.
- Paragraph 5: [C64 game.md](../../games/fort_apocalypse_c64/game.md), Asset
  decoding / terrain and radar source tables, plus setup changes. Two terrain
  sources, the level-2 reuse and local clearing are verified local findings.
  This does not equate internal level indices with the designer's proposed
  fifty-level cartridge.
- Paragraph 6: the same game.md, Original's bugs and scenario comparisons:
  enemy climb through rock; fort explosion and prisoner-count underflow.
  No assertion that these bugs were deliberate or were the bugs Hales remembered.
- Paragraph 7: the AtariOnline short-story remark plus editorial interpretation
  of the locally reconstructed rescue/refuelling/return mechanics. No additional
  production history is implied.

## Marble Madness — A small marble, an elaborate world

Body: [article](../../resources/articles/marble_madness_amiga.txt). Seven paragraphs.
Research checked 30 October 2026.

- Paragraph 1: editorial description of the game and the imported Amiga artwork.
- Paragraphs 2–3:
  [Craig Grannell interviews Mark Cerny, Retro Gamer 53 (2008), The Making of Marble Madness, pp. 82–87](https://electronicsandbooks.com/edt/manual/Magazine/R/Retro%20Gamer%20UK/53.pdf).
  Full PDF retrieval timed out. The search index returned the interview's
  miniature-golf, isometric-grid, racing-to-clock and motorised-trackball
  passages; those passages, not the entire issue, were read. Under 200 words
  derive from this interview. Conventional controls in the released arcade
  game must not be described as motorised. No unsupported manufacturing cost
  or development-time estimate is added.
- [Cerny's GDC 2011 session listing](https://www.gdcvault.com/play/1014629/Classic-Game-Postmortem-MARBLE)
  and contemporary reporting were consulted as historical leads, but the video
  was not watched. Do not cite that listing as if its full talk was inspected.
  No technical implementation claim here depends on that reporting.
- Paragraph 4:
  [Electronic Arts original packaging, preserved with an Apple II copy](https://www.cvxmelody.net/Marble%20Madness%20Apple%20II%20original%20folder%2C%20command%20summary%20%26%20disk%20%281986%20Electronic%20Arts%29.pdf).
  Full 32 MB retrieval exceeded the browser limit. The indexed page transcription
  includes the Amiga-specific Larry Reed credit and the promotional arcade-at-EA
  story. The article identifies the latter as publisher storytelling. No exact
  staff count, porting duration or source-code transfer is inferred from it.
- Paragraphs 5–6:
  [Amiga game.md](../../games/marble_madness_amiga/game.md), Asset decoding,
  Findings (height field, wall covers and region interpreter), and Silly course
  rules. Bitmap presentation, height simulation, sprite masks, mutable ground and
  slope response are our original-image findings, not imported arcade research.
  No claim of identical internals between the arcade and Amiga versions.
- Paragraph 7: the same game.md, Findings / original mouse and joystick input
  transformations; the relationship to the home conversion is editorial synthesis.
  Does not claim that all original multiplayer features are implemented in Reprise.

## Mind Walker — An unfamiliar machine, an inner world

Body: [article](../../resources/articles/mindwalker_amiga.txt). Seven paragraphs.
Research checked 30 October 2026.

- Paragraph 1 and the manual observations in paragraphs 4 and 7:
  [Commodore's Mind Walker manual (1986)](https://openretro.org/file/d22eefdbeb23b36a95d402cd7334ed551439bac7/Mind_Walker_Manual_ocr.pdf),
  PDF text inspected, including requirements, case history, objectives and game
  selection. The premise is attributed to the manual rather than presented as a
  clinical explanation. Fewer than 200 article words draw on the manual.
- Paragraphs 2–4 and 6:
  [Bill Williams interview, YAAM issue 2, February 1989](https://www.vgpavilion.com/mags/1989/02/yaam/yaam-interview-bill-williams/),
  full interview text available and the relevant passages read. Sound attracted
  him to the prototype; the design changed freely; Synapse's breakup and the
  theme are his recollections; 256 KB/Intuition remained requirements; the Freud
  sequence arrived in the last month before beta; Roxy Wolosenko edited the
  manual. These four paragraphs total fewer than 200 words. No diagnosis of
  Williams is inferred from his informal descriptions of his state of mind.
- Paragraph 5 and the shared-progress observation in paragraph 7:
  [Amiga game.md](../../games/mindwalker_amiga/game.md), Overview, Boot chain and
  module protocol, World generator and stage-specific reconstruction. Seven
  compiled programs include setup and error handling as well as the five named
  presentation/play stages. Shared state is 784 bytes. Four preset seeds plus
  a clock seed generate the worlds. The article does not claim a measured memory
  saving or an established historical reason for each architectural decision.
- Paragraph 7 is interpretation of the original manual and the locally verified
  stage structure, not an attributed quotation about the creator's intentions.

All three use original prose, no direct quotations or reproduced artwork.
The catalog supplies titles and compact credits. The article navigation check
covers all five development stories, with no game import required.

Validation for these additions: all five development stories passed the article
layout and navigation checks in landscape and portrait (page keys, wheel, drag,
scroll bounds and return). Rendered beginnings and endings were reviewed,
including source credits. The macOS bundle was refreshed; no Android device
run was performed for this editorial update.

## Remaining Commodore collection

The eight additional stories, their paragraph-level references and evidence limits
are documented in [Commodore collection sources](commodore-collection.md).
All 13 Commodore games now have seven-paragraph development articles.

## Spy Mouse — From flight paths to a secret agent

Body: [article](../../resources/articles/spy_mouse_ios.txt). Seven paragraphs.
Research checked 30 October 2026. Original prose; no direct quotations or images.

- P1–2 and P5 name explanation:
  [Rob Murray, Gamezebo, Interview: Talking Spy Mouse with Firemint's Rob Murray (2011)](https://www.gamezebo.com/news/interview-talking-spy-mouse-with-firemints-rob-murray/).
  Relevant full interview passages read: prototypes after Flight Control, stealth,
  more than two years, authored content, character name versus product name.
  Under 200 derived words. The article does not infer that EA ordered the rename
  or repeat the interviewer's unsupported claim that Firemint invented a genre.
- P1 Flight Control comparison and P3:
  [Tracy Erickson interviews Rob Murray, Pocket Gamer, 8 March 2011](https://www.pocketgamer.com/spy-mouse/firemints-rob-murray-on-upcoming-agent-squeek-we-want-this-to-be-the-mario-of-th/).
  Full interview text read. Mario is Murray's stated ambition, not a claim of
  equivalent commercial success. Accessibility and depth are his design goals.
- P4 art attribution:
  [Rich McBride's Spy Mouse portfolio](https://www.richjmcbride.com/spy-mouse.html).
  Portfolio text read; listed responsibilities are style, backgrounds, world map,
  UI/UX. Do not infer he authored every animation or all of the character art.
- P4 presentation, P5 internal project name, P7:
  [iOS game.md](../../games/spy_mouse_ios/game.md), Builds, Drawing a path,
  Scenes and presentation. Original executable paths preserve AgentSqueek;
  route smoothing/wall handling/snapping and the map-driven demonstrating finger
  are local research findings. Public sources vary between Squeak and Squeek;
  the article preserves the distinction rather than normalising the embedded path.
  P1 and P7's relationship between gesture and intention is editorial synthesis.
- P6:
  [Laura Parker interviews Rob Murray, GameSpot, 19 August 2011](https://www.gamespot.com/articles/firemint-ceo-on-ea-acquisition-future-of-the-company/1100-6329514/)
  and [EA's launch announcement, 25 August 2011](https://news.ea.com/press-releases/press-releases-details/2011/When-the-Cats-Are-Away-SPY-mouse-Will-Play/default.aspx).
  Relevant full texts read. May is the acquisition announcement; August is launch.
  Resources/reach and update pressure are Murray's account. Six worlds and 72
  levels describe launch, not the later 1.1.1 image researched here. No budget,
  sales total, near-bankruptcy or promised-update delivery is inferred.

The article loads after all catalog entries are assembled, so standalone games
use the same packaged resource path as collection games. The short fallback also
now reflects the creator's account rather than attributing the name change to EA.

Validation: all 14 articles passed the shared SDL layout and navigation checks
(page keys, wheel, pointer/touch dragging, scroll bounds and return). Spy Mouse
beginnings and endings, including source credits, were visually reviewed in
portrait and landscape. The macOS bundle was refreshed; no Android-device run
was performed for this editorial addition.

## Sonic collection

The four handheld Sonic development stories have a separate
[paragraph-to-source map and evidence notes](sonic-collection.md).

## Layton collection

All six Layton adventures now have seven-paragraph development stories. See
[the paragraph-to-source map and evidence notes](layton-collection.md).
This completes expanded development-story coverage for the current 24-game catalog.

## Captain Toad: Treasure Tracker

The pending 3DS title has a seven-paragraph story, **Small worlds, big discoveries**.
See its [paragraph-to-source map and evidence limits](captain-toad.md).
The article is available before importing or connecting the game engine.
