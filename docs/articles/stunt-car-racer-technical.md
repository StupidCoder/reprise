# Stunt Car Racer: technical lesson evidence

Started 2026-10-30; opponent article added 2026-10-31. Three illustrated
articles, with no live gameplay simulation. Neither game logic nor saves are
changed by these lessons. The earlier opponent deferral and its resolution are
recorded below.

## Provenance

The same user-supplied Amiga image as games/stunt_car_racer_amiga/game.md:

- ADF SHA-256: `548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06`.
- Decoded program SHA-256: `4549a5291e6edca2ea578060bca14d457db3a239baf99b4924ae2bece16ab583`.
- The earlier RetroReverse `extracted/game.dec.bin` was compared byte for byte
  with sectors 110–914 of this ADF after the startup sample XOR in
  data/assets.cpp: equal lengths (412,160), zero differences.

The user authorised their own independent RetroReverse research as leads
(game.md, Prior work). No outside game-specific disassembly or port was used.
Prior research lives at ../RetroReverse/games/stunt-car-racer-amiga relative to
Reprise. Its prose alone is not verification. The relevant original execution
probes were rerun for the physics claims below. Research tools use a memory-image
model for comparison; that model is not shipped in Reprise.

## Article 1: A world measured in angles

Source text: resources/articles/stunt_car_racer_projection.txt.
UI: games/stunt_car_racer_amiga/lessons/technical_article.cpp.

| Section | Evidence and limits |
| --- | --- |
| Opening, 1–2: bearing, elevation, scale, roll | game.md, Race view / Projection; original $62454, $624C2, $62518; renderer Projector. 65536 angular units / 32 = 2048 screen pixels per turn. A 256 x 128 window gives 45 x 22.5 degrees. Height conversion /4 is established by frame comparisons. Smooth angle charts are illustrations, not exact original table outputs. |
| 3: angular vs perspective, near edges | Analytic comparison: k*a vs k*tan(a), k=2048/(2*pi), matching centre scale. Angle chart marks the actual horizontal edge at 22.5 degrees. game.md identifies $617D0 offscreen downward correction and $65B80 extension. Reprise subdivides edges instead. No claim that these methods are equivalent at all views. |
| 4: road representation | data/track.cpp, data/track.hpp and game.md Tracks. Road and height diagrams decode Hump Back from the installed program. Counts, dimensions and values are computed from those samples, not transcribed fixtures. |
| 5: drawing order and thinning | game.md Race view, $660B0 traversal and $65BEC thinning into $7ABDA. Left-height departure from neighbours' mean is compared with $A0; the final cross-section is retained. Reprise currently retains all cross-sections. No performance claim or tall-track equivalence claim. |
| Evidence note | game.md reports 15 frame comparisons of Little Ramp and Hump Back, approximately 95–99.8% pixel agreement. Those are the game agent's existing comparisons, not rerun by the lesson task. |

## Article 2: A car balanced on three springs

Source text: resources/articles/stunt_car_racer_physics.txt.
Primary research lead: RetroReverse/stunt-car-racer-amiga.md Part V, checked
against extract/physics/physics.go and execution of the original via physverify.

| Section | Evidence and limits |
| --- | --- |
| 1: common surface, bilinear interpolation | $5C1D0 surface sample, $5C0AA profile builder, $5C554 interpolation, $5C808 edge treatment. Surface checks passed on track IDs 1, 3, 7. UI selects adjacent non-degenerate Hump Back cross-sections with a rise; centre value uses their four imported heights. Rectangle is unfolded, not a to-scale terrain rendering. |
| 2: three contacts, weighted lift and torque | $61B70 contact heights, $61BCC suspension and combine ($61F42). Lift=((A+B)/2+C)/2; difference A-B and pair-average minus C feed turning forces. All corresponding oracle checks passed. Diagram identifies supports A/B/C instead of inferring physical axle orientation from variable names. |
| 3: change term and limits | $6180E uses (delta*276)>>8 plus compression. $61BCC bounds travel, zeros negative force and handles impact thresholds/damage. Example current=400, previous=400/200/600 gives 400/615/184 before limits. Arithmetic right shift rounds the negative product down. This is an illustrative isolated calculation, not a complete physical model. |
| 4: grip, transforms, gravity | $621DA grip, $620B8 drive, $6217A lateral response; $6158C velocity-to-body, $615E6 gravity-to-body, $61618 force-to-world. Corresponding checks passed. Avoids the earlier prose's unestablished claim of a full friction circle. |
| 5: update ordering and managed handling | $6185C main physics step; $61ADC force integration and $61950 position integration; $61012 track-relative correction/pitch assistance. Short original execution sequences verify the compared state, not all races or all possible arithmetic edge cases. |

