# Beacon reconstruction and handoff

Stable identity: `astro_bot_ps4`. This directory starts the Beacon integration;
it provides a character/music asset importer and viewer, but no playable Reprise gameplay.

## Source and media identity

Pinned clean Beacon commit: `89f52f6c105a2a57a56a9786cb27ca5768d5f4d1`.
`source-handoff.json` records each original file's SHA-256 and any extraction.
Own C++ namespaces are nested under `beacon`; original third-party files are
unchanged. Source files, ROMs, extracted assets and evidence in Beacon remain intact.

Source research identifies the user's `CUSA12392.v1.04.pkg`, 7,486,636,032 bytes,
SHA-256 `028c88e1dd07efcebd085d5a91ed9e4ab7b3d192d7ce4f0cc94958b568e807e2`.
Content ID: `EP9000-CUSA12392_00-PLATFORMERVR00EU`. The package filename/user
version is 1.04; the observed metadata reports APP_VER 01.03. Preserve that
qualification in release detection. These identities come from Beacon's
`.analysis/evidence/hashes.txt` and `docs/probe.md`, not a new package extraction.
The extracted executable hash is
`95ed2d85c176398cabab0ef2b565c2713fd536dcdb7cb8ba0b8c5bedc5831500`.

## Recovered core and evidence

Data readers cover GNF textures, JXMD models, skeleton/animation data, object
and level XML, resource archives, physics bodies, serialized collision and sound
banks. Native gameplay includes the hero, level links, moving joints, coins,
rescues, boxes, glass blocks, enemies, respawn, player rig and music decisions.
Definitions, tuning, placements and animation remain loaded from original data.
The simulation uses the original fixed 1/60 second cadence; the physics target
retains non-fused floating-point arithmetic to preserve support-point ties.

`docs/research/` preserves Beacon's curated findings, hypotheses and omissions.
References there to `.analysis/docs` correspond to these copied notes; references
to `../evidence` or `.analysis/tools` still refer to Beacon's local research
archive. That archive is not required to build Reprise. Reproducing original-ROM
comparisons requires the user's media and those recorded harnesses; this handoff
only checks preservation of the existing native reconstruction. No new original
execution or independent recovery is claimed. Prior AstroQuest findings remain
attributed in the copied notes under the source project's explicit exception.

Beacon's ROOFTOPS scene is an existing standalone playable slice, with substantial
known gaps; see `docs/research/backlog.md`. That is not Reprise playability.
The handoff excludes the standalone app, renderer, platform, video and ImGui.

## Randomness and fidelity limits

Preserved from the source: enemies use owned xorshift state as a stand-in for the
original engine RNG; treasure boxes use `mt19937` seeded by placement index;
glass blocks seed `mt19937` from the authored Seed attribute (default 5489).
`uniform_real_distribution` sampling is not promised identical across standard
libraries. Some randomness affects debris/coins or enemy behavior, so this is
not blanket cosmetic equivalence. Original sequence fidelity, cross-platform
replays and serialization of generator state remain unvalidated. No RNG change
is introduced by this handoff. Source comments mark inferred ranges and rules.

## Integration requirements still open

The character viewer imports a versioned .beacon bundle of original model,
texture, skeleton and clip files plus release metadata. It samples original clips,
builds skinning palettes and applies authored facial UV channels. The shared
viewer renders base textures and material tint with simple lighting; it does not
reproduce the original PBR renderer, particles, shadows, glow or transparency
ordering. Frames use fixed bind-pose bounds and follow root motion to keep the
model centered. Unsupported animations report an error and show bind pose.

Direct package/PFS extraction, full-game data imports, host gameplay/render/audio
adapters, portable movie decoding, quick saves, gameplay touch controls and
mod/save compatibility remain open. No gameplay feature badges or persistent-save
allowlist entry are enabled. After bundle import the viewer only reads Reprise's
installed content, with no dependency on Beacon or the export file.
See `docs/asset-viewer.md` for the envelope, controls and measured coverage.

Lesson candidates: authored object links and visibility, collision support-point
rounding, variable-height jump states, compressed skeletal animation and portable
sound-bank decoding. These are reconstruction topics, not original source code.
