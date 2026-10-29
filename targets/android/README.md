# Android test builds

Run `./build-android.sh` from the repository root. Output: `build-android/Reprise.apk`.
The script builds the currently connected CMake game modules, SDL and FreeType,
packages only curated `resources/`, and signs the APK with your standard local
Android debug key. No game image, extracted game assets or save is bundled.
This is a directly installable development APK, not a Play Store release.

## Prerequisites

- Python 3, CMake, Ninja, curl and JDK 17.
- Android SDK platform `android-36`, build-tools `36.1.0`, platform-tools.
- NDK `29.0.14206865` (override with `ANDROID_NDK_HOME` if needed).
- Set `ANDROID_HOME` or `ANDROID_SDK_ROOT` for a nonstandard SDK location.
  macOS defaults to `~/Library/Android/sdk`; Linux to `~/Android/Sdk`.
- macOS automatically selects JDK 17; elsewhere set `JAVA_HOME` as needed.

SDL 3.4.16 and FreeType 2.14.1 downloads are pinned by SHA-256 and cached under
`build-android/deps`. Subsequent builds are incremental. Native code is Release;
the application remains debuggable for device testing. No Gradle installation
is required: the script uses the SDK's javac/d8/aapt2/zipalign/apksigner tools.

## Install and update

Copy the APK to the device and open it, allowing installation from that file
manager when Android asks. Alternatively, enable USB debugging and run:

```sh
./build-android.sh --install
# Choose one device when more than one is attached:
./build-android.sh --install --serial DEVICE_SERIAL
```

Updates use the same package (`org.stupidcoder.reprise`) and local debug key,
preserving imported games and saves. Keep `~/.android/debug.keystore` if moving
the build to another machine. Uninstalling removes private application data.
Use `--version-code N` for a higher Android version code if desired.

## Scope

- ARM64 (`arm64-v8a`), Android 8/API 26 minimum, target API 36.
- Intended for AYN Thor and Galaxy S26 Ultra; physical hardware validation pending.
- Pure SDL launcher and game rendering. SDLActivity supplies Android lifecycle,
  audio, touch and gamepad input. No game-specific mappings or virtual controls
  are added here. Existing game integration availability still applies.
- The main collection shelf supports portrait and landscape rotation, with
  two columns and swipe scrolling in portrait. Selection survives rotation.
  Details and documentation reflow with scrollable text, save previews, asset
  viewers and lesson lists. Individual lesson experiments and in-game menus
  retain their original canvas, letterboxed in portrait. Dual-screen presentation, safe-area refinements and individual
  game input adaptation remain follow-ups.
- Android's system document picker is accessed through SDL. Selected content
  URIs are copied to private disposable cache for existing filesystem importers;
  up to 1 GiB per selection, replaced by the next selection. Imported games and
  saves use app-private persistent storage, with no broad storage permission.
- Both native libraries and the APK's uncompressed native entries are verified
  for 16 KB alignment. Signature verification runs after every build.
- Build metadata and APK checksum are in `build-android/build-info.json`.

References: [SDL Android](https://wiki.libsdl.org/SDL3/README-android),
[Android page sizes](https://developer.android.com/guide/practices/page-sizes).

## Validation (2026-10-28)

The signed 63.8 MiB APK built and installed in an ARM64 Android emulator; the
activity started successfully and the process remained running. ELF LOAD and
APK ZIP alignment plus v2/v3 signatures passed. The macOS bundle rebuilt and
all four CTest checks passed (navigation, motion, saves, Spy Mouse module).
No physical devices were attached. End-to-end document imports and gameplay
have not yet been exercised on Android; emulator startup is not game validation.

2026-10-29: rebuilt APK with collection-shelf reflow and Android rotation.
All four desktop tests pass, including portrait tab/card hit testing, swipe
versus tap handling, return from details and selection preservation on rotation.
Portrait Layton preview visually checked. Physical rotation testing pending.

2026-10-29: portrait details and documentation now share one cover/header,
with touch-sized actions, vertically scrolling descriptions/development notes,
save screenshots, asset browsing/audio/model controls and lesson selection.
Desktop navigation/motion/save/module checks cover the new portrait paths.
