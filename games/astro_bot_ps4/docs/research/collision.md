# Collision: physics bodies and level collision (2026-10-12)

From the user's data and executable only. Code: `src/data/{PhysicsFile,CollisionMesh}`,
`src/physics/CollisionWorld` (Beacon's own engine service), `src/game/LevelCollision`; tools
`beacon-level collision`, `.analysis/tools/{pfb,pelm}.py`. Decompiles in `evidence/decomp/physics*`.

## .pfb (PhysicsImportPfb, 0xc9cc70) - verified on all 1,378 files (byte-exact walk)

u32 version 1 + 12 zero bytes, then chunks {u32 type, u32 stride, u32 count, u32 0}:

- **1 rigid bodies** (0x280-byte record + shapes): +0 name, +0x100 body name
  (`JsRigidBody`, `JsRigidBody_LargeMesh`, `JsRigidBody_Convex_*`), +0x204 motion type
  (0: 2,904, 1: 947, 2: 425; meaning **open** - names do not separate static from dynamic),
  +0x208 position, +0x214 quaternion, +0x224 scale (the node frame: the model's), +0x230/+0x23c the
  body frame (where the rigid body sits; physics.md "Body frames"),
  +0x24c mass?, +0x254..+0x274 floats (restitution/friction/damping candidates: 0.2, 0.6, 0.99),
  +0x278 shape stride (56), +0x27c shape count.
  Shape: u32 kind, f32 pos[3], quat[4], data at +0x20. Kinds: 0 sphere (radius +0x34), 1 box
  (half extents +0x20; a 1.0x0.1x0.1 beam has 0.5/0.05/0.05 = its definition's `Size`),
  2/3 capsule/cylinder: +0x20..+0x34 hold [a, b, c, a, b, a]; the importer reads +0x2c (a =
  half length) and +0x30 (b = radius), axis local Y. Verified for kind 3 with
  mup_bg_scifi_cylinder_b (Size 1.01 x 0.5 x 1.0 -> a 0.25, b 0.5); kind 2 assumed to use the
  same order. (An earlier swapped reading turned start platforms into walls; fixing it moved every
  floor probe within 0.12 m.)
  4 convex hull and 6 mesh (followed by a vertex block {3, 4, n floats} and an index block
  {4, 4, n}), 7 "Large Mesh" (one block {6, size, 1} holding a serialized mesh). Importer
  messages confirm: "convex has no vertex/index", "Large Mesh has no vertices/indices",
  "Wrong Serialized Large Mesh", "Rigid Shape Type %d is unsupported type" (kind 5).
- **2 joints** (six kinds, two body names at +0x2c/+0x12c, frames): not decoded further yet.
- **5** per-body overrides keyed by FNV-1a-64 of a name (code hashes 4 bytes with the 64-bit
  FNV constants): not decoded further yet.

Totals: 4,276 bodies, 4,385 shapes (412 spheres, 1,232 boxes, 566 kind 2, 418 kind 3,
1,147 hulls, 610 meshes), 470,166 hull/mesh triangles.

## Serialized mesh "PELM" (deserializer 0xdf3cc0) - verified on all 597 blobs

Packed little-endian stream: "PELM", u8 0, u8 7, u8 layout (only 2 used), f32 centre[3],
f32 half[3], u32 islands, 4 x u32 (allocation sizes), u32 total size (checked), f32 0.025, pad
to 0x40; u32 preamble length counted from 0x40 (block skipped; contents **open**); islands:
u32, u8 nVert, u8 nEdge, u8 nFacet, f32 bounds[6], nVert x f32[3], nEdge x 4 bytes,
nFacet x {normal f32[3], f32, u8 vertex[3], u8 edge[3], u32 user}, max(nFacet-1,1) x 9-byte
tree nodes. The first decoded island of a test mesh had V-E+F = 33-93+62 = 2 (closed mesh) and
bounds equal to the header's centre +- half extent. Layouts 0/1/3 (quantized vertices) exist in
the code but not in this game's data.

## Level collision

- `MupLevelCollision` builds `levels/<name>/pfx/platform_design.lcb` (or `platform_art.lcb`).
  49 of 55 files are 16 bytes (no entries). Six hold one baked mesh: header {u32 4, u32 count,
  8 x 0}, entry {10, 16, 1, flags 0x00070000}, {11, size, 1, 0} + PELM, {4, 4, 1, 0} + u32
  material hash ("Stone", "Water": FNV-1a). Code asserts name the serialize types
  (kLargeMesh, kLargeMeshSerialized, kLargeMeshSerializedData, kVertices, kIndices).
- The bulk comes from placements: `HasCollision` true (art_bg 33k, reactive_object, foliage) with
  an object definition naming IPhysics/File. Gameplay classes (enemies, traps, switches) create
  their bodies at run time and are not in the static world.
- Surface flags per placement: HitTvHero / HitVrHero / HitOther (HitVrHero = the robot hero, the
  VR player's; HitTvHero = the other heroes; see physics.md "hit flags"; defaults true in art_bg.lox;
  ~22% of placements switch the robot or VR collision off), IsSlope (179 slide surfaces),
  IPhysics/Material (Wood, Metal, Soil, Sand, Grass, Lava, Water, Fire).
- Check (`beacon-level collision`): all 62 levels build (1-210 ms, up to 757k triangles); a ray
  from the hero start marker finds floor within 0.12 m in all 54 levels with a start, except the
  moving-boat boss, the credits, the waterfall challenge (no floor below) and the gorilla boss
  challenge (16.7 m drop: the start is in the air).
- Viewer: Levels tab, "Collision (C)" shows the world colour-coded (floor/wall/slope/VR-only).

## Open

- Motion type values; joint and type-5 chunks; kind 2 field order (assumed like kind 3).
- Engine-side collision filtering and how the hero queries the world (next step:
  MupHeroActionStates + Hero tuning).
- Objects under moving parents are in the static world at their start pose.
