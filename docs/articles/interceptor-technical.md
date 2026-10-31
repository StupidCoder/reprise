# F/A-18 Interceptor: technical lesson evidence

Three responsive SDL articles with limited, reproducible experiments. Authored
2026-10-31 from this repository's original-file research, not external game
reverse engineering. No original assets are bundled with the articles.

## Source and reconstruction boundary

- User ADF: `FA-18 Interceptor (1988)(Electronic Arts)[cr A-Ha].adf`,
  SHA-256 `e28e94698d68d0d2d3f009a67597a6bb70083e3ff2e4aea6c79369b76f93bfa2`.
- Imported `program`, 331,232 bytes, SHA-256
  `d7301e20958548f9b5f1ba744f8ff91fef476306c220eb886e7c90bac2802ffa`.
- Existing research: `games/fa_18_interceptor_amiga/game.md`, sections 2–4 and 7;
  renderer/road correction commit `0b7389c`. That agent's original execution
  checks are attributed below; this lesson work does not claim to have rerun
  every original capture.
- Reader text: `resources/articles/interceptor_{painting,roads,flight}.txt`.
  Implementation: `games/fa_18_interceptor_amiga/lessons/technical_lessons.*`.

## Painting the Hornet

| Reader subject / figure | Evidence and implementation |
| --- | --- |
| Points plus a draw recipe | Shape decoding in `data/shapes.*`; draw-list tree at `$112650`, face test `$112B1C`, polygon lists `$11284E` |
| Camera chooses ordering | Hornet type 4; decoded `DrawNode` points/branches/leaves; `game/draw_order.hpp` extracted without changing branch logic from `Scene::paint_shape` |
| One-sided face stops list | `$113836` and `Scene::paint_shape`; two-sided back colours retained |
| Jagged overlap correction | `0b7389c`, `game.md` “The port's renderer”; ordering within objects, depth buffer between standing objects, separate flat passes |
| `plane` | Actual imported type-4 polygon corners; shared order interpreter; one-sided list termination; orthographic teaching camera, added outlines, lines/dots deliberately omitted |
| `order` | Actual reached polygon-list identifiers; dynamic panel shows every list, even ones with no visible faces |

The lesson's dot-product face test uses model coordinates and the eye position;
rigid camera rotation leaves its sign invariant. It is an explanatory projection,
not a pixel-match claim. Eight viewpoints alternate above/below while orbiting.
List IDs are decoder-local identifiers, not guesses at named aircraft parts.
Existing research checked 21/21 Hornet transformed points against the running
original (`harness/pointcheck.py`). Axis-based scenery tests have hypotheses in
the research notes; this lesson stays with the Hornet's facing tree.

## Roads that start as zeros

| Reader subject / figure | Evidence and implementation |
| --- | --- |
| Startup, rather than damaged files | Original `$1197E0`, called by `$101628`; record-offset list `$139B74` |
| Stored endpoints and results | 20-byte records: A/B x,z then three generated pairs; `game::road_points` |
| Scaling and offsets | `$119CAE` / `$1108C4`, `scaled_vector`; length 46; coefficients -88 and ±241, shifts by eight |
| `road` | Endpoints read from imported program, generated coordinates returned by the same builder as `Scene`; full-segment view plus magnified A-end inset |
| `records` | Original coordinate pairs versus generated coordinate pairs, with counts computed from the import |
| Consumers | City parallelogram opcode `$78` and line opcode `$12C`; `data/shapes.cpp` |

Existing agent evidence: 120/120 generated points matched the running original;
city comparisons used the original camera via `sceneshot` / `F18_CAMERA`.
This turn independently reads 40 records / 120 points and confirms all 120 stored
pairs are `(0,0)`. It does not call that a new original-runtime comparison.
A code comment said “about 160 degrees”; the coefficient pair atan2(241,-88)
is about 110 degrees. Corrected the comment only; construction arithmetic is
unchanged. No claimed author intent, random-city generation, or whole-game
compression saving. The drawn offset lines are annotations, not surface polygons.

## Flight model

| Reader subject / figure | Evidence and implementation |
| --- | --- |
| Digital input ramps and response | `$10DFD2`, `$10557C`, `read_controls`, `ramp_inputs`, `respond`; table `$1327B8` |
| Attitude, bank turn | `$122526`, `$1226B8`, `attitude.cpp`; imported sine/arcsine and bank-turn tables |
| Power, fuel and speed | `$105ED4`, `$106A32`, `run_engine`; gear, brake and afterburner rules |
| Climb/sink | `$11AC14`, `$11ABC8`, `$11AB88`; `climb_and_sink`; 31 sink buckets, capped index |
| Momentum/contact | `$106B0E`, `move` and its signed integer smoothing; gear/belly/deck heights and touchdown checks |
| `response`, `sink` | Imported tables with explicitly internal units, discrete samples joined for readability |
| `flight`, `bank` | Two isolated calls of the real `game::fly` system, 120 ticks each, 15 Hz; selected two-second input pulse versus neutral |

Laboratory initial state: `start_at_place(...,0)` for F/A-18 defaults, then
height `0x200000` (8192 world units), airborne flags, gear `0xE0`, zero attitude,
rebuilt body/view matrices, engine speed and airspeed `0x1000` (4096 internal
units), velocity `(0,0,16384)`, original starting fuel. Throttle preset 60 or
120; no afterburner. Neutral/pull-up/right-roll for ticks 0–29, neutral thereafter.
Each edit resets both runs. No module, save API, pilot record, world contacts,
mission logic or random number generator is touched. Graph y scales fit both
runs and are labelled; signed bank angle can wrap at half a turn.

