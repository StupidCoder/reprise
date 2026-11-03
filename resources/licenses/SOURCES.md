# Portable typography sources

Retrieved 2026-10-26 from the upstream sources below. stb_truetype v1.26 is used only for Joyport's trusted bundled fonts; imported game files never enter the font parser. No platform UI or platform font API is used. Covers are decoded by SDL 3.4, and all text and controls are rendered into the SDL image buffers.

- https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h
- https://raw.githubusercontent.com/google/fonts/main/ofl/lato/Lato-Regular.ttf
- https://raw.githubusercontent.com/google/fonts/main/ofl/lato/Lato-Bold.ttf
- https://raw.githubusercontent.com/google/fonts/main/ofl/lato/OFL.txt

SHA-256 of the shipped files (relative to the project root):

- `src/third_party/stb_truetype.h`: `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab`
- `resources/fonts/Lato-Regular.ttf`: `d636e4683231f931eda222d588e944d082bfd3bdba02f928bee461c0f185b251`
- `resources/fonts/Lato-Bold.ttf`: `8a0aace75d33794eece4b28187bfc1df0bbd2888b5d8a56e01788c8d65d16be1`
- `resources/fonts/OFL.txt`: `74ba064d03f1f1c4a952da936c3eb71866c34404916734de3cae73b34357e59e`

# AAC decoder tables (Spy Mouse music)

Spy Mouse's music is HE-AAC; its decoder (`games/spy_mouse_ios/src/media`) is written for Reprise, but the standard's code tables in `aac_tables.cpp` are derived from the Android Open Source Project's PacketVideo AAC decoder (platform/frameworks/base, tag android-4.0.4_r2.1, media/libstagefright/codecs/aacdec; Copyright (C) 1998-2009 PacketVideo), under the Apache License 2.0: `AOSP-AAC-tables.txt`.


## Embedded 3DS reconstruction

`games/captain_toad_3ds` incorporates the user's Diorama reconstruction at
`0816c17a53afe613a0fea728872645c727120699`, under its MIT license
(`Captain-Toad-engine-MIT.txt`). The source manifest pins the original files.
No original game images or extracted assets are distributed with this code.

- Captain Toad material compilation: glslang 16.6.0 (glslang.txt) and SPIRV-Cross vulkan-sdk-1.4.328.1 (SPIRV-Cross.txt), pinned source archives in games/captain_toad_3ds/CMakeLists.txt. Statically linked on macOS and Android.

## Beacon portable core

Source: the user's Beacon project, commit
`89f52f6c105a2a57a56a9786cb27ca5768d5f4d1`; per-file hashes in
`games/astro_bot_ps4/source-handoff.json`. Beacon's own code had no selected
license. No game data or GPL AstroQuest code is included.

- LibAtrac9: https://github.com/Thealexbarney/LibAtrac9, commit
  `efca2e3af35562a09a9bb6deed90e45b4b824dc4`, MIT (`LibAtrac9.txt`).
- HE-VAG coefficients: vgmstream `src/coding/psv_decoder.c`, provenance and
  permission notice retained in the source header and `HE-VAG-coefficients.txt`.

- Beacon character texture previews: bcdec, preserved from Beacon’s pinned source (`src/third_party/bcdec.h`), MIT alternative (`bcdec.txt`).

- Astro Bot PKG import uses Mbed TLS 3.6.4, commit `c765c831e5c2a0971410692f92f7a81d6ec65ec2`, from https://github.com/Mbed-TLS/mbedtls (Apache-2.0 alternative; `Mbed-TLS.txt`). Its framework submodule is pinned by that commit. zlib uses the existing platform dependency. Public fake-package RSA numeric parameters were verified against https://github.com/maxton/LibOrbisPkg at `643477263b2644e0803e0f58b8726ea4e3f3b7d4`; no library code is included or linked.
