# Collection atmosphere artwork

Original AI-generated bitmap artwork, created 2026-10-28 with the built-in
image generator using the approved design/reprise/*-collection.png mockups as
style references. No original game data or commercial cover art is included.

- layton.png: engraved city map, river and bridges on ivory parchment.
- sonic.png: blue speed ribbons, gold arcs, checker motif and a small coastal
  loop landscape on pale blue paper.
- commodore.png: interpretive hardware design sketches on grey-green paper.
  These are decorative illustrations, not accurate hardware documentation.

The full-frame backgrounds contain no functional controls or labels. SDL
loads them as PNGs, caches a filtered resize, then draws live navigation and
cards above them. A feathered paper wash preserves text contrast. Backgrounds
stay fixed during shelf scrolling and respect the existing page transitions.
