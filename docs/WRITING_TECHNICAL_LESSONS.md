# Writing technical lessons for Reprise

A lesson explains how a game works. An illustrated article is one valid lesson
format; it does not need a live experiment to earn its place in the browser.
Development stories explain the people, circumstances and decisions around a
game. Technical lessons explain mechanisms. Link the two when useful, without
repeating the development story. For the other format, follow
[Writing development articles](WRITING_DEVELOPMENT_ARTICLES.md).

## Start with a question a player can recognise

Choose one central question: why does a car bounce, how does a painted wall hide
a marble, or how can a whole room fit into so few bytes? Start with its visible
consequence, then uncover the representation, transformation and resulting
behaviour. Aim for five or six short sections, usually 600–850 words, with three
or four figures. Length follows the explanation; do not pad a thin finding.

The first technical article pair is **A world measured in angles** and **A car
balanced on three springs**, in resources/articles/stunt_car_racer_*.txt.
Their detailed evidence map is [here](articles/stunt-car-racer-technical.md).

## Establish what we know before choosing the format

Read the game's game.md, plan.md, relevant decoder/engine code and validation
reports. Follow RECONSTRUCTION.md: game-specific technical evidence comes from
the user's original files and execution, not outside disassemblies, ports or
reverse-engineering guides. Earlier user-owned research may supply leads when
authorised, but check its claims and conflicts against the original. A plausible
re-implementation alone does not establish what the original did.

Maintain a section-to-evidence map in docs/articles/<game>-technical.md. Record
image hashes, relevant routine/data identifiers, commands, tool versions or
source fingerprints, outputs and coverage limits. Keep addresses, assembly and
code listings in these notes, not in the reader-facing lesson. Never infer
historical intent or performance measurements from an implementation choice.

Distinguish clearly between:

- Original behaviour supported by data or execution.
- An interpretation of that evidence, with its remaining uncertainty.
- A Reprise implementation choice or known fidelity difference.
- An illustrative model, which teaches one relationship but is not gameplay.

Conflicting evidence is a reason to narrow or postpone a claim. A tested renderer
is not a tested AI, and a tested physics step is not a complete tested race.
Do not manufacture decisions or states to make a lesson seem more interactive.
If a subject is deferred, record a concrete follow-up; do not expose an empty
chapter as if it were complete.

## Choose illustrations that do explanatory work

Use imported game data wherever the figure claims to show a particular room,
track, asset, storage size or measured value. Compute labels and totals from that
same decoded data. Never replace game data with an attractive invented example.

Use clean SDL diagrams for relationships: annotated geometry, a pipeline, a
small comparison table, a time graph or a decision tree. Explain what each axis,
colour and unit means. State whether an image is a capture, a reconstruction or
a schematic; identify simplifications such as unscaled height or floating-point
math replacing the original integer approximations. Comparison curves must say
what is held constant (for example centre scale versus field of view).

Connect every figure to the surrounding explanation. Say what to notice and
what it explains in play. Do not use generated artwork as evidence of a technical
mechanism. Prefer progressive explanation over a dense diagram containing every
variable at once. Use a few short equations when they clarify a relationship;
explain each quantity in ordinary language. No code listings.

## When to add interaction

Start with a readable article that stands on its own. Add controls only where
changing a value teaches something: scrub a frame, move a sampling point, inspect
a state transition or compare two projections. Label a teaching model as such.
A live demonstration must use the actual reconstructed game systems and isolated
state, never the player's current progress or persistent save. Preserve a clear
return to the lesson and explain the controls on touch, keyboard and gamepad.

For an unresolved subsystem, publish the established mechanism or wait for the
reconstruction. The Stunt Car Racer opponent chapter was deferred until league
race and contact comparisons could support its driving explanation; its evidence
map records that boundary and the later findings.

## Consistent voice and structure

Write for curious players, including developers. Explain each new term where it
first matters. Use concrete cause and effect: a rising road compresses a support,
unequal forces tilt the body, and that tilt changes the next contact calculation.
Avoid a catalogue of routines or an unbroken wall of prose. Do not call a model
"realistic" without explaining what it simulates and what it simplifies.

A useful sequence is: visible surprise; representation; transformation; visual
example; consequences and limits; a short evidence-and-scope note. The structure
is a guide, not a requirement to force every game into identical headings.
Keep source attribution compact in the UI and detailed in repository notes.

## SDL integration and review

- Use the existing Documentation / Lessons browser. Give each entry an informative
  title, one-sentence summary and a format label (Illustrated article, Experiment,
  or Live demonstration). Do not add another shortcut to bypass the browser.
- Keep presentation portable: SDL and shared platform/UI services only. Use
  platform::resource for app-owned article text; never bundle original assets.
  Import-dependent illustrations may require the player's installed game.
- A responsive Lesson may override set_canvas; render and input must use the same
  logical bounds. Keep reading lines at a comfortable maximum width. Support
  portrait, normal and wide landscape, fractional wheel scrolling, directional
  and page keys, touch dragging, bottom clamping and return navigation.
- Stunt Car Racer's reader supports a deliberately small text format: one title
  prefixed '# ', section headings prefixed '## ', one paragraph per line, blank
  separators and '!figure name' for a known SDL figure. This is not full Markdown.
  The parser must reject unknown figures rather than hide missing illustrations.
- Cache layout and expensive imported illustrations outside the per-frame path.
  Respect existing typography, paper/card colours and clear diagram labels.
- Check the whole article, especially the last paragraph, at several aspect
  ratios. Inspect rendered figures for clipping, overlaps, misleading scales
  and illegible labels. Test navigation using real imported data. Keep compact
  evidence; do not commit imported assets or bulk temporary captures.
- Record what was actually tested. Shared SDL code is not evidence that a build
  ran on Android. Update the evidence map when the reconstruction changes.
