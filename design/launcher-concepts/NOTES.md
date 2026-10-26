# Joyport launcher design concepts

2026-10-26. These images are archived design proposals. The collector's shelf direction has now been implemented in the SDL shell; validation is in `.analysis/shell-design/validation/REPORT.md`.
Generated with the built-in image generation tool.

## A — Cinematic gallery

Original painterly portrait covers on a midnight-indigo shell. Four supported titles take priority; planned titles remain a compact secondary section. Precise selection outline, readable status captions and a persistent Play/Options area.

## B — Collector's shelf

Original screenprinted illustrations on a warm grey shell, with petrol text and cobalt selection. A two-column layout gives the covers room at smaller window widths.

Selected by the user on 2026-10-26. Extend the screenprinted art direction to the planned games as separate text-free cover illustrations.

## Implementation considerations

These are interpretive AI illustrations, not recovered game assets, screenshots or evidence about the games. Do not reproduce original logos or box art. Keep titles, status and controls rendered separately from artwork in an implementation. Preserve gamepad/keyboard focus; adapt column count or scroll to keep every card accessible. Marble Madness remains a data viewer; planned titles must not imply playability. Retain the existing Joyport connector mark in production; generated marks are sketches.

The old 960x640 shell clipped the fifth Amiga card. The implemented two-column library now scrolls and pages through the full collection. Neither mockup is an implemented or interaction-tested UI. The dark mockup's Windows-like window controls should become native macOS chrome in implementation.

## Exact generation prompts

### A

Use case: ui-mockup
Create a polished high fidelity desktop game launcher design concept, landscape 1536x1024, shown straight on as the UI itself with no monitor or desk. Product name Joyport, a personal native C64 and Amiga game library. Design direction: cinematic illustrated game gallery, sophisticated midnight indigo #111529, pale ice text #E6EBFA, soft periwinkle selection #B4BCFF, restrained warm gold in art. Quiet clean modern humanist sans typography for interface, small pixel-style Joyport wordmark and small nine-pin joystick connector mark. Original commissioned-looking painterly cover artwork is the focus, no copied game logos or box art. Compact top header Joyport, Library tab, Import game button, small settings icon. Below header filter tabs All games, C64, Amiga. Main area is a beautiful orderly four-column grid of four large portrait artwork cards filling most of width; below that a slim planned-games section and a fixed selected-game action footer. Covers: Fort Apocalypse — small helicopter descending into a huge dark rocky subterranean fortress, amber beams, tiny rescue silhouettes; Bruce Lee — athletic martial artist silhouette leaping between dark temple platforms and hanging golden lanterns, dramatic teal and amber, no celebrity portrait; Wizball — whimsical luminous green spherical creature with expressive face floating through a surreal rocky alien landscape with airborne colourful paint droplets, violet and green; Marble Madness — striking white glass marble on impossible isometric stepped geometric courses over deep cobalt space, crisp ivory architecture. These are interpretive mood illustrations, not gameplay screenshots. Integrate consistent art direction with simple plain-text titles beneath art and small machine labels. Status text under each: first three C64 / Ready to play, fourth Amiga / Data viewer. Fort Apocalypse selected with a precise periwinkle outline, others no thick borders. Planned games compact text-only strip: Mindwalker, Cannon Fodder, Rock 'n Roll, Wings of Fury; visibly marked Planned. Bottom action area Fort Apocalypse, Ready to play, prominent Play button and secondary Options. Small keyboard hints bottom left. Generous spacing, mature native desktop software, all content fully within window, clean typography, no fake reviews, ratings, store prices, badges, gradients as decoration, no original publisher marks. Entire UI legible, not a website landing page.

### B

Use case: ui-mockup
Create one exquisite high fidelity desktop launcher UI mockup for Joyport, landscape 1536x1024, straight-on flat full-window screenshot, no physical monitor. Direction: collector's cartridge library, inspired by vintage Commodore industrial design and exceptionally designed screenprinted cassette sleeves. Warm light grey plastic #D9DBD5 background, dark petrol #153B47 text, cobalt #294BC2 selection, yellow #E7BD52 sparingly. Large friendly restrained geometric sans Joyport wordmark with nine-pin D-sub connector icon, clean sans interface type, no copied publisher logos or original game box art. Header Joyport at left; Library, Import game, Settings at right. Left narrow navigation with All games selected, Commodore 64, Amiga, Planned. Main panel Your library, followed by 2x2 grid of large landscape illustrated sleeve cards. Each card artwork occupies top two-thirds, plain text caption below. Artwork is bespoke 3-colour screenprint with wonderful tactile halftone, economical bold silhouettes and carefully composed dramatic angular shapes, expressive sophisticated graphic design rather than photorealism. Fort Apocalypse card: angular dark helicopter descending into a cavern with amber spotlights, dark blue and mustard and ivory. Bruce Lee card: black martial artist silhouette mid kick between architectural platforms with hanging red lanterns, vermilion and ink and ivory, no celebrity likeness. Wizball card: expressive green spherical character in surreal rolling rocky landscape, orbiting paint drops, emerald cobalt pale yellow. Marble Madness card: a marble rolling on stepped isometric architectural tracks over a void, cobalt salmon ivory. Each title is set as ordinary text in the SAME modern sans family, no imitation logos. First three captions C64 and Ready to play; Marble Madness caption Amiga and Data viewer. Bruce Lee selected with crisp cobalt focus frame. Bottom persistent selected-game strip Bruce Lee, C64, prominent cobalt Play button and quiet Options button. Small footer keyboard hints. All four covers equally beautiful and carefully aligned, plenty of breathing room, cards fully visible, typography very legible. This is functional desktop software, no marketing headline, no fictional ratings or statistics, no unnecessary ornaments, no glossy gradients or glowing neon. Interpretive cover-art concepts only, not gameplay imagery. No window controls in the top right; use discreet macOS traffic lights at top left.

## Final logo

The generated mockup's connector is a sketch only. The implemented SDL mark uses a recognizable D-sub outline with exactly nine contacts: five above four. A rendering test checks the contact count.