Original-flight evidence recorded by the game agent: `flightcheck` fc1 159/159,
fc2 258/259, fc3 196/198, fc4 238/239, with listed mismatches on key-event ticks;
F-16 f16a 328/328. Soft landings and water-contact coverage are narrower. The
article does not treat the laboratory plots as original captures or claim that
all aircraft physics has been proven identical.

## Validation commands and scope

- `interceptor_lesson_checks` verifies packaged titles, evidence sections and
  figure references without game assets (also in GitHub's macOS checks).
- `interceptor_lesson_checks <installed-content> <temporary-captures>` exercises
  real-data pages at 960x640, 640x1138 and 1422x640, experiment selection, held-fire
  edge handling, fractional wheel, touch drag, page keys and end/resize clamps.
- `shell_checks --interceptor-lessons <installed-content>` checks all three
  browser entries, resizing an open chapter and touch return to the browser.
  Its temporary library links the supplied content; no original import is copied
  or written, and the temporary validation cache is removed afterwards.
- `./build-android.sh --jobs 6` checks portable compilation and APK packaging.
  A built APK is not a physical-device rendering or performance test.

Screenshots belong in temporary output, not the repository. Review includes
all article endings, polygon-list overflow, road-inset label placement and graph
axis alignment. Future changes to the underlying flight or geometry reconstruction
must be checked against these explanations and their scope statements.

## Chapter 4: Landing the wrong way up (2026-10-31)

`resources/articles/interceptor_landing.txt`, three figures and eight controlled
contact probes in the existing SDL lesson reader. No game rules changed.
The original program identity is the same as above. Fresh inspection used the
user-ROM-derived `.analysis/fa_18_interceptor_amiga/work/all.dis`; no external
reverse engineering or internet anecdote was used as evidence.

| Topic | Evidence and limits |
| --- | --- |
| Fixed height | Original $106D6E–$106E6E reads deck flags and gear, adding $708 (Hornet gear down), $2EE (gear up), or $A8C (other aircraft type $14) to deck height. No roll-dependent clearance. `aircraft.cpp::move` reproduces this. Schematic wheel placement is not game geometry. |
| Touchdown threshold | $106F30–$107052 checks existing crash/ground state then compares the effective vertical rate with $240; signed less-than enters the soft path. Normal hard contact sets state bit $200 and mission crash state. The original also has a global bypass at $13AB8A; this lesson describes the normal branch, not every diagnostic/game-mode override. $107000–$107022 sets fast contact for water/gear up at speed >=$3C0; $10702E selects the tyre sound at $180. No roll test occurs in the touchdown classifier. |
| Controlled experiment | `landing_probe` constructs a fresh aircraft at the imported carrier's start point, pitch/heading 0, roll 0 or 180 degrees; body/view matrices use imported flight tables. Gear 0 or $E0, airspeed/speed 1200, velocity zero, surface initially clear, motion airborne, state moves. Height is deck*256+clearance+1. The production `Carrier::touch` first establishes deck flags. `coast` calls the same private `move` used by `fly`, with default flight rules and no world pointer; another `Carrier::touch` follows, using the pre-move pose. Sink 384/575/576/384 yields the tested effective rate because pitch is level and initial vertical velocity is zero. This intentionally bypasses engine, attitude integration and approach dynamics; it is not a full flight replay. |
| Carrier probes and wire | game.md "The carrier", $11B6AA/$11BA3E, `carrier.cpp`. Nose/tail probes do rotate. Carrier check follows movement with pre-move pose in `module.cpp`. Wire-zone/heading/hook conditions are separate; the probe is at the launch position, not a wire-catch scenario. Existing hull implementation explicitly approximates original face tests; no claim of exact inverted hull behavior. |
| Water, warnings, completion | `module.cpp` converts `events.in_water` to crash and handles other object contacts. `messages.cpp::ground_warnings` can inspect bank/pitch separately. game.md "Mission outcomes" ($118BA2) and the module's mission handling require a qualifying surface, flight progress, zero airspeed and outcome; no mission-completion claim is made by the isolated contact result. |

The original routine inspection supports why there is no upright check; it is
not a fresh original-emulator landing replay. Reconstructed boundary tests and
carrier contact must not be described as proof of an entire historical approach.

### Landing lesson validation

Built `reprise`, `interceptor_lesson_checks` and `shell_checks` in an isolated
snapshot of committed HEAD plus only this lesson's files. Imported-data checks
passed all eight probes: deck recognised, no hull hit, grounded; 384/575 survive,
576 crashes in both orientations; gear-up at 384 sets fast contact, and reference
clearance is 750 instead of 1800 regardless of roll. The reader passed at 960x640,
640x1138 and 1422x640, including selection, held-fire edges, touch drag, fractional
scroll, bottom bounds and resize. The launcher test reaches the fourth lesson
by scrolling the list, opens it, resizes and returns. Six CTest cases passed:
interceptor_lessons, shell_navigation, shell_motion, development_articles,
technical_articles, import_progress. Rendered portrait figures and the ending
were inspected for clipping and readable labels. No Android-device run claimed.

Commands in the isolated snapshot:

```
build/interceptor_lesson_checks <imported-interceptor> /tmp/interceptor-landing-captures
build/shell_checks --interceptor-lessons <imported-interceptor>
ctest --test-dir build -R 'shell_navigation|shell_motion|interceptor_lessons|technical_articles|development_articles|import_progress' --output-on-failure
```

SHA-256 of inspected evidence/implementation:

- ROM-derived work/all.dis: `4934ec03efa6fb41f89d3617cacb77ab852077b985144268278c578d42043cce`
- game/aircraft.cpp: `a01d25c7ef6a3cbc466f8bcd643d76a5668be580613079dd18f51bb375995031`
- game/carrier.cpp: `774ab93a9153cf6043bcde4729a681b8921f2df03a05c70725c98ccce2812c32`
