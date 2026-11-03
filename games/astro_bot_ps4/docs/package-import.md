# Direct package import

Reprise accepts the user's `images/CUSA12392.v1.04.pkg` through Import or drag/drop.
No export step, Python runtime, Beacon installation, pre-extracted folder or user
key file is required. The existing `.beacon` bundle reader remains compatible.
This imports the character/music viewer's data, not playable game execution.

## Supported input and extraction

The supported merged package is exactly 7,486,636,032 bytes with SHA-256
`028c88e1dd07efcebd085d5a91ed9e4ab7b3d192d7ce4f0cc94958b568e807e2` and content ID
`EP9000-CUSA12392_00-PLATFORMERVR00EU`. Metadata still reports APP_VER 01.03; the
user's filename/version is 1.04. Another title is rejected as NotThisGame;
a different/truncated revision or damaged copy is reported as unsupported.
The entire package is hashed with bounded buffers before nested data is parsed.
This deliberate exact-release restriction also rejects modified filesystem data.

The original Beacon research reader (`.analysis/tools/pkgfs.py`) established the
PKG entry table, RSA/AES-CBC image-key envelope, AES-XTS outer PFS, PFSC deflate
blocks and inner PFS directory layout. The new C++ implementation bounds reads,
metadata counts, directories, paths, sizes, compressed output and caches. It
uses FileSource for large-file/random access, including Android document URIs,
and Library's transactional staging, manifest verification and replacement.
The package and Beacon sources are not changed.

The outer image uses mode 0xD, 64 KiB blocks and four inodes. Its
`uroot/pfs_image.dat` inode records a 7,463,161,332-byte compressed extent and a
15,238,365,184-byte expanded size. The inner PFS uses mode 0x8 and 64 KiB blocks.
The reader tries the two observed key-derivation forms and validates the root
inode, preserving the source research's documented flag inconsistency.

Asset selection follows the same rules as `tools/export_assets.py`: character
asset directories, original base textures discovered from model material tables,
and `*_bgm.sxd` banks. Executables and unrelated movies/levels are not installed.
The limits remain 12,000 selected assets, 64 MiB per asset, 2 GiB selected data,
and 512-byte paths. Total input limit is 8 GiB. Extraction works directly through
cached random-access readers; no 14 GiB intermediate filesystem is written.

## Dependencies and provenance

Mbed TLS 3.6.4 (`c765c831e5c2a0971410692f92f7a81d6ec65ec2`) supplies portable
SHA-256, HMAC, RSA arithmetic and AES; CMake fetches that pinned commit and its
pinned framework submodule. Reprise selects its Apache-2.0 licensing option.
The existing zlib dependency supplies deflate decoding. Neither dependency is
a game engine or an emulator.

Only the public numeric fake-package RSA parameters were taken from
[LibOrbisPkg](https://github.com/maxton/LibOrbisPkg/blob/643477263b2644e0803e0f58b8726ea4e3f3b7d4/LibOrbisPkg/Util/Keys.cs),
a general platform tool, and compared with Beacon's existing parameters.
No LibOrbisPkg implementation or game-specific external research is included.
These are publicly supplied fake-package format parameters, not retail keys or
private user credentials. No local key JSON is shipped or read at runtime.

## Reproduction

```sh
./build.sh
build/games/astro_bot_ps4/beacon_package_check images/CUSA12392.v1.04.pkg \
  /path/to/previously-validated/content /path/to/new/test-library
ctest --test-dir build --output-on-failure
./build-android.sh
build/reprise --import images/CUSA12392.v1.04.pkg
```

The package check imports into a fresh Library, hashes every extracted asset
against the previously validated bundle installation, reopens and verifies the
library, then attempts a full-size damaged package replacement and checks that
the good installation survives. Asset-free checks cover another title and a
recognised but truncated package. Screenshots and audio/model decoding remain
covered by the existing asset viewer checks; identical payload hashes link those
checks to this extraction path.

## Validation (2026-11-03)

The supplied package produced 2,267 files, matching every filename and SHA-256
in the previously validated character/music bundle (661,257,260 payload bytes).
Fresh-library import plus comparison took 33.745 seconds on this macOS host.
A full-size package with the correct header but damaged body was rejected by
the package checksum; the existing verified installation survived.
All 17 CTest checks pass. Logs and reader/binary identities are retained under
`package-validation/`. The source Beacon working tree remains unchanged.

The packaged macOS app imported directly from `images/CUSA12392.v1.04.pkg`
into the user's normal library; its music waveform screenshot was inspected.
macOS packaging and the Android arm64 build pass, including APK alignment and
signature verification. Android document-picker/runtime extraction has not
been exercised on a physical device.
