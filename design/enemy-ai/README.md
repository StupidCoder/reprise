# Bruce Lee: behind the decisions

Open Details for the imported game, then select Behind the enemy decisions
from the lesson list with Enter or a click. This
prototype is drawn entirely with the existing SDL presentation layer.

The chapter runs a separate reconstructed World using the user's imported
assets. It never starts the normal game session or writes saves/options.
Practice actors are placed in room 1; replacement enemy arrivals are held back,
enemy hits on the player are harmless, and player fire is disabled. Sound is
not played. An exit, death or restart request pauses the scenario for reset.

Use the arrows/WASD to move and jump; Enter runs/pauses. Click Step to advance
one logic pass (three PAL frames), Reset to recreate the scenario, the speed
button for quarter/full speed, and the enemy name to switch the inspected
actor and recreate the setup. Click a decision gate for an explanation.
The height preset starts the player airborne above grounded enemies.

## Meaning of the graphics

- Labels over both enemies describe current reconstructed moves.
- A directional band for the inspected enemy shows the horizontal and vertical
  region used to choose an attack. It is not a hitbox. Red means an attack
  move is playing; it does not mean a hit occurred.
- The white vertical mark is the actor's coordinate origin. Sprite poses can
  extend to either side of it; the overlay intentionally uses the same origin
  as the decision comparisons.
- The diagram holds the last sampled decision until the next one occurs.
  Reached gates are blue; bypassed gates explicitly say they were not reached.
  The result is reported directly by the branch taken, not inferred from a
  sprite pose. Delay and remaining steps explain why the picture can continue
  moving while the diagram stays unchanged.

## Evidence and validation

Explanations are derived from the current reconstructed Actors::think and
wake_enemies, whose recovery annotations point to the supplied image; see
.analysis/bruce_lee_c64/game.md for the existing source provenance. The chapter
adds observation only to the decision routine. It does not expose assembly,
C++ or guest addresses, and it does not claim the reconstruction is perfect.
No outside game-specific material was consulted for this chapter.

The shell navigation test passes. With the user's imported data, the optional
shell_checks invocation checks Ninja strike boundaries, Yamo chop/kick
boundaries, height/facing decisions, pause and one-pass stepping. It also
renders initial, running, close, height and enemy-switch scenarios, plus entry
and exit through the actual launcher. These captures were visually reviewed.

Run locally with:

    build/shell_checks /path/to/imported/bruce_lee_c64 /path/to/capture-prefix

This validates the presentation against the current reconstruction, not against
an additional original-ROM execution. Further original comparison, other
chapters, audio, and touch/gamepad-only teaching controls remain open in PLAN.md.
