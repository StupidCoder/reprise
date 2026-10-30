# Writing development articles for Reprise

Write for a curious player, not for a reverse engineer. The result should feel
like a short magazine feature explaining how a particular game came to exist.
The first examples are [Stunt Car Racer](../resources/articles/stunt_car_racer_amiga.txt)
and [Bruce Lee](../resources/articles/bruce_lee_c64.txt).

## Research before writing

1. Read the game's game.md, plan.md and current catalog entry. Separate original
   game findings from limitations or bugs in Reprise.
2. Research development history using creator interviews, contemporary magazines,
   manuals, credits and original announcements. Prefer primary testimony; record
   whether you read the full source or only an indexed excerpt. Search snippets
   can identify leads, but must not silently become full-source verification.
3. Follow RECONSTRUCTION.md: outside historical sources are editorial context,
   never implementation evidence. Do not consult outside disassemblies,
   decompilations, ports, symbol/asset maps or reverse-engineering guides.
4. Create a paragraph-to-source map in docs/articles/sources.md (or a linked
   game-specific source note). Include URLs, publication/issue/page when known,
   local game.md section references, access date and unresolved claims.
5. Trace striking claims to their source. A creator's recollection is testimony,
   not proof of a mechanism. A program's unusual layout is evidence, not proof
   of the developer's intention. Avoid unverified financial crises, attribution,
   exact durations, causal explanations and claims of priority.
6. If sources conflict, qualify the point or omit it. Never fill the gap with
   a plausible story. Use game data and execution for any new technical finding.

## Voice and shape

Aim for six or seven paragraphs, usually 400–600 words, but prefer a shorter
well-supported piece to padding. Start with a concrete surprise or a feature
the player recognises. Let the game's own story determine the sequence: original
idea, people, experiments, constraints, platform changes and the resulting play.

Connect technical details to what they made possible. Explain specialised terms
in ordinary language. No code listings, addresses, register names or research
logs in the reader-facing prose. Those belong in the evidence notes. A small
diagram or an imported illustration is useful only if it explains the story;
do not decorate with unlicensed scans.

Use fresh prose, not publisher copy or a close paraphrase of one article.
Keep quotations short and explicitly attributed. Avoid hype, generic nostalgia,
invented scenes and a chronological list of unrelated facts. Do not insert
Reprise's migration history or a status report on its implementation.

Label uncertainty naturally: "Fortier recalled..." or "Our examination suggests..."
Distinguish confirmed observation, attributed recollection and interpretation.
Do not turn "emulator" in an interview into a claim about CPU emulation unless
the evidence establishes that meaning. Do not equate a close port with an exact
mechanical translation.

## Integration and review

- Article body: resources/articles/<catalog-id>.txt, UTF-8, blank lines between
  paragraphs. Keep each paragraph on one line; the SDL renderer wraps it.
- Title and short source credit: catalog metadata. The article metadata table in app/catalog.cpp
  lists the titles and credits and loads each body with platform::resource.
  Extend that table when adding an article. Missing files fall back to
  the older short note; verify that the article actually loads.
- Detailed references: docs/articles/sources.md, linked from this guide. Keep
  source URLs out of running prose; the launcher shows a compact source credit.
- App-owned resources are copied into macOS and Android packages; no imports
  should be required to read an article.
- Verify the complete text, including its final paragraph and source credit,
  in both landscape and portrait. Check wheel, keyboard/page keys, gamepad
  navigation and touch dragging, clipping, scrollbar and return navigation.
- Recheck factual claims against the evidence map. Original data findings must
  not be presented as developer testimony, or vice versa.
- Add a focused regression check when changing article rendering or navigation.
  Do not claim a platform was tested merely because it shares the renderer.

For companion technical explanations, use [Writing technical lessons](WRITING_TECHNICAL_LESSONS.md). Illustrated articles belong in the lesson browser alongside experiments.
