# Sonic collection: development stories

Research checked 30 October 2026. Each article has seven paragraphs. Original
prose, no direct quotations or external illustrations. Historical sources are
editorial context only; all implementation descriptions come from the supplied
cartridges as documented in [game.md](../../games/sonic/docs/game.md).
The collection's [plan.md](../../games/sonic/docs/plan.md) was also reviewed;
unfinished Reprise features are not described as original-game defects.

## Sonic the Hedgehog — A small Sonic starts a studio

[Article](../../resources/articles/sonic_gg.txt).

- P1–2 and P7: [Yuzo Koshiro, Red Bull Music Academy, Stockholm 2018](https://www.redbullmusicacademy.com/lectures/yuzo-koshiro/).
  Relevant full transcript passages read: company formation to accept the Sega
  commission, purpose of Ancient, subsequent Streets of Rage 2 work. These are
  attributed recollections, not independently verified contract records.
- P3: [Ryan Lambie, Yuzo Koshiro's Sonic, Film Stories, 21 December 2021](https://filmstories.co.uk/features/yuzo-koshiros-sonic/).
  Relevant full text read, including Koshiro testimony reproduced from Japansoft:
  An Oral History. The book itself was not consulted. Latitude over later stages
  is attributed testimony; the assessment of the resulting identity is editorial.
  Accounts differ in how they describe the initial platform commission. No claim
  is made that either the Master System or Game Gear build definitely came first.
- P4–6: game.md, The four cartridges; Sonic 1 / Acts, rooms and progression;
  Data formats; Sound. Size, compression, linked-room state, remembered life
  monitors and sampled logo voice are local findings. No intention is inferred
  from an unused table or a reconstruction limitation.
- Research provenance: an existing Sonic disassembly was briefly seen before the
  project's constraint was established and was removed. The research notes state
  that findings were reverified against the supplied ROM. This article relies on
  that recorded verification; it does not claim an unexposed clean-room history.

## Sonic the Hedgehog 2 — A sequel with a different destination

[Article](../../resources/articles/sonic2_gg.txt).

- P1 and P4: Sega's 1992 Game Gear instruction manual, original document text
  read through Manualzz (document 22753994, Sonic The Hedgehog 2 Game Gear Instruction Manual).
  The original text begins with Starting Up / Robotnik Returns; the site's AI
  summary and generated FAQ were not used as evidence. Printed sections on the
  rescue, emeralds and Scrambled Egg support these paragraphs. A second location
  for the [manual scan](https://www.gamesdatabase.org/Media/SYSTEM/Sega_Game_Gear//Manual/formated/Sonic_The_Hedgehog_2_-_1992_-_Sega.pdf)
  was located, but the scan itself was not visually reviewed.
- P2 developer/year: existing catalog attribution, cross-checked against
  [Games Database's game record](https://www.gamesdatabase.org/game/sega-game-gear/sonic-the-hedgehog-2.aspx).
  This is a catalog record, not creator testimony. No interview about Aspect's
  internal schedule, staffing or budget was found. The story deliberately does
  not borrow the Mega Drive sequel's development history.
- P2 cartridge comparison, P3 and P5–7: game.md, The four cartridges; Sonic 2
  and Triple Trouble / Common design; Sonic 2 sections on zones, camera and
  objects. Distinct engines, sizes, vehicles, effects, script vocabulary and
  later lineage are local findings. More capacity is not claimed to have caused
  a particular feature. The old fallback's unverified "first four-megabit"
  marketing claim was removed; doubling of the examined ROM size is verified.
- P3–4 interpretation of travel and search, P6 framing, and P7 significance are
  editorial synthesis. With fewer production sources, this article gives more
  space to construction visible in the finished game; it does not invent a
  behind-the-scenes chronology.

## Sonic Triple Trouble — A new rival finds his personality

[Article](../../resources/articles/triple_trouble_gg.txt).

- P1–3 and the character-design conclusion:
  [Jack Yarwood interviews Shinichi Higashi, Time Extension, 7 February 2026](https://www.timeextension.com/features/interview-i-believed-fang-was-forgotten-former-sonic-artist-talks-batman-design-changes-and-swapping-games-for-vinyl-toys).
  Full relevant interview passages read. Higashi's influences, initial jerboa
  design, changes in character and weapon, and Ihoroi attribution are testimony.
  His proposed connection to Knuckles remains tentative. No censorship claim or
  categorical "gun removed from the finished game" assertion is made. Derived
  interview material kept below 200 words; the broader design assessment is our
  synthesis with the independently examined game structure.
- P4–6: game.md, Triple Trouble / Program modes, Zones, Player states; shared
  lineage / Common design. Separate special modes and Nack scene, train and
  contextual movement, and expanded object instructions are local findings.
  Shared formats do not prove that every subsystem or staff member was retained.
- P7: editorial synthesis of the attributed art history and recorded game
  structure. No claim that we recovered Aspect's authoring tools.

## Sonic Advance — New hardware, familiar momentum

[Article](../../resources/articles/sonic_advance_gba.txt).

- P1–2 and development testimony in P6:
  [Shigeru Miyamoto x Yuji Naka, December 2001 interview, shmuplations translation](https://shmuplations.com/miyamotoxnaka/).
  Full relevant exchange read. New audience, enjoyment of 2D tuning, link feature
  difficulties and Nintendo's benefit from testing are attributed testimony.
  Under 200 derived words. No attempt to settle which simultaneous release was
  the absolute first Sonic on Nintendo, and no interview jokes treated as facts.
- P3: [Takashi Iizuka interviewed at TGS 2010, Last Minute Continue](https://lastminutecontinue.com/interview/tgs-interview-takashi-iizuka/).
  Relevant complete question and answer read. Partnership starting with Advance
  and praise of Dimps are Iizuka's account. No claim that Dimps made Sonic Pocket
  Adventure is inferred from the surrounding discussion.
- P4, Tiny Chao Garden description in P6, P7:
  [Ricardo Torres, GameSpot review, 8 February 2002](https://www.gamespot.com/reviews/sonic-advance-review/1900-2846624/).
  Full review text read as contemporary first-hand reception. Cast, visual
  comparison and link feature are reported observations; criticism is explicitly
  Torres' judgement. The review's erroneous 1989 Sonic debut date is not used.
- P5: game.md, The four cartridges; Sonic Advance / Stage data and Player.
  Distinct architecture, floor/wall/angle data, two collision layers and separate
  compressed placement lists come from local ROM analysis, not external guides.
- P4/P7 linking the old and new design concerns is editorial interpretation.

## Integration

All four articles use app-owned text resources and the same SDL development-story
page as the existing articles. Matching short fallbacks avoid retaining claims
that the expanded research did not substantiate. The shared article check covers
all eighteen stories in landscape and portrait.

Validation: all eighteen articles passed the SDL layout and scrolling checks
(page movement, wheel, pointer/touch drag, scroll bounds and return navigation)
in landscape and portrait. All four new stories were visually reviewed at their
beginnings and endings, including source credits. No Android-device run was
performed for this editorial addition.
