# Phase 0 probe (2026-10-12)

Input: `CUSA12392.v1.04.pkg` (7,486,636,032 bytes, SHA-256 in `../evidence/hashes.txt`).
Content ID `EP9000-CUSA12392_00-PLATFORMERVR00EU`, param.sfx APP_VER 01.03 (package/patch
version 1.04 per file name and user). Base game with the 1.04 patch merged.

## Package and file system (tools/pkgfs.py)

- Fake-signed PKG: EKPFS recovered from the IMAGE_KEY entry with the published fake keys
  (LibOrbisPkg, a general platform tool). Outer PFS: mode 0xD (signed, encrypted), block 0x10000,
  4 inodes, AES-XTS from sector 16.
- **Key derivation note:** `pfs_flags` = 0xa0000000000003cc has bit 0x2000000000000000 set,
  which LibOrbisPkg treats as "new crypt" (HMAC(EKPFS, seed) first). For this package the plain
  EKPFS is correct (verified: root inode decrypts to a directory, all 30,685 files extract).
  The tool tries both and validates.
- Inner image `uroot/pfs_image.dat` is PFSC (deflate blocks), inner PFS mode 0x8, unencrypted.
- 30,685 files, 13,839,550,382 bytes. Listing with sizes: `../evidence/file_list.txt`.
  Extracted to `.tools/game/CUSA12392/` (+ `sce_sys/param.sfo` from PKG entry 0x1000), the
  folder shape shadPS4 and AstroQuest expect.

## Executable (tools/self2elf.py)

- `eboot.bin` is a SELF with plaintext segments (fake-signed); converted to `.tools/elf/eboot.elf`.
- ELF type 0xFE10 (SCE dynamic executable), entry 0xBA9980, 11 program headers:
  code LOAD 0x0-0x165E1E8 (r-x, ~23 MB incl. read-only data), RELRO 0x1660000, data LOAD
  0x1714000 (file 0x17BBC68, mem 0x1A2A078), TLS, **GNU_EH_FRAME present** (0x15BD134,
  0xA10B4 bytes: function boundaries for the whole program), SCE_DYNLIBDATA (imports by NID).
- No C++ symbols or mangled type names found by a string scan (`_Z` count 0). RTTI still to be
  checked in Ghidra (vtables).
- **~390 source paths survive in assert/log strings** (`../evidence/source_paths.txt`):
  root `D:\asobi2.0\source\app\` with `Common/`, `Game/` (engine-level game framework),
  `Module/` (platform services) and `PlayRoom/Game/Room/MultiPlatformer/Mup*.cpp` (gameplay,
  ~190 files: every enemy, boss, gadget, level manager, VR menu, save data).
- Engine lineage (hypothesis, strong): the game is a "room" inside the studio's Playroom
  framework; the binary still holds `PlayroomVR_%04d_%02d_%02d-...tvf` and `%ASOBI_ROOT%` tool
  paths (`JsVisualTools\batch\atgi2jxm.bat` converts art to `.jxm`).

## Middleware

| Area | Finding | Consequence |
|---|---|---|
| Physics | In-house `Module/Physics` (PhysicsWorldImpl, RigidBody, ParticleWorld, `PhysicsImportPfb`), bodies named `JsRigidBody_Convex_*`, plus `Game/Physics/ClothSimulation`, `MupRootDynamics` | No Havok/PhysX/Bullet. Recoverable as game code. `.pfb` files hold physics bodies. |
| Animation | Sony Edge Animation (`external/EdgeAnim`); `.skel` magic `ES06`, `.anim` magic `EA08` | Must recover Edge's skeleton and compressed-curve format from the binary. |
| Sound | Sony "sndx" engine over NGS2 + Audio3d (`sndx_engine_ngs2_orbis.c`); `.sxd` banks (81 MB SFX library) | Recover the bank format; the playback engine is replaced natively. |
| Rendering | Own Gnm wrapper `Module/Xgnm`, `Module/Gfx` (scene, materials, IBL), `JsParticle`/`JfParticle` | Rewrite natively; shaders must be recovered from the compiled shader binaries. |
| Other | wws_sdk malloc, TBB, sce_geometry, telemetry (otg) | Not needed. |

## Data formats (first look)

| Ext | Count | Total | Notes |
|---|---|---|---|
| .gnf | 7197 | 11.1 GB | Sony GNF textures (platform format, `GNF ` magic); many with `__low` variants |
| .jxm | 7418 | 1.36 GB | Models, magic `JXMD` |
| .sxd | 72 | 551 MB | Sound banks |
| .anim / .skel | 11798 / 1465 | 481 MB | Edge Animation |
| .lvx | 64 | 110 MB | Levels: `levels/<name>/lvx/design.lvx` (+ `art.lvx`), ~60 levels incl. challenges and bosses |
| .pfb | 1378 | 25 MB | Physics bodies |
| .jpx / .lox / .spx / .efb / .lcb / .vib | | small | to identify |
| .xml / .odx / .rbx / .apx | | small | **Plain XML**: tuning parameters, object definitions (`ObjectDefinition`), resource binders |

Many gameplay parameters are XML (e.g. `data/dualshock4/dualshock4.xml`), which fits the
code-versus-data rule and makes modding straightforward. Level lists: `mup_levels.xml`,
`mup_product_levels.xml`.

## Next steps

1. Ghidra project from `eboot.elf` (GhidraOrbis or plain ELF + NID resolution), function starts
   from EH frame, label functions by their assert source paths.
2. GNF texture decoder (incl. GPU tiling) + JXMD model parser -> model viewer.
3. Edge skeleton/animation decoding -> animation playback.
4. LVX level format -> level viewer.
