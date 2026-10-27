# Marble Madness lessons — 2026-10-27

These pure-SDL chapters describe the current Amiga reconstruction. They do not
claim universal equivalence with every original release, and contain no code
listings. The laboratory owns a separate Race and View, reads the user's imported
files, freezes the clock, and never writes progress or saves. Article geometry,
routes, pen cells and masks come from that same import, not generated artwork.

## Evidence and scope

Source image and original-execution tools are documented in
`.analysis/marble_madness_amiga/disk.md`; the supplied ADF's MD5 is
735dc697d64b3eeaa000778eb0b1153a. Existing reconstruction observations and capture
coverage are in `.analysis/marble_madness_amiga/play.md`. The prior RetroReverse
exposure disclosed there still applies. The catalogue's external editorial
history was not used to derive these lessons. No external disassemblies, ports
or game-specific guides were consulted for this work.

- **A marble in a painted world:** Terrain and Probe decode the Track height
  field, four quadrant heights per vertex, and two-triangle interpolation
  (`terrain.cpp`, `probe.cpp`; recovered ground probe $10EDD0). The wireframe
  is an explanatory projection of actual terrain, not an original rendering
  mode. Forces/contact follow `marble_rules.cpp` and `Race::roll` ($115470).
  Occlusion follows `marble_covers.cpp` ($11EC42, $11F486) and View's actual
  sprite drawing ($1114F8). Before/after removes only wall covers; other
  course-part masks remain active. The fixture searches for a visible sprite
  with genuinely removed pixels. Imported cover patterns are shown separately.
- **Inside the black marble:** `steelies.cpp` ($11A378, $11A49A, $11A7AE)
  supplies patrol/hunt/chase/return rules, eligibility, distance and recovery.
  The route is read from the spawned steelie's Track path. The approximate
  octagonal boundary depicts the 40-unit chase threshold in the steelie's
  horizontal plane, not collision range or predicted safe movement. The green
  line identifies the current steering target; the red vector extrapolates
  current velocity for eight updates without claiming a trajectory prediction.
  The mode display reads the actual simulation state.
- **How Silly Race bends the rules:** `slope_force` reverses the slope term for
  Silly, while `push` keeps airborne gravity unchanged. The comparison executes
  both native slope branches on copies with identical imported terrain and zero
  horizontal velocity. `silly.cpp` supplies the pen bitmap, critter behavior,
  catch rewards ($11CA44), birds and their animations. Upward funnel rules and
  the reverse scroll are documented in play.md's Silly section. Imported
  animations remain data; their interpreter is unchanged.

## Laboratory construction

Physics opens on a real sloped cell. The cover sample searches the first Practice
view for partial occlusion, checks real before/after pixels, and leaves the user
free to step or steer from that position. Start position restores the normal
course start. Article pages pause simulation; Page Up/Down or the page buttons
remain available while the arrows steer in the lab.

Steelie presets invoke the existing spawn and steering routines for the first
imported Beginner route. They select nearby real ground, reset velocities and
seed eligibility: Patrol uses another zone, Hunt places the player farther away,
and Chase places the player nearby in the same zone. Normal course zone tracking
resumes on the first full world update. The seed is a teaching setup, not a
recording of a player reaching that point. Range alone does not imply chasing.

Silly's Catch sample deliberately places the player on the first spawned critter
in its pen zone. One real world update grants 500 points and 3 clock seconds.
The Slope sample searches imported Silly terrain for a genuine sloped cell and
centers the camera there. Neither setup changes the game module's running race.
The laboratory is silent; it drains its private sound events.

## Validation

`tests/shell.cpp --marble <imported-content> <capture-directory>` checks all three
launcher factories and return paths, paused initialization, stepping/reset,
genuine slope/occlusion examples, visible steelies in the three expected modes,
Silly's catch reward and slope fixture, and continued real world updates.
Headless captures cover every article page, the browser and laboratories.

The existing original-run replay comparisons were rerun for Beginner's steelie
sequence: 298/298 player and creature updates, 232/232 cover lists match. This
revalidates the underlying reconstruction, not the newly fabricated setups.
Practice rolling/falling also matches 598/598 updates and 544/544 cover lists;
the Silly route matches 1049/1049 updates and 655/655 cover lists.
No production physics, enemy decision or rendering rules were changed.

## Limits / follow-up

- A new original-Amiga execution comparison of the fabricated laboratory
  presets has not been recorded. The lessons describe reconstructed mechanics.
- Baiting over an edge is explained from chase steering, momentum and fall
  handling; no guaranteed lure replay is presented. The initial straight-line
  steering probes did not establish a reproducible fall, so removal on scrolling
  out of view must not be called a successful lure.
- The funnel and birds are illustrated explanations, not dedicated controlled
  collision presets. Two-player target selection is explained but not simulated
  in this one-player laboratory.
