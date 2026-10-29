# Application targets

The root CMake project builds the SDL desktop application `reprise`.
`tools/package_macos.py` packages `build/Reprise.app`, with fonts, original
cover artwork, SDL and licences, but no source images or imported game data.

Android and portrait layouts are a follow-up; preserve Picarat’s existing
Android target until a replacement is validated. Diorama XR will remain a
separate OpenXR frontend sharing its engine and renderer.
