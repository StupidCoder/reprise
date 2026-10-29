# BlueStreak: speed archive

2026-10-26. An original shell identity; no cartridge graphics or game artwork is bundled.

## Direction

Midnight #0C1627, ice #EBF1F8, electric blue #3167F6, aqua #71E5E2, peach #FFAF75 and violet #BEB4FF. Lato regular for supporting text, bold for titles and focused controls. The oblique wordmark and two swept ribbons suggest speed and a B. The runtime mark is authored geometry and bundled typography in shell_frame.cpp; art/logo.png is its transparent export. This keeps the final lettering and edges precise at the shell's small sizes.

The expanding generation panels lead into a central carousel of numbered geometric editions. The art is abstract, not a claim about any game's scenery. Each hardware generation retains a distinct accent. Long titles have a dark backing. Level selection, sound test, launch cards, pause and enhancements share the same typography and colours.

## Motion and portability

Selection eases to rest over 300 ms without overshoot. A small travelling dash animates the library rail, the pause panel enters over 200 ms, and launch/screen changes use diagonal sweeps. Use --reduced-motion to settle selections immediately, freeze decorative movement and remove the sweeps. Gameplay timing is unaffected.

All controls, labels, icons and branding render into portable RGBA image buffers presented by SDL. Lato is bundled under the OFL; stb_truetype handles trusted bundled fonts only. No AppKit, CoreText or native font APIs are introduced. Resource loading uses SDL IO so assets can be packaged on Android. UI textures use linear scaling; game textures keep nearest-neighbour sampling. The existing SDL ROM-picker service is retained.

## Image generation provenance

Built-in image generation was used for logo exploration. The generated lettering retained distressed edges and did not reliably separate the emblem from the B in the wordmark, so the shipping logo is an original code-native interpretation, not either raster draft. The first exploration is retained in logo-exploration.png; it is not a runtime asset.

Initial prompt:

Use case: logo-brand. Create a polished original logo for BlueStreak, a portable classic platform-game library. Transparent background. Wide horizontal lockup, compact abstract swept-wing B emblem on the left and exact wordmark 'BlueStreak' on the right. The emblem is built from two sharp electric-blue forward-swept ribbons with ice-white negative space, suggesting speed and the letter B. Wordmark: bold condensed italic geometric sans-serif, ice-white letters, excellent legibility on midnight navy. A tiny electric-blue underline streak at the right. Flat precise screenprinted vector aesthetic, no gradients, no glow, no shadows, no mockup, no characters, no existing game logos, no additional text. Keep close balanced spacing; output clean transparent PNG.

Refinement prompt (using the first image as reference):

Refine this BlueStreak logo. Preserve the swept blue B emblem and italic white wordmark composition. Remove ALL grunge, distress, noise, holes, black speckles and thin outlines. Every letter must have a completely solid clean ice-white interior with crisp smooth edges; emblem solid electric blue. Ensure exact full text 'BlueStreak' to the right of the emblem (including a separate capital B at the start of the wordmark). No other text. Flat professional vector style. Preserve transparent background.

## Verification

Build and ad-hoc-signed macOS bundle passed. CTest shell_checks covers all 13 catalog entries, animated carousel hit regions, level selection targets, pause/enhancement targets, symbol coverage, bundled typography and reduced motion without ROM data. Render captures in ignored .analysis/shell-design cover home, library, planned titles, long GBA titles, level selection, music, pause, enhancements and launch.

Run:

    cmake --build build --target shell_checks
    ctest --test-dir build --output-on-failure
    build/shell_checks .analysis/shell-design

Live macOS checks also verified library → level selection → gameplay → pause → enhancements → library. The pause header reserves room for the FPS badge.

Android, Windows and Linux builds and physical controllers have not been tested. Existing gameplay/reconstruction code was not changed.