### Fresh physics validation

Working directory: ../RetroReverse/games/stunt-car-racer-amiga/extract

```
GOCACHE=/tmp/reprise-research-go-cache go run ./cmd/physverify ../extracted/game.dec.bin
```

Result: exit 0, `ALL OK`. The tool uses a deterministic seed and compares
original 68000 routine execution with its research reconstruction. Its initial
routine loop runs 3000 seeded states per routine. Track-dependent setup, surface,
section and coupling cases have separate bounded loops. Final $6185C comparisons
ran 60 consecutive steps on each of track IDs 1, 3 and 7. Surface, suspension,
contact-height, interpolation, drive, lateral tyre, load, matrix, gravity,
integration and the final three frame sequences all reported OK. This is research
coverage of selected states; it does not make the unimplemented Reprise car playable.

### Earlier opponent deferral (2026-10-30)

The previous Part V labels two physics calls as player/opponent; Part VI says
the opponent has no physics. This conflict is not resolved by the rendering
oracle. A fresh `caroracle -in ../extracted/game.dec.bin -id 0` run reported all
32 construction edges matching, but zero visible fills for that camera. It
checks car construction after placement, not speed selection, avoidance,
collisions, overtaking or the main-loop state ownership. Do not turn that result
into an AI lesson. Resume after those behaviours are reconstructed and validated.

## UI validation

The dedicated stunt_lesson_checks target checks packaged article structure with
no imports. With an imported content directory it checks real decoded figures,
960x640, 640x1138 and 1422x640 canvases, fractional wheel movement, directional and
page keys, dragging, scroll bounds and preservation of reading position on resize.
Its optional second argument writes viewport captures for visual review. No
original assets or temporary captures belong in the repository.

### Research source fingerprints

SHA-256 at validation time:

- extract/physics/physics.go: `913c1aefd888b27da107e4d1208b03d422abe895d4a134f1133ccca097ed9ea0`.
- extract/cmd/physverify/main.go: `beb1c492913ad60dca609f5f9343a9bf228bebd153c12dbe5888e8f42bf3e233`.

### Reader validation results

The imported-data reader checks passed at all three aspect ratios. The browser
integration check imported the ADF into an isolated temporary library, opened
both articles through the real launcher, resized each open article into portrait,
and returned to the lesson list. It passed. All eight figures and the final
paragraphs were reviewed in rendered viewport captures; the bearing labels were
spaced apart and the road map gained an enlarged segment inset for legible rungs.
This is desktop/headless SDL validation; Android device execution was not tested.

Commands:

```
build/stunt_lesson_checks /tmp/reprise-stunt-lesson-content /tmp/reprise-stunt-lesson-captures
build/shell_checks --stunt-lessons <user-image.adf>
ctest --test-dir build -R '^(shell_navigation|shell_motion|development_articles|technical_articles|import_progress)$' --output-on-failure
```

## Article 3: A rival built for the road (2026-10-31)

Source: resources/articles/stunt_car_racer_opponent.txt. Four SDL figures,
read-only; no gameplay or save state is created. This resolves the earlier
publication deferral for the established mechanism, not every possible AI case.

The new evidence is the game agent's local `game.md`, "The opponent's driving
(Observed, verified)", and its reconstructed `race/opponent.cpp/.hpp`. Those
files were pending game work during lesson authoring; they were inspected but
are not included in the lesson commit. The article renderer deliberately does
not depend on that new engine API. The original image provenance above remains
unchanged. No outside game disassembly or historical article was consulted.

