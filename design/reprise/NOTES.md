# Reprise — unified collection concept

Visual mockups only; no application code, imports or game modules were merged.

Catalog basis: Joyport 11 entries, BlueStreak 13, Picarat 6, DoubleOCheese 1: 31 entries including planned and partial implementations. The library shows nine representative entries in a curated view, not a complete list or a claim that every title is playable. Platform editions remain separate entries. Source: each project's local catalog and README.

Design: retain the collector's shelf with paper grey #DCDED7, ivory #F3F2E9, petrol #153B47, cobalt #254ECF and muted teal. Humanist sans typography, illustrated horizontal covers, quiet chrome. A lifted two-layer game tile with connected diagram nodes replaces the hardware-specific Joyport mark. Collections are navigation filters within one library. The details concept keeps gameplay actions on the left and discovery content on the right. Spy Mouse lessons are explicitly conceptual.

Generated with the built-in image_gen tool. Original AI-generated mockup illustrations; not shipped game assets or verified gameplay captures. The in-game save illustration is conceptual. These mockups do not establish name availability. The screenprint and reading-room artwork styles were informed by existing local project artwork.

Final files: `library.png`, `spy-mouse-details.png`.

## Library prompt

Use case: ui-mockup. Create a beautifully polished high-fidelity desktop game-library application mockup, straight-on full screen UI only, no device, no desk, no perspective. Landscape 16:10 aspect ratio, high resolution with legible carefully typeset text. App name exactly "Reprise". Tagline exactly "Classic games, rebuilt and revealed." This is a visual concept for integrating 31 game entries from four native reimplementation collections into one app: Commodore classics (11), Sonic collection (13), Layton collection (6), Spy Mouse (1). No old app names, publisher logos or original box art.
DESIGN: evolve the collector's shelf into an elegant tactile archive. Warm grey-green paper background #DCDED7, ivory cards #F3F2E9, deep petrol text #153B47, cobalt selection #254ECF, muted teal secondary labels, sparing ochre detail. Crisp humanist sans typography similar to Lato, bold simple Reprise wordmark, no serif text, no monospaced text, no glassmorphism, no gradients, no neon, no macOS native widgets. Fine subtle card shadows, small corner radii, generous alignment, mostly flat interface. Artwork supplies vivid color; UI remains quiet.
NEW LOGO top left: a compact two-layer square game tile; upper solid petrol square slightly lifted/offset, exposing a cobalt underlayer with a few connected diagram nodes. Clever simple flat silhouette suggesting a game revealing its inner workings. NOT a joystick connector, not a replay arrow or play triangle.
LAYOUT: top bar logo and "Reprise", tagline to its right, search field and "+ Import game" at upper right. Left sidebar about 215px, main library to right. Sidebar: "All games" selected with count "31"; "Continue playing"; thin rule; heading "Collections"; "Commodore classics" count 11; "Sonic collection" count 13; "Layton collection" count 6; "Spy Mouse" count 1. Bottom small unobtrusive line "Your games. A closer look."
Main heading "Your library", controls "All platforms" and "Curated view". Exactly NINE beautiful illustrated game cards, 3 columns by 3 rows, broad horizontal original artwork with concise title and platform BELOW artwork. Match all card typography and sizes. Order:
row1 "Bruce Lee" / "Commodore 64": vermilion and ivory screenprint martial artist silhouette and lanterns; "Sonic the Hedgehog" / "Game Gear": original cobalt and emerald screenprinted swift blue hedgehog silhouette, gold rings and graphic loop landscape; "Curious Village" / "Nintendo DS": golden-ochre illustrated impossible hillside village, clock tower and winding bridge.
row2 "Marble Madness" / "Amiga": cobalt marble on angular ivory and coral isometric ramps; "Sonic Triple Trouble" / "Game Gear": energetic violet and teal screenprint two small running animal silhouettes, spiral ramps and rings; "Pandora's Box" / "Nintendo DS": sepia teal and brass illustration of a mysterious steam train, mountain viaduct and glowing compartment windows.
row3 "F/A-18 Interceptor" / "Amiga": original orange cobalt screenprint banking twin-tail Hornet over bay; "Lost Future" / "Nintendo DS": sepia teal illustrated clockwork city and great clockface; "Spy Mouse" / "iOS": original mustard petrol and ivory screenprint a tiny tuxedoed mouse following a dotted curling route toward cheese while a huge feline shadow lurks in angular domestic architecture.
Spy Mouse card is selected with crisp cobalt outline and a barely lifted shadow. No fake play buttons on cards; clicking a card leads to details. Bottom narrow selected-game bar: small Spy Mouse art thumbnail, "Spy Mouse", "iOS · DoubleOCheese collection", a tasteful "Details" button on right, no Start/Continue buttons on library. Keep all text readable and artwork richly textured like collectible illustrated prints. No huge hero banner, no charts, no ads, no glossy game-store clutter. Show all nine cards fully. This should look like a real considered app screenshot ready for a design review.

