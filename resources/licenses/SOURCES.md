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