| Section / figure | Evidence and limits |
| --- | --- |
| 1: progress and lane; imported road | Original frame order recorded at $64F68, movement at $63996, placement at $5A186. `OpponentDriver::drive/place` advances segment-relative progress and samples road heights; airborne status does not switch to a free world trajectory. The Hump Back road figure reuses actual decoded `data::road`, not a new invented circuit. |
| 2: segment pace; override bars | Track-load $5B1B8/$63A8E/$5B1E0; `prepare` warms the generator, rolls track-specific top/curve parameters, and walks segments backwards. Marked pairs affect themselves and a following run in this backwards traversal. `aim_speed/change_speed/push` establish braking, drag, slope and no airborne drive push. Figure reads `Track::segment_marks` directly: low seven bits are the target; high bit distinguishes marked entries. Empty chart positions are not zero-speed targets. These are stored directives, not generated final targets, mph, or a measured speed trace. |
| 3: choices | $6076C shortest-loop gap; $636C0 lane selection. `measure/steer` check near distance, ahead/behind, lateral gap, and habit bits. Dodge/pass branches differ; road look-ahead is conditional, not a universal centre override. Movement is 9 lane units per grounded frame within 32..224, with a 16-unit tolerance. Diagram deliberately suppresses thresholds, exceptions and side-selection details; it is labelled a simplified branch map, not an executable AI. |
| 4: suspension and disturbances | $63EE2 spring calculation and constrained body heights; $5BD1E timed wobble; $5A186 road samples and visible corner heights. Reconstructed suspension includes compression/change term, vertical integration, airborne nose-down adjustment and optional driver hops. These are smaller, distinct mechanics, not "no physics" and not the player's entire simulation. |
| 5: contact / exchange diagram | $63CE4 checks separation and height, creates impulses and spring damage; $63E2E applies the player's impulse; `take_knock` changes rival speed and wheel vertical velocities. Diagram is a causal schematic, not a contact capture or proof of conservation of momentum. |

### Reported original-execution coverage

The game agent records `work/league_lr1.bin`: a 600-frame Division 4 race
against Jumpin' Jack, with compared frames 3–231 and 233–599 matching every
recorded field; two frames were lost around 232. `league_lr2.bin` is a second
318-frame skill roll. `league_touch.bin` uses an opening state written into the
original and blocking inputs, with contact at frame 1319 and all 1342 compared
frames matching. The checker is `stunt_car_racer_physcheck <content> <capture>
<track> race [first] [count]`. These are the game agent's recorded results,
not freshly rerun original captures by the lesson task. They support specific
league races, not exhaustive coverage of all driver habits or collisions.

Source fingerprints at review (SHA-256):

- race/opponent.cpp: `b32addd9a1e80b98aa02b9ec646cff97ea3307412efc2f86ade465344c96c73a`
- race/opponent.hpp: `42b3717d09486f21d9e08a2c7b25ff65233d0b8dee2ba0ac2d5b4d839d6f5727`

The former conflict in the older research is resolved at the level needed for
this article: the rival has its own suspension and speed dynamics, while track
progress and lane replace the player's free vehicle movement. Neither "a second
player car" nor "no physics" describes that combination adequately.

### Opponent article validation

Built `reprise`, `shell_checks` and `stunt_lesson_checks` in an isolated snapshot
of committed HEAD plus only this lesson's seven files, excluding the pending
opponent/game changes. The reader passed with the user's imported program at
960x640, 640x1138 and 1422x640, including fractional wheel input, key/page and
drag scrolling, bounds and resize. The real launcher imported the original ADF,
opened all three chapters and returned to the lesson list successfully. CTest
passed shell_navigation, shell_motion, development_articles, technical_articles
and import_progress. Reviewed the new chart, decision diagram, contact diagram
and final paragraph in rendered portrait captures, plus landscape chart layout.
These are desktop/headless checks; no Android-device run is claimed.

Commands (isolated snapshot):

```
build/stunt_lesson_checks /tmp/reprise-stunt-lesson-content /tmp/stunt-opponent-captures
build/shell_checks --stunt-lessons <user-image.adf>
ctest --test-dir build -R 'shell_navigation|shell_motion|technical_articles|development_articles|import_progress' --output-on-failure
```