## Library refinement

Refine this Reprise launcher mockup with three small, precise changes. Preserve the entire existing layout, text, logo, all nine artworks, colors, typography and spacing. 1. Replace the Commodore C= logo beside "Commodore classics" with an original small nine-pin D-sub connector silhouette (five upper contacts, four lower), not a manufacturer's logo. 2. Replace the three-circle mouse-head icon beside "Spy Mouse" with an original small side-view mouse silhouette with a pointed nose and curved tail, not a famous character logo. 3. In the selected-game footer beneath "Spy Mouse", replace "iOS · DoubleOCheese collection" with "iOS · Partially playable". Keep everything else exactly the same. This is a visual concept, no app implementation.

## Details prompt

Use case: ui-mockup. Transform this Reprise library screenshot into a companion GAME DETAILS screen for Spy Mouse, preserving exactly the same Reprise logo, typography, colour palette, header style, tactile paper-and-ink visual quality, screen aspect ratio and desktop screenshot presentation. This is a future-design concept, not a claim that integration or lessons are implemented. Keep the top Reprise header and tagline. Remove the collections sidebar and library grid. Add a small "< Library" button below the header. Left column approximately 330px wide: the same mustard and petrol screenprinted mouse/cat artwork from the reference's Spy Mouse card, title "Spy Mouse", platform "iOS", a restrained "Partial support" badge, small honest text "Supported chapters available", side-by-side buttons "Start" and "Continue", then wide "Documentation" button and quiet "Remove game files" text action. No fake progress values or dates.
Right main column: heading "A small agent. A big plan." then description "Draw a route, gather the cheese and slip past the cats. A steady hand and a clever escape plan can make all the difference." Below: a polished wide saved-game panel heading "Continue your game", a tasteful miniature illustrated top-down domestic maze with small mouse, cheese and cats as a CONCEPT screenshot (no actual original assets), text "Your last saved moment", small "Continue" button.
Below that, a restrained editorial teaser area headed "Behind the game" with small badge "Lesson concepts". Two horizontal tiles, illustrations and concise copy: first "How a cat spots you" with top-down cat sight-cone diagram, dotted route and simple labels "Patrol", "Notice", "Chase", subtitle "Sight, pursuit and the route back."; second "From finger to footsteps" with raw dotted stroke and smooth curved path, subtitle "How a drawn route becomes movement." This is a preview of documentation, not a live running game or code listing. Maintain ample whitespace, legible restrained typography, subtle rules, no huge new hero artwork. Keep the cover visually anchored on the left, actions grouped clearly. NO editor code, no implementation details, no macOS native UI controls, no publisher logos. The overall screen should communicate play, continue, and curiosity together while feeling calm and immediately usable.


## Shelf refinements — 2026-10-29

Continue playing uses the validated save record's UTC timestamp, descending,
with release/title tie-breaking; copied-file modification times are irrelevant.
The Sonic collection has an explicit four-game order and a Sonic Advance GBA
preview. Its engine remains under games/sonic awaiting the shared-host adapter.
Unimported covers are desaturated with a paper wash and a readable status badge;
imported covers retain full colour. Details and import actions remain accessible.
The top-left Reprise mark now follows library.png's original rounded layers,
circular connected nodes and small blue branch. Geometry is coverage-sampled
at 8x8 samples per logical pixel, then cached; no platform-native drawing.
