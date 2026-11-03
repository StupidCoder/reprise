# Beacon core in Reprise

Follow ../../AGENTS.md, ../../RECONSTRUCTION.md and ../../ADDING_A_GAME.md.
Stable catalog/content ID: `astro_bot_ps4`. Keep the native engine's code in
`beacon` namespaces. Do not change the source Beacon project during integration.

This is a partial handoff: the portable core is compiled here, and the launcher
imports the supplied CUSA12392.v1.04.pkg directly (and legacy .beacon bundles),
and displays animated textured models and music. A playable GameModule is not
connected yet. See docs/package-import.md for exact-release limits.
Read plan.md and game.md before extending it; source-handoff.json pins the copy.
Do not mark the catalog playable until the host integration is validated.

Preserve the original project's evidence caveats and code-versus-data split.
Its project-specific permission to consult AstroQuest remains limited to this
game: attribute any such finding, and do not copy GPL code into the engine.
No new external game-specific research was used for this handoff.

Reprise owns the window, input, audio, presentation and durable saves. The source
Vulkan window/swapchain, private SDL audio device and AVFoundation movie decoder
must not be embedded as the gameplay adapter. Keep data out of packages.
