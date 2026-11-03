# Licensing scope and exceptions

The root [LICENSE](LICENSE) is the MIT license for Reprise's original code and
documentation, copyright © 2026 StupidCoder. It does not relicense third-party
material or original game/system data. Preserve applicable notices when copying
or redistributing components.

## C64 and Amiga system-font exception

The following embedded glyph data are **not covered by Reprise's MIT license**:

- `shared/c64/character_rom.cpp`: the included Commodore 64 character-ROM glyphs.
- `shared/amiga/topaz.cpp`: the included Amiga Kickstart Topaz 8 and Topaz 9 glyphs
  and associated font-data tables.

Their original rights remain with their respective rights holders. Reprise's
MIT license grants no additional permission for these data. The source files
record their provenance and the project owner's inclusion decisions.

The same existing exclusion applies to the Kickstart system-gadget imagery in
`shared/amiga/intuition_imagery.cpp`; it must not be mistaken for MIT artwork.
These exceptions concern the original data, not a blanket exception for all
new C++ code in the corresponding platform directories.

## Third-party components

- Lato interface fonts: SIL Open Font License, in `resources/fonts/OFL.txt`
  and the corresponding imported engine resource folders.
- stb_truetype: its supplied license, in `resources/licenses/stb_truetype.txt`
  and source headers.
- HE-AAC standard code tables derived from AOSP/PacketVideo: Apache License 2.0,
  in `resources/licenses/AOSP-AAC-tables.txt` and
  `games/spy_mouse_ios/src/media/LICENSE-AOSP-AAC-TABLES.txt`.
- SDL, FreeType, zlib and other linked dependencies retain their upstream
  licenses; bundling them does not turn them into MIT-licensed Reprise code.

Further source/provenance information is in `resources/licenses/SOURCES.md`
and component-local notices. The imported engine folders retain their original
MIT license files and third-party notices. Original catalog illustrations have
provenance recorded in `resources/covers/PROVENANCE.md`.

User-supplied game images and extracted game assets remain the property of their
rights holders and are not licensed by this project. No trademark rights are granted.

## Beacon core handoff

The user's Beacon source copied into `games/astro_bot_ps4` did not have a
selected license at the pinned source commit. This handoff does not assign it
Reprise's MIT license. Its third-party LibAtrac9 decoder (MIT) and HE-VAG
coefficient table (vgmstream permission notice) retain their own notices, also
in `resources/licenses/LibAtrac9.txt` and `HE-VAG-coefficients.txt`.
