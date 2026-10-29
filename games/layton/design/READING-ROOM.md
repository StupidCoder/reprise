# Reading-room shell — 2026-10-26

The shell keeps its existing logo and illustrated title cards, surrounded by
ivory paper, dark teal controls and brass rules. Fine borders and restrained
shadows replace the heavier controls. Lato Regular and Bold provide smooth,
density-aware text. A shared outline icon alphabet replaces the bitmap icons,
with rounded strokes rasterized at device density. Icon-only buttons use
explicit square bounds to centre their artwork independently of text baselines.

## Portable rendering

All controls remain drawn into the shell canvas and presented through SDL.
Only the platform layer handles font resource IO and rasterization, using
bundled stb_truetype. No native UI controls or system fonts were introduced.
Font and stb licenses ship with the app. Text measurement avoids rasterization;
rendered labels use a bounded 16 MiB cache.

The library retains its landscape design and scrolling portrait layout, along
with the existing Android density and safe-area handling. Dialog buttons are
44 layout units tall and stack when their combined width will not fit. The
pause menu has an explicit Continue playing action, and uses two columns on
short landscape windows. Existing swipe momentum remains; no continuous idle
animation was added. Original game presentation and rules are unchanged.

## Validation

- Release desktop build succeeded; all 20 CTest tests passed.
- The new shell layout test renders library, hub, puzzle collection, pause menu
  and dialogs for 1080×720, 390×844, 844×390 and 600×800 viewports. It checks
  font coverage and measurement, viewport bounds, and pause-menu actions.
- Captures were visually inspected, including portrait hubs, stacked dialog
  buttons and the compact landscape pause menu. Captures can be regenerated
  with `build-release/shell_layout_tests .analysis/shell-polish`.
- Live desktop keyboard navigation was checked from library to game hub,
  into a game, through the pause menu and back to the collection.
- The ARM64 Android APK rebuilt and passed signature verification with fonts
  bundled. No Android device or emulator was connected for this change;
  on-device rotation, safe areas and touch interaction remain to be checked.

Existing concurrent game-engine work was left intact. These checks do not
change any game's recovery or gameplay-completeness claims.
